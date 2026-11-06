#!/usr/bin/env python3
"""Cell stage: the meat a killed creature leaves behind is the same for both players, and either can
eat it once (player report 2026-10-24: "when one of us killed an enemy, the other did not see the meat").

Each player in turn kills an NPC creature next to the partner with the game's own bite (Bite 0xE7B0A0,
the hit of a weapon part: DamageCell, and BreakOrSwallow for the lethal one; `coop.pool kill`). Then:

  * the host's game killed it (the host simulates the pool): `npc_deaths` on the host;
  * the guest's game spawns no loot of its own (`loot_cells` on the guest stays 0: its meat is the
    host's, mirrored), and has no meat the host does not have;
  * every meat cell that appeared on the host around the kill appears on the guest;
  * over the kills, meat dropped for the host's kills and for the guest's kills (a creature's loot
    table is random: some kills leave nothing).

Then each player eats one of these chunks (the game's eat handler EatFood, `coop.pool eat`: the
fixture's cell is a herbivore whose mouth does not take meat): it is gone on both machines and the
shared food went up once; and both eat the same chunk at the same moment: it counts once.

  coop_cell_meat.py [--kills N]   (kills per player, default 5: bite, hit, bite, hit, bite)
"""
import argparse
import re
import threading
import time

from twoinst import Checks, shot, start


def stat(con):
    out = con.call('coop.pool')['out']
    d = {}
    for k in ('npc_deaths', 'loot_calls', 'loot_cells', 'food', 'own_eats', 'eats_in', 'eats_sent', 'kills_in', 'kills_sent',
              'death_replays', 'loot_skipped', 'eats_late'):
        m = re.search(r'\b' + k + r'=(-?\d+)', out)
        d[k] = int(m.group(1)) if m else 0
    return d


def meat(con):
    out = con.call('coop.pool meat')['out']
    return {m.group(1): (float(m.group(2)), float(m.group(3)))
            for m in re.finditer(r'meat (\S+) pos=\((\S+) (\S+)\)', out)}


def kill(killer, other, name, mode):
    """kill the nearest NPC creature (mode 'bite': the game's Bite, the lethal one breaks it;
    'hit': DamageCell only, the lethal one goes to KillCell); returns (key, kill spot) or (None, None)"""
    k = killer.call('coop.pool kill' + (' - hit' if mode == 'hit' else ''))['out']
    m = re.match(r'bit (c\d+) .* pos=\((\S+) (\S+)\)', k)
    if not m:
        print(f'  ({name}: {k})')
        return None, None
    key = m.group(1)
    killer.call(f'coop.pool swimto {key} tp')
    time.sleep(0.4)
    other.call('coop.pool tp 2 0')  # the partner watches from right next to the killer
    at = (float(m.group(2)), float(m.group(3)))
    for _ in range(12):
        time.sleep(1.0)
        k = killer.call(f'coop.pool kill {key}' + (' hit' if mode == 'hit' else ''))['out']
        m = re.match(r'bit \S+ .* pos=\((\S+) (\S+)\)', k)
        if m:
            at = (float(m.group(1)), float(m.group(2)))
        if 'dying' in k or k == 'gone':
            break
    print(f'  {name} killed {key} ({mode}) at ({at[0]:.1f} {at[1]:.1f}): {k}')
    return key, at


def loot(host, since, at):
    """the cells the host's loot drops created around `at` in the last `since` seconds, still alive: {key: kind}"""
    out = host.call('coop.pool loot')['out']
    return {m.group(1): m.group(2) for m in re.finditer(r'loot (\S+) (\S+) age=(\S+) pos=\((\S+) (\S+)\)', out)
            if float(m.group(3)) <= since and (float(m.group(4)) - at[0]) ** 2 + (float(m.group(5)) - at[1]) ** 2 < 30 ** 2}


def eat(con, other, name, key, ck):
    """eat a chunk with the game's eat handler (EatFood, the event a mouth's touch posts: this
    fixture's cell is a herbivore, its mouth does not take meat): gone on both, food up once"""
    s0, o0 = stat(con), stat(other)
    r = con.call(f'coop.pool eat {key}')['out']
    m = re.search(r'points=(\d+)', r)
    pts = int(m.group(1)) if m else 0
    time.sleep(2.0)
    s1, o1 = stat(con), stat(other)
    print(f'  {name}: {r}')
    ck(pts > 0, f'{name} ate meat {key} ({pts} points)')
    ck(key not in meat(con) and key not in meat(other), '  it is gone on both machines')
    ck(s1['food'] == o1['food'] == s0['food'] + pts, f'  the shared food went up once ({s0["food"]} -> {s1["food"]} / {o1["food"]})')


def eat_both(host, guest, key, ck):
    """both eat the same chunk at the same moment: it counts once"""
    h0 = stat(host)
    out = {}
    ts = [threading.Thread(target=lambda n, c: out.__setitem__(n, c.call(f'coop.pool eat {key}')['out']), args=a)
          for a in (('host', host), ('guest', guest))]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    time.sleep(2.5)
    h1, g1 = stat(host), stat(guest)
    pts = [int(m.group(1)) for m in (re.search(r'points=(\d+)', v) for v in out.values()) if m]
    print(f'  both ate {key}: {out}; food {h0["food"]} -> host {h1["food"]} guest {g1["food"]}, late eats on the host {h1["eats_late"]}')
    ck(h1['food'] == g1['food'] == h0['food'] + max(pts + [0]),
       f'both ate the same chunk at once: counted once ({h0["food"]} -> {h1["food"]} / {g1["food"]})')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--kills', type=int, default=5)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    g_loot0 = stat(guest)['loot_cells']
    dropped = {'host': 0, 'guest': 0}
    kills = {'host': 0, 'guest': 0}
    eats = ['guest', 'host', 'both']
    missing_total, local_total = 0, 0
    for i in range(a.kills * 2):
        name = 'host' if i % 2 == 0 else 'guest'
        mode = 'bite' if (i // 2) % 2 == 0 else 'hit'
        killer, other = (host, guest) if name == 'host' else (guest, host)
        h0 = stat(host)
        mh0, mg0 = meat(host), meat(guest)
        t_kill = time.time()
        g0 = stat(guest)
        key, at = kill(killer, other, name, mode)
        if not key:
            continue
        time.sleep(2.5)  # a break plays its animation before the loot (event 5)
        h1, g1 = stat(host), stat(guest)
        mh1, mg1 = meat(host), meat(guest)
        died = h1['npc_deaths'] > h0['npc_deaths']
        ck(died, f'{name} kill {i // 2 + 1}: the host\'s game killed the creature (npc_deaths {h0["npc_deaths"]} -> {h1["npc_deaths"]})')
        kills[name] += died
        if mode == 'hit':  # KillCell on the host: the guest's copy dies the same way (effect, dying animation)
            ck(g1['death_replays'] > g0['death_replays'],
               f'  the guest saw it die ({g0["death_replays"]} -> {g1["death_replays"]} deaths replayed)')
        drop = loot(host, time.time() - t_kill, at)  # what this kill left (the cells of the host's loot drop)
        missing = {k for k in drop if 'no such' in guest.call(f'coop.pool cell {k}')['out']}
        if missing:  # a state is 0.1 s old: look again a moment later (only cells the host still has)
            time.sleep(1.0)
            drop2 = loot(host, time.time() - t_kill, at)
            missing = {k for k in missing if k in drop2 and 'no such' in guest.call(f'coop.pool cell {k}')['out']}
        local = {k for k in mg1 if k.startswith('l')}
        missing_total += len(missing)
        local_total += len(local)
        kinds = sorted(set(drop.values()))
        print(f'    left behind on the host: {len(drop)} cells {kinds} (loot cells {h0["loot_cells"]} -> {h1["loot_cells"]}), '
              f'on the guest {len(drop) - len(missing)}, guest-only meat {len(local)}, guest loot cells {g1["loot_cells"]}')
        meat_drop = sorted(k for k, kind in drop.items() if kind == 'meat' and k not in missing)
        if meat_drop:
            dropped[name] += 1
            shot(host, f'meat_{name}{i // 2 + 1}_host.png')
            shot(guest, f'meat_{name}{i // 2 + 1}_guest.png')
        ck(not missing, f'  every cell it left on the host is on the guest too (missing {sorted(missing)})')
        # eat a chunk right away (they go when the players swim on: cells outside the host's area are removed):
        # the guest one of the first kill with meat, the host one of the next, then both the same one
        if eats and meat_drop:
            what = eats.pop(0)
            if what == 'guest':
                eat(guest, host, 'guest', meat_drop[0], ck)
            elif what == 'host':
                eat(host, guest, 'host', meat_drop[0], ck)
            else:
                eat_both(host, guest, meat_drop[0], ck)
    g_loot = stat(guest)['loot_cells'] - g_loot0
    ck(g_loot == 0, f'the guest\'s game spawned no loot of its own ({g_loot} cells: its meat is the host\'s)')
    ck(local_total == 0, f'the guest never had meat the host does not have ({local_total})')
    ck(kills['host'] > 0 and kills['guest'] > 0, f'both players killed creatures (host {kills["host"]}, guest {kills["guest"]})')
    ck(dropped['host'] > 0 and dropped['guest'] > 0,
       f'meat dropped for the host\'s kills and for the guest\'s ({dropped["host"]} / {dropped["guest"]} kills with meat)')
    ck(not eats, f'all eating checks done ({eats} left: too few kills with meat)')
    hs, gs = stat(host), stat(guest)
    ck(hs['food'] == gs['food'], f'food equal at the end ({hs["food"]} / {gs["food"]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
