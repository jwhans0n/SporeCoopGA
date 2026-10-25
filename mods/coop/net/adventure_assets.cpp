#include "adventure_assets.h"

#include <algorithm>
#include <cstring>
#include <tuple>

#include "buffer.h"
#include "../../../updater/sha256.h"

namespace net::adventure_content {

static auto order(const dbpf::Key& k) { return std::tuple{k.group, k.type, k.instance}; }

bool valid_hash(const std::string& hash)
{
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}

static std::string manifest_hash(const dbpf::Key& root, const std::vector<Entry>& entries)
{
    Writer w;
    w.u32(root.instance); w.u32(root.type); w.u32(root.group);
    w.u16((uint16_t)entries.size());
    for (const auto& e : entries) {
        w.u32(e.key.instance); w.u32(e.key.type); w.u32(e.key.group);
        w.u32(e.size); w.raw(e.hash.data(), e.hash.size());
    }
    return upd::sha256_hex(w.data);
}

bool make_bundle(const dbpf::Key& root, std::vector<dbpf::Record> records, Bundle& out, std::string& error)
{
    out = {};
    if (!root.instance || !root.type || records.empty() || records.size() > kMaxRecords) {
        error = "invalid adventure resource count or root";
        return false;
    }
    std::sort(records.begin(), records.end(), [](const auto& a, const auto& b) { return order(a.key) < order(b.key); });
    Bundle b;
    b.root = root;
    bool found_root = false;
    for (size_t i = 0; i < records.size(); ++i) {
        const auto& r = records[i];
        if (!r.key.type || r.data.size() > kMaxRecordBytes || r.data.size() > kMaxBytes - b.bytes ||
            (i && r.key == records[i - 1].key)) {
            error = "duplicate adventure resource or resource size limit exceeded";
            return false;
        }
        found_root |= r.key == root;
        b.bytes += (uint32_t)r.data.size();
        b.manifest.push_back({r.key, (uint32_t)r.data.size(), upd::sha256_hex(r.data)});
    }
    if (!found_root) {
        error = "adventure resource is absent from its bundle";
        return false;
    }
    b.hash = manifest_hash(root, b.manifest);
    b.records = std::move(records);
    out = std::move(b);
    return true;
}

void Receiver::reset() { *this = Receiver{}; }

bool Receiver::begin(uint64_t run, const dbpf::Key& root, uint16_t count, uint32_t bytes,
                     const std::string& hash, std::string& error)
{
    reset();
    if (!run || !root.instance || !root.type || !count || count > kMaxRecords || bytes > kMaxBytes || !valid_hash(hash)) {
        error = "invalid adventure content offer";
        return false;
    }
    run_ = run;
    root_ = root;
    bytes_ = bytes;
    hash_ = hash;
    manifest_.resize(count);
    seen_.assign(count, false);
    return true;
}

bool Receiver::entry(uint16_t index, const Entry& e, std::string& error)
{
    if (!active() || manifest_done_ || index >= manifest_.size() || seen_[index] || !e.key.type ||
        !valid_hash(e.hash) || e.size > kMaxRecordBytes || e.size > bytes_ - declared_) {
        error = "invalid adventure content manifest entry";
        return false;
    }
    manifest_[index] = e;
    seen_[index] = true;
    declared_ += e.size;
    return true;
}

bool Receiver::manifest_done(const CacheRead& cache, std::vector<uint16_t>& missing, std::string& error)
{
    missing.clear();
    if (!active() || manifest_done_ || declared_ != bytes_ || std::find(seen_.begin(), seen_.end(), false) != seen_.end()) {
        error = "incomplete adventure content manifest";
        return false;
    }
    bool found_root = false;
    for (size_t i = 0; i < manifest_.size(); ++i) {
        found_root |= manifest_[i].key == root_;
        if (i && !(order(manifest_[i - 1].key) < order(manifest_[i].key))) {
            error = "adventure content manifest keys are duplicated or unsorted";
            return false;
        }
    }
    if (!found_root || manifest_hash(root_, manifest_) != hash_) {
        error = "adventure content manifest hash mismatch";
        return false;
    }
    records_.resize(manifest_.size());
    have_.assign(manifest_.size(), 0);
    for (size_t i = 0; i < manifest_.size(); ++i) {
        auto& r = records_[i];
        const auto& e = manifest_[i];
        r.key = e.key;
        if ((cache && cache(e.hash, r.data)) && r.data.size() == e.size && upd::sha256_hex(r.data) == e.hash) {
            have_[i] = e.size;
            received_ += e.size;
        } else {
            r.data.clear();
            r.data.resize(e.size);
            missing.push_back((uint16_t)i);
        }
    }
    manifest_done_ = true;
    return true;
}

bool Receiver::chunk(uint16_t index, uint32_t offset, const uint8_t* data, uint16_t size, std::string& error)
{
    if (!manifest_done_ || index >= records_.size() || !size || size > kChunkBytes || !data ||
        offset > records_[index].data.size() || size > records_[index].data.size() - offset) {
        error = "invalid adventure content chunk";
        return false;
    }
    auto& bytes = records_[index].data;
    if (offset < have_[index]) {
        if (size <= have_[index] - offset && !memcmp(bytes.data() + offset, data, size))
            return true; // identical duplicate, never count it twice
        error = "conflicting adventure content chunk";
        return false;
    }
    if (offset != have_[index]) {
        error = "adventure content chunk is out of order";
        return false;
    }
    memcpy(bytes.data() + offset, data, size);
    have_[index] += size;
    received_ += size;
    return true;
}

bool Receiver::finish(Bundle& out, std::string& error)
{
    if (!manifest_done_) {
        error = "adventure content manifest is incomplete";
        return false;
    }
    for (size_t i = 0; i < records_.size(); ++i)
        if (have_[i] != manifest_[i].size || upd::sha256_hex(records_[i].data) != manifest_[i].hash) {
            error = "adventure content is incomplete or its hash does not match";
            return false;
        }
    // The canonical manifest and every record hash were checked above. Move the buffers into
    // the finished bundle, avoiding a second 128 MiB copy in the game's 32-bit address space.
    Bundle b;
    b.root = root_;
    b.records = std::move(records_);
    b.manifest = manifest_;
    b.hash = hash_;
    b.bytes = bytes_;
    manifest_done_ = false;
    out = std::move(b);
    return true;
}

} // namespace net::adventure_content
