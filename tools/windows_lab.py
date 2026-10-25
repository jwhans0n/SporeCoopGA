#!/usr/bin/env python3
"""Native Windows test lane with a verified invisible rendering fixture.

Run `py -3 tools/windows_lab.py probe --hidden` before launching a hidden lane.
Inactive private desktops are also supported when the GPU permits rendering there.
`prepare` copies the installation and deploys build/*.dll into that private copy.
`run` keeps desktop/process handles open, records exact PIDs, and stops only its
own children on exit. No taskkill-by-name or SwitchDesktop calls are used.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[1]
LAB_ROOT = ROOT / 'out' / 'windows-lab'
EXPECTED_EXE = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
SOURCE_DEFAULT = Path(r'D:\GOG Galaxy\Games\SPORE')


def require_windows():
    if os.name != 'nt':
        raise RuntimeError('this runner requires native Windows Python')


def winapi():
    require_windows()
    user = C.WinDLL('user32', use_last_error=True)
    user.CreateDesktopW.argtypes = [W.LPCWSTR, W.LPCWSTR, C.c_void_p, W.DWORD, W.DWORD, C.c_void_p]
    user.CreateDesktopW.restype = W.HANDLE
    user.OpenInputDesktop.argtypes = [W.DWORD, W.BOOL, W.DWORD]
    user.OpenInputDesktop.restype = W.HANDLE
    user.GetThreadDesktop.argtypes = [W.DWORD]
    user.GetThreadDesktop.restype = W.HANDLE
    user.GetUserObjectInformationW.argtypes = [W.HANDLE, C.c_int, C.c_void_p, W.DWORD, C.POINTER(W.DWORD)]
    user.GetUserObjectInformationW.restype = W.BOOL
    user.CloseDesktop.argtypes = [W.HANDLE]
    user.CloseDesktop.restype = W.BOOL
    return user


def desktop_name(user, handle):
    buf = C.create_unicode_buffer(256)
    needed = W.DWORD()
    if not handle or not user.GetUserObjectInformationW(handle, 2, buf, C.sizeof(buf), C.byref(needed)):
        raise C.WinError(C.get_last_error())
    return buf.value


def input_desktop(user):
    handle = user.OpenInputDesktop(0, False, 1)
    if not handle:
        raise C.WinError(C.get_last_error())
    try:
        return desktop_name(user, handle)
    finally:
        user.CloseDesktop(handle)


def json_write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def inside_lab(path):
    resolved = path.resolve()
    if resolved == LAB_ROOT.resolve() or LAB_ROOT.resolve() not in resolved.parents:
        raise RuntimeError('test destination must be a child of ' + str(LAB_ROOT))
    return resolved


class DesktopProcess:
    """CreateProcessW wrapper: Python subprocess does not forward lpDesktop."""
    def __init__(self, args, desktop, cwd=None):
        self.args = list(map(str, args))
        self.returncode = None
        self.kernel = C.WinDLL('kernel32', use_last_error=True)

        class StartupInfo(C.Structure):
            _fields_ = [('cb', W.DWORD), ('reserved', W.LPWSTR), ('desktop', W.LPWSTR), ('title', W.LPWSTR),
                        ('x', W.DWORD), ('y', W.DWORD), ('width', W.DWORD), ('height', W.DWORD),
                        ('chars_x', W.DWORD), ('chars_y', W.DWORD), ('fill', W.DWORD), ('flags', W.DWORD),
                        ('show', W.WORD), ('reserved_size', W.WORD), ('reserved_ptr', C.c_void_p),
                        ('stdin', W.HANDLE), ('stdout', W.HANDLE), ('stderr', W.HANDLE)]

        class ProcessInfo(C.Structure):
            _fields_ = [('process', W.HANDLE), ('thread', W.HANDLE), ('pid', W.DWORD), ('tid', W.DWORD)]

        self.kernel.CreateProcessW.argtypes = [W.LPCWSTR, W.LPWSTR, C.c_void_p, C.c_void_p, W.BOOL, W.DWORD,
                                               C.c_void_p, W.LPCWSTR, C.POINTER(StartupInfo), C.POINTER(ProcessInfo)]
        self.kernel.CreateProcessW.restype = W.BOOL
        self.kernel.CloseHandle.argtypes = [W.HANDLE]
        self.kernel.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
        self.kernel.WaitForSingleObject.restype = W.DWORD
        self.kernel.GetExitCodeProcess.argtypes = [W.HANDLE, C.POINTER(W.DWORD)]
        self.kernel.TerminateProcess.argtypes = [W.HANDLE, W.UINT]
        startup = StartupInfo()
        startup.cb = C.sizeof(startup)
        startup.desktop = 'WinSta0\\' + desktop
        startup.flags = 1
        startup.show = 4  # SW_SHOWNOACTIVATE, only on the private desktop
        info = ProcessInfo()
        command = C.create_unicode_buffer(subprocess.list2cmdline(self.args))
        if not self.kernel.CreateProcessW(None, command, None, None, False, 0x08000000, None,
                                          str(cwd) if cwd else None, C.byref(startup), C.byref(info)):
            raise C.WinError(C.get_last_error())
        self.pid = info.pid
        self.handle = info.process
        self.kernel.CloseHandle(info.thread)

    def poll(self):
        if self.returncode is None and self.kernel.WaitForSingleObject(self.handle, 0) == 0:
            code = W.DWORD()
            if not self.kernel.GetExitCodeProcess(self.handle, C.byref(code)):
                raise C.WinError(C.get_last_error())
            self.returncode = code.value
        return self.returncode

    def wait(self, timeout=None):
        if self.kernel.WaitForSingleObject(self.handle, 0xffffffff if timeout is None else int(timeout * 1000)) == 258:
            raise subprocess.TimeoutExpired(self.args, timeout)
        return self.poll()

    def terminate(self):
        if self.poll() is None and not self.kernel.TerminateProcess(self.handle, 1):
            raise C.WinError(C.get_last_error())

    kill = terminate

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


def png_rgb(path, width, height, pixels):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    scan = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(scan)) + chunk(b'IEND', b''))


def com_method(obj, slot, result, *types):
    table = C.cast(obj, C.POINTER(C.POINTER(C.c_void_p))).contents
    return C.WINFUNCTYPE(result, C.c_void_p, *types)(table[slot])


def check_hr(hr, label):
    if hr < 0:
        raise RuntimeError(f'{label}: HRESULT 0x{hr & 0xffffffff:08X}')


def paint_hidden_windows(user, owned, paint=True):
    """An invisible HWND receives no automatic WM_PAINT scheduling on Windows."""
    user.IsWindowVisible.argtypes = [W.HWND]
    user.IsWindowVisible.restype = W.BOOL
    user.GetWindowThreadProcessId.argtypes = [W.HWND, C.POINTER(W.DWORD)]
    user.GetClientRect.argtypes = [W.HWND, C.POINTER(W.RECT)]
    user.PostMessageW.argtypes = [W.HWND, W.UINT, W.WPARAM, W.LPARAM]
    callback_type = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM)
    user.EnumWindows.argtypes = [callback_type, W.LPARAM]
    visible = []
    candidates = {}

    @callback_type
    def visit(hwnd, _):
        pid = W.DWORD()
        user.GetWindowThreadProcessId(hwnd, C.byref(pid))
        if pid.value in owned:
            if user.IsWindowVisible(hwnd):
                visible.append(int(hwnd))
            rect = W.RECT()
            if user.GetClientRect(hwnd, C.byref(rect)) and rect.right >= 64 and rect.bottom >= 64:
                candidates[int(hwnd)] = (rect.right * rect.bottom, int(hwnd))
        return True

    user.EnumWindows(visit, 0)
    if visible:
        raise RuntimeError('test process created a visible window: ' + str(visible))
    if paint:
        for _, hwnd in candidates.values():
            user.PostMessageW(hwnd, 0x000F, 0, 0)
    return [hwnd for _, hwnd in candidates.values()]


def render_probe(report_path, expected_desktop, hidden=False):
    """Worker launched with lpDesktop set before it creates any UI objects."""
    user = winapi()
    kernel = C.WinDLL('kernel32', use_last_error=True)
    kernel.GetCurrentThreadId.restype = W.DWORD
    current = desktop_name(user, user.GetThreadDesktop(kernel.GetCurrentThreadId()))
    before = input_desktop(user)
    report = dict(pid=os.getpid(), desktop=current, input_before=before,
                  mode='hidden-window' if hidden else 'private-desktop', ok=False)
    hwnd = None
    objects = []
    try:
        if current != expected_desktop or (not hidden and current == before):
            raise RuntimeError('worker is not isolated from the input desktop')
        user.CreateWindowExW.argtypes = [W.DWORD, W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_int, C.c_int,
                                        C.c_int, C.c_int, W.HWND, W.HMENU, W.HINSTANCE, C.c_void_p]
        user.CreateWindowExW.restype = W.HWND
        user.DestroyWindow.argtypes = [W.HWND]
        user.ShowWindow.argtypes = [W.HWND, C.c_int]
        hwnd = user.CreateWindowExW(0, 'STATIC', 'SporeCoop isolated D3D probe', 0x00CF0000,
                                   0, 0, 192, 160, None, None, None, None)
        if not hwnd:
            raise C.WinError(C.get_last_error())
        if not hidden:
            user.ShowWindow(hwnd, 4)
        user.IsWindowVisible.argtypes = [W.HWND]
        user.IsWindowVisible.restype = W.BOOL
        if hidden and user.IsWindowVisible(hwnd):
            raise RuntimeError('hidden rendering fixture window is visible')

        class PresentParameters(C.Structure):
            _fields_ = [('width', W.UINT), ('height', W.UINT), ('format', C.c_int), ('count', W.UINT),
                        ('multisample', C.c_int), ('quality', W.DWORD), ('swap', C.c_int), ('hwnd', W.HWND),
                        ('windowed', W.BOOL), ('depth', W.BOOL), ('depth_format', C.c_int), ('flags', W.DWORD),
                        ('refresh', W.UINT), ('interval', W.UINT)]

        class LockedRect(C.Structure):
            _fields_ = [('pitch', C.c_int), ('bits', C.c_void_p)]

        d3dlib = C.WinDLL('d3d9')
        d3dlib.Direct3DCreate9.argtypes = [W.UINT]
        d3dlib.Direct3DCreate9.restype = C.c_void_p
        d3d = d3dlib.Direct3DCreate9(32)
        if not d3d:
            raise RuntimeError('Direct3DCreate9 failed')
        objects.append(d3d)
        class DisplayMode(C.Structure):
            _fields_ = [('width', W.UINT), ('height', W.UINT), ('refresh', W.UINT), ('format', C.c_int)]
        mode = DisplayMode()
        mode_hr = com_method(d3d, 8, C.c_long, W.UINT, C.POINTER(DisplayMode))(d3d, 0, C.byref(mode))
        report['adapter'] = dict(count=com_method(d3d, 4, W.UINT)(d3d), mode_hr=mode_hr,
                                 width=mode.width, height=mode.height, format=mode.format)
        pp = PresentParameters(width=128, height=96, format=0, count=1, swap=1, hwnd=hwnd,
                               windowed=True, depth=False, interval=0x80000000)
        device = C.c_void_p()
        report['device_attempts'] = []
        for adapter in range(report['adapter']['count']):
            hr = com_method(d3d, 16, C.c_long, W.UINT, C.c_int, W.HWND, W.DWORD,
                            C.POINTER(PresentParameters), C.POINTER(C.c_void_p))(
                                d3d, adapter, 1, hwnd, 0x20, C.byref(pp), C.byref(device))
            report['device_attempts'].append(dict(adapter=adapter, hr=f'0x{hr & 0xffffffff:08X}'))
            if hr >= 0:
                report['selected_adapter'] = adapter
                break
        check_hr(hr, 'CreateDevice on private desktop')
        objects.append(device)
        back = C.c_void_p()
        check_hr(com_method(device, 18, C.c_long, W.UINT, W.UINT, C.c_int, C.POINTER(C.c_void_p))(
            device, 0, 0, 0, C.byref(back)), 'GetBackBuffer')
        objects.append(back)
        surface = C.c_void_p()
        check_hr(com_method(device, 36, C.c_long, W.UINT, W.UINT, C.c_int, C.c_int,
                            C.POINTER(C.c_void_p), C.c_void_p)(
                                device, 128, 96, 22, 2, C.byref(surface), None), 'CreateOffscreenPlainSurface')
        objects.append(surface)
        presentations = []
        for _ in range(3):
            check_hr(com_method(device, 43, C.c_long, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, C.c_float, W.DWORD)(
                device, 0, None, 1, 0xFF2D5F94, 1.0, 0), 'Clear')
            check_hr(com_method(device, 32, C.c_long, C.c_void_p, C.c_void_p)(device, back, surface), 'GetRenderTargetData')
            locked = LockedRect()
            check_hr(com_method(surface, 13, C.c_long, C.POINTER(LockedRect), C.c_void_p, W.DWORD)(
                surface, C.byref(locked), None, 0x10), 'LockRect')
            try:
                pixels = bytearray()
                samples = []
                for y in range(96):
                    row = C.string_at(locked.bits + y * locked.pitch, 128 * 4)
                    for x in range(128):
                        b, g, r = row[x * 4:x * 4 + 3]
                        pixels.extend((r, g, b))
                    samples.append(list(pixels[-3:]))
                if any(sample != [45, 95, 148] for sample in samples):
                    raise RuntimeError('private desktop framebuffer did not contain the expected rendered color')
                png_rgb(report_path.with_suffix('.png'), 128, 96, pixels)
            finally:
                check_hr(com_method(surface, 14, C.c_long)(surface), 'UnlockRect')
            hr = com_method(device, 17, C.c_long, C.c_void_p, C.c_void_p, W.HWND, C.c_void_p)(
                device, None, None, None, None)
            check_hr(hr, 'Present on private desktop')
            presentations.append(hr)
        after = input_desktop(user)
        if before != after or (not hidden and current == after):
            raise RuntimeError('input desktop changed during fixture')
        if hidden and user.IsWindowVisible(hwnd):
            raise RuntimeError('hidden rendering fixture became visible')
        report.update(ok=True, input_after=after, presentations=presentations, framebuffer='2d5f94')
    except Exception as exc:
        report['error'] = str(exc)
    finally:
        for obj in reversed(objects):
            com_method(obj, 2, W.ULONG)(obj)
        if hwnd:
            user.DestroyWindow(hwnd)
        json_write(report_path, report)
    return 0 if report['ok'] else 1


class PrivateDesktop:
    def __init__(self, output, hidden=False):
        self.output = inside_lab(output)
        self.output.mkdir(parents=True, exist_ok=True)
        self.user = winapi()
        self.original_input = input_desktop(self.user)
        self.hidden = hidden
        if hidden:
            self.name = self.original_input
            self.handle = None
            return
        self.name = 'SporeCoopLab-' + uuid.uuid4().hex
        # No DESKTOP_SWITCHDESKTOP access is requested.
        self.handle = self.user.CreateDesktopW(self.name, None, None, 0, 0x00FF, None)
        if not self.handle:
            raise C.WinError(C.get_last_error())

    def probe(self):
        path = self.output / 'desktop-probe.json'
        command = [sys.executable, str(Path(__file__).resolve()), '_probe-worker',
                   '--desktop', self.name, '--report', str(path)]
        if self.hidden:
            command.append('--hidden')
        process = DesktopProcess(command, self.name)
        try:
            process.wait(timeout=30)
            report = json.loads(path.read_text(encoding='utf-8')) if path.exists() else {}
            if process.returncode or not report.get('ok') or report.get('pid') != process.pid or report.get('desktop') != self.name:
                error = report.get('error') or ('probe worker exit ' + str(process.returncode))
                raise RuntimeError('private desktop rendering fixture failed: ' + error)
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
            process.close()
        if input_desktop(self.user) != self.original_input:
            raise RuntimeError('input desktop changed')
        return report

    def close(self):
        if self.handle:
            self.user.CloseDesktop(self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()


def prepare(source, destination, build):
    source = source.resolve()
    destination = inside_lab(destination)
    if source == destination or source in destination.parents:
        raise RuntimeError('test installation must be separate from the source installation')
    exe = source / 'SporebinEP1' / 'SporeApp.exe'
    if sha256(exe) != EXPECTED_EXE:
        raise RuntimeError('unverified Spore executable')
    for name in ('steam_api.dll', 'sporekit.dll', 'coop.dll', 'SporeCoop.package'):
        if not (build / name).is_file():
            raise RuntimeError('build artifact missing: ' + str(build / name))
    marker = destination / 'sporecoop-test-copy.json'
    if destination.exists() and not marker.exists():
        raise RuntimeError('refusing to reuse an unmarked existing installation directory')
    destination.mkdir(parents=True, exist_ok=True)
    # Real copies, including assets: no junctions/hardlinks to writable player files.
    for name in ('Data', 'DataEP1', 'bp1content', 'presets', 'SporebinEP1'):
        if (source / name).is_dir():
            shutil.copytree(source / name, destination / name, dirs_exist_ok=True)
    for name in ('config_options.ini', 'file_swap_state.ini', 'z.ini'):
        if (source / name).is_file():
            shutil.copy2(source / name, destination / name)
    bin_dir = destination / 'SporebinEP1'
    original = bin_dir / 'steam_api_orig.dll'
    # Always take the original from the source, not the already-deployed proxy.
    source_original = source / 'SporebinEP1' / 'steam_api_orig.dll'
    shutil.copy2(source_original if source_original.exists() else source / 'SporebinEP1' / 'steam_api.dll', original)
    mods = bin_dir / 'mods'
    (mods / 'logs').mkdir(parents=True, exist_ok=True)
    for name in ('steam_api.dll', 'sporekit.dll'):
        shutil.copy2(build / name, bin_dir / name)
    shutil.copy2(build / 'coop.dll', mods / 'coop.dll')
    shutil.copy2(build / 'SporeCoop.package', destination / 'Data' / 'SporeCoop.package')
    symbols = mods / 'sporekit' / 'symbols'
    symbols.mkdir(parents=True, exist_ok=True)
    for path in (ROOT / 'data' / 'symbols').glob('*.sym'):
        shutil.copy2(path, symbols / path.name)
    (mods / 'sporekit.ini').write_text('[sporekit]\nlog_level=0\nconsole_port=47800\nbackground_mode=1\n[mods]\nload=coop\n', encoding='utf-8')
    shutil.copy2(ROOT / 'data' / 'config' / 'coop.ini', mods / 'coop.ini')
    json_write(marker, dict(source=str(source), executable_sha256=EXPECTED_EXE, prepared_at=time.time()))
    return destination


def prepare_profile(name):
    if not re.fullmatch(r'winlab_[a-z0-9_]+', name):
        raise RuntimeError('invalid isolated profile name')
    root = Path(os.environ['APPDATA']) / ('SporeKit-' + name) / 'Spore'
    preferences = root / 'Preferences'
    preferences.mkdir(parents=True, exist_ok=True)
    # Missing OptionVersion makes this build replace every supplied setting with
    # defaults, including unmuting audio. Version9 is emitted by this verified build.
    want = dict(OptionVersion='9', OptionFullScreen='0', OptionTutorialsEnabled='0', OptionShowHints='0',
                OptionEdgeScroll='0', OptionScreenSize='12', AudioMuteAll='true', AudioMasterVolume='0.000000',
                AudioMusicVolume='0.000000', AudioVOXVolume='0.000000', AudioSFXVolume='0.000000')
    path = preferences / 'Preferences.prop'
    previous = path.read_text(encoding='utf-8', errors='replace').splitlines() if path.exists() else []
    written = set()
    lines = []
    for line in previous:
        key = line.split(' ', 1)[0]
        if key in want:
            line = key + ' ' + want[key]
            written.add(key)
        lines.append(line)
    lines.extend(key + ' ' + value for key, value in want.items() if key not in written)
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return root


class WindowsLab:
    """Context manager for integration tests; `consoles` uses sporectl.Console."""
    def __init__(self, installation, output=None, instances=2, first_instance=21, hidden=False):
        self.installation = inside_lab(installation)
        self.output = inside_lab(output or LAB_ROOT / ('run-' + uuid.uuid4().hex[:12]))
        self.instances = instances
        self.first_instance = first_instance
        self.hidden = hidden
        self.processes = []
        self.consoles = []
        self.desktop = None
        self.paint_thread = None
        self.paint_stop = threading.Event()
        self.paint_error = None
        if not 1 <= instances <= 4 or not 2 <= first_instance <= 100:
            raise RuntimeError('use 1..4 instances and first-instance2..100')
        marker = self.installation / 'sporecoop-test-copy.json'
        if not marker.is_file() or sha256(self.installation / 'SporebinEP1' / 'SporeApp.exe') != EXPECTED_EXE:
            raise RuntimeError('prepare a verified private test copy first')

    def record(self):
        json_write(self.output / 'processes.json', dict(installation=str(self.installation),
                   desktop=self.desktop.name if self.desktop else None,
                   processes=[dict(pid=p.pid, instance=i, profile=n, exit_code=p.poll())
                              for p, i, n in self.processes]))

    def start(self, timeout=180):
        from sporectl import Console
        self.desktop = PrivateDesktop(self.output, self.hidden)
        self.desktop.probe()  # fail closed before launching Spore
        if self.hidden:
            def pump():
                try:
                    while not self.paint_stop.is_set():
                        paint_hidden_windows(self.desktop.user, {p.pid for p, _, _ in list(self.processes) if p.poll() is None})
                        self.paint_stop.wait(1 / 30)
                except Exception as exc:
                    self.paint_error = str(exc)
            self.paint_thread = threading.Thread(target=pump, name='hidden-window-paint', daemon=True)
            self.paint_thread.start()
        try:
            for offset in range(self.instances):
                instance = self.first_instance + offset
                # Do not accidentally connect to a console belonging to another process.
                with socket.socket() as probe:
                    if probe.connect_ex(('127.0.0.1', 47800 + instance - 1)) == 0:
                        raise RuntimeError('instance console port is already occupied')
                profile = 'winlab_' + self.output.name.replace('-', '_') + '_' + str(instance)
                prepare_profile(profile)
                exe = self.installation / 'SporebinEP1' / 'SporeApp.exe'
                command = [str(exe), '-w', '-sporekit-instance:' + str(instance), '-sporekit-profile:' + profile]
                if self.hidden:
                    command.append('-sporekit-invisible')
                process = DesktopProcess(command, self.desktop.name, cwd=exe.parent)
                self.processes.append((process, instance, profile))
                self.record()
                console = Console(instance, timeout=30)
                deadline = time.monotonic() + timeout
                while time.monotonic() < deadline:
                    self.assert_invisible()
                    self.enforce_audio()
                    if process.poll() is not None:
                        raise RuntimeError(f'test instance {instance} exited with {process.returncode}')
                    try:
                        if console.call('ping').get('ok'):
                            break
                    except (OSError, ValueError):
                        if console.sock:
                            console.sock.close()
                        console = Console(instance, timeout=30)
                    time.sleep(0.5)
                else:
                    raise RuntimeError(f'test instance {instance} console did not start')
                self.consoles.append(console)
            if input_desktop(self.desktop.user) != self.desktop.original_input:
                raise RuntimeError('input desktop changed during game launch')
            self.record()
            self.assert_invisible()
            self.enforce_audio()
            self.verify_bootstrap()
            return self
        except Exception:
            self.stop()
            raise

    def stop(self):
        self.paint_stop.set()
        if self.paint_thread:
            self.paint_thread.join(timeout=2)
        for console in self.consoles:
            if console.sock:
                console.sock.close()
        self.consoles.clear()
        # subprocess handles identify our children even if the OS later reuses a PID.
        for process, _, _ in self.processes:
            if process.poll() is None:
                process.terminate()
        for process, _, _ in self.processes:
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=10)
        self.record()
        for process, _, _ in self.processes:
            process.close()
        if self.desktop:
            self.desktop.close()

    def assert_invisible(self):
        if not self.hidden:
            return
        if self.paint_error:
            raise RuntimeError(self.paint_error)
        paint_hidden_windows(self.desktop.user, {p.pid for p, _, _ in self.processes if p.poll() is None}, paint=False)

    def enforce_audio(self):
        from windows_audio import mute_processes
        pids = {p.pid for p, _, _ in self.processes if p.poll() is None}
        if pids:
            json_write(self.output / 'audio.json', mute_processes(pids))

    def verify_bootstrap(self):
        """Regression for preferred-base rebasing, including a relocated singleton operand."""
        evidence = []
        for (process, instance, _), console in zip(self.processes, self.consoles):
            info = console.call('info')
            if not info.get('ok') or EXPECTED_EXE not in info.get('out', ''):
                raise RuntimeError('test instance did not load the verified executable')
            out = info['out']
            if f'pid={process.pid}' not in out or ' coop' not in out or 'coop(unloaded)' in out:
                raise RuntimeError('test console does not belong to the expected modded child')
            match = re.search(r'base=0x([0-9a-fA-F]+)', out)
            if not match:
                raise RuntimeError('image base absent from test console')
            base = int(match[1], 16)
            address = base + 0x67DCD0 - 0x400000
            symbol = console.call('sym App::IGameModeManager::Get')
            if not symbol.get('ok') or not symbol.get('out', '').startswith(f'0x{address:08X}'):
                raise RuntimeError('preferred image base was not applied to a singleton symbol')
            memory = console.call(f'mem.read 0x{address:08X} 6')
            if not memory.get('ok'):
                raise RuntimeError('rebased singleton code is unreadable')
            raw = bytes(int(byte, 16) for byte in memory['out'].split()[1:7])
            expected = b'\xA1' + struct.pack('<I', base + 0x15FD8B0 - 0x400000) + b'\xC3'
            if raw != expected:
                raise RuntimeError('singleton machine code has an incorrect relocation')
            evidence.append(dict(instance=instance, pid=process.pid, base=base,
                                 singleton_address=address, singleton_bytes=raw.hex(), info=out))
        json_write(self.output / 'bootstrap.json', evidence)

    def __enter__(self):
        return self.start()

    def __exit__(self, *args):
        self.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='action', required=True)
    probe = subs.add_parser('probe', help='prove invisible D3D rendering without launching Spore')
    probe.add_argument('--out', type=Path, default=LAB_ROOT / 'probe')
    probe.add_argument('--hidden', action='store_true', help='test an always-hidden HWND on the input desktop')
    prep = subs.add_parser('prepare', help='copy installation and deploy the compiled mod into the copy')
    prep.add_argument('--source', type=Path, default=SOURCE_DEFAULT)
    prep.add_argument('--destination', type=Path, default=LAB_ROOT / 'installation')
    prep.add_argument('--build', type=Path, default=ROOT / 'build')
    run = subs.add_parser('run', help='launch isolated instances, report consoles, and retain for a bounded duration')
    run.add_argument('--installation', type=Path, default=LAB_ROOT / 'installation')
    run.add_argument('--out', type=Path)
    run.add_argument('--instances', type=int, default=2)
    run.add_argument('--first-instance', type=int, default=21)
    run.add_argument('--seconds', type=int, default=120)
    run.add_argument('--hidden', action='store_true', help='require invisible test hooks and hidden HWND rendering')
    run.add_argument('--keep-survivors', action='store_true', help='retain healthy children if another test child exits')
    extend = subs.add_parser('extend', help='renew a live run, bounded to one hour from its console startup')
    extend.add_argument('--out', type=Path, required=True)
    extend.add_argument('--seconds', type=int, default=600)
    regression = subs.add_parser('check-status', help='check offline/host status on recorded, disconnected lab children')
    regression.add_argument('--out', type=Path, required=True)
    pump = subs.add_parser('pump', help='attach a visibility-checked paint pump to a previously started test child')
    pump.add_argument('--pid', type=int, required=True)
    pump.add_argument('--seconds', type=int, default=600)
    worker = subs.add_parser('_probe-worker', help=argparse.SUPPRESS)
    worker.add_argument('--desktop', required=True)
    worker.add_argument('--report', type=Path, required=True)
    worker.add_argument('--hidden', action='store_true')
    args = parser.parse_args()
    require_windows()
    if args.action == '_probe-worker':
        return render_probe(args.report, args.desktop, args.hidden)
    if args.action == 'probe':
        with PrivateDesktop(args.out, args.hidden) as desktop:
            report = desktop.probe()
            print(json.dumps(report), flush=True)
    elif args.action == 'prepare':
        print(prepare(args.source, args.destination, args.build), flush=True)
    elif args.action == 'extend':
        if not 1 <= args.seconds <= 3600:
            raise RuntimeError('--seconds must be1..3600')
        output = inside_lab(args.out)
        if not (output / 'processes.json').is_file():
            raise RuntimeError('no recorded test run at this output path')
        json_write(output / 'lease.json', dict(until_unix=time.time() + args.seconds))
        print(output / 'lease.json', flush=True)
    elif args.action == 'check-status':
        from sporectl import Console
        output = inside_lab(args.out)
        recorded = json.loads((output / 'processes.json').read_text(encoding='utf-8'))
        installation = inside_lab(Path(recorded['installation']))
        if sha256(installation / 'SporebinEP1' / 'SporeApp.exe') != EXPECTED_EXE:
            raise RuntimeError('status regression requires a verified lab copy')
        evidence = []
        for child in recorded['processes']:
            console = Console(child['instance'], timeout=15)
            started_host = False
            try:
                info = console.call('info')
                if not info.get('ok') or f"pid={child['pid']}" not in info.get('out', '') or EXPECTED_EXE not in info['out']:
                    raise RuntimeError('recorded console does not belong to the expected lab child')
                before = console.call('coop.status')
                if not before.get('ok') or not before.get('out', '').startswith('offline\n'):
                    raise RuntimeError('status regression only runs on disconnected test children')
                hosted = console.call('coop.host 0')
                if not hosted.get('ok'):
                    raise RuntimeError('regression could not start its temporary host')
                started_host = True
                during = console.call('coop.status')
                if not during.get('ok') or not during.get('out', '').startswith('host port='):
                    raise RuntimeError('host status regression failed')
                left = console.call('coop.leave')
                started_host = False
                after = console.call('coop.status')
                if not left.get('ok') or not after.get('ok') or not after.get('out', '').startswith('offline\n'):
                    raise RuntimeError('status regression did not return to disconnected state')
                evidence.append(dict(instance=child['instance'], pid=child['pid'], before=before,
                                     hosted=hosted, during=during, left=left, after=after))
            finally:
                if started_host:
                    console.call('coop.leave')
                if console.sock:
                    console.sock.close()
        json_write(output / 'status-regression.json', evidence)
        print(f'status regression passed on {len(evidence)} native children', flush=True)
    elif args.action == 'run':
        if not 1 <= args.seconds <= 3600:
            raise RuntimeError('--seconds must be1..3600')
        with WindowsLab(args.installation, args.out, args.instances, args.first_instance, args.hidden) as lab:
            print(json.dumps(dict(output=str(lab.output), pids=[p.pid for p, _, _ in lab.processes],
                                  instances=[i for _, i, _ in lab.processes])), flush=True)
            for console in lab.consoles:
                print(json.dumps(console.call('info')), flush=True)
            started = time.time()
            deadline = started + args.seconds
            while time.time() < deadline:
                lease = lab.output / 'lease.json'
                if lease.is_file():
                    renewed = float(json.loads(lease.read_text(encoding='utf-8'))['until_unix'])
                    deadline = max(deadline, min(renewed, started + 3600))
                alive = [p.poll() is None for p, _, _ in lab.processes]
                if not any(alive) or (not args.keep_survivors and not all(alive)):
                    raise RuntimeError('a test instance exited')
                if not all(alive):
                    lab.record()
                lab.assert_invisible()
                lab.enforce_audio()
                time.sleep(1)
    elif args.action == 'pump':
        if not 1 <= args.seconds <= 3600:
            raise RuntimeError('--seconds must be1..3600')
        kernel = C.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        kernel.OpenProcess.restype = W.HANDLE
        kernel.QueryFullProcessImageNameW.argtypes = [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)]
        kernel.WaitForSingleObject.argtypes = [W.HANDLE, W.DWORD]
        kernel.CloseHandle.argtypes = [W.HANDLE]
        handle = kernel.OpenProcess(0x00101000, False, args.pid)
        if not handle:
            raise C.WinError(C.get_last_error())
        try:
            path = C.create_unicode_buffer(32768)
            size = W.DWORD(len(path))
            if not kernel.QueryFullProcessImageNameW(handle, 0, path, C.byref(size)):
                raise C.WinError(C.get_last_error())
            exe = inside_lab(Path(path.value))
            if exe.name.lower() != 'sporeapp.exe' or sha256(exe) != EXPECTED_EXE:
                raise RuntimeError('paint pump only accepts a verified lab game copy')
            user = winapi()
            deadline = time.monotonic() + args.seconds
            print(json.dumps(dict(pid=args.pid, painting_hidden_windows=True)), flush=True)
            while kernel.WaitForSingleObject(handle, 0) == 258 and time.monotonic() < deadline:
                paint_hidden_windows(user, {args.pid})
                time.sleep(1 / 30)
        finally:
            kernel.CloseHandle(handle)
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (OSError, RuntimeError, ValueError, subprocess.SubprocessError) as exc:
        print('windows_lab: ' + str(exc), file=sys.stderr)
        sys.exit(1)
