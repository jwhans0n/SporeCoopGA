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


def actor_headers(reader, data, raw, creature_spatial=False):
    # Exact F3D620 -> 645E40 lookup, not a generic guessed container walk.
    lookup=reader.base+0x645E40-PREFERRED_BASE
    expected=bytes.fromhex('8b44240883ec08568b3033d2578b79088bc6f7f78b49048b04918d149185c0740b3b30742e8b400885c075f5')
    if reader.read(lookup,len(expected))!=expected:raise ValueError('Native marker actor lookup signature changed')
    reader.call_limit=10000
    map_address=data+0xB0;header=reader.read(map_address,0x10)
    buckets,count=struct.unpack_from('<II',header,4)
    if not 0<count<=65536 or not 0x10000<=buckets<=0xFFFFFFFF-(count+1)*4:
        raise ValueError('Native marker actor hash buckets are invalid or unbounded')
    key=word(raw,0x228);bucket_address=buckets+(key%count)*4;sentinel_address=buckets+count*4
    bucket_raw=reader.read(bucket_address,4);sentinel_raw=reader.read(sentinel_address,4)
    node=word(bucket_raw);sentinel=word(sentinel_raw);seen=set();nodes=[]
    proof=[(lookup,expected),(map_address,header),(bucket_address,bucket_raw),(sentinel_address,sentinel_raw)]
    found=0
    while node:
        if node==sentinel:break
        if node in seen or len(nodes)>=4096:raise ValueError('Native marker actor hash chain is cyclic or unbounded')
        seen.add(node);value=reader.read(node,12);proof.append((node,value))
        native_key,actor,next_node=struct.unpack('<III',value)
        nodes.append(dict(node=node,key=native_key,actor=actor,next=next_node,bytes=value.hex()))
        if native_key==key:found=actor;break
        node=next_node
    result=dict(map=map_address,map_header=header.hex(),buckets=buckets,bucket_count=count,
        key=key,bucket=key%count,bucket_address=bucket_address,sentinel=sentinel,nodes=nodes,actor=found)
    if found:
        object_header=reader.read(found,0x80);table_address=word(object_header)
        image_end=reader.base+reader.image_size
        if not reader.base<=table_address<=image_end-0x80:raise ValueError('Native actor vtable is outside the verified executable')
        table=reader.read(table_address,0x80);functions={}
        proof.extend(((found,object_header),(table_address,table)))
        for name,offset in [('cast',0x0C),('type',0x20)]:
            address=word(table,offset)
            if not reader.base<=address<=image_end-0x80:raise ValueError('Native actor getter is outside the verified executable')
            code=reader.read(address,0x80);proof.append((address,code))
            functions[name]=dict(address=address,preferred=address-reader.base+PREFERRED_BASE,code=code.hex())
        result.update(header=object_header.hex(),vtable=table_address,
            vtable_preferred=table_address-reader.base+PREFERRED_BASE,vtable_bytes=table.hex(),functions=functions)
        if creature_spatial:
            if (result['vtable_preferred']!=0x146A080 or functions['type']['preferred']!=0xC03270
                    or not functions['type']['code'].startswith('b85eb48e01c3')):
                raise ValueError('Spatial sampling requires the exact verified creature implementation')
            spatial=found+0xC0;spatial_header=reader.read(spatial,0x80);spatial_vtable=word(spatial_header)
            if spatial_vtable!=reader.base+0x1469EC0-PREFERRED_BASE:
                raise ValueError('Creature spatial vtable differs from the verified constructor')
            spatial_table=reader.read(spatial_vtable,0x80);getters={}
            for name,offset,address,signature in [('position',0x2C,0x6E6560,'8d4104c3'),
                    ('orientation',0x30,0x97E7C0,'8d4110c3')]:
                actual=word(spatial_table,offset);expected_address=reader.base+address-PREFERRED_BASE
                code=reader.read(actual,4)
                if actual!=expected_address or code!=bytes.fromhex(signature):
                    raise ValueError('Creature spatial getter layout differs from verified code')
                proof.append((actual,code));getters[name]=dict(address=actual,preferred=address,code=code.hex())
            if word(spatial_table,0x3C)!=reader.base+0xC0DF10-PREFERRED_BASE:
                raise ValueError('Creature spatial orientation setter differs from the verified implementation')
            proof.extend(((spatial,spatial_header),(spatial_vtable,spatial_table)))
            result['spatial']=dict(address=spatial,vtable=spatial_vtable,header=spatial_header.hex(),
                vtable_bytes=spatial_table.hex(),getters=getters,position=floats(spatial_header,4,3),
                rotation=floats(spatial_header,0x10,4),uninterpreted_word20=f'{word(spatial_header,0x20):08X}',
                orientation_setter_preferred=0xC0DF10)
    elif creature_spatial:
        raise ValueError('Creature spatial sampling requires a current actor binding')
    for address,payload in proof:
        if reader.read(address,len(payload))!=payload:raise ValueError('Native actor map or code identity changed during passive sample')
    return result


def sample_marker(reader, slot=253, include_actor=False, creature_spatial=False):
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
    actor=actor_headers(reader,data,raw,creature_spatial) if include_actor or creature_spatial else None
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
        actor_lookup=actor if include_actor or creature_spatial else 'Omitted unless --actor-headers explicitly requested',
        native_calls=0,game_calls=0,mutations=0,read_calls=reader.calls,read_bytes=reader.bytes)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--metadata',type=Path,required=True)
    parser.add_argument('--instances',type=int,nargs='+',default=[95,96])
    parser.add_argument('--dll-sha256',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--slot',type=int,default=253)
    parser.add_argument('--actor-headers',action='store_true',help='Use exact verified uint32 marker actor lookup; read raw object/getter bytes without executing Cast')
    parser.add_argument('--creature-spatial',action='store_true',help='Read exact verified creature+C0 spatial pose and getter bytes, without calling getters')
    args=parser.parse_args()
    if (not re.fullmatch('[0-9a-f]{64}',args.dll_sha256) or not 1<=len(args.instances)<=4
            or len(set(args.instances))!=len(args.instances) or not 0<=args.slot<4096):
        parser.error('Exact candidate hash, distinct bounded instances and verified marker slot required')
    execute(args,lambda reader:sample_marker(reader,args.slot,args.actor_headers,args.creature_spatial))


if __name__=='__main__':main()
