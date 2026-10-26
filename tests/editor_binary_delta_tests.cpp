#include "net/editor_binary_delta.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>

namespace bd = editor::binary_delta;
static unsigned checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
static uint32_t random_state = 0xC011AB1E;
static uint32_t random32() { random_state ^= random_state << 13; random_state ^= random_state >> 17; random_state ^= random_state << 5; return random_state; }
static std::vector<uint8_t> random_bytes(size_t n) { std::vector<uint8_t> out(n); for (auto& b : out) b = uint8_t(random32()); return out; }
static void put32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    CHECK(at <= bytes.size() && bytes.size() - at >= 4);
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = uint8_t(value >> (i * 8));
}
static void rejected(const std::vector<uint8_t>& base, const std::vector<uint8_t>& patch) {
    std::vector<uint8_t> out{9, 8, 7}; std::string error;
    CHECK(!bd::apply(base, patch, out, error)); CHECK(out == (std::vector<uint8_t>{9, 8, 7})); CHECK(!error.empty());
}
static std::vector<uint8_t> roundtrip(const std::vector<uint8_t>& base, const std::vector<uint8_t>& target) {
    std::string error; std::vector<uint8_t> patch, out;
    CHECK(bd::encode(base, target, patch, error)); CHECK(patch.size() <= bd::kPatchBytes);
    CHECK(bd::apply(base, patch, out, error)); CHECK(out == target); return patch;
}
int main() {
    for (size_t n : {0u, 1u, 63u, 64u, 65u, 1000u, 262144u, 1048576u}) {
        auto base = random_bytes(n); auto patch = roundtrip(base, base);
        CHECK(patch.size() <= 157);
        auto changed = base; if (!changed.empty()) changed[n / 2] ^= 0xE1;
        roundtrip(base, changed); roundtrip({}, base); roundtrip(base, {});
    }
    for (size_t n : {262144u, 1048576u}) {
        auto base = random_bytes(n), target = base;
        for (size_t i = 1024; i < n; i += 8192) target[i] ^= 0x71;
        target.insert(target.begin() + 777, {1, 2, 3, 4, 5}); target.erase(target.begin() + 10000, target.begin() + 10007);
        auto patch = roundtrip(base, target); CHECK(patch.size() < 16000);
        auto wrong_base = base; wrong_base.back() ^= 1; rejected(wrong_base, patch);
    }
    roundtrip(std::vector<uint8_t>(1048576, 0), std::vector<uint8_t>(1048576, 0));
    auto zeros = std::vector<uint8_t>(1048576, 0), shifted = zeros;
    for (size_t i = 64; i < shifted.size(); i += 65) shifted[i] = 1;
    roundtrip(zeros, shifted); // Repeated anchors have a bounded candidate list.
    {
        auto base = random_bytes(128); auto copy = roundtrip(base, base);
        for (size_t n = 0; n < copy.size(); ++n) rejected(base, {copy.begin(), copy.begin() + n});
        for (size_t at : {0u, 4u, 8u, 12u, 16u, 80u, 144u, 148u, 149u, 153u}) {
            auto bad = copy; bad[at] ^= 0x80; rejected(base, bad);
        }
        auto bad = copy; put32(bad, 12, bd::kRootBytes + 1); rejected(base, bad);
        bad = copy; put32(bad, 144, bd::kCommands + 1); rejected(base, bad);
        bad = copy; put32(bad, 149, 129); rejected(base, bad);
        bad = copy; put32(bad, 153, 0); rejected(base, bad);
        bad = copy; put32(bad, 153, 129); rejected(base, bad);
        bad = copy; bad.push_back(0); rejected(base, bad);
        auto literal = roundtrip({}, base); bad = literal; put32(bad, 149, 0xFFFFFFFF); rejected({}, bad);
        bad = literal; bad[148] = 2; rejected({}, bad);
    }
    for (unsigned iteration = 0; iteration < 300; ++iteration) {
        auto base = random_bytes(random32() % 8192), target = base;
        for (unsigned i = 0; i < 12; ++i) {
            if (!target.empty() && random32() % 3) target[random32() % target.size()] ^= uint8_t(random32());
            else target.insert(target.begin() + (target.empty() ? 0 : random32() % target.size()), uint8_t(random32()));
        }
        const auto patch = roundtrip(base, target);
        for (unsigned trial = 0; trial < 5; ++trial) {
            auto damaged = patch;
            if (trial == 0) damaged.resize(random32() % damaged.size());
            else if (trial == 1) damaged.push_back(uint8_t(random32()));
            else damaged[random32() % damaged.size()] ^= uint8_t(1u << (random32() % 8));
            std::vector<uint8_t> out{3, 2, 1}; std::string error;
            if (bd::apply(base, damaged, out, error)) CHECK(out == target);
            else CHECK(out == (std::vector<uint8_t>{3, 2, 1}));
        }
    }
    for (unsigned iteration = 0; iteration < 1000; ++iteration) rejected(random_bytes(random32() % 128), random_bytes(random32() % 512));
    {
        auto limit = std::vector<uint8_t>(bd::kRootBytes, 0x6C); roundtrip(limit, limit);
        limit.push_back(0); std::vector<uint8_t> patch{7}; std::string error;
        CHECK(!bd::encode(limit, {}, patch, error)); CHECK(patch == (std::vector<uint8_t>{7}));
        CHECK(!bd::encode({}, limit, patch, error)); CHECK(patch == (std::vector<uint8_t>{7}));
        rejected({}, std::vector<uint8_t>(bd::kPatchBytes + 1, 0));
    }
    std::cout << "PASS " << checks << " editor binary delta checks\n";
}
