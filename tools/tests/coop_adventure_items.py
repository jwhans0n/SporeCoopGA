#!/usr/bin/env python3
"""Native held-tool repair and shared item transfer in an existing hidden lab pair.

Requires recorded isolated profiles. No games are launched or profiles edited.
The attachment check discovers Phyrus Farmers in Adventure Town and releases one
guest tool through native DropItem, then requires authoritative reconstruction.
The transfer checks use the same native pickup/drop/physics adapters as gameplay.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct
import time

from adventure_interaction_fixture import Native, _class_data
from coop_adventure import FixtureError, Game, verify_lab


def actors(game, marker):
    rows = []
    for line in game.call(f'coop.adventure.world actorsat {marker} 48').splitlines():
        match = re.search(r'marker=(\d+) incarnation=(\d+) noun=(\d+) object=(0x[0-9a-fA-F]+) class=(\d+)', line)
        if match:
            values = match.groups()
            host = re.search(r'host_incarnation=(\d+)', line)
            rows.append(dict(marker=int(values[0]), incarnation=int(values[1]), noun=int(values[2]),
                             object=int(values[3], 16), class_index=int(values[4]),
                             host_incarnation=int(host[1]) if host else int(values[1]), text=line))
    return rows


def inventory(memory, creature):
    first, last = struct.unpack('<II', memory.read(creature + 0xE40, 8))
    if last < first or (last-first) % 0x24 or (last-first) // 0x24 > 128:
        raise FixtureError('invalid native held-item inventory')
    return [memory.pointer(first+slot+0x20) for slot in range(0, last-first, 0x24)]


def text(memory, address):
    pointer = memory.pointer(address+8)
    if not pointer:
        return ''
    value = bytearray()
    for i in range(160):
        word = memory.read(pointer+i*2, 2)
        if word == b'\0\0':
            break
        value += word
    return value.decode('utf-16le', 'replace')


def creature_name(memory, creature):
    # dc04 cCreatureBase::mSpeciesProfile +B20; native species string16 +51C.
    profile = memory.pointer(creature + 0xB20)
    pointer = memory.pointer(profile + 0x51C) if profile else 0
    if not pointer:
        return ''
    value = bytearray()
    for index in range(160):
        word = memory.read(pointer + index*2, 2)
        if word == b'\0\0':
            break
        value += word
    return value.decode('utf-16le', 'replace')


def captain(game, player):
    if player == game.player_id():
        match = re.search(r'local=([0-9A-Fa-f]+)', game.call('coop.avatar'))
    else:
        match = re.search(rf'remote {player} .*?puppet=(?:0x)?([0-9A-Fa-f]+)', game.call('coop.status'))
    if not match:
        raise FixtureError('party captain address is unavailable')
    return int(match[1], 16)


def item_state(game):
    output = game.call('coop.adventure.world status')
    match = re.search(r'items: attachments=(\d+) revision=(\d+) throws=(\d+) catches=(\d+) gives=(\d+) flying=(\d+)', output)
    if not match:
        raise FixtureError('preview8 item authority diagnostics are unavailable')
    result = dict(zip(('attachments', 'revision', 'throws', 'catches', 'gives', 'flying'), map(int, match.groups())))
    result['relations'] = [dict(zip(('marker', 'incarnation', 'holder_marker', 'holder_incarnation', 'player', 'slot'), map(int, values)))
                           for values in re.findall(r'attachment item=(\d+):(\d+) holder=(\d+):(\d+) player=(\d+) slot=(\d+)', output)]
    return result


def hammer_repair(games, records, output):
    host, guest = games
    with Native(host, records[host.instance]) as a, Native(guest, records[guest.instance]) as b:
        mode = a.memory.pointer(a.memory.singleton)
        resource = a.memory.pointer(a.memory.pointer(mode+0x74)+0x10)
        classes, markers = _class_data(a.memory, resource, runtime=True)
        # This class uses the default "***" override. Discover its real species
        # name from an already live creature, after native act population.
        live_creature_classes = set()
        creature_classes = {entry['index'] for entry in classes if entry['asset']['type'] == 0x2B978C46}
        for marker in markers:
            if marker['class_index'] not in creature_classes:
                continue
            for actor in actors(host, marker['index']):
                if actor['noun'] == 0x18EB45E and 'phyrus farmer' in creature_name(a.memory, actor['object']).lower():
                    live_creature_classes.add(marker['class_index'])
        farmers = [entry for entry in classes if entry['index'] in live_creature_classes]
        if not farmers:
            raise FixtureError('Adventure Town Phyrus Farmer authored class is not loaded')
        candidates = [m for m in markers if m['class_index'] in {c['index'] for c in farmers}]
        rows = []
        for marker in candidates:
            local = [[r for r in actors(g, marker['index']) if r['noun'] == 0x18EB45E] for g in games]
            if not all(local):
                continue
            objects = [entry[0]['object'] for entry in local]
            slots = [inventory(n.memory, obj) for n, obj in zip((a, b), objects)]
            if not any(slots[0]):
                continue
            host_relations = [r for r in item_state(host)['relations'] if r['player'] == 255 and r['holder_marker'] == marker['index']]
            if not host_relations:
                raise FixtureError('native farmer has a held tool but no replicable item identity')
            guest.wait('farmer tools occupy matching native hand slots', lambda: all(
                inventory(b.memory, objects[1])[relation['slot']] for relation in host_relations))
            relation = host_relations[0]
            slot = relation['slot']
            held = inventory(b.memory, objects[1])[slot]
            before = dict(marker=marker['index'], objects=objects, slots=slots, relation=relation,
                          item_states=[item_state(g) for g in games])
            guest.call(f'coop.adventure.world detachtool {marker["index"]} {slot}')
            guest.wait('authoritative NPC hand attachment repaired', lambda: inventory(b.memory, objects[1])[slot] == held)
            after = dict(slots=[inventory(n.memory, obj) for n, obj in zip((a, b), objects)], item_states=[item_state(g) for g in games])
            rows.append(dict(before=before, after=after))
        if not rows:
            raise FixtureError('no native farmer with a held hammer was found')
        return dict(result='PASS', kind='hammer_repair', rows=rows)


def physical(game, marker):
    candidates = [entry for entry in actors(game, marker) if 'powerup=9 ' not in entry['text']]
    if len(candidates) != 1:
        raise FixtureError('one exact native physical item is required at the requested marker')
    return candidates[0]


def box_center(native, obj):
    spatial = native.virtual(obj, 0x0C, 0x01186577)
    if not spatial:
        raise FixtureError('native item/captain spatial cast is unavailable')
    scratch = native.scratch(bytes(24))
    native.virtual(spatial, 0x6C, scratch)
    values = struct.unpack('<6f', native.memory.read(scratch, 24))
    return spatial, [(values[i]+values[i+3])*.5 for i in range(3)]


def throw_pass(games, records, marker, staging, timed=False):
    host, guest = games
    with Native(host, records[host.instance]) as a, Native(guest, records[guest.instance]) as b:
        natives = [a, b]
        original = [physical(game, marker) for game in games]
        kind = 10 if timed else 12
        if any(not re.search(rf'\bpowerup={kind}\b', entry['text']) for entry in original):
            raise FixtureError('the normal test requires a key; the timed test requires a native timed bomb')
        spatials = [native.virtual(entry['object'], 0x0C, 0x01186577) for native, entry in zip(natives, original)]
        def held_by(player):
            return all(spatial in inventory(native.memory, captain(game, player))
                       for game, native, spatial in zip(games, natives, spatials))
        passes = []
        started = None
        started_sim = None
        for owner, receiver in ((guest, host), (host, guest)):
            player, collector = owner.player_id(), receiver.player_id()
            if not held_by(player):
                owner.call(f'coop.adventure.world near {marker} {player}')
                # Let the host observe the guest's movement before validating
                # its distance-limited pickup request.
                time.sleep(.7)
                if timed and started is None:
                    started = time.monotonic()
                    started_sim = a.memory.goals()['elapsed']
                owner.call(f'coop.adventure.world pickup {marker} {player}')
                host.wait('source holds the same physical item on both clients', lambda: held_by(player))
            owner.call(f'coop.adventure.world near {staging} {player} -3 0')
            receiver.call(f'coop.adventure.world near {staging} {collector} 3 0')
            time.sleep(.8)
            _, source = box_center(a, original[0]['object'])
            _, target = box_center(a, captain(host, collector))
            direction = [target[i]-source[i] for i in range(3)]
            distance = math.sqrt(sum(value*value for value in direction))
            if not 1 < distance < 15:
                raise FixtureError('throw source and receiver did not settle at a distinct nearby separation')
            magnitude = 100
            impulse = [value/distance*magnitude for value in direction]
            before = [item_state(game) for game in games]
            if timed and started is None:
                raise FixtureError('timed fixture must begin with an uncollected bomb and an unstarted fuse')
            owner.call(f'coop.adventure.world throw {marker} {player} ' + ' '.join(f'{value:.6f}' for value in impulse))
            host.wait('receiver auto-catches the thrown physical item on every client', lambda: held_by(collector), timeout=8)
            passes.append(dict(source=player, collector=collector, distance=distance, impulse=impulse,
                               before=before, after=[item_state(game) for game in games]))
        result = dict(result='PASS', kind='timed_bomb_pass' if timed else 'ordinary_item_pass', items=original, passes=passes)
        if timed:
            host.wait('the same physical bomb detonates after both catches', lambda: all(
                all(entry['host_incarnation'] != original[0]['host_incarnation'] for entry in actors(game, marker))
                for game in games), timeout=20)
            result['fuse_wall_seconds'] = time.monotonic()-started
            result['fuse_simulation_seconds'] = (a.memory.goals()['elapsed']-started_sim)/1000
            if not 14 <= result['fuse_simulation_seconds'] <= 17:
                raise FixtureError('15-second fixture fuse did not retain its original schedule across both catches')
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--instances', default='61,62')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--mode', choices=('inspect', 'hammer-repair', 'throw-pass', 'timed-pass'), default='inspect')
    parser.add_argument('--marker', type=int, default=125)
    parser.add_argument('--staging', type=int, default=158)
    args = parser.parse_args()
    instances = tuple(map(int, args.instances.split(',')))
    records = verify_lab(args.metadata, instances)
    games = [Game(i, args.out, timeout=90) for i in instances]
    args.out.mkdir(parents=True, exist_ok=True)
    try:
        if args.mode == 'hammer-repair':
            result = hammer_repair(games, records, args.out)
        elif args.mode in ('throw-pass', 'timed-pass'):
            result = throw_pass(games, records, args.marker, args.staging, timed=args.mode == 'timed-pass')
        else:
            result = dict(result='PASS', kind='read_only_item_inspection', states=[item_state(g) for g in games])
        for game in games:
            game.record(args.mode)
            if 'total faults: 0' not in game.call('exc'):
                raise FixtureError('native item regression logged an exception')
        (args.out/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print('PASS:', args.mode)
        return 0
    except (FixtureError, OSError, ValueError) as error:
        (args.out/'failure.json').write_text(json.dumps(dict(error=str(error)), indent=2)+'\n', encoding='utf-8')
        print('FAIL:', error)
        return 1
    finally:
        for game in games:
            game.save_log()
            if game.console.sock:
                game.console.sock.close()


if __name__ == '__main__':
    raise SystemExit(main())
