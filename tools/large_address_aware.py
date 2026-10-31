#!/usr/bin/env python3
"""Inspect, enable or restore the exact GOG 2024 4 GB executable variant.

No executable is distributed. Only the reviewed PE Characteristics bit changes.
An original-byte backup is durable before replacement; unknown images are refused.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile

GOG2024 = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
GOG2024_LAA = '47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab'
SUPPORTED_EXES = frozenset((GOG2024, GOG2024_LAA))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inspect(data):
    sha = digest(data)
    if sha not in SUPPORTED_EXES:
        raise ValueError('Unverified executable; only the exact GOG 2024 images are supported')
    if len(data) < 64 or data[:2] != b'MZ':
        raise ValueError('Invalid DOS header')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe + 248 > len(data) or data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Invalid PE header')
    if struct.unpack_from('<H', data, pe + 4)[0] != 0x14c or struct.unpack_from('<H', data, pe + 24)[0] != 0x10b:
        raise ValueError('Expected an x86 PE32 image')
    characteristics = struct.unpack_from('<H', data, pe + 22)[0]
    return dict(sha256=sha, large_address_aware=bool(characteristics & 0x20),
                characteristics_offset=pe + 22, size=len(data))


def patched(data):
    info = inspect(data)
    result = bytearray(data)
    result[info['characteristics_offset']] |= 0x20
    if digest(result) != GOG2024_LAA:
        raise ValueError('The patched image does not match the verified 4 GB image')
    return bytes(result)


def replace(path, data):
    """Flush a same-directory replacement before the atomic rename."""
    fd, temporary = tempfile.mkstemp(prefix=path.name + '.', suffix='.tmp', dir=path.parent)
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        shutil.copystat(path, temporary)
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def update(path, backup, restore=False):
    path, backup = path.resolve(strict=True), backup.resolve()
    if path == backup:
        raise ValueError('The backup must be separate from the executable')
    before = path.read_bytes()
    info = inspect(before)
    if restore:
        original = backup.read_bytes()
        if digest(original) != GOG2024:
            raise ValueError('The rollback backup is not the verified original executable')
        result = original
    else:
        result = patched(before)
        if info['large_address_aware']:
            return dict(before=info, after=info, changed=False, backup=str(backup))
        if backup.exists():
            if backup.read_bytes() != before:
                raise ValueError('Conflicting backup; it has been preserved')
        else:
            with backup.open('xb') as stream:
                stream.write(before)
                stream.flush()
                os.fsync(stream.fileno())
        if backup.read_bytes() != before:
            raise ValueError('Backup verification failed')
    # Refuse a concurrent change between verification and replacement.
    if path.read_bytes() != before:
        raise ValueError('The executable changed during verification')
    if result != before:
        replace(path, result)
    after = inspect(path.read_bytes())
    if after['sha256'] != digest(result):
        raise ValueError('Installed executable verification failed')
    return dict(before=info, after=after, changed=before != result, backup=str(backup))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    action = parser.add_mutually_exclusive_group()
    action.add_argument('--apply', action='store_true')
    action.add_argument('--restore', action='store_true')
    parser.add_argument('--backup', type=Path)
    args = parser.parse_args()
    try:
        if args.apply or args.restore:
            result = update(args.executable, args.backup or args.executable.with_suffix('.exe.pre-4gb.bak'), args.restore)
        else:
            result = inspect(args.executable.read_bytes())
        print(json.dumps(result, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
