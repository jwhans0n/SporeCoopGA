"""Independent native stamp column assertions for owned Win32 lab fixtures.

Native effect Append uses the current planet seed, including on Alt-clone. This
module checks authored property captures, never invokes a native function and
does not depend on a live process. Run with --selftest for negative controls.
"""
import argparse
import copy
import json
from pathlib import Path
import struct
import unittest

EFFECT = {0x02A907B5: 12, 0x02A907B6: 56, 0x03A23F97: 4,
          0x03DD8E59: 4, 0x03DD8E5A: 4, 0x7A6AA70B: 4}
MODEL = {0x03A90C57: 12, 0x03A90C5F: 56}
PLANET_SEED = 0x02A907B7


def array_rows(value, width):
    raw = bytes.fromhex(value[2])
    assert len(raw) >= 8, 'truncated native property array'
    count, actual_width = struct.unpack_from('>II', raw)
    assert actual_width == width and count <= 8192 and len(raw) == 8 + count * width
    return [raw[8 + i * width:8 + (i + 1) * width] for i in range(count)]


def verify_stamp_change(before, after, kind, index, control):
    assert kind in ('effect', 'model') and control in ('delete', 'clone', 'wheel')
    assert type(index) is int and 0 <= index < 8192
    a, b = ({int(k): v for k, v in fields.items()} for fields in (before, after))
    columns = EFFECT if kind == 'effect' else MODEL
    transform = 0x02A907B6 if kind == 'effect' else 0x03A90C5F
    changed = {k for k in a.keys() | b.keys() if a.get(k) != b.get(k)}
    assert changed and changed <= (set(columns) if control != 'wheel' else {transform})
    counts = []
    for key, width in columns.items():
        old, new = array_rows(a[key], width), array_rows(b[key], width)
        assert index < len(old)
        counts.append((len(old), len(new)))
        if control == 'delete':
            assert new == old[:index] + old[index + 1:]
        elif control == 'clone':
            assert len(new) == len(old) + 1 and new[:-1] == old
            if key == 0x03DD8E59:
                assert tuple(a[PLANET_SEED][:2]) == (0xA, 0)
                assert new[-1] == bytes.fromhex(a[PLANET_SEED][2]), 'clone must use the current planet seed'
            elif key != transform:
                assert new[-1] == old[index]
        else:
            assert len(new) == len(old)
            assert new[:index] == old[:index] and new[index + 1:] == old[index + 1:]
    assert len(set(counts)) == 1, 'parallel native columns have different lengths'
    return sorted(changed)


class StampChecks(unittest.TestCase):
    def fixture(self, kind='effect'):
        columns = EFFECT if kind == 'effect' else MODEL
        fields = {PLANET_SEED: (0xA, 0, '00000063')}
        for key, width in columns.items():
            fields[key] = (0, 0x9C, (struct.pack('>II', 2, width) + bytes([7]) * width + bytes([8]) * width).hex())
        return fields

    def append(self, fields, kind='effect'):
        out = copy.deepcopy(fields)
        for key, width in (EFFECT if kind == 'effect' else MODEL).items():
            rows = array_rows(out[key], width)
            added = bytes.fromhex(out[PLANET_SEED][2]) if key == 0x03DD8E59 else rows[0]
            out[key] = (*out[key][:2], (struct.pack('>II', 3, width) + b''.join(rows) + added).hex())
        return out

    def test_native_current_planet_seed_and_model_append(self):
        for kind in ('effect', 'model'):
            before = self.fixture(kind)
            self.assertTrue(verify_stamp_change(before, self.append(before, kind), kind, 0, 'clone'))

    def test_source_seed_is_not_the_native_clone_contract(self):
        before = self.fixture();after = self.append(before)
        value = list(after[0x03DD8E59]);value[2] = value[2][:-8] + '07070707';after[0x03DD8E59] = value
        with self.assertRaises(AssertionError): verify_stamp_change(before, after, 'effect', 0, 'clone')

    def test_unrelated_edit_and_old_row_mutation_reject(self):
        before = self.fixture();after = self.append(before);after[123] = (1, 0, '01')
        with self.assertRaises(AssertionError): verify_stamp_change(before, after, 'effect', 0, 'clone')
        after = self.append(before);raw = bytearray.fromhex(after[0x02A907B5][2]);raw[8] ^= 1
        after[0x02A907B5] = (0, 0x9C, raw.hex())
        with self.assertRaises(AssertionError): verify_stamp_change(before, after, 'effect', 0, 'clone')

    def test_truncated_wrong_width_and_parallel_length_reject(self):
        for bad in ((0, 0, '00'), (0, 0, (struct.pack('>II', 2, 1) + b'00').hex())):
            with self.assertRaises(AssertionError): array_rows(bad, 4)
        before = self.fixture();after = self.append(before);after[0x03A23F97] = before[0x03A23F97]
        with self.assertRaises(AssertionError): verify_stamp_change(before, after, 'effect', 0, 'clone')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', '--selftest', dest='selftest', action='store_true')
    parser.add_argument('--capture', type=Path, help='result.json containing before/after checks and a case')
    args = parser.parse_args()
    if args.selftest:
        unittest.main(argv=['terrain-stamp-checks']);return
    if not args.capture: parser.error('--capture or --selftest is required')
    data = json.loads(args.capture.read_text(encoding='utf-8-sig'))
    before, after = (next(c for c in data['checks'] if c['label'] == label)['rows'][0]['planet']['fields']
                     for label in ('before', 'after'))
    case = data['case']
    print(json.dumps({'passed': True, 'changed_fields': verify_stamp_change(
        before, after, case['kind'], case['index'], case['control'])}))


if __name__ == '__main__': main()
