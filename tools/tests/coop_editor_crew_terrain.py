#!/usr/bin/env python3
"""Crew/Terraform UI checks on an already-owned hidden Windows editor party.

Preparation opens no connections. Execution needs exact lab ownership, candidate
hash and a measured UI recipe. Probe mode records public diagnostics only. The
fixture never launches/deploys games, calls native functions, writes memory, or
changes profile databases. Every accepted edit must produce one transaction and
Undo item, converge natively, and survive Undo/Redo through normal shortcuts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import time

from coop_adventure import FixtureError
from coop_adventure_editor import EditorPair, status
from coop_editor_creations import input_steps, validate_steps, verify_viewport
from coop_editor_party import Ownership, PublicConsole, PublicGame, parse_history
from sporectl import Console

FAMILIES = (
    'crew_capacity', 'crew_add', 'crew_replace', 'crew_remove', 'crew_lock',
    'sculpt', 'paint', 'smooth', 'erase', 'stamp_move', 'stamp_scale', 'stamp_rotate',
    'stamp_delete', 'water', 'atmosphere', 'ground', 'hazards', 'colors', 'gradient',
    'flora_add', 'flora_remove', 'flora_density', 'ground_effect', 'time',
    'day_night', 'visual_style', 'planet_preset',
)
RECIPE_FIELDS = {0x02A907B5, 0x02A907B6, 0x03A23F97, 0x03DD8E59, 0x03DD8E5A,
                 0x7A6AA70B, 0x03A90C57, 0x03A90C5F}
PLANET_FIELDS = {0x00B2CCCB, 0x02A907B7, 0x03A23F98, 0x03A23F99, 0x03ABB0AF,
                 0x04E13CB7, 0x0536250C, 0x0536250D, 0x072BEC91, 0x073137BB,
                 0x073154E0, 0x07B18058, 0x0B208B41, 0x48C74C5C, 0x56B14F05,
                 0x8E390196, 0x968C496A, 0x968C496B, 0xB6929C92, 0xB6929C93,
                 0xB6929C94, 0xB6929C95, 0xD2F669EC}


class NeedMore(Exception):
    pass


def planet_prefix(data):
    """Decode only the verified property prefix, never the adventure DBPF body."""
    def take(at, count):
        if at+count > len(data):
            raise NeedMore()
        return data[at:at+count]
    if struct.unpack('<II', take(0, 8)) != (0xB523D4F9, 17):
        raise FixtureError('The native scenario header is not the verified version17')
    count, = struct.unpack('>I', take(8, 4))
    if count > 4096:
        raise FixtureError('Native terrain property count exceeds its verified bound')
    at, result = 12, {}
    sizes = {0:16, 0x20:16, 0x31:16, 0x32:16, 0x33:16, 0x34:16,
             1:1, 2:1, 5:1, 6:1, 3:2, 7:2, 8:2, 4:4, 9:4, 0xA:4,
             0xD:4, 0xB:8, 0xC:8, 0xE:8, 0x30:8, 0x38:56}
    for _ in range(count):
        ident, kind, flags = struct.unpack('>IHH', take(at, 8));at += 8
        begin = at
        if ident in result:
            raise FixtureError('Duplicate native planet property')
        if flags & 0x80:
            items, width = struct.unpack('>II', take(at, 8));at += 8
            if items > 8192 or width > 32 << 20:
                raise FixtureError('Native planet array exceeds its verified bound')
            if kind in (0x12, 0x13):
                for _ in range(items):
                    chars, = struct.unpack('>I', take(at, 4));at += 4
                    length = chars*(2 if kind == 0x13 else 1)
                    take(at, length);at += length
            else:
                take(at, items*width);at += items*width
        elif kind in (0x12, 0x13):
            chars, = struct.unpack('>I', take(at, 4));at += 4
            length = chars*(2 if kind == 0x13 else 1)
            take(at, length);at += length
        else:
            if kind not in sizes:
                raise FixtureError('Unsupported native planet scalar shape')
            take(at, sizes[kind]);at += sizes[kind]
        value = bytearray(data[begin:at])
        if not flags & 0x80 and kind in (0x20, 0x31, 0x32) and len(value) == 16:
            value[12:16] = bytes(4)
        result[ident] = (kind, flags, value.hex())
    return result, at


class TerrainConsole(PublicConsole):
    def call(self, line):
        if re.fullmatch(r'coop.editor inspect bytes [0-9]{1,6} 2048', line):
            if time.monotonic() > self.deadline:
                raise FixtureError('The bounded Crew/Terraform fixture expired')
            return Console.call(self, line)
        return super().call(line)


def native_planet(game):
    data, identity = bytearray(), None
    # Console replies are bounded to8192 characters. A4096-byte hex page plus
    # its header is truncated, so use2048-byte pages with exact count checks.
    for offset in range(0, 262144, 2048):
        page = game.call(f'coop.editor inspect bytes {offset} 2048')
        found = re.fullmatch(r'NATIVE_BYTES size=(\d+) offset=(\d+) count=(\d+) sha256=([0-9a-f]{64})\nhex=([0-9a-f]*)\s*', page)
        if not found:
            raise FixtureError('Native planet inspection is unavailable: '+page)
        size, actual, count = map(int, found.groups()[:3])
        if actual != offset or count != len(found[5])//2 or count > 2048:
            raise FixtureError('Malformed native byte page')
        stamp = (size, found[4])
        if identity is not None and stamp != identity:
            raise FixtureError('Native snapshot changed during property pagination')
        identity = stamp;data.extend(bytes.fromhex(found[5]))
        try:
            fields, end = planet_prefix(data)
            return dict(fields=fields, prefix_bytes=end,
                        prefix_sha256=hashlib.sha256(data[:end]).hexdigest())
        except NeedMore:
            if not count or len(data) == size:
                raise FixtureError('The native planet property stream is truncated')
    raise FixtureError('This focused fixture needs a planet prefix under256KiB')


def semantic(game):
    text = game.call('coop.editor inspect')
    found = re.search(r'\bdocument_semantic_sha256=([0-9a-f]{64})\b', text)
    if not found:
        raise FixtureError('Native semantic projection failed: '+text)
    return found[1]


def recipes(path):
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    viewport = data.get('viewport')
    if data.get('schema') != 1 or viewport != [1024, 768]:
        raise ValueError('A schema1 recipe measured at1024x768 is required')
    cases = data.get('cases')
    if not isinstance(cases, list) or not 1 <= len(cases) <= 80:
        raise ValueError('A focused recipe needs1..80 cases')
    labels = set()
    for case in cases:
        if not isinstance(case, dict) or not re.fullmatch('[a-z0-9_-]{1,64}', case.get('label', '')):
            raise ValueError('Every case needs a bounded stable label')
        if case['label'] in labels or case.get('family') not in FAMILIES:
            raise ValueError('Duplicate case label or unknown control family')
        labels.add(case['label'])
        validate_steps(case.get('action'))
        if case.get('setup'):
            validate_steps(case['setup'])
        for steps in (case.get('setup', []), case['action']):
            for step in steps:
                action, value = next(iter(step.items()))
                if action in ('click', 'drag') and any(value[i] >= viewport[i%2] for i in range(len(value))):
                    raise ValueError('A measured input leaves its owned viewport')
        fields = case.get('planet_fields', [])
        if not isinstance(fields, list) or any(not isinstance(v, str) or not re.fullmatch('[0-9a-fA-F]{8}', v) for v in fields):
            raise ValueError('Expected planet fields must be eight-digit hexadecimal IDs')
        if type(case.get('recipe_change', False)) is not bool:
            raise ValueError('recipe_change must be Boolean')
        if case['family'].startswith('crew_') and (fields or case.get('recipe_change')):
            raise ValueError('Crew cases cannot claim terrain mutations')
        if not case['family'].startswith('crew_') and not fields and not case.get('recipe_change'):
            raise ValueError('Terraform cases must name their observed native property or recipe change')
    return data


def run(args):
    recipe = recipes(args.recipes) if args.recipes else dict(viewport=[1024, 768], cases=[])
    args.output.mkdir(parents=True, exist_ok=False)
    owner = Ownership(args.metadata, args.instances, args.dll_sha256)
    deadline = time.monotonic()+args.deadline
    games = [PublicGame(i, args.output, args.timeout, deadline) for i in args.instances]
    for game in games:
        game.console = TerrainConsole(game.instance, deadline)
    host = games[0]
    result = dict(passed=False, complete_acceptance=False, candidate_sha256=args.dll_sha256,
                  probe=args.probe, checks=[], coverage=[], recipe=recipe)

    def write():
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')

    def settled(revision=None):
        rows = [status(game) for game in games]
        if any(row.get('frozen') for row in rows):
            raise FixtureError('A Crew/Terraform edit froze a participant')
        if revision is not None and any(row.get('revision', 0) > revision for row in rows):
            raise FixtureError('One UI action produced duplicate synchronized transactions')
        if not all(row.get('phase') == 4 and row.get('ready') and not row.get('gesture') and
                   row.get('revision') == row.get('durable') for row in rows):
            return False
        if len({row['revision'] for row in rows}) != 1 or revision is not None and rows[0]['revision'] != revision:
            return False
        checks = [game.call('coop.editor check').strip() for game in games]
        return rows[0]['revision'] if checks[0].startswith('CONVERGED ') and len(set(checks)) == 1 else False

    def record(label):
        owner.verify_windows();participants = []
        for game in games:
            faults = game.call('exc')
            if 'total faults: 0' not in faults:
                raise FixtureError('Native fault during '+label+': '+faults)
            game.record(label)
            participants.append(dict(instance=game.instance, status=status(game),
                                     inspect=game.call('coop.editor inspect'),
                                     history=game.call('coop.editor history'),
                                     leases=game.call('coop.editor leases'), faults=faults))
        result['checks'].append(dict(label=label, participants=participants));write()

    try:
        result['owned_windows'] = verify_viewport(owner, games, recipe['viewport'])
        # Revision zero is a valid baseline; return its value through a tuple.
        host.wait('initial durable editor party', lambda: (settled() is not False))
        record('baseline')
        if args.probe:
            result['passed'] = True;return
        for case in recipe['cases']:
            if args.only and case['label'] not in args.only:
                continue
            for author in games[:2]:
                label = f'{case["label"]}-{author.instance}'
                revision = status(host)['revision']
                if case.get('setup'):
                    input_steps(author, case['setup'], label)
                    if status(host)['revision'] != revision:
                        raise FixtureError('Navigation setup changed the shared draft: '+label)
                before_semantic = semantic(host);before = native_planet(host)
                history = parse_history(host.call('coop.editor history'))
                owner.verify_windows();input_steps(author, case['action'], label)
                host.wait(label, lambda: settled(revision+1))
                after_history = parse_history(host.call('coop.editor history'))
                if len(after_history) != len(history)+1:
                    raise FixtureError(label+' did not create one shared Undo item')
                after_semantic = semantic(host);after = native_planet(host)
                if after_semantic == before_semantic:
                    raise FixtureError('The expected native edit had no authored semantic effect')
                changed = {key for key in set(before['fields'])|set(after['fields'])
                           if before['fields'].get(key) != after['fields'].get(key)}
                crew = case['family'].startswith('crew_')
                expected = {int(value, 16) for value in case.get('planet_fields', [])}
                if crew and (changed or after_history[-1][-1] != 'Edit adventure crew'):
                    raise FixtureError('The Crew UI case changed another authoring family')
                if not crew:
                    if changed-(PLANET_FIELDS|RECIPE_FIELDS) or expected and not expected <= changed:
                        raise FixtureError('The native control did not change its expected planet fields')
                    if bool(changed&RECIPE_FIELDS) != bool(case.get('recipe_change', False)):
                        raise FixtureError('The native recipe change did not match its measured control family')
                    if after_history[-1][-1] not in ('Edit planet settings', 'Terraform adventure'):
                        raise FixtureError('The Terraform input committed an unrelated authoring family')
                record(label+'-committed')
                for operation, expected_fields, expected_semantic in (
                        ('z', before['fields'], before_semantic), ('y', after['fields'], after_semantic),
                        ('z', before['fields'], before_semantic)):
                    revision += 1;EditorPair.hotkey(author, operation)
                    host.wait(label+' native history', lambda r=revision+1: settled(r))
                    if native_planet(host)['fields'] != expected_fields:
                        raise FixtureError('Native terrain Undo/Redo changed unrelated authored values')
                    if crew and semantic(host) != expected_semantic:
                        raise FixtureError('Crew Undo/Redo changed unrelated authored values')
                result['coverage'].append(dict(label=label, family=case['family'], author=author.instance,
                                                changed_planet_fields=[f'{key:08X}' for key in sorted(changed)]))
                record(label+'-restored')
        covered = {(row['family'], row['author']) for row in result['coverage']}
        result['missing_family_authors'] = [dict(family=family, author=game.instance)
            for family in FAMILIES for game in games[:2] if (family, game.instance) not in covered]
        result['passed'] = bool(result['coverage'])
        result['full_control_family_pass'] = result['passed'] and not result['missing_family_authors']
    except Exception as error:
        result['error'] = repr(error);raise
    finally:
        write()
        for game in games:
            game.save_log()
        owner.close()


def self_test(root):
    data = (root/'out/editor-terrain-root.bin').read_bytes()
    fields, end = planet_prefix(data)
    assert len(fields) == 30 and end == 14262
    assert struct.unpack('>I', bytes.fromhex(fields[0x02A907B5][2])[:4])[0] == 162
    try:
        planet_prefix(data[:20])
    except NeedMore:
        pass
    else:
        raise AssertionError('Truncated planet property prefix accepted')
    print('Crew/Terraform fixture codec: genuine native prefix30 properties,162 records,14262 bytes')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true');parser.add_argument('--probe', action='store_true')
    parser.add_argument('--self-test', action='store_true');parser.add_argument('--metadata', type=Path)
    parser.add_argument('--instances', type=int, nargs='+', default=[95, 96])
    parser.add_argument('--dll-sha256');parser.add_argument('--recipes', type=Path)
    parser.add_argument('--output', type=Path);parser.add_argument('--only', nargs='+')
    parser.add_argument('--timeout', type=int, default=120);parser.add_argument('--deadline', type=int, default=3600)
    args = parser.parse_args()
    if args.self_test:
        self_test(Path(__file__).resolve().parents[2]);return
    if not args.execute:
        print(json.dumps(dict(prepared=True, launches_games=False, required_families=FAMILIES,
                              recipe_schema=1, viewport=[1024, 768], normal_input_only=True)));return
    if (not args.metadata or not args.output or not args.probe and not args.recipes or
            len(args.instances) not in (2, 4) or len(set(args.instances)) != len(args.instances) or
            not re.fullmatch('[0-9a-f]{64}', args.dll_sha256 or '') or
            not 30 <= args.timeout <= 300 or not 60 <= args.deadline <= 7200):
        parser.error('Exact private metadata, frozen DLL hash, measured recipe and bounded2/4-player arguments required')
    run(args)


if __name__ == '__main__':
    main()
