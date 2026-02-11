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

def normalize_type(t):
    if t is None:
        return None
    return TYPE_MAP.get(t, t)


def parse_block(block_lines):
    input_type = input_id = None
    sl = sc = el = ec = None
    output_type = output_id = None
    input_op = None

    i = 0
    while i < len(block_lines):
        line = block_lines[i]

        if OBJ_RE.search(line):
            if i + 1 < len(block_lines):
                obj_line = block_lines[i + 1]

                m = TYPE_ID_RE.search(obj_line)
                if m:
                    input_type, input_id = m.group(1), int(m.group(2))

                lm = LOC_RE.search(obj_line)
                if lm:
                    sl, sc, el, ec = lm.groups()

        # capture vpiOpType anywhere in block
        om = OPTYPE_RE.search(line)
        if om:
            op_num = int(om.group(1))
            input_op = VPI_OP_MAP.get(op_num)

        if RESULT_RE.search(line):
            if i + 1 < len(block_lines):
                m = TYPE_ID_RE.search(block_lines[i + 1])
                if m:
                    output_type, output_id = m.groups()

        i += 1

    # normalize types
    input_type = normalize_type(input_type)
    output_type = normalize_type(output_type)

    # append op name to input type
    if input_type == "Operation" and input_op:
        input_type = f"{input_type}:{input_op}"

    return input_type, input_id, sl, sc, el, ec, output_type, output_id


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

def main(log_file, cout_file):
    text = Path(log_file).read_text(errors="ignore").splitlines()

    rows = []
    current_block = []
    depth = 0

    for line in text:
        if START_RE.search(line):
            if depth == 0:
                current_block = []
            depth += 1
            continue

        if END_RE.search(line):
            depth -= 1
            if depth == 0:
                row = parse_block(current_block)
                rows.append(row)
            continue

        if depth > 0:
            current_block.append(line)

    header = "InputObjType | InputObjId | sl | sc | el | ec | OutputObjType | OutputObjId"

    print(header)
    print("-" * len(header))

    rows = sort_and_dedup_stage2(rows)
    rows = sort_and_dedup_stage3(rows)
    rows = sort_and_dedup_stage4(rows)

    output_file = Path(log_file).parent / "parsed_output.txt"

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
    if len(sys.argv) != 3:
        print("Usage: python parse_expreval_log.py <expreval.log> <cout.log>")
        sys.exit(1)

    main(sys.argv[1], sys.argv[2])
