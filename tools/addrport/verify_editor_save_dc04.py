#!/usr/bin/env python3
"""Check exact GOG native editor Save As, completion and preview ABI receipts."""
import argparse
import json
from pathlib import Path
import re
import struct
import sys
from pe import PE

SHA='dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
ENTRIES=[
    ('AssignMetadataText',0x423690,0x423733,8,'558bec83ec08894df8'),
    ('CreationGroup',0x15B03A8,None,None,'00008a40'),
    ('CaptureTerrain',0xF27C00,0xF27C7B,0,'83ec18a1a47a6c01568d7118'),
    ('ContinueCommand',0xED5820,0xED5875,0,'568bf18b46584883f806'),
    ('EditorCommand',0xED75E0,0xED78BB,4,'83ec145355568b74242433db8be9'),
    ('DisplayVtable',0x1489DFC,None,None,'2052ed00'),
    ('CanSave',0xF41110,0xF4118F,4,'a1a47a6c0153568bf1bb01000000'),
    ('BeginSaveCopy',0xED7390,0xED73DF,4,'a1a47a6c01568bf18b4874'),
    ('SaveResourcePipeline',0xF44E80,0xF4547B,0,'81ec7c0200005355565733db8bf9'),
    ('SaveInputHold',0xED4B80,0xED4CCA,4,'57e80a33a8ff8b10'),
    ('NativePreview',0xEF12D0,0xEF1308,4,'568bf18b86cc00000083f802'),
    ('NativeReturn',0xEF1280,0xEF12BA,0,'568bf18b86cc00000083f801'),
    ('PreviewCleanup',0xF18F30,0xF18F7C,0,'8b819000000083c0fb83f801'),
    ('ReturnDisplayMode',0xED47A0,0xED47A7,4,'8b442404894120'),
    ('SelectDisplayMode',0xED6620,0xED6908,4,'5356578bf18b4e24'),
    ('PreviewCaptain',0xFF3DB0,0xFF3DB6,0,'8b818c000000'),
    ('ValidatePlacement',0xF412C0,0xF412ED,16,'568b74240857568bf9'),
]
CALLS=[
    (0xF3CC39,0x423690,0xE8),
    (0xF45201,0xF27C00,0xE8),(0xF27C10,0xF33A50,0xE8),
    (0xED7FC0,0xED75E0,0xE8),(0xED7FE9,0xED75E0,0xE8),(0xEDA319,0xED75E0,0xE8),
    (0xEF2167,0xED5130,0xE8),(0xED739D,0xF41110,0xE8),
    (0xED73D2,0xF3D760,0xE9),(0xED67E1,0xEF12D0,0xE8),
    (0xED7E5D,0xFF3DB0,0xE8),(0xED7E6F,0xF412C0,0xE8),
    (0xED7E7D,0xED6620,0xE8),(0xF0EB0B,0xF18F30,0xE8),
    (0xF0EB1A,0xED47A0,0xE8),(0xF0EB25,0xEF1280,0xE8),
    (0xF1CA0D,0xEF1280,0xE8),
    (0xEFEA0B,0xF412C0,0xE8),(0xEFEA4B,0xF412C0,0xE8),
]
LAYOUT=[
    (0xF3CC0C,'8d5f48'),(0xF3CC73,'c6878100000001'),
    (0xF3B670,'39417c'),(0xF3B675,'89417c'),
    (0xF44AA2,'3b0da8035b01'),(0xF44AA8,'0f85a8010000'),
    (0xED5149,'c706fc9d4801'),(0xEF2186,'897e14'),
    (0xF3D770,'807c240800c74618010000000f94c088461c'),
    (0xF44EE1,'680d936a36'),(0xF4535B,'385f1c753d'),
    (0xF4540E,'895f18'),(0xF45421,'3ac3744c'),
    (0xF45464,'68d0087afd'),(0xED6EA6,'8d7ef06a018bcf'),
    (0xF19528,'80791400750f'),(0xF1952E,'83b8d0000000007406'),
    (0xF186C1,'e85a0e000084c00f844a010000'),
    (0xF1647F,'85c00f84140a0000'),(0xF1908E,'83b8d0000000007407'),
    (0xED47A4,'894120'),(0xED57AB,'3b7e1c'),(0xED5801,'897e1c'),
    (0xED67E6,'8b4e1c894e20'),
    (0xF412E4,'838e2402000010'),(0xF412F0,'83a624020000ef'),
    (0xF41303,'838e2402000002'),(0xF4131B,'83a624020000fd'),
    (0xF41331,'838e2402000004'),(0xF4133D,'83a624020000fb'),
]

def verify(pe):
    if pe.sha256!=SHA or pe.image_base!=0x400000:raise ValueError('unsupported executable: '+pe.sha256)
    for name,address,ret,count,prefix in ENTRIES:
        raw=bytes.fromhex(prefix)
        if pe.read(address,len(raw))!=raw:raise ValueError(name+': entry mismatch')
        if ret is not None:
            raw=b'\xc3' if not count else b'\xc2'+struct.pack('<H',count)
            if pe.read(ret,len(raw))!=raw:raise ValueError(name+': stack cleanup mismatch')
    for site,target,opcode in CALLS:
        raw=pe.read(site,5)
        if raw[0]!=opcode or site+5+struct.unpack('<i',raw[1:])[0]!=target:raise ValueError('native call mismatch '+hex(site))
    for site,encoded in LAYOUT:
        raw=bytes.fromhex(encoded)
        if pe.read(site,len(raw))!=raw:raise ValueError('native field/branch mismatch '+hex(site))
    return {'sha256':SHA,'symbols':{'SporeCoop::Editor::'+name:address for name,address,*_ in ENTRIES},
            'calls':len(CALLS),'layout_receipts':len(LAYOUT)}

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('exe')
    parser.add_argument('--check-map',type=Path);parser.add_argument('--report',type=Path);args=parser.parse_args()
    try:
        result=verify(PE(args.exe))
        if args.check_map:
            source=args.check_map.read_text(encoding='utf-8')
            # The runtime loader selects build maps by this header in its first
            # ten lines; a matching filename and valid addresses do not load it.
            if '# exe_sha256='+SHA not in source.splitlines()[:10]:
                raise ValueError('save map has no exact-build runtime loader header')
            actual={name:int(address,16) for name,address in re.findall(r'^([^;#\s=]+)=0x([0-9A-Fa-f]+)',source,re.M)}
            if actual!=result['symbols']:raise ValueError('save map differs from verified entries')
        if args.report:args.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(f"verified {len(ENTRIES)} editor save/preview symbols, {len(CALLS)} call edges, {len(LAYOUT)} field/branch receipts");return 0
    except (OSError,ValueError) as error:
        print('verification failed: '+str(error),file=sys.stderr);return 1

if __name__=='__main__':sys.exit(main())
