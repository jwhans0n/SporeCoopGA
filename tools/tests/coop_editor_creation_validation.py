"""Bounded read-only validation receipt for already-owned native creation editors.

Run only after the UI owner explicitly grants an idle diagnostic window. This
script sends no console commands, calls no game functions and performs no writes
to either process. Process queries establish executable/module ownership; all
native object inspection uses ReadProcessMemory. No editor input is generated.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import hashlib
import json
from pathlib import Path
import re
import struct
import time

ROOT=Path(__file__).resolve().parents[2]
SUPPORTED={
    'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37',
    '47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab'}
# Exact GA New request variants. Family and subtype are checked independently.
EDITORS={'creature':0x465C50BA,'building-house':0x4E3F7777,
         'vehicle-military-land':0x7D433FAD,'spaceship':0x96B24187}
TYPE_NAMES={0x99E92F05:'BuildingCityHall',0x4E3F7777:'BuildingHouse',0xBDD15F3D:'BuildingFarm',
    0x47C10953:'BuildingFactory',0x72C49181:'BuildingEntertainment',
    0x7D433FAD:'VehicleMilitaryLand',0x8F963DCB:'VehicleMilitaryWater',0x441CD3E6:'VehicleMilitaryAir',
    0xF670AA43:'VehicleEconomicLand',0x2A5147A9:'VehicleEconomicWater',0x1A4E0708:'VehicleEconomicAir',
    0x9AD7D4AA:'VehicleCulturalLand',0x1F2A25B6:'VehicleCulturalWater',0x449C040F:'VehicleCulturalAir',
    0xBC1041E6:'VehicleColonyLand',0xC15695DA:'VehicleColonyWater',0x2090A11B:'VehicleColonyAir',
    0xC0B74287:'VehicleHarvester',0x98E03C0D:'UFO',0xDFAD9F51:'Cell',
    0x9EA3031A:'AnimalCreature',0x372E2C04:'TribeCreature',0xCCC35C46:'CivCreature',
    0x65672ADE:'SpaceCreature',0x4178B8E8:'AdventurerCreature',
    0xBCD73E89:'FloraSmall',0xB8669EC9:'FloraMedium',0x37148141:'FloraLarge'}
WIDGETS={0x03F67620:'early_done_visibility_gate',0x058A3350:'done',0x90439D7C:'cancel',
         0xD0353720:'type_chooser',0x503517B0:'type_list'}

def u32(data,at=0):return struct.unpack_from('<I',data,at)[0]

def verify_named_symbols():
    names={}
    for file in ('march2017_dc04aee5.sym','overrides_dc04aee5.sym'):
        for line in (ROOT/'data/symbols'/file).read_text().splitlines():
            match=re.match(r'([^=]+)=0x([0-9A-Fa-f]{8})\b',line)
            if match:names[match[1]]=int(match[2],16)
    required={'App::IGameModeManager::Get':0x67DCD0,'App::cGameModeManager::GetActiveMode':0x7D8380,
        'Editors::cEditor::OnEnter':0x58E6D0,'Editors::cEditor::OnExit':0x587A20,
        'UTFWin::Window::GetFlags':0x641760,'UTFWin::Window::GetControlID':0xA02C70,
        'UTFWin::UILayout::GetContainerWindow':0x810610}
    if any(names.get(name)!=address for name,address in required.items()):raise ValueError('Verified named symbol contract changed')

class Image:
    def __init__(self,path):
        self.bytes=path.read_bytes();at=u32(self.bytes,0x3C)
        if self.bytes[:2]!=b'MZ' or self.bytes[at:at+4]!=b'PE\0\0':raise ValueError('Invalid owned PE')
        opt=at+24
        if struct.unpack_from('<H',self.bytes,opt)[0]!=0x10B:raise ValueError('Expected supported Win32 image')
        self.base=u32(self.bytes,opt+28);self.size=u32(self.bytes,opt+56)
        self.sections=[];start=opt+struct.unpack_from('<H',self.bytes,at+20)[0]
        for index in range(struct.unpack_from('<H',self.bytes,at+6)[0]):
            row=start+40*index
            self.sections.append((u32(self.bytes,row+12),u32(self.bytes,row+16),u32(self.bytes,row+20)))
    def at(self,address,count):
        rva=address-self.base
        for virtual,size,raw in self.sections:
            if virtual<=rva and rva+count<=virtual+size:
                return self.bytes[raw+rva-virtual:raw+rva-virtual+count]
        raise ValueError('Verified code address is outside the owned PE')

class Module(C.Structure):
    _fields_=[('dwSize',W.DWORD),('th32ModuleID',W.DWORD),('th32ProcessID',W.DWORD),
        ('GlblcntUsage',W.DWORD),('ProccntUsage',W.DWORD),('modBaseAddr',C.c_void_p),
        ('modBaseSize',W.DWORD),('hModule',W.HMODULE),('szModule',W.WCHAR*256),('szExePath',W.WCHAR*260)]

def native_sample(kernel,handle,image,base,family,edit_source=None,observe_other=False):
    reads=0;total=0;started=time.perf_counter()
    def read(address,count):
        nonlocal reads,total
        if not address or not 0<count<=65536 or total+count>2*1024*1024 or reads>=30000:
            raise ValueError('Native diagnostic read bound exceeded')
        if time.perf_counter()-started>2:raise ValueError('Native diagnostic idle-window bound exceeded')
        data=C.create_string_buffer(count);done=C.c_size_t()
        if not kernel.ReadProcessMemory(handle,address,data,count,C.byref(done)) or done.value!=count:
            raise C.WinError(C.get_last_error())
        reads+=1;total+=count;return data.raw
    def word(address):return u32(read(address,4))
    def relocated(preferred):return base+preferred-image.base
    # Reproduce the two verified named getter reads, never call those getters.
    getter=image.at(0x67DCD0,6)
    if getter[0]!=0xA1 or getter[5]!=0xC3:raise ValueError('Mode-manager getter ABI changed')
    manager_global=relocated(u32(getter,1));manager=word(manager_global)
    begin,end=struct.unpack('<II',read(manager+0x14,8));active=struct.unpack('<i',read(manager+0x28,4))[0]
    if end<begin or (end-begin)%24 or not 0<=active<(end-begin)//24<=256:raise ValueError('Invalid owned mode roster')
    mode_row=read(begin+24*active,24);editor,mode_id=struct.unpack_from('<II',mode_row)
    if mode_id!=0x00DBDBA1:raise ValueError('Participant is not in the requested native creation editor')
    vt=word(editor);methods=read(vt+0x18,8)
    if struct.unpack('<II',methods)!=(relocated(0x58E6D0),relocated(0x587A20)):
        raise ValueError('Active mode is not the verified cEditor interface')
    head=read(editor,0x4B6);request=u32(head,0x1CC);request_bytes=read(request,0x9C)
    if (u32(request_bytes)!=relocated(0x13F6FEC) or u32(request_bytes,4)!=relocated(0x13F6FE8) or
        not 0<u32(request_bytes,8)<1000000 or u32(request_bytes,0xC)not in ((EDITORS[family],0xFFFFFFFF)if observe_other else (0xFFFFFFFF if edit_source is not None else EDITORS[family],)) or
        u32(request_bytes,0x1C)!=0x01654C10):raise ValueError('Native creation request owner/family changed')
    if edit_source is not None and (bool(request_bytes[0x20]) or list(struct.unpack_from('<III',request_bytes,0x10))!=edit_source):
        raise ValueError('Native Edit request/source changed')
    model=u32(head,0x98);resource=u32(head,0x7C);config=u32(head,0x24);ui=u32(head,0x78)
    if word(ui+0x5C)!=editor:raise ValueError('Creation UI belongs to another native editor')
    model_type=word(model+0x58);early_owner=word(resource+0xC);early_flag=read(early_owner+0x44,1)[0]
    allowed_begin,allowed_end=struct.unpack_from('<II',head,0x334)
    if allowed_end<allowed_begin or (allowed_end-allowed_begin)%4 or allowed_end-allowed_begin>256:
        raise ValueError('Allowed native subtype roster exceeds bound')
    allowed=list(struct.unpack('<'+'I'*((allowed_end-allowed_begin)//4),read(allowed_begin,allowed_end-allowed_begin))) if allowed_end!=allowed_begin else []
    raw_errors=struct.unpack_from('<4I',request_bytes,0x24)
    excluded=struct.unpack('<4I',read(relocated(0x15DA7EC),16))
    catalog=struct.unpack('<4I',read(relocated(0x15DAA40),16))
    current=[a&~b&0xFFFFFFFF for a,b in zip(raw_errors,excluded)]
    external=[a&~b&~c&0xFFFFFFFF for a,b,c in zip(catalog,excluded,current)]
    load=struct.unpack_from('<4I',head,0x48)
    intersection=lambda a,b:any(x&y for x,y in zip(a,b))
    edited=bool(head[0x4B3] or head[0x4B4])
    finish_without_debug=(2 if intersection(load,current) else 1 if intersection(load,external) else 0) if edited else (4 if intersection(load,current) else 3)
    # UILayout::GetContainerWindow810610 reads [this+14]->+88. Walk only
    # verified IWindow getters and the existing adapter's bounded child list.
    widgets={};unknown=[];visited=set();stack=[];roots=[];vtable_cache={}
    for offset in (0x14,0x2C):
        layout=word(ui+offset+0x14)
        if layout:
            root=word(layout+0x88)
            if root:roots.append(root);stack.append(root)
    while stack:
        window=stack.pop()
        if window in visited:continue
        if len(visited)>=4096:raise ValueError('Native window-node bound exceeded')
        visited.add(window);row=read(window,0x84);window_vt=u32(row)
        if window_vt not in vtable_cache:
            slots=read(window_vt,0x2C)
            vtable_cache[window_vt]=(u32(slots,0x1C)==relocated(0xA02C70) and u32(slots,0x28)==relocated(0x641760))
        if not vtable_cache[window_vt]:unknown.append(window);continue
        control=u32(row,0x7C)
        if control in WIDGETS:
            widgets.setdefault(f'{control:08X}',[]).append(dict(pointer=window,kind=WIDGETS[control],flags=u32(row,0x28),visible=bool(u32(row,0x28)&1)))
        anchor=window+0x38;node=u32(row,0x38);links=set()
        while node and node!=anchor:
            if node in links or len(links)>=4096:raise ValueError('Native child-list bound/cycle')
            links.add(node);stack.append(node-4);node=word(node)
    # Reject a replaced request or transition instead of combining two scenes.
    if (word(manager_global)!=manager or read(manager+0x14,8)!=struct.pack('<II',begin,end) or
        word(manager+0x28)!=active or read(begin+24*active,24)!=mode_row or word(editor+0x1CC)!=request or
        read(request,0x34)!=request_bytes[:0x34] or word(editor+0x98)!=model or word(editor+0x7C)!=resource or
        word(editor+0x78)!=ui or word(ui+0x5C)!=editor or word(model+0x58)!=model_type or
        read(editor+0x48,32)!=head[0x48:0x68] or read(editor+0x38C,4)!=head[0x38C:0x390]):
        raise ValueError('Creation owner/validation changed during the bounded sample')
    return dict(mode_manager=manager,active_index=active,mode_id=mode_id,editor=editor,request=request,
        request_editor_id=u32(request_bytes,0xC),source_key=list(struct.unpack_from('<III',request_bytes,0x10)),
        return_mode=u32(request_bytes,0x1C),new_asset=bool(request_bytes[0x20]),request_flags_34_3D=request_bytes[0x34:0x3E].hex(),
        model=model,model_type=model_type,model_type_name=TYPE_NAMES.get(model_type,'unmapped'),
        allowed_types=[dict(type=t,name=TYPE_NAMES.get(t,'unmapped')) for t in allowed],resource=resource,
        editor_config=config,early_done_owner=early_owner,early_done_owner_flag44=early_flag,
        load_blocking=list(load),load_secondary=list(struct.unpack_from('<4I',head,0x58)),
        request_current_raw=list(raw_errors),excluded_errors=list(excluded),error_catalog=list(catalog),
        current_masked=current,external_masked=external,changed_4B3_4B4=list(head[0x4B3:0x4B5]),
        finish_state_if_debug_disabled=finish_without_debug,debug_property='not sampled; computed finish state is conditional',
        save_phase_38C=u32(head,0x38C),ui=ui,ui_pending_command_cc=word(ui+0xCC),
        ui_confirmation_flag_ec=read(ui+0xEC,1)[0],layout_roots=roots,widgets=widgets,
        window_nodes=len(visited),unknown_window_getters=unknown[:16],container_window_traversal_complete=not unknown,
        widget_scope='Verified container descendants only; absence does not prove native UILayout::FindWindowByID absence',
        read_calls=reads,read_bytes=total,elapsed_ms=(time.perf_counter()-started)*1000)

def edit_source_key(text):
    key=re.fullmatch(r'([0-9A-Fa-f]{8})!([0-9A-Fa-f]{8})\.([0-9A-Fa-f]{8})',text)
    if not key or not int(key[2],16)or not int(key[3],16):raise ValueError('Exact Edit source instance/type required; group0 is valid')
    return [int(key[2],16),int(key[3],16),int(key[1],16)]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true')
    parser.add_argument('--metadata',type=Path)
    parser.add_argument('--candidate')
    parser.add_argument('--participant',action='append',help='Exact instance:PID:family, repeat for owned editors')
    parser.add_argument('--output',type=Path)
    parser.add_argument('--observe-other-editors',action='store_true',help='Read-only context pins; not an authoring or Save proof')
    parser.add_argument('--edit-source',help='Exact existing GROUP!INSTANCE.TYPE; Edit preserves the constructor FFFFFFFF sentinel')
    args=parser.parse_args()
    if not args.execute:
        print(json.dumps(dict(prepared=True,read_only=True,no_console=True,no_native_calls=True,profiles=EDITORS),indent=2));return
    if not args.metadata or not args.output or not args.participant:parser.error('Explicit metadata, participant and OUT output are required')
    output=args.output.resolve()
    if not output.is_relative_to((ROOT/'out').resolve()) or output.exists():parser.error('A new OUT receipt is required')
    edit_source=None
    if args.edit_source and args.observe_other_editors:parser.error('Author Edit proof and other-editor observer modes are separate')
    if args.edit_source:
        if len(args.participant)!=1:parser.error('One exact Edit participant required')
        edit_source=edit_source_key(args.edit_source)
    verify_named_symbols()
    if not re.fullmatch('[0-9a-f]{64}',args.candidate or ''):raise ValueError('Exact candidate SHA256 required')
    metadata=json.loads(args.metadata.read_text(encoding='utf-8-sig'));installation=Path(metadata['installation']).resolve()
    if not any(p.name=='windows-lab' and p.parent.name=='out' for p in installation.parents):raise ValueError('Private windows-lab installation required')
    if not (installation/'sporecoop-test-copy.json').is_file():raise ValueError('Private-copy marker missing')
    exe=(installation/'SporebinEP1/SporeApp.exe').resolve();dll=(installation/'SporebinEP1/mods/coop.dll').resolve();image=Image(exe)
    if hashlib.sha256(image.bytes).hexdigest() not in SUPPORTED or hashlib.sha256(dll.read_bytes()).hexdigest()!=args.candidate:
        raise ValueError('Executable/candidate payload hash mismatch')
    if image.at(0xA02C70,4)!=bytes.fromhex('8b417cc3') or image.at(0x641760,4)!=bytes.fromhex('8b4128c3'):
        raise ValueError('Native IWindow getter ABI changed')
    participants=[];records={r['instance']:r for r in metadata['processes']}
    for raw in args.participant:
        instance,pid,family=raw.split(':');instance=int(instance);pid=int(pid)
        if family not in EDITORS or instance not in records or records[instance]['pid']!=pid or not re.fullmatch('winlab_[a-z0-9_]+',records[instance]['profile']):raise ValueError('Exact owned participant/family mismatch')
        if instance in [r[0]for r in participants]:raise ValueError('Duplicate participant')
        participants.append((instance,pid,family))
    if not 1<=len(participants)<=4:raise ValueError('At most four exact participants')
    kernel=C.WinDLL('kernel32',use_last_error=True)
    kernel.OpenProcess.argtypes=(W.DWORD,W.BOOL,W.DWORD);kernel.OpenProcess.restype=W.HANDLE
    kernel.ReadProcessMemory.argtypes=(W.HANDLE,W.LPCVOID,W.LPVOID,C.c_size_t,C.POINTER(C.c_size_t));kernel.ReadProcessMemory.restype=W.BOOL
    kernel.QueryFullProcessImageNameW.argtypes=(W.HANDLE,W.DWORD,W.LPWSTR,C.POINTER(W.DWORD));kernel.QueryFullProcessImageNameW.restype=W.BOOL
    kernel.CreateToolhelp32Snapshot.argtypes=(W.DWORD,W.DWORD);kernel.CreateToolhelp32Snapshot.restype=W.HANDLE
    kernel.Module32FirstW.argtypes=(W.HANDLE,C.POINTER(Module));kernel.Module32FirstW.restype=W.BOOL
    kernel.Module32NextW.argtypes=(W.HANDLE,C.POINTER(Module));kernel.Module32NextW.restype=W.BOOL
    kernel.CloseHandle.argtypes=(W.HANDLE,)
    result=dict(candidate_sha256=args.candidate,time=time.time(),read_only=True,no_console=True,no_native_calls=True,observer_only=args.observe_other_editors,rows=[])
    for instance,pid,family in participants:
        handle=kernel.OpenProcess(0x1010,False,pid)
        if not handle:raise C.WinError(C.get_last_error())
        try:
            path=C.create_unicode_buffer(32768);length=W.DWORD(len(path))
            if not kernel.QueryFullProcessImageNameW(handle,0,path,C.byref(length)) or Path(path.value).resolve()!=exe:raise ValueError('Owned executable path mismatch')
            modules=kernel.CreateToolhelp32Snapshot(0x18,pid)
            if modules==C.c_void_p(-1).value:raise C.WinError(C.get_last_error())
            try:
                module=Module();module.dwSize=C.sizeof(Module);found={};ok=kernel.Module32FirstW(modules,C.byref(module));count=0
                while ok:
                    count+=1
                    if count>512:raise ValueError('Module ownership bound exceeded')
                    found[Path(module.szExePath).resolve()]=module.modBaseAddr
                    ok=kernel.Module32NextW(modules,C.byref(module))
                if exe not in found:raise ValueError('Expected private executable is not loaded')
                row=native_sample(kernel,handle,image,found[exe],family,edit_source,args.observe_other_editors)
                result['rows'].append(dict(instance=instance,pid=pid,expected_family=family,image_base=found[exe],
                    dll_base=found.get(dll),dll_module_witness='optional: SporeKit may map payload outside the loader module list; candidate hash pins installed payload, owner pins this launch',**row))
            finally:kernel.CloseHandle(modules)
        finally:kernel.CloseHandle(handle)
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(dict(output=str(args.output),participants=len(result['rows']),rows=[dict(instance=r['instance'],editor=r['editor'],request=r['request'],model_type=r['model_type_name'],read_bytes=r['read_bytes'],elapsed_ms=r['elapsed_ms'])for r in result['rows']]),indent=2))

if __name__=='__main__':main()
