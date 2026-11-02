#!/usr/bin/env python3
"""Bounded, passive PropertyList/Transform reads from an owned hidden dc04 lab.

No game console, native calls, process/profile writes, audio changes or inputs.
The operator must explicitly execute against recorded private process identities.
Unknown PropertyList implementations and moving snapshots are rejected.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import time

EXE_SHA256 = '47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab'
PREFERRED_BASE = 0x400000
MODE_PTR = 0x16C7AA4
SET_PROPERTY = 0x6A2E20
TRANSFORMS = ((0x02A907B6, 'effects', 0x28), (0x03A90C5F, 'models', 0xB4))


def word(data, at=0):
    return struct.unpack_from('<I', data, at)[0]


def bounded_vector(begin, end, capacity, stride, limit):
    if (not all(type(value) is int and 0 <= value < 1 << 32 for value in (begin, end, capacity))
            or not begin <= end <= capacity or (end-begin) % stride or (capacity-begin) % stride
            or (capacity-begin)//stride > limit or (end != begin and begin < 0x10000)):
        raise ValueError('Unsupported or unbounded native vector')
    return (end-begin)//stride


def property_rows(header, rows):
    if len(header) != 0x38:
        raise ValueError('Truncated PropertyList header')
    begin, end, capacity = struct.unpack_from('<III', header, 0x18)
    count = bounded_vector(begin, end, capacity, 24, 4096)
    if len(rows) != count*24:
        raise ValueError('Truncated PropertyList entries')
    ids = [word(rows, at) for at in range(0, len(rows), 24)]
    if any(a >= b for a, b in zip(ids, ids[1:])):
        raise ValueError('PropertyList entries are not strictly sorted')
    return {ident: rows[at:at+24] for ident, at in zip(ids, range(0, len(rows), 24))}


def transform_property(row):
    if len(row) != 24:
        raise ValueError('Truncated Property entry')
    pointer, stride, count = struct.unpack_from('<III', row, 4)
    flags, kind = struct.unpack_from('<HH', row, 20)
    if kind != 0x38 or not flags & 0x80 or not flags & 0x30 or stride != 56 or count > 8192:
        raise ValueError('Unsupported Transform property representation')
    if count and not 0x10000 <= pointer <= 0xFFFFFFFF-count*56:
        raise ValueError('Invalid Transform property array pointer')
    return dict(pointer=pointer, stride=stride, count=count, flags=flags, type=kind)


class Module(C.Structure):
    _fields_ = [('dwSize', W.DWORD), ('th32ModuleID', W.DWORD), ('th32ProcessID', W.DWORD),
                ('GlblcntUsage', W.DWORD), ('ProccntUsage', W.DWORD), ('modBaseAddr', C.c_void_p),
                ('modBaseSize', W.DWORD), ('hModule', W.HMODULE), ('szModule', W.WCHAR*256),
                ('szExePath', W.WCHAR*260)]


class Reader:
    def __init__(self, pid, exe):
        self.kernel = C.WinDLL('kernel32', use_last_error=True)
        k = self.kernel
        k.OpenProcess.argtypes = (W.DWORD, W.BOOL, W.DWORD); k.OpenProcess.restype = W.HANDLE
        k.ReadProcessMemory.argtypes = (W.HANDLE, W.LPCVOID, W.LPVOID, C.c_size_t, C.POINTER(C.c_size_t))
        k.ReadProcessMemory.restype = W.BOOL
        k.QueryFullProcessImageNameW.argtypes = (W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD))
        k.QueryFullProcessImageNameW.restype = W.BOOL
        k.WaitForSingleObject.argtypes = (W.HANDLE, W.DWORD); k.WaitForSingleObject.restype = W.DWORD
        k.CreateToolhelp32Snapshot.argtypes = (W.DWORD, W.DWORD); k.CreateToolhelp32Snapshot.restype = W.HANDLE
        k.Module32FirstW.argtypes = (W.HANDLE, C.POINTER(Module)); k.Module32FirstW.restype = W.BOOL
        k.Module32NextW.argtypes = (W.HANDLE, C.POINTER(Module)); k.Module32NextW.restype = W.BOOL
        k.CloseHandle.argtypes = (W.HANDLE,); k.CloseHandle.restype = W.BOOL
        # READ_VM | QUERY_LIMITED_INFORMATION | SYNCHRONIZE. The last right is
        # required by the passive liveness wait; WAIT_FAILED is never "exited".
        self.handle = k.OpenProcess(0x00101010, False, pid)
        if not self.handle: raise C.WinError(C.get_last_error())
        self.calls = self.bytes = 0; self.started = time.monotonic(); self.call_limit = 1000
        try:
            name = C.create_unicode_buffer(32768); length = W.DWORD(len(name))
            if not k.QueryFullProcessImageNameW(self.handle, 0, name, C.byref(length)):
                raise C.WinError(C.get_last_error())
            if Path(name.value).resolve() != exe.resolve(): raise ValueError('PID image path differs from the private runner')
            snapshot = k.CreateToolhelp32Snapshot(0x18, pid)
            if snapshot == C.c_void_p(-1).value: raise C.WinError(C.get_last_error())
            try:
                entry = Module(); entry.dwSize = C.sizeof(entry); found = k.Module32FirstW(snapshot, C.byref(entry))
                self.base = None
                for unused in range(512):
                    if not found: break
                    if Path(entry.szExePath).resolve() == exe.resolve():
                        self.base = entry.modBaseAddr; self.image_size = entry.modBaseSize; break
                    found = k.Module32NextW(snapshot, C.byref(entry))
                if self.base is None: raise ValueError('Private executable module is absent')
            finally: k.CloseHandle(snapshot)
        except BaseException:
            self.close(); raise

    def close(self):
        if self.handle: self.kernel.CloseHandle(self.handle); self.handle = None

    def read(self, address, size):
        self.calls += 1; self.bytes += size
        if (not 0 < size <= 1 << 20 or not 0x10000 <= address <= 0x100000000-size
                or self.calls > self.call_limit or self.bytes > 4 << 20 or time.monotonic()-self.started > 5):
            raise ValueError('Passive native read budget exceeded')
        wait = self.kernel.WaitForSingleObject(self.handle, 0)
        if wait == 0xFFFFFFFF: raise C.WinError(C.get_last_error())
        if wait != 258: raise ValueError('Owned process exited (wait result '+str(wait)+')')
        buffer = C.create_string_buffer(size); received = C.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, buffer, size, C.byref(received)) or received.value != size:
            raise C.WinError(C.get_last_error())
        return buffer.raw

    def pointer(self, address): return word(self.read(address, 4))


def hidden_windows(pids):
    user = C.WinDLL('user32', use_last_error=True)
    user.GetWindowThreadProcessId.argtypes = (W.HWND, C.POINTER(W.DWORD))
    user.IsWindowVisible.argtypes = (W.HWND,); user.IsWindowVisible.restype = W.BOOL
    callback = C.WINFUNCTYPE(W.BOOL, W.HWND, W.LPARAM); rows = []
    @callback
    def inspect(hwnd, unused):
        pid = W.DWORD(); user.GetWindowThreadProcessId(hwnd, C.byref(pid))
        if pid.value in pids: rows.append(dict(pid=pid.value, hwnd=int(hwnd), visible=bool(user.IsWindowVisible(hwnd))))
        return True
    user.EnumWindows.argtypes = (callback, W.LPARAM); user.EnumWindows.restype = W.BOOL
    if not user.EnumWindows(inspect, 0): raise C.WinError(C.get_last_error())
    if {row['pid'] for row in rows} != pids or any(row['visible'] for row in rows):
        raise ValueError('Every recorded private process must have only hidden HWNDs')
    return rows


def sample(reader):
    singleton = reader.base+MODE_PTR-PREFERRED_BASE
    mode = reader.pointer(singleton); mode_header = reader.read(mode, 0xDC)
    if word(mode_header, 0xCC) != 1 or word(mode_header, 0xD0) != 0: raise ValueError('Native Adventure Editor is not active')
    data = word(mode_header, 0x74); resource = reader.pointer(data+0x10); resource_header = reader.read(resource, 0x18)
    tool = word(mode_header, 0x18); tool_header = reader.read(tool, 0x1CC)
    properties = word(tool_header, 0x0C); header = reader.read(properties, 0x38)
    vtable = word(header); setter = reader.pointer(vtable+0x14)
    if setter != reader.base+SET_PROPERTY-PREFERRED_BASE: raise ValueError('Unverified PropertyList vtable')
    begin, end, capacity = struct.unpack_from('<III', header, 0x18)
    count = bounded_vector(begin, end, capacity, 24, 4096)
    entries = reader.read(begin, count*24) if count else b''; by_id = property_rows(header, entries)
    result = dict(image_base=reader.base, mode=mode, data=data, resource=resource, source=list(struct.unpack_from('<III', resource_header, 8)),
                  tool=tool, property_list=properties,
                  mode_header=mode_header.hex(), tool_header=tool_header.hex(), property_header=header.hex(),
                  setter=setter, property_count=count, property_ids=list(by_id), arrays={})
    saved = []
    for ident, label, offset in TRANSFORMS:
        if ident not in by_id: raise ValueError('Expected Terraform Transform property is absent')
        info = transform_property(by_id[ident]); size = info['count']*56
        raw = reader.read(info['pointer'], size) if size else b''; saved.append((info['pointer'], raw))
        first, last, cap = struct.unpack_from('<III', tool_header, offset)
        vector_count = bounded_vector(first, last, cap, 56, 8192)
        live = reader.read(first, vector_count*56) if vector_count else b''; saved.append((first, live))
        result['arrays'][label] = dict(property_id=ident, **info, bytes=raw.hex(), live_count=vector_count,
                                       live_bytes=live.hex(), exact_equal=raw == live,
                                       counters=[struct.unpack_from('<H', raw, n+2)[0] for n in range(0, len(raw), 56)],
                                       live_counters=[struct.unpack_from('<H', live, n+2)[0] for n in range(0, len(live), 56)])
    for address, payload in saved:
        if payload and reader.read(address, len(payload)) != payload: raise ValueError('Transform payload changed during read')
    if (reader.read(properties, 0x38) != header or (entries and reader.read(begin, len(entries)) != entries)
            or reader.read(mode, 0xDC) != mode_header or reader.read(tool, 0x1CC) != tool_header
            or reader.pointer(data+0x10) != resource or reader.read(resource, 0x18) != resource_header
            or reader.pointer(singleton) != mode): raise ValueError('Native PropertyList owner changed during read')
    result.update(read_calls=reader.calls, read_bytes=reader.bytes)
    return result


def execute(args, sampler=None):
    if os.name != 'nt' or not args.execute: raise ValueError('Explicit --execute on Windows is required')
    metadata = json.loads(args.metadata.read_text(encoding='utf-8-sig'))
    installation = Path(metadata['installation']).resolve()
    if (not any(p.name == 'windows-lab' and p.parent.name == 'out' for p in installation.parents)
            or not (installation/'sporecoop-test-copy.json').is_file()): raise ValueError('Only a marked private lab installation is accepted')
    exe = installation/'SporebinEP1/SporeApp.exe'; dll = installation/'SporebinEP1/mods/coop.dll'
    if hashlib.sha256(exe.read_bytes()).hexdigest() != EXE_SHA256: raise ValueError('Expected exact supported LAA executable')
    if hashlib.sha256(dll.read_bytes()).hexdigest() != args.dll_sha256: raise ValueError('Candidate DLL hash differs')
    records = [row for row in metadata['processes'] if row['instance'] in args.instances]
    if (len(records) != len(args.instances) or len({row['pid'] for row in records}) != len(records)
            or any(row.get('exit_code') is not None or not re.fullmatch(r'winlab_[a-z0-9_]+', row.get('profile', '')) for row in records)):
        raise ValueError('Exact active private process records are required')
    pids = {row['pid'] for row in records}
    command = ('Get-CimInstance Win32_Process | Where-Object {$_.ProcessId -in @('+','.join(map(str, sorted(pids)))+
               ')} | Select-Object ProcessId,CommandLine | ConvertTo-Json -Compress')
    done = subprocess.run(['powershell', '-NoProfile', '-NonInteractive', '-Command', command],
                          capture_output=True, text=True, check=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
    processes = json.loads(done.stdout); processes = processes if isinstance(processes, list) else [processes]
    lines = {row['ProcessId']: row['CommandLine'] or '' for row in processes}
    for row in records:
        for token in (f"-sporekit-instance:{row['instance']}", '-sporekit-profile:'+row['profile'], '-sporekit-invisible'):
            if not re.search(r'(?:^|\s|")'+re.escape(token)+r'(?=\s|"|$)', lines.get(row['pid'], '')):
                raise ValueError('PID lacks its exact private invisible runner arguments')
    result = dict(read_only=True, native_calls=0, game_calls=0, mutations=0, passed=False,
                  candidate_sha256=args.dll_sha256, executable_sha256=EXE_SHA256, metadata=str(args.metadata.resolve()), rows=[])
    args.output.mkdir(parents=True, exist_ok=False)
    try:
        result['windows_before'] = hidden_windows(pids)
        for record in records:
            reader = Reader(record['pid'], exe)
            try: result['rows'].append(dict(instance=record['instance'], pid=record['pid'], profile=record['profile'], **(sampler or sample)(reader)))
            finally: reader.close()
        result['windows_after'] = hidden_windows(pids); result['passed'] = True
    finally: (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(dict(passed=True, output=str(args.output), rows=[dict(instance=row['instance'],
        **({'arrays':{k:dict(count=v['count'], live_count=v['live_count'], exact_equal=v['exact_equal']) for k,v in row['arrays'].items()}}
           if 'arrays' in row else {'owned_object_count':row.get('owned_object_count')})) for row in result['rows']])))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true')
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--instances', type=int, nargs='+', default=[95, 96])
    parser.add_argument('--dll-sha256', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if (not re.fullmatch('[0-9a-f]{64}', args.dll_sha256) or not 1 <= len(args.instances) <= 4
            or len(set(args.instances)) != len(args.instances)): parser.error('Exact candidate hash and distinct bounded instances required')
    execute(args)


if __name__ == '__main__': main()
