#!/usr/bin/env python3
"""Package the experimental GOG editor preview without changing the game install."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

from large_address_aware import SUPPORTED_EXES, GOG2024_LAA

ROOT = Path(__file__).resolve().parents[1]
EXE = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
PREVIEW_VERSION = '0.7.5.0-ga-editor-preview.9'
PROTOCOL = 23
LOADED_EDITOR_MAP = 'data/symbols/overrides_editor_dc04aee5.sym'
REQUIRED_EDITOR_SYMBOLS = {
    'SporeCoop::CreationEditor::' + name
    for name in ('NotifyResult', 'RequestCtor', 'Allocate', 'SetClassAsset', 'GetClass', 'AssetModelType')
} | {
    'SporeCoop::EditorComplexity::' + name
    for name in ('Measure', 'WeightedRatio', 'CanPlace', 'CreateMarkerLimit', 'CloneMarkerLimit', 'ClassLimit')
}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def release_identity():
    version = re.search(r'^#define COOP_VERSION "([^"]+)"',
                        (ROOT / 'mods/coop/coop.cpp').read_text(), re.M)
    protocol = re.search(r'^constexpr uint16_t kProtoVersion = (\d+);',
                         (ROOT / 'mods/coop/net/protocol.h').read_text(), re.M)
    if not version or version[1] != PREVIEW_VERSION:
        raise ValueError('This packaging recipe is scoped to editor preview 9')
    if not protocol or int(protocol[1]) != PROTOCOL:
        raise ValueError('Editor preview 9 requires protocol 23')
    return version[1], int(protocol[1])


def symbol_build(path):
    # Match SporeKit file_hash_header: ten fgets reads with a 512-byte buffer.
    # The Windows CRT translates CRLF before applying that buffer bound.
    # Long definition lines consume several reads before a later header.
    with io.BytesIO(path.read_bytes().replace(b'\r\n', b'\n')) as stream:
        for _ in range(10):
            value = re.search(rb'exe_sha256=([^\r\n ]+)', stream.readline(511))
            if value:
                return value[1].decode('ascii', errors='replace')
    return None


def validate_loaded_symbols():
    primary = ROOT / LOADED_EDITOR_MAP
    if not primary.is_file():
        raise ValueError('The supporting SporeKit loaded editor symbol map is missing')
    if symbol_build(primary) != EXE:
        raise ValueError('The editor symbol map needs its verified executable header within SporeKit discovery')
    present = set()
    for path in sorted((ROOT / 'data/symbols').glob('*.sym')):
        if symbol_build(path) == EXE:
            present.update(re.findall(r'^(SporeCoop::(?:CreationEditor|EditorComplexity)::\w+)=',
                                      path.read_text(encoding='utf-8'), re.M))
    if not REQUIRED_EDITOR_SYMBOLS <= present:
        raise ValueError('Creation/complexity symbols need executable-matched SporeKit symbol maps')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--kit-installation', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        version, protocol = release_identity()
        validate_loaded_symbols()
    except ValueError as error:
        parser.error(str(error))
    kit = args.kit_installation.resolve()
    if sha(kit / 'SporebinEP1/SporeApp.exe') not in SUPPORTED_EXES:
        parser.error('The supporting installation is not the verified GOG executable')
    if version.encode() + b'\0' not in (args.build / 'coop.dll').read_bytes():
        parser.error('The payload DLL does not identify this preview version')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    stage = output / 'stage'
    payload = stage / 'SporeCoop'
    (payload / 'mods/sporekit/symbols').mkdir(parents=True)
    files = {
        'steam_api.sporekit.dll': kit / 'SporebinEP1/steam_api.dll',
        'sporekit.dll': kit / 'SporebinEP1/sporekit.dll',
        'mods/coop.dll': args.build / 'coop.dll',
        'SporeCoop.package': kit / 'Data/SporeCoop.package',
    }
    for relative, source in files.items():
        shutil.copy2(source, payload / relative)
    for source in sorted((ROOT / 'data/symbols').glob('*.sym')):
        shutil.copy2(source, payload / 'mods/sporekit/symbols' / source.name)
    for source in sorted((ROOT / 'data/config').glob('*.ini')):
        shutil.copy2(source, payload / 'mods' / source.name)
    distribution = output / 'SporeCoopGA'
    distribution.mkdir()
    shutil.copy2(args.build / 'SporeCoopSetup.exe', distribution / 'SporeCoopSetup.exe')
    pack = distribution / ('SporeCoopGA-' + version + '.pack')
    subprocess.run([sys.executable, str(ROOT / 'tools/mkpack.py'), str(stage), version, str(pack)], check=True)
    for relative in ['docs/MULTIPLAYER-ADVENTURE-EDITOR.md',
                     'docs/reports/EDITOR-NATIVE-FLOW-VALIDATION.md',
                     'docs/reports/EDITOR-PREVIEW9-VALIDATION.md', 'LICENSE']:
        source = ROOT / relative
        if source.suffix == '.md':
            # The archive carries standalone documents. Keep their source
            # references usable without shipping an incomplete repository tree.
            def link(match):
                target = match[1]
                if re.match(r'[a-zA-Z][\w+.-]*:|#', target):
                    return match[0]
                path, marker, anchor = target.partition('#')
                resolved = (source.parent / path).resolve().relative_to(ROOT)
                return '](' + 'https://github.com/jwhans0n/SporeCoopGA/blob/feat/ga-editor-multi-act/' + resolved.as_posix() + marker + anchor + ')'
            data = re.sub(r'\]\(([^)]+)\)', link, source.read_text(encoding='utf-8'))
            (distribution / source.name).write_text(data, encoding='utf-8')
        else:
            shutil.copy2(source, distribution / source.name)
    (distribution / 'README.txt').write_text(
        f'SporeCoopGA {version} (experimental editor preview, protocol {protocol})\n\n'
        'Extract this folder. Keep SporeCoopSetup.exe beside its matching .pack.\n'
        'End all game sessions before installing. Select the normal SPORE installation.\n'
        'All participants need this exact preview and Galactic Adventures.\n'
        'Supported SporebinEP1/SporeApp.exe SHA-256:\n' + EXE + '\n' +
        'Optional exact 4 GB variant:\n' + GOG2024_LAA + '\n\n'
        'Host a party and open the native Adventure Editor. Guests join\n'
        'the party and follow automatically. Unsaved drafts can be shared. F9 is\n'
        'the normal party/chat lobby. Use native Undo/Redo, Save, Play and Build.\n'
        'Native Play offers Test Solo or Test Together. Group testing requires\n'
        'host approval and explicit party readiness for one committed revision.\n'
        'Each player browses acts independently. Use native act properties,\n'
        'timeline add/delete and the lobby Move act left/right controls.\n'
        'Read the guide and validation report for scope and pending release gates.\n\n'
        'This candidate adds shared crew controls, ordered terrain editing and\n'
        'independent asset editors with immutable asset replacement. Native\n'
        'acceptance of these additions is pending. Extended complexity is optional;\n'
        'publication compatibility is not established. Selective history and\n'
        'editing during playtest remain unavailable. Unsupported authoring\n'
        'controls remain held. Lab smoke and sustained acceptance are pending.\n\n'
        'Fork: https://github.com/jwhans0n/SporeCoopGA\n'
        'Based on SporeCoop by WoodyWizard: https://github.com/WoodyWizard/SporeCoop\n',
        encoding='utf-8')
    manifest = {'version': version, 'protocol': protocol, 'supported_exe_sha256': EXE, 'supported_laa_exe_sha256': GOG2024_LAA,
                'complete_release_acceptance': False,
                'payload': {str(p.relative_to(stage)).replace('\\', '/'): sha(p)
                            for p in sorted(stage.rglob('*')) if p.is_file()},
                'distribution': {p.name: sha(p) for p in sorted(distribution.iterdir()) if p.is_file()}}
    (distribution / 'SHA256.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    archive = output / ('SporeCoopGA-' + version + '.zip')
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as bundle:
        for source in sorted(distribution.iterdir()):
            bundle.write(source, 'SporeCoopGA/' + source.name)
    print(json.dumps({'zip': str(archive), 'sha256': sha(archive), 'payload_dll': sha(payload / 'mods/coop.dll')}))


if __name__ == '__main__':
    main()
