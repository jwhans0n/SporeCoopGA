#!/usr/bin/env python3
"""Civilization: the guest's vehicles drive while the host is in the main menu (BUG-0151).

The host simulates the vehicles and handles the guest's orders only while it plays Civ; while it
was in the main menu the guest's vehicles stood still (their AI is skipped on a guest) and its
orders went to a host that dropped them.

1. The guest buys a vehicle (its own), the host goes to the main menu (`coop.menu`).
2. The guest orders it to drive 40 m (`coop.civ move`: the game's SetMoveOrder, hooked).
3. It drives in the guest's game (position moves by more than 5 m).
4. The host loads its world again: the guest mirrors the host's vehicles again.

--nofix: `coop.civ fix away 0` on the guest (the old frozen vehicles): must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_civ_away.py [--nofix]
"""
import argparse
import time

from coop_civ import civ, dist
from twoinst import Checks, lane_state, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ')
    ck = Checks()
    if a.nofix:
        print(' ', guest.call('coop.civ fix away 0')['out'].strip())
    time.sleep(4)
    hc, hv = civ(host)
    pc = [k for k, v in hc.items() if v['player']]
    if not ck(pc, 'a player city'):
        return ck.result()
    print(' ', guest.call(f'coop.civ buy {pc[0]} 0')['out'].strip())
    time.sleep(4)
    gc, gv = civ(guest)
    mine = [i for i, v in gv.items() if v['owner'] == 1]
    if not ck(mine, f'the guest owns a vehicle {mine}'):
        return ck.result()
    vid = mine[0]
    print(' ', host.call('coop.menu')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 90 and 'host_away=1' not in guest.call('coop.status')['out']:
        time.sleep(1)
    ck('host_away=1' in guest.call('coop.status')['out'], 'the guest knows the host is away')
    gc, gv = civ(guest)
    p0 = gv[vid]['pos']
    cpos = gc[pc[0]]['pos']
    d = dist(p0, cpos) or 1
    tgt = tuple(c + (q - c) / d * (d + 40) for q, c in zip(p0, cpos))
    print(' ', guest.call('coop.civ move %d %.1f %.1f %.1f' % ((vid,) + tgt))['out'].strip())
    time.sleep(10)
    _, gv = civ(guest)
    print('  ', [ln for ln in guest.call('coop.civ')['out'].splitlines() if f'veh #{vid} ' in ln or 'civ(guest)' in ln])
    moved = dist(gv[vid]['pos'], p0) if vid in gv else 0
    ck(moved > 5, f'the guest\'s vehicle drives while the host is away ({moved:.1f} m)')
    lst = lane_state()
    slot, name = lst.get('planet', 'Game0/Scalinus').split('/')
    if lst.get('slot', 9) != 9:
        slot = f'Game{lst["slot"]}'
    print(' ', host.call(f'coop.load {slot} {name}')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 150 and 'host_away=0' not in guest.call('coop.status')['out']:
        time.sleep(2)
    time.sleep(8)
    hc, hv = civ(host)
    gc, gv = civ(guest)
    ck(hv and set(hv) == set(gv), f'the guest mirrors the host\'s vehicles again ({len(hv)} / {len(gv)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
