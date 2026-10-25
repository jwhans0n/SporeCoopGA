#!/usr/bin/env python3
"""Verify native automatic recovery in an already committed isolated GA pair."""
import argparse
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_combat import hp, hit, expect_health, check


def position(game):
    match = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', game.call('coop.avatar'))
    if not match:
        raise FixtureError('captain position unavailable')
    return tuple(map(float, match.groups()))


def run(host, guest):
    state = host.adventure()
    check('Adventure in progress' in state['text'] and 'death=2' in state['text'],
          'a committed automatic-recovery adventure is required')
    # Keep the rescuer away so proximity revival cannot mask the automatic timer.
    guest.call('coop.adventure.world near 0 1')
    guest.wait('separated captains', lambda: sum((a-b)**2 for a,b in zip(position(host), position(guest))) > 10000, timeout=10)
    maximum = hp(host)[1]
    hit(host, 0, 'env', 100000, 10)
    expect_health(host, maximum)
    origin = position(host)
    hit(host, 0, 'env', 100000)
    expect_health(host, 1)
    started = time.monotonic()
    host.record('automatic-downed')
    while time.monotonic() - started < 27:
        check(hp(host)[0] == 1, 'automatic revival occurred before its 30-second delay')
        time.sleep(.4)
    expect_health(host, maximum * .5, timeout=12)
    recovered = time.monotonic() - started
    destination = position(host)
    check(sum((a-b)**2 for a,b in zip(origin, destination)) < 1, 'automatic revival teleported the captain')
    for game in (host, guest):
        check('total faults: 0' in game.call('exc'), 'native recovery logged a fault')
    host.record('automatically-recovered')
    return dict(run=state['run'], elapsed_wall_seconds=recovered, before=origin, after=destination,
                health=hp(host), checks=['30-second recovery', 'half health', 'downed location', 'no native faults'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, default=21)
    parser.add_argument('--guest', type=int, default=22)
    parser.add_argument('--metadata', type=Path, default=ROOT / 'out/windows-lab/runtime-bootstrap-3/processes.json')
    parser.add_argument('--out', type=Path, default=ROOT / 'out/windows-lab/adventure-recovery')
    args = parser.parse_args()
    profiles = verify_lab(args.metadata, (args.host, args.guest))
    host, guest = Game(args.host, args.out), Game(args.guest, args.out)
    try:
        result = run(host, guest)
        result.update(result='PASS', profiles=profiles)
        (args.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('PASS: native automatic recovery')
        return 0
    except (FixtureError, OSError, ValueError) as error:
        print('FAIL:', error)
        return 1
    finally:
        host.save_log(); guest.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
