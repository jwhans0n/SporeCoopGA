#!/usr/bin/env python3
"""A partner's animation in the frame after a world teardown never reaches the freed puppet
(BUG-0091). The host's avatar plays animations all the time (`coop.overlay animsend`: the message a real
animation sends; idle keys play none without a target) while the
guest leaves the world for the galaxy menu (`coop.menu`) and gets the host's world back, several
times. The host's animation messages that arrive while the guest's world is torn down used to be
handled at the start of the next frame, before the mod noticed the teardown: play_animation ran on
the freed puppet. The guest counts every animation call on a puppet the game no longer lists
(`coop.framefix`: made=N) and must make none; neither game may fault.

  coop_anim_teardown.py [--rounds 6] [--without-fix]   (--without-fix: coop.framefix 0, the old
                                                         order, so the test must FAIL)"""
import argparse
import re
import threading
import time

import lane
from twoinst import Checks, Console, start


def made(con):
    out = con.call('coop.framefix')['out']
    m = re.search(r'anims dropped=(\d+) made=(\d+), despawns dropped=(\d+) made=(\d+)', out)
    return tuple(int(x) for x in m.groups()) if m else (-1, -1, -1, -1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=6)
    ap.add_argument('--without-fix', action='store_true')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    if a.without_fix:
        print(' ', guest.call('coop.framefix 0')['out'].strip())
    stop = threading.Event()

    def animate():  # the host's avatar "plays" a reaction animation: the same MSG_ANIM as a real one
        con = Console(lane.HOST)  # a connection of its own: the main thread talks to the host too
        while not stop.is_set():
            try:
                con.call('coop.overlay animsend F37DB906')
            except (OSError, ValueError):
                pass
            time.sleep(0.1)
    time.sleep(3)
    rx0 = re.search(r'received (\d+)', guest.call('coop.status')['out'])
    th = threading.Thread(target=animate, daemon=True)
    th.start()
    time.sleep(4)
    rx1 = re.search(r'received (\d+)', guest.call('coop.status')['out'])
    ck(rx0 and rx1 and int(rx1.group(1)) > int(rx0.group(1)),
       f'the guest receives the host\'s animations ({rx0 and rx0.group(1)} -> {rx1 and rx1.group(1)})')
    back_all = True
    for n in range(a.rounds):
        print(f'  round {n + 1}:', guest.call('coop.menu')['out'].strip(), flush=True)
        t0 = time.time()
        left = back = False
        while time.time() - t0 < 150:
            mode = guest.call('game.mode')['out']
            if 'Creature' not in mode:
                left = True
            if left and 'Creature' in mode and guest.call('coop.avatar')['ok'] \
                    and 'together 1 1' in host.call('coop.status')['out']:
                back = True
                break
            time.sleep(0.5)
        print(f'    back after {time.time() - t0:.0f} s, {made(guest)}', flush=True)
        back_all &= back
        if not back:
            break
        time.sleep(3)
    stop.set()
    th.join(2)
    ck(back_all, f'the guest came back into the host\'s world {a.rounds} times')
    dropped, unlisted, d_dropped, d_unlisted = made(guest)
    print(f'  on a puppet the game no longer lists: anims dropped={dropped} made={unlisted}, '
          f'despawns dropped={d_dropped} made={d_unlisted}')
    ck(unlisted == 0 and d_unlisted == 0, 'no animation or despawn reached a puppet the game had freed')
    played = re.search(r'played (\d+)', guest.call('coop.status')['out'])
    ck(played and int(played.group(1)) > 0, f'the host\'s animations play on its puppet ({played and played.group(1)})')
    guest.call('coop.framefix 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
