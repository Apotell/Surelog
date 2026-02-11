import subprocess
import pathlib
import sys

# ---- scan function (your version) ----
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


# ---- args ----
if len(sys.argv) != 3:
    print("Usage: python uhdm_dump_all.py <extracted_root> <surelog_workspace>")
    sys.exit(1)

extracted_root = pathlib.Path(sys.argv[1]).resolve()
workspace = pathlib.Path(sys.argv[2]).resolve()

test_dirs = [
    workspace / "tests",
    workspace / "third_party" / "tests"
]

tests_map = scan(test_dirs, [])

# build root bucket map
root_map = {}

tests_root = (workspace / "tests").resolve()
third_root = (workspace / "third_party" / "tests").resolve()

for name, sl_path in tests_map.items():
    sl_path = sl_path.resolve()

    if tests_root in sl_path.parents:
        root_map[name] = tests_root
    elif third_root in sl_path.parents:
        root_map[name] = third_root
    else:
        root_map[name] = None  # safety fallback

script_dir = pathlib.Path(__file__).parent.resolve()
driver = script_dir / "uhdm_dump.py"

crashed = []
missing_files = []
unmatched = []
parser_fail = []

print(f"\nDiscovered {len(tests_map)} Surelog tests")
print("Starting batch run...\n")

for job_dir in sorted(extracted_root.iterdir()):
    if not job_dir.is_dir():
        continue

    name = job_dir.name

    print(f"=== {name} ===")

    if name not in tests_map:
        unmatched.append(name)
        print("  [SKIP] testcase not found in scan")
        continue

    log_file = job_dir / f"{name}.log"
    uhdm_file = job_dir / "surelog.uhdm"

    if not log_file.exists() or not uhdm_file.exists():
        missing_files.append(name)
        print("  [MISS] missing log or UHDM")
        continue

    try:
        root = root_map.get(name)

        if root is None:
            unmatched.append(name)
            print("  [SKIP] no root match")
            continue
        print(root)
        r = subprocess.run(
            ["python", str(driver), str(job_dir), str(root)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            timeout=60,
            check=False
        )

        if r.returncode != 0:
            parser_fail.append(name)
            print("  [FAIL] parser returned error")

    except subprocess.TimeoutExpired:
        crashed.append(name)
        print("  [CRASH] timeout")
        continue

    except Exception as e:
        crashed.append(name)
        print("  [CRASH]", e)
        continue


# ---- summary ----
print("\n================ SUMMARY ================\n")

def show(title, items):
    print(f"{title}: {len(items)}")
    for x in items:
        print("  ", x)
    print()

show("CRASHED", crashed)
show("MISSING FILES", missing_files)
show("UNAVAILABLE TESTS", unmatched)
show("PARSER FAILURES", parser_fail)

# print("\n===== TEST MAP =====\n")

# for name, path in tests_map.items():
#     print(f"{name} -> {path}")

# print("\n====================\n")

print("Done.")
