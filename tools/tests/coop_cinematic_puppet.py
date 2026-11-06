#!/usr/bin/env python3
"""A puppet removed while a cinematic plays stays a puppet until it is destroyed (BUG-0066).

`game::destroy_creature` keeps creatures until the running cinematic is over (the 0xC6AE60 crash,
docs/re/creature.md 16). It used to take them out of the puppet list first: while it waited, the
game's AI ran the creature again and the host's NPC sync sent it to the guests as one of its NPCs
(a guest saw an AI-driven copy of its own creature).

The host plays CRG_ClaimNest (its caption keeps it running) and the guest picks another creation
(`coop.creations next creature`), so the host builds a new puppet and removes the old one during
the cinematic. While the cinematic plays, the old puppet must not be one of the host's NPCs (no net
id, so nothing is mirrored on the guest; h_NPCTickAI skips puppets, so no AI). Afterwards it is gone.

  coop_cinematic_puppet.py [--without-fix]   (--without-fix: coop.npc deferpuppet off, the old
                                              behaviour, so the test must FAIL)"""
import re
import sys
import time

from twoinst import Checks, start

CLAIM = '63B7060A'  # CRG_ClaimNest


def eax(con, what):
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call ' + what)['out'])
    return int(m.group(1), 16) if m else None


def cinematic(con):
    m = re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])
    return m.group(1) if m else None


def puppet(con):
    m = re.search(r'remote 1 .*?puppet=([0-9A-F]{8})', con.call('coop.status')['out'])
    return int(m.group(1), 16) if m else 0


def host_ids(con):
    """host: {ptr: id} of the NPCs it shares"""
    return {int(m.group(2), 16): m.group(1)
            for m in re.finditer(r'#(\d+) ([0-9A-F]{8}) ', con.call('coop.npc list')['out'])}


def creature_pos(con, ptr):
    for line in con.call('coop.npc creatures')['out'].splitlines():
        m = re.search(r'creature ([0-9A-F]{8}) .* pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and int(m.group(1), 16) == ptr:
            return tuple(float(m.group(i)) for i in (2, 3, 4))
    return None


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return None


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without:
        print(' ', host.call('coop.npc deferpuppet off')['out'].strip())
    old = wait(lambda: puppet(host), 20)
    ck(bool(old), f'the host has the guest\'s puppet ({old and hex(old)})')
    cm = eax(host, 'Simulator::cCinematicManager::Get cdecl')
    print(' ', host.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {cm:#x} s:CRG_ClaimNest 1 1 0 0 0')['out'].strip())
    ck(wait(lambda: cinematic(host) == CLAIM, 10), 'the host plays CRG_ClaimNest')
    print(' ', guest.call('coop.creations next creature')['out'].strip())
    new = wait(lambda: (lambda p: p if p and p != old else 0)(puppet(host)), 30)
    playing = cinematic(host) == CLAIM
    ck(bool(new) and playing, f'the host built a new puppet ({new and hex(new)}) while the cinematic plays ({playing})')
    if new and playing:
        st = re.search(r'deferred=(\d+) done_after=(\d+) pending=(\d+)', host.call('coop.npc')['out'])
        print('  host', st.group(0) if st else '?')
        p0 = creature_pos(host, old)
        ids, seen = [], 0
        t0 = time.time()
        while time.time() - t0 < 6 and cinematic(host) == CLAIM:
            i = host_ids(host).get(old)
            seen += 1
            if i:
                ids.append(i)
            time.sleep(0.5)
        p1 = creature_pos(host, old)
        print(f'  old puppet {old:08X}: pos {p0} -> {p1}, host npc ids {sorted(set(ids))} in {seen} reads')
        ck(p0 is not None, 'the old puppet waits in the world for the end of the cinematic')
        ck(not ids, f'the old puppet is no NPC of the host while it waits (ids {sorted(set(ids))})')
        # (its position is no sign of AI: with the fix it still slid 1.4 m on its last velocity, and
        # 0.4 m without it, lane 5 2026-10-28)
    # the caption: Enter until the cinematic is over
    t0 = time.time()
    while time.time() - t0 < 40 and cinematic(host):
        host.call('key enter')
        time.sleep(2)
    ck(cinematic(host) is None, 'the cinematic is over')
    gone = wait(lambda: creature_pos(host, old) is None, 10)
    ck(gone, 'after the cinematic the old puppet is gone')
    host.call('coop.npc deferpuppet on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
