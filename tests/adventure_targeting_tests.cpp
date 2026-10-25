// Win32 ABI fixture for the native Scenario political-category matcher.
// This tests exact caller fences and guard behavior, not autonomous game AI.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "adventure_targeting.h"
#include "game.h"
#include "sporekit_game.hpp"

extern "C" {
using MatchCallback = bool(SK_CDECL*)(void*, const void*);
MatchCallback targeting_fixture_match = nullptr;
extern char targeting_match_return_one[], targeting_match_return_two[], targeting_match_return_three[], targeting_match_return_four[];
bool SK_CDECL targeting_match_one(void*, const void*);
bool SK_CDECL targeting_match_two(void*, const void*);
bool SK_CDECL targeting_match_three(void*, const void*);
bool SK_CDECL targeting_match_four(void*, const void*);
}

// Real x86 return labels exercise production __builtin_return_address fences.
asm(".text\n"
    ".globl _targeting_match_one\n_targeting_match_one:\npushl 8(%esp)\npushl 8(%esp)\ncalll *_targeting_fixture_match\n"
    ".globl _targeting_match_return_one\n_targeting_match_return_one:\naddl $8,%esp\nretl\n"
    ".globl _targeting_match_two\n_targeting_match_two:\npushl 8(%esp)\npushl 8(%esp)\ncalll *_targeting_fixture_match\n"
    ".globl _targeting_match_return_two\n_targeting_match_return_two:\naddl $8,%esp\nretl\n"
    ".globl _targeting_match_three\n_targeting_match_three:\npushl 8(%esp)\npushl 8(%esp)\ncalll *_targeting_fixture_match\n"
    ".globl _targeting_match_return_three\n_targeting_match_return_three:\naddl $8,%esp\nretl\n"
    ".globl _targeting_match_four\n_targeting_match_four:\npushl 8(%esp)\npushl 8(%esp)\ncalll *_targeting_fixture_match\n"
    ".globl _targeting_match_return_four\n_targeting_match_return_four:\naddl $8,%esp\nretl\n");

namespace {
using skg::field;
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
struct Object {
    std::array<uint8_t, 0x16A0> bytes{};
    bool valid = true, hostile = true;
};
Object npc, local, guest, guest_two, unrelated;
std::array<Object*, 5> objects{&npc, &local, &guest, &guest_two, &unrelated};
std::vector<adventure_targeting::Captain> captains;
std::vector<void*> remote_identities;
std::map<std::string, uintptr_t> symbols;
void* subject;
bool active = true, npc_live = true, hook_fails = false;
const char* executable_hash = kHash;
int failures = 0, native_calls = 0, hostile_calls = 0, valid_calls = 0, hook_calls = 0;

void check(bool condition, const char* message)
{
    if (!condition) { ++failures; std::printf("FAIL: %s\n", message); }
}
Object* find(void* pointer)
{
    for (auto* object : objects) if (object->bytes.data() == pointer) return object;
    return nullptr;
}
void* SK_CDECL native_valid(void* object)
{
    ++valid_calls;
    const auto* found = find(object); return found && found->valid ? object : nullptr;
}
bool SK_THISCALL native_hostile(void* observer, void* target)
{
    ++hostile_calls;
    check(observer == npc.bytes.data() && find(target), "native hostility preserves complete NPC and captain thiscall arguments");
    return npc.hostile && find(target) && find(target)->hostile;
}
bool SK_CDECL native_match(void* candidate, const void* target)
{
    ++native_calls;
    check(find(candidate) && target, "native matcher receives complete GameData and BehaviorTarget cdecl arguments");
    if (!find(candidate) || !target) return false;
    const uint32_t type = static_cast<const uint32_t*>(target)[0], value = static_cast<const uint32_t*>(target)[1];
    if (type == 0) return true;
    if (type == 1) return field<uint32_t>(candidate, 0x30) == value;
    if (type == 2) return field<uint32_t>(candidate, 0x1C) == value;
    if (type == 8) return candidate == local.bytes.data();
    return false;
}
int create_hook(SkMod*, void* original, void* replacement, void** trampoline)
{
    ++hook_calls;
    check(original == reinterpret_cast<void*>(&native_match), "only the reviewed Scenario matcher is hooked");
    if (hook_fails) return 1;
    *trampoline = original;
    targeting_fixture_match = reinterpret_cast<MatchCallback>(replacement);
    return 0;
}
uintptr_t symbol(const char* name) { const auto found = symbols.find(name); return found == symbols.end() ? 0 : found->second; }
const char* hash() { return executable_hash; }
adventure_targeting::Ctx context()
{
    return {[] { return active; }, [](void* object) { return npc_live && object == npc.bytes.data(); },
        [] { return captains; }, [](void* object) {
            return std::find(remote_identities.begin(), remote_identities.end(), object) != remote_identities.end();
        }};
}
void reset_case()
{
    for (auto* object : objects) {
        object->bytes.fill(0); object->valid = object->hostile = true;
        field<float>(object->bytes.data(), 0x5E0) = 90;
        field<bool>(object->bytes.data(), 0x135) = true;
        field<uint32_t>(object->bytes.data(), 0x30) = 0xFFFFFFFF;
    }
    field<uint32_t>(local.bytes.data(), 0x30) = 1;
    field<uint32_t>(local.bytes.data(), 0xB58) = 0x200;
    captains = {{local.bytes.data(), 0}, {guest.bytes.data(), 1}, {guest_two.bytes.data(), 2}};
    remote_identities = {guest.bytes.data(), guest_two.bytes.data()};
    subject = npc.bytes.data(); active = npc_live = true;
    adventure_targeting::reset();
}
bool invoke(MatchCallback callback, Object& candidate, const std::array<uint32_t, 2>& target)
{
    const int before = native_calls;
    const bool result = callback(candidate.bytes.data(), target.data());
    check(native_calls == before + 1, "every admitted or rejected match invokes original native predicate exactly once");
    return result;
}
} // namespace

namespace game {
void* npc_ai_subject() { return subject; }
void* avatar() { return local.bytes.data(); }
}

int main()
{
    symbols = {
        {"SporeCoop::Adventure::MatchesBehaviorTarget", reinterpret_cast<uintptr_t>(&native_match)},
        {"SporeCoop::Adventure::AttackConditionTargetReturn", reinterpret_cast<uintptr_t>(targeting_match_return_one)},
        {"SporeCoop::Adventure::AttackCurrentTargetReturn", reinterpret_cast<uintptr_t>(targeting_match_return_two)},
        {"SporeCoop::Adventure::AttackSensorTargetReturn", reinterpret_cast<uintptr_t>(targeting_match_return_three)},
        {"SporeCoop::Adventure::AttackSensorConditionReturn", reinterpret_cast<uintptr_t>(targeting_match_return_four)},
        {"SporeCoop::Adventure::IsHostileToCreature", reinterpret_cast<uintptr_t>(&native_hostile)},
        {"SporeCoop::BT_ValidTarget", reinterpret_cast<uintptr_t>(&native_valid)},
    };
    SkHost host{}; host.sym = symbol; host.exe_sha256 = hash; host.hook_create = create_hook;
    executable_hash = "unsupported";
    check(!adventure_targeting::init(&host, nullptr, context()), "unsupported executable fails closed"); executable_hash = kHash;
    const auto saved = symbols["SporeCoop::Adventure::AttackSensorConditionReturn"];
    symbols.erase("SporeCoop::Adventure::AttackSensorConditionReturn");
    check(!adventure_targeting::init(&host, nullptr, context()), "missing native condition return site fails closed");
    symbols["SporeCoop::Adventure::AttackSensorConditionReturn"] = saved;
    hook_fails = true;
    check(!adventure_targeting::init(&host, nullptr, context()), "native matcher hook failure fails closed");
    hook_fails = false;
    check(adventure_targeting::init(&host, nullptr, context()) && adventure_targeting::available(), "verified native matcher initializes");
    check(hook_calls == 2, "one failed and one successful initialization request only the matcher hook");

    reset_case();
    const std::array<uint32_t, 2> player{1, 1};
    const auto guest_before = guest.bytes, npc_before = npc.bytes;
    for (auto callback : {targeting_match_one, targeting_match_two, targeting_match_three, targeting_match_four})
        check(invoke(callback, guest, player), "all four exact condition/action sites recognize alive hostile party player category");
    check(guest.bytes == guest_before && npc.bytes == npc_before, "category extension changes no native flags, political ID, ownership, sensor or target fields");
    check(!invoke(targeting_fixture_match, guest, player), "unrelated caller preserves original classification");
    check(!invoke(targeting_match_four, guest, {1, 2}), "foreign political category is unchanged");
    check(!invoke(targeting_match_four, guest, {2, 0xFFFFFFFE}), "scenario class -2 is not broadened");
    check(!invoke(targeting_match_four, guest, {8, 0}), "singleton avatar target type is not broadened");
    check(invoke(targeting_match_four, guest, {0, 0}), "native default successful predicate is preserved");
    check(invoke(targeting_match_four, guest, {1, 0xFFFFFFFF}), "native successful foreign category is preserved");
    check(invoke(targeting_match_four, local, player), "local avatar native matching remains unchanged");
    check(!invoke(targeting_match_four, unrelated, player), "nonparty creature never inherits player ownership");

    active = false; check(!invoke(targeting_match_four, guest, player), "inactive, guest or held context cannot broaden target"); active = true;
    subject = nullptr; check(!invoke(targeting_match_four, guest, player), "no original native NPC AI scope cannot broaden target"); subject = npc.bytes.data();
    npc_live = false; check(!invoke(targeting_match_four, guest, player), "stale run/act/NPC pointer cannot broaden target"); npc_live = true;
    npc.hostile = false; check(!invoke(targeting_match_four, guest, player), "neutral NPC is never forced hostile"); npc.hostile = true;
    guest.hostile = false; check(!invoke(targeting_match_four, guest, player), "native relationship rejection is preserved"); guest.hostile = true;
    field<float>(npc.bytes.data(), 0x5E0) = 0; check(!invoke(targeting_match_four, guest, player), "dead NPC cannot acquire a captain"); field<float>(npc.bytes.data(), 0x5E0) = 90;
    field<float>(guest.bytes.data(), 0x5E0) = 0; check(!invoke(targeting_match_four, guest, player), "dead guest cannot match player category"); field<float>(guest.bytes.data(), 0x5E0) = 90;
    field<bool>(guest.bytes.data(), 0x135) = false; check(!invoke(targeting_match_four, guest, player), "untargetable guest remains excluded"); field<bool>(guest.bytes.data(), 0x135) = true;
    guest.valid = false; check(!invoke(targeting_match_four, guest, player), "native target validation is retained"); guest.valid = true;
    captains = {{local.bytes.data(), 0}};
    check(!invoke(targeting_match_four, guest, player), "downed guest identity remains known but absent alive pool cannot match");
    captains.push_back({guest.bytes.data(), 1}); remote_identities.clear();
    check(!invoke(targeting_match_four, guest, player), "disconnected or replaced pointer cannot match"); remote_identities.push_back(guest.bytes.data());
    captains.push_back({guest.bytes.data(), 3});
    check(!invoke(targeting_match_four, guest, player), "duplicate current party pointer fails closed"); captains.pop_back();
    captains[1].player = 0;
    check(!invoke(targeting_match_four, guest, player), "duplicate player identity fails closed"); captains[1].player = 1;
    captains[1].player = 4;
    check(!invoke(targeting_match_four, guest, player), "out-of-range player identity fails closed"); captains[1].player = 1;
    field<void*>(npc.bytes.data(), 0xE7C) = unrelated.bytes.data() + 0x5A8;
    check(!invoke(targeting_match_four, guest, player), "existing authored nonparty combat target is retained");
    field<void*>(npc.bytes.data(), 0xE7C) = local.bytes.data() + 0x5A8;
    check(invoke(targeting_match_four, guest, player), "existing local-party combat target does not block native guest matching");
    remote_identities.push_back(guest_two.bytes.data());
    field<void*>(npc.bytes.data(), 0xE7C) = guest_two.bytes.data() + 0x5A8;
    check(invoke(targeting_match_four, guest, player), "known downed remote target is not mistaken for authored NPC target");
    field<uint32_t>(local.bytes.data(), 0x30) = 7;
    check(!invoke(targeting_match_four, guest, player) && invoke(targeting_match_four, guest, {1, 7}),
          "player category follows actual local native political ID rather than a global hardcoded value");

    reset_case();
    std::array<std::array<uint8_t, 0x16A0>, 5> baseline;
    for (size_t i = 0; i < objects.size(); ++i) baseline[i] = objects[i]->bytes;
    for (unsigned round = 0; round < 2000; ++round)
        check(invoke(round % 2 ? targeting_match_four : targeting_match_three,
                     round % 3 ? guest : guest_two, player), "repeated independent native condition/action queries remain valid");
    for (size_t i = 0; i < objects.size(); ++i)
        check(objects[i]->bytes == baseline[i], "two thousand category matches mutate no native actor or ownership bytes");
    check(adventure_targeting::status().find("match_aliases=2000") != std::string::npos,
          "bounded diagnostics count repeated native matches without retaining candidates");
    check(valid_calls > 2000 && hostile_calls > 2000, "native validity and hostility checks remain active for repeated matches");
    adventure_targeting::reset();
    check(adventure_targeting::status().find("match_queries=0 match_aliases=0 match_rejected=0") != std::string::npos,
          "run reset clears targeting counters");
    std::printf("adventure targeting: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
