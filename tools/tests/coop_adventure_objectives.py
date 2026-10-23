#!/usr/bin/env python3
"""Verify guest native objective callbacks and shared act transitions in a lab pair."""
import argparse
import json
import re
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'tests'))
from coop_adventure import Game, FixtureError, verify_lab
from adventure_memory import AdventureMemory


def run(host, guest, host_memory, guest_memory):
    before = host_memory.goals()
    other = guest_memory.goals()
    if before['act'] != other['act']:
        raise FixtureError('captains are in different acts')
    goal = next((g for g in before['goals'] if not g['complete'] and g['type'] in (1, 2, 4, 5) and g['targets']), None)
    if not goal:
        raise FixtureError('fixture requires an active move/talk/befriend/ally objective')
    marker = goal['targets'][0]
    guest.call(f'coop.adventure.world near {marker} 1')
    if goal['type'] != 1:
        def arrived():
            local = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', guest.call('coop.avatar'))
            remote = re.search(r'remote 1 .*?pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', host.call('coop.status'))
            return (local and remote and
                    sum((float(a) - float(b)) ** 2 for a, b in zip(local.groups(), remote.groups())) < 16)
        guest.wait('host receives the guest position before its social completion', arrived, timeout=10)
        # This emits the real native completion callback and tests its validated
        # network replay; it does not test the social minigame/conversation UI.
        guest.call(f'coop.adventure.world social {goal["index"]} {marker}')

    def advanced():
        current = host_memory.goals()
        if current['act'] != before['act']:
            return current
        updated = current['goals'][goal['index']]
        return current if updated['complete'] or updated['count'] > goal['count'] else None

    after = host.wait('guest contribution on authoritative host', advanced, timeout=15)
    def replicated():
        # Completion can be observed one frame before the native host changes
        # acts. Compare with its current state instead of waiting for that
        # short-lived completed vector to reappear on the guest.
        authoritative = host_memory.goals()
        current = guest_memory.goals()
        if current['act'] != authoritative['act']:
            return None
        expected = [(g['count'], g['complete']) for g in authoritative['goals']]
        actual = [(g['count'], g['complete']) for g in current['goals']]
        return current if expected == actual else None
    received = guest.wait('shared act and goal counters', replicated, timeout=15)
    for game in (host, guest):
        if 'total faults: 0' not in game.call('exc'):
            raise FixtureError('native act transition logged an exception')
        if not re.search(r'together\s+\d+\s+1\b', game.call('coop.status')):
            raise FixtureError('captain replica did not survive the act transition')
    return dict(before=before, contribution=goal, after=after, guest=received)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, default=21)
    parser.add_argument('--guest', type=int, default=22)
    parser.add_argument('--metadata', type=Path, default=ROOT / 'out/windows-lab/runtime-bootstrap-3/processes.json')
    parser.add_argument('--out', type=Path, default=ROOT / 'out/windows-lab/adventure-objectives')
    args = parser.parse_args()
    records = verify_lab(args.metadata, (args.host, args.guest))
    host, guest = Game(args.host, args.out), Game(args.guest, args.out)
    readers = []
    try:
        readers = [AdventureMemory(host, records[args.host]), AdventureMemory(guest, records[args.guest])]
        result = run(host, guest, *readers)
        result.update(result='PASS', profiles=records)
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('PASS: guest native objective and shared act transition')
        return 0
    except (FixtureError, OSError, ValueError) as exc:
        print('FAIL:', exc)
        for game in (host, guest):
            try:
                game.record('failure')
            except (OSError, ValueError):
                pass
        return 1
    finally:
        for reader in readers:
            reader.close()
        host.save_log(); guest.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
