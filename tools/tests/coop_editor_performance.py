#!/usr/bin/env python3
"""Measure actual frame intervals in already-owned hidden Windows editor games.

This fixture does not drive UI, change frame caps or write game memory. Capture
shared and independent samples in the same native editors/cameras, then compare.
Native editor inspection and fault checks bracket sampling, outside the measured
interval. The bounded frame callback records only numbers; sorting happens after
recording stops. A diagnostic run on a small fixture is not a large-editor pass.
"""
import argparse
import configparser
from concurrent.futures import ThreadPoolExecutor
import ctypes
from ctypes import wintypes as W
import json
import hashlib
import math
import os
from pathlib import Path
import re
import sys
import subprocess
import threading
import time

from coop_adventure import Game, FixtureError
from adventure_interaction_fixture import _verify_game


def values(text):
    return {key: float(value) if '.' in value else int(value)
            for key, value in re.findall(r'\b([a-z_][a-z0-9_]*)=(-?[0-9]+(?:\.[0-9]+)?)(?=\s|$)', text)}


def configuration(game):
    result = {name: game.call(command) for name, command in (
        ('frame', 'coop.frame_metrics config'), ('background', 'bg'), ('window', 'window'), ('mods', 'mods'))}
    frame = values(result['frame'])
    if any(key not in frame for key in ('frame_limit_ms', 'screen_width', 'screen_height')) or min(frame['screen_width'], frame['screen_height']) <= 0:
        raise FixtureError('Actual frame cap and rendered dimensions are unavailable')
    if result['background'].strip() != 'background mode on':
        raise FixtureError('Every hidden editor must keep rendering in background mode')
    return result


def projection(game, config):
    text = game.call('coop.editor objects')
    frame = values(config['frame']); points = []
    for x, y in re.findall(r'\bscreen=(-?[0-9]+(?:\.[0-9]+)?),(-?[0-9]+(?:\.[0-9]+)?)', text):
        x, y = float(x), float(y)
        if 0 <= x < frame['screen_width'] and 0 <= y < frame['screen_height']:
            points.append((x, y))
    if len(points) < 3:
        raise FixtureError('At least three visible authored landmarks are required to verify the camera')
    origin = points[0]
    second = next((point for point in points[1:] if math.dist(point, origin) > 5), None)
    if second is None or not any(abs((second[0] - origin[0]) * (point[1] - origin[1]) -
                                  (second[1] - origin[1]) * (point[0] - origin[0])) > 25 for point in points):
        raise FixtureError('Camera landmarks must not all coincide or lie on one screen line')
    return text


def source_fence(before, after):
    prior, current = before.splitlines(), after.splitlines()
    if not prior or not current:
        raise FixtureError('Native frame-source log coverage is unavailable')
    anchor = prior[-min(3, len(prior)):]
    locations = [i for i in range(len(current) - len(anchor) + 1) if current[i:i + len(anchor)] == anchor]
    if not locations:
        raise FixtureError('The frame-source log wrapped during measurement; retry a shorter sample')
    added = current[locations[0] + len(anchor):]
    if any('frames:' in line and ('Present' in line or 'frames=loop' in line) for line in added):
        raise FixtureError('The frame source switched during measurement')
    return {'verified': True, 'new_log_lines': len(added),
            'anchor_sha256': hashlib.sha256('\n'.join(anchor).encode('utf-8')).hexdigest()}


def memory(pid):
    class Counters(ctypes.Structure):
        _fields_ = [('cb', W.DWORD), ('faults', W.DWORD)] + [
            (name, ctypes.c_size_t) for name in ('peak_working_set', 'working_set', 'peak_paged',
                'paged', 'peak_nonpaged', 'nonpaged', 'pagefile', 'peak_pagefile', 'private_bytes')]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes = (W.DWORD, W.BOOL, W.DWORD)
    kernel.OpenProcess.restype = W.HANDLE
    kernel.CloseHandle.argtypes = (W.HANDLE,)
    process = kernel.OpenProcess(0x1000 | 0x10, False, pid)
    if not process:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        psapi = ctypes.WinDLL('psapi', use_last_error=True)
        psapi.GetProcessMemoryInfo.argtypes = (W.HANDLE, ctypes.POINTER(Counters), W.DWORD)
        psapi.GetProcessMemoryInfo.restype = W.BOOL
        counters = Counters(); counters.cb = ctypes.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(process, ctypes.byref(counters), counters.cb):
            raise ctypes.WinError(ctypes.get_last_error())
        return {'private_bytes': counters.private_bytes, 'working_set': counters.working_set}
    finally:
        kernel.CloseHandle(process)


def sample(instance, record, args, barrier=None):
    game = Game(instance, args.output.parent / (args.output.stem + '-commands'))
    recording = False
    try:
        _verify_game(game, record)
        info = game.call('info')
        if 'frames from: Present' not in info or '(Scenario)' not in info:
            raise FixtureError('Performance requires native Scenario rendering from Present')
        before = game.call('coop.editor inspect')
        counts = values(before)
        if counts.get('placements', 0) < args.minimum_placements:
            raise FixtureError('Too few editor placements for the requested performance gate: ' + before)
        state_before = game.call('coop.editor status')
        if args.kind == 'shared' and ('phase=4' not in state_before or 'frozen=1' in state_before):
            raise FixtureError('Shared measurement requires a ready, unfrozen shared editor')
        if args.kind == 'independent' and 'phase=0' not in state_before:
            raise FixtureError('Independent measurement requires an inactive editor coordinator')
        party = game.call('coop.status')
        if args.kind == 'independent' and party.strip() != 'offline':
            raise FixtureError('Independent native editors must leave the party before baseline measurement')
        if args.kind == 'shared' and not (party.startswith('host port=') or re.match(r'client id=\d+ connected\n', party)):
            raise FixtureError('Shared measurement requires a connected editor party')
        config = configuration(game)
        camera = projection(game, config)
        before_memory = memory(record['pid'])
        # Do not include serializer/inspection or previous asynchronous UI work.
        time.sleep(2)
        if barrier:
            barrier.wait(timeout=60)
        source_before = game.call('log 2000')
        start_wall = time.monotonic()
        recording = True
        game.call('coop.frame_metrics start')
        time.sleep(args.seconds)
        measured = values(game.call('coop.frame_metrics stop'))
        recording = False
        stop_wall = time.monotonic()
        after_memory = memory(record['pid'])
        after = game.call('coop.editor inspect')
        state_after = game.call('coop.editor status')
        info_after = game.call('info')
        if before != after or 'frames from: Present' not in info_after:
            raise FixtureError('The authored fixture, camera, or frame source changed during measurement')
        if config != configuration(game) or camera != projection(game, config):
            raise FixtureError('The frame cap, viewport, loaded module or projected camera changed during measurement')
        source = source_fence(source_before, game.call('log 2000'))
        _verify_game(game, record)
        if 'total faults: 0' not in game.call('exc'):
            raise FixtureError('Native faults were recorded')
        prior_state, current_state = values(state_before), values(state_after)
        if any(prior_state.get(key) != current_state.get(key) for key in ('phase', 'revision', 'durable', 'frozen')):
            raise FixtureError('Editor authority or revision changed during measurement')
        if measured.get('samples', 0) < 100 or measured.get('invalid', 1) or measured.get('recording', 1):
            raise FixtureError('Frame samples were incomplete or invalid: ' + repr(measured))
        if not args.seconds * .9 <= measured.get('seconds', 0) <= args.seconds * 1.1:
            raise FixtureError('Sampling saturated or stalled; use a shorter interval: ' + repr(measured))
        validate_metrics(measured)
        return {'instance': instance, 'pid': record['pid'], 'inspect': before, 'counts': counts,
                'configuration': config, 'projection': camera, 'state': state_before, 'info': info, 'party': party,
                'sample_start': start_wall, 'sample_stop': stop_wall, 'source_fence': source,
                'metrics': measured, 'memory_before': before_memory, 'memory_after': after_memory}
    except Exception:
        if barrier:
            barrier.abort()
        raise
    finally:
        if recording:
            try:
                game.call('coop.frame_metrics stop', required=False)
            except Exception:
                pass
        game.save_log()
        if game.console.sock:
            game.console.sock.close()


def validate_metrics(metrics):
    for name in ('median_fps', 'median_ms', 'p95_ms', 'seconds', 'samples'):
        value = metrics.get(name)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
            raise FixtureError('Missing, nonfinite or nonpositive frame metric: ' + name)
    if metrics.get('invalid') != 0 or metrics.get('recording') != 0 or metrics['samples'] < 100 or metrics['samples'] > 8192:
        raise FixtureError('Frame samples are incomplete or invalid')
    if metrics['p95_ms'] < metrics['median_ms'] or not math.isclose(metrics['median_fps'] * metrics['median_ms'], 1000, rel_tol=1e-5):
        raise FixtureError('Frame percentiles or reciprocal FPS are inconsistent')


def machine():
    command = ("@{cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,NumberOfCores,NumberOfLogicalProcessors);"
               "gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,PNPDeviceID,VideoModeDescription);"
               "spore_pids=@(Get-Process SporeApp -ErrorAction SilentlyContinue | Sort-Object Id | Select-Object -ExpandProperty Id)} | ConvertTo-Json -Depth 4 -Compress")
    result = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command', command],
        capture_output=True, text=True, check=True, timeout=45, creationflags=subprocess.CREATE_NO_WINDOW)
    data = json.loads(result.stdout)
    if not data.get('cpu') or not data.get('gpu') or not data.get('spore_pids'):
        raise FixtureError('Actual CPU, GPU/driver and total Spore process roster are required')
    return data


def process_roster(hardware, measured, diagnostic=False):
    running = hardware.get('spore_pids')
    if (not isinstance(running, list) or not running or
            any(type(pid) is not int or pid <= 0 for pid in running) or len(set(running)) != len(running)):
        raise FixtureError('The complete live Spore process roster is invalid')
    if any(type(pid) is not int or pid <= 0 for pid in measured) or len(set(measured)) != len(measured):
        raise FixtureError('Measured Spore process identities are invalid')
    if not set(measured).issubset(running):
        raise FixtureError('A measured process is missing from the live Spore roster')
    complete = set(measured) == set(running)
    if not complete and not diagnostic:
        raise FixtureError('Large-editor measurement must cover every live Spore process; use --diagnostic with unrelated games present')
    return complete


def sessions(report, diagnostic=False):
    if report.get('schema') != 2 or not re.fullmatch(r'[0-9a-f]{64}', report.get('expected_dll_sha256', '')) or not report.get('machine') or not report.get('sporekit_ini_sha256'):
        raise FixtureError('Use current owned-process samples with recorded native configuration')
    seconds = report.get('seconds_requested', 0)
    if isinstance(seconds, bool) or not isinstance(seconds, (int, float)) or not math.isfinite(seconds) or not 5 <= seconds <= 120:
        raise FixtureError('Invalid requested native recording duration')
    rows = report.get('sessions')
    if not isinstance(rows, list) or not 1 <= len(rows) <= 4:
        raise FixtureError('A sample must have 1..4 distinct process records')
    result = {}
    for row in rows:
        instance, pid = row.get('instance'), row.get('pid')
        if type(instance) is not int or type(pid) is not int or not 2 <= instance <= 103 or pid <= 0 or instance in result:
            raise FixtureError('A sample has invalid or duplicate process identity')
        if pid in {prior['pid'] for prior in result.values()}:
            raise FixtureError('Two instance records identify the same process')
        validate_metrics(row.get('metrics', {}))
        if not 1 <= row.get('counts', {}).get('placements', 0) <= 1024 or not row.get('projection'):
            raise FixtureError('A sample lacks a bounded native fixture and camera signature')
        if any(not isinstance(row.get(key), (int, float)) or not math.isfinite(row[key]) for key in ('sample_start', 'sample_stop')) or row['sample_stop'] <= row['sample_start']:
            raise FixtureError('A sample lacks its actual recording interval')
        if not row.get('source_fence', {}).get('verified'):
            raise FixtureError('A sample cannot rule out temporary non-Present frame sources')
        result[instance] = row
    process_roster(report['machine'], [row['pid'] for row in rows], diagnostic)
    overlap = min(row['sample_stop'] for row in rows) - max(row['sample_start'] for row in rows)
    if overlap < seconds * .9:
        raise FixtureError('Peer recording intervals did not cover the same workload')
    if report.get('kind') == 'independent':
        if any(row.get('party', '').strip() != 'offline' for row in rows):
            raise FixtureError('Independent native baseline still had co-op gameplay enabled')
    elif report.get('kind') == 'shared':
        slots, hosts = [], []
        for row in rows:
            line = row.get('party', '').splitlines()[0] if row.get('party') else ''
            host = re.fullmatch(r'host port=\d+ players=(\d+)/4', line)
            guest = re.fullmatch(r'client id=(\d+) connected', line)
            if host:
                hosts.append(int(host.group(1))); slots.append(0)
            elif guest:
                slots.append(int(guest.group(1)))
            else:
                raise FixtureError('A shared sample has no connected party identity')
        if hosts != [len(rows)] or sorted(slots) != list(range(len(rows))):
            raise FixtureError('Measure every member of the same shared party, not a partial process roster')
    else:
        raise FixtureError('Unknown native measurement kind')
    return result


def compare(args):
    baseline = json.loads(args.baseline.read_text(encoding='utf-8-sig'))
    shared = json.loads(args.shared.read_text(encoding='utf-8-sig'))
    if not baseline.get('pass') or not shared.get('pass') or baseline.get('kind') != 'independent' or shared.get('kind') != 'shared':
        raise FixtureError('Both reports must be successful samples of the stated kind')
    if baseline['fixture'] != shared['fixture'] or baseline['hardware'] != shared['hardware'] or baseline['minimum_placements'] != shared['minimum_placements'] or baseline.get('machine') != shared.get('machine') or baseline.get('expected_dll_sha256') != shared.get('expected_dll_sha256') or baseline.get('seconds_requested') != shared.get('seconds_requested') or baseline.get('sporekit_ini_sha256') != shared.get('sporekit_ini_sha256'):
        raise FixtureError('Fixture, hardware or requested fixture size differs')
    diagnostic = getattr(args, 'diagnostic', False)
    if not diagnostic and (baseline.get('diagnostic') or shared.get('diagnostic')):
        raise FixtureError('Diagnostic samples cannot establish the large-editor gate; use --diagnostic for comparison')
    original, current = sessions(baseline, diagnostic), sessions(shared, diagnostic)
    if original.keys() != current.keys():
        raise FixtureError('Process count or instance roster differs')
    comparisons = []
    for instance, row in current.items():
        prior = original[instance]
        if row['pid'] != prior['pid'] or row['inspect'] != prior['inspect'] or row['configuration'] != prior['configuration'] or row['projection'] != prior['projection']:
            raise FixtureError('Use the same processes, exact authored fixture/camera and render configuration')
        if prior.get('party', '').strip() != 'offline':
            raise FixtureError('Independent native baseline still had co-op gameplay enabled')
        if not getattr(args, 'diagnostic', False) and (len(current) not in (2, 4) or baseline['minimum_placements'] < 400 or
                min(row['counts']['placements'], prior['counts']['placements']) < 400 or
                values(row['configuration']['frame']).get('frame_limit_ms') != 0):
            raise FixtureError('Large-editor acceptance requires 2 or 4 processes, at least 400 placements each and an uncapped native render loop; use --diagnostic for other comparisons')
        fps_ratio = row['metrics']['median_fps'] / prior['metrics']['median_fps']
        p95_ratio = row['metrics']['p95_ms'] / prior['metrics']['p95_ms']
        comparisons.append({'instance': instance, 'median_fps_ratio': fps_ratio,
                            'p95_frame_time_ratio': p95_ratio, 'pass': fps_ratio >= .90 and p95_ratio <= 1.15})
    return {'pass': all(row['pass'] for row in comparisons), 'kind': 'diagnostic-comparison' if getattr(args, 'diagnostic', False) else 'comparison',
            'large_editor_gate': not getattr(args, 'diagnostic', False) and all(row['pass'] for row in comparisons),
            'memory_turnover_verified': False,
            'baseline': str(args.baseline), 'shared': str(args.shared), 'sessions': comparisons}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    sub = p.add_subparsers(dest='action', required=True)
    capture = sub.add_parser('sample')
    capture.add_argument('--metadata', type=Path, required=True)
    capture.add_argument('--diagnostic', action='store_true', help='Allow unrelated Spore games; this recording cannot establish large-editor acceptance')
    capture.add_argument('--execute', action='store_true', help='Explicitly authorize metrics recording in verified owned games')
    capture.add_argument('--expected-dll-sha256', required=True)
    capture.add_argument('--instances', nargs='+', type=int, required=True)
    capture.add_argument('--kind', choices=('independent', 'shared'), required=True)
    capture.add_argument('--seconds', type=float, default=20)
    capture.add_argument('--minimum-placements', type=int, default=400)
    capture.add_argument('--fixture', required=True, help='Matching fixture/configuration label')
    capture.add_argument('--hardware', required=True, help='Recorded CPU/GPU/driver identity')
    capture.add_argument('--output', type=Path, required=True)
    comparison = sub.add_parser('compare')
    comparison.add_argument('--baseline', type=Path, required=True)
    comparison.add_argument('--shared', type=Path, required=True)
    comparison.add_argument('--output', type=Path, required=True)
    comparison.add_argument('--diagnostic', action='store_true', help='Compare smaller/capped diagnostics without claiming large-editor acceptance')
    args = p.parse_args(); result = {'pass': False}
    try:
        if args.action == 'compare':
            result = compare(args)
        else:
            if not 5 <= args.seconds <= 120 or args.minimum_placements < 1 or not 1 <= len(set(args.instances)) == len(args.instances) <= 4:
                raise FixtureError('Use 1..4 distinct instances, 5..120 seconds and a positive placement minimum')
            if not args.execute or os.name != 'nt' or not re.fullmatch(r'[0-9a-f]{64}', args.expected_dll_sha256):
                raise FixtureError('Sampling requires Windows, --execute and an exact lowercase candidate SHA-256')
            from coop_editor_party import Ownership
            ownership = Ownership(args.metadata, args.instances, args.expected_dll_sha256)
            try:
                ini = ownership.installation / 'SporebinEP1' / 'mods' / 'sporekit.ini'
                ini_bytes = ini.read_bytes(); ini_hash = hashlib.sha256(ini_bytes).hexdigest()
                settings = configparser.ConfigParser(); settings.read_string(ini_bytes.decode('utf-8-sig'))
                if settings.getint('sporekit', 'log_level', fallback=0) > 1:
                    raise FixtureError('Frame-source coverage requires debug/info logging in the cold-start lab configuration')
                hardware = machine()
                process_roster(hardware, [ownership.records[i]['pid'] for i in args.instances], args.diagnostic)
                result.update(schema=2, diagnostic=args.diagnostic, kind=args.kind, fixture=args.fixture, hardware=args.hardware, machine=hardware,
                    expected_dll_sha256=args.expected_dll_sha256, sporekit_ini_sha256=ini_hash,
                    seconds_requested=args.seconds, minimum_placements=args.minimum_placements)
                barrier = threading.Barrier(len(args.instances))
                with ThreadPoolExecutor(max_workers=len(args.instances)) as pool:
                    result['sessions'] = list(pool.map(lambda i: sample(i, ownership.records[i], args, barrier), args.instances))
                ownership.verify_windows()
                if hardware != machine() or hashlib.sha256(ini.read_bytes()).hexdigest() != ini_hash:
                    raise FixtureError('Hardware/driver identity or total Spore process roster changed during measurement')
                sessions(result, args.diagnostic)
            finally:
                ownership.close()
            result['pass'] = True
    except Exception as error:
        result['error'] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
