#!/usr/bin/env python3
"""A guest that joins gets the game the host plays right now, not an older one (player report
2026-10-24: "the host started a completely new save, the guest was put into an older save of the
host; the host had to delete it").

Like a player: the host has an older game in its profile (fixture `creature`: Game0/Scalinus) and
starts a NEW game from the galaxy menu (a free disc, a stage, a planet name it types itself), hosts,
and the guest joins from its menu. Spore keeps every planet of the galaxy in the same slot folder
(Game0 then holds Scalinus.spo and the new planet), so the slot alone does not say which game is
played: the host names its planet (CurrentGameName) and the guest loads that one.
Before the fix a planet name with letters outside ASCII ("Земля") was read as "_____.spo": the host
did not find its file and sent the slot with the name of another planet in it (the old game), and
the guest built the name for the load from UTF-8 bytes, so "Земля.spo" did not load at all.

  SPORE_LANE=N python3 tools/tests/coop_join_new_game.py [--stage cell|creature] [--name NAME]
"""
import argparse
import os
import subprocess
import sys
import time

from twoinst import Checks, HERE, restore_creations, wait_guest_in, wait_up, watch, shot  # (sets the path)
import lane  # noqa: E402
from lane import sh  # noqa: E402
from sporectl import Console  # noqa: E402


def world_of(con):
    """the planet file the game plays ("Name.spo"), from coop.avatar"""
    out = con.call('coop.avatar').get('out', '')
    first = out.splitlines()[0] if out else ''
    if "world '" not in first:
        return None
    return first.split("world '", 1)[1].rsplit("'", 1)[0]


def files_of(d):
    try:
        return sorted(os.listdir(d))
    except OSError:
        return []


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--stage', default='cell', choices=('cell', 'creature'))
    ap.add_argument('--name', default='Земля', help='the new planet name the host types (the game drops '
                    "an apostrophe; '!' and spaces stay)")
    a = ap.parse_args()
    ck = Checks()
    old_planet = 'Scalinus'  # the fixture's game (tools/tests/fixtures.py)

    sh('kill')
    time.sleep(2)
    sh('deploy')
    sh('fixture', 'restore', 'creature', lane.HOST_PROFILE)
    restore_creations()
    subprocess.run(['rm', '-rf', lane.GUEST_DIR + '/Games', lane.GUEST_DIR + '/Games-sporecoop-backup'])
    lane.run_guest()
    time.sleep(2)
    lane.run_host()
    for inst, run in ((lane.GUEST, lane.run_guest), (lane.HOST, lane.run_host)):
        if not wait_up(inst, 90):
            run()
            assert wait_up(inst), f'instance {inst} did not start'
    host, guest = Console(lane.HOST), Console(lane.GUEST)

    # the host starts a new game on a free disc of its galaxy (slot 0 is free in this fixture's galaxy)
    t0 = time.time()
    r = subprocess.run([sys.executable, os.path.join(HERE, 'new_stage_game.py'), a.stage, '-i', str(lane.HOST),
                        '--slot', '0', '--name', a.name], capture_output=True, text=True, timeout=900)
    print('  new game:', ' / '.join(l for l in r.stdout.splitlines() if l.strip())[-200:], flush=True)
    if not ck(r.returncode == 0, f'the host started a new {a.stage} game ({time.time() - t0:.0f} s)'):
        shot(host, 'join_new_game_wizard.png')
        return ck.result()
    # the invariants (freezes, crashes) from here: building a new planet can hold the console longer
    watch(lane.HOST, lane.GUEST)
    hw = world_of(host)
    ck(hw == a.name + '.spo', f'the host plays the new planet ({hw!r}, typed {a.name!r})')
    slot0 = os.path.join(lane.HOST_DIR, 'Games', 'Game0')
    print('  host Game0 before the join:', ', '.join(f for f in files_of(slot0) if f.endswith('.spo')), flush=True)

    host.call(lane.HOST_CMD)
    for _ in range(60):
        if 'GalaxyGameEntry' in guest.call('game.mode')['out']:
            break
        time.sleep(1)
    guest.call('wait.frames 200 60000')
    guest.call(lane.JOIN)
    t1 = time.time()
    joined = wait_guest_in(host, guest, 180)
    gw = world_of(guest)
    print(f'  guest in after {time.time() - t1:.0f} s: {joined}, plays {gw!r}', flush=True)
    spo = [f for f in files_of(slot0) if f.endswith('.spo')]
    ck(a.name + '.spo' in spo and old_planet + '.spo' in spo,
       f'the new game was saved next to the old one in the same slot (Game0: {", ".join(spo)})')
    ck(joined and gw == hw, f'the guest plays the host\'s current game ({gw!r} == {hw!r})')
    ck(gw != old_planet + '.spo', 'the guest did not get the host\'s older game')
    hs = host.call('coop.status')['out']
    ck('together 1 1' in hs, 'host and guest are in the same world')
    if not (joined and gw == hw):
        shot(guest, 'join_new_game_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
