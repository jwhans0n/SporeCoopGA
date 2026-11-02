// Owned creation versioning and cast changes. No live editor/scene mutations.
#pragma once
#include "editor_native.h"
#include "net/dbpf.h"
#include <optional>

namespace editor_creation_native {
bool init(const SkHost*,SkMod*);
void shutdown();
// Effective native lookup, including the BEM type aliases. Missing lookup
// infrastructure is treated as occupied; callers must also check draft keys.
bool resource_reserved(const editor::ResourceKey&);
// Outside Present: inspector selection, falling back to the selected placement.
std::optional<editor::ObjectId> selected_cast();
// Unique match only: callers must resolve an ambiguous picker selection.
std::optional<editor::ObjectId> resolve_cast_asset(const editor::Document&,const editor::ResourceKey&);
bool prepare_cast_save(const editor_native::Snapshot&,const editor::ResourceKey&,
                       std::optional<editor::ObjectId> target,editor_native::Snapshot&,std::string&);
// Fresh project-owned aliases avoid per-profile IDs and in-place resource
// replacement. Record bytes stay immutable; authored ancestry remains original.
bool immutable_version(const editor::ProjectId&,const editor::ResourceKey& source,
                       const std::vector<dbpf::Record>& source_records,
                       const std::function<bool(const editor::ResourceKey&)>& reserved,
                       editor::ResourceKey& version,std::vector<dbpf::Record>& records,std::string&);
bool collect_version(const editor::ProjectId&,const editor::ResourceKey& source,
                     const std::function<bool(const editor::ResourceKey&)>& reserved,
                     editor::ResourceKey& version,std::vector<dbpf::Record>& records,std::string&);
} // namespace editor_creation_native
