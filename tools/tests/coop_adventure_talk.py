#!/usr/bin/env python3
"""Complete an ordinary native conversation in an existing isolated GA pair.

The caller supplies a visible NPC body coordinate after positioning the captain.
Only mouse clicks and native Next/Finished controls complete the conversation.
No objective callback or result is injected.
"""
import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
from coop_adventure import Game, FixtureError, verify_lab
from adventure_memory import AdventureMemory


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, default=21)
    parser.add_argument('--guest', type=int, default=22)
    parser.add_argument('--x', type=int, required=True)
    parser.add_argument('--y', type=int, required=True)
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
        if before['act'] != b.goals()['act']:
            raise FixtureError('captains are in different acts')
        pages = []
        guest.click(args.x, args.y)
        guest.wait('native conversation cinematic opens',
                   lambda: guest.caption('letterbox_top'), timeout=12)
        deadline = time.monotonic() + 90
        finished = False
        while time.monotonic() < deadline:
            if guest.control(0x0445C99D):
                pages.append(guest.call('coop.ui tree 8'))
                guest.record('conversation-final-page')
                guest.click_control(0x0445C99D)
                finished = True
                break
            if guest.control(0x062EF353):
                pages.append(guest.call('coop.ui tree 8'))
                guest.click_control(0x062EF353)
            else:
                time.sleep(.3)
        if not finished:
            raise FixtureError('native conversation did not reach its Finished control')
        def advanced():
            current = a.goals()
            return current if current['act'] != before['act'] or any(
                new['count'] > old['count'] or new['complete'] and not old['complete']
                for old, new in zip(before['goals'], current['goals'])) else None
        host.wait('native guest conversation contributes to host goals', advanced, timeout=20)
        def replicated():
            x, y = a.goals(), b.goals()
            return y if x['act'] == y['act'] and [
                (g['count'], g['complete']) for g in x['goals']] == [
                (g['count'], g['complete']) for g in y['goals']] else None
        guest.wait('conversation progress replicated', replicated, timeout=20)
        exceptions = [game.call('exc') for game in (host, guest)]
        if any('total faults: 0' not in value for value in exceptions):
            raise FixtureError('conversation logged a native exception')
        result = dict(result='PASS', profiles=records, before=before, host=a.goals(),
                      guest=b.goals(), pages=pages, exceptions=exceptions,
                      source='ordinary NPC mouse click and native Next/Finished controls')
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('PASS: ordinary guest conversation and shared objective progress')
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
