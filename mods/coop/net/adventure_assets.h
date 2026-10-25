// Bounded, game-independent adventure resource bundles and receive validation.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "dbpf.h"

namespace net::adventure_content {

constexpr uint32_t kMaxBytes = 128u << 20;
constexpr uint32_t kMaxRecordBytes = 16u << 20;
constexpr uint16_t kMaxRecords = 2048;
constexpr uint16_t kChunkBytes = 900;

struct Entry {
    dbpf::Key key;
    uint32_t size = 0;
    std::string hash; // lowercase SHA-256 of the uncompressed record
};

struct Bundle {
    dbpf::Key root;
    std::vector<dbpf::Record> records; // canonical key order
    std::vector<Entry> manifest;
    std::string hash; // SHA-256 of the root and canonical manifest
    uint32_t bytes = 0;
};

bool valid_hash(const std::string& hash);
bool make_bundle(const dbpf::Key& root, std::vector<dbpf::Record> records, Bundle& out, std::string& error);

// Cache callbacks return raw, uncompressed bytes. Every hit is hashed before use.
using CacheRead = std::function<bool(const std::string&, std::vector<uint8_t>&)>;

class Receiver {
public:
    bool begin(uint64_t run, const dbpf::Key& root, uint16_t count, uint32_t bytes,
               const std::string& hash, std::string& error);
    bool entry(uint16_t index, const Entry& entry, std::string& error);
    bool manifest_done(const CacheRead& cache, std::vector<uint16_t>& missing, std::string& error);
    bool chunk(uint16_t index, uint32_t offset, const uint8_t* data, uint16_t size, std::string& error);
    bool finish(Bundle& out, std::string& error);
    void reset();
    uint64_t run() const { return run_; }
    bool active() const { return run_ != 0; }
    bool receiving() const { return manifest_done_; }
    uint32_t received() const { return received_; }
    uint32_t bytes() const { return bytes_; }

private:
    uint64_t run_ = 0;
    dbpf::Key root_;
    uint32_t bytes_ = 0, received_ = 0, declared_ = 0;
    std::string hash_;
    std::vector<Entry> manifest_;
    std::vector<bool> seen_;
    std::vector<dbpf::Record> records_;
    std::vector<uint32_t> have_;
    bool manifest_done_ = false;
};

} // namespace net::adventure_content
