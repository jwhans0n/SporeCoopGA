#!/usr/bin/env python3
"""Cell stage: `coop.cell spawn <slot>` with a slot that is not a live cell (BUG-0093).

The debug command took the slot from atoi and read a model pointer and a scale at
data + slot * 0x398, with no range or liveness check: `coop.cell spawn 99999` or `-1` read far
outside the cell pool and passed what it found to CreateCellObject (a fault in the game it was
typed into).

1. The host's pool: `coop.cell list` shows the live cells.
2. `coop.cell spawn 99999`, `-1`, `abc`, `0` and the slot one past the live range: each answers
   "no such cell", the game keeps running, no fault.
3. A live slot still spawns ("created").

The FAIL run is the code before the fix (main's mods/coop/debug.cpp): the first bad slot faults.
"""
import re

from twoinst import Checks, start


def main():
    host, guest = start('cell')
    ck = Checks()
    lst = host.call('coop.cell list')['out']
    slots = [int(s) for s in re.findall(r'^[ >]#(\d+) ', lst, re.M)]
    ck(slots, f'the host lists its live cells ({len(slots)})')
    past = (max(slots) + 1) if slots else 4095
    for bad in ('99999', '-1', 'abc', '0', str(past)):
        try:
            out = host.call(f'coop.cell spawn {bad}')['out'].strip()
        except OSError as e:  # the game hung or died in the command (the bug)
            ck(False, f'coop.cell spawn {bad}: no answer ({e})')
            return ck.result()
        ck('no such cell' in out or (bad == str(past) and 'created' in out and past in slots),
           f'coop.cell spawn {bad}: {out[:80]}')
        ck('Cell' in host.call('game.mode')['out'], f'the host still runs in the Cell stage after spawn {bad}')
    if slots:
        live = next((s for s in slots if not lst.count(f'>#{s} ')), slots[0])
        out = host.call(f'coop.cell spawn {live}')['out'].strip()
        ck('created' in out, f'a live slot still spawns: {out[:80]}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
