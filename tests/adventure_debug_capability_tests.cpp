// Exercise the actual legacy command with absent native capabilities. No game
// process is opened and no unsupported native address is invoked.
#include <array>
#include <cstdarg>
#include <cstdio>
#include <string>
#include "../mods/coop/debug.cpp"

namespace {
int failures, avatar_calls, projections, managers, viewers, rays;
bool manager_symbol = true, ray_symbol = true, avatar_present = true;
bool manager_present = true, viewer_present = true, ray_result = true;
std::array<uintptr_t, 24> manager_vtable{};
uintptr_t manager_object;
int viewer_object;
std::string output;
void check(bool value, const char* message)
{
    if (!value) { ++failures; std::printf("FAIL: %s\n", message); }
}
void* SK_CDECL get_manager() { ++managers; return manager_present ? &manager_object : nullptr; }
void* SK_THISCALL get_viewer(void* self)
{
    ++viewers; check(self == &manager_object, "viewer getter receives native manager");
    return viewer_present ? &viewer_object : nullptr;
}
bool SK_THISCALL get_ray(void* self, float x, float y, skg::Vector3* origin, skg::Vector3* direction)
{
    ++rays; check(self == &viewer_object && x == 32.f && y == 64.f, "inverse receives exact viewer and projection");
    *origin = {0, 0, 0}; *direction = {1, 0, 0}; return ray_result;
}
uintptr_t symbol(const char* name)
{
    if (!std::strcmp(name, "App::IGameModeManager::Get")) return manager_symbol ? reinterpret_cast<uintptr_t>(&get_manager) : 0;
    if (!std::strcmp(name, "App::cViewer::GetCameraToPoint")) return ray_symbol ? reinterpret_cast<uintptr_t>(&get_ray) : 0;
    check(false, "no unexpected native capability is requested"); return 0;
}
int project(const float*, float* screen) { ++projections; screen[0] = 32; screen[1] = 64; return 1; }
void print(SkOut*, const char* format, ...)
{
    char text[512]; va_list args; va_start(args, format); std::vsnprintf(text, sizeof text, format, args); va_end(args); output += text;
}
}
namespace game {
void* avatar() { ++avatar_calls; return avatar_present ? reinterpret_cast<void*>(0x12345678) : nullptr; }
net::Vec3 position(void*) { return {1, 2, 3}; }
uint32_t mode_id() { check(false, "unrelated debug commands never execute"); return 0; }
float dna() { check(false, "unrelated DNA diagnostic never executes"); return 0; }
}
namespace world {
bool save_game(std::string&) { check(false, "camera diagnostic never saves a game"); return false; }
bool load_local(const std::string&, const std::string&, std::string&) { check(false, "camera diagnostic never loads a game"); return false; }
}
int main()
{
    SkHost host{}; host.sym = symbol; host.world_to_screen = project; host.out_printf = print; H = &host;
    manager_vtable[0x58 / 4] = reinterpret_cast<uintptr_t>(&get_viewer);
    manager_object = reinterpret_cast<uintptr_t>(manager_vtable.data());
    ray_symbol = false;
    check(c_w2s(nullptr, 0, nullptr, nullptr) == 1 && !avatar_calls && !projections && !managers && !viewers && !rays,
          "missing inverse capability refuses before every native/game call");
    ray_symbol = true; manager_symbol = false;
    check(c_w2s(nullptr, 0, nullptr, nullptr) == 1 && !avatar_calls && !projections && !rays,
          "missing mode manager capability also fails closed");
    manager_symbol = true; host.world_to_screen = nullptr;
    check(c_w2s(nullptr, 0, nullptr, nullptr) == 1 && !avatar_calls && !rays, "missing projection callback fails closed");
    host.world_to_screen = project; avatar_present = false;
    check(c_w2s(nullptr, 0, nullptr, nullptr) == 1 && !projections && !managers, "absent avatar enters no native camera helper");
    avatar_present = true; manager_present = false;
    check(c_w2s(nullptr, 0, nullptr, nullptr) == 1 && !viewers && !rays, "null manager never invokes a virtual getter");
    manager_present = true; viewer_present = false;
    check(c_w2s(nullptr, 0, nullptr, nullptr) == 1 && !rays, "null viewer never invokes the inverse");
    viewer_present = true; ray_result = false;
    check(c_w2s(nullptr, 0, nullptr, nullptr) == 1 && rays == 1, "failed native inverse returns a diagnostic error");
    ray_result = true;
    check(c_w2s(nullptr, 0, nullptr, nullptr) == 0 && rays == 2, "complete verified fake camera path still works");
    std::printf("adventure native debug capabilities: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
