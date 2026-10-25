#!/usr/bin/env python3
"""Exercise a controlled native co-op success and original-captain XP saving.

This checks results/progression plumbing, not completion of the authored goals.
"""
import argparse
import json
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
from coop_adventure import Game, FixtureError, verify_lab


def xp(game, key=None):
    command = 'coop.adventure xp'
    if key:
        command += ' ' + ' '.join(f'0x{key[name]:08X}' for name in ('instance', 'type', 'group'))
    text = game.call(command)
    match = re.search(r'xp=(-?\d+) level=(-?\d+)', text)
    if not match:
        raise FixtureError('native captain XP unavailable: ' + text)
    return dict(xp=int(match.group(1)), level=int(match.group(2)))


def run(host, guest, expected, resume=False):
    games = (host, guest)
    keys = [game.captain_key() for game in games]
    before = [xp(game, key) for game, key in zip(games, keys)]
    if not resume:
        host.call('coop.adventure.world finish success')
    for game in games:
        def results_ready():
            if game.caption('Mission Complete!'):
                return True
            # The ending dialog can appear a few frames after SetState returns.
            if game.control(0x0445C99D):
                game.click_control(0x0445C99D)
            return False
        game.wait('native mission results after the ending dialog', results_ready)
        if not game.caption('Done') and game.caption('Choose a Reward'):
            # Initial test captains unlock their first reward here. The native
            # results dialog hides Done until a reward is selected. This point
            # is the first reward in the measured 1024x768 isolated fixture.
            for row in range(8):
                game.modal_click(631, 235 + row * 37)
                if game.caption('Done'):
                    break
        game.wait_caption('Done')
        status = game.adventure()['text']
        if 'applied=5' not in status or 'suppressed score submissions=1' not in status:
            raise FixtureError('native result or score suppression mismatch: ' + status)
        game.record('controlled-success-results')
    # F16460 saves absolute native target XP when Done/Replay accepts the result.
    for game in games:
        game.press('Done')
        game.wait('native Done returns to menu', game.menu, timeout=20)
    after = [xp(game, key) for game, key in zip(games, keys)]
    for game, old, new in zip(games, before, after):
        if new['xp'] - old['xp'] != expected:
            raise FixtureError(f'instance {game.instance}: XP {old} -> {new}, expected +{expected}')
        if 'total faults: 0' not in game.call('exc'):
            raise FixtureError('native result/save logged an exception')
    return dict(controlled_result=True, complete_adventure_playthrough=False,
                keys=keys, before=before, after=after, expected_award=expected,
                checks=['native success panel', 'single score suppression', 'original captain XP', 'Done returns to menu'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, default=21)
    parser.add_argument('--guest', type=int, default=22)
    parser.add_argument('--expected-xp', type=int, default=100)
    parser.add_argument('--resume', action='store_true', help='continue a controlled success already at its ending/results')
    parser.add_argument('--metadata', type=Path, default=ROOT / 'out/windows-lab/runtime-bootstrap-3/processes.json')
    parser.add_argument('--out', type=Path, default=ROOT / 'out/windows-lab/adventure-results')
    args = parser.parse_args()
    profiles = verify_lab(args.metadata, (args.host, args.guest))
    host, guest = Game(args.host, args.out, timeout=30), Game(args.guest, args.out, timeout=30)
    try:
        result = run(host, guest, args.expected_xp, args.resume)
        result.update(result='PASS', profiles=profiles)
        (args.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('PASS: controlled native success, captain XP and Done')
        return 0
    except (FixtureError, OSError, ValueError) as error:
        print('FAIL:', error)
        for game in (host, guest):
            try: game.record('failure')
            except (OSError, ValueError, FixtureError): pass
        return 1
    finally:
        host.save_log(); guest.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
