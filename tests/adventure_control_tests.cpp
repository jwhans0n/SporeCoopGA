// Win32 native control regression: real hook wrappers and life transitions.
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "../mods/coop/adventure_control.cpp"

namespace {
using skg::field;
std::array<uint8_t, 0x16A0> local{}, remote{}, outsider{};
std::array<uint8_t, 0x610> local_behavior{}, remote_behavior{};
ga::Captain local_state, remote_state;
bool active = true;
bool local_paused;
int failures, hooks, brain_calls, walk_calls, cursor_calls, order_calls, stopped, positioned, cleared;
void* last_strategy;
std::string hash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
void check(bool value, const char* message)
{ if (!value) { ++failures; std::printf("FAIL: %s\n", message); } }
void SK_THISCALL native_brain(void* actor, float seconds)
{ ++brain_calls; check((actor == local.data() || actor == remote.data() || actor == outsider.data()) && seconds == .125f,
                     "brain forwards native actor and float argument"); }
void SK_THISCALL native_walk(void* strategy) { ++walk_calls; last_strategy = strategy; }
bool SK_THISCALL native_cursor(void* strategy, bool first, bool second)
{ ++cursor_calls; last_strategy = strategy; check(first && !second, "cursor bool stack arguments survive the hook"); return true; }
uintptr_t SK_THISCALL native_order(void*, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g)
{
    ++order_calls;
    check(a == 1 && b == 2 && c == 0x3F000000 && d == 4 && e == 5 && f == 6 && g == 7,
          "all seven opaque order words, including float bits, survive the native wrapper");
    return 0xCAFE;
}
void SK_THISCALL native_clear(void* list, uint32_t low, uint32_t high)
{
    ++cleared;
    check((list == local_behavior.data() + 8 || list == remote_behavior.data() + 8) &&
          low == UINT32_MAX && high == UINT32_MAX, "transition clears all native order groups exactly once");
}
uintptr_t symbol(const char* name)
{
    if (!std::strcmp(name, "SporeCoop::Adventure::AvatarTickAI")) return reinterpret_cast<uintptr_t>(&native_brain);
    if (!std::strcmp(name, "SporeCoop::Adventure::MoveAvatarWASD")) return reinterpret_cast<uintptr_t>(&native_walk);
    if (!std::strcmp(name, "SporeCoop::Adventure::MoveAvatarToCursor")) return reinterpret_cast<uintptr_t>(&native_cursor);
    if (!std::strcmp(name, "SporeCoop::Adventure::IssueCreatureOrder")) return reinterpret_cast<uintptr_t>(&native_order);
    if (!std::strcmp(name, "SporeCoop::Adventure::RemoveBehaviorOrders")) return reinterpret_cast<uintptr_t>(&native_clear);
    return 0;
}
int hook(SkMod*, void* target, void*, void** previous) { ++hooks; *previous = target; return 0; }
const char* executable_hash() { return hash.c_str(); }
adventure_control::Ctx context()
{
    adventure_control::Ctx result;
    result.active = [] { return active; };
    result.local_avatar = [] { return local.data(); };
    result.local_paused = [] { return local_paused; };
    result.captain_state = [](void* object, ga::Captain& state) {
        if (object == local.data()) state = local_state;
        else if (object == remote.data()) state = remote_state;
        else return false;
        return true;
    };
    return result;
}
uintptr_t issue(void* actor)
{ return adventure_control::order(actor, 1, 2, 0x3F000000, 4, 5, 6, 7); }
} // namespace

namespace game {
net::Vec3 position(void* object) { return field<net::Vec3>(object, kPosOff); }
void set_position(void* object, const net::Vec3& value) { ++positioned; field<net::Vec3>(object, kPosOff) = value; }
void stop(void*) { ++stopped; }
} // namespace game

int main()
{
    SkHost host{}; host.sym = symbol; host.hook_create = hook; host.exe_sha256 = executable_hash;
    hash = "unsupported";
    check(!adventure_control::init(&host, nullptr, context()) && !hooks, "unsupported executables install no control hooks");
    hash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
    check(adventure_control::init(&host, nullptr, context()) && hooks == 4, "all verified control hooks install together");
    field<void*>(local.data(), 0xB4C) = local_behavior.data(); field<uint32_t>(local.data(), 0x24) = 1;
    field<void*>(remote.data(), 0xB4C) = remote_behavior.data(); field<uint32_t>(remote.data(), 0x24) = 2;
    local_state.player = 0; remote_state.player = 1;
    local_state.position = {10, 20, 30}; remote_state.position = {20, 20, 30};
    void* strategy = reinterpret_cast<void*>(0x13572468);
    adventure_control::brain(local.data(), .125f); adventure_control::walk(strategy);
    check(adventure_control::cursor(strategy, true, false) && issue(local.data()) == 0xCAFE && last_strategy == strategy,
          "alive captain controls and native returns remain intact");
    check(brain_calls == 1 && walk_calls == 1 && cursor_calls == 1 && order_calls == 1, "alive native calls execute once");
    local_state.life = ga::Life::Downed;
    for (int n = 0; n < 10000; ++n) {
        adventure_control::brain(local.data(), .125f); adventure_control::walk(strategy);
        check(!adventure_control::cursor(strategy, true, false) && !issue(local.data()), "downed native commands are rejected");
        adventure_control::creature_update(local.data(), true);
        // Simulate the graphics/physics update; presentation is allowed to run.
        field<net::Vec3>(local.data(), game::kVelOff) = {1, 2, 3};
        adventure_control::creature_update(local.data(), false);
    }
    check(brain_calls == 1 && walk_calls == 1 && cursor_calls == 1 && order_calls == 1,
          "held movement, cursor commands, brains and abilities cannot execute while downed");
    check(stopped == 1 && cleared == 1 && positioned == 1, "stable downed updates do not repeat stop, order teardown or pose writes");
    check(field<net::Vec3>(local.data(), game::kVelOff).x == 0, "native update cannot leave residual velocity behind");
    field<net::Vec3>(local.data(), game::kPosOff) = {11, 20, 30};
    adventure_control::creature_update(local.data(), false);
    check(positioned == 2 && game::position(local.data()).x == 10, "actual simulation displacement is corrected immediately");
    adventure_control::brain(outsider.data(), .125f); check(issue(outsider.data()) == 0xCAFE, "unrelated NPCs remain untouched");
    local_state.life = ga::Life::Alive; remote_state.life = ga::Life::Downed;
    adventure_control::creature_update(local.data(), true);
    adventure_control::walk(strategy); adventure_control::brain(remote.data(), .125f);
    check(walk_calls == 2 && brain_calls == 2 && !issue(remote.data()), "remote knockout cannot lock the standing local captain");
    adventure_control::creature_update(remote.data(), true);
    check(stopped == 2 && cleared == 2, "remote downed presentation has one independent transition");
    field<uint32_t>(remote.data(), 0x24) = 3;
    adventure_control::creature_update(remote.data(), true);
    check(stopped == 3 && cleared == 3, "same-address reincarnated captain receives a fresh transition");
    local_state.life = ga::Life::Alive; local_paused = true;
    int previous_brains = brain_calls, previous_orders = order_calls;
    adventure_control::brain(local.data(), .125f); check(!issue(local.data()), "a local pause intent blocks owner orders");
    adventure_control::walk(strategy); check(!adventure_control::cursor(strategy, true, false), "a local pause blocks owner input");
    check(brain_calls == previous_brains && order_calls == previous_orders, "owner pause cannot execute its old brain or commands");
    adventure_control::brain(outsider.data(), .125f); check(issue(outsider.data()) == 0xCAFE,
          "one player's pause leaves host NPC brains and orders advancing");
    const auto menu_position = game::position(local.data());
    adventure_control::creature_update(local.data(), true);
    field<net::Vec3>(local.data(), game::kVelOff) = {5, 0, 0};
    field<net::Vec3>(local.data(), game::kPosOff).x += 2;
    adventure_control::creature_update(local.data(), false);
    check(game::position(local.data()).x == menu_position.x && field<net::Vec3>(local.data(), game::kVelOff).x == 0,
          "partial menu pause stops only the owner's residual motion at the native Update boundary");
    local_paused = false; adventure_control::creature_update(local.data(), true);
    adventure_control::brain(local.data(), .125f); check(issue(local.data()) == 0xCAFE, "Resume restores owner controls");
    active = false; int writes = positioned;
    adventure_control::creature_update(reinterpret_cast<void*>(0x1234), true);
    adventure_control::brain(local.data(), .125f); check(issue(remote.data()) == 0xCAFE, "inactive adventure restores normal controls");
    adventure_control::reset();
    check(positioned == writes && adventure_control::held.empty(), "cancellation forgets held pointers without touching retired objects");
    std::printf("adventure native controls: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
