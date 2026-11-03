#!/usr/bin/env python3
"""BUG-0149: digits typed in the cheat console are no social ability keys.

The guest engages a mirrored Kook (social stance, a click on it, as in coop_creature_social.py),
opens the cheat console (Ctrl+Shift+C) and types 1 and 2 there: the host gets no social act from
the guest (`acts_in` of the host's coop.social) and the guest's avatar plays nothing. After the
console is closed the key 1 works again (the host counts the act).

  coop_social_console.py [--without-fix]   (--without-fix: coop.social textfix off, must FAIL)

The console opens only while the cheat manager's flag +0x64 is set (cCheatManager vt 0x4C,
SetEnabled; AppShortcuts_OnKeyDown -> vt 0x44 -> 0x67E730 returns at once without it). It is 0 on
the lane games, which load their save by command, not through the galaxy menu's UI (the GGE UI
code at 0xDEA3D0 sets it to 1, 0xDEB450 to 0): the test sets it first, as the game does
(docs/re/creature.md#creature-cheat-console-flag).
"""
import re
import sys
import time

from coop_creature_social import KOOK, engage, hold, num, social
from twoinst import Checks, shot, start


def enable_console(con):
    """the cheat manager's enabled flag (+0x64), which the galaxy menu's UI sets for a player"""
    m = re.search(r'eax=0x([0-9A-F]+)', con.call('call App::ICheatManager::Get cdecl')['out'])
    if m:
        con.call(f'call App::cCheatManager::func4Ch thiscall 0x{m.group(1)} 1')


def console_open(con):
    return '014A4606 none' not in con.call('coop.ui where 014A4606')['out']


def close_console(con):
    """Esc closes it (Ctrl+Shift+C does not); the pause menu Esc may open as well is closed"""
    from uifind import press, shown
    con.call('key esc')
    time.sleep(1.5)
    if shown(con, 'Resume Game'):
        press(con, 'Resume Game')
        time.sleep(1)


def console(con):
    for k in ('key ctrl down', 'key shift down', 'key c', 'key shift up', 'key ctrl up'):
        con.call(k)
        time.sleep(0.2)
    time.sleep(1.0)


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('creature')
    ck = Checks()
    if without:
        print(' ', guest.call('coop.social textfix off')['out'].strip())
    for c in (host, guest):
        c.call('coop.team hp 1000')
    time.sleep(3)
    mirrored = set(re.findall(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=' + KOOK + r' spawned', guest.call('coop.npc list')['out']))
    kooks = [m.group(1) for m in re.finditer(r'#(\d+) (?:[0-9A-Fa-f]{8} )?species=' + KOOK + r' hp=([\d.]+) dist=([\d.]+)(?! DEAD)',
                                             host.call('coop.npc list')['out'])
             if m.group(1) in mirrored and float(m.group(2)) > 0]
    target = None
    for k in kooks[:3]:
        if engage(guest, 'n' + k):
            target = k
            break
    ck(target is not None, f'the guest engaged a mirrored Kook (n{target})')
    if not target:
        return ck.result()

    def acts():
        return num(r'acts_in=(\d+)', social(host), 0)

    enable_console(guest)
    console(guest)
    keys = guest.call('coop.social keys')['out'].strip()
    print('  guest:', keys)
    ck(console_open(guest), 'the cheat console is open on the guest (Ctrl+Shift+C)')
    a0 = acts()
    for vk in (0x31, 0x32, 0x31):  # "121" typed into the console
        hold(guest, vk)
        time.sleep(0.5)
    time.sleep(2.5)
    a1 = acts()
    print(f'  host acts_in while the console was open: {a0} -> {a1}')
    shot(guest, 'social_console_guest.png')
    ck(a1 == a0, 'digits typed in the cheat console play no social ability (the host got no act)')
    close_console(guest)
    ck(not console_open(guest), 'the cheat console is closed again')
    time.sleep(1)
    guest.call(f'coop.social goto {target}')
    for _ in range(3):  # (the engagement may have run out meanwhile: engage again)
        if re.search(r'engaged=' + target + r'\b', social(guest)) or engage(guest, 'n' + target, 20):
            break
    a2 = acts()
    hold(guest, 0x31)
    time.sleep(2.5)
    a3 = acts()
    print(f'  host acts_in after the console was closed: {a2} -> {a3}')
    ck(a3 > a2, 'the key 1 plays the ability again once the console is closed')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
