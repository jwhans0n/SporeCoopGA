"""Read-only dc04 fixture layouts, restricted to a recorded isolated lab process."""
import ctypes
from ctypes import wintypes
import re
import struct


class AdventureMemory:
    def __init__(self, game, record):
        if not record.get('profile', '').startswith('winlab_') or record.get('exit_code') is not None:
            raise ValueError('an active isolated Windows lab process is required')
        self.game = game
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
        self.kernel.OpenProcess.restype = wintypes.HANDLE
        self.kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE, wintypes.LPCVOID, wintypes.LPVOID,
                                                 ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
        self.kernel.ReadProcessMemory.restype = wintypes.BOOL
        self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
        self.kernel.CloseHandle.restype = wintypes.BOOL
        self.handle = self.kernel.OpenProcess(0x10 | 0x1000, False, record['pid'])
        if not self.handle:
            raise ctypes.WinError(ctypes.get_last_error())
        symbol = game.call('sym App::cScenarioMode::ptr')
        self.singleton = int(symbol.split()[0], 16)

    def close(self):
        if self.handle:
            self.kernel.CloseHandle(self.handle)
            self.handle = None

    def read(self, address, size):
        if not 0x10000 <= address <= 0xFFFFFFFF or not 0 < size <= 16 * 1024 * 1024 or address + size > 0x100000000:
            raise ValueError('invalid 32-bit native fixture read')
        data = ctypes.create_string_buffer(size)
        count = ctypes.c_size_t()
        if not self.kernel.ReadProcessMemory(self.handle, address, data, size, ctypes.byref(count)) or count.value != size:
            raise ctypes.WinError(ctypes.get_last_error())
        return data.raw

    def pointer(self, address):
        return struct.unpack('<I', self.read(address, 4))[0]

    def goals(self):
        if 'Scenario' not in self.game.call('game.mode'):
            raise ValueError('native scenario is not active')
        mode = self.pointer(self.singleton)
        data = self.pointer(mode + 0x74)
        resource = self.pointer(data + 0x10)
        play = self.pointer(mode + 0x78)
        begin, end = struct.unpack('<II', self.read(resource + 0x2C10, 8))
        if end < begin or (end - begin) % 0x238 or end - begin > 16 * 1024 * 1024:
            raise ValueError('invalid native marker vector')
        markers = self.read(begin, end - begin) if end != begin else b''
        first, last = struct.unpack('<II', self.read(play + 0x64, 8))
        if last < first or (last - first) % 0x1AC or (last - first) // 0x1AC > 8:
            raise ValueError('invalid native goal vector')
        act, remaining, elapsed = struct.unpack('<iii', self.read(play + 0xB8, 12))
        goals = []
        for index in range((last - first) // 0x1AC):
            goal = self.read(first + index * 0x1AC, 0x34)
            kind, primary, secondary, required = struct.unpack_from('<iiii', goal, 0x24)
            targets = [struct.unpack_from('<I', markers, offset + 0x22C)[0]
                       for offset in range(0, len(markers), 0x238)
                       if struct.unpack_from('<i', markers, offset + 4)[0] == primary]
            goals.append(dict(index=index, type=kind, primary=primary, secondary=secondary,
                              required=required, complete=bool(goal[0]),
                              count=struct.unpack_from('<i', goal, 4)[0], targets=targets))
        return dict(act=act, state=self.pointer(play + 0x90), remaining=remaining, elapsed=elapsed, goals=goals)

    def captain_inventory(self):
        avatar = self.game.call('coop.avatar')
        match = re.search(r'local=([0-9A-Fa-f]+)', avatar)
        if not match:
            raise ValueError('local captain unavailable')
        address = int(match.group(1), 16)
        begin, end = struct.unpack('<II', self.read(address + 0xE40, 8))
        if end < begin or (end - begin) % 0x24 or end - begin > 32 * 0x24:
            raise ValueError('invalid native captain item slots')
        return [self.pointer(begin + offset + 0x20) for offset in range(0, end - begin, 0x24)]
