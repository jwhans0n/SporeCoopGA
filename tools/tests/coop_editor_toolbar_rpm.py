#!/usr/bin/env python3
"""Passive bounded inventory of the current native UILayout's owned objects.

No Cast, native function calls, children traversal, console, input or mutation.
Identity and raw vector bytes are double-read through exact private-process
ownership checks shared with the passive Terraform PropertyList reader.
"""
import argparse
from collections import Counter
from pathlib import Path
import re
import struct

from coop_editor_terrain_properties_rpm import MODE_PTR, PREFERRED_BASE, bounded_vector, execute, word


def sample_toolbar(reader):
    singleton=reader.base+MODE_PTR-PREFERRED_BASE
    mode=reader.pointer(singleton);mode_header=reader.read(mode,0xE0)
    if word(mode_header,0xCC)!=1 or word(mode_header,0xD0)!=0:
        raise ValueError('Native Adventure Editor is not active')
    display=word(mode_header,0x14);display_header=reader.read(display,0x2C)
    layout=word(display_header,0x24);layout_header=reader.read(layout,0x18)
    owner=word(layout_header,0x14);owner_header=reader.read(owner,0x6C)
    data=word(mode_header,0x74);data_header=reader.read(data,0x88)
    resource=word(data_header,0x10);resource_header=reader.read(resource,0x18)
    begin,end=struct.unpack_from('<II',owner_header,0x64)
    # The verified owning list supplies begin/end. It is not assumed to expose
    # a capacity word at +6C; only the exact present span is interpreted.
    count=bounded_vector(begin,end,end,4,4096)
    raw=reader.read(begin,count*4) if count else b''
    pointers=list(struct.unpack('<'+'I'*count,raw)) if count else []
    if any(pointer and pointer<0x10000 for pointer in pointers):
        raise ValueError('Owned object vector contains an invalid non-null pointer')
    if ((raw and reader.read(begin,len(raw))!=raw) or reader.read(owner,0x6C)!=owner_header
            or reader.read(layout,0x18)!=layout_header or reader.read(display,0x2C)!=display_header
            or reader.read(data,0x88)!=data_header or reader.read(resource,0x18)!=resource_header
            or reader.read(mode,0xE0)!=mode_header or reader.pointer(singleton)!=mode):
        raise ValueError('Native toolbar identity or owned list changed during inventory')
    counts=Counter(pointers)
    return dict(image_base=reader.base,mode=mode,display=display,layout=layout,owner=owner,data=data,resource=resource,
                source=list(struct.unpack_from('<III',resource_header,8)),
                mode_header=mode_header.hex(),display_header=display_header.hex(),layout_header=layout_header.hex(),
                owner_header=owner_header.hex(),data_header=data_header.hex(),resource_header=resource_header.hex(),
                owned_begin=begin,owned_end=end,owned_object_count=count,owned_pointers=pointers,owned_bytes=raw.hex(),
                unique_nonnull_objects=len(counts)-int(0 in counts),null_objects=counts.get(0,0),
                duplicate_objects={str(pointer):total for pointer,total in counts.items() if total>1},
                enter_pending=bool(mode_header[0xDC]),dirty_flags=list(data_header[0x80:0x86]),
                read_calls=reader.calls,read_bytes=reader.bytes,native_calls=0)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--metadata',type=Path,required=True)
    parser.add_argument('--instances',type=int,nargs='+',default=[95,96])
    parser.add_argument('--dll-sha256',required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if (not re.fullmatch('[0-9a-f]{64}',args.dll_sha256) or not 1<=len(args.instances)<=4
            or len(set(args.instances))!=len(args.instances)):
        parser.error('Exact candidate hash and distinct bounded instances required')
    execute(args,sample_toolbar)


if __name__=='__main__':main()
