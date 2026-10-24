#!/usr/bin/env python3
"""Shared preparation regression. Only operates on recorded isolated lab profiles."""
import argparse
import json
from pathlib import Path
import re
import time

from coop_adventure import Game, FixtureError, verify_lab
from adventure_memory import AdventureMemory
from coop_adventure_preparation import menu

PUZZLE = '0xC2C7B154 0x366A930D 0x408A0000'
TOWN = '0xBF0CE0D0 0x366A930D 0x408A0000'


def click_named(game, name):
    game.call('wait.frames 15 15000')
    was_ready = game.adventure()['ready'] if name == 'prep_ready' else None
    state = game.call('coop.uistate')
    match = re.search(r'^' + name + r' (\d+) (\d+) (\d+) (\d+) 1 ', state, re.M)
    if not match:
        raise FixtureError(f'{game.instance}: missing visible lobby control {name}')
    x1, y1, x2, y2 = map(int, match.groups())
    for _ in range(4):
        game.click((x1+x2)//2, (y1+y2)//2)
        time.sleep(.5)
        state = game.call('coop.uistate')
        if name == 'prep_settings' and not re.search(r'^adventure_back .* 1 ', state, re.M):
            continue
        if name == 'adventure_back' and not re.search(r'^prep_ready .* 1 ', state, re.M):
            continue
        if name == 'prep_ready' and game.adventure()['ready'] == was_ready:
            continue
        return
    raise FixtureError(f'{game.instance}: native button did not accept {name}')


def lobby(game, count, title=None):
    def ready():
        state = game.call('coop.uistate')
        if len(re.findall(r'^captain player=.* stage=5$', state, re.M)) != count:
            return None
        if title and title not in state:
            return None
        return state
    return game.wait('saved captain cards in shared lobby', ready)


def playing(games, records):
    games[0].wait('committed shared adventure', lambda: all(
        'Adventure in progress' in g.adventure()['text'] for g in games))
    for game in games:
        memory = AdventureMemory(game, records[game.instance])
        def active():
            if game.control(0x0445C99D):
                game.click_control(0x0445C99D)
            return memory.goals()['state'] == 3
        game.wait('native adventure intro finishes', active)
        memory.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--instances', default='21,22')
    parser.add_argument('--four-player-only', action='store_true')
    args = parser.parse_args()
    instances = tuple(map(int, args.instances.split(',')))
    records = verify_lab(args.metadata, instances)
    games = [Game(i, args.output, timeout=180) for i in instances]
    host = games[0]
    result = dict(pass_=False, checks=[])
    def check(text):
        result['checks'].append(text)
        print('PASS:', text, flush=True)
    try:
        for game in games:
            game.dismiss_registration()
            game.call('coop.leave')
        host.call('coop.host 47830 AdventureHostLongName')
        for i, game in enumerate(games[1:], 1):
            game.call(f'coop.join 127.0.0.1:47830 AdventureGuest{i}LongName')
        host.wait('full party', lambda: host.adventure()['players'] == len(games))
        host.call('coop.adventure start ' + PUZZLE)
        for game in games:
            state = lobby(game, len(games), "Mr Puzzle's Magical Gates")
            if 'By Maxis' not in state or 'adventure_portrait=1' not in state or 'change_enabled=0' not in state:
                raise FixtureError('stock adventure metadata, artwork or captain lock is missing')
            if re.search(r'^captain .*ready=1', state, re.M):
                raise FixtureError('participant was implicitly ready')
        check('matching adventure card, authored captain portraits, locks and explicit readiness')
        click_named(host, 'prep_settings')
        host.wait_caption('ADVENTURE DIFFICULTY')
        click_named(host, 'adventure_back')
        for game in games[1:]:
            click_named(game, 'prep_ready')
        host.wait('guest readiness visible on host', lambda: len(re.findall(
            r'^captain .*ready=1', host.call('coop.uistate'), re.M)) == len(games)-1)
        if host.adventure()['ready'] or 'Adventure in progress' in host.adventure()['text']:
            raise FixtureError('guest confirmation bypassed host Ready')
        click_named(host, 'prep_ready')
        playing(games, records)
        check('native ready buttons and shared Mr Puzzle launch')
        for act in (1, 2, 3):
            host.call(f'coop.adventure.world act {act}')
            for game in games:
                memory = AdventureMemory(game, records[game.instance])
                game.wait('replicated native act', lambda: memory.goals()['act'] == act)
                mode = memory.pointer(memory.singleton)
                play = memory.pointer(mode+0x78)
                result.setdefault('native_music', []).append(dict(instance=game.instance, act=act,
                    music=memory.pointer(play+0xF4)))
                data = memory.pointer(mode+0x74)
                resource = memory.pointer(data+0x10)
                acts = memory.pointer(resource+0x23C)
                result['native_music'][-1]['authored'] = memory.pointer(acts+act*0x534+0x80)
                memory.close()
        check('stock native act transitions preserve music selection')
        host.call('coop.adventure cancel')
        for game in games:
            menu(game, records[game.instance])
        if not args.four_player_only:
            host.call('coop.adventure start ' + TOWN)
            names = [f'LobbyNew{i}' for i in instances]
            for game, name in zip(games, names):
                game.prepare_captain(name)
            for game in games:
                state = lobby(game, len(games), 'Adventure Town')
                for name in names:
                    if name not in state:
                        raise FixtureError('a saved captain name was not shared')
                if len(re.findall(r'^captain .*portrait=1', state, re.M)) != len(games):
                    raise FixtureError('a captain portrait was not shared')
                game.record('shared-new-captains')
            check('distinct brand-new named captains and portraits on all clients')
            for game in games:
                click_named(game, 'prep_change')
            for game in games:
                game.wait_caption('Choose or Create Your Captain')
            for game in games:
                game.click_control(0x05933FE1)
            for game in games:
                lobby(game, len(games))
            check('simultaneous replacement cancellation retains saved captains')
            click_named(games[-1], 'prep_change')
            games[-1].prepare_captain('LobbyReplacement')
            for game in games:
                state = lobby(game, len(games))
                if 'LobbyReplacement' not in state or re.search(r'^captain .*ready=1', state, re.M):
                    raise FixtureError('replacement did not refresh all cards and clear readiness')
            check('replacement refreshes portraits and requires fresh confirmations')
            click_named(host, 'prep_cancel')
            for game in games:
                menu(game, records[game.instance])
            host.call('coop.adventure start ' + PUZZLE)
            for game in games:
                lobby(game, len(games))
            games[-1].call('coop.leave')
            for game in games:
                menu(game, records[game.instance])
            check('cancel, immediate retry and preparation disconnect restore native menus')
        for game in games:
            if 'total faults: 0' not in game.call('exc'):
                raise FixtureError('native faults were recorded')
        result['pass_'] = True
    except (FixtureError, OSError, ValueError) as error:
        result['error'] = str(error)
    finally:
        args.output.mkdir(parents=True, exist_ok=True)
        for game in games:
            try:
                game.record('lobby-result')
                game.save_log()
            except (OSError, ValueError):
                pass
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    print(json.dumps(result), flush=True)
    return 0 if result['pass_'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
