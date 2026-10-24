// Native GA captain selection and owner-scoped model sharing.
#pragma once

#include <functional>
#include <string>

#include "net/dbpf.h"
#include "net/session.h"
#include "sporekit.h"

namespace adventure_captains {

using Key = dbpf::Key;
using Selected = std::function<void(const Key&)>;
bool init(const SkHost* host, SkMod* mod, net::Session* session);
bool available();
// Locked adventures use the authored captain immediately. Personal selection uses the
// native captain wizard and calls back only after saving and all model acknowledgements.
bool choose(const Key& locked, Selected selected);
// Publish a captain already selected by the host's normal Sporepedia Play workflow.
bool publish(const Key& captain, Selected selected);
// Called only at the gated native loading request, after the wizard saved its captain.
bool native_pending();
bool complete_native(const Key& adventure, const Key& captain);
void restore_galaxy(); // native view reset, outside-frame only
void close_picker(); // outside-frame cleanup; preserves saved creations
void tick(double now);
void on_message(uint8_t from, net::Reader& payload); // owns subkind 192..223
void reset();
std::string status();
// Original native key for the local captain (keeps local progression/rewards intact),
// owner-scoped resource key for another player's model on this machine.
Key key_for(uint8_t player);

} // namespace adventure_captains
