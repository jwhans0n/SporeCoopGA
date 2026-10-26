#!/usr/bin/env python3
"""Real native Adventure Editor acceptance against an already owned Windows lab.

The bounded WindowsLab runner owns hidden/muted processes. This fixture uses
ordinary UI input and public co-op actions only. It never writes game memory,
changes personal profiles, invents native outcomes, or edits database files.
Use --stage for diagnostic partial runs; --stage all reports the two-peer workflow, not full project acceptance.
"""
import argparse
import ctypes
import json
import math
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
        r'\b(revision|durable|frozen|phase|available|editing|ready|playtesting|gesture|captures|restores|rejected|previews)=(\d+)', text))
    values['text'] = text
    selection = re.search(r'\bselection=(?:Placement )?(\d+:\d+)', text)
    values['selection'] = selection[1] if selection else None
    return values


def retry_grant(text, selected):
    """Read a local visible grant without assuming a pending request succeeded."""
    local=re.search(r'^local=(\d+:\d+) phase=4 revision=\d+$',text,re.M)
    members=re.findall(r'^member peer=(\d+:\d+) .*\bpending=(\d+)\b',text,re.M)
    if (not local or not members or 'truncated=1' in text or
            not any(peer==local[1] for peer,_ in members) or
            any(int(pending)!=0 for _,pending in members)):
        return False
    for kind,object_id,token,remaining in re.findall(
            r'^lease scope=(\w+) object=(\d+:\d+) token=(\d+) remaining_ms=(\d+)$',text,re.M):
        if int(token)>0 and int(remaining)>0 and (kind=='Document' or
                (kind=='Placement' and object_id==selected)):
            return True
    return False


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
            initial = status(game)
            if not initial.get('available'):
                game.save_log()
                raise FixtureError('Native editor adapter unavailable before input; verify the exact DLL and all matching symbol maps: '+initial['text'])

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
                if (game.caption('Login') or game.caption("If you don't register") or
                    game.caption('letterbox_top')):
                    game.dismiss_registration()
                else:
                    # Reused offline authoring profiles may expose Register
                    # without the Welcome/GA navigation control required by the
                    # captain-onboarding helper. Create remains fully usable.
                    game.wait('native Galaxy menu', game.menu)
        if not status(host).get('editing'):
            if self.args.fixture == 'blank':
                if not host.caption('Choose a Planet'):
                    if not host.control(0x07A7F798):
                        if not host.control(0x03685038):
                            host.press('Create')
                        host.wait('ordinary Create radial', lambda: host.control(0x07A7B140))
                        host.click(553,522)
                        host.wait('native adventure creator option', lambda: host.control(0x07A7F798))
                    next_click = 0.
                    def planet_browser():
                        nonlocal next_click
                        if host.caption('Choose a Planet'):
                            return True
                        if time.monotonic() >= next_click:
                            host.click(820,350)
                            next_click = time.monotonic()+1
                        return False
                    host.wait('native planet browser', planet_browser)
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
            # Uncapped native editors can render these frame fences in a few
            # milliseconds. Keep ordinary drag timing above the native click
            # thresholds as well as waiting for queued input delivery.
            time.sleep(.12)
            for fraction in (.35, .7, 1.):
                game.call(f'move {round(sx + (tx-sx)*fraction)} {round(sy + (ty-sy)*fraction)}')
                game.call('wait.frames 6 15000')
                time.sleep(.06)
        finally:
            game.call(f'button {tx} {ty} up')
        game.call('wait.frames 20 15000')

    def gesture(self, game, label, action):
        revision = status(self.host)['revision']
        def committed():
            state=status(self.host)
            if state.get('frozen'):
                raise FixtureError('Editor froze after '+label+': '+state['text'])
            if state['revision']>revision+1:
                raise FixtureError(label+' produced duplicate canonical gestures')
            if state['revision']==revision+1:
                self.converge(revision+1);self.record(label)
                return True
            return False
        for attempt in range(3):
            # A delayed prior action may have committed after its poll window.
            # Recheck immediately before every real repeat, including retries.
            quiet_deadline=time.monotonic()+3
            while True:
                if committed():return
                current=status(game)
                leases=game.call('coop.editor leases')
                pending=any(int(value)!=0 for value in re.findall(r'\bpending=(\d+)',leases))
                if not current.get('gesture') and not pending:break
                if time.monotonic()>=quiet_deadline:
                    self.record(label+'-pending')
                    raise FixtureError('A native or canonical operation is still pending before '+label)
                time.sleep(.1)
            self.verify(game)
            if committed():return
            before=status(game)
            action()
            # First input may request a lease and be held before native mutation.
            # A second genuine gesture after lease delivery is allowed; no input
            # is replayed by this fixture behind a player's back.
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if committed():return
                current=status(game)
                if (not current.get('gesture') and current.get('captures')==before.get('captures') and
                        current.get('rejected',0)>before.get('rejected',0)):
                    leases=game.call('coop.editor leases')
                    if retry_grant(leases,current.get('selection')):
                        self.result.setdefault('prompt_lease_retries',[]).append(dict(
                            label=label,attempt=attempt+1,time=time.time(),leases=leases,
                            native_before=before['text'],native_after=current['text']))
                        self.write()
                        # A rejected input started no native capture. Its grant
                        # arrived, so the next real gesture can use it promptly.
                        break
                time.sleep(.05)
        if committed():return
        self.record(label + '-failed')
        raise FixtureError('No canonical gesture accepted for ' + label)

    def objects(self, game):
        result = {}
        for line in game.call('coop.editor objects').splitlines():
            m = re.match(r'(\d+:\d+) class=(\d+) pos=([^ ]+) scale=([^ ]+) screen=([^ ]+)', line)
            if m:
                result[m[1]] = {'class': int(m[2]), 'position': tuple(map(float,m[3].split(','))),
                                'scale': float(m[4]), 'screen': None if m[5] == 'hidden' else tuple(map(float,m[5].split(',')))}
                rotation = re.search(r'\brotation=([^ ]+)',line)
                if rotation:
                    try:
                        components=tuple(map(float,rotation[1].split(',')))
                        if len(components)==4:result[m[1]]['rotation']=components
                    except ValueError:
                        # The public console bounds large-object diagnostics;
                        # a partial last quaternion is not a native field value.
                        pass
        return result

    def body(self, game, object_id):
        point = self.objects(game).get(object_id, {}).get('screen')
        if point is None:
            raise FixtureError('Native placement is not visible: ' + object_id)
        # Native projection is the feet, while the selectable creature body is
        # above it. This fixture's ordinary Ace captain is scale2.
        return round(point[0]), round(point[1] - 28)

    def selected(self, game, object_id):
        game.wait('local native selection ' + object_id,
                  lambda: status(game).get('selection') == object_id)

    @staticmethod
    def hotkey(game, key):
        try:
            game.call('key ctrl down'); game.call('key ' + key)
        finally:
            game.call('key ctrl up')

    def modified_wheel(self, game, point, notches, shift=False, ctrl=False, left_held=False):
        """Deliver ordinary Windows wheel modifier bits missing from SporeKit0.3.4."""
        if not isinstance(notches,int) or not 1 <= abs(notches) <= 4:
            raise FixtureError('Modified wheel input is limited to one through four notches')
        self.verify(game)
        info=game.call('info')
        match=re.search(r'\bhwnd=([0-9A-Fa-f]+)',info)
        if not match:
            raise FixtureError('The owned native input window is unavailable')
        hwnd=int(match[1],16)
        user=ctypes.WinDLL('user32',use_last_error=True)
        user.GetWindowThreadProcessId.argtypes=(ctypes.c_void_p,ctypes.POINTER(ctypes.c_ulong))
        owner=ctypes.c_ulong()
        if not user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner)) or owner.value!=self.records[game.instance]['pid']:
            raise FixtureError('The native wheel input HWND changed ownership')
        class Point(ctypes.Structure):
            _fields_=[('x',ctypes.c_long),('y',ctypes.c_long)]
        screen=Point(*point)
        user.ClientToScreen.argtypes=(ctypes.c_void_p,ctypes.POINTER(Point))
        user.PostMessageW.argtypes=(ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t)
        if not user.ClientToScreen(hwnd,ctypes.byref(screen)):
            raise FixtureError('The owned wheel input point could not be projected')
        def mouse_message(message,modifiers,coordinates):
            if not user.GetWindowThreadProcessId(hwnd,ctypes.byref(owner)) or owner.value!=self.records[game.instance]['pid']:
                raise FixtureError('The native wheel input HWND changed ownership before sending')
            if not user.PostMessageW(hwnd,message,modifiers,coordinates):
                raise FixtureError('The ordinary modified mouse input was rejected')
        client_point=(point[0]&0xFFFF)|((point[1]&0xFFFF)<<16)
        game.call(f'move {point[0]} {point[1]}')
        try:
            if shift:game.call('key shift down')
            if ctrl:game.call('key ctrl down')
            game.call('wait.frames 3 15000')
            time.sleep(.12)
            # left_held is only supplied while the fixture's ordinary button
            # down is outstanding; it does not manufacture a native drag.
            modifiers=(4 if shift else 0)|(8 if ctrl else 0)|(1 if left_held else 0)
            # SporeKit's ordinary move also omits Shift/Ctrl in wParam. Deliver
            # the changed modifier state before the wheel without moving the
            # user's real mouse or invoking native tool actions directly.
            mouse_message(0x0200,modifiers,client_point)
            game.call('wait.frames 3 15000')
            delta=120 if notches>0 else -120
            for _ in range(abs(notches)):
                wparam=modifiers|((delta&0xFFFF)<<16)
                lparam=(screen.x&0xFFFF)|((screen.y&0xFFFF)<<16)
                mouse_message(0x020A,wparam,lparam)
                game.call('wait.frames 6 15000')
                time.sleep(.12)
        finally:
            if ctrl:game.call('key ctrl up')
            if shift:game.call('key shift up')
            mouse_message(0x0200,1 if left_held else 0,client_point)

    def edit(self):
        for game in self.games:
            game.call('coop.uistate 0')
        before = self.objects(self.host)
        # The ordinary Alpine editor accepts an unpickable placement at
        # (650,470), even without sharing. The bootstrap leaves the verified
        # native (601,470) land point free, rather than treating that ordinary
        # native picking limitation as a reconstruction failure. Cast icons also
        # merge overlapping rectangles, so Clone uses separate front land.
        self.gesture(self.host, 'host-create', lambda: self.slow_drag(self.host, (88,170), (601,470)))
        created = set(self.objects(self.host)) - set(before)
        if len(created) != 1:
            raise FixtureError('Native Create did not add exactly one placement')
        moved = next(iter(created))
        self.selected(self.host, moved)
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
                self.slow_drag(self.host, source, (430,source[1]))
            finally:
                self.host.call('key alt up')
        self.gesture(self.host, 'host-clone', clone)
        clones = set(self.objects(self.host)) - before_clone
        if len(clones) != 1:
            raise FixtureError('Native Clone did not add exactly one placement')
        cloned = next(iter(clones))
        self.selected(self.host, cloned)
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
        before_guest = set(self.objects(self.guest))
        self.gesture(self.guest, 'guest-create', lambda: self.slow_drag(self.guest, (88,170), (430,470)))
        guest_created = set(self.objects(self.guest)) - before_guest
        if len(guest_created) != 1:
            raise FixtureError('Guest Create did not add exactly one placement')
        guest_object = next(iter(guest_created))
        self.selected(self.guest, guest_object)
        guest_position = self.objects(self.guest)[guest_object]['position']
        def guest_move():
            source = self.body(self.guest, guest_object)
            self.slow_drag(self.guest, source, (source[0]-25,source[1]))
        self.gesture(self.guest, 'guest-move', guest_move)
        guest_moved = self.objects(self.guest)[guest_object]['position']
        if guest_moved == guest_position:
            raise FixtureError('Guest Move did not change its exact placement')
        def disjoint_host_move():
            source = self.body(self.host, moved)
            self.host.click(*source)
            self.slow_drag(self.host, source, (source[0]-15,source[1]))
        self.gesture(self.host, 'host-disjoint-move', disjoint_host_move)
        host_position = self.objects(self.host)[moved]['position']
        self.gesture(self.guest, 'guest-undo-after-remote-edit', lambda: self.hotkey(self.guest, 'z'))
        after_undo = self.objects(self.guest)
        if after_undo[guest_object]['position'] != guest_position or after_undo[moved]['position'] != host_position:
            raise FixtureError('Own chronological Undo changed a remote placement or skipped its own Move')
        self.gesture(self.guest, 'guest-redo-after-remote-edit', lambda: self.hotkey(self.guest, 'y'))
        after_redo = self.objects(self.guest)
        if after_redo[guest_object]['position'] != guest_moved or after_redo[moved]['position'] != host_position:
            raise FixtureError('Own chronological Redo changed a remote placement or restored the wrong gesture')
        self.result['edited_hash'] = self.converge()

    def yaw(self, games=None, target_ids=None):
        """Use the verified Alpine/Ace native ring; never guess a grouped target.

        A party wrapper may supply its participant list and expected instance-to-
        stable-ID mapping. Locate among multiple same-class actors is not assumed
        to preserve selection: validate its actual target and calibrated camera.
        """
        participants=list(self.games if games is None else games)
        for game in participants:
            self.verify(game)
            current=status(game)
            if current.get('phase')!=4 or not current.get('ready') or current.get('gesture'):
                raise FixtureError('Native yaw requires settled shared editing')
            game.call('coop.uistate 0')
        time.sleep(1.3)
        self.converge()
        def authored(value):
            return {key:value.get(key) for key in ('class','position','scale','rotation')}
        for game in participants:
            game.call('move 88 170');game.call('click 88 170 right')
            game.call('wait.frames 25 15000');time.sleep(1.8)
            object_id=status(game).get('selection')
            if target_ids is not None and target_ids.get(game.instance)!=object_id:
                raise FixtureError('Native Locate did not select the intended yaw placement')
            before_all=self.objects(game);before=before_all.get(object_id)
            point=before.get('screen') if before else None
            if (not point or abs(point[0]-512)>15 or abs(point[1]-394)>15 or
                    'rotation' not in before):
                raise FixtureError('Native Locate has not exposed the calibrated front rotation ring')
            revision=status(self.host)['revision']
            self.gesture(game,str(game.instance)+'-native-yaw',
                         lambda:self.slow_drag(game,(601,475),(625,435)))
            after_all=self.objects(game);after=after_all.get(object_id)
            if set(after_all)!=set(before_all):
                raise FixtureError('Native yaw changed placement identities')
            for other in before_all:
                if other!=object_id and authored(after_all[other])!=authored(before_all[other]):
                    raise FixtureError('Native yaw changed an unrelated placement: '+other)
            if (not after or after['position']!=before['position'] or after['scale']!=before['scale'] or
                    after['class']!=before['class'] or 'rotation' not in after):
                raise FixtureError('Native yaw changed fields outside the intended rotation')
            q0,q1=before['rotation'],after['rotation']
            denominator=math.sqrt(sum(v*v for v in q0)*sum(v*v for v in q1))
            if not math.isfinite(denominator) or denominator<=0:
                raise FixtureError('Native yaw returned an invalid quaternion')
            cosine=abs(sum(a*b for a,b in zip(q0,q1)))/denominator
            if cosine>.99999:
                raise FixtureError('Native yaw did not change the intended orientation')
            self.converge(revision+1)
            self.result.setdefault('native_yaw',[]).append(dict(instance=game.instance,object=object_id,
                revision_before=revision,revision_after=revision+1,before=authored(before),after=authored(after),
                absolute_quaternion_cosine=cosine,other_placements_unchanged=True))
            self.record(str(game.instance)+'-native-yaw-converged')

    def playtest(self):
        for cycle in range(self.args.playtest_cycles):
            before = self.converge()
            self.host.call('coop.editor playtest')
            self.host.wait('explicit readiness preparation', lambda: all(status(g).get('phase') == 5 for g in self.games))
            # Alternate the last confirmer, including the host. Editor Test has
            # no native beam cinematic; its absent intro may complete only after
            # these genuine explicit editor confirmations.
            order=self.games[cycle % len(self.games):] + self.games[:cycle % len(self.games)]
            for game in order[:-1]: game.call('coop.editor ready 1')
            time.sleep(.75)
            if any(status(g).get('playtesting') or status(g).get('phase') != 5 for g in self.games):
                raise FixtureError('Native preview launched before the last explicit Ready')
            order[-1].call('coop.editor ready 1')
            self.host.wait('both native preview submodes', lambda: all(status(g).get('playtesting') for g in self.games))
            def native_playing():
                result=[]
                for game in self.games:
                    match=re.search(r'^act=(-?\d+) state=(-?\d+) time_ms=(-?\d+) remaining_ms=(-?\d+)$',
                                    game.call('coop.adventure.world status'),re.M)
                    if not match or int(match[2]) != 3: return False
                    result.append(dict(zip(('act','state','time_ms','remaining_ms'),map(int,match.groups()))))
                return result
            initial=self.host.wait('natural native playing state without fixture Continue',native_playing)
            time.sleep(1.5)
            later=native_playing()
            if not later or any(row['time_ms'] <= initial[index]['time_ms'] for index,row in enumerate(later)):
                raise FixtureError('Native editor preview clocks did not advance on every participant')
            def reciprocal_puppets():
                result=[]
                for game in self.games:
                    text=game.call('coop.status')
                    errors=list(map(float,re.findall(r'\s+puppet pos=\([^)]+\) err=([0-9.]+)',text)))
                    if len(errors) != len(self.games)-1 or any(error>1 for error in errors): return False
                    result.append({'instance':game.instance,'avatar':game.call('coop.avatar'),'session':text})
                return result
            positions=self.host.wait('reciprocal native puppets within one metre of received poses',reciprocal_puppets)
            self.result.setdefault('native_simulation',[]).append({'cycle':cycle+1,'before':initial,'after':later,
                'positions':positions,'confirmation_source':'explicit editor Ready; no fixture Continue'})
            self.record(f'playtest-{cycle+1}')
            self.host.call('coop.editor return')
            self.host.wait('both native editors returned', lambda: all(status(g).get('ready') and status(g).get('phase') == 4 for g in self.games))
            after = self.converge()
            if after != before:
                raise FixtureError('Group playtest return changed the canonical authored draft')
            self.record(f'returned-{cycle+1}')

    def negative(self):
        """Exercise ordinary unsupported controls while placement sharing owns the editor."""
        for game in self.games:
            game.call('coop.uistate 0')
        time.sleep(1.3)  # The cached co-op panel closes on its ordinary UI tick.
        baseline = self.converge()
        revision = status(self.host)['revision']
        attempts = [
            ('native-play-button', lambda game: game.click(513,35)),
            ('native-terraform-button', lambda game: game.click(571,35)),
            ('native-title-control', lambda game: game.click(515,751)),
            ('native-act-add-control', lambda game: game.click(806,722)),
            ('native-act-next-control', lambda game: game.click(850,722)),
            ('native-save-button', lambda game: game.click(897,753)),
            ('native-done-button', lambda game: game.click(1006,753)),
            ('native-cancel-button', lambda game: game.click(968,753)),
            ('native-save-shortcut', lambda game: self.hotkey(game,'s')),
            ('native-reset-shortcut', lambda game: self.hotkey(game,'n')),
            ('authored-avatar-drag', lambda game: self.slow_drag(game,(967,655),(700,470))),
        ]
        for game in self.games:
            for label, action in attempts:
                before = game.call('coop.editor inspect')
                action(game)
                game.call('wait.frames 30 15000')
                states = [status(peer) for peer in self.games]
                if any(s.get('phase') != 4 or not s.get('ready') or s.get('frozen') or s.get('revision') != revision for s in states):
                    raise FixtureError('Unsupported input escaped the shared editor: ' + label + ' ' + repr(states))
                if game.call('coop.editor inspect') != before:
                    raise FixtureError('Unsupported input changed authored native bytes: ' + label)
                if self.converge(revision) != baseline:
                    raise FixtureError('Unsupported input changed the canonical draft: ' + label)
                self.record(str(game.instance) + '-blocked-' + label)

    def hints(self):
        """Observe a real held drag without publishing its pose as authored state."""
        for game in self.games:
            game.call('coop.uistate 0')
        time.sleep(1.3)
        self.converge()
        owned = sorted((key for key in self.objects(self.host) if int(key.split(':')[0]) >= 4 << 32),
                       key=lambda key: int(key.split(':')[0]))
        if not owned:
            raise FixtureError('The held-drag fixture requires a previously accepted host placement')
        object_id = owned[0]
        self.host.click(*self.body(self.host, object_id))
        self.selected(self.host, object_id)
        revision = status(self.host)['revision']
        observed = False
        for _ in range(3):
            source = self.body(self.host, object_id)
            target = source[0]+12, source[1]
            remote_before = self.objects(self.guest)
            try:
                self.host.call(f'move {source[0]} {source[1]}')
                self.host.call('wait.frames 5 15000')
                self.host.call(f'button {source[0]} {source[1]} down')
                self.host.call('wait.frames 10 15000')
                self.host.call(f'move {target[0]} {target[1]}')
                time.sleep(.75)
                if not status(self.host).get('gesture'):
                    continue  # Ordinary first input may only request its lease.
                self.guest.wait('renderer-only remote drag hint', lambda: status(self.guest).get('previews',0) > 0)
                held_states = [status(game) for game in self.games]
                if any(row['revision'] != revision for row in held_states):
                    raise FixtureError('A held drag committed before the owner released it')
                if self.objects(self.guest) != remote_before:
                    raise FixtureError('A renderer-only hint changed remote native placements')
                self.guest.record('remote-drag-hint')
                self.host.record('owner-held-drag')
                self.guest.call('coop.uistate 1')
                time.sleep(1.3)
                self.guest.record('shared-panel-held-drag')
                self.result['held_hint'] = {'object':object_id,'states':held_states,
                                           'remote_native_unchanged':True,'commit_before_release':False}
                observed = True
            finally:
                self.host.call(f'button {target[0]} {target[1]} up')
            if observed:
                break
            time.sleep(.5)
        if not observed:
            raise FixtureError('No actual native held drag began after editing ownership was requested')
        self.converge(revision+1)
        self.guest.call('coop.uistate 0')
        self.guest.wait('remote hint cleared after commit', lambda: status(self.guest).get('previews') == 0)
        self.record('hint-committed-after-release')

    def class_negative(self):
        """Reject actual class input before mutation, including its normal close boundary."""
        for game in self.games:
            game.call('coop.uistate 0')
        time.sleep(1.3)
        for game in self.games:
            objects = self.objects(game)
            selected = status(game).get('selection')
            object_id = selected if selected in objects else sorted(objects,key=lambda key:int(key.split(':')[0]))[-1]
            before = game.call('coop.editor inspect')
            canonical = self.converge()
            revision = status(self.host)['revision']
            point = self.body(game,object_id)
            game.click(*point); game.click(*point)
            time.sleep(.75)
            if game.caption('Speech/Captions'):
                self.slow_drag(game,(235,253),(100,253))
                game.click(282,162)  # The ordinary class settings dismiss control.
                route='visible class slider blocked, normal close'
            else:
                route='native class opener blocked before editable controls'
            self.result.setdefault('class_input_routes',[]).append({'instance':game.instance,'route':route})
            game.call('wait.frames 30 15000');time.sleep(.75)
            states = [status(peer) for peer in self.games]
            if any(row.get('frozen') or row.get('phase') != 4 or row.get('revision') != revision for row in states):
                raise FixtureError('Class settings input reached an authored setter: '+repr(states))
            if game.call('coop.editor inspect') != before or self.converge(revision) != canonical:
                raise FixtureError('Class settings changed authored native state')
            def move_after_class_close():
                source=self.body(game,object_id)
                self.slow_drag(game,source,(source[0]-8,source[1]))
            self.gesture(game,str(game.instance)+'-valid-move-after-class-close',move_after_class_close)
            self.record(str(game.instance)+'-class-binder-blocked-and-closed')


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
    p.add_argument('--stage', choices=('all','bootstrap','join','edit','yaw','playtest','negative','hints','class_negative','snapshot'), default='all')
    args = p.parse_args(); pair = None
    try:
        pair = EditorPair(args)
        stages = ('bootstrap','join','edit','playtest','negative') if args.stage == 'all' else (args.stage,)
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
