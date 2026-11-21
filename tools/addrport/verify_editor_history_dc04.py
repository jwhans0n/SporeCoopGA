#!/usr/bin/env python3
"""Verify exact GOG native history retirement ABI and native reset receipts."""
import argparse
import json
from pathlib import Path
import re
import struct
import sys
from pe import PE

SHA = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
ENTRIES = [
    ('EraseHistoryRange', 0xE25BB0, 0xE25BFF, 8, '538b5c240c5556578bf98b4c2414',
     'thiscall(history vector*,entry** first,entry** last),ret8; native owning-vector erase releases removed entries via primary vt4 and updates end4; does not reset cursor14 or history serial18'),
    ('DiscardHistoryEntry', 0xF322B0, 0xF322D2, 0, '8b0dfc65600185c97411',
     'cdecl(),ret; clears global owned editing entry24 before primary vt4 Release and sets notEditing20 true; does not restore a scenario or remove committed history'),
]
CALLS = [(0xF41BF5, 0xE25BB0), (0xF4476E, 0xE25BB0), (0xF44E30, 0xE25BB0),
         (0xF466D7, 0xE25BB0), (0xF341CD, 0xE25BB0), (0xEE97CB, 0xF322B0),
         (0xEEBD80, 0xF322B0), (0xEFF2F2, 0xF322B0), (0xF3ACF9, 0xF322B0)]
RECEIPTS = [
    (0xE25BD5, '8b0e85c974078b118b4204ffd083c604'),
    (0xE25BE9, '8b4424142bd8c1fb02f7db03db03db015f04'),
    (0xF322BA, 'c705fc656001000000008b018b5004ffd2c605f865600101'),
    (0xF41BDD, 'a1dc6560018b0dd86560015051b9d8656001893dec656001'),
    (0xF41C00, 'ff05f0656001c605f865600101'),
    (0xF44755, '8b0ddc6560018b15d86560015152b9d8656001891dec656001'),
    (0xF44779, 'ff05f0656001c605f865600101'),
    (0xF44E17, '8b0ddc6560018b15d86560015152b9d8656001891dec656001'),
    (0xF44E3A, 'ff05f0656001c605f865600101'),
    (0xF466BF, '8b15dc656001a1d86560015250b9d8656001893dec656001'),
    (0xF466DC, 'b8010000000105f0656001a2f8656001'),
    (0xF3B2C0, 'a1f06560013b8180000000770c80b98400000000'),
    (0xF3B7C0, 'a1f06560013b8180000000771580b98400000000'),
    (0xF3B7D6, '80b985000000007503'),
    (0xF34226, 'b801000000014614014618'),
    (0xED9B46, 'b801000000014618884620'),
    (0xED9B8E, 'b801000000014618884620'),
]

def verify(pe):
    if pe.sha256 != SHA or pe.image_base != 0x400000:
        raise ValueError('unsupported executable: ' + pe.sha256)
    for name, address, ret, count, prefix, _ in ENTRIES:
        raw = bytes.fromhex(prefix)
        if pe.read(address, len(raw)) != raw: raise ValueError(name + ': entry mismatch')
        raw = b'\xc3' if not count else b'\xc2' + struct.pack('<H', count)
        if pe.read(ret, len(raw)) != raw: raise ValueError(name + ': stack cleanup mismatch')
    for site, target in CALLS:
        raw = pe.read(site, 5)
        if raw[0] != 0xE8 or site + 5 + struct.unpack('<i', raw[1:])[0] != target:
            raise ValueError('native caller mismatch: ' + hex(site))
    for site, encoded in RECEIPTS:
        raw = bytes.fromhex(encoded)
        if pe.read(site, len(raw)) != raw: raise ValueError('native layout mismatch: ' + hex(site))
    return dict(sha256=SHA, symbols={'SporeCoop::Editor::' + n: a for n, a, *_ in ENTRIES},
                calls=len(CALLS), layout_receipts=len(RECEIPTS))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe'); parser.add_argument('--write-map', type=Path)
    parser.add_argument('--check-map', type=Path); parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        result = verify(PE(args.exe))
        lines = ['# Exact GOG native history retirement ABI.', '# exe_sha256=' + SHA,
                 '# evidence=tools/addrport/verify_editor_history_dc04.py; docs/re/adventure-editor-history.md']
        lines += [f'SporeCoop::Editor::{n}=0x{a:08X} ; verified static: {note}'
                  for n, a, _, _, _, note in ENTRIES]
        rendered = '\n'.join(lines) + '\n'
        if args.write_map: args.write_map.write_text(rendered, encoding='utf-8', newline='\n')
        if args.check_map and args.check_map.read_text(encoding='utf-8') != rendered:
            raise ValueError('history map differs from verified receipts')
        if args.report: args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(f'verified {len(ENTRIES)} history entries, {len(CALLS)} callers, {len(RECEIPTS)} layout receipts')
        return 0
    except (OSError, ValueError) as error:
        print('verification failed: ' + str(error), file=sys.stderr); return 1

if __name__ == '__main__': sys.exit(main())
