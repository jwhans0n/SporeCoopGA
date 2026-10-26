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
    net::adventure_content::Bundle bundle;
    std::shared_ptr<const std::vector<uint8_t>> base_root;
};
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
