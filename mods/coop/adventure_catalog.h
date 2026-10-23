#pragma once

#include "sporekit.h"

namespace adventure_catalog {
// Read-only native resource inspection. Registers a command outside Present;
// private clones are released after each key and never replace the live scenario.
bool init(const SkHost* host, SkMod* mod);
} // namespace adventure_catalog
