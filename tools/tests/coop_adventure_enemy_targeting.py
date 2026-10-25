#!/usr/bin/env python3
"""Observe natural host NPC acquisition and damage of a nearby party captain.

Uses the stock locked-captain Crystal Mine and the existing owner-local placement
fixture. No NPC target, AI order, health, authored property or damage is written.
The caller must own the recorded hidden/muted Windows pair lease.
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time

from adventure_control_fixture import local_avatar, teleport
from adventure_interaction_fixture import Native, _class_data, _verify_game
from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_actor_motion import CREATURE, MotionMemory, start_mine
from coop_adventure_preparation import menu

SCENARIO = '408A0000!6E008979.366A930D'
HOUND_CLASS = 1082


def check(value, message):
    if not value:
        raise FixtureError(message)


def norm(vector):
    return math.sqrt(sum(value*value for value in vector))


def distance(first, second):
    return norm([x-y for x, y in zip(first, second)])


def unit(vector):
    length = norm(vector)
    check(length > .001, 'degenerate native captain staging vector')
    return [value/length for value in vector]


def nearby_point(memory, enemy, forward=None, offset=3.5):
    position = struct.unpack('<3f', memory.read(enemy+0xC4, 12))
    velocity = forward if forward is not None else struct.unpack('<3f', memory.read(enemy+0xC0+0x1C8, 12))
    normal = unit(position)
    vertical_speed = sum(x*y for x, y in zip(velocity, normal))
    heading = [velocity[index]-normal[index]*vertical_speed for index in range(3)]
    if norm(heading) < .1:
        heading = [-normal[1], normal[0], 0]
    heading = unit(heading)
    destination = unit([position[index]+heading[index]*offset for index in range(3)])
    return [value*norm(position) for value in destination]


def captain_refs(game):
    text = game.call('coop.status')
    result = {game.player_id(): local_avatar(game)}
    for match in re.finditer(r"remote (\d+) [^\n]+?puppet=(?:0x)?([0-9A-Fa-f]+)", text):
        result[int(match[1])] = int(match[2], 16)
    check(set(result) == {0, 1} and all(result.values()), 'both native party captain identities are required')
    return result


def read_captains(reader, refs):
    memory = reader.memory
    return {player: dict(object=avatar, incarnation=memory.pointer(avatar+0x24),
                         political_id=memory.pointer(avatar+0x30),
                         position=struct.unpack('<3f', memory.read(avatar+0xC4, 12)),
                         health=struct.unpack('<f', memory.read(avatar+0x5E0, 4))[0],
                         dead=bool(memory.read(avatar+0xB5E, 1)[0]),
                         targetable=bool(memory.read(avatar+0x135, 1)[0]),
                         stealth=bool(memory.read(avatar+0xBB1, 1)[0]),
                         noise=struct.unpack('<f', memory.read(avatar+0xB80, 4))[0],
                         species=memory.pointer(avatar+0xB20),
                         species_key=struct.unpack('<3I', memory.read(avatar+0xB28, 12)),
                         native_avatar_flags=memory.pointer(avatar+0xB58))
            for player, avatar in refs.items()}


def verify_forward(game, memory, enemy):
    base = int(re.search(r'\bbase=0x([0-9A-Fa-f]+)', game.call('info'))[1], 16)
    table = memory.pointer(enemy+0xC0)
    address = memory.pointer(table+0x5C)
    check(table == base+0x1469EC0-0x400000 and address == base+0xC891B0-0x400000,
          'native animal spatial/forward target differs from the reviewed dc04 table')
    expected = bytes.fromhex('83ec188b018b5030ffd2508d44241068')
    check(memory.read(address, len(expected)) == expected and
          memory.read(address+0x71, 3) == bytes.fromhex('c20400') and
          memory.read(address+0x9D, 3) == bytes.fromhex('c20400'),
          'native forward prefix or reviewed ret4 differs')
    return dict(table=hex(table), address=hex(address), bytes=expected.hex(), ret4_offsets=[0x71, 0x9D])


def sense_parameter_fields(memory, enemy):
    # These are readonly source fields, not ST0 helper return values. The EAX-only
    # console call bridge cannot safely capture native float-returning helpers.
    phenotype = memory.pointer(enemy+0xE84)
    fields = list(struct.unpack('<4f', memory.read(phenotype+0x34C, 16))) if phenotype else None
    mode = memory.pointer(memory.singleton)
    resource = memory.pointer(memory.pointer(mode+0x74)+0x10)
    classes, _ = _class_data(memory, resource, runtime=True)
    authored = next((item for item in classes if item['index'] == HOUND_CLASS), None)
    act = memory.goals()['act']
    current = authored['acts'][act] if authored and 0 <= act < len(authored['acts']) else None
    return dict(species=memory.pointer(enemy+0xB20), phenotype=phenotype,
                phenotype_fields=dict(zip(('awareness', 'fov_degrees', 'hearing', 'sense_multiplier'), fields))
                if fields else None, act=act, resource_class_act=current['address'] if current else None,
                resource_act_awareness=struct.unpack('<f', memory.read(current['address']+0x484, 4))[0]
                if current else None)


def native_forward(native, enemy, scratch):
    check(native.virtual(enemy+0xC0, 0x5C, scratch) == scratch, 'native forward did not return its scratch output')
    result = struct.unpack('<3f', native.memory.read(scratch, 12))
    check(all(math.isfinite(value) for value in result) and .9 < norm(result) < 1.1,
          'native forward is not a finite normalized vector')
    return result


def sensed_records(memory, enemy):
    # dc04 C04993 dereferences one record pointer and C0499F advances by 4.
    # C068B0 constructs the pointed-to 0x14-byte records in the native pool.
    first, last = struct.unpack('<II', memory.read(enemy+0x1124, 8))
    check(last >= first and (last-first) % 4 == 0 and (last-first)//4 <= 256,
          'native sensed-record bounds differ from the reviewed layout')
    records = []
    for entry in range(first, last, 4):
        address = memory.pointer(entry)
        check(address >= 0x10000, 'native sensed-record pointer is null or invalid')
        raw = memory.read(address, 0x14)
        records.append(dict(address=address, distance=struct.unpack_from('<f', raw)[0], sight=raw[4], heard=raw[5],
                            target=struct.unpack_from('<I', raw, 8)[0],
                            reference=struct.unpack_from('<I', raw, 0x10)[0]))
    return dict(first=first, last=last, records=records)


def deque_count(iterators):
    """Read the verified eight-record dc04 deque without calling pool code."""
    begin_current, begin_block, begin_end, begin_node, end_current, end_block, end_end, end_node = iterators
    if not any(iterators):
        return 0
    if (begin_end-begin_block != 0xA0 or end_end-end_block != 0xA0 or
            not begin_block <= begin_current <= begin_end or not end_block <= end_current <= end_end or
            (begin_current-begin_block) % 0x14 or (end_current-end_block) % 0x14 or
            begin_node % 4 or end_node % 4 or not 0 <= end_node-begin_node <= 4096*4):
        return None
    count = (8*((end_node-begin_node)//4) + (end_current-end_block)//0x14 -
             (begin_current-begin_block)//0x14)
    return count if 0 <= count <= 4096 else None


def sensed_pool(memory, enemy):
    # Native C068B0 / C05FE0 iterator advances establish 0x14-byte records,
    # eight per 0xA0-byte block. Malformed/asynchronously mixed reads are unknown.
    raw = list(struct.unpack('<8I', memory.read(enemy+0x1034+8, 32)))
    return dict(iterators=raw, count=deque_count(raw))


def scenario_sensor(memory, enemy, captains):
    # C7D210(NPC+670) returns the inline pointer vector at NPC+6D8.
    # EC0FF2/EC10F5 consume spatial pointers with a four-byte stride.
    first, last, capacity = struct.unpack('<3I', memory.read(enemy+0x6D8, 12))
    check(last >= first and (last-first) % 4 == 0 and (last-first)//4 <= 4096,
          'native scenario sensor pointer-vector bounds differ')
    pointers = list(struct.unpack('<'+'I'*((last-first)//4), memory.read(first, last-first))) if last > first else []
    return dict(first=first, last=last, capacity=capacity, pointers=pointers,
                captain_players=[player for player, captain in captains.items() if captain+0xC0 in pointers])


def custom_entries(memory, enemy):
    # C072B1 copies authored classAct+4CC to NPC+674; EC0F08/EC0F11
    # index that vector by uint16 node+12 shifted left five (0x20-byte entries).
    first, last = struct.unpack('<2I', memory.read(enemy+0x674, 8))
    check(last >= first and (last-first) % 0x20 == 0 and (last-first)//0x20 <= 256,
          'native scenario custom-entry bounds differ')
    fields = ('condition', 'condition_target_type', 'condition_target_value', 'action',
              'action_target_type', 'action_target_value', 'secondary_type', 'secondary_value')
    return dict(first=first, last=last,
                entries=[dict(index=index, **dict(zip(fields, struct.unpack('<8I', memory.read(address, 0x20)))))
                         for index, address in enumerate(range(first, last, 0x20))])


def behavior_tree(memory, enemy):
    # Verified BC9160/BC927E/BC92F1/BC9450/BC8C36: tree+0 is root*, while
    # node+80 points to inline 0x88-byte child nodes, not an array of pointers.
    tree = memory.pointer(enemy+0xB4C)
    root = memory.pointer(tree) if tree else 0
    result = dict(tree=tree, root=root, nodes=[], errors=[])
    pending, visited = [(root, 0)], set()
    while pending:
        address, depth = pending.pop()
        if not address or address in visited:
            continue
        if depth > 8 or len(visited) >= 512:
            result['errors'].append('native tree walk exceeded its reviewed bounds')
            break
        visited.add(address)
        try:
            raw = memory.read(address, 0x88)
            count, children = struct.unpack_from('<II', raw, 0x7C)
            if count > 64 or (count and children < 0x10000):
                result['errors'].append('invalid native child-array bounds at '+hex(address))
                continue
            result['nodes'].append(dict(address=address, depth=depth, id=struct.unpack_from('<I', raw)[0],
                                        score_callback=struct.unpack_from('<I', raw, 8)[0],
                                        custom_entry_index=struct.unpack_from('<H', raw, 0x12)[0],
                                        behavior=struct.unpack_from('<I', raw, 0x70)[0],
                                        child_count=count, child_array=children))
            pending.extend((children+index*0x88, depth+1) for index in reversed(range(count)))
        except (OSError, ValueError) as error:
            result['errors'].append(str(error))
    return result


def verify_calls(game, memory):
    evidence = []
    for name, preferred, encoded in (
            ('SporeCoop::Adventure::CreatureTarget', 0xC0EFD0, '8b897c0e000085c9740d8b018b505c68'),
            ('SporeCoop::Adventure::IsHostileToCreature', 0xC0B9C0, '568bf1e8f81bf3ff85c0742d8b4c2408')):
        address = int(game.call('sym '+name).split()[0], 16)
        raw = memory.read(address, len(bytes.fromhex(encoded)))
        check(raw == bytes.fromhex(encoded), 'reviewed native getter prefix mismatch: '+name)
        evidence.append(dict(name=name, preferred=hex(preferred), address=hex(address), bytes=raw.hex()))
    return evidence


def analyze(samples, near_player, initial_owner_health, captain_refs_on_host):
    actual_near_seconds = 0.
    selected = getter_selected = 0
    duplicate_max = 0
    sight_samples = 0
    sight_streak = cone_streak = longest_sight = longest_cone = 0.
    ranges, errors = [], []
    target = captain_refs_on_host[near_player]
    for index, row in enumerate(samples):
        near = row['peers'][0][near_player]
        far = row['peers'][0][1-near_player]
        owner = row['peers'][near_player][near_player]
        separation = distance(row['enemy_position'], near['position'])
        ranges.append(separation)
        errors.append(distance(near['position'], owner['position']))
        dt = min(.15, samples[index+1]['wall']-row['wall']) if index+1 < len(samples) else 0
        if separation < 10 and distance(row['enemy_position'], far['position']) > 60:
            # Bound the contribution across console/screenshot gaps rather than
            # treating a single asynchronous read as a long proximity window.
            actual_near_seconds += dt
        selected += row['target_player'] == near_player
        getter_selected += row.get('target_getter_player') == near_player
        duplicate_max = max(duplicate_max, sum(record['target'] == target or record['reference'] == target
                                                for record in row['sensed']['records']))
        sight = any(record['target'] == target and record['sight'] for record in row['sensed']['records'])
        sight_samples += sight
        continuous = index+1 == len(samples) or samples[index+1]['wall']-row['wall'] <= .2
        sight_streak = sight_streak+dt if sight and continuous else 0.
        longest_sight = max(longest_sight, sight_streak)
        cone = (separation < 10 and row.get('captain_forward_dot', {}).get(near_player, -1) >= .5 and
                not near.get('stealth', False))
        cone_streak = cone_streak+dt if cone and continuous else 0.
        longest_cone = max(longest_cone, cone_streak)
    owner_health = [row['peers'][near_player][near_player]['health'] for row in samples]
    loss = initial_owner_health-min(owner_health) if owner_health else 0
    pool_counts = [row['sensed_pool']['count'] for row in samples
                   if row.get('sensed_pool', {}).get('count') is not None]
    return dict(actual_host_near_seconds=actual_near_seconds, target_samples=selected,
                checked_getter_target_samples=getter_selected, natural_owner_health_loss=loss,
                owner_health_first=owner_health[0] if owner_health else None,
                owner_health_last=owner_health[-1] if owner_health else None,
                actual_host_range_min=min(ranges) if ranges else None,
                actual_host_range_max=max(ranges) if ranges else None,
                owner_host_pose_error_max=max(errors) if errors else None,
                sensed_near_captain_records_max=duplicate_max,
                scenario_sensor_near_captain_samples=sum(
                    near_player in row.get('scenario_sensor', {}).get('captain_players', []) for row in samples),
                scenario_sensor_count_max=max((len(row.get('scenario_sensor', {}).get('pointers', []))
                                               for row in samples), default=0),
                native_sight_samples=sight_samples,
                longest_consecutive_native_sight_seconds=longest_sight,
                longest_consecutive_actual_forward_cone_seconds=longest_cone,
                sensed_record_count_max=max((len(row['sensed']['records']) for row in samples), default=0),
                sensed_pool_count_min=min(pool_counts, default=None),
                sensed_pool_count_max=max(pool_counts, default=None),
                sensed_pool_count_first=pool_counts[0] if pool_counts else None,
                sensed_pool_count_last=pool_counts[-1] if pool_counts else None,
                sensed_pool_unknown_samples=sum(row.get('sensed_pool', {}).get('count') is None for row in samples),
                raw_target_counts=dict(Counter(str(row['target_combatant']) for row in samples)))


def loaded_identity(games, metadata, expected):
    installation = Path(json.loads(metadata.read_text(encoding='utf-8'))['installation'])
    result = []
    for game in games:
        mods = game.call('mods')
        generation = re.search(r'^coop\s+loaded\s+gen=(\d+)\b', mods, re.M)
        check(generation, 'loaded co-op generation is unavailable')
        path = installation/'SporebinEP1/mods/.shadow'/f'coop.i{game.instance}.{generation[1]}.dll'
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        check(not expected or digest == expected.lower(), 'loaded shadow differs from the requested test build')
        result.append(dict(instance=game.instance, shadow=str(path), sha256=digest, mods=mods))
    return result


def run(args, games, records):
    result = dict(result='INCONCLUSIVE', scenario=SCENARIO, near_player=args.near_player,
                  profiles=records, samples=[], placement_events=[], phases=[],
                  configuration=dict(seconds=args.seconds, switches=args.switches,
                                     maintain_near=args.maintain_near, native_forward=args.native_forward,
                                     near_distance=args.near_distance, stage_interval=args.stage_interval,
                                     expected=args.expect),
                  limits=['Only owner-local captain placement; no NPC target, AI, health, authored property or damage writes.',
                          'Read-only native samples are asynchronous; authored behavior remains native.',
                          'A host-near positive control is required before interpreting ignored guests as a bug.'])
    readers = []
    try:
        for game in games:
            _verify_game(game, records[game.instance])
        result['loaded_shadows'] = loaded_identity(games, args.metadata, args.expected_sha256)
        start_mine(games, records, args.port)
        readers = [MotionMemory(game, records[game.instance]) for game in games]
        refs = [captain_refs(game) for game in games]
        memory = readers[0].memory
        result['captains'] = refs
        result['native_entry_preflight'] = verify_calls(games[0], memory)
        found = [actor for actor in readers[0].discover() if actor['noun'] == CREATURE and
                 actor['cls'] == HOUND_CLASS and not memory.read(actor['object']+0xB5E, 1)[0]]
        enemy = next((actor for actor in found if actor['marker'] == args.marker), None)
        check(enemy, 'the requested fresh native Crystal Hound is unavailable')
        obj = enemy['object']
        result['enemy'] = enemy
        result['custom_entries'] = custom_entries(memory, obj)
        result['behavior_tree'] = behavior_tree(memory, obj)
        result['sense_parameter_fields'] = sense_parameter_fields(memory, obj)
        before = [read_captains(reader, avatars) for reader, avatars in zip(readers, refs)]
        result['before_placement'] = before
        result['initial_status'] = [game.call('coop.adventure.world status') for game in games]
        with Native(games[0], records[games[0].instance]) as native:
            forward_scratch = None
            if args.native_forward:
                result['native_forward_preflight'] = verify_forward(games[0], memory, obj)
                forward_scratch = native.scratch(bytes(12))
            direction = native_forward(native, obj, forward_scratch) if args.native_forward else None
            hostile = {player: bool(native.call('SporeCoop::Adventure::IsHostileToCreature', 'thiscall', obj, avatar))
                       for player, avatar in refs[0].items()}
            check(all(hostile.values()), 'the native actor must be hostile to both captains')
            result['hostile'] = hostile
            point = nearby_point(memory, obj, direction, args.near_distance)
            teleport(games[args.near_player], records[games[args.near_player].instance], point)
            result['placement_events'].append(dict(wall=0, player=args.near_player, requested_position=point))
            time.sleep(.2)
            for game in games:
                game.shot('staged-captain-near')
            start, next_getter, next_stage = time.monotonic(), 0., args.stage_interval
            phase, near_player = 0, args.near_player
            phase_length = args.seconds/(args.switches+1)
            result['phases'].append(dict(index=phase, player=near_player, wall=0,
                                         initial_owner_health=before[near_player][near_player]['health']))
            while time.monotonic()-start < args.seconds:
                elapsed = time.monotonic()-start
                if memory.pointer(obj+0x24) != enemy['incarnation'] or memory.read(obj+0x21, 1)[0]:
                    break
                direction = native_forward(native, obj, forward_scratch) if args.native_forward else None
                if phase < args.switches and elapsed >= (phase+1)*phase_length:
                    old_player, phase = near_player, phase+1
                    near_player = 1-near_player
                    original = before[old_player][old_player]['position']
                    teleport(games[old_player], records[games[old_player].instance], original)
                    result['placement_events'].append(dict(wall=elapsed, player=old_player,
                                                          requested_position=original, purpose='return-far'))
                    point = nearby_point(memory, obj, direction, args.near_distance)
                    teleport(games[near_player], records[games[near_player].instance], point)
                    result['placement_events'].append(dict(wall=elapsed, player=near_player,
                                                          requested_position=point, purpose='switch-near'))
                    initial_health = read_captains(readers[near_player], refs[near_player])[near_player]['health']
                    result['phases'].append(dict(index=phase, player=near_player, wall=elapsed,
                                                 initial_owner_health=initial_health))
                    next_stage = time.monotonic()-start+args.stage_interval
                # Repeated owner-only staging is a fixture prerequisite, never
                # an NPC pursuit order. Stop staging once native AI acquires it.
                if (args.maintain_near and elapsed >= next_stage and
                        memory.pointer(obj+0xE7C) != refs[0][near_player]+0x5A8):
                    point = nearby_point(memory, obj, direction, args.near_distance)
                    teleport(games[near_player], records[games[near_player].instance], point)
                    result['placement_events'].append(dict(wall=elapsed, player=near_player, requested_position=point))
                    next_stage = elapsed+args.stage_interval
                row = dict(wall=time.monotonic()-start, phase=phase, near_player=near_player,
                           enemy_player_evaluator_mode=memory.pointer(obj+0x101C),
                           enemy_position=struct.unpack('<3f', memory.read(obj+0xC4, 12)),
                           enemy_health=struct.unpack('<f', memory.read(obj+0x5E0, 4))[0],
                           enemy_dead=bool(memory.read(obj+0xB5E, 1)[0]),
                           target_combatant=memory.pointer(obj+0xE7C), sensed=sensed_records(memory, obj),
                           scenario_sensor=scenario_sensor(memory, obj, refs[0]),
                           sensed_pool=sensed_pool(memory, obj))
                row['target_player'] = next((player for player, avatar in refs[0].items()
                                             if row['target_combatant'] == avatar+0x5A8), None)
                if elapsed >= next_getter:
                    target = native.call('SporeCoop::Adventure::CreatureTarget', 'thiscall', obj)
                    row.update(target_getter=target, target_getter_player=next(
                        (player for player, avatar in refs[0].items() if target == avatar), None))
                    next_getter = elapsed+.5
                row['peers'] = [read_captains(reader, avatars) for reader, avatars in zip(readers, refs)]
                if direction is not None:
                    row['enemy_forward'] = direction
                    row['captain_forward_dot'] = {
                        player: sum(a*b for a, b in zip(direction, unit([
                            a-b for a, b in zip(row['peers'][0][player]['position'], row['enemy_position'])])))
                        for player in (0, 1)}
                result['samples'].append(row)
                if row['enemy_dead'] or any(row['peers'][player][player]['health'] <= 1 for player in (0, 1)):
                    break
                time.sleep(.05)
        for phase in result['phases']:
            phase['summary'] = analyze([row for row in result['samples'] if row['phase'] == phase['index']],
                                       phase['player'], phase['initial_owner_health'], refs[0])
        result['summary'] = result['phases'][0]['summary']
        result['pool_and_sense_summary'] = {
            player: analyze(result['samples'], player, before[player][player]['health'], refs[0])
            for player in (0, 1)}
        result['final_status'] = [game.call('coop.adventure.world status') for game in games]
        result['faults_before_cleanup'] = {}
        for game in games:
            faults = game.call('exc')
            result['faults_before_cleanup'][game.instance] = faults
            check('total faults: 0' in faults, 'native exception during natural targeting observation')
            game.shot('after-natural-targeting-window')
        evidence = result['summary']
        check(evidence['actual_host_near_seconds'] >= .75, 'actual host-side captain proximity prerequisite failed')
        attacked = evidence['target_samples'] >= 3 and evidence['checked_getter_target_samples'] > 0 and evidence['natural_owner_health_loss'] > .1
        if args.expect == 'attacked':
            check(attacked, 'native host AI did not acquire and naturally damage the nearby captain')
            check(all(summary['sensed_near_captain_records_max'] <= 1
                      for summary in result['pool_and_sense_summary'].values()),
                  'duplicate sensed-captain entries accumulated')
            check(evidence['sensed_pool_count_max'] is not None, 'native pool count could not be observed safely')
        elif args.expect == 'ignored':
            check(evidence['target_samples'] == 0 and evidence['natural_owner_health_loss'] == 0,
                  'the ignored-captain baseline no longer reproduces')
        result.update(result='PASS', naturally_attacked=attacked)
    except (FixtureError, OSError, ValueError) as error:
        result.update(result='FAIL', error=str(error))
    finally:
        for reader in readers:
            reader.close()
        try:
            games[0].call('coop.adventure cancel', required=False)
            for game in games:
                menu(game, records[game.instance])
            result['clean_native_menus'] = True
        except (FixtureError, OSError, ValueError) as error:
            result.update(result='FAIL', cleanup_error=str(error))
        for game in games:
            game.save_log()
        (args.out/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--host', type=int, default=81)
    parser.add_argument('--guest', type=int, default=82)
    parser.add_argument('--near-player', type=int, choices=(0, 1), default=1)
    parser.add_argument('--marker', type=int, default=138)
    parser.add_argument('--seconds', type=float, default=6)
    parser.add_argument('--maintain-near', action='store_true')
    parser.add_argument('--native-forward', action='store_true',
                        help='stage ahead of the verified native forward getter and record cone geometry')
    parser.add_argument('--near-distance', type=float, default=3.5,
                        help='owner-local staging offset in meters, bounded within the Mine sight fixture')
    parser.add_argument('--stage-interval', type=float, default=.25,
                        help='minimum seconds between bounded owner-local staging updates')
    parser.add_argument('--switches', type=int, default=0,
                        help='bounded alternating near-player phases; NPC target is never cleared')
    parser.add_argument('--expect', choices=('observe', 'ignored', 'attacked'), default='observe')
    parser.add_argument('--expected-sha256')
    parser.add_argument('--port', type=int, default=47960)
    args = parser.parse_args()
    check(args.host != args.guest and 3 <= args.seconds <= 15, 'use two instances and a bounded 3..15 second observation')
    check(0 <= args.switches <= 4 and args.seconds/(args.switches+1) >= 2,
          'allow 0..4 captain switches with at least two seconds per natural-AI phase')
    check(2 <= args.near_distance <= 7, 'native owner-only staging offset must remain 2..7 meters')
    check(.1 <= args.stage_interval <= .5, 'owner-only staging interval must remain .1.. .5 seconds')
    args.out.mkdir(parents=True, exist_ok=True)
    records = verify_lab(args.metadata, (args.host, args.guest))
    result = run(args, [Game(instance, args.out, timeout=180) for instance in (args.host, args.guest)], records)
    print(json.dumps({key: result[key] for key in ('result', 'summary', 'error', 'cleanup_error') if key in result}), flush=True)
    return 0 if result['result'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
