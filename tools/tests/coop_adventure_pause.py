#!/usr/bin/env python3
"""Exercise unanimous native pause on caller-owned, hidden lab games.

The pair must already be in an ordinary playing adventure (state 3, after the
shared Intro). Uses real Esc/Resume and W/D input, read-only native poses/clocks,
and existing diagnostics. No native pause counters, goals or profiles are edited.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import time

from adventure_memory import AdventureMemory
from coop_adventure import FixtureError, Game, verify_lab
from coop_adventure_preparation import menu


def check(value, text):
    if not value:
        raise FixtureError(text)


def pause_state(game):
    text = game.adventure()['text']
    match = re.search(r'party pause: run=(\d+) local=(\d+) paused=(\d+)/(\d+) held=(\d+) revision=(\d+)', text)
    check(match, 'unanimous pause diagnostics are unavailable: ' + text)
    return dict(zip(('run', 'local', 'count', 'members', 'held', 'revision'), map(int, match.groups())))


class Reader:
    def __init__(self, game, record):
        self.game = game
        self.memory = AdventureMemory(game, record)
        info = game.call('info')
        check(f"pid={record['pid']}\n" in info and
              'exe sha256=dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37' in info,
              'native console identity differs from the recorded isolated process')
        self.info = info
        self.player = game.player_id()
        self.avatar = int(re.search(r'local=([0-9A-Fa-f]+)', game.call('coop.avatar')).group(1), 16)
        self.incarnation = self.memory.pointer(self.avatar + 0x24)
        response = game.call('call Simulator::cGameTimeManager::Get cdecl')
        self.time = int(re.search(r'eax=0x([0-9A-Fa-f]+)', response).group(1), 16)
        self.npcs = []
        marker = 0
        for _ in range(12):
            rows = []
            for line in game.call(f'coop.adventure.world actors {marker} 48').splitlines():
                match = re.search(r'marker=(\d+) incarnation=(\d+) noun=(\d+) object=(0x[0-9A-Fa-f]+)', line)
                if match:
                    m, incarnation, noun, obj = match.groups()
                    row = dict(marker=int(m), incarnation=int(incarnation), noun=int(noun), object=int(obj, 16))
                    rows.append(row)
                    if row['noun'] == 0x018EB45E and row['object'] != self.avatar:
                        self.npcs.append(row)
            if len(rows) < 48 or len(self.npcs) >= 12:
                break
            marker = max(row['marker'] for row in rows) + 1

    def position(self, obj):
        return list(struct.unpack('<fff', self.memory.read(obj + 0xC4, 12)))

    def sample(self):
        check(self.memory.pointer(self.avatar + 0x24) == self.incarnation, 'local captain changed during pause measurement')
        npcs = {}
        for row in self.npcs:
            if self.memory.pointer(row['object'] + 0x24) == row['incarnation']:
                npcs[str(row['marker']) + ':' + str(row['incarnation'])] = self.position(row['object'])
        # Frame-boundary diagnostic reads the native game::position. A raw
        # asynchronous read can catch physics before its after-Update correction.
        pose = re.search(r'avatar=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', self.game.call('coop.cam'))
        check(pose, 'native frame-boundary captain position is unavailable')
        status = self.game.call('coop.status')
        puppets = {match[1]: list(map(float, match.groups()[1:])) for match in re.finditer(
            r"remote (\d+) [^\n]+\n\s+puppet pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)", status)}
        return dict(goals=self.memory.goals(), position=list(map(float, pose.groups())),
                    raw_position=self.position(self.avatar), puppets=puppets,
                    health=struct.unpack('<f', self.memory.read(self.avatar + 0x5A8 + 0x38, 4))[0],
                    time_flags=self.memory.pointer(self.time + 0x48), npcs=npcs)


def sample_for(readers, seconds):
    samples = []
    started = time.monotonic()
    while time.monotonic() - started < seconds:
        samples.append(dict(wall=time.monotonic() - started, players=[r.sample() for r in readers]))
        time.sleep(.15)
    return samples


def displacement(samples, player, origin):
    return max(math.dist(row['players'][player]['position'], origin) for row in samples)


def npc_displacement(samples, initial, player=0):
    return max((math.dist(position, initial[key]) for row in samples for key, position in row['players'][player]['npcs'].items()
                if key in initial), default=0.)


def open_menu(game):
    if not game.caption('Resume Game'):
        game.call('key esc')
    game.wait('native pause menu', lambda: game.caption('Resume Game'), timeout=8)
    game.wait('local pause intent', lambda: pause_state(game)['local'] == 1, timeout=8)


def close_menu(game):
    if game.caption('Resume Game'):
        # Esc closes the real menu without the harness's six-frame synthetic
        # mouse hold continuing over the resumed world behind its button.
        game.call('key esc')
    game.wait('native menu closes', lambda: not game.caption('Resume Game'), timeout=8)
    game.wait('local Resume intent', lambda: pause_state(game)['local'] == 0, timeout=8)


def wait_shared(games, value):
    games[0].wait('same authoritative pause on both clients',
                  lambda: all(pause_state(game)['held'] == value for game in games), timeout=8)


def partial(games, readers, paused_index, require_npc, observation):
    open_menu(games[paused_index]); wait_shared(games, 0)
    time.sleep(.4)
    before = [reader.sample() for reader in readers]
    observation.update(paused_player=paused_index, before=before)
    check(all(sample['health'] > 1 for sample in before),
          'pause movement fixture requires living captains above the downed health floor')
    check(all(not sample['time_flags'] & 1 for sample in before), 'one menu still paused a native world clock')
    mover = games[1 - paused_index]
    samples = []
    for key in ('D', 'W'):
        mover.call(f'key {key} down')
        try:
            samples += sample_for(readers, 2)
        finally:
            mover.call(f'key {key} up', required=False)
    after = [reader.sample() for reader in readers]
    moved = displacement(samples, 1 - paused_index, before[1 - paused_index]['position'])
    stopped = displacement(samples, paused_index, before[paused_index]['position'])
    npc_moved = npc_displacement(samples, before[0]['npcs'])
    observation.update(after=after, samples=samples, moving_captain_max_distance=moved,
                       menu_captain_max_drift=stopped, host_npc_max_distance=npc_moved)
    check(all(sample['health'] > 1 for sample in after),
          'live combat downed a captain during the partial-pause fixture; use safe ground')
    check(moved > .2, 'unpaused captain did not physically move; establish an open-ground input baseline')
    check(stopped < .04, 'the captain in its local menu retained movement or an old order')
    check(after[0]['goals']['elapsed'] - before[0]['goals']['elapsed'] > 500,
          'one player opening Esc stopped host adventure simulation')
    # The host-menu case directly exercises the old global simulation stop.
    # Native Town actors may choose to idle during the inverse case; keep that
    # observation, while its host clock and captain still must advance.
    if require_npc and paused_index == 0:
        check(npc_moved > .1, 'host NPC poses did not advance during the one-player menu')
    for game in games:
        game.record('host-menu-only' if paused_index == 0 else 'guest-menu-only')
    close_menu(games[paused_index]); wait_shared(games, 0)
    return observation


def run(games, records, args):
    result = dict(result='FAIL', profiles=records, checks=[], native_pause_counter_writes=False)
    readers = []
    try:
        readers = [Reader(game, records[game.instance]) for game in games]
        installation = Path(json.loads(args.metadata.read_text(encoding='utf-8'))['installation'])
        result['identity'] = []
        for reader in readers:
            mods = reader.game.call('mods')
            generation = int(re.search(r'coop\s+loaded\s+gen=(\d+)', mods).group(1))
            shadow = installation / f'SporebinEP1/mods/.shadow/coop.i{reader.game.instance}.{generation}.dll'
            digest = hashlib.sha256(shadow.read_bytes()).hexdigest()
            result['identity'].append(dict(info=reader.info, mods=mods, npcs=reader.npcs,
                shadow=str(shadow), shadow_sha256=digest, avatar=reader.avatar,
                incarnation=reader.incarnation, player=reader.player, time_manager=reader.time))
            if args.expected_sha256:
                check(digest == args.expected_sha256.lower(), 'loaded shadow differs from the supplied candidate identity')
        check(all(r.memory.goals()['state'] == 3 for r in readers), 'pause test requires an active shared playing adventure')
        for game in games:
            check(not game.caption('Resume Game'), 'start this test with both native menus closed')
            game.call('coop.uistate 0')
        result['partial_host'] = {}
        partial(games, readers, 0, args.require_npc_motion, result['partial_host'])
        result['partial_guest'] = {}
        partial(games, readers, 1, args.require_npc_motion, result['partial_guest'])
        open_menu(games[0]); open_menu(games[1]); wait_shared(games, 1)
        time.sleep(.6)
        before = [reader.sample() for reader in readers]
        check(all(sample['time_flags'] & 1 for sample in before), 'unanimous pause did not enter both native paused clocks')
        for game in games:
            game.call('key W down')
        try:
            samples = sample_for(readers, 2.5)
        finally:
            for game in games:
                game.call('key W up', required=False)
        after = [reader.sample() for reader in readers]
        for index in range(2):
            check(all(row['players'][index]['goals'] == before[index]['goals'] for row in samples),
                  'unanimous pause advanced native adventure clocks or goals')
            check(displacement(samples, index, before[index]['position']) < .04,
                  'unanimous pause admitted captain motion')
        host_npc_drift = npc_displacement(samples, before[0]['npcs'])
        guest_npc_drift = npc_displacement(samples, before[1]['npcs'], 1)
        result['unanimous'] = dict(before=before, after=after, samples=samples,
                                   statuses=[pause_state(game) for game in games],
                                   host_npc_max_drift=host_npc_drift, guest_npc_max_drift=guest_npc_drift,
                                   settled_before_measurement_seconds=.6)
        check(host_npc_drift < .04, 'unanimous pause admitted host NPC motion')
        check(guest_npc_drift < .1, 'guest NPC poses kept moving after settled authoritative pause snapshots')
        for game in games:
            game.record('unanimous-native-pause')
        close_menu(games[0]); wait_shared(games, 0)
        resumed = sample_for(readers, .6)
        result['one_resume'] = dict(samples=resumed, statuses=[pause_state(game) for game in games])
        # Compare puppet steps against actual owner travel. Ground settling,
        # root motion or legitimate still-held input can move an owner after
        # Resume; that is not buffered interpolation replay.
        previous = after
        steps = []
        for row in resumed:
            current = row['players']
            for viewer, owner in ((0, 1), (1, 0)):
                key = str(readers[owner].player)
                old = previous[viewer]['puppets'].get(key)
                new = current[viewer]['puppets'].get(key)
                check(old is not None and new is not None, 'remote captain disappeared across pause/Resume')
                owner_step = math.dist(previous[owner]['position'], current[owner]['position'])
                puppet_step = math.dist(old, new)
                old_error = math.dist(old, previous[owner]['position'])
                steps.append(dict(viewer=viewer, owner=owner, owner_step=owner_step,
                                  puppet_step=puppet_step, previous_error=old_error))
                check(puppet_step <= owner_step + old_error + 1.,
                      'a resumed puppet replayed spatial motion beyond its owner travel and prior correction error')
            previous = current
        result['one_resume']['movement_steps'] = steps
        check(readers[0].memory.goals()['elapsed'] > after[0]['goals']['elapsed'], 'one Resume did not restart host simulation')
        check(all(not r.sample()['time_flags'] & 1 for r in readers), 'one Resume left a native world paused')
        close_menu(games[1]); wait_shared(games, 0)
        check(all(game.call('exc').strip() == 'total faults: 0' for game in games), 'native pause test recorded an exception')
        result.update(result='PASS', checks=['host-only menu leaves guest and host simulation active',
                      'guest-only menu leaves host active', 'local paused captain input and residual motion stop',
                      'all-player native clocks and movement freeze', 'one Resume restarts all worlds without buffered spatial catchup'])
    except (FixtureError, OSError, ValueError) as error:
        result['error'] = str(error)
        for game in games:
            try:
                game.record('pause-failure')
            except (FixtureError, OSError, ValueError):
                pass
    finally:
        for game in games:
            game.call('key W up', required=False); game.call('key D up', required=False)
            if game.caption('Resume Game'):
                game.call('key esc')
        if not args.keep_running:
            try:
                games[0].call('coop.adventure cancel')
                for game in games:
                    menu(game, records[game.instance])
                result['connected_menus'] = [game.adventure() for game in games]
            except (FixtureError, OSError, ValueError) as error:
                result['cleanup_error'] = str(error); result['result'] = 'FAIL'
        for reader in readers:
            reader.memory.close()
        for game in games:
            game.save_log()
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--instances', default='81,82')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-npc-motion', action='store_true')
    parser.add_argument('--keep-running', action='store_true')
    parser.add_argument('--expected-sha256')
    args = parser.parse_args()
    instances = tuple(map(int, args.instances.split(',')))
    if len(instances) != 2 or len(set(instances)) != 2:
        parser.error('supply exactly two caller-owned lab instance IDs')
    args.output.mkdir(parents=True, exist_ok=True)
    records = verify_lab(args.metadata, instances)
    result = run([Game(i, args.output) for i in instances], records, args)
    print(json.dumps({key: result[key] for key in ('result', 'error', 'cleanup_error') if key in result}))
    return 0 if result['result'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
