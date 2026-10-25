#!/usr/bin/env python3
"""Verify controlled co-op success and normal native captain progression saves.

This exercises result plumbing, not a full authored adventure playthrough. Host
Done must finish an unconfirmed guest ending, preserve required reward choices,
and save both original captains through the ordinary native results UI once.
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
from coop_adventure_preparation import menu
from coop_adventure_lobby import TOWN, lobby, click_named, playing


def xp(game, key=None):
    command = 'coop.adventure xp'
    if key:
        command += ' ' + ' '.join(f'0x{key[name]:08X}' for name in ('instance', 'type', 'group'))
    text = game.call(command)
    match = re.search(r'xp=(-?\d+) level=(-?\d+)', text)
    if not match:
        raise FixtureError('native captain XP unavailable: ' + text)
    return dict(xp=int(match.group(1)), level=int(match.group(2)))


def score_submissions(game):
    match = re.search(r'suppressed score submissions=(\d+)', game.adventure()['text'])
    if not match:
        raise FixtureError('native score suppression counter unavailable')
    return int(match[1])


def choose_reward(game):
    """Choose only through the ordinary visible native reward UI."""
    if not game.caption('Choose a Reward') or game.caption('Done'):
        return False
    for row in range(8):
        if game.menu():
            return True
        grid = game.control(0x52DF67AF)
        if not grid or not grid[4]:
            raise FixtureError('visible enabled native reward grid is unavailable')
        # The native custom-drawn reward grid exposes one window rather than
        # eight child buttons. Click one category's first item inside that real
        # grid, then let its selection/XP animation settle before another choice.
        game.click(grid[0]+62, grid[1]+26+row*37)
        # A pending party return accepts Done immediately once reward selection
        # permits it. Stop if that normal save has already returned to galaxy.
        try:
            game.wait('native reward selection settles to Done or party menu',
                      lambda: game.menu() or game.caption('Done'), timeout=8)
            return True
        except FixtureError:
            if not game.caption('Choose a Reward'):
                raise
    raise FixtureError('native reward selection did not expose Done')


def run(host, guest, expected, resume=False):
    games = (host, guest)
    keys = [game.captain_key() for game in games]
    before = [xp(game, key) for game, key in zip(games, keys)]
    score_before = [score_submissions(game) for game in games]
    rewards = []
    if not resume:
        host.call('coop.adventure.world finish success')

    def host_results_ready():
        if host.caption('Mission Complete!'):
            return True
        # Confirm only the host. The guest ending remains unconfirmed.
        button = host.control(0x0445C99D)
        if button and button[4]:
            host.click((button[0]+button[2])//2, (button[1]+button[3])//2)
        return False

    host.wait('native host mission results', host_results_ready)
    def host_done_ready():
        if host.caption('Done'):
            return True
        if choose_reward(host):
            rewards.append(dict(instance=host.instance, phase='host-before-Done'))
        return host.caption('Done')
    host.wait('native host reward selection and Done', host_done_ready)
    guest_cinematic = guest.control(0x0445C99D)
    if not guest_cinematic or not guest_cinematic[4] or guest.caption('Done'):
        raise FixtureError('guest ending must remain unconfirmed before host Done')
    for game in games:
        if 'applied=5' not in game.adventure()['text']:
            raise FixtureError('native successful result was not applied')
        game.record('controlled-success-before-host-Done')

    # Native Done saves the host. The return adapter must end the guest scene
    # and invoke its normal Done path once. Never skip mandatory reward phase 4.
    host.press('Done')
    reward_wait_observed = False
    deadline = time.monotonic()+45
    while time.monotonic() < deadline and not all(game.menu() for game in games):
        if guest.caption('Choose a Reward') and not guest.caption('Done'):
            reward_wait_observed = True
            guest.record('guest-native-reward-after-host-Done')
            if choose_reward(guest):
                rewards.append(dict(instance=guest.instance, phase='guest-after-host-Done'))
        time.sleep(.2)
    for game in games:
        game.wait('host Done returns both players after native saves', game.menu, timeout=5)
    after = [xp(game, key) for game, key in zip(games, keys)]
    score_after = [score_submissions(game) for game in games]
    for game, old, new in zip(games, before, after):
        if new['xp']-old['xp'] != expected:
            raise FixtureError(f'instance {game.instance}: XP {old} -> {new}, expected +{expected}')
        if 'total faults: 0' not in game.call('exc'):
            raise FixtureError('native result/save logged an exception')
    if not resume and any(new-old != 1 for old, new in zip(score_before, score_after)):
        raise FixtureError(f'native score suppression did not increase once: {score_before} -> {score_after}')
    return dict(controlled_result=True, complete_adventure_playthrough=False,
                keys=keys, before=before, after=after, expected_award=expected,
                score_before=score_before, score_after=score_after, rewards=rewards,
                guest_cinematic_before_host_done=guest_cinematic,
                guest_reward_wait_observed=reward_wait_observed,
                native_status_after=[game.adventure()['text'] for game in games],
                checks=['native success panel', 'relative single score suppression',
                        'original captain XP', 'host Done finishes guest cinematic and saves once',
                        'native mandatory reward selection', 'party Done returns to menu'])


def launch_town(games, records, captains, port, create=False):
    host, guest = games
    for game in games:
        menu(game, records[game.instance])
        game.call('coop.leave')
    host.call(f'coop.host {port} ProgressHost')
    guest.call(f'coop.join 127.0.0.1:{port} ProgressGuest')
    host.wait('two progression-test players', lambda: host.adventure()['players'] == 2)
    host.call('coop.adventure start '+TOWN)
    for game, name in zip(games, captains):
        game.prepare_captain(name, create=create)
    for game in games:
        lobby(game, 2)
    for game in (guest, host):
        click_named(game, 'prep_ready')
    playing(games, records)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, default=21)
    parser.add_argument('--guest', type=int, default=22)
    parser.add_argument('--expected-xp', type=int, default=100)
    parser.add_argument('--resume', action='store_true', help='continue a controlled success already at its ending/results')
    parser.add_argument('--launch-town', action='store_true')
    parser.add_argument('--captains', default='PreparationNew41,PreparationNew42')
    parser.add_argument('--new-captains', action='store_true')
    parser.add_argument('--port', type=int, default=47853)
    parser.add_argument('--metadata', type=Path, default=ROOT / 'out/windows-lab/runtime-bootstrap-3/processes.json')
    parser.add_argument('--out', type=Path, default=ROOT / 'out/windows-lab/adventure-results')
    args = parser.parse_args()
    profiles = verify_lab(args.metadata, (args.host, args.guest))
    host, guest = Game(args.host, args.out, timeout=180), Game(args.guest, args.out, timeout=180)
    try:
        if args.launch_town:
            captains = args.captains.split(',')
            if len(captains) != 2 or args.resume:
                raise FixtureError('Town launch requires two saved captain names without --resume')
            launch_town((host, guest), profiles, captains, args.port, args.new_captains)
        result = run(host, guest, args.expected_xp, args.resume)
        for game in (host, guest):
            menu(game, profiles[game.instance])
        result.update(result='PASS', profiles=profiles)
        (args.out/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print('PASS: controlled native success, original captain XP and host-only Done')
        return 0
    except (FixtureError, OSError, ValueError) as error:
        print('FAIL:', error)
        (args.out/'failure.json').write_text(json.dumps(dict(result='FAIL', error=str(error)), indent=2)+'\n', encoding='utf-8')
        for game in (host, guest):
            try:
                game.record('failure')
            except (OSError, ValueError, FixtureError):
                pass
        return 1
    finally:
        host.save_log()
        guest.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
