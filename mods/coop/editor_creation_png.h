#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace editor_creation_png {
// Straight, top-down RGBA8. Both functions leave out untouched on failure.
// Encoding is fixed across machines; WIC is used only for bounded decoding.
bool from_rgba(uint32_t width,uint32_t height,std::span<const uint8_t> rgba,
               std::vector<uint8_t>& out,std::string& error);
bool sanitize(std::span<const uint8_t> png,std::vector<uint8_t>& out,std::string& error);
}
