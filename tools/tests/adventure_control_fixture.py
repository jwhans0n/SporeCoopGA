"""Reviewed owner-local placement for recorded dc04 control/lighting fixtures.

Uses the same spatial virtual0x44 as game::teleport. All native writes belong to
disposable scratch memory; no authored data, settings or profiles are modified.
"""
import re
import struct

from adventure_interaction_fixture import Native
from coop_adventure import FixtureError


def local_avatar(game):
    match=re.search(r'local=(?:0x)?([0-9A-Fa-f]+)',game.call('coop.avatar'))
    if not match:
        raise FixtureError('native local captain is unavailable')
    return int(match[1],16)


def teleport(game,record,position):
    with Native(game,record) as native:
        avatar=local_avatar(game)
        rotation=native.memory.read(avatar+0xD0,16)
        pose=native.scratch(struct.pack('<3f',*position)+rotation)
        native.virtual(avatar+0xC0,0x44,pose,pose+12)


def marker_teleport(game,record,marker,dx=0,dy=0):
    with Native(game,record) as native:
        memory=native.memory
        mode=memory.pointer(memory.singleton)
        resource=memory.pointer(memory.pointer(mode+0x74)+0x10)
        first,last=struct.unpack('<II',memory.read(resource+0x2C10,8))
        if last<first or (last-first)%0x238 or not 0<=marker<(last-first)//0x238:
            raise FixtureError('native authored marker bounds are invalid')
        position=list(struct.unpack('<3f',memory.read(first+marker*0x238+8,12)))
        position[0]+=dx;position[1]+=dy
    teleport(game,record,position)
    return position
