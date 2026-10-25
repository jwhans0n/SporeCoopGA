#include "net/adventure_assets.h"
#include "../updater/sha256.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace net::adventure_content;
static unsigned failures;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static const dbpf::Key root{7, 0x366A930D, 0x408A0000};

static Bundle bundle()
{
    Bundle b;
    std::string error;
    CHECK(make_bundle(root, {{root, std::vector<uint8_t>(2000, 0x42)}, {{8, 0x1A99B06B, 0x40626200}, {1, 2, 3}}}, b, error));
    return b;
}

static bool manifest(Receiver& receiver, const Bundle& b, std::string& error)
{
    if (!receiver.begin(0x100000002ull, b.root, (uint16_t)b.manifest.size(), b.bytes, b.hash, error)) return false;
    for (size_t i = 0; i < b.manifest.size(); ++i)
        if (!receiver.entry((uint16_t)i, b.manifest[i], error)) return false;
    return true;
}

static void round_trip()
{
    auto b = bundle();
    Receiver r;
    std::string error;
    CHECK(manifest(r, b, error));
    CHECK(r.run() == 0x100000002ull);
    std::vector<uint16_t> missing;
    CHECK(r.manifest_done({}, missing, error));
    CHECK(missing.size() == 2);
    for (auto index : missing) {
        const auto& data = b.records[index].data;
        for (size_t off = 0; off < data.size(); off += kChunkBytes) {
            uint16_t size = (uint16_t)std::min<size_t>(kChunkBytes, data.size() - off);
            CHECK(r.chunk(index, (uint32_t)off, data.data() + off, size, error));
            auto received = r.received();
            CHECK(r.chunk(index, (uint32_t)off, data.data() + off, size, error));
            CHECK(r.received() == received);
        }
    }
    Bundle received;
    CHECK(r.finish(received, error));
    CHECK(received.hash == b.hash);
    CHECK(received.records.size() == b.records.size());
    CHECK(received.records[0].data == b.records[0].data);
    CHECK(r.received() == b.bytes);
}

static void cache()
{
    auto b = bundle();
    Receiver r;
    std::string error;
    CHECK(manifest(r, b, error));
    std::vector<uint16_t> missing;
    CHECK(r.manifest_done([&](const auto& hash, auto& bytes) {
        for (size_t i = 0; i < b.manifest.size(); ++i)
            if (b.manifest[i].hash == hash) { bytes = b.records[i].data; return true; }
        return false;
    }, missing, error));
    CHECK(missing.empty());
    Bundle received;
    CHECK(r.finish(received, error));
    CHECK(received.hash == b.hash);
    CHECK(manifest(r, b, error));
    CHECK(r.manifest_done([](const auto&, auto& bytes) { bytes = {99}; return true; }, missing, error));
    CHECK(missing.size() == b.records.size()); // corrupted cache records must be requested again
}

static void hostile()
{
    auto b = bundle();
    Bundle out;
    std::string error;
    CHECK(!make_bundle(root, {{root, {}}, {root, {1}}}, out, error));
    CHECK(!make_bundle(root, {{{9, 1, 1}, {1}}}, out, error));
    CHECK(!make_bundle(root, {}, out, error));
    CHECK(!valid_hash(std::string(64, 'g')));
    Receiver r;
    CHECK(!r.begin(0, root, 1, 1, b.hash, error));
    CHECK(!r.begin(1, root, kMaxRecords + 1, 1, b.hash, error));
    CHECK(!r.begin(1, root, 1, kMaxBytes + 1, b.hash, error));
    CHECK(!r.begin(1, root, 1, 1, "../../bad", error));
    CHECK(r.begin(1, root, 1, 1, b.hash, error));
    CHECK(!r.entry(0, {root, kMaxRecordBytes + 1, b.hash}, error));
    CHECK(!r.entry(1, {root, 1, b.hash}, error));
    CHECK(manifest(r, b, error));
    CHECK(!r.entry(0, b.manifest[0], error));
    std::vector<uint16_t> missing;
    CHECK(r.manifest_done({}, missing, error));
    CHECK(!r.finish(out, error));
    uint16_t i = 0;
    const auto& data = b.records[i].data;
    CHECK(!r.chunk(i, 1, data.data(), 1, error));
    CHECK(!r.chunk(i, 0xFFFFFFFF, data.data(), 1, error));
    CHECK(!r.chunk(i, 0, data.data(), kChunkBytes + 1, error));
    CHECK(!r.chunk(0xFFFF, 0, data.data(), 1, error));
    CHECK(!r.chunk(i, 0, nullptr, 1, error));
    CHECK(r.chunk(i, 0, data.data(), 1, error));
    uint8_t other = data[0] ^ 0xFF;
    CHECK(!r.chunk(i, 0, &other, 1, error));
    CHECK(manifest(r, b, error));
    CHECK(r.manifest_done({}, missing, error));
    for (auto index : missing) {
        auto bytes = b.records[index].data;
        if (!bytes.empty()) bytes[0] ^= 0xFF;
        for (size_t off = 0; off < bytes.size(); off += kChunkBytes)
            CHECK(r.chunk(index, (uint32_t)off, bytes.data() + off,
                          (uint16_t)std::min<size_t>(kChunkBytes, bytes.size() - off), error));
    }
    CHECK(!r.finish(out, error));
    CHECK(r.begin(1, root, 2, b.bytes, b.hash, error));
    CHECK(r.entry(0, b.manifest[0], error));
    CHECK(!r.manifest_done({}, missing, error));
    auto wrong = b;
    wrong.hash[0] = wrong.hash[0] == '0' ? '1' : '0';
    CHECK(manifest(r, wrong, error));
    CHECK(!r.manifest_done({}, missing, error));
    auto unsorted = b;
    std::reverse(unsorted.manifest.begin(), unsorted.manifest.end());
    CHECK(manifest(r, unsorted, error));
    CHECK(!r.manifest_done({}, missing, error));
    r.reset();
    CHECK(!r.active());
    CHECK(!r.chunk(0, 0, data.data(), 1, error));
}

static void empty_record()
{
    Bundle b;
    std::string error;
    CHECK(make_bundle(root, {{root, {}}}, b, error));
    Receiver r;
    CHECK(manifest(r, b, error));
    std::vector<uint16_t> missing;
    CHECK(r.manifest_done({}, missing, error));
    Bundle received;
    CHECK(r.finish(received, error));
}

int main()
{
    round_trip(); cache(); hostile(); empty_record();
    std::printf("adventure assets tests: %u failure(s)\n", failures);
    return failures ? 1 : 0;
}
