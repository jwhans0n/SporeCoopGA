#!/usr/bin/env python3
"""Space: trips to a partner and the galaxy map's clicks (BUG-0126, BUG-0072, BUG-0063, BUG-0090,
BUG-0145).

* BUG-0126: `coop.space empire contact` three times leaves the x87 register stack empty (the
  game's ApplyRelationship returns a float in ST0; a void call left it there, 8 calls = NaN
  floats everywhere). Read with `coop.space fpu` between frames.
* BUG-0072: the host jumps to the guest with the co-op window's jump (`coop.space jump`, any
  partner): the notice names the guest, not player 0 (the host itself).
* BUG-0063: both in the galaxy view, the guest flying: `coop.space map go 1` (what a click on the
  guest's tag does) is refused while the host's ship is disabled, and with no energy left the
  teleport costs neither energy nor HP (SpaceTeleportTo spent energy, and with none it damaged
  the ship).
* BUG-0090 / BUG-0145: a click is the game window's WM_LBUTTONDOWN. The physical left button
  pressed over the guest's tag without a message to the game window (a click in another window
  over the game, or the secondary button of a swapped mouse) does nothing; the message alone
  (a swapped mouse's primary button: the physical right one) takes the host to the guest.

  SPORE_LANE=6 python3 tools/tests/coop_space_map_input.py [--without-fix]
"""
import re
import sys
import time

from coop_space_map import no_movie, place, sp, tags, to_galaxy, wait
from twoinst import Checks, start

DRIVE = 'interstellardrive'


def energy(con):
    m = re.search(r'energy ([\d.]+) max ([\d.]+) hp ([\d.]+)', sp(con, 'energy'))
    return (float(m.group(1)), float(m.group(2)), float(m.group(3))) if m else (-1.0, -1.0, -1.0)


def gpos(con, who):
    m = re.search(rf'roster {who} .* at (\S+) (\S+) (\S+) empire', sp(con, 'map roster', who))
    return tuple(float(m.group(i)) for i in (1, 2, 3)) if m else None


def gdist(con):
    a, b = gpos(con, 0), gpos(con, 1)
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5 if a and b else 1e9


def clicks(con):
    m = re.search(r'clicks=(\d+)', sp(con, 'map status'))
    return int(m.group(1)) if m else -1


def guest_tag(host):
    t = wait(lambda: [x for x in tags(host) if x['kind'] == 'player' and x['id'] == 1 and x['star'] == 0 and
                      x['at'][0] >= 0] or None, 20)
    return t[0] if t else None


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    for c in (host, guest):
        print('  drive:', sp(c, 'tool add', DRIVE), flush=True)
        no_movie(c)
    if without:
        for c in (host, guest):
            print(' ', sp(c, 'fpufix 0'), sp(c, 'jumpfix 0'), sp(c, 'map teleportfix 0').strip(),
                  sp(c, 'map clickfix 0').strip(), flush=True)
    home = place(host)[1]

    # ---- BUG-0126: the x87 stack after empire contacts ----
    npc = re.findall(r'^npc ([0-9A-F]{8})', sp(host, 'empire near 6'), re.M)
    ck(npc, f'an NPC empire star near home: {npc[:3]}')
    if npc:
        f0 = sp(host, 'fpu')
        for _ in range(3):
            r = sp(host, 'empire contact', npc[0])
        f1 = sp(host, 'fpu')
        print(f'  {r}: {f0} -> {f1}', flush=True)
        # (a leaked value moves TOP; seen with the tag word still "empty": top=5 after 3 calls)
        ck('top=0' in f1 and 'used=0' in f1, f'the x87 stack is empty after 3 contacts ({f1})')

    # ---- BUG-0072: the jump to "any partner" names the partner it went to ----
    near = [int(s, 16) for s in re.findall(r'^free ([0-9A-F]{8})', sp(host, 'empire near 4'), re.M)]
    ck(near, 'a free star near home')
    star = near[0] & 0xFFFFFF if near else 0
    for _ in range(10):
        no_movie(guest)
        r = sp(guest, 'jumpto', hex(star))
        if r == 'jumping':
            break
        time.sleep(2)
    ck(wait(lambda: place(guest)[1] == star, 30) is not None, f'the guest is at star {star:08X}')
    time.sleep(3)  # (the host learns the guest's place)
    no_movie(host)
    r = sp(host, 'jump')
    ck(r == 'jumping', f'host: jump to the partner: {r}')
    ck(wait(lambda: place(host)[1] == star, 30) is not None, 'the host jumped to the guest\'s star')
    st = host.call('coop.status')['out']
    line = [l for l in st.splitlines() if 'jumped to ' in l]
    print('  host notice:', line[-1:] if line else None, flush=True)
    ck(line and "jumped to Bob's star system" in line[-1], f'the notice names the guest ({line[-1:] if line else "none"})')
    no_movie(host)

    # ---- BUG-0063: to a partner flying in the galaxy view ----
    ck(to_galaxy(guest) and to_galaxy(host), 'both in the galaxy view')
    no_movie(guest)
    no_movie(host)
    # the guest flies off a bit: its ship is between stars, the host's tag is on it
    print('  guest goes home:', sp(guest, 'jumpto', hex(home)), flush=True)
    wait(lambda: place(guest)[1] == home, 30)
    ck(to_galaxy(guest), 'the guest is in the galaxy view again (at home)')
    no_movie(guest)
    time.sleep(3)
    t = guest_tag(host)
    ck(t is not None, f'the host draws the guest\'s tag on its ship: {t}')
    d0 = gdist(host)
    print('  host:', sp(host, 'disable 1'), flush=True)
    r = sp(host, 'map go 1')
    time.sleep(2)
    ck(r.startswith('the ship is disabled') and gdist(host) > 0.3 and abs(gdist(host) - d0) < 0.2,
       f'disabled: the trip is refused ({r}; distance {d0:.2f} -> {gdist(host):.2f})')
    print('  host:', sp(host, 'disable 0'), flush=True)
    sp(host, 'energy 0')
    e0 = energy(host)
    r = sp(host, 'map go 1')
    ck(wait(lambda: gdist(host) < 0.3, 15) is not None, f'map go: the host is next to the guest ({r})')
    time.sleep(1)
    e1 = energy(host)
    print(f'  energy/hp {e0} -> {e1}', flush=True)
    ck(e1[0] == e0[0] and e1[2] >= e0[2], f'the trip cost no energy and no HP (energy {e0[0]} -> {e1[0]}, hp {e0[2]} -> '
                                         f'{e1[2]})')
    sp(host, 'energy', e0[1])

    # ---- BUG-0090 / BUG-0145: the click comes from the game window ----
    print('  guest to the free star:', sp(guest, 'jumpto', hex(star)), flush=True)
    wait(lambda: place(guest)[1] == star, 30)
    ck(to_galaxy(guest), 'the guest is in the galaxy view at the other star')
    no_movie(guest)
    time.sleep(3)
    t = guest_tag(host)
    ck(t is not None, f'the host draws the guest\'s tag: {t}')
    if t:
        b = t['box']
        x, y = (b[0] + b[2]) // 2, (b[1] + b[3]) // 2
        d0, c0 = gdist(host), clicks(host)
        # the physical left button over the tag, no message to the game window
        host.call(f'move {x} {y}')
        time.sleep(0.5)
        host.call('key 0x01 down')
        time.sleep(0.8)
        host.call('key 0x01 up')
        time.sleep(3)
        ck(clicks(host) == c0 and gdist(host) > 0.3,
           f'a button press outside the game window does nothing (clicks {c0} -> {clicks(host)}, distance '
           f'{d0:.2f} -> {gdist(host):.2f})')
        t = guest_tag(host) or t
        b = t['box']
        x, y = (b[0] + b[2]) // 2, (b[1] + b[3]) // 2
        c0 = clicks(host)
        print('  postclick:', sp(host, 'map postclick', x, y), flush=True)
        ck(wait(lambda: gdist(host) < 0.3, 15) is not None,
           f'the primary click\'s message alone takes the host to the guest (clicks {c0} -> {clicks(host)})')

    ck.faults(host=host, guest=guest)
    if without:
        for c in (host, guest):
            for s in ('fpufix 1', 'jumpfix 1', 'map teleportfix 1', 'map clickfix 1'):
                sp(c, s)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
