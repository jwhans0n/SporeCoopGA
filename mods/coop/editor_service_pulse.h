// Passive scheduling values. No game, Session, or Native state is read here.
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>

namespace editor_service_pulse {
using Clock = std::chrono::steady_clock;
using Time = Clock::time_point;
struct Scope {
    std::array<uint64_t, 13> words{};
    bool operator==(const Scope&) const = default;
};
struct Observation {
    uint64_t transport = 0;
    bool connected = false;
    std::optional<Scope> editor;
    bool operator==(const Observation&) const = default;
};
struct Ticket { Observation observation; uint64_t serial = 0; bool editor = false; };

// A task owns its Run until completion. Retirement is checked before its caller
// touches module globals. The reservation remains held throughout callbacks.
class Run {
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    Observation observation_;
    Time frame_{}, last_editor_{}, last_generic_{};
    uint64_t serial_ = 0, queued_ = 0, version_ = 0;
public:
    std::atomic<bool> alive{true};
    void publish(const Observation& value, std::optional<Time> frame = {}) {
        std::lock_guard lock(mutex_);
        if (!alive.load()) return;
        const bool changed = observation_ != value;
        observation_ = value;
        if (frame) frame_ = *frame;
        if (changed) { ++version_; changed_.notify_one(); }
    }
    void retire() noexcept { alive.store(false); changed_.notify_all(); }
    bool reserve(Time now, Ticket& out) {
        std::lock_guard lock(mutex_);
        if (!alive.load() || queued_ || !frame_.time_since_epoch().count()) return false;
        const bool editor = observation_.connected && observation_.editor.has_value() && now - frame_ < std::chrono::milliseconds(500);
        if (editor ? (last_editor_.time_since_epoch().count() && now - last_editor_ < std::chrono::milliseconds(5))
                   : (now - frame_ < std::chrono::milliseconds(500) || (last_generic_.time_since_epoch().count() && now - last_generic_ < std::chrono::milliseconds(100)))) return false;
        if (serial_ == UINT64_MAX) return false;
        out = {observation_, ++serial_, editor};
        queued_ = out.serial;
        if (editor) last_editor_ = now; else last_generic_ = now;
        return true;
    }
    bool current(const Ticket& ticket, const Observation& fresh, Time now) const {
        std::lock_guard lock(mutex_);
        return alive.load() && queued_ == ticket.serial && ticket.observation == fresh &&
            (ticket.editor || now - frame_ >= std::chrono::milliseconds(500));
    }
    void complete(uint64_t serial) noexcept {
        std::lock_guard lock(mutex_);
        if (queued_ == serial) queued_ = 0;
    }
    void wait() {
        std::unique_lock lock(mutex_);
        const auto version = version_;
        changed_.wait_for(lock, observation_.editor ? std::chrono::milliseconds(5) : std::chrono::milliseconds(100),
            [&] { return !alive.load() || version_ != version; });
    }
};
// Main-thread guard. In particular, a nested Windows-message pump must not run
// another Session update while an outer update or frame callback is active.
struct ServiceGuard {
    bool& busy;
    bool entered;
    explicit ServiceGuard(bool& value) : busy(value), entered(!value) { if (entered) busy = true; }
    ~ServiceGuard() { if (entered) busy = false; }
};
}
