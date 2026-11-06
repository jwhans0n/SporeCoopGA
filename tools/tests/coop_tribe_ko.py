#!/usr/bin/env python3
"""Tribe: a member knocked out on the host lies down on the guest too (BUG-0173).

A member of an NPC tribe (and the game's chieftain) that loses all its health is not removed at
once: the host's game sets its dead flag and plays the knock-out/death node, and the member stays
in the tribe until it respawns. The guest applied only positive health to its mirrors, so the
mirror kept its last health and stood.

1. Two games on the tribe fixture. The host hits a member of the NPC tribe with a blow of 1000
   from one of its own members (the citizen's TakeDamage, `coop.tribe pincer`).
2. Within 3 s the guest's mirror of that member has the game's dead flag (+0xB5E, `dead=1` in
   `coop.tribe`) and no health, like the host's member.
3. If the host's member gets health again within 60 s, the guest shows it alive again.

--nofix: `coop.tribe fix ko 0` (the code before): must FAIL.

  SPORE_LANE=7 python3 tools/tests/coop_tribe_ko.py [--nofix]
"""
import argparse
import re
import time

from twoinst import Checks, start


def members(con):
    """id -> (player tribe, hp, dead)"""
    out, player = {}, False
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe \d+( \(player\))?', line)
        if m:
            player = m.group(1) is not None
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=-?\d+ .*?hp=([\d.]+) .*? dead=(\d)', line)
        if m and int(m.group(1)):
            out[int(m.group(1))] = (player, float(m.group(2)), m.group(3) == '1')
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    for c in (host, guest):
        print(' ', c.call(f'coop.tribe fix ko {0 if a.nofix else 1}')['out'].strip())
    time.sleep(2)
    hm = members(host)
    npc = [i for i, (p, hp, d) in hm.items() if not p and hp > 0 and not d]
    mine = [i for i, (p, hp, d) in hm.items() if p and hp > 0]
    ck(npc and mine, f'the host has NPC tribe members {npc} and own members {mine}')
    if not (npc and mine):
        return ck.result()
    v = npc[0]
    gm = members(guest)
    ck(v in gm and not gm[v][2], f'the guest shows NPC member #{v} alive first ({gm.get(v)})')
    print('  host hits it:', host.call(f'coop.tribe pincer {v} {mine[0]} 1000')['out'].strip(), flush=True)
    time.sleep(3)
    h, g = members(host).get(v), members(guest).get(v)
    print(f'  #{v} host {h} guest {g}', flush=True)
    ck(h and h[2] and h[1] <= 0, 'the host\'s member is knocked out (dead flag, no health)')
    ck(g and g[2] and g[1] <= 0, f'the guest\'s mirror lies knocked out too (dead flag, no health): {g}')
    t0 = time.time()
    while time.time() - t0 < 60:
        h = members(host).get(v)
        if h and h[1] > 0 and not h[2]:
            time.sleep(3)
            g = members(guest).get(v)
            print(f'  #{v} back on the host after {time.time() - t0:.0f} s; guest {g}', flush=True)
            ck(g and not g[2] and g[1] > 0, f'the guest shows it alive again ({g})')
            break
        time.sleep(2)
    else:
        print('  (the host\'s member did not get up within 60 s: the respawn is not checked)', flush=True)
    print(' ', [l for l in guest.call('coop.tribe')['out'].splitlines() if 'lifetime' in l][0][:160], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
