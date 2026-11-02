#!/usr/bin/env python3
"""Check the native creation request/result and detached-import ABI receipts."""
import argparse
import json
from pathlib import Path
import re
import struct
import sys

from pe import PE

HASHES = {
    'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37',
    '47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab',
}
SYMBOLS = {
    'NotifyResult': 0x57A710, 'RequestCtor': 0x5A9140, 'Allocate': 0xF47240,
    'SetClassAsset': 0xF43990, 'GetClass': 0xF3E740, 'AssetModelType': 0xEECAC0,
    'ValidateModel': 0x4BB170, 'AdventureValidationMask': 0x15DAB18,
}
RECEIPTS = [
    (0x57A710, '5356578b7c241033db8bf13b'),
    (0x57A732, '68c7110c03'), (0x57A955, 'c20400'),
    (0x57A83C, '8b8ecc0100008b511c'),
    (0xED42A1, '807e44000f856d030000'),
    (0x5A9140, '5333db568bf1c74604b8b83e01895e08'), (0x5A921A, 'c3'),
    # Native subtype routing is selected by the ctor's unresolved editor ID.
    (0x5A915D, 'c7460cffffffff'),
    (0x5A930F, '837f0cff750b8d4710e823fdffff89470c'),
    (0x5A9074, '8b018b500c68f809c603ffd285c074148b401850'),
    # Direct unknown source families use a fixed placeholder, not that source.
    (0xEEC99C, '81fa04b1b1000f85ef000000'),
    (0xEEDFA3, '3d942268240f85fe010000'),
    (0xEEE19A, '3dc7986a470f848b0000003d010000f07427'),
    (0xEEE1AC, '68e4888c01e84af2c4ff8bc8e8332ac3ff558bf06aff6822f050078bcee84217d8ff'),
    (0xF43990, '8b44240481ece00400005557508be9e8'), (0xF43A82, 'c20800'),
    (0xF3E740, '53568b71100fb686082c00008b96f42b'), (0xF3E78D, 'c20400'),
    (0xEECAC0, '51535683cbffc744240800000000e8bd'), (0xEECBC6, 'c3'),
    (0xF47240, '8b4424188b4c24148b542408508b4424'),
    (0x4BB170, '558bec51894dfc0fb645'), (0x4BB1AE, 'c21800'),
    (0xF3C24C, '68f809c603ffd2'),
    (0xF3C257, '8b0d18ab5d016a0183ec108bd4890a8b0d1cab5d01'),
    (0xF3C26C, '894a048b0d20ab5d01894a088b0d24ab5d01894a0c'),
    (0xF46427, '50e8d395ebff8b4e0ce88bd9ffff'),
    (0xF43DF0, '8d6fb0558bcee8d583ffff'),
    (0xF43B24, '8d4c241851578bcbe85ffeffff'),
    (0x4F3E00, '558bec81ece4010000c745b000'),
]
CALLS = [(0xF46389, 0xF43990), (0xF43A6B, 0xF42470),
         (0xF43A01, 0xEECAC0), (0x5A95B4, 0x5A9140),
         (0x57C1CB, 0x57A710), (0x58628F, 0x577650),
         (0x5A9318, 0x5A9040), (0x5A9088, 0x4333E0),
         (0xF3C288, 0x4BB170), (0x4BB1A0, 0x4F3E00),
         (0xF46430, 0xF43DC0), (0xF43DF6, 0xF3C1D0),
         (0xF43B2C, 0xF43990)]


def verify(pe, symbol_map=None):
    if pe.sha256 not in HASHES:
        raise ValueError('Expected one of the two reviewed GOG 2024 images')
    for address, encoded in RECEIPTS:
        if pe.read(address, len(bytes.fromhex(encoded))) != bytes.fromhex(encoded):
            raise ValueError(f'Creation ABI receipt mismatch at {address:08X}')
    for address, target in CALLS:
        code = pe.read(address, 5)
        if not code or code[0] != 0xE8 or address + 5 + struct.unpack('<i', code[1:])[0] != target:
            raise ValueError(f'Creation caller mismatch at {address:08X}')
    if symbol_map:
        actual = {}
        for line in Path(symbol_map).read_text(encoding='utf-8').splitlines():
            match = re.fullmatch(r'SporeCoop::CreationEditor::(\w+)=0x([0-9A-Fa-f]{8}) ; verified .+', line)
            if match:
                if match[1] in actual:
                    raise ValueError('Duplicate creation symbol line')
                actual[match[1]] = int(match[2], 16)
            elif line.startswith('SporeCoop::CreationEditor::'):
                raise ValueError('Unreviewed creation symbol line')
        if actual != SYMBOLS:
            raise ValueError('Creation symbol map differs from verified receipts')
    return dict(sha256=pe.sha256, symbols=SYMBOLS, receipts=len(RECEIPTS), callers=len(CALLS))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('--check-map', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        result = verify(PE(args.executable), args.check_map)
        if args.report:
            args.report.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(f"verified {len(SYMBOLS)} creation editor symbols and {len(CALLS)} callers")
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
