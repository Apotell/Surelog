import subprocess
import sys
from pathlib import Path

_this_filepath = Path(__file__).resolve()
_this_dirpath = _this_filepath.parent

if len(sys.argv) != 3:
    print("Usage: python extract_all.py <zip_folder> <output_folder>")
    sys.exit(1)

zip_dir = Path(sys.argv[1]).resolve()
out_dir = Path(sys.argv[2]).resolve()

script = (_this_dirpath / "extract.py").resolve()
zips = sorted(zip_dir.glob("*.zip"))


if not zips:
    print("No zip files found.")
    sys.exit(1)

for z in zips:
    print("\n=== Extracting: ", z.name, " ===")

    cmd = [
        sys.executable,
        str(script),
        "db", "log", "tsv",
        "--zip-filepath", str(z),
        "--output-dirpath", str(out_dir)
    ]

    subprocess.run(cmd)

print("Done extracting all archives.")
