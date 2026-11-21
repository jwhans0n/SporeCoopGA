"""Offline C46 oracle plus controlled decoded/prefix mutations; no Native calls.

The decoded negative seam does not pretend to regenerate a genuine NativeWrite
container. Public compare separately exercises the two genuine raw artifacts.
"""
import contextlib
import io
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools/tests'))
import coop_editor_saved_projection as tool


class SavedProjection(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.saved_path = ROOT/'out/preview9-c46-creatures-native-save/persisted-root-b70a1968f457c56bc932892b7fcca6da5bbf17044f8424f4e3a14f1ae7504dea.bin'
        cls.cold_path = ROOT/'out/preview9-c46-cold-bytes-r2/cold.native.bin'
        cls.saved, cls.cold = cls.saved_path.read_bytes(), cls.cold_path.read_bytes()
        assert tool.native.sha(cls.saved) == 'b70a1968f457c56bc932892b7fcca6da5bbf17044f8424f4e3a14f1ae7504dea'
        assert tool.native.sha(cls.cold) == 'ff3067e491eb8d2dd59f8246687bb8d9be3484499efe7a71ec958be732e2a125'
        cls.a, cls.b = tool.native.native(cls.saved), tool.native.native(cls.cold)
        cls.da, cls.db = cls.a['decoded'], cls.b['decoded']

    def asset(self, data, owner=1342, slot=0):
        root, _ = tool.native.attributes(data)
        at = root[tool.native.CLASSES][0]
        cursor = at+4
        for _ in range(tool.native.word(data, at, '>')):
            ident = tool.native.word(data, cursor, '>')
            fields, cursor = tool.native.attributes(data, cursor+4)
            if ident == owner:
                offset, size, _ = fields[tool.SLOTS[slot]]
                return tool.native.attributes(data, offset)[0], offset, size
        self.fail('Missing genuine class owner')

    def altered(self, data, offset, value=None):
        result = bytearray(data)
        if value is None:
            result[offset] ^= 1
        else:
            struct.pack_into('>I', result, offset, value)
        return bytes(result)

    def test_genuine_complete_projection_and_exact_input(self):
        report = tool.compare(self.saved, self.cold)
        self.assertEqual((report['placements'], report['classes'], report['acts']), (419, 122, 5))
        self.assertEqual([r['native_class_index'] for r in report['changed_machine_owners']], [1342, 1343, 1344, 1345])
        self.assertTrue(all(r['slot'] == 0 and r['saved_machine'] == '5523CA25' and r['cold_machine'] == '00000000'
                            for r in report['changed_machine_owners']))
        self.assertEqual(len(report['unused_scalar_key_words']), 1)
        self.assertFalse(report['native_or_production_authority_granted'])
        self.assertEqual(tool.compare(self.saved, self.saved)['changed_machine_owners'], [])

    def test_reference_mismatches_and_machine_qualification(self):
        asset, _, _ = self.asset(self.da)
        for field in (tool.KEY, tool.SERVER, tool.MODEL):
            with self.subTest(field=field), self.assertRaises(ValueError):
                tool._compare_decoded(self.da, self.altered(self.db, asset[field][0]))
        for value in (0x5523CA26, 0xFFFFFFFF):
            with self.subTest(machine=value), self.assertRaises(ValueError):
                tool._compare_decoded(self.altered(self.da, asset[tool.MACHINE_FIELD][0], value), self.db)
        # An already-cleared unchanged machine is exact equality, not an exception.
        self.assertEqual(len(tool._compare_decoded(self.altered(self.da, asset[tool.MACHINE_FIELD][0], 0), self.db)), 3)
        with self.assertRaises(ValueError):
            tool._compare_decoded(self.da, self.altered(self.db, asset[tool.MACHINE_FIELD][0], 1))
        with self.assertRaises(ValueError):
            tool._compare_decoded(self.db, self.da)
        for field, delta, value in ((tool.KEY, 8, 0x6D8E12FB), (tool.KEY, 4, 0xDEADBEEF),
                                    (tool.SERVER, 0, 0), (tool.SERVER, 4, 0)):
            with self.subTest(nonprivate=(field, delta)), self.assertRaises(ValueError):
                tool._compare_decoded(self.altered(self.da, asset[field][0]+delta, value),
                                      self.altered(self.db, asset[field][0]+delta, value))

    def test_same_owner_secondary_slot_is_checked(self):
        a, b = bytearray(self.da), bytearray(self.db)
        for data in (a, b):
            _, source, width = self.asset(data)
            _, target, size = self.asset(data, slot=1)
            self.assertEqual(width, size)
            data[target:target+size] = data[source:source+width]
        changed = tool._compare_decoded(bytes(a), bytes(b))
        self.assertEqual(len(changed), 5)
        self.assertTrue(any(r['native_class_index'] == 1342 and r['slot'] == 1 for r in changed))

    def test_pose_acts_captain_crew_and_opaque_are_exact(self):
        marker, _ = tool.native.attributes(self.db, self.b['markers'][0]['start'])
        for field in (0x071147E6, 0x071147E7, 0x071147E8):
            with self.subTest(pose=field), self.assertRaises(ValueError):
                tool._compare_decoded(self.da, self.altered(self.db, marker[field][0]))
        root = self.b['attributes']
        for field in (tool.native.ACTS, 0x07102E78, 0x07749AD3):
            with self.subTest(root=field), self.assertRaises(ValueError):
                tool._compare_decoded(self.da, self.altered(self.db, root[field][0]))
        opaque = next((offset for field, (offset, size, _) in root.items()
                       if field not in (tool.native.MARKERS, tool.native.CLASSES, tool.native.ACTS,
                                        0x07102E78, 0x07749AD3) and size), None)
        self.assertIsNotNone(opaque)
        with self.assertRaises(ValueError):
            tool._compare_decoded(self.da, self.altered(self.db, opaque))

    def test_shapes_rosters_and_bounds(self):
        asset, _, _ = self.asset(self.db)
        mutations = [self.db[:-1], self.altered(self.db, asset[tool.MACHINE_FIELD][2], 8),
                     self.altered(self.db, self.b['attributes'][tool.native.CLASSES][0], 123)]
        for data in mutations:
            with self.subTest(shape=len(data)), self.assertRaises(ValueError):
                tool._compare_decoded(self.da, data)
        for data in (b'', bytearray(self.saved), bytes(tool.native.MAX_NATIVE+1)):
            with self.subTest(bounds=type(data)), self.assertRaises(ValueError):
                tool.compare(data, self.cold)

    def test_prefix_exception_is_only_the_scalar_key_fourth_word(self):
        base = self.b['base']
        _, shape, unused = tool._prefix(self.cold, base)
        allowed = self.altered(self.cold, unused[0][1])
        self.assertTrue(tool.compare(self.saved, allowed)['passed'])
        offsets = [shape[0][3], base-8, base-4]
        offsets.append(next(start for _, kind, flags, start, _ in shape if kind == 0 and flags == 0))
        for offset in offsets:
            with self.subTest(prefix=offset), self.assertRaises(ValueError):
                tool.compare(self.saved, self.altered(self.cold, offset))
        # Typed vector padding and None payloads are not silently generalized.
        def prefix(kind):
            data = struct.pack('<II', 0xB523D4F9, 17)+struct.pack('>IIHH', 1, 0x12345678, kind, 0)+bytes(16)
            return data+struct.pack('<I', 1)+b'X'
        a = prefix(0x31);b = self.altered(a, 32)
        self.assertNotEqual(tool._prefix(a, len(a)-1)[0], tool._prefix(b, len(b)-1)[0])

    def test_unknown_container_bytes_are_not_discarded(self):
        base = self.b['base']
        for offset in (base+0x50, base+96):
            with self.subTest(container=offset), self.assertRaises(ValueError):
                tool.compare(self.saved, self.altered(self.cold, offset))

    def test_cli_never_overwrites_old_receipts_or_accepts_wrong_hash(self):
        with tempfile.TemporaryDirectory(dir=ROOT/'out', prefix='saved-projection-contract-') as directory:
            output = Path(directory)/'receipt.json'
            output.write_text('old failure')
            argv = ['comparator', '--saved', str(self.saved_path), '--saved-sha256', tool.native.sha(self.saved),
                    '--cold', str(self.cold_path), '--cold-sha256', tool.native.sha(self.cold), '--output', str(output)]
            with mock.patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(tool.main(), 1)
            self.assertEqual(output.read_text(), 'old failure')
            argv[argv.index('--saved-sha256')+1] = '0'*64
            with mock.patch.object(sys, 'argv', argv), contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(tool.main(), 1)


if __name__ == '__main__':
    unittest.main()
