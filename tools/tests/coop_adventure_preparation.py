#!/usr/bin/env python3
"""Native two-player preparation recovery regression, restricted to a recorded lab.

Supply an adventure already present in the isolated host profile. This test never
copies a profile or writes to the user's installation. The runner owns both games.
"""
import argparse
import json
from pathlib import Path
import re
import time

from coop_adventure import Game, FixtureError, native_key, verify_lab
from adventure_memory import AdventureMemory


def menu(game, record):
    game.wait('cancelled preparation returns to galaxy',
              lambda: game.menu() and not game.adventure()['active'])
    game.wait('native Galactic Adventures button', lambda: game.control(0x070D74E7))
    memory = AdventureMemory(game, record)
    try:
        result = game.call('call App::IStateManager::Get cdecl')
        manager = int(re.search(r'eax=0x([0-9a-fA-F]+)', result).group(1), 16)
        result = game.call('call Sporepedia::AssetBrowser::Get cdecl')
        browser = int(re.search(r'eax=0x([0-9a-fA-F]+)', result).group(1), 16)
        if memory.read(browser + 0x1C, 1) != b'\0':
            raise FixtureError(f'{game.instance}: owned browser is still active')
        state = memory.pointer(manager + 0x88)
        if state != 0x02CCDA9C:
            raise FixtureError(f'{game.instance}: galaxy application state is {state:08X}')
    finally:
        memory.close()
    if 'total faults: 0' not in game.call('exc'):
        raise FixtureError(f'{game.instance}: native exception during recovery')


def start(host, guest, key):
    host.call('coop.adventure start ' + ' '.join(f'0x{x:08X}' for x in key))
    for game in (host, guest):
        game.wait_caption('Choose or Create Your Captain')
        state = game.call('coop.uistate')
        if re.search(r"row .*'ready'", state):
            raise FixtureError('unfinished captain was displayed as ready')


def cancel(host, games, records):
    host.call('coop.adventure cancel')
    for game in games:
        menu(game, records[game.instance])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--key', type=native_key, default=native_key('Town'))
    parser.add_argument('--host', type=int, default=21)
    parser.add_argument('--guest', type=int, default=22)
    parser.add_argument('--port', type=int, default=47830)
    parser.add_argument('--play', action='store_true', help='finish two brand-new captains after recovery checks')
    args = parser.parse_args()
    records = verify_lab(args.metadata, (args.host, args.guest))
    games = [Game(i, args.output, timeout=180) for i in (args.host, args.guest)]
    host, guest = games
    result = dict(pass_=False, checks=[])
    try:
        for game in games:
            game.dismiss_registration()
            game.call('coop.leave')
        host.call(f'coop.host {args.port} PreparationHost')
        guest.call(f'coop.join 127.0.0.1:{args.port} PreparationGuest')
        host.wait('two-member party', lambda: host.adventure()['players'] == 2)

        start(host, guest, args.key)
        cancel(host, games, records)
        result['checks'].append('selection cancellation and immediate retry')
        print(result['checks'][-1], flush=True)

        start(host, guest, args.key)
        guest.prepare_captain('CancelledName', save=False)
        cancel(host, games, records)
        result['checks'].append('naming cancellation restores app state and mode')
        print(result['checks'][-1], flush=True)

        start(host, guest, args.key)
        guest.click_control(0x07CA4119)
        guest.wait('owned native shopper', lambda: guest.control(0x53C6DC80))
        cancel(host, games, records)
        result['checks'].append('owned shopper closes and retry remains possible')
        print(result['checks'][-1], flush=True)

        start(host, guest, args.key)
        guest.prepare_captain('CancelledSave', save=False)
        guest.click_control(0x05951103)
        cancel(host, games, records)
        result['checks'].append('saving animation cancellation and stale native work')
        print(result['checks'][-1], flush=True)

        start(host, guest, args.key)
        guest.prepare_captain('DisconnectName', save=False)
        guest.call('coop.leave')
        for game in games:
            menu(game, records[game.instance])
        result['checks'].append('disconnect during naming restores both menus')
        print(result['checks'][-1], flush=True)
        guest.call(f'coop.join 127.0.0.1:{args.port} PreparationGuest')
        host.wait('rejoined party', lambda: host.adventure()['players'] == 2)
        start(host, guest, args.key)
        if args.play:
            names = [f'PreparationNew{i}' for i in (args.host, args.guest)]
            for game, name in zip(games, names):
                game.prepare_captain(name)
            guest.call('coop.adventure ready 1')
            host.wait('same committed adventure', lambda: all(
                'Adventure in progress' in game.adventure()['text'] for game in games))
            keys = [game.captain_key() for game in games]
            if keys[0] == keys[1]:
                raise FixtureError('new captains were not saved under distinct keys')
            result.update(names=names, captains=keys, run=host.adventure()['run'])
            result['checks'].append('distinct named saved captains enter the same adventure')
        else:
            cancel(host, games, records)
        result['pass_'] = True
    except (FixtureError, OSError, ValueError) as error:
        result['error'] = str(error)
    finally:
        args.output.mkdir(parents=True, exist_ok=True)
        for game in games:
            try:
                game.record('preparation-result')
                game.save_log()
                if game.console.sock:
                    game.console.sock.close()
            except (OSError, ValueError):
                pass
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print('PASS' if result['pass_'] else 'FAIL', json.dumps(result), flush=True)
    return 0 if result['pass_'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
