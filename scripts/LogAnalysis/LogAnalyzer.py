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

def parse_block(block_lines):
    input_type = input_id = None
    sl = sc = el = ec = None
    output_type = output_id = None

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

        if RESULT_RE.search(line):
            if i + 1 < len(block_lines):
                m = TYPE_ID_RE.search(block_lines[i + 1])
                if m:
                    output_type, output_id = m.groups()

        i += 1

    return input_type, input_id, sl, sc, el, ec, output_type, output_id

def sort_and_dedup_stage3(rows):
    rows.sort(key=functools.cmp_to_key(sort_2))
    return rows

def sort_and_dedup_stage4(rows):
    final_rows = []

    for r in rows:
        itype, iid, sl, sc, el, ec, otype, oid = r

        if itype == "constant" and otype == "constant":
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

    header = (
        f"{'InputObjType':<25} {'InputObjId':<8}  "
        f"{'Start':<15} {'End':<15}  "
        f"{'OutputObjType':<25} {'OutputObjId':<8}"
    )

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
            start = f"({r[2]},{r[3]})" if r[2] else "-"
            end = f"({r[4]},{r[5]})" if r[4] else "-"

            line = (
                f"{(r[0] or '-'):25} {(r[1] or '-'):8}  "
                f"{start:15} {end:15} "
                f"{(r[6] or '-'):25} {(r[7] or '-'):8}"
            )

            print(line)
            out.write(line + "\n")

    print("\nSaved parsed output to:", output_file)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python parse_expreval_log.py <expreval.log> <cout.log>")
        sys.exit(1)

    main(sys.argv[1], sys.argv[2])
