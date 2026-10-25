#pragma once

#include <cstdint>
#include <string>

#include "net/dbpf.h"
#include "sporekit.h"

namespace adventure_progress {

struct Snapshot {
    dbpf::Key captain;
    dbpf::Key properties;
    int32_t xp = 0;
    int32_t level = 0;
    bool xp_present = false;
    bool level_present = false;
};

bool init(const SkHost* host, SkMod* mod);
bool available();
// Read only, on the game main thread outside Present. Pass the original local
// captain key. Native resource loading can return a cached property list; proving
// disk persistence requires reading again after restarting the same test profile.
bool read(const dbpf::Key& captain, Snapshot& result, std::string& error);

} // namespace adventure_progress
