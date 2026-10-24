#!/usr/bin/env python3
"""Verify stationary actor replication in an existing, committed lab adventure.

The runner owns hidden, muted Windows games and their recorded metadata. This
test never connects a party, launches, cancels, changes a save or resets a run.
It moves one ordinary stationary actor using the verified outside-frame nudge
adapter, then restores its original pose. Forward act changes are opt-in.
"""
import argparse
from collections import Counter
import json
import math
from pathlib import Path
import re
import time

from coop_adventure import FixtureError, Game, verify_lab


EXE_SHA256 = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
FLOAT = r'[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?'
POSE_TOLERANCE = .015  # Native text diagnostics round positions to six figures.


def require(value, message):
    if not value:
        raise FixtureError(message)


def distance(a, b):
    return math.sqrt(sum((x-y)**2 for x, y in zip(a, b)))


def parse_world(text):
    summary = re.search(r'^adventure world: (.+)$', text, re.M)
    stats = re.search(r'^static replication: (.+)$', text, re.M)
    mode = re.search(r'^act=(\d+) state=(\d+) time_ms=(\d+) remaining_ms=(-?\d+)', text, re.M)
    require(summary and stats and mode, 'complete preview 6 native world diagnostics are required')
    result = dict(counts={key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', summary[1])},
                  stats={key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', stats[1])},
                  act=int(mode[1]), state=int(mode[2]), time_ms=int(mode[3]), actors=[])
    for line in text.splitlines():
        actor = re.match(rf'^marker=(\d+) incarnation=(\d+) noun=(\d+) class=(-?\d+) '
                         rf'pos=({FLOAT}),({FLOAT}),({FLOAT})(?:\s|$)', line)
        if not actor:
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        position = [float(value) for value in actor.groups()[4:]]
        require(all(math.isfinite(value) for value in position), 'nonfinite native actor position')
        result['actors'].append(dict(marker=int(actor[1]), incarnation=int(actor[2]),
            noun=int(actor[3]), class_index=int(actor[4]), position=position,
            candidate=fields.get('static_candidate') == '1', replication=fields.get('replication'), line=line))
    for key in ('static_bytes', 'dynamic_bytes', 'baselines', 'state_updates', 'promoted'):
        require(key in result['stats'], f'missing stationary replication counter {key}')
    return result


def parse_details(text):
    result = []
    for line in text.splitlines():
        match = re.match(r'^marker=(\d+) incarnation=(\d+) noun=(\d+) object=(?:0x)?([0-9a-fA-F]+) class=(-?\d+)', line)
        if not match:
            continue
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        result.append(dict(marker=int(match[1]), incarnation=int(match[2]), noun=int(match[3]),
            object=int(match[4], 16), class_index=int(match[5]),
            powerup=int(fields['powerup']) if 'powerup' in fields else None,
            host_incarnation=int(fields['host_incarnation']) if 'host_incarnation' in fields else None,
            line=line))
    return result


def unique_ordinary_details(host_rows, guest_rows, selected):
    """Exclude all common powerups, including kind0, and same-marker siblings."""
    if len(host_rows) != 1 or len(guest_rows) != 1:
        return False
    host, guest = host_rows[0], guest_rows[0]
    return (host['powerup'] is None and guest['powerup'] is None and
            host['incarnation'] == selected['incarnation'] and
            guest['host_incarnation'] == selected['incarnation'] and
            host['noun'] == guest['noun'] == selected['noun'] and
            host['class_index'] == guest['class_index'] == selected['class_index'])


def snapshot(game):
    return parse_world(game.call('coop.adventure.world status'))


def actor_at(state, selected, incarnation=None):
    matches = [actor for actor in state['actors'] if actor['marker'] == selected['marker'] and
               actor['noun'] == selected['noun'] and actor['class_index'] == selected['class_index']]
    if incarnation is not None:
        matches = [actor for actor in matches if actor['incarnation'] == incarnation]
    return matches[0] if len(matches) == 1 else None


def clean_faults(games):
    result = []
    for game in games:
        text = game.call('exc')
        match = re.search(r'total faults:\s*(\d+)', text)
        require(match and int(match[1]) == 0, f'instance {game.instance}: native faults: {text}')
        result.append(dict(instance=game.instance, total_faults=int(match[1])))
    return result


class Suite:
    def __init__(self, games, records, args):
        self.games, self.records, self.args = games, records, args
        self.host, self.guest = games
        self.run = None
        self.players = None
        self.selected = None
        self.guest_incarnation = None
        self.original_act = None
        self.original_position = None
        self.host_moved = False

    def same_run(self):
        states = [game.adventure() for game in self.games]
        require(all(state['active'] and state['run'] == self.run and state['players'] == self.players and
                    'Adventure in progress' in state['text'] for state in states),
                'the party, committed run or active adventure changed during stationary verification')
        return states

    def prepare(self):
        for game in self.games:
            info = game.call('info')
            require(re.search(rf'\bpid={self.records[game.instance]["pid"]}\b', info) and
                    f'exe sha256={EXE_SHA256}' in info,
                    f'instance {game.instance}: console PID/executable differs from the recorded lab')
            require('Scenario' in game.call('game.mode'), 'both games must already be in a native adventure')
        require(self.host.player_id() == 0 and self.guest.player_id() > 0,
                '--instances must name the existing host followed by one of its guests')
        initial = self.host.adventure()
        self.run, self.players = initial['run'], initial['players']
        require(self.run and self.players >= 2, 'an existing multiplayer adventure is required')
        self.same_run()
        states = [snapshot(game) for game in self.games]
        require(all(state['state'] == 3 for state in states),
                'finish shared Intro or the current conversation before running this test')
        require(states[0]['act'] == states[1]['act'], 'host and guest native acts differ')
        self.original_act = states[0]['act']
        self.wait_rosters()
        return dict(run=self.run, players=self.players, act=self.original_act,
                    faults=clean_faults(self.games))

    def wait_rosters(self, act=None):
        def complete():
            self.same_run()
            states = [snapshot(game) for game in self.games]
            if act is not None and any(state['act'] != act for state in states):
                return None
            host, guest = [state['counts'] for state in states]
            expected = host.get('actors', 0)
            return states if expected and all(guest.get(key) == expected for key in ('remote', 'mirrored', 'roster')) else None
        return self.host.wait('all host actor baselines bound on the guest', complete, timeout=self.args.timeout)

    def choose(self):
        states = [snapshot(game) for game in self.games]
        counts = Counter(actor['marker'] for actor in states[0]['actors'])
        require(any(actor['replication'] is not None for actor in states[0]['actors']),
                'the DLL needs per-actor replication=static|dynamic diagnostics')
        for actor in states[0]['actors']:
            if (not actor['candidate'] or actor['replication'] != 'static' or counts[actor['marker']] != 1 or
                    (self.args.marker is not None and actor['marker'] != self.args.marker)):
                continue
            guest_actor = actor_at(states[1], actor)
            if not guest_actor or distance(actor['position'], guest_actor['position']) > POSE_TOLERANCE:
                continue
            host_details = parse_details(self.host.call(f'coop.adventure.world actorsat {actor["marker"]} 48'))
            guest_details = parse_details(self.guest.call(f'coop.adventure.world actorsat {actor["marker"]} 48'))
            if not unique_ordinary_details(host_details, guest_details, actor):
                continue
            self.selected = actor
            self.guest_incarnation = guest_details[0]['incarnation']
            self.original_position = list(actor['position'])
            return dict(host=actor, guest=guest_actor, host_details=host_details, guest_details=guest_details)
        raise FixtureError('no unpromoted, solitary, bound non-powerup stationary actor was found')

    def current(self):
        self.same_run()
        states = [snapshot(game) for game in self.games]
        require(all(state['act'] == self.original_act for state in states),
                'the native act changed during pose verification')
        actors = [actor_at(state, self.selected, incarnation) for state, incarnation in
                  zip(states, (self.selected['incarnation'], self.guest_incarnation))]
        require(all(actors), 'the selected actor was removed, replaced or acquired a sibling')
        return states, actors

    def quiet(self):
        """Find a real two-second unchanged window after initial queue drain."""
        deadline = time.monotonic() + self.args.timeout
        attempts = []
        while time.monotonic() < deadline:
            states, actors = self.current()
            before = states[0]['stats']
            require(actors[0]['replication'] == 'static', 'selected actor was promoted before the test')
            start = time.monotonic()
            samples = []
            while time.monotonic() - start < self.args.steady_seconds:
                time.sleep(.25)
                states, actors = self.current()
                after = states[0]['stats']
                samples.append(dict(wall_seconds=time.monotonic() - start, stats=after))
                require(distance(actors[0]['position'], self.original_position) <= POSE_TOLERANCE and
                        distance(actors[1]['position'], self.original_position) <= POSE_TOLERANCE,
                        'the selected actor moved before a test nudge')
                if any(after[key] != before[key] for key in ('static_bytes', 'baselines')):
                    break
            elapsed = time.monotonic() - start
            attempts.append(dict(before=before, samples=samples, wall_seconds=elapsed))
            if elapsed >= self.args.steady_seconds and all(after[key] == before[key] for key in ('static_bytes', 'baselines')):
                if states[0]['stats'].get('dynamic', 0):
                    require(after['dynamic_bytes'] > before['dynamic_bytes'], 'dynamic world traffic stopped during the steady interval')
                return dict(attempts=attempts, before=before, after=after, wall_seconds=elapsed)
        raise FixtureError('no unchanged stationary-byte/baseline interval was observed before timeout')

    def nudge(self, game, delta):
        command = f'coop.adventure.world nudge {self.selected["marker"]} ' + ' '.join(f'{value:.9g}' for value in delta)
        response = game.call(command)
        require('Native static actor moved.' in response, 'the verified native nudge was not accepted: ' + response)
        return dict(instance=game.instance, delta=list(delta), command=command, response=response)

    def matching(self, target, dynamic=False):
        states, actors = self.current()
        if dynamic and actors[0]['replication'] != 'dynamic':
            return None
        return dict(states=states, actors=actors) if all(
            distance(actor['position'], target) <= POSE_TOLERANCE for actor in actors) else None

    def drift(self):
        states, _ = self.current()
        before = states[0]['stats']
        nudge = self.nudge(self.guest, (1., 0., 0.))
        start = time.monotonic()
        first = snapshot(self.guest)
        repaired = self.host.wait('guest drift repaired from retained stationary pose',
                                  lambda: self.matching(self.original_position), timeout=self.args.timeout)
        after = repaired['states'][0]['stats']
        require(after['static_bytes'] == before['static_bytes'] and after['baselines'] == before['baselines'],
                'guest drift repair needed a new host stationary packet/baseline')
        require(after['promoted'] == before['promoted'], 'a guest-only nudge promoted a host actor')
        return dict(before=before, nudge=nudge, first_guest_observation=first,
                    repaired=repaired, repair_wall_seconds=time.monotonic() - start,
                    note='The outside-frame nudge can be repaired before the next diagnostic read.')

    def movement(self):
        states, _ = self.current()
        before = states[0]['stats']
        first_target = [self.original_position[0] + 1., *self.original_position[1:]]
        self.host_moved = True
        first_nudge = self.nudge(self.host, (1., 0., 0.))
        first = self.host.wait('host motion promotes and reaches the guest',
                               lambda: self.matching(first_target, dynamic=True), timeout=self.args.timeout)
        promoted = first['states'][0]['stats']
        require(promoted['promoted'] == before['promoted'] + 1, 'host movement did not promote exactly the selected identity')
        require(promoted['baselines'] > before['baselines'], 'promotion did not queue a reliable full correction')
        second_target = [first_target[0], first_target[1] + 1., first_target[2]]
        second_nudge = self.nudge(self.host, (0., 1., 0.))
        second = self.host.wait('further host motion reaches the guest on dynamic snapshots',
                                lambda: self.matching(second_target, dynamic=True), timeout=self.args.timeout)
        continued = second['states'][0]['stats']
        require(continued['promoted'] == promoted['promoted'], 'the same identity was promoted twice')
        require(continued['baselines'] == promoted['baselines'], 'continued dynamic movement queued another stationary baseline')
        require(continued['dynamic_bytes'] > promoted['dynamic_bytes'], 'continued movement produced no dynamic world traffic')
        return dict(before=before, first_nudge=first_nudge, first=first, second_nudge=second_nudge, second=second)

    def restore(self):
        if not self.selected or not self.host_moved:
            return dict(required=False)
        states, actors = self.current()
        delta = [original-current for original, current in zip(self.original_position, actors[0]['position'])]
        nudge = self.nudge(self.host, delta)
        restored = self.host.wait('original stationary actor pose restored on both games',
                                  lambda: self.matching(self.original_position, dynamic=True), timeout=self.args.timeout)
        self.host_moved = False
        return dict(required=True, nudge=nudge, restored=restored,
                    note='The actor remains promoted until the next act, as required by replication.')

    def transitions(self, acts):
        result = []
        previous = self.original_act
        for act in acts:
            require(previous < act < 8, 'opt-in act changes must move strictly forward within 0..7')
            self.same_run()
            response = self.host.call(f'coop.adventure.world act {act}')
            require('Native act changed.' in response, 'native act change was not accepted')
            states = self.wait_rosters(act)
            result.append(dict(act=act, response=response, counts=[state['counts'] for state in states],
                               stats=[state['stats'] for state in states]))
            previous = act
        return result


def self_test():
    text = ('adventure world: available=1 run=7 actors=1 mirrored=1 remote=1 roster=1\n'
            'static replication: static=1 dynamic=2 static_bytes=70 dynamic_bytes=663 '
            'baselines=1 state_updates=0 promoted=0 deferred=0\n'
            'act=0 state=3 time_ms=12 remaining_ms=-1\n'
            'marker=108 incarnation=22 noun=123 class=0 pos=-1.25,2e+02,3.5 '
            'static_candidate=1 replication=static hp=50\n')
    state = parse_world(text)
    selected = state['actors'][0]
    assert selected['position'] == [-1.25, 200., 3.5] and selected['replication'] == 'static'
    assert state['stats']['baselines'] == 1 and state['counts']['run'] == 7
    host = parse_details('marker=108 incarnation=22 noun=123 object=0x12345678 class=0\n')
    guest = parse_details('marker=108 incarnation=99 noun=123 object=12345679 class=0 host_incarnation=22\n')
    assert unique_ordinary_details(host, guest, selected)
    assert not unique_ordinary_details(host * 2, guest, selected)
    powerup = parse_details('marker=108 incarnation=22 noun=123 object=0x12345678 class=0 powerup=0\n')
    assert not unique_ordinary_details(powerup, guest, selected)
    guest[0]['host_incarnation'] = 23
    assert not unique_ordinary_details(host, guest, selected)
    assert actor_at(state, selected, 22) and not actor_at(state, selected, 23)
    try:
        parse_world('incomplete old DLL diagnostics')
    except FixtureError:
        pass
    else:
        raise AssertionError('old diagnostics were accepted')
    print('PASS: stationary native harness parser and identity checks (offline)', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--instances', help='existing host,guest console instances (default: 21,22)')
    parser.add_argument('--host', type=int, help='host instance, used together with --guest')
    parser.add_argument('--guest', type=int, help='guest instance, used together with --host')
    parser.add_argument('--marker', type=int, help='optional explicit solitary non-powerup stationary marker')
    parser.add_argument('--steady-seconds', type=float, default=2.)
    parser.add_argument('--timeout', type=float, default=30.)
    parser.add_argument('--act-transitions', nargs='?', const='1,2',
                        help='opt in to forward native act changes after pose tests (default: 1,2)')
    parser.add_argument('--self-test', action='store_true', help='run offline parser/identity checks without connecting to games')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if args.metadata is None or args.output is None:
        parser.error('--metadata and --output are required for native verification')
    if args.host is not None or args.guest is not None:
        if args.host is None or args.guest is None or args.instances is not None:
            parser.error('use both --host and --guest, or use --instances HOST,GUEST')
        instances = (args.host, args.guest)
    else:
        instances = tuple(map(int, (args.instances or '21,22').split(',')))
    if len(instances) != 2 or len(set(instances)) != 2 or not 2 <= args.steady_seconds <= 30 or not 5 <= args.timeout <= 120:
        parser.error('use exactly host,guest and a steady interval of 2..30 seconds with timeout 5..120')
    acts = tuple(map(int, args.act_transitions.split(','))) if args.act_transitions else ()
    records = verify_lab(args.metadata, instances)
    games = [Game(instance, args.output, timeout=args.timeout) for instance in instances]
    suite = Suite(games, records, args)
    result = dict(pass_=False, records=records, act_changes_opt_in=list(acts),
                  limitations=['Per-recipient byte counters measure application payloads, not UDP/IP or retransmissions.'])
    args.output.mkdir(parents=True, exist_ok=True)
    try:
        result['initial'] = suite.prepare()
        previous = suite.original_act
        for act in acts:
            require(previous < act < 8, 'opt-in act changes must move strictly forward within 0..7')
            previous = act
        result['selection'] = suite.choose()
        result['steady'] = suite.quiet()
        result['guest_cache_repair'] = suite.drift()
        result['host_promotion_and_dynamic_motion'] = suite.movement()
        result['pose_restore'] = suite.restore()
        if acts:
            result['forward_act_changes'] = suite.transitions(acts)
        suite.same_run()
        result['faults'] = clean_faults(games)
        result['pass_'] = True
        print('PASS: stationary baseline silence, cached drift repair and dynamic promotion', flush=True)
        return 0
    except (FixtureError, OSError, ValueError) as error:
        result['error'] = str(error)
        print('FAIL:', error, flush=True)
        if suite.host_moved:
            try:
                result['pose_restore_after_failure'] = suite.restore()
            except (FixtureError, OSError, ValueError) as restore_error:
                result['pose_restore_error'] = str(restore_error)
        for game in games:
            try:
                game.record('static-failure')
            except (FixtureError, OSError, ValueError):
                pass
        return 1
    finally:
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        for game in games:
            game.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
