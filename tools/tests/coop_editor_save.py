#!/usr/bin/env python3
"""Opt-in native Save/reopen and naturally completed Editor Test checks.

Preparation only until an owner assigns existing hidden, muted lab processes.
This tool never launches games, deploys binaries, repairs saves, calls Recover,
writes memory, invokes native functions or manufactures a mission result.
Save/reopen reads the exact native copy before any collaboration restoration.
Terminal stages require an owner-prepared adventure with a real normal goal
path; they do not configure goals or claim a complete adventure playthrough.
"""
import argparse
import collections
import json
import math
from pathlib import Path
import re
import time

from coop_adventure import FixtureError
from coop_adventure_editor import EditorPair, status
from coop_editor_lifecycle import Lifecycle, authored, inspected
from coop_editor_party import PublicConsole, ROOT, native_play, public_command
from sporectl import Console
from uifind import LINE, match_groups

STAGES = ('save_reopen', 'terminal_success', 'terminal_failure')
VERIFY_COMMAND = 'coop.editor verify-reopen'


def edit_browser(game):
    """Verified reused-profile route, without toggling an already-open radial."""
    if game.caption('Welcome to Spore Galactic Adventures'):
        game.click(473, 516)  # Fallback only while the actual Welcome is visible.
    else:
        if not game.control(0x07A7F798):
            if not game.control(0x03685038):
                game.press('Create')
            game.wait('normal existing Create radial', lambda: game.control(0x07A7B140))
            game.click(553, 522)
            game.wait('normal Galactic Adventures creation choices', lambda: game.control(0x07A7F798))
        game.click(820, 410)  # Measured Edit Adventure, distinct from Create820350.
    game.call('wait.frames 40 15000')


def exact_card_points(groups, title):
    points = set()
    for group in groups:
        # findall is a substring search. Require the full actual card label,
        # including its hidden full-name label when the visible one is clipped.
        caption = re.search(r" '([^']*)'$", group[0]) if group else None
        if not caption or caption[1] != title:
            continue
        for index, line in enumerate(group):
            if 'id=0x0674AA70' not in line:
                continue
            nodes = [LINE.match(value) for value in group[index:]]
            if not nodes or not all(nodes):
                continue
            rects = [(int(node.group(1), 16), *(int(node.group(i)) for i in range(2, 6))) for node in nodes]
            if not all(rect[0] & 1 for rect in rects):
                continue
            _, x1, y1, x2, y2 = rects[0]
            point = (sum(rect[1] for rect in rects[1:]) + (x1+x2)//2,
                     sum(rect[2] for rect in rects[1:]) + (y1+y2)//2)
            if 0 <= point[0] < 1024 and 0 <= point[1] < 768:
                points.add(point)
    return points


def select_exact_card(game, title):
    def unique_card():
        points = exact_card_points(match_groups(game.console, title), title)
        if len(points) > 1:
            raise FixtureError('Multiple visible native cards have the exact private title: ' + title)
        return next(iter(points)) if points else None
    point = game.wait('one visible exact-title native adventure card: ' + title, unique_card)
    for _ in range(4):
        game.click(*point)
        for _ in range(4):
            if status(game).get('ready') and status(game).get('editing'):
                return
            check = game.control(0x00AD0E52)
            if check and check[4]:
                game.click_control(0x00AD0E52)
                return
            time.sleep(.25)
        point = game.wait('same unique native adventure card', unique_card)
    raise FixtureError('Exact native adventure card did not accept normal selection')


def saved_coverage(text):
    """Only explicit exact-serial native ACK diagnostics qualify, never UDP ACKs."""
    result = {}
    for key in ('saved_revision', 'saved_applied', 'saved_expected', 'settled'):
        found = re.search(r'\b' + key + r'=(\d+)\b', text)
        if not found:
            raise FixtureError('The candidate lacks read-only native Save coverage: ' + key)
        result[key] = int(found[1])
    if result['settled'] not in (0, 1) or result['saved_applied'] > result['saved_expected']:
        raise FixtureError('Invalid native Save coverage diagnostic')
    return result


def reopen_receipt(text):
    match = re.search(r'^REOPEN_VERIFIED revision=(\d+) source=([0-9A-F]{8}![0-9A-F]{8}\.[0-9A-F]{8})(?:\s|$)', text)
    if not match:
        raise FixtureError('Read-only native reopen proof did not pass: ' + text)
    return {'revision': int(match[1]), 'source': match[2], 'text': text}


def pose_multiset(objects):
    """Native reopen assigns new IDs: compare authored values with multiplicity."""
    result = collections.Counter()
    for row in objects.values():
        position, rotation = tuple(row['position']), tuple(row.get('rotation', ()))
        if (len(position) != 3 or len(rotation) != 4 or row['scale'] <= 0 or
                not all(map(math.isfinite, (*position, *rotation, row['scale'])))):
            raise FixtureError('Incomplete or invalid native placement diagnostic')
        result[(row['class'], position, rotation, row['scale'])] += 1
    return result


class ReopenConsole(PublicConsole):
    def call(self, command):
        if command == VERIFY_COMMAND:
            if time.monotonic() > self.deadline:
                raise FixtureError('The bounded native save fixture deadline expired')
            return Console.call(self, command)
        return super().call(command)


class NativeSave(Lifecycle):
    def __init__(self, args):
        super().__init__(args)
        self.result.update(kind='native-save-reopen-and-natural-results',
            lifecycle_pass=False, save_reopen_pass=False, natural_result_pass=False,
            complete_acceptance=False, open_gates=[
                'This tool has no execution evidence until its owned native run passes.',
                'Native reopen proof is separate from checkpoint recovery and must run before Recover.',
                'Natural result fixtures require owner-verified ordinary goals; no outcome is injected.',
                'Cold-process native save reopen, broad authored coverage, WAN, FPS and memory remain separate.'])
        for game in self.all_games:
            old = game.console
            console = ReopenConsole(game.instance, self.deadline)
            console.sock, console.buf = old.sock, old.buf
            old.sock, old.buf = None, b''
            game.console = console

    def open_adventure(self, game, title, source=None):
        edit_browser(game)
        select_exact_card(game, title)
        game.wait('selected native adventure editor ready', lambda:
            status(game).get('ready') and status(game).get('editing') and status(game).get('phase') == 0)
        game.call('wait.frames 100 15000')
        current = inspected(game.call('coop.editor inspect'))
        if source is not None and current['source'] != source:
            raise FixtureError('The exact-title native card reopened another resource/version')
        return current

    def bootstrap(self):
        # Pre-open existing adventures ourselves so the pair helper never takes
        # its older Welcome-only wrench route on reused profiles.
        if self.args.fixture == 'existing' and not status(self.host).get('editing'):
            self.open_adventure(self.host, self.args.adventure)
        # EditorParty.bootstrap starts by dismissing Galaxy registration for
        # every peer. An owner-prepared host is already inside its editor, so
        # reuse only the native pair Save As path, then connect the extra peers.
        self.games = self.all_games[:2]
        try:
            EditorPair.bootstrap(self)
        finally:
            self.games = self.all_games
        for game in self.all_games[2:]:
            game.call(f'coop.join 127.0.0.1:{self.args.port} NativeSaveGuest{game.instance}')
            game.wait('additional native save party connection', lambda g=game: 'connected' in g.call('coop.status'))
        self.host.wait('all assigned native save party members', lambda:
            self.host.adventure()['players'] == len(self.all_games))
        self.result['gates']['normal_native_save_as'] = True
        self.record('native-save-all-assigned-offers')

    def seed_move(self):
        self.converge()
        for game in self.active_games:
            game.call('coop.uistate 0')
        time.sleep(1.4)
        objects = self.objects(self.host)
        selected = status(self.host).get('selection')
        target = self.args.object or (selected if selected in objects else next(iter(objects), None))
        if target not in objects:
            raise FixtureError('Native save seeding requires a verified existing placement')
        before = authored(objects)
        self.gesture(self.host, 'native-save-genuine-authored-move',
                     lambda: self.move_object(self.host, target, horizontal=8))
        after = authored(self.objects(self.host))
        if set(before) != set(after) or before[target]['position'] == after[target]['position']:
            raise FixtureError('Save prerequisite did not genuinely move the intended native placement')
        if any(before[key] != after[key] for key in before if key != target):
            raise FixtureError('Native save seeding changed an unrelated placement')
        self.result['gates']['genuine_native_edit_before_save'] = True

    def save_reopen(self):
        self.seed_move()
        before = self.checkpoint('before-genuine-native-save')
        revision = before[str(self.host.instance)]['state']['revision']
        old_source = before[str(self.host.instance)]['inspected']['source']
        self.host.call('coop.editor save')
        self.host.wait('actual native successful Save confirmation', lambda:
            ' saved.' in self.host.call('coop.ui tree 7'))
        self.host.record('genuine-native-save-confirmation')
        self.host.click(772, 470)  # Measured normal native successful-save check.

        def saved_everywhere():
            states = [status(game) for game in self.active_games]
            if any(state.get('frozen') for state in states):
                raise FixtureError('Native save/checkpoint froze a participant')
            if any(state.get('phase') != 4 or state.get('revision') != revision for state in states):
                return False
            coverage = saved_coverage(states[0]['text'])
            if (coverage['saved_revision'] != revision or not coverage['settled'] or
                    coverage['saved_expected'] != len(self.active_games) or
                    coverage['saved_applied'] != coverage['saved_expected']):
                return False
            sources = [inspected(game.call('coop.editor inspect'))['source'] for game in self.active_games]
            return {'coverage': coverage, 'source': sources[0]} if len(set(sources)) == 1 and sources[0] != old_source else False
        saved = self.host.wait('latest native Save and every exact SavedApplied acknowledgement', saved_everywhere)
        self.converge(revision)
        after = self.checkpoint('native-save-acknowledged-on-every-peer')
        for game in self.active_games:
            key = str(game.instance)
            if (authored(before[key]['objects']) != authored(after[key]['objects']) or
                    before[key]['history'] != after[key]['history']):
                raise FixtureError('Native Save changed authored placements or collaboration history')
        expected_poses = pose_multiset(after[str(self.host.instance)]['objects'])
        self.result['native_save'] = dict(saved, revision=revision, source_before=old_source,
            acknowledgements='explicit latest native SavedApplied serial, not transport acknowledgement')
        self.result['gates']['genuine_native_save_all_peers_applied'] = True
        self.write()

        self.host.call('coop.editor end')
        self.host.wait('settled End detaches all native owners', lambda: all(
            status(game).get('phase') == 0 and not status(game).get('playtesting') for game in self.active_games))
        for game in self.active_games:
            self.exit_detached(game)
        self.result['gates']['native_save_closed_normally_before_reopen'] = True
        self.open_adventure(self.host, self.args.title, source=saved['source'])
        reopened = self.checkpoint('native-file-reopened-before-any-recovery', [self.host])[str(self.host.instance)]
        if reopened['inspected']['source'] != saved['source']:
            raise FixtureError('The title selected a different native resource/version')
        if pose_multiset(reopened['objects']) != expected_poses:
            raise FixtureError('The native file reopened with different authored placement values')
        proof = reopen_receipt(self.host.call(VERIFY_COMMAND))
        if proof['revision'] != revision or proof['source'] != saved['source']:
            raise FixtureError('The byte-authored native proof belongs to another saved boundary')
        self.result['native_reopen'] = {'proof': proof, 'source': reopened['inspected']['source'],
            'raw_native_sha256': reopened['inspected']['native_sha256'],
            'raw_sha_is_diagnostic': True, 'recover_called': False,
            'known_pose_multiset_preserved': True, 'opaque_authored_fields_verified': True}
        self.result['gates']['exact_native_file_reopen_before_recovery'] = True
        self.result['save_reopen_pass'] = True

    def natural_result(self, success):
        before = self.checkpoint('before-natural-editor-test-result')
        self.prepare(0)
        for game in self.active_games:
            game.call('coop.editor ready 1')
        self.host.wait('every explicit confirmation launches native Editor Test', lambda: all(
            status(game).get('phase') == 6 and status(game).get('playtesting') for game in self.active_games))
        expected = 5 if success else 6
        samples = []
        def terminal():
            rows = [native_play(game) for game in self.active_games]
            samples.append({'time': time.time(), 'native': rows})
            if len(samples) > 2400:
                raise FixtureError('Natural-results sample bound exhausted')
            if any(row and row['state'] in (5, 6) and row['state'] != expected for row in rows):
                raise FixtureError('The owner-prepared adventure naturally reached the other result')
            return rows if all(row and row['state'] == expected for row in rows) else False
        try:
            first = self.host.wait('natural native ' + ('success' if success else 'failure'),
                                   terminal, timeout=self.args.result_timeout)
        finally:
            self.result['natural_samples'] = samples
            self.write()
        time.sleep(2)
        last = terminal()
        if not last or any(a['act'] != b['act'] or a['time_ms'] != b['time_ms'] for a, b in zip(first, last)):
            raise FixtureError('The shared terminal result reopened simulation or advanced an act/clock')
        title = 'Mission Complete!' if success else 'Mission Failed!'
        def panels():
            complete = True
            for game in self.active_games:
                if game.caption(title) and game.caption('Done'):
                    continue
                complete = False
                control = game.control(0x0445C99D)
                if control and control[4] and 0 <= control[0] < control[2] <= 1024 and 0 <= control[1] < control[3] <= 768:
                    game.click_control(0x0445C99D)  # Actual visible ending confirmation.
            return complete
        self.host.wait('actual native mission results panels', panels, timeout=45)
        for game in self.active_games:
            game.record('natural-native-mission-results')
        self.result['natural_result'] = {'expected': expected, 'title': title, 'first': first,
            'last': last, 'outcome_injected': False, 'complete_adventure_playthrough': False,
            'return_action': self.args.result_return}
        if self.args.result_return == 'done':
            self.host.press('Done')
            self.host.wait('normal native results Done returns every editor', lambda: all(
                status(game).get('phase') == 4 and status(game).get('ready') and
                not status(game).get('playtesting') for game in self.active_games))
            self.converge()
        else:
            self.return_native()
        self.same_draft(before, self.checkpoint('editor-after-natural-results'), local_context=True)
        self.result['gates']['natural_shared_' + ('success' if success else 'failure') + '_and_native_return'] = True
        # A second ordinary Prepare/Cancel proves retry, without forcing a result.
        self.prepare(0)
        self.host.call('coop.editor cancel')
        self.host.wait('retry preparation cancels normally', lambda: all(
            status(game).get('phase') == 4 and status(game).get('ready') for game in self.active_games))
        self.same_draft(before, self.checkpoint('native-result-immediate-preparation-retry'), local_context=True)
        self.result['natural_result_pass'] = True


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--instances', type=int, nargs='+')
    parser.add_argument('--expected-dll-sha256')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--stage', choices=STAGES)
    parser.add_argument('--start', choices=('galaxy', 'native', 'ready'), default='ready',
        help='native: owner-prepared unshared host editor plus idle Galaxy guests; normal Save As starts sharing')
    parser.add_argument('--fixture', choices=('blank', 'existing'), default='blank')
    parser.add_argument('--adventure', default='Alpine Valley')
    parser.add_argument('--title', help='exact unique title of the private native copy, at most28 ASCII characters')
    parser.add_argument('--object', help='verified native placement ID for the genuine pre-Save move')
    parser.add_argument('--result-return', choices=('done', 'hammer'), default='done')
    parser.add_argument('--result-timeout', type=int, default=300)
    parser.add_argument('--port', type=int, default=59287)
    parser.add_argument('--seconds', type=int, default=1800)
    parser.add_argument('--timeout', type=int, default=120)
    args = parser.parse_args()
    if args.self_test:
        assert saved_coverage('saved_revision=9 saved_applied=4 saved_expected=4 settled=1')['saved_applied'] == 4
        assert reopen_receipt('REOPEN_VERIFIED revision=9 source=408A0000!12345678.366A930D\n')['revision'] == 9
        sample = {'1:1': {'class': 8, 'position': (1, 2, 3), 'rotation': (0, 0, 0, 1), 'scale': 1}}
        assert pose_multiset(sample) == pose_multiset({'88:9': sample['1:1']})
        assert sum(pose_multiset({'1:1': sample['1:1'], '2:1': sample['1:1']}).values()) == 2
        for operation, value in ((saved_coverage, 'saved_revision=9'),
                (reopen_receipt, 'REOPEN_FAILED'), (reopen_receipt, 'prefix REOPEN_VERIFIED revision=9 source=408A0000!12345678.366A930D')):
            try:
                operation(value)
            except FixtureError:
                pass
            else:
                raise AssertionError('Proof parser accepted incomplete evidence')
        assert public_command('coop.editor save') and not public_command('coop.adventure.world finish success')
        class Menu:
            def __init__(self, state):
                self.state = state; self.events = []
            def caption(self, value):
                return self.state == 'welcome' and value == 'Welcome to Spore Galactic Adventures'
            def control(self, value):
                return ((self.state == 'ga' and value == 0x07A7F798) or
                        (self.state == 'radial' and value in (0x03685038, 0x07A7B140)))
            def click(self, x, y):
                self.events.append(('click', x, y))
                if (x, y) == (553, 522): self.state = 'ga'
            def press(self, caption):
                self.events.append(('press', caption)); self.state = 'radial'
            def wait(self, description, predicate):
                assert predicate(), description
            def call(self, command):
                self.events.append(('call', command))
        for initial, actions in (
                ('ga', [('click',820,410)]),
                ('radial', [('click',553,522),('click',820,410)]),
                ('closed', [('press','Create'),('click',553,522),('click',820,410)]),
                ('welcome', [('click',473,516)])):
            menu = Menu(initial); edit_browser(menu)
            assert menu.events[:-1] == actions
        label = "Window 1 id=0x53D6FE29 cmd=0 fl=0x0 [0 0 10 10] 'Exact Private Title'"
        card = "Window 2 id=0x0674AA70 cmd=0 fl=0x1 [20 40 120 140] ''"
        assert exact_card_points([[label,card]], 'Exact Private Title') == {(70,90)}
        assert not exact_card_points([[label,card]], 'Exact Private')
        assert not exact_card_points([[label,card.replace('fl=0x1','fl=0x0')]], 'Exact Private Title')
        class Objects:
            def call(self, command):
                assert command == 'coop.editor objects'
                return ('1:1 class=8 pos=1,2,3 scale=2 screen=hidden rotation=0,0,0,1\n'
                        '2:1 class=8 pos=4,5,6 scale=2 screen=hid')
        objects = EditorPair.objects(None, Objects())
        assert set(objects) == {'1:1'} and objects['1:1']['screen'] is None
        assert objects['1:1']['rotation'] == (0, 0, 0, 1)
        print('PASS: pure native Save/reopen parsers and no-synthetic-outcome boundaries; no game connection')
        return 0
    if not args.execute or not all((args.metadata, args.instances, args.expected_dll_sha256, args.output, args.stage, args.title)):
        parser.error('Requires --execute, ownership metadata, exact DLL hash, instances, output, stage and exact private title')
    if (len(args.instances) not in (2, 4) or len(set(args.instances)) != len(args.instances) or
            min(args.instances) < 2 or max(args.instances) > 100):
        parser.error('Two or four distinct private instances2..100 required')
    if (not re.fullmatch('[0-9a-fA-F]{64}', args.expected_dll_sha256) or not 60 <= args.seconds <= 3500 or
            not 1 <= args.timeout <= 300 or not 1 <= args.port <= 65535 or
            not 10 <= args.result_timeout <= 1200 or args.result_timeout >= args.seconds or
            not re.fullmatch(r'[A-Za-z0-9 _-]{1,28}', args.title) or
            args.object is not None and not re.fullmatch(r'[1-9][0-9]*:[1-9][0-9]*', args.object)):
        parser.error('Exact SHA, bounded timing/port, unique ASCII title and valid optional stable ID required')
    args.output = args.output.resolve()
    if (ROOT / 'out').resolve() not in args.output.parents:
        parser.error('Output must be a child of this worktree out directory')
    args.expected_dll_sha256 = args.expected_dll_sha256.lower()
    args.host, args.guest = args.instances[:2]
    args.recovery_port = args.port + 1 if args.port < 65535 else args.port - 1
    args.roundtrip_cycles = 0; args.resume_result = None; args.onboard_host = False
    args.history_input = 'hotkeys'; args.direct_cancel = True
    args.body_offset = 18; args.settle_seconds = 2; args.beam_check = True
    party = None
    try:
        party = NativeSave(args)
        if args.start in ('galaxy', 'native'):
            guests = party.all_games if args.start == 'galaxy' else party.all_games[1:]
            if any(not game.menu() or status(game).get('phase') != 0 for game in guests):
                raise FixtureError('Start requires every assigned guest settled at Galaxy')
            if args.start == 'native':
                state = status(party.host)
                if state.get('phase') != 0 or not state.get('editing') or not state.get('ready') or state.get('gesture'):
                    raise FixtureError('Native start requires a detached ready unshared host editor')
            party.bootstrap()
            party.join()
        else:
            party.converge()
        if args.stage == 'save_reopen':
            party.save_reopen()
        else:
            party.natural_result(args.stage == 'terminal_success')
        party.result.update(lifecycle_pass=True)
        party.result['pass'] = True
        if not party.cleanup():
            raise FixtureError('Native checks passed but normal owned cleanup did not complete')
        party.write()
        return 0
    except Exception as error:
        if party:
            party.result.update(error=str(error), lifecycle_pass=False)
            party.result['pass'] = False
            try:
                party.checkpoint('native-save-results-failure')
            except Exception:
                party.write()
            party.cleanup(failed=True)
        else:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / 'result.json').write_text(json.dumps({'pass': False, 'error': str(error)}, indent=2)+'\n', encoding='utf-8')
        print(str(error))
        return 1
    finally:
        if party:
            party.close()


if __name__ == '__main__':
    raise SystemExit(main())
