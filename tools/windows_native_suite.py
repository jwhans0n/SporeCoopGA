#!/usr/bin/env python3
"""Execute unchanged suite cases on an explicit owned native Windows lane.

The supervisor puts each case into a kill-on-close Windows Job before allowing
it to start games. Original assertions, fixture facts, monitor, crash/fault gates
and loggates remain active. Missing prerequisites are UNAVAILABLE, never PASS.
"""
import argparse
import ctypes
from ctypes import wintypes
import datetime
import json
import os
from pathlib import Path
import re
import runpy
import shlex
import subprocess
import sys
import time
import uuid

from windows_native_lane import ROOT, LaneConfig, FixtureStore, backend, required_fixtures, write_json

sys.path.insert(0, str(ROOT / 'tools' / 'tests'))
import suite  # noqa: E402
import loggates  # noqa: E402

REPORTS = ('crash-*.txt', 'crash-*.dmp', 'report-*.txt')


class OwnedJob:
    """No PID/name scans: closing this handle terminates only this case's tree."""
    def __init__(self):
        if os.name != 'nt':
            raise RuntimeError('native Windows Python required')
        kernel = self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
        kernel.CreateJobObjectW.restype = wintypes.HANDLE
        kernel.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
        kernel.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
        kernel.CloseHandle.argtypes = [wintypes.HANDLE]

        class Basic(ctypes.Structure):
            _fields_ = [('process_time', ctypes.c_longlong), ('job_time', ctypes.c_longlong),
                        ('flags', wintypes.DWORD), ('min_working_set', ctypes.c_size_t),
                        ('max_working_set', ctypes.c_size_t), ('process_limit', wintypes.DWORD),
                        ('affinity', ctypes.c_size_t), ('priority', wintypes.DWORD),
                        ('scheduling', wintypes.DWORD)]

        class Io(ctypes.Structure):
            _fields_ = [(n, ctypes.c_ulonglong) for n in (
                'read_ops', 'write_ops', 'other_ops', 'read_bytes', 'write_bytes', 'other_bytes')]

        class Extended(ctypes.Structure):
            _fields_ = [('basic', Basic), ('io', Io), ('process_memory', ctypes.c_size_t),
                        ('job_memory', ctypes.c_size_t), ('peak_process_memory', ctypes.c_size_t),
                        ('peak_job_memory', ctypes.c_size_t)]

        self.handle = kernel.CreateJobObjectW(None, None)
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        info = Extended()
        info.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        if not kernel.SetInformationJobObject(self.handle, 9, ctypes.byref(info), ctypes.sizeof(info)):
            self.close()
            raise ctypes.WinError(ctypes.get_last_error())

    def assign(self, process):
        if not self.kernel.AssignProcessToJobObject(self.handle, int(process._handle)):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def report_files(config):
    logs = config.installation / 'SporebinEP1' / 'mods' / 'logs'
    return {str(p): (p.stat().st_mtime_ns, p.stat().st_size) for pattern in REPORTS for p in logs.glob(pattern)}


def original_command(test):
    words = shlex.split(test['cmd'])
    environment = {}
    while words and re.fullmatch(r'[A-Z][A-Z0-9_]*=.*', words[0]):
        key, value = words.pop(0).split('=', 1)
        environment[key] = value
    if not words or words.pop(0) not in ('python3', 'python', 'py'):
        raise ValueError('case needs an explicit native command adapter: ' + test['cmd'])
    target = (ROOT / words.pop(0)).resolve()
    if ROOT / 'tools' / 'tests' not in target.parents or not target.is_file():
        raise ValueError('case command is outside the real test tree')
    return target, words, environment


def verdict(test, code, text, failures):
    has_result = 'RESULT:' in text or test['name'] == 'unit'
    skipped = bool(re.search(r'RESULT: (?:SKIP|INCONCLUSIVE)\b', text))
    return (code == 0 and has_result and not skipped and 'RESULT: FAIL' not in text
            and 'Traceback' not in text and not failures)


def repository_identity():
    environment = {key: value for key, value in os.environ.items()
                   if key not in ('GIT_DIR', 'GIT_COMMON_DIR', 'GIT_WORK_TREE', 'GIT_INDEX_FILE')}
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=ROOT, env=environment, text=True).strip()
    return git('rev-parse', 'HEAD'), git('branch', '--show-current')


def lab_record(config, results, run_id, branch, began):
    """The existing loggates/labcov/main-gate schema, with native identity added."""
    return dict(run=run_id, tree=str(ROOT), branch=branch, head=config.source_commit, lanes=1,
                seconds=round(time.time() - began), ok=bool(results) and all(r['ok'] for r in results),
                quick=False, skipped=sum(bool(r.get('skipped')) for r in results), results=results,
                backend='windows-native', **config.evidence())


def worker(config_path, name, gate, receipt):
    # Never create children until the supervisor owns this process's whole tree.
    deadline = time.monotonic() + 30
    while not Path(gate).is_file():
        if time.monotonic() >= deadline:
            raise RuntimeError('supervisor did not acquire the case process job')
        time.sleep(.05)
    os.environ['SPORE_WINDOWS_LANE_CONFIG'] = str(Path(config_path).resolve())
    os.environ['SPORE_WINDOWS_LANE_OWNER'] = str(os.getpid())
    os.environ['SPORE_WARM'] = '0'
    test = next(t for t in suite.T if t['name'] == name)
    lane = backend()
    config = lane.config
    started = time.time()
    reports0 = report_files(config)
    marks = {r: loggates.log_marks(str(config.game_log(r))) for r in ('host', 'guest')}
    failures = []
    code = 1
    try:
        if name == 'unit':
            if not config.unit_command:
                raise RuntimeError('UNAVAILABLE: configure the real repository make test command')
            unit_env = {key: value for key, value in os.environ.items()
                        if key not in ('SPORE_WINDOWS_LANE_CONFIG', 'SPORE_WINDOWS_LANE_OWNER')}
            code = subprocess.call(list(config.unit_command), cwd=ROOT, env=unit_env)
        else:
            target, args, environment = original_command(test)
            os.environ.update(environment)
            sys.argv = [str(target), *args]
            try:
                runpy.run_path(str(target), run_name='__main__')
                code = 0
            except SystemExit as result:
                code = result.code if isinstance(result.code, int) else 0 if result.code is None else 1
        # Same post-check crash window as lab.py, with owned process handles.
        live = {r: [(p, p.poll()) for p, _, _ in lab.processes] for r, lab in lane.labs.items()}
        time.sleep(5 if live else 1)
        for role, processes in live.items():
            for process, previous in processes:
                if previous is None and process.poll() is not None:
                    failures.append(f'FAIL lab: {role} exited after the test checks')
            for console in lane.labs[role].consoles:
                try:
                    if not console.call('ping').get('ok'):
                        failures.append(f'FAIL lab: {role} console stopped answering after the checks')
                except (OSError, ValueError):
                    failures.append(f'FAIL lab: {role} console stopped answering after the checks')
        reports1 = report_files(config)
        changed = [p for p, info in reports1.items() if reports0.get(p) != info]
        if not test.get('reports'):
            failures.extend('FAIL lab: new report ' + Path(p).name for p in changed)
        write_json(receipt, dict(config.evidence(), name=name, rc=code, failures=failures,
                   started=started, ended=time.time(), reports=changed, log_marks=marks,
                   original_command=test['cmd'], games_started=sum(lane.generations.values())))
    finally:
        lane.close()
    return code


def run(config_path, selection, repeat=1):
    config_path = Path(config_path).resolve()
    config = LaneConfig.load(config_path)
    head, branch = repository_identity()
    if head != config.source_commit:
        raise RuntimeError('test checkout HEAD differs from the payload source commit; do not misattribute smoke')
    selected = suite.select(selection)
    config.verify_payload()
    store = FixtureStore(config.fixtures, config.fixture_hashes)
    missing = []
    for name in required_fixtures(selected):
        try:
            store.verified(name)
        except (OSError, ValueError) as error:
            missing.append(str(error))
    if any(t['name'] == 'unit' for t in selected) and not config.unit_command:
        missing.append('configure the real repository make test command')
    config.output.mkdir(parents=True, exist_ok=True)
    if missing:
        write_json(config.output / 'prerequisites.json', dict(status='UNAVAILABLE', reasons=missing))
        print('UNAVAILABLE: ' + '; '.join(missing), flush=True)
        return 2
    if os.name != 'nt':
        raise RuntimeError('native Windows Python required')
    began_run = time.time()
    run_id = datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-windows-' + uuid.uuid4().hex[:6]
    record_root = ROOT / 'out' / 'lab' / run_id
    record_root.mkdir(parents=True)
    results = []
    # Serial execution is intentional: UI discovery and native renderer reset
    # qualification are exclusive operations, not independent Linux lanes.
    for round_index in range(repeat):
        for test in selected:
            name = test['name'] + (f'#{round_index + 1}' if repeat > 1 else '')
            output = config.output / (name + '-' + uuid.uuid4().hex[:8])
            output.mkdir()
            gate, receipt, log = output / 'owned.start', output / 'native.json', output / 'test.log'
            env = dict(os.environ, PYTHONUNBUFFERED='1', SPORE_LAB_RUN=str(config.output), SPORE_LAB_TEST=name)
            if test.get('monitor') is not None:
                env['SPORE_MONITOR'] = test['monitor'] if isinstance(test['monitor'], str) else json.dumps(test['monitor'])
            job = OwnedJob()
            process = None
            began = time.time()
            timed_out = False
            try:
                with log.open('w', encoding='utf-8') as stream:
                    command = [sys.executable, __file__, '_case', str(config_path), test['name'], str(gate), str(receipt)]
                    process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT,
                                               creationflags=subprocess.CREATE_NO_WINDOW)
                    job.assign(process)
                    gate.write_text('owned\n', encoding='ascii')
                    try:
                        code = process.wait(timeout=test.get('timeout') or 1800)
                    except subprocess.TimeoutExpired:
                        timed_out, code = True, 124
            finally:
                job.close()
                if process is not None:
                    if process.poll() is None:
                        # Assignment may have failed before any game could start.
                        process.terminate()
                    process.wait(timeout=30)
            text = log.read_text(encoding='utf-8', errors='replace')
            native = json.loads(receipt.read_text(encoding='utf-8')) if receipt.exists() else {}
            failures = list(native.get('failures', ()))
            if timed_out:
                failures.append('FAIL lab: case timed out; owned job terminated')
            if not receipt.exists():
                failures.append('FAIL lab: case did not complete native/report qualification')
            logs = {}
            for role in ('host', 'guest'):
                source = config.game_log(role)
                if source.exists() and source.stat().st_mtime >= began - 2:
                    target = output / (role + '.game.log')
                    shutil_copy_log(source, target)
                    logs[role] = str(target)
            gate_failures = []
            try:
                gate_failures, notes, _ = loggates.check_logs(logs, test.get('gates'),
                    list(test.get('allow') or []) + suite.ALLOW_ALL, text,
                    marks=native.get('log_marks'), start=began, end=time.time())
                failures.extend(gate_failures)
            except Exception as error:
                failures.append('FAIL lab: log gate failed to execute: ' + repr(error))
                notes = []
            if not test.get('reports'):
                for role, path in logs.items():
                    contents = Path(path).read_text(encoding='utf-8', errors='replace')
                    for hook in sorted(set(re.findall(r'hook_create ([0-9A-Fa-f]+) failed: MH_ERROR_\w+', contents))):
                        failures.append(f'FAIL lab: {role} hook {hook} was not installed')
                from lab import fault_count
                for line in text.splitlines():
                    if ' faults: ' in line and fault_count(line) > 0:
                        failures.append('FAIL lab: ' + line.strip())
            for source in native.get('reports', ()):
                path = Path(source)
                if path.is_file():
                    shutil_copy_log(path, output / path.name)
            ok = verdict(test, code, text, failures)
            skipped = bool(re.search(r'RESULT: (?:SKIP|INCONCLUSIVE)\b', text))
            recorded_log = record_root / (name + '.log')
            shutil_copy_log(log, recorded_log)
            for role, source in logs.items():
                shutil_copy_log(source, record_root / (name + '.' + role + '.game.log'))
            result = dict(name=name, ok=ok, rc=code, failures=failures, notes=notes,
                          fails=failures, gates=gate_failures + notes,
                          skipped=skipped, inconclusive=skipped, verdict='PASS' if ok else 'INCONCLUSIVE' if skipped else 'FAIL',
                          lane=1, quick=False, harness=not receipt.exists(), reports=native.get('reports', ()),
                          log_marks=native.get('log_marks', {}),
                          seconds=round(time.time() - began, 2),
                          log=str(recorded_log), native=str(receipt), **config.evidence())
            results.append(result)
            record = lab_record(config, results, run_id, branch, began_run)
            write_json(config.output / 'results.json', record)
            write_json(record_root / 'results.json', record)
            (record_root / 'summary.md').write_text('# lab run ' + run_id + '\n\n'
                + f'{branch} {config.source_commit}, native Windows, {len(results)} tests\n\n'
                + '\n'.join(f'- {r["name"]}: {r["verdict"]}' for r in results) + '\n', encoding='utf-8')
            print(f'{"PASS" if ok else "FAIL"} {name} ({result["seconds"]} s): {"; ".join(failures)}', flush=True)
    return 0 if results and all(r['ok'] for r in results) else 1


def shutil_copy_log(source, target):
    # Bytes preserve timestamps/offset semantics for the real log gates.
    import shutil
    shutil.copy2(source, target)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('run', '_case'))
    parser.add_argument('config', type=Path)
    parser.add_argument('selection', nargs='?', default='smoke')
    parser.add_argument('gate', nargs='?')
    parser.add_argument('receipt', nargs='?')
    parser.add_argument('--repeat', type=int, default=1)
    args = parser.parse_args(argv)
    if args.command == '_case':
        return worker(args.config, args.selection, args.gate, args.receipt)
    return run(args.config.resolve(), args.selection, args.repeat)


if __name__ == '__main__':
    raise SystemExit(main())
