#!/usr/bin/env python3
"""Verify the reviewed dc04 build addresses before generating its restricted symbol map.

This is an evidence manifest for one executable, not an offset-based address porter.
Each entry records bytes and return sites inspected in the installed March 2017
executable. Unreviewed old GOG overrides deliberately remain absent.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

from pe import PE

EXE_SHA256 = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
UPSTREAM_COMMIT = '69b27b5b407a2c76e1e701ba27630a77a9ce5d01'


def fn(name, address, prefix, returns, note, calls=()):
    return dict(name=name, address=address, prefix=prefix, returns=returns, note=note,
                calls=calls)


# Return pairs are absolute return-instruction VA and stack bytes popped (0 = C3).
# Source identity comes from upstream ModAPI plus the behavioral checks in the notes.
FUNCTIONS = [
    fn('App::cAppSystem::PreInit', 0x7E8C40, '6aff64a10000000068ce6d2101', [(0x7E93C3, 8)],
       'thiscall(self,int,int), ret8; SEH prologue and command-line initialization'),
    fn('App::cAppSystem::Init', 0x7E6850, '6aff68b26a210164a100000000', [(0x7E7017, 4), (0x7E714F, 4)],
       'thiscall(self,int), ret4; application subsystem initialization'),
    fn('App::cSporeApp::Init', 0xF47E90, '81ec8c000000568bf1', [(0xF47EB0, 4), (0xF4813B, 4), (0xF482AB, 4), (0xF4880A, 4)],
       'thiscall(self,int), ret4; creates AppSystem, references shutdown lifecycle'),
    fn('App::cSporeApp::Shutdown', 0xF475A0, '53568bf18b4e2033db885e0c', [(0xF477C2, 0)],
       'thiscall(self), ret; posts shutdown notifications via IMessageManager', [(0xF475C9, 0x67DC80)]),
    fn('App::IGameModeManager::Get', 0x67DCD0, 'a1b0d85f01c3', [(0x67DCD5, 0)],
       'cdecl(), mov eax,[0x15FD8B0]; ret'),
    fn('App::cGameModeManager::SwitchToIndex', 0x7D8670, '64a1000000006aff6823602101506489', [(0x7D87F2, 4)],
       'thiscall(manager,index) -> void, ret4; registry+0x14 entries0x18 bytes: mode+0 ID+4; OnExit then activeIndex+0x28 then OnEnter'),
    fn('App::cGameModeManager::SetActiveMode', 0x7D8CF0, '8b51182b5114b8abaaaa2af7eac1fa02', [(0x7D8D30, 4), (0x7D8D3D, 4)],
       'thiscall(manager,modeID) -> bool AL, ret4; finds registry ID then calls SwitchToIndex; false if ID absent',
       [(0x7D8D34, 0x7D8670)]),
    fn('App::ICheatManager::Get', 0x67DDE0, 'a1a4d85f01c3', [(0x67DDE5, 0)],
       'cdecl(), mov eax,[0x15FD8A4]; ret'),
    fn('App::IMessageManager::Get', 0x67DC80, 'a190d85f01c3', [(0x67DC85, 0)],
       'cdecl(), mov eax,[0x15FD890]; ret'),
    fn('App::IStateManager::Get', 0x67DCE0, 'a1b4d85f01c3', [(0x67DCE5, 0)],
       'cdecl()->stateManager, singleton15FD8B4; native quick-play DFBFAF and results Done EEFFD0'),
    fn('App::cStateManager::SwitchToState', 0x7E3BF0, '64a1000000006aff68d0662101506489', [(0x7E3E31, 4)],
       'thiscall(stateManager,uint32 stateID)->bool AL, ret4; current state+88; same-state early return7E3C1C; native quick-play uses21851EBE and Done02CCDA9C',
       [(0x7E3CDA, 0x7E28C0), (0x7E3CF4, 0x7E3150), (0x7E3D2D, 0x7E2D90)]),
    fn('App::Property::GetValueInt32', 0x41E950, '558bec83ec08894df88b45f80fb74812', [(0x41E9B0, 0)],
       'thiscall(property)->int32 pointer, ret; type0x09/0x10 and flags0x30 choose inline or pointer data; native captain XP F15859/F164DE'),
    fn('SporeCoop::Adventure::LoadAssetProperties', 0x5BF1E0, 'e80bec0b0085c074338b4c24048b5108',
       [(0x5BF21B, 0), (0x5BF21E, 0)],
       'cdecl(ResourceKey*,PropertyList**)->bool, ret; intrusive output; transforms group=(group&FFFFE5FF)|E500; native results captain property resource',
       [(0x5BF1E0, 0x67DDF0)]),
    fn('ArgScript::PrintF', 0x8410D0, '64a1000000008b4c24046aff', [(0x84115B, 0), (0x8411CC, 0)],
       'cdecl(stream,format,...), ret; upstream Console_PrintF, stream formatting and write calls'),
    fn('Simulator::cGameNounManager::Get', 0xB3D400, 'a1e0ea6701c3', [(0xB3D405, 0)],
       'cdecl(), mov eax,[0x167EAE0]; ret; called by creature Create/Update'),
    fn('Simulator::cGameNounManager::CreateInstance', 0xB20BF0, '558be956b988c96701', [(0xB20CBB, 4)],
       'thiscall(nounMgr,noun), ret4; Create passes animal noun 0x18EB45E'),
    fn('Simulator::cGameNounManager::DestroyInstance', 0xB22560, '53568b74240cc6462001', [(0xB225D4, 4)],
       'thiscall(nounMgr,object), ret4; marks object+0x20 and releases registry entry'),
    fn('Simulator::cGameNounManager::SetAvatar', 0xB1FCA0, '538bd98b4354565785c0', [(0xB1FCEE, 4)],
       'thiscall(nounMgr,creature), ret4; changes avatar bit0x200 at creature+0xB58'),
    fn('Simulator::cGameNounManager::CreateHerd', 0xB23920, '538b5c240c33c0578bf9', [(0xB23A13, 0x18)],
       'thiscall(pos,species,size,owned,personality,nest), ret0x18; noun0x1BE418E, calls SetEnabled',
       [(0xB23938, 0xB20BF0), (0xB23A09, 0xC6AE80)]),
    fn('Simulator::cCreatureAnimal::Create', 0xC09B40, '83ec4853555657', [(0xC09C96, 0), (0xC0A0FA, 0)],
       'cdecl(pos,species,age,herd,isAvatar,reuse); ret; noun0x18EB45E and SetAvatar path',
       [(0xC09B95, 0xB3D400), (0xC09BA1, 0xB20BF0)]),
    fn('Simulator::cCreatureAnimal::NPCTickAI', 0xC09570, '83ec48568bf18b864c0b0000', [(0xC09B37, 4)],
       'thiscall(creature,float), ret4; called by Update when not avatar'),
    fn('Simulator::cCreatureAnimal::Update', 0xC0AE30, '568bf183be7416000000', [(0xC0AE56, 4), (0xC0B0E3, 4)],
       'thiscall(creature,int dt), ret4; herd+0x1674 check and NPCTickAI dispatch',
       [(0xC0AF63, 0xC09570)]),
    fn('Simulator::cCreatureAnimal::TakeDamage', 0xC075E0, '83ec6c538bd9', [(0xC077B8, 0x14), (0xC07803, 0x14), (0xC08205, 0x14)],
       'thiscall(creature+0x5A8,float,pol,type,dir,attacker), ret0x14; animal combatant vtable, not upstream misnamed base BFCF10',
       [(0xC07825, 0xC1A520)]),
    fn('Simulator::cCreatureBase::TakeDamage', 0xC1A520, '83ec1856578bf98b8758faffff8b502c', [(0xC1AB5C, 0x14), (0xC1AC1E, 0x14)],
       'thiscall(creature+0x5A8,float,pol,type,dir,attacker), ret0x14; animal delegates here, then calls base combatant',
       [(0xC1A982, 0xBFCF10)]),
    fn('Simulator::cCombatant::TakeDamage', 0xBFCF10, '83ec48538b5c246055568bf1578b7e28', [(0xBFD16E, 0x14), (0xBFD1F2, 0x14)],
       'thiscall(combatant,float,attackerPoliticalID,intType,Vector3*dir,combatantAttacker), ret0x14; invincible+0x80, HP+0x38, type10 heal, generic death goals',
       [(0xBFD1DA, 0xF249B0)]),
    fn('Simulator::cCreatureAnimal::SetSpecies', 0xC03920, '538b5c241056578bf184db', [(0xC03AA2, 0xC)],
       'thiscall(creature,species,age,keepState), ret0xC; species/ability/model refresh'),
    fn('SporeCoop::Adventure::IsHostileToCreature', 0xC0B9C0, '568bf1e8f81bf3ff85c0742d8b4c2408',
       [(0xC0B9ED, 4), (0xC0B9F6, 4), (0xC0B9FC, 4)],
       'thiscall(creature,other), ret4 -> bool; active native relation2/5 predicate, false without relationship manager',
       [(0xC0B9C3, 0xB3D5C0), (0xC0B9DB, 0xBA40E0)]),
    fn('SporeCoop::Adventure::GetCreatureRelationship', 0xBA40E0, '83ec14535556578be9e82278fbff3d10',
       [(0xBA41CE, 0xC), (0xBA4235, 0xC), (0xBA424A, 0xC), (0xBA4259, 0xC), (0xBA437D, 0xC), (0xBA43BD, 0xC)],
       'thiscall(speciesRelationshipManager,ResourceKey*,creature1,creature2)->int, ret0xC; Scenario branch ignores key, picks non-avatar actor by flag0x200, then authored class/act relationship; null class returns0',
       [(0xBA40E9, 0xB5B910), (0xBA41D9, 0xF3BF80), (0xBA41F7, 0xB18470), (0xBA41FD, 0xBA29F0)]),
    fn('SporeCoop::Adventure::CreatureTarget', 0xC0EFD0, '8b897c0e000085c9740d8b018b505c68',
       [(0xC0EFE6, 0), (0xC0EFE9, 0)],
       'thiscall(creature)->targetCreature, ret; target combatant+E7C virtual5C Cast CE9F6639; native social performer verifies reciprocal player target'),
    fn('SporeCoop::Adventure::CreatureSocialMode', 0xC0E800, '5155568bf133ed5785f60f84b2000000',
       [(0xC0E899, 0), (0xC0E8AD, 0), (0xC0E8C1, 0), (0xC0E970, 0), (0xC0E994, 0),
        (0xC0E9B1, 0), (0xC0E9CE, 0), (0xC0E9E4, 0), (0xC0EA09, 0)],
       'thiscall(creature)->int socialMode, ret; Scenario relation3->2,4->1,1/2/5->0,6->4 only with posse capacity and nonparty actor; ordinary native social eligibility',
       [(0xC0E8DF, 0xBA40E0), (0xC0E933, 0xD53980)]),
    fn('SporeCoop::Adventure::RefreshGoalHUD', 0xE16A50, '81ec84000000b8ac7b66018944242c89',
       [(0xE170C5, 4)],
       'thiscall(goalHUD,bool)->void, ret4; CountGoal F1AE8E and goal completion F198E9 pass ScenarioMode+6C->148 with false; rereads native goal counters/cards',
       [(0xE16AB8, 0xF1B890), (0xE16ADB, 0xF1B8E0), (0xE16BA2, 0xF19AA0)]),
    fn('SporeCoop::Adventure::SocialOrderTarget', 0xD998E0, '8b4424045685c0743c8b482c85c97435',
       [(0xD99924, 0), (0xD99928, 0)],
       'cdecl(behaviorOrder*)->liveCreatureAnimal, ret; order+2C object Cast D0036E08, targetable+135 and notdead/flag4; SocialPropose init uses it'),
    fn('SporeCoop::Adventure::SocialAbilityEligibility', 0xD1E720, '568b74240c85f675058d46045ec3578b',
       [(0xD1E72D, 0), (0xD1E776, 0), (0xD1E799, 0), (0xD1E7E1, 0), (0xD1E7E6, 0)],
       'cdecl(avatar,targetCreature)->int, plain ret; SocialRespond rejects state5 without native SocialPropose BT leaf; ready controller+0x1E4 permits response input',
       [(0xD1F8D0, 0xD1E720)]),
    fn('SporeCoop::Adventure::SocialProposeStart', 0xDA4360, '83ec18535556578b7c244085ff7418c7',
       [(0xDA48E6, 0), (0xDA4944, 0)],
       'cdecl(actor,double time,word4,word5,POD16*state,word7)->bool AL, ret; native BT init cleanup1C atBC943F; initializes state0/4/8/C and native shared decider/rainbow UI',
       [(0xDA439B, 0xBC9810), (0xDA43B9, 0xD998E0), (0xDA4454, 0xBCB5F0), (0xDA4463, 0xD9F740)]),
    fn('SporeCoop::Adventure::SocialProposeTick', 0xDA4950, '515356578b7c24148b874c0b00008b98',
       [(0xDA497F, 0), (0xDA4ADC, 0), (0xDA4B1E, 0), (0xDA4B69, 0), (0xDA4BBD, 0),
        (0xDA4C5A, 0), (0xDA4CA9, 0), (0xDA4DEB, 0), (0xDA4E14, 0), (0xDA4E2F, 0)],
       'cdecl(actor,double time,word4,word5,POD16*state,word7,float deltaSeconds)->bool AL, ret; native BT tick cleanup20; performer/shared-supporter animation and real rainbow success->OnBefriend',
       [(0xDA4970, 0xA74F90), (0xDA49A6, 0xC0EFD0), (0xDA49DD, 0xD9B9C0), (0xDA49FB, 0xD9BBD0)]),
    fn('SporeCoop::Adventure::SocialProposeEnd', 0xD9F7A0, '538b5c24088b834c0b0000558ba80006',
       [(0xD9F8E2, 0)],
       'cdecl(actor,double time,word4,word5,POD16*state), ret; native BT deactivate cleanup18; closes rainbow, native posse side effects, detaches shared controller and removes social orders',
       [(0xD9F7D0, 0xE319D0), (0xD9F7F5, 0xD542E0), (0xD9F89E, 0xBCB5A0)]),
    fn('SporeCoop::Adventure::TickBehaviorTree', 0xBC9160, '558bec83e4c081ecb401000053568b75',
       [(0xBC9769, 0), (0xBC9774, 0), (0xBC97DF, 0), (0xBC97F6, 0), (0xBC9801, 0)],
       'cdecl(nine stack words)->bool AL; float delta,double time,tree,actor,context,three state words; vehicle caller F39F69 passes actor fifth',
       [(0xBC918D, 0xBC8980)]),
    fn('SporeCoop::Adventure::TickVehicleWeapons', 0xC9F460, '56578b7c240c8bf129be800c00008b86', [(0xC9F4B2, 4)],
       'thiscall(vehicle,uint32 deltaGameTime), ret4; weapon counters C78/C80/C84; vehicle strategy F39EB6',
       [(0xC9F481, 0xC9F3E0)]),
    fn('SporeCoop::Adventure::TickVehicleTimers', 0xCA9850, '51568bf180becc0b00000057741d8d8e', [(0xCA9905, 0)],
       'thiscall(vehicle), ret; status timers BCC/BD1/BF8, events19/1B; tailcall CA8310; vehicle strategy F39EBD',
       [(0xCA9864, 0xBC3300), (0xCA988A, 0xBC3300), (0xCA98E6, 0xBC3300)]),
    fn('SporeCoop::Adventure::ProcessActor', 0xEF1850, 'd94424088b442404518b4970d91c2450', [(0xEF1865, 8)],
       'thiscall(scenarioMode,combatant,float deltaSeconds), ret8; mode+70 actor processor; vehicle F39F91 and creature C0C360 path',
       [(0xEF1860, 0xEBDBB0)]),
    fn('SporeCoop::Adventure::BuildingInventoryGive', 0xBCF600, '56578bf983bfec010000000f84870000',
       [(0xBCF695, 4), (0xBCF69C, 4)],
       'thiscall(building,itemGameData)->bool AL, ret4; inventory+1EC, native Give D84093; emits OnPickUp',
       [(0xBCF682, 0xF1AED0)]),
    fn('SporeCoop::Adventure::VehicleInventoryGive', 0xCA44B0, '56578bf983bf940d0000000f84870000',
       [(0xCA4545, 4), (0xCA4561, 4)],
       'thiscall(vehicle,itemGameData)->bool AL, ret4; inventory+D94, native Give D840DF; emits OnPickUp',
       [(0xCA4532, 0xF1AED0)]),
    fn('SporeCoop::Adventure::VehicleGiveConsumesItem', 0xC9F0B0, '568bf1578dbed80c00008bcfe85fc834',
       [(0xC9F0E5, 0), (0xC9F0EA, 0)],
       'thiscall(vehicle)->bool AL, ret; timerCD8/CF8, native Give D840B6 selects consume branch',
       [(0xC9F0BC, 0xFEB920), (0xC9F0C7, 0xBC3300)]),
    fn('SporeCoop::Adventure::PosseGet', 0xD53980, 'a1b8e5690185c075475050505068a4b7',
       [(0xD539BA, 0), (0xD539D0, 0)],
       'cdecl()->posseManager, ret; lazy singleton169E5B8, alloc58, native scenario caller F1D877',
       [(0xD53994, 0xF47240), (0xD539A2, 0xD538A0)]),
    fn('SporeCoop::Adventure::PosseRecruit', 0xD542E0, '81ecb00000005355568bd957895c2414',
       [(0xD5464B, 4), (0xD5465A, 4)],
       'thiscall(posseManager,creature)->bool AL, ret4; capacity/duplicate checks and native OnAlly callback',
       [(0xD543DE, 0xB23430), (0xD544C5, 0xF1D3E0)]),
    fn('SporeCoop::Adventure::ClassActRelationship', 0xBA29F0, '8b4c240485c90f84b60000008b442408',
       [(0xBA2A54, 0), (0xBA2A5A, 0), (0xBA2A94, 0), (0xBA2A9A, 0), (0xBA2AB1, 0), (0xBA2AB4, 0)],
       'cdecl(scenarioClass,act,optionalCreature)->int, ret; null creature checked BA2A14; authored team/stance/advanced actions'),
    fn('SporeCoop::Adventure::GiveAnimationFinished', 0xC12560, 'e9bbbeffff',
       [(0xC0E4B3, 4), (0xC0E4BA, 4), (0xC0E4C1, 4), (0xC0E4C7, 4)],
       'thiscall(creature,animationID)->bool AL; exact E9 tail to C0E420, ret4 in target; native Give D84006'),
    fn('SporeCoop::Adventure::TickContainerGive', 0xD83E90, '83ec2453558b6c24308b8d4c0b000056',
       [(0xD83F0B, 0), (0xD83FFF, 0), (0xD8410B, 0)],
       'cdecl(actor,double time,word4,word5,state,word7,float delta)->bool AL; eight stack words; native BT BC9477 cleanup20, actor1/state6/delta8',
       [(0xD83EAB, 0xBC9810), (0xD84006, 0xC12560)]),
    fn('SporeCoop::Adventure::GetBehaviorOrder', 0xBC9810, '8b81c00100008b91c4010000538b5c24',
       [(0xBC9830, 8), (0xBC9851, 8), (0xBC9868, 8)],
       'thiscall(orderList,maskLow,maskHigh)->order, ret8; native Give D83EAB uses behavior+B4C+8, mask0/40000, order+C==6'),
    fn('Simulator::cCreatureBase::PlayAnimation', 0xC12470, '535556578bf133ff39be540b0000', [(0xC124B8, 0xC), (0xC12547, 0xC)],
       'thiscall(creature,animID,block,disableBlend), ret0xC'),
    fn('Simulator::cCreatureBase::PlayAnimationTo', 0xC14B20, '8b44241056576a008bf9', [(0xC14B68, 0x10)],
       'thiscall(creature,animID,target,block,blend), ret0x10; calls PlayAnimation', [(0xC14B32, 0xC12470)]),
    fn('Simulator::cHerd::SetEnabled', 0xC6AE80, '8b81600100008a542404889118020000', [(0xC6AEC0, 4)],
       'thiscall(herd,bool), ret4; writes herd+0x218, called by CreateHerd'),
    fn('Editors::cSpeciesManager::Get', 0x401090, '558beca1240c5d015dc3', [(0x401099, 0)],
       'cdecl(), singleton0x15D0C24'),
    fn('Editors::cSpeciesManager::GetSpeciesProfile', 0x4DF5C0, '558bec81ec8c000000', [(0x4DF732, 4)],
       'thiscall(manager,ResourceKey*), ret4; species cache lookup and load'),
    fn('UTFWin::IWindowManager::Get', 0x67CA60, 'a14ccc5f01c3', [(0x67CA65, 0)],
       'cdecl(), singleton0x15FCC4C'),
    fn('UTFWin::IButton::Create', 0x9670A0, '8b44240885c07505', [(0x9670D4, 8), (0x9670D9, 8)],
       'stdcall(two args), ret8; UTFWin allocation0x888, button constructor', [(0x9670BA, 0x951230)]),
    fn('UTFWin::ITextEdit::Create', 0x98CAA0, '8b44240885c07505', [(0x98CAD4, 8), (0x98CAD9, 8)],
       'stdcall(two args), ret8; UTFWin allocation0x658, text edit constructor', [(0x98CABA, 0x951230)]),
    fn('UTFWin::UILayout::UILayout', 0x8100A0, '8bc133c9894804c70060834101', [(0x8100B9, 0)],
       'thiscall(layout), ret; vtable0x1418360 and zeroes +4..+0x14'),
    fn('UTFWin::UILayout::Load', 0x8121C0, '535556578bf1', [(0x81223A, 0xC), (0x812243, 0xC)],
       'thiscall(layout,key*,insert,group), ret0xC; LoadByName forwards here'),
    fn('UTFWin::UILayout::GetContainerWindow', 0x810610, '8b411485c074078b8088000000', [(0x81061D, 0), (0x810620, 0)],
       'thiscall(layout), ret; layout+0x14 then container+0x88'),
    fn('UTFWin::Image::GetImage', 0x806320, '83ec248b4424285356', [(0x806402, 0)],
       'cdecl(key*,ImagePtr*), ret; resource-backed image lookup'),
    fn('UTFWin::UTFWinObject::new_', 0x951230, '8b4424084883c807', [(0x951284, 0), (0x95128B, 0)],
       'cdecl(size,align,name,allocator), ret; used by button/text factories'),
    fn('UTFWin::GetAllocator', 0x951220, 'a1b4aa6601c3', [(0x951225, 0)],
       'cdecl(), allocator singleton0x166AAB4'),
    fn('UTFWin::SporeStdDrawable::SporeStdDrawable', 0x830A30, '83ec08535556578bf1', [(0x830B2B, 0)],
       'thiscall(drawable), ret; vtables141A978/141A960/141A920; info at +0x7C', [(0x830A57, 0x8308F0)]),
    fn('UTFWin::SporeStdDrawableImageInfo::SporeStdDrawableImageInfo', 0x8308F0, '0f57c0f30f100d40374501', [(0x8309C6, 0)],
       'thiscall(info), ret; vtables141A900/141A8F0 and initializes image states'),
    fn('Memory::operator_new_named', 0x9262B0, '56e88afcffff8b4c2410', [(0x9262E1, 0), (0x9262E5, 0)],
       'cdecl(size,name,0,0,0,0), ret; allocator allocation; ScenarioResource::Read calls this'),
    fn('Resource::DatabasePackedFile::ctor', 0x8DA2A0, '53568bf1c74604a01a3f01', [(0x8DA45A, 8)],
       'thiscall(db,path16*,allocator), ret8; vtable1408528, sets location via8D9CE0'),
    fn('Resource::GetDatabaseByID', 0x6B1F20, '518d442408508d4c240451', [(0x6B1F43, 0), (0x6B1F47, 0)],
       'cdecl(databaseID), ret; ID tree lookup at152FDC4'),
    fn('Resource::IResourceManager::Get', 0x8DE4B0, 'a1a07a6601c3', [(0x8DE4B5, 0)],
       'cdecl(), resource manager singleton1667AA0; App getter uses15FD894'),
    fn('Simulator::cScenarioPlayMode::Initialize', 0xF1F060, '83ec10538bd9', [(0xF1F282, 0)],
       'thiscall(playMode), ret; Scenario+0x74 resource, initializes act and goals', [(0xF1F0AC, 0xF1EE70)]),
    fn('Simulator::cScenarioPlayMode::CheckGoalProgress', 0xF1F4E0, '558bec83e4f883ec5853555657', [(0xF1F932, 0)],
       'thiscall(playMode), ret; active-state3 guard and CompleteAct call', [(0xF1F926, 0xF1F290)]),
    fn('Simulator::cScenarioPlayMode::CompleteAct', 0xF1F290, '515355568bf18b4e682b4e64', [(0xF1F35F, 0)],
       'thiscall(playMode), ret; current goal vector, native SetState/SetCurrentAct; tailcalls UpdateGoals'),
    fn('Simulator::cScenarioPlayMode::JumpToAct', 0xF1F3C0, '51568bf183be9000000003', [(0xF1F4D0, 4)],
       'thiscall(playMode,act), ret4; resets scenario state, not a passive setter'),
    fn('Simulator::cScenarioPlayMode::SetCurrentAct', 0xF1EE70, '83ec0c56578b7c24188bf157', [(0xF1F059, 8)],
       'thiscall(playMode,act,bool), ret8; writes current act+0xB8'),
    fn('Simulator::cScenarioPlayMode::SetState', 0xF1A9C0, '8b442404568bf1578bbe90000000', [(0xF1AAB4, 4), (0xF1ABDC, 4)],
       'thiscall(playMode,state), ret4; current play state at+0x90 and cinematic handling'),
    fn('Simulator::cScenarioPlayMode::Update', 0xF1F960, '83ec0c53568bf1', [(0xF1FF5A, 4)],
       'thiscall(playMode,int dt), ret4; calls CheckGoalProgress', [(0xF1F9B1, 0xF1F4E0)]),
    fn('Simulator::cScenarioPlayMode::UpdateGoals', 0xF1C390, '83ec28a1a47a6c0153555657', [(0xF1C55D, 0), (0xF1C569, 0)],
       'thiscall(playMode), ret; resource+0x74/current-goals+0x64 and validation'),
    fn('Simulator::cScenarioResource::Read', 0xF2FC40, '83ec3853568b7424448b068b5024', [(0xF2FE97, 4), (0xF2FF69, 4)],
       'thiscall(resource,stream), ret4; stream parse and embedded DBPF allocation', [(0xF2FD6D, 0x8DA2A0)]),
    fn('Simulator::cScenarioData::CreateObjectInstance', 0xEEDD40, '83ec20538b5c2428555657', [(0xEEE44A, 0)],
       'cdecl(key,pos,rot,scale,isAvatar,class,marker,acts,act); ret; scenario-aware object creation'),
    fn('Simulator::cScenarioData::GetClass', 0xF3E740, '53568b71100fb686082c0000', [(0xF3E78D, 4), (0xF3E795, 4)],
       'thiscall(data,classID), ret4; reads resource class map+0x2BF4'),
    fn('Simulator::cScenarioData::GetCurrentClassAct', 0xF3F7F0, '8b4424048b501c5652', [(0xF3F825, 4), (0xF3F83D, 4)],
       'thiscall(data,object), ret4; reads object+0x1C and Scenario+0xCC/+0x78'),
    fn('Simulator::cScenarioData::GetObjectForMarker', 0xF3D620, '8b44240483ec083b818c000000', [(0xF3D63E, 4), (0xF3D676, 4)],
       'thiscall(data,marker), ret4; avatar marker returns native avatar; upstream old generated alias was reversed'),
    fn('Simulator::cScenarioData::GetMarkerForObject', 0xF3DB10, '83ec08578bf9', [(0xF3DB32, 4), (0xF3DB7D, 4)],
       'thiscall(data,object), ret4; native avatar returns data+0x8C, otherwise map lookup'),
    fn('SporeCoop::Adventure::ActorMapFind32', 0x645E40, '8b44240883ec08568b3033d2578b7908', [(0x645E90, 8)],
       'thiscall(map,Iterator*out,key32*), ret8; iterator is node and bucket pointer, node has key/value/next'),
    fn('SporeCoop::Adventure::EraseObjectMarker', 0x1068390, '8b5424088b420853568b74240c578b7c', [(0x10683DC, 0xC), (0x1068406, 0xC)],
       'thiscall(map,Iterator*out,node,bucket), ret0xC; reverse-map erase frees node without object Release'),
    fn('SporeCoop::Adventure::EraseMarkerObject', 0xADC8F0, '8b5424088b42085355568b742410578b', [(0xADC966, 0xC)],
       'thiscall(map,Iterator*out,node,bucket), ret0xC; forward-map erase releases object via vtable+4 then frees node'),
    fn('SporeCoop::Adventure::ObjectMarkerSlot', 0xB475C0, '83ec1456578b7c2420578d44240c508b', [(0xB47617, 4)],
       'thiscall(data+0x90,objectKey*), ret4 -> uint32*; find-or-insert reverse marker slot', [(0xB475D1, 0x645E40)]),
    fn('SporeCoop::Adventure::MarkerObjectSlot', 0xB70020, '83ec1456578b7c2420578d44240c508b', [(0xB7004D, 4), (0xB70096, 4)],
       'thiscall(data+0xB0,markerID*), ret4 -> objectRef*; find-or-insert forward intrusive-reference slot', [(0xB70031, 0x645E40)]),
    fn('SporeCoop::Adventure::AssignObjectReference', 0xB5FAB0, '538b5c2408568bf1578b3e3bdf741b85', [(0xB5FADF, 4)],
       'thiscall(objectRefSlot,object), ret4; AddRef new object then Release previous object'),
    fn('SporeCoop::Adventure::ApplyMarkerState', 0xF3D930, '83ec0c5355578b7c241c8bd93bbb8c00', [(0xF3DB06, 8)],
       'thiscall(data,marker,bool), ret8; native placement transform and class-specific state; GetObjectForMarker lookup',
       [(0xF3D997, 0xF3D620)]),
    fn('SporeCoop::Adventure::ApplyMarkerVisibility', 0xF40A50, '8b44240485c074628b005650e8dfdcff', [(0xF40ABA, 8)],
       'thiscall(data,marker,object), ret8; spatial+0x78 visibility from current class-act enabled byte', [(0xF40A5C, 0xF3E740)]),
    fn('SporeCoop::Adventure::RemoveMarkerObject', 0xF3FC30, '8b44240483ec0c8338fe535556578bf9', [(0xF3FD37, 4), (0xF3FE05, 4)],
       'thiscall(data,marker), ret4; native DestroyInstance and both map erasures; avatar branch also frees marker, reject avatar',
       [(0xF3FDBB, 0xB22560), (0xF3FDD5, 0xADC8F0), (0xF3FDF9, 0x1068390)]),
    fn('Simulator::cScenarioClass::IsConsumible', 0xF25D50, '8b417083b8a4040000ff0f95c0c3', [(0xF25D5D, 0)],
       'thiscall(class), ret -> bool; first act+0x4A4 is not -1; selects special marker powerup factory'),
    fn('SporeCoop::Adventure::CreateMarkerPowerup', 0xF237B0, '515556576a0168c59d1c816818c34801', [(0xF238BC, 0)],
       'cdecl(marker), ret -> object; native special consumible creation path used by internal marker factory',
       [(0xF23811, 0xF212C0)]),
    fn('Simulator::cScenarioData::Initialize', 0xF44690, 'a130055b0183ec0c53555657', [(0xF448D3, 4)],
       'thiscall(data,bool), ret4; native scenario resource/data initialization'),
    fn('Simulator::cScenarioResource::Write', 0xF25E00, '83ec1453568b742420576a016a018d44', [(0xF260A1, 4)],
       'thiscall(resource,stream), ret4; embedded DBPF serialization, reverse of Read', [(0xF25E8A, 0x8DA2A0)]),
    fn('Simulator::Scenario::ResolveLocalServerAsset', 0xF257B0, '83ec0c535657c744240c00000000', [(0xF25821, 0), (0xF2582A, 0)],
       'cdecl(uint64 serverID,ResourceKey* out), ret; local lookup only, no network download',
       [(0xF257CE, 0x67CAF0), (0xF257E7, 0x54E460), (0xF257F4, 0x54E740)]),
    fn('App::cScenarioMode::OnEnter', 0xEF24C0, '568bf18d4e20', [(0xEF2550, 0)],
       'thiscall(mode), ret; ctorEF0210 installs vtable148AFD8, slot0x18 points here; initializes GameInput+0x20'),
    fn('Simulator::cScenarioData::LoadScenarioResource', 0xF44A90, '8b54240483ec3453558be98b4a08', [(0xF44C5B, 4)],
       'thiscall(data,key*), ret4; validates key type then resource-manager load'),
    fn('Simulator::cScenarioData::Load', 0xF464D0, '83ec145355568b74242457568be9', [(0xF4672C, 4)],
       'thiscall(data,key*), ret4; loads scenario then creates native actor/act state', [(0xF464E0, 0xF44A90)]),
    fn('SporeCoop::Adventure::CaptainWizardStart', 0xdfe1f0, '83ec2c535556578bf1e8b2b5', [(14672929, 0)],
       'thiscall(wizard), ret; native choose-create-name-review flow; offline fallback may preload Welcome'),
    fn('SporeCoop::Adventure::CaptainWizardSelectAdventure', 0xe00070, '515356578bf9c744240c0000', [(14680422, 4)],
       'thiscall(wizard,key*), ret4; loads adventure metadata; native async stage8 resumes stage1'),
    fn('SporeCoop::Adventure::CaptainWizardNext', 0xdf9ec0, '568bf18b4e102b4e0cb8398e', [(14655358, 0), (14655364, 0)],
       'thiscall(wizard), ret; advances native step vector and shows naming/review panel'),
    fn('SporeCoop::Adventure::CaptainWizardShow', 0xdfaaf0, 'a1a47a6c0183ec18558be98b', [(14658662, 4)],
       'thiscall(wizard,bool), ret4; native quickplay controls visibility'),
    fn('SporeCoop::Adventure::CaptainWizardReset', 0xdf9aa0, '56578bf1e807fdffff6a018d', [(14654195, 0)],
       'thiscall(wizard), ret; hides step panels and returns stage0'),
    fn('SporeCoop::Adventure::FrontEndRequestMode', 0xde8680, '807c240800568bf1746c8b86', [(14583518, 8), (14583539, 8), (14583553, 8)],
       'thiscall(frontend,int,bool), ret8; queues component13 captain wizard or component2 menu'),
    fn('SporeCoop::Adventure::CaptainWizardSave', 0xDFBD40, '51535556578bf1', [(0xDFBFC7, 0)],
       'thiscall(wizard), ret; converts raw creature to captain4178B8E8, saves name, fills request then switches application state'),
    fn('SporeCoop::Adventure::AssetBrowserShow', 0x64A370, 'a118d95f0183ec1853568bf1', [(0x64A8FB, 12)],
       'thiscall(browser,bool show,bool,bool notifyCancel), ret12; sets browser+0x1C visibility and closes native layout; Show caller64A934 passes1,0,0'),
    fn('SporeCoop::Adventure::GalaxyViewReset', 0xDE0840, '83ec40c744243000000000', [(0xDE08AA, 0)],
       'no stack arguments, ECX unused, ret; sets view globals and sends07BE7DF5; native quickplay Back callDFE699'),
    fn('SporeCoop::Adventure::GalaxyIdleEffect', 0xDE52E0, '807c240400578bf9', [(0xDE536A, 4), (0xDE536F, 4)],
       'thiscall(frontend,bool), ret4 or tailcall effect+8; native quickplay BackDFE6B5 passes true'),
    fn('UTFWin::UILayout::SetVisible', 0x810630, '8b491485c97405e974fdffff', [(0x81063E, 4), (0x810423, 4)],
       'thiscall(layout,bool), ret4 or tailcall8103B0; quickplay FinishDFAE51 hides frontend layout+144 with false'),
]

GLOBALS = [
    dict(name='App::cScenarioMode::ptr', address=0x16C7AA4,
         references=[(0xF1F072, 'a1a47a6c01'), (0xF3F804, 'a1a47a6c01')],
         note='global cScenarioMode*; Initialize reads data+0x74; GetCurrentClassAct checks mode+0xCC and play+0x78'),
    dict(name='Resource::ResourceManager_ptr', address=0x15FD894,
         references=[(0x67DC90, 'a194d85f01c3')],
         note='App resource manager singleton, getter67DC90; RegisterDatabase uses vtable+0x50'),
    dict(name='SporeCoop::Adventure::FrontEnd_ptr', address=0x16a1344, references=[(0xdfae84, 'a144136a01')], note='global native front-end manager pointer; component registry at +0x124, quickplay component ID13'),
    dict(name='SporeCoop::Adventure::CaptainWizard_vtable', address=0x147eda0, references=[(0xdfb9a4, 'c706a0ed4701')], note='native quickplay controller primary vtable; object size0x178, secondary browser listener at +8'),
]


def verify(pe):
    if pe.sha256 != EXE_SHA256:
        raise ValueError('unsupported executable SHA-256: ' + pe.sha256)
    if pe.image_base != 0x400000:
        raise ValueError('unexpected preferred image base')
    evidence = []
    for entry in FUNCTIONS:
        va = entry['address']
        if not pe.in_text(va):
            raise ValueError(entry['name'] + ': function outside .text')
        expected = bytes.fromhex(entry['prefix'])
        if pe.read(va, len(expected)) != expected:
            raise ValueError(entry['name'] + ': prologue mismatch')
        for site, popped in entry['returns']:
            expected = b'\xc3' if not popped else b'\xc2' + struct.pack('<H', popped)
            if pe.read(site, len(expected)) != expected:
                raise ValueError(entry['name'] + ': return mismatch at ' + hex(site))
        for site, target in entry['calls']:
            raw = pe.read(site, 5)
            if not raw or raw[0] != 0xE8 or site + 5 + struct.unpack('<i', raw[1:])[0] != target:
                raise ValueError(entry['name'] + ': call edge mismatch at ' + hex(site))
        evidence.append(entry)
    for entry in GLOBALS:
        va = entry['address']
        sections = [s for s in pe.sections if s['va'] <= va < s['va'] + s['vsize']]
        if not sections or sections[0]['chars'] & 0x20000000:
            raise ValueError(entry['name'] + ': global is not in a data section')
        for site, encoded in entry['references']:
            raw = bytes.fromhex(encoded)
            if pe.read(site, len(raw)) != raw or struct.pack('<I', va) not in raw:
                raise ValueError(entry['name'] + ': global reference mismatch')
        evidence.append(entry)
    return evidence


def map_text(evidence):
    lines = ['# SporeKit restricted March2017 symbol database (static verification).',
             '# exe_sha256=' + EXE_SHA256,
             '# source=Spore-Community/Spore-ModAPI@' + UPSTREAM_COMMIT,
             '# evidence=tools/addrport/verify_dc04.py; docs/re/build-dc04.md',
             '# Only reviewed bootstrap, native UI, creature/captain and scenario entries are enabled.']
    for e in sorted(evidence, key=lambda x: x['name']):
        lines.append(f"{e['name']}=0x{e['address']:08X} ; verified static: {e['note']}")
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe')
    parser.add_argument('--write-map', type=Path)
    parser.add_argument('--check-map', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        pe = PE(args.exe)
        evidence = verify(pe)
        rendered = map_text(evidence)
        if args.check_map and args.check_map.read_text(encoding='utf-8') != rendered:
            raise ValueError('symbol map differs from reviewed evidence manifest')
        if args.write_map:
            args.write_map.write_text(rendered, encoding='utf-8', newline='\n')
        if args.report:
            args.report.write_text(json.dumps(dict(sha256=pe.sha256, symbols=evidence), indent=2) + '\n', encoding='utf-8')
        print(f'verified {len(FUNCTIONS)} functions and {len(GLOBALS)} globals for {pe.sha256}')
        return 0
    except (OSError, ValueError, AssertionError) as exc:
        print('verification failed: ' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
