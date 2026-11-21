#!/usr/bin/env python3
"""Space: another thread does not switch the planet seeding off (BUG-0123).

Each game generates the planets of a star the first time it is needed; the generating thread's
draws come from a generator seeded by the star, so both games make the same planets
(docs/re/space.md 7.3). The state that says "this thread generates, outside the names" was one
for the whole process: a thread inside GenerateRandomName (or two threads changing the counter at
once) switched the seeding off. Forced here: `coop.space namesheld <ms>` keeps another thread
"inside the name hook" (the hook's bookkeeping, no game call) while the guest jumps into a star
nobody visited. Both games then must have the same planets of that star (the PlanetIDs the game
loaded in the system).

  SPORE_LANE=6 python3 tools/tests/coop_space_seed_thread.py [--without-fix]
"""
import re
import sys
import time

from coop_space_jump import place, settle, sp, system_planets
from twoinst import Checks, start


def fresh_free(con, n=8):
    """unvisited stars without an empire whose planets were not generated yet"""
    out = sp(con, 'empire', 'near', n)
    return [int(m.group(1), 16) for m in re.finditer(
        r'^free ([0-9A-F]{8}) pol=\S+ d=\S+ contact=\d flags=\S+ visited=0 planets=0', out, re.M)]


def jump(con, who, star):
    for _ in range(10):
        settle(con, who, 20)
        r = sp(con, 'jumpto', hex(star))
        if r == 'jumping':
            break
        time.sleep(2)
    for _ in range(30):
        if place(con)[1] == star:
            return True
        time.sleep(1)
    return False


def planets(con, star):
    for _ in range(10):
        p = sorted(system_planets(con, star)[0])
        if p:
            return p
        time.sleep(1)
    return []


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if without:
        print(' ', sp(guest, 'seedfix 0'))
    stars = [s for s in fresh_free(host) if s in fresh_free(guest)]
    print('  fresh stars:', [f'{s:08X}' for s in stars])
    if not ck(len(stars) >= 2, 'two stars near home whose planets nobody generated'):
        return ck.result()
    for k, star in enumerate(stars[:2]):
        print(' ', sp(guest, 'namesheld 25000'))
        ck(jump(guest, 'guest', star), f'round {k + 1}: the guest jumped to {star:08X}')
        ck(jump(host, 'host', star), f'round {k + 1}: the host followed')
        settle(host, 'host')
        settle(guest, 'guest')
        hp, gp = planets(host, star), planets(guest, star)
        print(f'  host {[f"{i:08X}" for i in hp]}\n  guest {[f"{i:08X}" for i in gp]}')
        ck(hp and hp == gp, f'round {k + 1}: both games made the same planets of {star:08X} '
                            f'({len(hp)} and {len(gp)})')
        time.sleep(26 - 20)  # the held thread lets go before the next round
    print(' ', sp(guest, 'namesheld 0'))
    ck.faults(host=host, guest=guest)
    if without:
        sp(guest, 'seedfix 1')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
