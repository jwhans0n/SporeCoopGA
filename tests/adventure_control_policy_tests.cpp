#include <cstdio>
#include "adventure_control.h"

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char* message) { if (!ok) { ++failures; std::printf("FAIL: %s\n", message); } };
    check(!ga::legacy_world_time(0x01654C10), "GA never applies or publishes the legacy planet-orientation packet");
    for (uint32_t mode : {0x01654C00u, 0x01654C02u, 0x01654C03u, 0x01654C04u, 0x01654C05u})
        check(ga::legacy_world_time(mode), "base-stage time synchronization remains available");
    ga::Captain captain; captain.life = ga::Life::Downed; captain.position = {10, 20, 30};
    net::PlayerState state; state.pos = {100, 200, 300}; state.vel = {4, 5, 6}; state.species = 123;
    state.flags = net::PF_IN_WORLD | net::PF_MOVING | net::PF_RUNNING | net::PF_JUMPING | net::PF_DEAD;
    check(ga::hold_downed_movement(captain, state) && state.pos.x == 10 && state.pos.y == 20 && state.pos.z == 30 &&
          state.vel.x == 0 && state.vel.y == 0 && state.vel.z == 0 && state.flags == net::PF_IN_WORLD && state.species == 123,
          "downed replication keeps the authoritative anchor, no movement flags, and the selected captain");
    captain.life = ga::Life::Alive; state.pos = {1, 2, 3}; state.vel = {4, 5, 6};
    check(!ga::hold_downed_movement(captain, state) && state.pos.x == 1 && state.vel.x == 4,
          "standing captains retain native movement");
    std::printf("adventure control policies: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
