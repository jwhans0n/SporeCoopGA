#include "native_behavior_tree.h"
#include "sporekit_game.hpp"

#include <array>

namespace native_behavior_tree {
namespace {
// Verified GOG 2024 alias at BC9160: cdecl, nine stack words, bool in AL.
// Preserve float delta, double time and all pointers/state words bit for bit.
using Native = bool(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                              uint32_t, uint32_t, uint32_t, uint32_t);
const SkHost* H;
SkMod* M;
uintptr_t entry;
Native original;
std::array<Filter, 2> filters{};

bool SK_CDECL tick(uint32_t delta, uint32_t time_low, uint32_t time_high, uint32_t tree,
                   uint32_t agent, uint32_t context, uint32_t state0, uint32_t state1, uint32_t state2)
{
    for (auto filter : filters)
        if (filter && !filter(reinterpret_cast<void*>(static_cast<uintptr_t>(agent)))) return false;
    return original && original(delta, time_low, time_high, tree, agent, context, state0, state1, state2);
}
} // namespace

bool available(uintptr_t expected_entry) { return expected_entry && entry == expected_entry && original; }

bool subscribe(const SkHost* host, SkMod* mod, uintptr_t expected_entry, Scope scope, Filter filter)
{
    const size_t index = static_cast<size_t>(scope);
    if (!host || !host->sym || !host->hook_create || !filter || index >= filters.size() || !expected_entry ||
        host->sym("Simulator::BehaviorTreeTickGeneric") != expected_entry ||
        (filters[index] && filters[index] != filter)) return false;
    if (original) {
        if (H != host || M != mod || !available(expected_entry)) return false;
    } else {
        Native forward = nullptr;
        if (host->hook_create(mod, reinterpret_cast<void*>(expected_entry), reinterpret_cast<void*>(tick),
                              reinterpret_cast<void**>(&forward)) != 0 || !forward) return false;
        H = host; M = mod; entry = expected_entry; original = forward;
    }
    filters[index] = filter;
    return true;
}

void unsubscribe(Scope scope, Filter filter)
{
    const size_t index = static_cast<size_t>(scope);
    if (index < filters.size() && filters[index] == filter) filters[index] = nullptr;
}

} // namespace native_behavior_tree
