#!/usr/bin/env python3
"""Build and inspect native interaction adventures in recorded Windows lab profiles.

The package contains copies of the installed Mr Puzzle scenario with only its
class-zero palette asset changed. Profiles and saved creations are never edited.
Registration and runtime tuning require verify_lab metadata and the reviewed dc04
executable. Native calls always use reviewed symbols or explicit hexadecimal
virtual-function addresses. This helper never starts an adventure or a party.
"""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from mkpackage import build as build_package  # noqa: E402
from coop_adventure import Game, FixtureError, verify_lab  # noqa: E402
from adventure_memory import AdventureMemory  # noqa: E402

OUTPUT = ROOT / 'out' / 'preview6-fixtures'
EXE_SHA256 = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
SCENARIO_TYPE, SCENARIO_GROUP = 0x366A930D, 0x408A0000
PALETTE_GROUP, PROP_TYPE = 0xD87454E6, 0x00B1B104
SOURCE = ROOT / 'out' / 'stock-adventures' / 'C2C7B154.bin'
SOURCE_ASSET = 0x34FB4AB0
CRATE_GOAL_KEY = (0x7F00600E, SCENARIO_TYPE, SCENARIO_GROUP)
FIXTURES = {
    'mine': (0x7F006001, 0xF70AB170, 6),
    'barrel': (0x7F006002, 0xA0EBD95A, 15),
    'crate': (0x7F006003, 0x93512140, 15),
    'bomb_long': (0x7F006004, 0xD41F216B, 10),
    'bomb': (0x7F006005, 0xC7C9CC8D, 10),
    'bomb_short': (0x7F006006, 0x48B8E425, 10),
    'grenade': (0x7F006007, 0x812057C5, 13),
    'health': (0x7F006008, 0x71B5854F, 1),
    'energy': (0x7F006009, 0x56CCE16B, 2),
    'speed': (0x7F00600A, 0xB50BD5CF, 3),
    'damage': (0x7F00600B, 0xA340633B, 4),
    'armor': (0x7F00600C, 0xA880F62B, 5),
    'teleporter': (0x7F00600D, 0x90C48D09, 8),
}


def fixture_key(kind):
    return FIXTURES[kind][0], SCENARIO_TYPE, SCENARIO_GROUP


def _sha(data):
    return hashlib.sha256(data).hexdigest()


def fixture_defaults(kind):
    """Read installed palette scalars from the prior offline package index."""
    index = ROOT / 'out' / 'party-flow-re' / 'powerup-properties.json'
    if not index.exists():
        return {}
    record = next((entry for entry in json.loads(index.read_text()) if entry['instance'] == FIXTURES[kind][1]), None)
    if not record:
        return {}
    with open(record['package'], 'rb') as source:
        source.seek(record['offset']); data = source.read(record['size'])
    if record['compression'] == 65535:
        if data[1] != 0xFB:
            raise FixtureError('palette record is not the indexed RefPack data')
        size = int.from_bytes(data[2:6] if data[0] & 128 else data[2:5], 'big')
        position, decoded = (6 if data[0] & 128 else 5), bytearray()
        while position < len(data):
            code = data[position]; position += 1
            if code < 128:
                extra = data[position]; position += 1
                literals, count, distance = code & 3, ((code & 28) >> 2) + 3, ((code & 96) << 3) + extra + 1
            elif code < 192:
                first, second = data[position:position + 2]; position += 2
                literals, count, distance = first >> 6, (code & 63) + 4, ((first & 63) << 8) + second + 1
            elif code < 224:
                first, second, third = data[position:position + 3]; position += 3
                literals, count, distance = code & 3, ((code & 12) << 6) + third + 5, ((code & 16) << 12) + (first << 8) + second + 1
            else:
                literals = ((code & 31) << 2) + 4 if code < 252 else code & 3
                decoded += data[position:position + literals]; position += literals
                if code >= 252:
                    break
                continue
            decoded += data[position:position + literals]; position += literals
            for _ in range(count):
                decoded.append(decoded[-distance])
        if len(decoded) != size:
            raise FixtureError('decoded palette length differs from its RefPack header')
        data = bytes(decoded)
    result = dict(palette_sha256=_sha(data), powerup=FIXTURES[kind][2])
    for name, ident in (('duration', 0xAAC3B137), ('amount', 0x6219412B)):
        needle = struct.pack('>IHH', ident, 0x000D, 0)
        if data.count(needle) == 1:
            result[name] = struct.unpack_from('>f', data, data.index(needle) + 8)[0]
    return result


def _json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def build_fixtures(output=OUTPUT):
    """Create immutable packages and a manifest, without connecting to a game."""
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    source = SOURCE.read_bytes()
    needle = struct.pack('>III', PALETTE_GROUP, PROP_TYPE, SOURCE_ASSET)
    if source.count(needle) != 1:
        raise FixtureError('source scenario must contain exactly one literal class-zero gate asset')
    offset = source.index(needle) + 8
    specs, entries = [], {}
    digest = hashlib.sha256()
    for kind, (instance, asset, powerup) in FIXTURES.items():
        data = bytearray(source)
        struct.pack_into('>I', data, offset, asset)
        if bytes(data[:offset]) != source[:offset] or bytes(data[offset + 4:]) != source[offset + 4:]:
            raise FixtureError('fixture patch changed more than the reviewed palette instance field')
        path = output / f'{instance:08X}-{_sha(data)[:12]}.bin'
        if path.exists() and path.read_bytes() != data:
            raise FixtureError('immutable fixture scenario already exists with different bytes')
        if not path.exists():
            path.write_bytes(data)
        specs.append(f'0x{SCENARIO_GROUP:08X}!0x{instance:08X}.0x{SCENARIO_TYPE:08X}={path}')
        digest.update(struct.pack('<I', instance)); digest.update(data)
        entries[kind] = dict(instance=instance, type=SCENARIO_TYPE, group=SCENARIO_GROUP,
                             palette_asset=asset, powerup=powerup, class_index=0,
                             primary_marker=108, scenario=str(path), sha256=_sha(data),
                             tuning='copied from source; inspect native class acts before testing')
        entries[kind]['palette_defaults'] = fixture_defaults(kind)
        if kind == 'crate':
            entries[kind]['runtime_defaults'] = dict(amount=0.0,
                reason='Source gate class act has amount10; tune_fixture restores the stock harmless crate default.')
        entries[kind]['visuals'] = 'Source class appearance overrides are retained; tune_fixture selects authored marker exit for teleporters.'
    package = output / f'SporeCoopInteractionFixtures-{digest.hexdigest()[:16]}.package'
    if not package.exists():
        build_package(str(package), specs)
    # Independently check the DBPF record table and every registered payload.
    blob = package.read_bytes()
    if blob[:4] != b'DBPF' or struct.unpack_from('<I', blob, 0x24)[0] != len(entries):
        raise FixtureError('fixture package header or record count is invalid')
    table = struct.unpack_from('<I', blob, 0x40)[0] + 8
    for index, entry in enumerate(entries.values()):
        typ, group, instance, position, size, usize, compression, flags = struct.unpack_from('<IIIIIIHH', blob, table + index * 28)
        expected = Path(entry['scenario']).read_bytes()
        if ((instance, typ, group) != (entry['instance'], entry['type'], entry['group']) or
                usize != len(expected) or size & 0x7FFFFFFF != usize or compression or flags != 1 or
                blob[position:position + usize] != expected):
            raise FixtureError('fixture package record differs from its manifest')
    assets = {name: value[1] for name, value in FIXTURES.items()}
    stock = []
    for path in sorted(SOURCE.parent.glob('*.bin')):
        data = path.read_bytes()
        found = {name: data.count(struct.pack('>I', asset)) for name, asset in assets.items()
                 if struct.pack('>I', asset) in data}
        if found:
            stock.append(dict(instance=int(path.stem, 16), literal_asset_references=found))
    stock.sort(key=lambda entry: -len(entry['literal_asset_references']))
    manifest = dict(source=str(SOURCE), source_sha256=_sha(source), source_asset=SOURCE_ASSET,
                    patch_offset=offset, package=str(package), package_sha256=_sha(blob),
                    fixtures=entries, stock_reference_scan=stock,
                    note='Stock scan finds serialized literal references, not verified runtime class or actor counts.')
    _json(output / 'manifest.json', manifest)
    return manifest


def _decode_refpack(data):
    if len(data) < 5 or data[1] != 0xFB:
        raise FixtureError('embedded scenario record is not RefPack')
    position = 6 if data[0] & 128 else 5
    size = int.from_bytes(data[2:position], 'big')
    if not 0 < size <= 16 * 1024 * 1024:
        raise FixtureError('embedded scenario record exceeds the fixture size bound')
    decoded = bytearray()
    while position < len(data):
        code = data[position]; position += 1
        if code < 128:
            extra = data[position]; position += 1
            literals, count, distance = code & 3, ((code & 28) >> 2) + 3, ((code & 96) << 3) + extra + 1
        elif code < 192:
            first, second = data[position:position + 2]; position += 2
            literals, count, distance = first >> 6, (code & 63) + 4, ((first & 63) << 8) + second + 1
        elif code < 224:
            first, second, third = data[position:position + 3]; position += 3
            literals, count, distance = code & 3, ((code & 12) << 6) + third + 5, ((code & 16) << 12) + (first << 8) + second + 1
        else:
            literals = ((code & 31) << 2) + 4 if code < 252 else code & 3
            if position + literals > len(data) or len(decoded) + literals > size:
                raise FixtureError('truncated or oversized embedded scenario literals')
            decoded += data[position:position + literals]; position += literals
            if code >= 252:
                break
            continue
        if position + literals > len(data) or len(decoded) + literals + count > size:
            raise FixtureError('truncated or oversized embedded scenario copy')
        decoded += data[position:position + literals]; position += literals
        if not 0 < distance <= len(decoded):
            raise FixtureError('invalid embedded scenario RefPack distance')
        for _ in range(count):
            decoded.append(decoded[-distance])
    if len(decoded) != size or position != len(data):
        raise FixtureError('embedded scenario RefPack length differs from its declaration')
    return decoded


def _serialized_attributes(data, start):
    ident, length, count = struct.unpack_from('>III', data, start)
    if ident != 0x01A80D26 or count > 128 or start + 8 + length > len(data):
        raise FixtureError('invalid reviewed native ClassSerializer record')
    cursor = start + 12 + count * 8
    attributes = {}
    for index in range(count):
        key, size = struct.unpack_from('>II', data, start + 12 + index * 8)
        if key in attributes or cursor + size > start + 8 + length:
            raise FixtureError('invalid reviewed native attribute bounds')
        attributes[key] = (cursor, size)
        cursor += size
    if cursor != start + 8 + length:
        raise FixtureError('native attribute lengths do not fill their record')
    return attributes, cursor


def build_crate_goal_fixture(output=OUTPUT):
    """Build an immutable diagnostic for expected native Kill rejection 0x0A.

    Installed attribute constructors138B190 map the reviewed IDs to goal fields.
    The source embeds one compressed ClassSerializer record in a nested DBPF.
    Only type, target class, required count and the class-zero palette change in
    its decoded record. Repacking it uncompressed keeps normal native parsing
    and native initialization intact. Stock powerup crates are invalid authored
    Kill targets in this executable; this fixture diagnoses that engine boundary.
    """
    output = Path(output).resolve() / 'crate-goal'
    output.mkdir(parents=True, exist_ok=True)
    source = SOURCE.read_bytes()
    if _sha(source) != '2b0f3f9b4255e96cc27dee6bdfe29960e26d5b715b73efe730bb4e700f83090a':
        raise FixtureError('authored crate-goal fixture requires the exact reviewed stock source')
    if source.count(b'DBPF') != 1:
        raise FixtureError('source must contain one reviewed embedded DBPF')
    base = source.index(b'DBPF'); inner = source[base:]
    count, index_size, index = (struct.unpack_from('<I', inner, offset)[0] for offset in (0x24, 0x2C, 0x40))
    if (count, index_size) != (1, 36) or struct.unpack_from('<I', source, base - 4)[0] != len(inner):
        raise FixtureError('source embedded database layout differs from the reviewed fixture')
    flags, typ, group, high, instance, offset, size, usize, compression, state = struct.unpack_from('<8I2H', inner, index)
    size &= 0x7FFFFFFF
    if (flags, typ, group, high, instance, compression, state) != (7, 0x01897C18, 0x01897C18, 0, 0x01897C18, 0xFFFF, 1):
        raise FixtureError('source embedded scenario key or compression differs from the reviewed fixture')
    data = _decode_refpack(inner[offset:offset + size])
    if len(data) != usize:
        raise FixtureError('embedded decoded size differs from DBPF index')
    original = bytes(data)
    attributes, end = _serialized_attributes(data, 0)
    if end != len(data):
        raise FixtureError('embedded ClassSerializer has trailing data')
    acts, acts_size = attributes[0x07102E6A]
    if struct.unpack_from('>I', data, acts)[0] != 4:
        raise FixtureError('source does not contain the four reviewed authored acts')
    first_act, _ = _serialized_attributes(data, acts + 4)
    goals, goals_size = first_act[0x0711477B]
    if struct.unpack_from('>I', data, goals)[0] != 1:
        raise FixtureError('first source act does not contain one reviewed goal')
    goal, goal_end = _serialized_attributes(data, goals + 4)
    if goal_end != goals + goals_size:
        raise FixtureError('source first-goal block has unexpected trailing data')
    fields = [(0x07114B5F, 'type', 2, 3), (0x07114B60, 'targetClassIndex', 1037, 0),
              (0x071D3B1D, 'requiredCount', 1, 3)]
    changes = []
    for key, name, before, after in fields:
        position, length = goal[key]
        if length != 4 or struct.unpack_from('>I', data, position)[0] != before:
            raise FixtureError('serialized source goal schema differs from its reviewed attributes')
        struct.pack_into('>I', data, position, after)
        changes.append(dict(attribute=key, name=name, decoded_offset=position, before=before, after=after))
    asset = struct.pack('>III', PALETTE_GROUP, PROP_TYPE, SOURCE_ASSET)
    if data.count(asset) != 1:
        raise FixtureError('decoded source must contain one class-zero gate palette')
    position = data.index(asset) + 8
    struct.pack_into('>I', data, position, FIXTURES['crate'][1])
    changes.append(dict(name='classZeroPalette', decoded_offset=position, before=SOURCE_ASSET, after=FIXTURES['crate'][1]))
    expected = bytearray(original)
    for change in changes:
        struct.pack_into('>I', expected, change['decoded_offset'], change['after'])
    if expected != data:
        raise FixtureError('authored crate fixture changed an unreviewed decoded field')
    decoded_path = output / f'authored-{_sha(data)[:12]}.bin'
    if not decoded_path.exists():
        decoded_path.write_bytes(data)
    embedded_path = output / f'embedded-{_sha(data)[:12]}.package'
    if not embedded_path.exists():
        build_package(str(embedded_path), [f'0x{group:08X}!0x{instance:08X}.0x{typ:08X}={decoded_path}'])
    embedded = embedded_path.read_bytes()
    scenario = bytearray(source[:base])
    struct.pack_into('<I', scenario, base - 4, len(embedded))
    scenario += embedded
    scenario_path = output / f'7F00600E-{_sha(scenario)[:12]}.bin'
    if not scenario_path.exists():
        scenario_path.write_bytes(scenario)
    package = output / f'SporeCoopCrateGoal-{_sha(scenario)[:16]}.package'
    if not package.exists():
        build_package(str(package), [f'0x{SCENARIO_GROUP:08X}!0x{CRATE_GOAL_KEY[0]:08X}.0x{SCENARIO_TYPE:08X}={scenario_path}'])
    manifest = dict(key=list(CRATE_GOAL_KEY), source_sha256=_sha(source), source_embedded_offset=base,
        source_decoded_sha256=_sha(original), scenario=str(scenario_path), scenario_sha256=_sha(scenario),
        package=str(package), package_sha256=_sha(package.read_bytes()), decoded_record=str(decoded_path),
        decoded_record_sha256=_sha(data), decoded_changes=changes, goal=dict(type=3, target_class=0, required_count=3),
        runtime_defaults=dict(amount=0.0, reason='Use ordinary crate tuning to override the copied gate amount10 before attacks.'),
        native_parser_verified=False, native_gameplay_verified=False,
        note='Immutable authored schema, not live goal retargeting. Normal native load must validate and initialize it before testing.')
    _json(output / 'manifest.json', manifest)
    return manifest


def _verify_game(game, record, menu=False):
    if not record.get('profile', '').startswith('winlab_') or record.get('exit_code') is not None:
        raise FixtureError('an active isolated Windows lab record is required')
    info = game.call('info')
    if f"pid={record['pid']}\n" not in info or f'exe sha256={EXE_SHA256}' not in info:
        raise FixtureError('the console PID or reviewed executable does not match lab metadata')
    if menu and ('GalaxyGameEntry' not in game.call('game.mode') or
                 not re.search(r'\bactive=0\b', game.call('coop.adventure status'))):
        raise FixtureError('fixture package registration/release requires the native menu and no active attempt')


class Native:
    """Reviewed native calls plus disposable non-executable remote scratch memory."""
    def __init__(self, game, record):
        _verify_game(game, record)
        self.game, self.record = game, record
        self.memory = AdventureMemory(game, record)
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.VirtualAllocEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD, wintypes.DWORD]
        self.kernel.VirtualAllocEx.restype = wintypes.LPVOID
        self.kernel.VirtualFreeEx.argtypes = [wintypes.HANDLE, wintypes.LPVOID, ctypes.c_size_t, wintypes.DWORD]
        self.kernel.VirtualFreeEx.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.handle = self.kernel.OpenProcess(0x1038, False, record['pid'])
        if not self.handle:
            self.memory.close()
            raise ctypes.WinError(ctypes.get_last_error())
        self.buffers = []

    def __enter__(self):
        return self

    def __exit__(self, exc_type, *_):
        error = None
        try:
            for address in self.buffers:
                if not self.kernel.VirtualFreeEx(self.handle, address, 0, 0x8000):
                    error = ctypes.WinError(ctypes.get_last_error())
        finally:
            self.kernel.CloseHandle(self.handle)
            self.memory.close()
        if error is not None and exc_type is None:
            raise error

    def write(self, address, data):
        self.game.call(f'mem.write 0x{address:X} {data.hex()}')
        if self.memory.read(address, len(data)) != data:
            raise FixtureError('native fixture write did not read back identically')

    def scratch(self, data):
        address = self.kernel.VirtualAllocEx(self.handle, None, max(len(data), 16), 0x3000, 4)
        if not address or not 0x10000 <= address <= 0xFFFFFFFF:
            raise FixtureError('cannot allocate a 32-bit private fixture scratch buffer')
        self.buffers.append(address)
        self.write(address, data)
        return address

    def call(self, function, convention, *args):
        target = f'0x{function:X}' if isinstance(function, int) else function
        if isinstance(function, str) and not ('::' in function or re.fullmatch(r'0x[0-9a-fA-F]+', function)):
            raise FixtureError('numeric native function targets must have an explicit hexadecimal prefix')
        arguments = ' '.join(f'0x{value:X}' if isinstance(value, int) else value for value in args)
        result = self.game.call(f'call {target} {convention} {arguments}')
        match = re.search(r'eax=0x([0-9a-fA-F]+)', result)
        if not match:
            raise FixtureError('native call returned no register result: ' + result)
        return int(match[1], 16)

    def virtual(self, obj, offset, *args):
        function = self.memory.pointer(self.memory.pointer(obj) + offset)
        return self.call(function, 'thiscall', obj, *args)

    def manager(self):
        address = int(self.game.call('sym Resource::ResourceManager_ptr').split()[0], 16)
        return self.memory.pointer(address)


def _class_data(memory, resource, runtime=False):
    first, last = struct.unpack('<II', memory.read(resource + 0x2BF4, 8))
    if last < first or (last - first) % 0x27E8 or not 0 < (last - first) // 0x27E8 <= 1024:
        raise FixtureError('invalid reviewed native class-map bounds')
    classes = []
    for base in range(first, last, 0x27E8):
        cls = base + 8
        index = memory.pointer(base)
        asset = struct.unpack('<III', memory.read(cls, 12))
        start, end = struct.unpack('<II', memory.read(cls + 0x70, 8))
        if end < start or (end - start) % 0x4E0 or (end - start) // 0x4E0 > 8:
            raise FixtureError('invalid native class-act bounds')
        acts = []
        for address in range(start, end, 0x4E0):
            values = struct.unpack('<6f', memory.read(address + 0x48C, 24))
            acts.append(dict(address=address, visible=bool(memory.read(address, 1)[0]),
                             invulnerable=bool(memory.read(address + 1, 1)[0]),
                             dead=bool(memory.read(address + 2, 1)[0]),
                             health_multiplier=values[0], speed_multiplier=values[1],
                             damage_multiplier=values[2], radius=values[3], amount=values[4], jump=values[5],
                             spawn_delay=struct.unpack('<i', memory.read(address + 0x4A4, 4))[0]))
        classes.append(dict(index=index, address=cls, asset=dict(zip(('instance', 'type', 'group'), asset)),
                            primary_override=memory.pointer(cls + 0x24),
                            primary_override_asset=list(struct.unpack('<III', memory.read(cls + 0x28, 12))),
                            secondary_override_asset=list(struct.unpack('<III', memory.read(cls + 0x50, 12))),
                            secondary_override=memory.pointer(cls + 0x48), acts=acts))
    first, last = struct.unpack('<II', memory.read(resource + 0x2C10, 8))
    if last < first or (last - first) % 0x238 or (last - first) // 0x238 > 16384:
        raise FixtureError('invalid native scenario marker bounds')
    markers = []
    for base in range(first, last, 0x238):
        marker = base + 4
        index = (base - first) // 0x238
        runtime_id = memory.pointer(marker + 0x228)
        if runtime and runtime_id != index:
            raise FixtureError('native marker ID does not match its bounded vector position')
        markers.append(dict(index=index, address=marker,
                            runtime_id=runtime_id,
                            class_index=struct.unpack('<i', memory.read(marker, 4))[0],
                            position=struct.unpack('<3f', memory.read(marker + 4, 12)),
                            orientation=struct.unpack('<4f', memory.read(marker + 0x10, 16)),
                            scale=struct.unpack('<f', memory.read(marker + 0x20, 4))[0],
                            teleport_position=struct.unpack('<3f', memory.read(marker + 0x1E8, 12))))
    return classes, markers


def inspect_resource(game, record, key):
    """Load an owned private scenario, copy diagnostics and release it normally."""
    with Native(game, record) as native:
        scratch = native.scratch(struct.pack('<III', *key) + bytes(20))
        if not native.virtual(native.manager(), 0x1C, scratch, scratch + 16, 0, 0, 0, 0):
            raise FixtureError('native private scenario load failed')
        resource_object = native.memory.pointer(scratch + 16)
        if not resource_object:
            raise FixtureError('native private scenario load returned no resource')
        try:
            resource = native.virtual(resource_object, 0x0C, 0xE742574A)
            if not resource:
                raise FixtureError('resource does not expose the reviewed scenario interface')
            classes, markers = _class_data(native.memory, resource)
            first, last = struct.unpack('<II', native.memory.read(resource + 0x23C, 8))
            if last < first or (last - first) % 0x534 or not 0 < (last - first) // 0x534 <= 8:
                raise FixtureError('invalid reviewed native authored-act vector')
            acts = []
            for act_index, act_address in enumerate(range(first, last, 0x534)):
                begin, end = struct.unpack('<II', native.memory.read(act_address + 0x84, 8))
                if end < begin or (end - begin) % 0x188 or (end - begin) // 0x188 > 3:
                    raise FixtureError('invalid reviewed native authored-goal vector')
                goals = []
                for index, address in enumerate(range(begin, end, 0x188)):
                    kind, target, second, required = struct.unpack('<4i', native.memory.read(address, 16))
                    goals.append(dict(index=index, type=kind, target_class=target, second_class=second,
                                      required_count=required))
                acts.append(dict(index=act_index, goals=goals))
            return dict(key=list(key), classes=classes, markers=markers, authored_acts=acts,
                        note='Private resource released; addresses are diagnostic and must not be reused.')
        finally:
            native.virtual(resource_object, 0x04)


def inspect_fixture(game, record, kind):
    result = inspect_resource(game, record, fixture_key(kind))
    cls = next((entry for entry in result['classes'] if entry['index'] == 0), None)
    if not cls or cls['asset']['instance'] != FIXTURES[kind][1]:
        raise FixtureError('native parsed fixture class zero does not match its requested palette asset')
    result['kind'] = kind
    result['target_class'] = cls
    result['target_markers'] = [entry for entry in result['markers'] if entry['class_index'] == 0]
    return result


def register_fixtures(game, record, output=OUTPUT):
    _verify_game(game, record, menu=True)
    output = Path(output).resolve()
    manifest = build_fixtures(output)
    return _register_fixture_package(game, record, manifest, output, fixture_key('mine'))


def register_crate_goal_fixture(game, record, output=OUTPUT):
    """Register the native-rejection diagnostic package at the native menu."""
    _verify_game(game, record, menu=True)
    manifest = build_crate_goal_fixture(output)
    return _register_fixture_package(game, record, manifest, Path(output).resolve() / 'crate-goal', CRATE_GOAL_KEY)


def inspect_crate_goal_fixture(game, record):
    """Check diagnostic bytes; matching schema does not make Kill eligibility valid."""
    result = inspect_resource(game, record, CRATE_GOAL_KEY)
    cls = next((entry for entry in result['classes'] if entry['index'] == 0), None)
    goal = result['authored_acts'][0]['goals']
    if (not cls or cls['asset']['instance'] != FIXTURES['crate'][1] or len(goal) != 1 or
            (goal[0]['type'], goal[0]['target_class'], goal[0]['second_class'], goal[0]['required_count']) != (3, 0, -1, 3)):
        raise FixtureError('native authored crate-goal parse did not match the immutable fixture manifest')
    result['kind'] = 'crate_goal'
    return result


def _register_fixture_package(game, record, manifest, output, probe_key):
    state_path = output / f'registration-{game.instance}-{record["pid"]}.json'
    if state_path.exists():
        old = json.loads(state_path.read_text(encoding='utf-8'))
        if old.get('registered') and old.get('package_sha256') == manifest['package_sha256']:
            inspect_resource(game, record, probe_key)
            return old
    with Native(game, record) as native:
        path = native.scratch((manifest['package'] + '\0').encode('utf-16le'))
        db = native.call('Memory::operator_new_named', 'cdecl', 0x388, 's:SporeCoop_fixture', 0, 0, 0, 0)
        if not db:
            raise FixtureError('cannot allocate native packed database')
        native.call('Resource::DatabasePackedFile::ctor', 'thiscall', db, path, 0)
        native.virtual(db + 4, 0)  # The caller owns this reference until release_fixtures.
        registered = False
        try:
            if not native.virtual(db, 0x04) or not native.virtual(db, 0x18, 1, 6, 0):
                raise FixtureError('cannot open immutable native fixture package')
            if not native.virtual(native.manager(), 0x50, 1, db, 8000):
                raise FixtureError('cannot register native fixture package')
            registered = True
        finally:
            if not registered:
                native.virtual(db + 4, 0x04)
    state = dict(instance=game.instance, pid=record['pid'], profile=record['profile'], database=db,
                 package=manifest['package'], package_sha256=manifest['package_sha256'], registered=True,
                 cleanup='Return to the native menu, then call release_fixtures; normal process exit also releases resources.')
    _json(state_path, state)
    return state


def release_fixtures(game, record, output=OUTPUT):
    _verify_game(game, record, menu=True)
    path = Path(output) / f'registration-{game.instance}-{record["pid"]}.json'
    state = json.loads(path.read_text(encoding='utf-8'))
    if not state.get('registered'):
        return state
    with Native(game, record) as native:
        if not native.virtual(native.manager(), 0x50, 0, state['database'], 0):
            raise FixtureError('native resource manager did not release fixture registration')
        native.virtual(state['database'] + 4, 0x04)
    state['registered'] = False
    _json(path, state)
    return state


def release_crate_goal_fixture(game, record, output=OUTPUT):
    return release_fixtures(game, record, Path(output).resolve() / 'crate-goal')


def configure_crate_kill_goal(game, record, required=3):
    """Refuse the retired runtime-retarget approach that failed native validation."""
    raise FixtureError('live Talk-to-Kill retargeting is invalid; immutable CRATE_GOAL_KEY is only a native 0x0A rejection diagnostic')


def tune_fixture(game, record, kind, marker=108, radius=None, amount=None,
                 invulnerable=False, teleporter_exit=None, health=None, allow_invalid_exit=False):
    """Tune only the loaded lab fixture; return actual native class/marker/actors.

    Plain field writes go through the main-thread console. This does not
    edit the immutable package, any loaded save file, or a player's own profile.
    Call on every test participant when client visuals/native behavior need the
    same overrides. Stock pickup duration remains a palette property, untouched.
    allow_invalid_exit bypasses only the source-radius guard for an explicit
    native rejection test; destination coordinates must still be finite.
    """
    _verify_game(game, record)
    if 'Scenario' not in game.call('game.mode'):
        raise FixtureError('runtime fixture tuning requires a loaded native adventure')
    for value in (radius, amount, health):
        if value is not None and (not math.isfinite(value) or not 0 <= value <= 1e6):
            raise FixtureError('fixture tuning must be finite and between zero and one million')
    if kind == 'crate' and amount is None:
        amount = 0.0
    with Native(game, record) as native:
        mode = native.memory.pointer(native.memory.singleton)
        data = native.memory.pointer(mode + 0x74)
        resource = native.memory.pointer(data + 0x10)
        play = native.memory.pointer(mode + 0x78)
        act = native.memory.pointer(play + 0xB8)
        classes, markers = _class_data(native.memory, resource, runtime=True)
        cls = next((entry for entry in classes if entry['index'] == 0), None)
        target = next((entry for entry in markers if entry['index'] == marker), None)
        if (not cls or cls['asset']['instance'] != FIXTURES[kind][1] or
                not target or target['class_index'] != 0 or act >= len(cls['acts'])):
            raise FixtureError('loaded runtime class/marker is not the requested isolated interaction fixture')
        current = cls['acts'][act]['address']
        native.write(current + 1, bytes([bool(invulnerable)]))
        if radius is not None:
            native.write(current + 0x498, struct.pack('<f', radius))
        if amount is not None:
            native.write(current + 0x49C, struct.pack('<f', amount))
        exit_position = None
        if kind == 'teleporter':
            exit_position = tuple(teleporter_exit or (target['position'][0] + 30, target['position'][1], target['position'][2]))
            if len(exit_position) != 3 or not all(math.isfinite(value) for value in exit_position):
                raise FixtureError('teleporter destination must be three finite coordinates')
            distance = math.sqrt(sum((a - b) ** 2 for a, b in zip(exit_position, target['position'])))
            if not allow_invalid_exit and distance <= 3 * (target['scale'] + 1):
                raise FixtureError('teleporter destination is inside the native source rejection radius')
            native.write(cls['address'] + 0x48, struct.pack('<I', 1))
            native.write(target['address'] + 0x1E8, struct.pack('<3f', *exit_position))
            native.write(target['address'] + 0x1F4, struct.pack('<4f', 0, 0, 0, 1))
            native.write(target['address'] + 0x204, struct.pack('<f', max(target['scale'], 0.1)))
        text = game.call(f'coop.adventure.world actorsat {marker} 48')
        actors = []
        for line in text.splitlines():
            match = re.search(r'marker=(\d+) incarnation=(\d+) noun=(\d+) object=(0x[0-9A-Fa-f]+) class=(\d+)(?: powerup=(\d+))?', line)
            if not match:
                continue
            values = match.groups()
            actor = dict(marker=int(values[0]), incarnation=int(values[1]), noun=int(values[2]),
                         object=int(values[3], 16), class_index=int(values[4]),
                         powerup=int(values[5]) if values[5] is not None else None)
            combatant = native.virtual(actor['object'], 0x0C, 0x013F94D4)
            if combatant and actor['powerup'] != 9:
                native.write(combatant + 0x80, bytes([bool(invulnerable)]))
                if health is not None:
                    native.write(combatant + 0x38, struct.pack('<f', health))
                actor['combatant'] = combatant
            actors.append(actor)
        after_classes, after_markers = _class_data(native.memory, resource, runtime=True)
        return dict(kind=kind, key=list(fixture_key(kind)), act=act, marker=marker, actors=actors,
                    target_class=next(entry for entry in after_classes if entry['index'] == 0),
                    target_marker=next(entry for entry in after_markers if entry['index'] == marker),
                    teleporter_exit=exit_position,
                    note='Runtime addresses are valid only for this recorded attempt and incarnation.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('build', 'register', 'inspect', 'inspect-stock', 'tune', 'release'))
    parser.add_argument('--output', type=Path, default=OUTPUT)
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--instance', type=int, default=21)
    parser.add_argument('--kind', choices=tuple(FIXTURES), default='mine')
    parser.add_argument('--stock', type=lambda value: int(value, 0), default=0xE6F78525)
    parser.add_argument('--marker', type=int, default=108)
    parser.add_argument('--radius', type=float)
    parser.add_argument('--amount', type=float)
    parser.add_argument('--health', type=float)
    parser.add_argument('--exit', type=float, nargs=3)
    args = parser.parse_args()
    if args.command == 'build':
        result = build_fixtures(args.output)
    else:
        if not args.metadata:
            parser.error('--metadata is required for every command that connects to a game')
        record = verify_lab(args.metadata, (args.instance,))[args.instance]
        game = Game(args.instance, args.output / 'commands')
        try:
            if args.command == 'register':
                result = register_fixtures(game, record, args.output)
            elif args.command == 'release':
                result = release_fixtures(game, record, args.output)
            elif args.command == 'inspect':
                result = inspect_fixture(game, record, args.kind)
            elif args.command == 'inspect-stock':
                result = inspect_resource(game, record, (args.stock, SCENARIO_TYPE, SCENARIO_GROUP))
            else:
                result = tune_fixture(game, record, args.kind, args.marker, args.radius, args.amount,
                                      teleporter_exit=args.exit, health=args.health)
        finally:
            game.save_log()
    path = args.output / f'{args.command}-{args.instance}-{args.kind}.json'
    _json(path, result)
    print(json.dumps(dict(result=str(path), package=result.get('package'), fixtures=len(result.get('fixtures', {}))), indent=2))


if __name__ == '__main__':
    main()
