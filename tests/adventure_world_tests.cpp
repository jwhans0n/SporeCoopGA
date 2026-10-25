#include "net/adventure_health.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>

static bool close(float a, float b) { return std::fabs(a - b) < 0.001f; }

int main()
{
    // Reproduces creature +2C=1.0 with native maximum=100: a wounded creature
    // must retain its HP fraction, not become fully healed or receive 1 HP.
    ga::HealthScale creature;
    float tuning = 1.f, health = 40.f;
    auto maximum = [&] { return 100.f * tuning; };
    assert(creature.apply(0, tuning, health, 2.5f, maximum));
    assert(close(tuning, 2.5f) && close(maximum(), 250.f) && close(health, 100.f));
    health = 60.f;
    assert(!creature.apply(0, tuning, health, 2.5f, maximum));
    assert(close(health, 60.f));
    assert(creature.apply(1, tuning, health, 2.5f, maximum));
    assert(close(tuning, 2.5f) && close(health, 60.f)); // no act-to-act compounding

    // Native act reload changed authored tuning and HP before scaling.
    tuning = 2.f; health = 50.f;
    assert(creature.apply(2, tuning, health, 2.5f, maximum));
    assert(close(tuning, 5.f) && close(health, 125.f));
    assert(creature.apply(2, tuning, health, 1.f, maximum));
    assert(close(tuning, 2.f) && close(health, 50.f)); // befriended NPC loses enemy-only tuning
    assert(creature.apply(2, tuning, health, 2.5f, maximum));
    assert(close(tuning, 5.f) && close(health, 125.f));

    // Generic building combatants use an absolute raw value and the same rule.
    ga::HealthScale building;
    float building_tuning = 300.f, building_health = 150.f;
    assert(building.apply(0, building_tuning, building_health, 3.f, [&] { return building_tuning; }));
    assert(close(building_tuning, 900.f) && close(building_health, 450.f));

    // A failed native maximum query must restore tuning without changing HP.
    ga::HealthScale invalid;
    float invalid_tuning = 1.f, invalid_health = 30.f;
    assert(!invalid.apply(0, invalid_tuning, invalid_health, 2.f, [&] {
        return invalid_tuning == 1.f ? 100.f : std::numeric_limits<float>::infinity();
    }));
    assert(close(invalid_tuning, 1.f) && close(invalid_health, 30.f) && invalid.act == -1);
    assert(!invalid.apply(0, invalid_tuning, invalid_health, 0.f, [&] { return 100.f; }));

    std::puts("adventure_world_tests: passed");
}
