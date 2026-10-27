#include "editor_project.h"
#include "net/buffer.h"
#include "../../updater/sha256.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace ep = editor_project;
static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)

static uint32_t get32(const std::vector<uint8_t>& bytes, size_t at) {
    CHECK(at <= bytes.size() && bytes.size() - at >= 4);
    return uint32_t(bytes[at]) | uint32_t(bytes[at + 1]) << 8 | uint32_t(bytes[at + 2]) << 16 | uint32_t(bytes[at + 3]) << 24;
}
static void put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    CHECK(at <= bytes.size() && bytes.size() - at >= 4);
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = uint8_t(value >> (i * 8));
}
static size_t row(const std::vector<uint8_t>& package, size_t index) {
    auto at = get32(package, 0x40); CHECK(get32(package, at) == 4); // dbpf::write's full-key index.
    CHECK(index < get32(package, 0x24)); return at + 8 + index * 28;
}
static std::vector<uint8_t> envelope(const editor_native::Snapshot& native, const std::vector<uint8_t>& package) {
    std::vector<uint8_t> mapping; std::string error; CHECK(ep::encode_mapping(native, mapping, error));
    net::Writer w; w.u32(0x31524445); w.u32(1); w.u32(uint32_t(mapping.size()));
    w.raw(mapping.data(), mapping.size()); w.u32(uint32_t(package.size())); w.raw(package.data(), package.size()); return std::move(w.data);
}
static void reject_package(const editor_native::Snapshot& native, const std::vector<uint8_t>& package, const char* reason) {
    ep::Resources out; out.root = {123, 456, 789}; out.act = 3; out.bindings = {{{99, 3}, 4}};
    out.bundle.hash = "unchanged sentinel"; out.bundle.bytes = 7; out.bundle.records = {{{9, 8, 7}, {6, 5, 4}}};
    std::string error; CHECK(!ep::decode_resources(envelope(native, package), out, error));
    CHECK(error == reason); CHECK(out.root == (dbpf::Key{123, 456, 789}) && out.act == 3);
    CHECK(out.bindings == (std::vector<editor_native::Binding>{{{99, 3}, 4}}));
    CHECK(out.bundle.hash == "unchanged sentinel" && out.bundle.bytes == 7 && out.bundle.records.size() == 1);
    CHECK(out.bundle.records[0].key == (dbpf::Key{9, 8, 7}) && out.bundle.records[0].data == (std::vector<uint8_t>{6, 5, 4}));
}
static void test_record_pre_read_limits(const editor_native::Snapshot& native) {
    const dbpf::Key root{native.source.instance, native.source.type, native.source.group};
    const char* limit = "Editor package record exceeds its limit";
    auto ordinary = dbpf::write({{root, {1, 2, 3, 4, 5}}});
    // The generic DBPF reader ignores usize for uncompressed entries. The editor
    // envelope must reject both directions, including a legacy unspecified zero.
    for (uint32_t usize : {0u, 4u, 6u, 0xFFFFFFFFu}) {
        auto mismatch = ordinary; put32(mismatch, row(mismatch, 0) + 20, usize);
        reject_package(native, mismatch, limit);
    }
    auto zero_size = ordinary; put32(zero_size, row(zero_size, 0) + 16, 0x80000000u);
    reject_package(native, zero_size, "Editor root record is empty");
    auto empty = dbpf::write({{root, {}}}); reject_package(native, empty, "Editor root record is empty");
    auto zero_dependency = dbpf::write({{root, {1}}, {{333, 7, 0}, {5, 6}}});
    put32(zero_dependency, row(zero_dependency, 1) + 16, 0x80000000u);
    reject_package(native, zero_dependency, limit);
    auto compressed = ordinary; compressed[row(compressed, 0) + 24] = 0xFF; compressed[row(compressed, 0) + 25] = 0xFF;
    reject_package(native, compressed, limit);
    {
        auto oversized = dbpf::write({{root, std::vector<uint8_t>(net::adventure_content::kMaxRecordBytes + 1, 0x6B)}});
        reject_package(native, oversized, limit);
    }
    // Nine logical 16 MiB records alias one physical body. The complete transfer
    // is only ~16 MiB; count all records before each read to reject the ninth,
    // rather than trusting package size or allocating the full 144 MiB projection.
    std::vector<dbpf::Record> records{{root, std::vector<uint8_t>(net::adventure_content::kMaxRecordBytes, 0x71)}};
    for (uint32_t i = 1; i < 9; ++i) records.push_back({{1000 + i, 7, 0}, {}});
    auto overlapping = dbpf::write(records); records.clear(); records.shrink_to_fit();
    for (size_t i = 1; i < 9; ++i) {
        const auto at = row(overlapping, i);
        put32(overlapping, at + 12, 96); put32(overlapping, at + 16, 0x80000000u | net::adventure_content::kMaxRecordBytes);
        put32(overlapping, at + 20, net::adventure_content::kMaxRecordBytes);
    }
    CHECK(overlapping.size() < net::adventure_content::kMaxBytes);
    reject_package(native, overlapping, limit);
    // Exact inclusive limits remain valid, even with overlapping physical data.
    put32(overlapping, 0x24, 8); put32(overlapping, 0x2C, 8 + 8 * 28);
    ep::Resources boundary; std::string error;
    CHECK(ep::decode_resources(envelope(native, overlapping), boundary, error));
    CHECK(boundary.bundle.bytes == net::adventure_content::kMaxBytes && boundary.bundle.records.size() == 8);
    for (const auto& record : boundary.bundle.records) CHECK(record.data.size() == net::adventure_content::kMaxRecordBytes);
}
static std::vector<uint8_t> root_bytes(const ep::Resources& resources) {
    for (const auto& record : resources.bundle.records) if (record.key == resources.root) return record.data;
    return {};
}
static void test_v2_root_delta(const editor_native::Snapshot& native) {
    std::string error;
    uint32_t random = 0x1C0FFEE;
    for (size_t size : {262144u, 1048576u}) {
        std::vector<uint8_t> base(size);
        for (auto& b : base) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; b = uint8_t(random); }
        auto target = base;
        for (size_t i = 1024; i < size; i += 8192) target[i] ^= 0x97;
        target.insert(target.begin() + 777, {6, 7, 8});
        const auto first_target = target;
        ep::Resources resources;
        resources.root = {native.source.instance, native.source.type, native.source.group};
        resources.act = native.act; resources.bindings = native.bindings;
        resources.base_root = std::make_shared<const std::vector<uint8_t>>(base);
        CHECK(net::adventure_content::make_bundle(resources.root, {{resources.root, target}, {{12, 3, 4}, {8, 9, 10}}}, resources.bundle, error));
        std::vector<uint8_t> full, delta;
        CHECK(ep::encode_resources(resources, full, error)); CHECK(get32(full, 4) == 2);
        CHECK(ep::encode_delta_resources(resources, delta, error)); CHECK(get32(delta, 4) == 2);
        CHECK(delta.size() < 17000 && full.size() < base.size() + 18000);
        ep::Resources decoded;
        CHECK(ep::decode_resources(full, decoded, error));
        CHECK(decoded.bundle.hash == resources.bundle.hash && root_bytes(decoded) == target);
        CHECK(decoded.base_root && *decoded.base_root == base);
        auto shared = decoded.base_root;
        CHECK(ep::decode_resources(full, decoded, error, shared)); CHECK(decoded.base_root == shared);
        CHECK(ep::decode_resources(delta, decoded, error, shared));
        CHECK(decoded.bundle.records.size() == 1 && root_bytes(decoded) == target && decoded.base_root == shared);
        CHECK(decoded.bindings == native.bindings && decoded.root == resources.root);
        const auto before = decoded.bundle.hash;
        CHECK(!ep::decode_resources(delta, decoded, error)); CHECK(decoded.bundle.hash == before && decoded.base_root == shared);
        auto wrong = std::make_shared<const std::vector<uint8_t>>(base.size(), 0);
        CHECK(!ep::decode_resources(delta, decoded, error, wrong)); CHECK(decoded.bundle.hash == before);
        CHECK(!ep::decode_resources(full, decoded, error, wrong)); CHECK(decoded.bundle.hash == before);
        // Every new root remains a patch against the original immutable base,
        // including a normal native SaveAs that assigns a different root key.
        resources.root.instance += 10;
        for (unsigned i = 0; i < 20; ++i) {
            target[300 + i] ^= uint8_t(i + 1);
            CHECK(net::adventure_content::make_bundle(resources.root, {{resources.root, target}}, resources.bundle, error));
            CHECK(ep::encode_delta_resources(resources, delta, error));
            CHECK(ep::decode_resources(delta, decoded, error, shared));
            CHECK(root_bytes(decoded) == target && decoded.base_root == shared && decoded.root == resources.root);
        }
        for (size_t cut : {size_t(0), size_t(7), size_t(15), full.size() / 2, full.size() - 1}) {
            CHECK(!ep::decode_resources({full.begin(), full.begin() + cut}, decoded, error, shared));
            CHECK(decoded.base_root == shared && root_bytes(decoded) == target);
        }
        auto bad = full; put32(bad, 8, 2); CHECK(!ep::decode_resources(bad, decoded, error));
        bad = full; put32(bad, 16 + get32(full, 12), 0xFFFFFFFF); CHECK(!ep::decode_resources(bad, decoded, error));
        bad = full; const auto base_at = 16 + get32(full, 12); bad[base_at + 4] ^= 1;
        CHECK(!ep::decode_resources(bad, decoded, error));
        bad = full; bad.push_back(0); CHECK(!ep::decode_resources(bad, decoded, error));
        const auto patch_at = base_at + 4 + get32(full, base_at);
        const auto package_at = patch_at + 4 + get32(full, patch_at);
        const std::vector<uint8_t> package(full.begin() + package_at + 4, full.end());
        const auto replace_package = [&](const std::vector<uint8_t>& replacement) {
            std::vector<uint8_t> envelope_bytes(full.begin(), full.begin() + package_at);
            net::Writer tail; tail.u32(uint32_t(replacement.size())); tail.raw(replacement.data(), replacement.size());
            envelope_bytes.insert(envelope_bytes.end(), tail.data.begin(), tail.data.end()); return envelope_bytes;
        };
        auto invalid_package = package; put32(invalid_package, row(invalid_package, 0) + 20, 0);
        CHECK(!ep::decode_resources(replace_package(invalid_package), decoded, error)); CHECK(error == "Editor package record exceeds its limit");
        invalid_package = package; put32(invalid_package, row(invalid_package, 0), native.source.type);
        put32(invalid_package, row(invalid_package, 0) + 4, native.source.group); put32(invalid_package, row(invalid_package, 0) + 8, native.source.instance);
        CHECK(!ep::decode_resources(replace_package(invalid_package), decoded, error)); CHECK(error == "Editor delta dependency package replaces its root");
        invalid_package = package; invalid_package[row(invalid_package, 0) + 24] = 0xFF; invalid_package[row(invalid_package, 0) + 25] = 0xFF;
        CHECK(!ep::decode_resources(replace_package(invalid_package), decoded, error)); CHECK(error == "Editor package record exceeds its limit");
        if (size == 262144) {
            std::vector<dbpf::Record> records{{{1000, 7, 0}, std::vector<uint8_t>(net::adventure_content::kMaxRecordBytes, 0x72)}};
            for (uint32_t i = 1; i < 8; ++i) records.push_back({{1000 + i, 7, 0}, {}});
            auto overlapping = dbpf::write(records); records.clear(); records.shrink_to_fit();
            for (size_t i = 1; i < 8; ++i) {
                const auto at = row(overlapping, i); put32(overlapping, at + 12, 96);
                put32(overlapping, at + 16, 0x80000000u | net::adventure_content::kMaxRecordBytes);
                put32(overlapping, at + 20, net::adventure_content::kMaxRecordBytes);
            }
            CHECK(!ep::decode_resources(replace_package(overlapping), decoded, error)); CHECK(error == "Editor package record exceeds its limit");
        }
        // Malformed v2 lengths, header fields, base/target digests and commands
        // cannot partially replace the last successfully reconstructed snapshot.
        for (unsigned trial = 0; trial < 300; ++trial) {
            auto malformed = full;
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            const size_t candidates[] = {8, 12, base_at, base_at + 4, patch_at, patch_at + 4, patch_at + 20, patch_at + 84, package_at};
            const size_t at = candidates[random % 9]; malformed[at] ^= uint8_t(1u << (random % 8));
            const auto old_hash = decoded.bundle.hash; const auto old_base = decoded.base_root;
            ep::Resources fuzzed = decoded;
            if (!ep::decode_resources(malformed, fuzzed, error, shared)) CHECK(fuzzed.bundle.hash == old_hash && fuzzed.base_root == old_base);
            else CHECK(root_bytes(fuzzed) == first_target);
        }
        resources.base_root.reset(); delta = {7};
        CHECK(!ep::encode_delta_resources(resources, delta, error)); CHECK(delta == (std::vector<uint8_t>{7}));
    }
    const dbpf::Key root{native.source.instance, native.source.type, native.source.group};
    ep::Resources legacy;
    CHECK(ep::decode_resources(envelope(native, dbpf::write({{root, native.bytes}})), legacy, error));
    CHECK(legacy.base_root && *legacy.base_root == native.bytes);
}
static bool same_resources(const ep::Resources& a, const ep::Resources& b) {
    return a.root == b.root && a.act == b.act && a.bindings == b.bindings && a.metadata==b.metadata && a.bundle.hash == b.bundle.hash &&
           a.bundle.bytes == b.bundle.bytes && root_bytes(a) == root_bytes(b) && a.base_root && b.base_root && *a.base_root == *b.base_root;
}
static void test_join_cache(const editor_native::Snapshot& native) {
    std::string error;
    ep::Resources host; host.root = {native.source.instance, native.source.type, native.source.group};
    host.act = native.act; host.bindings = native.bindings;
    host.metadata=editor_native::Snapshot::Metadata{{u"Cached draft",u"",u"preview"},0};
    std::vector<uint8_t> base(131072); uint32_t random = 0x534133;
    for (auto& b : base) { random ^= random << 13; random ^= random >> 17; random ^= random << 5; b = uint8_t(random); }
    auto target = base; target[981] ^= 0x87; target[54321] ^= 0x34;
    host.base_root = std::make_shared<const std::vector<uint8_t>>(base);
    const dbpf::Key first{11, 3, 4}, second{12, 3, 4}, empty{13, 3, 4};
    CHECK(net::adventure_content::make_bundle(host.root, {{host.root, target}, {first, std::vector<uint8_t>(65536, 0xA5)},
        {second, std::vector<uint8_t>(98304, 0x59)}, {empty, {}}}, host.bundle, error));
    ep::JoinInventory inventory; CHECK(ep::make_inventory(host, inventory, error));
    CHECK(inventory.base_size == base.size() && inventory.base_hash == upd::sha256_hex(base) && inventory.records.size() == 3);
    CHECK(inventory.records[0].key == first && inventory.records[1].key == second && inventory.records[2].key == empty);
    CHECK(inventory.records[2].size == 0 && inventory.records[2].hash == upd::sha256_hex(std::vector<uint8_t>{}));
    std::vector<uint8_t> encoded; CHECK(ep::encode_inventory(inventory, encoded, error));
    ep::JoinInventory roundtrip; CHECK(ep::decode_inventory(encoded, roundtrip, error));
    CHECK(roundtrip.base_hash == inventory.base_hash && roundtrip.records[1].hash == inventory.records[1].hash);
    for (size_t cut = 0; cut < encoded.size(); ++cut) {
        const auto before = roundtrip.base_hash;
        CHECK(!ep::decode_inventory({encoded.begin(), encoded.begin() + cut}, roundtrip, error));
        CHECK(roundtrip.base_hash == before && roundtrip.records.size() == 3);
    }
    auto invalid = encoded; invalid.push_back(0); CHECK(!ep::decode_inventory(invalid, roundtrip, error));
    invalid = encoded; put32(invalid, 76, 0xFFFFFFFF); CHECK(!ep::decode_inventory(invalid, roundtrip, error));
    invalid = encoded; invalid[12] = 'G'; CHECK(!ep::decode_inventory(invalid, roundtrip, error));
    invalid.assign(ep::kInventoryBytes + 1, 0); CHECK(!ep::decode_inventory(invalid, roundtrip, error));
    auto bad_claims = inventory; bad_claims.records.push_back(bad_claims.records.back());
    std::vector<uint8_t> untouched{3, 4}; CHECK(!ep::encode_inventory(bad_claims, untouched, error)); CHECK(untouched == (std::vector<uint8_t>{3, 4}));
    bad_claims = inventory; std::swap(bad_claims.records[0], bad_claims.records[1]); CHECK(!ep::verify_inventory(bad_claims, error));
    bad_claims = inventory; bad_claims.base_size = 0; CHECK(!ep::verify_inventory(bad_claims, error));
    bad_claims = inventory; bad_claims.records[0].size = net::adventure_content::kMaxRecordBytes + 1; CHECK(!ep::verify_inventory(bad_claims, error));
    ep::JoinInventory maximum;
    for (uint32_t i = 0; i < net::adventure_content::kMaxRecords; ++i) maximum.records.push_back({{i, 3, 4}, 0, inventory.records[2].hash});
    CHECK(ep::encode_inventory(maximum, encoded, error)); CHECK(encoded.size() == 80 + 80 * maximum.records.size());
    CHECK(ep::decode_inventory(encoded, roundtrip, error) && roundtrip.records.size() == maximum.records.size());
    maximum.records.push_back({{9999, 3, 4}, 0, inventory.records[2].hash}); CHECK(!ep::verify_inventory(maximum, error));
    ep::JoinInventory absent; CHECK(ep::encode_inventory(absent, encoded, error)); CHECK(ep::decode_inventory(encoded, roundtrip, error));
    CHECK(roundtrip.base_hash.empty() && roundtrip.records.empty());
    encoded[12] = '0'; CHECK(!ep::decode_inventory(encoded, roundtrip, error));

    ep::Resources cache = host, decoded;
    std::vector<uint8_t> bytes;
    CHECK(ep::encode_join_resources(host, inventory, bytes, error)); CHECK(get32(bytes, 0) == 0x314A4445);
    CHECK(bytes.size() < 3000); CHECK(ep::decode_join_resources(bytes, cache, decoded, error)); CHECK(same_resources(decoded, host));
    const auto all_hit = bytes;
    const auto manifest_at = 12 + get32(bytes, 8);
    const auto records_at = manifest_at + 72;
    const auto bodies_at = records_at + inventory.records.size() * 80;
    CHECK(get32(bytes, bodies_at) == 0);
    CHECK(ep::encode_join_resources(host, absent, bytes, error)); CHECK(get32(bytes, 0) == 0x31524445 && get32(bytes, 4) == 2);
    ep::Resources wrong_cache = cache; wrong_cache.base_root = std::make_shared<const std::vector<uint8_t>>(base.size(), 0);
    CHECK(ep::decode_join_resources(bytes, wrong_cache, decoded, error)); CHECK(same_resources(decoded, host));
    CHECK(ep::decode_join_resources(envelope(native, dbpf::write({{host.root, target}})), ep::JoinCacheRead{}, decoded, error));
    CHECK(root_bytes(decoded) == target);

    ep::JoinInventory partial = inventory; partial.records.resize(1);
    CHECK(ep::encode_join_resources(host, partial, bytes, error));
    CHECK(bytes.size() > 98304 && bytes.size() < 101304); CHECK(get32(bytes, bodies_at) == 2);
    CHECK(get32(bytes, bodies_at + 4) == 2 && get32(bytes, bodies_at + 8) == 98304);
    const auto last_body = bodies_at + 12 + 98304; CHECK(get32(bytes, last_body) == 3 && get32(bytes, last_body + 4) == 0);
    ep::Resources only_first = cache;
    std::erase_if(only_first.bundle.records, [&](const auto& r) { return !(r.key == first); });
    CHECK(ep::decode_join_resources(bytes, only_first, decoded, error)); CHECK(same_resources(decoded, host));
    const auto partial_wire = bytes;
    partial = inventory; partial.base_hash.clear(); partial.base_size = 0;
    CHECK(ep::encode_join_resources(host, partial, bytes, error)); CHECK(get32(bytes, bodies_at) == 1);
    CHECK(get32(bytes, bodies_at + 4) == 0 && get32(bytes, bodies_at + 8) == base.size());
    ep::Resources no_base = cache; no_base.base_root.reset();
    CHECK(ep::decode_join_resources(bytes, no_base, decoded, error)); CHECK(same_resources(decoded, host));
    // A stale same-key record and an equal body under the wrong key are not hits.
    partial = inventory; partial.records[0].hash = inventory.records[1].hash;
    CHECK(ep::encode_join_resources(host, partial, bytes, error)); CHECK(get32(bytes, bodies_at) == 1 && get32(bytes, bodies_at + 4) == 1);
    CHECK(ep::decode_join_resources(bytes, cache, decoded, error)); CHECK(same_resources(decoded, host));

    auto reject = [&](const std::vector<uint8_t>& input, const ep::Resources& available) {
        const auto old_hash = decoded.bundle.hash; const auto old_base = decoded.base_root; const auto old_key = decoded.root;
        CHECK(!ep::decode_join_resources(input, available, decoded, error));
        CHECK(decoded.bundle.hash == old_hash && decoded.base_root == old_base && decoded.root == old_key);
    };
    reject(all_hit, wrong_cache); reject(all_hit, no_base);
    auto missing = cache; std::erase_if(missing.bundle.records, [&](const auto& r) { return r.key == first; }); reject(all_hit, missing);
    auto corrupt = cache; for (auto& r : corrupt.bundle.records) if (r.key == first) r.data[0] ^= 1; reject(all_hit, corrupt);
    for (size_t cut = 0; cut < all_hit.size(); ++cut) reject({all_hit.begin(), all_hit.begin() + cut}, cache);
    invalid = all_hit; invalid.push_back(0); reject(invalid, cache);
    invalid = all_hit; put32(invalid, 4, 2); reject(invalid, cache);
    invalid = all_hit; put32(invalid, 20, 1); reject(invalid, cache); // root delta flags.
    invalid = all_hit; put32(invalid, manifest_at, 0); reject(invalid, cache);
    invalid = all_hit; put32(invalid, manifest_at + 68, 2048); reject(invalid, cache);
    invalid = all_hit; invalid[records_at + 16] = 'X'; reject(invalid, cache);
    invalid = all_hit; put32(invalid, records_at, host.root.instance); put32(invalid, records_at + 4, host.root.type);
    put32(invalid, records_at + 8, host.root.group); reject(invalid, cache);
    invalid = partial_wire; put32(invalid, bodies_at + 4, 4); reject(invalid, cache);
    invalid = partial_wire; put32(invalid, bodies_at + 8, 1); reject(invalid, cache);
    invalid = partial_wire; put32(invalid, last_body, 2); reject(invalid, cache);
    invalid = partial_wire; invalid[bodies_at + 12] ^= 1; reject(invalid, cache);
    invalid = partial_wire; invalid.pop_back(); reject(invalid, cache);
    // Logical declared totals are bounded before dependency reads, even if the
    // attacker tries to alias cheap cached records into many large claims.
    ep::JoinInventory huge = inventory; huge.records.clear();
    for (uint32_t i = 0; i < 9; ++i) huge.records.push_back({{i, 3, 4}, net::adventure_content::kMaxRecordBytes, inventory.records[0].hash});
    CHECK(!ep::verify_inventory(huge, error));
    int base_reads = 0, record_reads = 0;
    ep::JoinCacheRead reader;
    reader.base = [&](const auto&, uint32_t, auto& out) { ++base_reads; out = base; return true; };
    reader.record = [&](const auto& entry, auto& out) { ++record_reads; for (const auto& r : cache.bundle.records) if (r.key == entry.key) { out = r.data; return true; } return false; };
    ep::JoinInventory verified;
    CHECK(ep::make_inventory(inventory, reader, verified, error)); CHECK(base_reads == 1 && record_reads == 3 && verified.records.size() == 3);
    reader.record = [&](const auto& entry, auto& out) { for (const auto& r : cache.bundle.records) if (r.key == entry.key && !(r.key == second)) { out = r.data; if (r.key == first) out[0] ^= 1; return true; } return false; };
    CHECK(ep::make_inventory(inventory, reader, verified, error)); CHECK(verified.records.size() == 1 && verified.records[0].key == empty);
    reader.base = [](const auto&, uint32_t, auto&) -> bool { throw 1; };
    CHECK(!ep::make_inventory(inventory, reader, verified, error)); CHECK(verified.records.size() == 1);
    CHECK(!ep::decode_join_resources(all_hit, reader, decoded, error)); CHECK(same_resources(decoded, host));
    // A valid 128 MiB dependency manifest plus the nonempty root exceeds the
    // aggregate bound. Reject before a single dependency cache callback runs.
    net::Writer aggregate;
    aggregate.raw(all_hit.data(), manifest_at + 68); aggregate.u32(8);
    for (uint32_t i = 0; i < 8; ++i) {
        aggregate.u32(i); aggregate.u32(3); aggregate.u32(4);
        aggregate.u32(net::adventure_content::kMaxRecordBytes); aggregate.raw(inventory.records[0].hash.data(), 64);
    }
    aggregate.u32(0); record_reads = 0;
    reader.base = [&](const auto&, uint32_t, auto& out) { out = base; return true; };
    reader.record = [&](const auto&, auto&) { ++record_reads; return false; };
    CHECK(!ep::decode_join_resources(aggregate.data, reader, decoded, error));
    CHECK(record_reads == 0 && same_resources(decoded, host));
    CHECK(error == "Editor join dependency replaces its root or exceeds its limit");
    // Equal bytes under another key cannot satisfy an omitted same-hash record.
    missing = cache;
    for (auto& r : missing.bundle.records) if (r.key == first) ++r.key.instance;
    reject(all_hit, missing);
    // SaveAs can rotate the root key without invalidating immutable base and
    // dependency hits. The current root is reconstructed before being exposed.
    auto rotated = host; rotated.root.instance += 1;
    for (auto& r : rotated.bundle.records) if (r.key == host.root) { r.key = rotated.root; r.data[777] ^= 0xF1; }
    CHECK(net::adventure_content::make_bundle(rotated.root, rotated.bundle.records, rotated.bundle, error));
    CHECK(ep::encode_join_resources(rotated, inventory, bytes, error)); CHECK(bytes.size() < 3000);
    CHECK(ep::decode_join_resources(bytes, cache, decoded, error)); CHECK(same_resources(decoded, rotated));
    for (unsigned trial = 0; trial < 500; ++trial) {
        auto fuzzed = all_hit; random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        fuzzed[random % fuzzed.size()] ^= uint8_t(1u << (random % 8));
        ep::Resources result = decoded;
        if (!ep::decode_join_resources(fuzzed, cache, result, error)) CHECK(same_resources(result, decoded));
        else CHECK(result.bundle.bytes <= net::adventure_content::kMaxBytes && result.base_root && !root_bytes(result).empty());
    }
}
int main() {
    std::string error;
    editor_native::Snapshot native;
    native.source = {42, 0x366A930D, 0x408A0000}; native.act = 2;
    native.bindings = {{{0x400000010, 1}, 7}, {{0x400000011, 3}, 0}};
    native.bytes = {0, 255, 13, 37, 0};
    test_record_pre_read_limits(native);
    test_v2_root_delta(native);
    test_join_cache(native);
    std::vector<uint8_t> bytes;
    CHECK(ep::encode_mapping(native, bytes, error));
    editor_native::Snapshot decoded;
    CHECK(ep::decode_mapping(bytes, decoded, error));
    CHECK(decoded.source == native.source && decoded.act == native.act && decoded.bindings == native.bindings);
    const auto legacy=bytes;
    native.metadata=editor_native::Snapshot::Metadata{{u"Unsaved draft",u"Description \u03a9",u"coop,preview"},0xE27DDAD4};
    CHECK(ep::encode_mapping(native,bytes,error));
    CHECK(ep::decode_mapping(bytes,decoded,error));CHECK(decoded.metadata==native.metadata);
    auto too_long=native;too_long.metadata->text[0].assign(4097,u'x');
    std::vector<uint8_t> refused;CHECK(!ep::encode_mapping(too_long,refused,error));
    for (size_t n = 0; n < bytes.size(); ++n) {
        auto before = decoded.bindings;
        CHECK(!ep::decode_mapping({bytes.begin(), bytes.begin() + n}, decoded, error));
        CHECK(decoded.bindings == before);
    }
    CHECK(ep::decode_mapping(legacy,decoded,error));CHECK(!decoded.metadata);
    auto duplicate = native; duplicate.bindings[1].marker = 7;
    CHECK(!ep::encode_mapping(duplicate, bytes, error));
    duplicate = native; duplicate.bindings[1].object = duplicate.bindings[0].object;
    CHECK(!ep::encode_mapping(duplicate, bytes, error));
    duplicate = native; duplicate.act = 8;
    CHECK(!ep::encode_mapping(duplicate, bytes, error));
    ep::Resources resources;
    resources.root = {native.source.instance, native.source.type, native.source.group};
    resources.act = native.act; resources.bindings = native.bindings;
    resources.metadata=native.metadata;
    CHECK(net::adventure_content::make_bundle(resources.root, {{resources.root, native.bytes}, {{12, 3, 4}, {7, 0, 8}}}, resources.bundle, error));
    CHECK(ep::encode_resources(resources, bytes, error));
    ep::Resources imported;
    CHECK(ep::decode_resources(bytes, imported, error));
    CHECK(imported.root == resources.root && imported.bundle.hash == resources.bundle.hash);
    CHECK(imported.bindings == native.bindings);
    CHECK(imported.metadata==native.metadata);
    CHECK(imported.base_root && *imported.base_root == native.bytes);
    const auto original = bytes;
    for (size_t n = 0; n < bytes.size(); ++n) {
        auto hash = imported.bundle.hash;
        CHECK(!ep::decode_resources({bytes.begin(), bytes.begin() + n}, imported, error));
        CHECK(imported.bundle.hash == hash);
    }
    bytes.push_back(0);
    CHECK(!ep::decode_resources(bytes, imported, error));
    resources.root.type = 1;
    CHECK(!ep::encode_resources(resources, bytes, error));
    resources.root = imported.root;
    editor::State state;
    auto& doc = state.document;
    doc.project[0] = 21; doc.epoch = 123; doc.next_id = 0x400000020;
    doc.classes.insert({2, 1}); doc.acts.insert({3, 1});
    for (auto binding : native.bindings) {
        editor::Placement p; p.object = binding.object; p.class_id = {2, 1}; p.act_id = {3, 1};
        doc.placements.emplace(p.object, p);
    }
    CHECK(ep::native_snapshot(imported, doc, decoded, error));
    CHECK(decoded.bytes == native.bytes);
    auto broken = doc; broken.placements.erase(native.bindings[0].object);
    CHECK(!ep::native_snapshot(imported, broken, decoded, error));
    editor_store::Snapshot stored;
    CHECK(ep::store_snapshot(state, decoded, "private native draft", stored, error));
    editor::State restored;
    editor_native::Snapshot restored_native;
    CHECK(ep::load_snapshot(stored, restored, restored_native, error));
    CHECK(restored == state && restored_native.bytes == native.bytes && restored_native.bindings == native.bindings);
    ++stored.epoch;
    CHECK(!ep::load_snapshot(stored, restored, restored_native, error));
    CHECK(restored == state);
    std::cout << "PASS " << checks << " editor project checks\n";
}
