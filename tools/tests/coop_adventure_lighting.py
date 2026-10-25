#!/usr/bin/env python3
"""Verify GA lighting isolation with real UDP counters and native memory.

The standalone case starts Town in a recorded hidden lab pair through a loopback
proxy. It observes stationary guest lighting while the host walks for 20 seconds,
then delivers a valid legacy WORLD_TIME packet while the guest's native simulator
is paused. A native Apply probe proves that the delivered packet is ignored rather
than applied and overwritten before a screenshot can capture it.
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

from adventure_memory import AdventureMemory
from adventure_network_proxy import NetworkProxy
from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_combat import check
from coop_adventure_lobby import TOWN, lobby, playing, click_named
from coop_adventure_preparation import menu
from adventure_control_fixture import marker_teleport


def messages(data):
    if len(data) < 31 or data[:4] != b'SPCO':
        return
    offset = 31
    for _ in range(data[30]):
        if offset + 4 > len(data):
            return
        kind, flags, size = struct.unpack_from('<BBH', data, offset)
        offset += 6 if flags & 1 else 4
        if offset + size > len(data):
            return
        yield kind, bytes(data[offset:offset + size])
        offset += size


class LightingProxy(NetworkProxy):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.last_host_header = None
        self.injected = 0

    def _record(self, direction, data):
        super()._record(direction, data)
        extra = Counter()
        for kind, payload in messages(data):
            extra[f'{direction}_kind_{kind}_messages'] += 1
            extra[f'{direction}_kind_{kind}_bytes'] += len(payload)
        with self.lock:
            self.counts.update(extra)
            if direction == 'down' and len(data) >= 31 and data[:4] == b'SPCO':
                self.last_host_header = bytes(data[:31])

    def inject_world_time(self, quaternion):
        with self.lock:
            check(self.last_host_header is not None and self.client is not None, 'proxy has no active host/guest connection')
            header = bytearray(self.last_host_header)
            client = self.client
        # Existing Connection::on_packet delivers unreliable messages in valid
        # duplicate-sequence packets. Reuse a real header so connection/acks stay
        # valid, and change only its count/payload. No reliable IDs are invented.
        header[30] = 1
        packet = bytes(header) + struct.pack('<BBH4f', 22, 0, 16, *quaternion)
        self._record('down', packet)
        self.socket.sendto(packet, client)
        self.injected += 1
        return dict(connection=struct.unpack_from('<I', header, 6)[0], sequence=struct.unpack_from('<H', header, 10)[0],
                    bytes=len(packet), quaternion=quaternion)


def address(text, name):
    match = re.search(name + r'=(?:0x)?([0-9A-Fa-f]+)', text)
    if not match:
        raise FixtureError('native diagnostic address unavailable: ' + name)
    return int(match.group(1), 16)


class LightingMemory:
    def __init__(self, memory):
        self.memory = memory
        game = memory.game
        self.avatar = address(game.call('coop.avatar'), 'local')
        self.incarnation = memory.pointer(self.avatar + 0x24)
        camera = game.call('coop.cam')
        self.viewer = address(camera, 'viewer')
        self.camera = address(camera, 'camera')
        self.planet = address(game.call('call SporeCoop::TimeOfDay_GetObject cdecl'), 'eax')
        check(self.planet != 0, 'native active planet unavailable')

    def sample(self):
        memory = self.memory
        check(memory.pointer(self.avatar + 0x24) == self.incarnation, 'native captain changed during lighting measurement')
        return dict(position=struct.unpack('<fff', memory.read(self.avatar + 0xC4, 12)),
                    quaternion=struct.unpack('<ffff', memory.read(self.planet + 0x10, 16)),
                    camera_to_world=struct.unpack('<16f', memory.read(self.viewer, 64)),
                    camera=struct.unpack('<16f', memory.read(self.camera, 64)))


def distance(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))


def quaternion_angle(a, b):
    norm_a = math.sqrt(sum(x * x for x in a))
    norm_b = math.sqrt(sum(x * x for x in b))
    check(norm_a > .9 and norm_b > .9, 'native planet quaternion is invalid')
    dot = abs(sum(x * y for x, y in zip(a, b)) / (norm_a * norm_b))
    return math.degrees(2 * math.acos(min(1, dot)))


def probe_hits(text):
    match = re.search(r'TimeOfDay_Apply\S* hits=(\d+)', text)
    if not match:
        # symbols_reverse can prefer another alias at the same verified address.
        match = re.search(r'^probe 0x[0-9A-Fa-f]+ .*?hits=(\d+)', text, re.M)
    check(match is not None, 'native Apply probe counter unavailable')
    return int(match.group(1))


def receive_guard(guest, reader, proxy):
    owned_probe = False
    opened_pause = False
    try:
        check('no probes' in guest.call('probe.list'), 'lighting fixture requires no pre-existing native probes')
        guest.call('key esc')
        guest.wait('guest native pause menu', lambda: guest.caption('Resume Game'), timeout=8)
        opened_pause = True
        response = guest.call('probe SporeCoop::TimeOfDay_Apply')
        check('probe installed' in response, 'native Apply probe did not install')
        owned_probe = True
        guest.call('probe.list clear')
        before = reader.sample()
        time.sleep(.6)
        baseline = guest.call('probe.list')
        check(probe_hits(baseline) == 0, 'native lighting still updates while paused; cannot isolate packet application')
        # An identity quaternion is valid but usually far from Town's authored
        # orientation. If already equal, use a 90-degree rotation about X.
        candidate = (0., 0., 0., 1.)
        if quaternion_angle(before['quaternion'], candidate) < 15:
            candidate = (math.sqrt(.5), 0., 0., math.sqrt(.5))
        injection = proxy.inject_world_time(candidate)
        time.sleep(.7)
        guest.call('wait.frames 5 15000')
        after = reader.sample()
        report = guest.call('probe.list')
        check(probe_hits(report) == 0, 'guest applied a legacy planet-orientation packet inside GA')
        check(quaternion_angle(before['quaternion'], after['quaternion']) < .001,
              'injected legacy time packet changed the paused native planet quaternion')
        return dict(injection=injection, before=before, after=after, baseline_probe=baseline, after_probe=report,
                    check='valid host WORLD_TIME delivered; native Apply remains at zero calls and planet orientation unchanged')
    finally:
        if owned_probe:
            guest.call('probe.clear', required=False)
        if opened_pause:
            guest.press('Resume Game')


def run_case(games, records, proxy, seconds=20):
    check(seconds >= 20, 'lighting motion measurement must last at least 20 seconds')
    memories = [AdventureMemory(game, records[game.instance]) for game in games]
    host, guest = games
    try:
        readers = [LightingMemory(memory) for memory in memories]
        for game in games:
            game.call('coop.uistate 0')
            game.record('lighting-initial')
        initial = [reader.sample() for reader in readers]
        goals_before = memories[0].goals()
        traffic_before = proxy.snapshot()
        samples = []
        started = time.monotonic()
        key = None
        try:
            while time.monotonic() - started < seconds:
                elapsed = time.monotonic() - started
                wanted = 'W' if 4 <= elapsed < 10 else 'D' if 10 <= elapsed < 16 else None
                if wanted != key:
                    if key:
                        host.call(f'key {key} up')
                    if wanted:
                        host.call(f'key {wanted} down')
                    key = wanted
                samples.append(dict(wall=elapsed, phase=wanted or 'stationary', host=readers[0].sample(), guest=readers[1].sample()))
                time.sleep(.1)
        finally:
            for released in ('W', 'D'):
                host.call(f'key {released} up', required=False)
        traffic = proxy.difference(traffic_before, proxy.snapshot())
        # Preserve raw evidence even when an environmental contact invalidates
        # the stationary-player precondition or a later assertion fails.
        (host.output / 'motion-samples.json').write_text(json.dumps(dict(
            initial=initial, samples=samples, native_traffic=traffic), indent=2)+'\n', encoding='utf-8')
        check(traffic.get('down_kind_22_messages', 0) == 0 and traffic.get('up_kind_22_messages', 0) == 0,
              'native GA still publishes a legacy WORLD_TIME packet')
        host_distance = max(distance(row['host']['position'], initial[0]['position']) for row in samples)
        guest_distance = max(distance(row['guest']['position'], initial[1]['position']) for row in samples)
        check(host_distance >= 2, 'ordinary host movement did not exercise local lighting changes')
        check(guest_distance <= .2, 'guest moved during stationary lighting measurement')
        guards = receive_guard(guest, readers[1], proxy)
        goals_after = memories[0].goals()
        check(goals_after['act'] == goals_before['act'] and goals_after['elapsed'] > goals_before['elapsed'],
              'shared adventure simulation clock did not continue through the lighting check')
        for game in games:
            check('total faults: 0' in game.call('exc'), 'native lighting check logged an exception')
            game.record('lighting-final')
        return dict(elapsed=time.monotonic() - started, samples=samples, native_traffic=traffic,
                    native_world_time_messages=traffic.get('down_kind_22_messages', 0),
                    host_max_displacement=host_distance, guest_max_displacement=guest_distance,
                    guest_max_planet_angle=max(quaternion_angle(row['guest']['quaternion'], initial[1]['quaternion']) for row in samples),
                    host_max_planet_angle=max(quaternion_angle(row['host']['quaternion'], initial[0]['quaternion']) for row in samples),
                    guest_max_camera_component_delta=max(abs(a - b) for row in samples
                        for a, b in zip(row['guest']['camera_to_world'], initial[1]['camera_to_world'])),
                    receive_guard=guards, goals_before=goals_before, goals_after=goals_after,
                    lighting_expectation='Each client retains native authored/local lighting; host/guest quaternions need not match.',
                    checks=['actual per-peer WORLD_TIME count is zero during ordinary host movement',
                            'stationary guest native position and camera/planet data recorded',
                            'valid legacy packet is ignored while native lighting is paused', 'shared host clock advances', 'no native faults'])
    finally:
        for memory in memories:
            memory.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--captains', required=True, help='two existing saved captain names, host first')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--seconds', type=float, default=20)
    parser.add_argument('--port', type=int, default=47870)
    args = parser.parse_args()
    captains = args.captains.split(',')
    if len(captains) != 2:
        parser.error('--captains requires two comma-separated saved names')
    records = verify_lab(args.metadata, (args.host, args.guest))
    games = [Game(instance, args.out, timeout=180) for instance in (args.host, args.guest)]
    host, guest = games
    try:
        host.call('coop.adventure cancel', required=False)
        for game in games:
            menu(game, records[game.instance]); game.call('coop.leave')
        with LightingProxy(args.port + 10, args.port) as proxy:
            host.call(f'coop.host {args.port} LightingHost')
            guest.call(f'coop.join 127.0.0.1:{args.port + 10} LightingGuest')
            host.wait('two lighting participants', lambda: host.adventure()['players'] == 2)
            host.call('coop.adventure start ' + TOWN)
            for game, captain in zip(games, captains):
                game.prepare_captain(captain, create=False)
            for game in games:
                lobby(game, 2)
            for game in (guest, host):
                click_named(game, 'prep_ready')
            playing(games, records)
            # Town's spawn overlaps captains. Complete its real MoveTo objective
            # before separating them, so host motion cannot push an idle guest.
            for game in games:
                game.call(f'coop.adventure.world near 169 {game.player_id()} 0 0')
            memories=[AdventureMemory(game,records[game.instance]) for game in games]
            try:
                host.wait('Town opening MoveTo advances into the populated town',
                          lambda:all(memory.goals()['act']==1 for memory in memories))
                # Keep the walking host on the gate's open street. A positive
                # X offset there lands inside its tower for some captain sizes.
                # The guest stays at a distant, stable ground-key placement.
                marker_teleport(host,records[host.instance],169,-5)
                guest.call(f'coop.adventure.world near 98 {guest.player_id()} -10 0')
                readers=[LightingMemory(memory) for memory in memories]
                previous=[reader.sample()['position'] for reader in readers]
                stable_since=time.monotonic()
                def settled():
                    nonlocal previous,stable_since
                    current=[reader.sample()['position'] for reader in readers]
                    if any(distance(a,b)>.025 for a,b in zip(current,previous)):
                        stable_since=time.monotonic()
                    previous=current
                    return time.monotonic()-stable_since>=1.5
                host.wait('separated native captains settle before lighting measurement',settled,timeout=20)
            finally:
                for memory in memories:memory.close()
            result = run_case(games, records, proxy, args.seconds)
            result.update(result='PASS', profiles=records, versions=[game.call('mods') for game in games],
                          target=proxy.target, proxy=proxy.socket.getsockname(), client=proxy.client)
            installation = Path(json.loads(args.metadata.read_text())['installation'])
            result['dll_sha256'] = hashlib.sha256((installation / 'SporebinEP1/mods/coop.dll').read_bytes()).hexdigest()
            result['loaded_shadows']=[]
            for game in games:
                mods=game.call('mods')
                generation=re.search(r'^coop\s+loaded\s+gen=(\d+)\b',mods,re.M)
                check(generation is not None,'loaded co-op generation is unavailable')
                shadow=installation/'SporebinEP1/mods/.shadow'/f'coop.i{game.instance}.{generation[1]}.dll'
                result['loaded_shadows'].append(dict(instance=game.instance,mods=mods,path=str(shadow),
                    sha256=hashlib.sha256(shadow.read_bytes()).hexdigest()))
            (args.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
            print('PASS: native GA lighting, packet suppression and delivered-packet receive guard', flush=True)
        return 0
    except (FixtureError, OSError, ValueError) as error:
        print('FAIL:', error, flush=True)
        return 1
    finally:
        host.call('coop.adventure cancel', required=False)
        for game in games:
            try:
                menu(game, records[game.instance])
            finally:
                game.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
