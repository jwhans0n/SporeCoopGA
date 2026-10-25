#!/usr/bin/env python3
"""Read-only installed stock-music audit input builder, with no game/profile calls.

Writes an ignored compact DBPF with actual descriptors and sample-key baselines,
then lists stock act music IDs for tests/adventure_music_tests.cpp's offline mode.
The live collector still uses effective native ResourceManager provenance. This
offline package is not installable content and does not verify audible playback.
"""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'tests'))
from adventure_interaction_fixture import _decode_refpack, _serialized_attributes  # noqa: E402

EXE_SHA256 = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
AUDIO_GROUP, DESCRIPTOR, SAMPLE = 0x021407EE, 0x02B9F662, 0x01A527DB


def index(path):
    with path.open('rb') as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        if len(data) < 96 or data[:4] != b'DBPF':
            raise ValueError(f'not a DBPF: {path}')
        u32 = lambda at: struct.unpack_from('<I', data, at)[0]
        count, start, size = u32(0x24), u32(0x40) or u32(0x28), u32(0x2C)
        if start + size > len(data):
            raise ValueError('invalid DBPF index bounds')
        flags, at, fixed = u32(start), start + 4, [None] * 3
        for slot in range(3):
            if flags & (1 << slot):
                fixed[slot] = u32(at)
                at += 4
        for _ in range(count):
            typ, group = fixed[:2]
            if typ is None:
                typ, at = u32(at), at + 4
            if group is None:
                group, at = u32(at), at + 4
            instance, at = u32(at), at + 4
            offset, length, unpacked, compression, used = struct.unpack_from('<IIIHH', data, at)
            at += 16
            length &= 0x7FFFFFFF
            if offset + length > len(data) or at > start + size:
                raise ValueError('invalid DBPF record bounds')
            if (group == AUDIO_GROUP and typ in (DESCRIPTOR, SAMPLE)) or typ == 0x366A930D:
                payload = bytes(data[offset:offset + length]) if typ != SAMPLE else b''
                if typ != SAMPLE and compression == 0xFFFF:
                    payload = bytes(_decode_refpack(payload))
                if typ != SAMPLE and len(payload) != unpacked:
                    raise ValueError('unexpected decoded record length')
                yield (instance, typ, group), payload, dict(package=str(path), offset=offset,
                    size=length, unpacked=unpacked, compression=compression)


def act_music(scenario):
    base = scenario.index(b'DBPF')
    inner = scenario[base:]
    offset = struct.unpack_from('<I', inner, 0x40)[0]
    flags, typ, group, high, instance, at, size, usize, compression, state = struct.unpack_from('<8I2H', inner, offset)
    if flags != 7 or typ != 0x01897C18 or high != 0:
        raise ValueError('unreviewed embedded stock scenario layout')
    data = inner[at:at + (size & 0x7FFFFFFF)]
    if compression == 0xFFFF:
        data = _decode_refpack(data)
    attributes, _ = _serialized_attributes(data, 0)
    acts, size = attributes[0x07102E6A]
    count = struct.unpack_from('>I', data, acts)[0]
    at, result = acts + 4, []
    if count > 8:
        raise ValueError('unreviewed stock act count')
    for _ in range(count):
        fields, at = _serialized_attributes(data, at)
        position, length = fields[0x078FE86A]
        if length != 4:
            raise ValueError('invalid native actMusicId attribute')
        result.append(struct.unpack_from('>I', data, position)[0])
    return result


def write_package(path, records):
    body = bytearray(96)
    table = bytearray(struct.pack('<II', 4, 0))
    for (instance, typ, group), data in sorted(records.items()):
        table += struct.pack('<6I2H', typ, group, instance, len(body), len(data) | 0x80000000, len(data), 0, 1)
        body += data
    offset = len(body)
    body += table
    struct.pack_into('<4sII', body, 0, b'DBPF', 3, 0)
    for at, value in [(0x24, len(records)), (0x2C, len(table)), (0x3C, 3), (0x40, offset)]:
        struct.pack_into('<I', body, at, value)
    path.write_bytes(body)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'out/music-resolution')
    args = parser.parse_args()
    game, output = args.game.resolve(), args.output.resolve()
    if output == game or game in output.parents:
        raise ValueError('audit output must be outside the game installation')
    executable = game / 'SporebinEP1/SporeApp.exe'
    if hashlib.sha256(executable.read_bytes()).hexdigest() != EXE_SHA256:
        raise ValueError('requires the verified installed dc04 executable')
    records, candidates, scenarios, music = {}, {}, [], {}
    # GA EP1 records are the last source in this offline baseline. Native testing
    # independently checks actual in-process package precedence and overrides.
    paths = [p for directory in ('Data', 'bp1content', 'DataEP1')
             for p in sorted((game / directory).glob('*.package')) if p.name != 'SporeCoop.package']
    for path in paths:
        for key, data, provenance in index(path):
            instance, typ, group = key
            if typ == 0x366A930D:
                values = act_music(data)
                scenarios.append(dict(instance=f'{instance:08X}', **provenance,
                                      music=[f'{value:08X}' for value in values]))
                for act, value in enumerate(values, 1):
                    music.setdefault(f'{value:08X}', []).append(f'{instance:08X}:{act}')
            else:
                records[key] = data
                label = f'{group:08X}!{instance:08X}.{typ:08X}'
                candidates.setdefault(label, []).append(dict(**provenance,
                    decoded_sha256=hashlib.sha256(data).hexdigest() if typ == DESCRIPTOR else None))
    output.mkdir(parents=True, exist_ok=True)
    write_package(output / 'stock-audio-index.package', records)
    (output / 'music-ids.txt').write_text(' '.join(sorted(music)) + '\n', encoding='utf-8')
    report = dict(executable_sha256=EXE_SHA256, scenarios=len(scenarios), act_music_values=music,
                  scenario_records=scenarios, audio_record_candidates=candidates,
                  note='Offline stock baseline only; sample payloads omitted; no audible or live override proof.')
    (output / 'audit-input.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f'{len(scenarios)} stock scenarios, {len(music)} act music values, {len(records)} indexed audio resources')
    print(f'Wrote offline audit input to {output}')


if __name__ == '__main__':
    main()
