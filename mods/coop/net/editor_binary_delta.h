// Bounded binary copy/literal patches for opaque editor root bytes.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace editor::binary_delta {
constexpr size_t kRootBytes = 16u << 20;
constexpr size_t kPatchBytes = kRootBytes + 256;
constexpr uint32_t kCommands = 65536;
bool encode(const std::vector<uint8_t>& base, const std::vector<uint8_t>& target,
            std::vector<uint8_t>& patch, std::string& error);
bool apply(const std::vector<uint8_t>& base, const std::vector<uint8_t>& patch,
           std::vector<uint8_t>& target, std::string& error);
} // namespace editor::binary_delta
