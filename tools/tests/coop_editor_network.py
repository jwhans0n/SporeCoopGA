#!/usr/bin/env python3
"""Opt-in actual native editor LAN/WAN gestures in an already-owned Windows lab.

Requires Galaxy on two/four hidden, muted, isolated processes and an exact DLL.
Uses ordinary native creation/save/mouse input plus public co-op actions. No game
launches, DLL writes, memory writes, synthetic edits or personal profile access.
This covers native command latency and adversity, not bulk-content contention.
"""
import argparse
from contextlib import ExitStack
import json
import math
from pathlib import Path
import re
import time

import coop_editor_party as party_module
from coop_adventure_editor import EditorPair, status
from coop_adventure import FixtureError
from editor_network_proxy import EditorProxy, Profile


ROOT = Path(__file__).resolve().parents[2]
PROFILES = (
    ('lan', Profile(), 200),
    ('wan_100ms_1percent', Profile(rtt_ms=100,loss=.01,reorder=.02,duplicate=.01), 500),
    ('stress_100ms_10percent', Profile(rtt_ms=100,loss=.10,reorder=.05,duplicate=.02), None),
)


def timing(game):
    text=game.call('coop.editor timing')
    match=re.search(r'^confirmation_ms=([0-9.,]*)$',text,re.M)
    count=re.search(r'\bsamples=(\d+)\b',text)
    if not match or not count:
        raise FixtureError('Native confirmation instrumentation is unavailable')
    values=list(map(float,match[1].split(','))) if match[1] else []
    if len(values)!=int(count[1]) or any(not math.isfinite(v) or v<0 for v in values):
        raise FixtureError('Invalid bounded native confirmation measurements')
    return values


def run(args):
    args.host,args.guest=args.instances[:2]
    args.stage='network';args.fixture='blank';args.adventure='Alpine Valley';args.roundtrip_cycles=0
    args.resume_result=None;args.onboard_host=False;args.history_input='hotkeys'
    party=None
    original_game=party_module.PublicGame
    normal_finished=False

    def cleanup_before_proxies_close():
        if normal_finished or party is None:
            return
        # Best effort, within the existing owner lease. Never dismiss a native
        # Save/foreign dialog or fabricate a result to make cleanup succeed.
        receipt={'complete':False,'errors':[],'native_dialogs_forced':False}
        deadline=min(party.deadline,time.monotonic()+15)
        try:
            party.ownership.verify_windows()
            for game in party.all_games:
                if time.monotonic()>=deadline:break
                try:game.call('coop.editor end')
                except Exception as error:receipt['errors'].append(str(error))
            while time.monotonic()<deadline:
                states=[status(game) for game in party.all_games]
                if all(state.get('phase') in (0,1) for state in states):
                    receipt['complete']=True;break
                time.sleep(.1)
            for game in party.all_games:
                if time.monotonic()>=deadline:break
                try:game.call('coop.leave')
                except Exception as error:receipt['errors'].append(str(error))
        except Exception as error:
            receipt['errors'].append(str(error))
        receipt['owner_must_settle_native_windows']=True
        party.result['failure_cleanup']=receipt;party.write()
    try:
        with ExitStack() as stack:
            proxies={instance:stack.enter_context(EditorProxy(args.port,seed=0xBEEF1234+index*101))
                     for index,instance in enumerate(args.instances[1:])}

            class ProxiedGame(original_game):
                def call(self,command,required=True):
                    words=command.split()
                    if words and words[0]=='coop.join':
                        if self.instance not in proxies or words[1]!='127.0.0.1:'+str(args.port):
                            raise FixtureError('Only this owned party join may use the proxy')
                        words[1]='127.0.0.1:'+str(proxies[self.instance].port)
                        command=' '.join(words)
                    return super().call(command,required)

            party_module.PublicGame=ProxiedGame
            party=party_module.EditorParty(args)
            # Registered last: public End/leave runs while all proxies remain
            # alive, before ExitStack tears down their sockets after a failure.
            stack.callback(cleanup_before_proxies_close)
            for game in party.all_games:
                if not game.menu() or status(game).get('phase')!=0:
                    raise FixtureError('Network fixture requires settled Galaxy and no editor session')
            if len(args.instances)==4:
                party.bootstrap()
            else:
                EditorPair.bootstrap(party)
            party.join()
            if len(args.instances)==2:
                party.result['gates'].pop('four_native_baselines',None)
            for game in party.all_games:
                game.call('coop.uistate 0')
            time.sleep(1.4)
            placements=party.objects(party.host)
            if len(placements)!=1:
                raise FixtureError('Native network gestures require the fresh single-placement fixture')
            object_id=next(iter(placements))
            author=party.guest
            def move(index):
                source=party.body(author,object_id)
                target=(source[0]+(8 if index%2==0 else -8),source[1])
                party.slow_drag(author,source,target)
            party.gesture(author,'warm-native-remote-move',lambda:move(0))
            results=[]
            for name,profile,budget in PROFILES:
                for proxy in proxies.values():
                    proxy.configure(profile)
                time.sleep(2.5);party.converge()
                author.call('coop.editor timing reset')
                before={str(instance):proxy.snapshot() for instance,proxy in proxies.items()}
                revision=status(party.host)['revision']
                for index in range(args.samples):
                    party.gesture(author,f'{name}-actual-native-move-{index+1}',lambda n=index:move(n))
                author.call('coop.editor timing stop')
                values=timing(author)
                if len(values)!=args.samples or status(party.host)['revision']!=revision+args.samples:
                    raise FixtureError('Every measured native gesture must commit exactly once')
                party.converge(revision+args.samples)
                after={str(instance):proxy.snapshot() for instance,proxy in proxies.items()}
                p95=sorted(values)[math.ceil(.95*len(values))-1]
                row={'name':name,'profile':vars(profile),'native_confirmation_ms':values,'p95_ms':p95,
                     'budget_ms':budget,'budget_pass':p95<=budget if budget else None,
                     'traffic':{key:EditorProxy.difference(before[key],after[key]) for key in before},
                     'canonical_revision':revision+args.samples}
                results.append(row);party.result['native_network']=results;party.write()
                if budget and p95>budget:
                    raise FixtureError(f'Actual native {name} p95 {p95:.2f}ms exceeds {budget}ms')
            party.result.update(pass_native_network=True,bulk_asset_contention_pass=False)
            party.result['pass']=True
            party.result['unvalidated']=[item for item in party.result.get('unvalidated',[]) if item!='real packet loss/reordering between game processes']
            party.result['method']='Native Runtime Controller.submit through own accepted decision after native/durable apply. Lease gating/inverse settlement precede measurement. Real UDP source bytes include headers, ACKs and retransmissions before injected duplication; IP/UDP headers excluded.'
            party.record('native-network-complete')
            # Close the editor session while the owned proxies still exist.
            party.host.call('coop.editor end')
            party.host.wait('all native editors detached',lambda:all(status(g).get('phase')==0 for g in party.all_games))
            for game in party.all_games:
                party.exit_editor(game);game.call('coop.leave')
            normal_finished=True
            return 0
    except Exception as error:
        if party:
            party.result.update(pass_native_network=False,error=str(error))
            party.result['pass']=False
            try:party.record('native-network-failure')
            except Exception:party.write()
        else:
            args.output.mkdir(parents=True,exist_ok=True)
            (args.output/'result.json').write_text(json.dumps({'pass':False,'error':str(error)},indent=2)+'\n',encoding='utf-8')
        print(str(error));return 1
    finally:
        party_module.PublicGame=original_game
        if party:party.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--metadata',type=Path,required=True)
    parser.add_argument('--instances',type=int,nargs='+',required=True)
    parser.add_argument('--expected-dll-sha256',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--port',type=int,default=59087)
    parser.add_argument('--seconds',type=int,default=1800)
    parser.add_argument('--samples',type=int,default=30)
    parser.add_argument('--timeout',type=int,default=120)
    parser.add_argument('--title',default='Native Network Editor Fixture')
    args=parser.parse_args()
    if not args.execute:parser.error('Requires --execute and already-owned native lab processes')
    if len(args.instances) not in (2,4) or len(set(args.instances))!=len(args.instances) or min(args.instances)<2 or max(args.instances)>100:
        parser.error('Two or four distinct private instances2..100 required')
    if not re.fullmatch('[0-9a-f]{64}',args.expected_dll_sha256) or not 30<=args.samples<=100 or not 60<=args.seconds<=3500 or not 1<=args.timeout<=300 or not 1<=args.port<=65535:
        parser.error('Exact candidate hash and bounded test settings required')
    args.output=args.output.resolve()
    if (ROOT/'out').resolve() not in args.output.parents:parser.error('Output must be under this worktree out directory')
    return run(args)


if __name__=='__main__':
    raise SystemExit(main())
