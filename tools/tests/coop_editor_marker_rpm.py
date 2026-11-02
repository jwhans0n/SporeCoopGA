#!/usr/bin/env python3
"""Passive exact marker-slot pose sample from an owned hidden dc04 lab.

Uses the same process/image/profile/HWND proof as the terrain reader. It never
calls native code or reads an unverified actor-lookup map. A native slot is not
silently asserted to be a logical ID: the adapter's value-owned bindings can
override marker_id's default (1<<32)|slot mapping.
"""
import argparse
from pathlib import Path
import re
import struct

from coop_editor_terrain_properties_rpm import MODE_PTR, PREFERRED_BASE, bounded_vector, execute, word


def floats(raw, offset, count):
    values = struct.unpack_from('<'+'f'*count, raw, offset)
    return dict(values=list(values), bits=[f'{value:08X}' for value in struct.unpack_from('<'+'I'*count, raw, offset)],
                bytes=raw[offset:offset+4*count].hex())


def sample_marker(reader, slot=253):
    if type(slot) is not int or not 0 <= slot < 4096:
        raise ValueError('Native marker slot must be within the verified4096 bound')
    singleton=reader.base+MODE_PTR-PREFERRED_BASE
    mode=reader.pointer(singleton);mode_header=reader.read(mode,0xE0)
    if word(mode_header,0xCC)!=1 or word(mode_header,0xD0)!=0:
        raise ValueError('Native Adventure Editor is not active')
    data=word(mode_header,0x74);data_header=reader.read(data,0x88)
    resource=word(data_header,0x10);resource_header=reader.read(resource,0x18)
    pool_header=reader.read(resource+0x2C10,0x1C)
    begin,end=struct.unpack_from('<II',pool_header)
    count=bounded_vector(begin,end,end,0x238,4096)
    if slot>=count:raise ValueError('Native marker slot is outside the present pool')
    row_address=begin+slot*0x238;row=reader.read(row_address,0x238)
    if word(row)&0x80000000:raise ValueError('Native marker slot is not live')
    marker=row_address+4;raw=row[4:]
    acts_begin,acts_end=struct.unpack_from('<II',raw,0x28)
    acts_count=bounded_vector(acts_begin,acts_end,acts_end,0x34,8)
    acts=reader.read(acts_begin,acts_count*0x34) if acts_count else b''
    paths=[];proof=[]
    for index in range(acts_count):
        first,last=struct.unpack_from('<II',acts,index*0x34+0x1C)
        points=bounded_vector(first,last,last,12,4096)
        payload=reader.read(first,points*12) if points else b''
        paths.append(dict(act=index,begin=first,end=last,points=points,bytes=payload.hex()))
        if payload:proof.append((first,payload))
    if acts:proof.append((acts_begin,acts))
    proof.extend(((row_address,row),(resource+0x2C10,pool_header),(resource,resource_header),(data,data_header),(mode,mode_header)))
    for address,payload in proof:
        if reader.read(address,len(payload))!=payload:raise ValueError('Native marker pose or owner changed during passive sample')
    if reader.pointer(singleton)!=mode:raise ValueError('Native Scenario singleton changed during passive sample')
    return dict(image_base=reader.base,mode=mode,data=data,resource=resource,
        source=list(struct.unpack_from('<III',resource_header,8)),native_slot=slot,
        default_logical_id=(1<<32)|slot,logical_binding_verified=False,
        binding_limit='Default marker_id fallback only; current adapter bindings are not read by this passive sampler',
        pool_begin=begin,pool_end=end,pool_count=count,pool_header=pool_header.hex(),
        row_address=row_address,marker=marker,live_flags=word(row),class_index=word(raw),
        position=floats(raw,4,3),rotation=floats(raw,0x10,4),scale=floats(raw,0x20,1),
        companions=dict(relative_scale=floats(raw,0x24,1),elevation=floats(raw,0x220,1),
            sound_pitch=floats(raw,0x1E0,1),sound_gain=floats(raw,0x1E4,1),
            destination_position=floats(raw,0x1E8,3),destination_rotation=floats(raw,0x1F4,4),
            destination_scale=floats(raw,0x204,1)),
        marker_flags=word(raw,0x224),act_count=acts_count,act_begin=acts_begin,act_end=acts_end,
        act_rows=acts.hex(),paths=paths,row_bytes=row.hex(),mode_header=mode_header.hex(),
        data_header=data_header.hex(),resource_header=resource_header.hex(),
        actor_lookup='Omitted: no verified passive actor-map traversal',
        native_calls=0,game_calls=0,mutations=0,read_calls=reader.calls,read_bytes=reader.bytes)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--metadata',type=Path,required=True)
    parser.add_argument('--instances',type=int,nargs='+',default=[95,96])
    parser.add_argument('--dll-sha256',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--slot',type=int,default=253)
    args=parser.parse_args()
    if (not re.fullmatch('[0-9a-f]{64}',args.dll_sha256) or not 1<=len(args.instances)<=4
            or len(set(args.instances))!=len(args.instances) or not 0<=args.slot<4096):
        parser.error('Exact candidate hash, distinct bounded instances and verified marker slot required')
    execute(args,lambda reader:sample_marker(reader,args.slot))


if __name__=='__main__':main()
