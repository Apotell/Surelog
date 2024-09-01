#!/usr/bin/env python

"""
Script to scan a directory looking for test logs and collect integration errors.
"""

import argparse
import concurrent.futures
import zipfile
import tarfile
import traceback
from os import error
from pathlib import Path


_platform_ids = ['.linux', '.osx', '.msys', '.win', '']
_zip_file_count = 8
_zip_filename_pattern = 'sl-{buildno}-linux-gcc-release-regression-{index}.zip'


def _merge_categories(dicta, dictb):
  return {
    key: dicta.get(key, 0) + dictb.get(key, 0)
    for key in set(dicta.keys()).union(set(dictb.keys()))
  }


def _scan(zip_filepath):
  archive_name = Path(zip_filepath).stem
  
  errors = []
  categories = {}
  with zipfile.ZipFile(zip_filepath, 'r') as zipfile_strm:
    with zipfile_strm.open(f'{archive_name}.tar.gz') as tarfile_strm:
      with tarfile.open(fileobj=tarfile_strm) as archive_strm:
        for test_archive_path in archive_strm.getnames():
          test_archive_name = Path(Path(test_archive_path).stem).stem
          
          with tarfile.open(fileobj=archive_strm.extractfile(test_archive_path)) as test_archive_strm:
            for platform_id in _platform_ids:
              src_filepath = f'{test_archive_name}/{test_archive_name}{platform_id}.log'

              if src_filepath in test_archive_strm.getnames():
                try:
                  src_strm = test_archive_strm.extractfile(src_filepath)
                  
                  for line in src_strm:
                    line = line.decode().rstrip()
                    if line.startswith("[ERR:IG"):
                      errors.append(f"{test_archive_name}: {line}")

                      category = line[:12]
                      categories[category] = categories.get(category, 0) + 1

                except Exception:
                  print(f"Failed to parse {src_filepath}")
                  traceback.print_last()

  return errors, categories


def _main():
  parser = argparse.ArgumentParser()
  parser.add_argument('input_dirpath', type=str, help='Directory to scan')
  parser.add_argument('build_no', type=str, help='CI build no.')
  parser.add_argument('output_filepath', type=str, help='Path to output file.')
  args = parser.parse_args()
  
  input_dirpath = Path(args.input_dirpath)
  output_filepath = Path(args.output_filepath)
  
  zip_filepaths = [input_dirpath / _zip_filename_pattern.format(buildno=args.build_no, index=i) for i in range(8)]
  max_workers = len(zip_filepaths)
  errors = []
  categories = {}

  if max_workers == 0:
    for filepath in zip_filepaths:
      errs, cats = _scan(filepath)
      errors.extend(errs)
      categories = _merge_categories(categories, cats)
  else:
    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers*2) as executor:
      for errs, cats in executor.map(_scan, zip_filepaths):
        errors.extend(errs)
        categories = _merge_categories(categories, cats)

  with output_filepath.open("w") as strm:
    strm.write('\n'.join(sorted(errors)))

  for category in sorted(categories.keys()):
    print(f"{category}: {categories[category]:>6}")
  print(f"Found {len(errors)} total errors.")

  return 0


if __name__ == '__main__':
  import sys
  sys.exit(_main())
