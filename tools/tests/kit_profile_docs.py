#!/usr/bin/env python3
"""A test profile keeps the game out of the player's Documents (BUG-0156, one game of the lane).

Spore keeps "My Spore Creations" (creation cards, screenshots, videos) in Documents; a profile
redirected only AppData, so a test on lane 1 wrote into the owner's own Documents. The game starts
with a fresh profile and must make its "My Spore Creations" under
AppData/Roaming/SporeKit-<profile>/Documents, and must not change anything in the bottle's own
Documents/My Spore Creations (what the game does by itself at start, no command forces it).

  --old    start with -sporekit-profile-docs:0 (AppData only, the code before the fix): must FAIL

  SPORE_LANE=3 python3 tools/tests/kit_profile_docs.py [--old]
"""
import os
import shutil
import sys
import time

import lane
from twoinst import Checks, wait_up
from sporectl import Console  # noqa: E402 (twoinst puts tools/ on the path)

PROFILE = 'docstest'
USERDIR = os.path.dirname(os.path.dirname(lane.APPDATA))  # drive_c/users/<user>
OWN = os.path.join(USERDIR, 'Documents', 'My Spore Creations')
PROF = os.path.join(lane.APPDATA, 'SporeKit-' + PROFILE)


def tree(root):
    """every file and folder under root with its size and mtime"""
    out = {}
    for d, dirs, files in os.walk(root):
        for n in dirs + files:
            p = os.path.join(d, n)
            try:
                st = os.stat(p)
            except OSError:
                continue
            out[os.path.relpath(p, root)] = (st.st_size if n in files else -1, st.st_mtime_ns)
    return out


def main():
    old = '--old' in sys.argv
    if old and lane.LANE == 1:
        sys.exit('kit_profile_docs --old: not on lane 1 (the game would use the owner\'s own Documents)')
    ck = Checks()
    lane.sh('kill')
    time.sleep(2)
    shutil.rmtree(PROF, ignore_errors=True)  # a fresh test profile
    before = tree(OWN)
    lane.sh('deploy')
    args = ['run', '-i', str(lane.HOST), '-p', PROFILE] + (['-x', '-sporekit-profile-docs:0'] if old else [])
    lane.sh(*args)
    ck(wait_up(lane.HOST, 180), 'the game starts')
    c = Console(lane.HOST)
    for _ in range(120):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 120 60000')
    time.sleep(3)
    mine = os.path.join(PROF, 'Documents', 'My Spore Creations')
    ck(os.path.isdir(mine), f'the game made My Spore Creations in the profile ({os.path.relpath(mine, lane.APPDATA)})')
    after = tree(OWN)
    changed = sorted(k for k in set(before) | set(after) if before.get(k) != after.get(k))
    ck(not changed, f'nothing changed in the bottle\'s own Documents/My Spore Creations ({changed[:5]})')
    lane.sh('kill')
    shutil.rmtree(PROF, ignore_errors=True)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
