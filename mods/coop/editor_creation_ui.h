// Native cast-card Edit action and informational ownership decoration.
#pragma once
#include "sporekit.h"
#include <string>
namespace editor_creation_ui {
void init(const SkHost*);
void tick(); // Queues all window work outside Present.
void shutdown();
std::string diagnostic(); // Cached display state; never captures native data.
}
