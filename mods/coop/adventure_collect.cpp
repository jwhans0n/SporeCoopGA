#include "adventure_collect.h"
#include "adventure_music.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <tuple>

#include "net/adventure_assets.h"
#include "sporekit_game.hpp"

namespace adventure_collect {

using Key = dbpf::Key;
namespace content = net::adventure_content;
static const SkHost* H;
static SkMod* M;
static uintptr_t a_Rm, a_Resolve, a_Write, a_Read, a_New, a_ScenarioCtor;
static uintptr_t a_Name, a_Author, a_Properties, a_String16;
static bool g_supported;
constexpr uint32_t kAdventure = 0x366A930D, kBem = 0x1A99B06B, kPng = 0x2F7D0004;
constexpr uint32_t kMeta = 0x030BDEE3, kSummary = 0x02D5C9AF, kProp = 0x00B1B104;
constexpr uint32_t kScenarioClass = 0xE742574A;

static auto key_order(const Key& k) { return std::tuple{k.group, k.type, k.instance}; }
struct KeyLess { bool operator()(const Key& a, const Key& b) const { return key_order(a) < key_order(b); } };
static void* manager() { return a_Rm ? *(void**)a_Rm : nullptr; }

static std::string key_text(const Key& k)
{
    char b[48];
    snprintf(b, sizeof b, "%08X!%08X.%08X", k.group, k.instance, k.type);
    return b;
}

// Verified dc04 native IRecord and IStream slots. The database stream decompresses DBPF and
// DBBF records, so collection never parses or writes the user's Pollination database.
static void* find_record(const Key& key, Key& real)
{
    void* rm = manager();
    real = {};
    return rm ? skg::vcall<void*>(rm, 0x30 / 4, &key, &real, (void*)nullptr) : nullptr;
}
bool unused_snapshot_key(const Key& key)
{
    if(!g_supported || !manager() || !key.instance || key.type!=kAdventure)return false;
    Key real;
    for(uint32_t type:{kAdventure,kMeta,kSummary,kPng,0xFD60F0C1u})
        if(find_record({key.instance,type,key.group},real))return false;
    return !find_record({key.instance,0x05528D58,0x40A00200},real) &&
        !find_record({key.instance,kProp,0x408AE500},real);
}

static bool vanilla(void* database)
{
    const wchar_t* location = database ? skg::vcall<const wchar_t*>(database, 0x28 / 4) : nullptr;
    if (!location) return false;
    std::wstring path;
    for (size_t i = 0; i < 1000; ++i) {
        if (!skg::readable(location + i, 2)) return false;
        if (!location[i]) break;
        wchar_t c = location[i];
        path += c >= L'A' && c <= L'Z' ? c + (L'a' - L'A') : c;
    }
    auto slash = path.find_last_of(L"\\/");
    std::wstring filename = path.substr(slash == std::wstring::npos ? 0 : slash + 1);
    static const std::set<std::wstring> names = {
        L"spore_game.package", L"spore_content.package", L"spore_graphics.package", L"spore_audio1.package",
        L"spore_audio2.package", L"patchdata.package", L"spore_pack_03.package", L"spore_ep1_data.package",
        L"spore_ep1_content_01.package", L"spore_ep1_content_02.package", L"spore_ep1_locale_01.package",
        L"spore_ep1_locale_02.package", L"boosterpack_01.package"
    };
    return names.count(filename) != 0;
}

static bool extract(const Key& key, dbpf::Record& out, bool& built_in, std::string& error, bool include_vanilla = false)
{
    Key real;
    void* db = find_record(key, real);
    built_in = vanilla(db);
    if (!db) return false;
    if (built_in && !include_vanilla) return true;
    void* record = nullptr;
    if (!skg::vcall<bool>(db, 0x34 / 4, &real, &record, 1, 6, true, (void*)nullptr) || !record) {
        error = "cannot open adventure resource " + key_text(key);
        return false;
    }
    void* stream = skg::vcall<void*>(record, 0x18 / 4);
    bool ok = false;
    if (stream) {
        uint32_t size = skg::vcall<uint32_t>(stream, 0x1C / 4);
        if (size <= content::kMaxRecordBytes && skg::vcall<bool>(stream, 0x28 / 4, 0, 0)) {
            out.key = real;
            out.data.resize(size);
            size_t offset = 0;
            while (offset < size) {
                int read = skg::vcall<int>(stream, 0x30 / 4, out.data.data() + offset, size - (uint32_t)offset);
                if (read <= 0 || (uint32_t)read > size - offset) break;
                offset += (uint32_t)read;
            }
            ok = offset == size;
        }
    }
    skg::vcall<bool>(record, 0x24 / 4); // RecordClose
    skg::vcall<int>(record, 0x08 / 4); // IRecord's Release (its destructor precedes AddRef)
    if (!ok) error = "cannot read adventure resource or record exceeds 16 MiB: " + key_text(key);
    return ok;
}

static adventure_music::Lookup load_music(const Key& key, bool bytes,
                                        adventure_music::Record& out, std::string& error)
{
    Key real;
    void* db = find_record(key, real);
    if (!db) return adventure_music::Lookup::Missing;
    out.key = real;
    out.built_in = vanilla(db);
    if (!out.built_in) {
        const wchar_t* location = skg::vcall<const wchar_t*>(db, 0x28 / 4);
        if (location) {
            size_t length = 0;
            while (length < 1000 && skg::readable(location + length, 2) && location[length]) ++length;
            out.source.resize(length * 4);
            int size = WideCharToMultiByte(CP_UTF8, 0, location, int(length), out.source.data(),
                                          int(out.source.size()), nullptr, nullptr);
            out.source.resize(size > 0 ? size_t(size) : 0);
        }
        return adventure_music::Lookup::Found;
    }
    if (bytes) {
        dbpf::Record record;
        bool built_in = false;
        if (!extract(real, record, built_in, error, true)) {
            if (error.empty()) error = "cannot read native audio descriptor " + key_text(real);
            return adventure_music::Lookup::Error;
        }
        out.data = std::move(record.data);
    }
    return adventure_music::Lookup::Found;
}

// An owned stream exposes the independently verified native IO::IStream ABI. Native scenario
// serialization writes into this buffer and cannot change a profile or the loaded scenario.
struct WriteStream {
    void** vtable;
    std::vector<uint8_t> bytes;
    uint32_t position = 0;
    int refs = 1;
    bool failed = false;
    bool read_only = false;
};

static void* SK_THISCALL stream_destroy(WriteStream* s, int) { return s; }
static int SK_THISCALL stream_addref(WriteStream* s) { return ++s->refs; }
static int SK_THISCALL stream_release(WriteStream* s) { return --s->refs; }
static uint32_t SK_THISCALL stream_type(WriteStream*) { return 0; }
static int SK_THISCALL stream_access(WriteStream* s) { return s->read_only ? 1 : 3; }
static int SK_THISCALL stream_state(WriteStream* s) { return s->failed ? -1 : 0; }
static bool SK_THISCALL stream_close(WriteStream* s) { return !s->failed; }
static uint32_t SK_THISCALL stream_size(WriteStream* s) { return (uint32_t)s->bytes.size(); }
static bool SK_THISCALL stream_resize(WriteStream* s, uint32_t size)
{
    if (s->read_only || size > content::kMaxRecordBytes) { s->failed = true; return false; }
    s->bytes.resize(size);
    s->position = std::min(s->position, size);
    return true;
}
static int SK_THISCALL stream_position(WriteStream* s, int origin)
{
    return origin == 0 ? (int)s->position : origin == 1 ? 0 : (int)s->position - (int)s->bytes.size();
}
static bool SK_THISCALL stream_seek(WriteStream* s, int offset, int origin)
{
    int64_t base = origin == 0 ? 0 : origin == 1 ? s->position : origin == 2 ? s->bytes.size() : -1;
    int64_t position = base + offset;
    if (base < 0 || position < 0 || position > content::kMaxRecordBytes ||
        (s->read_only && uint64_t(position) > s->bytes.size())) { s->failed = true; return false; }
    if ((uint32_t)position > s->bytes.size()) s->bytes.resize((uint32_t)position);
    s->position = (uint32_t)position;
    return true;
}
static int SK_THISCALL stream_available(WriteStream* s) { return (int)s->bytes.size() - (int)s->position; }
static int SK_THISCALL stream_read(WriteStream* s, void* data, uint32_t size)
{
    const uint32_t requested = size;
    size = std::min<uint32_t>(size, (uint32_t)s->bytes.size() - s->position);
    if ((size && !data) || (s->read_only && requested != size)) { s->failed = true; return 0; }
    if (size) memcpy(data, s->bytes.data() + s->position, size);
    s->position += size;
    return (int)size;
}
static bool SK_THISCALL stream_flush(WriteStream* s) { return !s->failed; }
static bool SK_THISCALL stream_write(WriteStream* s, const void* data, uint32_t size)
{
    if (s->read_only || size > content::kMaxRecordBytes - s->position || (size && !data)) { s->failed = true; return false; }
    uint32_t end = s->position + size;
    if (end > s->bytes.size()) s->bytes.resize(end);
    if (size) memcpy(s->bytes.data() + s->position, data, size);
    s->position = end;
    return true;
}
static void* stream_vtable[] = {
    (void*)stream_destroy, (void*)stream_addref, (void*)stream_release, (void*)stream_type,
    (void*)stream_access, (void*)stream_state, (void*)stream_close, (void*)stream_size,
    (void*)stream_resize, (void*)stream_position, (void*)stream_seek, (void*)stream_available,
    (void*)stream_read, (void*)stream_flush, (void*)stream_write
};

struct Collector {
    std::map<Key, dbpf::Record, KeyLess> found;
    std::set<Key, KeyLess> assets;
    uint32_t bytes = 0;
    bool changed = false;
    std::string& error;

    bool put(dbpf::Record record)
    {
        auto previous = found.find(record.key);
        const size_t old = previous == found.end() ? 0 : previous->second.data.size();
        if ((previous == found.end() && found.size() >= content::kMaxRecords) ||
            record.data.size() > content::kMaxRecordBytes || record.data.size() > content::kMaxBytes - (bytes - old)) {
            error = "adventure dependency bundle exceeds its resource or byte limit"; return false;
        }
        bytes = uint32_t(bytes - old + record.data.size());
        found[record.key] = std::move(record);
        return true;
    }

    bool add(const Key& key, bool required = false, bool include_vanilla = false)
    {
        dbpf::Record record;
        bool base = false;
        std::string why;
        bool ok = extract(key, record, base, why, include_vanilla);
        if (!ok) {
            if (!why.empty() || required) { error = why.empty() ? "missing adventure resource " + key_text(key) : why; return false; }
            return true;
        }
        if (base && !include_vanilla) return true;
        if (found.count(record.key)) return true;
        return put(std::move(record));
    }

    bool creation(const Key& key)
    {
        if (!key.instance || !key.type || !assets.insert(key).second) return true;
        Key real;
        void* db = find_record(key, real);
        if (!db) { error = "missing adventure dependency " + key_text(key); return false; }
        if (vanilla(db)) return true;
        // These creation formats can regenerate their baked graphics locally. Unknown custom
        // gameplay props may reference arbitrary scripts/effects and require further tracing.
        switch (key.type) {
        case 0x2B978C46: case 0x2399BE55: case 0x24682294: case 0x476A98C7: case 0x438F6347: case 0x3D97A8E4: break;
        default: error = "custom adventure dependency type is not supported yet: " + key_text(key); return false;
        }
        if (!add(key, true)) return false;
        for (uint32_t type : {kBem, key.type, kMeta, kSummary, kPng, 0x0F43029Au})
            if (!add({key.instance, type, key.group})) return false;
        if (!add({key.instance, kPng, key.group + 1})) return false;
        return add({key.instance, kProp, (key.group & 0xFFFF0000u) | 0xE500u});
    }

    bool asset(uint8_t* ref)
    {
        if (!skg::readable(ref, 0x20)) { error = "unreadable adventure dependency reference"; return false; }
        Key key;
        memcpy(&key, ref, sizeof key);
        uint64_t server;
        memcpy(&server, ref + 0x10, 8);
        if (server && server != UINT64_MAX) {
            Key resolved;
            if (!a_Resolve || !skg::cdecl_call<bool>(a_Resolve, server, &resolved) || !resolved.instance) {
                error = "adventure dependency is not installed locally (server id " + std::to_string(server) + ")";
                return false;
            }
            key = resolved;
            memcpy(ref, &key, sizeof key);
            server = UINT64_MAX;
            memcpy(ref + 0x10, &server, 8);
            changed = true;
        }
        return creation(key);
    }

    bool vector(uint8_t* base, size_t offset, size_t stride, size_t limit, const std::function<bool(uint8_t*)>& visit)
    {
        uint32_t start = skg::field<uint32_t>(base, offset), end = skg::field<uint32_t>(base, offset + 4);
        if (end < start || (end - start) % stride || (end - start) / stride > limit ||
            (end > start && !skg::readable((void*)(uintptr_t)start, end - start))) {
            error = "invalid native adventure dependency array"; return false;
        }
        for (uintptr_t p = start; p < end; p += stride) if (!visit((uint8_t*)p)) return false;
        return true;
    }
};

static bool collect_resource(const Key& key, uint8_t* scenario, Collector& collector,
                             bool include_root, bool serialize_snapshot)
{
    auto& error = collector.error;
    if (!scenario || !skg::readable(scenario, 0x2CB0)) {
        error = "the selected resource did not produce a scenario"; return false;
    }
    std::set<uint32_t> validated_music;
    // Native schema initialization confirms 130 avatar,178 posse,2BF4 classes,23C acts.
    bool ok = (!include_root || collector.add(key, true, true)) && collector.asset(scenario + 0x130) &&
        collector.vector(scenario, 0x178, 0x38, 32, [&](uint8_t* p) { return collector.asset(p); }) &&
        collector.vector(scenario, 0x2BF4, 0x27E8, 1024, [&](uint8_t* p) {
            return collector.asset(p + 8) && collector.asset(p + 8 + 0x28) && collector.asset(p + 8 + 0x50);
        }) && collector.vector(scenario, 0x23C, 0x534, 8, [&, act = size_t(0)](uint8_t* p) mutable {
            const size_t index = act++;
            const uint32_t music = skg::field<uint32_t>(p, 0x80);
            if (validated_music.count(music)) return true;
            adventure_music::Result resolution;
            std::string why;
            if (!adventure_music::validate(music, load_music, resolution, why)) {
                char context[100];
                snprintf(context, sizeof context, "Adventure %08X act %u music %08X: ", key.instance, unsigned(index + 1), music);
                error = std::string(context) + why; return false;
            }
            validated_music.insert(music);
            if (music && music != UINT32_MAX)
                H->log(M, SK_INFO, "adventure music: scenario=%08X act=%u authored=%08X native=%s dependencies=%u",
                       key.instance, unsigned(index + 1), music,
                       resolution.native_control ? "control" : "descriptor", unsigned(resolution.dependencies.size()));
            return true;
        });
    if (ok) {
        // Source companions are optional for a draft with a newly assigned key.
        for (uint32_t type : {kMeta, kSummary, kPng, 0xFD60F0C1u}) if (!collector.add({key.instance, type, key.group})) ok = false;
        if (!collector.add({key.instance, 0x05528D58, 0x40A00200})) ok = false;
        if (!collector.add({key.instance, kProp, 0x408AE500})) ok = false;
    }
    if (ok && (serialize_snapshot || collector.changed)) {
        if (!a_Write) { error = "native scenario serialization is unavailable for the private snapshot"; return false; }
        WriteStream stream{stream_vtable, {}, 0, 1, false};
        ok = skg::thiscall<bool>(a_Write, scenario, &stream) && !stream.failed && !stream.bytes.empty();
        if (ok) ok = collector.put({key, std::move(stream.bytes)});
        else error = "cannot serialize the normalized private adventure copy";
    }
    return ok;
}

static void publish_records(Collector& collector, std::vector<dbpf::Record>& records)
{
    for (auto& [unused, record] : collector.found) records.push_back(std::move(record));
    H->log(M, SK_INFO, "adventure collector: %u records, %u dependencies, normalized_server_refs=%d",
           unsigned(records.size()), unsigned(collector.assets.size()), collector.changed ? 1 : 0);
}

bool init(const SkHost* h, SkMod* m)
{
    H = h; M = m;
    a_Rm = H->sym("Resource::ResourceManager_ptr");
    a_Resolve = H->sym("Simulator::Scenario::ResolveLocalServerAsset");
    a_Write = H->sym("Simulator::cScenarioResource::Write");
    a_Read = H->sym("Simulator::cScenarioResource::Read");
    a_New = H->sym("SporeCoop::Editor::Allocate");
    a_ScenarioCtor = H->sym("SporeCoop::Editor::ScenarioResourceCtor");
    a_Name = H->sym("Pollinator::cAssetMetadata::NameText");
    a_Author = H->sym("Pollinator::cAssetMetadata::AuthorText");
    a_Properties = H->sym("SporeCoop::Adventure::LoadAssetProperties");
    a_String16 = H->sym("App::Property::GetValueString16");
    // Field offsets and native vtable slots below have been inspected in this exact image.
    g_supported = std::string(H->exe_sha256()) == "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37" && a_Rm;
    return g_supported;
}

bool available() { return g_supported; }

Presentation presentation(const Key& key)
{
    Presentation result;
    if (!g_supported || !manager() || !key.instance) return result;
    Key real;
    for (uint32_t group : {key.group, key.group + 1}) {
        Key png{key.instance, kPng, group};
        if (find_record(png, real)) { result.image = real; break; }
    }
    if (!a_Name || !a_Author) return result;
    Key meta{key.instance, kMeta, key.group};
    void* object = nullptr;
    if (!skg::vcall<bool>(manager(), 0x1C / 4, &meta, &object, (void*)nullptr, (void*)nullptr, (void*)nullptr, (void*)nullptr) || !object) return result;
    void* metadata = skg::vcall<void*>(object, 0x0C / 4, kMeta);
    auto text = [](const wchar_t* value) {
        size_t length = 0;
        while (value && length < 256 && skg::readable(value + length, 2) && value[length]) ++length;
        if (!length || !skg::readable(value, length * 2)) return std::string();
        std::string s(length * 4, '\0');
        int bytes = WideCharToMultiByte(CP_UTF8, 0, value, int(length), s.data(), int(s.size()), nullptr, nullptr);
        s.resize(bytes > 0 ? size_t(bytes) : 0);
        for (char& c : s) if (c == '\r' || c == '\n') c = ' ';
        return s;
    };
    if (metadata && skg::readable(metadata, 0x88)) {
        result.name = text(skg::thiscall<const wchar_t*>(a_Name, metadata));
        result.author = text(skg::thiscall<const wchar_t*>(a_Author, metadata));
    }
    skg::vcall<int>(object, 0x04 / 4);
    // GA names live in captain properties, independently of the source body's
    // pollen metadata. Native quickplay writes String16 property75ED3A1C.
    if (key.type == 0x2B978C46 && a_Properties && a_String16) {
        void* properties = nullptr;
        bool loaded = skg::cdecl_call<bool>(a_Properties,&key,&properties);
        if (properties) {
            void* property = nullptr;
            if (loaded && skg::vcall<bool>(properties,0x24/4,uint32_t(0x75ED3A1C),&property) && property &&
                skg::readable(property,0x14) && skg::field<uint16_t>(property,0x12)==0x13) {
                void* string = skg::thiscall<void*>(a_String16,property);
                std::string name = string && skg::readable(string,4) ? text(*static_cast<const wchar_t**>(string)) : std::string();
                if (!name.empty()) result.name = name;
            }
            skg::vcall<int>(properties,0x04/4);
        }
    }
    return result;
}

bool collect_creation(const Key& key, std::vector<dbpf::Record>& records, std::string& error)
{
    records.clear(); error.clear();
    if (!g_supported || !manager()) { error = "creation collection is unavailable for this game build"; return false; }
    Collector collector{{}, {}, 0, false, error};
    if (!collector.creation(key)) return false;
    for (auto& [unused, record] : collector.found) records.push_back(std::move(record));
    return true;
}

bool inspect(const Key& key, Key& locked_captain, std::string& error)
{
    locked_captain = {};
    error.clear();
    if (!g_supported || !manager()) { error = "adventure inspection is unavailable for this game build"; return false; }
    if (!key.instance || key.type != kAdventure) { error = "the selected resource is not an adventure"; return false; }
    void* object = nullptr;
    if (!skg::vcall<bool>(manager(), 0x1C / 4, &key, &object, (void*)nullptr, (void*)nullptr, (void*)nullptr, (void*)nullptr) || !object) {
        error = "cannot load a private copy of the selected adventure"; return false;
    }
    uint8_t* scenario = (uint8_t*)skg::vcall<void*>(object, 0x0C / 4, kScenarioClass);
    bool ok = scenario && skg::readable(scenario, 0x176);
    if (!ok) error = "the selected resource did not produce a scenario";
    else if (scenario[0x175]) {
        memcpy(&locked_captain, scenario + 0x130, sizeof locked_captain);
        uint64_t server;
        memcpy(&server, scenario + 0x140, sizeof server);
        if (server && server != UINT64_MAX)
            ok = a_Resolve && skg::cdecl_call<bool>(a_Resolve, server, &locked_captain);
        ok = ok && locked_captain.instance && locked_captain.type;
        if (!ok) error = "the adventure's locked captain is not installed locally";
    }
    skg::vcall<int>(object, 0x04 / 4);
    return ok;
}

bool collect(const Key& key, std::vector<dbpf::Record>& records, std::string& error)
{
    records.clear();
    error.clear();
    if (!g_supported || !manager()) { error = "adventure dependency collection is unavailable for this game build"; return false; }
    if (!key.instance || key.type != kAdventure) { error = "the selected resource is not an adventure"; return false; }
    void* object = nullptr;
    // PrivateResource uses the same verified six-argument wrapper as GetResource, with the
    // cache flag cleared. All reference normalization therefore changes only this owned copy.
    if (!skg::vcall<bool>(manager(), 0x1C / 4, &key, &object, (void*)nullptr, (void*)nullptr, (void*)nullptr, (void*)nullptr) || !object) {
        error = "cannot load a private copy of the selected adventure"; return false;
    }
    uint8_t* scenario = (uint8_t*)skg::vcall<void*>(object, 0x0C / 4, kScenarioClass);
    Collector collector{{}, {}, 0, false, error};
    const bool ok = collect_resource(key, scenario, collector, true, false);
    skg::vcall<int>(object, 0x04 / 4); // the private resource's owned reference
    if (!ok) return false;
    publish_records(collector, records);
    return true;
}

bool collect_snapshot(const Key& key, const std::vector<uint8_t>& native_bytes,
                      std::vector<dbpf::Record>& records, std::string& error)
{
    records.clear(); error.clear();
    if (!g_supported || !manager() || !a_Read || !a_Write || !a_New || !a_ScenarioCtor) {
        error = "native adventure snapshot collection is unavailable for this game build"; return false;
    }
    if (!key.instance || key.type != kAdventure) {
        error = "the shared editor snapshot requires an adventure resource key"; return false;
    }
    if (native_bytes.empty() || native_bytes.size() > content::kMaxRecordBytes) {
        error = "the native adventure snapshot is empty or exceeds the resource size limit"; return false;
    }
    // Start empty so a native reader cannot retain fields from an older saved
    // source. Companion records still resolve through the supplied root key.
    void* allocation = skg::cdecl_call<void*>(a_New, uint32_t(0x2CB0), "CoopAdventureSnapshot", 0, 0, 0, 0);
    if (!allocation) { error = "cannot allocate a private adventure snapshot"; return false; }
    void* object = skg::thiscall<void*>(a_ScenarioCtor, allocation);
    // The verified constructor returns its allocation and starts at refcount0.
    skg::vcall<int>(object, 0x00 / 4);
    auto* scenario = static_cast<uint8_t*>(object);
    auto release = [](void* resource) { if (resource) skg::vcall<int>(resource, 0x04 / 4); };
    std::unique_ptr<void, decltype(release)> owned(object, release);
    WriteStream stream{stream_vtable, native_bytes, 0, 1, false, true};
    bool ok = skg::readable(scenario, 0x2CB0) &&
              skg::thiscall<bool>(a_Read, scenario, &stream) && !stream.failed;
    Collector collector{{}, {}, 0, false, error};
    if (ok) ok = collect_resource(key, scenario, collector, false, true);
    else error = "cannot read the owned native adventure snapshot";
    if (!ok) return false;
    publish_records(collector, records);
    return true;
}

} // namespace adventure_collect
