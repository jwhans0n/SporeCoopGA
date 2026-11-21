// Shared owner of the verified BehaviorTreeTickGeneric/TickBehaviorTree alias.
#pragma once

#include <cstdint>
#include "sporekit.h"

namespace native_behavior_tree {

enum class Scope : uint8_t { Adventure, Civilization };
// The predicate receives the fifth native stack word as the exact agent pointer.
// False leaves the native tree pending. True preserves the sole native forward.
using Filter = bool(*)(void* agent);

// The first verified subscriber creates the one native detour. Later subscribers
// must name that same entry, host and module. Fixed scopes cannot overwrite each
// other's filter. No participant/run callbacks or objects are retained here.
bool subscribe(const SkHost* host, SkMod* mod, uintptr_t expected_entry, Scope scope, Filter filter);
void unsubscribe(Scope scope, Filter filter); // exact owner only, before its context tears down
bool available(uintptr_t expected_entry);

} // namespace native_behavior_tree
