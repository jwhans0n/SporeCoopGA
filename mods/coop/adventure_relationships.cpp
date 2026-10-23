#include "adventure_relationships.h"

#include <cstring>
#include <utility>

#include "sporekit_game.hpp"

namespace adventure_relationships {
static_assert(sizeof(void*) == 4, "The native relationship adapter requires Win32.");
namespace {
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
using Relationship = int(SK_THISCALL*)(void*, const void*, void*, void*);
Relationship original;
Ctx C;
bool ready;
uint64_t aliases;
uint64_t authoritative;

int SK_THISCALL relationship(void* manager, const void* species_key, void* first, void* second)
{
    if (ready && C.active()) {
        bool alias_first = first && C.remote_captain(first);
        bool alias_second = second && C.remote_captain(second);
        void* avatar = C.local_avatar();
        if (avatar) {
            bool party_first = alias_first || first == avatar;
            bool party_second = alias_second || second == avatar;
            if (first && second && party_first != party_second && C.authoritative_relationship) {
                int value = -1;
                if (C.authoritative_relationship(party_first ? second : first, value) && value >= 0 && value <= 7) {
                    ++authoritative;
                    return value;
                }
            }
            if (alias_first || alias_second) {
                if (alias_first) first = avatar;
                if (alias_second) second = avatar;
                ++aliases;
            }
        }
    }
    // In Scenario the native function ignores species_key and derives authored
    // class/act relationships from the non-avatar participant. Keep every other
    // argument and the full int result unchanged, including the solo/null paths.
    return original(manager, species_key, first, second);
}
} // namespace

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    if (ready || !host || !host->sym || !host->exe_sha256 || !host->hook_create ||
        !ctx.active || !ctx.remote_captain || !ctx.local_avatar) return false;
    const char* hash = host->exe_sha256();
    if (!hash || std::strcmp(hash, kHash)) return false;
    auto* target = reinterpret_cast<void*>(host->sym("SporeCoop::Adventure::GetCreatureRelationship"));
    if (!target) return false;
    C = std::move(ctx);
    if (host->hook_create(mod, target, reinterpret_cast<void*>(&relationship),
                          reinterpret_cast<void**>(&original))) return false;
    aliases = 0;
    authoritative = 0;
    ready = true;
    return true;
}

bool available() { return ready; }
uint64_t aliased_calls() { return aliases; }
uint64_t authoritative_calls() { return authoritative; }
bool query_local_party(void* manager, const void* species_key, void* npc, int& value)
{
    if (!ready || !C.active() || !manager || !species_key || !npc) return false;
    void* avatar = C.local_avatar();
    if (!avatar || npc == avatar || C.remote_captain(npc)) return false;
    int result = original(manager, species_key, npc, avatar);
    if (result < 0 || result > 7) return false;
    value = result;
    return true;
}
void reset() { aliases = 0; authoritative = 0; }
} // namespace adventure_relationships
