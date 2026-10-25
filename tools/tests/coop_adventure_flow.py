#!/usr/bin/env python3
"""Exercise shared intro confirmations using recorded, hidden Windows lab games."""
import argparse
import json
from pathlib import Path
import re
import time

from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_lobby import PUZZLE, lobby, click_named
from adventure_memory import AdventureMemory


def confirmation_hud(game, count, kind, local_ready=False):
    text = game.call('coop.adventure.hud')
    if 'confirmation_hud=1' not in text or f"text='{kind} " not in text:
        raise FixtureError('native bottom-bar confirmations unavailable: ' + text)
    if not game.call('coop.uistate').startswith('open=0 '):
        raise FixtureError('confirmation unexpectedly opened the full co-op dialog')
    check = game.control(0x0445C99D)
    bar = game.control(0x045023D0)
    rows = re.findall(r'^confirmation_player \d+ ([\d.-]+) ([\d.-]+) ([\d.-]+) ([\d.-]+) ', text, re.M)
    if not check or not bar or len(rows) != count:
        raise FixtureError('native confirmation controls/participants missing: ' + text)
    for row in rows:
        x1, y1, x2, y2 = map(float, row)
        if not (bar[0] <= x1 < x2 < check[0] and bar[1] <= y1 < y2 <= bar[3]):
            raise FixtureError('confirmation label escaped bottom bar or overlaps the check: ' + text)
    if local_ready and check[4]:
        raise FixtureError('local confirmation left the native check enabled')
    return text


def check_intro(games, memories, guest_first=True):
    host = games[0]
    host.wait('shared load commit', lambda: all('Adventure in progress' in g.adventure()['text'] for g in games))
    for game in games:
        game.wait('intro Continue control', lambda: game.control(0x0445C99D))
        confirmation_hud(game, len(games), 'Beam-down')
    before = [m.goals() for m in memories]
    order=games[1:]+games[:1] if guest_first else games
    first,last=order[0],order[-1]
    first.click_control(0x0445C99D)
    first.wait('one shared intro confirmation', lambda: f'confirmations: 1/{len(games)}' in first.adventure()['text'])
    time.sleep(3)
    held = [m.goals() for m in memories]
    if any(s['state'] == 3 or s['elapsed'] != before[i]['elapsed'] or s['remaining'] != before[i]['remaining'] for i, s in enumerate(held)):
        raise FixtureError('intro confirmation allowed gameplay or clocks before every player confirmed')
    first.record('waiting-for-intro')
    for game in games:
        confirmation_hud(game, len(games), 'Beam-down', game is first)
    for game in order[1:-1]:game.click_control(0x0445C99D)
    first.wait('all but final confirmation',lambda:f'confirmations: {len(games)-1}/{len(games)}'in first.adventure()['text'])
    if any(m.goals()['state']==3 for m in memories):raise FixtureError('intro released before its final participant')
    last.click_control(0x0445C99D)
    host.wait('both native playing states', lambda: all(m.goals()['state'] == 3 for m in memories))
    host.wait('host adventure clock advances', lambda: memories[0].goals()['elapsed'] > held[0]['elapsed'])
    host.wait('reciprocal current captain models', lambda: all(g.call('coop.status').count('puppet pos=')==len(games)-1 for g in games))
    sizes = []
    for game in games:
        text = game.call('coop.status')
        matches = re.findall(r'captain run=(\d+) selection=(\d+) expected=([0-9A-F]+) scale=([\d.]+) owner_scale=([\d.]+)', text)
        if len(matches)!=len(games)-1 or any(abs(float(match[3])-float(match[4]))>.0002 for match in matches):
            raise FixtureError('remote captain scale differs from its owning captain: ' + text)
        sizes.append(matches)
    def complete_rosters():
        counts=[]
        for game in games:
            match=re.search(r'actors=(\d+) mirrored=(\d+).*?remote=(\d+).*?roster=(\d+)',game.adventure()['text'])
            if not match: return None
            counts.append(dict(zip(('actors','mirrored','remote','roster'),map(int,match.groups()))))
        expected=counts[0]['actors']
        return counts if expected and all(row['remote']==row['mirrored']==row['roster']==expected for row in counts[1:]) else None
    rosters=host.wait('complete reliable actor baselines on every guest',complete_rosters)
    for game in games:
        if 'confirmation_hud=0' not in game.call('coop.adventure.hud'):
            raise FixtureError('confirmation labels survived group release')
    return dict(before=before, held=held, after=[m.goals() for m in memories], scales=sizes,rosters=rosters)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--instances',default='21,22')
    parser.add_argument('--port',type=int,default=47830)
    parser.add_argument('--long-names',action='store_true')
    args = parser.parse_args()
    instances=tuple(map(int,args.instances.split(',')))
    records = verify_lab(args.metadata, instances)
    games = [Game(i, args.output, timeout=120) for i in instances]
    memories = []
    try:
        host = games[0]
        if not args.resume:
            for game in games:
                game.dismiss_registration()
                game.call('coop.leave')
            host_name='FlowHostWithALongName' if args.long_names else 'FlowHost'
            host.call(f'coop.host {args.port} {host_name}')
            for game in games[1:]:
                name=f'FlowGuestWithALongName{game.instance}' if args.long_names else f'FlowGuest{game.instance}'
                game.call(f'coop.join 127.0.0.1:{args.port} {name}')
            host.wait('full party', lambda: host.adventure()['players'] == len(games))
            host.call('coop.adventure start ' + PUZZLE)
            for game in games:
                state=lobby(game, len(games), "Mr Puzzle's Magical Gates")
                if 'By Maxis' not in state or 'adventure_portrait=1' not in state or 'change_enabled=0' not in state:
                    raise FixtureError('shared adventure artwork or authored captain lock is missing')
                if re.search(r'^captain .*ready=1',state,re.M):
                    raise FixtureError('a participant was implicitly ready')
            for game in games[1:]:click_named(game,'prep_ready')
            click_named(host, 'prep_ready')
        memories = [AdventureMemory(g, records[g.instance]) for g in games]
        result = check_intro(games, memories)
        for game in games:
            if 'total faults: 0' not in game.call('exc'):
                raise FixtureError('native fault during shared intro')
        result.update(pass_=True, checks=['guest-first Continue holds both clocks', 'all-player release', 'reciprocal captain models and scale'])
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        print('PASS: shared intro, clocks and captain scale', flush=True)
        return 0
    except (FixtureError, OSError, ValueError) as error:
        print('FAIL:', error, flush=True)
        for game in games:
            try: game.record('failure')
            except (FixtureError, OSError, ValueError): pass
        return 1
    finally:
        for m in memories: m.close()
        for g in games: g.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
