#!/usr/bin/env python3
"""Space: hit messages about ships the game already freed, and star ids past the star table
(BUG-0054, BUG-0092), stale rescue timing (BUG-0147).

The game frees NPC ships, our mirrors and puppets on every context switch; a hit message handled
in the frame after that used to hand the freed ship to the game's TakeDamage as the attacker. The
one-frame window is held open by test commands: `coop.fleet freemirror <id>` (guest) frees a
mirror the way a context switch does and keeps its pointer listed until the next MSG_UFO_DAMAGE;
`coop.fleet freepuppet <p>` (host) frees the guest's puppet and makes it the next MSG_UFO_HIT's
attacker. The checks: the hit lands (the ship's hp on the receiving game drops), no freed ship
reaches TakeDamage (att_freed, counted where the game is called), no fault.
`coop.space starrec <id>`: a star id one past the last star of its sector finds no record and
reads nothing outside the sector's array. The guest is disabled twice next to the host: the
second repair starts from 0 (it kept the first one's last time step, BUG-0147).

  SPORE_LANE=6 python3 tools/tests/coop_space_stale.py [--without-fix]
"""
import re
import sys
import time

from twoinst import Checks, start


def num(con, key):
    m = re.search(r'space\(\w+\):.*?\b' + key + r'=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def ships(con):
    out = con.call('coop.fleet list')['out']
    d = {}
    for m in re.finditer(r'ship #(-?\d+) \S+ type=(\d+) pol=\w+ hp=([\d.]+)/([\d.]+)( \(me\))?', out):
        d.setdefault('me' if m.group(5) else int(m.group(1)), []).append((int(m.group(2)), float(m.group(3))))
    return d


def hp(con):
    m = re.search(r'hp ([\d.]+)', con.call('coop.space hp')['out'])
    return float(m.group(1)) if m else -1.0


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if without:
        for con in (host, guest):
            print(' ', con.call('coop.fleet hitfix 0')['out'].strip(), con.call('coop.space starfix 0')['out'].strip())

    # ---- BUG-0054, guest: MSG_UFO_DAMAGE whose attacker mirror was just freed
    print(' ', host.call('coop.fleet spawn 2')['out'].strip())
    time.sleep(3)
    hs, gs = ships(host), ships(guest)
    npc = [k for k, v in hs.items() if k != 'me' and k >= 0 and v[0][0] == 2 and k in gs]
    ck(npc, f'the host has an NPC ship mirrored on the guest {npc}')
    if npc:
        sid = npc[0]
        h0, d0, f0 = hp(guest), num(guest, 'damage_in'), num(guest, 'att_freed')
        r = guest.call(f'coop.fleet freemirror {sid}')['out'].strip()
        ck(r == 'freed', f'guest: mirror #{sid} freed by the game ({r})')
        print(' ', host.call(f'coop.fleet hurt {sid} 1 5')['out'].strip())
        time.sleep(1.5)
        h1 = hp(guest)
        ck(num(guest, 'damage_in') > d0, 'guest: the hit message was applied')
        ck(0 <= h1 < h0, f'guest: the own ship took the hit (hp {h0} -> {h1})')
        ck(num(guest, 'att_freed') == f0, f'guest: no freed mirror reached TakeDamage (att_freed {f0} -> '
                                          f'{num(guest, "att_freed")})')

        # ---- BUG-0054, host: MSG_UFO_HIT while the guest's puppet was just freed
        time.sleep(3)  # the guest spawns the mirror again with the next ship state
        hs = ships(host)
        hp0, n0, f0 = hs.get(sid, [(0, -1)])[0][1], num(host, 'hits_in'), num(host, 'att_freed')
        r = host.call('coop.fleet freepuppet 1')['out'].strip()
        ck(r == 'freed', f'host: the guest puppet freed by the game ({r})')
        print(' ', guest.call(f'coop.fleet shoot {sid} 5')['out'].strip())
        time.sleep(1.5)
        hs = ships(host)
        ck(num(host, 'hits_in') > n0, 'host: the guest shot was applied')
        ck(sid in hs and hs[sid][0][1] < hp0, f'host: ship #{sid} took the shot (hp {hp0} -> '
                                              f'{hs.get(sid, [(0, -1)])[0][1]})')
        ck(num(host, 'att_freed') == f0, f'host: no freed puppet reached TakeDamage (att_freed {f0} -> '
                                         f'{num(host, "att_freed")})')

    # ---- BUG-0092: a star id past the end of its sector
    m = re.search(r'star (\d+)', host.call('coop.space star')['out'])
    star = int(m.group(1)) if m else 0
    r = host.call(f'coop.space starrec {star}')['out'].strip()
    print(' ', r)
    m = re.search(r'rec=(\w+) sector_size=(-?\d+)', r)
    ck(m and m.group(1) == 'yes' and int(m.group(2)) > 0, 'the own star is found by id')
    if m and int(m.group(2)) > 0:
        past = (star >> 12 << 12) | int(m.group(2))
        r = host.call(f'coop.space starrec {past:#x}')['out'].strip()
        print(' ', r)
        ck('rec=none' in r and 'read_outside=0' in r, f'star {past:08X} (one past the sector): no record, '
                                                      'nothing read outside the array')
        r = host.call(f'coop.space jumpto {past:#x}')['out'].strip()
        ck(r == 'no such star', f'jumpto {past:08X}: {r}')
    # ---- BUG-0147: the second disable's repair starts at 0, not with the old step of up to 0.5 s
    if without:
        print(' ', guest.call('coop.space repairfix 0')['out'].strip())
    reps = []
    for rnd in range(2):
        if rnd:
            time.sleep(62)  # the rescue has a 60 s cooldown
        guest.call('coop.space goto')
        time.sleep(0.5)
        guest.call('coop.space hit 100000')
        time.sleep(0.15)
        r = guest.call('coop.space hp')['out'].strip()
        m = re.search(r'down=(\d).*?repair=([\d.]+)', r)
        print(f'  disable {rnd + 1}: {r}')
        ck(m and m.group(1) == '1', f'disable {rnd + 1}: the guest ship is disabled next to the host')
        reps.append(float(m.group(2)) if m else -1)
        for _ in range(10):  # repaired by the host close by (2 s)
            if 'down=0' in guest.call('coop.space hp')['out']:
                break
            time.sleep(1)
        ck('down=0' in guest.call('coop.space hp')['out'], f'disable {rnd + 1}: repaired')
    ck(0 <= reps[1] < 0.45, f'the second repair starts from 0 (repair {reps[1]} s after 0.15 s)')

    ck.faults(host=host, guest=guest)
    if without:
        for con in (host, guest):
            con.call('coop.fleet hitfix 1')
            con.call('coop.space starfix 1')
            con.call('coop.space repairfix 1')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
