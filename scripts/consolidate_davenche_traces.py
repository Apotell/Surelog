import functools
import multiprocessing
import operator
import sys
from pathlib import Path


START_MARKER = "==================== REDUCTION START ====================="
END_MARKER   = "===================== REDUCTION END ======================"


def _load_log(log_filepath):
  rows = []
  inside = False

  with log_filepath.open("r", errors="ignore") as strm:
    for line in strm:
      line = line.rstrip("\n")

      if START_MARKER in line:
        inside = True
        skip = 2

      elif END_MARKER in line:
        break

      elif inside:
        if skip > 0:
          skip -= 1
        elif not line.startswith('---'):
          parts = line.split()[:-1]
          rows.append(tuple(parts))

  return rows


def _process_one(log_filepath):
  return [(log_filepath.stem, *r) for r in _load_log(log_filepath)]


def _process_batch(dirpath):
  params = sorted(dirpath.rglob("*.log"))
  jobs = min(len(params), multiprocessing.cpu_count())

  print(f"Processing {len(params)} files using {jobs} processes ...")

  if jobs <= 1:
    results = [_process_one(param) for param in params]
  else:
    with multiprocessing.Pool(processes=jobs) as pool:
      results = pool.map(_process_one, params)

  return functools.reduce(operator.iconcat, results, [])


def _save(rows, filepath):
  header = "TestName | InputObjType | InputObjId | sl | sc | el | ec | OutputObjType | OutputObjId"

  with filepath.open("w", encoding="utf-8") as strm:
    strm.write(header)
    strm.write("\n")
    for r in rows:
      strm.write(" | ".join("" if v is None else str(v) for v in r))
      strm.write("\n")
    strm.flush()

  print(f"Generated '{filepath.name}' with {len(rows)} entries.")
  return 0


def _main():
  input_path = Path(sys.argv[1])
  output_path = Path(sys.argv[2])

  rows = _process_batch(input_path) if input_path.is_dir() else _process_one(input_path)
  return _save(rows, output_path)


if __name__ == "__main__":
  if len(sys.argv) != 3:
    print("Usage: python consolidate_davenche_traces.py <file-path|dir-path> <output-file-path>")
    sys.exit(1)

  sys.exit(_main())
