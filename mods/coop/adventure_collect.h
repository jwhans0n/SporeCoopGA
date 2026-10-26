// Build-specific native adventure dependency collection, called outside the frame.
#pragma once

#include <string>
#include <vector>

#include "net/dbpf.h"
#include "sporekit.h"

namespace adventure_collect {

struct Presentation {
    std::string name, author;
    dbpf::Key image;
};
// Read-only, outside-frame metadata and installed thumbnail lookup.
Presentation presentation(const dbpf::Key& resource);

bool init(const SkHost* host, SkMod* mod);
bool available();
bool inspect(const dbpf::Key& scenario, dbpf::Key& locked_captain, std::string& error);
bool collect_creation(const dbpf::Key& creation, std::vector<dbpf::Record>& records, std::string& error);
bool collect(const dbpf::Key& scenario, std::vector<dbpf::Record>& records, std::string& error);
// Collect an adapter-owned native ScenarioResource::Write snapshot. The live
// editor resource is never read or changed by this routine. Outside frame only.
bool collect_snapshot(const dbpf::Key& root_key, const std::vector<uint8_t>& owned_native_scenario_bytes,
                      std::vector<dbpf::Record>& records, std::string& error);

} // namespace adventure_collect
