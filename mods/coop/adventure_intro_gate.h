// Ownership tokens for native cinematics held at the party's scene barriers.
#pragma once

#include <cstdint>

namespace adventure_intro {
struct Cinematic {
    uintptr_t manager = 0;
    uint32_t active = 0, id = 0;
};
enum class Action { Ignore, EndCinematic, CinematicMessage, PlayMessage, Changed };
struct Release {
    Action action = Action::Ignore;
    uint64_t generation = 0;
    uint32_t native_message = 0;
};

class Gate {
public:
    bool capture(uint64_t run, Cinematic current, uint32_t native_message = 0)
    {
        if (!run || !current.manager || !current.active || releasing_ || released(run)) return false;
        if (pending_ && run_ == run && cinematic_.manager &&
            (cinematic_.manager != current.manager || cinematic_.id != current.id)) return false;
        if (run_ != run) native_message_ = 0;
        run_ = run; cinematic_ = current; pending_ = true;
        if (native_message) native_message_ = native_message;
        return true;
    }
    void legacy_message(uint64_t run)
    {
        if (!run || releasing_ || released(run)) return;
        if (run_ != run) { cinematic_ = {}; native_message_ = 0; }
        run_ = run; pending_ = true;
    }
    Release release(uint64_t run, Cinematic current)
    {
        if (!pending_ || !run || run != run_) return {};
        Action action = !cinematic_.manager ? Action::PlayMessage :
            current.manager == cinematic_.manager && current.active && current.id == cinematic_.id
            ? native_message_ ? Action::CinematicMessage : Action::EndCinematic : Action::Changed;
        uint32_t native_message = native_message_;
        run_ = 0; pending_ = false; cinematic_ = {};
        native_message_ = 0;
        releasing_ = action != Action::Ignore && action != Action::Changed;
        if (releasing_) released_run_ = run;
        return {action, generation_, native_message};
    }
    void finish(Release release)
    {
        if (release.generation == generation_ &&
            release.action != Action::Ignore && release.action != Action::Changed) releasing_ = false;
    }
    void reset()
    {
        ++generation_; run_ = released_run_ = 0; native_message_ = 0;
        pending_ = releasing_ = false; cinematic_ = {};
    }
    bool pending() const { return pending_; }
    bool releasing() const { return releasing_; }
    // Caption advance may finish the native cinematic on a later frame. The
    // same released run must bypass both end hooks until reset/new native entry.
    bool released(uint64_t run) const { return run && released_run_ == run; }
    uint64_t run() const { return run_; }
    uint64_t generation() const { return generation_; }
    Cinematic cinematic() const { return cinematic_; }
private:
    uint64_t generation_ = 0, run_ = 0, released_run_ = 0;
    uint32_t native_message_ = 0;
    bool pending_ = false, releasing_ = false;
    Cinematic cinematic_;
};

// Talk can occur repeatedly within one run. A run and serial own one native
// cinematic ID, including the asynchronous tail after replaying Finished.
class SceneGate {
public:
    bool begin(uint64_t run, uint64_t serial, Cinematic current)
    {
        reset();
        if (!run || !serial || !gate_.capture(serial, current)) return false;
        run_ = run; serial_ = serial; id_ = current.id;
        return true;
    }
    bool capture(uint64_t run, uint64_t serial, Cinematic current, uint32_t message = 0)
    {
        return matches(run, serial) && !settling_ && !completed_ && gate_.capture(serial, current, message);
    }
    Release release(uint64_t run, uint64_t serial, Cinematic current)
    {
        if (!matches(run, serial)) return {};
        auto result = gate_.release(serial, current);
        settling_ = result.action == Action::EndCinematic || result.action == Action::CinematicMessage;
        return result;
    }
    void replayed(Release result) { gate_.finish(result); }
    bool complete(uint64_t run, uint32_t id)
    {
        if (!settling_ || !run || run != run_ || id != id_) return false;
        // Clear before native cleanup and its objective callback can re-enter.
        settling_ = false; completed_ = true;
        return true;
    }
    bool duplicate(uint64_t run, uint32_t id) const
    {
        return completed_ && run && run == run_ && id == id_;
    }
    bool matches(uint64_t run, uint64_t serial) const
    {
        return run && serial && run == run_ && serial == serial_;
    }
    bool owns(uint64_t run, Cinematic current) const
    {
        return run && run == run_ && !completed_ && current.manager == gate_.cinematic().manager &&
            current.active && current.id == id_;
    }
    bool settling() const { return settling_; }
    uint64_t generation() const { return gate_.generation(); }
    void reset()
    {
        gate_.reset(); run_ = serial_ = 0; id_ = 0;
        settling_ = completed_ = false;
    }
private:
    Gate gate_;
    uint64_t run_ = 0, serial_ = 0;
    uint32_t id_ = 0;
    bool settling_ = false, completed_ = false;
};
} // namespace adventure_intro
