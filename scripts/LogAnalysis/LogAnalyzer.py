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

def sort_cout_map(objid_to_file):
    return dict(sorted(objid_to_file.items(), key=lambda x: x[0]))

def parse_cout_log(cout_file):
    """
    Parses cout.log and returns:
      dict[int, str] -> { InputObjId : filename }
    """
    objid_to_file = {}

    with open(cout_file, "r", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            m = re.match(r"^(\d+):(.*)$", line)
            if not m:
                continue

            obj_id = int(m.group(1))
            path = m.group(2).strip()

            # Only create mapping if path exists
            if path:
                objid_to_file[obj_id] = path

    return objid_to_file

def extract_text_from_file(filename, sl, sc, el, ec):
    try:
        with open(filename, "r", errors="ignore") as f:
            lines = f.readlines()

        # Convert to 0-based
        sl -= 1
        el -= 1
        sc -= 1
        ec -= 1

        if sl < 0 or el >= len(lines) or sl > el:
            return "NOTEXT"

        if sl == el:
            return lines[sl][sc:ec+1].strip()

        parts = []
        parts.append(lines[sl][sc:].rstrip())

        for i in range(sl + 1, el):
            parts.append(lines[i].rstrip())

        parts.append(lines[el][:ec+1].rstrip())

        return " ".join(parts).strip()

    except Exception:
        return "NOTEXT"

def enrich_with_text(rows, objid_to_file):
    enriched = []

    for r in rows:
        input_id = r[1]
        sl, sc, el, ec = r[2], r[3], r[4], r[5]

        if ec is not None:
            ec = int(ec)
            ec -= 1  # adjust end column

        text = "NOTEXT"

        input_id_int = int(input_id) if input_id is not None else None

        if (
            input_id is not None
            and input_id_int in objid_to_file
            and sl is not None
            and sc is not None
            and el is not None
            and ec is not None
        ):
            filename = objid_to_file[input_id_int]
            text = extract_text_from_file(
                filename,
                int(sl), int(sc),
                int(el), int(ec)
            )

        enriched.append(r + (text,))

    return enriched

def parse_block(block_lines):
    input_type = input_id = None
    sl = sc = el = ec = None
    output_type = output_id = None
    invalid_value = None

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

        # Output object
        if RESULT_RE.search(line):
            if i + 1 < len(block_lines):
                m = TYPE_ID_RE.search(block_lines[i + 1])
                if m:
                    output_type, output_id = m.groups()

        # invalidValue (keep updating → last one wins)
        m = INVALID_RE.search(line)
        if m:
            invalid_value = int(m.group(1))

        i += 1

    status = "UNKNOWN"
    if invalid_value == 0:
        status = "REDUCED"
    elif invalid_value == 1:
        status = "FAILURE"

    return input_type, input_id, sl, sc, el, ec, output_type, output_id, status

def sort_and_dedup(rows):
    # Sort by InputObjId (None-safe)
    rows = sorted(
        rows,
        key=lambda r: int(r[1]) if r[1] is not None else -1
    )

    seen_ids = set()
    unique_rows = []

    for r in rows:
        input_id = r[1]
        if input_id not in seen_ids:
            seen_ids.add(input_id)
            unique_rows.append(r)

    return unique_rows

def dump_rows(rows, title="ROWS"):
    print(f"\n==== {title} ====")
    for r in rows:
        itype, iid, sl, sc, el, ec, otype, oid, status, text = r

        start = f"({sl},{sc})" if sl else "-"
        end   = f"({el},{ec})" if el else "-"

        print(
            f"{(itype or '-'):15} {(iid or '-'):12} "
            f"{start:12} {end:12} "
            f"{(otype or '-'):15} {(oid or '-'):13} "
            f"{status:8} {text}"
        )
    print("==== END ====\n")


def sort_and_dedup_stage3(rows):
    rows.sort( key=functools.cmp_to_key(sort_2))
    # dump_rows(rows, "After sort_2")
    # final_rows = []
    # seen_semantic = set()

    # for r in rows:
    #     itype, iid, sl, sc, el, ec, otype, oid, status, text = r

    #     # Rule 1: keep all constants
    #     if itype == "constant":
    #         final_rows.append(r)
    #         continue

    #     # Rule 2: input id == output id → keep
    #     if iid is not None and oid is not None and int(iid) == int(oid):
    #         final_rows.append(r)
    #         continue

    #     # Rule 3: inputType != outputType → semantic dedup
    #     if itype != otype:
    #         key = (itype, otype, status, text)

    #         if key not in seen_semantic:
    #             seen_semantic.add(key)
    #             final_rows.append(r)
    #         # else: drop duplicate
    #         continue

    #     # Default: keep (safe fallback)
    #     final_rows.append(r)

    return rows

def sort_and_dedup_stage4(rows):
    final_rows = []

    for r in rows:
        itype, iid, sl, sc, el, ec, otype, oid, status, text = r

        # Rule 4.1: drop constant → constant
        if itype == "constant" and otype == "constant":
            continue

        # Rule 4.2: same input/output id → mark INVALID
        if iid is not None and oid is not None and int(iid) == int(oid):
            status = "INVALID"

        final_rows.append(
            (itype, iid, sl, sc, el, ec, otype, oid, status, text)
        )

    return final_rows

def sort_and_dedup_stage2(rows):
    # Sort for deterministic output
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

        # 🔹 If location is missing, keep ALL of them
        if sl is None or sc is None or el is None or ec is None:
            final_rows.append(r)
            continue

        # 🔹 Location-aware dedup key
        key = (
            r[0],        # InputObjType
            sl, sc,      # start
            el, ec       # end
        )

        # Keep the row with the lowest InputObjId
        if key not in unique or int(r[1]) < int(unique[key][1]):
            unique[key] = r

    # Combine location-based uniques with location-less rows
    final_rows.extend(unique.values())

    return final_rows

def sort_2(lhs,rhs):
    itype0, iid0, sl0, sc0, el0, ec0, otype0, oid0, status0,text0 = lhs
    itype1, iid1, sl1, sc1, el1, ec1, otype1, oid1, status1,text1 = rhs

    if itype0 != itype1:
        return -1 if itype0 < itype1 else 1
    if otype0 != otype1:
        return -1 if otype0 < otype1 else 1
    if status0 != status1:
        return -1 if status0 < status1 else 1
    if text0 != text1:
        return -1 if text0 < text1 else 1
    if iid0 != iid1:
        return -1 if int(iid0) < int(iid1) else 1
    if oid0 != oid1:
        return -1 if int(oid0) < int(oid1) else 1
    return 0

def main(log_file, cout_file):
    cout_map = parse_cout_log(cout_file)
    cout_map = sort_cout_map(cout_map)
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

    # Print table
    header = (
        f"{'InputObjType':<15} {'InputObjId':<12} "
        f"{'Start':<12} {'End':<12} "
        f"{'OutputObjType':<15} {'OutputObjId':<13} Status"
    )

    print(header)
    print("-" * len(header))

    rows = sort_and_dedup(rows)
    rows = sort_and_dedup_stage2(rows)

    # for r in rows:
    #     start = f"({r[2]},{r[3]})" if r[2] else "-"
    #     end = f"({r[4]},{r[5]})" if r[4] else "-"

    #     print(
    #         f"{(r[0] or '-'):15} {(r[1] or '-'):12} "
    #         f"{start:12} {end:12} "
    #         f"{(r[6] or '-'):15} {(r[7] or '-'):13} {r[8]}"
    #     )

    rows = enrich_with_text(rows, cout_map)
    rows = sort_and_dedup_stage3(rows)
    rows = sort_and_dedup_stage4(rows)
    for r in rows:
        start = f"({r[2]},{r[3]})" if r[2] else "-"
        end = f"({r[4]},{r[5]})" if r[4] else "-"

        print(
            f"{(r[0] or '-'):15} {(r[1] or '-'):12} "
            f"{start:12} {end:12} "
            f"{(r[6] or '-'):15} {(r[7] or '-'):13} "
            f"{r[8]:8} {r[9]}"
        )

    output_file = Path(log_file).parent / "parsed_output.txt"

    with open(output_file, "w", encoding="utf-8") as out:
        out.write(header + "\n")
        out.write("-" * len(header) + "\n")

        for r in rows:
            start = f"({r[2]},{r[3]})" if r[2] else "-"
            end = f"({r[4]},{r[5]})" if r[4] else "-"

            line = (
                f"{(r[0] or '-'):15} {(r[1] or '-'):12} "
                f"{start:12} {end:12} "
                f"{(r[6] or '-'):15} {(r[7] or '-'):13} "
                f"{r[8]:8} {r[9]}"
            )

            print(line)          # keep console output
            out.write(line + "\n")

    print("\nSaved parsed output to:", output_file)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python parse_expreval_log.py <expreval.log> <cout.log>")
        sys.exit(1)

    main(sys.argv[1], sys.argv[2])
