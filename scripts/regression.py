#!/usr/bin/env python3

import argparse
import hashlib
import multiprocessing
import pprint
import psutil
import re
import shutil
import subprocess
import sys
import tabulate
import time
import traceback

from contextlib import redirect_stdout, redirect_stderr
from datetime import datetime, timedelta
from pathlib import Path
from pathlibutil.json import (
  load as json_load,
  dump as json_dump,
  loads as json_loads,
  dumps as json_dumps,
)
from typing import Any

import blacklisted
from utils import (
  build_filters,
  find_files,
  generate_tarball,
  get_platform_id,
  is_ci_build,
  is_windows,
  log,
  mkdir,
  normalize_log,
  restore_directory_state,
  rmdir,
  rmtree,
  snapshot_directory_state,
  Status,
)

_this_filepath = Path(__file__).resolve()
_workspace_dirpath = _this_filepath.parent.parent

# Except for the workspace dirpath all paths are expected to be relative
# either to the workspace directory or the build directory
_default_test_dirpaths = [ _workspace_dirpath / 'tests', _workspace_dirpath / 'third_party' / 'tests' ]
_default_build_dirpath = _workspace_dirpath / 'build'

if is_ci_build():
  _default_build_dirpath = _workspace_dirpath / 'build'
else:
  _default_build_dirpath = _workspace_dirpath / 'out' / 'build' / 'x64-Debug'
  # _default_build_dirpath = _workspace_dirpath / 'out' / 'build' / 'x64-Release'
  # _default_build_dirpath = _workspace_dirpath / 'out' / 'build' / 'x64-Clang-Debug'
  # _default_build_dirpath = _workspace_dirpath / 'out' / 'build' / 'x64-Clang-Release'
  pass

_default_output_dirpath = Path('regression')
_default_surelog_filename = Path('surelog.exe' if is_windows() else 'surelog')
_default_surelog_filepath = Path('bin') / _default_surelog_filename

_re_status_1 = re.compile(r'^\s*\[\s*(?P<status>\w+)\]\s*:\s*(?P<count>\d+)$')
_re_status_2 = re.compile(r'^\s*\|\s*(?P<status>\w+)\s*\|\s*(?P<count1>\d+|\s+)\s*\|\s*(?P<count2>\d+|\s+)\s*\|\s*$')

_blacklisted_dump_uhdm_tests = {
  'AmiqEth',
  'AmiqSimpleTestSuite',
  'BuildUVMPkg',
  'CoresSweRV',
  'CoresSweRVMP',
  'Driver',
  'Earlgrey',
  'Earlgrey_0_1',
  'Earlgrey_Verilator_0_1',
  'Earlgrey_Verilator_01_05_21',
  'Google',
  'Ibex',
  'IbexGoogle',
  'IncompTitan',
  'MiniAmiq',
  'Monitor',
  'Opentitan',
  'Scoreboard',
  'SeqDriver',
  'SimpleClass1',
  'SimpleInterface',
  'SimpleUVM',
  'UnitAmiqEth',
  'UVMNestedSeq',
  'UVMSwitch',
  'Xgate',
  'YosysOpenSparc',
  'YosysSmallBoom',
}


def _get_surelog_log_filepaths(name: str, golden_dirpath: Path, output_dirpath: Path):
  platform_id = get_platform_id()

  golden_log_filepath = golden_dirpath / f'{name}{platform_id}.log'
  if golden_log_filepath.is_file():
    surelog_log_filepath = output_dirpath / f'{name}{platform_id}.log'
  else:
    golden_log_filepath = golden_dirpath / f'{name}.log'
    surelog_log_filepath = output_dirpath / f'{name}.log'

  return golden_log_filepath, surelog_log_filepath


def _scan(dirpaths: list[Path], filters: list[str], shard: int, num_shards: int):
  def _is_filtered(name):
    if int.from_bytes(hashlib.sha256(name.encode()).digest()[:4], 'little') % num_shards != shard:
      return False
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
  blacklisted_tests = set()
  for dirpath in dirpaths:
    for sub_dirpath, sub_dirnames, filenames in dirpath.walk():
      for filename in filenames:
        if filename.endswith('.sl'):
          name = filename[:-3]
          filepath = sub_dirpath / filename

          all_tests[name] = filepath
          if blacklisted.is_blacklisted(name):
            blacklisted_tests.add(name)
          elif _is_filtered(name):
            filtered_tests.add(name)

  return [
    { name : all_tests[name] for name in sorted(all_tests.keys(), key=lambda t: t.lower()) },
    { name : all_tests[name] for name in sorted(filtered_tests, key=lambda t: t.lower()) },
    { name : all_tests[name] for name in sorted(blacklisted_tests, key=lambda t: t.lower()) }
  ]


def _get_log_statistics(filepath: Path) -> dict[str, Any]:
  statistics = {}
  if not filepath.is_file():
    return statistics

  uhdm_dump_markers = [
    '====== UHDM =======',
    '==================='
  ]

  uhdm_stat_dump_markers = [
    '=== UHDM Object Stats Begin (Non-Elaborated Model) ===',
    '=== UHDM Object Stats Begin (Elaborated Model) ===',
    '=== UHDM Object Stats End ==='
  ]

  negatives = {}
  uhdm_dump_started = False
  uhdm_stats = {}
  uhdm_stat_dump_started = False
  uhdm_line_count = 0
  with filepath.open() as strm:
    for line in strm:
      line = line.strip()

      if line in uhdm_dump_markers:
        uhdm_dump_started = not uhdm_dump_started
        continue
      elif line in uhdm_stat_dump_markers:
        uhdm_stat_dump_started = not uhdm_stat_dump_started
        continue

      if uhdm_stat_dump_started:
        parts = [part.strip() for part in line.split()]
        if len(parts) == 2:
          uhdm_stats[parts[0]] = uhdm_stats.get(parts[0], 0) + int(parts[1])
        continue

      m = _re_status_2.match(line)
      if m:
        count1 = m.group('count1').strip()
        count2 = m.group('count2').strip()
        count1 = int(count1) if count1 else 0
        count2 = int(count2) if count2 else 0
        statistics[m.group('status')] = statistics.get(m.group('status'), 0) + count1 + count2
      else:
        m = _re_status_1.match(line)
        if m:
          statistics[m.group('status')] = int(m.group('count'))
        elif uhdm_dump_started and (line.startswith('|') or line.startswith('\\')):
          uhdm_line_count += 1

      if 'ERR:' in line and ('/dev/null' in line or '\\dev\\null' in line):
        # On Windows, this is reported as an error but on Linux it isn't.
        # Don't count it as error on Windows as well so that numbers across platforms can match.
        negatives['ERROR'] = negatives.get('ERROR', 0) + 1

  statistics['NOTE'] = statistics.get('NOTE', 0) + uhdm_line_count
  statistics['STATS'] = uhdm_stats

  for key, value in negatives.items():
    statistics[key] = max(statistics.get(key, 0) - value, 0)

  return statistics


def _get_run_args(
  name: str, filepath: Path, dirpath: Path, binary_filepath: Path,
  uvm_reldirpath: Path, mp: int, mt: int, tool: str, output_dirpath: Path
):
  tool_log_filepath = None
  tool_args_list = []
  if tool == 'valgrind':
    tool_log_filepath = output_dirpath / 'valgrind.log'
    tool_args_list = [
      'valgrind',
      '--tool=memcheck',
      '--leak-check=full',
      '--track-origins=yes',
      '--show-leak-kinds=all',
      '--show-mismatched-frees=yes',
      f'--log-file={tool_log_filepath}'
    ]
  elif tool == 'ddd':
    tool_args_list = ['ddd']

  if tool_args_list:
    print('Tool args list:')
    pprint.pprint(tool_args_list)
    print('\n')

  cmdline = filepath.open().read().strip()
  print(f'Loaded command line: {cmdline}')

  cmdline = cmdline.replace('\r', '')
  cmdline = cmdline.replace('\\', '')
  cmdline = cmdline.replace('\n', ' ')
  cmdline = cmdline.replace('"', '\\"')
  cmdline = cmdline.replace("'", "\\'")
  cmdline = re.sub(r'[.\\\/]+[\\/]UVM', str(uvm_reldirpath).replace('\\', '\\\\'), cmdline)
  cmdline = cmdline.strip()

  if '.sh' in cmdline or '.bat' in cmdline:
    args = ['sh'] + [arg for arg in cmdline.split() if arg] + [str(binary_filepath)]
  else:
    if '*/*.v' in cmdline:
      cmdline = cmdline.replace('*/*.v', ' '.join(str(p) for p in find_files(dirpath, '*.v')))
    if '*/*.sv' in cmdline:
      cmdline = cmdline.replace('*/*.sv', ' '.join(str(p) for p in find_files(dirpath, '*.sv')))
    if '-mt' in cmdline:
      cmdline = re.sub(r'-mt\s+(max|\d+)', '', cmdline)

    if mp and ((mp == 'max') or (mp.isnumeric() and int(mp) > 0)):
      cmdline = re.sub(r'-mp\s+(max|\d+)', '', cmdline)  # Option overridden from command prompt
    if mp or ('-mp' in cmdline):
      cmdline = cmdline.replace('-nocache', '')
    if '-lowmem' in cmdline:
      cmdline = re.sub(r'-mp\s+(max|\d+)', '', cmdline)
      mp = '1'

    parts = cmdline.split(' ')
    for i in range(0, len(parts)):
      if parts[i] and ('*' in parts[i] or '?' in parts[i]):
          if parts[i].endswith('.v') or parts[i].endswith('.sv') or parts[i].endswith('.pkg'):
            parts[i] = ' '.join(str(p) for p in find_files(dirpath, parts[i]))

    parts += ['-mt', (mt or '0')]
    if mp or '-mp' not in cmdline:
      parts += ['-mp', (mp or '0')]
    parts += ['-d', 'uhdmstats'] # Force print uhdm stats
    parts += ['-d', 'cache']
    if name not in _blacklisted_dump_uhdm_tests:
      parts += ['-d', 'uhdm']
    parts += ['-noreduce', '-noelab'] # Disable reduction & elaboration

    rel_output_dirpath = output_dirpath.relative_to(dirpath, walk_up=True)
    parts += ['-o', str(rel_output_dirpath)]

    cmdline = ' '.join(['"' + part + '"' if '"' in part else part for part in parts if part])
    print(f'Processed command line: {cmdline}')

    args = tool_args_list + [str(binary_filepath)] + cmdline.split()

  return args, tool_log_filepath


def _run_surelog(
    name, filepath, dirpath, surelog_filepath,
    surelog_log_filepath, uvm_reldirpath, mp, mt, tool, output_dirpath):
  start_dt = datetime.now()
  print(f'start-time: {start_dt}')

  surelog_timedelta = timedelta(seconds=0)

  args, tool_log_filepath = _get_run_args(
      name, filepath, dirpath, surelog_filepath,
      uvm_reldirpath, mp, mt, tool, output_dirpath)

  print('Launching surelog with arguments:')
  pprint.pprint(args)
  print('\n')

  status = Status.PASS
  max_cpu_time = 0
  max_vms_memory = 0
  max_rss_memory = 0
  with surelog_log_filepath.open('wt') as surelog_log_strm:
    surelog_start_dt = datetime.now()
    try:
      process = subprocess.Popen(
          args,
          stdout=surelog_log_strm,
          stderr=subprocess.STDOUT,
          cwd=dirpath)

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
      surelog_timedelta = datetime.now() - surelog_start_dt
      print(f'Surelog terminated with exit code: {returncode} in {str(surelog_timedelta)}')
    except:
      status = Status.FAIL
      surelog_timedelta = datetime.now() - surelog_start_dt
      print(f'Surelog threw an exception')
      traceback.print_exc()

    surelog_log_strm.flush()

  if status == Status.PASS and tool_log_filepath and tool_log_filepath.is_file():
    content = tool_log_filepath.open().read()
    if 'ERROR SUMMARY: 0' not in content:
      status = Status.TOOLFAIL

  end_dt = datetime.now()
  delta = end_dt - start_dt
  print(f'end-time: {str(end_dt)} {str(delta)}')

  return {
    'STATUS': status,
    'CPU-TIME': max_cpu_time,
    'VTL-MEM': max_vms_memory,
    'PHY-MEM': max_rss_memory,
    'WALL-TIME': surelog_timedelta
  }


def _run_one(params):
  start_dt = datetime.now()
  name, filepath, surelog_filepath, mp, mt, tool, output_dirpath = params

  log(f'Running {name} ...')

  dirpath = filepath.parent
  env_filepath = output_dirpath / 'env.json'
  regression_log_filepath = output_dirpath / 'regression.log'
  golden_log_filepath, surelog_log_filepath = _get_surelog_log_filepaths(name, dirpath, output_dirpath)
  uvm_reldirpath = (_workspace_dirpath / 'third_party' / 'UVM').relative_to(dirpath, walk_up=True)
  uhdm_slpp_all_filepath = output_dirpath / 'slpp_all' / 'surelog.uhdm'
  uhdm_slpp_unit_filepath = output_dirpath / 'slpp_unit' / 'surelog.uhdm'

  rmtree(dirpath, ['slpp_all', 'slpp_unit'])
  rmdir(output_dirpath)
  mkdir(output_dirpath)

  json_dump({
    'test-name': name,
    'regression': {    
      'test-dirpath': dirpath,
      'test-filepath': filepath,
      'workspace-dirpath': _workspace_dirpath,
      'surelog-filepath': surelog_filepath,
      'uvm-reldirpath': uvm_reldirpath,
      'output-dirpath': output_dirpath,
      'golden-log-filepath': golden_log_filepath,
      'surelog-log-filepath': surelog_log_filepath,
      'uhdm-slpp_all-filepath': uhdm_slpp_all_filepath,
      'uhdm-slpp_unit-filepath': uhdm_slpp_unit_filepath,
      'tool': tool,
    }
  }, env_filepath.open('w'), indent=2)

  result = {
    'TESTNAME': name,
    'STATUS': Status.PASS,
    'golden-log-filepath': golden_log_filepath,
    'surelog-log-filepath': surelog_log_filepath,
    'golden': {},
    'current': {}
  }

  with regression_log_filepath.open('wt') as regression_log_strm, \
          redirect_stdout(regression_log_strm), \
          redirect_stderr(regression_log_strm):
    completed = False
    try:
      print(f'start-time: {start_dt}')
      print( '')
      print( 'Environment:')
      print(f'               test-name: {name}')
      print(f'            test-dirpath: {dirpath}')
      print(f'           test-filepath: {filepath}')
      print(f'       workspace-dirpath: {_workspace_dirpath}')
      print(f'        surelog-filepath: {surelog_filepath}')
      print(f'          uvm-reldirpath: {uvm_reldirpath}')
      print(f'          output-dirpath: {output_dirpath}')
      print(f'     golden-log-filepath: {golden_log_filepath}')
      print(f'    surelog-log-filepath: {surelog_log_filepath}')
      print(f'  uhdm-slpp_all-filepath: {uhdm_slpp_all_filepath}')
      print(f' uhdm-slpp_unit-filepath: {uhdm_slpp_unit_filepath}')
      print(f'                    tool: {tool}')
      print( '\n')

      print('Snapshot ...')
      golden_snapshot = snapshot_directory_state(dirpath)
      print(f'Found {len(golden_snapshot)} files & directories')
      print('\n')

      print('Running Surelog ...', flush=True)
      result.update(_run_surelog(
          name, filepath, dirpath, surelog_filepath,
          surelog_log_filepath, uvm_reldirpath, mp, mt, tool, output_dirpath))
      print('\n')
      regression_log_strm.flush()

      uhdm_src_filepath = None
      if result['STATUS'] == Status.PASS:
        if uhdm_slpp_all_filepath.is_file():
          uhdm_src_filepath = uhdm_slpp_all_filepath
        elif uhdm_slpp_unit_filepath.is_file():
          uhdm_src_filepath = uhdm_slpp_unit_filepath
        else:
          print(f'File not found: {uhdm_slpp_all_filepath}')
          print(f'File not found: {uhdm_slpp_unit_filepath}')

      print(f'Normalizing surelog log file {surelog_log_filepath}')
      if surelog_log_filepath.is_file():
        content = surelog_log_filepath.open().read()
        if 'Segmentation fault' in content:
          result['STATUS'] = Status.SEGFLT

        content = normalize_log(content, {
          str(_workspace_dirpath.as_posix()): '${SURELOG_DIR}',
          r'\${SURELOG_DIR}/out/build/': r'\${SURELOG_DIR}/build/',
        })

        surelog_log_filepath.open('wt').write(content)
      else:
        print(f'File not found: {surelog_log_filepath}')
        result['STATUS'] == Status.FAIL
      print('\n')

      # If golden file is missing, then fail the test explicitly!
      if result['STATUS'] == Status.PASS and not golden_log_filepath.is_file():
        result['STATUS'] = Status.NOGOLD

      result.update({
        'golden': _get_log_statistics(golden_log_filepath),
        'current': _get_log_statistics(surelog_log_filepath)
      })

      if result['STATUS'] == Status.PASS:
        current = result['current']
        golden = result['golden']
        if len(current) == len(golden):
          for k, v in current.items():
            if k == 'STATS':
              # current_stat = v
              # golden_stat = golden.get(k, {})
              # if len(current_stat) == len(golden_stat):
              #   for m, c in current_stat.items():
              #     if c != golden_stat.get(m, 0):
              #       result['STATUS'] = Status.DIFF
              #       break
              # elif golden_stat:
              #   result['STATUS'] = Status.DIFF
              #   break
              pass
            elif v != golden.get(k, 0):
              result['STATUS'] = Status.DIFF
              break

            if result['STATUS'] != Status.PASS:
              break
        else:
          result['STATUS'] = Status.DIFF

      print('Restoring pristine state ...', flush=True)
      current_snapshot = snapshot_directory_state(dirpath)
      print(f'Found {len(current_snapshot)} files & directories')

      restore_directory_state(dirpath, golden_snapshot, output_dirpath, current_snapshot)
      print('\n')

      pprint.pprint({'result': result})
      print('\n')

      end_dt = datetime.now()
      delta = end_dt - start_dt
      print(f'end-time: {str(end_dt)} {str(delta)}')

      completed = True
    except:
      result['STATUS'] = Status.EXECERR
      traceback.print_exc()

    regression_log_strm.flush()

  if is_ci_build():
    generate_tarball(output_dirpath)
    rmdir(output_dirpath)

  log(f'... {name} Completed.' if completed else f'... {name} FAILED.')
  return result


def _print_report(results):
  columns = [
    'TESTNAME', 'STATUS', 'FATAL', 'SYNTAX', 'ERROR',
    'WARNING', 'NOTE', 'CPU-TIME', 'VTL-MEM', 'PHY-MEM'
  ]

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
      result[columns[0]],                                     # TESTNAME
      result[columns[1]].name,                                # STATUS
      _get_cell_value(columns[2]),                            # FATAL
      _get_cell_value(columns[3]),                            # SYNTAX
      _get_cell_value(columns[4]),                            # ERROR
      _get_cell_value(columns[5]),                            # WARNING
      _get_cell_value(columns[6]),                            # NOTE
      '{:.2f}'.format(result.get(columns[7], 0)),             # CPU-TIME
      str(round(result.get(columns[8], 0) / (1024 * 1024))),  # VTL-MEM
      str(round(result.get(columns[9], 0) / (1024 * 1024))),  # PHY-MEM
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


def _run(args, tests):
  if not tests:
    return 0  # No selected tests

  params = [(
    name,
    filepath,
    args.surelog_filepath,
    args.mp,
    args.mt,
    args.tool,
    args.output_dirpath / name
  ) for name, filepath in tests.items()]

  if args.jobs <= 1:
    results = [_run_one(param) for param in params]
  else:
    with multiprocessing.Pool(processes=args.jobs) as pool:
      results = pool.map(_run_one, params)

  print('')
  _print_report(results)

  return sum([entry['STATUS'].value for entry in results])


def _main():
  # Configure the standard streams to be unicode compatible
  sys.stdout.reconfigure(encoding='cp850')
  sys.stderr.reconfigure(encoding='cp850')

  start_dt = datetime.now()
  print(f'Starting Surelog Regression Tests @ {str(start_dt)}')

  parser = argparse.ArgumentParser()

  parser.add_argument(
      '--test-dirpaths', dest='test_dirpaths', required=False, default=_default_test_dirpaths, nargs='*', type=str,
      help='Directories, either absolute or relative to workspace directory, to scan for tests.')
  parser.add_argument(
      '--output-dirpath', dest='output_dirpath', required=False, default=_default_output_dirpath, type=str,
      help='Output directory path, either absolute or relative to the workspace directory.')
  parser.add_argument(
      '--build-dirpath', dest='build_dirpath', required=False, default=_default_build_dirpath, type=str,
      help='Directory, either absolute or relative to workspace directory, to locate surelog binary')
  parser.add_argument(
      '--surelog-filepath', dest='surelog_filepath', required=False, default=_default_surelog_filepath, type=str,
      help='Location, either absolute or relative to build directory, of surelog executable')
  parser.add_argument(
      '--filters', nargs='+', required=False, default=[], type=str, help='Filter tests matching these regex inputs')
  parser.add_argument(
      '--jobs', nargs='?', required=False, default=multiprocessing.cpu_count(), type=int,
      help='Run tests in parallel, optionally providing max number of concurrent processes. Set 0 to run sequentially.')
  parser.add_argument(
      '--tool', dest='tool', choices=['ddd', 'valgrind'], required=False, default=None, type=str,
      help='Run regression test using specified tool.')
  parser.add_argument('--mt', dest='mt', default=None, type=str, help='Enable multithreading mode')
  parser.add_argument('--mp', dest='mp', default=None, type=str, help='Enable multiprocessing mode')
  parser.add_argument('--num_shards', dest='num_shards', required=False,
                      type=int, default=1, help='Number of shards')
  parser.add_argument('--shard', dest='shard', required=False,
                      type=int, default=0, help='This shard')

  args = parser.parse_args()

  if (args.shard >= args.num_shards):
    print("Shard %d out of range 0..%d" % (args.shard, args.num_shards - 1))
    return 1

  args.build_dirpath = Path(args.build_dirpath)
  args.surelog_filepath = Path(args.surelog_filepath)
  args.output_dirpath = Path(args.output_dirpath)
  args.test_dirpaths = [Path(dirpath) for dirpath in args.test_dirpaths]

  if (args.jobs == None) or (args.jobs > multiprocessing.cpu_count()):
    args.jobs = multiprocessing.cpu_count()

  if not args.build_dirpath.is_absolute():
    args.build_dirpath = _workspace_dirpath / args.build_dirpath
  args.build_dirpath = args.build_dirpath.resolve()

  if not args.surelog_filepath.is_absolute():
    args.surelog_filepath = args.build_dirpath / args.surelog_filepath
  args.surelog_filepath = args.surelog_filepath.resolve()

  if not args.output_dirpath.is_absolute():
    args.output_dirpath = args.build_dirpath / args.output_dirpath
  args.output_dirpath = args.output_dirpath.resolve()

  args.test_dirpaths = [
    (dirpath if dirpath.is_absolute() else (_workspace_dirpath / dirpath)).resolve()
    for dirpath in args.test_dirpaths
  ]

  args.filters = build_filters(args.filters)
  all_tests, filtered_tests, blacklisted_tests = _scan(args.test_dirpaths, args.filters, args.shard, args.num_shards)

  if args.jobs > len(filtered_tests):
    args.jobs = len(filtered_tests)

  print( 'Environment:')
  print(f'      command-line: {" ".join(sys.argv)}')
  print(f'   current-dirpath: {Path.cwd()}')
  print(f' workspace-dirpath: {_workspace_dirpath}')
  print(f'     build-dirpath: {args.build_dirpath}')
  print(f'  surelog-filepath: {args.surelog_filepath}')
  print(f'     test-dirpaths: {"; ".join(str(p) for p in args.test_dirpaths)}')
  print(f'    output-dirpath: {args.output_dirpath}')
  print(f'   multi-threading: {args.mt}')
  print(f'  multi-processing: {args.mp}')
  print(f'          max-jobs: {args.jobs}')
  print(f'         max-tests: {len(all_tests)}')
  print(f' blacklisted-tests: {len(blacklisted_tests)}')
  print(f'    filtered-tests: {len(filtered_tests)}')
  print( '\n\n')

  mkdir(args.output_dirpath)

  print(f'Running {len(filtered_tests)} tests ...')
  result = _run(args, filtered_tests)
  print('\n\n')

  end_dt = datetime.now()
  delta = round((end_dt - start_dt).total_seconds())
  print(f'Surelog Regression Test Completed @ {str(end_dt)} in {str(delta)} seconds')
  return result


if __name__ == '__main__':
  sys.exit(_main())
