import argparse
from pathlib import Path

RED_START = "==================== REDUCTION START ====================="
RED_END   = "===================== REDUCTION END ======================"


def extract_table(log_path):
    rows = []
    inside = False

    with open(log_path, "r", errors="ignore") as f:
        for line in f:
            line = line.rstrip("\n")

            if RED_START in line:
                inside = True
                skip = 2
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


def format_row(folder_name, row):
    parts = row.split()

    if len(parts) <= 1:
        return None

    # drop last column
    parts = parts[:-1]

    # prepend folder name
    parts.insert(0, folder_name)

    return " | ".join(parts)


def main(root):
    root = Path(root)

    folders = [p for p in root.iterdir() if p.is_dir()]
    if not folders:
        print("No folders found")
        return

    output_lines = []

    for folder in folders:
        log_file = folder / f"{folder.name}.log"
        if not log_file.exists():
            continue

        rows = extract_table(log_file)

        for r in rows:
            formatted = format_row(folder.name, r)
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
