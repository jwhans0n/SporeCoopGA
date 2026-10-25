// Exercise the actual creature Update wrapper; filter queries stay pure and a
// rejected simulation call invokes the optional handler exactly once.
#include <cstdio>
#include "../mods/coop/game.cpp"

namespace {
int failures, originals, blocked, extra_calls, base_calls, observed_before, observed_after;
bool extra_allows = true, base_allows = true;
bool npc_ai_allows;
int npc_ai_calls, npc_ai_queries, visual_updates;
float replicated_position, rendered_position;
void* actor = reinterpret_cast<void*>(0x12345678);
void check(bool value, const char* message)
{
    if (!value) { ++failures; std::printf("FAIL: %s\n", message); }
}
void SK_THISCALL original(void* self, int milliseconds)
{
    ++originals; check(self == actor && milliseconds == 123, "original Update receives exact self and milliseconds");
}
void handler(void* self, int milliseconds)
{
    ++blocked; check(self == actor && milliseconds == 123, "blocked handler receives exact self and milliseconds");
}
bool extra(void*) { ++extra_calls; return extra_allows; }
bool base(void*) { ++base_calls; return base_allows; }
void observer(void* self, bool before)
{
    check(self == actor, "simulation observer receives the exact native actor");
    if (before) { ++observed_before; check(originals == observed_before - 1, "before observer precedes the native update"); }
    else { ++observed_after; check(originals == observed_after, "after observer follows the native update"); }
}
bool npc_ai(void* self)
{
    ++npc_ai_queries; check(self == actor, "NPC authority filter sees the original creature pointer");
    return npc_ai_allows;
}
void SK_THISCALL original_npc_ai(void* self, float seconds)
{
    ++npc_ai_calls;
    check(self == actor && seconds == .123f, "NPC AI forwards the exact this pointer and float stack argument");
}
void SK_THISCALL native_visual_update(void* self, int milliseconds)
{
    check(self == actor && milliseconds == 123, "presentation update preserves native Update arguments");
    game::h_NPCTickAI(self, .123f);
    // A native creature Update consumes the replicated spatial pose for its
    // visual state. The old whole-Update fence never reaches this work.
    rendered_position = replicated_position;
    ++visual_updates;
}
}

int main()
{
    game::o_Update = original;
    game::set_extra_update_filter(extra); game::set_update_filter(base);
    game::set_blocked_update_handler(handler);
    game::set_update_observer(observer);
    game::h_Update(actor, 123);
    check(originals == 1 && !blocked && extra_calls == 1 && base_calls == 1, "allowed Update preserves ordinary native behavior");
    extra_allows = false; base_allows = false;
    game::h_Update(actor, 123);
    check(originals == 1 && blocked == 1 && base_calls == 1, "first rejection calls handler once without evaluating later filters");
    extra_allows = true;
    game::h_Update(actor, 123);
    check(originals == 1 && blocked == 2 && base_calls == 2, "second rejection calls handler once and skips original");
    check(observed_before == 1 && observed_after == 1, "rejected updates never invoke native simulation observers");
    for (int i = 0; i < 10000; ++i) { extra(actor); base(actor); }
    check(blocked == 2, "pure ownership/filter queries never run social presentation");
    for (int i = 0; i < 10000; ++i) game::h_Update(actor, 123);
    check(blocked == 10002 && originals == 1, "repeated rejected native calls preserve thiscall stack and exactly-once dispatch");
    game::set_blocked_update_handler(nullptr); game::h_Update(actor, 123);
    check(blocked == 10002 && originals == 1, "unloaded handler is optional and rejection remains effective");
    game::set_extra_update_filter(nullptr); game::set_update_filter(nullptr); game::h_Update(actor, 123);
    check(originals == 2, "normal native updates resume when filters are removed");
    check(observed_before == 2 && observed_after == 2, "allowed native updates remain balanced across filter changes");
    game::set_update_observer(nullptr);

    game::o_Update = native_visual_update; game::o_NPCTickAI = original_npc_ai;
    game::set_npc_ai_filter(npc_ai);
    replicated_position = 10; rendered_position = 0;
    game::set_extra_update_filter(extra); extra_allows = false;
    game::h_Update(actor, 123);
    check(rendered_position == 0 && !visual_updates && !npc_ai_calls,
          "legacy whole-Update rejection reproduces a frozen rendered pose despite a changed replicated spatial position");
    game::set_extra_update_filter(nullptr);
    auto counts = game::creature_update_counts();
    game::h_Update(actor, 123);
    check(rendered_position == 10 && visual_updates == 1 && !npc_ai_calls && npc_ai_queries == 1,
          "live guest presentation consumes replicated motion while its autonomous NPC brain stays suppressed");
    replicated_position = 20; game::h_Update(actor, 123);
    check(rendered_position == 20 && visual_updates == 2 && !npc_ai_calls,
          "later authoritative positions keep updating visual state without local NPC decisions");
    check(game::creature_update_counts().native == counts.native + 2 &&
          game::creature_update_counts().npc_ai_suppressed == counts.npc_ai_suppressed + 2,
          "diagnostics count actual native presentation and rejected brain dispatch separately");
    counts = game::creature_update_counts();
    for (int i = 0; i < 10000; ++i) npc_ai(actor);
    check(game::creature_update_counts().npc_ai_suppressed == counts.npc_ai_suppressed,
          "pure filter queries do not masquerade as suppressed native calls");
    for (int i = 0; i < 10000; ++i) game::h_Update(actor, 123);
    check(visual_updates == 10002 && !npc_ai_calls,
          "repeated native presentation plus AI suppression preserves both thiscall stacks");
    npc_ai_allows = true; game::h_Update(actor, 123);
    check(npc_ai_calls == 1, "host or otherwise permitted NPCs retain their original native brain update");
    int queries = npc_ai_queries;
    game::puppet_register(actor); game::h_Update(actor, 123); game::puppet_unregister(actor);
    check(npc_ai_calls == 1 && npc_ai_queries == queries,
          "existing remote captain puppet suppression remains effective before the adventure NPC filter");
    game::set_npc_ai_filter(nullptr); game::h_Update(actor, 123);
    check(npc_ai_calls == 2, "removing the adventure filter restores ordinary native NPC AI");
    std::printf("adventure blocked Update dispatch: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
