#!/usr/bin/env python3
"""Read-only owned native snapshot export; never opens, saves or restores an editor."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import time

from coop_editor_party import Ownership, PublicConsole, PublicGame, ROOT
from coop_adventure_editor import status
from sporectl import Console

LIMIT = 128 << 20


def byte_command(command):
    found = re.fullmatch(r'coop\.editor inspect bytes ([0-9]{1,9}) ([0-9]{1,4})', command)
    return bool(found and int(found[1]) <= LIMIT and 1 <= int(found[2]) <= 4096)


class AuditConsole(PublicConsole):
    def call(self, command):
        if byte_command(command):
            if time.monotonic() > self.deadline:
                raise ValueError('Native audit deadline expired')
            return Console.call(self, command)
        return super().call(command)


def page(text, offset, identity=None):
    found = re.fullmatch(r'NATIVE_BYTES size=([0-9]{1,9}) offset=([0-9]{1,9}) count=([0-9]{1,4}) sha256=([0-9a-f]{64})\nhex=([0-9a-f]*)\n', text)
    if not found or len(text) > 12 * 1024:
        raise ValueError('Malformed native snapshot page')
    size, actual_offset, count = map(int, found.groups()[:3])
    if not 0 < size <= LIMIT or offset != actual_offset or not 0 <= offset <= size:
        raise ValueError('Native snapshot page identity/offset is invalid')
    if count != min(4096, size - offset) or len(found[5]) != count * 2:
        raise ValueError('Native snapshot page size is invalid')
    current = (size, found[4])
    if identity is not None and current != identity:
        raise ValueError('Native snapshot changed during export')
    return current, bytes.fromhex(found[5])


def context(state):
    found = re.search(r'editor_native[^\n]*\bgeneration=([0-9]+)\b', state['text'])
    if (not found or state['phase'] != 0 or state.get('frozen') or state.get('gesture') or
            state.get('playtesting') or not state.get('editing') or not state.get('ready')):
        raise ValueError('Audit requires an idle detached native editor')
    return int(found[1]), state.get('selection')


def self_test():
    body = bytes(range(256)) * 17
    digest = hashlib.sha256(body).hexdigest()
    def response(at, data, size=len(body), sha=digest):
        return f'NATIVE_BYTES size={size} offset={at} count={len(data)} sha256={sha}\nhex={data.hex()}\n'
    identity, first = page(response(0, body[:4096]), 0)
    _, last = page(response(4096, body[4096:]), 4096, identity)
    assert first + last == body
    for text, offset, previous in ((response(0, body[:4096]), 1, None),
            (response(4096, body[4096:], sha='0'*64), 4096, identity),
            (response(0, body[:4]), 0, None),
            (response(0, body[:4096], size=LIMIT+1), 0, None),
            (response(0, body[:4096]) + 'extra', 0, None)):
        try:
            page(text, offset, previous)
        except ValueError:
            pass
        else:
            raise AssertionError('Invalid page was accepted')
    assert byte_command('coop.editor inspect bytes 0 4096')
    for command in ('coop.editor inspect bytes -1 1', 'coop.editor inspect bytes 0 0',
            'coop.editor inspect bytes 0 4097', 'coop.editor inspect bytes 134217729 1',
            'coop.editor inspect bytes 0 1\ncoop.editor begin'):
        assert not byte_command(command)
    print('PASS: native audit paging and command boundaries; no game connection')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--instances', type=int, nargs='+')
    parser.add_argument('--instance', type=int)
    parser.add_argument('--expected-dll-sha256')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if args.self_test:
        self_test(); return 0
    if not args.execute:
        parser.error('No game connection without --execute')
    if (not args.metadata or not args.instances or len(args.instances) not in (2, 4) or
            args.instance not in args.instances or not args.output or
            not re.fullmatch('[0-9a-f]{64}', args.expected_dll_sha256 or '')):
        parser.error('Exact owned cohort, target, DLL hash and fresh output are required')
    output = args.output.resolve()
    if (ROOT / 'out').resolve() not in output.parents or output.exists():
        parser.error('Output must be a fresh OUT child')
    output.mkdir(parents=True)
    owner = Ownership(args.metadata, args.instances, args.expected_dll_sha256)
    game = None
    result = dict(pass_=False, read_only=True, recover_called=False)
    try:
        owner.verify_windows()
        game = PublicGame(args.instance, output, 30, time.monotonic() + 240)
        original = game.console
        audit = AuditConsole(args.instance, original.deadline)
        audit.sock, audit.buf = original.sock, original.buf
        original.sock, original.buf = None, b''
        game.console = audit
        before = status(game); expected_context = context(before)
        if game.call('coop.status').splitlines()[0] != 'offline':
            raise ValueError('Audit requires a detached offline native editor')
        inspection = game.call('coop.editor inspect')
        identity = None; data = bytearray()
        while identity is None or len(data) < identity[0]:
            owner.verify_windows()
            identity, part = page(game.call(f'coop.editor inspect bytes {len(data)} 4096'), len(data), identity)
            data.extend(part)
        if hashlib.sha256(data).hexdigest() != identity[1]:
            raise ValueError('Reassembled native snapshot hash is invalid')
        after = status(game)
        if (context(after) != expected_context or game.call('coop.editor inspect') != inspection or
                game.call('coop.status').splitlines()[0] != 'offline'):
            raise ValueError('Native editor changed during read-only audit')
        faults = game.call('exc')
        if 'total faults: 0' not in faults:
            raise ValueError('Native fault during audit')
        owner.verify_windows()
        (output / 'current.native').write_bytes(data)
        result.update(pass_=True, size=identity[0], sha256=identity[1],
            before=before, after=after, inspection=inspection, faults=faults)
    except Exception as error:
        result['error'] = str(error)
    finally:
        if game:
            game.save_log()
            if game.console.sock:
                game.console.sock.close()
        owner.close()
        (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result.get(key) for key in ('pass_', 'size', 'sha256', 'error')}))
    return 0 if result['pass_'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
