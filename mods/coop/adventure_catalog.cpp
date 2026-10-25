#include "adventure_catalog.h"

#include <windows.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "net/dbpf.h"
#include "sporekit_game.hpp"

namespace adventure_catalog {
namespace {
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
const SkHost* H;
uintptr_t resource_manager;

bool vector(uint8_t* base, size_t offset, size_t stride, size_t maximum, uint8_t*& begin, size_t& count)
{
    uintptr_t first = skg::field<uintptr_t>(base, offset), last = skg::field<uintptr_t>(base, offset + 4);
    if (last < first || (last - first) % stride || (last - first) / stride > maximum ||
        (last != first && !skg::readable(reinterpret_cast<void*>(first), last - first))) return false;
    begin = reinterpret_cast<uint8_t*>(first); count = (last - first) / stride; return true;
}

std::string text(uint8_t* scenario_string)
{
    const wchar_t* value = skg::field<const wchar_t*>(scenario_string, 8);
    size_t length = 0;
    if (value) {
        while (length < 160 && skg::readable(value + length, 2) && value[length]) ++length;
    } else {
        uintptr_t first = skg::field<uintptr_t>(scenario_string, 0x14);
        uintptr_t last = skg::field<uintptr_t>(scenario_string, 0x18);
        if (last >= first && (last - first) % 2 == 0 && last - first <= 320 &&
            (last == first || skg::readable(reinterpret_cast<void*>(first), last - first))) {
            value = reinterpret_cast<const wchar_t*>(first); length = (last - first) / 2;
        }
    }
    if (!value || !length || !skg::readable(value, length * 2)) return {};
    std::string result(length * 4, '\0');
    int bytes = WideCharToMultiByte(CP_UTF8, 0, value, int(length), result.data(), int(result.size()), nullptr, nullptr);
    result.resize(bytes > 0 ? size_t(bytes) : 0);
    for (char& c : result) if (c == '\r' || c == '\n') c = ' ';
    return result;
}

void inspect(uint32_t instance, SkOut* output)
{
    dbpf::Key key{instance, 0x366A930D, 0x408A0000};
    void* manager = *reinterpret_cast<void**>(resource_manager);
    void* object = nullptr;
    bool loaded = manager && skg::vcall<bool>(manager, 0x1C / 4, &key, &object,
        static_cast<void*>(nullptr), static_cast<void*>(nullptr), static_cast<void*>(nullptr), static_cast<void*>(nullptr));
    struct Owned {
        void* object;
        ~Owned() { if (object) skg::vcall<int>(object, 0x04 / 4); }
    } owned{object};
    if (!loaded || !object) { H->out_printf(output, "%08X unavailable\n", instance); return; }
    auto* scenario = skg::vcall<uint8_t*>(object, 0x0C / 4, 0xE742574Au);
    uint8_t *acts = nullptr, *markers = nullptr;
    size_t act_count = 0, marker_count = 0;
    if (!scenario || !skg::readable(scenario, 0x2CB0) ||
        !vector(scenario, 0x23C, 0x534, 8, acts, act_count) ||
        !vector(scenario, 0x2C10, 0x238, 8192, markers, marker_count)) {
        H->out_printf(output, "%08X invalid native scenario arrays\n", instance); return;
    }
    H->out_printf(output, "%08X acts=%u posse=%d locked=%d intro=\"%s\"\n", instance,
        unsigned(act_count), skg::field<int>(scenario, 0x238), int(skg::field<bool>(scenario, 0x175)), text(scenario + 0x7C).c_str());
    for (size_t act = 0; act < act_count; ++act) {
        auto* row = acts + act * 0x534;
        uint8_t* goals = nullptr; size_t goal_count = 0;
        if (!vector(row, 0x84, 0x188, 3, goals, goal_count)) {
            H->out_printf(output, " act%u invalid goal array\n", unsigned(act)); continue;
        }
        H->out_printf(output, " act%u \"%s\"", unsigned(act), text(row).c_str());
        for (size_t goal = 0; goal < goal_count; ++goal) {
            auto* item = goals + goal * 0x188;
            int kind = skg::field<int>(item, 0), cls = skg::field<int>(item, 4), required = skg::field<int>(item, 12);
            H->out_printf(output, " goal%u:type%d,class%d,count%d,markers[", unsigned(goal), kind, cls, required);
            size_t shown = 0;
            for (size_t marker = 0; marker < marker_count && shown < 8; ++marker) {
                auto* placement = markers + marker * 0x238;
                if (skg::field<int>(placement, 4) == cls) {
                    H->out_printf(output, "%s%u", shown++ ? "," : "", skg::field<uint32_t>(placement, 0x22C));
                }
            }
            H->out_printf(output, "]");
        }
        H->out_printf(output, "\n");
    }
}

int command(void*, int argc, const char* const* argv, SkOut* output)
{
    if (argc < 2 || argc > 17) {
        H->out_printf(output, "coop.adventure.catalog <stock adventure instance hex> [up to 16 keys]\n"); return 1;
    }
    uint32_t keys[16]{};
    for (int i = 1; i < argc; ++i) {
        char* end = nullptr; errno = 0;
        unsigned long value = std::strtoul(argv[i], &end, 16);
        if (errno || !argv[i][0] || !end || *end || !value || std::strlen(argv[i]) > 10) {
            H->out_printf(output, "Invalid stock adventure instance.\n"); return 1;
        }
        keys[i - 1] = uint32_t(value);
    }
    for (int i = 1; i < argc; ++i) inspect(keys[i - 1], output);
    return 0;
}
} // namespace

bool init(const SkHost* host, SkMod* mod)
{
    if (!host || !host->exe_sha256 || !host->sym || !host->command_add || !host->out_printf) return false;
    const char* hash = host->exe_sha256();
    if (!hash || std::strcmp(hash, kHash)) return false;
    resource_manager = host->sym("Resource::ResourceManager_ptr");
    if (!resource_manager) return false;
    H = host;
    return H->command_add(mod, "coop.adventure.catalog", "Read-only stock adventure goals and captain restrictions",
        command, nullptr, SK_CMD_OUTSIDE_FRAME) == 0;
}
} // namespace adventure_catalog
