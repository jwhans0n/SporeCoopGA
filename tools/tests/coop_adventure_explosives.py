#!/usr/bin/env python3
"""Exercise actual native explosives in recorded hidden, muted Windows lab games.

Fixtures copy a stock adventure and use installed native palettes. Damage, fuses,
throws and effects execute through the mod's outside-frame native debug adapters.
No objective completion, damage result or detonation result is injected.

--crate-objective is a compatibility diagnostic, not a valid gameplay test.
The installed native validator rejects stock powerup crates as Kill targets with
reason 0x0A. This option records that expected rejection and exits through the
failure diagnostic path; it does not verify crate objective counts.
"""
import argparse
from contextlib import ExitStack
import json
import math
from pathlib import Path
import re
import struct
import threading
import time

from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_combat import hp, expect_health
from coop_adventure_lobby import lobby, click_named, playing
from coop_adventure_gates import inventory as party_inventory
from coop_adventure_preparation import menu
from adventure_memory import AdventureMemory
from adventure_network_proxy import NetworkProxy
from adventure_interaction_fixture import (
    FIXTURES, Native, fixture_key, fixture_defaults, register_fixtures, tune_fixture,
    CRATE_GOAL_KEY, register_crate_goal_fixture, inspect_crate_goal_fixture,
)

KINDS = ('mine', 'barrel', 'crate', 'bomb_long', 'bomb', 'bomb_short', 'grenade', 'chain', 'recovery')
MARKER = 108
HOME = 158


class GoalMonitor:
    """Read-only host validation trace across the native Continue transition."""
    def __init__(self, memory, output, run):
        self.memory, self.output, self.run = memory, Path(output), run
        self.play = memory.pointer(memory.pointer(memory.singleton) + 0x78)
        self.goal = memory.pointer(self.play + 0x64)
        self.stop = threading.Event()
        self.states = []
        self.thread = threading.Thread(target=self._sample, daemon=True)

    def _sample(self):
        start, previous = time.monotonic(), None
        while not self.stop.is_set():
            try:
                if self.memory.pointer(self.play + 0x64) != self.goal:
                    self.states.append(dict(seconds=time.monotonic()-start, goal_vector_released=True))
                    break
                raw = self.memory.read(self.goal, 8)
                values = (self.memory.pointer(self.play + 0x90), bool(raw[0]),
                          struct.unpack_from('<i', raw, 4)[0], self.memory.pointer(self.goal + 0x1A8),
                          self.memory.pointer(self.play + 0xB0))
                if values != previous:
                    self.states.append(dict(seconds=time.monotonic()-start, native_state=values[0],
                                            complete=values[1], count=values[2], goal_failure=values[3], play_b0=values[4]))
                    previous = values
                if len(self.states) >= 2048:
                    break
            except (OSError, ValueError) as error:
                self.states.append(dict(seconds=time.monotonic()-start, read_error=str(error)))
                break
            self.stop.wait(.001)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *ignored):
        self.stop.set(); self.thread.join(timeout=2)
        path = self.output / f'crate-native-validation-run{self.run}.json'
        path.write_text(json.dumps(dict(run=self.run, play=self.play, goal=self.goal,
                                       states=self.states), indent=2)+'\n')


def playing_crate(games, memories):
    """Capture the expected native stock-crate Kill rejection without waiting 120s."""
    def state(memory):
        value = memory.goals()['state']
        if value == 6:
            time.sleep(.03)  # Preserve the host's final validation trace.
            raise FixtureError('native crate goal ended during Continue validation')
        return value
    for game, memory in zip(games, memories):
        if state(memory) != 3:
            game.click_control(0x0445C99D)
    for _ in range(3):
        for game, memory in zip(games, memories):
            if state(memory) != 3 and game.control(0x0445C99D):
                game.click_control(0x0445C99D)
    games[0].wait('all authored crate native intro confirmations', lambda: all(state(m) == 3 for m in memories))


def physical(game, marker, kind):
    """Return the physical object, never the distinct same-marker respawner9."""
    text = game.call(f'coop.adventure.world actorsat {marker} 48')
    for line in text.splitlines():
        if not re.search(rf'\bpowerup={kind}\b', line):
            continue
        match = re.search(r'incarnation=(\d+).*?object=(0x[0-9A-Fa-f]+)', line)
        if match:
            host = re.search(r'host_incarnation=(\d+)', line)
            return dict(incarnation=int(match[1]), host_incarnation=int(host[1]) if host else int(match[1]),
                        object=int(match[2], 16), line=line)
    return None


def counters(game):
    text = game.adventure()['text']
    match = re.search(r'explosives ready activated=(\d+) detonated=(\d+) thrown=(\d+) rejected=(\d+) queued=(\d+)', text)
    if not match:
        raise FixtureError('native explosive module diagnostics unavailable: ' + text)
    return dict(zip(('activated', 'detonated', 'thrown', 'rejected', 'queued'), map(int, match.groups())))


def snapshot(games, memories):
    health, positions = [], []
    for game in games:
        text = game.call('coop.avatar')
        hitpoints = re.search(r'hp=([\d.]+)/([\d.]+)', text)
        position = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', text)
        if not hitpoints or not position:
            raise FixtureError('native captain health/position is unavailable: ' + text)
        health.append(tuple(map(float, hitpoints.groups())))
        positions.append(tuple(map(float, position.groups())))
    return dict(health=health, captain_positions=positions, counters=[counters(g) for g in games],
                clocks=[m.goals() for m in memories])


def clean_exceptions(games):
    for game in games:
        if 'total faults: 0' not in game.call('exc'):
            raise FixtureError(f'{game.instance}: native explosive interaction fault')


def expect_damage(games, before, amount):
    for game, health in zip(games, before):
        expect_health(game, health[0] - amount, timeout=5)


def park(games, marker=HOME):
    for game in games:
        game.call(f'coop.adventure.world near {marker} {game.player_id()} 4 4')
    time.sleep(.4)


def stage(games, marker=MARKER):
    for index, game in enumerate(games):
        # Setup during the shared intro cannot trigger a mine early.
        # Native respawners defer physical creation while a captain overlaps the
        # placement. Stand outside its body while staying inside the blast.
        game.call(f'coop.adventure.world near {marker} {game.player_id()} {8 + index * .25} 0')


def settle(games):
    """Wait until the host's actual damage recipients reach their staged owners."""
    players = [(game, game.player_id()) for game in games[1:]]
    def arrived():
        positions = {}
        for game, player in players:
            match = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', game.call('coop.avatar'))
            if not match:
                return False
            positions[player] = tuple(map(float, match.groups()))
        status = games[0].call('coop.status')
        for player, position in positions.items():
            match = re.search(rf'remote {player} .*?\n\s+puppet pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', status)
            if not match or math.dist(position, tuple(map(float, match.groups()))) > 2.5:
                return False
        return True
    games[0].wait('host captain replicas reached staged owner positions', arrived, timeout=12)


def start(games, records, memories, kind, friendly_fire, amount=10., radius=10., crate_objective=False):
    host = games[0]
    if host.adventure()['active']:
        host.call('coop.adventure cancel')
    for game in games:
        menu(game, records[game.instance])
        register_fixtures(game, records[game.instance])
        if crate_objective:
            register_crate_goal_fixture(game, records[game.instance])
            inspect_crate_goal_fixture(game, records[game.instance])
    host.call(f'coop.adventure friendlyfire {friendly_fire}')
    host.call('coop.adventure death revive')
    key = CRATE_GOAL_KEY if crate_objective else fixture_key(kind)
    host.call('coop.adventure start ' + ' '.join(f'0x{value:08X}' for value in key))
    for game in games:
        state = lobby(game, len(games))
        if re.search(r'^captain .*ready=1', state, re.M):
            raise FixtureError('fixture launch reused an implicit Ready flag')
    for game in games[1:]:
        click_named(game, 'prep_ready')
    click_named(host, 'prep_ready')
    host.wait('fixture committed with shared intro', lambda: all(
        'Adventure in progress' in g.adventure()['text'] for g in games))
    for game in games:
        game.wait('fixture beam-down Continue', lambda game=game: game.control(0x0445C99D))
    tuning = []
    for game in games:
        tuning.append(tune_fixture(game, records[game.instance], kind, MARKER,
                                   radius=radius, amount=amount,
                                   health=50. if game is host else None, invulnerable=False))
    stage(games)
    held = snapshot(games, memories)
    if any(m.goals()['state'] == 3 for m in memories):
        raise FixtureError('staging escaped the shared intro hold')
    if crate_objective:
        run = host.adventure()['run']
        initial = dict(run=run, native_goals=memories[0].goals(),
                       actors={marker: host.call(f'coop.adventure.world actorsat {marker} 48')
                               for marker in (108, 110, 112, 116, 117, 118)}, tuning=tuning[0])
        (host.output / f'crate-initial-actors-run{run}.json').write_text(json.dumps(initial, indent=2)+'\n')
        with GoalMonitor(memories[0], games[0].output, run):
            playing_crate(games, memories)
    else:
        playing(games, records)
    for game in games:
        game.wait('native physical fixture spawn after Continue',
                  lambda game=game: physical(game, MARKER, FIXTURES[kind][2]), timeout=20)
    settle(games)
    return dict(kind=kind, friendly_fire=friendly_fire, run=host.adventure()['run'],
                tuning=tuning, before=held, palette=fixture_defaults(kind))


def retired(games, marker, kind, before):
    games[0].wait('one authoritative detonation on every peer', lambda: all(
        counters(g)['detonated'] == c['detonated'] + 1 for g, c in zip(games, before)), timeout=25)
    games[0].wait('physical explosive retired on every peer', lambda: all(
        not physical(g, marker, kind) for g in games), timeout=5)


def same_count(games, expected, duration=1.):
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        if [counters(g)['detonated'] for g in games] != expected:
            raise FixtureError('a duplicate/independent detonation changed the event count')
        time.sleep(.2)


def mine_case(games, records, memories, friendly_fire):
    result = start(games, records, memories, 'mine', friendly_fire, radius=20.)
    before = result['before']
    games[1].call(f'coop.adventure.world near {MARKER} {games[1].player_id()} 0 0')
    retired(games, MARKER, 6, before['counters'])
    expect_damage(games, before['health'], 10.)
    same_count(games, [c['detonated'] + 1 for c in before['counters']])
    result['native_contact'] = snapshot(games, memories)
    park(games)
    # A second physical mine remains available. An off-host request made far
    # from it must be rejected without any client-owned damage or consumption.
    guest = games[1]
    before = snapshot(games, memories)
    guest.call(f'coop.adventure.world powerup 110 {guest.player_id()}')
    time.sleep(1)
    if any(counters(g)['detonated'] != c['detonated'] for g, c in zip(games, before['counters'])):
        raise FixtureError('a distant guest mine claim detonated an object')
    if not all(physical(g, 110, 6) for g in games):
        raise FixtureError('a distant guest mine claim consumed the physical mine')
    expect_damage(games, before['health'], 0.)
    result['distant_claim'] = snapshot(games, memories)
    # The consumed mine must return as a new incarnation, clear its old spent
    # state and be usable by the host. Observers remain outside its radius.
    respawn = games[0].wait('authored mine respawn after the spent incarnation retired',
                            lambda: physical(games[0], MARKER, 6), timeout=25)
    result['respawn_actor'] = respawn
    for game in games:
        tune_fixture(game, records[game.instance], 'mine', MARKER, radius=10., amount=10., health=50.)
    before = snapshot(games, memories)
    games[0].call(f'coop.adventure.world near {MARKER} 0 0 0')
    retired(games, MARKER, 6, before['counters'])
    expect_health(games[0], before['health'][0][0] - 10.)
    for game, health in zip(games[1:], before['health'][1:]):
        expect_health(game, health[0])
    result['host_respawn_contact'] = snapshot(games, memories)
    park(games)
    return result


def shooting_case(games, records, memories, kind, friendly_fire, check_goal=False):
    # check_goal retains the historical authored-Kill diagnostic. Stock powerup
    # crates are rejected with native reason 0x0A during Continue; the old count
    # assertions below are unreachable for that rejected fixture, not acceptance.
    amount = 0. if kind == 'crate' else 10.
    result = start(games, records, memories, kind, friendly_fire, amount=amount, radius=20.,
                   crate_objective=check_goal)
    if check_goal:
        initial = [memory.goals() for memory in memories]
        if any(state['act'] != 0 or len(state['goals']) != 1 or
               (state['goals'][0]['type'], state['goals'][0]['primary'], state['goals'][0]['required'],
                state['goals'][0]['count'], state['goals'][0]['complete']) != (3, 0, 3, 0, False)
               for state in initial):
            raise FixtureError('immutable crate Kill fixture did not initialize its authored native goal')
        result['objective_fixture'] = dict(key=list(CRATE_GOAL_KEY), initialized=initial)
    attempts = []
    for origin, marker in ((games[0], MARKER), (games[1], 110)):
        if marker != MARKER:
            for game in games:
                tune_fixture(game, records[game.instance], kind, marker, radius=20., amount=amount,
                             health=50. if game is games[0] else None)
            stage(games, marker)
            settle(games)
        before = snapshot(games, memories)
        origin.call(f'coop.adventure.world actorhit {marker} {origin.player_id()} 10000 2')
        retired(games, marker, 15, before['counters'])
        expect_damage(games, before['health'], amount)
        same_count(games, [c['detonated'] + 1 for c in before['counters']])
        if check_goal:
            expected = len(attempts) + 1
            games[0].wait('legacy crate-count branch (stock powerups are rejected before this point)', lambda: all(
                memory.goals()['goals'][0]['count'] == expected and
                not memory.goals()['goals'][0]['complete'] for memory in memories), timeout=8)
        attempts.append(dict(origin=origin.instance, marker=marker, before=before, after=snapshot(games, memories)))
        checkpoint = dict(kind=kind, friendly_fire=friendly_fire, completed_attempts=attempts)
        (games[0].output / f'{kind}-ff{friendly_fire}-shooting-checkpoint.json').write_text(json.dumps(checkpoint, indent=2) + '\n')
        park(games)
    result['shooting'] = attempts
    return result


def chain_case(games, records, memories, friendly_fire):
    result = start(games, records, memories, 'barrel', friendly_fire, radius=100.)
    for game in games:
        for marker in (112, 116, 117, 118):
            tune_fixture(game, records[game.instance], 'barrel', marker,
                         radius=100., amount=10., health=100000. if game is games[0] else None, invulnerable=True)
        tune_fixture(game, records[game.instance], 'barrel', 110,
                     radius=100., amount=10., health=5. if game is games[0] else None, invulnerable=False)
        tune_fixture(game, records[game.instance], 'barrel', MARKER,
                     radius=100., amount=10., health=50. if game is games[0] else None, invulnerable=False)
    before = snapshot(games, memories)
    origin = games[1]
    origin.call(f'coop.adventure.world actorhit {MARKER} {origin.player_id()} 10000 2')
    observations = []
    deadline = time.monotonic() + 20
    started = time.monotonic()
    while time.monotonic() < deadline:
        current = counters(games[0])
        observations.append(dict(wall_seconds=time.monotonic() - started, counters=current))
        if current['detonated'] == before['counters'][0]['detonated'] + 2:
            break
        time.sleep(.08)
    else:
        raise FixtureError('native blast did not cause its nearby barrel chain')
    games[0].wait('same two native chain events on all clients', lambda: all(
        counters(g)['detonated'] == c['detonated'] + 2 for g, c in zip(games, before['counters'])), timeout=8)
    expect_damage(games, before['health'], 20.)
    same_count(games, [c['detonated'] + 2 for c in before['counters']], duration=2.)
    for game in games:
        if physical(game, MARKER, 15) or physical(game, 110, 15):
            raise FixtureError('native chain retained a spent barrel')
        if not all(physical(game, marker, 15) for marker in (112, 116, 117, 118)):
            raise FixtureError('chain affected an invulnerable neighboring barrel')
    result.update(kind='chain', before_chain=before, native_observations=observations,
                  after=snapshot(games, memories))
    park(games)
    return result


def pick(games, records, memories, owner, marker, kind):
    local = physical(owner, marker, kind)
    if not local:
        raise FixtureError('native explosive disappeared before pickup')
    spatials = []
    for game in games:
        actor = physical(game, marker, kind)
        if not actor:
            raise FixtureError('native explosive has no matching peer binding before pickup')
        with Native(game, records[game.instance]) as native:
            powerup = native.virtual(actor['object'], 0x0C, 0x08367427)
            spatials.append(native.virtual(powerup, 0x08))
    started, sim_start = time.monotonic(), memories[0].goals()['elapsed']
    player = owner.player_id()
    owner.call(f'coop.adventure.world pickup {marker} {player}')
    owner.wait('same physical explosive held by its collector on every peer', lambda: all(
        spatial in party_inventory(game, memory, player)
        for game, memory, spatial in zip(games, memories, spatials)))
    return dict(actor=local, started_wall=started, started_simulation_ms=sim_start,
                spatial_by_peer=spatials)


def timer_case(games, records, memories, kind, friendly_fire, repick=False):
    result = start(games, records, memories, kind, friendly_fire)
    duration = result['palette'].get('duration')
    if duration is None:
        raise FixtureError('installed bomb palette has no verified native duration')
    owner = games[1]
    before = snapshot(games, memories)
    picked = pick(games, records, memories, owner, MARKER, 10)
    # Native timing, including the drop, must remain owned by the original fuse.
    owner.call(f'coop.adventure.world throw {MARKER} {owner.player_id()} 0 0 0')
    host = games[0]
    if repick:
        host.wait('first native timed-bomb throw accepted', lambda:
                  counters(host)['thrown'] == before['counters'][0]['thrown'] + 1)
        result['repick'] = pick(games, records, memories, host, MARKER, 10)
        host.call(f'coop.adventure.world throw {MARKER} {host.player_id()} 0 0 0')
        host.wait('same bomb thrown again only after native repick', lambda:
                  counters(host)['thrown'] == before['counters'][0]['thrown'] + 2)
    host.wait('timed bomb actual explosion', lambda: counters(host)['detonated'] > before['counters'][0]['detonated'],
              timeout=duration + 8)
    wall = time.monotonic() - picked['started_wall']
    sim_ms = memories[0].goals()['elapsed'] - picked['started_simulation_ms']
    retired(games, MARKER, 10, before['counters'])
    if not duration - .6 <= sim_ms / 1000 <= duration + 1.8:
        raise FixtureError(f'{kind}: native fuse elapsed {sim_ms / 1000:.2f}s differs from authored {duration}s')
    expect_damage(games, before['health'], 10.)
    same_count(games, [c['detonated'] + 1 for c in before['counters']])
    result.update(owner=owner.instance, picked=picked, authored_duration=duration, wall_seconds=wall,
                  simulation_seconds=sim_ms / 1000, after=snapshot(games, memories))
    park(games)
    return result


def grenade_case(games, records, memories, friendly_fire):
    result = start(games, records, memories, 'grenade', friendly_fire, radius=20.)
    owner = games[1] if friendly_fire else games[0]
    # Native grace excludes only its thrower. Other captains beside the held
    # grenade are valid contacts and must stand away during the self probe.
    for game in games:
        if game is not owner:
            game.call(f'coop.adventure.world near {HOME} {game.player_id()} 4 4')
    settle(games)
    before = snapshot(games, memories)
    local = pick(games, records, memories, owner, MARKER, 13)
    with Native(games[0], records[games[0].instance]) as native:
        host_actor = physical(games[0], MARKER, 13)
        powerup = native.virtual(host_actor['object'], 0x0C, 0x08367427)
    # The native physics argument is an impulse, not a velocity. A stationary
    # drop keeps this contact/damage check inside the authored20m radius.
    velocity = [0., 0., 0.]
    owner.call(f'coop.adventure.world throw {MARKER} {owner.player_id()} ' + ' '.join(f'{v:.6f}' for v in velocity))
    games[0].wait('guest throw accepted by native host', lambda: counters(games[0])['thrown'] == before['counters'][0]['thrown'] + 1)
    age, self_probe = None, 'native contact completed before probe'
    current = physical(games[0], MARKER, 13)
    if current and current['host_incarnation'] == local['actor']['host_incarnation']:
        age = struct.unpack('<f', memories[0].read(powerup + 0x34, 4))[0]
        if age < 1:
            games[0].call(f'coop.adventure.world powerup {MARKER} {owner.player_id()}')
            self_probe = ('ignored during native grace' if counters(games[0])['activated'] == before['counters'][0]['activated']
                          else 'native nonthrower contact overlapped asynchronous probe')
    def contact_or_expiry():
        if counters(games[0])['detonated'] == before['counters'][0]['detonated'] + 1:
            return 'native_nonthrower_contact'
        if struct.unpack('<f', memories[0].read(powerup + 0x34, 4))[0] >= 1.05:
            return 'thrower_contact_after_grace'
        return False
    contact_mode = games[0].wait('native nonthrower contact or thrower grace expiry', contact_or_expiry, timeout=4)
    if contact_mode == 'thrower_contact_after_grace':
        games[0].call(f'coop.adventure.world powerup {MARKER} {owner.player_id()}')
    retired(games, MARKER, 13, before['counters'])
    for game, health in zip(games, before['health']):
        expect_health(game, health[0] - (10. if game is owner else 0.), timeout=5)
    same_count(games, [c['detonated'] + 1 for c in before['counters']])
    result.update(owner=owner.instance, picked=local, grace_probe_age=age,
                  self_probe=self_probe, contact_mode=contact_mode, after=snapshot(games, memories))
    park(games)
    return result


def replay(games, records, memories):
    host = games[0]
    previous = host.adventure()['run']
    captains = [g.captain_key() for g in games]
    host.call('coop.adventure.world finish fail')
    for game in games:
        def results(game=game):
            if game.caption('Replay'):
                return True
            if game.control(0x0445C99D):
                game.click_control(0x0445C99D)
            return False
        game.wait('native results after an armed-bomb attempt', results)
    for game in games[1:] + games[:1]:
        choice = 'Done' if game is host else 'Replay'
        def accepted(game=game, choice=choice):
            if not game.caption('Replay'):
                return True
            game.press(choice)
            return False
        game.wait('native saved result accepted', accepted, timeout=25)
    for game in games:
        state = lobby(game, len(games))
        if re.search(r'^captain .*ready=1', state, re.M) or game.adventure()['run'] <= previous:
            raise FixtureError('armed-bomb Replay retained an old run/readiness state')
    for game in games[1:]:
        click_named(game, 'prep_ready')
    click_named(host, 'prep_ready')
    playing(games, records)
    if [g.captain_key() for g in games] != captains:
        raise FixtureError('armed-bomb Replay changed a saved captain')
    same_count(games, [0] * len(games), duration=2.)
    return dict(previous_run=previous, new_run=host.adventure()['run'], saved_captains=captains,
                after=snapshot(games, memories))


def recovery_case(games, records, memories, friendly_fire):
    result = start(games, records, memories, 'bomb_long', friendly_fire)
    host, guest = games[:2]
    picked = pick(games, records, memories, host, MARKER, 10)
    # This invokes the normal native objective-conversation hook, which opens
    # the real camera/dialog on all participants and holds shared simulation.
    guest.call(f'coop.adventure.world near {HOME} {guest.player_id()} 1 0')
    settle(games)
    guest.call(f'coop.adventure.world talk 0 {HOME} {guest.player_id()}')
    for game in games:
        game.wait('real shared native conversation holds the fuse', lambda game=game:
                  game.caption('letterbox_top') and 'Conversation' in game.adventure()['text'])
    frozen = snapshot(games, memories)
    duration = result['palette']['duration'] + 1.
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        if not all(physical(game, MARKER, 10) for game in games):
            raise FixtureError('native timed bomb expired during a shared conversation hold')
        if any(counters(g)['detonated'] != c['detonated'] for g, c in zip(games, frozen['counters'])):
            raise FixtureError('a delayed explosion executed during a shared conversation hold')
        time.sleep(.4)
    held = snapshot(games, memories)
    if [c['elapsed'] for c in held['clocks']] != [c['elapsed'] for c in frozen['clocks']]:
        raise FixtureError('shared conversation advanced the adventure clock')
    expect_damage(games, frozen['health'], 0.)
    result.update(kind='recovery', picked=picked, frozen=frozen, held=held, held_wall_seconds=duration)
    old_run = host.adventure()['run']
    host.call('coop.adventure cancel')
    for game in games:
        menu(game, records[game.instance])
        if game.adventure()['players'] != len(games):
            raise FixtureError('cancelling an armed conversation disconnected the party')
    clean_exceptions(games)
    # The same fixture must launch immediately. Cleanup flush executes the
    # old native callbacks, so this also exercises their cancelled-run guards.
    retry = start(games, records, memories, 'bomb_long', friendly_fire)
    if retry['run'] <= old_run:
        raise FixtureError('cancel/retry did not allocate a new adventure run')
    same_count(games, [0] * len(games), duration=2.)
    for game in games:
        if hp(game)[0] != hp(game)[1]:
            raise FixtureError('a cancelled fuse damaged a captain in the new run')
    result['retry'] = dict(old_run=old_run, new_run=retry['run'], after=snapshot(games, memories))
    pick(games, records, memories, guest, MARKER, 10)
    result['armed_replay'] = replay(games, records, memories)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--instances', default='21,22')
    parser.add_argument('--kinds', default=','.join(KINDS))
    parser.add_argument('--friendly-fire', default='0,100')
    parser.add_argument('--port', type=int, default=47830)
    parser.add_argument('--crate-objective', action='store_true',
                        help='diagnose expected native 0x0A rejection of stock-crate Kill goals; not a gameplay test')
    parser.add_argument('--repick', action='store_true', help='repick and throw the same timed bomb without restarting its fuse')
    parser.add_argument('--loss', type=float, default=0., help='per-direction packet loss fraction, e.g. 0.03')
    parser.add_argument('--jitter', type=float, default=0., help='random delay in seconds from 0 to this value; permits reordering')
    parser.add_argument('--seed', type=int, default=7, help='deterministic loss/jitter seed (offset per guest)')
    args = parser.parse_args()
    instances = tuple(map(int, args.instances.split(',')))
    kinds = tuple(args.kinds.split(','))
    friendly = tuple(map(int, args.friendly_fire.split(',')))
    if (any(k not in KINDS for k in kinds) or any(f not in (0, 100) for f in friendly) or
            not 2 <= len(instances) <= 4 or len(set(instances)) != len(instances)):
        parser.error('use supported explosive kinds, friendly-fire0/100, and two to four distinct recorded games')
    if not math.isfinite(args.loss) or not 0 <= args.loss < 1 or not math.isfinite(args.jitter) or not 0 <= args.jitter <= 5:
        parser.error('--loss must be a fraction in [0,1), and --jitter seconds in [0,5]')
    if not 1 <= args.port <= 65535 - len(instances) + 1:
        parser.error('--port must leave room for one loopback proxy port per guest')
    records = verify_lab(args.metadata, instances)
    games = [Game(i, args.output, timeout=120) for i in instances]
    memories = []
    proxy_stack, proxies = ExitStack(), {}
    result = dict(pass_=False, records=records, cases=[], network=dict(
        enabled=bool(args.loss or args.jitter), loss=args.loss, jitter_seconds=args.jitter, seed=args.seed, peers={}))
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        if games[0].adventure()['active']:
            games[0].call('coop.adventure cancel')
            for game in games:
                menu(game, records[game.instance])
        for game in games:
            game.dismiss_registration()
            game.call('coop.leave')
        games[0].call(f'coop.host {args.port} ExplosivesHost')
        for index, game in enumerate(games[1:], 1):
            target = args.port
            if args.loss or args.jitter:
                target = args.port + index
                proxies[game.instance] = proxy_stack.enter_context(NetworkProxy(
                    target, args.port, loss=args.loss, jitter=args.jitter, seed=args.seed + index))
                result['network']['peers'][game.instance] = dict(listen=target, target=args.port, seed=args.seed + index)
            game.call(f'coop.join 127.0.0.1:{target} ExplosivesGuest{game.instance}')
        games[0].wait('complete explosive test party', lambda: games[0].adventure()['players'] == len(games))
        memories = [AdventureMemory(g, records[g.instance]) for g in games]
        for kind in kinds:
            for ff in friendly:
                network_before = {instance: proxy.snapshot() for instance, proxy in proxies.items()}
                if kind == 'mine':
                    case = mine_case(games, records, memories, ff)
                elif kind in ('barrel', 'crate'):
                    case = shooting_case(games, records, memories, kind, ff,
                                         check_goal=args.crate_objective and kind == 'crate')
                elif kind == 'grenade':
                    case = grenade_case(games, records, memories, ff)
                elif kind == 'chain':
                    case = chain_case(games, records, memories, ff)
                elif kind == 'recovery':
                    case = recovery_case(games, records, memories, ff)
                else:
                    case = timer_case(games, records, memories, kind, ff, repick=args.repick)
                clean_exceptions(games)
                if proxies:
                    case['network'] = {instance: NetworkProxy.difference(network_before[instance], proxy.snapshot())
                                       for instance, proxy in proxies.items()}
                result['cases'].append(case)
                (args.output / f'{kind}-ff{ff}.json').write_text(json.dumps(case, indent=2) + '\n')
                print(f'PASS: native {kind}, friendly fire {ff}, {len(games)} players', flush=True)
        result['network']['totals'] = {instance: proxy.snapshot() for instance, proxy in proxies.items()}
        result['pass_'] = True
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        return 0
    except (FixtureError, OSError, ValueError, RuntimeError) as error:
        result['error'] = str(error)
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        print('FAIL:', error, flush=True)
        for game in games:
            try:
                game.record('explosives-failure')
            except (FixtureError, OSError, ValueError):
                pass
        return 1
    finally:
        proxy_stack.close()
        if 'totals' not in result['network']:
            try:
                result['network']['totals'] = {instance: proxy.snapshot() for instance, proxy in proxies.items()}
            except RuntimeError as error:
                result['network']['proxy_error'] = str(error)
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
        for memory in memories:
            memory.close()
        for game in games:
            game.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
