// Build-specific native adventure dependency collection, called outside the frame.
#pragma once

#include <string>
#include <vector>

#include "net/dbpf.h"
#include "editor_saved_dependencies.h"
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
// Probe the resource manager without opening or changing a database.
bool unused_snapshot_key(const dbpf::Key&);
// Outside frame, before shared editor import/registration. Only the exact
// mutable adventure root is exempt from installed effective-byte comparison.
bool validate_uploaded_records(const dbpf::Key& mutable_root,const std::vector<dbpf::Record>& records,std::string& error);
bool inspect(const dbpf::Key& scenario, dbpf::Key& locked_captain, std::string& error);
bool collect_creation(const dbpf::Key& creation, std::vector<dbpf::Record>& records, std::string& error);
bool collect(const dbpf::Key& scenario, std::vector<dbpf::Record>& records, std::string& error);
// Collect an adapter-owned native ScenarioResource::Write snapshot. The live
// editor resource is never read or changed by this routine. Outside frame only.
bool collect_snapshot(const dbpf::Key& root_key, const std::vector<uint8_t>& owned_native_scenario_bytes,
                      std::vector<dbpf::Record>& records, std::string& error);
// Exact pre-normalization Saved image. The owned Native reader observes every
// Captain/Crew/class reference and the actual installed stock database; absence
// from the custom closure never establishes stock provenance. Outside frame.
bool saved_dependencies(const dbpf::Key&,const std::vector<uint8_t>& genuine_saved,
                        editor_saved_dependencies::Inventory&,std::vector<dbpf::Record>&,std::string&);
// Decode only the exact raw reference slots, without resolving or loading any
// dependency. Used before cold admission to bind a receipt to Native's roster.
bool saved_references(const dbpf::Key&,const std::vector<uint8_t>& genuine_saved,
                      editor_saved_dependencies::Inventory&,std::string&);
// Caller holds the freshly reacquired selected Native root outside Present.
// No Native callback occurs while reading its complete raw reference roster.
bool selected_saved_references(void* owned_root,editor_saved_dependencies::Inventory&,std::string&);

} // namespace adventure_collect
