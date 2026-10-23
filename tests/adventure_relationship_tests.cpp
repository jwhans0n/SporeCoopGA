// Native Win32 thiscall regression: three stack arguments, exact forwarding,
// authored relationship outcomes, and inactive/solo/null capability boundaries.
#include <array>
#include <cstdio>
#include <cstring>

#include "adventure_relationships.h"
#include "sporekit_game.hpp"

namespace {
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
const char* hash = kHash;
bool active = true, symbol_present = true, avatar_present = true, fail_hook;
bool use_snapshot;
int failures, creates, native_calls, predicate_calls, avatar_calls, snapshot_calls;
int snapshot_value = 6;
std::array<unsigned char, 0xB60> avatar{}, remote{}, remote2{}, npc{}, other{};
std::array<unsigned char, 12> species_key{};
void* manager = reinterpret_cast<void*>(0x1234ABCD);
void* expected_first;
void* expected_second;
int native_result = 5;

void check(bool value, const char* message)
{
    if (!value) { ++failures; std::printf("FAIL: %s\n", message); }
}
int SK_THISCALL native_relationship(void* self, const void* key, void* first, void* second)
{
    ++native_calls;
    check(self == manager && key == species_key.data(), "manager and species key survive unchanged");
    check(first == expected_first && second == expected_second, "only registered remote captain arguments are aliased");
    return native_result;
}
using Relationship = decltype(&native_relationship);
Relationship detour;
uintptr_t symbol(const char* name)
{
    check(!std::strcmp(name, "SporeCoop::Adventure::GetCreatureRelationship"), "verified relationship symbol requested");
    return symbol_present ? reinterpret_cast<uintptr_t>(&native_relationship) : 0;
}
const char* exe_hash() { return hash; }
int create(SkMod*, void* target, void* hook, void** out)
{
    ++creates;
    if (fail_hook) return 1;
    check(target == reinterpret_cast<void*>(&native_relationship), "verified relationship target hooked");
    *out = target; detour = reinterpret_cast<Relationship>(hook); return 0;
}
adventure_relationships::Ctx context()
{
    return {[] { return active; }, [](void* value) {
        ++predicate_calls; return value == remote.data() || value == remote2.data();
    }, []() -> void* { ++avatar_calls; return avatar_present ? avatar.data() : nullptr; },
    [](void* actor, int& result) {
        ++snapshot_calls;
        if (!use_snapshot || actor != npc.data()) return false;
        result = snapshot_value;
        return true;
    }};
}
void call(void* first, void* second, void* want_first, void* want_second, int result)
{
    expected_first = want_first; expected_second = want_second; native_result = result;
    check(detour(manager, species_key.data(), first, second) == result, "full native int relationship result preserved");
}
} // namespace

int main()
{
    SkHost host{}; host.sym = symbol; host.exe_sha256 = exe_hash; host.hook_create = create;
    auto original_avatar = avatar, original_remote = remote, original_npc = npc;
    hash = "unverified";
    check(!adventure_relationships::init(&host, nullptr, context()) && creates == 0, "unverified build installs no hook");
    hash = kHash; symbol_present = false;
    check(!adventure_relationships::init(&host, nullptr, context()) && creates == 0, "missing symbol installs no hook");
    symbol_present = true; fail_hook = true;
    check(!adventure_relationships::init(&host, nullptr, context()) && !adventure_relationships::available(), "hook failure leaves adapter unavailable");
    fail_hook = false;
    check(adventure_relationships::init(&host, nullptr, context()), "verified relationship adapter initializes");
    call(npc.data(), remote.data(), npc.data(), avatar.data(), 5);
    call(remote.data(), npc.data(), avatar.data(), npc.data(), 3);
    call(remote.data(), remote2.data(), avatar.data(), avatar.data(), 6);
    call(npc.data(), other.data(), npc.data(), other.data(), 0);
    call(nullptr, remote.data(), nullptr, avatar.data(), -7);
    call(remote.data(), nullptr, avatar.data(), nullptr, 0x12345678);
    unsigned predicates = predicate_calls, avatars = avatar_calls;
    active = false;
    call(npc.data(), remote.data(), npc.data(), remote.data(), 0);
    check(predicate_calls == int(predicates) && avatar_calls == int(avatars), "solo or inactive path performs no party lookups");
    active = true; avatar_present = false;
    call(npc.data(), remote.data(), npc.data(), remote.data(), 0);
    avatar_present = true;
    for (int i = 0; i < 10000; ++i) {
        call(npc.data(), remote.data(), npc.data(), avatar.data(), 2);
        call(remote.data(), npc.data(), avatar.data(), npc.data(), 5);
    }
    check(native_calls == 20008 && adventure_relationships::aliased_calls() == 20005,
          "repeated thiscall calls preserve stack balance and alias count");
    int before = native_calls;
    use_snapshot = true;
    for (int value = 0; value <= 7; ++value) {
        snapshot_value = value;
        check(detour(manager, species_key.data(), npc.data(), remote.data()) == value,
              "host snapshot overrides NPC-versus-remote relationship");
        check(detour(manager, species_key.data(), remote.data(), npc.data()) == value,
              "host snapshot preserves reversed NPC-versus-remote query");
        check(detour(manager, species_key.data(), npc.data(), avatar.data()) == value,
              "host snapshot overrides NPC-versus-local relationship");
        check(detour(manager, species_key.data(), avatar.data(), npc.data()) == value,
              "host snapshot preserves reversed NPC-versus-local query");
    }
    check(native_calls == before && adventure_relationships::authoritative_calls() == 32,
          "authoritative party queries do not evaluate divergent guest caches");
    int snapshots = snapshot_calls;
    call(npc.data(), other.data(), npc.data(), other.data(), 2);
    call(remote.data(), remote2.data(), avatar.data(), avatar.data(), 6);
    call(avatar.data(), remote.data(), avatar.data(), avatar.data(), 6);
    call(nullptr, remote.data(), nullptr, avatar.data(), 0);
    check(snapshot_calls == snapshots, "NPC-versus-NPC, party pairs and nulls never ask for a snapshot");
    // A supplied snapshot only authorizes the actor accepted by the exact binding
    // accessor. Missing/stale and malformed values retain native evaluation.
    call(other.data(), remote.data(), other.data(), avatar.data(), 3);
    snapshot_value = -1;
    call(npc.data(), remote.data(), npc.data(), avatar.data(), 5);
    snapshot_value = 8;
    call(npc.data(), avatar.data(), npc.data(), avatar.data(), 3);
    snapshot_value = 6; active = false; snapshots = snapshot_calls;
    call(npc.data(), avatar.data(), npc.data(), avatar.data(), 3);
    check(snapshot_calls == snapshots, "inactive query never reads host snapshots");
    active = true;
    int captured = -99;
    expected_first = npc.data(); expected_second = avatar.data(); native_result = 5;
    before = native_calls; snapshots = snapshot_calls;
    check(adventure_relationships::query_local_party(manager, species_key.data(), npc.data(), captured) && captured == 5,
          "host capture reads the original local-party relationship");
    check(native_calls == before + 1 && snapshot_calls == snapshots,
          "host capture bypasses the hook and authoritative callback");
    native_result = 8; captured = -99;
    check(!adventure_relationships::query_local_party(manager, species_key.data(), npc.data(), captured) && captured == -99,
          "invalid native enum leaves capture output unchanged");
    native_result = 5; before = native_calls;
    check(!adventure_relationships::query_local_party(manager, species_key.data(), remote.data(), captured),
          "capture excludes registered party captains");
    check(!adventure_relationships::query_local_party(nullptr, species_key.data(), npc.data(), captured) &&
          !adventure_relationships::query_local_party(manager, nullptr, npc.data(), captured) &&
          !adventure_relationships::query_local_party(manager, species_key.data(), nullptr, captured),
          "capture rejects missing native participants");
    avatar_present = false;
    check(!adventure_relationships::query_local_party(manager, species_key.data(), npc.data(), captured), "capture requires actual avatar");
    avatar_present = true; active = false;
    check(!adventure_relationships::query_local_party(manager, species_key.data(), npc.data(), captured), "capture excludes inactive runs");
    active = true;
    check(native_calls == before, "rejected capture never calls native code");
    check(avatar == original_avatar && remote == original_remote && npc == original_npc,
          "alias adapter never mutates flags object data or authored actors");
    adventure_relationships::reset();
    check(adventure_relationships::available() && adventure_relationships::aliased_calls() == 0 &&
          adventure_relationships::authoritative_calls() == 0, "reset keeps hook and clears diagnostics");
    std::printf("adventure relationships: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
