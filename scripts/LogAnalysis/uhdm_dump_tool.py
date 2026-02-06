import argparse
import subprocess
import pathlib
import sys
import re
import functools
from pathlib import Path


CONSOLIDATED_NAME = "consolidated_output.txt"
PARSED_OUTPUT_NAME = "parsed_output.txt"
def append_consolidated(consolidated_path, folder, width, parsed_file):
    if not parsed_file.exists():
        return

    pad = folder.ljust(width)

    with open(parsed_file, "r", errors="ignore") as src, \
         open(consolidated_path, "a", encoding="utf-8") as dst:

        for line in src:
            line = line.rstrip("\n")

            # skip header lines
            if (
                not line.strip()
                or "InputObjType" in line
                or set(line.strip()) == {"-"}
            ):
                continue

            dst.write(f"{pad}{line}\n")

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
# UHDM SINGLE RUN (from uhdm_dump.py)
############################################################

def run_single(job_dir, test_root):
    script_dir = pathlib.Path(__file__).parent.resolve()
    binary = script_dir / "uhdm-dump.exe"
    parser = script_dir / "LogAnalyzer.py"

    folder_name = job_dir.name
    log_file = job_dir / f"{folder_name}.log"
    uhdm_file = job_dir / "surelog.uhdm"
    cout_file = job_dir / "cout.log"

    if not log_file.exists() or not uhdm_file.exists():
        return False, "missing files"

    r1 = subprocess.run(
        [str(binary), str(uhdm_file)],
        cwd=job_dir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )


    if r1.returncode != 0:
        return False, "uhdm-dump failed"

    if not cout_file.exists():
        return False, "cout.log missing"

    OLD_PREFIX = "C:\\Surelog\\"
    NEW_PREFIX = str(test_root).rstrip("\\") + "\\"

    lines = cout_file.read_text(errors="ignore").splitlines()
    new_lines = []

    for line in lines:
        if ":" in line:
            idx, path = line.split(":", 1)
            path = path.replace(OLD_PREFIX, NEW_PREFIX)
            new_lines.append(f"{idx}:{path}")
        else:
            new_lines.append(line)

    cout_file.write_text("\n".join(new_lines), encoding="utf-8")

    r2 = subprocess.run(
        ["python", str(parser), str(log_file), str(cout_file)],
        cwd=job_dir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )


    return r2.returncode == 0, "ok"

############################################################
# BATCH MODE (from uhdm_dump_all.py)
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

    tests_root = (workspace / "tests").resolve()
    third_root = (workspace / "third_party" / "tests").resolve()

    root_map = {}

    for name, sl_path in tests_map.items():
        sl_path = sl_path.resolve()

        if tests_root in sl_path.parents:
            root_map[name] = tests_root
        elif third_root in sl_path.parents:
            root_map[name] = third_root
        else:
            root_map[name] = None

    crashed = []
    missing = []
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

        root = root_map.get(name)
        if root is None:
            unmatched.append(name)
            print("  skip: no root")
            continue

        ok, reason = run_single(job_dir, Path(workspace))

        if ok:
            parsed_file = job_dir / PARSED_OUTPUT_NAME
            append_consolidated(consolidated_path, name, width, parsed_file)

        if not ok:
            if reason == "missing files":
                missing.append(name)
            elif reason == "uhdm-dump failed":
                crashed.append(name)
            else:
                parser_fail.append(name)

    print("\n===== SUMMARY =====\n")
    print("CRASHED:", crashed)
    print("MISSING_UHDM:", missing)
    print("UNMATCHED_TESTCASES:", unmatched)
    print("PARSER FAIL:", parser_fail)

############################################################
# CLI ENTRY
############################################################
def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--singlefile",
        nargs=2,
        metavar=("JOB_DIR", "TEST_ROOT"),
        help="Run single test folder"
    )

    parser.add_argument(
        "--multiplefiles",
        nargs=2,
        metavar=("EXTRACTED_ROOT", "WORKSPACE"),
        help="Run batch mode"
    )

    args = parser.parse_args()

    if args.singlefile:
        job_dir, test_root = args.singlefile
        ok, reason = run_single(Path(job_dir), Path(test_root))
        print("Result:", ok, reason)
        return

    if args.multiplefiles:
        extracted_root, workspace = args.multiplefiles
        run_batch(Path(extracted_root), Path(workspace))
        return

    parser.print_help()

if __name__ == "__main__":
    main()
