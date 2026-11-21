// Owned editor resource envelopes. No game pointers or native row layouts.
#pragma once
#include "editor_native.h"
#include "editor_store.h"
#include "net/adventure_assets.h"
#include <memory>

namespace editor_project {
struct Resources {
    dbpf::Key root;
    uint32_t act = 0;
    std::vector<editor_native::Binding> bindings;
    std::optional<editor_native::Snapshot::Metadata> metadata;
    net::adventure_content::Bundle bundle;
    std::shared_ptr<const std::vector<uint8_t>> base_root;
};
// Claims identify uncompressed immutable bytes. The base has no resource key:
// native Save As may rotate the current root key while this base stays fixed.
struct JoinInventory {
    uint32_t base_size = 0;
    std::string base_hash;
    std::vector<net::adventure_content::Entry> records;
};
constexpr size_t kInventoryBytes = 256u << 10;
struct JoinCacheRead {
    // Callers must bound their file reads by the supplied expected size. Every
    // successful hit is checked again for exact size and SHA before use.
    std::function<bool(const std::string&, uint32_t, std::vector<uint8_t>&)> base;
    std::function<bool(const net::adventure_content::Entry&, std::vector<uint8_t>&)> record;
};
bool verify_inventory(const JoinInventory&, std::string&);
bool encode_inventory(const JoinInventory&, std::vector<uint8_t>&, std::string&);
bool decode_inventory(const std::vector<uint8_t>&, JoinInventory&, std::string&);
bool make_inventory(const Resources&, JoinInventory&, std::string&);
// A persistent-cache catalog may be stale. Only independently verified hits are
// advertised; missing/corrupt files are omitted and can be transferred normally.
bool make_inventory(const JoinInventory& catalog, const JoinCacheRead&, JoinInventory&, std::string&);
bool encode_join_resources(const Resources&, const JoinInventory&, std::vector<uint8_t>&, std::string&);
bool decode_join_resources(const std::vector<uint8_t>&, const JoinCacheRead&, Resources&, std::string&);
bool decode_join_resources(const std::vector<uint8_t>&, const Resources& cache, Resources&, std::string&);
bool encode_mapping(const editor_native::Snapshot&, std::vector<uint8_t>&, std::string&);
bool decode_mapping(const std::vector<uint8_t>&, editor_native::Snapshot&, std::string&);
bool encode_resources(const Resources&, std::vector<uint8_t>&, std::string&);
bool encode_delta_resources(const Resources&, std::vector<uint8_t>&, std::string&);
bool decode_resources(const std::vector<uint8_t>&, Resources&, std::string&,
                      std::shared_ptr<const std::vector<uint8_t>> base_root = {});
bool native_snapshot(const Resources&, const editor::Document&, editor_native::Snapshot&, std::string&);
bool store_snapshot(const editor::State&, const editor_native::Snapshot&, const std::string& metadata,
                    editor_store::Snapshot&, std::string&);
bool load_snapshot(const editor_store::Snapshot&, editor::State&, editor_native::Snapshot&, std::string&);
} // namespace editor_project
