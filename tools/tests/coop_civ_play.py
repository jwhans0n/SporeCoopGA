#!/usr/bin/env python3
"""Civilization co-op mechanics through the game's own functions (not a real battle), on a planet
with cities of other nations (fixture civ_npc):
joint siege (capture damage x1.5 when two players attack a city), the alarm for an attacked
player city (everybody is told, beacons), the superweapon fired by a vote."""
import re
import time

from coop_civ import civ
from coop_ui import Ui
from twoinst import Checks, shot, start


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(4)
    hc, _ = civ(host)
    npc = [k for k, v in hc.items() if not v['player']]
    mine = [k for k, v in hc.items() if v['player']]
    print('  cities: player', mine, 'npc', npc[:6])
    # a city of another nation (the fixture has three), besieged by both players
    ck(npc, f'the planet has cities of other nations: {npc}')
    if npc:
        c = npc[0]
        r1 = host.call(f'coop.civ siege {c} 0')['out']
        r2 = host.call(f'coop.civ siege {c} 1')['out']
        print('  host hit:', r1, '| guest hit:', r2)
        d = [float(b) - float(a) for a, b in re.findall(r'capture ([\d.]+) -> ([\d.]+)', r1 + ' ' + r2)]
        ck(len(d) == 2 and abs(d[0] - 1.0) < 0.05 and abs(d[1] - 1.5) < 0.05,
           f'a city attacked by two players falls 50% faster (capture +{d})')
        ck('joint siege' in host.call('coop.status')['out'], 'everybody is told about the joint siege')
    if mine:
        print(' ', host.call(f'coop.civ alarm {mine[0]}')['out'])
        time.sleep(2)
        ck('under attack: help' in guest.call('coop.status')['out'], 'the guest is told the player city is attacked')
        shot(guest, 'civ_alarm_guest.png')
    # the superweapon waits for a vote
    print(' ', host.call('coop.civ super 0')['out'])
    time.sleep(2)
    ck('vote active' in guest.call('coop.vote')['out'], 'a superweapon starts a vote')
    gu = Ui(guest, 'guest')
    gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
    # the window has just opened for the vote: a click in its first frames was lost twice in seven
    # runs (BUG-0176; one by hand seconds later worked). Settle first, click again like a player
    # when nothing happened; how many clicks it took is printed
    time.sleep(1.0)
    clicks = 0
    for clicks in range(1, 4):
        gu.click('ready')
        time.sleep(3)
        if 'the superweapon fires' in host.call('coop.status')['out']:
            break
    print(f'  the guest\'s "I\'m ready" took {clicks} click(s)')
    st = host.call('coop.status')['out']
    ck('the superweapon fires' in st, 'it fires when everybody is ready')
    print(' ', st.split('civ(host)')[1].splitlines()[0])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
