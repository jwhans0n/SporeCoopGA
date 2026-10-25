#!/usr/bin/env python3
"""Build an immutable one-act Puzzle copy and test native Talk-to-results.

Build is offline. Run/register only consume caller-owned, recorded Windows lab
instances; they do not launch, reload, or terminate processes. No result, goal
callback, shared cached scenario, save, or native cleanup delay is injected.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

from adventure_interaction_fixture import (
    SOURCE, SCENARIO_GROUP, SCENARIO_TYPE, _decode_refpack, _serialized_attributes,
    _register_fixture_package, _verify_game, build_package, inspect_resource,
)
from adventure_memory import AdventureMemory
from coop_adventure import FixtureError, Game, verify_lab
from coop_adventure_flow import check_intro, confirmation_hud
from coop_adventure_lobby import click_named, lobby

ROOT = Path(__file__).resolve().parents[2]
KEY = (0x7F008101, SCENARIO_TYPE, SCENARIO_GROUP)
OUTPUT = ROOT / 'out/lab/preview8-terminal-talk-fixture'
SOURCE_SHA256 = '2b0f3f9b4255e96cc27dee6bdfe29960e26d5b715b73efe730bb4e700f83090a'
ACTS, GOALS, LOCK, AVATAR = 0x07102E6A, 0x0711477B, 0x07734A68, 0x07102E78


def sha(data):
    return hashlib.sha256(data).hexdigest()


def immutable(path, data):
    if path.exists() and path.read_bytes() != data:
        raise FixtureError('immutable fixture path already contains different data: ' + str(path))
    if not path.exists():
        path.write_bytes(data)


def encode_attributes(original, values):
    attributes, end = _serialized_attributes(original, 0)
    if end != len(original) or set(values) - set(attributes):
        raise FixtureError('fixture serializer received an unexpected native schema')
    payloads = [(key, values.get(key, original[position:position + length]))
                for key, (position, length) in attributes.items()]
    body = struct.pack('>I', len(payloads))
    body += b''.join(struct.pack('>II', key, len(value)) for key, value in payloads)
    body += b''.join(value for _, value in payloads)
    return struct.pack('>II', 0x01A80D26, len(body)) + body


def audit_package(path, key, payload):
    blob = path.read_bytes()
    if blob[:4] != b'DBPF' or struct.unpack_from('<I', blob, 0x24)[0] != 1:
        raise FixtureError('immutable fixture package has an unexpected record count')
    index = struct.unpack_from('<I', blob, 0x40)[0]
    flags, high = struct.unpack_from('<II', blob, index)
    typ, group, instance, offset, size, unpacked, compression, used = struct.unpack_from('<6I2H', blob, index + 8)
    if ((flags, high) != (4, 0) or (instance, typ, group) != key or
            size & 0x7FFFFFFF != len(payload) or unpacked != len(payload) or compression or used != 1 or
            blob[offset:offset + len(payload)] != payload):
        raise FixtureError('immutable package payload differs from its expected native resource')


def build(output=OUTPUT):
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = SOURCE.read_bytes()
    if sha(source) != SOURCE_SHA256 or source.count(b'DBPF') != 1:
        raise FixtureError('terminal Talk requires the exact reviewed copied Mr Puzzle source')
    base = source.index(b'DBPF')
    inner = source[base:]
    index = struct.unpack_from('<I', inner, 0x40)[0]
    count, index_size = (struct.unpack_from('<I', inner, offset)[0] for offset in (0x24, 0x2C))
    flags, typ, group, high, instance, offset, size, unpacked, compression, used = struct.unpack_from('<8I2H', inner, index)
    if ((count, index_size) != (1, 36) or struct.unpack_from('<I', source, base - 4)[0] != len(inner) or
            (flags, typ, group, high, instance, compression, used) !=
            (7, 0x01897C18, 0x01897C18, 0, 0x01897C18, 0xFFFF, 1)):
        raise FixtureError('copied Puzzle embedded database differs from its reviewed layout')
    original = bytes(_decode_refpack(inner[offset:offset + (size & 0x7FFFFFFF)]))
    if len(original) != unpacked:
        raise FixtureError('copied Puzzle decoded length differs from its index')
    attributes, end = _serialized_attributes(original, 0)
    if end != len(original):
        raise FixtureError('copied Puzzle serialized record has trailing data')
    acts, acts_size = attributes[ACTS]
    if struct.unpack_from('>I', original, acts)[0] != 4:
        raise FixtureError('copied Puzzle must have four authored acts')
    first_act, first_end = _serialized_attributes(original, acts + 4)
    goals, goals_size = first_act[GOALS]
    goal, goal_end = _serialized_attributes(original, goals + 4)
    if struct.unpack_from('>I', original, goals)[0] != 1 or goal_end != goals + goals_size:
        raise FixtureError('first source act must have exactly one original Talk goal')
    expected = {0x07114B5F: 2, 0x07114B60: 1037, 0x071D3B1D: 1}
    if any(length != 4 or struct.unpack_from('>I', original, position)[0] != expected[key]
           for key in expected for position, length in (goal[key],)):
        raise FixtureError('first source goal differs from its reviewed Talk/class/count')
    if original[attributes[LOCK][0]:attributes[LOCK][0] + attributes[LOCK][1]] != b'\x01':
        raise FixtureError('copied Puzzle must retain its authored captain lock')
    first_bytes = original[acts + 4:first_end]
    decoded = encode_attributes(original, {ACTS: struct.pack('>I', 1) + first_bytes})
    replacement, replacement_end = _serialized_attributes(decoded, 0)
    if replacement_end != len(decoded):
        raise FixtureError('new serialized root is not bounded exactly')
    for key, (position, length) in attributes.items():
        other, other_length = replacement[key]
        if key != ACTS and original[position:position + length] != decoded[other:other + other_length]:
            raise FixtureError('fixture changed an attribute besides the root act array')
    trimmed_acts, trimmed_size = replacement[ACTS]
    if (struct.unpack_from('>I', decoded, trimmed_acts)[0] != 1 or
            decoded[trimmed_acts + 4:trimmed_acts + trimmed_size] != first_bytes):
        raise FixtureError('fixture changed the original first act bytes')
    decoded_path = output / f'decoded-{sha(decoded)[:16]}.bin'
    immutable(decoded_path, decoded)
    embedded_path = output / f'embedded-{sha(decoded)[:16]}.package'
    if not embedded_path.exists():
        build_package(str(embedded_path), [f'0x{group:08X}!0x{instance:08X}.0x{typ:08X}={decoded_path}'])
    audit_package(embedded_path, (instance, typ, group), decoded)
    embedded = embedded_path.read_bytes()
    scenario = bytearray(source[:base])
    struct.pack_into('<I', scenario, base - 4, len(embedded))
    scenario += embedded
    scenario_path = output / f'{KEY[0]:08X}-{sha(scenario)[:16]}.bin'
    immutable(scenario_path, scenario)
    package = output / f'SporeCoopTerminalTalk-{sha(scenario)[:16]}.package'
    if not package.exists():
        build_package(str(package), [f'0x{KEY[2]:08X}!0x{KEY[0]:08X}.0x{KEY[1]:08X}={scenario_path}'])
    audit_package(package, KEY, scenario)
    avatar_at, avatar_size = attributes[AVATAR]
    manifest = dict(key=list(KEY), package=str(package), package_sha256=sha(package.read_bytes()),
                    scenario=str(scenario_path), scenario_sha256=sha(scenario), source=str(SOURCE),
                    source_sha256=sha(source), decoded_sha256=sha(decoded), original_decoded_sha256=sha(original),
                    first_act_sha256=sha(first_bytes), original_goal_sha256=sha(original[goals + 4:goal_end]),
                    authored_acts_before=4, authored_acts_after=1, captain_locked=True,
                    captain_asset_sha256=sha(original[avatar_at:avatar_at + avatar_size]),
                    goal=dict(type=2, target_class=1037, required_count=1, native_marker=158),
                    mutation='Only root act-array count/payload and required container lengths changed; first act and every other root attribute are byte-identical.',
                    dependencies='Original installed Puzzle dependencies and locked captain; no dependency keys changed.',
                    native_parser_verified=False, native_terminal_talk_verified=False,
                    delayed_cleanup_forced=False,
                    limitation='No existing bounded lab-only cleanup-delay instrumentation is available. Native run uses guest-first Finished; deterministic delayed-end ordering is covered by runtime/flow fixtures.')
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    return manifest


def register(games, records, fixture, directory):
    inspections = []
    for game in games:
        _verify_game(game, records[game.instance], menu=True)
        _register_fixture_package(game, records[game.instance], fixture, directory, KEY)
        inspected = inspect_resource(game, records[game.instance], KEY)
        expected = fixture['goal']
        goals = inspected['authored_acts'][0]['goals'] if len(inspected['authored_acts']) == 1 else []
        if len(goals) != 1 or any(goals[0][name] != expected[name] for name in ('type', 'target_class', 'required_count')):
            raise FixtureError('native parser did not retain the single original Talk goal')
        inspections.append(inspected)
    return inspections


def finish_pages(game):
    deadline = time.monotonic() + 90
    while time.monotonic() < deadline:
        if game.control(0x0445C99D):
            game.record('terminal-talk-final-page')
            game.click_control(0x0445C99D)
            return
        if game.control(0x062EF353):
            game.click_control(0x062EF353)
        else:
            time.sleep(.25)
    raise FixtureError('native Talk did not reach its original Finished control')


def run(games, records, fixture, args):
    host, guest = games
    result = dict(result='FAIL', profiles=records, fixture=fixture, delayed_cleanup_forced=False)
    memories = []
    try:
        if not args.resume:
            result['native_parses'] = register(games, records, fixture, args.fixture)
            if any(game.adventure()['players'] != 2 for game in games):
                raise FixtureError('caller must supply an existing connected two-player party in native menus')
            host.call('coop.adventure start ' + ' '.join(f'0x{part:08X}' for part in KEY))
            for game in games:
                state = lobby(game, 2)
                if 'change_enabled=0' not in state:
                    raise FixtureError('immutable fixture lost its authored captain restriction')
            click_named(guest, 'prep_ready'); click_named(host, 'prep_ready')
        memories = [AdventureMemory(game, records[game.instance]) for game in games]
        if not args.resume:
            result['intro'] = check_intro(games, memories)
        before = [memory.goals() for memory in memories]
        if any(state['act'] != 0 or len(state['goals']) != 1 or state['goals'][0]['type'] != 2 or
               state['goals'][0]['primary'] != 1037 or state['goals'][0]['complete'] for state in before):
            raise FixtureError('native fixture is not awaiting its original first Talk goal')
        result['before'] = before
        guest.click(args.x, args.y)
        for game in games:
            game.wait('shared original NPC conversation', lambda game=game: game.caption('letterbox_top'), timeout=15)
        finish_pages(guest)
        host.wait('guest-only Talk confirmation', lambda: 'Conversation confirmations: 1/2' in host.adventure()['text'])
        frozen = [memory.goals() for memory in memories]
        time.sleep(2)
        if [memory.goals() for memory in memories] != frozen:
            raise FixtureError('guest Finished advanced terminal goal or clocks before host confirmation')
        confirmation_hud(guest, 2, 'Conversation', True)
        if not guest.caption('letterbox_top'):
            raise FixtureError('guest Finished retired its native dialog before group release')
        result['guest_waiting'] = frozen
        if args.check_f9:
            guest.call('key f9')
            guest.wait('F9 opens party controls while locally confirmed', lambda: guest.call('coop.uistate').startswith('open=1 '))
            guest.record('terminal-talk-confirmed-f9')
            if 'Conversation confirmations: 1/2' not in host.adventure()['text']:
                raise FixtureError('F9 changed the existing Talk confirmation')
            guest.call('key f9')
            guest.wait('F9 returns to the retained native Talk', lambda: guest.call('coop.uistate').startswith('open=0 '))
            confirmation_hud(guest, 2, 'Conversation', True)
            if [memory.goals() for memory in memories] != frozen:
                raise FixtureError('confirmed F9 navigation advanced clocks or the terminal goal')
            result['confirmed_f9'] = 'PASS'
        finish_pages(host)
        for game, memory in zip(games, memories):
            game.wait('original native terminal Talk reaches success state', lambda memory=memory: memory.goals()['state'] == 5, timeout=25)
            def results_ready(game=game):
                if game.caption('Mission Complete!'):
                    return True
                if game.control(0x0445C99D):
                    game.click_control(0x0445C99D)
                return False
            game.wait('real native Mission Complete results after terminal Talk', results_ready, timeout=35)
            status = game.adventure()['text']
            if 'applied=5' not in status or 'suppressed score submissions=1' not in status:
                raise FixtureError('native terminal result was not applied exactly once: ' + status)
            if 'total faults: 0' not in game.call('exc'):
                raise FixtureError('terminal Talk/result transition recorded a native exception')
            game.record('terminal-talk-results')
        final = [memory.goals() for memory in memories]
        progress = [[(goal['complete'], goal['count']) for goal in state['goals']] for state in final]
        if progress[0] != progress[1] or progress[0] != [(True, 1)]:
            raise FixtureError('terminal success did not publish matching final goal counts: ' + json.dumps(final))
        result.update(result='PASS', after=final,
                      status=[game.adventure() for game in games],
                      source='Ordinary NPC body click and native Next/Finished/ending controls; no injected goal or result.',
                      limitation=fixture['limitation'])
    except (FixtureError, OSError, ValueError) as error:
        result['error'] = str(error)
        for game in games:
            try:
                game.record('terminal-talk-failure')
            except (FixtureError, OSError, ValueError):
                pass
    finally:
        for memory in memories:
            memory.close()
        for game in games:
            game.save_log()
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('build', 'register', 'run'))
    parser.add_argument('--fixture', type=Path, default=OUTPUT)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/lab/preview8-terminal-talk')
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--instances', default='61,62')
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--check-f9', action='store_true')
    parser.add_argument('--x', type=int, default=420)
    parser.add_argument('--y', type=int, default=320)
    args = parser.parse_args()
    fixture = build(args.fixture)
    if args.action == 'build':
        print(json.dumps(fixture, indent=2))
        return 0
    if not args.metadata:
        parser.error('--metadata is required before connecting to caller-owned lab games')
    instances = tuple(map(int, args.instances.split(',')))
    if len(instances) != 2 or len(set(instances)) != 2:
        parser.error('--instances requires two distinct caller-owned lab IDs')
    args.output.mkdir(parents=True, exist_ok=True)
    records = verify_lab(args.metadata, instances)
    games = [Game(instance, args.output) for instance in instances]
    if args.action == 'register':
        try:
            result = register(games, records, fixture, args.fixture)
            (args.output / 'native-parses.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
            print('PASS: private one-act fixture registered and parsed; no adventure started')
            return 0
        finally:
            for game in games:
                game.save_log()
    result = run(games, records, fixture, args)
    print(json.dumps({key: result[key] for key in ('result', 'error') if key in result}))
    return 0 if result['result'] == 'PASS' else 1


if __name__ == '__main__':
    raise SystemExit(main())
