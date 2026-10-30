#!/usr/bin/env python3
"""Teleporter placement regression on an already-owned hidden editor party.

Uses ordinary UI input and read-only diagnostics. The runner owns private
profiles, deployment and processes. Run with two peers, then four peers.
"""
import argparse
import json
from pathlib import Path
import re
import time

from coop_editor_party import Ownership, PublicGame, parse_history
from coop_adventure_editor import EditorPair, status
from coop_editor_presence import cast_cards


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    owner = Ownership(args.metadata, args.instances, args.dll_sha256)
    games = [PublicGame(i, args.output, 60, time.monotonic()+900) for i in args.instances]
    host = games[0]
    result = dict(passed=False, dll_sha256=args.dll_sha256, checks=[])

    def converged(revision):
        rows = [status(g) for g in games]
        if any(r.get('frozen') for r in rows):
            raise RuntimeError('Shared editor froze: '+repr(rows))
        if any(r.get('phase') != 4 or r.get('gesture') or r.get('revision') != revision for r in rows):
            return False
        hashes = [g.call('coop.editor check').strip() for g in games]
        return hashes[0].startswith('CONVERGED ') and len(set(hashes)) == 1

    def record(label):
        owner.verify_windows()
        rows = []
        for g in games:
            faults = g.call('exc')
            if 'total faults: 0' not in faults:
                raise RuntimeError('Native fault on '+str(g.instance)+': '+faults)
            rows.append(dict(instance=g.instance, state=status(g), visuals=g.call('coop.editor visuals'),
                             history=g.call('coop.editor history'), faults=faults))
            g.record(label)
        result['checks'].append(dict(label=label, participants=rows))
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        print(label, flush=True)

    try:
        for mover in games:
            for attempt in range(3):
                if not mover.caption('Play Offline'):
                    break
                if mover.control(0x050FAD80):
                    mover.click_control(0x050FAD80)
                mover.press('Play Offline')
                mover.call('wait.frames 12 15000')
            mover.call('coop.uistate 0')
            for attempt in range(3):
                if cast_cards(mover, 'Teleporter'):
                    break
                mover.click(219, 42)
                mover.call('move 88 170')
                mover.call('wait.frames 12 15000')
                time.sleep(.3)
            cards = mover.wait('Teleporter cast card', lambda: cast_cards(mover, 'Teleporter'))
            x1, y1, x2, y2 = cards[0]
            sx, sy = (x1+x2)//2, (y1+y2)//2
            revision = status(host)['revision']
            host.wait('shared baseline', lambda: converged(revision))
            history = host.call('coop.editor history')
            try:
                mover.call(f'move {sx} {sy}')
                mover.call(f'button {sx} {sy} down')
                mover.call('wait.frames 10 15000')
                time.sleep(.2)
                for x, y in ((440, 350), (560, 350), (630, 350)):
                    mover.call(f'move {x} {y}')
                    mover.call('wait.frames 10 15000')
                    time.sleep(.15)
                host.wait('remote native teleporter previews', lambda: all(
                    any(line.startswith('ghost=') and ' exit=' in line
                        for line in g.call('coop.editor visuals').splitlines())
                    for g in games if g is not mover), timeout=12)
                if status(host)['revision'] != revision or host.call('coop.editor history') != history:
                    raise RuntimeError('Preview changed revision or history before release')
                record('live-place-'+str(mover.instance))
            finally:
                mover.call('button 630 350 up')
            host.wait('settled teleporter gesture', lambda: converged(revision+1))
            host.wait('both temporary ornaments retired', lambda: all(
                'ghost=' not in g.call('coop.editor visuals') for g in games))
            if len(parse_history(host.call('coop.editor history'))) != len(parse_history(history))+1:
                raise RuntimeError('Placement did not create exactly one Undo item')
            EditorPair.hotkey(mover, 'z')
            host.wait('placement Undo', lambda: converged(revision+2))
            EditorPair.hotkey(mover, 'y')
            host.wait('placement Redo', lambda: converged(revision+3))
            EditorPair.hotkey(mover, 'z')
            host.wait('restore authored teleporter roster', lambda: converged(revision+4))
            record('settled-place-'+str(mover.instance))
        result['passed'] = True
    except Exception as error:
        result['error'] = repr(error)
        raise
    finally:
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        for g in games:
            g.save_log()
        owner.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--instances', nargs='+', type=int, default=[91, 92])
    parser.add_argument('--dll-sha256')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not args.execute:
        print(json.dumps(dict(prepared=True, launches_games=False)))
        return
    if not args.metadata or not args.output or not re.fullmatch('[0-9a-f]{64}', args.dll_sha256 or '') or not 2 <= len(args.instances) <= 4:
        parser.error('Exact metadata, DLL SHA, output and two to four owned instances required')
    run(args)


if __name__ == '__main__':
    main()
