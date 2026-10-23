#!/usr/bin/env python3
"""Play a native guest social session and verify its authoritative result.

Requires an existing isolated pair and a visible, eligible NPC body coordinate.
The fixture uses only mouse/ability input to complete the social session. It does
not inject a completion callback, change relationships or force a goal result.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
from coop_adventure import Game, FixtureError, verify_lab
from adventure_memory import AdventureMemory


def actor(game, marker):
    text = game.call(f'coop.adventure.world actors {marker} 48')
    found = re.search(rf'^marker={marker} incarnation=(\d+) .*?object=(0x[0-9a-fA-F]+)', text, re.M)
    if not found:
        raise FixtureError('target is outside the bounded native actor diagnostic batch')
    return int(found.group(2), 16), int(found.group(1))


def flags(memory, identity):
    address, incarnation = identity
    if memory.pointer(address + 0x24) != incarnation:
        raise FixtureError('native social target incarnation changed')
    return memory.pointer(address + 0xB58)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, default=21)
    parser.add_argument('--guest', type=int, default=22)
    parser.add_argument('--marker', type=int, required=True)
    parser.add_argument('--x', type=int, required=True)
    parser.add_argument('--y', type=int, required=True)
    parser.add_argument('--key', choices=('1', '2', '3', '4'), default='1')
    parser.add_argument('--mode', choices=('befriend', 'recruit'), required=True)
    parser.add_argument('--near-player', type=int,
                        help='isolated fixture: place this party captain beside the selected NPC before greeting')
    parser.add_argument('--timeout', type=float, default=50)
    parser.add_argument('--metadata', type=Path, default=ROOT / 'out/windows-lab/runtime-bootstrap-3/processes.json')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    records = verify_lab(args.metadata, (args.host, args.guest))
    host, guest = Game(args.host, args.out), Game(args.guest, args.out)
    readers = []
    try:
        readers = [AdventureMemory(host, records[args.host]), AdventureMemory(guest, records[args.guest])]
        a, b = readers
        before = a.goals()
        recruitment_goal = next((goal for goal in before['goals'] if goal['type'] == 5 and
                                args.marker in goal['targets'] and not goal['complete']), None)
        if args.mode == 'recruit' and not recruitment_goal:
            raise FixtureError('recruitment fixture requires a matching incomplete Ally objective')
        targets = [actor(game, args.marker) for game in (host, guest)]
        old_flags = [flags(memory, target) for memory, target in zip(readers, targets)]
        bit = 0x80 if args.mode == 'befriend' else 0x100
        if any(value & bit for value in old_flags):
            raise FixtureError('target already has the expected social result')
        def starts():
            value = re.search(r'native social presentation: starts=(\d+)', guest.call('coop.adventure status'))
            return int(value.group(1)) if value else -1
        initial_starts = starts()
        guest.click(args.x, args.y)
        if args.near_player is not None:
            guest.call(f'coop.adventure.world near {args.marker} {args.near_player}')
        guest.call(f'key {args.key} down')
        try:
            time.sleep(.3)
        finally:
            guest.call(f'key {args.key} up')
        # Let native approach/greeting finish before sending a response. Repeated
        # input during approach can replace its pending target as the camera moves.
        guest.wait('native NPC social performer starts', lambda: starts() > initial_starts, timeout=18)
        deadline = time.monotonic() + args.timeout
        samples = []
        complete = False
        while time.monotonic() < deadline:
            guest.call(f'key {args.key} down')
            try:
                time.sleep(.3)
            finally:
                guest.call(f'key {args.key} up')
            guest.call('wait.frames 8 15000')
            current = a.goals()
            sample = dict(host=current, guest=b.goals(), status=guest.call('coop.adventure status'))
            if current['act'] == before['act']:
                sample['flags'] = [flags(memory, target) for memory, target in zip(readers, targets)]
                complete = all(value & bit for value in sample['flags'])
                if args.mode == 'recruit':
                    goal = current['goals'][recruitment_goal['index']]
                    complete = complete and (goal['complete'] or goal['count'] > recruitment_goal['count'])
            elif args.mode == 'recruit':
                complete = True
            samples.append(sample)
            if len(samples) % 4 == 0:
                guest.shot('social-progress')
            if complete:
                break
        if not complete:
            raise FixtureError('ordinary native social input did not produce the shared result')
        guest.wait('host act reaches guest', lambda: a.goals()['act'] == b.goals()['act'], timeout=15)
        exceptions = [game.call('exc') for game in (host, guest)]
        if any('total faults: 0' not in text for text in exceptions):
            raise FixtureError('native social session logged an exception')
        for game in (host, guest):
            game.record('social-completed')
        result = dict(result='PASS', mode=args.mode, marker=args.marker, profiles=records,
                      before=before, initial_flags=old_flags, samples=samples,
                      host=a.goals(), guest=b.goals(), exceptions=exceptions,
                      source='ordinary NPC mouse click and native ability key input')
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('PASS: ordinary guest social session and authoritative', args.mode)
        return 0
    except (FixtureError, OSError, ValueError) as error:
        print('FAIL:', error)
        for game in (host, guest):
            try:
                game.record('failure')
            except (FixtureError, OSError, ValueError):
                pass
        return 1
    finally:
        for reader in readers:
            reader.close()
        host.save_log()
        guest.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
