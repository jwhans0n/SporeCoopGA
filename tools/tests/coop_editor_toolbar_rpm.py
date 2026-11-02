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


def raw_topology(reader,objects):
    """Interpret only the exact statically proved IWindow intrusive fields."""
    reader.call_limit=20000
    get_parent=reader.base+0x95F930-PREFERRED_BASE
    get_id=reader.base+0xA02C70-PREFERRED_BASE
    if reader.read(get_id,4)!=bytes.fromhex('8b417cc3'):
        raise ValueError('Native raw control-ID layout signature changed')
    if reader.read(get_parent,14)!=bytes.fromhex('8b413485c0740483c004c333c0c3'):
        raise ValueError('Native raw parent layout signature changed')
    if any(row['cast_preferred']!=0x7F3290 or row['cast_code'][:16]!='83e904e928c61600' for row in objects):
        raise ValueError('Root Cast adjustment has not been verified for this topology')
    pending=[(row['object'],None) for row in objects];nodes={};tables={};proof=[];errors=[];edges=0
    while pending:
        window,parent=pending.pop()
        if window in nodes:
            if parent is not None:errors.append('A window appears under multiple observed parents or a cycle: '+hex(window))
            continue
        if len(nodes)>=4096:errors.append('The native topology exceeds4096 windows');break
        header=reader.read(window,0x80);vtable=word(header)
        if not reader.base<=vtable<=reader.base+reader.image_size-0x80:
            errors.append('A window vtable is outside the verified module: '+hex(window));break
        if vtable not in tables:tables[vtable]=reader.read(vtable,0x80);proof.append((vtable,tables[vtable]))
        table=tables[vtable]
        if word(table,0x10)!=get_parent or word(table,0x1C)!=get_id:
            errors.append('A window overrides an unverified raw field getter: '+hex(window));break
        raw_parent=word(header,0x34);actual_parent=raw_parent+4 if raw_parent else 0
        if parent is not None and actual_parent!=parent:
            errors.append('A child parent field disagrees with the observed list: '+hex(window))
        anchor=window+0x38;first=word(header,0x38);last=word(header,0x3C);cursor=first
        children=[];seen=set()
        while cursor!=anchor:
            if not 0x10004<=cursor<0x100000000 or cursor in seen or len(children)>=4096 or edges>=8192:
                errors.append('An intrusive child list is null, cyclic or unbounded at '+hex(window));break
            seen.add(cursor);child=cursor-4;links=reader.read(cursor,8);next_node,previous_node=struct.unpack('<II',links)
            proof.append((cursor,links))
            # Only next and node-minus4 are interpreted by the verified sui
            # traversal. The second link/anchor word is retained as raw bytes.
            children.append(child);edges+=1;cursor=next_node
        nodes[window]=dict(window=window,parent=actual_parent,id=word(header,0x7C),vtable=vtable,
            header=header.hex(),anchor=anchor,first=first,last=last,children=children,terminated=cursor==anchor)
        proof.append((window,header))
        for child in children:pending.append((child,window))
        if len(pending)>8192:errors.append('The native topology pending list exceeds its bound');break
    for address,payload in proof:
        if reader.read(address,len(payload))!=payload:raise ValueError('Native child topology changed during passive read')
    return dict(complete=not errors and not pending,node_count=len(nodes),edge_count=edges,errors=errors,nodes=list(nodes.values()),
                undo=[window for window,row in nodes.items() if row['id']==0xB006EF6E],
                redo=[window for window,row in nodes.items() if row['id']==0xF006EFA5])


def sample_toolbar(reader,root_headers=False,topology=False):
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
    objects=[];object_proofs=[]
    if root_headers or topology:
        unique=list(dict.fromkeys(pointer for pointer in pointers if pointer))
        if len(unique)>32:raise ValueError('Root-header inventory is limited to32 unique owned objects')
        image_end=reader.base+reader.image_size
        for pointer in unique:
            object_header=reader.read(pointer,0x80);vtable=word(object_header)
            if not reader.base<=vtable<=image_end-0x80:raise ValueError('Owned object vtable is outside the verified native executable')
            table=reader.read(vtable,0x80);cast=word(table,0x0C)
            if not reader.base<=cast<=image_end-0x80:raise ValueError('Owned object Cast code is outside the verified native executable')
            code=reader.read(cast,0x80)
            objects.append(dict(object=pointer,header=object_header.hex(),vtable=vtable,vtable_bytes=table.hex(),
                cast=cast,cast_preferred=cast-reader.base+PREFERRED_BASE,cast_code=code.hex()))
            object_proofs.extend(((pointer,object_header),(vtable,table),(cast,code)))
        for address,payload in object_proofs:
            if reader.read(address,len(payload))!=payload:raise ValueError('Owned object/vtable/Cast code changed during inventory')
    tree=raw_topology(reader,objects) if topology else None
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
                owned_objects=objects,root_headers_requested=root_headers or topology,topology=tree,
                enter_pending=bool(mode_header[0xDC]),dirty_flags=list(data_header[0x80:0x86]),
                read_calls=reader.calls,read_bytes=reader.bytes,native_calls=0)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--metadata',type=Path,required=True)
    parser.add_argument('--instances',type=int,nargs='+',default=[95,96])
    parser.add_argument('--dll-sha256',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--root-headers',action='store_true',help='Read at most32 object/vtable/Cast headers inside the verified executable; never calls them')
    parser.add_argument('--topology',action='store_true',help='Read bounded raw intrusive children only after exact root/interface/getter signature proof; never calls native code')
    args=parser.parse_args()
    if (not re.fullmatch('[0-9a-f]{64}',args.dll_sha256) or not 1<=len(args.instances)<=4
            or len(set(args.instances))!=len(args.instances)):
        parser.error('Exact candidate hash and distinct bounded instances required')
    execute(args,lambda reader:sample_toolbar(reader,args.root_headers,args.topology))


if __name__=='__main__':main()
