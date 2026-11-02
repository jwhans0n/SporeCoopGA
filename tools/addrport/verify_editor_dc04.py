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
    ('CopyTerrainProperties',0xDFCA20,'5355568bf1578b7c24148b4708',[(0xDFCA89,4)],
     'thiscall(PropertyList destination,const PropertyList* source)->destination,ret4; owning map and parent copy used by native resource deep copy',
     [(0xDFFA29,0xDFCA20),(0xDFCA4B,0x6A1E80)],[(0xDFFA19,'8d4d18'),(0xDFFA23,'8d4b18'),(0xDFCA6C,'895e30')]),
    ('TerraformInitializeResource',0xF261B0,'a1a47a6c01578b781885ff741a',[(0xF261D8,0)],
     'thiscall(ScenarioResource),plain ret; native load and Terraform activation build the planet before actor grounding',
     [(0xF46502,0xF261B0),(0xF37F22,0xF261B0),(0xF261D1,0xF37530)],[]),
    ('TerraformActivate',0xF37DC0,'83ec0c5355568bf1c6868c01000001',[(0xF37FE3,4)],
     'thiscall(TerraformMode,const ResourceKey*),ret4; native scene activation initializes resource or default planet before ScenarioData',
     [(0xEF1DEE,0xF37DC0),(0xF37F3A,0xF379C0)],
     [(0xEF1DD6,'897e18'),(0xEF1E77,'c786cc00000001000000'),(0xEF1ED0,'897e74'),(0xEF1F19,'e8b2450500')]),
    ('TerraformBuildProperties',0xF37530,'83ec1c8b4424205657508bf1',[(0xF378CF,4)],
     'thiscall(TerraformMode,const PropertyList*),ret4; loads properties before native vectors/terrain/environment/flora rebuild',
     [(0xF3753C,0xF32A90),(0xF37A20,0xF37530)],[]),
    ('TerraformSetTime',0xF33420,'83ec14f30f10442418f30f11',[],
     'thiscall(TerraformMode,float fraction),tail F322E0 consumes4; clamps0..1 and writes ERL D4',
     [(0xF3AC20,0xF33420)],[(0xF33475,'e966eeffff'),(0xF33462,'f30f1181d4000000')]),
    ('TerraformSetDayNight',0xF33480,'807c240400741d',[],
     'thiscall(TerraformMode,bool),tail F322E0 consumes4; writes ERL DC and refreshes',
     [(0xF3AEF0,0xF33480),(0xF3AEE4,0xF33A90),(0xF3AEF7,0xF34400)],[(0xF3349F,'e93ceeffff'),(0xF334B7,'e924eeffff')]),
    ('TerraformSetVisualStyle',0xF339F0,'8b442404568bf18b8ef4000000',[(0xF33A40,4)],
     'thiscall(TerraformMode,const ResourceKey*),ret4; replaces style F4/FC and refreshes owned effect',
     [(0xF33A33,0xF32260),(0xF33A3A,0xF32B50)],[]),
    ('TerraformAppendModel',0xF35890,'568bf18b86a40000003b86a8',[(0xF358FA,8)],
     'thiscall(TerraformMode,const ResourceKey*,const Transform56*),ret8 or owning vector-insert tail; appends A0/B4 arrays',
     [(0xF37225,0xF35890)],[(0xF35901,'e96af3ffff')]),
    ('TerraformAppendEffect',0xF35910,'83ec0c53568bf18b4618',[(0xF35A67,24),(0xF35A7B,24)],
     'thiscall(TerraformMode,raw32,raw32,const Transform56*,raw32,raw32,raw32),ret18; appends parallel effect columns',
     [(0xF37328,0xF35910)],[(0xF3595A,'8b462c'),(0xF35A37,'8d4e78')]),
    ('TerraformDiscardHistory',0xF322B0,'8b0dfc65600185c97411',[(0xF322D2,0)],
     'native no-stack-argument history abandon, ECX unused; releases ownedEntry24 and marks notEditing20',
     [(0xEE97CB,0xF322B0),(0xEEBD80,0xF322B0)],[]),
    ('TerraformUndoHistoryEntry',0xF383E0,'568bf18b0d98d25a018b01',[],
     'thiscall(TerraformHistoryEntry),no stack arguments; mode3C, beforePropertyList0C, tail Rebuild',
     [(0xF383FA,0xF32A90)],[(0xF38404,'e9f7f5ffff')]),
    ('TerraformSetWaterLevel',0xF320E0,'83ec08568bf1e825f00e0085',[(0xF32190,4)],
     'thiscall(TerraformMode,float),ret4; normalizes water range into WTA C8 and refreshes water contacts',[(0xF0CF2B,0xF320E0)],[]),
    ('TerraformSetWaterType',0xF31BC0,'8b4424048d50ff83fa0e7715',[(0xF31BE1,4)],
     'thiscall(TerraformMode,int),ret4 or state tail FBAF20; bounded1..15 writes EC',[(0xF0D047,0xF31BC0)],[]),
    ('TerraformSetAtmosphereType',0xF31BF0,'8b4424048d50ff83fa0e7715',[(0xF31C11,4)],
     'thiscall(TerraformMode,int),ret4 or state tail FBAEE0; bounded1..15 writes F0',[(0xF0D084,0xF31BF0)],[]),
    ('TerraformSetGroundEffect',0xF35160,'8b442404898160010000e891',[(0xF3516F,4)],
     'thiscall(TerraformMode,uint32 effectID),ret4; writes160 and rebuilds handles',[(0xEE81A2,0xF35160),(0xEE8233,0xF35160)],[]),
    ('TerraformRefreshColors',0xF34240,'83ec14568bf18b8e94010000',[(0xF3436E,4)],
     'thiscall(TerraformMode,bool nativeColors),ret4; copies water/atmosphere/beach/cliff and owning color/elevation vectors',[(0xF0D08D,0xF34240)],[]),
    ('FloraRemove',0xEE94A0,'83ec085657894c240ce8627c',[(0xEE958C,4)],
     'thiscall(FloraUI,IWindow* card),ret4; active PlanetRecord flora vectors inside Terraform Start/Commit',
     [(0xEE94E7,0xF33A90),(0xEE9578,0xF34400)],[]),
    ('FloraAdd',0xEE9590,'83ec08535556578be9e8727b',[(0xEE96DE,8)],
     'thiscall(FloraUI,IWindow* card,const ResourceKey*),ret8; active PlanetRecord flora key/bracket/density vectors',
     [(0xEE9707,0xEE9590),(0xEE95D4,0xF33A90),(0xEE96CB,0xF34400)],[]),
    ('FloraInput',0xEE9740,'83ec34535556578be9e8c279',[(0xEE982F,8),(0xEE98F0,8)],
     'thiscall(FloraUI+8 IWinProc,IWindow*,Msg*)->boolAL,ret8; control072BFDFD density slider and active PlanetRecord10C',
     [(0xEE981A,0xF33A90),(0xEE97B3,0xF34400)],[(0xEE98AE,'f30f11830c010000')]),
    ('TerraformStartHistory',0xF33A90,'5153555633db5733ff8be989',[(0xF33C05,0)],
     'thiscall(TerraformMode),ret or native Release tail; distinct40 entry properties0C/24, placement companions10/28 and mode3C',
     [(0xEE72FC,0xF33A90),(0xF00A4F,0xF33A90)],[(0xF33B89,'896e3c')]),
    ('TerraformCommitHistory',0xF34400,'558be98b0dfc65600185c90f',[(0xF344F1,0)],
     'thiscall(TerraformMode),ret or Release tail; exports property vectors and captures placement companions',
     [(0xEE7340,0xF34400),(0xF00C64,0xF34400)],[(0xF344AD,'8d4628')]),
    ('TerraformLoadProperties',0xF32A90,'a1a47a6c0183ec1883b8d000',[(0xF32B42,4)],
     'thiscall(TerraformMode,const PropertyList*),ret4; owns mode0C and sets dirty1BA',
     [(0xF383FA,0xF32A90),(0xF3842A,0xF32A90)],[(0xF32B37,'c686ba01000001')]),
    ('TerraformRebuild',0xF37A00,'a1a47a6c01568bf180beba01',[(0xF37A74,0)],
     'thiscall(TerraformMode),ret; rebuilds terrain, grounds placements and validates acts, then clears dirty flags',
     [(0xF381C2,0xF37A00)],[(0xF37A35,'e816680000'),(0xF37A4C,'e8ef850000')]),
    ('TerraformSelection',0xF32630,'56578b7c240c8bf185ff7440',[(0xF32680,4),(0xF3268A,4)],
     'thiscall(TerraformMode,GameData*)->borrowed descriptor16 or null; definition4/index8 and definition68 model flag',
     [(0xF00A41,0xF32630),(0xF00D3D,0xF32630)],[]),
    ('TerraformRemoveObject',0xF37FF0,'8b4424045556508bf1e832a6',[(0xF381C9,4)],
     'thiscall(TerraformMode,GameData*),ret4; removes parallel recipe columns and scene descriptor',[(0xF00A6C,0xF37FF0)],[]),
    ('TerraformUpdateObject',0xF381D0,'83ec68538bd98b83b0010000',[(0xF383DB,4)],
     'thiscall(TerraformMode,bool final),ret4; descriptor1B0 resolves kind/index and updates recipe',[(0xF00C5C,0xF381D0)],[]),
    ('TerrainLoaded',0xFA0D40,'33c03981fc0000000f94c0c3',[(0xFA0D4B,0)],
     'thiscall(TerrainSphere)->boolAL; true when loaderFC is null, confirmed by immediate completion callback F96C70',[],
     [(0xFA1BB4,'c706a80b4901'),(0xF96C70,'83b9fc000000007419')]),
    ('TerraformSetWaterColor',0xF31FA0,'8b4424048b10899148010000',[(0xF31FE8,4)],
     'thiscall(TerraformMode,const ColorRGB*),ret4; color148 and state manager',[(0xEE7588,0xF31FA0)],[]),
    ('TerraformSetAtmosphereColor',0xF31FF0,'8b4424048b10899154010000',[(0xF32038,4)],
     'thiscall(TerraformMode,const ColorRGB*),ret4; color154 and state manager',[(0xEE746E,0xF31FF0)],[]),
    ('TerraformSetBeachColor',0xF32040,'8b4424048b10899130010000',[(0xF32088,4)],
     'thiscall(TerraformMode,const ColorRGB*),ret4; color130 and state manager',[(0xEE74DD,0xF32040)],[]),
    ('TerraformSetCliffColor',0xF32090,'8b4424048b1089913c010000',[(0xF320D8,4)],
     'thiscall(TerraformMode,const ColorRGB*),ret4; color13C and state manager',[(0xEE7519,0xF32090)],[]),
    ('TerraformSetTemperature',0xF31C20,'f30f10442404568bf1f30f11',[(0xF31C74,4)],
     'thiscall(TerraformMode,float),ret4; WTA CC and native temperature/state manager',[(0xF0CF49,0xF31C20)],[]),
    ('TerraformSetAtmosphere',0xF31C80,'f30f10442404568bf1f30f11',[(0xF31CD4,4)],
     'thiscall(TerraformMode,float),ret4; WTA D0 and native atmosphere/state manager',[],[]),
    ('TerraformSetGroundType',0xF31AE0,'8b4424048d50ff83fa3f7753',[(0xF31B3F,4)],
     'thiscall(TerraformMode,int),ret4; planetInfo E0 bounded1..64',[(0xF0CFCD,0xF31AE0)],[]),
    ('TerraformSetDeadType',0xF31B50,'8b4424048d50ff83fa3f7753',[(0xF31BAF,4)],
     'thiscall(TerraformMode,int),ret4; planetInfo E4 bounded1..64',[(0xF0D00A,0xF31B50)],[]),
    ('TerraformSetOverride',0xF31AC0,'8b4424048881040100008b89',[],
     'thiscall(TerraformMode,bool),tail FBD680; writes override104, target consumes4 bytes',[],[(0xF31AD4,'e9a7bb0800')]),
    ('TerraformSetHazards',0xF31D70,'8a44240488818d010000',[(0xF31D7A,4)],
     'thiscall(TerraformMode,bool),ret4; writes hazards18D only',[],[]),
    ('TerraformSetGradient',0xF34380,'8b4424045355568bd9578db3',[(0xF343FB,8)],
     'thiscall(TerraformMode,const floatVector*,const colorVector*),ret8; owns copies into11C/108 and terrain state',[(0xEE7339,0xF34380)],[]),
    ('CopyCrewRows',0xDFB6F0,'558b6c2408568bf13bee',[(0xDFB819,4)],
     'thiscall(destination native crew vector,const source vector*)->destination,ret4; owns primitive38 rows and allocator metadata',
     [(0xDFFBF4,0xDFB6F0)],[]),
    ('AddCrew',0xF40980,'83ec38535556578bf9',[(0xF40A3C,4),(0xF40A48,4)],
     'thiscall(ScenarioData,const ResourceKey*)->int index or minus1,ret4; capacity238 and native178 rows, optional captain-scene refresh',
     [(0xED6BEC,0xF40980),(0xED6D05,0xF40980)],[]),
    ('ReplaceCrew',0xF3BB80,'55568b74240c578bf9',[(0xF3BC20,8)],
     'thiscall(ScenarioData,int index,const ResourceKey*),ret8; bounded178 row, optional captain-scene refresh',
     [(0xED6BDC,0xF3BB80),(0xED6CF0,0xF3BB80)],[]),
    ('RemoveCrew',0xF3E6B0,'53568b74240c8bd957',[(0xF3E731,4)],
     'thiscall(ScenarioData,int index),ret4 or native erase tailFF14B0; bounded178 row, optional scene cleanup',
     [(0xED6E0F,0xF3E6B0)],[]),
    ('LockCrew',0xF3BC30,'568b7110578b7c240c',[(0xF3BC79,8)],
     'thiscall(ScenarioData,int index,bool locked),ret8; bounded178 row byte20, no scene writes',[],[]),
    ('SetCrewCapacity',0xF3BB60,'8b4110568b74240889b038020000',[(0xF3BB77,4)],
     'thiscall(ScenarioData,int count),ret4; resource238 and posse manager18',[(0xED6E2A,0xF3BB60)],[]),
    ('MarkerCtor',0xF2A960,'8bc1c700ffffffff',[(0xF2AAC9,0)],
     'thiscall(storage234)->marker,ret; initializes owning inline marker-act vector28/2C/30 and detached placement IDFFFFFFFF at228',
     [(0xF44276,0xF2A960)],[(0xF2A9DF,'89503c'),(0xF2A9E5,'894828')]),
    ('MarkerDtor',0xDFC580,'568bf18b860c020000',[(0xDFC5C4,0)],
     'thiscall(marker),ret; destroys owning path/act storage, never frees marker storage itself',
     [(0xF44291,0xDFC580)],[]),
    ('ResizeMarkerActs',0xF42D90,'83ec345355568bf1',[(0xF42E06,4),(0xF42E1E,4)],
     'thiscall(marker+28,uint32 count),ret4; constructs/destroys native34 act rows, up to eight for editor previews',
     [(0xF4437C,0xF42D90)],[]),
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
    ('CurrentOrbitTarget',
 11600224,
 'b80100000084056cbd6701',
 [(11600304, 0)],
 'thiscall(editor orbit camera)->borrowed static Vec3*,ret; current direction54/58/5C times radius28, copied before '
 'any later native call',
 [],
 [(11600243, 'f30f104128'), (11600248, 'f30f104954'), (11600253, 'f30f105158'), (11600258, 'f30f10595c')]),
    ('CurrentOrbitScalars',
 11596192,
 'd981540100008b4424048b542408',
 [(11596228, 12)],
 'thiscall(editor orbit camera,float*yaw,float*pitch,float*distance),retC; actual rendered scalar values154/130/10C; '
 'distinct from desired getterB0F170',
 [],
 [(11596192, 'd98154010000'),
  (11596208, 'd98130010000'),
  (11596220, 'd9810c010000'),
  (11613253, 'f30f10830c010000'),
  (11613359, 'f30f108330010000'),
  (11613385, 'd98354010000'),
  (11613503, '8d8bac000000'),
  (11613709, 'f30f105b28'),
  (11613746, 'f30f594354')]),
    ('SetOrbitScalarsInstant',
 11601600,
 '83ec10f30f104424140f57c9',
 [(11601919, 12)],
 'thiscall(editor orbit camera,float yaw,float pitch,float distance),retC; native instant current/desired setter and '
 'interpolation cleanup, native pitch2AC/2B0 clamp and distance>=0.1',
 [(15720012, 11601600)],
 [(11601689, 'f30f1081ac020000'), (11601703, 'f30f1081b0020000'), (11601909, 'c6810403000001')]),
]


def verify(pe):
    if pe.sha256 != SHA or pe.image_base != 0x400000:
        raise ValueError('unsupported native executable: ' + pe.sha256)
    # Native camera identity/current-view restoration, separately cross-checked
    # against the editor initialize/cleanup and final camera transform paths.
    camera_known=[('SimCamera_LookAt', 11611408, '83ec2055568bf1', 11611844, 12), ('IGameModeManager_Get', 6806736, 'a1b0d85f01', 6806741, 0), ('CameraByID', 8152992, '83ec0856578bf9', 8153051, 4), ('OrbitCameraCast', 11595952, '8bc18b4c240481f9ed661901', 11595980, 4), ('ActiveCamera', 8151616, '8b81a800000085c0', 8151635, 0)]
    camera_calls=[(15728762, 15719872), (15720012, 11601600), (15720378, 11611408), (15720392, 11614960), (15720407, 11600928)]
    camera_fields=[(11600243, 'f30f104128'), (11600248, 'f30f104954'), (11600253, 'f30f105158'), (11600258, 'f30f10595c'), (11596192, 'd98154010000'), (11596208, 'd98130010000'), (11596220, 'd9810c010000'), (11613253, 'f30f10830c010000'), (11613359, 'f30f108330010000'), (11613385, 'd98354010000'), (11613503, '8d8bac000000'), (11613709, 'f30f105b28'), (11613746, 'f30f594354'), (11601689, 'f30f1081ac020000'), (11601703, 'f30f1081b0020000'), (11601909, 'c6810403000001'), (15719902, '8b4250'), (15719911, '8b4240'), (15719933, '8b420c'), (15719936, '68ed661901'), (15719914, '6801b8eb00'), (15743821, '8b4250'), (15743830, '8b4238'), (15743848, '8b420c'), (15743851, '68ed661901'), (16056077, '8b1089511c')]
    for name,address,prefix,ret,count in camera_known:
        raw=bytes.fromhex(prefix)
        returned=b'\xc3' if not count else b'\xc2'+struct.pack('<H',count)
        if pe.read(address,len(raw))!=raw or pe.read(ret,len(returned))!=returned:
            raise ValueError('Native editor camera ABI mismatch: '+name)
    for site,target in camera_calls:
        raw=pe.read(site,5)
        if raw[0]!=0xE8 or site+5+struct.unpack('<i',raw[1:])[0]!=target:
            raise ValueError('Native editor camera call mismatch '+hex(site))
    for site,encoded in camera_fields:
        raw=bytes.fromhex(encoded)
        if pe.read(site,len(raw))!=raw:
            raise ValueError('Native editor current-camera field mismatch '+hex(site))
    # Deferred Terraform controls use copied primitive values and re-resolve the
    # live FloraUI/card through the verified display ownership chain.
    for site, encoded in [(0xED5FAC,'897e28'),(0xEE9090,'c70650ab4801'),
                          (0xEE8A3D,'897e18'),(0xEE7CC5,'c70698ab4801'),
                          (0xEE8C92,'8b8b68010000'),(0xEE917C,'8b4918'),
                          (0x50D5C5,'8b41042b02'),(0x473A24,'8b41042b02'),
                          (0xF34393,'e808925dff'),(0xF343A5,'e856f653ff')]:
        if pe.read(site,len(bytes.fromhex(encoded)))!=bytes.fromhex(encoded):
            raise ValueError('Deferred Terraform control layout mismatch '+hex(site))
    # Native active reference selectors explicitly choose custom rules, or the
    # matching standard pickup/give/track selector. Retained inactive targets
    # are authored bytes but cannot block loading a valid native adventure.
    for site, encoded in [(0xED8394,'80b8c804000001'),(0xED83ED,'83b8b004000003'),(0xED83F6,'8b80bc040000'),
                          (0xED8404,'80b8c804000001'),(0xED845D,'83b8b404000003'),(0xED8466,'8b80c0040000'),
                          (0xED8474,'80b8c804000001'),(0xED84CD,'83b8b804000005'),(0xED84D6,'8b80c4040000')]:
        if pe.read(site,len(bytes.fromhex(encoded)))!=bytes.fromhex(encoded):
            raise ValueError('Active cast behavior reference selector mismatch '+hex(site))
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
    return [dict(name='SporeCoop::Editor::TerraformUIVTable',address=0x148AB50,
                 note='primary TerraformUI vtable; native display28 owns it, current FloraUI at18'),
            dict(name='SporeCoop::Editor::FloraUIVTable',address=0x148AB98,
                 note='primary FloraUI vtable; rootIWindow168 and IWinProc8; re-resolve current card before deferred mutation')] + [{'name':'SporeCoop::Editor::'+n,'address':a,'note':s}
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
        installed_map=Path(__file__).resolve().parents[2]/'data/symbols/overrides_editor_dc04aee5.sym'
        if installed_map.exists() and '# exe_sha256='+SHA not in installed_map.read_text(encoding='utf-8').splitlines()[:10]:
            raise ValueError('native editor map has no exact executable hash in the loader first ten lines')
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
