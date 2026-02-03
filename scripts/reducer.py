#!/usr/bin/env python3

import argparse
import multiprocessing
import pprint
import psutil
import re
import subprocess
import sys
import tabulate
import time
import traceback
import zipfile
from contextlib import redirect_stdout, redirect_stderr
from datetime import datetime, timedelta
from pathlib import Path
from pathlibutil.json import (
  load as json_load,
  dump as json_dump,
  loads as json_loads,
  dumps as json_dumps,
)

from utils import (
  build_filters,
  find_files,
  generate_tarball,
  is_ci_build,
  is_windows,
  log,
  merge_files,
  normalize_log,
  rmdir,
  rmfile,
  Status,
)

_this_filepath = Path(__file__).resolve()
_workspace_dirpath = _this_filepath.parent.parent

# Except for the workspace dirpath all paths are expected to be relative
# either to the workspace directory or the build directory
if is_ci_build() or not is_windows():
  _default_build_dirpath = _workspace_dirpath / 'build'
else:
  _default_build_dirpath = _workspace_dirpath / 'out/build/x64-Debug/third_party/UHDM'
  # _default_build_dirpath = _workspace_dirpath / 'out/build/x64-Release/third_party/UHDM'
  # _default_build_dirpath = _workspace_dirpath / 'out/build/x64-Clang-Debug/third_party/UHDM'
  # _default_build_dirpath = _workspace_dirpath / 'out/build/x64-Clang-Release/third_party/UHDM'

_default_reducer_filename = Path('uhdm-reduce.exe') if is_windows() else Path('uhdm-reduce')
_default_reducer_filepath = Path('bin') / _default_reducer_filename


def _scan(dirpath, filters):
  def _is_filtered(name):
    if not filters:
      return True
    for filter in filters:
      if isinstance(filter, str):
        if filter.lower() == name.lower():
          return True
      else:
        if filter.search(name):  # Note: match() reports success only if the match is at index 0
          return True
    return False

  all_tests = {}
  filtered_tests = set()
  for test_dirpath in dirpath.iterdir():
    name = test_dirpath.stem
    all_tests[name] = test_dirpath

    if _is_filtered(name):
      filtered_tests.add(name)

  return [
    { name: all_tests[name] for name in sorted(all_tests.keys(), key=lambda t: t.lower()) },
    { name: all_tests[name] for name in sorted(filtered_tests, key=lambda t: t.lower()) },
  ]


def _get_log_statistics(filepath: Path) -> dict[str, int]:
  statistics = {
    'BEFORE': 0,
    'AFTER': 0,
    'ADDED': 0,
    'REMOVED': 0,
  }
  if not filepath.is_file():
    return statistics

  start_marker = '= BEGIN REDUCTION RESULT ='
  end_marker = '= END REDUCTION RESULT ='
  started = False

  with open(filepath, 'rt') as strm:
    for line in strm:
      line = line.strip()

      if not started and start_marker in line:
        started = True
      elif started and end_marker in line:
        break

      if started:
        parts = line.split()
        if len(parts) == 4:
          statistics['BEFORE'] = int(parts[0])
          statistics['AFTER'] = int(parts[1])
          statistics['ADDED'] = int(parts[2])
          statistics['REMOVED'] = int(parts[3])
          break

  return statistics


def _run_reducer(name, test_dirpath, uhdm_src_filepath, uhdm_dst_filepath, reducer_log_filepath, reducer_filepath, verbose):
  start_dt = datetime.now()
  print(f'start-time: {start_dt}')

  reducer_timedelta = timedelta(seconds=0)

  args = [reducer_filepath, uhdm_src_filepath, uhdm_dst_filepath]
  if verbose:
    args.append['-v']

  print('Launching reducer with arguments:')
  pprint.pprint(args)
  print('\n')

  status = Status.PASS
  max_cpu_time = 0
  max_vms_memory = 0
  max_rss_memory = 0
  with open(reducer_log_filepath, 'wt') as reducer_log_strm:
    reducer_start_dt = datetime.now()
    try:
      process = subprocess.Popen(
          args,
          stdout=reducer_log_strm,
          stderr=subprocess.STDOUT,
          cwd=test_dirpath)

      while psutil.pid_exists(process.pid) and process.poll() == None:
        cpu_time = 0
        rss_memory = 0
        vms_memory = 0
        try:
          pp = psutil.Process(process.pid)

          descendants = list(pp.children(recursive=True))
          descendants = [pp] + descendants

          for descendant in descendants:
            try:
              cpu_time += descendant.cpu_times().user

              mem_info = descendant.memory_info()
              rss_memory += mem_info.rss
              vms_memory += mem_info.vms
            except (psutil.NoSuchProcess, psutil.AccessDenied):
              # sometimes a subprocess descendant will have terminated between the time
              # we obtain a list of descendants, and the time we actually poll this
              # descendant's memory usage.
              pass

        except (psutil.NoSuchProcess, psutil.AccessDenied):
          pass

        max_cpu_time = max(max_cpu_time, cpu_time)
        max_vms_memory = max(max_vms_memory, vms_memory)
        max_rss_memory = max(max_rss_memory, rss_memory)

        time.sleep(0.25)

      returncode = process.poll()
      reducer_timedelta = datetime.now() - reducer_start_dt
      print(f'Reducer terminated with exit code: {returncode} in {str(reducer_timedelta)}')
    except:
      status = Status.FAIL
      reducer_timedelta = datetime.now() - reducer_start_dt
      print(f'Reducer threw an exception')
      traceback.print_exc()

    reducer_log_strm.flush()

  end_dt = datetime.now()
  delta = end_dt - start_dt
  print(f'end-time: {str(end_dt)} {str(delta)}')

  return {
    'STATUS': status,
    'CPU-TIME': max_cpu_time,
    'VTL-MEM': max_vms_memory,
    'PHY-MEM': max_rss_memory,
    'WALL-TIME': reducer_timedelta
  }


def _run_one(params):
  start_dt = datetime.now()
  name, test_dirpath, reducer_filepath, verbose = params

  log(f'Running {name} ...')

  slpp_all_filepath: Path = test_dirpath / 'slpp_all' / 'surelog.uhdm'
  slpp_unit_filepath: Path = test_dirpath / 'slpp_unit' / 'surelog.uhdm'

  source_regression_log_filepath = test_dirpath / 'regression.log'
  source_test_log_filepath = test_dirpath / f'{name}.log'

  reduction_log_filepath = test_dirpath / 'reduction.log'       # Merge this with source_regression_log_filepath
  reducer_log_filepath = test_dirpath / 'reducer.log'           # Merge this with source_test_log_filepath

  env_filepath = test_dirpath / 'env.json'
  env = json_load(env_filepath.open())

  golden_log_filepath = Path(env['regression']['golden-log-filepath'])

  uhdm_src_filepath = None
  if slpp_all_filepath.is_file():
    uhdm_src_filepath = slpp_all_filepath
  elif slpp_unit_filepath.is_file():
    uhdm_src_filepath = slpp_unit_filepath

  uhdm_dst_filepath = uhdm_src_filepath.parent / 'reduced.uhdm' if uhdm_src_filepath else None

  completed = False
  result = {
    'TESTNAME': name,
    'STATUS': Status.PASS,
    'golden': {},
    'current': {},
  }

  env['reducer'] = {
    'test-dirpath': test_dirpath,
    'reducer-filepath': reducer_filepath,
    'uhdm-src-filepath': uhdm_src_filepath,
    'uhdm-dst-filepath': uhdm_dst_filepath,
    'reduction-log-filepath': reduction_log_filepath,
    'reducer-log-filepath': reducer_log_filepath,
    'golden-log-filepath': golden_log_filepath,
    'source-regression-log-filepath': source_regression_log_filepath,
    'source-test-log-filepath': source_test_log_filepath,
  }
  json_dump(env, env_filepath.open('w'), indent=2)

  with reduction_log_filepath.open('w') as reduction_log_strm, \
          redirect_stdout(reduction_log_strm), \
          redirect_stderr(reduction_log_strm):
    print(f'start-time: {start_dt}')
    print( '')
    print( 'Environment:')
    print(f'           test-name: {name}')
    print(f'        test-dirpath: {test_dirpath}')
    print(f'    reducer-filepath: {reducer_filepath}')
    print(f'   uhdm-src-filepath: {uhdm_src_filepath}')
    print(f'   uhdm-dst-filepath: {uhdm_dst_filepath}')
    print(f'reducer-log-filepath: {reducer_log_filepath}')
    print( '\n')

    try:
      if uhdm_src_filepath and uhdm_dst_filepath:
        print('Running Reducer ...', flush=True)
        result.update(
          _run_reducer(name, test_dirpath, uhdm_src_filepath, uhdm_dst_filepath,
                       reducer_log_filepath, reducer_filepath, verbose)
        )
        print('\n')
        reduction_log_strm.flush()
      else:
        print(f'Failed to find uhdm source database: {uhdm_src_filepath}')

      result.update({
        'golden': _get_log_statistics(golden_log_filepath),
        'current': _get_log_statistics(reducer_log_filepath)
      })

      if result['STATUS'] == Status.PASS:
        current = result['current']
        golden = result['golden']

        if current.get('BEFORE', 0) != golden.get('BEFORE', 0) or \
            current.get('AFTER', 0) != golden.get('AFTER', 0) or \
            current.get('ADDED', 0) != golden.get('ADDED', 0) or \
            current.get('REMOVED', 0) != golden.get('REMOVED', 0):
          result['STATUS'] = Status.DIFF

      print('Merging test logs ...')
      merge_files(source_test_log_filepath, '#**', source_test_log_filepath, reducer_log_filepath)
      rmfile(reducer_log_filepath)

      print(f'Normalizing test log file {source_test_log_filepath}')
      content = source_test_log_filepath.open().read()
      if 'Segmentation fault' in content:
        result['STATUS'] = Status.SEGFLT

      content = normalize_log(content, {
        str(_workspace_dirpath.as_posix()): '${SURELOG_DIR}',
        str(Path(env['regression']['workspace-dirpath']).as_posix()): '${SURELOG_DIR}',
        r'\${SURELOG_DIR}/out/build/': r'\${SURELOG_DIR}/build/',
      })
      source_test_log_filepath.open('w').write(content)

      # If golden file is missing, then fail the test explicitly!
      if result['STATUS'] == Status.PASS and not golden_log_filepath.is_file():
        result['STATUS'] = Status.NOGOLD

      completed = True
    except:
      result['STATUS'] = Status.EXECERR
      traceback.print_exc()

    pprint.pprint({'result': result})
    print('\n')

    end_dt = datetime.now()
    delta = end_dt - start_dt
    print(f'end-time: {str(end_dt)} {str(delta)}')

    reduction_log_strm.flush()

  # Merge regression logs
  merge_files(source_regression_log_filepath, '#**', source_regression_log_filepath, reduction_log_filepath)
  rmfile(reduction_log_filepath)

  if is_ci_build():
    generate_tarball(test_dirpath)
    rmdir(test_dirpath)

  log(f'... {name} Completed.' if completed else f'... {name} FAILED.')
  return result


def _print_report(results):
  columns = [ 'TESTNAME', 'STATUS', 'BEFORE', 'AFTER', 'ADDED', 'REMOVED', 'CPU-TIME', 'VTL-MEM', 'PHY-MEM' ]

  results = sorted(results, key=lambda r: (-r['STATUS'].value, r['TESTNAME']))

  rows = []
  summary = { status.name : 0 for status in Status }
  summary[''] = ''
  for result in results:
    current = result['current']
    golden = result['golden']

    def _get_cell_value(name):
      if golden and current.get(name, 0) != golden.get(name, 0):
        return f'{current.get(name, 0)} ({current.get(name, 0) - golden.get(name, 0):+})'
      else:
        return f'{current.get(name, 0)}'

    summary[result[columns[1]].name] += 1
    rows.append([
      result[columns[0]],                               # TESTNAME
      result[columns[1]].name,                          # STATUS
      _get_cell_value(columns[2]),                      # BEFORE
      _get_cell_value(columns[3]),                      # AFTER
      _get_cell_value(columns[4]),                      # ADDED
      _get_cell_value(columns[5]),                      # REMOVED
      result.get(columns[6], 0),                        # CPU-TIME
      round(result.get(columns[7], 0) / (1024 * 1024)), # VTL-MEM
      round(result.get(columns[8], 0) / (1024 * 1024)), # PHY-MEM
    ])

  print('Results:')
  print(tabulate.tabulate(rows, headers=columns, tablefmt="outline", floatfmt=".2f"))
  print('')

  longest_cpu_test = max(results, key=lambda result: result.get('CPU-TIME', 0))
  total_cpu_time = sum([result.get('CPU-TIME', 0) for result in results])
  summary['MAX CPU TIME'] = f'{round(longest_cpu_test.get("CPU-TIME", 0), 2)} ({longest_cpu_test["TESTNAME"]})'
  summary['TOTAL CPU TIME'] = str(round(total_cpu_time, 2))
  
  longest_wall_test = max(results, key=lambda result: result.get('WALL-TIME', timedelta(seconds=0)))
  summary['MAX WALL TIME'] = f'{round(longest_wall_test.get("WALL-TIME", timedelta(seconds=0)).total_seconds())} ({longest_wall_test["TESTNAME"]})'
  
  largest_test = max(results, key=lambda result: result.get('PHY-MEM', 0))
  summary['MAX MEMORY'] = f'{round(largest_test.get("PHY-MEM", 0) / (1024 * 1024))} ({largest_test["TESTNAME"]})'

  print('Summary:')
  print(tabulate.tabulate(list(summary.items()), tablefmt="outline", floatfmt=".2f"))
  print('')


def _main():
  start_dt = datetime.now()
  print(f'Starting Reducer Regression Tests @ {str(start_dt)}')

  parser = argparse.ArgumentParser()

  parser.add_argument(
      '--source-dirpath', dest='source_dirpath', required=True, type=str,
      help='Source directory path, either absolute or relative to workspace directory, to scan for tests.')
  parser.add_argument(
      '--build-dirpath', dest='build_dirpath', required=False, default=_default_build_dirpath, type=str,
      help='Directory, either absolute or relative to workspace directory, to locate reducer binary')
  parser.add_argument(
      '--reducer-filepath', dest='reducer_filepath', required=False, default=_default_reducer_filepath, type=str,
      help='Location, either absolute or relative to build directory, of reducer executable')
  parser.add_argument(
      '--filters', nargs='+', required=False, default=[], type=str, help='Filter tests matching these regex inputs')
  parser.add_argument(
      '--jobs', nargs='?', required=False, default=multiprocessing.cpu_count(), type=int,
      help='Run tests in parallel, optionally providing max number of concurrent processes. Set 0 to run sequentially.')
  parser.add_argument('--verbose', required=False, default=False, action='store_true', help='Generate verbose logs.')

  args = parser.parse_args()

  if (args.jobs == None) or (args.jobs > multiprocessing.cpu_count()):
    args.jobs = multiprocessing.cpu_count()

  args.build_dirpath = Path(args.build_dirpath)
  args.source_dirpath = Path(args.source_dirpath)
  args.reducer_filepath = Path(args.reducer_filepath)

  if not args.build_dirpath.is_absolute():
    args.build_dirpath = _workspace_dirpath / args.build_dirpath
  args.build_dirpath = args.build_dirpath.resolve()

  if not args.source_dirpath.is_absolute():
    args.source_dirpath = _workspace_dirpath / args.source_dirpath
  args.source_dirpath = args.source_dirpath.resolve()

  if not args.reducer_filepath.is_absolute():
    args.reducer_filepath = args.build_dirpath / args.reducer_filepath
  args.reducer_filepath = args.reducer_filepath.resolve()

  args.filters = build_filters(args.filters)
  all_tests, filtered_tests = _scan(args.source_dirpath, args.filters)

  if args.jobs > len(filtered_tests):
    args.jobs = len(filtered_tests)

  print( 'Environment:')
  print(f'      command-line: {" ".join(sys.argv)}')
  print(f'   current-dirpath: {Path.cwd()}')
  print(f' workspace-dirpath: {_workspace_dirpath}')
  print(f'     build-dirpath: {args.build_dirpath}')
  print(f'  reducer-filepath: {args.reducer_filepath}')
  print(f'    source-dirpath: {args.source_dirpath}')
  print(f'          max-jobs: {args.jobs}')
  print(f'         max-tests: {len(all_tests)}')
  print(f'    filtered-tests: {len(filtered_tests)}')
  print( '\n')

  results = []
  if filtered_tests:
    print(f'Running {len(filtered_tests)} tests ...')

    params = [
      (name, args.source_dirpath / name, args.reducer_filepath, args.verbose) for name in filtered_tests.keys()
    ]

    if args.jobs <= 1:
      results = [_run_one(param) for param in params]
    else:
      with multiprocessing.Pool(processes=args.jobs) as pool:
        results = pool.map(_run_one, params)

  print('')
  _print_report(results)

  end_dt = datetime.now()
  delta = round((end_dt - start_dt).total_seconds())
  print(f'Reducer Regression Test Completed @ {str(end_dt)} in {str(delta)} seconds')

  return sum([entry['STATUS'].value for entry in results])


if __name__ == '__main__':
  sys.exit(_main())
