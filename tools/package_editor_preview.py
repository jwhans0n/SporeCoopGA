#!/usr/bin/env python3
"""Package the experimental GOG editor preview without changing the game install."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
EXE = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--kit-installation', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    version = re.search(r'^#define COOP_VERSION "([^"]+)"',
                        (ROOT / 'mods/coop/coop.cpp').read_text(), re.M)[1]
    if not re.fullmatch(r'0\.7\.5\.0-ga-editor-preview\.3', version):
        parser.error('This packaging recipe is scoped to editor preview 3')
    kit = args.kit_installation.resolve()
    if sha(kit / 'SporebinEP1/SporeApp.exe') != EXE:
        parser.error('The supporting installation is not the verified GOG executable')
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
                     'docs/reports/EDITOR-NATIVE-FLOW-VALIDATION.md', 'LICENSE']:
        source = ROOT / relative
        if source.suffix == '.md':
            # The archive carries two standalone documents. Keep their source
            # references usable without shipping an incomplete repository tree.
            def link(match):
                target = match[1]
                if re.match(r'[a-zA-Z][\w+.-]*:|#', target):
                    return match[0]
                path, marker, anchor = target.partition('#')
                resolved = (source.parent / path).resolve().relative_to(ROOT)
                return '](' + 'https://github.com/jwhans0n/SporeCoopGA/blob/feat/ga-editor-native-flow/' + resolved.as_posix() + marker + anchor + ')'
            data = re.sub(r'\]\(([^)]+)\)', link, source.read_text(encoding='utf-8'))
            (distribution / source.name).write_text(data, encoding='utf-8')
        else:
            shutil.copy2(source, distribution / source.name)
    (distribution / 'README.txt').write_text(
        f'SporeCoopGA {version} (experimental editor preview, protocol 18)\n\n'
        'Extract this folder. Keep SporeCoopSetup.exe beside its matching .pack.\n'
        'End all game sessions before installing. Select the normal SPORE installation.\n'
        'All participants need this exact preview and Galactic Adventures.\n'
        'Supported SporebinEP1/SporeApp.exe SHA-256:\n' + EXE + '\n\n'
        'Host a party and open the native Adventure Editor at Act 1. Guests join\n'
        'the party and follow automatically. Unsaved drafts can be shared. F9 is\n'
        'the normal party/chat lobby. Use native Undo/Redo, Save, Play and Build.\n'
        'Read the guide and validation report for scope and pending release gates.\n\n'
        'Terrain, nested creation editors, independent acts, selective history and\n'
        'editing during playtest remain unavailable. Other unsupported authoring\n'
        'controls remain held. Sustained release acceptance is pending.\n\n'
        'Fork: https://github.com/jwhans0n/SporeCoopGA\n'
        'Based on SporeCoop by WoodyWizard: https://github.com/WoodyWizard/SporeCoop\n',
        encoding='utf-8')
    manifest = {'version': version, 'protocol': 18, 'supported_exe_sha256': EXE,
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
