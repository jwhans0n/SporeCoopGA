#include "adventure_progress.h"

#include <cstring>

#include "sporekit_game.hpp"

namespace adventure_progress {
namespace {
static_assert(sizeof(void*) == 4, "Native captain progression requires the Win32 ABI.");
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
constexpr uint32_t kXP = 0xCF837237, kLevel = 0x5888EF41;
uintptr_t a_load, a_int32;
bool ready;

using Release = uint32_t(SK_THISCALL*)(void*);
struct ListRef {
    void* list = nullptr;
    Release release = nullptr;
    ~ListRef() { if (list && release) release(list); }
};

bool integer(void* list, uint32_t id, int32_t& value, bool& present, std::string& error)
{
    value = 0; present = false;
    void* property = nullptr;
    if (!skg::vcall<bool>(list, 0x24 / 4, id, &property)) return true;
    if (!property || !skg::readable(property, 0x14) || skg::field<uint16_t>(property, 0x12) != 9) {
        error = "The native captain progression property is not an Int32."; return false;
    }
    auto* pointer = skg::thiscall<int32_t*>(a_int32, property);
    if (!pointer || !skg::readable(pointer, sizeof(*pointer))) {
        error = "The native captain progression value is unavailable."; return false;
    }
    value = *pointer; present = true;
    return true;
}
} // namespace

bool init(const SkHost* host, SkMod*)
{
    ready = false; a_load = a_int32 = 0;
    if (!host || !host->sym || !host->exe_sha256) return false;
    const char* hash = host->exe_sha256();
    if (!hash || std::strcmp(hash, kHash) != 0) return false;
    a_load = host->sym("SporeCoop::Adventure::LoadAssetProperties");
    a_int32 = host->sym("App::Property::GetValueInt32");
    ready = a_load && a_int32;
    return ready;
}

bool available() { return ready; }

bool read(const dbpf::Key& captain, Snapshot& result, std::string& error)
{
    result = {}; error.clear();
    if (!ready) { error = "Native captain progression reading is unavailable for this executable."; return false; }
    if (!captain.instance || captain.type != 0x2B978C46 || !captain.group) {
        error = "An original local captain key is required."; return false;
    }
    ListRef reference;
    bool loaded = skg::cdecl_call<bool>(a_load, &captain, &reference.list);
    // The loader returns an intrusive reference. Release exactly once on every
    // readable return path, including failure with a partially supplied list.
    if (reference.list && skg::readable(reference.list, sizeof(void*))) {
        auto* table = skg::vtable(reference.list);
        if (table && skg::readable(table, 8)) reference.release = reinterpret_cast<Release>(table[1]);
    }
    if (!loaded || !reference.list || !reference.release || !skg::readable(reference.list, sizeof(void*))) {
        error = "The local captain property list could not be loaded."; return false;
    }
    auto* table = skg::vtable(reference.list);
    if (!skg::readable(table, 0x28) || !table[0x24 / 4]) {
        error = "The local captain property list has no native property getter."; return false;
    }
    Snapshot snapshot;
    snapshot.captain = captain;
    snapshot.properties = {captain.instance, 0x00B1B104, (captain.group & 0xFFFFE5FFu) | 0xE500u};
    if (!integer(reference.list, kXP, snapshot.xp, snapshot.xp_present, error) ||
        !integer(reference.list, kLevel, snapshot.level, snapshot.level_present, error)) return false;
    result = snapshot;
    return true;
}

} // namespace adventure_progress
