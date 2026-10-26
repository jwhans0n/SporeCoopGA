#!/usr/bin/env python3
"""Verify the exact GOG Adventure Editor ABI receipts and render its symbol map."""
import argparse
import json
from pathlib import Path
import struct
import sys

from pe import PE

SHA = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
ENTRIES = [
    ('Allocate',0xF47240,'8b4424188b4c24148b542408508b4424',[(0xF47269,0)],
     'cdecl(size,name,flags0,flags0,int0,int0)->native allocation,ret; GeneralAllocator16C8B44 through928C80; pairs withF47220 plainfree, NOT9262B0 header allocation',
     [(0xF45877,0xF47240),(0xF458B4,0xF47240),(0xF47264,0x928C80)],
     [(0xF2E9BF,'56e85b880100'),(0xF3CBE3,'56e837a60000')]),
    ('ScenarioResourceCtor', 0xF2E670, '5355568bf157c706bcbc3e0133c08d4e', [(0xF2E9A1, 0)],
     'thiscall(uninitialized native allocation2CB0)->resource EAX,ret; refcount starts0, vtable148C644 AddRef0/Release4',
     [(0xF45885, 0xF2E670)], [(0xF2E694, 'c70644c64801')]),
    ('CopyScenarioResource', 0xDFFA00, '53558b6c240c8b45088bd98943088b4d', [(0xDFFD67, 4)],
     'thiscall(destination resource,const source resource*)->destination EAX,ret4; deep copies properties/strings/acts/classes/markers and camera',
     [(0xF458A0, 0xDFFA00), (0xF459AC, 0xDFFA00), (0xF46428, 0xDFFA00)], []),
    ('HistoryEntryCtor', 0xF3CAE0, '8b442404538b5c240c568bf1c7460438', [(0xF3CB68, 12)],
     'thiscall(allocation18,ScenarioData* parent,resource* before,resource* after)->entry EAX,retC; owns snapshots viaAddRef0/Release4; parent+0C borrowed',
     [(0xF458C6, 0xF3CAE0)], [(0xF3CB16, '89460c')]),
    ('CopyMarkerRows',0xDFF300,'518b442408568bf13bc60f8418010000',[(0xDFF42C,4)],
     'thiscall(native marker row vector* destination,const row vector* source)->destination,ret4; owning deep-copy/destruction; native ResourceCopy then copiespool14/18 metadata',
     [(0xDFFC4C,0xDFF300)],[(0xDFFC51,'8b46148947148b4e18894f18')]),
    ('InsertMarker',0xF2EBC0,'81ec34020000568bf1578b7e1881ff',[(0xF2ECEB,4)],
     'thiscall(native marker pool*,const Marker*)->uint32 pool index,ret4; owns deep marker copy, native flags/free-list handling, no scene or UI calls',
     [(0xF444F4,0xF2EBC0),(0xF44282,0xF2EBC0)],[]),
    ('StartHistory', 0xF45810, '803df865600100578bf90f84f2000000', [(0xF45913, 0)],
     'thiscall(ScenarioData*),ret or nativeRelease tailcall; may reuse previous native after resource as before; adapter must copy current live independently; no mutation veto return',
     [(0xED0BA2, 0xF45810)], [(0xF458FA, 'c605f865600100'),(0xF45852,'8b701485f6')]),
    ('CommitHistory', 0xF45920, '803df865600100558be90f85b3000000', [(0xF459E4, 0)],
     'thiscall(ScenarioData*),ret or nativeRelease tailcall; deep copies after snapshot, appends one native gesture, clears owned editing entry',
     [], [(0xF459B7, 'c605f865600101')]),
    ('RestoreHistorySnapshot', 0xF46400, '5355568bf18b0d98d25a018b018b5020', [(0xF464A4, 4)],
     'thiscall(real historyEntry*,resource* snapshot),ret4; parent+0C, native selection clear/scene teardown/deepcopy/reconstruction/currentact rebuild and synchronous072BDB11',
     [(0xF464B4, 0xF46400), (0xF464C4, 0xF46400)], [(0xF46421, '8b4e0c8b491050')]),
    ('UndoHistoryEntry', 0xF464B0, '8b411050e847ffffffc3', [(0xF464B9, 0)],
     'thiscall(real historyEntry*),ret; restore owned before snapshot+10', [], []),
    ('RedoHistoryEntry', 0xF464C0, '8b411450e837ffffffc3', [(0xF464C9, 0)],
     'thiscall(real historyEntry*),ret; restore owned after snapshot+14', [], []),
    ('GetAct', 0xEFC410, 'a1887b6c0185c074038b00c333c0c3', [(0xEFC41B, 0), (0xEFC41E, 0)],
     'cdecl()->uint32 editor act,ret; selected-act global16C7B88 points current integer, null returns0',
     [(0xF4643F, 0xEFC410)], []),
    ('SetSelection', 0xEFE810, 'a1887b6c01578bf985c00f845b010000', [(0xEFE86D,4),(0xEFE97C,4)],
     'thiscall(toolManager,GameData*),ret4; native owned selection atContext+4, auxiliaries+8/+10; null clears, updates gizmo/UI, no authored mutation',
     [], [(0xEFE8E3,'8b0d887b6c015683c104')]),
    ('ToolAction', 0xF03770, '8b542404a1887b6c01568bf18b480483', [(0xF0379B,12),(0xF037BD,12),(0xF037CE,12),(0xF03820,12),(0xF03839,12),(0xF03852,12)],
     'thiscall(toolManager,uint32 action,uint32 raw,uint32 raw)->uint32 result,retC; action1 begins selected drag,6 modifies scale,11 clone or path node; action0 finalizes existing gesture',
     [(0xF037C3,0xEFED40)], []),
    ('DeleteSelection',0xF02CE0,'a1887b6c0183ec18538bd985c00f84f3',[(0xF02E65,0)],
     'thiscall(toolManager),ret; selected marker/path-node removal, owns Start/Commit boundary',
     [(0xF02E69,0xF45810),(0xF02E93,0xF428A0),(0xF02EC8,0xF45920)],[]),
    ('PlaceClass',0xF00670,'568b742408578bf98b4f0856e8afe403',[(0xF006A0,4),(0xF00723,4)],
     'thiscall(toolManager,uint32 nativeClassID)->boolAL,ret4; StartHistory then creates native placement held by cursor until ToolAction finalizes',
     [(0xF006A6,0xF45810),(0xF006B2,0xEFEDC0)],[]),
    ('CloneSelection',0xF00730,'a1887b6c0183781000535556578bf9',[(0xF00877,4),(0xF00880,4),(0xF009D5,4)],
     'thiscall(toolManager,GameData* source)->boolAL,ret4; Alt-drag pre-entry before native StartHistory and source actor hiding; owns native CloneMarker/cursor path',
     [(0xF008D7,0xF45810),(0xF0096B,0xF44460)],[]),
    ('CloneMarker',0xF44460,'51578bf98b4c240c8b0183f8fe7523',[(0xF4448F,4),(0xF4458D,4)],
     'thiscall(ScenarioData,constMarker*)->newMarker*,ret4; native deep marker-map insert, assigns new runtime index, instantiates/registers actor and preserves opaque per-marker fields',
     [(0xF444F4,0xF2EBC0),(0xF44560,0xF3D930)],[(0xF444DE,'3d00040000')]),
    ('CreateMarker',0xF44210,'8b44240481ec48020000535556508bd9',[(0xF44438,16),(0xF44446,16),(0xF44455,16)],
     'thiscall(ScenarioData,uint32 classID,constVec3*,constQuat*,float scale)->newMarker*,ret10; native default marker creation with current authored act count and actor registration',
     [(0xF44222,0xF3E740),(0xF44282,0xF2EBC0),(0xF443FA,0xF3D930)],[(0xF44267,'3d00040000')]),
    ('DeleteMarker',0xF428A0,'568bf1e8b8fdffff8b442408508bcee8',[(0xF428BC,4)],
     'thiscall(ScenarioData,Marker*),ret4; native economy checks around F416C0 actor cleanup/map erase; no invented loot or class changes',
     [(0xF428AF,0xF416C0)],[]),
    ('ImportClass',0xF462F0,'b8c42c0000e846a4290056578bf98b47',[(0xF463E6,4),(0xF463F3,4)],
     'thiscall(ScenarioData,constResourceKey*)->classID orFFFFFFFF,ret4; allocates class map entry and increments class counter; shared preview holds this before mutation',
     [(0xF46357,0xED36D0)],[]),
    ('UndoHistory',0xED9B20,'568bf1807e200074288b461485c07621',[(0xED9B52,0)],
     'thiscall(editorHistory*),ret; decrements cursor14 BEFORE entryUndo, changes native history counts; gate manager itself before cursor mutation',
     [],[(0xED9B30,'8b0e48894614c6462000')]),
    ('RedoHistory',0xED9B60,'568bf1807e200074308b0e8b56048b46',[(0xED9B9A,0)],
     'thiscall(editorHistory*),ret; increments cursor14 BEFORE entryRedo, changes native history counts; gate before mutation',
     [],[(0xED9B7A,'8d5001895614c6462000')]),
    ('HasUndo',0xED4750,'80792000740c837914007606',[(0xED4761,0),(0xED4764,0)],
     'thiscall(editorHistory*)->boolAL,ret; native toolbar/shortcut precondition checks notEditing20 and cursor14>0; shared exact history query uses cached chronological availability',
     [(0xEDA4D4,0xED4750)],[(0xEDA4DB,'0f841fffffff')]),
    ('HasRedo',0xED47C0,'8079200074138b41042b01c1f802',[(0xED47D8,0),(0xED47DB,0)],
     'thiscall(editorHistory*)->boolAL,ret; native toolbar/shortcut precondition checks notEditing20 and cursor14<vector size; shared exact history query uses cached chronological availability',
     [(0xEDA528,0xED47C0)],[(0xEDA52F,'0f84cbfeffff')]),
    ('ScriptInput',0xEE4290,'81ece0010000a1a47a6c01538b58745556578bf9',[(0xEE5352,8)],
     'thiscall(ScriptUI+8 IWinProc,window*,Msg*)->boolAL,ret8; authored act/class/goal/text controls; initial shared editing leaves event unhandled before native authored setters',
     [],[(0x148A778,'9042ee00'),(0xEDD4A4,'c7460860a74801'),
         (0xF08DFB,'8b0da47a6c018b51148b4a188b511853568b7210')]),
    ('ClassInput',0xEE5380,'81ec6405000053558be98b45145657',[(0xEE6716,8)],
     'thiscall(classSettings+0C IWinProc,window*,Msg*)->boolAL,ret8; separately registered native class-property binder, Msg8 type/0C control; shared exact owner suppresses setters while retaining dismiss0595E0A8',
     [],[(0x148A7C0,'8053ee00'),(0xEE53C8,'8bb4247c050000'),(0xEE59CC,'817e0c10cd4207')]),
    ('FindLayoutWindow',0x810650,'8b491485c97405e944fcffff33c0c20800',[(0x81065E,8)],
     'thiscall(UILayout,uint32 controlID,bool recursive)->IWindow*,ret8 or tail8102A0; searches this layout owned controls; property UI root073E6BA0, never disable shared parentlayer returned by GetContainerWindow',
     [(0xEDEC4D,0x810650)],[(0xEDEC46,'6a0168a06b3e07'),(0x8102FB,'c20800'),(0x810304,'c20800')]),
]


def verify(pe):
    if pe.sha256 != SHA or pe.image_base != 0x400000:
        raise ValueError('unsupported native executable: ' + pe.sha256)
    for name, address, prefix, returns, note, calls, checks in ENTRIES:
        if not pe.in_text(address) or pe.read(address,len(bytes.fromhex(prefix))) != bytes.fromhex(prefix):
            raise ValueError(name + ': entry mismatch')
        for site, count in returns:
            raw = b'\xc3' if not count else b'\xc2' + struct.pack('<H',count)
            if pe.read(site,len(raw)) != raw: raise ValueError(name + ': return mismatch ' + hex(site))
        for site, target in calls:
            raw=pe.read(site,5)
            if raw[0]!=0xE8 or site+5+struct.unpack('<i',raw[1:])[0]!=target:
                raise ValueError(name + ': caller mismatch ' + hex(site))
        for site, encoded in checks:
            raw=bytes.fromhex(encoded)
            if pe.read(site,len(raw))!=raw: raise ValueError(name + ': layout mismatch ' + hex(site))
    # Marker float +24 is the separately serialized scale-range companion.
    # Native Scale supplies its output pointer before writing physical +20.
    for site, encoded in [(0x138C2B8,'c7442414e9471107'),
                          (0x138C2F4,'c744241824000000'),
                          (0xF005B7,'8d5624'),(0xF005CF,'d95e20')]:
        raw=bytes.fromhex(encoded)
        if pe.read(site,len(raw))!=raw:
            raise ValueError('Marker scale-range companion receipt mismatch '+hex(site))
    raw=pe.read(0xF005C6,5)
    if raw[0]!=0xE8 or 0xF005CB+struct.unpack('<i',raw[1:])[0]!=0xF40840:
        raise ValueError('Native Scale companion calculation caller mismatch')
    # F3BF00 invokes the owned, in-progress entry Undo to cancel invalid native
    # placement. Its QI(0743BB11) returns this exact primary history interface.
    for site, encoded in [(0xF3CAFA,'c7066cd24801'),
                          (0xF3CB70,'8bc18b4c2404'),
                          (0xF3CB7E,'81f911bb4307'),(0xF3CB94,'c20400'),
                          (0xF3BF00,'803df865600100'),(0xF3BF09,'8b0dfc656001'),
                          (0xF3BF18,'6811bb4307'),(0xF3BF27,'8b4210'),
                          (0xF3BF2A,'ffd0'),(0xF3BF47,'c605f865600101')]:
        raw=bytes.fromhex(encoded)
        if pe.read(site,len(raw))!=raw:
            raise ValueError('Owned history cancellation receipt mismatch '+hex(site))
    for slot, target in [(0x0C,0xF3CB70),(0x10,0xF464B0),(0x14,0xF464C0)]:
        if pe.u32(0x148D26C+slot)!=target:
            raise ValueError('History primary interface slot mismatch '+hex(slot))
    for site, target in [(0xF0329F,0xF030B0),(0xF030E0,0xF3BF00)]:
        raw=pe.read(site,5)
        if raw[0]!=0xE8 or site+5+struct.unpack('<i',raw[1:])[0]!=target:
            raise ValueError('Native invalid-scale cancellation caller mismatch '+hex(site))
    # Native history has vector0, cursor14, editing flag20, owned entry24.
    if pe.read(0xF4582A,5) != bytes.fromhex('b9d8656001'):
        raise ValueError('History global reference mismatch')
    if pe.read(0xB3D510,6)!=bytes.fromhex('a128eb6701c3'):
        raise ValueError('Loading screen singleton receipt mismatch')
    # Fresh owned history entry must acquire its first primary-interface ref.
    if pe.u32(0x148D270)!=0x7B87B0 or pe.read(0x7B87B0,14)!=bytes.fromhex('8b410883c10483c0ff8941047511'):
        raise ValueError('History entry zero-reference Release receipt mismatch')
    # Successful native editor shell load retains display24; failure clears it.
    if pe.read(0xED5CCC,3)!=bytes.fromhex('897e24') or pe.read(0xED5D06,7)!=bytes.fromhex('c7462400000000'):
        raise ValueError('Native editor shell readiness layout mismatch')
    # Read returns byte count in EAX, unlike the bool-AL Write slot.
    if pe.read(0xF2FD9F,0x11) != bytes.fromhex('8b168b523050578bceffd28b4c24103bc1'):
        raise ValueError('ScenarioResource::Read IStream count ABI mismatch')
    return [{'name':'SporeCoop::Editor::'+n,'address':a,'note':s}
            for n,a,p,r,s,c,k in ENTRIES] + [dict(name='SporeCoop::Editor::History',address=0x16065D8,
            note='global cScenarioEditHistory object; vector0,cursor14,notEditing20,ownedEntry24; not callable'),
            dict(name='SporeCoop::Editor::HistoryEntryVTable',address=0x148D26C,
                 note='read-only primary history interface vtable; ctorF3CAFA, QI0743BB11 returns this; Undo10/Redo14; F3BF00 owned current-entry cancellation uses Undo10, not chronological history'),
            dict(name='SporeCoop::Editor::Context',address=0x16C7B88,
                 note='global native editor context pointer; act0,ownedselectionGameData4,auxiliary8/10,tool68; verified EFC410/EFE810/F03770 references'),
            dict(name='SporeCoop::Editor::ToolManager',address=0x15AD298,
                 note='global native tool manager pointer; SetSelection vt20,ToolAction vt2C,DeleteSelection vt10,PlaceClass vt34; verified148B708 slots'),
            dict(name='SporeCoop::Editor::LoadingScreen',address=0x167EB28,
                 note='global loading screen pointer; existing verified B3D510 getter, active flag370; ready requires native Build display1C zero and inactive loading')]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe');parser.add_argument('--write-map',type=Path)
    parser.add_argument('--check-map',type=Path);parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    try:
        evidence=verify(PE(args.exe))
        lines=['# SporeKit verified native Adventure Editor overrides for GOG 2024.',
               '# exe_sha256='+SHA,
               '# evidence=tools/addrport/verify_editor_dc04.py; docs/re/adventure-world.md']
        lines += [f"{e['name']}=0x{e['address']:08X} ; verified static: {e['note']}" for e in evidence]
        rendered='\n'.join(lines)+'\n'
        if args.write_map:args.write_map.write_text(rendered,encoding='utf-8',newline='\n')
        if args.check_map and args.check_map.read_text(encoding='utf-8')!=rendered:
            raise ValueError('native editor map differs from reviewed evidence')
        if args.report:args.report.write_text(json.dumps(dict(sha256=SHA,symbols=evidence),indent=2)+'\n',encoding='utf-8')
        print(f'verified {len(evidence)} Adventure Editor entries for {SHA}');return 0
    except (OSError,ValueError) as error:
        print('verification failed: '+str(error),file=sys.stderr);return 1


if __name__=='__main__':sys.exit(main())
