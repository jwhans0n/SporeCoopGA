#!/usr/bin/env python3
"""Observe real Maxis Crystal Mine AI, native models and combat in a lab pair.

No actors, goals, damage, AI orders, or authored properties are manufactured. The
normal adventure/Ready/Continue path starts the stock locked-captain adventure.
The measured phase uses ordinary owner keyboard input; the other captain stays
still. Read-only native samples distinguish replicated spatial coordinates from
the actual graphics model transform, which a spatial-only regression would miss.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time

from adventure_interaction_fixture import Native, _verify_game
from adventure_memory import AdventureMemory
from adventure_network_proxy import NetworkProxy
from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_lobby import click_named, lobby, playing
from coop_adventure_preparation import menu

MINE = '0x6E008979 0x366A930D 0x408A0000'
TITLE = 'Defend the Crystal Mine'
CREATURE = 0x018EB45E
ACTOR = re.compile(r'^marker=(\d+) incarnation=(\d+) noun=(\d+) object=(0x[0-9a-fA-F]+) class=(-?\d+)(.*)$', re.M)


def check(condition, message):
    if not condition:
        raise FixtureError(message)


def distance(a, b):
    return math.sqrt(sum((x-y)**2 for x, y in zip(a, b)))


def actors(text):
    result = []
    for match in ACTOR.finditer(text):
        marker, incarnation, noun, obj, cls, extra = match.groups()
        host = re.search(r'\bhost_incarnation=(\d+)', extra)
        result.append(dict(marker=int(marker), incarnation=int(incarnation), noun=int(noun),
                           object=int(obj, 16), cls=int(cls),
                           host_incarnation=int(host[1]) if host else int(incarnation)))
    return result


def percentile(values, fraction):
    return sorted(values)[min(len(values)-1, int((len(values)-1)*fraction))] if values else None


def native_update_counts(text):
    match = re.search(r'native creature updates: total=(\d+) npc_ai_suppressed=(\d+)', text)
    return dict(zip(('native', 'npc_ai_suppressed'), map(int, match.groups()))) if match else None


def animation_counts(text):
    match = re.search(r'npc animations: sent=(\d+) received=(\d+) played=(\d+) expired=(\d+) queued=(\d+)', text)
    return dict(zip(('sent', 'received', 'played', 'expired', 'queued'), map(int, match.groups()))) if match else None


class MineMemory:
    def __init__(self, game, record):
        self.game, self.record = game, record
        self.memory = AdventureMemory(game, record)
        self.generator = None
        self.mode = self.memory.pointer(self.memory.singleton)
        resource = self.memory.pointer(self.memory.pointer(self.mode+0x74)+0x10)
        check(self.memory.read(resource+0x175, 1) == b'\1', 'stock Mine must require its authored captain')
        self.captain_key = list(struct.unpack('<3I', self.memory.read(resource+0x130, 12)))
        check(self.captain_key == [0x07CCC687, 0x2B978C46, 0x40626200], 'unexpected stock Mine captain resource')

    def close(self):
        self.memory.close()

    def discover(self):
        # Native debug enumeration preserves same-marker spawner siblings. Do
        # not collapse the key to marker alone or compare local incarnations.
        found = {}
        for first in range(0, 166, 24):
            for actor in actors(self.game.call(f'coop.adventure.world actors {first} 48')):
                found[(actor['marker'], actor['host_incarnation'])] = actor
        # Spawned siblings are appended after the authored actors. A bounded
        # marker>=batch can fill with earlier authored rows before reaching a
        # later-spawned low-marker enemy. Query real active Kill targets by exact
        # marker too, preserving every reported incarnation at that marker.
        targets = {marker for goal in self.memory.goals()['goals'] if goal['type'] == 3
                   for marker in goal['targets']}
        for marker in sorted(targets):
            for actor in actors(self.game.call(f'coop.adventure.world actorsat {marker} 48')):
                found[(actor['marker'], actor['host_incarnation'])] = actor
        generator = next((a for a in found.values() if a['cls'] == 1069), None)
        if generator and self.generator != generator['object']:
            with Native(self.game, self.record) as native:
                combatant = native.virtual(generator['object'], 0x0C, 0x013F94D4)
            check(combatant != 0, 'native Mine generator combatant unavailable')
            self.generator = generator['object']
            self.generator_combatant = combatant
        return [a for a in found.values() if a['noun'] == CREATURE]

    def sample(self, discovered):
        memory = self.memory
        rows = []
        for actor in discovered:
            obj = actor['object']
            try:
                if memory.pointer(obj+0x24) != actor['incarnation'] or memory.read(obj+0x21, 1)[0]:
                    continue
                spatial = struct.unpack('<3f', memory.read(obj+0xC4, 12))
                animated = memory.pointer(obj+0xB54)
                if not animated:
                    continue
                model = memory.pointer(animated+0x180)
                if not model:
                    continue
                core = memory.pointer(animated+0x17C)
                if not core:
                    continue
                # dc04 C16550 consumes creature+B54, A05B60 owns its model+180;
                # cMWModel stores Transform+08 with its offset at Transform+04.
                model_position = struct.unpack('<3f', memory.read(model+0x0C, 12))
                if not all(math.isfinite(x) and abs(x) < 100000 for x in (*spatial, *model_position)):
                    continue
                rows.append(dict(**actor, spatial=spatial, animated=animated, model=model,
                                 animated_position=struct.unpack('<3f', memory.read(animated+4, 12)),
                                 core_position=struct.unpack('<3f', memory.read(core+0x18, 12)),
                                 model_position=model_position,
                                 model_spatial_error=distance(spatial, model_position),
                                 health=struct.unpack('<f', memory.read(obj+0x5E0, 4))[0],
                                 dead=bool(memory.read(obj+0xB5E, 1)[0])))
            except OSError:
                # Native removal can race a read; the next bounded enumeration
                # replaces this incarnation rather than dereferencing it again.
                continue
        avatar = self.game.call('coop.cam')
        match = re.search(r'^avatar=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', avatar, re.M)
        check(match is not None, 'raw frame-boundary captain position unavailable')
        goals = memory.goals()
        return dict(captain_position=list(map(float, match.groups())), captain=self.game.call('coop.avatar'),
                    goals=goals, actors=rows,
                    generator_health=struct.unpack('<f', memory.read(self.generator_combatant+0x38, 4))[0]
                    if self.generator else None)


def summarize(samples):
    reports = []
    for side in ('host', 'guest'):
        histories = {}
        errors = []
        for row in samples:
            for actor in row[side]['actors']:
                key = (actor['marker'], actor['host_incarnation'])
                histories.setdefault(key, []).append(actor)
                errors.append(actor['model_spatial_error'])
        moved = []
        for key, history in histories.items():
            if len(history) < 3:
                continue
            displacement = max(distance(a['spatial'], history[0]['spatial']) for a in history)
            if displacement >= 2:
                moved.append(dict(marker=key[0], incarnation=key[1], samples=len(history),
                                  spatial_displacement=displacement,
                                  model_displacement=max(distance(a['model_position'], history[0]['model_position']) for a in history),
                                  error_p95=percentile([a['model_spatial_error'] for a in history], .95)))
        reports.append(dict(side=side, model_error_p95=percentile(errors, .95), model_error_max=max(errors) if errors else None,
                            moving_incarnations=moved,
                            captain_displacement=max(distance(row[side]['captain_position'], samples[0][side]['captain_position']) for row in samples)))
    return reports


def capture(games, records, seconds, expect, output):
    readers = [MineMemory(game, records[game.instance]) for game in games]
    samples, screenshots = [], []
    key_down = False
    try:
        discovered = [reader.discover() for reader in readers]
        initial_status = [game.call('coop.adventure.world status') for game in games]
        start = time.monotonic()
        next_discovery = next_shot = 0
        while time.monotonic()-start < seconds:
            elapsed = time.monotonic()-start
            moving = seconds*.25 <= elapsed < seconds*.70
            if moving != key_down:
                games[0].call('key W '+('down' if moving else 'up'))
                key_down = moving
            if elapsed >= next_discovery:
                discovered = [reader.discover() for reader in readers]
                next_discovery = elapsed+1
            row = dict(wall=elapsed, phase='host_W_guest_still' if moving else 'both_still')
            for side, reader, entities in zip(('host', 'guest'), readers, discovered):
                row[side] = reader.sample(entities)
            samples.append(row)
            if elapsed >= next_shot:
                label = f'movement-{len(screenshots):02d}'
                for game in games:
                    game.shot(label)
                screenshots.append(dict(wall=elapsed, label=label))
                next_shot = elapsed+2
            time.sleep(.1)
        result = dict(result='CAPTURED', seconds=seconds, samples=samples, screenshots=screenshots,
                      initial_status=initial_status, final_status=[game.call('coop.adventure.world status') for game in games],
                      summary=summarize(samples), captain_keys=[reader.captain_key for reader in readers],
                      limits=['Native spatial/model/health/goal observations use asynchronous read-only memory; captain pose uses main-thread coop.cam.',
                              'No synthetic damage or AI orders; native combat and goal deltas are observations, not complete adventure coverage.',
                              'Host uses the reviewed adventure-start diagnostic entry; captain restriction, Ready and native Continue remain normal.',
                              'Normal guest attack selection is not exercised by this automatic trace.'])
        motion = result['summary']
        result['animation_deltas'] = []
        for before_text, after_text in zip(initial_status, result['final_status']):
            before, after = animation_counts(before_text), animation_counts(after_text)
            result['animation_deltas'].append({k: after[k]-before[k] for k in before} if before and after else None)
        if motion[0]['captain_displacement'] < 2 or motion[1]['captain_displacement'] > .2:
            result['limits'].append('Moving-host/stationary-guest precondition was not met; NPC spatial/model observations remain independent of that condition.')
        (output/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        check(any(report['moving_incarnations'] for report in result['summary']), 'native NPC AI produced no measurable movement')
        if expect == 'baseline':
            check(any(a['error_p95'] > 5 for a in result['summary'][1]['moving_incarnations']),
                  'baseline did not reproduce a stale guest graphics model despite NPC spatial motion')
        elif expect == 'fixed':
            guest_moving = result['summary'][1]['moving_incarnations']
            host_rows = {(a['marker'], a['incarnation']): a for a in result['summary'][0]['moving_incarnations']}
            # Native animated/root-motion offsets are measurable on the host
            # too. Bound the guest against that real control as well as a small
            # floor instead of claiming every model pivot equals its feet.
            shared = [(a, host_rows[(a['marker'], a['incarnation'])]) for a in guest_moving
                      if (a['marker'], a['incarnation']) in host_rows]
            check(shared and all(a['error_p95'] < max(2, b['error_p95']+1) and a['model_displacement'] > 1
                                 for a, b in shared),
                  'moving guest graphics model remains detached from its native spatial pose')
            before = native_update_counts(initial_status[1])
            after = native_update_counts(result['final_status'][1])
            check(before and after and after['native'] > before['native'] and
                  after['npc_ai_suppressed'] > before['npc_ai_suppressed'],
                  'guest native visual Update must advance while native NPC AI is suppressed')
            result['native_update_delta'] = {k: after[k]-before[k] for k in before}
            sent, received = result['animation_deltas']
            check(sent and received and sent['sent'] > 0 and received['received'] > 0 and received['played'] > 0,
                  'native NPC clip requests must cross the host stream and replay on the guest')
        for game in games:
            check('total faults: 0' in game.call('exc'), 'native exception during Mine movement/combat')
            game.record('movement-final')
        result['result'] = 'PASS'
        (output/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        return result
    finally:
        games[0].call('key W up', required=False)
        for reader in readers:
            reader.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=25)
    parser.add_argument('--port', type=int, default=47960)
    parser.add_argument('--attach', action='store_true', help='measure an already running stock Mine attempt')
    parser.add_argument('--direct', action='store_true', help='connect directly without a fixture-owned traffic proxy')
    parser.add_argument('--keep-running', action='store_true', help='leave the existing native attempt for an explicit lab handoff')
    parser.add_argument('--expect', choices=('observe', 'baseline', 'fixed'), default='observe')
    args = parser.parse_args()
    check(args.seconds >= 10, 'native trace must last at least 10 seconds')
    check(not args.keep_running or args.attach or args.direct,
          '--keep-running requires an existing directly connected pair; a fixture proxy cannot outlive its owner')
    records = verify_lab(args.metadata, (args.host, args.guest))
    args.out.mkdir(parents=True, exist_ok=True)
    games = [Game(instance, args.out, timeout=180) for instance in (args.host, args.guest)]
    host, guest = games
    proxy = None
    for game in games:
        # Check the live console PID and executable before any cancel/host/UI
        # mutation, not merely the existence of an old lab metadata file.
        _verify_game(game, records[game.instance])
    try:
        if not args.attach:
            host.call('coop.adventure cancel', required=False)
            for game in games:
                menu(game, records[game.instance]); game.call('coop.leave')
            if not args.direct:
                proxy = NetworkProxy(args.port+10, args.port)
                proxy.__enter__()
            host.call(f'coop.host {args.port} MineHost')
            guest.call(f'coop.join 127.0.0.1:{args.port if args.direct else args.port+10} MineGuest')
            host.wait('two Mine participants', lambda: host.adventure()['players'] == 2)
            host.call('coop.adventure start '+MINE)
            for game in games:
                state = lobby(game, 2, TITLE)
                check('By Maxis' in state and 'change_enabled=0' in state, 'stock Mine presentation/captain lock mismatch')
            for game in (guest, host):
                click_named(game, 'prep_ready')
            playing(games, records)
        installation = Path(json.loads(args.metadata.read_text())['installation'])
        versions = []
        for game in games:
            mods = game.call('mods')
            generation = re.search(r'^coop\s+loaded\s+gen=(\d+)\b', mods, re.M)
            check(generation is not None, 'loaded generation unavailable')
            path = installation/'SporebinEP1/mods/.shadow'/f'coop.i{game.instance}.{generation[1]}.dll'
            versions.append(dict(instance=game.instance, mods=mods, path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        (args.out/'context.json').write_text(json.dumps(dict(profiles=records, loaded_shadows=versions,
            expectation=args.expect, scenario=MINE, title=TITLE), indent=2)+'\n', encoding='utf-8')
        traffic_before = proxy.snapshot() if proxy else None
        result = capture(games, records, args.seconds, args.expect, args.out)
        result.update(profiles=records, loaded_shadows=versions,
                      traffic=proxy.difference(traffic_before, proxy.snapshot()) if proxy else None)
        (args.out/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(result['summary'], indent=2), flush=True)
        return 0
    except (FixtureError, OSError, ValueError) as error:
        (args.out/'failure.json').write_text(json.dumps(dict(result='FAIL', error=str(error),
            expectation=args.expect, timestamp=time.time()), indent=2)+'\n', encoding='utf-8')
        print('FAIL:', error, flush=True)
        return 1
    finally:
        try:
            if not args.keep_running:
                # Keep the fixture transport alive until both native clients
                # receive cancellation and restore their own menu lifecycle.
                host.call('coop.adventure cancel', required=False)
                for game in games:
                    menu(game, records[game.instance])
        finally:
            if proxy:
                proxy.__exit__(None, None, None)
            for game in games:
                game.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
