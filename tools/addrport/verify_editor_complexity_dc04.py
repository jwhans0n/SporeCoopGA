#!/usr/bin/env python3
"""Verify native adventure complexity gates without changing the executable."""
import argparse
import json
from pathlib import Path
import re
import struct

from pe import PE

HASHES = {
    'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37',
    '47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab',
}
SYMBOLS = {
    'Measure': 0xF3E7B0, 'WeightedRatio': 0xF3DB80, 'CanPlace': 0xF3EB30,
    'CreateMarkerLimit': 0xF44267, 'CloneMarkerLimit': 0xF444DE, 'ClassLimit': 0xF46326,
    'CountLiveMarkers': 0xEF18B0, 'TuningWeights': 0x16C8884, 'TuningMaximum': 0x16C8944,
}
RECEIPTS = [
    (0xF3E7B0, '83ec7c5333db578bf9381da8896c017408d9ee5f5b83c47cc3'),
    (0xF3EB25, 'c3'),
    # Complete reviewed function locks the float32 accumulation groups/order,
    # all twelve live tuning addresses, native N+1 term and strict max choice.
    (0xF3DB80, '518b442408f30f2a402cf30f590534896c01f30f2a4828f30f590d24896c01'
     'f30f2a5010f30f5915c4886c01f30f58c1f30f2a4824f30f590d14896c01f30f58c1'
     'f30f2a4820f30f590d04896c01f30f58c1f30f2a481cf30f590df4886c01f30f58c1'
     'f30f2a4818f30f590de4886c01f30f58c1f30f2a4814f30f590dd4886c01f30f58ca'
     'f30f2a500cf30f5915b4886c01f30f58caf30f2a5008f30f5915a4886c01f30f58ca'
     'f30f2a5004f30f591594886c01f30f58caf30f2a108b4110f30f591584886c01'
     '8b88242c00008b90142c000005102c0000f30f58caf30f58c1f30f5e0544896c01'
     'f30f1144240881f9ffffff3f730a69c9380200000308eb028bca8b442408505251'
     'e8223cfbff4083c40c890424db042485c07d06d805c04f3f01d80df00141018d0424'
     'd91424d9442408d9c9dff1ddd877048d442408d90059c20400'),
    (0xEF18B0, '8b4c2404568b74240c33c03bf17426908bd181c138020000894c24088b12'
     'c1ea1ef6c201750a8b11c1ea1ff6c20175e0403bf175db5ec3'),
    (0xF3DC60, 'f30f5e0544896c01'), (0xF3DCA2, 'd80df0014101'),
    (0xF3DCC0, '59c20400'),
    (0xF3EB30, 'e87bfcffffd9e8dff1ddd87608b801000000c2040033c0c204'),
    (0xF44267, '3d000400000f83d7010000'),
    (0xF444DE, '3d000400000f839d000000'),
    (0xF46326, '3d000400000f8db8000000'),
    (0x138E3D0, '6a0168c59d1c816888d44801e86f4a5aff'),
    (0x138E3E1, 'f30f1005f03b4701'), (0x138E411, 'f30f110544896c01'),
    (0xECFE50, '8bc1c7400400000000c70000984801c3'),
]
CALLS = [
    (0xF3EB19, 0xF3DB80), (0xF3EB30, 0xF3E7B0),
    (0xED6476, 0xF3E7B0), (0xF40D50, 0xF3E7B0),
    (0xECFE6D, 0xF3EB30), (0xF003E8, 0xF3EB30), (0xF0067C, 0xF3EB30),
    (0xF4425F, 0xEF18B0), (0xF444D6, 0xEF18B0),
    (0xF44282, 0xF2EBC0), (0xF444F4, 0xF2EBC0), (0xF44714, 0xECFE50),
]


def verify(pe, symbol_map=None):
    if pe.sha256 not in HASHES:
        raise ValueError('Expected the exact reviewed GOG 2024 image or its LAA variant')
    for address, encoded in RECEIPTS:
        if pe.read(address, len(bytes.fromhex(encoded))) != bytes.fromhex(encoded):
            raise ValueError(f'Complexity receipt mismatch at {address:08X}')
    helper = Path(__file__).resolve().parents[2] / 'mods/coop/editor_native_complexity_ratio.h'
    source = helper.read_text(encoding='utf-8')
    literal = re.search(r'kFormulaCode=detail::unhex\((.*?)\);', source, re.S)
    if not literal:
        raise ValueError('Missing runtime native-formula byte receipt')
    encoded = ''.join(re.findall(r'"([0-9a-f]+)"', literal[1]))
    if len(encoded) != 0x144 * 2 or bytes.fromhex(encoded) != pe.read(0xF3DB80, 0x144):
        raise ValueError('Runtime complexity formula receipt differs from reviewed executable')
    for address, target in CALLS:
        code = pe.read(address, 5)
        if not code or code[0] != 0xE8 or address+5+struct.unpack('<i', code[1:])[0] != target:
            raise ValueError(f'Complexity caller mismatch at {address:08X}')
    if pe.read(0x148D488, 14) != b'MaxComplexity\0':
        raise ValueError('Native MaxComplexity tuning identity changed')
    if struct.unpack('<f', pe.read(0x1473BF0, 4))[0] != 10.0 or struct.unpack('<f', pe.read(0x14101F0, 4))[0] != 1/1024:
        raise ValueError('Native complexity defaults changed')
    if symbol_map:
        first_lines = Path(symbol_map).read_text(encoding='utf-8').splitlines()[:10]
        if not any('exe_sha256=' in line for line in first_lines):
            raise ValueError('SporeKit requires the executable hash header within the first ten lines')
        actual = {}
        for line in Path(symbol_map).read_text(encoding='utf-8').splitlines():
            match = re.fullmatch(r'SporeCoop::EditorComplexity::(\w+)=0x([0-9A-Fa-f]{8}) ; verified .+', line)
            if match:
                if match[1] in actual:
                    raise ValueError('Duplicate complexity symbol line')
                actual[match[1]] = int(match[2], 16)
            elif line.startswith('SporeCoop::EditorComplexity::'):
                raise ValueError('Unreviewed complexity symbol line')
        if actual != SYMBOLS:
            raise ValueError('Complexity symbol map differs from its verified receipts')
    return dict(sha256=pe.sha256, symbols=SYMBOLS, receipts=len(RECEIPTS), callers=len(CALLS),
                default_max_complexity=10, default_marker_limit=1024, class_limit=1024,
                native_marker_ratio='(N+1)/1024', extended_marker_ratio='N/4096',
                weighted_operation_groups=[[11,10,9,8,7,6],[5,4,3,2,1,0]])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable')
    parser.add_argument('--check-map', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        result = verify(PE(args.executable), args.check_map)
        if args.report:
            args.report.write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print(f"verified {len(SYMBOLS)} complexity symbols and {len(CALLS)} callers")
    except (OSError, ValueError) as error:
        parser.exit(1, str(error)+'\n')


if __name__ == '__main__':
    main()
