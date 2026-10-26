#!/usr/bin/env python3
"""Four-player native editor workflow against an already-owned hidden Windows lab.

Preparing a runner never starts it. Running this fixture requires --execute,
matching process metadata and an explicit candidate DLL hash. Inputs are normal
mouse/key input or public co-op actions. No memory writes/native calls, save-file
rewrites, process launches, DLL deployment or personal profiles are used here.
The separate generated runner has a bounded lease and owns its own children.
Even a successful workflow is not complete release acceptance: packet adversity,
large-editor performance and cold-process recovery remain separate gates.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import ctypes
from ctypes import wintypes as W
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import threading
import time

from coop_adventure_editor import EditorPair, status
from coop_adventure import Game, FixtureError, verify_lab
from adventure_interaction_fixture import _verify_game, EXE_SHA256

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from sporectl import Console


def public_command(line):
    if not line.strip() or '\n' in line or '\r' in line:
        return False
    words = line.split()
    command = words[0]
    if command in {'info', 'ping', 'exc', 'game.mode', 'coop.status', 'coop.avatar'}:
        return len(words) == 1
    if command in {'move', 'click', 'drag', 'button', 'key', 'text', 'shot', 'wait.frames'}:
        return len(words) > 1
    if command == 'coop.editor':
        return len(words) == 2 and words[1] in {
            'status', 'objects', 'check', 'inspect', 'history', 'begin', 'join',
            'undo', 'redo', 'save', 'playtest', 'return', 'end', 'recover'} or (
                len(words) == 3 and words[1] == 'ready' and words[2] in {'0', '1'})
    if command == 'coop.ui':
        return len(words) > 1 and words[1] in {'tree', 'where', 'find', 'findall'}
    if command == 'coop.uistate':
        return len(words) == 1 or len(words) == 2 and words[1] in {'0', '1'}
    if command in {'coop.host', 'coop.join'}:
        return len(words) >= 2
    if command == 'coop.leave':
        return len(words) == 1
    if command == 'coop.adventure':
        return words[1:] == ['status']
    if command in {'coop.adventure.world', 'coop.adventure.hud'}:
        return words[1:] in ([], ['status'])
    return False


class PublicConsole(Console):
    def __init__(self, instance, deadline):
        super().__init__(instance, timeout=25)
        self.deadline = deadline

    def call(self, line):
        if time.monotonic() > self.deadline:
            raise FixtureError('The bounded four-player fixture deadline expired')
        if not public_command(line):
            raise FixtureError('Non-public or unsupported fixture command refused: ' + line)
        return super().call(line)


class PublicGame(Game):
    def __init__(self, instance, output, timeout, deadline):
        super().__init__(instance, output, timeout)
        self.console = PublicConsole(instance, deadline)

    def call(self, command, required=True):
        result = super().call(command, required)
        if time.monotonic() >= getattr(self, 'next_log_flush', 0):
            self.save_log()
            self.next_log_flush = time.monotonic() + 5
        return result

    def start_welcome(self):
        self.open_welcome()
        # The cached large-Play control ID also resolves to the lower wrench.
        # Use the verified visible Welcome Play in this fixed native viewport.
        self.click(357, 387)


def parse_identity(text):
    project = re.search(r'\bproject=([a-fA-F0-9]+)', text)
    capability = re.search(r'\bcapability=(\d+):(\d+)', text)
    if not project or not capability:
        raise FixtureError('Project/capability missing from native editor status: ' + text)
    return project.group(1), tuple(map(int, capability.groups()))


def parse_history(text):
    entries = []
    for line in text.splitlines():
        match = re.match(r'author=(\d+):(\d+) sequence=(\d+) epoch=(\d+) revision=(\d+) applied=(\d+) redo=(\d+) (.*)', line)
        if match:
            entries.append((*map(int, match.groups()[:-1]), match.group(8)))
    return entries


def native_play(game):
    text = game.call('coop.adventure.world status')
    match = re.search(r'^act=(-?\d+) state=(-?\d+) time_ms=(-?\d+) remaining_ms=(-?\d+)$', text, re.M)
    return dict(zip(('act', 'state', 'time_ms', 'remaining_ms'), map(int, match.groups()))) if match else None


def parse_party_positions(avatar, session):
    local = re.search(r'^pos=\(([^)]+)\)', avatar, re.M)
    if not local:
        return None
    position = tuple(map(float, local.group(1).split()))
    if len(position) != 3 or not all(math.isfinite(n) for n in position):
        raise FixtureError('The native local captain position is invalid')
    remotes = {}
    for line in session.splitlines():
        match = re.match(r"remote (\d+) '.*' puppet=([0-9A-Fa-fx]+) state=(yes|no) age=([0-9.]+)s pos=\(([^)]+)\)", line)
        if match:
            peer, pointer, state, age, point = match.groups()
            remotes[int(peer)] = dict(puppet=int(pointer, 16), state=state,
                                      age=float(age), position=tuple(map(float, point.split())))
        match = re.match(r'\s+puppet pos=\(([^)]+)\) err=([0-9.]+)', line)
        if match and remotes:
            remotes[next(reversed(remotes))].update(puppet_position=tuple(map(float, match.group(1).split())),
                                                   error=float(match.group(2)))
    together = {int(peer): int(value) for peer, value in re.findall(r'^together (\d+) ([01])$', session, re.M)}
    for peer, row in remotes.items():
        if not row['puppet'] or row['state'] != 'yes' or row['age'] >= 3 or together.get(peer) != 1 or row.get('error', math.inf) > 1:
            return None
        if any(len(row[k]) != 3 or not all(math.isfinite(n) for n in row[k]) for k in ('position', 'puppet_position')):
            raise FixtureError('A native party puppet position is invalid')
    return dict(local=position, remotes=remotes) if len(remotes) == 3 else None


class Ownership:
    """Hold exact process handles and refuse visible/non-lab targets before input."""
    def __init__(self, metadata, instances, expected_dll):
        if os.name != 'nt':
            raise FixtureError('Native acceptance requires Windows Python')
        self.records = verify_lab(metadata, instances)
        self.metadata = json.loads(Path(metadata).read_text(encoding='utf-8-sig'))
        self.installation = Path(self.metadata['installation']).resolve()
        if not any(parent.name == 'windows-lab' and parent.parent.name == 'out' for parent in self.installation.parents):
            raise FixtureError('Only a private out/windows-lab installation is accepted')
        if not (self.installation / 'sporecoop-test-copy.json').is_file():
            raise FixtureError('The installation lacks its private-copy marker')
        exe = self.installation / 'SporebinEP1' / 'SporeApp.exe'
        dll = self.installation / 'SporebinEP1' / 'mods' / 'coop.dll'
        if hashlib.sha256(exe.read_bytes()).hexdigest() != EXE_SHA256:
            raise FixtureError('The private executable is not the supported GOG image')
        if hashlib.sha256(dll.read_bytes()).hexdigest() != expected_dll:
            raise FixtureError('The private DLL does not match the requested candidate')
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.OpenProcess.argtypes = (W.DWORD, W.BOOL, W.DWORD)
        self.kernel.OpenProcess.restype = W.HANDLE
        self.kernel.QueryFullProcessImageNameW.argtypes = (W.HANDLE, W.DWORD, W.LPWSTR, ctypes.POINTER(W.DWORD))
        self.kernel.WaitForSingleObject.argtypes = (W.HANDLE, W.DWORD)
        self.kernel.CloseHandle.argtypes = (W.HANDLE,)
        self.handles = {}
        try:
            for instance, record in self.records.items():
                if not re.fullmatch(r'winlab_[a-z0-9_]+', record['profile']):
                    raise FixtureError('Invalid isolated profile')
                handle = self.kernel.OpenProcess(0x00101000, False, record['pid'])
                if not handle:
                    raise ctypes.WinError(ctypes.get_last_error())
                self.handles[instance] = handle
                path = ctypes.create_unicode_buffer(32768); length = W.DWORD(len(path))
                if not self.kernel.QueryFullProcessImageNameW(handle, 0, path, ctypes.byref(length)):
                    raise ctypes.WinError(ctypes.get_last_error())
                if Path(path.value).resolve() != exe.resolve():
                    raise FixtureError('A recorded PID belongs to another executable path')
            pids = ','.join(str(record['pid']) for record in self.records.values())
            command = ('Get-CimInstance Win32_Process | Where-Object {$_.ProcessId -in @(' + pids +
                       ')} | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress')
            completed = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command', command],
                capture_output=True, text=True, check=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
            rows = json.loads(completed.stdout)
            rows = rows if isinstance(rows, list) else [rows]
            by_pid = {row['ProcessId']: row['CommandLine'] or '' for row in rows}
            for instance, record in self.records.items():
                line = by_pid.get(record['pid'], '')
                for token in (f'-sporekit-instance:{instance}', '-sporekit-profile:' + record['profile'], '-sporekit-invisible'):
                    if not re.search(r'(?:^|\s|")' + re.escape(token) + r'(?=\s|"|$)', line):
                        raise FixtureError('The recorded process lacks its exact isolated/invisible arguments')
            self.verify_windows()
        except Exception:
            self.close()
            raise

    def verify_windows(self):
        wanted = {record['pid'] for record in self.records.values()}
        for handle in self.handles.values():
            if self.kernel.WaitForSingleObject(handle, 0) != 258:
                raise FixtureError('An owned lab process exited')
        user = ctypes.WinDLL('user32', use_last_error=True)
        user.GetWindowThreadProcessId.argtypes = (W.HWND, ctypes.POINTER(W.DWORD))
        user.IsWindowVisible.argtypes = (W.HWND,)
        callback_type = ctypes.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
        windows = []
        @callback_type
        def inspect(hwnd, unused):
            pid = W.DWORD(); user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            if pid.value in wanted:
                windows.append({'pid': pid.value, 'hwnd': int(hwnd), 'visible': bool(user.IsWindowVisible(hwnd))})
            return True
        user.EnumWindows.argtypes = (callback_type, W.LPARAM)
        if not user.EnumWindows(inspect, 0):
            raise ctypes.WinError(ctypes.get_last_error())
        if {row['pid'] for row in windows} != wanted or any(row['visible'] for row in windows):
            raise FixtureError('All owned game HWNDs must exist and remain hidden: ' + repr(windows))
        # Audio changes are confined to the verified child PIDs, as in the runner.
        from windows_audio import mute_processes
        audio = mute_processes(wanted)
        return {'windows': windows, 'audio': audio, 'audio_scope': 'verified owned child PIDs only'}

    def close(self):
        for handle in getattr(self, 'handles', {}).values():
            self.kernel.CloseHandle(handle)
        self.handles = {}


class EditorParty(EditorPair):
    def __init__(self, args):
        self.args = args; self.output = args.output.resolve(); self.output.mkdir(parents=True, exist_ok=True)
        self.ownership = Ownership(args.metadata, args.instances, args.expected_dll_sha256)
        self.records = self.ownership.records
        self.deadline = time.monotonic() + args.seconds
        self.all_games = [PublicGame(i, self.output, args.timeout, self.deadline) for i in args.instances]
        self.games = self.all_games; self.active_games = list(self.games)
        self.host, self.guest = self.games[:2]
        self.created = {}; self.completed_cycles = 0
        self.result = {'pass': False, 'four_player_workflow_pass': False, 'complete_acceptance': False,
            'stage': args.stage, 'candidate_sha256': args.expected_dll_sha256, 'instances': args.instances,
            'history_input': getattr(args, 'history_input', 'actions'),
            'lab_processes': {str(i): self.records[i]['pid'] for i in args.instances},
            'checks': [], 'gates': {}, 'unvalidated': ['real packet loss/reordering between game processes',
                'large-editor FPS and retained-memory budgets', 'cold-process checkpoint recovery',
                'unsupported authored-feature roundtrip breadth']}
        try:
            for game in self.games:
                self.verify(game)
                if game.shot('initial-layout') != (1024,768):
                    raise FixtureError('The native UI fixture requires a1024x768 viewport')
            if args.resume_result:
                previous = json.loads(args.resume_result.read_text(encoding='utf-8-sig'))
                if previous.get('candidate_sha256') != args.expected_dll_sha256 or previous.get('lab_processes') != self.result['lab_processes']:
                    raise FixtureError('Resume evidence belongs to another candidate or process lease')
                self.created = {int(i): value for i,value in previous.get('authored_placements',{}).items()}
                self.result['resume_evidence'] = str(args.resume_result.resolve())
        except Exception:
            self.close()
            raise

    def verify(self, game):
        _verify_game(game, self.records[game.instance])
        if 'frames from: Present' not in game.events[-1].get('out',''):
            raise FixtureError('The native fixture must actually render through Present')

    def record(self, label):
        safety = self.ownership.verify_windows()
        row = {'label': label, 'time': time.time(), 'states': [], 'safety': safety}
        for game in self.all_games:
            self.verify(game); game.record(label)
            faults = game.call('exc')
            if 'total faults: 0' not in faults:
                raise FixtureError('Native faults on instance ' + str(game.instance) + ': ' + faults)
            state = status(game); state['instance'] = game.instance
            if state.get('frozen'):
                raise FixtureError('Editor froze: ' + state['text'])
            row['states'].append(state)
            if state.get('ready'):
                state.update(check=game.call('coop.editor check'), objects=game.call('coop.editor objects'),
                             history=game.call('coop.editor history'), inspect=game.call('coop.editor inspect'))
        self.result['checks'].append(row); self.write()

    def converge(self, revision=None, games=None):
        games = self.active_games if games is None else games
        def ready():
            states = [status(game) for game in games]
            if any(state.get('frozen') for state in states):
                raise FixtureError('Shared editor froze: ' + repr(states))
            if any(state.get('phase') != 4 or not state.get('ready') or state.get('gesture') for state in states):
                return False
            revisions = {state.get('revision') for state in states}
            if len(revisions) != 1 or revision is not None and revisions != {revision}:
                return False
            if any(state.get('durable') != state.get('revision') for state in states):
                return False
            projects = [parse_identity(state['text'])[0] for state in states]
            checks = [game.call('coop.editor check').strip() for game in games]
            return checks[0] if len(set(projects)) == len(set(checks)) == 1 and checks[0].startswith('CONVERGED ') else False
        return self.host.wait('four-player native/durable canonical convergence', ready)

    def bootstrap(self):
        for game in self.games:
            game.dismiss_registration()
        if self.args.onboard_host and not status(self.host).get('editing'):
            self.host.onboard('EditorParty' + str(self.host.instance))
            self.host.exit_adventure()
        if not status(self.host).get('editing'):
            next_close = 0
            def main_menu():
                nonlocal next_close
                if self.host.caption('Create'):
                    return True
                # Galaxy mode arrives before the radial finishes its native
                # opening transition. Retry the normal close after it settles.
                if self.host.caption('Welcome to Spore Galactic Adventures') and time.monotonic() >= next_close:
                    self.host.click(539, 501)
                    next_close = time.monotonic() + 1
                return False
            self.host.wait('native main-menu Create after Welcome radial closes', main_menu)
        # Reuse the native-owned pair bootstrap without modifying its source.
        self.games = self.all_games[:2]
        try:
            super().bootstrap()
        finally:
            self.games = self.all_games
        for game in self.all_games[2:]:
            game.call(f'coop.join 127.0.0.1:{self.args.port} EditorGuest{game.instance}')
            game.wait('party connection', lambda g=game: 'connected' in g.call('coop.status'))
        self.host.wait('four party members', lambda: self.host.adventure()['players'] == 4)
        self.result['gates']['normal_native_save_as'] = True
        self.record('four-player-offers')

    def join(self):
        for game in self.all_games[1:]:
            game.wait('explicit shared-editor offer', lambda g=game: status(g).get('phase') == 1)
            game.call('coop.editor join')
        self.result['initial_hash'] = self.converge()
        self.result['gates']['four_native_baselines'] = True
        self.record('four-player-baseline')

    def move_object(self, game, object_id, horizontal=45):
        source = self.body(game, object_id)
        target = (max(180, min(880, source[0] + horizontal)), max(250, min(620, source[1] - 8)))
        if target == source:
            raise FixtureError('No safe screen-space movement remains for ' + object_id)
        self.slow_drag(game, source, target)

    def hotkey(self, game, key):
        if getattr(self.args, 'history_input', 'actions') == 'actions':
            game.call('coop.editor ' + {'z': 'undo', 'y': 'redo'}[key])
        else:
            super().hotkey(game, key)

    def edit(self):
        for index, game in enumerate(self.all_games):
            game.call('coop.uistate 0')
            before = self.objects(self.host)
            self.gesture(game, f'author-{index}-create', lambda g=game, n=index: self.slow_drag(g, (88,170), (430+n*100,450+(n%2)*55)))
            added = set(self.objects(self.host)) - set(before)
            if len(added) != 1:
                raise FixtureError('Create must add exactly one stable placement')
            object_id = added.pop(); self.created[game.instance] = object_id
            self.result['authored_placements'] = self.created.copy()
            before_move = self.objects(self.host)[object_id]['position']
            self.gesture(game, f'author-{index}-move', lambda g=game, obj=object_id: self.move_object(g,obj,35))
            after_move = self.objects(self.host)[object_id]['position']
            if after_move == before_move:
                raise FixtureError('New native model was not interactable after Create')
            self.gesture(game, f'author-{index}-undo', lambda g=game: self.hotkey(g,'z'))
            if self.objects(self.host)[object_id]['position'] != before_move:
                raise FixtureError('Own Undo did not restore the exact prior transform')
            self.gesture(game, f'author-{index}-redo', lambda g=game: self.hotkey(g,'y'))
            if self.objects(self.host)[object_id]['position'] != after_move:
                raise FixtureError('Own Redo did not restore the moved transform')
        self.result['gates']['four_authors_create_move_undo_redo'] = True

    def concurrent(self):
        if len(self.created) != 4:
            raise FixtureError('Concurrent UI checks require the per-author edit stage')
        for conflict in (False, True):
            games = self.all_games[1:3]
            objects = [self.created[game.instance] for game in games]
            if conflict: objects[1] = objects[0]
            start_revision = status(self.host)['revision']
            before = {obj: self.objects(self.host)[obj]['position'] for obj in objects}
            for attempt in range(3):
                pending = list(zip(games,objects)) if conflict else [(g,obj) for g,obj in zip(games,objects) if self.objects(self.host)[obj]['position'] == before[obj]]
                barrier = threading.Barrier(len(pending))
                def move(pair):
                    game, obj = pair
                    source = self.body(game,obj); barrier.wait(timeout=10)
                    self.slow_drag(game,source,(source[0]+30,source[1]-8))
                with ThreadPoolExecutor(max_workers=2) as pool:
                    list(pool.map(move,pending))
                time.sleep(2); self.converge()
                delta = status(self.host)['revision'] - start_revision
                if delta >= (1 if conflict else 2):
                    break
            expected = (1,2) if conflict else (2,)
            if delta not in expected:
                raise FixtureError('Concurrent gestures did not serialize within their expected revision count: ' + str(delta))
            now = self.objects(self.host)
            if any(now[obj]['position'] == previous for obj,previous in before.items()):
                raise FixtureError('Concurrent gestures left an intended placement unchanged')
            self.result['gates']['concurrent_same_placement' if conflict else 'concurrent_disjoint'] = {
                'accepted_gestures': delta, 'meaning': 'serialized or one noncommitted; UI timing does not prove simultaneous lease ownership'}
            self.record('concurrent-conflict' if conflict else 'concurrent-disjoint')

    def confirmation(self, count, expected_caption):
        for game in self.active_games:
            state = status(game)
            if state.get('phase') != 5 or state.get('playtesting'):
                raise FixtureError('Preview launched before every editor participant confirmed')
        self.result['checks'].append({'label': expected_caption, 'confirmed_by_fixture': count, 'total': len(self.active_games)})
        self.write()

    def preview(self, cycle):
        before = self.converge()
        self.host.call('coop.editor playtest')
        self.host.wait('all four editor confirmation states', lambda: all(status(g).get('phase') == 5 for g in self.active_games))
        self.confirmation(0,'no-implicit-ready-' + str(cycle))
        # Explicitly leave a different final player unready each round, including
        # the host. Recheck the native submode before supplying that last action.
        order = self.active_games[cycle % 4:] + self.active_games[:cycle % 4]
        for count, game in enumerate(order[:-1],1):
            game.call('coop.editor ready 1'); time.sleep(.4)
            self.confirmation(count,'editor-ready-hold-' + str(cycle) + '-' + str(count))
        time.sleep(1); self.confirmation(3,'all-but-final-ready-' + str(cycle))
        order[-1].call('coop.editor ready 1')
        self.host.wait('four native preview submodes', lambda: all(status(g).get('playtesting') for g in self.active_games))
        self.host.wait('committed shared preview run', lambda: all('Adventure in progress' in g.adventure()['text'] for g in self.active_games))
        runs = {g.adventure()['run'] for g in self.active_games}
        if len(runs) != 1 or not next(iter(runs)):
            raise FixtureError('Native preview participants entered different run identities')
        if self.args.beam_check:
            # Native editor Test has no beam cinematic/Continue. The bridge
            # acknowledges that absent intro only after this explicit editor
            # round is ready and each native scene is actually playing. Observe
            # its normal completion; never synthesize another confirmation.
            def naturally_playing():
                rows = [native_play(game) for game in self.active_games]
                if any(row is None or row['state'] != 3 for row in rows):
                    return False
                return rows if all('Beam-down confirmations:' not in game.adventure()['text'] for game in self.active_games) else False
            initial = self.host.wait('four natural native playing states with no pending beam hold', naturally_playing)
            time.sleep(1.5)
            later = [native_play(game) for game in self.active_games]
            if any(row is None or row['state'] != 3 or row['time_ms'] <= initial[i]['time_ms'] for i,row in enumerate(later)):
                raise FixtureError('Native editor playtest clocks did not advance on every participant')
            def reciprocal_positions():
                rows = [parse_party_positions(g.call('coop.avatar'), g.call('coop.status')) for g in self.active_games]
                return rows if all(rows) else False
            positions = self.host.wait('four reciprocal live captain positions within1m of their received poses', reciprocal_positions)
            self.result['checks'].append({'label': 'native-simulation-' + str(cycle), 'before': initial,
                                         'after': later, 'positions': positions,
                                         'continue_controls': [g.control(0x0445C99D) for g in self.active_games],
                                         'confirmation_source': 'explicit editor Ready only; no fixture beam acknowledgement'})
        self.record(f'preview-{cycle}')
        self.host.call('coop.editor return')
        self.host.wait('four native editor returns', lambda: all(status(g).get('ready') and status(g).get('phase') == 4 for g in self.active_games))
        if self.converge() != before:
            raise FixtureError('Preview simulation changed the authored canonical draft')
        self.record(f'returned-{cycle}')

    def cycles(self):
        if len(self.created) != 4:
            raise FixtureError('Changed edit/preview cycles require the per-author placements')
        for cycle in range(1,self.args.cycles+1):
            game = self.all_games[(cycle-1)%4]; obj = self.created[game.instance]
            direction = 35 if (cycle-1)//4 % 2 == 0 else -35
            self.gesture(game,f'cycle-{cycle}-edit',lambda g=game,o=obj,d=direction:self.move_object(g,o,d))
            self.preview(cycle); self.completed_cycles += 1
            self.result['completed_changed_preview_cycles'] = self.completed_cycles; self.write()
        self.result['gates']['twenty_changed_preview_returns'] = self.completed_cycles >= 20

    def exit_editor(self, game):
        game.call('coop.uistate 0')
        game.wait('shared editor detached',lambda:status(game).get('phase') == 0)
        game.click_control(0x90439D7C)  # Verified normal native Cancel/Exit.
        game.wait('native discard prompt or Galaxy',lambda:game.menu() or game.caption('Quit'))
        if not game.menu(): game.press('Quit')
        game.wait('Galaxy after normal native editor exit',game.menu)

    def reconnect(self):
        game = self.all_games[-1]; before = self.converge(); old_cap = parse_identity(status(game)['text'])[1]
        game.call('coop.leave'); self.active_games = self.all_games[:-1]
        self.host.wait('three connected party members',lambda:self.host.adventure()['players'] == 3)
        if self.converge() != before: raise FixtureError('Disconnect changed the surviving draft')
        self.exit_editor(game)
        game.call(f'coop.join 127.0.0.1:{self.args.port} EditorGuest{game.instance}Rejoined')
        game.wait('new editing offer',lambda:status(game).get('phase') == 1)
        game.call('coop.editor join'); self.active_games = list(self.all_games)
        if self.converge() != before: raise FixtureError('Rejoin did not reconstruct the same committed draft')
        if parse_identity(status(game)['text'])[1] == old_cap:
            raise FixtureError('Rejoin reused an old author capability')
        revision = status(self.host)['revision']; self.hotkey(game,'z'); time.sleep(1)
        if status(self.host)['revision'] != revision or self.converge() != before:
            raise FixtureError('A new connection inherited another incarnation\'s Undo authority')
        self.result['gates']['disconnect_rejoin_new_capability'] = True; self.record('rejoined-fourth-player')

    def recovery(self):
        self.converge()
        authored = lambda: {key:{k:v for k,v in row.items() if k != 'screen'} for key,row in self.objects(self.host).items()}
        before = authored()
        history = parse_history(self.host.call('coop.editor history'))
        owner = parse_identity(status(self.host)['text'])[1]
        if not any(row[:2] == owner and row[5] for row in history):
            raise FixtureError('Recovery Undo validation requires an applied own gesture from the edit stages')
        project = parse_identity(status(self.host)['text'])[0]; revision = status(self.host)['revision']
        self.host.call('coop.editor end')
        self.host.wait('all editors gracefully detached',lambda:all(status(g).get('phase') == 0 for g in self.all_games))
        for game in self.all_games[1:]: self.exit_editor(game)
        self.host.call('coop.editor recover')
        self.host.wait('host recovered durable draft',lambda:status(self.host).get('phase') == 4 and status(self.host).get('ready'))
        for game in self.all_games[1:]:
            game.wait('recovered project offer',lambda g=game:status(g).get('phase') == 1); game.call('coop.editor join')
        self.converge(revision)
        if parse_identity(status(self.host)['text'])[0] != project or authored() != before:
            raise FixtureError('Durable recovery changed stable placement identity or authored transform')
        if parse_history(self.host.call('coop.editor history')) != history:
            raise FixtureError('Durable recovery discarded or rewrote semantic history')
        self.gesture(self.host,'recovered-own-undo',lambda:self.hotkey(self.host,'z'))
        self.gesture(self.host,'recovered-own-redo',lambda:self.hotkey(self.host,'y'))
        if authored() != before: raise FixtureError('Recovered Undo/Redo failed to restore the exact authored placements')
        self.result['gates']['ordinary_graceful_end_durable_recovery'] = True; self.record('recovered-four-player-draft')

    def close(self):
        self.write()
        for game in self.all_games:
            if game.console.sock: game.console.sock.close()
        self.ownership.close()


RUNNER = r'''# Generated template only. It starts processes ONLY when explicitly executed.
import argparse, hashlib, json, os, re, socket, subprocess, sys, time
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--expected-dll-sha256',required=True)
p.add_argument('--seconds',type=int,default=3600)
p.add_argument('--reuse-private-profiles',action='store_true')
a=p.parse_args()
if not re.fullmatch('[0-9a-fA-F]{64}',a.expected_dll_sha256) or not 1<=a.seconds<=3600:raise SystemExit('exact candidate hash and bounded seconds required')
OLD=Path(r'D:\Code\SporeCoop-wt\ga-run-outcome')
OUT=Path(__file__).resolve().parent
installation=OLD/'out/windows-lab/installation'
sys.path.insert(0,str(OLD/'tools'))
import windows_lab
from windows_lab import WindowsLab
from sporectl import Console
if hashlib.sha256((installation/'SporebinEP1/mods/coop.dll').read_bytes()).hexdigest()!=a.expected_dll_sha256.lower():raise SystemExit('private DLL candidate mismatch; no deployment performed')
initial=OLD/'out/windows-lab/ga_editor_party_c7'
if (OUT/'stop-request').exists():raise SystemExit('old stop-request exists; review the closed runner before reuse')
live=subprocess.run(['powershell','-NoProfile','-NonInteractive','-Command',
    "Get-CimInstance Win32_Process | Where-Object {$_.CommandLine -match '-sporekit-profile:winlab_ga_editor_party_c7_(89|90|91|92)( |$|\")'} | Select-Object ProcessId | ConvertTo-Json -Compress"],
    capture_output=True,text=True,check=True,timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
if live.stdout.strip():raise SystemExit('a target private profile is still loaded')
for instance in range(89,93):
    with socket.socket() as probe:
        if probe.connect_ex(('127.0.0.1',47800+instance-1))==0:raise SystemExit('private console already in use')
    profile=Path(os.environ['APPDATA'])/('SporeKit-winlab_ga_editor_party_c7_'+str(instance))
    if profile.exists() and not a.reuse_private_profiles:raise SystemExit('profile already exists; review closed ownership before explicit reuse')
original=windows_lab.DesktopProcess
previous=None
def spawn(command,desktop,cwd=None):
    global previous
    if Path(str(command[0])).name.lower()=='sporeapp.exe' and previous is not None:
        pid,instance=previous;c=Console(instance,timeout=10)
        try:
            deadline=time.monotonic()+180
            while time.monotonic()<deadline:
                info=c.call('info')
                if not info.get('ok') or f'pid={pid}\n' not in info.get('out',''):raise RuntimeError('prior private PID changed')
                if 'GalaxyGameEntry' in c.call('game.mode').get('out',''):break
                time.sleep(.5)
            else:raise RuntimeError('prior private game did not reach Galaxy')
        finally:
            if c.sock:c.sock.close()
    process=original(command,desktop,cwd)
    if Path(str(command[0])).name.lower()=='sporeapp.exe':
        instance=int(next(str(arg).split(':',1)[1] for arg in command if str(arg).startswith('-sporekit-instance:')))
        previous=(process.pid,instance)
    return process
windows_lab.DesktopProcess=spawn
OUT.mkdir(parents=True,exist_ok=True)
try:
    with WindowsLab(installation,initial,4,89,True) as lab:
        lab.output=OUT;lab.record();lab.enforce_audio()
        (OUT/'startup.json').write_text(json.dumps([c.call('info') for c in lab.consoles],indent=2)+'\n',encoding='utf-8')
        print(json.dumps(dict(ready=True,expected_dll_sha256=a.expected_dll_sha256,pids=[p.pid for p,_,_ in lab.processes])),flush=True)
        deadline=time.monotonic()+a.seconds
        while time.monotonic()<deadline and not (OUT/'stop-request').exists():
            if any(p.poll() is not None for p,_,_ in lab.processes):raise RuntimeError('owned four-player child exited')
            lab.assert_invisible();lab.enforce_audio();time.sleep(1)
        if not (OUT/'stop-request').exists():raise RuntimeError('bounded four-player lease expired')
except Exception as error:
    (OUT/'runner-error.json').write_text(json.dumps(dict(error=str(error)),indent=2)+'\n',encoding='utf-8');raise
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action',required=True)
    template = sub.add_parser('runner-template',help='write a bounded private runner; do not launch')
    template.add_argument('--output',type=Path,required=True)
    run = sub.add_parser('run',help='drive an already-owned four-player lab')
    run.add_argument('--execute',action='store_true')
    run.add_argument('--metadata',type=Path,required=True); run.add_argument('--output',type=Path,required=True)
    run.add_argument('--expected-dll-sha256',required=True)
    run.add_argument('--instances',nargs=4,type=int,default=[89,90,91,92])
    run.add_argument('--port',type=int,default=45189); run.add_argument('--timeout',type=float,default=150)
    run.add_argument('--seconds',type=int,default=3300); run.add_argument('--cycles',type=int,default=20)
    run.add_argument('--stage',choices=('all','bootstrap','join','edit','concurrent','cycles','reconnect','recovery','snapshot'),default='all')
    run.add_argument('--no-onboard-host',dest='onboard_host',action='store_false')
    run.add_argument('--no-beam-check','--no-simulation-check',dest='beam_check',action='store_false',
                     help='diagnostic only: skip native simulation clocks and reciprocal captain proof')
    run.add_argument('--history-input',choices=('actions','hotkeys'),default='actions',
                     help='explicit public co-op history actions, or diagnostic native shortcuts')
    run.add_argument('--title',default='Four Player Native Editor Acceptance')
    run.add_argument('--resume-result',type=Path,help='reuse placement identities only from this exact candidate/PID lease')
    sub.add_parser('self-test',help='check pure parsers/command boundaries without connecting to games')
    args = parser.parse_args()
    if args.action == 'self-test':
        assert public_command('coop.editor ready 1') and public_command('coop.adventure.world status')
        assert public_command('drag 88 170 600 470')
        assert not public_command('mem.write 0x1234 FF') and not public_command('call Anything cdecl')
        assert not public_command('coop.adventure.world finish 1') and not public_command('game.mode set Scenario')
        assert parse_identity('project=0123 capability=2:71') == ('0123',(2,71))
        assert parse_history('author=2:71 sequence=5 epoch=88 revision=9 applied=1 redo=0 Move') == [(2,71,5,88,9,1,0,'Move')]
        session = '\n'.join(f"remote {i} 'Peer{i}' puppet=0x1234 state=yes age=0.10s pos=(1 2 3)\n   puppet pos=(1 2 3) err=0.00 rot_err=0.0\ntogether {i} 1" for i in (1,2,3))
        assert parse_party_positions('pos=(1 2 3) |r|=3.7', session)['local'] == (1.,2.,3.)
        assert parse_party_positions('pos=(1 2 3)', session.replace('err=0.00','err=2.00',1)) is None
        print('PASS: pure harness parser/command boundary checks; no game connections'); return 0
    output = args.output.resolve()
    if (ROOT/'out').resolve() not in output.parents:
        parser.error('output must be a child of this worktree out directory')
    if args.action == 'runner-template':
        output.parent.mkdir(parents=True,exist_ok=True); output.write_text(RUNNER,encoding='utf-8')
        print('Prepared only; no process launched: ' + str(output)); return 0
    if not args.execute: parser.error('run requires --execute after the candidate runner is approved and started')
    if len(set(args.instances)) != 4 or min(args.instances) < 2 or max(args.instances) > 100:
        parser.error('four distinct private instances2..100 required')
    if not re.fullmatch('[0-9a-fA-F]{64}',args.expected_dll_sha256) or not 1 <= args.seconds <= 3500 or not 1 <= args.cycles <= 40:
        parser.error('exact candidate hash, bounded seconds and1..40 cycles required')
    if not 1 <= args.port <= 65535 or not 1 <= args.timeout <= 300:
        parser.error('valid UDP port and bounded timeout required')
    args.expected_dll_sha256 = args.expected_dll_sha256.lower()
    args.fixture='blank'; args.adventure='Alpine Valley'; args.roundtrip_cycles=0; args.host=args.instances[0]; args.guest=args.instances[1]
    party = None
    try:
        party = EditorParty(args)
        stages = ('bootstrap','join','edit','concurrent','cycles','reconnect','recovery') if args.stage == 'all' else (args.stage,)
        for stage in stages:
            if stage == 'snapshot': party.record('snapshot')
            else: getattr(party,stage)()
        party.result['pass'] = True
        party.result['four_player_workflow_pass'] = args.stage == 'all' and args.cycles >= 20 and args.beam_check and all(party.result['gates'].values())
        party.result['complete_acceptance'] = False
        party.write(); print(json.dumps(party.result,indent=2)); return 0
    except Exception as error:
        if party:
            party.result.update(error=str(error),four_player_workflow_pass=False)
            party.result['pass'] = False
            try: party.record('failure')
            except Exception: party.write()
        else:
            output.mkdir(parents=True,exist_ok=True)
            (output/'result.json').write_text(json.dumps({'pass':False,'complete_acceptance':False,'error':str(error)},indent=2)+'\n',encoding='utf-8')
        print(str(error),file=sys.stderr); return 1
    finally:
        if party: party.close()


if __name__ == '__main__':
    raise SystemExit(main())
