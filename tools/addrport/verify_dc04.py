#!/usr/bin/env python3
"""Verify the reviewed dc04 build addresses before generating its GA symbol override.

This is an evidence manifest for one executable, not an offset-based address porter.
Each entry records bytes and return sites inspected in the installed March 2017
executable. Its GA override augments the full upstream map without changing its build hashes.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import sys

from pe import PE

EXE_SHA256 = 'dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37'
UPSTREAM_COMMIT = '69b27b5b407a2c76e1e701ba27630a77a9ce5d01'


def fn(name, address, prefix, returns, note, calls=(), vslots=(), checks=()):
    return dict(name=name, address=address, prefix=prefix, returns=returns, note=note,
                calls=calls, vslots=vslots, checks=checks)


# Return pairs are absolute return-instruction VA and stack bytes popped (0 = C3).
# Source identity comes from upstream ModAPI plus the behavioral checks in the notes.
FUNCTIONS = [
    fn('SporeCoop::BT_ValidTarget', 0xd993d0, '568b74240885f6743e80be5e0b000000',
       [[14259222, 0], [14259226, 0]],
       'cdecl(complete creature*)->same pointer/null,ret; rejects null/dead/untargetable/retired and cCreatureAnimal B58 bit4, does not require native avatar flag or scenario class',
       calls=[[14259198, 11304976]], vslots=[], checks=[[14259161, '80be5e0b000000753580be3501000000742c8b068b502c8bceffd284c075'], [14259206, '85c07409f686580b0000047504']]),
    fn('SporeCoop::Adventure::CreatureAwareness', 0xc0e650, '51568bf1e8b7d2f4ff3d104c65017546',
       [[12641957, 0], [12641997, 0], [12642006, 0]],
       'thiscall(complete creature)->ST0 float,ret; Scenario play modeCC2 returns current authored classAct+484 for non-avatar, otherwise phenotypeE84+34C or17.0',
       calls=[[12621269, 12641872], [12641940, 12601520]], vslots=[], checks=[[12641924, '8b8e580b0000c1e909f6c10175148bc8e81762ffff85c07409d98084040000'], [12641949, 'd980840400005e']]),
    fn('SporeCoop::Adventure::CreatureViewAngle', 0xc0c170, '518b81840e000085c0741bf30f108050',
       [[12632469, 0], [12632477, 0]],
       'thiscall(complete creature)->ST0 float,ret; nonnegative phenotypeE84+350 full FOV degrees or120 default',
       calls=[[12621284, 12632432]], vslots=[], checks=[[12621289, 'd80d88cf4501'], [21352328, '35fa0e3c']]),
    fn('SporeCoop::Adventure::CreaturePerceptionMultiplier', 0xd38a30, '51f30f100540374501568b7424108bce',
       [[13863567, 0], [13863578, 0], [13863589, 0], [13863600, 0], [13863611, 0], [13863622, 0], [13863633, 0], [13863641, 0], [13863645, 0]],
       'cdecl(int kind,complete creature)->ST0 float,ret; kinds1 hearing and2 sight share nonnegative phenotypeE84+358 or1.0',
       calls=[[12621243, 13863472], [12621255, 13863472]], vslots=[], checks=[[13863648, '8b8ad3008b8ad3008b8ad300'], [13863503, '8bcee8aa38edfff30f1080580300000f57c90f2fc8f30f11442404760ef30f100540374501f30f11442404']]),
    fn('SporeCoop::Adventure::CreatureCanHear', 0xc0d6c0, '0f57d2578b7c2408f30f108f800b0000',
       [[12637989, 12], [12637995, 12], [12638124, 12], [12638130, 12]],
       'thiscall(observerCreature,targetCreature,float hearingMultiplier,float edgeDistance)->boolAL,retC; positive target+B80 noise, observer phenotypeE84+354 or10.0, no new target/order writes',
       calls=[[12621581, 12637888]], vslots=[], checks=[[12637913, '8b81840e000085c0740df30f1080540300000f2fc27308f30f1005b4c85001']]),
    fn('SporeCoop::Adventure::NPCPerceptionCleanup', 0xc028f0, '8b442404568b70088b168b422c8bceff',
       [[12593447, 0], [12593454, 0]],
       'cdecl(perceptionRecord*)->boolAL,ret; true removes retired/avatar-bit0x200/untargetable/flag0x10 record; complete target+8, native owning ref+10. Only exact scoped cleanup callers may extend remote-party removal',
       calls=[], vslots=[], checks=[[12622002, '8b86281100008b8e241100005068f028c0008dae241100005051e8afb0ffff83c40c508bcde874d4ffffd9'], [12601747, '8b0650ffd383c40484c0750d83c604'], [12596280, '8b0650ffd583c40484c075078b'], [12593413, '8b8e580b0000c1e909f6c1017515388635010000740df6861001000010750433c0']]),
    fn('SporeCoop::Adventure::PerceptionPoolBack', 0xc05fe0, '8b41188b511c8b49243bc2750c83e904',
       [[12607480, 0], [12607484, 0]],
       'thiscall(native record deque*)->last record*,ret; deque back, caller must confirm nonempty',
       calls=[[12621985, 12607456]], vslots=[], checks=[]),
    fn('SporeCoop::Adventure::PerceptionPoolPopBack', 0xc06000, '8b41183b411c7424568d70ec8971188b',
       [[12607537, 0]],
       'thiscall(native record deque*),ret or native tailcall; pop_back balances record+10/+C intrusive refs; no raw list ownership writes',
       calls=[[12621997, 12607488]], vslots=[], checks=[[12607497, '8d70ec8971188b4e1085c974078b018b5004ffd28b4e0c5e85c9740c8b018b5004ffe2']]),
    fn('SporeCoop::Adventure::PerceptionRecordFactory', 0xc068b0, '83ec1453568b7424208b46188d481457',
       [[12609885, 0]],
       'cdecl(native deque*,targetCreature*,float edgeDistance,bool sight,bool heard)->record*,ret; returns0x14B native record owns target via+10. Guard exact C09570 factory caller before optional previous-party back cleanup',
       calls=[[12622064, 12609712]], vslots=[], checks=[[12609808, '8b7c24288a4c24308a542434f30f1044242c8d70ec897e08f30f1106884e04885605'], [12609842, '8b5e103bfb741c85ff74088b078b108bcfffd2897e1085db74098b038b50048bcbffd2']]),
    fn('SporeCoop::Adventure::GetLocomotionRequest', 0xc42020, '8d81f4010000c3',
       [[12853286, 0]],
       'thiscall(locomotiveSpatial)->inline cLocomotionRequest*, ret; lea spatial+1F4. Native creature AI requests MoveTo then sets returnedrequest+5C from mode1 to2. Mode1/2 share goal completion; vehicle empty-route mode2 skips arrival sqrt cap, retaining later native acceleration/turn/collision.',
       calls=[[14074637, 12853280], [14075359, 12853280]], vslots=[], checks=[[14074635, '8bcde80e5dedfff30f108424c8000000c7405c02000000'], [14075357, '8bcde83c5aedff6a018bcfc7405c02000000'], [12858096, '6b32c4006b32c4008a32c4008a32c400'], [13297752, 'f30f105c243cf30f59d3f30f59cbf30f59c3f30f11542474f30f114c2478e997090000'], [14074114, '8dafc0000000'], [14074625, '516a028bcfe82500ebff'], [12698508, '8d8ec0000000']]),
    fn('SporeCoop::Adventure::VehicleGAGroundLocomotionUpdate', 0xcb3670, '83ec20568b742428578bf98b4e14897c2418',
       [[13318256, 4]],
       'thiscall(strategy,nativeMotionContext*) ret4; verified native GA ordinary vehicle strategy. spatial+4CC points strategy; live Mine both vehicles resolve to vtable14749B4, updateCB3670. Other strategy implementations require separate acceptance.',
       calls=[], vslots=[[21449140, 12, 13317744]], checks=[[13250968, 'c700b4494701c74008dc504701'], [12858222, '8b8ecc040000895424088944240c897424188b118b520c8d44240450ffd2']]),
    fn('SporeCoop::Adventure::VehicleTeleport', 0xC888D0, '568bf18b4c24088b068b5038518bceffd28b',
       [(13142288, 8)],
       'thiscall(vehicleSpatial,Vec3*,Quat*), ret8; native shared teleport calls38/3C plus PhysicsWorld B48870(owner,false)',
       vslots=[(21449704, 68, 13142224)]),
    fn('SporeCoop::Adventure::VehicleMoveTo', 0xCA35A0, '81ecf800000053555633ed80bc2414010000',
       [(13252584, 16)],
       'thiscall(vehicleSpatial,Vec3* destination,float goalStop,float acceptableStop,bool straight), ret10; installs locomotion request and authored current-act desired speed',
       vslots=[(21449704, 224, 13252000)]),
    fn('SporeCoop::Adventure::VehicleSetDesiredSpeed', 0xC9EE70, '8b442408d94424045051d91c24e8fe2ffaff',
       [(13233794, 8)],
       'thiscall(vehicleSpatial,float speed,int flag), ret8; forwards C41E80, writes spatial+1E0',
       vslots=[(21449704, 196, 13233776)]),
    fn('SporeCoop::Adventure::VehicleStandardSpeed', 0xC42080, 'd981e4010000c3',
       [(12853382, 0)],
       'thiscall(vehicleSpatial)->ST0 float, ret; spatial+1E4',
       vslots=[(21449704, 204, 12853376)]),
    fn('SporeCoop::Adventure::VehicleStopMovement', 0xC43800, '83ec74f30f1005e4e76801f30f100dac0e47',
       [(12859624, 0)],
       'thiscall(vehicleSpatial), ret; empty locomotion request, speed0 with flag1; does not change world health/goals',
       vslots=[(21449704, 236, 12859392)]),
    fn('SporeCoop::Adventure::CreatureMoveTo', 0xC435F0, '81ecf800000053565733ff80bc241401',
       [(12859380, 16)],
       'thiscall(creatureSpatial,Vec3*,float goalStop,float acceptableStop,bool), ret10; restores authored standard speed before desired-speed override',
       vslots=[(21405376, 224, 12858864)]),
    fn('SporeCoop::Adventure::CreatureSetDesiredSpeed', 0xC11570, 'd9442404568bf1518d8e40ffffffd91c',
       [(12654010, 8), (12654036, 8)],
       'thiscall(creatureSpatial,float speed,int flag), ret8; native desired locomotion speed',
       vslots=[(21405376, 196, 12653936)]),
    fn('SporeCoop::Adventure::CreatureStandardSpeed', 0xC42080, 'd981e4010000c3cccccccccccccccccc',
       [(12853382, 0)],
       'thiscall(creatureSpatial)->ST0 float, ret; reads spatial+1E4',
       vslots=[(21405376, 204, 12853376)]),
    fn('SporeCoop::Adventure::CreatureStopMovement', 0xC43800, '83ec74f30f1005e4e76801f30f100dac',
       [(12859624, 0)],
       'thiscall(creatureSpatial), ret; clears locomotion request and desired speed',
       vslots=[(21405376, 236, 12859392)]),
    fn('SporeCoop::Adventure::RecalculatePauseFlags', 0xB320B0, '83ec0c53568bf18b4e48c7464800000000',
       [(0xB321DF,0)],
       'thiscall(timeManager),ret or tailcall effects vtable2C/30; first active signed counter selects flags; manager map+4C root+58, vector+68/+6C, entries counter/flags8 bytes. UserToggle04BF38A8 flags7, UIToggle04BF38A4 and ModalDialog04BF38A9 flags3 registered atB32500. Preserve counters and other pause reasons in active GA party policy',
       [(0xB3234E,0xB320B0),(0xB3237E,0xB320B0),(0xB323BF,0xB320B0),(0xB32419,0xB320B0),(0xB324EA,0xB320B0)],
       checks=[(0xB321FF,'8b4e688b40148b04c1'),
               (0xE5C780,'8b410c8d5104578b7c240c8bca85c07416568b3739701072078bc88b4004eb028b0085c0'),
               (0xB32500,'565768ac034601b8070000005068a838bf048bf1e887fdffff68a0034601bf030000005768a738bf048bcee870fdffff68940346015768a438bf048bcee85efdffff68840346015768a938bf048bcee84cfdffff')]),
    fn('SporeCoop::Adventure::HandleCinematicMessage', 0xAE0DB0, '8b44240481ec44020000535556578be9',
       [(0xAE0E37,8),(0xAE1D29,8),(0xAE1EC0,8),(0xAE1ED4,8)],
       'thiscall(cinematicManager,uint32 messageID,void* data)->bool AL,ret8; native captionContinue0445F729 mutates UI before EndCinematic, so gate the complete message branch',
       [(0xAE1D0B,0xAD7880),(0xAE1EAF,0xAE0000),(0xAE1EC3,0xADFF50)]),
    fn('Simulator::cCinematicManager::Get', 0xB3D5D0, 'a150eb6701c3', [(0xB3D5D5,0)],
       'cdecl()->cinematicManager singleton167EB50; intro end uses the already verified upstream manager getter'),
    fn('SporeCoop::cCinematicManager_EndCinematic', 0xADFF50, '83ec40568bf1837e2c00', [(0xADFFF0,0)],
       'thiscall(cinematicManager),ret; ends current cinematic and camera/input suppression; same upstream entry used for GA Intro release',
       [(0xADFF60,0xADF7C0),(0xADFF82,0xB3D350)],
       checks=[(0xADFFA5,'8b8648010000c744240444b83e01c744243c000000008944240cc7864801000000000000'),
               (0xADFFD3,'6a008d4c2408516889114f048bc8ffd2')]),
    fn('SporeCoop::Adventure::SetThrowCombatantReference', 0xAE6610, '538b5c2408568bf1578b3e3bdf', [(0xAE6640,4)],
       'thiscall(combatantRefStorage,combatant*)->storage EAX,ret4; intrusive ref uses combatant virtual60/64; full throw D7F9FB uses interactable+10C and captain+5A8', [(0xD7F9FB,0xAE6610)]),
    fn('SporeCoop::Adventure::AvatarTickAI', 0xC04020, '83ec08d944240c535657', [(0xC040ED,4)],
       'thiscall(creature,float deltaSeconds),ret4; local captain input/AI update called by C0B015', [(0xC0B015,0xC04020)]),
    fn('SporeCoop::Adventure::MoveAvatarWASD', 0xD32330, '81ec3c01000053568bd957', [(0xD32DE5,0)],
       'thiscall(creatureStrategy),ret; native avatar WASD input; calls from D32E81,D32EA9', [(0xD32E81,0xD32330),(0xD32EA9,0xD32330)]),
    fn('SporeCoop::Adventure::MoveAvatarToCursor', 0xD31A70, '81ece40000005355568bf157', [(0xD32329,8)],
       'thiscall(creatureStrategy,bool,bool)->bool AL,ret8; cursor movement D32E9A pushes0,1', [(0xD32E9A,0xD31A70)]),
    fn('SporeCoop::Adventure::IssueCreatureOrder', 0xC032F0, '8b44241cd944240c8b542418', [(0xC03357,0x1C)],
       'thiscall(creature,seven raw32bit stackwords)->order EAX,ret1C; third word floatbits; forwarding preserves exact words'),
    fn('SporeCoop::Adventure::CanReceiveItem', 0xC13DC0, '5153568bf1c644240b00e8417bf4ff8b', [(0xC13F02,0xC)],
       'thiscall(creature,spatialItem,boolAllowHeld,boolRequireMouth)->bool AL; native receive D8A2BF uses true,false; validates inventory and pickup flags', [(0xD8A2BF,0xC13DC0)]),
    fn('SporeCoop::Adventure::RemoveBehaviorOrders', 0xBC9A60, '5153558b6c24108bd98b4c2414238bc4', [(0xBC9AE2,8)],
       'thiscall(orderList,maskLow,maskHigh),ret8; clears matching orders and releases target refs; native receive teardown D8A485 uses mask0,40000', [(0xD8A485,0xBC9A60)]),
    fn('SporeCoop::Adventure::ThrowPairGrace', 0xB4C700, '8b412083ec1083e8000f849a00000083', [(0xB4C7A6,0xC)],
       'thiscall(physics,captainSpatial,itemSpatial,float duration),retC; native ordinary throw D7FCF4 supplies0.5; temporary pair collision exclusion', [(0xD7FCF4,0xB4C700)]),
    fn('SporeCoop::Adventure::ExplosionEffect', 0xF21C00, '83ec6056c744240400000000', [(0xF21D2F,0)],
       'cdecl(Vec3* position,Quat* orientation,float scale,uint32 effect); actual authored explosion presentation, no damage/object destruction; F24730 passes its sampled native pose, radius-derived or object scale, and property effect ID',
       [(0xF2486C,0xF21C00),(0xF21CC9,0x4AA060)]),
    fn('SporeCoop::Adventure::WorldBounds', 0xC8A630, '83ec3856578bf98b078b5068', [(0xC8A6D7,4)],
       'thiscall(spatial,Box24*out)->Box24*, ret4; six float min/max copied from local extents then transformed to world; native explosion F205A8/F205DB calls slot6C and averages min/max into centers',
       [(0xC8A6BF,0xC89970),(0xC8A6CB,0x409DD0)],
       [(0x146A3D0,0x6C,0xC8A630),(0x146CB68,0x6C,0xC8A630)]),
    fn('SporeCoop::Adventure::BombFuseExpiry', 0xf20810, '568b7424088b4e048b018b50', [(0xf20856,0)],
       'cdecl(nativeTimerTask*); objectref+4; kills timed bomb with ownHP,type19. Guard old coop callbacks before native cleanup flush executes them', []),
    fn('SporeCoop::Adventure::ExplosionDelayExpiry', 0xf24910, '568b7424088b4e048b018b50', [(0xf24942,0),(0xf24950,0)],
       'cdecl(nativeTimerTask*); retained object+4, checksnativeDestroyed then ExplodePowerup. Guard stale run before cleanup flush', [(0xf24939,0xf24730)]),
    fn('SporeCoop::Adventure::SchedulePowerupExplosion', 0xf24960, 'f30f1044240c0f2e05b45248', [(0xf249aa,0)],
       'cdecl(powerup*,combatantAttacker*,float delay); nonzero delay queues ExplosionDelayExpiry, otherwise actual explosion immediately', [(0xf2498f,0xf20220),(0xf249a1,0xf24730),(0xf24b80,0xf24960)]),
    fn('SporeCoop::Adventure::FlushPowerupTimers', 0xf20310, '518b0d1c836c0156e8a30df1', [(0xf20386,0)],
       'cdecl(); native scenario cleanup EF0BA0 calls atEF0BC6; flush EXECUTES callbacks+10 before releasing objects and pool records, does not merely discard', [(0xef0bc6,0xf20310)]),
    fn('SporeCoop::Adventure::ApplyCreatureAbility', 0xc20390, '81eccc0000005556578bbc24', [(0xc21180,12),(0xc2118e,12)],
       'thiscall(captain,combatantTarget,int abilityIndex,Vec3* hitPosition); ret0C; native outgoing damage boost C2064F and armor C206AE; GA skips random damage branch', [(0xc02b59,0xc20390),(0xc24024,0xc20390)]),
    fn('SporeCoop::Adventure::ActivateMine', 0xf207d0, '8b4c24048b018b5004ffd285', [(15861775, 0)],
       'cdecl(powerup*,combatant*); kills mine with own HP,type19; resulting actual blast is separate ExplodePowerup', [(15875550, 15861712), (15875831, 15861712)]),
    fn('SporeCoop::Adventure::ActivateGrenade', 0xf20860, '568b7424088b068b50048bce', [(15861995, 0)],
       'cdecl(powerup*,contactGameData*); requires thrown+30, preserves contact-reference+38 grace while age+34 below1sec; kills grenade with type19', [(15875455, 15861856)]),
    fn('SporeCoop::Adventure::ArmGrenade', 0xf208f0, '538b5c240c568b74240c57c6', [(15862061, 0)],
       'cdecl(powerup*,contactReferenceGameData*); sets thrown+30, age+34=0, retains reference+38; native throwing controller D7F9C0 passes acting captain through F20A70', []),
    fn('SporeCoop::Adventure::StartBombFuse', 0xf210c0, '83ec0c568b7424148b068b50', [(15864203, 0)],
       'cdecl(powerup*); native timed-bomb movement/fuse creation from properties, queues callback F20810', [(15864190, 15860256)]),
    fn('SporeCoop::Adventure::ExplodePowerup', 0xf24730, '83ec2853568b7424348b068b', [(15878415, 0)],
       'cdecl(powerup*,combatantAttacker*); authored class radius+498/damage+49C and property force, FX, blast, drop, destroy; no contents creation', [(15878252, 15866880), (15878366, 15877504), (15878379, 15872240), (15878404, 11674976)]),
    fn('SporeCoop::Adventure::ExplosionRadius', 0xf24580, 'f30f1044241083ec180f2e05', [(15877585, 0), (15877929, 0)],
       'cdecl(sourceGameData*,combatantAttacker*,float damage,float radius,float force); native sphere selection and falloff; radiuszero direct path', [(15877854, 15863632)]),
    fn('SporeCoop::Adventure::ExplosionUnownedRadius', 0xf230f0, '83ec14c7042400000000c744', [(15872359, 0)],
       'cdecl(sourceGameData*,Vec3* center,float damage,float radius,float force); native sphere selection and same falloff helper', [(15872340, 15863632)]),
    fn('SporeCoop::Adventure::TickPowerupTimers', 0xf20390, '518b0d1c836c0156e8230df1', [(15860762, 0)],
       'cdecl(float seconds); advances global scenario delayed callbacks elapsed+0C, executes callback+10 and releases; pause during group scenes', [(15675692, 15860624)]),
    fn('SporeCoop::Adventure::TickPowerups', 0xf24c40, '83ec1c535657e835f4ffffe8', [(15879827, 0)],
       'cdecl(float seconds); native powerup/respawner update, grenade age+34 increments F24CF9/F24DF3; pause with group scenes', [(15675704, 15879232), (15879455, 15863232), (15879705, 15863232)]),
    fn('Simulator::cPhysicsWorld::Get', 0xb3d410, 'a1e8ea6701c3cccccccccccc', [(11785237, 0)],
       'cdecl()->physics singleton167EAE8', []),
    fn('Simulator::cPhysicsWorld::ApplyImpulse', 0xb453f0, '83ec08578bf9837f2000751e', [(11818007, 12), (11818059, 12), (11818137, 12)],
       'thiscall(physics,spatial*,Vec3* impulse,bool); ret0C; native throw D7FCCD..D7FCD4 and explosion F20749..F20750 use same impulse dispatch', [(14154964, 11817968), (15861584, 11817968)]),
    fn('SporeCoop::Adventure::GrenadeThrowReference', 0xf20a70, '8b4c240485c9742c8b018b50', [(15862436, 0)],
       'cdecl(object*,actingCaptain*); concrete interactive ornament cast, grenade13 then tailjump ArmGrenade; native D7F9C0 passes EDI captain', [(14154187, 15862384)]),
    fn('SporeCoop::Adventure::GetFloatDefault', 0x4e1ce0, '558bec83ec08837d08007428', [(5119258, 0)],
       'cdecl(PropertyList*,uint32,float default)->float; ret; native boost property reader', [(15871724, 5119200)]),
    fn('SporeCoop::Adventure::ActivateMedKit', 0xf225e0, '83ec445355568b742458d986', [(15869936, 0)],
       'cdecl(powerup*,captain*); native heals collector plus posse, emits collect then destroys, do not replay wholesale in coop', [(15875636, 15869408)]),
    fn('SporeCoop::Adventure::ActivateSpeedBoost', 0xf22ed0, '83ec08d9ee5355568b742418', [],
       'cdecl(powerup*,captain*); speed modifier7, duration property; tailjmp UpdateGoals atF230E8; native also iterates posse', [(15875708, 15871696), (15871746, 12636704)]),
    fn('SporeCoop::Adventure::ActivateDamageBoost', 0xf22c20, '83ec10d9ee5355568b742420', [],
       'cdecl(powerup*,captain*); amountB14/flagB10, native expiry refresh; tailjmp UpdateGoals F22EC2; also iterates posse', [(15875740, 15871008)]),
    fn('SporeCoop::Adventure::ActivateArmorBoost', 0xf22970, '83ec10d9ee5355568b742420', [],
       'cdecl(powerup*,captain*); amountB1C/flagB18, native expiry refresh; tailjmp UpdateGoals F22C12; also iterates posse', [(15875772, 15870320)]),
    fn('SporeCoop::Adventure::ActivateTeleporter', 0xf23280, '83ec34578b7c244080bf600e', [(15873193, 0)],
       'cdecl(powerup*,captain*); native E60 cooldown2s, destination query plus owner cleanup, then also teleports posse', [(15875790, 15872640), (15872722, 15864288), (15872962, 15872464)]),
    fn('SporeCoop::Adventure::TeleportDestination', 0xf211e0, '83ec0c5355568b74241c578d', [(15864441, 0), (15864506, 0)],
       'cdecl(powerup*,Vec3* position,Quat* orientation)->bool AL; native authored exit query, rejects colocated/invalid exit', [(15872722, 15864288)]),
    fn('SporeCoop::Adventure::TeleportCaptain', 0xf231d0, '8b44240c83ec0c5657508d4c', [(15872625, 0)],
       'cdecl(powerup*,captain*,Vec3*,Quat*,bool playerEffect); owner-only native clamp, teleport, clear locomotion history and stop, effect', [(15872962, 15872464)]),
    fn('SporeCoop::Adventure::PickupEffect', 0xf22500, '83ec38538b5c244456578b7c', [(15869394, 0)],
       'cdecl(captain*,uint32 effect); get/create creature effect and scale to captain; collector-only visual helper', [(15871230, 15869184)]),
    fn('SporeCoop::Adventure::OnCollect', 0xf1bcf0, '83ec1c83b99000000003890c', [(15843057, 8)],
       'thiscall(play*,object*,captain*); ret8; native collect/use objective bookkeeping', [(15869880, 15842544)]),
    fn('SporeCoop::Adventure::SetSpeedModifier', 0xc0d220, '8b54240453568bf183fa0873', [(12636943, 8)],
       'thiscall(captain*,int modifier,bool enabled); ret8; modifier7 native speed boost flag and recalculation', [(15871746, 12636704)]),
    fn('SporeCoop::Adventure::HandlePlayMessage', 0xF1C570, '8b44240483ec10535556578b',
       [(0xF1C757,8),(0xF1C787,8),(0xF1C9B4,8)],
       'thiscall(play,uint32 messageID,void* payload)->bool, ret8; intro044F1189 directly writes state3, results070B18C7 payload+8=2 calls in-place replay',
       [(0xF1C779,0xF1C1D0),(0xF1C9A6,0xEF1320)],
       checks=[(0xF1C6E8,'83be900000000175118bcec7869000000003000000e8aed0ffff'),
               (0xF1C7A7,'8b8a2c01000056e88dbdffff5fc7869400000002000000'),
               (0xF1C994,'8b5424288b420883f80275178b0da47a6c01e87549fdff5f5e5d32c05b83c410c2080083f8010f853c0900008b0da47a6c018b81d000000083e800743c83e801742683e8010f851d09000068054c6501e8370ac2ff8bc8e86019c0ff5f5e5d32c05b83c410c20800e8cf35fdff5f5e5d32c05b83c410c20800e86e48fdff5f5e5d32c05b83c410c2'),
               (0xF17713,'c74614010000006a018bcec786d40000000a000000e833edffff')]),
    fn('SporeCoop::Adventure::ResultsHandleUIMessage', 0xF16FA0,
       '81ec800000005356578bbc24940000008b470883e8018bf1',
       [(0xF1773F,8),(0xF17078,8),(0xF170B9,8)],
       'thiscall(results,IWindow*,UTFWin::Message*)->boolAL,ret8; ComponentActivated287259F6 control07C79948 follows ordinary Done, sets choice14=1 and phaseD4=10, calls native captain-saveF16460(true); only owned visible/enabled phase0/9 UI may be driven',
       [(0xF17728,0xF16460)],
       vslots=[(0x148C0AC,0,0xF16FA0)],
       checks=[(0xF16FE9,'2d4099c707740983e8080f85390700008b8edc00000085c9740b8b018b507c6a006a01ffd233c0817f0c4899c7070f95c040894614'),
               (0xF1771A,'6a018bcec786d40000000a000000e833edffff'),
               (0xF17EC0,'8b86d40000003bc5740ac64424180083f8097505c644241801'),
               (0xF185E4,'8b5c2424c786d4000000010000008b7e10'),
               (0xF18694,'8b4e1083b990000000058b15a47a6c018b')]),
    fn('SporeCoop::Adventure::TalkBegin', 0xF1A000, '83ec345355568b7424488be9', [(0xF1A1EE,8)],
       'thiscall(play,goal*,targetGameData*), ret8; native camera/dialog start, stores C8/CC/D4/D8',
       [(0xF1A159,0xAE08B0)]),
    fn('SporeCoop::Adventure::TalkFinish', 0xF1C1D0, '515356578bf1e89544b0ff8b', [(0xF1C38D,0)],
       'thiscall(play), ret; native dialog exit and presentation cleanup; CountGoal uses stored C8/CC',
       [(0xF1C21E,0xF1ACF0)]),
    fn('SporeCoop::Adventure::ActivateGate', 0xF23AB0, '535556578b7c241c57e802feffff', [],
       'cdecl(captain,keyPowerup*,gatePowerup*), tailjmp UpdateGoals F1C390 atF23B3E; opens/replaces gate, emits goal callback, drops and destroys key',
       [(0xF23CBE,0xF23AB0),(0xF23AB9,0xF238C0),(0xF23B17,0xC15EB0),(0xF23B2D,0xB22560)]),
    fn('SporeCoop::Adventure::GateKey', 0xF21670, '8b0dcc836c018b4424048b4018', [(0xF21689,0)],
       'cdecl(scenarioPowerup*)->uint32 native matching key category from powerup property list+18; caller cleans4',
       [(0xF23C9F,0xF21670)]),
    fn('SporeCoop::Adventure::OpenGate',0xF238C0,'83ec2c5355568b74243c8b06',[(0xF23AA2,0)],
       'cdecl(gatePowerup*), ret; destroys closed gate, emits effects and constructs open ornament powerup14 with the authored marker, scale and secondary appearance',
       [(0xF239CD,0xB22560),(0xF23AB9,0xF238C0)]),
    fn('App::Property::GetValueString16', 0x68A4E0, '0fb741126683f813', [(0x68A4FD, 0), (0x68A507, 0)],
       'thiscall(property)->eastl string16*; validates type13 and pointer/inline flags'),
    fn('Pollinator::cAssetMetadata::NameText', 0x414E00, '558bec83ec08894df88b45f8', [(0x414E18, 0)],
       'thiscall(metadata)->UTF16 pointer; reads name+78; Sporepedia GetName641800 tailcalls here'),
    fn('Pollinator::cAssetMetadata::AuthorText', 0x550880, '558bec83ec08894df88b45f8', [(0x550898, 0)],
       'thiscall(metadata)->UTF16 pointer; reads author+58; Sporepedia GetAuthorName641810 tailcalls here'),
    fn('UTFWin::IImageDrawable::AddImageWindow', 0x807970, '83ec148b4424188b088b5008', [(0x807B33, 0)],
       'cdecl(key*,float x,float y,parent)->IWindow*; native image lookup, drawable/window creation and parent AddWindow',
       [(0x8079A2, 0x806320)]),
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
    fn('SporeCoop::Adventure::NPCPlayerTargetScore', 14207776, '83ec0c53558b6c241883bd1c1000000156', [[14207967, 0], [14208236, 0], [14208327, 0]], ('cdecl(NPC*,double simulationTime,uint32 flags,targetOutput*,bool* '
 'accepted,decider*,context*)->ST0 float,ret; eight DWORD stack slots. Native callback dispatcher '
 'BC8B42 calls decider+8 and addsesp20. For read-only watched-NPC diagnostics, preserve all args '
 'and native float return without forcing score or target.'), calls=[], checks=[[12356376, '8b4c2430dd4424248b44241c8b564d5657518946048b4641525083ec08dd1c24893ec64608008b4f0853ffd183c420'], [12356418, 'ffd183c4205f5e5b59c3'], [14207781, '8b6c241883bd1c1000000156570f85ab010000f744242c080400000f849a000000'], [14208072, '807e04007506807e050074648b46088b90c00000008d88c00000008b4258ffd084c074488b760880be5e0b000000753c85f674388b168b42208bceffd03db7b48e017528'], [22570320, 'acece0120100000020cbd800'], [22579864, 'acece0120100000020cbd800'], [22603080, 'acece0120100000020cbd800']]),
    fn('SporeCoop::Adventure::NPCDeciderScoreDispatcher', 12356320, '51dd4424180f57c0538b5c2418568b742428578b7c2414', [[12356427, 0], [12356450, 0]], ('Read-only diagnostic candidate only. cdecl(decider*,uint32 mode,uint32 index,NPC*,double '
 'time,context*,bool* accepted)->ST0 float, ret. Seven C args/eight DWORD stack slots. Child '
 'callers BC8C61..73 push accepted/context/double/NPC/index/mode/decider and addesp20. Original '
 'dispatcher applies BC8A50 before optional decider+8 score call; observing entry does not '
 'establish that callback executed.'), calls=[[12356358, 12356176]], checks=[[12356418, 'ffd183c4205f5e5b59c3'], [12356705, '505683ec08dd1c2451526a0157c644244800e868feffff']]),
    fn('SporeCoop::Adventure::MatchesBehaviorTarget', 13095472, '568b74240c8b0683f808774eff248590d2c700', [[13095494, 0], [13095517, 0], [13095536, 0], [13095561, 0], [13095565, 0]], ('cdecl(complete cGameData*,const BehaviorTarget*)->bool AL, plain ret. Type1 invokes GameData '
 'primary+4C and compares politicalID to target+4. Creature primary146A080+4C=5AADB0 returns '
 'completeGameData+30. Type2 scenario class and type8 singleton avatar remain native. Native '
 'callback runs once; strict scoped host captain-category extension only at listed scenario '
 'condition/attack sites.'), calls=[[13095544, 11664640]], vslots=[[21405824, 76, 5942704]], checks=[[5942704, '8b4130c3'], [13095495, '8b4c24088b018b504cffd233c93b46045e0f94c18ac1c3']]),
]

# Noncallable return labels scope native detours to reviewed caller instructions.
CODE_SITES = [{'name': 'SporeCoop::Adventure::NPCPerceptionAvatarReturn',
  'address': 12621203,
  'prefix': '33db8bf8895c2410885c',
  'note': 'noncallable return-address label after initial singleton perception-avatar read, never a function '
          'entry',
  'calls': [[12621198, 11664640]],
  'checks': []},
 {'name': 'SporeCoop::Adventure::NPCPerceptionCleanupReturn',
  'address': 12601752,
  'prefix': '83c40484c0750d83c604',
  'note': 'noncallable callback return-address label in native sensed-vector remove_if, requires host NPC '
          'subject scope',
  'calls': [],
  'checks': [[12601747, '8b0650ffd383c40484c0']]},
 {'name': 'SporeCoop::Adventure::NPCPerceptionCopyCleanupReturn',
  'address': 12596285,
  'prefix': '83c40484c075078b0e89',
  'note': 'noncallable callback return-address label in native sensed-vector remove-copy, requires host NPC '
          'subject scope',
  'calls': [],
  'checks': [[12596280, '8b0650ffd583c40484c0']]},
 {'name': 'SporeCoop::Adventure::PerceptionRecordFactoryReturn',
  'address': 12622069,
  'prefix': '8944243c83c4148d4424',
  'note': 'noncallable native sensed-record factory return; earlier C04980/C06D50 vector cleanup is complete '
          'before this call',
  'calls': [[12622064, 12609712], [12622039, 12610896]],
  'checks': [[12622044, 'd94424488b4c24188b54241c515251d91c245753e8bbcfffff']]},
 {'address': 15470448,
 'calls': [[15470443, 13095472]],
 'name': 'SporeCoop::Adventure::AttackConditionTargetReturn',
 'note': 'Noncallable return label after native complete GameData/BehaviorTarget cdecl match; caller '
         'addsesp8. Condition1 and action0 share native sensor and retirement checks.',
 'prefix': '83c40884c00f848b010000'},
 {'address': 15470479,
 'calls': [[15470474, 13095472]],
 'name': 'SporeCoop::Adventure::AttackCurrentTargetReturn',
 'note': 'Noncallable return label after native complete GameData/BehaviorTarget cdecl match; caller '
         'addsesp8. Condition1 and action0 share native sensor and retirement checks.',
 'prefix': '83c40884c0743c'},
 {'address': 15470642,
 'calls': [[15470637, 13095472]],
 'name': 'SporeCoop::Adventure::AttackSensorTargetReturn',
 'note': 'Noncallable return label after native complete GameData/BehaviorTarget cdecl match; caller '
         'addsesp8. Condition1 and action0 share native sensor and retirement checks.',
 'prefix': '83c40884c00f84b8000000'},
 {'address': 15469813,
 'calls': [[15469808, 13095472]],
 'name': 'SporeCoop::Adventure::AttackSensorConditionReturn',
 'note': 'Noncallable return label after native complete GameData/BehaviorTarget cdecl match; caller '
         'addsesp8. Condition1 and action0 share native sensor and retirement checks.',
 'prefix': '83c40884c00f85a7010000'}]

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
    for entry in [*FUNCTIONS, *CODE_SITES]:
        va = entry['address']
        if not pe.in_text(va):
            raise ValueError(entry['name'] + ': native entry outside .text')
        expected = bytes.fromhex(entry['prefix'])
        if pe.read(va, len(expected)) != expected:
            raise ValueError(entry['name'] + ': entry prefix mismatch')
        for site, popped in entry.get('returns', ()):
            expected = b'\xc3' if not popped else b'\xc2' + struct.pack('<H', popped)
            if pe.read(site, len(expected)) != expected:
                raise ValueError(entry['name'] + ': return mismatch at ' + hex(site))
        for site, target in entry['calls']:
            raw = pe.read(site, 5)
            if not raw or raw[0] != 0xE8 or site + 5 + struct.unpack('<i', raw[1:])[0] != target:
                raise ValueError(entry['name'] + ': call edge mismatch at ' + hex(site))
        for table, slot, target in entry.get('vslots', ()):
            if pe.u32(table + slot) != target:
                raise ValueError(entry['name'] + ': vtable slot mismatch at ' + hex(table + slot))
        for site, encoded in entry.get('checks', ()):
            if pe.read(site, len(bytes.fromhex(encoded))) != bytes.fromhex(encoded):
                raise ValueError(entry['name'] + ': layout evidence mismatch at ' + hex(site))
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
    lines = ['# SporeKit verified Galactic Adventures overrides for GOG 2024 (dc04).',
             '# exe_sha256=' + EXE_SHA256,
             '# source=Spore-Community/Spore-ModAPI@' + UPSTREAM_COMMIT,
             '# evidence=tools/addrport/verify_dc04.py; docs/re/build-dc04.md',
             '# Loads after overrides_dc04aee5.sym; preserves reviewed GA entry points over imported aliases.',
             '# App::Property::GetFloat remains the upstream bool/out accessor at 0040CF10.',
             '# GetFloatDefault is the distinct float/default accessor at 004E1CE0.']
    for e in sorted(evidence, key=lambda x: x['name']):
        lines.append(f"{e['name']}=0x{e['address']:08X} ; verified static: {e['note']}")
    return '\n'.join(lines) + '\n'


def check_resolved_symbols(directory, evidence):
    """Apply SporeKit's exact-build normal/override load order before checking GA."""
    symbols = {}
    files = sorted(directory.glob('*.sym'),
                   key=lambda path: (path.name.startswith('overrides'), path.name))
    for path in files:
        lines = path.read_text(encoding='utf-8').splitlines()
        headers = '\n'.join(lines[:10])
        if not re.search(r'exe_sha256=' + EXE_SHA256 + r'(?:\s|$)', headers):
            continue
        for line in lines:
            entry = re.fullmatch(r'([^#;\s=]+)=(0x[0-9A-Fa-f]+)(?:\s*;.*)?', line)
            if entry:
                symbols[entry[1]] = int(entry[2], 16)
    expected = {entry['name']: entry['address'] for entry in evidence}
    # Space trade's bool/out reader must coexist with GA's float/default reader.
    expected['App::Property::GetFloat'] = 0x40CF10
    for name, address in expected.items():
        if symbols.get(name) != address:
            raise ValueError(f'{name}: resolved symbol differs from verified address '
                             f'0x{address:08X} (got 0x{symbols.get(name, 0):08X})')
    return len(symbols)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe')
    parser.add_argument('--write-map', type=Path)
    parser.add_argument('--check-map', type=Path)
    parser.add_argument('--check-symbols', type=Path,
                        help='verify the effective .sym directory in SporeKit load order')
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        pe = PE(args.exe)
        evidence = verify(pe)
        rendered = map_text(evidence)
        if args.check_map and args.check_map.read_text(encoding='utf-8') != rendered:
            raise ValueError('symbol map differs from reviewed evidence manifest')
        if args.check_symbols:
            count = check_resolved_symbols(args.check_symbols, evidence)
            print(f'verified effective symbol load order ({count} names)')
        if args.write_map:
            args.write_map.write_text(rendered, encoding='utf-8', newline='\n')
        if args.report:
            args.report.write_text(json.dumps(dict(sha256=pe.sha256, symbols=evidence), indent=2) + '\n', encoding='utf-8')
        print(f'verified {len(FUNCTIONS)} functions and {len(GLOBALS)} globals and {len(CODE_SITES)} code sites for {pe.sha256}')
        return 0
    except (OSError, ValueError, AssertionError) as exc:
        print('verification failed: ' + str(exc), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
