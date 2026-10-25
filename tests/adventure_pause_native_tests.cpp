#include <array>
#include <cstdio>
#include <vector>

#include "../mods/coop/adventure_pause_native.cpp"

using namespace skg;
static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
static std::array<uint8_t, 0x80> storage;
static std::array<std::array<int, 2>, 5> reasons;
static std::array<std::array<uint8_t, 0x18>, 3> nodes;
static bool active, held, intent, nested;
static unsigned original_calls, reports;
static std::vector<std::pair<SkTaskFn, void*>> tasks;
static void queue(SkTaskFn f, void* p) { tasks.emplace_back(f, p); }
static void drain() { auto pending = std::move(tasks); tasks.clear(); for (auto [f, p] : pending) f(p); }
static void* SK_CDECL get_manager() { return storage.data(); }
static const char* hash() { return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37"; }
static void SK_THISCALL original(void* self)
{
    ++original_calls; field<uint32_t>(self, 0x48) = 0;
    auto* p = field<int*>(self, 0x68); auto* end = field<int*>(self, 0x6C);
    for (; p != end; p += 2) if (p[0] > 0) { field<uint32_t>(self, 0x48) = uint32_t(p[1]); break; }
    if (nested) { nested = false; ++reasons[3][0]; adventure_pause_native::recalculate(self); }
}
static uintptr_t sym(const char* name)
{
    if (!std::strcmp(name, "Simulator::cGameTimeManager::Get")) return reinterpret_cast<uintptr_t>(&get_manager);
    if (!std::strcmp(name, "SporeCoop::Adventure::RecalculatePauseFlags")) return reinterpret_cast<uintptr_t>(&original);
    return 0;
}
static int hook(SkMod*, void*, void*, void** result) { *result = reinterpret_cast<void*>(&original); return 0; }
static void setup()
{
    reasons = {{{0, 1}, {0, 7}, {0, 3}, {0, 3}, {0, 3}}};
    storage.fill(0); for (auto& node : nodes) node.fill(0);
    field<void*>(storage.data(), 0x68) = reasons.data();
    field<void*>(storage.data(), 0x6C) = reasons.data() + reasons.size();
    // Sorted native tree: UIToggle < UserToggle < ModalDialog. Native vector
    // indices intentionally differ from that order; no fixed index is assumed.
    field<uint32_t>(nodes[0].data(), 0x10) = 0x04BF38A8; field<uint32_t>(nodes[0].data(), 0x14) = 1;
    field<uint32_t>(nodes[1].data(), 0x10) = 0x04BF38A4; field<uint32_t>(nodes[1].data(), 0x14) = 3;
    field<uint32_t>(nodes[2].data(), 0x10) = 0x04BF38A9; field<uint32_t>(nodes[2].data(), 0x14) = 4;
    field<void*>(nodes[0].data(), 4) = nodes[1].data(); field<void*>(nodes[0].data(), 0) = nodes[2].data();
    field<void*>(storage.data(), 0x58) = nodes[0].data();
}
int main()
{
    setup(); SkHost api{}; api.sym = sym; api.exe_sha256 = hash; api.hook_create = hook; api.run_outside_frame = queue;
    adventure_pause_native::Ctx c; c.active = [] { return active; }; c.held = [] { return held; };
    c.local_intent = [](bool value) { intent = value; ++reports; };
    CHECK(adventure_pause_native::init(&api, nullptr, c));
    auto recalc = [] { adventure_pause_native::recalculate(storage.data()); };
    reasons[3][0] = 1; recalc(); CHECK(field<int>(storage.data(), 0x48) == 3 && reasons[3][0] == 1 && reports == 0);
    active = true; recalc(); CHECK(intent && field<int>(storage.data(), 0x48) == 0 && reasons[3][0] == 1);
    held = true; recalc(); CHECK(field<int>(storage.data(), 0x48) == 3 && reasons[3][0] == 1);
    held = false; reasons[2][0] = 1; recalc(); CHECK(field<int>(storage.data(), 0x48) == 3); // gameplay preserved
    reasons[0][0] = 1; recalc(); CHECK(field<int>(storage.data(), 0x48) == 1); // cinematic priority preserved
    reasons[0][0] = reasons[2][0] = 0;
    reasons[1][0] = 2; reasons[4][0] = -2; recalc();
    CHECK(field<int>(storage.data(), 0x48) == 0 && reasons[1][0] == 2 && reasons[4][0] == -2);
    CHECK(adventure_pause_native::local_intent());
    nested = true; recalc();
    CHECK(reasons[3][0] == 2 && reasons[1][0] == 2 && field<int>(storage.data(), 0x48) == 0); // nested increment retained and still filtered
    reasons[1][0] = reasons[3][0] = 0; recalc(); CHECK(!intent && !adventure_pause_native::local_intent());
    reasons[3][0] = 1;
    unsigned before = original_calls;
    adventure_pause_native::changed(); adventure_pause_native::changed(); CHECK(tasks.size() == 1);
    active = false; adventure_pause_native::reset(); CHECK(field<int>(storage.data(), 0x48) == 3);
    adventure_pause_native::changed(); CHECK(tasks.size() == 2); drain();
    CHECK(original_calls == before + 2); // reset + fresh task; cancelled task did nothing
    active = true;
    field<uint32_t>(nodes[0].data(), 0x14) = 0xFFFFFFFF; recalc();
    CHECK(field<int>(storage.data(), 0x48) == 3 && reasons[3][0] == 1); // unverified layout preserves native semantics
    setup(); reasons[4][0] = 1; recalc(); CHECK(intent && field<int>(storage.data(), 0x48) == 0 && reasons[4][0] == 1);
    std::printf("adventure native pause: %d failures\n", failures);
    return failures ? 1 : 0;
}
