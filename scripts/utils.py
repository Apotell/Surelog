#!/usr/bin/env python3

import os
import platform
import re
import shutil
import subprocess
import sys
import tarfile
import time
import traceback
from collections import OrderedDict
from enum import Enum, unique
from pathlib import Path
from threading import Lock


def is_ci_build():
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


def is_windows():
  return platform.system() == 'Windows'


def is_linux():
  return platform.system() == 'Linux'


def is_macosx():
  return platform.system() == 'Darwin'


def get_platform_id():
  system = platform.system()
  if system == 'Linux':
    return '.linux'
  elif system == 'Darwin':
    return '.osx'
  elif system == 'Windows':
    return '.msys' if 'MSYSTEM' in os.environ else '.win'

  return ''


def transform_path(path):
  if 'MSYSTEM' not in os.environ:
    return path

  path = path.replace('/', '\\').replace('\\\\', '\\').replace('\\', '\\\\')
  result = subprocess.run(['cygpath', '-u', path], capture_output=True, text=True)
  result.check_returncode()
  return result.stdout.strip()


def find_files(dirpath, pattern):
  relpaths = []
  for filepath in Path(dirpath).rglob(pattern):
    relpaths.append(os.path.relpath(filepath, dirpath))

  if 'MSYSTEM' in os.environ:
    relpaths = [relpath.replace('\\', '/') for relpath in relpaths]

  return sorted(relpaths)


def mkdir(dirpath, retries=10):
  count = 0
  while count < retries:
    os.makedirs(dirpath, exist_ok=True)

    if os.path.exists(dirpath):
      return True

    count += 1
    time.sleep(0.1)

  return os.path.exists(dirpath)


def rmdir(dirpath, retries=10):
  count = 0
  while count < retries:
    shutil.rmtree(dirpath, ignore_errors=True)

    if not os.path.exists(dirpath):
      return True

    count += 1
    time.sleep(0.1)

  shutil.rmtree(dirpath)
  return not os.path.exists(dirpath)


def rmtree(dirpath, patterns):
  for pattern in patterns:
    for path in Path(dirpath).rglob(pattern):
      if os.path.isdir(path):
        rmdir(path)


def normalize_log(content, path_mappings):
  content = re.sub(r'\d+\.\d{3}s', 't.ttts', content)
  content = re.sub(r'\d+\.\d{6}s', 't.tttttts', content)
  for path, mapping in path_mappings.items():
    pattern = re.sub(r'(\\|\/)+', r'(\\\\|\/)+', path)
    content = re.sub(pattern, mapping, content)
  return content


def snapshot_directory_state(dirpath):
  snapshot = set()
  for sub_dirpath, sub_dirnames, filenames in os.walk(dirpath):
    snapshot.add(sub_dirpath)
    snapshot.update([os.path.join(sub_dirpath, filename) for filename in filenames])

  return snapshot


def restore_directory_state(dirpath, golden_snapshot, output_dirpath, current_snapshot):
  dirt = set(current_snapshot).difference(set(golden_snapshot))
  # Sort based on the length of the string and then chronologically
  dirt = sorted(dirt, key=lambda item: (len(item), item))

  for path in dirt:
    if os.path.isdir(path) or os.path.isfile(path):
      src_rel_path = os.path.relpath(path, dirpath)
      dst_abs_path = os.path.join(output_dirpath, src_rel_path)
      try:
        mkdir(os.path.dirname(dst_abs_path))
        shutil.move(path, dst_abs_path)
      except:
        print(f'Failed to move {path} to {dst_abs_path}')
        traceback.print_exc()


def generate_tarball(dirpath):
  with tarfile.open(dirpath + '.tar.gz', 'w:gz', format=tarfile.GNU_FORMAT) as tarball:
    tarball.add(dirpath, arcname=os.path.basename(dirpath), recursive=True)
