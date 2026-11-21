#include "editor_binary_delta.h"
#include "buffer.h"
#include "../../../updater/sha256.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <unordered_map>

namespace editor::binary_delta {
namespace {
constexpr uint32_t kMagic = 0x31424445, kVersion = 1;
constexpr size_t kAnchor = 64;
constexpr uint64_t kMultiplier = 257;
struct Anchors { std::array<uint32_t, 4> offsets{}; unsigned count = 0; };
uint64_t fingerprint(const std::vector<uint8_t>& bytes, size_t pos) {
    uint64_t hash = 0;
    for (size_t i = 0; i < kAnchor; ++i) hash = hash * kMultiplier + uint64_t(bytes[pos + i]) + 1;
    return hash;
}
void header(net::Writer& w, const std::vector<uint8_t>& base, const std::vector<uint8_t>& target, uint32_t commands) {
    w.u32(kMagic); w.u32(kVersion); w.u32(uint32_t(base.size())); w.u32(uint32_t(target.size()));
    const auto before = upd::sha256_hex(base), after = upd::sha256_hex(target);
    w.raw(before.data(), before.size()); w.raw(after.data(), after.size()); w.u32(commands);
}
std::vector<uint8_t> literal_patch(const std::vector<uint8_t>& base, const std::vector<uint8_t>& target) {
    net::Writer w; header(w, base, target, target.empty() ? 0 : 1);
    if (!target.empty()) { w.u8(1); w.u32(uint32_t(target.size())); w.raw(target.data(), target.size()); }
    return std::move(w.data);
}
}
bool encode(const std::vector<uint8_t>& base, const std::vector<uint8_t>& target,
            std::vector<uint8_t>& patch, std::string& error) {
    if (base.size() > kRootBytes || target.size() > kRootBytes) { error = "Editor binary delta root exceeds its limit"; return false; }
    try {
        net::Writer commands; uint32_t count = 0;
        const auto literal = [&](size_t start, size_t length) {
            if (!length) return;
            ++count; commands.u8(1); commands.u32(uint32_t(length)); commands.raw(target.data() + start, length);
        };
        const auto copy = [&](size_t start, size_t length) { ++count; commands.u8(0); commands.u32(uint32_t(start)); commands.u32(uint32_t(length)); };
        if (base == target) { if (!target.empty()) copy(0, target.size()); }
        else if (base.size() < kAnchor || target.size() < kAnchor) literal(0, target.size());
        else {
            // At most four candidates per fingerprint and at most one anchor per
            // 64 base bytes. Verify bytes even when fingerprints match.
            std::unordered_map<uint64_t, Anchors> index;
            index.reserve(base.size() / kAnchor + 1);
            for (size_t pos = 0; pos + kAnchor <= base.size(); pos += kAnchor) {
                auto& bucket = index[fingerprint(base, pos)];
                if (bucket.count < bucket.offsets.size()) bucket.offsets[bucket.count++] = uint32_t(pos);
            }
            uint64_t power = 1; for (size_t n = 1; n < kAnchor; ++n) power *= kMultiplier;
            size_t pos = 0, pending = 0;
            uint64_t hash = fingerprint(target, 0);
            size_t work = 0, max_work = 8 * (base.size() + target.size()) + 4096;
            bool fallback = false;
            while (pos + kAnchor <= target.size()) {
                size_t best = 0, source = 0;
                if (auto found = index.find(hash); found != index.end()) {
                    for (unsigned n = 0; n < found->second.count; ++n) {
                        const size_t at = found->second.offsets[n];
                        if (max_work - work < kAnchor) { fallback = true; break; }
                        work += kAnchor;
                        if (std::memcmp(base.data() + at, target.data() + pos, kAnchor)) continue;
                        size_t length = kAnchor;
                        while (at + length < base.size() && pos + length < target.size()) {
                            if (++work > max_work) { fallback = true; break; }
                            if (base[at + length] != target[pos + length]) break;
                            ++length;
                        }
                        if (fallback) break;
                        if (length > best) { best = length; source = at; }
                    }
                }
                if (fallback) break;
                if (best) {
                    literal(pending, pos - pending); copy(source, best); pos += best; pending = pos;
                    if (pos + kAnchor <= target.size()) hash = fingerprint(target, pos);
                } else {
                    if (pos + kAnchor < target.size()) hash = (hash - (uint64_t(target[pos]) + 1) * power) * kMultiplier + uint64_t(target[pos + kAnchor]) + 1;
                    ++pos;
                }
                if (count >= kCommands || commands.size() > target.size() + 5) { fallback = true; break; }
            }
            if (fallback) { auto result = literal_patch(base, target); patch = std::move(result); error.clear(); return true; }
            literal(pending, target.size() - pending);
        }
        if (count > kCommands || commands.size() > target.size() + 5) { auto result = literal_patch(base, target); patch = std::move(result); error.clear(); return true; }
        net::Writer result; header(result, base, target, count); if (commands.size()) result.raw(commands.data.data(), commands.size());
        patch = std::move(result.data); error.clear(); return true;
    } catch (...) { error = "Editor binary delta encoding failed"; return false; }
}
bool apply(const std::vector<uint8_t>& base, const std::vector<uint8_t>& patch,
           std::vector<uint8_t>& target, std::string& error) {
    if (base.size() > kRootBytes || patch.size() > kPatchBytes || patch.size() < 148) { error = "Invalid editor binary delta size"; return false; }
    try {
        net::Reader r(patch.data(), patch.size());
        const auto magic = r.u32(), version = r.u32(), base_size = r.u32(), size = r.u32();
        std::array<char, 64> before{}, after{}; r.raw(before.data(), before.size()); r.raw(after.data(), after.size()); const auto count = r.u32();
        if (!r.ok() || magic != kMagic || version != kVersion || base_size != base.size() || size > kRootBytes || count > kCommands ||
            count > r.left() / 5 || (!size && count) || (size && !count) || std::string(before.data(), before.size()) != upd::sha256_hex(base)) {
            error = "Invalid editor binary delta header or base hash"; return false;
        }
        std::vector<uint8_t> result; result.reserve(size);
        for (uint32_t i = 0; i < count; ++i) {
            const auto kind = r.u8(); uint32_t offset = 0;
            if (kind == 0) offset = r.u32();
            const auto length = r.u32();
            if (!r.ok() || kind > 1 || !length || length > size - result.size() ||
                (kind == 0 && (offset > base.size() || length > base.size() - offset)) || (kind == 1 && length > r.left())) {
                error = "Invalid editor binary delta command"; return false;
            }
            if (kind == 0) result.insert(result.end(), base.begin() + offset, base.begin() + offset + length);
            else { result.insert(result.end(), r.cur(), r.cur() + length); r.skip(length); }
        }
        if (!r.ok() || r.left() || result.size() != size || std::string(after.data(), after.size()) != upd::sha256_hex(result)) {
            error = "Editor binary delta target hash or length mismatch"; return false;
        }
        target = std::move(result); error.clear(); return true;
    } catch (...) { error = "Editor binary delta application failed"; return false; }
}
} // namespace editor::binary_delta
