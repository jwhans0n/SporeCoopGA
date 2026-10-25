#!/usr/bin/env python3
"""Verify native results Replay returns an isolated party to explicit readiness."""
import argparse
import json
from pathlib import Path
import re
import time

from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_lobby import lobby, click_named
from coop_adventure_flow import check_intro
from adventure_memory import AdventureMemory


def run(games, memories, rounds=2):
    results=[]
    host,guest=games
    for iteration in range(rounds):
        previous=host.adventure()['run']
        captains=[g.captain_key() for g in games]
        host.call('coop.adventure.world finish fail')
        for game in games:
            def ready(game=game):
                if game.caption('Replay'): return True
                if game.control(0x0445C99D): game.click_control(0x0445C99D)
                return False
            game.wait('native results panel',ready)
        for game in (guest,host):
            # Replay requires the host's explicit Replay choice. Host Done
            # authoritatively returns the party to the galaxy menu, including
            # when a guest has already requested Replay.
            choice='Replay'
            def accept(game=game,choice=choice):
                # Native result widgets can remain discoverable behind the
                # newly opened lobby. A newer run confirms that the real result
                # button was accepted without depending on retained captions.
                if game.adventure()['run']>previous: return True
                if not game.caption('Replay'): return True
                # Do not enter a nested caption wait: native cleanup can replace
                # the results panel between two UI reads. Recheck the new run on
                # the next poll if the requested button is no longer present.
                point=game.caption(choice)
                if point: game.click(*point)
                return False
            game.wait('Replay accepted by native results',accept,timeout=20)
        for game in games:
            state=lobby(game,2)
            if re.search(r'^captain .*ready=1',state,re.M):
                raise FixtureError('Replay implicitly readied a captain')
            if game.adventure()['run']<=previous:
                raise FixtureError('Replay reused the old adventure run')
            game.record(f'replay-lobby-{iteration}')
        click_named(host,'prep_ready')
        if 'Adventure in progress' in host.adventure()['text']:
            raise FixtureError('host alone launched Replay')
        click_named(guest,'prep_ready')
        intro=check_intro(games,memories,guest_first=False)
        if [g.captain_key() for g in games]!=captains:
            raise FixtureError('Replay replaced saved captains')
        results.append(dict(previous_run=previous,new_run=host.adventure()['run'],captains=captains,intro=intro))
    return results


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--rounds',type=int,default=2)
    parser.add_argument('--host',type=int,default=21)
    parser.add_argument('--guest',type=int,default=22)
    parser.add_argument('--timeout',type=float,default=90,help='native transition timeout in seconds')
    args=parser.parse_args()
    if args.host==args.guest:parser.error('--host and --guest must be distinct lab instances')
    instances=(args.host,args.guest)
    records=verify_lab(args.metadata,instances)
    games=[Game(i,args.output,timeout=args.timeout) for i in instances]
    memories=[AdventureMemory(g,records[g.instance])for g in games]
    try:
        rounds=run(games,memories,args.rounds)
        for g in games:
            if 'total faults: 0' not in g.call('exc'): raise FixtureError('native Replay fault')
        (args.output/'result.json').write_text(json.dumps(dict(pass_=True,rounds=rounds),indent=2)+'\n')
        print('PASS: native co-op Replay and explicit readiness',flush=True)
        return 0
    except (FixtureError,OSError,ValueError) as error:
        print('FAIL:',error,flush=True)
        for g in games:
            try:g.record('failure')
            except (FixtureError,OSError,ValueError):pass
        return 1
    finally:
        for m in memories:m.close()
        for g in games:g.save_log()


if __name__=='__main__':raise SystemExit(main())
