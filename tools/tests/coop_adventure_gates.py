#!/usr/bin/env python3
"""Validate native colored key ownership and gate activation in a hidden lab pair."""
import argparse
import json
from pathlib import Path
import re
import time

from coop_adventure import Game, FixtureError, verify_lab
from adventure_memory import AdventureMemory


def actor(game, marker, kind):
    text=game.call(f'coop.adventure.world actorsat {marker} 48')
    for line in text.splitlines():
        if not re.search(rf'\bpowerup={kind}\b',line): continue
        match=re.search(r'incarnation=(\d+).*?object=(0x[0-9a-fA-F]+).*?key_category=(\d+)',line)
        if match: return dict(incarnation=int(match[1]),object=int(match[2],16),category=int(match[3]),text=line)
    return None


def inventory(game,memory,player):
    if player==game.player_id():
        text=game.call('coop.avatar');match=re.search(r'local=([0-9A-Fa-f]+)',text)
    else:
        text=game.call('coop.status');match=re.search(rf'remote {player} .*?puppet=(?:0x)?([0-9A-Fa-f]+)',text)
    if not match: raise FixtureError('captain inventory address unavailable')
    captain=int(match[1],16)
    first,last=memory.pointer(captain+0xE40),memory.pointer(captain+0xE44)
    if last<first or (last-first)%0x24 or last-first>32*0x24: raise FixtureError('invalid captain item vector')
    return [memory.pointer(p+0x20) for p in range(first,last,0x24)]


def run(games,memories):
    host,guest=games[:2]
    guest_id=guest.player_id()
    key=125;wrong_gate=19;matching_gate=108
    original=[actor(g,key,12)for g in games]
    closed=[actor(g,matching_gate,11)for g in games]
    wrong=[actor(g,wrong_gate,11)for g in games]
    if not all(original+closed+wrong): raise FixtureError('Mr Puzzle physical key/gate fixture is missing')
    if any(k['category']!=d['category'] or k['category']==w['category']for k,d,w in zip(original,closed,wrong)):
        raise FixtureError('key/gate native color fixture mismatch')
    guest.call(f'coop.adventure.world near {key} {guest_id}');time.sleep(1)
    guest.call(f'coop.adventure.world pickup {key} {guest_id}')
    host.wait('guest holds one key on both clients',lambda:all(
        original[i]['object']+0x34 in inventory(g,m,guest_id)for i,(g,m)in enumerate(zip(games,memories))))
    host.call(f'coop.adventure.world near {key} 0');time.sleep(1)
    host.call(f'coop.adventure.world pickup {key} 0',required=False)
    if any(original[i]['object']+0x34 in inventory(g,m,0)for i,(g,m)in enumerate(zip(games,memories))):
        raise FixtureError('host took a key already owned by the guest')
    guest.call(f'coop.adventure.world near {wrong_gate} {guest_id}');time.sleep(2)
    if not all(actor(g,wrong_gate,11)for g in games):raise FixtureError('wrong colored key opened a gate')
    guest.call(f'coop.adventure.world near {matching_gate} {guest_id} 0 0')
    host.wait('guest opens matching gate on both clients',lambda:all(actor(g,matching_gate,14)for g in games),timeout=20)
    opened=[actor(g,matching_gate,14)for g in games]
    guest.call(f'coop.adventure.world forgetgate {matching_gate}')
    guest.wait('open gate reconstructed from host state',lambda:actor(guest,matching_gate,14),timeout=20)
    if any(original[i]['object']+0x34 in inventory(g,m,guest_id)for i,(g,m)in enumerate(zip(games,memories))):
        raise FixtureError('gate did not consume the held key')
    for g in games:
        g.record('guest-opened-gate')
        if 'total faults: 0'not in g.call('exc'):raise FixtureError('native gate activation fault')
    # A different physical key of the same color exercises host-originated activation.
    second_key=136;second_gate=110
    keys=[actor(g,second_key,12)for g in games]
    if not all(keys):raise FixtureError('second key fixture missing')
    host.call(f'coop.adventure.world near {second_key} 0');time.sleep(1)
    host.call(f'coop.adventure.world pickup {second_key} 0')
    host.wait('host holds key on both clients',lambda:all(
        keys[i]['object']+0x34 in inventory(g,m,0)for i,(g,m)in enumerate(zip(games,memories))))
    host.call(f'coop.adventure.world near {second_gate} 0 0 0')
    host.wait('host opens matching gate on both clients',lambda:all(actor(g,second_gate,14)for g in games),timeout=20)
    if any(keys[i]['object']+0x34 in inventory(g,m,0)for i,(g,m)in enumerate(zip(games,memories))):
        raise FixtureError('host key was not consumed on every client')
    return dict(pass_=True,original_keys=original,closed=closed,opened=opened,
                checks=['guest native pickup and replicated holding','host cannot take a key held by another captain','wrong color rejected','guest native proximity opens matching gate',
                        'host native proximity opens matching gate','native key consumption on every client','open gate snapshot reconstruction'])


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--instances',default='21,22')
    args=parser.parse_args();instances=tuple(map(int,args.instances.split(',')));records=verify_lab(args.metadata,instances)
    games=[Game(i,args.output,timeout=90)for i in instances]
    memories=[AdventureMemory(g,records[g.instance])for g in games]
    try:
        result=run(games,memories)
        (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        print('PASS: native colored keys and shared gates',flush=True);return 0
    except(FixtureError,OSError,ValueError)as error:
        print('FAIL:',error,flush=True)
        for g in games:
            try:g.record('failure')
            except(FixtureError,OSError,ValueError):pass
        return 1
    finally:
        for m in memories:m.close()
        for g in games:g.save_log()


if __name__=='__main__':raise SystemExit(main())
