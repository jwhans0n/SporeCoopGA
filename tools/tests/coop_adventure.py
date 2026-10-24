#!/usr/bin/env python3
"""Native GA fixtures driven through the normal menu and captain UI.

The Windows lab runner owns game processes, invisibility and muting. This helper
only connects to its existing test instances. It never copies or changes saves.

Examples (run the Windows lab first):
  py -3 tools/tests/coop_adventure.py onboard -i 22 --name CoopGuest22
  py -3 tools/tests/coop_adventure.py pair --host 21 --guest 22 --host-name Phien --guest-name CoopGuest22

``onboard`` creates a personal captain in a fresh test profile and starts Welcome.
``pair`` connects two menu instances, starts Welcome through the host's normal
Play button, and checks the same committed run, world, and reciprocal puppets.
Both profiles must already have a personal captain for the pair case. The guest
captain setup wizard reuses its saved captain name, supplied with --guest-name.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from sporectl import Console, quote, recompress_png  # noqa: E402
from uifind import LINE, match_groups, shown  # noqa: E402


class FixtureError(RuntimeError):
    pass


class Game:
    def __init__(self, instance, output, timeout=120):
        self.instance = instance
        self.output = Path(output)
        self.output.mkdir(parents=True, exist_ok=True)
        self.timeout = timeout
        self.console = Console(instance, timeout=20)
        self.events = []

    def call(self, command, required=True):
        response = self.console.call(command)
        self.events.append({'time': time.time(), 'command': command, **response})
        if required and not response.get('ok'):
            raise FixtureError(f'instance {self.instance}: {command}: {response.get("out", "")}')
        return response.get('out', '')

    def wait(self, description, predicate, timeout=None):
        deadline = time.monotonic() + (self.timeout if timeout is None else timeout)
        while time.monotonic() < deadline:
            result = predicate()
            if result:
                return result
            time.sleep(.25)
        raise FixtureError(f'instance {self.instance}: timeout waiting for {description}')

    def caption(self, text):
        point = shown(self.console, text)
        if point:
            return point
        # The first full-name match in a Sporepedia card is its hidden overflow
        # label (53D6FE29). Its card remains visible and has a second clipped
        # label (53D6FE30). Click the visible card without relying on truncation.
        for lines in match_groups(self.console, text):
            if not lines or 'id=0x53D6FE29' not in lines[0]:
                continue
            nodes = [LINE.match(line) for line in lines]
            if len(nodes) < 2 or not all(nodes):
                continue
            values = [tuple(int(node.group(i), 16 if i == 1 else 10) for i in range(1, 6)) for node in nodes]
            if not all(node[0] & 1 for node in values[1:]):
                continue
            _, x1, y1, x2, y2 = values[1]
            ox, oy = sum(node[1] for node in values[2:]), sum(node[2] for node in values[2:])
            return ox + (x1 + x2) // 2, oy + (y1 + y2) // 2
        return None

    def wait_caption(self, text):
        return self.wait(repr(text), lambda: self.caption(text))

    def click(self, x, y):
        self.call(f'move {int(x)} {int(y)}')
        time.sleep(.15)
        self.call(f'click {int(x)} {int(y)}')
        self.call('wait.frames 7 15000')  # native click releases after six frames

    def control(self, ident):
        text = self.call(f'coop.ui where {ident:08X}')
        match = re.search(r'^[0-9A-F]{8} (-?\d+) (-?\d+) (-?\d+) (-?\d+) enabled=(\d+)', text)
        return tuple(map(int, match.groups())) if match else None

    def click_control(self, ident, enabled=True):
        def usable():
            rect = self.control(ident)
            return rect if rect and (rect[4] or not enabled) else None
        x1, y1, x2, y2, _ = self.wait(f'control {ident:08X}', usable)
        self.click((x1 + x2) // 2, (y1 + y2) // 2)

    def control_caption(self, ident):
        tree = self.call('coop.ui tree 7')
        line = next((line for line in tree.splitlines() if f'id=0x{ident:08X} ' in line), None)
        if line is None:
            return None
        match = re.search(r"'([^']*)'", line)
        return match.group(1) if match else ''

    def press(self, caption):
        x, y = self.wait_caption(caption)
        self.click(x, y)

    def shot(self, label):
        path = (self.output / f'{self.instance}-{label}.png').resolve()
        self.call('shot ' + quote(str(path)))
        return recompress_png(path)

    def modal_click(self, x, y):
        # The shipped onboarding panels are centered with fixed pixel dimensions.
        # Coordinates below were measured in the 1024x768 native Windows fixture.
        width, height = self.shot('before-click')
        self.click(x + (width - 1024) // 2, y + (height - 768) // 2)

    def record(self, label):
        self.shot(label)
        tree = self.call('coop.ui tree 6', required=False)
        (self.output / f'{self.instance}-{label}-ui.txt').write_text(tree, encoding='utf-8')
        self.save_log()

    def save_log(self):
        (self.output / f'{self.instance}-commands.json').write_text(
            json.dumps(self.events, indent=2) + '\n', encoding='utf-8')

    def menu(self):
        return 'GalaxyGameEntry' in self.call('game.mode')

    def dismiss_registration(self):
        # Closing registration and accepting its confirmation only affects the
        # isolated lab profile; no online account is created or contacted here.
        def ready():
            if self.caption('letterbox_top'):
                self.call('key esc')  # native opening movie
                time.sleep(.5)
            if self.caption('Login'):
                self.modal_click(868, 159)
                self.wait('registration closes or asks for confirmation',
                          lambda: self.caption("If you don't register") or not self.caption('Login'))
            if self.caption("If you don't register"):
                self.press('OK')
            return self.menu() and (self.control(0x070D74E7) or self.caption('Welcome to Spore Galactic Adventures'))
        self.wait('galaxy menu controls', ready)

    def open_welcome(self):
        self.dismiss_registration()
        for _ in range(4):
            if self.caption('Welcome to Spore Galactic Adventures'):
                break
            self.press('Galactic Adventures')
            self.call('wait.frames 10 15000')
            time.sleep(.25)
        self.wait_caption('Welcome to Spore Galactic Adventures')
        self.record('welcome')

    def start_welcome(self):
        self.open_welcome()
        self.click_control(0x074D0932, enabled=False)  # native large Play button

    def accept_shopper(self):
        self.shot('shopper-selection')
        self.click_control(0x00AD0E52)  # native Sporepedia green check

    def choose_card(self, name):
        def card_point():
            # Names also appear in hover previews. Select the actual thumbnail
            # card, whose native container is 0674AA70, after layout settles.
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
                    x = sum(rect[1] for rect in rects[1:]) + (x1 + x2) // 2
                    y = sum(rect[2] for rect in rects[1:]) + (y1 + y2) // 2
                    if 0 <= x < 1024 and 0 <= y < 768:
                        return x, y
            return None
        self.wait('visible native card: ' + name, card_point)
        for _ in range(4):
            self.click(*self.wait('visible native card: ' + name, card_point))
            # A thumbnail can ignore its first click while a category's layout
            # settles. Require the native selection check before accepting it.
            for _ in range(4):
                if not self.control(0x53C6DC80):
                    return  # a native double click already accepted the card
                check = self.control(0x00AD0E52)
                if check and check[4]:
                    self.accept_shopper()
                    return
                time.sleep(.25)
        raise FixtureError(f'instance {self.instance}: native card did not select: {name}')

    def start_saved_adventure(self, adventure_name, captain_name):
        self.start_welcome()
        self.wait('adventure choice', lambda: self.adventure()['active'] or self.caption('Choose an Adventure'))
        if self.adventure()['active']:
            return
        self.modal_click(620, 392)  # choose an adventure card, opening Sporepedia
        self.choose_card(adventure_name.strip('<>'))
        self.wait('adventure selection', lambda: self.adventure()['active'] or self.caption('Choose an Adventure'))
        if self.adventure()['active']:
            return
        self.modal_click(860, 470)
        self.wait('captain choice', lambda: self.adventure()['active'] or self.caption('Choose or Create Your Captain'))
        if self.adventure()['active']:
            return
        self.click_control(0x07CA4119)  # choose an existing native captain
        self.select_saved_captain(captain_name)
        self.wait_caption('Choose or Create Your Captain')
        self.click_control(0x05384D06)
        self.wait('captain review', lambda: self.caption('Review Your Captain') or self.caption('Name Your Captain'))
        if self.caption('Review Your Captain'):
            self.modal_click(860, 470)
        else:
            self.click_control(0x05951103)

    def prepare_captain(self, name, create=True, save=True):
        self.wait_caption('Choose or Create Your Captain')
        if create:
            self.onboard(name, stop_after='named')
        else:
            self.click_control(0x07CA4119)
            self.select_saved_captain(name)
            self.wait_caption('Choose or Create Your Captain')
            self.click_control(0x05384D06)
            self.wait('captain review', lambda: self.caption('Review Your Captain') or self.caption('Name Your Captain'))
        if save:
            # Native preview layout can expose Finish before it accepts input.
            # Require the panel to close rather than treating one mouse tap as a save.
            for _ in range(4):
                self.click_control(0x05951103)
                time.sleep(.5)
                if not self.caption('Review Your Captain') and not self.caption('Name Your Captain'):
                    break
            else:
                raise FixtureError('native captain save confirmation did not leave review')

    def onboard(self, name, stop_after='play'):
        if not self.caption('Choose or Create Your Captain') and not self.caption('Name Your Captain'):
            self.start_welcome()
            self.wait('captain creation panel', lambda: self.caption('Choose or Create Your Captain') or self.caption('Name Your Captain'))
        if self.caption('Choose or Create Your Captain'):
            self.record('choose-captain')
            self.click_control(0x07CCC126)  # generate a body in this profile
            if stop_after == 'choose':
                self.record('body-selected')
                return
            self.click_control(0x05384D06)
        self.wait_caption('Name Your Captain')
        self.click_control(0x05400FA5)
        time.sleep(.35)  # captain name edit must take focus before text messages
        # Modifier taps can be translated after Ctrl is released and insert a
        # literal 'a'. Use the edit's ordinary End/backspace handling instead.
        self.call('key 0x23')
        time.sleep(.2)
        self.call('text ' + quote('\b' * 128))
        self.wait('empty captain name', lambda: self.control_caption(0x05400FA5) == '')
        self.call('text ' + quote(name))
        self.wait('exact captain name', lambda: self.control_caption(0x05400FA5) == name)
        self.record('named-captain')
        if stop_after == 'named':
            return
        self.click_control(0x05951103)
        self.wait('native Welcome captain', lambda: self.scenario_avatar())
        self.record('welcome-playing')

    def scenario_avatar(self):
        if 'Scenario' not in self.call('game.mode'):
            return None
        # An avatar is allocated while the loading screen is still up. Its
        # initial stats are not the playable captain's final stats yet.
        if self.control(0x0762350F):
            return None
        avatar = self.console.call('coop.avatar')
        return avatar['out'] if avatar.get('ok') else None

    def captain_key(self):
        avatar = self.call('coop.avatar')
        local = re.search(r'local=([0-9A-Fa-f]+)', avatar)
        if not local:
            raise FixtureError('native local captain is unavailable: ' + avatar)
        # Verified cCreatureAnimal::mSpeciesKey offset, shared with game.h.
        address = int(local.group(1), 16) + 0xB28
        text = self.call(f'mem.read 0x{address:X} 12')
        match = re.search(r'^[0-9A-Fa-f]+\s+((?:[0-9A-Fa-f]{2} ?){12})', text)
        if not match:
            raise FixtureError('cannot read native captain key: ' + text)
        raw = bytes.fromhex(match.group(1))
        model_type = self.call(f'call Sporepedia::GetAssetModelType cdecl 0x{address:X}')
        if 'eax=0x4178B8E8' not in model_type:
            raise FixtureError('native local model is not an outfitted captain: ' + model_type)
        return {name: int.from_bytes(raw[index * 4:index * 4 + 4], 'little')
                for index, name in enumerate(('instance', 'type', 'group'))}

    def exit_adventure(self):
        if self.menu():
            return
        for _ in range(3):
            if self.caption('Exit to Main Menu'):
                break
            self.call('key esc')
            time.sleep(.5)  # the first Escape can dismiss the native intro movie
        self.wait_caption('Exit to Main Menu')
        self.press('Exit to Main Menu')
        self.wait_caption('All progress will be lost')
        self.press('Quit')
        self.wait('galaxy menu after native adventure exit', self.menu)

    def adventure(self):
        text = self.call('coop.adventure status')
        match = re.search(r'run=(\d+) active=(\d+) ready=(\d+) players=(\d+)', text)
        if not match:
            raise FixtureError('unrecognized adventure status: ' + text)
        return dict(zip(('run', 'active', 'ready', 'players'), map(int, match.groups())), text=text)

    def select_saved_captain(self, name):
        for _ in range(4):
            if self.caption(name):
                break
            if self.caption('My Creations'):
                self.press('My Creations')
            time.sleep(.5)  # the native category rows settle after the picker opens
        self.choose_card(name)


def verify_lab(metadata, instances):
    """Refuse to drive an unknown/personal profile when used on native Windows."""
    data = json.loads(Path(metadata).read_text(encoding='utf-8'))
    records = {entry['instance']: entry for entry in data['processes']}
    for instance in instances:
        entry = records.get(instance)
        if not entry or not entry.get('profile', '').startswith('winlab_'):
            raise FixtureError(f'instance {instance} is not recorded as an isolated Windows lab profile')
        if entry.get('exit_code') is not None:
            raise FixtureError(f'instance {instance} has exited (code {entry["exit_code"]})')
    return {instance: records[instance] for instance in instances}


def native_key(text):
    if text.lower() in ('town', 'adventure-town'):
        return 0xBF0CE0D0, 0x366A930D, 0x408A0000
    try:
        parts = tuple(int(part, 16) for part in re.split(r'[:/!]', text))
    except ValueError as error:
        raise argparse.ArgumentTypeError('use Town or INSTANCE:TYPE:GROUP in hexadecimal') from error
    if len(parts) != 3 or not parts[0] or not parts[1] or any(part < 0 or part > 0xFFFFFFFF for part in parts):
        raise argparse.ArgumentTypeError('use Town or a valid INSTANCE:TYPE:GROUP resource key')
    return parts


def pair(host, guest, port, host_name, guest_name, adventure_name, resume=False,
         friendly_fire=None, death_mode=None, debug_start=None):
    # Normal runs use native Sporepedia Play. The explicit debug shortcut enters
    # the same coordinator while retaining native personal captain selection.
    if resume and (friendly_fire is not None or death_mode is not None or debug_start is not None):
        raise FixtureError('difficulty and debug-start options require a fresh attempt without --resume')
    if not resume:
        for game in (host, guest):
            game.dismiss_registration()
            game.call('coop.leave')
        host.call(f'coop.host {port} CoopHost')
        guest.call(f'coop.join 127.0.0.1:{port} CoopGuest')
    host.wait('two-member lobby', lambda: host.adventure()['players'] == 2)
    guest.wait('joined lobby', lambda: guest.adventure()['players'] == 2)
    if not resume:
        host.call(f'coop.adventure friendlyfire {friendly_fire if friendly_fire is not None else 0}')
        host.call(f'coop.adventure death {death_mode or "revive"}')
        if debug_start is not None:
            host.call('coop.adventure start ' + ' '.join(f'0x{part:08X}' for part in debug_start))
        else:
            host.start_saved_adventure(adventure_name, host_name)
    host.call('coop.adventure ready 1')
    if not host.adventure()['text'].startswith('Waiting for party readiness'):
        if host.caption('Choose or Create Your Captain'):
            host.prepare_captain(host_name, create=False)
    guest.prepare_captain(guest_name, create=False)
    guest.call('coop.adventure ready 1')

    def committed():
        a, b = host.adventure(), guest.adventure()
        if not a['active'] and a['run']:
            raise FixtureError('host aborted: ' + a['text'])
        if not b['active'] and b['run']:
            raise FixtureError('guest aborted: ' + b['text'])
        return (a['run'] == b['run'] and a['run'] > 0 and
                'Adventure in progress' in a['text'] and 'Adventure in progress' in b['text'])

    host.wait('same committed adventure run', committed)
    avatars = [game.wait('local Scenario captain', game.scenario_avatar) for game in (host, guest)]
    captain_keys = [game.captain_key() for game in (host, guest)]
    if captain_keys[0] == captain_keys[1]:
        raise FixtureError('the unlocked Adventure Town fixture must use two personal captains')
    places = [re.search(r'place=0x([0-9A-Fa-f]+)', avatar) for avatar in avatars]
    if not all(places) or places[0].group(1) != places[1].group(1):
        raise FixtureError('captains are not in the same adventure world: ' + repr(avatars))
    for game in (host, guest):
        game.wait('remote captain puppet', lambda: 'puppet pos=' in game.call('coop.status'))
        game.record('coop-playing')
    status = host.adventure()
    mode = re.search(r'death=(\d+)', status['text'])
    friendly = re.search(r'friendly_fire=(\d+)', status['text'])
    return {'run': status['run'], 'avatars': avatars, 'captain_keys': captain_keys,
            'death_mode': ('revive', 'fail', 'auto')[int(mode.group(1))] if mode else None,
            'friendly_fire': int(friendly.group(1)) if friendly else None,
            'launch': 'resume' if resume else 'debug' if debug_start is not None else 'native_play'}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, default=ROOT / 'out/windows-lab/runtime-bootstrap-3/processes.json')
    parser.add_argument('--output', type=Path, default=ROOT / 'out/windows-lab/coop-adventure')
    parser.add_argument('--timeout', type=float, default=120)
    sub = parser.add_subparsers(dest='command', required=True)
    onboard = sub.add_parser('onboard', help='create a native personal captain and start Welcome')
    onboard.add_argument('-i', '--instance', type=int, required=True)
    onboard.add_argument('--name', required=True)
    onboard.add_argument('--stop-after', choices=('choose', 'named', 'play'), default='play')
    onboard.add_argument('--return-menu', action='store_true', help='leave the solo fixture through its native menu after saving the captain')
    shared = sub.add_parser('pair', help='start the same Welcome adventure from a two-player lobby')
    shared.add_argument('--host', type=int, default=21)
    shared.add_argument('--guest', type=int, default=22)
    shared.add_argument('--guest-name', required=True)
    shared.add_argument('--host-name', required=True)
    shared.add_argument('--adventure-name', default='Adventure Town')
    shared.add_argument('--port', type=int, default=45021)
    shared.add_argument('--resume', action='store_true', help='continue an existing two-player attempt at its captain pickers')
    shared.add_argument('--friendly-fire', type=int, choices=(0, 25, 50, 100), help='host setting before Play (default: 0)')
    shared.add_argument('--death-mode', choices=('revive', 'auto', 'fail'), help='host setting before Play (default: revive)')
    shared.add_argument('--debug-start', nargs='?', const=native_key('Town'), type=native_key,
                        metavar='KEY', help='skip menu Play and start Town, or a hexadecimal INSTANCE:TYPE:GROUP')
    args = parser.parse_args(argv)
    instances = [args.instance] if args.command == 'onboard' else [args.host, args.guest]
    games = []
    try:
        profiles = verify_lab(args.metadata, instances)
        games = [Game(instance, args.output, args.timeout) for instance in instances]
        result = {'profiles': profiles, 'command': args.command}
        if args.command == 'onboard':
            games[0].onboard(args.name, args.stop_after)
            result['captain_name'] = args.name
            if args.stop_after == 'play':
                result['captain_key'] = games[0].captain_key()
                if args.return_menu:
                    games[0].exit_adventure()
                    games[0].record('onboarded-menu')
        else:
            if args.host == args.guest:
                raise FixtureError('host and guest instances must differ')
            result.update(pair(*games, args.port, args.host_name, args.guest_name, args.adventure_name, args.resume,
                               args.friendly_fire, args.death_mode, args.debug_start))
        (args.output / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('RESULT: PASS', json.dumps(result))
        return 0
    except (FixtureError, OSError, ValueError) as error:
        print('RESULT: FAIL', error)
        for game in games:
            try:
                game.record('failure')
            except (OSError, FixtureError):
                game.save_log()
        return 1
    finally:
        for game in games:
            if game.console.sock:
                game.console.sock.close()


if __name__ == '__main__':
    sys.exit(main())
