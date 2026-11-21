#!/usr/bin/env python3
"""Load the saved creature-stage game of the test profile (fixture 'creature').

Positions are for the windowed 1440x1080 test profile (fractions of the screen).
usage: load_game.py [-i instance]
"""
import os
import re
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from sporectl import Console, quote  # noqa: E402
import lane  # noqa: E402

SLOT = (0.896, 0.530)   # saved planet on the galaxy menu
PLAY = (0.643, 0.500)   # play button in the planet panel


def load_windows(c, instance, timeout=180):
    """Load this owned native fixture; galaxy slot coordinates vary by profile."""
    from windows_native_lane import backend
    slot, planet = backend().owned_planet(instance)
    if not c.call('game.mode').get('out', '').endswith(' GalaxyGameEntry'):
        raise RuntimeError('native fixture loader did not reach the galaxy menu')
    result = c.call(f'coop.load {slot} {quote(planet)}')
    if not result.get('ok') or result.get('out', '').strip() != f'loading {slot}/{planet}':
        raise RuntimeError('native fixture load was refused: ' + result.get('out', ''))
    expected = f"game_name '{planet}.spo'"

    def ready():
        mode = c.call('game.mode').get('out', '')
        avatar = c.call('coop.avatar')
        pauses = c.call('coop.pauses').get('out', '')
        if (mode.endswith(' Creature') and avatar.get('ok')
                and avatar.get('out', '').startswith('Creature:')
                and expected in avatar.get('out', '').splitlines()
                and re.search(r'\bpaused=0\b', pauses)):
            return avatar['out']
        return None

    deadline = time.monotonic() + timeout
    while not ready():
        if time.monotonic() >= deadline:
            raise RuntimeError('native Creature fixture did not become ready with its named avatar')
        time.sleep(.25)
    if not c.call('wait.frames 300 60000').get('ok'):
        raise RuntimeError('native Creature fixture stopped presenting after load')
    avatar = ready()
    if not avatar:
        raise RuntimeError('native Creature context changed while the fixture settled')
    return avatar


def main():
    inst = int(sys.argv[sys.argv.index('-i') + 1]) if '-i' in sys.argv else lane.HOST
    c = Console(inst)
    for _ in range(180):
        if 'GalaxyGameEntry' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    c.call('wait.frames 200 60000')
    if lane.WINDOWS:
        print(load_windows(c, inst))
        return
    w, h = 1440, 1080

    def click(f, frames=150):
        c.call('click %d %d' % (int(f[0] * w), int(f[1] * h)))
        c.call('wait.frames %d 30000' % frames)

    # the first click may only select/zoom to the planet: retry until the game starts
    for attempt in range(5):
        click(SLOT)
        click(SLOT)
        click(PLAY)
        started = False
        for _ in range(20):
            if 'Creature' in c.call('game.mode')['out']:
                started = True
                break
            time.sleep(1)
        if started:
            break
        print('retry', attempt + 1)
    for _ in range(180):
        if 'Creature' in c.call('game.mode')['out']:
            break
        time.sleep(1)
    for _ in range(180):
        if c.call('coop.avatar')['ok']:
            break
        time.sleep(1)
    c.call('wait.frames 300 60000')
    print(c.call('coop.avatar')['out'])


if __name__ == '__main__':
    main()
