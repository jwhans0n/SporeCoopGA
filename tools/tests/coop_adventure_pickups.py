#!/usr/bin/env python3
"""Exercise native collector-only upgrades and independent teleporters in a lab.

Requires the preview 6 DLL and recorded hidden, muted Windows lab games. All
accepted pickups use the native proximity reaction. Native ability damage uses
the reviewed outside-frame abilityhit diagnostic, before boost calculation.
No product profiles, loaded saves, completion callbacks or accepted claims are
written by this test. Fixture registration and live tuning come from the shared
interaction fixture helper.
"""
import argparse
import concurrent.futures
from collections import deque
from contextlib import ExitStack
import json
import math
from pathlib import Path
import re
import struct
import time

from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_lobby import lobby, playing
from coop_adventure_preparation import menu
from adventure_memory import AdventureMemory
from adventure_interaction_fixture import (OUTPUT as FIXTURE_OUTPUT, FIXTURES,
    Native, fixture_key, inspect_fixture, register_fixtures, tune_fixture)
from adventure_network_proxy import NetworkProxy


def require(value, message):
    if not value:
        raise FixtureError(message)


def distance(a, b):
    return math.sqrt(sum((x-y)**2 for x, y in zip(a, b)))


class CaptureProxy(NetworkProxy):
    """Record real protocol10 state for narrowly scoped private-lab faults."""
    def __init__(self, listen, target):
        super().__init__(listen, target)
        self.states = {}
        self.history = {}

    def _record(self, direction, data):
        super()._record(direction, data)
        if direction != 'up' or len(data)<31 or data[:4] != b'SPCO':
            return
        if struct.unpack_from('<H', data, 4)[0] != 10:
            self.error = 'pickup network fixture requires protocol10'
            return
        offset = 31
        for _ in range(data[30]):
            if offset+4>len(data):
                return
            kind, reliable, size = struct.unpack_from('<BBH', data, offset)
            offset += 6 if reliable else 4
            if offset+size>len(data):
                return
            if kind == 6 and size == 95:
                payload = bytes(data[offset:offset+size])
                with self.lock:
                    value = dict(header=bytes(data[:31]), payload=payload,
                        generation=struct.unpack_from('<Q', payload, 87)[0],
                        run=struct.unpack_from('<Q', payload, 71)[0],
                        position=struct.unpack_from('<3f', payload, 5), time=time.monotonic())
                    self.states[payload[0]] = value
                    self.history.setdefault(payload[0], deque(maxlen=256)).append(value)
            offset += size

    def state(self, player):
        self.snapshot()  # surface any asynchronous proxy error
        with self.lock:
            value = self.states.get(player)
            return dict(value) if value else None

    def pre_teleport_state(self, player, run, generation, destination, since):
        self.snapshot()
        with self.lock:
            return next((dict(value) for value in reversed(self.history.get(player, ()))
                         if value['run'] == run and value['generation'] == generation
                         and value['time'] >= since and distance(value['position'], destination)>20), None)

    def inject(self, player, kind, payload):
        current = self.state(player)
        require(current, 'no recorded native UDP state is available for the lab fault')
        header = bytearray(current['header']);header[30] = 1
        packet = bytes(header)+struct.pack('<BBH', kind, 0, len(payload))+payload
        self.socket.sendto(packet, self.target)
        return len(packet)

    def stale_movement(self, player, prior):
        current = self.state(player)
        require(current and current['run'] == prior['run'] and current['generation']>prior['generation'],
                'owning client has not adopted its teleport generation for the same adventure run')
        payload = bytearray(prior['payload'])
        # A current timestamp prevents an ordinary timestamp check from masking
        # the missing teleport fence. Position and generation are pre-teleport.
        struct.pack_into('<I', payload, 1, struct.unpack_from('<I', current['payload'], 1)[0]+1)
        return self.inject(player, 6, bytes(payload))


def captain(game, player):
    if game.player_id() == player:
        match = re.search(r'local=(?:0x)?([0-9a-fA-F]+)', game.call('coop.avatar'))
    else:
        match = re.search(rf'remote {player} .*?puppet=(?:0x)?([0-9a-fA-F]+)', game.call('coop.status'))
    require(match, f'{game.instance}: captain {player} is unavailable')
    return int(match[1], 16)


def state(game, memory, player):
    address = captain(game, player)
    block = memory.read(address, 0xFA8)
    def value(offset):
        number = struct.unpack_from('<f', block, offset)[0]
        return number if math.isfinite(number) else None
    return dict(address=address, incarnation=struct.unpack_from('<I', block, 0x24)[0],
        hp=value(0x5E0),
        position=struct.unpack_from('<3f', block, 0xC4),
        damage_enabled=bool(block[0xB10]), damage=value(0xB14),
        armor_enabled=bool(block[0xB18]), armor=value(0xB1C),
        speed=bool(struct.unpack_from('<I', block, 0xFA4)[0]&128),
        actual_speed=value(0x2A0), cooldown=bool(block[0xE60]), supported=bool(block[0x137]),
        velocity=struct.unpack_from('<3f', block, 0x288))


def counters(game):
    text = game.call('coop.adventure status')
    match = re.search(r'pickups ready grants=(\d+) teleports=(\d+) rejected=(\d+) pending=(\d+)', text)
    require(match, 'preview 6 pickup diagnostics are unavailable: '+text)
    return dict(zip(('grants', 'teleports', 'rejected', 'pending'), map(int, match.groups())))


def boosts(game):
    return {(int(player), int(kind)): dict(amount=float(amount), remaining=float(remaining))
            for player, kind, amount, remaining in re.findall(
                r'player=(\d+) type=(\d+) amount=([\deE+.-]+) remaining=([\deE+.-]+)',
                game.call('coop.adventure status'))}


def actor(game, marker, kind=None):
    text = game.call(f'coop.adventure.world actorsat {marker} 48')
    for line in text.splitlines():
        match = re.search(r'incarnation=(\d+).*?object=(0x[0-9a-fA-F]+)', line)
        powerup = re.search(r'powerup=(\d+)', line)
        actual_kind = int(powerup[1]) if powerup else 0
        if match and (kind is None or actual_kind == kind):
            host_id = re.search(r'host_incarnation=(\d+)', line)
            return dict(marker=marker, incarnation=int(match[1]), address=int(match[2], 16),
                        host_incarnation=int(host_id[1]) if host_id else int(match[1]), kind=actual_kind)
    return None


def float_property(native, item, key):
    """Read the actual powerup palette property without executing its FX helper."""
    powerup = native.virtual(item['address'], 0x0C, 0x08367427)
    require(powerup, 'native actor has no powerup interface')
    props = native.memory.pointer(powerup+0x18)
    scratch = native.scratch(bytes(4))
    if not native.virtual(props, 0x24, key, scratch):
        return 0.
    prop = native.memory.pointer(scratch)
    kind = struct.unpack('<H', native.memory.read(prop+0x12, 2))[0]
    require(kind in (0x0D, 0x10), f'native pickup float property has type {kind:X}')
    flags = native.memory.read(prop+0x10, 1)[0]
    value = native.memory.pointer(prop) if flags&0x30 else prop
    result = struct.unpack('<f', native.memory.read(value, 4))[0]
    require(math.isfinite(result), 'native pickup property is not finite')
    return result


def attack_ability(game, record, player):
    """Use verified native GetAbilitiesCount/GetAbility virtual signatures."""
    with Native(game, record) as native:
        address = captain(game, player)
        count = native.virtual(address, 0xB0)
        require(0 < count <= 128, 'invalid native captain ability count')
        choices = []
        for index in range(count):
            value = native.virtual(address, 0xB4, index)
            if not value:
                continue
            kind, category = struct.unpack('<II', native.memory.read(value+8, 8))
            damage = struct.unpack('<f', native.memory.read(value+0x3C, 4))[0]
            radius = struct.unpack('<f', native.memory.read(value+0x11C, 4))[0]
            if category == 1 and kind in (8, 30, 33) and damage > 0 and radius <= 0:
                choices.append(dict(index=index, kind=kind, authored_damage=damage, area_radius=radius))
        require(choices, 'fixture captain needs a native non-area Bite or Strike ability')
        return min(choices, key=lambda value: value['authored_damage'])


class Suite:
    def __init__(self, args, records, games, proxies):
        self.args, self.records, self.games = args, records, games
        self.host = games[0]
        self.memories = [AdventureMemory(g, records[g.instance]) for g in games]
        self.ids = []
        self.results = []
        self.proxies = proxies

    def close(self):
        for memory in self.memories:
            memory.close()

    def states(self, player):
        return [state(g, m, player) for g, m in zip(self.games, self.memories)]

    def owner_game(self, player):
        return self.games[self.ids.index(player)]

    def all_hp(self):
        return [self.states(player)[0]['hp'] for player in self.ids]

    def assert_hp(self, player, expected):
        def good():
            values = self.states(player)
            return values if all(abs(value['hp']-expected) < .16 for value in values) else None
        return self.host.wait(f'captain {player} shared health {expected:.3f}', good, timeout=5)

    def heal(self, player):
        game = self.owner_game(player)
        game.call(f'coop.adventure.world hit {player} env 100000 10')
        text = game.call('coop.avatar')
        maximum = float(re.search(r'hp=[\d.]+/([\d.]+)', text)[1])
        self.assert_hp(player, maximum)
        return maximum

    def native_hit(self, player, amount):
        before = self.states(player)[0]['hp']
        self.owner_game(player).call(f'coop.adventure.world hit {player} env {amount} 2')
        self.host.wait('native hit reaches host captain authority', lambda: self.states(player)[0]['hp'] < before-.01)
        return before-self.states(player)[0]['hp']

    def ability_hit(self, attacker, victim, ability):
        before = self.states(victim)[0]['hp']
        self.owner_game(attacker).call(f'coop.adventure.world abilityhit {victim} {attacker} {ability["index"]}')
        self.host.wait('native ability damage reaches captain authority', lambda: self.states(victim)[0]['hp'] < before-.01, timeout=6)
        after = self.states(victim)[0]['hp']
        self.assert_hp(victim, after)
        return before-after

    def launch(self, kind):
        if self.host.adventure()['active']:
            self.host.call('coop.adventure cancel')
            for game in self.games:
                menu(game, self.records[game.instance])
        self.host.call('coop.adventure start '+' '.join(f'0x{x:08X}' for x in fixture_key(kind)))
        for game in self.games:
            lobby(game, len(self.games))
        for game in self.games:
            game.call('coop.adventure ready 1')
        playing(self.games, self.records)
        self.host.wait('reciprocal captain puppets', lambda: all(
            game.call('coop.status').count('puppet pos=') == len(self.games)-1 for game in self.games))
        self.ids = [game.player_id() for game in self.games]
        spec = inspect_fixture(self.host, self.records[self.host.instance], kind)
        candidates = [entry['index'] for entry in spec['target_markers']]
        candidates.sort(key=lambda marker: (marker not in (108, 110), marker))
        native_kind = FIXTURES[kind][2]
        def physical_objects():
            actual = []
            for marker in candidates:
                if all(actor(game, marker, native_kind) for game in self.games):
                    actual.append(marker)
                if len(actual) >= 5:
                    break
            return actual if len(actual) >= 3 else None
        # Native spawner9 clocks are held during Intro. Its authored delay only
        # begins after all Continue confirmations, so do not inspect just once.
        actual = self.host.wait(f'{kind}: initial authored physical powerups', physical_objects, timeout=20)
        for game in self.games:
            game.call('coop.uistate 0')
        self.host.call('coop.adventure.world near 158 0 12 0')
        for game, player in zip(self.games[1:], self.ids[1:]):
            game.call(f'coop.adventure.world near 158 {player} 15 0')
        self.host.wait('native playing state', lambda: all(m.goals()['state'] == 3 for m in self.memories))
        return spec, actual

    def consume(self, kind, marker, player):
        native_kind = FIXTURES[kind][2]
        previous = [actor(g, marker, native_kind) for g in self.games]
        require(all(previous), 'physical pickup missing before native contact')
        before = counters(self.host)['grants']
        self.owner_game(player).call(f'coop.adventure.world near {marker} {player} 0 0')
        self.host.wait('one native pickup grant', lambda: counters(self.host)['grants'] == before+1)
        self.host.wait('same consumed incarnation removed on all clients', lambda: all(
            not (current := actor(g, marker, native_kind)) or current['incarnation'] != old['incarnation']
            for g, old in zip(self.games, previous)))
        # Leave the contact area before an authored respawner produces a new item.
        self.owner_game(player).call(f'coop.adventure.world near 158 {player} 15 0')
        return previous

    def check_effect(self, player, kind, amount):
        typ = FIXTURES[kind][2]
        def consistent():
            return all((player, typ) in boosts(game) and abs(boosts(game)[player, typ]['amount']-amount) < .001
                       for game in self.games)
        self.host.wait('same saved recipient boost on every peer', consistent)
        values = self.states(player)
        for value in values:
            if kind == 'speed':
                require(value['speed'], 'native speed modifier7 was not applied')
            elif kind == 'damage':
                require(value['damage_enabled'] and abs(value['damage']-amount)<.001, 'native damage boost fields differ')
            else:
                require(not value['armor_enabled'] and abs(value['armor']-amount)<.001, 'native armor was enabled in addition to host armor')
        return values

    def rejected_handlers(self, kind, marker):
        names = dict(health='ActivateMedKit', speed='ActivateSpeedBoost', damage='ActivateDamageBoost', armor='ActivateArmorBoost')
        game = self.games[1]
        player = self.ids[1]
        game.call(f'coop.adventure.world near {marker} {player} 25 0')
        before = counters(self.host)
        item = actor(game, marker, FIXTURES[kind][2])
        with Native(game, self.records[game.instance]) as native:
            value = native.virtual(item['address'], 0x0C, 0x08367427)
            # These full native functions are hooked by preview6. The hook only
            # queues work; no FX/resource work executes inside this call frame.
            native.call('SporeCoop::Adventure::'+names[kind], 'cdecl', value, captain(game, player))
        self.host.wait('distant native activation rejected by host', lambda: counters(self.host)['rejected'] > before['rejected'])
        require(counters(self.host)['grants'] == before['grants'] and actor(game, marker, FIXTURES[kind][2]),
                'out-of-range activation consumed the shared pickup')
        before = counters(self.host)
        with Native(game, self.records[game.instance]) as native:
            value = native.virtual(item['address'], 0x0C, 0x08367427)
            native.call('SporeCoop::Adventure::'+names[kind], 'cdecl', value, captain(game, 0))
        time.sleep(.6)
        require(counters(self.host)['grants'] == before['grants'], 'guest activated a different player captain')
        proxy = self.proxies[game.instance]
        host_item = actor(self.host, marker, FIXTURES[kind][2])
        # A real guest transport connection attempts to claim an item for host0.
        # Its sender identity is still supplied by Session, not by this payload.
        payload = struct.pack('<BQQBBBII', 152, self.host.adventure()['run'], 0x7FFFFFFFFFFFFFFF,
            self.memories[0].goals()['act'], 0, FIXTURES[kind][2], marker, host_item['incarnation'])
        proxy.inject(player, 52, payload)
        time.sleep(.6)
        require(counters(self.host)['grants'] == before['grants'] and actor(self.host, marker, FIXTURES[kind][2]),
                'forged guest network ownership claimed the host captain')
        game.call(f'coop.adventure.world near 158 {player} 15 0')

    def group_hold(self, player, typ):
        """Start the hooked native Talk entry; finish with real native UI input."""
        host = self.host
        host.call('coop.adventure.world near 158 0 1 0')
        target = actor(host, 158)
        require(target, 'native conversation actor158 is missing from fixture')
        with Native(host, self.records[host.instance]) as native:
            mode = native.memory.pointer(native.memory.singleton)
            play = native.memory.pointer(mode+0x78)
            first, last = struct.unpack('<II', native.memory.read(play+0x64, 8))
            require(last >= first and (last-first)%0x1AC == 0, 'invalid native Talk goal vector')
            goal = next((address for address in range(first, last, 0x1AC)
                         if native.memory.pointer(address+0x24) == 2 and not native.memory.read(address, 1)[0]), None)
            require(goal, 'fixture has no incomplete native Talk goal')
            # Active preview6 intercepts this entry before rendering, validates
            # the goal and schedules all native presentation outside the frame.
            native.call('SporeCoop::Adventure::TalkBegin', 'thiscall', play, goal, target['address'])
        for game in self.games:
            game.wait('party native conversation presentation', lambda game=game: game.caption('letterbox_top'))
        before = self.memories[0].goals()
        remaining = boosts(host)[player, typ]['remaining']
        time.sleep(3)
        after = self.memories[0].goals()
        remaining_after = boosts(host)[player, typ]['remaining']
        require(after['elapsed'] == before['elapsed'] and after['remaining'] == before['remaining'] and
                abs(remaining_after-remaining)<.03, 'group conversation advanced clocks or timed upgrade expiry')
        for game in self.games:
            deadline = time.monotonic()+45
            while time.monotonic()<deadline:
                if game.control(0x0445C99D):
                    game.click_control(0x0445C99D)
                    break
                if game.control(0x062EF353):
                    game.click_control(0x062EF353)
                else:
                    time.sleep(.2)
            else:
                raise FixtureError('native held conversation did not reach Finished')
        host.wait('group conversation releases gameplay', lambda: self.memories[0].goals()['state'] == 3)
        return dict(before=before, after=after, boost_remaining_before=remaining, boost_remaining_after=remaining_after)

    def health(self):
        spec, markers = self.launch('health')
        maxima = [self.heal(player) for player in self.ids]
        for player in self.ids:
            self.native_hit(player, min(20., maxima[self.ids.index(player)]*.2))
        before = self.all_hp()
        consumed = []
        for index, player in enumerate(self.ids[:2]):
            consumed.append(self.consume('health', markers[index], player))
            self.assert_hp(player, maxima[index])
            for other in self.ids:
                if other != player:
                    expected = maxima[self.ids.index(other)] if self.ids.index(other)<index else before[self.ids.index(other)]
                    self.assert_hp(other, expected)
        self.rejected_handlers('health', markers[2])
        # Two normal proximity reactions compete for the same native pickup.
        for player in self.ids[:2]:
            self.native_hit(player, 10)
        old = counters(self.host)['grants']
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            list(pool.map(lambda pair: pair[0].call(f'coop.adventure.world near {markers[2]} {pair[1]} 0 0'),
                          zip(self.games[:2], self.ids[:2])))
        self.host.wait('simultaneous contact resolves exactly one claim', lambda: counters(self.host)['grants'] == old+1)
        values = self.all_hp()[:2]
        winners = sum(abs(value-maximum)<.16 for value, maximum in zip(values, maxima[:2]))
        require(winners == 1, 'simultaneous collection healed both captains')
        for game, player in zip(self.games[:2], self.ids[:2]):
            game.call(f'coop.adventure.world near 158 {player} 15 0')
        respawn = []
        for index, game in enumerate(self.games):
            previous = consumed[0][index]
            new = game.wait('authored health pickup respawns with a new incarnation', lambda game=game, previous=previous:
                current if (current := actor(game, markers[0], 1)) and current['incarnation'] != previous['incarnation'] else None,
                timeout=15)
            respawn.append(new)
        self.results.append(dict(kind='health', run=self.host.adventure()['run'], consumed=consumed,
            simultaneous_health=values, respawn=respawn, checks=['host collector only', 'guest collector only', 'consume once on every peer',
                'native full-health restoration', 'host rejects distant native request', 'foreign captain handler rejected',
                'simultaneous claims have one winner', 'authored native respawn receives fresh incarnation']))

    def upgrade(self, kind):
        spec, markers = self.launch(kind)
        typ = FIXTURES[kind][2]
        with Native(self.host, self.records[self.host.instance]) as native:
            first = actor(self.host, markers[0], typ)
            duration = float_property(native, first, 0xAAC3B137)
            amount = 1. if kind == 'speed' else float_property(native, first, 0x6219412B)
        require(duration >= 1 and duration <= self.args.max_duration, f'{kind}: authored duration {duration} is outside test wait budget')
        require(amount > 0, f'{kind}: native authored boost amount must be positive')
        before = {player: self.states(player) for player in self.ids}
        abilities, baselines = {}, {}
        if kind == 'damage':
            for player, victim in zip(self.ids[:2], reversed(self.ids[:2])):
                abilities[player] = attack_ability(self.owner_game(player), self.records[self.owner_game(player).instance], player)
                self.heal(victim)
                baselines[player] = self.ability_hit(player, victim, abilities[player])
                self.heal(victim)
        origins = []
        for index, player in enumerate(self.ids[:2]):
            self.consume(kind, markers[index], player)
            values = self.check_effect(player, kind, amount)
            for other in self.ids[index+1:]:
                require((other, typ) not in boosts(self.host), f'{kind} was also granted to non-collecting captain {other}')
            if kind == 'armor':
                self.heal(player)
                delta = self.native_hit(player, 12)
                require(abs(delta-12/amount)<.16, f'armor reduced damage incorrectly or twice: {delta} vs {12/amount}')
                self.heal(player)
            elif kind == 'damage':
                victim = self.ids[1-index]
                self.heal(victim)
                delta = self.ability_hit(player, victim, abilities[player])
                require(abs(delta-baselines[player]*amount)<.2, f'native damage boost applied incorrectly or twice: {delta} vs {baselines[player]*amount}')
                self.heal(victim)
            origins.append(dict(player=player, states=values))
        remaining_before = boosts(self.host)[self.ids[1], typ]['remaining']
        # A third authored object refreshes only the guest's timer.
        time.sleep(min(1.5, duration/4))
        remaining_old = boosts(self.host)[self.ids[1], typ]['remaining']
        self.consume(kind, markers[2], self.ids[1])
        self.check_effect(self.ids[1], kind, amount)
        remaining_new = boosts(self.host)[self.ids[1], typ]['remaining']
        require(remaining_new > remaining_old+.25, 'repeated native pickup did not refresh its duration')
        held_evidence = self.group_hold(self.ids[1], typ) if kind in ('armor', 'speed') else None
        self.host.wait('host-authoritative boost expires on all clients', lambda: all(
            (self.ids[1], typ) not in boosts(game) for game in self.games), timeout=duration+12)
        final = self.states(self.ids[1])
        if kind == 'speed':
            require(all(not value['speed'] for value in final), 'native speed flag remained after expiry')
        elif kind == 'damage':
            require(all(not value['damage_enabled'] for value in final), 'native damage flag remained after expiry')
            victim = self.ids[0];self.heal(victim)
            delta = self.ability_hit(self.ids[1], victim, abilities[self.ids[1]])
            require(abs(delta-baselines[self.ids[1]])<.2, 'expired damage boost still changed native damage')
        else:
            delta = self.native_hit(self.ids[1], 12)
            require(abs(delta-12)<.16, 'expired armor still changed host damage')
        self.results.append(dict(kind=kind, amount=amount, duration=duration, before=before, origins=origins,
            remaining_before=remaining_before, remaining_old=remaining_old, remaining_refreshed=remaining_new,
            final=final, native_abilities=abilities, damage_baselines=baselines, held=held_evidence,
            checks=['host native contact', 'guest native contact', 'collector only on every peer', 'refresh', 'host expiry', 'exact native modifier or damage']))

    def teleporter(self):
        spec, markers = self.launch('teleporter')
        marker = markers[0]
        # Surface point taken from an existing native marker rather than a
        # guessed planar offset, so native planet clamping remains exercised.
        destination = next(entry['position'] for entry in spec['markers'] if entry['index'] == 158)
        source = next(entry['position'] for entry in spec['target_markers'] if entry['index'] == marker)
        stage = next(entry['position'] for entry in spec['markers'] if entry['index'] == 2)
        require(distance(source, destination)>20, 'teleporter fixture exit is too close to its entry')
        require(distance(source, stage)>8 and distance(stage, destination)>20,
                'teleporter fixture needs a distinct native surface staging point')
        tuning = [tune_fixture(game, self.records[game.instance], 'teleporter', marker=marker,
                              teleporter_exit=destination) for game in self.games]
        length = math.hypot(destination[0], destination[1])
        require(length>1, 'teleporter fixture needs a non-polar safe parking point')
        tangent = (-destination[1]/length*6, destination[0]/length*6)
        warps = []
        for player in self.ids[:2]:
            other = self.ids[1-self.ids.index(player)]
            # A large world-X offset changes altitude on this spherical planet.
            # Park tangentially near a safe native marker and let terrain settle
            # before blaming any physical displacement on the teleporter.
            self.owner_game(other).call(f'coop.adventure.world near 158 {other} {tangent[0]:.6f} {tangent[1]:.6f}')
            self.host.wait('non-entering captain settles on its native terrain', lambda: all(
                math.sqrt(sum(x*x for x in value['velocity']))<.2 for value in self.states(other)), timeout=8)
            time.sleep(.4)
            before = self.states(other)
            count = counters(self.host)['teleports']
            proxy = self.proxies.get(self.owner_game(player).instance)
            # Stage on a real native surface away from the entry. Capturing a
            # genuine distant, old-generation packet makes the fault capable
            # of exposing a missing fence, even if native contact and warp run
            # between two ordinary UDP sends.
            staged_at = time.monotonic()
            self.owner_game(player).call(f'coop.adventure.world near 2 {player} 0 0')
            self.host.wait('captain reaches native pre-teleport staging point', lambda: all(
                distance(value['position'], stage)<8 for value in self.states(player)))
            if proxy:
                self.host.wait('real old-generation source-area movement is captured', lambda:
                    (value := proxy.state(player)) and value['time']>=staged_at
                    and distance(value['position'], stage)<8)
                prior = proxy.state(player)
            else:
                prior = None
            require(counters(self.host)['teleports'] == count, 'staging point unexpectedly activated a teleporter')
            self.owner_game(player).call(f'coop.adventure.world near {marker} {player} 0 0')
            self.host.wait('native authored teleport reaches every observer', lambda: all(
                distance(value['position'], destination)<8 for value in self.states(player)))
            self.host.wait('one accepted teleporter activation', lambda: counters(self.host)['teleports'] == count+1)
            # +137 is ground support, not a flight flag. The native helper clears
            # it for a new terrain query; normal physics can set it true again.
            self.host.wait('teleport clears residual locomotion velocity', lambda: all(
                math.sqrt(sum(x*x for x in value['velocity']))<.5 for value in self.states(player)), timeout=1)
            for old, current in zip(before, self.states(other)):
                require(distance(old['position'], current['position'])<2, 'teleporter moved a non-entering captain: '+json.dumps(dict(before=old, after=current)))
            # Probe cooldown before slower screenshots/network fault sampling.
            self.owner_game(player).call(f'coop.adventure.world near {marker} {player} 0 0')
            time.sleep(.25)
            require(counters(self.host)['teleports'] == count+1, 'native teleporter cooldown allowed a second activation')
            returned_at = time.monotonic()
            self.owner_game(player).call(f'coop.adventure.world near 158 {player} 0 0')
            if proxy:
                self.host.wait('owner advertises the host-authored teleport generation', lambda:
                    (current := proxy.state(player)) and current['generation']>prior['generation'])
                prior = proxy.pre_teleport_state(player, prior['run'], prior['generation'], destination, staged_at)
                require(prior, 'no genuine distant pre-teleport movement packet was captured')
                # The cooldown probe deliberately moves the owner back to the
                # source using its NEW generation. Wait for that valid movement
                # to leave every interpolation buffer before injecting OLD
                # generation packets, otherwise a normal interpolation could
                # be mistaken for a failed teleport fence.
                def back_at_exit():
                    current = proxy.state(player)
                    return current and current['time']>=returned_at and distance(current['position'], destination)<8 and all(
                        distance(value['position'], destination)<8 for value in self.states(player))
                self.host.wait('cooldown probe movement returns to exit on all peers', back_at_exit)
                time.sleep(.5)
                require(back_at_exit(), 'current-generation cooldown probe has not settled at the authored exit')
                injected = 0
                samples = []
                for _ in range(8):
                    injected += proxy.stale_movement(player, prior)
                    time.sleep(.08)
                    samples.append(self.states(player))
                require(all(distance(value['position'], destination)<8 for sample in samples for value in sample),
                        'current-time stale-generation movement rewound a teleported captain: '+json.dumps(dict(
                            prior_position=prior['position'], prior_generation=prior['generation'], samples=samples)))
            else:
                injected, samples = 0, []
            self.owner_game(player).call(f'coop.adventure.world near 158 {player} 15 0')
            self.host.wait('per-captain native teleporter cooldown expires', lambda: all(not x['cooldown'] for x in self.states(player)), timeout=5)
            warps.append(dict(player=player, after=self.states(player), other_before=before, other_after=self.states(other),
                              stale_movement_bytes=injected, stale_movement_samples=samples,
                              stale_prior={key: prior[key] for key in ('position', 'generation', 'run')} if prior else None))
        require(all(actor(game, marker, 8) for game in self.games), 'teleporter was consumed as a one-shot pickup')
        # A destination at the entry fails the real native authored-exit check.
        # It must neither move a captain nor begin a cooldown.
        for game in self.games:
            tune_fixture(game, self.records[game.instance], 'teleporter', marker=marker,
                         teleporter_exit=source, allow_invalid_exit=True)
        count = counters(self.host)['teleports']
        for player in self.ids[:2]:
            self.owner_game(player).call(f'coop.adventure.world near {marker} {player} 0 0')
        time.sleep(.5)
        invalid = [self.states(player) for player in self.ids[:2]]
        require(counters(self.host)['teleports'] == count and all(
            not value['cooldown'] and distance(value['position'], source)<8 for values in invalid for value in values),
            'invalid authored teleporter exit moved a captain or started a cooldown')
        for player in self.ids[:2]:
            self.owner_game(player).call(f'coop.adventure.world near 2 {player} 0 0')
        for game in self.games:
            tune_fixture(game, self.records[game.instance], 'teleporter', marker=marker, teleporter_exit=destination)
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            contacts = [pool.submit(self.owner_game(player).call,
                        f'coop.adventure.world near {marker} {player} 0 0') for player in self.ids[:2]]
            for contact in contacts:
                contact.result()
        self.host.wait('independent native captain teleports both activate', lambda:
            counters(self.host)['teleports'] == count+2, timeout=3)
        independent = [self.states(player) for player in self.ids[:2]]
        require(all(value['cooldown'] and distance(value['position'], destination)<8
                    for values in independent for value in values),
                'one captain cooldown blocked the other captain teleport')
        self.results.append(dict(kind='teleporter', tuning=tuning, warps=warps,
            invalid_exit=invalid, simultaneous_independent=independent,
            checks=['host native source contact', 'guest native source contact', 'authored exit on every peer', 'other captain stays put',
                    'native cleanup', 'per-captain cooldown', 'teleporter remains available', 'current-time stale-generation movement rejected',
                    'invalid authored exit rejected', 'simultaneous captains use independent cooldowns']))

    def run(self):
        for kind in self.args.cases.split(','):
            print(f'RUN: native {kind} interaction', flush=True)
            if kind == 'health':
                self.health()
            elif kind == 'teleporter':
                self.teleporter()
            else:
                self.upgrade(kind)
            checkpoint = dict(pass_=False, partial=True, completed_cases=[case['kind'] for case in self.results],
                              cases=self.results, note='Per-case checkpoint; final suite acceptance is result.json.')
            (self.args.output/'checkpoint.json').write_text(json.dumps(checkpoint, indent=2)+'\n', encoding='utf-8')
            for game in self.games:
                game.save_log()
            print(f'PASS: native {kind} interaction', flush=True)
        for game in self.games:
            require('total faults: 0' in game.call('exc'), 'native fault during pickup integration tests')
            game.record('pickup-suite-finished')
        return dict(pass_=True, cases=self.results,
            source='native proximity reactions and verified native ability calculator, with recorded isolated profiles')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, default=FIXTURE_OUTPUT)
    parser.add_argument('--instances', default='21,22')
    parser.add_argument('--port', type=int, default=47830)
    parser.add_argument('--max-duration', type=float, default=120)
    parser.add_argument('--cases', default='health,speed,damage,armor,teleporter')
    parser.add_argument('--expected-version', help='require this installed mod version (default: current source)')
    args = parser.parse_args()
    if args.expected_version is None:
        source = Path(__file__).resolve().parents[2] / 'mods/coop/coop.cpp'
        version = re.search(r'^#define COOP_VERSION "([^"]+)"', source.read_text(encoding='utf-8'), re.M)
        require(version, 'current source has no COOP_VERSION; supply --expected-version')
        args.expected_version = version[1]
    instances = tuple(map(int, args.instances.split(',')))
    require(2 <= len(instances) <= 4 and len(set(instances)) == len(instances), 'use two to four distinct lab instances')
    require(all(kind in ('health', 'speed', 'damage', 'armor', 'teleporter') for kind in args.cases.split(',')),
            'cases must be a comma-separated list of health,speed,damage,armor,teleporter')
    records = verify_lab(args.metadata, instances)
    games = [Game(i, args.output, timeout=120) for i in instances]
    result, suite = dict(pass_=False), None
    stack = ExitStack()
    try:
        for game in games:
            require(args.expected_version in game.call('mods'),
                    'install '+args.expected_version+' in the private lab before running this test')
            # Retrying after a failed assertion can leave the native adventure
            # open. Return normally before waiting for registration/menu UI.
            if game.adventure()['active']:
                game.call('coop.adventure cancel')
                menu(game, records[game.instance])
            game.dismiss_registration()
            game.call('coop.leave')
        # Only the host mounts the custom scenario package. Guests receive its
        # root through the real co-op content transfer and reuse stock assets.
        register_fixtures(games[0], records[games[0].instance], args.fixtures)
        games[0].call(f'coop.host {args.port} PickupHost')
        proxies = {}
        for index, game in enumerate(games[1:], 1):
            proxies[game.instance] = stack.enter_context(CaptureProxy(args.port+index, args.port))
            game.call(f'coop.join 127.0.0.1:{args.port+index} PickupGuest{game.instance}')
        games[0].wait('full native test party', lambda: games[0].adventure()['players'] == len(games))
        games[0].call('coop.adventure friendlyfire 100')
        games[0].call('coop.adventure damage 1')
        suite = Suite(args, records, games, proxies)
        result = suite.run()
    except (FixtureError, OSError, ValueError, KeyError) as error:
        result['error'] = str(error)
        if suite:
            result['cases'] = suite.results
        for game in games:
            try:
                game.record('pickup-suite-failure')
            except (FixtureError, OSError, ValueError):
                pass
    finally:
        if suite:
            suite.close()
        stack.close()
        for game in games:
            game.save_log()
        args.output.mkdir(parents=True, exist_ok=True)
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(('PASS' if result['pass_'] else 'FAIL')+': native pickup suite', result.get('error', ''), flush=True)
    return 0 if result['pass_'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
