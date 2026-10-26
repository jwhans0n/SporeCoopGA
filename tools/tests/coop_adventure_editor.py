#!/usr/bin/env python3
"""Real native Adventure Editor acceptance against an already owned Windows lab.

The bounded WindowsLab runner owns hidden/muted processes. This fixture uses
ordinary UI input and public co-op actions only. It never writes game memory,
changes personal profiles, invents native outcomes, or edits database files.
Use --stage for diagnostic partial runs; --stage all reports the two-peer workflow, not full project acceptance.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from coop_adventure import Game, FixtureError, verify_lab
from adventure_interaction_fixture import _verify_game
from sporectl import quote


def status(game):
    text = game.call('coop.editor status')
    values = dict((key, int(value)) for key, value in re.findall(
        r'\b(revision|durable|frozen|phase|available|editing|ready|playtesting|gesture|captures|restores|rejected)=(\d+)', text))
    values['text'] = text
    return values


class EditorPair:
    def __init__(self, args):
        self.args = args
        self.output = args.output.resolve()
        self.output.mkdir(parents=True, exist_ok=True)
        self.records = verify_lab(args.metadata, (args.host, args.guest))
        self.games = [Game(i, self.output, args.timeout) for i in (args.host, args.guest)]
        self.host, self.guest = self.games
        self.result = {'stage': args.stage, 'checks': []}
        for game in self.games:
            self.verify(game)

    def verify(self, game):
        _verify_game(game, self.records[game.instance])

    def record(self, label):
        for game in self.games:
            self.verify(game)
            game.record(label)
        states = [status(g) for g in self.games]
        checkpoint = {'label': label, 'states': states}
        if all(state.get('ready') for state in states):
            checkpoint['native_checks'] = [g.call('coop.editor check').strip() for g in self.games]
            checkpoint['native_objects'] = [g.call('coop.editor objects').strip() for g in self.games]
            checkpoint['history'] = [g.call('coop.editor history').strip() for g in self.games]
        self.result['checks'].append(checkpoint)
        self.write()

    def write(self):
        (self.output / 'result.json').write_text(json.dumps(self.result, indent=2) + '\n', encoding='utf-8')
        for game in self.games:
            game.save_log()

    def converge(self, revision=None):
        def settled():
            states = [status(g) for g in self.games]
            if any(s.get('frozen') for s in states):
                raise FixtureError('Shared editor froze: ' + repr(states))
            if any(s.get('phase') != 4 or s.get('gesture') for s in states):
                return False
            if revision is not None and any(s.get('revision') != revision for s in states):
                return False
            checks = [g.call('coop.editor check').strip() for g in self.games]
            return checks[0] if checks[0].startswith('CONVERGED ') and checks[0] == checks[1] else False
        return self.host.wait('matching native canonical editor state', settled)

    def bootstrap(self):
        """Prepare native Town and a genuine native Save As copy.

        The host test profile must have completed native first-use captain setup.
        Its real Welcome Create wrench at (473,516) opens Edit an Adventure.
        Cached duplicate control IDs are deliberately not used for that radial.
        """
        host, guest = self.games
        for game in self.games:
            self.verify(game)
            if not status(game).get('editing'):
                game.dismiss_registration()
        if not status(host).get('editing'):
            if self.args.fixture == 'blank':
                host.press('Create'); host.call('wait.frames 100 15000')
                host.click(553,522); host.call('wait.frames 70 15000')
                host.click(820,350); host.call('wait.frames 40 15000')
                host.choose_card('Alpine Valley')
                host.wait('native editor shell ready', lambda: status(host).get('ready'))
                host.call('wait.frames 100 15000')
                host.click(88,165); host.call('wait.frames 30 15000')
                host.choose_card('Ace'); host.call('wait.frames 25 15000')
                host.call('drag 88 170 601 470'); host.call('wait.frames 35 15000')
                # Leave the verified native placement point free for the
                # collaborative Create test. Move the original through the
                # ordinary editor before any party/session ownership begins.
                native_original=self.objects(host)
                host.click(601,442)
                self.slow_drag(host,(601,442),(500,442))
                host.call('wait.frames 25 15000')
                moved_original=self.objects(host)
                if (len(native_original)!=1 or set(moved_original)!=set(native_original) or
                    next(iter(native_original.values()))['position']==next(iter(moved_original.values()))['position']):
                    raise FixtureError('Ordinary native setup could not move the original away from the test land point')
                host.record('ordinary-native-original-moved')
                # Native CanSave reason2 requires actually placing the avatar,
                # even though its default portrait is already visible.
                host.call('drag 967 655 700 470'); host.call('wait.frames 35 15000')
            else:
                host.open_welcome(); host.click(473,516)
                host.call('wait.frames 40 15000'); host.choose_card(self.args.adventure)
                host.wait('native editor shell ready', lambda: status(host).get('ready'))
        if self.args.roundtrip_cycles:
            for _ in range(self.args.roundtrip_cycles):
                text = host.call('coop.editor roundtrip')
                if 'roundtrip=identical' not in text:
                    raise FixtureError('Native owned snapshot roundtrip failed: ' + text)
        host.call(f'coop.host {self.args.port} EditorHost')
        guest.call(f'coop.join 127.0.0.1:{self.args.port} EditorGuest')
        guest.wait('party connection', lambda: 'connected' in guest.call('coop.status'))
        for game in self.games:
            game.call('coop.uistate 0')
        # A legitimate authored title edit makes native CanSave eligible. Merely
        # opening an untouched Maxis template correctly produces reason5.
        host.click(515, 751)
        host.call('wait.frames 20 15000')  # Native name panel focus/transition.
        host.call('text ' + quote(self.args.title))
        host.click(669, 748)
        host.call('wait.frames 20 15000')
        host.call('coop.editor begin')
        host.wait('native successful save confirmation', lambda: ' saved.' in host.call('coop.ui tree 7'))
        host.record('native-save-confirmation')
        host.click(772, 470)  # Actual native successful-save confirmation check.
        host.wait('host durable shared draft', lambda: status(host).get('phase') == 4)
        guest.wait('shared draft offered', lambda: status(guest).get('phase') == 1)
        self.record('native-save-and-offer')

    def join(self):
        if status(self.guest).get('phase') == 1:
            self.guest.call('coop.editor join')
        self.result['initial_hash'] = self.converge()
        self.record('native-baseline-converged')

    @staticmethod
    def slow_drag(game, source, target):
        sx, sy = source
        tx, ty = target
        game.call(f'move {sx} {sy}')
        game.call('wait.frames 5 15000')
        try:
            game.call(f'button {sx} {sy} down')
            game.call('wait.frames 8 15000')
            for fraction in (.35, .7, 1.):
                game.call(f'move {round(sx + (tx-sx)*fraction)} {round(sy + (ty-sy)*fraction)}')
                game.call('wait.frames 6 15000')
        finally:
            game.call(f'button {tx} {ty} up')
        game.call('wait.frames 20 15000')

    def gesture(self, game, label, action):
        revision = status(self.host)['revision']
        for _ in range(3):
            self.verify(game)
            action()
            # First input may request a lease and be held before native mutation.
            # A second genuine gesture after lease delivery is allowed; no input
            # is replayed by this fixture behind a player's back.
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                state = status(self.host)
                if state.get('frozen'):
                    raise FixtureError('Editor froze after ' + label + ': ' + state['text'])
                if state['revision'] == revision + 1:
                    self.converge(revision + 1)
                    self.record(label)
                    return
                if state['revision'] > revision + 1:
                    raise FixtureError(label + ' produced duplicate canonical gestures')
                time.sleep(.2)
        self.record(label + '-failed')
        raise FixtureError('No canonical gesture accepted for ' + label)

    def objects(self, game):
        result = {}
        for line in game.call('coop.editor objects').splitlines():
            m = re.match(r'(\d+:\d+) class=(\d+) pos=([^ ]+) scale=([^ ]+) screen=([^ ]+)', line)
            if m:
                result[m[1]] = {'class': int(m[2]), 'position': tuple(map(float,m[3].split(','))),
                                'scale': float(m[4]), 'screen': None if m[5] == 'hidden' else tuple(map(float,m[5].split(',')))}
        return result

    def body(self, game, object_id):
        point = self.objects(game).get(object_id, {}).get('screen')
        if point is None:
            raise FixtureError('Native placement is not visible: ' + object_id)
        # Native projection is the feet, while the selectable creature body is
        # above it. This fixture's ordinary Ace captain is scale2.
        return round(point[0]), round(point[1] - 28)

    @staticmethod
    def hotkey(game, key):
        try:
            game.call('key ctrl down'); game.call('key ' + key)
        finally:
            game.call('key ctrl up')

    def edit(self):
        for game in self.games:
            game.call('coop.uistate 0')
        before = self.objects(self.host)
        # The native Alpine editor accepts a buried, unpickable placement at
        # (650,470), even without sharing. The bootstrap leaves the verified
        # native (601,470) land point free, rather than mistaking the buried
        # native placement for a reconstruction failure.
        self.gesture(self.host, 'host-create', lambda: self.slow_drag(self.host, (88,170), (601,470)))
        created = set(self.objects(self.host)) - set(before)
        if len(created) != 1:
            raise FixtureError('Native Create did not add exactly one placement')
        moved = next(iter(created))
        old_position = self.objects(self.host)[moved]['position']
        def move():
            source = self.body(self.host, moved)
            self.slow_drag(self.host, source, (source[0]-30,source[1]))
        self.gesture(self.host, 'host-move', move)
        if self.objects(self.host)[moved]['position'] == old_position:
            raise FixtureError('Accepted Move did not change the native placement position')
        before_clone = set(self.objects(self.host))
        def clone():
            source = self.body(self.host, moved)
            try:
                self.host.call('key alt down')
                self.slow_drag(self.host, source, (source[0]-35,source[1]))
            finally:
                self.host.call('key alt up')
        self.gesture(self.host, 'host-clone', clone)
        clones = set(self.objects(self.host)) - before_clone
        if len(clones) != 1:
            raise FixtureError('Native Clone did not add exactly one placement')
        cloned = next(iter(clones))
        def delete():
            self.host.click(*self.body(self.host, cloned))
            self.host.call('key del')
        self.gesture(self.host, 'host-delete', delete)
        if cloned in self.objects(self.host):
            raise FixtureError('Native Delete retained the selected clone')
        self.gesture(self.host, 'native-chronological-undo', lambda: self.hotkey(self.host, 'z'))
        if cloned not in self.objects(self.host):
            raise FixtureError('Own Undo did not restore the exact deleted placement identity')
        self.gesture(self.host, 'native-chronological-redo', lambda: self.hotkey(self.host, 'y'))
        if cloned in self.objects(self.host):
            raise FixtureError('Own Redo did not remove the restored placement')
        self.gesture(self.guest, 'guest-create', lambda: self.slow_drag(self.guest, (88,170), (601,470)))
        self.result['edited_hash'] = self.converge()

    def playtest(self):
        for cycle in range(self.args.playtest_cycles):
            before = self.converge()
            self.host.call('coop.editor playtest')
            self.host.wait('explicit readiness preparation', lambda: status(self.host).get('phase') == 5)
            for game in self.games:
                game.call('coop.editor ready 1')
            self.host.wait('both native preview submodes', lambda: all(status(g).get('playtesting') for g in self.games))
            self.record(f'playtest-{cycle+1}')
            self.host.call('coop.editor return')
            self.host.wait('both native editors returned', lambda: all(status(g).get('ready') for g in self.games))
            after = self.converge()
            if after != before:
                raise FixtureError('Group playtest return changed the canonical authored draft')
            self.record(f'returned-{cycle+1}')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--metadata', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--host', type=int, default=87); p.add_argument('--guest', type=int, default=88)
    p.add_argument('--port', type=int, default=45187); p.add_argument('--timeout', type=float, default=120)
    p.add_argument('--fixture', choices=('blank','existing'), default='blank')
    p.add_argument('--adventure', default='Adventure Town')
    p.add_argument('--title', default='Native Shared Editor Acceptance')
    p.add_argument('--roundtrip-cycles', type=int, default=1)
    p.add_argument('--playtest-cycles', type=int, default=1)
    p.add_argument('--stage', choices=('all','bootstrap','join','edit','playtest','snapshot'), default='all')
    args = p.parse_args(); pair = None
    try:
        pair = EditorPair(args)
        stages = ('bootstrap','join','edit','playtest') if args.stage == 'all' else (args.stage,)
        for stage in stages:
            if stage == 'snapshot': pair.record('snapshot')
            else: getattr(pair, stage)()
        pair.result['pass'] = True
        pair.result['pair_workflow_pass'] = args.stage == 'all'
        pair.result['complete_acceptance'] = False
        pair.write(); print(json.dumps(pair.result, indent=2)); return 0
    except Exception as error:
        if pair:
            pair.result.update({'pass': False, 'error': str(error)})
            try: pair.record('failure')
            except Exception: pair.write()
        print(str(error), file=sys.stderr); return 1


if __name__ == '__main__':
    raise SystemExit(main())
