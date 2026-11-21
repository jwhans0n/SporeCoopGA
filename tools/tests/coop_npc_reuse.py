#!/usr/bin/env python3
"""A creature object the game reuses for another NPC gets a new net id (BUG-0044), and a net id is
never given to a second living NPC (BUG-0023).

The host gives NPC net ids by creature object. The game frees creatures into a pool and builds new
ones in the same object; the new NPC used to inherit the old id within the 2 s the host keeps an id,
no spawn went out, and the guest went on showing the old species, or a corpse dragged along the new
creature's path when the old one had died.

1. The host kills an NPC (`coop.npc hosthit`); then its object becomes a living creature of the
   species again (`coop.npc reuse`: what host_tick sees when the game's Create takes the freed
   object from its creature pool, 0xAD2020; a natural reuse could not be forced). On the guest, the mirror of the host's creature
   (by the host's id of that object) must be alive in the guest's game and of its species
   (`coop.npc creatures`: the game's death flag and species key), not the old corpse.
2. The id allocator is pointed at an id a living NPC has (`coop.npc nextid`) and another NPC is
   made (`coop.npc epic`): it must get an id no other NPC has.

  coop_npc_reuse.py [--without-fix]   (--without-fix: coop.npc reusefix off / idfix off, the old
                                       behaviour, so the test must FAIL)"""
import re
import sys
import time

from twoinst import Checks, start


def npcs(con):
    """host: {id: (ptr, species, hp, dist, dead)}"""
    d = {}
    for m in re.finditer(r'#(\d+) ([0-9A-F]{8}) species=([0-9A-F]+) hp=([\d.]+) dist=([\d.]+)( DEAD)?',
                         con.call('coop.npc list')['out']):
        d[m.group(1)] = (int(m.group(2), 16), m.group(3), float(m.group(4)), float(m.group(5)), bool(m.group(6)))
    return d


def proxies(con):
    """guest: {id: (ptr, species, dead)} of spawned mirrors"""
    d = {}
    for m in re.finditer(r'#(\d+) species=([0-9A-F]+) spawned hp=[\d.]+ dist=[\d.-]+ err=\S+ age=\S+( DEAD)? want=.* ptr=([0-9A-F]+)',
                         con.call('coop.npc list')['out']):
        d[m.group(1)] = (int(m.group(4), 16), m.group(2), bool(m.group(3)))
    return d


def game_creature(con, ptr):
    """the guest's game: (species, dead) of the creature object at ptr, None if it is not there"""
    for line in con.call('coop.npc creatures')['out'].splitlines():
        m = re.search(r'creature ([0-9A-F]{8}) species=([0-9A-F]+) ', line)
        if m and int(m.group(1), 16) == ptr:
            return m.group(2).zfill(8), ' DEAD' in line
    return None


def family(con):
    m = re.search(r'key=0x([0-9A-F]+)', con.call('coop.avatar')['out'])
    return m.group(1).zfill(8) if m else ''


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without:
        print(' ', host.call('coop.npc reusefix off')['out'].strip())
        print(' ', host.call('coop.npc idfix off')['out'].strip())
    time.sleep(3)
    fam = family(host)
    hn = npcs(host)
    prox = proxies(guest)
    cand = sorted((n[3], i) for i, n in hn.items() if i in prox and not n[4] and n[2] > 0 and n[1] != fam and n[3] < 90)
    ck(bool(cand), f'host NPCs mirrored on the guest ({len(cand)})')

    # ---- 1. a dead NPC's object reused for a new creature of its species
    reused = None
    for _, nid in cand[:4]:
        n = hn[nid]
        host.call(f'coop.npc hosthit {nid} 100000')
        t0 = time.time()
        while time.time() - t0 < 6 and not (proxies(guest).get(nid, (0, '', False))[2]):
            time.sleep(0.5)
        res = host.call(f'coop.npc reuse {nid}')['out'].strip()
        print(' ', res)
        if 'alive again' in res:
            reused = (nid, n[0], n[1])
            break
    ck(reused is not None, f'a dead NPC\'s object is a living creature again ({reused})')
    if reused:
        nid, ptr, sp = reused
        time.sleep(4)
        now = {i: n for i, n in npcs(host).items() if n[0] == ptr}
        hid = next(iter(now), None)
        ck(hid is not None and not now[hid][4], f'host: object {ptr:08X} is a living {sp} (id {hid}, was #{nid})')
        print(' ', host.call('coop.npc')['out'].splitlines()[0])
        px = proxies(guest).get(hid) if hid else None
        gc = game_creature(guest, px[0]) if px else None
        print(f'  guest mirror of #{hid}: {px}, its game creature: {gc}')
        ck(gc is not None and gc[0] == sp.zfill(8),
           f'guest: the mirror of host #{hid} is a {sp} in the guest\'s game ({gc and gc[0]})')
        ck(gc is not None and not gc[1], f'guest: the mirror of host #{hid} is alive in the guest\'s game, not the corpse')

    # ---- 2. the allocator never repeats a living NPC's id
    hn = npcs(host)
    live = sorted(int(i) for i in hn)
    if live:
        print(' ', host.call(f'coop.npc nextid {live[0]}')['out'].strip())
        print(' ', host.call('coop.npc epic 0 8')['out'].strip())
        time.sleep(1.5)
        lines = re.findall(r'#(\d+) ([0-9A-F]{8}) species', host.call('coop.npc list')['out'])
        ids = [i for i, _ in lines]
        dup = sorted({i for i in ids if ids.count(i) > 1})
        ck(not dup, f'every host NPC has an id of its own after the allocator met id {live[0]} in use (repeated: {dup})')
    host.call('coop.npc reusefix on')
    host.call('coop.npc idfix on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
