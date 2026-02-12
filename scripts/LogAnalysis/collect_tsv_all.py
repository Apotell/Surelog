import argparse
import subprocess
import pathlib
from pathlib import Path

TSV_NAME = "compact.tsv"

def run_single(job_dir):
    script_dir = pathlib.Path(__file__).parent.resolve()
    extractor = script_dir / "collect_tsv.py"

    folder_name = job_dir.name
    log_file = job_dir / f"{folder_name}.log"
    out_file = job_dir / TSV_NAME

    if not log_file.exists():
        return False, "missing log"

    r = subprocess.run(
        ["python", str(extractor), str(log_file), str(out_file)],
        cwd=job_dir,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    return r.returncode == 0, "ok"

def run_batch(extracted_root):

    failed = []

    for job_dir in sorted(extracted_root.iterdir()):
        if not job_dir.is_dir():
            continue

        name = job_dir.name
        print(f"=== {name} ===")

        ok, reason = run_single(job_dir)

        if not ok:
            failed.append(name)
            print("  fail:", reason)

    print("\n===== SUMMARY =====")
    print("FAILED:", failed)

def main():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--single",
        metavar="JOB_DIR",
        help="Run extractor on one job folder"
    )

    parser.add_argument(
        "--batch",
        metavar="EXTRACTED_ROOT",
        help="Run extractor on all job folders"
    )

    args = parser.parse_args()

    if args.single:
        ok, reason = run_single(Path(args.single))
        print("Result:", ok, reason)
        return

    if args.batch:
        run_batch(Path(args.batch))
        return

    parser.print_help()

if __name__ == "__main__":
    main()
