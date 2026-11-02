#!/usr/bin/env python3
"""Offline private complexity inputs; never opens a process or a profile.

Inputs must be explicitly hashed copies under this worktree's OUT directory.
Only a NEW output directory is written. Native parsing, placement boundaries,
Save/reopen and publication remain separate acceptance checks.
"""
import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'out'
MAX_NATIVE = 16 << 20
MAX_PACKAGE = 256 << 20
MAX_MARKERS = 4096
MARKERS, CLASSES, ACTS = 0x07102E6C, 0x072A9B58, 0x07102E6A
MARKER_ACTS = 0x076CA8D3
SERIALIZER = 0x01A80D26
CORE_KEY = (0x01897C18, 0x01897C18, 0x01897C18)  # group, instance, type


def need(condition, message):
    if not condition:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def word(data, at, endian='<'):
    need(0 <= at <= len(data)-4, 'Truncated integer')
    return struct.unpack_from(endian+'I', data, at)[0]


def attributes(data, start=0):
    need(start >= 0 and start+12 <= len(data), 'Truncated ClassSerializer header')
    ident, length, count = struct.unpack_from('>III', data, start)
    end = start+8+length
    need(ident == SERIALIZER and 0 < count <= 128 and start+12+count*8 <= end <= len(data),
         'Unsupported or malformed ClassSerializer record')
    cursor = start+12+count*8
    result = {}
    for n in range(count):
        key, size = struct.unpack_from('>II', data, start+12+n*8)
        need(key not in result and size <= end-cursor, 'Duplicate or truncated serialized attribute')
        result[key] = (cursor, size, start+16+n*8)
        cursor += size
    need(cursor == end, 'Serialized attributes leave unknown trailing data')
    return result, end


def refpack(data, expected):
    need(5 <= len(data) and data[1] == 0xFB, 'Unsupported RefPack header')
    p = 6 if data[0] & 0x80 else 5
    need(p <= len(data) and int.from_bytes(data[2:p], 'big') == expected and 0 < expected <= MAX_NATIVE,
         'Invalid RefPack decoded length')
    out = bytearray()
    while p < len(data):
        c = data[p]; p += 1
        if c < 0x80:
            need(p < len(data), 'Truncated RefPack short copy')
            x = data[p]; p += 1
            literals, count, distance = c & 3, ((c & 28) >> 2)+3, ((c & 96) << 3)+x+1
        elif c < 0xC0:
            need(p+2 <= len(data), 'Truncated RefPack medium copy')
            x, y = data[p:p+2]; p += 2
            literals, count, distance = x >> 6, (c & 63)+4, ((x & 63) << 8)+y+1
        elif c < 0xE0:
            need(p+3 <= len(data), 'Truncated RefPack long copy')
            x, y, z = data[p:p+3]; p += 3
            literals, count, distance = c & 3, ((c & 12) << 6)+z+5, ((c & 16) << 12)+(x << 8)+y+1
        else:
            literals = ((c & 31) << 2)+4 if c < 0xFC else c & 3
            need(p+literals <= len(data) and len(out)+literals <= expected, 'Invalid RefPack literals')
            out.extend(data[p:p+literals]); p += literals
            if c >= 0xFC:
                need(p == len(data) and len(out) == expected, 'Trailing or short RefPack stream')
                return bytes(out)
            continue
        need(p+literals <= len(data) and len(out)+literals+count <= expected, 'Oversized RefPack copy')
        out.extend(data[p:p+literals]); p += literals
        need(0 < distance <= len(out), 'Invalid RefPack distance')
        for unused in range(count):
            out.append(out[-distance])
    raise ValueError('RefPack stream has no terminating instruction')


@dataclass(frozen=True)
class Entry:
    key: tuple
    offset: int
    size: int
    usize: int
    compression: int
    state: int
    location: int


def package(data):
    need(96 <= len(data) <= MAX_PACKAGE and data[:4] == b'DBPF' and word(data, 4) == 3 and word(data, 8) == 0,
         'Expected bounded DBPF3.0, not DBBF or another package format')
    count, size = word(data, 0x24), word(data, 0x2C)
    primary, legacy = word(data, 0x40), word(data, 0x28)
    need(not primary or not legacy or primary == legacy, 'Ambiguous DBPF index offsets')
    index = primary or legacy
    need(0 < count <= 16384 and 96 <= index and 4 <= size <= len(data)-index and word(data, 0x3C) == 3,
         'Invalid DBPF index bound or version')
    flags = word(data, index)
    need(flags <= 7, 'Unsupported DBPF index flags')
    p = index+4
    constants = []
    for flag in (1, 2, 4):
        constants.append(word(data, p) if flags & flag else None)
        if flags & flag:
            p += 4
    entries = []
    keys = set()
    for unused in range(count):
        fields = []
        for flag, fixed in zip((1, 2, 4), constants):
            fields.append(fixed if flags & flag else word(data, p))
            if not flags & flag:
                p += 4
        need(p+20 <= index+size, 'Truncated DBPF entry')
        typ, group, high = fields
        instance, offset, stored_size, usize, compression, state = struct.unpack_from('<IIIIHH', data, p)
        key = group, instance, typ
        payload_size = stored_size & 0x7FFFFFFF
        need(high == 0 and typ and instance and key not in keys and compression in (0, 0xFFFF) and state == 1,
             'Unsupported DBPF key, compression, duplicate or state')
        need(96 <= offset <= len(data) and 0 < payload_size <= len(data)-offset and 0 < usize <= MAX_NATIVE,
             'Invalid DBPF record bounds')
        need(offset+payload_size <= index or offset >= index+size, 'DBPF record overlaps active index')
        need(compression != 0 or payload_size == usize, 'Uncompressed DBPF size differs')
        keys.add(key)
        entries.append(Entry(key, offset, payload_size, usize, compression, state, p))
        p += 20
    need(p == index+size, 'DBPF index has unknown trailing data')
    ranges = sorted((entry.offset,entry.offset+entry.size) for entry in entries)
    need(all(before[1] <= after[0] for before,after in zip(ranges,ranges[1:])), 'DBPF records overlap')
    return entries, index, size, primary, legacy


def record(data, entry):
    raw = data[entry.offset:entry.offset+entry.size]
    return refpack(raw, entry.usize) if entry.compression else raw


def replace_record(data, key, payload):
    """Append-only copy: all unrelated raw records and old bytes stay intact."""
    need(0 < len(payload) <= MAX_NATIVE, 'Replacement exceeds native record bound')
    entries, index, size, primary, legacy = package(data)
    target = next((entry for entry in entries if entry.key == key), None)
    need(target is not None, 'Private carrier does not contain the exact requested root')
    new_index = bytearray(data[index:index+size])
    at = target.location-index
    struct.pack_into('<IIIHH', new_index, at+4, len(data), len(payload) | 0x80000000, len(payload), 0, target.state)
    result = bytearray(data)
    result.extend(payload)
    result.extend(new_index)
    if primary:
        struct.pack_into('<I', result, 0x40, len(data)+len(payload))
    if legacy:
        struct.pack_into('<I', result, 0x28, len(data)+len(payload))
    need(len(result) <= MAX_PACKAGE, 'Generated private package exceeds bound')
    result = bytes(result)
    changed, unused_index, unused_size, unused_primary, unused_legacy = package(result)
    need(len(changed) == len(entries), 'Generated package changed record count')
    for old, new in zip(entries, changed):
        need(old.key == new.key, 'Generated package changed record order or identity')
        if old.key == key:
            need(record(result, new) == payload, 'Generated root does not read back exactly')
        else:
            need((old.offset,old.size,old.usize,old.compression,old.state) ==
                 (new.offset,new.size,new.usize,new.compression,new.state) and
                 result[new.offset:new.offset+new.size] == data[old.offset:old.offset+old.size],
                 'Generated package changed an unrelated raw record')
    return result


def native(data):
    need(0 < len(data) <= MAX_NATIVE and data.count(b'DBPF') == 1, 'Expected one bounded embedded native DBPF')
    base = data.index(b'DBPF')
    need(base >= 4 and word(data, base-4) == len(data)-base, 'Native DBPF envelope length differs')
    entries, unused_index, unused_size, unused_primary, unused_legacy = package(data[base:])
    need(len(entries) == 1 and entries[0].key == CORE_KEY, 'Unsupported embedded native scenario key or count')
    decoded = record(data[base:], entries[0])
    attrs, end = attributes(decoded)
    need(end == len(decoded) and all(key in attrs for key in (MARKERS, CLASSES, ACTS)), 'Incomplete native authored root')
    act_pos, act_size, unused = attrs[ACTS]
    act_count = word(decoded, act_pos, '>')
    need(1 <= act_count <= 8, 'Invalid native act count')
    cursor = act_pos+4
    for unused in range(act_count):
        unused_attrs, cursor = attributes(decoded, cursor)
    need(cursor == act_pos+act_size, 'Act vector has trailing data')
    class_pos, class_size, unused = attrs[CLASSES]
    class_count = word(decoded, class_pos, '>')
    need(0 < class_count <= 1024, 'Invalid native class count')
    classes = set(); cursor = class_pos+4
    for unused in range(class_count):
        ident = word(decoded, cursor, '>')
        need(ident <= 0x7FFFFFFF and ident not in classes, 'Duplicate or unsupported native class index')
        classes.add(ident)
        unused_attrs, cursor = attributes(decoded, cursor+4)
    need(cursor == class_pos+class_size, 'Class vector has trailing data')
    marker_pos, marker_size, unused = attrs[MARKERS]
    count = word(decoded, marker_pos, '>')
    need(0 < count <= MAX_MARKERS, 'Invalid native placement count')
    markers = []; cursor = marker_pos+4
    for index in range(count):
        fields, end = attributes(decoded, cursor)
        need(0x071147E5 in fields and fields[0x071147E5][1] == 4, 'Marker class attribute is missing or malformed')
        class_index = word(decoded, fields[0x071147E5][0], '>')
        need(class_index in classes, 'Marker references a missing native class')
        for ident, floats in ((0x071147E6,3),(0x071147E7,4),(0x071147E8,1)):
            need(ident in fields and fields[ident][1] == floats*4, 'Marker primitive pose shape is unsupported')
            values = struct.unpack_from('>'+str(floats)+'f', decoded, fields[ident][0])
            need(all(math.isfinite(value) for value in values), 'Marker primitive pose is nonfinite')
            if ident == 0x071147E8:
                need(values[0] > 0, 'Marker scale is invalid')
        need(MARKER_ACTS in fields and fields[MARKER_ACTS][1] >= 4,
             'Marker per-act data is missing or malformed')
        per_act, per_act_size, unused = fields[MARKER_ACTS]
        need(word(decoded,per_act,'>') == act_count, 'Marker act count differs from the root')
        per_act_end = per_act+4
        for unused in range(act_count):
            unused_attrs, per_act_end = attributes(decoded,per_act_end)
        need(per_act_end == per_act+per_act_size, 'Marker act vector has trailing data')
        markers.append(dict(index=index, start=cursor, end=end, class_index=class_index,
                            sha256=sha(decoded[cursor:end]), bytes=end-cursor))
        cursor = end
    need(cursor == marker_pos+marker_size, 'Marker vector has trailing data')
    return dict(base=base, decoded=decoded, attributes=attrs, markers=markers, count=count,
                classes=class_count, acts=act_count)


def splice_native(data, marker, expected_class, expected_marker_sha, count):
    source = native(data)
    need(isinstance(count,int) and source['count'] < count <= MAX_MARKERS, 'Only bounded placement appends are supported')
    need(isinstance(marker,int) and 0 <= marker < source['count'], 'Selected source marker is outside the native list')
    selected = source['markers'][marker]
    need(selected['class_index'] == expected_class and selected['sha256'] == expected_marker_sha,
         'Selected native marker identity differs from the reviewed input')
    decoded = source['decoded']; attrs = source['attributes']
    position, size, directory = attrs[MARKERS]
    addition = decoded[selected['start']:selected['end']] * (count-source['count'])
    need(len(decoded)+len(addition) <= MAX_NATIVE, 'Expanded ClassSerializer exceeds native bound')
    expanded = bytearray(decoded[:position+size]+addition+decoded[position+size:])
    struct.pack_into('>I', expanded, 4, len(expanded)-8)
    struct.pack_into('>I', expanded, directory, size+len(addition))
    struct.pack_into('>I', expanded, position, count)
    expanded = bytes(expanded)
    updated_attrs, end = attributes(expanded)
    need(end == len(expanded) and list(updated_attrs) == list(attrs), 'Generated root attributes changed identity or order')
    retained = {}
    for key, (at, length, unused) in attrs.items():
        new_at, new_length, unused = updated_attrs[key]
        if key != MARKERS:
            need(decoded[at:at+length] == expanded[new_at:new_at+new_length], 'Generated root changed an unrelated attribute')
            retained[f'{key:08X}'] = sha(decoded[at:at+length])
    base = source['base']
    inner = replace_record(data[base:], CORE_KEY, expanded)
    result = bytearray(data[:base]); struct.pack_into('<I',result,base-4,len(inner)); result.extend(inner)
    result = bytes(result); actual = native(result)
    need(actual['count'] == count and actual['classes'] == source['classes'] and actual['acts'] == source['acts'],
         'Generated native count or authored roster differs')
    for old, new in zip(source['markers'],actual['markers']):
        need(old['sha256'] == new['sha256'], 'Generated native root changed a retained placement')
    need(all(row['sha256'] == selected['sha256'] for row in actual['markers'][source['count']:]),
         'Generated native append differs from its exact source record')
    prefix = bytearray(result[:base]); struct.pack_into('<I',prefix,base-4,word(data,base-4))
    need(bytes(prefix) == data[:base], 'Generated native root changed its property/metadata prefix')
    return result, dict(source_sha256=sha(data), generated_sha256=sha(result), source_count=source['count'],
        generated_count=count, classes=source['classes'], acts=source['acts'], selected_marker=selected,
        retained_attribute_sha256=retained, native_prefix_preserved_except_embedded_length=True,
        retained_markers_byte_exact=True, appended_markers_byte_exact=True,
        dependency_references_unchanged=True, low_weight_stock_prop_verified=False,
        native_parser_verified=False, native_scene_verified=False, save_reopen_verified=False,
        publishing_qualified=False)


def pinned_file(path, expected, maximum=MAX_PACKAGE):
    path = path.resolve()
    need(OUT.resolve() in path.parents and path.is_file() and re.fullmatch('[0-9a-f]{64}',expected),
         'Input must be an explicitly hashed copied file under OUT')
    need(0 < path.stat().st_size <= maximum, 'Copied input exceeds its file-size bound')
    data = path.read_bytes(); need(sha(data) == expected, 'Input hash differs')
    return path, data


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--native-sha256',required=True)
    parser.add_argument('--inspect',action='store_true')
    parser.add_argument('--marker',type=int)
    parser.add_argument('--marker-class',type=int)
    parser.add_argument('--marker-sha256')
    parser.add_argument('--count',type=int)
    parser.add_argument('--carrier',type=Path)
    parser.add_argument('--carrier-sha256')
    parser.add_argument('--carrier-key',help='GROUP!INSTANCE.TYPE, eight hex digits each')
    parser.add_argument('--output',type=Path)
    args = parser.parse_args()
    try:
        source_path, source = pinned_file(args.native,args.native_sha256,MAX_NATIVE)
        if args.inspect:
            need(not any((args.carrier,args.output,args.count is not None)), 'Inspect does not build or write artifacts')
            result = native(source)
            print(json.dumps(dict(source_sha256=sha(source),count=result['count'],classes=result['classes'],
                                  acts=result['acts'],markers=result['markers']),indent=2));return 0
        need(args.output is not None and None not in (args.marker,args.marker_class,args.marker_sha256,args.count),
             'Build requires exact reviewed marker identity, target count and fresh OUT output')
        output = args.output.resolve()
        need(OUT.resolve() in output.parents and not output.exists(), 'Output must be a NEW directory under OUT')
        generated, report = splice_native(source,args.marker,args.marker_class,args.marker_sha256,args.count)
        carrier_path = carrier = generated_carrier = None
        if args.carrier:
            need(args.carrier_sha256 and re.fullmatch(r'[0-9a-fA-F]{8}![0-9a-fA-F]{8}\.[0-9a-fA-F]{8}',args.carrier_key or ''),
                 'Carrier requires its exact hash and private root key')
            key = tuple(int(value,16) for value in re.split('[!.]',args.carrier_key))
            need(key[2] == 0x366A930D and key[0] == 0x408A0000, 'Carrier root must be a native adventure key')
            carrier_path, carrier = pinned_file(args.carrier,args.carrier_sha256)
            entries, unused_index, unused_size, unused_primary, unused_legacy = package(carrier)
            target = next((entry for entry in entries if entry.key == key),None)
            need(target is not None and record(carrier,target) == source,
                 'Private carrier root is not the explicitly supplied owned native bytes')
            generated_carrier = replace_record(carrier,key,generated)
            report.update(carrier_source_sha256=sha(carrier),carrier_generated_sha256=sha(generated_carrier),
                          carrier_key=args.carrier_key,carrier_unrelated_raw_records_byte_exact=True)
        else:
            need(not args.carrier_sha256 and not args.carrier_key, 'Carrier arguments require a copied carrier')
        need(source_path.read_bytes() == source and (carrier_path is None or carrier_path.read_bytes() == carrier),
             'Input changed during offline preparation')
        output.mkdir(parents=True,exist_ok=False)
        (output/'scenario.native').write_bytes(generated)
        if generated_carrier is not None:
            (output/'private-carrier.package').write_bytes(generated_carrier)
        report.update(output=str(output),input_files_unchanged=True,process_access=False,profile_writes=False)
        (output/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        print(json.dumps(dict(output=str(output),count=args.count,native_parser_verified=False)));return 0
    except (ValueError,OSError,struct.error,OverflowError) as error:
        parser.exit(1,str(error)+'\n')


if __name__ == '__main__':
    raise SystemExit(main())
