// Portable scalar encoding of the verified Native RefPack format. No game calls.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace editor_native_refpack {
inline constexpr size_t kInputLimit=16000000;
enum class Window : uint32_t { Small=0x3FFF,Large=0x1FFFF };
// Synchronous borrowed input, owned staging/output. Output is unchanged on failure,
// including when input aliases output. The bytes establish no Native provenance.
bool encode(std::span<const uint8_t>,Window,std::vector<uint8_t>&,std::string&);
}
