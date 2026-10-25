#!/usr/bin/env python3
"""Complete an ordinary native conversation in an existing isolated GA pair.

The caller supplies a visible NPC body coordinate after positioning the captain.
Only mouse clicks and native Next/Finished controls complete the conversation.
No objective callback or result is injected.
"""
import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
from coop_adventure import Game, FixtureError, verify_lab
from adventure_memory import AdventureMemory
from coop_adventure_flow import confirmation_hud


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, default=21)
    parser.add_argument('--guest', type=int, default=22)
    parser.add_argument('--observers',default='')
    parser.add_argument('--x', type=int, required=True)
    parser.add_argument('--y', type=int, required=True)
    parser.add_argument('--metadata', type=Path, default=ROOT / 'out/windows-lab/runtime-bootstrap-3/processes.json')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    observer_ids=tuple(int(x)for x in args.observers.split(',')if x)
    records = verify_lab(args.metadata, (args.host, args.guest)+observer_ids)
    host, guest = Game(args.host, args.out), Game(args.guest, args.out)
    observers=[Game(i,args.out)for i in observer_ids]
    games=[host,guest]+observers
    readers = []
    try:
        readers = [AdventureMemory(g,records[g.instance])for g in games]
        a, b = readers[:2]
        before = a.goals()
        if before['act'] != b.goals()['act']:
            raise FixtureError('captains are in different acts')
        pages = []
        guest.click(args.x, args.y)
        guest.wait('native conversation cinematic opens',
                   lambda: guest.caption('letterbox_top'), timeout=12)
        host.wait('same native conversation on host',lambda: host.caption('letterbox_top'),timeout=12)
        for observer in observers:observer.wait('shared conversation on observer',lambda observer=observer:observer.caption('letterbox_top'),timeout=15)
        host_play=a.pointer(a.pointer(a.singleton)+0x78)
        host.wait('host greeting reaches its first text page',lambda:a.pointer(host_play+0xD4)<0x80000000,timeout=12)
        frozen = a.goals()
        host_page=a.pointer(host_play+0xD4)
        deadline = time.monotonic() + 90
        finished = False
        while time.monotonic() < deadline:
            if guest.control(0x0445C99D):
                pages.append(guest.call('coop.ui tree 8'))
                guest.record('conversation-final-page')
                guest.click_control(0x0445C99D)
                finished = True
                break
            if guest.control(0x062EF353):
                pages.append(guest.call('coop.ui tree 8'))
                guest.click_control(0x062EF353)
            else:
                time.sleep(.3)
        if not finished:
            raise FixtureError('native conversation did not reach its Finished control')
        guest.wait('guest confirmation reaches host',lambda:f'Conversation confirmations: 1/{len(games)}' in host.adventure()['text'])
        time.sleep(3)
        held=a.goals()
        if held != frozen or a.pointer(host_play+0xD4)!=host_page:
            raise FixtureError('guest dialog input advanced host pages, clocks or objectives: '+json.dumps(
                dict(before=frozen,held=held,page_before=host_page,page_after=a.pointer(host_play+0xD4))))
        guest.record('waiting-for-host-dialog')
        confirmation_hud(guest,len(games),'Conversation',True)
        if not guest.caption('letterbox_top'):
            raise FixtureError('local Finished dismissed its native cinematic before group release')
        for participant in observers+[host]:
            finished=False
            deadline=time.monotonic()+90
            while time.monotonic()<deadline:
                if participant.control(0x0445C99D):
                    participant.click_control(0x0445C99D); finished=True; break
                if participant.control(0x062EF353): participant.click_control(0x062EF353)
                else: time.sleep(.3)
            if not finished: raise FixtureError('participant conversation did not reach its Finished control')
        def advanced():
            current = a.goals()
            return current if current['act'] != before['act'] or any(
                new['count'] > old['count'] or new['complete'] and not old['complete']
                for old, new in zip(before['goals'], current['goals'])) else None
        host.wait('native guest conversation contributes to host goals', advanced, timeout=20)
        def replicated():
            x, y = a.goals(), b.goals()
            return y if x['act'] == y['act'] and [
                (g['count'], g['complete']) for g in x['goals']] == [
                (g['count'], g['complete']) for g in y['goals']] else None
        guest.wait('conversation progress replicated', replicated, timeout=20)
        for game in games:
            if 'confirmation_hud=0' not in game.call('coop.adventure.hud'):
                raise FixtureError('conversation confirmation labels survived group release')
        exceptions = [game.call('exc') for game in games]
        if any('total faults: 0' not in value for value in exceptions):
            raise FixtureError('conversation logged a native exception')
        result = dict(result='PASS', profiles=records, before=before, host=a.goals(),
                      guest=b.goals(), pages=pages, exceptions=exceptions,
                      held=held,host_page_while_guest_read=host_page,observers=[m.goals()for m in readers[2:]],
                      source='ordinary NPC mouse click, independent native pages and group Finished controls')
        args.out.mkdir(parents=True, exist_ok=True)
        (args.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('PASS: ordinary guest conversation and shared objective progress')
        return 0
    except (FixtureError, OSError, ValueError) as error:
        print('FAIL:', error)
        for game in games:
            try:
                game.record('failure')
            except (FixtureError, OSError, ValueError):
                pass
        return 1
    finally:
        for reader in readers:
            reader.close()
        for game in games:game.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
