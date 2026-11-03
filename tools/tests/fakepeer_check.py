#!/usr/bin/env python3
"""Exercise fakepeer against the actual C++ Session over localhost UDP."""
import importlib.util
from pathlib import Path
import re
import queue
import socket
import struct
import subprocess
import sys
import threading

ROOT = Path(__file__).resolve().parents[2]
HOST = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / 'build' / 'host' / ('net_tests.exe' if sys.platform == 'win32' else 'net_tests')
PEER = ROOT / 'tools' / 'fakepeer.py'


def check_close(protocol, welcome, build=None):
    # A host closing before accepting the guest must not look like a successful join.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind(('127.0.0.1', 0))
        server.settimeout(3)
        args = [sys.executable, '-I', str(PEER),
                f'127.0.0.1:{server.getsockname()[1]}', '--seconds', '2', '--protocol', str(protocol)]
        if build is not None:
            args += ['--build', build]
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            data, addr = server.recvfrom(2048)
            header = '<IHIHHQQB'
            magic, proto, conn, seq, _, _, _, _ = struct.unpack_from(header, data)
            hello_proto, hello_build, _ = struct.unpack_from('<HIB', data, struct.calcsize(header) + 6)
            if (proto, hello_proto, hello_build) != (protocol, protocol, int(build or 'dc04aee5', 16)):
                raise AssertionError(f'unexpected HELLO: protocol={proto}/{hello_proto}, build={hello_build:08x}')
            messages = b''
            if welcome:
                payload = struct.pack('<BIB', 1, 0x12345678, 0)
                messages += struct.pack('<BBH', 2, 0, len(payload)) + payload
            messages += struct.pack('<BBH', 10, 0, 1) + b'\0'
            server.sendto(struct.pack(header, magic, proto, conn, 0, seq, 0, 0,
                                      2 if welcome else 1) + messages, addr)
            out, err = proc.communicate(timeout=3)
            if proc.returncode != (0 if welcome else 1) or 'host closed the session' not in out:
                raise AssertionError(f'host close: {proc.returncode} {out} {err}')
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate()


def check_session():
    try:
        host = subprocess.Popen([str(HOST), '--fakepeer-host'], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True)
    except OSError as exc:
        raise AssertionError(f'cannot start host {HOST}: {exc}') from exc
    try:
        lines = queue.Queue()
        threading.Thread(target=lambda: lines.put(host.stdout.readline()), daemon=True).start()
        try:
            line = lines.get(timeout=5).strip()
        except queue.Empty:
            host.kill()
            _, error = host.communicate()
            raise AssertionError(f'host did not start within 5 seconds: {error}') from None
        if not line.isdigit():
            if host.poll() is None:
                host.kill()
            _, error = host.communicate()
            raise AssertionError(f'host failed to start: {line} {error}')
        addr = f'127.0.0.1:{line}'

        wrong = subprocess.run([sys.executable, '-I', str(PEER), addr, '--build', '922af7c7',
                                '--seconds', '3'], capture_output=True, text=True, timeout=5)
        if wrong.returncode == 0 or 'rejected: different SporeApp.exe build' not in wrong.stdout:
            raise AssertionError(f'wrong build: {wrong.returncode} {wrong.stdout} {wrong.stderr}')

        peer = subprocess.run([sys.executable, '-I', str(PEER), addr, '--seconds', '5', '--say', 'fakepeer test'],
                              capture_output=True, text=True, timeout=10)
        if peer.returncode or 'welcome: my id 1' not in peer.stdout:
            raise AssertionError(f'fakepeer failed: {peer.returncode} {peer.stdout} {peer.stderr}')
        _, error = host.communicate(timeout=10)
        if host.returncode:
            raise AssertionError(f'host did not receive the expected messages: {error}')
        print('fakepeer: handshake, player state, chat, leave and build mismatch OK')
    finally:
        if host.poll() is None:
            host.kill()
            host.communicate()


def main():
    spec = importlib.util.spec_from_file_location('fakepeer', PEER)
    peer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(peer)
    protocol_header = (ROOT / 'mods' / 'coop' / 'net' / 'protocol.h').read_text()
    match = re.search(r'\bkProtoVersion\s*=\s*(\d+)', protocol_header)
    if not match or peer.PROTO != int(match.group(1)):
        actual = match.group(1) if match else 'not found'
        raise AssertionError(f'fakepeer protocol {peer.PROTO} differs from kProtoVersion {actual}')

    # Keep the socket bound so Windows does not report an ICMP port-unreachable error.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        server.bind(('127.0.0.1', 0))
        result = subprocess.run([sys.executable, '-I', str(PEER),
                                 f'127.0.0.1:{server.getsockname()[1]}', '--seconds', '0.15'],
                                capture_output=True, text=True, timeout=3)
        if result.returncode == 0 or 'no welcome received' not in result.stdout:
            raise AssertionError(f'silent host: {result.returncode} {result.stdout} {result.stderr}')

    check_close(6, False, '922af7c7')
    check_close(7, False)
    check_close(7, True)
    check_session()


if __name__ == '__main__':
    main()
