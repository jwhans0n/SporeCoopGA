"""Packaging boundaries using temporary synthetic payloads, without a game install."""
from contextlib import redirect_stderr, redirect_stdout
import hashlib
import io
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

REPOSITORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY / 'tools'))
import package_editor_preview as preview
PRODUCTION_EXES = preview.SUPPORTED_EXES


class PreviewPackageTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / 'source'
        self.build = self.root / 'build'
        self.kit = self.root / 'kit'
        self.output = self.root / 'distribution'
        source_files = {
            'mods/coop/coop.cpp': '#define COOP_VERSION "' + preview.PREVIEW_VERSION + '"\n',
            'mods/coop/net/protocol.h': 'constexpr uint16_t kProtoVersion = 23;\n',
            'data/symbols/test.sym': 'Fixture=0x1 ; verified synthetic fixture\n',
            'data/config/coop.ini': '[fixture]\n',
            'docs/MULTIPLAYER-ADVENTURE-EDITOR.md': '[Report](reports/EDITOR-PREVIEW9-VALIDATION.md)\n',
            'docs/reports/EDITOR-NATIVE-FLOW-VALIDATION.md': '[Guide](../MULTIPLAYER-ADVENTURE-EDITOR.md)\n',
            'docs/reports/EDITOR-PREVIEW9-VALIDATION.md': '[External](https://example.com/evidence)\n',
            'LICENSE': 'Synthetic license fixture\n',
        }
        for relative, text in source_files.items():
            path = self.source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(text, encoding='utf-8')
        (self.source / preview.LOADED_EDITOR_MAP).write_text(
            '# exe_sha256='+preview.EXE+'\n'+'\n'.join(name+'=0x1 ; verified synthetic fixture'
                      for name in sorted(preview.REQUIRED_EDITOR_SYMBOLS))+'\n', encoding='utf-8')
        (self.source / 'tools').mkdir()
        shutil.copy2(REPOSITORY / 'tools/mkpack.py', self.source / 'tools/mkpack.py')
        self.build.mkdir()
        (self.build / 'coop.dll').write_bytes(b'fixture\0' + preview.PREVIEW_VERSION.encode() + b'\0')
        (self.build / 'SporeCoopSetup.exe').write_bytes(b'synthetic setup')
        for relative, data in {
            'SporebinEP1/SporeApp.exe': b'synthetic supported executable',
            'SporebinEP1/steam_api.dll': b'synthetic loader',
            'SporebinEP1/sporekit.dll': b'synthetic kit',
            'Data/SporeCoop.package': b'synthetic resources',
        }.items():
            path = self.kit / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        self.original_exe = (self.kit / 'SporebinEP1/SporeApp.exe').read_bytes()
        self.supported = {hashlib.sha256(self.original_exe).hexdigest()}
        for item in (patch.object(preview, 'ROOT', self.source),
                     patch.object(preview, 'SUPPORTED_EXES', self.supported)):
            item.start()
            self.addCleanup(item.stop)

    def run_package(self):
        output = io.StringIO()
        with redirect_stdout(output), redirect_stderr(io.StringIO()):
            preview.main(['--build', str(self.build), '--kit-installation', str(self.kit),
                          '--output', str(self.output)])
        return json.loads(output.getvalue())

    def rejects_before_output(self):
        with self.assertRaises(SystemExit) as result:
            self.run_package()
        self.assertEqual(result.exception.code, 2)
        self.assertFalse(self.output.exists())

    def test_manifest_archive_pack_and_documents_match_preview9(self):
        receipt = self.run_package()
        distribution = self.output / 'SporeCoopGA'
        manifest = json.loads((distribution / 'SHA256.json').read_text())
        self.assertEqual(manifest['version'], preview.PREVIEW_VERSION)
        self.assertEqual(manifest['protocol'], 23)
        self.assertFalse(manifest['complete_release_acceptance'])
        self.assertEqual(manifest['supported_exe_sha256'], preview.EXE)
        self.assertEqual(manifest['supported_laa_exe_sha256'], preview.GOG2024_LAA)
        for relative, digest in manifest['payload'].items():
            self.assertEqual(preview.sha(self.output / 'stage' / relative), digest)
        for name, digest in manifest['distribution'].items():
            self.assertEqual(preview.sha(distribution / name), digest)
        archive = Path(receipt['zip'])
        self.assertEqual(preview.sha(archive), receipt['sha256'])
        self.assertEqual(receipt['payload_dll'], preview.sha(self.build / 'coop.dll'))
        with zipfile.ZipFile(archive) as bundle:
            self.assertEqual(set(bundle.namelist()),
                             {'SporeCoopGA/' + p.name for p in distribution.iterdir()})
        guide = (distribution / 'MULTIPLAYER-ADVENTURE-EDITOR.md').read_text()
        self.assertIn('https://github.com/jwhans0n/SporeCoopGA/blob/feat/ga-editor-multi-act/docs/reports/EDITOR-PREVIEW9-VALIDATION.md', guide)
        self.assertIn('https://example.com/evidence',
                      (distribution / 'EDITOR-PREVIEW9-VALIDATION.md').read_text())
        readme = (distribution / 'README.txt').read_text()
        self.assertIn('protocol 23', readme)
        self.assertIn('Native\nacceptance of these additions is pending', readme)
        self.assertNotIn('Terrain, nested creation editors', readme)
        self.assertEqual((self.kit / 'SporebinEP1/SporeApp.exe').read_bytes(), self.original_exe)
        self.assertFalse(any('SporeApp.exe' in name for name in manifest['payload']))
        pack = (distribution / ('SporeCoopGA-' + preview.PREVIEW_VERSION + '.pack')).read_bytes()
        self.assertEqual(pack[:4], b'SCUP')
        version_bytes = struct.unpack_from('<H', pack, 8)[0]
        self.assertEqual(pack[10:10 + version_bytes].decode(), preview.PREVIEW_VERSION)
        self.assertIn((preview.PREVIEW_VERSION + '\n').encode(), pack)

    def test_repository_identity_and_loaded_symbol_map(self):
        with patch.object(preview, 'ROOT', REPOSITORY):
            self.assertEqual(preview.release_identity(), (preview.PREVIEW_VERSION, 23))
            preview.validate_loaded_symbols()
        self.assertEqual(PRODUCTION_EXES, {preview.EXE, preview.GOG2024_LAA})

    def test_previous_source_version_is_rejected(self):
        path = self.source / 'mods/coop/coop.cpp'
        path.write_text('#define COOP_VERSION "0.7.5.0-ga-editor-preview.8"\n')
        self.rejects_before_output()

    def test_second_allowlisted_executable_variant_is_accepted(self):
        variant = b'synthetic supported LAA variant'
        (self.kit / 'SporebinEP1/SporeApp.exe').write_bytes(variant)
        self.supported.add(hashlib.sha256(variant).hexdigest())
        self.run_package()
        self.assertEqual((self.kit / 'SporebinEP1/SporeApp.exe').read_bytes(), variant)

    def test_previous_protocol_is_rejected(self):
        path = self.source / 'mods/coop/net/protocol.h'
        path.write_text('constexpr uint16_t kProtoVersion = 22;\n')
        self.rejects_before_output()

    def test_missing_source_identity_is_rejected(self):
        (self.source / 'mods/coop/coop.cpp').write_text('// no version\n')
        self.rejects_before_output()

    def test_old_payload_dll_is_rejected(self):
        (self.build / 'coop.dll').write_bytes(b'0.7.5.0-ga-editor-preview.8\0')
        self.rejects_before_output()

    def test_unknown_supporting_executable_is_rejected(self):
        (self.kit / 'SporebinEP1/SporeApp.exe').write_bytes(b'unknown executable')
        self.rejects_before_output()

    def test_missing_loaded_symbol_map_is_rejected(self):
        (self.source / preview.LOADED_EDITOR_MAP).unlink()
        self.rejects_before_output()

    def test_symbols_only_in_an_unloaded_map_are_rejected(self):
        loaded = self.source / preview.LOADED_EDITOR_MAP
        unloaded = loaded.with_name('overrides_editor_creation_dc04aee5.sym')
        unloaded.write_bytes(loaded.read_bytes().split(b'\n',1)[1])
        loaded.write_text('# exe_sha256='+preview.EXE+'\n')
        self.rejects_before_output()

    def test_symbols_in_a_separate_correctly_headed_map_are_accepted(self):
        loaded = self.source / preview.LOADED_EDITOR_MAP
        separate = loaded.with_name('overrides_editor_creation_dc04aee5.sym')
        separate.write_bytes(loaded.read_bytes())
        loaded.write_text('# exe_sha256='+preview.EXE+'\n')
        self.run_package()

    def test_header_after_ten_native_reads_is_rejected(self):
        loaded = self.source / preview.LOADED_EDITOR_MAP
        loaded.write_bytes(b'# comment\n'*10+loaded.read_bytes())
        self.rejects_before_output()

    def test_long_lines_cannot_hide_header_outside_discovery(self):
        loaded = self.source / preview.LOADED_EDITOR_MAP
        loaded.write_bytes(b'# '+b'x'*5110+b'\n'+loaded.read_bytes())
        self.rejects_before_output()

    def test_header_in_tenth_native_read_is_accepted(self):
        loaded = self.source / preview.LOADED_EDITOR_MAP
        loaded.write_bytes(b'# comment\n'*9+loaded.read_bytes())
        self.run_package()

    def test_crlf_translation_preserves_tenth_native_read_boundary(self):
        loaded = self.source / preview.LOADED_EDITOR_MAP
        loaded.write_bytes(b'#'+b'x'*4597+b'\r\n'+loaded.read_bytes())
        self.run_package()

    def test_existing_output_is_preserved(self):
        self.output.mkdir()
        marker = self.output / 'keep'
        marker.write_bytes(b'existing output')
        with self.assertRaises(FileExistsError):
            self.run_package()
        self.assertEqual(marker.read_bytes(), b'existing output')


if __name__ == '__main__':
    unittest.main()
