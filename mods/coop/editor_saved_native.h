// Saved creation dependency persistence and selected-adventure cold admission.
#pragma once
#include "net/dbpf.h"
#include "sporekit.h"
#include <functional>

namespace editor_saved_native {
bool init(const SkHost*,SkMod*);
// Outside frame, before Kit removes this module's hooks. Cancels every owned
// Native pending load; leaving phase0 for Native's timeout is unsafe.
void shutdown();
// Called only after the ordinary Save/terrain/semantic proof. genuine_raw is
// the unchanged persisted record, before any normalization. Admission bytes
// belong to the current canonical draft, never a historical cache directory.
bool persist(dbpf::Key,const std::vector<uint8_t>& genuine_raw,const std::vector<dbpf::Record>& admitted,
             const std::function<bool()>& current,std::string& error);
std::string status();
}
