#!/usr/bin/env python3
"""BUG-1275: full preview identity and the numeric PE quad are independent strict checks.

Uses real, minimal VERSIONINFO PE files, not mocked pe_version.read results. No compiler,
network, release download or game is needed. --tools selects old tools for the failing twin.
"""
import argparse
import contextlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def node(key, value=b'', typ=0, children=()):
    raw = bytearray(struct.pack('<HHH', 0, len(value) // 2 if typ else len(value), typ))
    raw.extend((key + '\0').encode('utf-16-le'))
    raw.extend(bytes(-len(raw) % 4))
    raw.extend(value)
    raw.extend(bytes(-len(raw) % 4))
    for child in children:
        raw.extend(child)
        raw.extend(bytes(-len(raw) % 4))
    struct.pack_into('<H', raw, 0, len(raw))
    return raw


def pe(version, fixed=(0, 7, 15, 0), file_version=None):
    strings = dict(CompanyName='Test', FileDescription='Test DLL', OriginalFilename='coop.dll',
                   ProductName='SporeCoop', LegalCopyright='Test', FileVersion=file_version or version,
                   ProductVersion=version)
    table = node('040904b0', children=[node(k, (v + '\0').encode('utf-16-le'), 1) for k, v in strings.items()])
    ms, ls = fixed[0] << 16 | fixed[1], fixed[2] << 16 | fixed[3]
    info = struct.pack('<13I', 0xFEEF04BD, 0x10000, ms, ls, ms, ls, 0x3F, 0, 0x40004, 2, 0, 0, 0)
    vi = node('VS_VERSION_INFO', info, children=[node('StringFileInfo', children=[table])])
    resource = bytearray(0x58)
    for offset, name, child in ((0, 16, 0x80000018), (0x18, 1, 0x80000030), (0x30, 0x409, 0x48)):
        struct.pack_into('<HH', resource, offset + 12, 0, 1)
        struct.pack_into('<II', resource, offset + 16, name, child)
    struct.pack_into('<4I', resource, 0x48, 0x1058, len(vi), 1200, 0)
    resource.extend(vi)
    image = bytearray(0x200)
    image[:2] = b'MZ'
    struct.pack_into('<I', image, 0x3C, 0x80)
    image[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', image, 0x84, 0x14C, 1, 0, 0, 0, 224, 0x2102)
    struct.pack_into('<H', image, 0x98, 0x10B)
    struct.pack_into('<II', image, 0x98 + 112, 0x1000, len(resource))
    image[0x178:0x180] = b'.rsrc\0\0\0'
    struct.pack_into('<IIII', image, 0x180, len(resource), 0x1000, len(resource), 0x200)
    return bytes(image + resource)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tools', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    pv = load('pe_version_identity_test', args.tools / 'pe_version.py')
    triage = load('crash_triage_identity_test', args.tools / 'crash_triage.py')
    version = '0.7.15.0-ga-editor-preview.16'
    checks = 0

    def check(value, message):
        nonlocal checks
        checks += 1
        assert value, message

    with tempfile.TemporaryDirectory(prefix='preview-identity-') as tmp:
        root = Path(tmp)
        dll = root / 'coop.dll'
        dll.write_bytes(pe(version))

        def expect(want):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                result = pv.main(['--expect', want, str(dll)])
            return result, output.getvalue()

        check(pv.read(dll)[0] == (0, 7, 15, 0), 'fixture contains a real four-field numeric VERSIONINFO')
        check(expect(version)[0] == 0, 'matching full prerelease strings and numeric PE quad pass')
        for wrong in ('0.7.15.0', version.replace('.16', '.15'), version + '+other'):
            check(expect(wrong)[0] == 1, 'same numeric quad cannot hide a different preview identity: ' + wrong)
        dll.write_bytes(pe(version, fixed=(0, 7, 15, 1)))
        result, text = expect(version)
        check(result == 1 and 'FILEVERSION' in text, 'matching preview strings cannot hide a wrong fixed PE quad')
        dll.write_bytes(pe(version, file_version=version.replace('.16', '.15')))
        check(expect(version)[0] == 1, 'both FileVersion and ProductVersion must match exactly')
        for release, quad in (('0.7.15', (0, 7, 15, 0)), ('0.7.15.2', (0, 7, 15, 2)),
                              ('0.7.15.0-beta', (0, 7, 15, 0)), (version + '+build.abc123', (0, 7, 15, 0))):
            dll.write_bytes(pe(release, quad))
            check(expect(release)[0] == 0, 'release/prerelease numeric quad is preserved: ' + release)
        for invalid in ('0.7.15.0garbage', '0.7.15.0-', '0.7.15.0.1', '65536.7.15.0', '0..7', version + '.'):
            check(expect(invalid)[0] == 2, 'invalid --expect is refused cleanly: ' + invalid)

        dll.write_bytes(pe(version))
        for actual in (version, version + '+build.abc123', '0.7.15.0-beta'):
            for context in ('mods: coop %s  \n', 'SporeCoop %s, name Player\n',
                            '[INF] coop: loaded coop %s\n', '[INF] coop: SporeCoop %s, name Player\n',
                            'fallback coop %s here\n'):
                found = triage.coop_version(context % actual)
                check(found and found.group(1) == actual, 'full report identity survives every version context')
        found = triage.coop_version('Summary: coop 0.7.13\nmods: coop ' + version + '\n')
        check(found and found.group(1) == version, 'authoritative report version still outranks an old summary')
        for actual in (version, '0.7.15'):
            found = triage.coop_version('fallback coop ' + actual + '.')
            check(found and found.group(1) == actual, 'sentence punctuation is not part of the version identity')
        opts = SimpleNamespace(symbols=str(root))
        check(triage.find_symbols(version, opts)[0] == str(root), 'exact full report/DLL version accepts local symbols')
        for wrong in ('0.7.15.0', version.replace('.16', '.15'), version + '+other'):
            folder, note = triage.find_symbols(wrong, opts)
            check(folder is None and 'offsets would name wrong functions' in note,
                  'same-core wrong-source symbol folder is refused: ' + wrong)
        dll.write_bytes(('SporeCoop ' + version + '\0').encode())
        check(triage.dll_version(dll) == version, 'legacy embedded-string fallback retains full preview identity')
        dll.write_bytes(('SporeCoop ' + version + '\0SporeCoop ' + version.replace('.16', '.15') + '\0').encode())
        check(triage.dll_version(dll) is None, 'conflicting embedded DLL identities remain refused')
    print(f'prerelease_identity_check: {checks} checks passed')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
