#include "adventure_targeting.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <utility>

#include "game.h"
#include "sporekit_game.hpp"

namespace adventure_targeting {
static_assert(sizeof(void*) == 4, "Native captain targeting requires Win32.");
namespace {
using namespace skg;
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
using Match = bool(SK_CDECL*)(void*, const void*);
using Hostile = bool(SK_THISCALL*)(void*, void*);
using ValidTarget = void*(SK_CDECL*)(void*);
Match original_match;
Hostile hostile;
ValidTarget valid_target;
std::array<uintptr_t, 4> match_returns{};
Ctx C;
bool ready;
uint64_t queries = 0, aliases = 0, rejected = 0;

bool living(void* object)
{
    if (!object || !readable(object, 0xE80) || field<bool>(object, 0x20) ||
        field<bool>(object, 0x21) || field<bool>(object, 0xB5E) ||
        !field<bool>(object, 0x135) || (field<uint32_t>(object, 0xB58) & 4) ||
        (field<uint32_t>(object, 0x110) & 0x10)) return false;
    const float health = field<float>(object, 0x5E0);
    return std::isfinite(health) && health > 0 && valid_target(object) == object;
}

bool SK_CDECL match(void* candidate, const void* target)
{
    const uintptr_t caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
    const bool native_result = original_match(candidate, target);
    if (native_result || !ready ||
        std::find(match_returns.begin(), match_returns.end(), caller) == match_returns.end() ||
        !C.active()) return native_result;
    void* npc = game::npc_ai_subject();
    if (!npc || !C.live_npc(npc) || !living(npc) || !target || !readable(target, 8) ||
        static_cast<const uint32_t*>(target)[0] != 1 || !candidate || !C.remote_captain(candidate))
        return native_result;
    ++queries;
    void* local = game::avatar();
    if (!local || !readable(local, 0x34) ||
        static_cast<const uint32_t*>(target)[1] != field<uint32_t>(local, 0x30)) {
        ++rejected; return native_result;
    }
    const auto captains = C.captains();
    bool alive_party = !captains.empty() && captains.size() <= 4;
    bool found = false;
    for (size_t index = 0; alive_party && index < captains.size(); ++index) {
        const auto& captain = captains[index];
        if (!captain.object || captain.player >= 4 ||
            std::any_of(captains.begin(), captains.begin() + index, [&](const Captain& other) {
                return other.object == captain.object || other.player == captain.player;
            })) alive_party = false;
        if (captain.object == candidate) found = true;
    }
    if (!alive_party || !found || candidate == npc || !living(candidate) || !hostile(npc, candidate)) {
        ++rejected; return native_result;
    }
    void* combat = field<void*>(npc, 0xE7C);
    if (combat && combat != static_cast<uint8_t*>(local) + 0x5A8 &&
        !C.remote_captain(reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(combat) - 0x5A8))) {
        ++rejected; return native_result;
    }
    // Both the condition and action use the native Scenario sensor and matcher.
    // Only the authored local-player political category gains current living
    // party members. Native AI retains visibility, choice, orders and attacks;
    // no political ID, avatar flag, sensor container or combat target is changed.
    ++aliases;
    return true;
}
} // namespace

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    if (ready || !host || !host->sym || !host->exe_sha256 || !host->hook_create ||
        !ctx.active || !ctx.live_npc || !ctx.captains || !ctx.remote_captain) return false;
    const char* hash = host->exe_sha256();
    if (!hash || std::strcmp(hash, kHash)) return false;
    const char* names[] = {
        "SporeCoop::Adventure::MatchesBehaviorTarget",
        "SporeCoop::Adventure::AttackConditionTargetReturn",
        "SporeCoop::Adventure::AttackCurrentTargetReturn",
        "SporeCoop::Adventure::AttackSensorTargetReturn",
        "SporeCoop::Adventure::AttackSensorConditionReturn",
        "SporeCoop::Adventure::IsHostileToCreature", "SporeCoop::BT_ValidTarget"
    };
    std::array<uintptr_t, std::size(names)> addresses{};
    for (size_t index = 0; index < addresses.size(); ++index) {
        addresses[index] = host->sym(names[index]);
        if (!addresses[index]) return false;
    }
    std::copy_n(addresses.begin() + 1, match_returns.size(), match_returns.begin());
    hostile = reinterpret_cast<Hostile>(addresses[5]); valid_target = reinterpret_cast<ValidTarget>(addresses[6]);
    if (host->hook_create(mod, reinterpret_cast<void*>(addresses[0]), reinterpret_cast<void*>(&match),
                          reinterpret_cast<void**>(&original_match))) return false;
    C = std::move(ctx); queries = aliases = rejected = 0; ready = true;
    return true;
}
bool available() { return ready; }
void reset() { queries = aliases = rejected = 0; }
std::string status()
{
    return "native captain targeting match_queries=" + std::to_string(queries) +
        " match_aliases=" + std::to_string(aliases) + " match_rejected=" + std::to_string(rejected);
}
} // namespace adventure_targeting
