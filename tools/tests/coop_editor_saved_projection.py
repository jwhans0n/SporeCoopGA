#!/usr/bin/env python3
"""Strict offline Saved/cold comparison for the qualified C46 private machine ID.

No process, Native API, profile or production validator is accessed. This tool
grants no asset/projection authority. Another machine ID requires new evidence.
"""
import argparse
import json
from pathlib import Path
import struct

import coop_editor_complexity_fixture as native

MACHINE = 0x5523CA25
FAMILIES = {0x2B978C46, 0x2399BE55, 0x24682294, 0x476A98C7}
SLOTS = (0x07114976, 0x96A94670, 0x96A94671)
KEY, MACHINE_FIELD, SERVER, MODEL = 0x62B74270, 0x62B74271, 0x62B74272, 0x62B74273


def _need(condition, message):
    if not condition:
        raise ValueError(message)


def _assets(decoded):
    root, end = native.attributes(decoded)
    _need(end == len(decoded) and all(k in root for k in (native.MARKERS, native.CLASSES, native.ACTS)),
          'Incomplete decoded authored root')
    start, size, _ = root[native.CLASSES]
    count = native.word(decoded, start, '>')
    _need(0 < count <= 1024, 'Unsupported class count')
    cursor, result, seen = start+4, {}, set()
    for _ in range(count):
        owner = native.word(decoded, cursor, '>')
        _need(owner <= 0x7FFFFFFF and owner not in seen, 'Duplicate or unsupported class identity')
        seen.add(owner)
        fields, cursor = native.attributes(decoded, cursor+4)
        for slot, field in enumerate(SLOTS):
            if field not in fields:
                continue
            offset, width, _ = fields[field]
            asset, end = native.attributes(decoded, offset)
            _need(end == offset+width and all(k in asset and asset[k][1] == n for k, n in
                  ((KEY, 12), (MACHINE_FIELD, 4), (SERVER, 8), (MODEL, 4))),
                  'Malformed class ScenarioAsset record')
            group, family, instance = struct.unpack_from('>III', decoded, asset[KEY][0])
            result[owner, slot] = dict(key=(group, instance, family),
                machine=native.word(decoded, asset[MACHINE_FIELD][0], '>'),
                machine_offset=asset[MACHINE_FIELD][0],
                server=struct.unpack_from('>Q', decoded, asset[SERVER][0])[0],
                model=native.word(decoded, asset[MODEL][0], '>'))
    _need(cursor == start+size, 'Class vector has trailing data')
    return result


def _compare_decoded(saved, cold):
    """Internal controlled-byte test seam; public compare first runs native()."""
    _need(type(saved) is bytes and type(cold) is bytes and
          0 < len(saved) <= native.MAX_NATIVE and len(saved) == len(cold),
          'Decoded image bounds or lengths differ')
    before, after = _assets(saved), _assets(cold)
    _need(before.keys() == after.keys(), 'Class/slot asset roster differs')
    checked, changed = bytearray(cold), []
    for owner, old in before.items():
        new = after[owner]
        if old['machine'] == new['machine']:
            continue
        group, instance, family = old['key']
        _need(old['machine'] == MACHINE and new['machine'] == 0,
              f'Unqualified machine transition for class {owner[0]} slot {owner[1]}')
        _need(old['key'] == new['key'] and instance >> 24 == 0x7D and family in FAMILIES and
              old['model'] == new['model'] and old['server'] == new['server'] == 0xFFFFFFFFFFFFFFFF,
              f'Machine exception has a different/nonprivate asset owner: class {owner[0]} slot {owner[1]}')
        struct.pack_into('>I', checked, new['machine_offset'], old['machine'])
        changed.append(dict(native_class_index=owner[0], slot=owner[1], field=f'{SLOTS[owner[1]]:08X}',
            key=f'{group:08X}!{instance:08X}.{family:08X}', model=f'{old["model"]:08X}',
            server='FFFFFFFFFFFFFFFF', saved_machine=f'{MACHINE:08X}', cold_machine='00000000'))
    if saved != checked:
        first = next(i for i, (a, b) in enumerate(zip(saved, checked)) if a != b)
        raise ValueError(f'Unqualified decoded difference at {first}: saved={saved[first:first+16].hex()} cold={cold[first:first+16].hex()}')
    return changed


def _prefix(data, base):
    _need(12 <= base-4 <= len(data) and struct.unpack_from('<II', data) == (0xB523D4F9, 17),
          'Unsupported native prefix header')
    count = native.word(data, 8, '>')
    _need(count <= 4096, 'Property count exceeds prefix bound')
    scalar_sizes = {0:16, 0x20:16, 0x31:16, 0x32:16, 0x33:16, 0x34:16,
        1:1, 2:1, 5:1, 6:1, 3:2, 7:2, 8:2, 4:4, 9:4, 0xA:4, 0xD:4,
        0xB:8, 0xC:8, 0xE:8, 0x30:8, 0x38:56}
    at, seen, shape, ignored = 12, set(), [], []
    checked = bytearray(data[:base])

    def take(size):
        nonlocal at
        _need(0 <= size <= base-4-at, 'Truncated property prefix')
        start = at
        at += size
        return start

    def text(kind):
        chars = native.word(data, take(4), '>')
        take(chars*(2 if kind == 0x13 else 1))

    for _ in range(count):
        ident, kind, flags = struct.unpack_from('>IHH', data, take(8))
        _need(ident not in seen, 'Duplicate prefix property')
        seen.add(ident)
        start = at
        if flags & 0x80:
            items, width = struct.unpack_from('>II', data, take(8))
            _need(items <= 8192 and width <= 32 << 20, 'Unsupported property array bound')
            if kind in (0x12, 0x13):
                for _ in range(items):
                    text(kind)
            else:
                take(items*width)
        elif kind in (0x12, 0x13):
            text(kind)
        else:
            _need(kind in scalar_sizes, 'Unsupported property scalar shape')
            take(scalar_sizes[kind])
        shape.append((ident, kind, flags, start, at-start))
        # C46: the inline ResourceKey uses three words; its fourth is unused.
        # Do not mask None unions, vectors, arrays, flags or unknown fields.
        if kind == 0x20 and flags == 0 and at-start == 16:
            ignored.append((ident, start+12, data[start+12:start+16].hex()))
            checked[start+12:start+16] = bytes(4)
    _need(native.word(data, base-4) == len(data)-base, 'Embedded DBPF length is not exact')
    checked[base-4:base] = bytes(4)
    return bytes(checked), shape, ignored


def _envelope(data):
    entries, index, size, primary, legacy = native.package(data)
    entry = entries[0]
    _need(len(entries) == 1 and entry.offset == 96 and index == entry.offset+entry.size and
          index+size == len(data), 'Unsupported native DBPF envelope layout')
    header, directory = bytearray(data[:96]), bytearray(data[index:])
    for offset, value in ((0x28, legacy), (0x40, primary)):
        if value:
            struct.pack_into('<I', header, offset, 0)
    # Preserve the stored-size high bit and every other index/header byte.
    at = entry.location-index+8
    struct.pack_into('<I', directory, at, native.word(data, entry.location+8) & 0x80000000)
    packed = data[entry.offset:entry.offset+entry.size]
    prefix = packed[:6 if packed[0] & 0x80 else 5] if entry.compression else b''
    return bytes(header), bytes(directory), bool(primary), bool(legacy), entry.compression, prefix


def compare(saved: bytes, cold: bytes) -> dict:
    """Return a compact complete-match report, or raise ValueError on mismatch."""
    _need(type(saved) is bytes and type(cold) is bytes and
          0 < len(saved) <= native.MAX_NATIVE and 0 < len(cold) <= native.MAX_NATIVE,
          'Expected bounded immutable native byte inputs')
    before, after = native.native(saved), native.native(cold)
    _need(before['base'] == after['base'], 'Native prefix boundaries differ')
    left, left_shape, left_unused = _prefix(saved, before['base'])
    right, right_shape, right_unused = _prefix(cold, after['base'])
    _need(left_shape == right_shape and left == right, 'Unqualified native property/outer-prefix difference')
    _need(_envelope(saved[before['base']:]) == _envelope(cold[after['base']:]),
          'Unqualified DBPF header/index/envelope difference')
    changed = _compare_decoded(before['decoded'], after['decoded'])
    _need((before['count'], before['classes'], before['acts']) == (after['count'], after['classes'], after['acts']),
          'Authored root counts differ')
    unused = [dict(property=f'{a[0]:08X}', offset=a[1], saved=a[2], cold=b[2])
              for a, b in zip(left_unused, right_unused) if a != b]
    return dict(passed=True, policy='C46-private-machine-5523CA25-to-zero',
        saved_sha256=native.sha(saved), cold_sha256=native.sha(cold),
        saved_decoded_sha256=native.sha(before['decoded']), cold_decoded_sha256=native.sha(after['decoded']),
        decoded_bytes=len(before['decoded']), placements=before['count'], classes=before['classes'], acts=before['acts'],
        changed_machine_owners=changed, unused_scalar_key_words=unused,
        all_other_decoded_bytes_exact=True, all_other_prefix_bytes_exact=True,
        native_or_production_authority_granted=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--saved', type=Path)
    parser.add_argument('--saved-sha256')
    parser.add_argument('--cold', type=Path)
    parser.add_argument('--cold-sha256')
    parser.add_argument('--output', type=Path, help='Optional NEW OUT JSON report; never overwrites a receipt')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        import importlib.util
        import unittest
        path = native.ROOT/'tests/editor_saved_projection_tool_tests.py'
        spec = importlib.util.spec_from_file_location('saved_projection_contracts', path)
        tests = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(tests)
        return int(not unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromModule(tests)).wasSuccessful())
    try:
        _need(args.saved is not None and args.cold is not None, 'Explicit Saved and cold inputs are required')
        sp, saved = native.pinned_file(args.saved, args.saved_sha256 or '', native.MAX_NATIVE)
        cp, cold = native.pinned_file(args.cold, args.cold_sha256 or '', native.MAX_NATIVE)
        report = compare(saved, cold)
        _need(sp.read_bytes() == saved and cp.read_bytes() == cold, 'Input changed during comparison')
        if args.output:
            target = args.output.resolve()
            _need(native.OUT.resolve() in target.parents, 'Report must be a NEW file under OUT')
            with target.open('x', encoding='utf-8') as stream:
                stream.write(json.dumps(report, indent=2)+'\n')
        print(json.dumps(report, separators=(',', ':')))
        return 0
    except (ValueError, OSError, struct.error, OverflowError) as error:
        print(json.dumps(dict(passed=False, error=str(error)), separators=(',', ':')))
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
