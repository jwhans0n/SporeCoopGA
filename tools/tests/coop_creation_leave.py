#!/usr/bin/env python3
"""After the session the guest's own creation of the same id is its own again (BUG-0055).

Creation ids are per profile and sequential, so a guest often owns another creation under the key
of the host's creature. During a session the guest registers the host's creature under that key
above its own My Creations (the host's world names its creatures by it). Nothing unregistered it at
the session end: for the rest of the game process the key resolved to the host's records, also in
the guest's own games (Sporepedia, editor, saves).

The host's creation is the TigerHorse of its My Creations (`coop.creations choose`); the guest's My
Creations get another creature (the Kirgle) under its key (`coop_nest_species.colliding_creations`,
as in coop_nest_species.py); the guest joins, gets the host's creation and
leaves the session (`coop.leave`). The bem the guest's game finds for that key (its resource
manager, `coop.creations bem`) must be the host's during the session and not the host's after it.

  coop_creation_leave.py [--without-fix]   (--without-fix: coop.creations worlddrop off, the old
                                            behaviour, so the test must FAIL)"""
import sys
import time

from coop_nest_species import HOST_ONLY, OTHER, bem, restart_guest
from twoinst import Checks, start


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    key = HOST_ONLY
    print(' ', host.call(f'coop.creations choose creature {key:08X}')['out'].strip())
    host_bem = bem(host, key)
    ck(bool(host_bem), f'the host\'s creation is its own {key:08X} (bem {host_bem})')
    # the guest again, with its own creation under the key of the host's
    guest, ok = restart_guest(host)
    ck(ok, 'the guest with a creation of its own under the host\'s key joined')
    if without:
        print(' ', guest.call('coop.creations worlddrop off')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 30 and bem(guest, key) != host_bem:
        time.sleep(1)
    during = bem(guest, key)
    ck(during == host_bem, f'during the session the key is the host\'s creation on the guest ({during})')
    print(' ', guest.call('coop.leave')['out'].strip())
    time.sleep(3)
    after = bem(guest, key)
    other = bem(guest, OTHER)
    print(f'  guest after leaving: bem {key:08X} = {after}, its own Kirgle {OTHER:08X} = {other}')
    ck(after is not None and after != host_bem,
       f'after the session the key is the guest\'s own creation again ({after}, host\'s {host_bem})')
    ck(after == other, 'it is the guest\'s own creature (the Kirgle\'s body)')
    print(' ', guest.call('coop.creations worlddrop on')['out'].strip())
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
