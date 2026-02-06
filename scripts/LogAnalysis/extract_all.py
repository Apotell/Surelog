import subprocess
import pathlib
import sys

if len(sys.argv) != 3:
    print("Usage: python extract_all.py <zip_folder> <output_folder>")
    sys.exit(1)

zip_dir = pathlib.Path(sys.argv[1]).resolve()
out_dir = pathlib.Path(sys.argv[2]).resolve()

script = pathlib.Path("extract_prestiene.py").resolve()

zips = sorted(zip_dir.glob("*.zip"))

if not zips:
    print("No zip files found.")
    sys.exit(1)

for z in zips:
    print("\n=== Extracting:", z.name, "===")

    cmd = [
        "python",
        str(script),
        "db", "log",
        "--zip-filepath", str(z),
        "--output-dirpath", str(out_dir)
    ]

    subprocess.run(cmd)

print("\nDone extracting all archives.")
