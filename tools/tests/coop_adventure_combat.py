#!/usr/bin/env python3
"""Exercise native captain hit forwarding and revival in an existing isolated GA pair."""
import argparse
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'tests'))
from coop_adventure import Game, FixtureError, verify_lab


def hp(game):
    text = game.call('coop.avatar')
    match = re.search(r'hp=([\d.]+)/([\d.]+)', text)
    if not match:
        raise FixtureError('captain health unavailable: ' + text)
    return tuple(map(float, match.groups()))


def expect_health(game, expected, timeout=3):
    def matches():
        current = hp(game)
        return current if abs(current[0] - expected) < .11 else None
    return game.wait(f'health {expected}', matches, timeout=timeout)


def hit(game, victim, source, amount, kind=2):
    game.call(f'coop.adventure.world hit {victim} {source} {amount} {kind}')


def check(condition, message):
    if not condition:
        raise FixtureError(message)


def run(host, guest, friendly_fire):
    a, b = host.adventure(), guest.adventure()
    check(a['run'] == b['run'] != 0 and a['players'] == b['players'] == 2,
          'two captains must be in the same committed attempt')
    for game in (host, guest):
        check('Adventure in progress' in game.adventure()['text'], 'attempt has not committed')
        check(f'friendly_fire={friendly_fire}' in game.adventure()['text'], 'wrong friendly fire setting')
        check('death=0' in game.adventure()['text'], 'this case requires manual proximity revival')
    # Difficulty is immutable after Play, including for the host.
    for game in (host, guest):
        response = game.console.call('coop.adventure friendlyfire 100')
        game.events.append({'time': time.time(), 'command': 'coop.adventure friendlyfire 100', **response})
        check(not response.get('ok'), 'running difficulty changed')

    maxima = [hp(game)[1] for game in (host, guest)]
    hit(host, 0, 'env', 100000, 10)
    hit(guest, 1, 'env', 100000, 10)
    expect_health(host, maxima[0]); expect_health(guest, maxima[1])

    # Both attack origins and several native damage categories reach the same policy.
    for category in (0, 2, 5):
        for origin, victim_game, victim_id, source_id in ((host, guest, 1, 0), (guest, host, 0, 1)):
            before = hp(victim_game)[0]
            hit(origin, victim_id, source_id, 20, category)
            expect_health(victim_game, before - 20 * friendly_fire / 100)
            time.sleep(.25)

    # Environmental hits originate on the victim's machine; healing uses native type10.
    hit(host, 0, 'env', 10)
    expect_health(host, maxima[0] - 60 * friendly_fire / 100 - 10)
    hit(host, 0, 'env', 100000, 10)
    hit(guest, 1, 'env', 100000, 10)
    expect_health(host, maxima[0]); expect_health(guest, maxima[1])

    # The fixture pair spawns together. Damage to the rescuer must reset its hold.
    hit(host, 0, 'env', 100000)
    expect_health(host, 1)
    time.sleep(.9)
    hit(guest, 1, 'env', 5)
    interrupted = time.monotonic()
    expect_health(guest, maxima[1] - 5)
    while time.monotonic() - interrupted < 2.35:
        check(hp(host)[0] == 1, 'revival did not reset when the rescuer took damage')
        time.sleep(.2)
    expect_health(host, maxima[0] * .5, timeout=3)
    # The immediate post-revival hit is ignored; a later hit applies normally.
    hit(host, 0, 'env', 10)
    expect_health(host, maxima[0] * .5)
    time.sleep(2.4)
    hit(host, 0, 'env', 10)
    expect_health(host, maxima[0] * .5 - 10)
    for game in (host, guest):
        check('total faults: 0' in game.call('exc'), 'native exception occurred')
    return {'run': a['run'], 'friendly_fire': friendly_fire, 'health': [hp(host), hp(guest)],
            'checks': ['difficulty lock', 'both attack origins', 'damage categories',
                       'environment', 'healing', 'interrupted revive', 'half health revive', 'revival grace']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, default=21)
    parser.add_argument('--guest', type=int, default=22)
    parser.add_argument('--friendly-fire', type=int, choices=(0, 25, 50, 100), default=0)
    parser.add_argument('--metadata', type=Path, default=ROOT / 'out/windows-lab/runtime-bootstrap-3/processes.json')
    parser.add_argument('--out', type=Path, default=ROOT / 'out/windows-lab/adventure-combat')
    args = parser.parse_args()
    profiles = verify_lab(args.metadata, (args.host, args.guest))
    host, guest = Game(args.host, args.out), Game(args.guest, args.out)
    try:
        result = run(host, guest, args.friendly_fire)
        result.update(result='PASS', profiles=profiles)
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('PASS: native captain combat and proximity revival')
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
        host.save_log(); guest.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
