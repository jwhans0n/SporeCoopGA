// BUG-1279: exercise the production adventure identity consumer, not a copy of it.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <windows.h>

#include "driver.h"
#include "mouse.h"
#include "cell_logic.h"
#include "creations.h"
#include "creature_hp.h"
#include "game.h"
#include "adventure_native.h"
#include "sporekit.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "world_name.h"
#include "net/interp.h"

// Local linkage lets the linker discard unrelated stage adapters. The included
// functions, native offsets and readable-memory checks are the production code.
namespace {
#include "../mods/coop/modes.cpp"
namespace tested_modes = modes;
}

namespace fixture {
int checks, failures;
bool preview, symbol_present = true;
uint32_t preview_place;
std::array<uint8_t, 0xD0> mode{};
std::array<uint8_t, 0x20> data{}, resource{};
void* mode_ptr;

void check(bool value, const char* what)
{
    ++checks;
    if (!value) {
        ++failures;
        std::printf("FAIL: %s\n", what);
    }
}

uintptr_t symbol(const char* name)
{
    return symbol_present && !std::strcmp(name, "App::cScenarioMode::ptr")
               ? reinterpret_cast<uintptr_t>(&mode_ptr) : 0;
}

void native_key(uint32_t instance, uint32_t group)
{
    mode_ptr = mode.data();
    skg::field<void*>(mode.data(), 0x74) = data.data();
    skg::field<void*>(data.data(), 0x10) = resource.data();
    skg::field<uint32_t>(resource.data(), 8) = instance;
    skg::field<uint32_t>(resource.data(), 16) = group;
}

void expect_key(bool expected, uint32_t instance, uint32_t group, const char* what)
{
    uint32_t actual_instance = 0xDEADBEEFu, actual_group = 0xCAFEBABEu;
    const bool result = tested_modes::adventure_key(actual_instance, actual_group);
    check(result == expected && actual_instance == instance && actual_group == group, what);
}

void expect_place(uint32_t place, const char* what)
{
    check(tested_modes::adventure_place() == place, what);
}
} // namespace fixture

namespace adventure_native {
bool editor_preview() { return fixture::preview; }
uint32_t editor_preview_place() { return fixture::preview_place; }
} // namespace adventure_native

int main()
{
    using namespace fixture;
    SkHost host{};
    host.sym = symbol;
    g_host_for_modes = &host;

    native_key(0x12345678u, 0x9ABCDEF0u);
    preview = true;
    preview_place = 0x24681357u;
    expect_key(true, 0x12345678u, 0x9ABCDEF0u,
               "preview diagnostics read the genuine native key");
    for (int i = 0; i != 64; ++i) {
        native_key(uint32_t(i * 79), uint32_t(i * 31));
        expect_place(preview_place, "preview place is stable across different private native keys");
    }
    native_key(0, 0);
    expect_key(true, 0, 0, "a genuine zero native key is reported without substituting party identity");
    expect_place(preview_place, "preview place survives a genuine zero native key");
    mode_ptr = nullptr;
    expect_key(false, 0, 0, "missing native mode clears both diagnostic outputs during preview");
    expect_place(preview_place, "launched preview uses its provider identity without a native key");

    tested_modes::adventure_force_place(0x77777777u);
    expect_place(preview_place, "canonical preview identity takes precedence over debug forced place");
    preview_place = 0;
    expect_place(0, "held or stale preview stays absent even with a forced place");
    native_key(42, 17);
    expect_place(0, "held or stale preview does not fall back to a readable native key");

    preview = false;
    tested_modes::adventure_force_place(0);
    expect_key(true, 42, 17, "ordinary adventure retains its native key");
    expect_place((42u ^ (17u * 0x9E3779B1u)) | 1u, "ordinary adventure retains native key hashing");
    tested_modes::adventure_force_place(0x77777777u);
    expect_place(0x77777777u, "ordinary adventure retains the existing forced-place diagnostic");
    mode_ptr = nullptr;
    expect_key(false, 0, 0, "missing native mode clears both outputs");
    expect_place(0, "ordinary forced place still requires readable native identity");
    native_key(3, 5);
    symbol_present = false;
    expect_key(false, 0, 0, "missing symbol clears both outputs");
    expect_place(0, "missing native symbol has no ordinary place");
    symbol_present = true;
    skg::field<void*>(mode.data(), 0x74) = nullptr;
    expect_key(false, 0, 0, "missing native data clears both outputs");
    native_key(3, 5);
    skg::field<void*>(data.data(), 0x10) = nullptr;
    expect_key(false, 0, 0, "missing native resource clears both outputs");

    void* inaccessible = VirtualAlloc(nullptr, 0x1000, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS);
    check(inaccessible != nullptr, "allocate an actually unreadable native-context stand-in");
    if (inaccessible) {
        mode_ptr = inaccessible;
        expect_key(false, 0, 0, "unreadable native mode is refused without dereferencing fields");
        native_key(3, 5);
        skg::field<void*>(mode.data(), 0x74) = inaccessible;
        expect_key(false, 0, 0, "unreadable native data is refused");
        native_key(3, 5);
        skg::field<void*>(data.data(), 0x10) = inaccessible;
        expect_key(false, 0, 0, "unreadable native resource is refused");
        VirtualFree(inaccessible, 0, MEM_RELEASE);
    }
    native_key(0, 0);
    tested_modes::adventure_force_place(0);
    expect_key(true, 0, 0, "ordinary genuine zero native key remains a successful read");
    expect_place(1, "ordinary zero-key hash behavior is unchanged");
    std::printf("adventure_place_tests: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
