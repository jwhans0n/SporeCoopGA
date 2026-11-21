#!/usr/bin/env python3
"""Owned native Windows lane for the original lab assertions.

An explicit config binds the source commit, payload, symbols, private installation,
fixtures, console identities, UDP discovery slot and measured viewport. Nothing is
inferred from a Wine prefix or from the player's installation/profile. This module
is also imported by tests/lane.py; importing it starts no game and changes no file.
"""
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import shlex
import socket
import tarfile
import tempfile
import time
import weakref

ROOT = Path(__file__).resolve().parents[1]
PAYLOAD = ('steam_api.dll', 'sporekit.dll', 'coop.dll', 'SporeCoop.package')
RESETTABLE = {'Games', 'Games-sporecoop-backup', 'Games-sporecoop-store', 'EditorSaves.package',
              'SporeCoop/holds', 'SporeCoop/badges', 'SporeCoop/settings', 'SporeCoop/choices.ini',
              'SporeCoop/worlds.ini'}
STOCK_SMOKE = {'creature', 'ui', 'team', 'select', 'frames_loop'}


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def contained(path, parent):
    path, parent = Path(path).resolve(), Path(parent).resolve()
    if path == parent or parent not in path.parents:
        raise ValueError(f'path must be strictly inside {parent}: {path}')
    return path


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')
    temporary.replace(path)


def real_unit_command(argv):
    """Permit the real make target, including the audited WSL toolchain adapter."""
    if not argv or Path(argv[0]).stem.lower() not in ('wsl', 'make'):
        return False
    if len(argv) >= 5 and tuple(argv[:4]) == ('wsl', '-e', 'bash', '-lc'):
        command = argv[4]
        if len(argv) != 5 or any(c in command for c in (';', '|', '&', '\n', '\r', '>', '<', '`', '$(')):
            return False
        words = shlex.split(command)
        if words and words[0].startswith('PATH='):
            words.pop(0)
        return bool(words and words[0] == 'make' and words[-1] == 'test'
                    and all(re.fullmatch(r'-j\d+|(?:B|HOSTCXX|LLVM_MINGW|TEST_JOBS)=[A-Za-z0-9_./:+-]+', word)
                            for word in words[1:-1]))
    return any(tuple(argv[i:i + 2]) == ('make', 'test') for i in range(len(argv) - 1))


@dataclass(frozen=True)
class LaneConfig:
    identity: str
    installation: Path
    output: Path
    fixtures: Path
    build: Path
    symbols: Path
    source_commit: str
    payload_hashes: dict
    symbol_hashes: dict
    fixture_hashes: dict
    host: int = 49
    guest: int = 50
    port: int = 47810
    viewport: tuple = (1440, 1080)
    screen_size: int = 12
    unit_command: tuple = ()

    @classmethod
    def load(cls, path, root=ROOT):
        raw = json.loads(Path(path).read_text(encoding='utf-8'))
        if raw.pop('schema', None) != 1:
            raise ValueError('Windows lane config schema must be 1')
        raw['identity'] = raw.pop('id')
        for key in ('installation', 'output'):
            raw[key] = contained(raw[key], Path(root) / 'out' / 'windows-lab')
        for key in ('fixtures', 'build', 'symbols'):
            raw[key] = Path(raw[key]).resolve()
        raw['viewport'] = tuple(raw.get('viewport', (1440, 1080)))
        raw['unit_command'] = tuple(raw.get('unit_command', ()))
        value = cls(**raw)
        if not re.fullmatch(r'[a-z][a-z0-9_]{0,39}', value.identity):
            raise ValueError('private lane id must be a short lowercase identifier')
        if not re.fullmatch(r'[a-f0-9]{40}', value.source_commit):
            raise ValueError('bind lane to a complete source commit')
        if value.host == value.guest or not all(2 <= n <= 100 for n in (value.host, value.guest)):
            raise ValueError('use distinct private console identities 2..100')
        if not 47810 <= value.port <= 47818:
            raise ValueError('choose an explicit discoverable UDP port 47810..47818')
        if value.viewport not in ((1440, 1080), (1024, 768)):
            raise ValueError('only audited campaign/editor viewports are supported')
        if not 0 <= value.screen_size <= 64:
            raise ValueError('invalid measured native screen-size option')
        if value.unit_command and not real_unit_command(value.unit_command):
            raise ValueError('unit adapter must invoke the real make test command')
        if set(value.payload_hashes) != set(PAYLOAD) or not value.symbol_hashes:
            raise ValueError('bind every payload and symbol file by SHA256')
        for records in (value.payload_hashes, value.symbol_hashes, value.fixture_hashes):
            for name, sha in records.items():
                if Path(name).name != name or not re.fullmatch(r'[a-f0-9]{64}', sha):
                    raise ValueError('hash records must use filenames and SHA256')
        return value

    def instance(self, role):
        return {'host': self.host, 'guest': self.guest}[role]

    def profile(self, role):
        return f'winlab_{self.identity}_{role}_{self.instance(role)}'

    def profile_dir(self, role):
        return Path(os.environ['APPDATA']).resolve() / ('SporeKit-' + self.profile(role)) / 'Spore'

    def game_log(self, role):
        return self.installation / 'SporebinEP1' / 'mods' / 'logs' / f'sporekit.{self.instance(role)}.log'

    def verify_payload(self):
        for directory, hashes in ((self.build, self.payload_hashes), (self.symbols, self.symbol_hashes)):
            for name, expected in hashes.items():
                if digest(directory / name) != expected:
                    raise ValueError(f'payload/symbol hash mismatch: {directory / name}')

    def payload_targets(self):
        return {name: (self.installation / 'Data' / name if name.endswith('.package') else
                      self.installation / 'SporebinEP1' / ('mods/' if name == 'coop.dll' else '') / name)
                for name in PAYLOAD}

    def verify_installation(self):
        for name, target in self.payload_targets().items():
            if digest(target) != self.payload_hashes[name]:
                raise ValueError('private installed payload hash mismatch: ' + str(target))
        symbols = self.installation / 'SporebinEP1' / 'mods' / 'sporekit' / 'symbols'
        if {p.name for p in symbols.glob('*.sym')} != set(self.symbol_hashes):
            raise ValueError('private installed symbol set differs from the pinned build')
        for name, sha in self.symbol_hashes.items():
            if digest(symbols / name) != sha:
                raise ValueError('private installed symbol hash mismatch: ' + name)

    def evidence(self):
        return dict(source_commit=self.source_commit, payload_sha256=self.payload_hashes,
                    symbols_sha256=self.symbol_hashes, viewport=self.viewport,
                    consoles=dict(host=self.host, guest=self.guest), coop_udp=self.port)


def required_fixtures(tests):
    """Actual selected bootstrap dependencies, not the union of unrelated suites.

    Stock smoke uses shipped Maxis assets. Genuine creation packages are additional
    explicit dependencies for creation scenarios, not dummy files required at boot.
    """
    required = set()
    for test in tests:
        if test['name'] == 'unit':
            continue
        fixture = test.get('fixture')
        if fixture is None:
            fixture = 'tribe' if test['name'] == 'chaos_tribe' else 'space' if test['name'] in (
                'space_play', 'space_page') else 'creature' if test['name'] in STOCK_SMOKE else None
        if fixture:
            required.add(fixture + '.tgz')
        required.update(test.get('creation_packages', ()))
    return sorted(required)


def wait_scene_ready(consoles, stage, timeout=120, receipt=None, clock=time.monotonic, sleep=time.sleep):
    """Read native gameplay readiness without clearing a pause or answering UI.

    Windows profiles can have cold native bakes after the old four-second setup
    bound. Pin each participant's actual world, not its temporary actor pointer.
    """
    if stage not in ('Cell', 'Creature', 'Tribe', 'Civ', 'Space') or not consoles:
        raise ValueError('native scene readiness requires a verified gameplay stage and participants')
    avatar_label = 'Civilization' if stage == 'Civ' else stage
    began, contexts, first_frames, last, previous, transitions = clock(), {}, {}, {}, {}, []
    stable = 0

    def record(phase, error=None):
        data = dict(phase=phase, stage=stage, elapsed=clock() - began, timeout=timeout,
                    contexts=contexts, last=last, transitions=transitions)
        if error is not None:
            data['error'] = str(error)
        if receipt is not None:
            write_json(receipt, data)
        return data

    try:
        while True:
            all_ready = True
            for role, console in consoles.items():
                mode = console.call('game.mode')
                avatar = console.call('coop.avatar')
                pauses = console.call('coop.pauses')
                ui = console.call('coop.uistate')
                frame = console.call('frame')
                facts = {name: response.get('out', '')[:8192] for name, response in (
                    ('mode', mode), ('avatar', avatar), ('pauses', pauses), ('ui', ui), ('frame', frame))}
                last[role] = facts
                parsed_mode = re.fullmatch(r'(0x[0-9A-Fa-f]+)\s+(\w+)', facts['mode'].strip())
                if not mode.get('ok') or not parsed_mode or parsed_mode[2] != stage:
                    raise RuntimeError(f'{role}: native stage/context changed while waiting for {stage}')
                parsed_avatar = re.search(r"^\w+: .*place=0x([0-9A-Fa-f]+) world '(.+)'$", facts['avatar'], re.M)
                game_name = re.search(r"^game_name '(.+)'$", facts['avatar'], re.M)
                context = None
                if (avatar.get('ok') and parsed_avatar and int(parsed_avatar[1], 16) and game_name
                        and facts['avatar'].startswith(avatar_label + ':')):
                    # The header uses the effective world identity. Raw game_name
                    # can temporarily be snapshot.spo during a native save (BUG-0258).
                    context = [parsed_mode[1], int(parsed_avatar[1], 16), parsed_avatar[2]]
                    if role in contexts and contexts[role] != context:
                        raise RuntimeError(f'{role}: native world context changed while waiting for readiness')
                    contexts[role] = context
                parsed_frame = re.search(r'\bframe=(\d+)\b', facts['frame'])
                current_frame = int(parsed_frame[1]) if frame.get('ok') and parsed_frame else None
                if current_frame is not None:
                    first_frames.setdefault(role, current_frame)
                advancing = current_frame is not None and current_frame > first_frames[role]
                unlocked = ui.get('ok') and re.search(r'\binput_lock=0\b', facts['ui']) is not None
                running = pauses.get('ok') and re.search(r'\bpaused=0\b', facts['pauses']) is not None
                ready = bool(context and advancing and unlocked and running)
                status = dict(context=context, pauses=facts['pauses'], unlocked=bool(unlocked),
                              advancing=advancing, ready=ready)
                if previous.get(role) != status:
                    transitions.append(dict(role=role, elapsed=clock() - began, **status))
                    transitions[:] = transitions[-64:]
                    previous[role] = status
                    record('waiting')
                all_ready &= ready
            stable = stable + 1 if all_ready else 0
            if stable >= 2:
                return record('ready')
            if clock() - began >= timeout:
                reasons = '; '.join(f'{role}: {facts["pauses"].strip()} / '
                                    f'{(facts["ui"].splitlines() or ["native UI readiness unavailable"])[0]}'
                                    for role, facts in last.items())
                raise RuntimeError('native gameplay readiness timed out: ' + reasons)
            sleep(.25)
    except Exception as error:
        record('failed', error)
        raise


class FixtureStore:
    MAX_BYTES = 2 * 1024 ** 3
    MAX_MEMBERS = 100000

    def __init__(self, directory, hashes):
        self.directory, self.hashes = Path(directory).resolve(), hashes

    def verified(self, name):
        if Path(name).name != name or name not in self.hashes:
            raise ValueError('fixture is absent from the verified manifest: ' + name)
        source = self.directory / name
        if digest(source) != self.hashes[name]:
            raise ValueError('fixture hash mismatch: ' + name)
        return source

    def extract(self, name, destination):
        """Validate everything before extraction; never follow archive links."""
        archive = self.verified(name + '.tgz')
        with tarfile.open(archive, 'r:gz') as source:
            members = source.getmembers()
            if len(members) > self.MAX_MEMBERS or sum(m.size for m in members) > self.MAX_BYTES:
                raise ValueError('fixture exceeds bounded extraction limits')
            names = set()
            for member in members:
                path = PurePosixPath(member.name)
                # Backslashes, colon and case-fold duplicates are unsafe on Windows.
                if (not path.parts or path.parts[0] != 'Games' or '..' in path.parts
                        or '\\' in member.name or ':' in member.name
                        or any(part.rstrip(' .') != part or re.fullmatch(
                            r'(?i)(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?', part) for part in path.parts)
                        or not (member.isdir() or member.isfile())):
                    raise ValueError('unsafe fixture member: ' + member.name)
                key = str(path).casefold()
                if key in names:
                    raise ValueError('duplicate fixture member: ' + member.name)
                names.add(key)
            if not any(m.isfile() and m.name.startswith('Games/Game0/') and m.name.endswith('.spo') for m in members):
                raise ValueError('fixture has no Game0 native planet save')
            for member in members:
                target = contained(Path(destination) / member.name, destination)
                if member.isdir():
                    target.mkdir(parents=True, exist_ok=True)
                else:
                    target.parent.mkdir(parents=True, exist_ok=True)
                    with source.extractfile(member) as data, target.open('xb') as output:
                        shutil.copyfileobj(data, output)


class WindowsLane:
    def __init__(self, config, lab_factory=None):
        self.config = config
        self.store = FixtureStore(config.fixtures, config.fixture_hashes)
        self.labs = {}
        self.owner_pid = os.getpid()
        self.lab_factory = lab_factory
        self.generations = {'host': 0, 'guest': 0}
        self.lease = None
        self.lease_finalizer = None

    def owner(self):
        if self.owner_pid != os.getpid() or os.environ.get('SPORE_WINDOWS_LANE_OWNER', str(self.owner_pid)) != str(self.owner_pid):
            raise RuntimeError('only the owning test process may mutate the private lane')
        if self.lease is None:
            # Upstream BUG-1070's whole-run lane ownership, using a native
            # kernel-released byte lock on Windows rather than Wine flock.
            # Acquire before any profile reset or deployment, not just launch.
            self.config.installation.mkdir(parents=True, exist_ok=True)
            lease = (self.config.installation / '.windows-native-lane.lock').open('a+b')
            try:
                lease.seek(0, 2)
                if lease.tell() == 0:
                    lease.write(b'\0')
                    lease.flush()
                lease.seek(0)
                if os.name == 'nt':
                    import msvcrt
                    msvcrt.locking(lease.fileno(), msvcrt.LK_NBLCK, 1)
                else:  # portable ownership contract tests only
                    import fcntl
                    fcntl.flock(lease.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                lease.seek(1)
                lease.write(json.dumps(dict(pid=self.owner_pid, lane=self.config.identity)).encode())
                lease.truncate()
                lease.flush()
            except OSError as error:
                lease.close()
                raise RuntimeError('private native installation is owned by another test run') from error
            self.lease = lease
            self.lease_finalizer = weakref.finalize(self, lease.close)

    def role(self, profile):
        return next((r for r in ('host', 'guest') if self.config.profile(r) == profile), None)

    def profile(self, role):
        self.owner()
        root = self.config.profile_dir(role)
        if root.resolve() != root:
            raise RuntimeError('private profile traverses a junction or symbolic link')
        marker = root / '.sporecoop-windows-lane.json'
        identity = dict(lane=self.config.identity, role=role, installation=str(self.config.installation))
        if marker.exists():
            if json.loads(marker.read_text(encoding='utf-8')) != identity:
                raise RuntimeError('private profile ownership marker differs')
        elif root.exists() and any(root.iterdir()):
            raise RuntimeError('refusing to adopt an unmarked nonempty profile')
        else:
            root.mkdir(parents=True, exist_ok=True)
            write_json(marker, identity)
        return root.resolve()

    def stopped(self, role):
        self.owner()
        if role in self.labs:
            raise RuntimeError('never change a loaded native save or creation package')

    def owned_planet(self, instance):
        """Read the sole Game0 planet of an already marked private test role.

        Setup helpers run as child processes: discovery must neither acquire the
        parent's lease nor create/adopt a profile. An ambiguous save is not a
        reason to choose an arbitrary world or fall back to another instance.
        """
        role = next((r for r in ('host', 'guest') if self.config.instance(r) == instance), None)
        if role is None:
            raise ValueError('load target is outside this native lane')
        root = self.config.profile_dir(role)
        marker = root / '.sporecoop-windows-lane.json'
        expected = dict(lane=self.config.identity, role=role, installation=str(self.config.installation))
        if (root.resolve() != root or not marker.is_file() or marker.resolve() != marker
                or json.loads(marker.read_text(encoding='utf-8')) != expected):
            raise RuntimeError('native load target has no matching private ownership marker')
        slot = root / 'Games' / 'Game0'
        if not slot.is_dir() or slot.resolve() != slot:
            raise RuntimeError('native load target has no owned Game0 directory')
        planets = list(slot.glob('*.spo'))
        if (len(planets) != 1 or not planets[0].is_file() or planets[0].resolve() != planets[0]
                or not re.fullmatch(r'[\w .-]+', planets[0].stem) or planets[0].stem in ('.', '..')):
            raise RuntimeError('native load requires one unambiguous owned Game0 planet')
        return 'Game0', planets[0].stem

    def remove(self, profile, relative):
        if relative not in RESETTABLE:
            raise ValueError('profile reset path is not audited: ' + relative)
        role = self.role(profile)
        if role is None:
            raise ValueError('profile does not belong to this lane')
        if relative in ('Games', 'Games-sporecoop-backup', 'Games-sporecoop-store', 'EditorSaves.package'):
            self.stopped(role)
        root = self.profile(role)
        target = contained(root / relative, root)
        if target.is_dir():
            shutil.rmtree(target)
        elif target.exists():
            target.unlink()

    def restore(self, name, profile, slot=None):
        role = self.role(profile)
        if role is None:
            raise ValueError('fixture target is not an owned private profile')
        if slot is None:
            self.stopped(role)
        elif not re.fullmatch(r'[1-9][0-9]{0,3}', str(slot)):
            raise ValueError('warm restoration requires a fresh nonzero save slot')
        root = self.profile(role)
        with tempfile.TemporaryDirectory(prefix='.fixture-', dir=root) as staging:
            self.store.extract(name, staging)
            source = Path(staging) / 'Games'
            if slot is not None:
                target = contained(root / 'Games' / ('Game' + str(slot)), root)
                if target.exists():
                    raise ValueError('warm fixture slot already exists; loaded/reused slots are immutable')
                target.parent.mkdir(parents=True, exist_ok=True)
                (source / 'Game0').replace(target)
            else:
                # Extraction and hashing succeeded before the committed Games tree changes.
                previous = root / '.fixture-previous'
                if previous.exists():
                    raise RuntimeError('unfinished fixture replacement requires explicit recovery')
                target = root / 'Games'
                if target.exists():
                    target.replace(previous)
                try:
                    source.replace(target)
                except OSError:
                    if previous.exists():
                        previous.replace(target)
                    raise
                if previous.exists():
                    shutil.rmtree(previous)

    def restore_creations(self, guest=True):
        for role in ('host', 'guest'):
            self.stopped(role)
            self.remove(self.config.profile(role), 'EditorSaves.package')
            name = 'creations-' + role + '.package'
            if (role != 'guest' or guest) and name in self.config.fixture_hashes:
                shutil.copy2(self.store.verified(name), self.profile(role) / 'EditorSaves.package')

    def seal(self, name, profile):
        """Pack a stopped private native save. Cold fact validation is still required.

        This operation deliberately does not add the archive to the trusted input
        manifest. A separate cold reopen must qualify it before a suite uses it.
        """
        role = self.role(profile)
        if role is None or not re.fullmatch(r'[a-z][a-z0-9_]{0,63}', name):
            raise ValueError('invalid private fixture source/name')
        self.stopped(role)
        contained(self.config.fixtures, ROOT / 'out')
        games = self.profile(role) / 'Games'
        if not list((games / 'Game0').glob('*.spo')):
            raise ValueError('no native Game0 planet save to seal')
        self.config.fixtures.mkdir(parents=True, exist_ok=True)
        target = self.config.fixtures / (name + '.tgz')
        if target.exists():
            raise ValueError('refusing to overwrite an existing fixture')
        if any(p.is_symlink() or p.resolve() != p.absolute() for p in games.rglob('*')):
            raise ValueError('fixture source contains a link or junction')
        temporary = target.with_suffix('.tgz.tmp')
        try:
            with tarfile.open(temporary, 'w:gz') as archive:
                archive.add(games, arcname='Games', recursive=True)
            temporary.replace(target)
        finally:
            if temporary.exists():
                temporary.unlink()
        receipt = dict(self.config.evidence(), profile=profile, archive=str(target), sha256=digest(target),
                       qualified=False, note='Requires a cold native reopen with stock/dependency and stage facts')
        write_json(target.with_suffix('.receipt.json'), receipt)
        return receipt

    def deploy(self):
        self.owner()
        if self.labs:
            raise RuntimeError('stop owned games before deployment')
        self.config.verify_payload()
        from windows_lab import inside_lab
        inside_lab(self.config.installation)
        marker = self.config.installation / 'sporecoop-test-copy.json'
        if not marker.is_file():
            raise RuntimeError('prepare an explicitly marked private installation first')
        for name, target in self.config.payload_targets().items():
            shutil.copy2(self.config.build / name, target)
            if digest(target) != self.config.payload_hashes[name]:
                raise RuntimeError('deployed payload did not verify')
        symbols = self.config.installation / 'SporebinEP1' / 'mods' / 'sporekit' / 'symbols'
        symbols.mkdir(parents=True, exist_ok=True)
        for path in symbols.glob('*.sym'):
            if path.name not in self.config.symbol_hashes:
                contained(path, symbols).unlink()
        for name in self.config.symbol_hashes:
            shutil.copy2(self.config.symbols / name, symbols / name)
        self.config.verify_installation()

    def run(self, role):
        self.owner()
        if role in self.labs:
            raise RuntimeError('private role already has an owned game')
        self.profile(role)
        self.config.verify_payload()
        self.config.verify_installation()
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
            # Only the host uses this port, so guest-first launches remain valid.
            if not self.labs:
                udp.bind(('127.0.0.1', self.config.port))
        if self.lab_factory is None:
            from windows_lab import WindowsLab
            factory = WindowsLab
        else:
            factory = self.lab_factory
        output = self.config.output / (self.config.identity + '_' + role)
        lab = factory(self.config.installation, output, instances=1, first_instance=self.config.instance(role),
                      hidden=True, screen_size=self.config.screen_size, viewport=self.config.viewport)
        try:
            lab.start()
            # These original scripts contain fixed pixel input. A configured preference
            # is insufficient: verify the actual native viewport before returning.
            from tests_viewport import viewport_of, canvas_of
            if (viewport_of(lab.consoles[0]) != self.config.viewport
                    or canvas_of(lab.consoles[0]) != (0, 0, *self.config.viewport)):
                raise RuntimeError('native renderer/UI canvas differs from the selected test recipe')
            from coop_adventure import Game
            startup = Game(self.config.instance(role), output / 'startup', timeout=120)
            try:
                startup.dismiss_registration()
            finally:
                write_json(output / 'startup-commands.json', startup.events)
                if startup.console.sock:
                    startup.console.sock.close()
        except BaseException:
            lab.stop()
            raise
        self.labs[role] = lab
        self.generations[role] += 1
        write_json(output / 'qualification.json', dict(self.config.evidence(), role=role,
                   generation=self.generations[role], profile=self.config.profile(role)))

    def stop(self):
        self.owner()
        errors = []
        for role, lab in list(self.labs.items()):
            try:
                lab.stop()
            except Exception as error:
                errors.append(str(error))
            else:
                del self.labs[role]
        if errors:
            raise RuntimeError('owned lane cleanup failed: ' + '; '.join(errors))

    def processes(self, instance=None):
        """Handle-backed inventory for upstream startup/lane-down predicates."""
        return [process.pid for lab in self.labs.values() for process, number, _ in lab.processes
                if (instance is None or number == instance) and process.poll() is None]

    def close(self):
        if self.labs:
            self.stop()  # failed cleanup retains handles and the whole-run lease
        if self.lease_finalizer is not None:
            self.lease_finalizer()
            self.lease_finalizer = None
        self.lease = None

    def command(self, *args):
        self.owner()
        if args == ('kill',):
            return self.stop()
        if args == ('deploy',):
            return self.deploy()
        if args[0] == 'fixture' and len(args) in (4, 5) and args[1] in ('restore', 'slot'):
            return self.restore(args[2], args[3], args[4] if len(args) == 5 else None)
        if args[0] == 'fixture' and len(args) == 4 and args[1] == 'save':
            return self.seal(args[2], args[3])
        if args[0] == 'run':
            instance = int(args[args.index('-i') + 1]) if '-i' in args else self.config.host
            role = next((r for r in ('host', 'guest') if self.config.instance(r) == instance), None)
            if role is None or ('-p' in args and args[args.index('-p') + 1] != self.config.profile(role)):
                raise ValueError('launch is outside the owned lane')
            return self.run(role)
        raise ValueError('unsupported Windows lane command: ' + repr(args))


_BACKEND = None


def backend():
    global _BACKEND
    if _BACKEND is None:
        _BACKEND = WindowsLane(LaneConfig.load(os.environ['SPORE_WINDOWS_LANE_CONFIG']))
    return _BACKEND
