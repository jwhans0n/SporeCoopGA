#!/usr/bin/env python3
"""Civilization: an NPC city founded while a guest joins reaches that guest (BUG-0120).

The host tells its guests about a new city with one MSG_CIV_BUILD event. A guest that is not in
Civ when it comes (receiving or loading the host's world) dropped it, and the host's save it loads
was written before the city: the guest never had that city.

1. Two games on civ_npc. The guest leaves and joins again; while its co-op window shows
   "receiving world", the host founds the next NPC city the way its own city manager does
   (cCivExpansion::SpawnNextCity(instant 0, who 0), the hooked function, which
   broadcasts the founding).
2. Once the guest is in the world: the game's own city list on the guest has a city within 60 m
   of every city of the host (positions from `coop.civ`), within 60 s.

--nofix: `coop.civ fix cities 0` (only the event, the code before): must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_civ_city_join.py [--nofix]
"""
import argparse
import math
import re
import time

import lane
from twoinst import Checks, start, wait_guest_in


def cities(con):
    return [tuple(float(x) for x in m.groups()) for m in
            re.finditer(r'^city -?\d+ \S+ pol=\S+.*? pos=\((-?[\d.]+) (-?[\d.]+) (-?[\d.]+)\)', con.call('coop.civ')['out'], re.M)]


def missing(host, guest):
    h, g = cities(host), cities(guest)
    return h, g, [c for c in h if not any(math.dist(c, d) < 60 for d in g)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    for c in (host, guest):
        print(' ', c.call(f'coop.civ fix cities {0 if a.nofix else 1}')['out'].strip())
    time.sleep(3)
    h0, g0, miss = missing(host, guest)
    print(f'  cities before: host {len(h0)}, guest {len(g0)}', flush=True)
    ck(h0 and not miss, f'both games start with the same cities ({len(h0)}, missing on the guest {miss})')

    guest.call('coop.leave')
    time.sleep(2)
    guest.call(lane.JOIN)
    t0, seen = time.time(), False
    while time.time() - t0 < 30:
        if 'receiving world' in guest.call('coop.status')['out']:
            seen = True
            break
        time.sleep(0.2)
    print(f'  guest receiving the world: {seen} after {time.time() - t0:.1f} s', flush=True)
    m = re.search(r'eax=(0x[0-9A-Fa-f]+)', host.call('call Simulator::cCivExpansion::Get cdecl')['out'])
    ck(m and int(m.group(1), 16), 'the host has a city simulator')
    if not (m and int(m.group(1), 16)):
        return ck.result()
    print('  host founds a city:', host.call(f'call Simulator::cCivExpansion::SpawnNextCity thiscall {m.group(1)} 0 0')['out'].strip())
    ok = wait_guest_in(host, guest, 180)
    ck(ok, 'the guest is back in the world')
    t1 = time.time()
    while time.time() - t1 < 30 and len(cities(host)) <= len(h0):
        host.call('key enter')  # the "new city" caption, if it waits for OK
        time.sleep(1)
    h1 = cities(host)
    ck(len(h1) > len(h0), f'the host founded a new city ({len(h0)} -> {len(h1)})')
    t1 = time.time()
    while time.time() - t1 < 60:
        h1, g1, miss = missing(host, guest)
        if not miss:
            break
        time.sleep(2)
    print(f'  cities after: host {len(h1)}, guest {len(g1)}, host cities missing on the guest: {miss}', flush=True)
    print(' ', guest.call('coop.civ')['out'].splitlines()[0][-140:], flush=True)
    ck(not miss and len(g1) == len(h1), f'every city of the host exists on the guest ({len(h1)} / {len(g1)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
