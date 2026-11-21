#!/usr/bin/env python3
"""Tribe: the guest plays on while the host is in the main menu (BUG-0151).

The host simulates every citizen and handles the guest's orders only while it plays Tribe. When the
host left for the main menu the guest was told "you play on alone", but its members stood still
(their AI is skipped on a guest) and its orders went to a host that dropped them.

1. The host goes to the main menu (`coop.menu`); the guest notices (host_away=1).
2. The guest selects one of its own members and orders it to walk ~20-60 m (`coop.tribe walk`: the
   game's WalkSelectedTo, as a right-click).
3. The member walks: its position in the guest's game moves by more than 8 m.
4. The host loads its world again: the guest follows the host's states again (no fault).

--nofix: `coop.tribe fix away 0` on the guest (the old frozen tribe): must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_tribe_away.py [--nofix]
"""
import argparse
import time

from coop_tribe import dist, members
from twoinst import Checks, lane_state, start


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        print(' ', guest.call('coop.tribe fix away 0')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 90:
        gm = members(guest)
        if any(v[0] == 1 for v in gm.values()):
            break
        time.sleep(3)
    gm = members(guest)
    mine = [i for i, v in gm.items() if v[0] == 1 and v[3] == 'player']
    if not ck(mine, f'the guest has own members {mine}'):
        return ck.result()
    print(' ', host.call('coop.menu')['out'].strip())
    t0 = time.time()
    away = False
    while time.time() - t0 < 90 and not away:
        away = 'host_away=1' in guest.call('coop.status')['out']
        time.sleep(1)
    ck(away, 'the guest knows the host is away')
    time.sleep(2)
    gm = members(guest)
    me = mine[0]
    p0 = gm[me][2]
    # a place 20-80 m away the game can walk to: where a member of another tribe stands
    cands = sorted((dist(p0, v[2]), v[2]) for i, v in gm.items() if v[3] != 'player')
    tgt = next((p for d, p in cands if 20 < d < 80), cands[0][1] if cands else None)
    if not ck(tgt is not None, 'a target to walk to'):
        return ck.result()
    for i, v in gm.items():
        if v[1]:
            guest.call(f'coop.tribe select {i} 0')
    print(' ', guest.call(f'coop.tribe select {me} 1')['out'].strip())
    print(' ', guest.call('coop.tribe walk %.1f %.1f %.1f' % tgt)['out'].strip(), f'({dist(p0, tgt):.0f} m)')
    time.sleep(10)
    p1 = members(guest).get(me, (0, 0, p0))[2]
    moved = dist(p0, p1)
    st = [ln for ln in guest.call('coop.status')['out'].splitlines() if ln.startswith('tribe(guest)')]
    print(f'  member #{me} moved {moved:.1f} m; {st[0] if st else ""}')
    ck(moved > 8, f'the guest\'s member walks while the host is away ({moved:.1f} m)')
    lst = lane_state()
    slot, name = lst.get('planet', 'Game0/Scalinus').split('/')
    if lst.get('slot', 9) != 9:
        slot = f'Game{lst["slot"]}'
    print(' ', host.call(f'coop.load {slot} {name}')['out'].strip())
    t0 = time.time()
    back = False
    while time.time() - t0 < 150 and not back:
        back = 'host_away=0' in guest.call('coop.status')['out'] and host.call('coop.tribe')['ok']
        time.sleep(2)
    ck(back, 'the host is back')
    time.sleep(6)
    ck(any(v[0] == 1 for v in members(guest).values()), 'the guest follows the host\'s tribe again')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
