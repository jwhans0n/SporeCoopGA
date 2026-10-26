#!/usr/bin/env python3
"""Opt-in native editor lifecycle checks in an already-owned hidden Windows lab.

No launches, deployment, memory writes, external save writes or synthetic results.
The existing owner must explicitly assign two or four isolated processes. Normal
native Save As, discard and Build-hammer input is limited to those processes.
"""
import argparse
import json
import math
from pathlib import Path
import re
import time

from coop_adventure import FixtureError
from coop_adventure_editor import EditorPair, status
from coop_editor_party import EditorParty, native_play, parse_history, parse_identity, public_command

ROOT = Path(__file__).resolve().parents[2]
STAGES = ('cancel_preparation', 'native_return', 'disconnect_preparing',
          'disconnect_playing', 'imported_recovery')


def inspected(text):
    result = {}
    for key, pattern in (
            ('source', r'\bsource=([0-9A-F]{8}![0-9A-F]{8}\.[0-9A-F]{8})'),
            ('native_sha256', r'\bnative_sha256=([0-9a-f]{64})'),
            ('semantic_sha256', r'\bdocument_semantic_sha256=([0-9a-f]{64})'),
            ('placements', r'\bplacements=(\d+)')):
        match = re.search(pattern, text)
        if not match:
            raise FixtureError('Incomplete native inspection: ' + text)
        result[key] = int(match[1]) if key == 'placements' else match[1]
    return result


def authored(objects):
    return {key: {field: value for field, value in row.items() if field != 'screen'}
            for key, row in objects.items()}


def camera_drift(before, after):
    drift = {}
    for key, row in before.items():
        point, other = row.get('screen'), after.get(key, {}).get('screen')
        if point is None or other is None:
            if point != other:
                drift[key] = 'visibility changed'
        elif math.dist(point, other) >= 1:
            drift[key] = math.dist(point, other)
    return drift


class Lifecycle(EditorParty):
    def __init__(self, args):
        super().__init__(args)
        self.result.update(kind='native-editor-lifecycle', complete_acceptance=False,
            lifecycle_pass=False, gates={}, lifecycle=[], open_gates=[
                'Native ESC/exit return is not scripted; only verified Build hammer07104130.',
                'Transport loss/reordering, cold-process recovery, FPS and memory remain separate gates.',
                'Disconnect stages preserve the detached draft then rejoin; independent imported recovery is a separate stage.'])
        self.result['native_integrity_policy'] = (
            'Require identical verified CONVERGED checks plus exact semantics, history and source. '
            'Raw native SHA stays diagnostic: the verified authored comparison normalizes documented '
            'local camera and derived placement fields. A changed raw SHA is not attributed to one field.')
        if not args.direct_cancel:
            self.result['open_gates'].append(
                'Direct host preparation Cancel is untested; cancellation uses guest End plus normal rejoin.')
        for game in self.all_games:
            original = game.call
            def guarded(command, required=True, game=game, original=original):
                words = command.split()
                mutating = words and (words[0] in {
                    'move', 'click', 'drag', 'button', 'key', 'wheel', 'text',
                    'coop.host', 'coop.join', 'coop.leave'} or
                    words[0] == 'coop.editor' and len(words) > 1 and words[1] in {
                        'begin', 'join', 'save', 'playtest', 'cancel', 'ready', 'return', 'end', 'recover'})
                if mutating:
                    self.ownership.verify_windows()
                    self.verify(game)
                return original(command, required)
            game.call = guarded

    def checkpoint(self, label, games=None):
        self.ownership.verify_windows()
        rows = {}
        for game in self.all_games if games is None else games:
            self.verify(game)
            fault = game.call('exc')
            if 'total faults: 0' not in fault:
                raise FixtureError('Native fault on instance ' + str(game.instance) + ': ' + fault)
            state = status(game)
            row = {'state': state, 'session': game.call('coop.status')}
            if state.get('ready'):
                history_text = game.call('coop.editor history')
                history = parse_history(history_text)
                count = re.search(r'^local=\d+:\d+ count=(\d+)$', history_text, re.M)
                if not count or len(history) != int(count[1]):
                    raise FixtureError('Lifecycle history diagnostic is incomplete or truncated')
                row.update(inspected=inspected(game.call('coop.editor inspect')),
                    objects=self.objects(game), history=history)
                if row['inspected']['placements'] != len(row['objects']):
                    raise FixtureError('Lifecycle checks require a complete small-fixture object diagnostic')
                for value in row['objects'].values():
                    numbers = (*value['position'], *value.get('rotation', ()), value['scale'])
                    if (len(value['position']) != 3 or len(value.get('rotation', ())) != 4 or
                            not all(map(math.isfinite, numbers)) or value['scale'] <= 0):
                        raise FixtureError('Invalid native authored placement diagnostic')
                if state.get('phase') == 4:
                    row['check'] = game.call('coop.editor check').strip()
                    if not row['check'].startswith('CONVERGED '):
                        raise FixtureError('Native authored integrity failed: ' + row['check'])
            rows[str(game.instance)] = row
            game.record(label)
        self.result['lifecycle'].append({'label': label, 'time': time.time(), 'games': rows})
        self.write()
        return rows

    def same_draft(self, before, after, games=None, local_context=False):
        for game in self.active_games if games is None else games:
            key = str(game.instance)
            old, new = before[key], after[key]
            if (not old.get('check') or old.get('check') != new.get('check') or
                    old['state']['revision'] != new['state']['revision'] or
                    old['history'] != new['history'] or
                    old['inspected']['source'] != new['inspected']['source'] or
                    old['inspected']['semantic_sha256'] != new['inspected']['semantic_sha256'] or
                    authored(old['objects']) != authored(new['objects'])):
                raise FixtureError('Lifecycle changed authored draft/history on instance ' + key)
            if local_context and (old['state']['selection'] != new['state']['selection'] or
                    camera_drift(old['objects'], new['objects'])):
                raise FixtureError('Lifecycle changed local native camera/selection/resource on instance ' + key)

    def prepare(self, ready_count=0):
        self.converge()
        self.host.call('coop.editor playtest')
        self.host.wait('every participant in preparation', lambda: all(
            status(game).get('phase') == 5 and not status(game).get('playtesting')
            for game in self.active_games))
        for game in self.active_games[:ready_count]:
            game.call('coop.editor ready 1')
        # Do not infer readiness from native ready=1, which means editor loaded.
        # The commanded confirmations and the mandatory unlaunched hold are
        # recorded separately. No beam/results acknowledgements are invented.
        time.sleep(.8)
        if any(status(game).get('phase') != 5 or status(game).get('playtesting')
               for game in self.active_games):
            raise FixtureError('Preparation launched before every explicit Ready')
        self.result['lifecycle'].append({'label': 'preparation-hold', 'ready_actions': ready_count,
                                        'participants': len(self.active_games)})
        self.write()

    def launch_prepared(self):
        for game in self.active_games:
            game.call('coop.editor ready 1')
        self.host.wait('native shared playtest', lambda: all(
            status(game).get('phase') == 6 and status(game).get('playtesting')
            for game in self.active_games))
        def playing():
            rows = [native_play(game) for game in self.active_games]
            return rows if all(row and row['state'] == 3 for row in rows) else False
        before = self.host.wait('natural native playing clocks', playing)
        time.sleep(1.2)
        after = playing()
        if not after or any(row['time_ms'] <= before[i]['time_ms'] for i, row in enumerate(after)):
            raise FixtureError('Natural native playtest clocks did not advance')
        runs = {game.adventure()['run'] for game in self.active_games}
        if len(runs) != 1 or not next(iter(runs)):
            raise FixtureError('Participants entered different preview runs')
        self.result['lifecycle'].append({'label': 'natural-preview', 'before': before, 'after': after,
                                        'run': next(iter(runs)), 'synthetic_ack': False})
        self.write()

    def return_native(self):
        self.host.call('coop.uistate 0')
        rect = self.host.wait('verified top native Build hammer', lambda: self.host.control(0x07104130))
        if not (400 <= rect[0] < rect[2] <= 600 and 0 <= rect[1] < rect[3] <= 80 and rect[4]):
            raise FixtureError('Native Build hammer resolved outside its verified visible top toolbar')
        self.host.click_control(0x07104130)  # Verified native Test Build hammer.
        self.host.wait('group native editor return', lambda: all(
            status(game).get('phase') == 4 and status(game).get('ready') and
            not status(game).get('playtesting') for game in self.active_games))
        self.converge()

    def exit_detached(self, game):
        state = status(game)
        if state.get('phase') not in (0, 1) or state.get('playtesting'):
            raise FixtureError('Native exit requires a detached Idle/Offered editor')
        if game.menu():
            return
        game.call('coop.uistate 0')
        game.click_control(0x90439D7C)  # Verified normal red-X, not Save/Done.
        game.wait('native discard prompt or Galaxy', lambda: game.menu() or game.caption('Quit'))
        if not game.menu():
            game.press('Quit')
        game.wait('Galaxy after private draft discard', game.menu)

    def rejoin(self, game, before, old_capability, transport=True):
        self.exit_detached(game)
        if transport:
            game.call(f'coop.join 127.0.0.1:{self.args.port} LifecycleRejoin{game.instance}')
        game.wait('fresh editor offer', lambda: status(game).get('phase') == 1)
        game.call('coop.editor join')
        self.active_games = list(self.all_games)
        self.converge()
        if parse_identity(status(game)['text'])[1] == old_capability:
            raise FixtureError('Rejoin reused the previous author capability')
        after = self.checkpoint('fresh-native-rejoin')
        self.same_draft(before, after)

    def cancel_preparation(self):
        for count in (0, 1):
            before = self.checkpoint('before-preparation-cancel-' + str(count))
            old_cap = parse_identity(status(self.guest)['text'])[1]
            self.prepare(count)
            if self.args.direct_cancel:
                if not public_command('coop.editor cancel'):
                    raise FixtureError('This harness checkout lacks the reviewed direct Cancel command boundary')
                if count == 0:
                    # Exercise the real Recover-slot caption, layout and click
                    # callback. Never substitute coordinates if it is missing.
                    self.host.wait_caption('Cancel playtest')
                    if self.host.shot('zero-ready-cancel-button') != (1024, 768):
                        raise FixtureError('Cancel UI acceptance requires the verified 1024x768 viewport')
                    self.host.press('Cancel playtest')
                else:
                    self.host.call('coop.editor cancel')
                self.host.wait('cancelled preparation returned to the same draft', lambda: all(
                    status(game).get('phase') == 4 and status(game).get('ready') for game in self.active_games))
                self.converge()
                self.same_draft(before, self.checkpoint('direct-preparation-cancel-' + str(count)),
                                local_context=True)
            else:
                self.guest.call('coop.editor end')
                self.host.wait('guest End cancels preparation and offers rejoin', lambda:
                    status(self.host).get('phase') == 4 and status(self.guest).get('phase') in (0, 1))
                survivors = [game for game in self.all_games if game != self.guest]
                self.active_games = survivors
                self.converge()
                self.same_draft(before, self.checkpoint('guest-end-cancel-' + str(count), survivors), survivors)
                self.rejoin(self.guest, before, old_cap, transport=False)
            self.result['gates']['cancel_preparation_' + str(count) + '_ready'] = True
        # A retry must genuinely play, not merely reopen the confirmation panel.
        before = self.checkpoint('cancel-retry-before-preview')
        self.prepare(); self.launch_prepared(); self.return_native()
        self.same_draft(before, self.checkpoint('cancel-retry-returned'), local_context=True)
        self.result['gates']['cancel_preparation_immediate_retry'] = True

    def native_return(self):
        initial = {game.instance: self.objects(game) for game in self.active_games}
        for index, game in enumerate(self.active_games):
            game.call('coop.uistate 0')
            objects = self.objects(game)
            selected = status(game).get('selection')
            if selected not in objects:
                target = next(iter(objects), None)
                if target is None:
                    raise FixtureError('A real placement selection is required')
                game.click(*self.body(game, target))
                self.selected(game, target)
            # Give each participant a different ordinary local camera before
            # the snapshot. Copying one peer's camera must not accidentally pass.
            notches = (index + 1) * (1 if index % 2 == 0 else -1)
            game.call(f'wheel 780 300 {notches}')
            game.call('wait.frames 20 15000')
        time.sleep(1.4)
        for game in self.active_games:
            current = self.objects(game)
            if not camera_drift(initial[game.instance], current):
                raise FixtureError('Ordinary native wheel did not establish a distinct local camera')
        before = self.checkpoint('before-native-hammer-return')
        self.prepare(); self.launch_prepared(); self.return_native()
        self.same_draft(before, self.checkpoint('after-native-hammer-return'), local_context=True)
        self.result['gates']['native_hammer_group_return_exact_local_context'] = True

    def disconnect(self, playing):
        before = self.checkpoint('before-disconnect-' + str(playing))
        old_cap = parse_identity(status(self.guest)['text'])[1]
        self.prepare(1)
        if playing:
            self.launch_prepared()
        self.guest.call('coop.leave')
        survivors = [game for game in self.all_games if game != self.guest]
        self.active_games = survivors
        self.host.wait('survivors returned and disconnected native owner detached', lambda:
            all(status(game).get('phase') == 4 and status(game).get('ready') for game in survivors)
            and status(self.guest).get('phase') in (0, 1) and status(self.guest).get('ready')
            and not status(self.guest).get('playtesting'))
        self.converge()
        self.same_draft(before, self.checkpoint('disconnect-survivors-' + str(playing), survivors), survivors)
        detached = self.checkpoint('disconnected-private-draft', [self.guest])
        if authored(detached[str(self.guest.instance)]['objects']) != authored(before[str(self.guest.instance)]['objects']):
            raise FixtureError('Disconnect discarded the guest native authored draft')
        self.rejoin(self.guest, before, old_cap)
        self.result['gates']['disconnect_' + ('playing' if playing else 'preparing') + '_rejoin'] = True
        self.result['open_gates'].append(
            'Disconnect native checkpoint preservation was observed; transient Frozen status is not required or inferred.')

    def imported_recovery(self):
        before = self.checkpoint('before-imported-guest-recovery')
        imported = self.guest
        key = str(imported.instance)
        old_source = before[key]['inspected']['source']
        project = parse_identity(before[key]['state']['text'])[0]
        self.host.call('coop.editor end')
        self.host.wait('all native owners detached before recovery', lambda: all(
            status(game).get('phase') == 0 and status(game).get('ready') for game in self.all_games))
        for game in self.all_games:
            game.call('coop.leave')
        imported.call(f'coop.host {self.args.recovery_port} ImportedRecoveryHost')
        imported.call('coop.editor recover')
        imported.wait('ordinary mandatory native Save As success dialog', lambda:
            ' saved.' in imported.call('coop.ui tree 7'))
        if status(imported).get('phase') == 4:
            raise FixtureError('Imported recovery became authoritative before native Save As confirmation')
        imported.record('mandatory-imported-recovery-native-save')
        imported.click(772, 470)  # Existing measured native successful-save check.
        imported.wait('completed native Save As then recovered host', lambda:
            status(imported).get('phase') == 4 and status(imported).get('ready'))
        recovered = self.checkpoint('imported-guest-now-private-host', [imported])[key]
        if (recovered['inspected']['source'] == old_source or
                parse_identity(recovered['state']['text'])[0] != project or
                recovered['state']['revision'] != before[key]['state']['revision'] or
                recovered['history'] != before[key]['history'] or
                authored(recovered['objects']) != authored(before[key]['objects'])):
            raise FixtureError('Imported native Save As did not preserve the draft under a new resource identity')
        self.result['gates']['imported_guest_recovery_mandatory_native_save_as'] = True
        self.result['imported_recovery'] = {'source_before': old_source,
            'source_after': recovered['inspected']['source'], 'original_resource_saved_by_fixture': False}
        # The newly recovered owner is the cleanup authority, even if joining the
        # remaining processes fails. Never keep using the former host's role.
        former = self.host; self.host = imported; self.guest = former
        self.args.port = self.args.recovery_port
        for game in self.all_games:
            if game == imported:
                continue
            self.exit_detached(game)
            game.call(f'coop.join 127.0.0.1:{self.args.port} RecoveredPeer{game.instance}')
            game.wait('recovered host offer', lambda g=game: status(g).get('phase') == 1)
            game.call('coop.editor join')
        self.active_games = list(self.all_games); self.converge()
        self.checkpoint('imported-recovery-shared-again')

    def cleanup(self, failed=False):
        receipt = {'complete': False, 'errors': [], 'forced_native_dialogs': False}
        deadline = min(self.deadline, time.monotonic() + (15 if failed else self.args.timeout))
        try:
            self.ownership.verify_windows()
            for game in self.all_games:
                if time.monotonic() >= deadline:
                    break
                try:
                    game.call('coop.editor end')
                except Exception as error:
                    receipt['errors'].append(str(error))
            while time.monotonic() < deadline:
                if all(status(game).get('phase') in (0, 1) for game in self.all_games):
                    receipt['complete'] = True
                    break
                time.sleep(.2)
            if receipt['complete'] and not failed:
                for game in self.all_games:
                    self.exit_detached(game)
            for game in self.all_games:
                if time.monotonic() >= deadline:
                    break
                try:
                    game.call('coop.leave')
                except Exception as error:
                    receipt['errors'].append(str(error))
        except Exception as error:
            receipt['errors'].append(str(error))
        receipt['owner_must_settle_native_windows'] = failed or not receipt['complete']
        self.result['cleanup'] = receipt; self.write()
        return receipt['complete'] and not receipt['errors']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test', action='store_true', help='pure checks; never connect to games')
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--instances', type=int, nargs='+')
    parser.add_argument('--expected-dll-sha256')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--start', choices=('galaxy', 'ready'), default='galaxy')
    parser.add_argument('--stage', choices=('all',) + STAGES, default='all')
    parser.add_argument('--direct-cancel', action='store_true', help='requires the reviewed C13 Cancel API')
    parser.add_argument('--seed-native-move', action='store_true',
                        help='make one ordinary shared host move before lifecycle baselines')
    parser.add_argument('--port', type=int, default=59187)
    parser.add_argument('--recovery-port', type=int, default=59188)
    parser.add_argument('--seconds', type=int, default=1800)
    parser.add_argument('--timeout', type=int, default=120)
    parser.add_argument('--title', default='Native Lifecycle Private Fixture')
    args = parser.parse_args()
    if args.self_test:
        value = inspected('placements=1 source=408A0000!12345678.366A930D\n'
                          'native_sha256=' + 'a'*64 + '\ndocument_semantic_sha256=' + 'b'*64)
        assert value['placements'] == 1 and value['semantic_sha256'] == 'b'*64
        assert not camera_drift({'1:1': {'screen': (1, 2)}}, {'1:1': {'screen': (1.5, 2)}})
        assert camera_drift({'1:1': {'screen': (1, 2)}}, {'1:1': {'screen': (2, 2)}})
        assert authored({'1:1': {'screen': (1, 2), 'scale': 1}}) == {'1:1': {'scale': 1}}
        assert public_command('coop.editor recover') and not public_command('call Unsafe thiscall')
        print('PASS: lifecycle pure parsers and boundaries; no game connection')
        return 0
    if not args.execute or not all((args.metadata, args.instances, args.expected_dll_sha256, args.output)):
        parser.error('Requires --execute, ownership metadata, exact candidate hash, instances and output')
    if (len(args.instances) not in (2, 4) or len(set(args.instances)) != len(args.instances)
            or min(args.instances) < 2 or max(args.instances) > 100):
        parser.error('Two or four distinct private instances2..100 required')
    if (not re.fullmatch('[0-9a-fA-F]{64}', args.expected_dll_sha256) or
            not 60 <= args.seconds <= 3500 or not 1 <= args.timeout <= 300 or
            not 1 <= args.port <= 65535 or not 1 <= args.recovery_port <= 65535 or
            args.port == args.recovery_port):
        parser.error('Exact candidate SHA, distinct valid ports and bounded settings required')
    args.output = args.output.resolve()
    if (ROOT / 'out').resolve() not in args.output.parents:
        parser.error('Output must be a child of this worktree out directory')
    args.expected_dll_sha256 = args.expected_dll_sha256.lower()
    args.host, args.guest = args.instances[:2]
    args.fixture = 'blank'; args.adventure = 'Alpine Valley'; args.roundtrip_cycles = 0
    args.resume_result = None; args.onboard_host = False; args.history_input = 'hotkeys'
    party = None
    try:
        party = Lifecycle(args)
        if args.start == 'galaxy':
            if any(not game.menu() or status(game).get('phase') != 0 for game in party.all_games):
                raise FixtureError('Galaxy start requires every assigned process settled at Galaxy')
            if len(args.instances) == 2:
                EditorPair.bootstrap(party)
            else:
                party.bootstrap()
            party.join()
        else:
            party.converge()
        if args.seed_native_move:
            for game in party.active_games:
                game.call('coop.uistate 0')
            time.sleep(1.4)
            objects = party.objects(party.host)
            selected = status(party.host).get('selection')
            target = selected if selected in objects else next(iter(objects), None)
            if target is None:
                raise FixtureError('Native history seeding requires an existing placement')
            party.gesture(party.host, 'native-lifecycle-history-seed',
                          lambda: party.move_object(party.host, target, horizontal=8))
            party.result['gates']['genuine_native_history_seed'] = True
        for stage in STAGES if args.stage == 'all' else (args.stage,):
            if stage == 'disconnect_preparing':
                party.disconnect(False)
            elif stage == 'disconnect_playing':
                party.disconnect(True)
            else:
                getattr(party, stage)()
            party.write()
        party.result['lifecycle_pass'] = True
        party.result['pass'] = True
        if not party.cleanup():
            raise FixtureError('Lifecycle checks passed but normal cleanup did not complete')
        party.write()
        return 0
    except Exception as error:
        if party:
            party.result.update(lifecycle_pass=False, error=str(error))
            party.result['pass'] = False
            try:
                party.checkpoint('lifecycle-failure')
            except Exception:
                party.write()
            party.cleanup(failed=True)
        else:
            args.output.mkdir(parents=True, exist_ok=True)
            (args.output / 'result.json').write_text(json.dumps({'pass': False, 'error': str(error)},
                indent=2)+'\n', encoding='utf-8')
        print(str(error))
        return 1
    finally:
        if party:
            party.close()


if __name__ == '__main__':
    raise SystemExit(main())
