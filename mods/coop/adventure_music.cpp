#include "adventure_music.h"

#include <algorithm>
#include <cstdio>
#include <set>
#include <tuple>

namespace adventure_music {
namespace {

constexpr uint32_t kParent = 0x5F6317D5, kWaves = 0x701ED91E;
constexpr size_t kMaxDescriptors = 64, kMaxProperties = 2048, kMaxReferences = 4096;
constexpr size_t kMaxDescriptorBytes = 1024 * 1024;

std::string key_text(const dbpf::Key& key)
{
    char text[48];
    std::snprintf(text, sizeof text, "%08X!%08X.%08X", key.group, key.instance, key.type);
    return text;
}

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }
uint16_t be16(const uint8_t* p) { return uint16_t(p[0]) << 8 | p[1]; }
uint32_t le32(const uint8_t* p) { return uint32_t(p[3]) << 24 | uint32_t(p[2]) << 16 | uint32_t(p[1]) << 8 | p[0]; }

// The audio factory reads a standard property stream: headers/lengths are big
// endian, ResourceKey payloads little endian. Scalar keys have a reserved word.
// Only the native parent and wave fields are dependency edges, not arbitrary keys
// used as audio property IDs, event parameters, curves or optional control data.
bool references(const std::vector<uint8_t>& data, dbpf::Key& parent,
                std::vector<dbpf::Key>& waves, std::string& error)
{
    auto bad = [&]() { error = "invalid or unsupported native audio descriptor property stream"; return false; };
    if (data.size() < 4 || data.size() > kMaxDescriptorBytes) return bad();
    uint32_t count = be32(data.data());
    if (count > kMaxProperties) return bad();
    size_t at = 4;
    std::set<uint32_t> seen;
    for (uint32_t i = 0; i < count; ++i) {
        if (data.size() - at < 8) return bad();
        const uint8_t* header = data.data() + at;
        uint32_t id = be32(header);
        uint16_t type = be16(header + 4), flags = be16(header + 6);
        at += 8;
        if (!seen.insert(id).second) return bad();
        size_t items = 1, item_bytes = 0, bytes = 0;
        bool array = (flags & 0x80) != 0;
        if (array) {
            if (data.size() - at < 8) return bad();
            items = be32(data.data() + at); item_bytes = be32(data.data() + at + 4);
            at += 8;
            if (items > kMaxReferences || item_bytes > kMaxDescriptorBytes) return bad();
            if (type == 0x12 || type == 0x13) {
                // The array element-size is the native string object size;
                // serialized strings themselves are individually length-prefixed.
                size_t cursor = at, width = type == 0x13 ? 2 : 1;
                for (size_t j = 0; j < items; ++j) {
                    if (data.size() - cursor < 4) return bad();
                    size_t chars = be32(data.data() + cursor);
                    cursor += 4;
                    if (chars > (data.size() - cursor) / width) return bad();
                    cursor += chars * width;
                }
                bytes = cursor - at;
            } else {
                if (items && item_bytes > (data.size() - at) / items) return bad();
                bytes = items * item_bytes;
            }
        } else {
            switch (type) {
            case 1: case 2: case 5: case 6: bytes = 1; break;
            case 3: case 7: case 8: bytes = 2; break;
            case 4: case 9: case 0xA: case 0xD: bytes = 4; break;
            case 0xB: case 0xC: case 0xE: case 0x30: bytes = 8; break;
            case 0x20: case 0x33: case 0x34: bytes = 16; break;
            case 0x31: case 0x32: bytes = 12; break;
            case 0x12: case 0x13: {
                if (data.size() - at < 4) return bad();
                size_t chars = be32(data.data() + at), width = type == 0x13 ? 2 : 1;
                if (chars > (data.size() - at - 4) / width) return bad();
                bytes = 4 + chars * width;
                break;
            }
            default: return bad();
            }
        }
        if (bytes > data.size() - at) return bad();
        if (id == kParent) {
            if (type != 0x20 || array || bytes != 16) return bad();
            const uint8_t* p = data.data() + at;
            parent = {le32(p), le32(p + 4), le32(p + 8)};
            if (!parent.type || parent.type == UINT32_MAX) parent.type = kDescriptor;
            if (!parent.group || parent.group == UINT32_MAX) parent.group = kAudioGroup;
        } else if (id == kWaves) {
            if (type != 0x20 || !array || item_bytes != 12) return bad();
            for (size_t j = 0; j < items; ++j) {
                // A28B70 takes only the ResourceKey instance; sample lookup is
                // always the native audio group/type, even if other fields vary.
                uint32_t id = le32(data.data() + at + j * item_bytes);
                if (id && id != UINT32_MAX) waves.push_back({id, kSample, kAudioGroup});
            }
        }
        at += bytes;
    }
    return at == data.size() || bad();
}

using Identity = std::tuple<uint32_t, uint32_t, uint32_t>;
Identity identity(const dbpf::Key& key) { return {key.group, key.type, key.instance}; }

struct Validator {
    const Loader& load;
    Result& result;
    std::string& error;
    std::set<Identity> active, checked;
    size_t descriptors = 0;

    bool visit(const dbpf::Key& key, bool root = false)
    {
        if (checked.count(identity(key))) return true;
        if (active.count(identity(key))) { error = "cyclic native audio dependency " + key_text(key); return false; }
        if (result.dependencies.size() >= kMaxReferences ||
            (key.type == kDescriptor && ++descriptors > kMaxDescriptors)) {
            error = "native audio dependency limit exceeded at " + key_text(key); return false;
        }
        Record record;
        auto found = load(key, key.type == kDescriptor, record, error);
        if (found == Lookup::Error) return false;
        if (found == Lookup::Missing) {
            // Verified native control used by Mr Puzzle's Magical Gates. It has
            // no descriptor; ordinary unresolved IDs must not acquire this bypass.
            if (root && key.instance == 1) { result.native_control = true; return true; }
            error = "missing native audio dependency " + key_text(key); return false;
        }
        if (!record.built_in) {
            error = "unsupported custom native audio dependency " + key_text(key);
            if (!record.source.empty()) error += " in " + record.source;
            return false;
        }
        if (record.key != key) {
            error = "unsupported native audio key alias " + key_text(key) + " -> " + key_text(record.key);
            return false;
        }
        result.dependencies.push_back(key);
        active.insert(identity(key));
        if (key.type == kDescriptor) {
            dbpf::Key parent;
            std::vector<dbpf::Key> waves;
            if (!references(record.data, parent, waves, error)) { error += " at " + key_text(key); return false; }
            if (parent.instance && parent.instance != UINT32_MAX) {
                if (parent.type != kDescriptor || parent.group != kAudioGroup) {
                    error = "unsupported native audio parent " + key_text(parent); return false;
                }
                if (!visit(parent)) return false;
            }
            for (const auto& wave : waves) if (!visit(wave)) return false;
        }
        active.erase(identity(key));
        checked.insert(identity(key));
        return true;
    }
};

} // namespace

bool validate(uint32_t music, const Loader& load, Result& result, std::string& error)
{
    result = {}; error.clear();
    if (!music || music == UINT32_MAX) return true;
    Validator validator{load, result, error, {}, {}, 0};
    return validator.visit({music, kDescriptor, kAudioGroup}, true);
}

} // namespace adventure_music
