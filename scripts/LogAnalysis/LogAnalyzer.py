import re
import sys
from pathlib import Path
import functools

START_RE = re.compile(r"<<<<<<<<<< ExprEval::reduceExpr")
END_RE = re.compile(r"ExprEval::reduceExpr >>>>>>>>>>")

OBJ_RE = re.compile(r">>\s*object: decompile:")
RESULT_RE = re.compile(r">>\s*result: decompile:")
TYPE_ID_RE = re.compile(r"^\s*([^:]+):.*?\bid:(\d+)")
INVALID_RE = re.compile(r">>\s*invalidValue:\s*(\d+)")
LOC_RE = re.compile(r"line:(\d+):(\d+),\s*endln:(\d+):(\d+)")
OPTYPE_RE = re.compile(r"\|vpiOpType:(\d+)")

VPI_OP_MAP = {
    1: "MinusOp",
    2: "PlusOp",
    3: "NotOp",
    4: "BitNegOp",
    5: "UnaryAndOp",
    6: "UnaryNandOp",
    7: "UnaryOrOp",
    8: "UnaryNorOp",
    9: "UnaryXorOp",
    10: "UnaryXNorOp",
    11: "SubOp",
    12: "DivOp",
    13: "ModOp",
    14: "EqOp",
    15: "NeqOp",
    16: "CaseEqOp",
    17: "CaseNeqOp",
    18: "GtOp",
    19: "GeOp",
    20: "LtOp",
    21: "LeOp",
    22: "LShiftOp",
    23: "RShiftOp",
    24: "AddOp",
    25: "MultOp",
    26: "LogAndOp",
    27: "LogOrOp",
    28: "BitAndOp",
    29: "BitOrOp",
    30: "BitXorOp",
    31: "BitXNorOp",
    32: "ConditionOp",
    33: "ConcatOp",
    34: "MultiConcatOp",
    35: "EventOrOp",
    36: "NullOp",
    37: "ListOp",
    38: "MinTypMaxOp",
    39: "PosedgeOp",
    40: "NegedgeOp",
    41: "ArithLShiftOp",
    42: "ArithRShiftOp",
    43: "PowerOp",

    50: "ImplyOp",
    51: "NonOverlapImplyOp",
    52: "OverlapImplyOp",
    53: "UnaryCycleDelayOp",
    54: "CycleDelayOp",
    55: "IntersectOp",
    56: "FirstMatchOp",
    57: "ThroughoutOp",
    58: "WithinOp",
    59: "RepeatOp",
    60: "ConsecutiveRepeatOp",
    61: "GotoRepeatOp",

    62: "PostIncOp",
    63: "PreIncOp",
    64: "PostDecOp",
    65: "PreDecOp",

    66: "MatchOp",
    67: "CastOp",
    68: "IffOp",
    69: "WildEqOp",
    70: "WildNeqOp",

    71: "StreamLROp",
    72: "StreamRLOp",

    73: "MatchedOp",
    74: "TriggeredOp",
    75: "AssignmentPatternOp",
    76: "MultiAssignmentPatternOp",
    77: "IfOp",
    78: "IfElseOp",
    79: "CompAndOp",
    80: "CompOrOp",
    81: "TypeOp",
    82: "AssignmentOp",

    83: "AcceptOnOp",
    84: "RejectOnOp",
    85: "SyncAcceptOnOp",
    86: "SyncRejectOnOp",
    87: "OverlapFollowedByOp",
    88: "NonOverlapFollowedByOp",
    89: "NexttimeOp",
    90: "AlwaysOp",
    91: "EventuallyOp",
    92: "UntilOp",
    93: "UntilWithOp",
    94: "ImpliesOp",
    95: "InsideOp",
}

TYPE_MAP = {
    "operation": "Operation",
    "ref_obj": "RefObj",
    "bit_select": "BitSelect",
    "func_call": "FuncCall",
    "hier_path": "HierPath",
    "int_var": "Variable",
    "integer_var": "Variable",
    "logic_var": "Variable",
    "class_var": "Variable",
    "ref_var": "Variable",
    "io_decl": "IODecl",
    "part_select": "PartSelect",
    "indexed_part_select": "IndexedPartSelect",
    "sys_func_call": "SysFuncCall",
    "logic_net": "Net",
    "enum_const": "EnumConst",
    "var_select": "VarSelect",
    "constant":"Constant"
}

DETAIL_TYPES = {
    "FuncCall",
    "SysFuncCall",
    "Variable",
    "IODecl",
    "EnumConst",
    "Net",
}


def normalize_type(t):
    if t is None:
        return None
    return TYPE_MAP.get(t, t)

def load_tsv(tsv_file):
    rows = []

    with open(tsv_file, "r", errors="ignore") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t")

            # pad safety
            while len(parts) < 9:
                parts.append(None)

            itype, iid, sl, sc, el, ec, otype, oid, optype, idetail = parts

            iid = int(iid) if iid and iid != "-" else None
            oid = int(oid) if oid and oid != "-" else None

            # normalize location
            sl = sl if sl != "-" else None
            sc = sc if sc != "-" else None
            el = el if el != "-" else None
            ec = ec if ec != "-" else None

            # normalize types
            itype = normalize_type(itype)
            otype = normalize_type(otype)

            # append operation name
            if itype == "Operation" and optype and optype != "-":
                op_name = VPI_OP_MAP.get(int(optype))
                if op_name:
                    itype = f"{itype}:{op_name}"

            elif itype in DETAIL_TYPES and idetail and idetail != "-":
                itype = f"{itype}:{idetail}"

            rows.append((itype, iid, sl, sc, el, ec, otype, oid))

    return rows


def sort_and_dedup_stage3(rows):
    rows.sort(key=functools.cmp_to_key(sort_2))
    return rows

def sort_and_dedup_stage4(rows):
    final_rows = []

    for r in rows:
        itype, iid, sl, sc, el, ec, otype, oid = r

        if itype == "Constant" and otype == "Constant":
            continue

        final_rows.append(r)

    return final_rows

def sort_and_dedup_stage2(rows):
    rows = sorted(
        rows,
        key=lambda r: (
            r[0] or "",
            int(r[2]) if r[2] is not None else -1,
            int(r[3]) if r[3] is not None else -1,
            int(r[4]) if r[4] is not None else -1,
            int(r[5]) if r[5] is not None else -1,
            int(r[1]) if r[1] is not None else float("inf"),
        )
    )

    unique = {}
    final_rows = []

    for r in rows:
        sl, sc, el, ec = r[2], r[3], r[4], r[5]

        if sl is None or sc is None or el is None or ec is None:
            final_rows.append(r)
            continue

        key = (r[0], sl, sc, el, ec)

        if key not in unique or int(r[1]) < int(unique[key][1]):
            unique[key] = r

    final_rows.extend(unique.values())
    return final_rows

def sort_2(lhs, rhs):
    itype0, iid0, sl0, sc0, el0, ec0, otype0, oid0 = lhs
    itype1, iid1, sl1, sc1, el1, ec1, otype1, oid1 = rhs

    if itype0 != itype1:
        return -1 if itype0 < itype1 else 1
    if otype0 != otype1:
        return -1 if otype0 < otype1 else 1
    if iid0 != iid1:
        return -1 if int(iid0) < int(iid1) else 1
    if oid0 != oid1:
        return -1 if int(oid0) < int(oid1) else 1
    return 0

def main(tsv_file):
    print("main")
    rows = load_tsv(tsv_file)
    header = "InputObjType | InputObjId | sl | sc | el | ec | OutputObjType | OutputObjId"

    print(header)
    print("-" * len(header))

    rows = sort_and_dedup_stage2(rows)
    rows = sort_and_dedup_stage3(rows)
    rows = sort_and_dedup_stage4(rows)

    output_file = Path(tsv_file).parent / "parsed_output.txt"
    print(output_file)

    with open(output_file, "w", encoding="utf-8") as out:
        out.write(header + "\n")
        out.write("-" * len(header) + "\n")

        for r in rows:
            sl = r[2] or "-"
            sc = r[3] or "-"
            el = r[4] or "-"
            ec = r[5] or "-"

            line = " | ".join([
                str(r[0] or "-"),
                str(r[1] or "-"),
                str(sl),
                str(sc),
                str(el),
                str(ec),
                str(r[6] or "-"),
                str(r[7] or "-"),
            ])

            print(line)
            out.write(line + "\n")

    print("\nSaved parsed output to:", output_file)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python LogAnalyzer.py <compact.tsv>")
        sys.exit(1)

    main(sys.argv[1])
