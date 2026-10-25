// Host-authored teleport generations fence delayed player movement packets.
#pragma once

#include <array>
#include <cmath>
#include "protocol.h"

namespace net {
class AdventureMotion {
public:
    void begin(uint64_t run) {
        if (run_ != run) { run_ = run; generations_.fill(0); }
    }
    void reset() { run_ = 0; generations_.fill(0); }
    uint64_t generation(uint8_t player) const {
        return player < kMaxPlayers ? generations_[player] : 0;
    }
    bool correct(uint64_t run, uint8_t player, uint64_t revision, const Vec3& pos, const Quat& rot) {
        if (!run || player >= kMaxPlayers || !revision ||
            !std::isfinite(pos.x) || !std::isfinite(pos.y) || !std::isfinite(pos.z) ||
            !std::isfinite(rot.x) || !std::isfinite(rot.y) || !std::isfinite(rot.z) || !std::isfinite(rot.w)) return false;
        const float norm = rot.x*rot.x + rot.y*rot.y + rot.z*rot.z + rot.w*rot.w;
        if (!std::isfinite(norm) || norm < 1e-8f) return false;
        if (run_ && run < run_) return false;
        begin(run);
        if (revision <= generations_[player]) return false;
        generations_[player] = revision;
        return true;
    }
    bool accepts(const PlayerState& state) const {
        if (state.mode != 0x01654C10 || !run_) return true;
        return state.id < kMaxPlayers && state.adventure_run == run_ &&
            state.movement_generation == generations_[state.id];
    }
private:
    uint64_t run_ = 0;
    std::array<uint64_t, kMaxPlayers> generations_{};
};
} // namespace net
