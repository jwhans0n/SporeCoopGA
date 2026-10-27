#include "editor_project.h"
#include "net/editor_protocol.h"
#include "net/editor_binary_delta.h"
#include "../../updater/sha256.h"
#include <algorithm>
#include <set>
#include <tuple>

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
    w.u32(kMapping); w.u32(s.metadata ? 2 : 1); w.u32(s.act);
    key(w, {s.source.instance, s.source.type, s.source.group});
    w.u32(uint32_t(s.bindings.size()));
    for (const auto& b : s.bindings) { id(w, b.object); w.u32(b.marker); }
    if(s.metadata) {
        w.u32(s.metadata->genre);
        for(const auto& text:s.metadata->text) {
            if(text.size()>4096 || text.find(u'\0')!=std::u16string::npos) {error="Invalid editor metadata text";return false;}
            w.u32(uint32_t(text.size()));for(char16_t c:text)w.u16(uint16_t(c));
        }
    }
    out = std::move(w.data); return true;
}
bool decode_mapping(const std::vector<uint8_t>& bytes, editor_native::Snapshot& s, std::string& error) {
    if (bytes.size() > editor_store::kMappingBytes) { error = "Editor mapping exceeds its limit"; return false; }
    net::Reader r(bytes.data(), bytes.size());
    editor_native::Snapshot candidate;
    const auto magic=r.u32(),version=r.u32();
    if (magic != kMapping || (version!=1 && version!=2)) { error = "Unsupported editor mapping"; return false; }
    candidate.act = r.u32(); auto source = key(r);
    candidate.source = {source.instance, source.type, source.group};
    uint32_t count = r.u32();
    if (candidate.act >= editor::Limits{}.acts || count > editor::Limits{}.placements || count > r.left() / 16) {
        error = "Invalid editor mapping size or act"; return false;
    }
    for (uint32_t i = 0; i < count; ++i) { auto object = id(r); candidate.bindings.push_back({object, r.u32()}); }
    if(version==2) {
        candidate.metadata.emplace();candidate.metadata->genre=r.u32();
        for(auto& text:candidate.metadata->text) {
            const auto size=r.u32();
            if(!r.ok() || size>4096 || size>r.left()/2) {error="Invalid editor metadata length";return false;}
            for(uint32_t i=0;i<size;++i) {const auto c=r.u16();if(!c){error="Invalid editor metadata text";return false;}text.push_back(char16_t(c));}
        }
    }
    if (!r.ok() || r.left() || !valid_bindings(candidate.bindings, error)) { if (error.empty()) error = "Truncated editor mapping"; return false; }
    s.act = candidate.act; s.source = candidate.source; s.bindings = std::move(candidate.bindings);
    s.metadata=std::move(candidate.metadata);
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
    mapping.metadata=resources.metadata;
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
    candidate.metadata=std::move(native.metadata);
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
    candidate.metadata=std::move(native.metadata);
    for (const auto& record : candidate.bundle.records) if (record.key == candidate.root) candidate.base_root = std::make_shared<const std::vector<uint8_t>>(record.data);
    resources = std::move(candidate); error.clear(); return true;
}
bool decode_resources(const std::vector<uint8_t>& bytes, Resources& resources, std::string& error,
                      std::shared_ptr<const std::vector<uint8_t>> base) {
    try { return decode_resource_envelope(bytes, resources, error, std::move(base)); }
    catch (...) { error = "Editor resource envelope decoding failed"; return false; }
}
namespace {
constexpr uint32_t kJoinResources = 0x314A4445;
constexpr uint32_t kInventory = 0x31494445;
auto record_order(const dbpf::Key& k) { return std::tuple{k.group, k.type, k.instance}; }
bool match(const net::adventure_content::Entry& claim, const std::vector<uint8_t>& bytes) {
    return bytes.size() == claim.size && upd::sha256_hex(bytes) == claim.hash;
}
std::string read_hash(net::Reader& r) {
    char bytes[64]{}; r.raw(bytes, sizeof bytes); return {bytes, sizeof bytes};
}
bool bundle_for_join(const Resources& resources, net::adventure_content::Bundle& bundle, std::string& error) {
    if (!resources.root.instance || resources.root.type != kScenario ||
        !net::adventure_content::make_bundle(resources.root, resources.bundle.records, bundle, error)) {
        if (error.empty()) error = "Invalid editor join resources"; return false;
    }
    for (const auto& record : bundle.records) if (record.key == resources.root && record.data.empty()) {
        error = "Editor root record is empty"; return false;
    }
    return true;
}
bool inventory_for(const Resources& resources, const net::adventure_content::Bundle& verified,
                   JoinInventory& out, std::string& error) {
    JoinInventory candidate;
    const std::vector<uint8_t>* base = resources.base_root.get();
    for (size_t i = 0; i < verified.records.size(); ++i) {
        if (verified.records[i].key == resources.root) { if (!base) base = &verified.records[i].data; }
        else candidate.records.push_back(verified.manifest[i]);
    }
    if (!base || base->empty() || base->size() > editor::binary_delta::kRootBytes) {
        error = "Invalid editor join immutable base"; return false;
    }
    candidate.base_size = uint32_t(base->size()); candidate.base_hash = upd::sha256_hex(*base);
    if (!verify_inventory(candidate, error)) return false;
    out = std::move(candidate); return true;
}
bool encode_join(const Resources& resources, const JoinInventory& peer, std::vector<uint8_t>& out, std::string& error) {
    if (!verify_inventory(peer, error)) return false;
    net::adventure_content::Bundle verified;
    if (!bundle_for_join(resources, verified, error)) return false;
    JoinInventory manifest; if (!inventory_for(resources, verified, manifest, error)) return false;
    const bool cached_base = manifest.base_size == peer.base_size && manifest.base_hash == peer.base_hash;
    auto cached_record = [&](const net::adventure_content::Entry& entry) {
        auto at = std::lower_bound(peer.records.begin(), peer.records.end(), entry.key,
            [](const auto& a, const auto& b) { return record_order(a.key) < record_order(b); });
        return at != peer.records.end() && at->key == entry.key && at->size == entry.size && at->hash == entry.hash;
    };
    const bool any_record = std::any_of(manifest.records.begin(), manifest.records.end(), cached_record);
    if (!cached_base && !any_record) return encode_resources(resources, out, error);
    Resources normalized; normalized.root = resources.root; normalized.act = resources.act;
    normalized.bindings = resources.bindings; normalized.base_root = resources.base_root;
    normalized.metadata=resources.metadata;
    for (const auto& record : verified.records) if (record.key == resources.root) normalized.bundle.records.push_back(record);
    if (!normalized.base_root) for (const auto& record : verified.records) if (record.key == resources.root)
        normalized.base_root = std::make_shared<const std::vector<uint8_t>>(record.data);
    std::vector<uint8_t> delta;
    if (!encode_delta_resources(normalized, delta, error)) return false;
    std::vector<std::pair<uint32_t, const std::vector<uint8_t>*>> included;
    if (!cached_base) included.push_back({0, normalized.base_root.get()});
    uint32_t index = 0;
    for (const auto& record : verified.records) if (!(record.key == resources.root)) {
        if (!cached_record(manifest.records[index])) included.push_back({index + 1, &record.data});
        ++index;
    }
    uint64_t bytes = 8 + 4 + delta.size() + 4 + 64 + 4 + manifest.records.size() * 80 + 4;
    for (const auto& entry : included) bytes += 8 + entry.second->size();
    if (bytes > editor::wire::kResourceBytes) { error = "Editor join envelope exceeds its limit"; return false; }
    net::Writer w; w.u32(kJoinResources); w.u32(1); w.u32(uint32_t(delta.size())); w.raw(delta.data(), delta.size());
    w.u32(manifest.base_size); w.raw(manifest.base_hash.data(), 64); w.u32(uint32_t(manifest.records.size()));
    for (const auto& entry : manifest.records) { key(w, entry.key); w.u32(entry.size); w.raw(entry.hash.data(), 64); }
    w.u32(uint32_t(included.size()));
    for (const auto& [at, data] : included) { w.u32(at); w.u32(uint32_t(data->size())); if (!data->empty()) w.raw(data->data(), data->size()); }
    out = std::move(w.data); error.clear(); return true;
}
bool decode_join(const std::vector<uint8_t>& bytes, const JoinCacheRead& cache, Resources& out, std::string& error) {
    if (bytes.size() > editor::wire::kResourceBytes) { error = "Editor join envelope exceeds its limit"; return false; }
    net::Reader r(bytes.data(), bytes.size()); const auto magic = r.u32();
    if (magic == kResources) return decode_resources(bytes, out, error);
    if (!r.ok() || magic != kJoinResources || r.u32() != 1) { error = "Unsupported editor join envelope"; return false; }
    const auto delta_size = r.u32();
    if (!r.ok() || delta_size < 12 || delta_size > r.left()) { error = "Invalid editor join root delta size"; return false; }
    std::vector<uint8_t> delta(r.cur(), r.cur() + delta_size); r.skip(delta_size);
    net::Reader header(delta.data(), delta.size());
    if (header.u32() != kResources || header.u32() != 2 || header.u32() != 0) { error = "Editor join requires a root-only v2 delta"; return false; }
    JoinInventory manifest; manifest.base_size = r.u32(); manifest.base_hash = read_hash(r);
    const auto count = r.u32();
    if (!r.ok() || !manifest.base_size || count >= net::adventure_content::kMaxRecords || count > r.left() / 80) {
        error = "Invalid editor join manifest size"; return false;
    }
    for (uint32_t i = 0; i < count; ++i) { auto k = key(r); auto size = r.u32(); manifest.records.push_back({k, size, read_hash(r)}); }
    if (!r.ok() || !verify_inventory(manifest, error)) return false;
    struct Body { const uint8_t* data = nullptr; uint32_t size = 0; bool present = false; };
    std::vector<Body> bodies(count + 1);
    const auto present = r.u32();
    if (!r.ok() || present > count + 1 || present > r.left() / 8) { error = "Invalid editor join content count"; return false; }
    uint32_t previous = 0;
    for (uint32_t i = 0; i < present; ++i) {
        const auto at = r.u32(), size = r.u32();
        if (!r.ok() || at > count || (i && at <= previous) || size > r.left() ||
            size != (at ? manifest.records[at - 1].size : manifest.base_size)) {
            error = "Invalid editor join content index or size"; return false;
        }
        bodies[at] = {r.cur(), size, true}; r.skip(size); previous = at;
    }
    if (!r.ok() || r.left()) { error = "Truncated or trailing editor join bytes"; return false; }
    auto obtain = [&](uint32_t at, std::vector<uint8_t>& data) {
        if (bodies[at].present) { data.assign(bodies[at].data, bodies[at].data + bodies[at].size); return true; }
        return at ? cache.record && cache.record(manifest.records[at - 1], data)
                  : cache.base && cache.base(manifest.base_hash, manifest.base_size, data);
    };
    std::vector<uint8_t> base;
    if (!obtain(0, base) || base.size() != manifest.base_size || upd::sha256_hex(base) != manifest.base_hash) {
        error = "Editor join is missing its verified immutable base"; return false;
    }
    Resources candidate;
    if (!decode_resources(delta, candidate, error, std::make_shared<const std::vector<uint8_t>>(std::move(base)))) return false;
    uint64_t total = candidate.bundle.bytes;
    for (const auto& entry : manifest.records) {
        if (entry.key == candidate.root || entry.size > net::adventure_content::kMaxBytes - total) {
            error = "Editor join dependency replaces its root or exceeds its limit"; return false;
        }
        total += entry.size;
    }
    std::vector<dbpf::Record> records = std::move(candidate.bundle.records);
    for (uint32_t i = 0; i < count; ++i) {
        std::vector<uint8_t> data;
        if (!obtain(i + 1, data) || !match(manifest.records[i], data)) {
            error = "Editor join is missing a verified dependency"; return false;
        }
        records.push_back({manifest.records[i].key, std::move(data)});
    }
    if (!net::adventure_content::make_bundle(candidate.root, std::move(records), candidate.bundle, error)) return false;
    out = std::move(candidate); error.clear(); return true;
}
}
bool verify_inventory(const JoinInventory& inventory, std::string& error) {
    if ((inventory.base_size == 0) != inventory.base_hash.empty() || inventory.base_size > editor::binary_delta::kRootBytes ||
        (inventory.base_size && !net::adventure_content::valid_hash(inventory.base_hash)) ||
        inventory.records.size() > net::adventure_content::kMaxRecords) { error = "Invalid editor cache inventory"; return false; }
    uint64_t total = 0;
    for (size_t i = 0; i < inventory.records.size(); ++i) {
        const auto& entry = inventory.records[i];
        if (!entry.key.type || entry.size > net::adventure_content::kMaxRecordBytes ||
            entry.size > net::adventure_content::kMaxBytes - total || !net::adventure_content::valid_hash(entry.hash) ||
            (i && !(record_order(inventory.records[i - 1].key) < record_order(entry.key)))) {
            error = "Invalid or unordered editor cache record claim"; return false;
        }
        total += entry.size;
    }
    error.clear(); return true;
}
bool encode_inventory(const JoinInventory& inventory, std::vector<uint8_t>& out, std::string& error) {
    try {
        if (!verify_inventory(inventory, error)) return false;
        net::Writer w; w.u32(kInventory); w.u32(1); w.u32(inventory.base_size);
        const auto hash = inventory.base_size ? inventory.base_hash : std::string(64, '\0');
        w.raw(hash.data(), 64); w.u32(uint32_t(inventory.records.size()));
        for (const auto& entry : inventory.records) { key(w, entry.key); w.u32(entry.size); w.raw(entry.hash.data(), 64); }
        if (w.size() > kInventoryBytes) { error = "Editor cache inventory exceeds its byte limit"; return false; }
        out = std::move(w.data); error.clear(); return true;
    } catch (...) { error = "Editor cache inventory encoding failed"; return false; }
}
bool decode_inventory(const std::vector<uint8_t>& bytes, JoinInventory& out, std::string& error) {
    try {
        if (bytes.size() > kInventoryBytes) { error = "Editor cache inventory exceeds its byte limit"; return false; }
        net::Reader r(bytes.data(), bytes.size());
        if (r.u32() != kInventory || r.u32() != 1) { error = "Unsupported editor cache inventory"; return false; }
        JoinInventory candidate; candidate.base_size = r.u32(); candidate.base_hash = read_hash(r);
        if (!candidate.base_size) {
            if (candidate.base_hash != std::string(64, '\0')) { error = "Invalid empty editor cache base claim"; return false; }
            candidate.base_hash.clear();
        }
        const auto count = r.u32();
        if (!r.ok() || count > net::adventure_content::kMaxRecords || count > r.left() / 80) { error = "Invalid editor cache inventory count"; return false; }
        for (uint32_t i = 0; i < count; ++i) { const auto k = key(r); const auto size = r.u32(); candidate.records.push_back({k, size, read_hash(r)}); }
        if (!r.ok() || r.left() || !verify_inventory(candidate, error)) { if (error.empty()) error = "Truncated or trailing editor cache inventory"; return false; }
        out = std::move(candidate); error.clear(); return true;
    } catch (...) { error = "Editor cache inventory decoding failed"; return false; }
}
bool make_inventory(const Resources& resources, JoinInventory& out, std::string& error) {
    try {
        net::adventure_content::Bundle verified;
        return bundle_for_join(resources, verified, error) && inventory_for(resources, verified, out, error);
    } catch (...) { error = "Editor cache inventory hashing failed"; return false; }
}
bool make_inventory(const JoinInventory& catalog, const JoinCacheRead& cache, JoinInventory& out, std::string& error) {
    try {
        if (!verify_inventory(catalog, error)) return false;
        JoinInventory verified; std::vector<uint8_t> bytes;
        if (catalog.base_size && cache.base && cache.base(catalog.base_hash, catalog.base_size, bytes) &&
            bytes.size() == catalog.base_size && upd::sha256_hex(bytes) == catalog.base_hash) {
            verified.base_size = catalog.base_size; verified.base_hash = catalog.base_hash;
        }
        for (const auto& entry : catalog.records) {
            bytes.clear();
            if (cache.record && cache.record(entry, bytes) && match(entry, bytes)) verified.records.push_back(entry);
        }
        out = std::move(verified); error.clear(); return true;
    } catch (...) { error = "Editor persistent cache verification failed"; return false; }
}
bool encode_join_resources(const Resources& resources, const JoinInventory& peer, std::vector<uint8_t>& out, std::string& error) {
    try { return encode_join(resources, peer, out, error); }
    catch (...) { error = "Editor join encoding failed"; return false; }
}
bool decode_join_resources(const std::vector<uint8_t>& bytes, const JoinCacheRead& cache, Resources& out, std::string& error) {
    try { return decode_join(bytes, cache, out, error); }
    catch (...) { error = "Editor join cache decoding failed"; return false; }
}
bool decode_join_resources(const std::vector<uint8_t>& bytes, const Resources& cached, Resources& out, std::string& error) {
    JoinCacheRead reader;
    reader.base = [&](const std::string& hash, uint32_t size, std::vector<uint8_t>& data) {
        if (!cached.base_root || cached.base_root->size() != size || upd::sha256_hex(*cached.base_root) != hash) return false;
        data = *cached.base_root; return true;
    };
    reader.record = [&](const net::adventure_content::Entry& entry, std::vector<uint8_t>& data) {
        for (const auto& record : cached.bundle.records) if (record.key == entry.key && match(entry, record.data)) { data = record.data; return true; }
        return false;
    };
    return decode_join_resources(bytes, reader, out, error);
}
bool native_snapshot(const Resources& resources, const editor::Document& doc, editor_native::Snapshot& out, std::string& error) {
    editor_native::Snapshot candidate;
    candidate.source = {resources.root.instance, resources.root.type, resources.root.group};
    candidate.act = resources.act; candidate.bindings = resources.bindings; candidate.document = doc;
    candidate.metadata=resources.metadata;
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
