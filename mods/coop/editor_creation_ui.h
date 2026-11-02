// Native compact creation menu beside the Adventure Editor cast browser.
#pragma once
#include "sporekit.h"
namespace editor_creation_ui {
void init(const SkHost*);
void tick(); // Queues all window work outside Present.
void shutdown();
}
