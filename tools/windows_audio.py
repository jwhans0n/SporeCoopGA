"""Inspect and mute only explicitly supplied native child PIDs through CoreAudio.

Interface IDs and slots follow the MinGW audiopolicy/mmdeviceapi/audioclient
headers used by this repository's Windows toolchain. No system volume changes.
"""
import ctypes as C
from ctypes import wintypes as W
import os
import uuid


def mute_processes(pids):
    if os.name != 'nt':
        raise RuntimeError('CoreAudio requires Windows')
    wanted = set(pids)
    if not wanted or any(pid <= 0 for pid in wanted):
        raise ValueError('supply child process IDs')
    ole = C.OleDLL('ole32')
    ole.CoInitializeEx.argtypes = [C.c_void_p, W.DWORD]
    ole.CoCreateInstance.argtypes = [C.c_void_p, C.c_void_p, W.DWORD, C.c_void_p, C.c_void_p]

    def guid(value):
        return C.create_string_buffer(uuid.UUID(value).bytes_le, 16)

    def invoke(obj, slot, *types):
        table = C.cast(obj, C.POINTER(C.POINTER(C.c_void_p))).contents
        return C.WINFUNCTYPE(C.c_long, C.c_void_p, *types)(table[slot])

    def check(result):
        if result < 0:
            raise RuntimeError('CoreAudio HRESULT 0x%08X' % (result & 0xffffffff))

    def release(obj):
        if obj:
            invoke(obj, 2)(obj)

    def query(obj, iid):
        result = C.c_void_p()
        check(invoke(obj, 0, C.c_void_p, C.c_void_p)(obj, guid(iid), C.byref(result)))
        return result

    check(ole.CoInitializeEx(None, 0))
    enumerator, devices = C.c_void_p(), C.c_void_p()
    evidence = []
    try:
        check(ole.CoCreateInstance(guid('BCDE0395-E52F-467C-8E3D-C4579291692E'), None, 1,
                                   guid('A95664D2-9614-4F35-A746-DE8DB63617E6'), C.byref(enumerator)))
        check(invoke(enumerator, 3, C.c_int, W.DWORD, C.c_void_p)(enumerator, 0, 1, C.byref(devices)))
        count = W.UINT()
        check(invoke(devices, 3, C.c_void_p)(devices, C.byref(count)))
        for device_index in range(count.value):
            device, manager, sessions = C.c_void_p(), C.c_void_p(), C.c_void_p()
            try:
                check(invoke(devices, 4, W.UINT, C.c_void_p)(devices, device_index, C.byref(device)))
                check(invoke(device, 3, C.c_void_p, W.DWORD, C.c_void_p, C.c_void_p)(
                    device, guid('77AA99A0-1BD6-484F-8BC7-2C654C9A9B6F'), 1, None, C.byref(manager)))
                check(invoke(manager, 5, C.c_void_p)(manager, C.byref(sessions)))
                length = C.c_int()
                check(invoke(sessions, 3, C.c_void_p)(sessions, C.byref(length)))
                for index in range(length.value):
                    control, control2, volume = C.c_void_p(), C.c_void_p(), C.c_void_p()
                    try:
                        check(invoke(sessions, 4, C.c_int, C.c_void_p)(sessions, index, C.byref(control)))
                        control2 = query(control, 'BFB7FF88-7239-4FC9-8FA2-07C950BE9C6D')
                        pid = W.DWORD()
                        check(invoke(control2, 14, C.c_void_p)(control2, C.byref(pid)))
                        if pid.value not in wanted:
                            continue
                        volume = query(control, '87CE5498-68D6-44E5-9215-6DA47EF883D8')
                        check(invoke(volume, 3, C.c_float, C.c_void_p)(volume, 0.0, None))
                        check(invoke(volume, 5, W.BOOL, C.c_void_p)(volume, True, None))
                        level, muted = C.c_float(), W.BOOL()
                        check(invoke(volume, 4, C.c_void_p)(volume, C.byref(level)))
                        check(invoke(volume, 6, C.c_void_p)(volume, C.byref(muted)))
                        if level.value != 0 or not muted.value:
                            raise RuntimeError('child audio mute did not persist')
                        evidence.append(dict(pid=pid.value, device=device_index, session=index,
                                             volume=level.value, muted=bool(muted.value)))
                    finally:
                        release(volume)
                        release(control2)
                        release(control)
            finally:
                release(sessions)
                release(manager)
                release(device)
        return evidence
    finally:
        release(devices)
        release(enumerator)
        ole.CoUninitialize()
