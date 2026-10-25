#!/usr/bin/env python3
"""Read-only high-rate native creature clip and vehicle model motion trace.

Only recorded invisible Windows lab children with the exact dc04 executable are
accepted. Stock Mine setup uses its authored captain, Ready and native Continue.
The trace never invokes additional native methods, writes poses, or issues AI
orders. Memory layouts are verified in docs/re/adventure-world.md.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time

from adventure_interaction_fixture import _verify_game
from adventure_memory import AdventureMemory
from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_combat_movement import CREATURE, MINE, TITLE, MineMemory, actors, distance, percentile
from coop_adventure_lobby import click_named, lobby, playing
from coop_adventure_preparation import menu

VEHICLE = 0x018C6DE8


def check(condition, message):
    if not condition:
        raise FixtureError(message)


def finite(values):
    return all(math.isfinite(value) and abs(value) < 100000 for value in values)


def current_clip(memory, animated):
    # Exact dc04 A05090 -> 9FF690 current-layer selector. This observes a
    # successful native channel, rather than counting PlayAnimation requests.
    query = memory.pointer(animated + 0x184)
    if not query:
        return None
    layer = memory.pointer(query + 0xFA4)
    if layer >= 16:
        return None
    index = memory.pointer(query + 0xF0C + 4 * layer)
    if index >= 16:
        return None
    slot = query + 8 + 0xF0 * index
    resource = memory.pointer(slot)
    subindex = memory.pointer(slot + 0x68)
    if not resource and not (subindex < 8 and memory.pointer(slot + 8 + 12 * subindex)):
        return None
    clock = struct.unpack('<d', memory.read(slot + 0xA8, 8))[0]
    if not math.isfinite(clock):
        return None
    return dict(query=query, layer=layer, index=index, slot=slot,
                generation=memory.pointer(slot + 0xB4),
                clip=memory.pointer(slot + 0xD8), time=clock,
                clock_aux=memory.pointer(slot + 0xB0), resource=resource)


class MotionMemory:
    def __init__(self, game, record):
        self.game = game
        self.memory = AdventureMemory(game, record)

    def close(self):
        self.memory.close()

    def discover(self):
        found = {}
        for first in range(0, 166, 24):
            for actor in actors(self.game.call(f'coop.adventure.world actors {first} 48')):
                found[(actor['marker'], actor['host_incarnation'])] = actor
        targets = {marker for goal in self.memory.goals()['goals'] if goal['type'] == 3
                   for marker in goal['targets']}
        for marker in sorted(targets):
            for actor in actors(self.game.call(f'coop.adventure.world actorsat {marker} 48')):
                found[(actor['marker'], actor['host_incarnation'])] = actor
        return [actor for actor in found.values() if actor['noun'] in (CREATURE, VEHICLE)]

    def sample(self, discovered):
        memory, rows = self.memory, []
        for actor in discovered:
            obj = actor['object']
            try:
                if memory.pointer(obj + 0x24) != actor['incarnation'] or memory.read(obj + 0x21, 1)[0]:
                    continue
                creature = actor['noun'] == CREATURE
                spatial = struct.unpack('<3f', memory.read(obj + (0xC4 if creature else 0x38), 12))
                animated = memory.pointer(obj + (0xB54 if creature else 0xD00))
                model = memory.pointer(animated + 0x180) if creature and animated else memory.pointer(obj + 0xD0) if not creature else 0
                if not model:
                    continue
                model_position = struct.unpack('<3f', memory.read(model + 0x0C, 12))
                if not finite((*spatial, *model_position)):
                    continue
                row = dict(**actor, kind='creature' if creature else 'vehicle', spatial=spatial,
                           model=model, model_position=model_position,
                           model_spatial_error=distance(spatial, model_position), animated=animated)
                if creature:
                    row.update(health=struct.unpack('<f', memory.read(obj + 0x5E0, 4))[0],
                               dead=bool(memory.read(obj + 0xB5E, 1)[0]))
                spatial_object = obj + (0xC0 if creature else 0x34)
                # Shared native getters C41EA0/C42080 read these scalar fields.
                row.update(desired_speed=struct.unpack('<f', memory.read(spatial_object + 0x1E0, 4))[0],
                           standard_speed=struct.unpack('<f', memory.read(spatial_object + 0x1E4, 4))[0])
                # dc04 D20610/C41EC0 velocity, C42020/C43F40 request copy.
                # Vehicle CA7A40 constructs this inherited locomotion layout;
                # vehicle C9EA20 and creature C02AD0 both reach C43FA0.
                request = spatial_object + 0x1F4
                row.update(velocity=struct.unpack('<3f', memory.read(spatial_object + 0x1C8, 12)),
                           destination=struct.unpack('<3f', memory.read(request + 0x14, 12)),
                           goal_stop_distance=struct.unpack('<f', memory.read(request + 0x20, 4))[0],
                           movement_mode=memory.pointer(request + 0x5C),
                           acceptable_stop_distance=struct.unpack('<f', memory.read(request + 0x60, 4))[0])
                first, last = struct.unpack('<II', memory.read(request, 8))
                row.update(waypoints_begin=first, waypoints_end=last, effective_destination=None)
                if first == last:
                    row['effective_destination'] = row['destination']
                elif last > first and (last-first) % 0x3C == 0 and (last-first)//0x3C <= 4096:
                    # C42520 returns end-3C as Vec3* for a nonempty route.
                    row['effective_destination'] = struct.unpack('<3f', memory.read(last-0x3C, 12))
                strategy = memory.pointer(spatial_object + 0x4CC)
                row['locomotion_strategy'] = strategy
                if strategy:
                    row['locomotion_strategy_vtable'] = memory.pointer(strategy)
                    row['locomotion_strategy_update'] = memory.pointer(row['locomotion_strategy_vtable'] + 0x0C)
                if animated:
                    core = memory.pointer(animated + 0x17C)
                    row.update(animated_position=struct.unpack('<3f', memory.read(animated + 4, 12)),
                               core=core,
                               core_position=struct.unpack('<3f', memory.read(core + 0x18, 12)) if core else None,
                               clip=current_clip(memory, animated))
                    if not creature:
                        animated_model = memory.pointer(animated + 0x180)
                        row.update(animated_model=animated_model,
                                   animated_model_position=struct.unpack('<3f', memory.read(animated_model + 0x0C, 12))
                                   if animated_model else None)
                if not creature:
                    flags = memory.read(obj + 0xA8, 4)
                    row.update(spatial_flag_bytes=list(flags), spatial_dirty=flags[0],
                               spatial_enabled=flags[1], spatial_flags=memory.pointer(obj + 0x84),
                               view=memory.pointer(obj + 0x14))
                    row['spatial_model_driven_mask04'] = bool(row['spatial_flags'] & 0x04)
                    if row['view']:
                        row['view_vtable'] = memory.pointer(row['view'])
                        row['view_update'] = memory.pointer(row['view_vtable'] + 4)
                if memory.pointer(obj + 0x24) == actor['incarnation'] and not memory.read(obj + 0x21, 1)[0]:
                    rows.append(row)
            except OSError:
                # Native destruction may race a read. Re-enumeration replaces
                # this incarnation, and no sampled pointers are called/written.
                continue
        return rows


def clip_summary(history):
    transitions, resets, clock_regressions = [], [], []
    valid = [(row['wall'], actor['clip']) for row, actor in history if actor.get('clip')]
    for (before_wall, before), (wall, current) in zip(valid, valid[1:]):
        same_clip = current['clip'] == before['clip']
        same_slot = (current['slot'], current['generation']) == (before['slot'], before['generation'])
        event = dict(wall=wall, previous_wall=before_wall, clip=current['clip'],
                     previous_clip=before['clip'], previous_time=before['time'], time=current['time'],
                     previous_generation=before['generation'], generation=current['generation'],
                     previous_slot=before['slot'], slot=current['slot'])
        if not same_clip:
            transitions.append(event)
        elif not same_slot:
            resets.append(event)
        elif current['time'] + 1e-6 < before['time']:
            # A clock wrap in a retained native slot can be a normal loop. It
            # is recorded separately and never counted as a playback restart.
            clock_regressions.append(event)
    return dict(valid_samples=len(valid), distinct_clips=sorted({clip['clip'] for _, clip in valid}),
                clip_transitions=transitions, same_clip_slot_replacements=resets,
                retained_slot_clock_regressions=clock_regressions)


def summarize(samples):
    result = []
    for side in ('host', 'guest'):
        histories = {}
        for row in samples:
            for actor in row[side]:
                histories.setdefault((actor['kind'], actor['marker'], actor['host_incarnation']), []).append((row, actor))
        reports = []
        for (kind, marker, incarnation), history in histories.items():
            first = history[0][1]
            steps = [distance(before['model_position'], current['model_position'])
                     for (_, before), (_, current) in zip(history, history[1:])]
            reports.append(dict(kind=kind, marker=marker, incarnation=incarnation, samples=len(history),
                                seconds=history[-1][0]['wall']-history[0][0]['wall'],
                                spatial_displacement=max(distance(actor['spatial'], first['spatial']) for _, actor in history),
                                model_displacement=max(distance(actor['model_position'], first['model_position']) for _, actor in history),
                                model_error_p95=percentile([actor['model_spatial_error'] for _, actor in history], .95),
                                model_step_p95=percentile(steps, .95), model_step_max=max(steps) if steps else 0,
                                unchanged_model_steps=sum(step < 0.0001 for step in steps),
                                clips=clip_summary(history)))
        result.append(dict(side=side, actors=reports))
    return result


def peer_summary(samples):
    """Compare independently sampled live peers, never a cached packet pose."""
    histories = {}
    for row in samples:
        host = {(actor['kind'], actor['marker'], actor['host_incarnation']): actor for actor in row['host']}
        for guest in row['guest']:
            key = (guest['kind'], guest['marker'], guest['host_incarnation'])
            if key not in host:
                continue
            control = host[key]
            histories.setdefault(key, []).append(dict(spatial_error=distance(control['spatial'], guest['spatial']),
                model_error=distance(control['model_position'], guest['model_position']),
                live=(not control['dead'] and not guest['dead']) if 'dead' in control and 'dead' in guest else None,
                clip_match=control.get('clip') and guest.get('clip') and control['clip']['clip'] == guest['clip']['clip']))
    return [dict(kind=kind, marker=marker, incarnation=incarnation, samples=len(history),
                 spatial_error_p95=percentile([row['spatial_error'] for row in history], .95),
                 spatial_error_max=max(row['spatial_error'] for row in history),
                 model_error_p95=percentile([row['model_error'] for row in history], .95),
                 known_live_samples=sum(row['live'] is True for row in history),
                 live_spatial_error_p95=percentile([row['spatial_error'] for row in history if row['live'] is True], .95),
                 actual_current_clip_matches=sum(bool(row['clip_match']) for row in history))
            for (kind, marker, incarnation), history in histories.items()]


def start_mine(games, records, port):
    host, guest = games
    host.call('coop.adventure cancel', required=False)
    for game in games:
        menu(game, records[game.instance])
        game.call('coop.leave')
    host.call(f'coop.host {port} MotionHost')
    guest.call(f'coop.join 127.0.0.1:{port} MotionGuest')
    host.wait('two Mine participants', lambda: host.adventure()['players'] == 2)
    host.call('coop.adventure start ' + MINE)
    for game in games:
        state = lobby(game, 2, TITLE)
        check('By Maxis' in state and 'change_enabled=0' in state, 'stock Mine captain lock mismatch')
    for game in (guest, host):
        click_named(game, 'prep_ready')
    playing(games, records)


def capture(games, records, output, seconds, hz):
    readers = [MotionMemory(game, records[game.instance]) for game in games]
    combat_readers = [MineMemory(game, records[game.instance]) for game in games]
    samples = []
    try:
        discovered = [reader.discover() for reader in readers]
        for reader in combat_readers:
            reader.discover()
        for game in games:
            game.shot('motion-start')
        initial = [game.call('coop.adventure.world status') for game in games]
        initial_combat = [reader.sample([]) for reader in combat_readers]
        start, next_discovery, next_sample = time.monotonic(), 1.0, 0.0
        while time.monotonic()-start < seconds:
            elapsed = time.monotonic()-start
            if elapsed >= next_discovery:
                discovered = [reader.discover() for reader in readers]
                next_discovery = time.monotonic()-start+1.0
            row = dict(wall=time.monotonic()-start)
            for side, reader, entities in zip(('host', 'guest'), readers, discovered):
                row[side] = reader.sample(entities)
            samples.append(row)
            next_sample = max(next_sample+1/hz, time.monotonic()-start+1/hz)
            time.sleep(max(0, start+next_sample-time.monotonic()))
        final = [game.call('coop.adventure.world status') for game in games]
        final_combat = [reader.sample([]) for reader in combat_readers]
        settling_combat = [dict(wall=time.monotonic()-start, peers=final_combat)]
        settle_deadline = time.monotonic()+2
        while time.monotonic() < settle_deadline:
            time.sleep(.2)
            final_combat = [reader.sample([]) for reader in combat_readers]
            settling_combat.append(dict(wall=time.monotonic()-start, peers=final_combat))
            if (final_combat[0]['generator_health'] == final_combat[1]['generator_health'] and
                    [goal['count'] for goal in final_combat[0]['goals']['goals']] ==
                    [goal['count'] for goal in final_combat[1]['goals']['goals']]):
                break
        for game in games:
            check('total faults: 0' in game.call('exc'), 'native fault during actor motion trace')
            game.shot('motion-end')
        return dict(result='CAPTURED', seconds=seconds, target_hz=hz, samples=samples,
                    initial_status=initial, final_status=final, summary=summarize(samples),
                    peer_summary=peer_summary(samples), initial_combat=initial_combat, final_combat=final_combat,
                    settling_combat=settling_combat,
                    limits=['Asynchronous read-only native observations; repeated wall samples can share a rendered frame.',
                            'Same-ID native slot/generation replacement is recorded separately from retained-slot loop-clock wraps.',
                            'A moving spatial/model path alone does not establish walking clip continuity.',
                            'No synthetic poses, actor orders, damage, or clip playback calls are used.'])
    finally:
        for reader in readers:
            reader.close()
        for reader in combat_readers:
            reader.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=20)
    parser.add_argument('--hz', type=float, default=20)
    parser.add_argument('--port', type=int, default=47960)
    parser.add_argument('--attach', action='store_true')
    parser.add_argument('--keep-running', action='store_true')
    args = parser.parse_args()
    check(5 <= args.seconds <= 60 and 5 <= args.hz <= 60, 'use a bounded 5..60 second, 5..60 Hz trace')
    records = verify_lab(args.metadata, (args.host, args.guest))
    args.out.mkdir(parents=True, exist_ok=True)
    games = [Game(instance, args.out, timeout=180) for instance in (args.host, args.guest)]
    for game in games:
        _verify_game(game, records[game.instance])
    try:
        if not args.attach:
            start_mine(games, records, args.port)
        installation = Path(json.loads(args.metadata.read_text(encoding='utf-8'))['installation'])
        shadows = []
        for game in games:
            mods = game.call('mods')
            generation = re.search(r'^coop\s+loaded\s+gen=(\d+)\b', mods, re.M)
            check(generation, 'loaded DLL generation unavailable')
            path = installation / 'SporebinEP1/mods/.shadow' / f'coop.i{game.instance}.{generation[1]}.dll'
            shadows.append(dict(instance=game.instance, path=str(path), sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        result = capture(games, records, args.out, args.seconds, args.hz)
        result.update(profiles=records, loaded_shadows=shadows, scenario=MINE)
        (args.out / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(dict(samples=len(result['samples']), actors=[len(side['actors']) for side in result['summary']])), flush=True)
        return 0
    except (FixtureError, OSError, ValueError) as error:
        (args.out / 'failure.json').write_text(json.dumps(dict(result='FAIL', error=str(error)), indent=2)+'\n', encoding='utf-8')
        print('FAIL:', error, flush=True)
        return 1
    finally:
        if not args.keep_running:
            games[0].call('coop.adventure cancel', required=False)
            for game in games:
                menu(game, records[game.instance])
        for game in games:
            game.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
