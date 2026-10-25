// Native act music dependency validation, independent of the game ABI.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/dbpf.h"

namespace adventure_music {

constexpr uint32_t kAudioGroup = 0x021407EE;
constexpr uint32_t kDescriptor = 0x02B9F662;
constexpr uint32_t kSample = 0x01A527DB;

struct Record {
    dbpf::Key key;
    bool built_in = false;
    std::string source;
    std::vector<uint8_t> data;
};

enum class Lookup { Missing, Found, Error };
// Only descriptor records need bytes. Sample validation checks effective provenance.
using Loader = std::function<Lookup(const dbpf::Key&, bool bytes, Record&, std::string&)>;

struct Result {
    std::vector<dbpf::Key> dependencies;
    bool native_control = false;
};

// IDs remain authored values. This verifies installed stock audio; custom audio is
// deliberately unsupported until its full native dependency graph can be transferred.
bool validate(uint32_t music, const Loader& load, Result& result, std::string& error);

} // namespace adventure_music
