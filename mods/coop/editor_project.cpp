#include "editor_project.h"
#include "net/editor_protocol.h"
#include "net/editor_binary_delta.h"
#include <algorithm>
#include <set>

namespace editor_project {
namespace {
constexpr uint32_t kMapping = 0x314D4445, kResources = 0x31524445;
constexpr uint32_t kScenario = 0x366A930D;
void id(net::Writer& w, editor::ObjectId v) { w.u32(uint32_t(v.id)); w.u32(uint32_t(v.id >> 32)); w.u32(v.generation); }
editor::ObjectId id(net::Reader& r) { uint64_t lo = r.u32(), hi = r.u32(); return {lo | (hi << 32), r.u32()}; }
void key(net::Writer& w, dbpf::Key k) { w.u32(k.instance); w.u32(k.type); w.u32(k.group); }
dbpf::Key key(net::Reader& r) { return {r.u32(), r.u32(), r.u32()}; }
bool valid_bindings(const std::vector<editor_native::Binding>& bindings, std::string& error) {
    std::set<editor::ObjectId> objects;
    std::set<uint32_t> markers;
    if (bindings.size() > editor::Limits{}.placements) { error = "Too many editor placement bindings"; return false; }
    for (const auto& b : bindings) {
        if (!b.object.id || !b.object.generation || !objects.insert(b.object).second || !markers.insert(b.marker).second) {
            error = "Invalid or duplicate editor placement binding"; return false;
        }
    }
    return true;
}
}
bool encode_mapping(const editor_native::Snapshot& s, std::vector<uint8_t>& out, std::string& error) {
    if (s.act >= editor::Limits{}.acts || !valid_bindings(s.bindings, error)) return false;
    net::Writer w;
    w.u32(kMapping); w.u32(1); w.u32(s.act);
    key(w, {s.source.instance, s.source.type, s.source.group});
    w.u32(uint32_t(s.bindings.size()));
    for (const auto& b : s.bindings) { id(w, b.object); w.u32(b.marker); }
    out = std::move(w.data); return true;
}
bool decode_mapping(const std::vector<uint8_t>& bytes, editor_native::Snapshot& s, std::string& error) {
    if (bytes.size() > editor_store::kMappingBytes) { error = "Editor mapping exceeds its limit"; return false; }
    net::Reader r(bytes.data(), bytes.size());
    editor_native::Snapshot candidate;
    if (r.u32() != kMapping || r.u32() != 1) { error = "Unsupported editor mapping"; return false; }
    candidate.act = r.u32(); auto source = key(r);
    candidate.source = {source.instance, source.type, source.group};
    uint32_t count = r.u32();
    if (candidate.act >= editor::Limits{}.acts || count > editor::Limits{}.placements || count > r.left() / 16) {
        error = "Invalid editor mapping size or act"; return false;
    }
    for (uint32_t i = 0; i < count; ++i) { auto object = id(r); candidate.bindings.push_back({object, r.u32()}); }
    if (!r.ok() || r.left() || !valid_bindings(candidate.bindings, error)) { if (error.empty()) error = "Truncated editor mapping"; return false; }
    s.act = candidate.act; s.source = candidate.source; s.bindings = std::move(candidate.bindings);
    return true;
}
static bool encode_resource_envelope(const Resources& resources, std::vector<uint8_t>& out, std::string& error, bool delta) {
    net::adventure_content::Bundle verified;
    std::vector<dbpf::Record> input;
    if (delta) { for (const auto& record : resources.bundle.records) if (record.key == resources.root) input.push_back(record); }
    else input = resources.bundle.records;
    if (resources.root.type != kScenario || !resources.root.instance ||
        !net::adventure_content::make_bundle(resources.root, std::move(input), verified, error)) {
        if (error.empty()) error = "Invalid editor adventure resource key"; return false;
    }
    editor_native::Snapshot mapping;
    mapping.source = {resources.root.instance, resources.root.type, resources.root.group};
    mapping.act = resources.act; mapping.bindings = resources.bindings;
    std::vector<uint8_t> bindings;
    if (!encode_mapping(mapping, bindings, error)) return false;
    const std::vector<uint8_t>* root = nullptr;
    std::vector<dbpf::Record> dependencies;
    for (const auto& record : verified.records) {
        if (record.key == resources.root) root = &record.data;
        else if (!delta) dependencies.push_back(record);
    }
    if (!root || root->empty()) { error = "Editor root record is empty"; return false; }
    if (delta && !resources.base_root) { error = "Editor root delta requires its immutable base"; return false; }
    const auto& base = resources.base_root ? *resources.base_root : *root;
    if (base.empty()) { error = "Editor root delta base is empty"; return false; }
    std::vector<uint8_t> patch;
    if (!editor::binary_delta::encode(base, *root, patch, error)) return false;
    auto package = dependencies.empty() ? std::vector<uint8_t>{} : dbpf::write(dependencies);
    const size_t base_size = delta ? 0 : base.size();
    if (package.size() + bindings.size() + base_size + patch.size() + 28 > editor::wire::kResourceBytes) {
        error = "Editor resource envelope exceeds its transfer limit"; return false;
    }
    net::Writer w; w.u32(kResources); w.u32(2); w.u32(delta ? 0 : 1); w.u32(uint32_t(bindings.size()));
    w.raw(bindings.data(), bindings.size()); w.u32(uint32_t(base_size)); if (base_size) w.raw(base.data(), base_size);
    w.u32(uint32_t(patch.size())); w.raw(patch.data(), patch.size());
    w.u32(uint32_t(package.size())); if (!package.empty()) w.raw(package.data(), package.size());
    out = std::move(w.data); error.clear(); return true;
}
bool encode_resources(const Resources& resources, std::vector<uint8_t>& out, std::string& error) {
    try { return encode_resource_envelope(resources, out, error, false); }
    catch (...) { error = "Editor resource envelope encoding failed"; return false; }
}
bool encode_delta_resources(const Resources& resources, std::vector<uint8_t>& out, std::string& error) {
    try { return encode_resource_envelope(resources, out, error, true); }
    catch (...) { error = "Editor root delta encoding failed"; return false; }
}
static bool decode_v2(net::Reader& r, Resources& out, std::string& error,
                      std::shared_ptr<const std::vector<uint8_t>> base) {
    const auto flags = r.u32(), mapping_size = r.u32();
    if (!r.ok() || flags > 1 || mapping_size < 28 || mapping_size > editor_store::kMappingBytes || mapping_size > r.left()) { error = "Invalid editor delta mapping envelope"; return false; }
    std::vector<uint8_t> mapping(mapping_size); if (!r.raw(mapping.data(), mapping.size())) return false;
    editor_native::Snapshot native;
    if (!decode_mapping(mapping, native, error)) return false;
    const auto base_size = r.u32();
    if (!r.ok() || base_size > editor::binary_delta::kRootBytes || base_size > r.left() || (flags == 0 && base_size) || (flags == 1 && !base_size)) { error = "Invalid editor delta base size"; return false; }
    if (flags == 1) {
        if (base && (base->size() != base_size || !std::equal(base->begin(), base->end(), r.cur()))) { error = "Editor immutable root base changed"; return false; }
        if (!base) base = std::make_shared<const std::vector<uint8_t>>(r.cur(), r.cur() + base_size);
        r.skip(base_size);
    }
    if (!base || base->empty() || base->size() > editor::binary_delta::kRootBytes) { error = "Editor root delta is missing its verified base"; return false; }
    const auto patch_size = r.u32();
    if (!r.ok() || patch_size > editor::binary_delta::kPatchBytes || patch_size > r.left()) { error = "Invalid editor root patch size"; return false; }
    std::vector<uint8_t> patch(r.cur(), r.cur() + patch_size); r.skip(patch_size);
    const auto package_size = r.u32();
    if (!r.ok() || package_size != r.left() || (!flags && package_size)) { error = "Invalid editor delta dependency envelope"; return false; }
    std::vector<uint8_t> root;
    if (!editor::binary_delta::apply(*base, patch, root, error)) return false;
    if (root.empty()) { error = "Editor root record is empty"; return false; }
    Resources candidate; candidate.root = {native.source.instance, native.source.type, native.source.group};
    if (!candidate.root.instance || candidate.root.type != kScenario) { error = "Invalid editor root resource"; return false; }
    std::vector<dbpf::Record> records; records.push_back({candidate.root, std::move(root)});
    uint64_t total = records.front().data.size();
    if (package_size) {
        std::vector<uint8_t> package(r.cur(), r.cur() + package_size); r.skip(package_size);
        dbpf::Package db;
        if (!db.load(package) || db.entries().size() >= net::adventure_content::kMaxRecords) { error = "Invalid editor resource package"; return false; }
        for (const auto& entry : db.entries()) {
            if (entry.key == candidate.root) { error = "Editor delta dependency package replaces its root"; return false; }
            if (entry.refpack || entry.usize != entry.size || entry.size > net::adventure_content::kMaxRecordBytes ||
                total > net::adventure_content::kMaxBytes || entry.size > net::adventure_content::kMaxBytes - total) { error = "Editor package record exceeds its limit"; return false; }
            dbpf::Record record{entry.key, {}};
            if (!db.read(entry, record.data)) { error = "Unreadable editor resource record"; return false; }
            total += record.data.size(); records.push_back(std::move(record));
        }
    }
    if (!r.ok() || r.left() || !net::adventure_content::make_bundle(candidate.root, std::move(records), candidate.bundle, error)) return false;
    candidate.act = native.act; candidate.bindings = std::move(native.bindings); candidate.base_root = std::move(base);
    out = std::move(candidate); error.clear(); return true;
}
static bool decode_resource_envelope(const std::vector<uint8_t>& bytes, Resources& resources, std::string& error,
                                     std::shared_ptr<const std::vector<uint8_t>> base) {
    if (bytes.size() > editor::wire::kResourceBytes) { error = "Editor resource envelope exceeds its limit"; return false; }
    net::Reader r(bytes.data(), bytes.size());
    const auto magic = r.u32(), version = r.u32();
    if (!r.ok() || magic != kResources || (version != 1 && version != 2)) { error = "Unsupported editor resource envelope"; return false; }
    if (version == 2) return decode_v2(r, resources, error, std::move(base));
    uint32_t size = r.u32();
    if (size < 28 || size > editor_store::kMappingBytes || size > r.left()) { error = "Invalid editor mapping envelope"; return false; }
    std::vector<uint8_t> mapping(size);
    if (!r.raw(mapping.data(), size)) return false;
    editor_native::Snapshot native;
    if (!decode_mapping(mapping, native, error)) return false;
    size = r.u32();
    if (!r.ok() || size != r.left()) { error = "Invalid editor package envelope"; return false; }
    std::vector<uint8_t> package(size);
    if (size && !r.raw(package.data(), size)) return false;
    dbpf::Package db;
    if (!db.load(package) || db.entries().size() > net::adventure_content::kMaxRecords) { error = "Invalid editor resource package"; return false; }
    std::vector<dbpf::Record> records;
    uint64_t total = 0;
    for (const auto& entry : db.entries()) {
        const dbpf::Key root{native.source.instance, native.source.type, native.source.group};
        if (entry.key == root && !entry.size) { error = "Editor root record is empty"; return false; }
        // Bound BEFORE RefPack expands an attacker-controlled record.
        if (entry.refpack || entry.usize != entry.size || entry.size > net::adventure_content::kMaxRecordBytes ||
            total > net::adventure_content::kMaxBytes || entry.size > net::adventure_content::kMaxBytes - total) {
            error = "Editor package record exceeds its limit"; return false;
        }
        dbpf::Record record{entry.key, {}};
        if (!db.read(entry, record.data)) { error = "Unreadable editor resource record"; return false; }
        total += record.data.size(); records.push_back(std::move(record));
    }
    Resources candidate;
    candidate.root = {native.source.instance, native.source.type, native.source.group};
    if (!candidate.root.instance || candidate.root.type != kScenario ||
        !net::adventure_content::make_bundle(candidate.root, std::move(records), candidate.bundle, error)) {
        if (error.empty()) error = "Invalid editor root resource"; return false;
    }
    candidate.act = native.act; candidate.bindings = std::move(native.bindings);
    for (const auto& record : candidate.bundle.records) if (record.key == candidate.root) candidate.base_root = std::make_shared<const std::vector<uint8_t>>(record.data);
    resources = std::move(candidate); error.clear(); return true;
}
bool decode_resources(const std::vector<uint8_t>& bytes, Resources& resources, std::string& error,
                      std::shared_ptr<const std::vector<uint8_t>> base) {
    try { return decode_resource_envelope(bytes, resources, error, std::move(base)); }
    catch (...) { error = "Editor resource envelope decoding failed"; return false; }
}
bool native_snapshot(const Resources& resources, const editor::Document& doc, editor_native::Snapshot& out, std::string& error) {
    editor_native::Snapshot candidate;
    candidate.source = {resources.root.instance, resources.root.type, resources.root.group};
    candidate.act = resources.act; candidate.bindings = resources.bindings; candidate.document = doc;
    if (candidate.bindings.size() != doc.placements.size()) { error = "Editor resource bindings do not match the document"; return false; }
    for (const auto& b : candidate.bindings) if (!doc.placements.contains(b.object)) { error = "Editor resource contains an unknown placement identity"; return false; }
    for (const auto& record : resources.bundle.records) if (record.key == resources.root) candidate.bytes = record.data;
    if (candidate.bytes.empty()) { error = "Editor root record is missing"; return false; }
    out = std::move(candidate); return true;
}
bool store_snapshot(const editor::State& state, const editor_native::Snapshot& native, const std::string& metadata,
                    editor_store::Snapshot& out, std::string& error) {
    editor_store::Snapshot candidate;
    candidate.project = state.document.project; candidate.epoch = state.document.epoch; candidate.revision = state.document.revision;
    candidate.native_snapshot = native.bytes; candidate.metadata = metadata;
    if (!editor::wire::encode_state(state, candidate.document, error) || !encode_mapping(native, candidate.id_mapping, error)) return false;
    out = std::move(candidate); return true;
}
bool load_snapshot(const editor_store::Snapshot& stored, editor::State& state, editor_native::Snapshot& native, std::string& error) {
    editor::State candidate;
    editor_native::Snapshot snapshot;
    if (!editor::wire::decode_state(stored.document, candidate, error) || !decode_mapping(stored.id_mapping, snapshot, error)) return false;
    if (candidate.document.project != stored.project || candidate.document.epoch != stored.epoch || candidate.document.revision != stored.revision ||
        snapshot.bindings.size() != candidate.document.placements.size()) { error = "Editor checkpoint identities do not match"; return false; }
    for (const auto& binding : snapshot.bindings) if (!candidate.document.placements.contains(binding.object)) { error = "Editor checkpoint has an unknown placement binding"; return false; }
    snapshot.document = candidate.document; snapshot.bytes = stored.native_snapshot;
    state = std::move(candidate); native = std::move(snapshot); return true;
}
} // namespace editor_project
