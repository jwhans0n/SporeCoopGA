#!/usr/bin/env python3
"""Native creation workflows on an already-owned hidden two/four-player lab.

Never launches/deploys games or writes profiles/databases. Native construction,
name/save dialogs and editor-specific input use a bounded UI recipe supplied for
the private fixture's installed seeds and viewport. Every recipe action is normal
mouse/key input or native card selection; arbitrary console commands are refused.
"""
import argparse
import ctypes
from ctypes import wintypes as W
import json
from pathlib import Path
import re
import time

from coop_adventure import FixtureError, LINE, match_groups
from coop_adventure_editor import EditorPair, status
from coop_editor_party import Ownership, PublicConsole, PublicGame, parse_history
from sporectl import quote

FAMILIES = ('creature', 'building', 'vehicle', 'spaceship')
TYPES = (0x2B978C46, 0x2399BE55, 0x24682294, 0x476A98C7)
NEW, EDIT, CHOICE = 0xC00F0301, 0xC00F0302, 0xC00F0310
ASSET = re.compile(r'^asset class=(\d+:\d+) settings=(\d+:\d+) key=([0-9A-Fa-f]{8})!([0-9A-Fa-f]{8})\.([0-9A-Fa-f]{8}) content_revision=(\d+) behavior_sha256=([0-9a-f]{64})$')


def creation_command(line):
    if line in ('coop.editor assets', 'coop.editor creations'):
        return True
    if re.fullmatch(r'coop\.editor model [0-9A-Fa-f]{8}![0-9A-Fa-f]{8}\.[0-9A-Fa-f]{8}', line):
        return True
    match = re.fullmatch(r'coop\.editor asset ([0-9]{1,20}):([0-9]{1,10})', line)
    return bool(match and 0 < int(match[1]) <= 0xFFFFFFFFFFFFFFFF and 0 < int(match[2]) <= 0xFFFFFFFF)


class CreationConsole(PublicConsole):
    def call(self, line):
        if creation_command(line):
            if time.monotonic() > self.deadline:
                raise FixtureError('Creation fixture deadline expired')
            # The parent validates every other command against public_command.
            from sporectl import Console
            return Console.call(self, line)
        return super().call(line)


class CreationGame(PublicGame):
    def __init__(self, instance, output, timeout, deadline, viewport):
        super().__init__(instance, output, timeout, deadline)
        self.console = CreationConsole(instance, deadline)
        self.viewport = viewport

    def choose_card(self, name):
        def point():
            for lines in match_groups(self.console, name):
                for index, line in enumerate(lines):
                    if 'id=0x0674AA70' not in line:
                        continue
                    nodes = [LINE.match(value) for value in lines[index:]]
                    if not nodes or not all(nodes):
                        continue
                    rects = [(int(node.group(1), 16), *(int(node.group(i)) for i in range(2, 6))) for node in nodes]
                    if not all(rect[0] & 1 for rect in rects):
                        continue
                    _, x1, y1, x2, y2 = rects[0]
                    x = sum(rect[1] for rect in rects[1:]) + (x1+x2)//2
                    y = sum(rect[2] for rect in rects[1:]) + (y1+y2)//2
                    if 0 <= x < self.viewport[0] and 0 <= y < self.viewport[1]:
                        return x, y
            return None
        self.wait('visible native card: '+name, point)
        for _ in range(4):
            self.click(*self.wait('visible native card: '+name, point))
            for _ in range(4):
                if not self.control(0x53C6DC80):
                    return
                check = self.control(0x00AD0E52)
                if check and check[4]:
                    self.accept_shopper();return
                time.sleep(.25)
        raise FixtureError(f'instance {self.instance}: native card did not select: {name}')


def assets(text):
    header = re.search(r'^revision=(\d+) durable=(\d+) canonicaldocumenthash=([0-9a-f]{64})$', text, re.M)
    if not header:
        raise FixtureError('Missing canonical asset diagnostic: ' + text)
    if re.search(r'^truncated=1$', text, re.M):
        raise FixtureError('Canonical asset diagnostic was truncated')
    rows = {}
    for line in text.splitlines():
        if not line.startswith('asset '):
            continue
        found = ASSET.fullmatch(line)
        if not found or found[1] in rows:
            raise FixtureError('Malformed/duplicate canonical asset row: ' + line)
        rows[found[1]] = dict(settings=found[2], key=tuple(int(found[i], 16) for i in (3, 4, 5)),
                              content_revision=int(found[6]), behavior=found[7])
    return dict(revision=int(header[1]), durable=int(header[2]), hash=header[3], rows=rows)


def validate_steps(steps):
    if not isinstance(steps, list) or not 1 <= len(steps) <= 80:
        raise ValueError('A recipe needs 1..80 ordinary UI actions')
    for step in steps:
        if not isinstance(step, dict) or len(step) != 1:
            raise ValueError('Each recipe action has exactly one key')
        action, value = next(iter(step.items()))
        if action in ('click', 'drag', 'slow_drag'):
            if not isinstance(value, list) or len(value) != (2 if action == 'click' else 4) or any(type(v) is not int or not 0 <= v <= 4095 for v in value):
                raise ValueError('Bounded native input coordinates required')
        elif action == 'control':
            if not isinstance(value, str) or not re.fullmatch('[0-9A-Fa-f]{8}', value):
                raise ValueError('Expected a native control ID')
        elif action == 'frames':
            if type(value) is not int or not 1 <= value <= 300:
                raise ValueError('Bounded native frame wait required')
        elif action in ('press', 'wait_caption', 'choose_card', 'hover_caption', 'text', 'key'):
            if not isinstance(value, str) or not 1 <= len(value) <= 256 or '\n' in value or '\r' in value:
                raise ValueError('Bounded single-line native input required')
            if action == 'key' and not re.fullmatch(r'[a-z0-9]+(?: (?:down|up))?', value):
                raise ValueError('Expected a native key action')
        else:
            raise ValueError('Unknown/native-call/file-write action refused: ' + action)


def recipes(path):
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    if data.get('schema') != 1 or set(data.get('families', {})) != set(FAMILIES):
        raise ValueError('Recipe schema1 requires the four Adventure-compatible creation families')
    viewport = data.get('viewport')
    if not isinstance(viewport, list) or len(viewport) != 2 or any(type(n) is not int or not 640 <= n <= 4096 for n in viewport):
        raise ValueError('Recipe must identify its measured native viewport')
    for family in FAMILIES:
        item = data['families'][family]
        for stage in ('prepare', 'save', 'edit', 'select', 'cancel'):
            validate_steps(item.get(stage))
    validate_steps(data['families']['creature'].get('behavior'))
    validate_steps(data.get('unrelated_asset'))
    stages = [steps for item in data['families'].values() for steps in item.values()] + [data['unrelated_asset']]
    for steps in stages:
        if not isinstance(steps, list):
            continue
        for step in steps:
            action, value = next(iter(step.items()))
            if action in ('click', 'drag', 'slow_drag') and any(value[i] >= viewport[i % 2] for i in range(len(value))):
                raise ValueError('Native recipe input must remain inside its measured viewport')
    return data


def input_steps(game, steps, name):
    for step in steps:
        action, value = next(iter(step.items()))
        if isinstance(value, str):
            value = value.replace('{name}', name)
        if action == 'control':
            game.click_control(int(value, 16))
        elif action == 'click':
            game.click(*value)
        elif action == 'drag':
            game.call('drag ' + ' '.join(map(str, value)))
        elif action == 'slow_drag':
            # Part palettes need native hover/hold time. Preserve ordinary
            # drag recipes, with an explicit calibrated option for creation.
            EditorPair.slow_drag(game, value[:2], value[2:])
        elif action == 'frames':
            game.call(f'wait.frames {value} 30000')
        elif action == 'press':
            game.press(value)
        elif action == 'wait_caption':
            game.wait_caption(value)
        elif action == 'choose_card':
            game.choose_card(value)
        elif action == 'hover_caption':
            x, y = game.wait_caption(value);game.call(f'move {x} {y}');game.call('wait.frames 8 15000')
        elif action == 'text':
            game.call('text ' + quote(value))
        elif action == 'key':
            game.call('key ' + value)


def verify_viewport(owner, games, viewport):
    """Read only the exact hidden HWNDs owned by this private lab."""
    windows = owner.verify_windows()
    user = ctypes.WinDLL('user32', use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = [W.HWND, ctypes.POINTER(W.DWORD)]
    user.GetWindowThreadProcessId.restype = W.DWORD
    user.GetClientRect.argtypes = [W.HWND, ctypes.POINTER(W.RECT)]
    user.GetClientRect.restype = W.BOOL
    measurements = []
    for game in games:
        info = game.call('info')
        found = re.search(r'\bhwnd=([0-9A-Fa-f]+)', info)
        if not found:
            raise FixtureError('Missing native game HWND: ' + info)
        hwnd = int(found[1], 16)
        owned = [item for item in windows['windows'] if item['hwnd'] == hwnd]
        pid, rect = W.DWORD(), W.RECT()
        if len(owned) != 1 or not user.GetWindowThreadProcessId(hwnd, ctypes.byref(pid)) or pid.value != owned[0]['pid']:
            raise FixtureError('Native creation viewport is outside the owned process set')
        if not user.GetClientRect(hwnd, ctypes.byref(rect)) or [rect.right-rect.left, rect.bottom-rect.top] != viewport:
            raise FixtureError('Native UI recipe viewport does not match its exact owned game window')
        measurements.append(dict(instance=game.instance, hwnd=hwnd, pid=pid.value, viewport=viewport))
    return measurements


def run(args):
    recipe = recipes(args.recipes)
    args.output.mkdir(parents=True, exist_ok=False)
    owner = Ownership(args.metadata, args.instances, args.dll_sha256)
    deadline = time.monotonic() + args.deadline
    games = [CreationGame(i, args.output, args.timeout, deadline, recipe['viewport']) for i in args.instances]
    host, guest = games[:2]
    result = dict(passed=False, complete_acceptance=False, dll_sha256=args.dll_sha256, checks=[], matrix=[], recipe=recipe)

    def canonical(revision=None):
        states = [status(game) for game in games]
        if any(state.get('frozen') for state in states):
            raise FixtureError('Creation workflow froze a participant')
        if any(state.get('phase') != 4 for state in states):
            return False
        snapshots = [assets(game.call('coop.editor assets')) for game in games]
        if any(s['revision'] != s['durable'] or revision is not None and s['revision'] != revision for s in snapshots):
            return False
        return snapshots[0] if len({(s['revision'], s['hash']) for s in snapshots}) == 1 and all(s['rows'] == snapshots[0]['rows'] for s in snapshots) else False

    def record(label):
        owner.verify_windows()
        row = dict(label=label, states=[status(g) for g in games], assets=[g.call('coop.editor assets') for g in games],
                   creations=[g.call('coop.editor creations') for g in games])
        for game in games:
            faults = game.call('exc')
            if 'total faults: 0' not in faults:
                raise FixtureError('Native exception: ' + faults)
            if args.screenshots:
                game.record(label)
        result['checks'].append(row)
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')

    def designing(game):
        return bool(re.search(r'creation_editor active=1 phase=1\b', game.call('coop.editor creations')))

    def open_new(game, family):
        game.call('coop.uistate 0');game.click_control(NEW);game.click_control(CHOICE+FAMILIES.index(family))
        # These families expose native subtypes before entry. The default
        # recipes use City hall / Military land, the first measured variants.
        if family in ('building', 'vehicle'):
            game.click_control(0xC00F0340)
        game.wait('local native '+family+' editor', lambda: designing(game))

    def returned(game):
        return 'creation_editor active=0' in game.call('coop.editor creations') and status(game).get('ready')

    def save(game, family, name, revision):
        input_steps(game, recipe['families'][family]['save'], name)
        game.wait('independent native adventure return', lambda: returned(game))
        return host.wait('durable creation admission', lambda: canonical(revision))

    def change_once(game, label, action):
        before = host.wait('canonical state before '+label, canonical)
        history = len(parse_history(host.call('coop.editor history')))
        action()
        after = host.wait(label, lambda: canonical(before['revision']+1))
        if len(parse_history(host.call('coop.editor history'))) != history+1:
            raise FixtureError(label+' did not produce exactly one shared Undo item')
        return before, after

    try:
        owner.verify_windows()
        host.wait('private shared adventure baseline', canonical)
        if not all(status(game).get('ready') for game in games):
            raise FixtureError('Every participant must begin in its native Adventure Editor')
        # UI recipes are measured for an exact viewport; validate without costly
        # native backbuffer readback, using the owned window client rectangle.
        result['owned_windows'] = verify_viewport(owner, games, recipe['viewport'])
        record('baseline')
        created = {}
        for family in FAMILIES:
            for author in (host, guest):
                name = f'Creation {args.tag} {family} {author.instance}'
                baseline = host.wait('canonical state before native creation', canonical);history = len(parse_history(host.call('coop.editor history')))
                open_new(author, family)
                input_steps(author, recipe['families'][family]['prepare'], name)
                if canonical() != baseline:
                    raise FixtureError('A local unsaved creation changed the shared adventure')
                after = save(author, family, name, baseline['revision']+1)
                added = set(after['rows'])-set(baseline['rows'])
                if len(added) != 1 or set(baseline['rows'])-set(after['rows']):
                    raise FixtureError('Saving a new creation did not add exactly one cast entry')
                cls = added.pop();row = after['rows'][cls]
                if row['key'][2] != TYPES[FAMILIES.index(family)] or row['key'][1] >> 24 != 0x7D:
                    raise FixtureError('The saved cast entry has the wrong native family or mutable identity')
                if len(parse_history(host.call('coop.editor history'))) != history+1:
                    raise FixtureError('Creation admission did not produce one Undo item')
                created[(family, author.instance)] = (cls, name)
                author.call('coop.editor undo');host.wait('creation Undo', lambda: canonical(after['revision']+1))
                if cls in canonical()['rows']:
                    raise FixtureError('Creation Undo left a partial cast entry')
                author.call('coop.editor redo');restored = host.wait('creation Redo', lambda: canonical(after['revision']+2))
                if restored['rows'][cls]['key'] != row['key']:
                    raise FixtureError('Creation Redo lost its immutable content')
                result['matrix'].append(dict(family=family, author=author.instance, class_id=cls))
                record(f'{family}-{author.instance}-save-undo-redo')
        # All participants independently leave the native Adventure Editor. The
        # first guest returns and commits while the host continues designing.
        baseline = canonical();pending = []
        for index, game in enumerate(games):
            family = FAMILIES[index];name = f'Parallel {args.tag} {game.instance}'
            open_new(game, family);input_steps(game, recipe['families'][family]['prepare'], name)
            pending.append((game, family, name))
        if not all(designing(game) for game in games) or canonical() != baseline:
            raise FixtureError('Simultaneous local editors did not preserve canonical authoring')
        record('all-participants-in-independent-editors')
        first = pending.pop(1);save(*first, baseline['revision']+1)
        if not designing(host):
            raise FixtureError('Guest return forced the host out of its creation editor')
        change_once(guest, 'guest edits an unrelated asset while host is away',
                    lambda: input_steps(guest, recipe['unrelated_asset'], args.tag))
        if not designing(host):
            raise FixtureError('Guest authoring resumed the host scene')
        for item in pending[1:]+pending[:1]:
            save(*item, canonical()['revision']+1)
        record('independent-return-and-host-away-authoring')
        # Edit another participant's asset while a second participant changes
        # that class's behavior. Content commit must retain the later behavior.
        cls, name = created[('creature', guest.instance)]
        input_steps(host, recipe['families']['creature']['select'], name)
        host.click_control(EDIT);host.wait('edit another participant creation', lambda: designing(host))
        baseline = canonical();old = baseline['rows'][cls]
        input_steps(guest, recipe['families']['creature']['select'], name)
        _, changed = change_once(guest, 'behavior change during content editing',
                                lambda: input_steps(guest, recipe['families']['creature']['behavior'], name))
        if changed['rows'][cls]['key'] != old['key'] or changed['rows'][cls]['behavior'] == old['behavior']:
            raise FixtureError('Concurrent class behavior edit was blocked or changed asset content')
        input_steps(host, recipe['families']['creature']['edit'], name)
        after = save(host, 'creature', name, changed['revision']+1)
        if after['rows'][cls]['behavior'] != changed['rows'][cls]['behavior'] or after['rows'][cls]['key'] == old['key']:
            raise FixtureError('Asset replacement lost a later behavior edit or reused mutable bytes')
        record('shared-asset-replacement-preserves-later-behavior')
        baseline = canonical();history = host.call('coop.editor history')
        open_new(guest, 'creature');input_steps(guest, recipe['families']['creature']['prepare'], 'Cancelled '+args.tag)
        input_steps(guest, recipe['families']['creature']['cancel'], args.tag)
        guest.wait('native cancellation returns independently', lambda: returned(guest))
        if canonical() != baseline or host.call('coop.editor history') != history:
            raise FixtureError('Cancelling a creation changed the cast/revision/history')
        record('native-creation-cancellation')
        checks = [game.call('coop.editor check').strip() for game in games]
        if not checks[0].startswith('CONVERGED ') or len(set(checks)) != 1:
            raise FixtureError('Returned native adventure scenes do not match the committed draft')
        result['passed'] = True
        result['workflow_passed'] = True
        print(f'PASS: {len(games)} participants, four Adventure-compatible creation families and independent native returns', flush=True)
    except Exception as error:
        result['error'] = repr(error)
        raise
    finally:
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        for game in games:
            game.save_log()
        owner.close()


def self_test():
    for line in ('coop.editor assets', 'coop.editor creations', 'coop.editor asset 8589935934:1',
                 'coop.editor model 40646200!2912711D.24682294'):
        assert creation_command(line)
    for line in ('coop.editor asset 0:1', 'coop.editor asset 18446744073709551616:1',
                 'coop.editor asset 1:4294967296', 'coop.editor model ../bad',
                 'coop.editor model 40646200!2912711D.2468229G',
                 'coop.editor model 40646200!2912711D.24682294\ncoop.editor end',
                 'coop.editor model 40646200!2912711D.24682294 extra'):
        assert not creation_command(line)
    row = 'revision=7 durable=7 canonicaldocumenthash='+'a'*64+'\nasset class=8589934593:1 settings=21474836481:1 key=40626200!7D000001.2B978C46 content_revision=6 behavior_sha256='+'b'*64+'\n'
    assert assets(row)['rows']['8589934593:1']['key'] == (0x40626200, 0x7D000001, 0x2B978C46)
    try:
        assets(row+'truncated=1\n')
    except FixtureError:
        pass
    else:
        raise AssertionError('Truncated canonical asset diagnostic accepted')
    validate_steps([{'click': [10, 20]}, {'slow_drag': [10, 20, 30, 40]}, {'control': 'C00F0301'}, {'text': '{name}'}, {'key': 'ctrl down'}])
    for invalid in ([{'call': 'Editor::Save'}], [{'mem.write': '00'}], [{'click': [-1, 2]}], [{'frames': 99999}], [{'key': 'esc\ncall foo'}]):
        try:
            validate_steps(invalid)
        except ValueError:
            pass
        else:
            raise AssertionError('Unsafe UI recipe accepted')
    print('creation integration fixture self-test passed')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--metadata', type=Path)
    parser.add_argument('--instances', type=int, nargs='+', default=[95, 96])
    parser.add_argument('--dll-sha256')
    parser.add_argument('--recipes', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--tag', default='Preview9')
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--deadline', type=int, default=3600)
    parser.add_argument('--screenshots', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test();return
    if not args.execute:
        print(json.dumps(dict(prepared=True, launches_games=False, required_recipe_families=FAMILIES,
                              required_stages=['prepare', 'save', 'edit', 'select', 'cancel'], behavior_required='creature')))
        return
    if (not args.metadata or not args.output or not args.recipes or len(args.instances) not in (2, 4) or
            len(set(args.instances)) != len(args.instances) or not re.fullmatch('[0-9a-f]{64}', args.dll_sha256 or '') or
            not re.fullmatch('[A-Za-z0-9_-]{1,32}', args.tag) or not 60 <= args.timeout <= 900 or not 300 <= args.deadline <= 7200):
        parser.error('Exact owned metadata, candidate SHA, native UI recipes and bounded two/four-player arguments required')
    run(args)


if __name__ == '__main__':
    main()
