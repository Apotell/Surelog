#!/usr/bin/env python3

import os
import platform
import re
import shutil
import subprocess
import tarfile
import time
import traceback
from enum import Enum, unique
from pathlib import Path
from threading import Lock


def is_ci_build() -> bool:
  return 'GITHUB_JOB' in os.environ


_log_mutex = Lock()
def log(text, end='\n'):
  _log_mutex.acquire()
  try:
    print(text, end=end, flush=True)
  finally:
    _log_mutex.release()


@unique
class Status(Enum):
  PASS = 0
  DIFF = -1
  FAIL = -2
  FAILDUMP = -3
  SEGFLT = -4
  NOGOLD = -5
  TOOLFAIL = -6
  EXECERR = -7

  def __str__(self):
    return str(self.name)


def is_windows() -> bool:
  return platform.system() == 'Windows'


def is_linux() -> bool:
  return platform.system() == 'Linux'


def is_macosx() -> bool:
  return platform.system() == 'Darwin'


def get_platform_id() -> str:
  system = platform.system()
  if system == 'Linux':
    return '.linux'
  elif system == 'Darwin':
    return '.osx'
  elif system == 'Windows':
    return '.win'

  return ''


def build_filters(filters: list[str]) -> list[str]:
  if not filters:
    return []

  processed = set()
  for filter in filters:
    if filter.startswith('@'):
      with Path(filter[1:]).open() as strm:
        for line in strm:
          line = line.strip()
          if line and not line.startswith('#'):
            processed.add(line)
    else:
      processed.add(filter)
  return [text if text.isalnum() else re.compile(text, re.IGNORECASE) for text in processed]


def find_files(dirpath: Path, pattern: list[str]) -> list[Path]:
  relpaths = []
  for filepath in Path(dirpath).rglob(pattern):
    relpaths.append(filepath.relative_to(dirpath))
  return sorted(relpaths)


def mkdir(dirpath: Path, retries=10) -> bool:
  count = 0
  while count < retries:
    os.makedirs(dirpath, exist_ok=True)

    if dirpath.is_dir():
      return True

    count += 1
    time.sleep(0.1)

  return dirpath.is_dir()


def rmdir(dirpath: Path, retries=10) -> bool:
  count = 0
  while count < retries:
    shutil.rmtree(dirpath, ignore_errors=True)

    if not dirpath.is_dir():
      return True

    count += 1
    time.sleep(0.1)

  shutil.rmtree(dirpath)
  return not dirpath.is_dir()


def rmfile(filepath: Path, retries=10) -> bool:
  count = 0
  while count < retries:
    filepath.unlink(missing_ok=True)

    if not filepath.is_file():
      return True

    count += 1
    time.sleep(0.1)

  filepath.unlink()
  return not filepath.is_file()


def rmtree(dirpath: Path, patterns: list[str]):
  for pattern in patterns:
    for path in Path(dirpath).rglob(pattern):
      if path.is_dir():
        rmdir(path)


def normalize_log(content: str, path_mappings: dict[str, str]) -> str:
  content = re.sub(r'\d+\.\d{3}s', 't.ttts', content)
  content = re.sub(r'\d+\.\d{6}s', 't.tttttts', content)
  for path, mapping in path_mappings.items():
    pattern = re.sub(r'(\\|\/)+', r'(\\\\|\/)+', str(path))
    content = re.sub(pattern, str(mapping), content)
  return content


def merge_files(output_filepath: Path, delimiter: str, *args: list[Path|str]):
  # NOTE: The output_filepath could be one of the input file i.e. among args as well.
  # So, collect the content to write and only then dump out to the output file.
  output_content = ''
  for path in args:
    path = Path(path)
    if path.is_file():
      content = path.open().read()
      if output_content:
        output_content += '\n'
        output_content += delimiter * (120 // len(delimiter))
        output_content += '\n\n'
      output_content += content

  with output_filepath.open('w') as strm:
    strm.write(output_content)
    strm.flush()


def snapshot_directory_state(dirpath: Path) -> set[Path]:
  snapshot = set()
  for sub_dirpath, sub_dirnames, filenames in dirpath.walk():
    snapshot.add(sub_dirpath)
    snapshot.update([sub_dirpath / filename for filename in filenames])

  return snapshot


def restore_directory_state(dirpath: Path, golden_snapshot: set[Path], output_dirpath: Path, current_snapshot: set[Path]):
  dirt = set(current_snapshot).difference(set(golden_snapshot))
  # Sort based on the length of the string and then chronologically
  dirt = sorted(dirt, key=lambda item: (len(str(item)), item))

  for path in dirt:
    if path.is_dir() or path.is_file():
      src_rel_path = path.relative_to(dirpath)
      dst_abs_path = output_dirpath / src_rel_path
      try:
        mkdir(dst_abs_path.parent)
        shutil.move(path, dst_abs_path)
      except:
        print(f'Failed to move {path} to {dst_abs_path}')
        traceback.print_exc()


def generate_tarball(dirpath: Path):
  with tarfile.open(str(dirpath) + '.tar.gz', 'w:gz', format=tarfile.GNU_FORMAT) as tarball:
    tarball.add(str(dirpath), arcname=dirpath.name, recursive=True)
