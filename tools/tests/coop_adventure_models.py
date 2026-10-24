#!/usr/bin/env python3
"""Check guest captain replacement and returning to a previously saved captain."""
import argparse
import json
from pathlib import Path
import re

from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_lobby import TOWN, lobby, playing, click_named
from coop_adventure_preparation import menu


def capture(games):
    rows=[]
    for game in games:
        text=game.call('coop.status')
        appearance=re.search(r'puppet pos=.*?species=([0-9A-Fa-f]+)',text)
        match=re.search(r'captain run=(\d+) selection=(\d+) expected=([0-9A-F]+) scale=([\d.]+) owner_scale=([\d.]+)',text)
        if not match or not appearance or int(appearance[1],16)!=int(match[3],16):
            raise FixtureError('remote captain uses a different native species profile: '+text)
        if abs(float(match[4])-float(match[5]))>.0002:raise FixtureError('remote captain scale mismatch')
        rows.append(dict(local=game.captain_key(),remote_model=match[3],run=int(match[1]),selection=int(match[2]),scale=float(match[4])))
    return rows


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();records=verify_lab(args.metadata,(21,22))
    games=[Game(i,args.output,timeout=180)for i in(21,22)];host,guest=games
    results=[]
    try:
        for index in range(2):
            host.call('coop.adventure cancel')
            for game in games:menu(game,records[game.instance])
            host.call('coop.adventure start '+TOWN)
            host.prepare_captain('LobbyNew21',create=False)
            guest.prepare_captain('LobbyNew22',create=False)
            for game in games:lobby(game,2)
            if index==0:
                click_named(guest,'prep_change')
                guest.prepare_captain('FlowGuestNew',create=True)
                for game in games:
                    if 'FlowGuestNew'not in lobby(game,2):raise FixtureError('replacement portrait/name missing')
            for game in games:click_named(game,'prep_ready')
            playing(games,records)
            host.wait('both current native captain profiles',lambda:all('puppet pos='in g.call('coop.status')for g in games))
            guest.call('coop.adventure.world nearplayer 0')
            rows=capture(games);results.append(rows)
            for game in games:
                game.record(f'selected-model-{index}')
                if 'total faults: 0'not in game.call('exc'):raise FixtureError('captain model fault')
        if results[0][1]['local']==results[1][1]['local'] or results[0][0]['remote_model']==results[1][0]['remote_model']:
            raise FixtureError('new and previously saved guest captains did not produce distinct models')
        (args.output/'result.json').write_text(json.dumps(dict(pass_=True,runs=results,
            checks=['new guest captain replaces an existing selection','next run uses selected existing captain rather than last-used new captain',
                    'native puppet profile matches current transferred resource','captain scale matches owner']),indent=2)+'\n')
        print('PASS: current guest captain identity, replacement and scale',flush=True);return 0
    except(FixtureError,OSError,ValueError)as error:
        print('FAIL:',error,flush=True)
        for g in games:
            try:g.record('failure')
            except(FixtureError,OSError,ValueError):pass
        return 1
    finally:
        for g in games:g.save_log()


if __name__=='__main__':raise SystemExit(main())
