#!/usr/bin/env python3
"""Regression for task callbacks queued by a real test DLL during mod unload."""
import argparse
import json
from pathlib import Path
import re
import shutil
import sys
import time

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
from sporectl import Console
from windows_lab import EXPECTED_EXE, ROOT, inside_lab, json_write, require_windows, sha256


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--instance', type=int, default=21)
    parser.add_argument('--dll', type=Path, default=ROOT / 'build' / 'windows_task_lifetime.dll')
    args = parser.parse_args()
    require_windows()
    output = inside_lab(args.out)
    run = json.loads((output / 'processes.json').read_text(encoding='utf-8'))
    child = next((p for p in run['processes'] if p['instance'] == args.instance), None)
    if not child or child.get('exit_code') is not None:
        raise RuntimeError('select a live recorded lab child')
    installation = inside_lab(Path(run['installation']))
    if sha256(installation / 'SporebinEP1' / 'SporeApp.exe') != EXPECTED_EXE:
        raise RuntimeError('unexpected lab executable')
    if not args.dll.is_file():
        raise RuntimeError('compile tests/windows_task_lifetime_mod.cpp first')
    shutil.copy2(args.dll, installation / 'SporebinEP1' / 'mods' / 'task_lifetime.dll')
    console = Console(args.instance, timeout=15)
    evidence = dict(instance=args.instance, pid=child['pid'])
    loaded = False
    try:
        info = console.call('info')
        if not info.get('ok') or f"pid={child['pid']}" not in info.get('out', '') or EXPECTED_EXE not in info['out']:
            raise RuntimeError('console identity does not match the lab child')
        evidence['load'] = console.call('mod.load task_lifetime')
        if not evidence['load'].get('ok'):
            raise RuntimeError('fixture load failed')
        loaded = True
        log = console.call('log 100')
        matches = re.findall(r'task-lifetime loaded cookie=(\d+) module=([0-9A-Fa-f]+)', log.get('out', ''))
        if not log.get('ok') or not matches:
            raise RuntimeError('fixture did not report its module and payload cookie')
        cookie, module = matches[-1]
        evidence.update(cookie=int(cookie), module=int(module, 16))
        evidence['unload'] = console.call('mod.unload task_lifetime')
        if not evidence['unload'].get('ok'):
            raise RuntimeError('fixture unload failed')
        loaded = False
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            log = console.call('log 100')
            if all(f'task-lifetime callback cookie={cookie} queue={kind} payload=deleted' in log.get('out', '')
                   for kind in ('main', 'outside')):
                break
            time.sleep(0.05)
        else:
            raise RuntimeError('callbacks did not execute after unload')
        evidence['callbacks'] = log
        evidence['module_after_callbacks'] = console.call(f'mem.read 0x{int(module, 16):08X} 4')
        if evidence['module_after_callbacks'].get('ok'):
            raise RuntimeError('fixture DLL stayed mapped after its callbacks completed')
        evidence['after'] = console.call('info')
        if not evidence['after'].get('ok'):
            raise RuntimeError('child did not survive the callback regression')
        json_write(output / f'task-lifetime-{args.instance}.json', evidence)
        print('native task lifetime regression passed: both callbacks ran, payloads deleted, DLL unmapped')
    finally:
        if loaded:
            console.call('mod.unload task_lifetime')
        if console.sock:
            console.sock.close()


if __name__ == '__main__':
    main()
