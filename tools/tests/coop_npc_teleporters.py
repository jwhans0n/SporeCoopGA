#!/usr/bin/env python3
"""Verify real NPC teleporter contact in an already-running private adventure.

The only gameplay action is the isolated actorroute command: ordinary native
locomotion towards an entry. No direct activation, pose writes or teleport calls.
Run separately in Solo, Test Together and ordinary co-op using the same adventure.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import time

from coop_adventure import Game, verify_lab
from coop_editor_party import Ownership
from coop_adventure_actor_motion import MotionMemory, CREATURE
from coop_adventure_combat_movement import actors, distance


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    owner = Ownership(args.metadata, args.instances, args.dll_sha256)
    records = verify_lab(args.metadata, args.instances)
    games = [Game(i, args.output, 60) for i in args.instances]
    memories = [MotionMemory(g, records[g.instance]) for g in games]
    host, memory = games[0], memories[0].memory
    result = dict(passed=False, mode=args.mode, dll_sha256=args.dll_sha256, samples=[])

    def rows(game):
        return game.call('coop.adventure.world actors 0 48')

    def snapshot():
        values = []
        for game, reader in zip(games, memories):
            text = game.call('coop.adventure.world actorsat %d 48' % npc['marker'])
            found = [a for a in actors(text) if a['marker'] == npc['marker'] and
                     a['host_incarnation'] == npc['host_incarnation']]
            values.append(reader.sample(found))
        return values

    try:
        owner.verify_windows()
        text = rows(host)
        entries = []
        for line in text.splitlines():
            if not re.search(r'\bpowerup=8\b', line):
                continue
            row = actors(line)[0]
            if args.entry is not None and row['marker'] != args.entry:
                continue
            marker = memory.pointer(row['object']+0x18)
            row['position'] = struct.unpack('<3f', memory.read(marker+4, 12))
            row['destination'] = struct.unpack('<3f', memory.read(marker+0x1E8, 12))
            entries.append(row)
        npcs = memories[0].sample([a for a in actors(text) if a['noun'] == CREATURE and
                                  (args.npc is None or a['marker'] == args.npc)])
        npcs = [a for a in npcs if not a['dead'] and a['health'] > 0]
        pairs = [(distance(n['spatial'], e['position']), n, e) for n in npcs for e in entries]
        if not pairs:
            raise RuntimeError('A living NPC and native entry are required')
        _, npc, entry = min(pairs, key=lambda item: item[0])
        result.update(npc=npc, entry=entry, initial=snapshot())
        line = next(line for line in text.splitlines() if actors(line) and
                    actors(line)[0]['object'] == npc['object'])
        generation = int(re.search(r'motion_generation=(\d+)', line)[1])
        deadline = time.monotonic()+args.seconds
        next_route = 0
        previous = npc['spatial']
        observed_jump = False
        while time.monotonic() < deadline:
            if time.monotonic() >= next_route:
                host.call('coop.adventure.world actorroute %d %d' % (npc['marker'], entry['marker']))
                next_route = time.monotonic()+.5
            states = snapshot()
            result['samples'].append(states)
            if states[0]:
                observed_jump = observed_jump or distance(previous, states[0][0]['spatial']) > 8
                previous = states[0][0]['spatial']
            host_text = host.call('coop.adventure.world actorsat %d 48' % npc['marker'])
            current_line = next((line for line in host_text.splitlines() if actors(line) and
                                 actors(line)[0]['object'] == npc['object']), '')
            current = re.search(r'motion_generation=(\d+)', current_line)
            warped = observed_jump if args.mode == 'solo' else current and int(current[1]) > generation
            if warped and all(len(s) == 1 and distance(s[0]['spatial'], entry['destination']) < 8 and
                              distance(s[0]['model_position'], s[0]['spatial']) < 2 for s in states):
                result['after'] = states
                result['accepted_generation'] = int(current[1]) if current else 0
                break
            time.sleep(.1)
        else:
            raise RuntimeError('Native NPC route did not produce a replicated teleporter contact')
        result['faults'] = [g.call('exc') for g in games]
        if any('total faults: 0' not in value for value in result['faults']):
            raise RuntimeError('A native fault occurred')
        for g in games:
            g.record('npc-teleported')
        result['passed'] = True
        print('PASS: '+args.mode+' NPC contact reached the authored exit on every native model', flush=True)
    except Exception as error:
        result['error'] = repr(error)
        raise
    finally:
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        for reader in memories:
            reader.close()
        for g in games:
            g.save_log()
        owner.close()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--execute', action='store_true')
    p.add_argument('--metadata', type=Path)
    p.add_argument('--instances', type=int, nargs='+', default=[91, 92])
    p.add_argument('--dll-sha256')
    p.add_argument('--output', type=Path)
    p.add_argument('--mode', choices=['solo', 'together', 'ordinary'], default='together')
    p.add_argument('--npc', type=int)
    p.add_argument('--entry', type=int)
    p.add_argument('--seconds', type=int, default=60)
    args = p.parse_args()
    if not args.execute:
        print(json.dumps(dict(prepared=True, launches_games=False)))
        return
    if not args.metadata or not args.output or not re.fullmatch('[0-9a-f]{64}', args.dll_sha256 or '') or not 1 <= len(args.instances) <= 4 or not 1 <= args.seconds <= 120:
        p.error('Exact owned metadata, DLL SHA, output and a bounded duration are required')
    if (args.mode == 'solo') != (len(args.instances) == 1):
        p.error('Solo uses one instance; co-op uses two to four')
    run(args)


if __name__ == '__main__':
    main()
