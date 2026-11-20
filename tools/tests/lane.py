"""The test lane this process runs in (docs/10-dev-pipeline.md). SPORE_LANE=N (default 1):
lane 1 = the real install in the "Games" bottle, lane N>1 = bottle SporeLabN + game folder
SPORE-lab/laneN (tools/spore.sh lane create N). Each lane has its own instances, ports and
profiles, so lanes run in parallel without seeing each other."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
SPORE = os.path.join(ROOT, 'tools', 'spore.sh')
sys.path.insert(0, os.path.join(ROOT, 'tools'))
WINDOWS = bool(os.environ.get('SPORE_WINDOWS_LANE_CONFIG'))
if WINDOWS:
    from windows_native_lane import backend
    _CONFIG = backend().config

LANE = int(os.environ.get('SPORE_LANE', '1'))
# lanes 1..10: console ports (TCP) 47800..47819, co-op ports (UDP) 47810..47828 do not collide
# (TCP and UDP ports are separate); LAN discovery finds hosts of lanes 1..5 only (the UI test)
assert 1 <= LANE <= 10, 'lanes 1..10 (copies of the GOG 2024 install since 2026-10-24)'
HOST = 2 * LANE - 1   # SporeKit instance numbers: console port 47800 + N - 1
GUEST = 2 * LANE
PORT = 47810 + HOST - 1  # the host's co-op port
if WINDOWS:
    HOST, GUEST, PORT = _CONFIG.host, _CONFIG.guest, _CONFIG.port
BOTTLE = 'Games' if LANE == 1 else f'SporeLab{LANE}'


def _user():
    """$USER, else the password database's name (the name Wine falls back to for drive_c/users), else
    'user' (a container uid without a passwd entry: imports keep working, a game test fails visibly at
    its fixture step). BUG-0252: make test ran without $USER into a KeyError."""
    if os.environ.get('USER'):
        return os.environ['USER']
    try:
        import getpass
        return getpass.getuser()
    except (KeyError, OSError, ImportError):
        return 'user'


APPDATA = os.path.expanduser(f'~/.var/app/com.usebottles.bottles/data/bottles/bottles/{BOTTLE}/drive_c/users/'
                             + _user() + '/AppData/Roaming')
HOST_PROFILE = 'test'
GUEST_PROFILE = 'inst2' if LANE == 1 else f'inst{GUEST}'
HOST_DIR = os.path.join(APPDATA, 'SporeKit-' + HOST_PROFILE, 'Spore')
GUEST_DIR = os.path.join(APPDATA, 'SporeKit-' + GUEST_PROFILE, 'Spore')
if WINDOWS:
    APPDATA = os.environ['APPDATA']
    HOST_PROFILE, GUEST_PROFILE = _CONFIG.profile('host'), _CONFIG.profile('guest')
    HOST_DIR, GUEST_DIR = str(_CONFIG.profile_dir('host')), str(_CONFIG.profile_dir('guest'))
JOIN = f'cheat coop join 127.0.0.1:{PORT} Bob'
if os.environ.get('SPORE_NET'):  # the guest goes through a bad network (tools/tests/netem.py)
    import netem
    JOIN = f'cheat coop join 127.0.0.1:{netem.from_env(PORT, LANE)} Bob'
HOST_CMD = f'cheat coop host {PORT} Alice'


def sh(*args, check=True):
    if WINDOWS:
        try:
            return backend().command(*args)
        except Exception:
            if check:
                raise
            return None
    subprocess.run([SPORE, *args], check=check, stdout=subprocess.DEVNULL)


def _run(*args):
    """Native handle ownership or upstream Wine duplicate-start protection."""
    if WINDOWS:
        return sh('run', *args)
    result = subprocess.run([SPORE, 'run', *args], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)
    if result.returncode == 3:
        print('  (' + result.stderr.strip() + ')', flush=True)
    elif result.returncode:
        raise subprocess.CalledProcessError(result.returncode, [SPORE, 'run', *args], stderr=result.stderr)


def run_host():
    _run('-i', str(HOST), '-p', HOST_PROFILE)


def run_guest():
    if LANE == 1 and not WINDOWS:
        _run('-i', '2')
    else:
        _run('-i', str(GUEST), '-p', GUEST_PROFILE)


_MAIN = None


def game_dir(n=None):
    """the game folder of lane n (default: this lane), from any checkout: lane 1 = the folder that
    holds the main checkout (SPORE/SporeCoop), lane N = SPORE-lab/laneN next to it (tools/spore.sh,
    lab.lane_game). Found from the git common dir, so a worktree (../SporeCoop-wt/<branch>) gives the
    same folder as the main checkout (BUG-0068)."""
    global _MAIN
    if WINDOWS:
        if n not in (None, LANE):
            raise ValueError('native lane cannot access a different installation')
        return str(_CONFIG.installation)
    n = LANE if n is None else n
    if _MAIN is None:
        common = subprocess.run(['git', 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                                cwd=ROOT, capture_output=True, text=True).stdout.strip()
        _MAIN = os.path.dirname(os.path.dirname(common)) if common else os.path.dirname(ROOT)
    return _MAIN if n == 1 else os.path.join(os.path.dirname(_MAIN), 'SPORE-lab', f'lane{n}')


def game_log(inst):
    """the SporeKit log of a game instance (instance 1: sporekit.log, else sporekit.<inst>.log).
    Instances 2N-1 and 2N run in lane N, the extra guests 20+N and 30+N too (BUG-0266)."""
    if WINDOWS:
        role = next((r for r in ('host', 'guest') if _CONFIG.instance(r) == inst), None)
        if role is None:
            raise ValueError('native log does not belong to this lane')
        return str(_CONFIG.game_log(role))
    n = (inst + 1) // 2 if inst <= 20 else inst - 20 if inst <= 30 else inst - 30
    return os.path.join(game_dir(n), 'SporebinEP1', 'mods', 'logs',
                        'sporekit.log' if inst == 1 else f'sporekit.{inst}.log')


def out_dir(*sub):
    """per-lane output (logs, screenshots) so parallel lanes do not overwrite each other"""
    p = os.path.join(str(_CONFIG.output), 'test-output', *sub) if WINDOWS else os.path.join(
        ROOT, 'out', *(sub if LANE == 1 else (f'lane{LANE}',) + sub))
    os.makedirs(p, exist_ok=True)
    return p


def profile_remove(directory, *relative):
    """Audited profile reset used by both native and Wine cold starts."""
    if directory not in (HOST_DIR, GUEST_DIR):
        raise ValueError('reset target is outside this lane')
    if WINDOWS:
        profile = HOST_PROFILE if directory == HOST_DIR else GUEST_PROFILE
        for name in relative:
            backend().remove(profile, name)
        return
    import shutil
    from pathlib import Path
    root = Path(directory).resolve()
    for name in relative:
        path = (root / name).resolve()
        if root not in path.parents:
            raise ValueError('reset path escapes the profile')
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()
