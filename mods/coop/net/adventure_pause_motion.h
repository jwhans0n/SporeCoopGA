#pragma once

#include "protocol.h"

namespace net {
// Retains an admitted pose across a unanimous pause while still admitting
// health/identity fields and sender timestamps. It is independent of teleport
// generations: pause transitions must never reset the teleport authority fence.
class AdventurePauseMotion {
public:
    bool transition(uint64_t run, bool paused, const PlayerState* seed)
    {
        if (run_ != run) { reset(); run_ = run; }
        paused = paused && run;
        if (paused_ == paused) return false;
        paused_ = paused;
        if (seed && seed->adventure_run == run_) {
            anchor_ = *seed; anchor_.vel = {}; anchor_.flags &= ~PF_MOVING;
            anchored_ = true;
            if (!have_time_ || int32_t(seed->time_ms - time_) > 0) { time_ = seed->time_ms; have_time_ = true; }
        }
        return true;
    }
    void reset() { run_ = 0; paused_ = anchored_ = have_time_ = false; time_ = 0; anchor_ = {}; }
    bool paused() const { return paused_; }
    const PlayerState* anchor() const { return anchored_ ? &anchor_ : nullptr; }
    bool admit(PlayerState& s)
    {
        if (!run_ || s.mode != 0x01654C10) return true;
        if (s.adventure_run != run_ || (have_time_ && int32_t(s.time_ms - time_) <= 0)) return false;
        time_ = s.time_ms; have_time_ = true;
        if (paused_) {
            if (!anchored_) return false;
            s.pos = anchor_.pos; s.rot = anchor_.rot; s.vel = {}; s.flags &= ~PF_MOVING;
        }
        return true;
    }
private:
    uint64_t run_ = 0;
    bool paused_ = false, anchored_ = false, have_time_ = false;
    uint32_t time_ = 0;
    PlayerState anchor_;
};
} // namespace net
