"""Patch/rollback, backup failures and exact-identity rejection without game data."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import large_address_aware as laa

PRODUCTION_EXES = laa.SUPPORTED_EXES


class LargeAddressTests(unittest.TestCase):
    def setUp(self):
        data = bytearray(1024)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 0x120)
        data[0x120:0x124] = b'PE\0\0'
        struct.pack_into('<H', data, 0x124, 0x14c)
        struct.pack_into('<H', data, 0x136, 0x102)
        struct.pack_into('<H', data, 0x138, 0x10b)
        self.original = bytes(data)
        data[0x136] |= 0x20
        self.modified = bytes(data)
        self.original_hash = hashlib.sha256(self.original).hexdigest()
        self.modified_hash = hashlib.sha256(self.modified).hexdigest()
        self.patches = [patch.object(laa, 'GOG2024', self.original_hash),
                        patch.object(laa, 'GOG2024_LAA', self.modified_hash),
                        patch.object(laa, 'SUPPORTED_EXES', {self.original_hash, self.modified_hash})]
        for item in self.patches:
            item.start()
            self.addCleanup(item.stop)
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.exe = Path(self.directory.name) / 'SporeApp.exe'
        self.backup = self.exe.with_suffix('.bak')
        self.exe.write_bytes(self.original)

    def test_patch_is_one_bit_idempotent_and_reversible(self):
        result = laa.update(self.exe, self.backup)
        self.assertTrue(result['changed'])
        self.assertEqual(self.exe.read_bytes(), self.modified)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertEqual([i for i, (a, b) in enumerate(zip(self.original, self.modified)) if a != b], [0x136])
        self.assertFalse(laa.update(self.exe, self.backup)['changed'])
        self.assertTrue(laa.update(self.exe, self.backup, restore=True)['changed'])
        self.assertEqual(self.exe.read_bytes(), self.original)

    def test_conflicting_backup_is_preserved(self):
        self.backup.write_bytes(b'previous unrelated backup')
        with self.assertRaises(ValueError):
            laa.update(self.exe, self.backup)
        self.assertEqual(self.exe.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), b'previous unrelated backup')

    def test_failed_replace_keeps_original_and_backup(self):
        with patch.object(laa.os, 'replace', side_effect=PermissionError('in use')):
            with self.assertRaises(PermissionError):
                laa.update(self.exe, self.backup)
        self.assertEqual(self.exe.read_bytes(), self.original)
        self.assertEqual(self.backup.read_bytes(), self.original)
        self.assertEqual(list(self.exe.parent.glob('*.tmp')), [])

    def test_unknown_image_and_bad_restore_are_refused(self):
        self.exe.write_bytes(self.original + b'changed')
        with self.assertRaises(ValueError):
            laa.update(self.exe, self.backup)
        self.assertFalse(self.backup.exists())
        self.exe.write_bytes(self.modified)
        self.backup.write_bytes(b'wrong backup')
        with self.assertRaises(ValueError):
            laa.update(self.exe, self.backup, restore=True)
        self.assertEqual(self.exe.read_bytes(), self.modified)

    def test_same_file_backup_is_refused(self):
        with self.assertRaises(ValueError):
            laa.update(self.exe, self.exe)
        self.assertEqual(self.exe.read_bytes(), self.original)

    def test_production_hashes_match_native_allowlist(self):
        header = (Path(__file__).resolve().parents[1] / 'sdk/include/sporekit_build.hpp').read_text()
        for sha in PRODUCTION_EXES:
            self.assertIn('"' + sha + '"', header)


if __name__ == '__main__':
    unittest.main()
