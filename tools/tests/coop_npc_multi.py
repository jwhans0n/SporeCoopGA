#!/usr/bin/env python3
"""NPC sync with three or four players (creature stage, nobody moves): the host and Bob from lane A
(SPORE_LANE), Carol and Dave from lane B (SPORE_LANE_B, default: the same lane,
instances 20+N / 30+N; another lane B: another bottle, empty profiles,
so they join from the galaxy menu and load the world the host sends).

Every few seconds each guest's copy of every living NPC is compared with the host's:
  npc        90th percentile of the position error over the whole run <= --limit (1.5 m)
  ground     the guest's own avatar stands at the host's height for it (the distance from the planet's
             centre differs by < 1.5 m). A guest whose planet came up without its height map (a fault
             while it loaded the world inside the frame, fixed 2026-10-23) stood ~6 m low, and its NPC
             proxies fell through to that ground between the driver's teleports (3-9 m off the host)
  faults     no caught fault in any game

  SPORE_LANE=1 SPORE_LANE_B=8 python3 tools/tests/coop_npc_multi.py [--players 4] [--seconds 40]
"""
import argparse
import math
import subprocess
import sys
import time

import lane
from coop_multi import join_cmd, lane_b, my_key, sample, start_extra, wait_all
from coop_sync import pct
from twoinst import Checks, start


def radius(v):
    return math.sqrt(sum(x * x for x in v))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--players', type=int, default=4, choices=(3, 4))
    ap.add_argument('--seconds', type=float, default=40)
    ap.add_argument('--limit', type=float, default=1.5)
    a = ap.parse_args()
    host, bob = start('creature')
    ck = Checks()
    extra = start_extra(a.players - 2)
    t0 = time.time()
    for name, (c, _) in extra.items():
        c.call(join_cmd(name))
        time.sleep(1)
    ck(wait_all(host, a.players - 1), f'all {a.players - 1} guests are in the host\'s world ({time.time() - t0:.0f} s)')
    guests = {'Bob': bob, **{n: c for n, (c, _) in extra.items()}}
    time.sleep(4)  # proxies spawn and settle
    errs = {n: [] for n in guests}
    ground = {n: [] for n in guests}
    t_end = time.time() + a.seconds
    rnd = 0
    while time.time() < t_end:
        h1 = sample(host)
        line = []
        for name, g in guests.items():
            gs = sample(g)
            h2 = sample(host)
            e = [min(math.dist(h1[k], gs[k]), math.dist(h2.get(k, h1[k]), gs[k]))
                 for k in h1 if k[0] == 'n' and k in gs]
            errs[name] += e
            me = my_key(g, gs)
            if me and me in h1:
                ground[name].append(radius(gs[me]) - radius(h1[me]))
            bad = sum(x > 3 for x in e)
            line.append(f'{name} n={len(e)} p90={pct(e, 0.9):.2f} >3m={bad}')
        print(f'  {rnd * 4:3d}s ' + ' | '.join(line), flush=True)
        rnd += 1
        time.sleep(3)
    for name in guests:
        ck(len(errs[name]) > 0, f'{name} mirrors NPCs ({len(errs[name])} samples)')
        if errs[name]:
            p90 = pct(errs[name], 0.9)
            ck(p90 <= a.limit, f'{name}: living NPCs in sync (p90 {p90:.2f} m <= {a.limit} m, max {max(errs[name]):.1f} m)')
        if ground[name]:
            worst = max(ground[name], key=abs)
            ck(abs(worst) < 1.5, f'{name}: its avatar stands at the host\'s height ({worst:+.2f} m)')
    ck.faults(host=host, **guests)
    subprocess.run([lane.SPORE, 'kill'], env=lane_b()[1], capture_output=True)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
