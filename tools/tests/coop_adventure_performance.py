#!/usr/bin/env python3
"""Check a running, isolated two-player adventure for the 438-actor FPS regression.

Start Adventure Town with coop_adventure.py pair first. This check does not drive
UI or change the adventure. Actual presented-frame deltas, rather than the FPS
overlay's moving average, determine the result. Both sessions must stay in the
same active attempt, keep at least 400 actors and report no native faults.
"""
import argparse
import concurrent.futures
import json
from pathlib import Path
import re
import sys
import time

from coop_adventure import Game, FixtureError, verify_lab
from adventure_memory import AdventureMemory

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from sporectl import Console


def measure(instance, record, seconds, minimum_actors, minimum_fps, output):
    console = Console(instance, timeout=15)
    reader = None
    def call(command):
        response = console.call(command)
        if not response.get('ok'):
            raise FixtureError(f'{instance}: {command}: {response}')
        return response['out']
    def state():
        info = call('info')
        if f"pid={record['pid']}\n" not in info or '(Scenario)' not in info:
            raise FixtureError(f'{instance}: the recorded Scenario process is not running')
        text = call('coop.adventure status')
        world = re.search(r'adventure world: available=1 run=(\d+) actors=(\d+)', text)
        if not world or 'Adventure in progress' not in text or 'active=1' not in text:
            raise FixtureError(f'{instance}: no active native adventure')
        if int(world[2]) < minimum_actors:
            raise FixtureError(f'{instance}: only {world[2]} actors; need {minimum_actors} for this regression')
        if 'total faults: 0' not in call('exc'):
            raise FixtureError(f'{instance}: native faults were recorded')
        native = reader.goals()
        if native['state'] != 3:
            raise FixtureError(f'{instance}: the native adventure is not in its playing state')
        return {'run': int(world[1]), 'actors': int(world[2]), 'info': info, 'status': text,
                'act': native['act'], 'elapsed_ms': native['elapsed']}
    def frame():
        response = call('frame')
        match = re.search(r'frame=(\d+)', response)
        if not match:
            raise FixtureError(f'{instance}: no frame counter: {response}')
        return int(match[1]), time.perf_counter()
    try:
        reader = AdventureMemory(Game(instance, output.parent / 'performance-reads'), record)
        before = state()
        start, t0 = frame()
        time.sleep(seconds)
        end, t1 = frame()
        after = state()
        elapsed = t1 - t0
        fps = (end - start) / elapsed
        passed = (before['run'] == after['run'] and before['act'] == after['act'] and
                  after['elapsed_ms'] > before['elapsed_ms'] and fps >= minimum_fps)
        return {'instance': instance, 'pass': passed, 'fps': fps, 'elapsed': elapsed,
                'frames': end - start, 'before': before, 'after': after}
    finally:
        if reader:
            reader.close()
        if console.sock:
            console.sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--host', type=int, default=21)
    parser.add_argument('--guest', type=int, default=22)
    parser.add_argument('--seconds', type=float, default=8)
    parser.add_argument('--minimum-actors', type=int, default=400)
    parser.add_argument('--minimum-fps', type=float, default=20)
    args = parser.parse_args()
    if args.host == args.guest or not 2 <= args.seconds <= 60 or args.minimum_fps <= 0 or args.minimum_actors < 1:
        parser.error('use distinct instances, 2..60 seconds, and positive actor/FPS thresholds')
    result = {'pass': False, 'minimum_fps': args.minimum_fps, 'minimum_actors': args.minimum_actors}
    try:
        records = verify_lab(args.metadata, [args.host, args.guest])
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(measure, instance, records[instance], args.seconds,
                                   args.minimum_actors, args.minimum_fps, args.output) for instance in (args.host, args.guest)]
            result['sessions'] = [future.result() for future in futures]
        result['pass'] = all(session['pass'] for session in result['sessions'])
        result['pass'] &= result['sessions'][0]['before']['run'] == result['sessions'][1]['before']['run']
    except (FixtureError, OSError, ValueError) as error:
        result['error'] = str(error)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print('RESULT:', 'PASS' if result['pass'] else 'FAIL', json.dumps(result))
    return 0 if result['pass'] else 1


if __name__ == '__main__':
    sys.exit(main())
