#!/usr/bin/env python3
"""Cancel and disconnect during shared scenes in a recorded hidden Windows lab."""
import argparse
import json
from pathlib import Path
import time

from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_lobby import PUZZLE, lobby, click_named, playing
from coop_adventure_preparation import menu
from adventure_memory import AdventureMemory


def finish_dialog(game):
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        if game.control(0x0445C99D):
            game.click_control(0x0445C99D)
            return
        if game.control(0x062EF353):
            game.click_control(0x062EF353)
        else:
            time.sleep(.2)
    raise FixtureError('native conversation did not reach Finished')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    records = verify_lab(args.metadata, (21, 22))
    games = [Game(i, args.output, timeout=120) for i in (21, 22)]
    host, guest = games
    memories = [AdventureMemory(g, records[g.instance]) for g in games]
    checks = []

    def start():
        host.call('coop.adventure start ' + PUZZLE)
        for game in games:
            lobby(game, 2)
            click_named(game, 'prep_ready')
        host.wait('both clients loaded', lambda: all(
            'Adventure in progress' in g.adventure()['text'] for g in games))
        for game in games:
            game.wait('intro Continue', lambda game=game: game.control(0x0445C99D))

    def cancel():
        host.call('coop.adventure cancel')
        for game in games:
            menu(game, records[game.instance])

    def passed(text):
        checks.append(text)
        print('PASS:', text, flush=True)

    try:
        cancel()
        start()
        guest.click_control(0x0445C99D)
        guest.wait('guest waiting at beam-down', lambda: 'confirmations: 1/2' in guest.adventure()['text'])
        cancel()
        passed('cancelling a confirmed intro restores both native menus')
        start()
        playing(games, records)
        guest.call(f'coop.adventure.world near 158 {guest.player_id()}')
        time.sleep(2)
        guest.click(557, 340)
        for game in games:
            game.wait('party conversation opens', lambda game=game: game.caption('letterbox_top'))
        finish_dialog(guest)
        host.wait('guest waiting for group conversation', lambda:
                  'Conversation confirmations: 1/2' in host.adventure()['text'])
        cancel()
        passed('cancelling a partly confirmed conversation restores both native menus')
        start()
        host.click_control(0x0445C99D)
        host.wait('host waiting at intro', lambda: 'confirmations: 1/2' in host.adventure()['text'])
        guest.call('coop.leave')
        menu(guest, records[guest.instance])
        host.wait('departed player removed from group barrier', lambda: memories[0].goals()['state'] == 3)
        host.call('coop.adventure cancel')
        menu(host, records[host.instance])
        passed('intro disconnect recovers guest menu and releases remaining host')
        guest.call('coop.join 127.0.0.1:47830 RecoveryGuest')
        host.wait('party reconnected', lambda: host.adventure()['players'] == 2)
        start()
        playing(games, records)
        guest.call(f'coop.adventure.world near 158 {guest.player_id()}')
        time.sleep(2)
        guest.click(557, 340)
        for game in games:
            game.wait('conversation after retry', lambda game=game: game.caption('letterbox_top'))
        finish_dialog(host)
        host.wait('host waiting for conversation', lambda: 'Conversation confirmations: 1/2' in host.adventure()['text'])
        guest.call('coop.leave')
        menu(guest, records[guest.instance])
        host.wait('remaining player conversation completes', lambda: memories[0].goals()['act'] == 1)
        host.call('coop.adventure cancel')
        menu(host, records[host.instance])
        passed('conversation disconnect releases the remaining participant exactly once')
        guest.call('coop.join 127.0.0.1:47830 RecoveryGuest')
        host.wait('party reconnects again', lambda: host.adventure()['players'] == 2)
        start()
        playing(games, records)
        for game in games:
            if 'total faults: 0' not in game.call('exc'):
                raise FixtureError('native fault during shared scene recovery')
        passed('same adventure retries after cancellations and disconnects without restarting')
        cancel()
        (args.output/'result.json').write_text(json.dumps(dict(pass_=True, checks=checks), indent=2)+'\n')
        return 0
    except (FixtureError, OSError, ValueError) as error:
        print('FAIL:', error, flush=True)
        for game in games:
            try:
                game.record('failure')
            except (FixtureError, OSError, ValueError):
                pass
        return 1
    finally:
        for memory in memories:
            memory.close()
        for game in games:
            game.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
