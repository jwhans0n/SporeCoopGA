// Inactive value-only format producer. Success is not admission or Native proof.
#pragma once
#include "net/editor_document.h"
#include <array>
#include <span>
#include <string>
#include <vector>

namespace editor_native_pose_format {
inline constexpr size_t kImageLimit=16u<<20,kMarkerLimit=4096,kEditLimit=64;
enum class Result { Produced,Unsupported,Error };
// All ten serialized fields are mandatory. Unadvertised semantic companions
// still require their exact raw BASE words; no default values are synthesized.
struct MarkerBits {
    std::array<uint32_t,3> position{};
    std::array<uint32_t,4> rotation{};
    uint32_t scale=0,relative_scale=0,elevation=0,pitch=0,gain=0;
    std::array<uint32_t,3> destination_position{};
    std::array<uint32_t,4> destination_rotation{};
    uint32_t destination_scale=0;
    bool operator==(const MarkerBits&)const=default;
};
struct MarkerRow {
    editor::ObjectId object;
    uint32_t slot=0,class_index=0;
    MarkerBits bits;
    bool operator==(const MarkerRow&)const=default;
};
struct PoseEdit {
    editor::ObjectId object;
    uint32_t slot=0,class_index=0;
    std::array<uint32_t,3> position{};
    std::array<uint32_t,4> rotation{};
    bool rotate=true;
    bool operator==(const PoseEdit&)const=default;
};
// Existing position(12 bytes), optionally quaternion(16 bytes), only. Stable
// input spans are borrowed for this synchronous call; no callbacks or owners
// are retained. Non-Produced results preserve output, including aliased input.
// Unsupported means a bounded capability/profile fallback. Malformed input,
// stale exact words/identities and allocation/codec failures are Error.
Result patch(std::span<const uint8_t> base,std::span<const MarkerRow> complete_roster,
             std::span<const PoseEdit>,std::vector<uint8_t>& output,std::string& error);
}
