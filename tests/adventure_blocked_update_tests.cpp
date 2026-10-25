// Exercise the actual creature Update wrapper; filter queries stay pure and a
// rejected simulation call invokes the optional handler exactly once.
#include <cstdio>
#include "../mods/coop/game.cpp"

namespace {
int failures, originals, blocked, extra_calls, base_calls, observed_before, observed_after;
bool extra_allows = true, base_allows = true;
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
    std::printf("adventure blocked Update dispatch: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
