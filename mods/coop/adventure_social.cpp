#include "adventure_social.h"

#include <array>
#include <cstring>
#include <map>
#include <utility>

#include "sporekit_game.hpp"

namespace adventure_social {
static_assert(sizeof(void*) == 4, "The native social adapter requires Win32.");
namespace {
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
constexpr uint32_t kSocialDecider = 0x14FCF96E, kPropose = 0x341BD4BE;
// Native BT BC943D/9477/94B4 cleans 7/8/6 stack words respectively.
using Start = bool(SK_CDECL*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t);
using Tick = bool(SK_CDECL*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t, float);
using End = void(SK_CDECL*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, void*);
using GetOrder = uint8_t*(SK_THISCALL*)(void*, uint32_t, uint32_t);
using OrderTarget = void*(SK_CDECL*)(void*);
using Target = void*(SK_THISCALL*)(void*);
using Eligibility = int(SK_CDECL*)(void*, void*);
Start start;
Tick tick;
End end;
GetOrder get_order;
OrderTarget order_target;
Target target;
Eligibility original_eligibility;
Ctx C;
const SkHost* H;
bool ready, dispatching;
Counters stats;
uint64_t cached_frame = UINT64_MAX, cached_run;
int cached_act = -1;
void* cached_avatar;
void* cached_partner;

struct Entry {
    // DA4360 writes four POD fields at 0/4/8/C. It constructs the separate
    // intrusive/shared decider through native BCB220/D9F740; no EASTL container
    // is fabricated in this callback storage.
    alignas(4) std::array<uint8_t, 16> state{};
    void* actor = nullptr;
    void* behavior = nullptr;
    void* decider = nullptr;
    uint32_t incarnation = 0;
    uint64_t run = 0;
    int act = -1;
};
std::map<void*, Entry> entries;

bool exists(void* actor)
{
    return actor && C.listed(actor) && skg::readable(actor, 0xE80) &&
        !skg::field<bool>(actor, 0x20) && !skg::field<bool>(actor, 0x21);
}
bool live(void* actor) { return exists(actor) && !skg::field<bool>(actor, 0xB5E); }
uint8_t* behavior(void* actor)
{
    auto* value = skg::field<uint8_t*>(actor, 0xB4C);
    return value && skg::readable(value, 0x60C) ? value : nullptr;
}
uint8_t* decider(uint8_t* data)
{
    auto* value = skg::field<uint8_t*>(data, 0x600);
    return value && skg::readable(value, 0x228) && skg::field<uint32_t>(value, 8) == kSocialDecider &&
        !skg::field<bool>(value, 0x1C) ? value : nullptr;
}
bool owns(const Entry& entry)
{
    return exists(entry.actor) && skg::field<uint32_t>(entry.actor, 0x24) == entry.incarnation &&
        behavior(entry.actor) == entry.behavior &&
        skg::field<void*>(entry.behavior, 0x600) == entry.decider;
}
void finish(Entry& entry)
{
    // If native teardown already detached/replaced the controller, its owner
    // performed the release. Never run old exit against a new controller.
    if (entry.decider && owns(entry)) {
        entry.decider = nullptr; // claim before reentrant native callbacks
        end(entry.actor, 0, 0, 0, 0, entry.state.data());
        ++stats.ends;
    }
}
void prune(uint64_t run, int act, void* avatar, void* partner)
{
    for (auto it = entries.begin(); it != entries.end();) {
        auto& entry = it->second;
        bool keep = run && entry.run == run && entry.act == act && owns(entry) && C.mirrored(entry.actor);
        if (keep) {
            auto* controller = static_cast<uint8_t*>(entry.decider);
            keep = skg::readable(controller, 0x1AC) &&
                skg::field<void*>(controller, 0x1A8) == avatar &&
                skg::field<void*>(controller, 0x1A4) == partner;
        }
        if (keep) { ++it; continue; }
        finish(entry); it = entries.erase(it);
    }
}
bool pending(void* actor, void* avatar, void* partner, uint8_t* data)
{
    auto* order = get_order(data + 8, 0x10000, 0);
    if (!order || !skg::readable(order, 0x30)) return false;
    void* recipient = order_target(order);
    int kind = skg::field<int>(order, 0xC);
    if (actor == partner) return kind == 1 && recipient == avatar;
    // Native supporters have kind two and explicitly join the performer's
    // already-constructed shared controller. They never start a separate game.
    if (kind != 2 || recipient != partner) return false;
    auto found = entries.find(partner);
    return found != entries.end() && owns(found->second) &&
        skg::field<void*>(found->second.decider, 0x1A8) == avatar;
}

int SK_CDECL eligibility(void* avatar, void* actor)
{
    int result = original_eligibility(avatar, actor);
    // Native D1E720 state five blocks ability input when SocialRespond cannot
    // find a native SocialPropose BT leaf. Our callback owns separate POD state;
    // authorize only its exact ready performer, without fabricating a BT frame.
    if (result != 5 || !ready || !C.active() || !avatar || avatar != C.local_avatar()) return result;
    auto found = entries.find(actor);
    if (found == entries.end()) return result;
    auto& entry = found->second;
    if (!entry.run || entry.run != C.run() || entry.act != C.act() ||
        !live(avatar) || !live(actor) || !C.mirrored(actor) || !owns(entry) ||
        target(avatar) != actor || entry.state[8] != 1 || skg::field<uint32_t>(entry.state.data(), 0) != 6)
        return result;
    auto* player_behavior = behavior(avatar);
    auto* controller = decider(static_cast<uint8_t*>(entry.behavior));
    if (!player_behavior || skg::field<uint32_t>(player_behavior, 0x1D8) != 0x941BD4BF ||
        controller != entry.decider || !skg::readable(controller, 0x1E5) ||
        skg::field<void*>(controller, 0x1A4) != actor || skg::field<void*>(controller, 0x1A8) != avatar ||
        !skg::field<bool>(controller, 0x1E4)) return result;
    ++stats.input_queries_allowed;
    return 0;
}
} // namespace

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    if (ready || !host || !host->sym || !host->exe_sha256 || !host->frame_index || !host->hook_create || !ctx.active || !ctx.run ||
        !ctx.act || !ctx.local_avatar || !ctx.mirrored || !ctx.listed) return false;
    const char* hash = host->exe_sha256();
    if (!hash || std::strcmp(hash, kHash)) return false;
    const char* names[] = {"SporeCoop::Adventure::SocialProposeStart", "SporeCoop::Adventure::SocialProposeTick",
        "SporeCoop::Adventure::SocialProposeEnd", "SporeCoop::Adventure::GetBehaviorOrder",
        "SporeCoop::Adventure::SocialOrderTarget", "SporeCoop::Adventure::CreatureTarget",
        "SporeCoop::Adventure::SocialAbilityEligibility"};
    uintptr_t resolved[7]{};
    for (size_t i = 0; i < std::size(names); ++i) {
        resolved[i] = host->sym(names[i]); if (!resolved[i]) return false;
    }
    start = reinterpret_cast<Start>(resolved[0]); tick = reinterpret_cast<Tick>(resolved[1]);
    end = reinterpret_cast<End>(resolved[2]); get_order = reinterpret_cast<GetOrder>(resolved[3]);
    order_target = reinterpret_cast<OrderTarget>(resolved[4]); target = reinterpret_cast<Target>(resolved[5]);
    C = std::move(ctx);
    if (host->hook_create(mod, reinterpret_cast<void*>(resolved[6]), reinterpret_cast<void*>(&eligibility),
                          reinterpret_cast<void**>(&original_eligibility))) return false;
    H = host; stats = {}; ready = true; return true;
}
bool available() { return ready; }

void blocked_update(void* actor, int milliseconds)
{
    if (!ready || dispatching) return;
    dispatching = true;
    uint64_t run = C.active() ? C.run() : 0;
    int act = run ? C.act() : -1;
    if (!run) {
        if (!entries.empty()) prune(0, -1, nullptr, nullptr);
        cached_frame = UINT64_MAX; dispatching = false; return;
    }
    // The real native Update owns this actor pointer. Look for an explicit
    // order before any expensive noun-registry or avatar/partner lookup.
    auto* data = actor && skg::readable(actor, 0xB50) ? behavior(actor) : nullptr;
    auto found = entries.find(actor);
    bool ordered = data && get_order(data + 8, 0x10000, 0);
    if (entries.empty() && !ordered) { dispatching = false; return; }
    uint64_t frame = H->frame_index();
    if (cached_frame != frame || cached_run != run || cached_act != act) {
        cached_frame = frame; cached_run = run; cached_act = act;
        cached_avatar = C.local_avatar();
        cached_partner = live(cached_avatar) ? target(cached_avatar) : nullptr;
        if (!live(cached_partner) || !C.mirrored(cached_partner)) cached_partner = nullptr;
        prune(cached_partner ? run : 0, act, cached_avatar, cached_partner);
        found = entries.find(actor);
    }
    if (found == entries.end() && !ordered) { dispatching = false; return; }
    void* avatar = cached_avatar;
    void* partner = cached_partner;
    if (!partner || !live(actor) || !C.mirrored(actor) || milliseconds < 0 || milliseconds > 1000) {
        dispatching = false; return;
    }
    if (!data) { dispatching = false; return; }
    if (found == entries.end()) {
        if (entries.size() >= 32) { ++stats.rejected; dispatching = false; return; }
        if (!pending(actor, avatar, partner, data)) { dispatching = false; return; }
        // An existing native BT owns its own initialized callback and teardown.
        // Do not duplicate that lifecycle or overwrite its frame storage.
        if (skg::field<uint32_t>(data, 0x1D8) == kPropose || skg::field<void*>(data, 0x600)) {
            ++stats.rejected; dispatching = false; return;
        }
        Entry entry; entry.actor = actor; entry.behavior = data;
        entry.incarnation = skg::field<uint32_t>(actor, 0x24); entry.run = run; entry.act = act;
        auto [position, inserted] = entries.emplace(actor, entry); (void)inserted;
        found = position;
        bool started = start(actor, 0, 0, 0, 0, found->second.state.data(), 0);
        found->second.decider = skg::field<void*>(data, 0x600);
        if (!started || !decider(data) ||
            skg::field<void*>(found->second.decider, 0x1A4) != partner ||
            skg::field<void*>(found->second.decider, 0x1A8) != avatar) {
            finish(found->second); entries.erase(found); ++stats.rejected;
            dispatching = false; return;
        }
        ++stats.starts;
    }
    auto& entry = found->second;
    ++stats.ticks;
    if (!tick(actor, 0, 0, 0, 0, entry.state.data(), 0, float(milliseconds) * 0.001f)) {
        finish(entry); entries.erase(found);
    }
    dispatching = false;
}

void reset()
{
    if (dispatching) return;
    dispatching = true;
    for (auto& [actor, entry] : entries) { (void)actor; finish(entry); }
    entries.clear(); stats = {}; cached_frame = UINT64_MAX; cached_run = 0;
    cached_act = -1; cached_avatar = nullptr; cached_partner = nullptr; dispatching = false;
}
Counters counters() { return stats; }
} // namespace adventure_social
