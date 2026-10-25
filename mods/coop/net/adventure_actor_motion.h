#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "protocol.h"

namespace adventure_actor_motion {
struct Scope {
    uint64_t run = 0;
    uint8_t act = 0;
    uint32_t host_incarnation = 0, local_incarnation = 0;
    bool operator==(const Scope&) const = default;
};
struct Sample {
    uint32_t sequence = 0;
    net::Vec3 position;
    net::Quat orientation;
    double arrival_time = 0;
};
struct Local {
    net::Vec3 position;
    float scale = 1, standard_speed = 0;
    bool held = false, attached = false, alive = true;
    float navigation_lead = 0; // additional native goal lookahead, separate from positional prediction
    float command_interval = .15f;
};
enum class Command { Keep, Move, Stop, Snap };
enum class Reason { None, Initial, Following, Stationary, Severe, Stuck, Stale, Held, Attached, Dead, Resuming };
struct Decision {
    Command command = Command::Keep;
    Reason reason = Reason::None;
    net::Vec3 target;
    net::Quat orientation;
    float speed = 0;
};

inline bool finite(const net::Vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
inline net::Vec3 difference(const net::Vec3& a, const net::Vec3& b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline float length(const net::Vec3& p) { return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z); }
inline float distance(const net::Vec3& a, const net::Vec3& b) { return length(difference(a,b)); }

// Policy only: the caller owns native bindings and applies a returned command
// outside the render frame. Native MoveTo must precede SetDesiredSpeed because
// MoveTo restores the creature's standard speed itself on dc04.
class Follower {
public:
    void reset() { *this = {}; }
    bool admit(const Scope& scope, Sample sample)
    {
        const auto& q=sample.orientation;
        const float norm=q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w;
        if (!scope.run || scope.act>=8 || !sample.sequence || !finite(sample.position) ||
            !std::isfinite(sample.arrival_time) || sample.arrival_time<0 || !std::isfinite(norm) || norm<.0001f) return false;
        if (present_ && scope_==scope && (int32_t(sample.sequence-latest_.sequence)<=0 ||
            sample.arrival_time<latest_.arrival_time)) return false;
        if (!present_ || !(scope_==scope)) {
            reset(); scope_=scope; latest_=sample; present_=initial_=true;
            anchor_=sample; return true;
        }
        const float moved=distance(sample.position,latest_.position);
        // Small individual deltas can still form a real slow walk. Compare
        // against one fixed quiet anchor, not just the preceding packet.
        if (!still_samples_) {still_since_=latest_.arrival_time;still_anchor_=latest_.position;}
        if (moved<=.025f && distance(sample.position,still_anchor_)<=.025f) {
            ++still_samples_;
        } else { still_samples_=0; still_since_=sample.arrival_time; still_anchor_=sample.position; }
        // Arrival jitter can deliver consecutive packets together. Use their
        // known 100-ms source cadence as an interval floor, not as a clock.
        const uint32_t sequences=sample.sequence-anchor_.sequence;
        const double observed=sample.arrival_time-anchor_.arrival_time;
        if (observed>=.18 || sequences>=2) {
            const double nominal=.1*std::min<uint32_t>(sequences,8);
            const double elapsed=std::clamp(observed,std::max(.04,nominal*.5),std::max(.08,nominal*2.));
            const auto delta=difference(sample.position,anchor_.position);
            net::Vec3 raw{float(delta.x/elapsed),float(delta.y/elapsed),float(delta.z/elapsed)};
            const float magnitude=length(raw);
            if (magnitude>100) { raw={}; discontinuity_=true; }
            if (!velocity_ready_) { velocity_=raw; velocity_ready_=true; }
            else velocity_={velocity_.x*.5f+raw.x*.5f,velocity_.y*.5f+raw.y*.5f,velocity_.z*.5f+raw.z*.5f};
            anchor_=sample;
        }
        latest_=sample;
        if (stationary()) velocity_={};
        return true;
    }
    const net::Vec3& velocity() const { return velocity_; }
    bool moving() const { return present_ && !stationary() && length(velocity_)>.02f; }
    bool confirmed_stationary() const { return present_ && stationary(); }
    bool has_sample() const { return present_; }
    const Sample& latest() const { return latest_; }

    Decision step(const Local& local, double now)
    {
        Decision result;
        if (!present_ || !std::isfinite(now) || !finite(local.position)) return result;
        result.target=latest_.position; result.orientation=latest_.orientation;
        const bool blocked=local.held || local.attached || !local.alive;
        if (blocked) {
            result.reason=local.held?Reason::Held:local.attached?Reason::Attached:Reason::Dead;
            if (!suspended_) result.command=Command::Stop;
            suspended_=true; commanded_=false; stuck_since_=-1;
            return result;
        }
        if (suspended_) {
            suspended_=false; resume_sequence_=latest_.sequence; waiting_fresh_=true;
            velocity_={}; velocity_ready_=false; still_samples_=0; anchor_=latest_;
            last_command_=-1; stuck_since_=-1;
        }
        if (waiting_fresh_) {
            result.reason=Reason::Resuming;
            if (int32_t(latest_.sequence-resume_sequence_)<=0) return result;
            waiting_fresh_=false;
        }
        const double age=std::max(0.,now-latest_.arrival_time);
        if (age>.65) {
            result.reason=Reason::Stale;
            if (!stale_stopped_) result.command=Command::Stop;
            stale_stopped_=true;
            commanded_=false; stuck_since_=-1;
            return result;
        }
        stale_stopped_=false;
        const float unit=std::isfinite(local.scale)?std::clamp(local.scale,.25f,8.f):1.f;
        const float error=distance(local.position,latest_.position);
        const float observed_speed=length(velocity_);
        if (initial_ || discontinuity_ || error>std::max(8.f*unit,observed_speed*.75f)) {
            result.command=Command::Snap; result.reason=initial_?Reason::Initial:Reason::Severe;
            initial_=discontinuity_=false; commanded_=false; stuck_since_=-1; last_command_=now;
            return result;
        }
        if (stationary()) {
            if (error>.8f*unit) {
                result.command=Command::Snap; result.reason=Reason::Stationary;
                commanded_=false; stuck_since_=-1; last_command_=now;
            } else if (commanded_) {
                result.command=Command::Stop; result.reason=Reason::Stationary;
                commanded_=false; stuck_since_=-1;
            }
            return result;
        }
        // A small lead prevents reaching each latest packet's point and
        // stopping the native gait before the next source sample arrives.
        const float extra=std::isfinite(local.navigation_lead)?std::clamp(local.navigation_lead,0.f,1.5f):0.f;
        const float prediction=float(std::min(age+.1,.2))+extra;
        result.target={latest_.position.x+velocity_.x*prediction,
                       latest_.position.y+velocity_.y*prediction,
                       latest_.position.z+velocity_.z*prediction};
        const float target_error=distance(local.position,result.target);
        if (target_error<=.3f*unit && observed_speed<=.02f) return result;
        if (commanded_ && error>1.25f*unit) {
            if (distance(local.position,progress_position_)>.15f*unit) {
                progress_position_=local.position; stuck_since_=now;
            } else if (stuck_since_<0) { stuck_since_=now; progress_position_=local.position; }
            else if (now-stuck_since_>1.5) {
                result.command=Command::Snap; result.reason=Reason::Stuck;
                commanded_=false; stuck_since_=-1; last_command_=now; return result;
            }
        } else { progress_position_=local.position; stuck_since_=-1; }
        const float target_threshold=std::min(.1f*unit,std::max(.005f*unit,observed_speed*.05f));
        const bool changed_target=distance(result.target,last_target_)>target_threshold;
        const float interval=std::isfinite(local.command_interval)?std::clamp(local.command_interval,.15f,.4f):.15f;
        if (!commanded_ || last_command_<0 || (now-last_command_>=interval && changed_target) || now-last_command_>=.4) {
            result.command=Command::Move; result.reason=Reason::Following;
            const float base=observed_speed>.02f?observed_speed:
                (std::isfinite(local.standard_speed)&&local.standard_speed>0?local.standard_speed:2.f);
            // Catch up modestly, preserving native locomotion, water/flying
            // strategy and authored scale rather than warping every sample.
            result.speed=std::min(base+target_error*.5f,base*1.5f);
            result.speed=std::clamp(result.speed,.01f,100.f);
            commanded_=true; last_command_=now; last_target_=result.target;
        }
        return result;
    }
private:
    bool stationary() const {
        const double elapsed=latest_.arrival_time-still_since_;
        return still_samples_>=3 && elapsed>=.19 && distance(latest_.position,still_anchor_)<=.02f*float(elapsed);
    }
    Scope scope_;
    Sample latest_,anchor_;
    net::Vec3 velocity_,last_target_,progress_position_,still_anchor_;
    bool present_=false,initial_=false,velocity_ready_=false,discontinuity_=false;
    bool suspended_=false,waiting_fresh_=false,commanded_=false,stale_stopped_=false;
    uint32_t still_samples_=0,resume_sequence_=0;
    double still_since_=0,last_command_=-1,stuck_since_=-1;
};
} // namespace adventure_actor_motion
