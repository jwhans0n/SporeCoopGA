// Durable collaboration projects, separate from native databases and asset cache.
#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace editor_store {
using Project = std::array<uint8_t, 16>;
constexpr uint32_t kSchema = 1;
constexpr size_t kNativeBytes = 128u << 20, kDocumentBytes = 64u << 20;
constexpr size_t kMappingBytes = 8u << 20, kManifestBytes = 8u << 20;
constexpr size_t kMetadataBytes = 16384, kJournalBytes = 96u << 20;
struct Snapshot {
    Project project{};
    uint64_t epoch = 0, revision = 0;
    uint32_t schema = kSchema;
    std::vector<uint8_t> native_snapshot, document, id_mapping, manifest;
    std::string metadata;
    bool operator==(const Snapshot&) const = default;
};
std::string project_name(const Project&);
bool encode_snapshot(const Snapshot&, std::vector<uint8_t>&, std::string&);
bool decode_snapshot(const std::vector<uint8_t>&, Snapshot&, std::string&);

class Store {
public:
    Store() = default;
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;
    // The directory is derived as root/<project hex>. Only our fixed filenames
    // are written. Existing stores are opened explicitly, never overwritten.
    bool create(const std::filesystem::path& root, const Snapshot&, std::string&);
    bool open(const std::filesystem::path& root, const Project&, std::string&);
    // Exact next revision, same epoch. Empty native_snapshot inherits the last
    // owned native bytes, avoiding an opaque-resource rewrite for each gesture.
    // True means the complete candidate is durable before publication.
    // False with healthy()==false has uncertain durability: freeze authoring and
    // reopen/recover before continuing. A failed flush may still recover a frame.
    bool commit(const Snapshot&, std::string&);
    // Same committed revision, including rehost into a new epoch or Save As.
    bool checkpoint(const Snapshot&, std::string&);
    // Explicit replica resynchronization only: caller independently verifies the
    // authoritative baseline. Same epoch may advance/equal revision; a newer
    // epoch nonce may replace the old session's history after caller validation.
    bool adopt_checkpoint(const Snapshot&, std::string&);
    void close();
    bool healthy() const { return healthy_; }
    uint64_t durable_revision() const { return snapshot_.revision; }
    const Snapshot& snapshot() const { return snapshot_; }
    const std::filesystem::path& directory() const { return directory_; }
private:
    std::filesystem::path directory_;
    Snapshot snapshot_;
    std::string digest_, native_hash_;
    uint64_t journal_bytes_ = 0;
    intptr_t lock_ = -1;
    bool healthy_ = false;
    bool lock(const std::filesystem::path&, const Project&, std::string&);
    bool save_checkpoint(const Snapshot&, std::string&);
};
} // namespace editor_store
