#!/usr/bin/env python3
"""Try one ordinary guest attack early in a fresh stock Crystal Mine run.

Only the reviewed owner-local teleport stages the captain near a real hostile.
Selection and attack use ordinary native mouse/key input. No NPC, AI, ability,
health, authored property or damage event is manufactured. A passive loopback
proxy records the exact native guest Hit identities and reliable retransmits.
The caller must hold the explicit lease on the two recorded hidden lab games.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time

from adventure_control_fixture import local_avatar, teleport
from adventure_interaction_fixture import Native, _verify_game
from adventure_network_proxy import NetworkProxy
from coop_adventure import FixtureError, Game, verify_lab
from coop_adventure_combat_movement import CREATURE, MINE, MineMemory, TITLE, actors, check
from coop_adventure_lobby import click_named, lobby, playing
from coop_adventure_preparation import menu


def messages(packet):
    """Bounded SPCO message decoder; never treats truncated bytes as a hit."""
    if len(packet) < 31 or packet[:4] != b'SPCO':
        return
    offset = 31
    for _ in range(packet[30]):
        if offset + 4 > len(packet):
            return
        kind, reliable, size = struct.unpack_from('<BBH', packet, offset)
        offset += 6 if reliable else 4
        if offset + size > len(packet):
            return
        yield kind, bool(reliable), packet[offset:offset+size]
        offset += size


class HitAudit(NetworkProxy):
    def __init__(self, listen, target):
        super().__init__(listen, target)
        self.hits = {}

    def _record(self, direction, data):
        super()._record(direction, data)
        if direction != 'up':
            return
        for kind, reliable, payload in messages(data):
            if kind != 52 or not reliable or not payload:
                continue
            if payload[0] == 130 and len(payload) == 33:
                run, event, marker, incarnation, damage, damage_type = struct.unpack_from('<QQIIfI', payload, 1)
                detail = dict(kind='actor', run=run, event=event, marker=marker,
                              incarnation=incarnation, damage=damage, damage_type=damage_type)
            elif payload[0] == 131 and len(payload) == 27:
                run, event, victim, source, damage, damage_type = struct.unpack_from('<QQBBfI', payload, 1)
                detail = dict(kind='captain', run=run, event=event, victim=victim,
                              source=source, damage=damage, damage_type=damage_type)
            else:
                continue
            identity = (detail['run'], detail['event'])
            with self.lock:
                if identity not in self.hits:
                    self.hits[identity] = dict(detail, first_wall=time.time(), wire_receptions=0)
                self.hits[identity]['wire_receptions'] += 1

    def audit(self):
        with self.lock:
            return [dict(value) for value in self.hits.values()]


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def unit(vector):
    length = math.sqrt(dot(vector, vector))
    check(length > .001, 'degenerate native camera/planet vector')
    return tuple(x/length for x in vector)


def camera(game, memory):
    match = re.search(r'viewer=([0-9A-Fa-f]+) camera=([0-9A-Fa-f]+)', game.call('coop.cam'))
    check(match, 'native camera is unavailable')
    viewer, native_camera = (int(value, 16) for value in match.groups())
    return (struct.unpack('<16f', memory.read(viewer, 64)),
            struct.unpack('<2f', memory.read(native_camera+0x5C, 8)),
            struct.unpack('<4I', memory.read(native_camera+0x78, 16)))


def project(state, position):
    # Same reviewed layout and projection as sporekit/gamehooks.cpp.
    view, half, viewport = state
    check(all(x > 0 for x in half) and viewport[2] and viewport[3], 'invalid native viewport')
    delta = [position[i]-view[12+i] for i in range(3)]
    x, z, y = dot(delta, view[:3]), dot(delta, view[4:7]), dot(delta, view[8:11])
    check(z > .1, 'real target is behind the native camera')
    return (viewport[0]+(x/(z*half[0])+1)*viewport[2]/2,
            viewport[1]+(1-y/(z*half[1]))*viewport[3]/2)


def goals_targets(reader):
    goals = reader.memory.goals()
    check(goals['state'] == 3 and goals['act'] == 0 and goals['elapsed'] < 45000,
          'ordinary attack requires the fresh first native wave')
    targets = sorted({marker for goal in goals['goals'] if goal['type'] == 3
                      for marker in goal['targets']})
    found = {}
    for marker in targets:
        for row in actors(reader.game.call(f'coop.adventure.world actorsat {marker} 48')):
            if row['noun'] == CREATURE:
                found[(row['marker'], row['host_incarnation'])] = row
    return reader.sample(list(found.values()))['actors']


def read_actor(reader, actor):
    memory = reader.memory
    obj = actor['object']
    try:
        if memory.pointer(obj+0x24) != actor['incarnation'] or memory.read(obj+0x21, 1) != b'\0':
            return None
        return dict(health=struct.unpack('<f', memory.read(obj+0x5E0, 4))[0],
                    position=struct.unpack('<3f', memory.read(obj+0xC4, 12)),
                    dead=bool(memory.read(obj+0xB5E, 1)[0]))
    except OSError:
        return None


def hits_count(text):
    match = re.search(r'\bhits=(\d+)', text)
    check(match, 'native forwarded hit counter unavailable')
    return int(match[1])


def verify_calls(game, record):
    """Recheck the installed dc04 entries before any fixture native call.

    Hostility and CreatureTarget are in verify_dc04.py. The existing upstream
    stance getter was separately inspected at D2E490: A1 0169E394; C3. Its
    absolute data operand relocates with the executable, unlike relative calls.
    """
    evidence = []
    with Native(game, record) as native:
        for name, preferred, prefix in (
            ('SporeCoop::Adventure::IsHostileToCreature', 0xC0B9C0, bytes.fromhex('568bf1e8f81bf3ff85c0742d8b4c2408')),
            ('SporeCoop::Adventure::CreatureTarget', 0xC0EFD0, bytes.fromhex('8b897c0e000085c9740d8b018b505c68')),
            ('Simulator::cCreatureGameData::GetAbilityMode', 0xD2E490, None),
        ):
            address = int(game.call('sym '+name).split()[0], 16)
            if prefix is None:
                prefix = b'\xA1'+struct.pack('<I', 0x169E394+address-preferred)+b'\xC3'
            actual = native.memory.read(address, len(prefix))
            check(actual == prefix, 'runtime bytes differ from reviewed dc04 entry: '+name)
            evidence.append(dict(name=name, preferred=hex(preferred), address=hex(address), bytes=actual.hex()))
    return evidence


def attempt(games, records, proxy, output):
    host, guest = games
    readers = [MineMemory(game, records[game.instance]) for game in games]
    result = dict(result='INCONCLUSIVE', samples=[], limits=[
        'One ordinary body-click/key1 input sequence and target, not complete combat/projectile coverage.',
        'NPCs continue normal host AI; concurrent native NPC damage can affect target HP.',
        'Read-only native memory samples are asynchronous.'])
    try:
        host_rows, guest_rows = [goals_targets(reader) for reader in readers]
        host_by_id = {(a['marker'], a['host_incarnation']): a for a in host_rows if a['health'] > 0 and not a['dead']}
        candidates = [a for a in guest_rows if (a['marker'], a['host_incarnation']) in host_by_id
                      and a['health'] > 0 and not a['dead']]
        check(candidates, 'no live exact-goal hostile target in the fresh wave')
        with Native(guest, records[guest.instance]) as native:
            avatar = local_avatar(guest)
            candidates = [a for a in candidates if native.call('SporeCoop::Adventure::IsHostileToCreature',
                          'thiscall', a['object'], avatar) & 255]
        check(candidates, 'goal targets did not pass native hostility against the guest captain')
        # Prefer a robust live target over a nearly dead actor. Recheck both
        # incarnation identities and health after captain placement.
        target = max(candidates, key=lambda a: a['health'])
        counterpart = host_by_id[(target['marker'], target['host_incarnation'])]
        result.update(target=target, authoritative_target=counterpart)
        view, _, _ = camera(guest, readers[1].memory)
        up = unit(target['spatial'])
        tangent = unit([view[4+i]-dot(view[4:7], up)*up[i] for i in range(3)])
        radius = math.sqrt(dot(target['spatial'], target['spatial']))
        placement = [v*radius for v in unit([target['spatial'][i]-tangent[i]*3.5 for i in range(3)])]
        result['placement'] = placement
        teleport(guest, records[guest.instance], placement)
        guest.call('wait.frames 18 15000')
        with Native(guest, records[guest.instance]) as native:
            stance = native.call('Simulator::cCreatureGameData::GetAbilityMode', 'cdecl')
            if stance != 1:
                width, height = guest.shot('before-combat-stance')
                guest.click(width/2+30, height-82)
                stance = native.call('Simulator::cCreatureGameData::GetAbilityMode', 'cdecl')
            check(stance == 1, 'native combat stance was not established')
            result['native_stance'] = stance
        before_pair = [read_actor(reader, actor) for reader, actor in zip(readers, (counterpart, target))]
        check(all(value and value['health'] > 0 and not value['dead'] for value in before_pair),
              'native target expired while placing the captain')
        check(abs(before_pair[0]['health']-before_pair[1]['health']) < .1,
              'host/guest target HP had not settled before attack')
        owner_position = struct.unpack('<3f', readers[1].memory.read(avatar+0xC4, 12))
        result['staged_range'] = math.dist(owner_position, before_pair[1]['position'])
        check(result['staged_range'] < 10, 'native captain did not remain within ordinary attack range')
        check(all(reader.memory.goals()['state'] == 3 for reader in readers), 'native run ended before attack')
        up = unit(before_pair[1]['position'])
        body = [before_pair[1]['position'][i]+up[i] for i in range(3)]
        screen = project(camera(guest, readers[1].memory), body)
        width, height = guest.shot('before-native-target-click')
        check(270 < screen[0] < width-100 and 180 < screen[1] < height-130,
              'projected native target is outside unobstructed gameplay')
        result.update(before_pair=before_pair, screen=screen, run=host.adventure()['run'],
                      host_hits_before=hits_count(host.call('coop.adventure.world status')),
                      wire_before=proxy.audit(), goals_before=[r.memory.goals() for r in readers])
        guest.click(*screen)
        with Native(guest, records[guest.instance]) as native:
            selected = native.call('SporeCoop::Adventure::CreatureTarget', 'thiscall', avatar)
        result['selected_native_creature'] = selected
        check(selected == target['object'], 'ordinary native click did not select the intended hostile creature')
        guest.record('native-target-selected')
        guest.call('key 1 down')
        guest.call('wait.frames 2 15000')
        guest.call('key 1 up')
        started = time.monotonic()
        while time.monotonic()-started < 5:
            row = dict(wall=time.monotonic()-started,
                       pair=[read_actor(reader, actor) for reader, actor in zip(readers, (counterpart, target))],
                       goals=[r.memory.goals() for r in readers], wire=proxy.audit(),
                       host_hits=hits_count(host.call('coop.adventure.world status')))
            result['samples'].append(row)
            if any(g['state'] != 3 for g in row['goals']):
                break
            time.sleep(.08)
        prior = {(e['run'], e['event']) for e in result['wire_before']}
        events = [e for e in proxy.audit() if (e['run'], e['event']) not in prior]
        target_events = [e for e in events if e['kind'] == 'actor' and e['run'] == result['run'] and
                         (e['marker'], e['incarnation']) == (target['marker'], target['host_incarnation'])]
        result['new_wire_events'] = events
        result['host_hits_delta'] = hits_count(host.call('coop.adventure.world status'))-result['host_hits_before']
        result['matching_damaged_samples'] = [s for s in result['samples'] if all(s['pair']) and
            abs(s['pair'][0]['health']-s['pair'][1]['health']) < .1 and
            s['pair'][0]['health'] < before_pair[0]['health']-.01]
        check(target_events, 'no native guest Hit event for the selected target was observed')
        check(len(events) == len(target_events), 'other guest hit events prevent isolated acceptance counting')
        check(result['host_hits_delta'] == len(target_events),
              'host accepted-hit counter did not equal unique native target events')
        check(result['matching_damaged_samples'], 'target health loss was not observed matching on host and guest')
        check(all(s['goals'][0]['state'] == 3 and s['goals'][1]['state'] == 3 for s in result['samples']),
              'native terminal cleanup invalidated the attack window')
        for game in games:
            check('total faults: 0' in game.call('exc'), 'native exception during ordinary guest attack')
            game.record('after-native-attack')
        result['result'] = 'PASS'
    except (FixtureError, OSError, ValueError) as error:
        result['coverage_limit'] = str(error)
    finally:
        guest.call('key 1 up', required=False)
        for reader in readers:
            reader.close()
        (output/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--port', type=int, default=47960)
    args = parser.parse_args()
    records = verify_lab(args.metadata, (args.host, args.guest))
    args.out.mkdir(parents=True, exist_ok=True)
    games = [Game(instance, args.out, timeout=120) for instance in (args.host, args.guest)]
    host, guest = games
    for game in games:
        _verify_game(game, records[game.instance])
        check(not game.adventure()['active'], 'fresh attack requires inactive lab clients')
    # The proxy lives through native cancellation and menu recovery so accepted
    # reliable work cannot be stranded by teardown.
    with HitAudit(args.port+10, args.port) as proxy:
        try:
            for game in games:
                menu(game, records[game.instance]); game.call('coop.leave')
            host.call(f'coop.host {args.port} AttackHost')
            guest.call(f'coop.join 127.0.0.1:{args.port+10} AttackGuest')
            host.wait('two attack participants', lambda: host.adventure()['players'] == 2)
            host.call('coop.adventure start '+MINE)
            for game in games:
                state = lobby(game, 2, TITLE)
                check('By Maxis' in state and 'change_enabled=0' in state, 'stock Mine authored captain lock mismatch')
            for game in (guest, host):
                click_named(game, 'prep_ready')
            playing(games, records)
            installation = Path(json.loads(args.metadata.read_text())['installation'])
            versions = []
            for game in games:
                mods = game.call('mods')
                match = re.search(r'^coop\s+loaded\s+gen=(\d+)\b', mods, re.M)
                check(match, 'loaded native mod generation unavailable')
                path = installation/'SporebinEP1/mods/.shadow'/f'coop.i{game.instance}.{match[1]}.dll'
                versions.append(dict(instance=game.instance, path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
            (args.out/'context.json').write_text(json.dumps(dict(profiles=records, loaded_shadows=versions,
                scenario=MINE, title=TITLE,
                reviewed_calls=[dict(instance=game.instance, entries=verify_calls(game, records[game.instance]))
                                for game in games]), indent=2)+'\n', encoding='utf-8')
            result = attempt(games, records, proxy, args.out)
            print(json.dumps({k: result[k] for k in ('result', 'coverage_limit', 'host_hits_delta', 'native_stance') if k in result}), flush=True)
            return 0 if result['result'] == 'PASS' else 1
        finally:
            host.call('coop.adventure cancel', required=False)
            for game in games:
                game.call('key 1 up', required=False)
                menu(game, records[game.instance])
                game.record('menu-handoff')


if __name__ == '__main__':
    raise SystemExit(main())
