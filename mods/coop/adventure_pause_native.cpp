#include "adventure_pause_native.h"

#include <array>
#include <cstring>
#include <memory>
#include <utility>

#include "sporekit_game.hpp"

namespace adventure_pause_native {
namespace {
using namespace skg;
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
constexpr std::array<uint32_t, 3> kLocalTypes{0x04BF38A8, 0x04BF38A4, 0x04BF38A9};
using Recalculate = void(SK_THISCALL*)(void*);
Recalculate original;
const SkHost* H;
Ctx C;
bool ready, queued;
unsigned filtering;
uint64_t generation;
uintptr_t getter;

void* manager()
{
    void* p = getter ? cdecl_call<void*>(getter) : nullptr;
    return p && readable(p, 0x74) ? p : nullptr;
}
struct Counters {
    std::array<int*, 3> values{};
    bool paused() const { for (auto p : values) if (*p > 0) return true; return false; }
};
bool counters(void* self, Counters& result)
{
    if (!self || !readable(self, 0x74)) return false;
    auto* first = field<uint8_t*>(self, 0x68);
    auto* end = field<uint8_t*>(self, 0x6C);
    uintptr_t a = reinterpret_cast<uintptr_t>(first), b = reinterpret_cast<uintptr_t>(end);
    if (!a || b < a || (b - a) % 8 || b - a > 128 * 8 || !readable(first, b - a)) return false;
    // GetPauseCount B321E0 uses the native ordered map at +4C. Its lookup
    // E5C780 compares unsigned keys at node+10, follows left+4/right+0,
    // and reads the vector index at node+14. Bound traversal and every read.
    for (size_t i = 0; i < kLocalTypes.size(); ++i) {
        void* node = field<void*>(self, 0x58);
        bool found = false;
        for (unsigned depth = 0; node && depth < 32; ++depth) {
            if (!readable(node, 0x18)) return false;
            uint32_t key = field<uint32_t>(node, 0x10);
            if (key == kLocalTypes[i]) {
                uint32_t index = field<uint32_t>(node, 0x14);
                if (index >= (b - a) / 8) return false;
                auto* entry = first + index * 8;
                uint32_t flags = field<uint32_t>(entry, 4);
                if (flags != (i == 0 ? 7u : 3u)) return false;
                result.values[i] = reinterpret_cast<int*>(entry); found = true; break;
            }
            node = field<void*>(node, kLocalTypes[i] < key ? 4 : 0);
        }
        if (!found) return false;
        for (size_t j = 0; j < i; ++j) if (result.values[j] == result.values[i]) return false;
    }
    return true;
}
void SK_THISCALL recalculate(void* self)
{
    Counters local;
    if (!ready || !C.active() || self != manager() || !counters(self, local)) {
        original(self); return;
    }
    if (!filtering) C.local_intent(local.paused());
    if (C.held()) { original(self); return; }
    // Reuse the complete native transition, including its clock restart and
    // audio/effects notifications. Only local user/menu reasons are excluded
    // from its first-active-reason selection. Preserve all signed counters.
    std::array<int, 3> excluded{};
    for (size_t i = 0; i < excluded.size(); ++i) {
        if (*local.values[i] > 0) { excluded[i] = *local.values[i]; *local.values[i] = 0; }
    }
    ++filtering;
    original(self);
    --filtering;
    // Add rather than assign so a synchronous native Pause/Resume nested in
    // an effects notification cannot lose its counter increment/decrement.
    for (size_t i = 0; i < excluded.size(); ++i) *local.values[i] += excluded[i];
}
} // namespace

bool init(const SkHost* host, SkMod* mod, Ctx context)
{
    if (ready || !host || !host->sym || !host->hook_create || !host->run_outside_frame ||
        !host->exe_sha256 || !context.active || !context.held || !context.local_intent) return false;
    const char* hash = host->exe_sha256();
    if (!hash || std::strcmp(hash, kHash)) return false;
    auto address = host->sym("SporeCoop::Adventure::RecalculatePauseFlags");
    getter = host->sym("Simulator::cGameTimeManager::Get");
    if (!address || !getter || host->hook_create(mod, reinterpret_cast<void*>(address),
        reinterpret_cast<void*>(&recalculate), reinterpret_cast<void**>(&original))) return false;
    H = host; C = std::move(context); ready = true;
    return true;
}
bool local_intent()
{
    Counters local;
    return ready && !filtering && counters(manager(), local) && local.paused();
}
void changed()
{
    if (!ready || queued) return;
    queued = true;
    H->run_outside_frame([](void* opaque) {
        std::unique_ptr<uint64_t> token(static_cast<uint64_t*>(opaque));
        if (*token != generation) return;
        queued = false;
        if (void* p = manager()) recalculate(p);
        if (C.reset_movement) C.reset_movement();
    }, new uint64_t(generation));
}
void reset()
{
    ++generation; queued = false;
    if (ready) if (void* p = manager()) recalculate(p);
}
} // namespace adventure_pause_native
