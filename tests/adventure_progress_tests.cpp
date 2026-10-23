// Win32 ABI and ownership regression for read-only native captain XP diagnostics.
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "adventure_progress.h"
#include "sporekit_game.hpp"

namespace {
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
const char* hash = kHash;
int failures, loads, releases, references, getters, value_getters;
bool load_ok = true, return_list = true, have_xp = true, have_level = true, null_value;
dbpf::Key original{0x29055757, 0x2B978C46, 0x40626200};
std::array<uint8_t, 0x14> xp_property{}, level_property{};
int32_t external_xp = 100;
void* table[10]{};
void** list = table;
void* property_list = &list;
std::map<std::string, uintptr_t> symbols;

void check(bool value, const char* message)
{
    if (!value) { ++failures; std::printf("FAIL: %s\n", message); }
}
bool SK_CDECL load(const dbpf::Key* key, void** output)
{
    ++loads; check(key && *key == original, "loader receives the original local captain key");
    if (return_list) { *output = property_list; ++references; }
    return load_ok;
}
uint32_t SK_THISCALL release(void* self)
{
    check(self == property_list, "Release receives the property list this pointer");
    ++releases; --references; return static_cast<uint32_t>(references);
}
bool SK_THISCALL property(void* self, uint32_t id, void** output)
{
    ++getters; check(self == property_list, "GetProperty receives the property list this pointer");
    if (id == 0xCF837237 && have_xp) { *output = xp_property.data(); return true; }
    if (id == 0x5888EF41 && have_level) { *output = level_property.data(); return true; }
    check(id == 0xCF837237 || id == 0x5888EF41, "only native XP and level properties are read");
    return false;
}
int32_t* SK_THISCALL integer(void* self)
{
    ++value_getters;
    check(self == xp_property.data() || self == level_property.data(), "Int32 getter receives the selected native property");
    if (null_value) return nullptr;
    return skg::field<uint16_t>(self, 0x10) & 0x30 ? skg::field<int32_t*>(self, 0) : static_cast<int32_t*>(self);
}
uintptr_t symbol(const char* name) { auto found = symbols.find(name); return found == symbols.end() ? 0 : found->second; }
const char* exe_hash() { return hash; }
} // namespace

int main()
{
    table[1] = reinterpret_cast<void*>(&release);
    table[0x24 / 4] = reinterpret_cast<void*>(&property);
    skg::field<uint16_t>(xp_property.data(), 0x12) = 9;
    skg::field<int32_t>(xp_property.data(), 0) = 37;
    skg::field<uint16_t>(level_property.data(), 0x12) = 9;
    skg::field<int32_t>(level_property.data(), 0) = 2;
    symbols = {{"SporeCoop::Adventure::LoadAssetProperties", reinterpret_cast<uintptr_t>(&load)},
               {"App::Property::GetValueInt32", reinterpret_cast<uintptr_t>(&integer)}};
    SkHost host{}; host.sym = symbol; host.exe_sha256 = exe_hash;
    hash = "wrong";
    check(!adventure_progress::init(&host, nullptr), "unknown executable fails closed");
    hash = kHash;
    symbols.erase("App::Property::GetValueInt32");
    check(!adventure_progress::init(&host, nullptr), "missing native capability fails closed");
    symbols["App::Property::GetValueInt32"] = reinterpret_cast<uintptr_t>(&integer);
    check(adventure_progress::init(&host, nullptr) && adventure_progress::available(), "reviewed native capabilities resolve");
    adventure_progress::Snapshot snapshot;
    std::string error;
    auto read = [&] { return adventure_progress::read(original, snapshot, error); };
    check(read() && snapshot.xp == 37 && snapshot.level == 2 && snapshot.xp_present && snapshot.level_present,
          "native scalar XP and level read correctly");
    check(snapshot.captain == original && snapshot.properties == dbpf::Key{0x29055757, 0x00B1B104, 0x4062E500},
          "property resource group matches native transformation");
    check(references == 0 && releases == 1, "successful load releases exactly one intrusive reference");
    have_xp = have_level = false;
    check(read() && !snapshot.xp_present && !snapshot.level_present && snapshot.xp == 0,
          "missing properties preserve the native zero baseline");
    have_xp = have_level = true;
    skg::field<uint16_t>(xp_property.data(), 0x10) = 0x10;
    skg::field<int32_t*>(xp_property.data(), 0) = &external_xp;
    check(read() && snapshot.xp == 100, "pointer-backed native Int32 uses the native value getter");
    skg::field<uint16_t>(xp_property.data(), 0x12) = 0x0D;
    check(!read() && references == 0 && !error.empty() && !snapshot.xp_present,
          "wrong property type rejects and releases without stale output");
    skg::field<uint16_t>(xp_property.data(), 0x12) = 9;
    null_value = true;
    check(!read() && references == 0, "missing native value releases the property list");
    null_value = false;
    load_ok = false;
    check(!read() && references == 0, "failed load with partial reference still releases it");
    return_list = false; unsigned before = releases;
    check(!read() && releases == static_cast<int>(before), "failed load without a reference performs no Release");
    load_ok = return_list = true;
    table[0x24 / 4] = nullptr;
    check(!read() && references == 0, "unavailable native property method still releases the valid list");
    table[0x24 / 4] = reinterpret_cast<void*>(&property);
    auto bad = original; bad.type = 0;
    int before_load = loads;
    check(!adventure_progress::read(bad, snapshot, error) && loads == before_load, "invalid captain key never reaches native code");
    auto old_xp = xp_property; auto old_level = level_property;
    for (unsigned i = 0; i < 10000; ++i) check(read() && snapshot.xp == 100, "repeated native read remains stable");
    check(references == 0 && xp_property == old_xp && level_property == old_level && external_xp == 100,
          "repeated reads preserve stack balance, references and all progression data");
    check(getters > 0 && value_getters > 0, "verified virtual and native value paths were exercised");
    std::printf("adventure progression: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
