import argparse
from pathlib import Path

RED_START = "==================== REDUCTION START ====================="
RED_END   = "===================== REDUCTION END ======================"

OBJ_WIDTH = 32
ID_WIDTH  = 8
SPAN_WIDTH = 15   # start/end width

def extract_table(log_path):
    rows = []
    inside = False

    with open(log_path, "r", errors="ignore") as f:
        for line in f:
            line = line.rstrip("\n")

            if RED_START in line:
                inside = True
                skip = 2  # skip header + dashed line
                continue

            if RED_END in line:
                inside = False
                continue

            if inside:
                if skip > 0:
                    skip -= 1
                    continue

                if line.strip():
                    rows.append(line)

    return rows


def format_row(testcase, row, tc_width):
    parts = row.split()

    # Expect:
    # in_type in_id start end out_type out_id status
    if len(parts) < 6:
        return None

    in_type  = parts[0]
    in_id    = parts[1]
    start    = parts[2]
    end      = parts[3]
    out_type = parts[4]
    out_id   = parts[5]
    # status removed

    return (
        f"{testcase.ljust(tc_width)} "
        f"{in_type.ljust(OBJ_WIDTH)} "
        f"{in_id.ljust(ID_WIDTH)} "
        f"{start.ljust(SPAN_WIDTH)} "
        f"{end.ljust(SPAN_WIDTH)} "
        f"{out_type.ljust(OBJ_WIDTH)} "
        f"{out_id.ljust(ID_WIDTH)}"
    )


def main(root):
    root = Path(root)

    folders = [p for p in root.iterdir() if p.is_dir()]
    if not folders:
        print("No folders found")
        return

    tc_width = max(len(p.name) for p in folders)

    output_lines = []

    for folder in folders:
        log_file = folder / f"{folder.name}.log"
        if not log_file.exists():
            continue

        rows = extract_table(log_file)

        for r in rows:
            formatted = format_row(folder.name, r, tc_width)
            if formatted:
                output_lines.append(formatted)

    out_file = root / "apotell.txt"
    with open(out_file, "w") as f:
        for line in output_lines:
            f.write(line + "\n")

    print(f"Written: {out_file}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", help="Root folder containing test folders")
    args = parser.parse_args()
    main(args.folder)
