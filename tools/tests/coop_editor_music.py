#!/usr/bin/env python3
"""Check music admission on an already-owned private two-player native editor.

The host must have the reported adventure open. Uses normal editor input,
public co-op actions and read-only native music IDs. No deployment, game launch,
profile/database rewrite, memory writes or native-function invocation.
"""
import argparse
import json
from pathlib import Path
import re
import struct

from adventure_memory import AdventureMemory
from coop_adventure import Game, verify_lab
from coop_adventure_editor import status
from coop_editor_party import Ownership, parse_history


def music_ids(memory):
    mode = memory.pointer(memory.singleton)
    resource = memory.pointer(memory.pointer(mode + 0x74) + 0x10)
    first, last = struct.unpack('<II', memory.read(resource + 0x23C, 8))
    if last <= first or (last-first) % 0x534 or last-first > 8*0x534:
        raise RuntimeError('Invalid native act vector')
    return [memory.pointer(row + 0x80) for row in range(first, last, 0x534)]


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    owner = Ownership(args.metadata, args.instances, args.dll_sha256)
    games = [Game(i, args.output, 120) for i in args.instances]
    records = verify_lab(args.metadata, args.instances)
    memories = []
    result = dict(passed=False, dll_sha256=args.dll_sha256, expect_rejection=args.expect_rejection)
    host, guest = games

    def converged(revision=None):
        rows = [status(g) for g in games]
        if any(row.get('frozen') for row in rows):
            raise RuntimeError('Music admission froze a shared editor')
        if not all(row.get('phase') == 4 and not row.get('gesture') and
                   (revision is None or row.get('revision') == revision) for row in rows):
            return False
        checks = [g.call('coop.editor check').strip() for g in games]
        return checks[0].startswith('CONVERGED ') and len(set(checks)) == 1

    try:
        owner.verify_windows()
        if not status(host).get('editing'):
            raise RuntimeError('Open the reported adventure in the private host editor first')
        host.wait('reported native editor finishes loading', lambda: status(host).get('ready'), timeout=300)
        memories.append(AdventureMemory(host, records[host.instance]))
        baseline = music_ids(memories[0])
        if len(baseline) <= args.act or baseline[args.act] != args.music:
            raise RuntimeError('The private native adventure does not contain the reported music')
        result['authored_music_before'] = baseline
        if status(host).get('phase') == 0:
            host.call('coop.editor begin', required=False)
        if args.expect_rejection:
            text = host.call('coop.editor status')
            if 'missing native audio dependency' not in text or f'{args.music:08X}' not in text:
                raise RuntimeError('The baseline did not reproduce the reported rejection')
            result['rejection'] = text
        else:
            host.wait('both native editors converge with reported music', converged, timeout=300)
            memories.append(AdventureMemory(guest, records[guest.instance]))
            if any(music_ids(memory) != baseline for memory in memories):
                raise RuntimeError('Sharing changed an authored music ID')
            revision = status(host)['revision']
            history_count = len(parse_history(host.call('coop.editor history')))
            for game, act in ((host, args.act), (guest, 0)):
                game.call('coop.uistate 0')
                if not re.search(r'^editor_act=' + str(act) + r'\b', game.call('coop.editor acts')):
                    game.click_control(0x0743B8E1 + act)
                game.wait('independent native act view', lambda game=game, act=act:
                          bool(re.search(r'^editor_act=' + str(act) + r'\b', game.call('coop.editor acts'))))
            def authored(game):
                return tuple(line for line in game.call('coop.editor acts').splitlines()
                             if line.startswith(('act=', ' goal=')))
            original = authored(guest)
            if not guest.caption('Description & Music'):
                guest.click_control(0x0743B978)
            guest.click_control(0x0710A140)
            guest.wait('act property ownership', lambda:
                       'lease scope=Settings ' in guest.call('coop.editor leases'))
            # A native checkbox avoids text-entry/focus timing in a cold,
            # large scene. Do not infer a commit from an edited text buffer.
            guest.click_control(0x07462210)
            host.wait('guest edit commits with raw act music', lambda: converged(revision + 1))
            if len(parse_history(host.call('coop.editor history'))) != history_count + 1:
                raise RuntimeError('The guest edit did not produce one history item')
            if authored(host) == original:
                raise RuntimeError('The guest edit did not reach the host')
            guest.call('coop.editor undo')
            host.wait('guest Undo preserves authored music', lambda: converged(revision + 2))
            if any(authored(game) != original for game in games):
                raise RuntimeError('Undo did not restore native act metadata')
            result['authored_music_after'] = [music_ids(memory) for memory in memories]
            if any(values != baseline for values in result['authored_music_after']):
                raise RuntimeError('Editing or Undo changed an authored music ID')
            result['participants'] = [status(g) for g in games]
        result['faults'] = [g.call('exc') for g in games]
        if any('total faults: 0' not in value for value in result['faults']):
            raise RuntimeError('A native fault occurred')
        if args.screenshots:
            for game in games:
                game.record('music-admission')
        result['passed'] = True
        print('PASS: ' + ('baseline music rejection reproduced' if args.expect_rejection else
                         'two-player editing and Undo preserve native act music'), flush=True)
    except Exception as error:
        result['error'] = repr(error)
        raise
    finally:
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n')
        for memory in memories:
            memory.close()
        for game in games:
            game.save_log()
        owner.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--instances', type=int, nargs=2, default=[95, 96])
    parser.add_argument('--dll-sha256')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--act', type=int, default=5, help='Zero-based reported act')
    parser.add_argument('--music', type=lambda value: int(value, 16), default=0x8F7E1796)
    parser.add_argument('--expect-rejection', action='store_true')
    parser.add_argument('--screenshots', action='store_true',
                        help='Opt in to native backbuffer readback on large scenes')
    args = parser.parse_args()
    if not args.execute:
        print(json.dumps(dict(prepared=True, launches_games=False)))
        return
    if (not args.metadata or not args.output or len(set(args.instances)) != 2 or
            not 0 <= args.act < 8 or not 0 < args.music < 0xFFFFFFFF or
            not re.fullmatch('[0-9a-f]{64}', args.dll_sha256 or '')):
        parser.error('Exact owned metadata, DLL SHA, output and bounded act/music are required')
    run(args)


if __name__ == '__main__':
    main()
