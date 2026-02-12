import argparse
import subprocess
import pathlib
from pathlib import Path

CONSOLIDATED_NAME = "presitine.txt"
PARSED_OUTPUT_NAME = "parsed_output.txt"

############################################################
# Consolidate parsed output
############################################################

def append_consolidated(consolidated_path, folder, width, parsed_file):
    if not parsed_file.exists():
        return

    with open(parsed_file, "r", errors="ignore") as src, \
         open(consolidated_path, "a", encoding="utf-8") as dst:

        for line in src:
            line = line.rstrip("\n")

            if (
                not line.strip()
                or "InputObjType" in line
                or set(line.strip()) == {"-"}
            ):
                continue

            dst.write(f"{folder} | {line}\n")

############################################################
# Scan tests
############################################################

def scan(dirpaths, filters):
    def is_filtered(name):
        if not filters:
            return True
        for f in filters:
            if isinstance(f, str):
                if f.lower() == name.lower():
                    return True
            elif f.search(name):
                return True
        return False

    tests = {}
    for dirpath in dirpaths:
        for filepath in dirpath.resolve().rglob("*.sl"):
            if is_filtered(filepath.stem):
                tests[filepath.stem] = filepath

    return {
        name: tests[name]
        for name in sorted(tests.keys(), key=lambda t: t.lower())
    }

############################################################
# Run analyzer on single folder
############################################################

def run_single(job_dir):
    script_dir = pathlib.Path(__file__).parent.resolve()
    parser = script_dir / "LogAnalyzer.py"

    tsv_file = job_dir / "compact.tsv"

    if not tsv_file.exists():
        return False, "missing compact.tsv"

    r = subprocess.run(
        ["python", str(parser), str(tsv_file)],
        cwd=job_dir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    return r.returncode == 0, "ok"

############################################################
# Batch mode
############################################################

def run_batch(extracted_root, workspace):

    job_names = [d.name for d in extracted_root.iterdir() if d.is_dir()]
    width = max((len(n) for n in job_names), default=0) + 5

    consolidated_path = extracted_root / CONSOLIDATED_NAME
    if consolidated_path.exists():
        consolidated_path.unlink()

    test_dirs = [
        workspace / "tests",
        workspace / "third_party" / "tests"
    ]

    tests_map = scan(test_dirs, [])

    unmatched = []
    parser_fail = []

    print(f"\nDiscovered {len(tests_map)} tests\n")

    for job_dir in sorted(extracted_root.iterdir()):
        if not job_dir.is_dir():
            continue

        name = job_dir.name
        print(f"=== {name} ===")

        if name not in tests_map:
            unmatched.append(name)
            print("  skip: not found")
            continue

        ok, reason = run_single(job_dir)

        if ok:
            parsed_file = job_dir / PARSED_OUTPUT_NAME
            append_consolidated(consolidated_path, name, width, parsed_file)
        else:
            parser_fail.append(name)
            print("  fail:", reason)

    print("\n===== SUMMARY =====\n")
    print("UNMATCHED_TESTCASES:", unmatched)
    print("PARSER FAIL:", parser_fail)

############################################################
# CLI
############################################################

def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--singlefile",
        metavar="JOB_DIR",
        help="Run single folder"
    )

    parser.add_argument(
        "--multiplefiles",
        nargs=2,
        metavar=("EXTRACTED_ROOT", "WORKSPACE"),
        help="Run batch mode"
    )

    args = parser.parse_args()

    if args.singlefile:
        ok, reason = run_single(Path(args.singlefile))
        print("Result:", ok, reason)
        return

    if args.multiplefiles:
        extracted_root, workspace = args.multiplefiles
        run_batch(Path(extracted_root), Path(workspace))
        return

    parser.print_help()

if __name__ == "__main__":
    main()
