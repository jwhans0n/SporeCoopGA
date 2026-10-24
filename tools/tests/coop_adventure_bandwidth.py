#!/usr/bin/env python3
"""Measure actual GA UDP traffic through a loopback proxy in a recorded hidden lab."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import time

from adventure_network_proxy import NetworkProxy
from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_lobby import TOWN, PUZZLE, lobby, playing, click_named
from coop_adventure_preparation import menu


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--key', choices=('Town', 'Puzzle'), default='Town')
    parser.add_argument('--seconds', type=float, default=10)
    parser.add_argument('--loss', type=float, default=0)
    parser.add_argument('--jitter', type=float, default=0)
    parser.add_argument('--instances', default='21,22')
    parser.add_argument('--captains', default='LobbyNew21,LobbyNew22')
    parser.add_argument('--port', type=int, default=47830)
    parser.add_argument('--static-check', action='store_true')
    parser.add_argument('--static-act-transitions', help='optional forward acts after static checks, e.g. 1,2')
    parser.add_argument('--fps-check', action='store_true', help='run the 400-actor regression after measuring Town')
    args = parser.parse_args()
    instances=tuple(map(int,args.instances.split(',')))
    captains=args.captains.split(',')
    if len(instances)!=2 or len(captains)!=2: parser.error('provide two instances and two saved captain names')
    if args.fps_check and args.key!='Town': parser.error('--fps-check requires Town')
    if args.static_act_transitions and not args.static_check: parser.error('--static-act-transitions requires --static-check')
    records = verify_lab(args.metadata, instances)
    games = [Game(i, args.output, timeout=180) for i in instances]
    host, guest = games
    try:
        host.call('coop.adventure cancel', required=False)
        for game in games:
            game.dismiss_registration()
            game.call('coop.leave')
        with NetworkProxy(args.port+10, args.port, loss=args.loss, jitter=args.jitter) as proxy:
            host.call(f'coop.host {args.port} BandwidthHost')
            guest.call(f'coop.join 127.0.0.1:{args.port+10} BandwidthGuest')
            host.wait('two-member party', lambda: host.adventure()['players'] == 2)
            host.call('coop.adventure start '+(TOWN if args.key == 'Town' else PUZZLE))
            if args.key == 'Town':
                for game,captain in zip(games,captains):
                    game.prepare_captain(captain, create=False)
            for game in games:
                lobby(game, 2)
            for game in (guest, host):
                click_named(game, 'prep_ready')
            playing(games, records)
            time.sleep(4)
            before = proxy.snapshot()
            started = time.monotonic()
            before_status = [g.call('coop.adventure status') for g in games]
            time.sleep(args.seconds)
            elapsed = time.monotonic()-started
            delta = proxy.difference(before, proxy.snapshot())
            after_status = [g.call('coop.adventure status') for g in games]
            actor_rates = {k: value/elapsed for k, value in delta.items() if re.fullmatch(r'down_actor_\d+_\d+', k)}
            for game in games:
                if 'total faults: 0' not in game.call('exc'):
                    raise FixtureError('native fault during bandwidth measurement')
            result = dict(pass_=False, measurement_complete=True, key=args.key, elapsed=elapsed, traffic=delta,
                down_bytes_per_second=delta.get('down_bytes', 0)/elapsed,
                up_bytes_per_second=delta.get('up_bytes', 0)/elapsed,
                loss=args.loss,jitter=args.jitter,before=before_status,after=after_status,
                actor_updates_per_second=actor_rates,
                versions=[g.call('mods') for g in games],network=[g.call('coop.status').splitlines()[:2] for g in games])
            installation=Path(json.loads(args.metadata.read_text())['installation'])
            result['dll_sha256']=hashlib.sha256((installation/'SporebinEP1/mods/coop.dll').read_bytes()).hexdigest()
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
            checks=[]
            if args.static_check:
                command=[sys.executable,str(Path(__file__).with_name('coop_adventure_static.py')),
                    '--metadata',str(args.metadata),'--host',str(instances[0]),'--guest',str(instances[1]),
                    '--output',str(args.output/'static')]
                if args.static_act_transitions: command+=['--act-transitions',args.static_act_transitions]
                checks.append(('static',command))
            if args.fps_check:
                checks.append(('fps',[sys.executable,str(Path(__file__).with_name('coop_adventure_performance.py')),
                    '--metadata',str(args.metadata),'--host',str(instances[0]),'--guest',str(instances[1]),
                    '--output',str(args.output/'performance.json')]))
            for name,command in checks:
                check=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
                (args.output/(name+'-driver.txt')).write_text(check.stdout)
                if check.returncode: raise FixtureError(name+' regression failed; see '+str(args.output/(name+'-driver.txt')))
            result['pass_']=True
            (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
            print('PASS bandwidth:', json.dumps({k:result[k] for k in ('key','elapsed','down_bytes_per_second','up_bytes_per_second')}), flush=True)
            host.call('coop.adventure cancel')
            for game in games:
                menu(game,records[game.instance])
            return 0
    except (FixtureError, OSError, ValueError) as error:
        print('FAIL:', error, flush=True)
        for game in games:
            try: game.record('failure')
            except (FixtureError, OSError, ValueError): pass
        try:
            host.call('coop.adventure cancel',required=False)
            for game in games: menu(game,records[game.instance])
        except (FixtureError,OSError,ValueError): pass
        return 1
    finally:
        for game in games:
            game.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
