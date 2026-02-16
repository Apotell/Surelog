import functools
import multiprocessing
import operator
import sys
from pathlib import Path


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
  "bit_select": "BitSelect",
  "class_var": "Variable",
  "constant":"Constant",
  "enum_const": "EnumConst",
  "func_call": "FuncCall",
  "hier_path": "HierPath",
  "indexed_part_select": "IndexedPartSelect",
  "int_var": "Variable",
  "integer_var": "Variable",
  "io_decl": "IODecl",
  "logic_net": "Net",
  "logic_var": "Variable",
  "operation": "Operation",
  "part_select": "PartSelect",
  "ref_obj": "RefObj",
  "ref_var": "Variable",
  "sys_func_call": "SysFuncCall",
  "var_select": "VarSelect",
}

DETAIL_TYPES = set({
  "EnumConst",
  "FuncCall",
  "IODecl",
  "Net",
  "SysFuncCall",
  "Variable",
})


def _load_tsv(tsv_filepath):
  rows = []
  with tsv_filepath.open(errors="ignore") as strm:
    for line in strm:
      parts = line.rstrip("\n").split("\t")
      _, icall, itype, ictx, iid, isl, isc, iel, iec, otype, oid = parts

      if icall != "ExprEval::reduceExpr":
        continue  # Ignore others for now

      # normalize IDs
      iid = int(iid) if iid else 0
      oid = int(oid) if oid else 0

      # normalize location
      isl = int(isl) if isl else 0
      isc = int(isc) if isc else 0
      iel = int(iel) if iel else 0
      iec = int(iec) if iec else 0

      # normalize types
      itype = TYPE_MAP.get(itype, itype) if itype else None
      otype = TYPE_MAP.get(otype, otype) if otype else None

      # append operation name
      if ictx:
        if itype == "Operation":
          ictx = VPI_OP_MAP.get(int(ictx))

        if ictx:
          itype = f"{itype}:{ictx}"

      rows.append((itype, iid, isl, isc, iel, iec, otype, oid))

  return rows


def _sort_and_dedup_comparer(lhs, rhs):
  itype0, iid0, _, _, _, _, otype0, oid0 = lhs
  itype1, iid1, _, _, _, _, otype1, oid1 = rhs

  if not itype0:
    print("lhs: ", lhs)

  if itype0 != itype1:
    return -1 if itype0 < itype1 else 1
  if otype0 != otype1:
    return -1 if otype0 < otype1 else 1
  if iid0 != iid1:
    return -1 if iid0 < iid1 else 1
  if oid0 != oid1:
    return -1 if oid0 < oid1 else 1
  return 0


def _sort_and_dedup(rows):
  unique_rows = []
  unique_keys = set()

  for r in rows:
    itype, iid, isl, isc, iel, iec, otype, oid = r

    if itype == "Constant" and otype == "Constant" and iid == oid:
      # Ignore if both are same Constant object
      continue

    key = (itype, isl, isc, iel, iec)
    if key not in unique_keys:
      unique_keys.add(key)
      unique_rows.append(r)

  unique_rows.sort(key=functools.cmp_to_key(_sort_and_dedup_comparer))
  return unique_rows


def _process_one(tsv_filepath):
  rows = _load_tsv(tsv_filepath)
  rows = _sort_and_dedup(rows)
  return [(tsv_filepath.stem, *r) for r in rows]


def _process_batch(dirpath):
  params = sorted(dirpath.rglob("*.tsv"))
  jobs = min(len(params), multiprocessing.cpu_count())

  print(f"Processing {len(params)} files using {jobs} processes ...")

  if jobs <= 1:
    results = [_process_one(param) for param in params]
  else:
    with multiprocessing.Pool(processes=jobs) as pool:
      results = pool.map(_process_one, params)

  return functools.reduce(operator.iconcat, results, [])


def _save(rows, filepath):
  header = "TestName | InputObjType | InputObjId | sl | sc | el | ec | OutputObjType | OutputObjId"

  with filepath.open("w", encoding="utf-8") as strm:
    strm.write(header)
    strm.write("\n")
    for r in rows:
      strm.write(" | ".join("" if v is None else str(v) for v in r))
      strm.write("\n")
    strm.flush()

  print(f"Generated '{filepath.name}' with {len(rows)} entries.")
  return 0


def _main():
  input_path = Path(sys.argv[1])
  output_path = Path(sys.argv[2])

  rows = _process_batch(input_path) if input_path.is_dir() else _process_one(input_path)
  return _save(rows, output_path)


if __name__ == "__main__":
  if len(sys.argv) != 3:
    print("Usage: python consolidate_pristine_traces.py <file-path|dir-path> <output-file-path>")
    sys.exit(1)

  sys.exit(_main())
