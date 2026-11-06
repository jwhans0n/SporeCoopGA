#!/usr/bin/env python3
"""The guest's kill list never outlives the frame it was filled in (BUG-0082). On a guest the update
filter collects the game's own NPCs into a list during the game's update; guest_tick destroys them
later in the frame. On the frame the host goes away (main menu) guest_tick is skipped, and the list
used to stay until the host came back, possibly minutes later: guest_tick then destroyed pointers
the game had freed or reused meanwhile.

The test forces what the filter does on that frame: while the host is away the guest puts its
avatar into the list (`coop.npc killhold`, a safe stand-in: guest_tick never destroys the avatar),
and the list must be empty a moment later. Then the host loads its world again and both play on.

  coop_host_away_kill.py [--without-fix]   (--without-fix: coop.npc killfix off, the old
                                            behaviour, so the test must FAIL)"""
import re
import sys
import time

from twoinst import Checks, lane_state, start


def npc_field(con, name):
    m = re.search(name + r'=(\d+)', con.call('coop.npc')['out'])
    return int(m.group(1)) if m else -1


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('creature')
    ck = Checks()
    if without:
        print(' ', guest.call('coop.npc killfix off')['out'].strip())
    time.sleep(3)
    ck(npc_field(guest, 'spawned') > 0, 'the guest mirrors the host NPCs')
    print(' ', host.call('coop.menu')['out'].strip())
    away = None
    t0 = time.time()
    while time.time() - t0 < 90:
        if 'host_away=1' in guest.call('coop.status')['out']:
            away = time.time() - t0
            break
        time.sleep(0.5)
    ck(away is not None, f'the guest knows the host is away ({away and round(away)} s)')
    held = []
    for _ in range(5):
        guest.call('coop.npc killhold')
        time.sleep(0.5)
        held.append(npc_field(guest, 'kill_held'))
    ck(held and max(held) == 0, f'nothing collected while the host is away stays in the kill list ({held})')
    st = lane_state()
    slot, name = st.get('planet', 'Game0/Scalinus').split('/')
    if st.get('slot', 9) != 9:
        slot = f'Game{st["slot"]}'
    print(' ', host.call(f'coop.load {slot} {name}')['out'].strip())
    back = None
    t0 = time.time()
    while time.time() - t0 < 150:
        if host.call('coop.avatar')['ok'] and 'host_away=0' in guest.call('coop.status')['out'] and \
                npc_field(guest, 'spawned') > 0:
            back = time.time() - t0
            break
        time.sleep(1)
    ck(back is not None, f'the host is back: the guest mirrors its NPCs again ({back and round(back)} s)')
    ck(npc_field(guest, 'kill_held') == 0, 'the kill list is empty after the host is back')
    ck(guest.call('coop.avatar')['ok'], 'the guest plays on')
    guest.call('coop.npc killfix on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
