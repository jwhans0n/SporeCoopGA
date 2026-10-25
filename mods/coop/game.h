// Thin, offset-based access to Spore creature-stage objects (see docs/05-re-notes.md).
#pragma once

#include <cstdint>
#include <vector>

#include "net/protocol.h"
#include "sporekit.h"

namespace game {

// cCreatureAnimal layout (GOG 2016 build, matches ModAPI March2017 headers)
constexpr uintptr_t kSpatialOff = 0xC0;     // cLocomotiveObject/cSpatialObject subobject
constexpr uintptr_t kPosOff = 0xC0 + 0x04;  // cSpatialObject::mPosition
constexpr uintptr_t kRotOff = 0xC0 + 0x10;  // cSpatialObject::mOrientation
constexpr uintptr_t kVelOff = 0xC0 + 0x1C8; // cLocomotiveObject::mVelocity
constexpr uintptr_t kCombatOff = 0x5A8;     // cCombatant subobject
constexpr uintptr_t kMaxHpOff = 0x5A8 + 0x2C;
constexpr uintptr_t kHpOff = 0x5A8 + 0x38;
constexpr uintptr_t kSpeciesOff = 0xB20;    // cSpeciesProfile*
constexpr uintptr_t kSpeciesKeyOff = 0xB28; // ResourceKey
constexpr uintptr_t kFlagsOff = 0xB58;      // bit 0x200 = avatar
constexpr uintptr_t kDeadOff = 0xB5E;       // bool mbDead
constexpr uintptr_t kNounAvatarOff = 0x54;  // cGameNounManager::mpAvatar
constexpr uintptr_t kNounAvatarHerdOff = 0x58; // cGameNounManager::mpAvatarHerd
constexpr uintptr_t kHerdOff = 0x1674;     // cCreatureAnimal::mHerd

bool init(const SkHost* host, SkMod* mod); // resolve symbols; false = coop disabled
bool ready();                              // symbols ok
uint32_t mode_id();
bool in_creature_stage();
void* avatar();
net::Vec3 position(void* c);
net::Quat orientation(void* c);
net::Vec3 velocity(void* c);
float health(void* c);
float max_health(void* c);
bool is_dead(void* c);
uint32_t species_instance(void* c);
void* species_profile(void* c);

// Every creature must belong to a herd: game code calls GetHerd() on neighbours
// without a null check (0xD41A20 -> 0xC046B0, see docs/05-re-notes.md).
void* avatar_herd();
void* spawn_creature(const net::Vec3& pos, void* species, void* herd);
void destroy_creature(void* c);
void teleport(void* c, const net::Vec3& pos, const net::Quat& rot);
// cSpatialObject::SetPosition (vtable 0x38): moves without touching locomotion/animation
void set_position(void* c, const net::Vec3& pos);
void set_orientation(void* c, const net::Quat& rot);
void move_to(void* c, const net::Vec3& dst, float stop_dist);
void stop(void* c);
void set_speed(void* c, float speed);
float scale(void* c);
float bounding_radius(void* c);
void set_scale(void* c, float s);

// Animation events: called for every PlayAnimation/PlayAnimationTo on any creature.
using AnimListener = void (*)(void* creature, uint32_t anim_id, int block, bool disable_blend, bool to_target);
void set_anim_listener(AnimListener fn);
void play_animation(void* c, uint32_t anim_id, int block, bool disable_blend);

// ---- creature registry: every cCreatureAnimal that ran Update since the last call ----
const std::vector<void*>& creatures_updated();   // swap per frame (call once per frame)
using UpdateFilter = bool (*)(void* creature);   // return false to skip the creature's Update
void set_update_filter(UpdateFilter fn);
void set_extra_update_filter(UpdateFilter fn); // composed with NPC filtering (adventure ownership/barriers)
// Suppress autonomous NPC decisions without skipping the native visual,
// locomotion and combatant maintenance in the enclosing creature Update.
void set_npc_ai_filter(UpdateFilter fn);
struct CreatureUpdateCounts { uint64_t native = 0, npc_ai_suppressed = 0; };
CreatureUpdateCounts creature_update_counts();
using BlockedUpdateHandler = void (*)(void* creature, int milliseconds);
// Called once only by the real Update hook after a filter rejects its update.
// Pure update-filter queries never invoke this handler.
void set_blocked_update_handler(BlockedUpdateHandler fn);
// Runs only around an allowed native update. Presentation keeps advancing while
// an adventure controller fences a downed captain's brain and movement commands.
using UpdateObserver = void (*)(void* creature, bool before);
void set_update_observer(UpdateObserver fn);
bool is_destroyed(void* c);

// ---- species / herds ----
struct Key {
    uint32_t instance, type, group;
};
Key species_key(void* c);
void* species_by_key(const Key& k);
void* create_herd(const net::Vec3& pos, void* species);
void* herd_of(void* c);
void herd_quiet(void* herd); // target size 0, disabled: the game spawns no members into it
constexpr uint32_t kHerdNoun = 0x01BE418E;      // cHerd (runtime: nest herds of the creature stage)
inline void* herd_species(void* herd) { return *(void**)((uint8_t*)herd + 0xA4); } // cSpeciesProfile*
inline void* herd_nest(void* herd) { return *(void**)((uint8_t*)herd + 0x160); }    // cNest* (mpNest)
uint32_t political_id(void* c);

// ---- combat ----
// filter for cCreatureAnimal::TakeDamage: return true to let the game apply it
// return false to drop the hit; the filter may change the damage
using DamageFilter = bool (*)(void* creature, float& damage, int type, void* attacker_creature);
void set_damage_filter(DamageFilter fn);
void apply_damage(void* c, float damage, int type, void* attacker_creature); // bypasses the filter
void hit(void* c, float damage, int type, void* attacker_creature);          // goes through the filter
void set_health(void* c, float hp);
// impressed by a social game (+0xB58 bit 0x80, set by the social success 0xD9BC40; docs/re/creature.md 8)
inline bool impressed(void* c) { return (*(uint32_t*)((uint8_t*)c + kFlagsOff) & 0x80) != 0; }
inline void set_impressed(void* c, bool on)
{
    uint32_t& f = *(uint32_t*)((uint8_t*)c + kFlagsOff);
    f = on ? (f | 0x80) : (f & ~0x80u);
}

// Legacy time of day: active planet orientation, not a position-independent clock.
// GA uses native authored/local lighting (see docs/re/adventure.md).
bool time_of_day(net::Quat& q);
void set_time_of_day(const net::Quat& q);

// ---- DNA (cCreatureGameData evolution points, a global) ----
float dna();
void dna_add_raw(float delta);              // original AddEvolutionPoints, not reported
using DnaListener = bool (*)(float delta);  // return false to swallow a gain
void set_dna_listener(DnaListener fn);

// ---- any game object (cGameData) ----
// cGameNounManager::mNouns (+0x78) lists every cGameData (list node at object+0xC)
void* noun_manager();
void for_each_object(bool (*fn)(void* obj, uint32_t noun, void* user), void* user);
uint32_t noun_id(void* obj);                    // ISimulatorSerializable::GetNounID (vt 0x20)
void* cast(void* obj, uint32_t type);           // Object::Cast (vt 0x0C)
bool object_position(void* obj, net::Vec3& out); // via cSpatialObject (TYPE 0x1186577), position +4
bool object_alive(void* obj);                    // cGameData::mbIsDestroyed (+0x21) not set
// a cGameDataUFO pointer is still a live UFO noun (the game may have freed it: never read it before)
bool ufo_listed(void* u);
// UFO puppets/mirrors found freed by the game (tests: coop.status "freed_by_game")
extern uint32_t g_ufos_freed;
// nearest object of `noun` within max_dist of pos (nullptr if none)
void* find_object(uint32_t noun, const net::Vec3& pos, float max_dist);

// AI suppression for puppets: NPCTickAI is skipped for registered creatures
void puppet_register(void* c);
void puppet_unregister(void* c);
bool is_puppet(void* c);
const std::vector<void*>& puppets(); // puppets and NPC proxies (creatures), UFO puppets in space

} // namespace game
