#!/usr/bin/env python3
"""Check native Windows smoke prerequisites without starting games or changing profiles.

A READY result qualifies prerequisites only. It never qualifies a native smoke run.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = ('creature.tgz', 'cell.tgz', 'tribe.tgz', 'civ.tgz', 'space.tgz',
            'space_empire.tgz', 'creations-host.package', 'creations-guest.package')
EXES = {'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37',
        '47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab'}

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--fixture-dir', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--installation', type=Path)
    args = parser.parse_args()
    common = Path(subprocess.check_output(['git','rev-parse','--path-format=absolute','--git-common-dir'],
                                         cwd=ROOT, text=True).strip())
    fixtures = (args.fixture_dir or common.parent / 'out' / 'fixtures').resolve()
    sys.path.insert(0, str(ROOT / 'tools' / 'tests'))
    import suite
    cases = [dict(name=test['name'], command=test['cmd']) for test in suite.T if 'smoke' in test['suites']]
    rows = [dict(name=name, present=(fixtures/name).is_file(),
                 sha256=digest(fixtures/name) if (fixtures/name).is_file() else None) for name in REQUIRED]
    missing = [row['name'] for row in rows if not row['present']]
    reasons = ['Missing native fixture: '+name for name in missing]
    if sys.platform != 'win32':
        reasons.append('Run the native Windows smoke launcher on Windows')
    if args.installation:
        exe = args.installation.resolve() / 'SporebinEP1' / 'SporeApp.exe'
        if not exe.is_file() or digest(exe) not in EXES:
            reasons.append('The installation is not an exact supported GOG 2024 original/LAA image')
    report = dict(status='UNAVAILABLE' if reasons else 'READY', native_smoke_passed=False,
                  project=str(ROOT), fixtures=str(fixtures), fixture_files=rows, cases=cases,
                  reasons=reasons, games_started=False, profiles_modified=False,
                  note='READY is not PASS. Run every smoke assertion and native/log gate before promoting main.')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(status=report['status'], missing=missing, cases=len(cases), native_smoke_passed=False)))
    return 2 if reasons else 0

if __name__ == '__main__':
    sys.exit(main())
