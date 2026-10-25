#include "adventure_music.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <tuple>

using namespace adventure_music;
static unsigned failures;
#define CHECK(value) do { if (!(value)) { std::printf("FAIL %d: %s\n", __LINE__, #value); ++failures; } } while (0)
using Identity = std::tuple<uint32_t, uint32_t, uint32_t>;
static Identity identity(const dbpf::Key& key) { return {key.group, key.type, key.instance}; }
static dbpf::Key descriptor(uint32_t id) { return {id, kDescriptor, kAudioGroup}; }
static dbpf::Key sample(uint32_t id) { return {id, kSample, kAudioGroup}; }

static std::vector<uint8_t> hex(const char* text)
{
    std::vector<uint8_t> bytes;
    for (unsigned value; *text; text += 2) {
        CHECK(std::sscanf(text, "%2x", &value) == 1);
        bytes.push_back(uint8_t(value));
    }
    return bytes;
}

struct Library {
    std::map<Identity, Record> records;
    unsigned lookups = 0;
    void put(dbpf::Key key, std::vector<uint8_t> bytes = {0, 0, 0, 0}, bool stock = true)
    {
        records[identity(key)] = {key, stock, stock ? "Spore_EP1_Data.package" : "CustomAudio.package", std::move(bytes)};
    }
    Loader loader()
    {
        return [this](const dbpf::Key& key, bool bytes, Record& record, std::string&) {
            ++lookups;
            auto found = records.find(identity(key));
            if (found == records.end()) return Lookup::Missing;
            record = found->second;
            if (!bytes) record.data.clear();
            return Lookup::Found;
        };
    }
};

// Real decompressed vanilla records from the installed dc04 EP1_Data package.
// Eighth Day's descriptor has a distinct sample ID, not a same-ID .01A527DB.
static const char* eighth_day =
    "000000045f6317d50020000004b99d86000000000000000001000000"
    "6dd082180001000001701ed91e0020009c000000010000000ca13fa54b0000000000000000"
    "ba03d0cd000a000000000005";
static const char* welcome =
    "000000035f6317d50020000004b99d86000000000000000001000000"
    "701ed91e0020009c000000010000000ce4fc672d0000000000000000"
    "ba03d0cd000a000000000005";
static const char* parent_music =
    "0000000f03593710000d000042c800000f616b72000d00004248000012d2a4d40001000001"
    "140435490020009c000000010000000c41029725000000000000000025df0108000d00003e99999a"
    "544e850b002000000ef48a0b0000000000000000010000005a32a0ce0001000001"
    "5f6317d5002000008e8c29510000000000000000010000006dd082180001000001"
    "98ac8996000d00003f8000009b3918c4000d009c0000000600000004000000003f8000003e4ccccd"
    "3e4ccccd3f80000000000000a095ea05000a009c00000001000000048514d965ba1341920001000001"
    "e8ab99b9000d000040000000f58ca0be0020009c000000030000000c17dc85af0000000000000000"
    "261e23cd00000000000000000b19d9720000000000000000";

static Library stock()
{
    Library library;
    library.put(descriptor(0x3DAFBC30), hex(eighth_day));
    library.put(descriptor(0x2D67FCE4), hex(welcome));
    library.put(descriptor(0x869DB904), hex(parent_music));
    library.put(descriptor(0x51298C8E));
    library.put(sample(0x4BA53FA1));
    library.put(sample(0x2D67FCE4));
    return library;
}

static void native_records()
{
    auto library = stock(); Result result; std::string error;
    CHECK(validate(0x3DAFBC30, library.loader(), result, error));
    CHECK(error.empty()); CHECK(result.dependencies.size() == 4);
    CHECK(result.dependencies.back() == sample(0x4BA53FA1));
    CHECK(!result.native_control);
    CHECK(!library.records.count(identity(sample(0x3DAFBC30))));
    CHECK(validate(0x2D67FCE4, library.loader(), result, error));
    CHECK(result.dependencies.back() == sample(0x2D67FCE4));
    for (uint32_t id : {0u, UINT32_MAX, 1u}) {
        CHECK(validate(id, library.loader(), result, error));
        CHECK(result.dependencies.empty()); CHECK(result.native_control == (id == 1));
    }
    // A same-ID palette or raw sample is not native descriptor resolution.
    library.put(sample(0x12345678));
    library.put({0x12345678, 0x00B1B104, 0xD87454E6});
    CHECK(!validate(0x12345678, library.loader(), result, error));
    CHECK(error.find("021407EE!12345678.02B9F662") != std::string::npos);
    // Real stock3F604E1B has variable-length String16 array elements. The
    // header's native element size16 is not a serialized fixed string stride.
    library.put(descriptor(0x3F604E1B), hex("00000001dc98300e0013009c0000000300000010"
        "0000000866006f006f00740074007900700065000000000c650070006900630066006f006f0074007400790070006500"
        "0000000b73007500720066006100630065007400790070006500"));
    CHECK(validate(0x3F604E1B, library.loader(), result, error));
}

static void effective_provenance()
{
    Result result; std::string error;
    for (dbpf::Key key : {descriptor(0x3DAFBC30), descriptor(0x869DB904), descriptor(0x51298C8E), sample(0x4BA53FA1)}) {
        auto library = stock(); library.records[identity(key)].built_in = false;
        library.records[identity(key)].source = "CustomAudio.package";
        CHECK(!validate(0x3DAFBC30, library.loader(), result, error));
        CHECK(error.find("unsupported custom native audio") != std::string::npos);
        CHECK(error.find("CustomAudio.package") != std::string::npos);
    }
    for (dbpf::Key key : {descriptor(0x869DB904), sample(0x4BA53FA1)}) {
        auto library = stock(); library.records.erase(identity(key));
        CHECK(!validate(0x3DAFBC30, library.loader(), result, error));
        CHECK(error.find("missing native audio dependency") != std::string::npos);
    }
    auto library = stock(); library.put(descriptor(1), {}, false);
    CHECK(!validate(1, library.loader(), result, error)); // control bypass cannot mask an override
    library = stock(); library.records[identity(descriptor(0x3DAFBC30))].key = descriptor(5);
    CHECK(!validate(0x3DAFBC30, library.loader(), result, error));
    CHECK(error.find("key alias") != std::string::npos);
}

static void malformed()
{
    Result result; std::string error;
    auto bytes = hex(eighth_day);
    for (size_t n = 0; n < bytes.size(); ++n) {
        auto library = stock(); library.put(descriptor(0x3DAFBC30), {bytes.begin(), bytes.begin() + n});
        CHECK(!validate(0x3DAFBC30, library.loader(), result, error));
    }
    auto library = stock();
    library.put(descriptor(0x869DB904), hex("000000015f6317d50020000030bcaf3d000000000000000001000000"));
    CHECK(!validate(0x3DAFBC30, library.loader(), result, error));
    CHECK(error.find("cyclic") != std::string::npos);
    // Invalid count/array size is rejected before allocation or resource lookup.
    library = stock(); bytes = hex(eighth_day); bytes[0x2D] = 0xFF;
    library.put(descriptor(0x3DAFBC30), bytes);
    CHECK(!validate(0x3DAFBC30, library.loader(), result, error));
    CHECK(library.lookups == 1);
    library = stock();
    library.put(descriptor(0x3DAFBC30), hex("00000001701ed91e000a000000000005"));
    CHECK(!validate(0x3DAFBC30, library.loader(), result, error));
    // Reused parent descriptors are checked once within one resolution.
    library = stock(); library.put(descriptor(0x3DAFBC30), hex("000000015f6317d50020000004b99d86000000000000000001000000"));
    CHECK(validate(0x3DAFBC30, library.loader(), result, error)); CHECK(library.lookups == 3);
    // Stop a well-formed but excessively deep native descriptor chain.
    library = {};
    for (uint32_t i = 0; i < 64; ++i) {
        auto parent = hex("000000015f6317d50020000000000000000000000000000001000000");
        uint32_t next = 0x1000 + i + 1;
        for (unsigned byte = 0; byte < 4; ++byte) parent[12 + byte] = uint8_t(next >> (byte * 8));
        library.put(descriptor(0x1000 + i), std::move(parent));
    }
    library.put(descriptor(0x1040));
    CHECK(!validate(0x1000, library.loader(), result, error));
    CHECK(error.find("limit exceeded") != std::string::npos);
    library.put(descriptor(0x103F));
    CHECK(validate(0x1000, library.loader(), result, error));
    CHECK(result.dependencies.size() == 64);
    Loader failed = [](const dbpf::Key&, bool, Record&, std::string& why) {
        why = "native record stream refused the read";
        return Lookup::Error;
    };
    CHECK(!validate(0x3DAFBC30, failed, result, error));
    CHECK(error == "native record stream refused the read");
}

// Optional offline audit: a compact DBPF assembled from effective installed
// descriptor records and sample-key baselines. Never opens a game or profile.
static void audit(const char* path, const std::vector<uint32_t>& ids)
{
    std::ifstream input(path, std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
    dbpf::Package package; CHECK(package.load(bytes));
    Loader loader = [&](const dbpf::Key& key, bool read, Record& out, std::string& error) {
        const auto* entry = package.find(key);
        if (!entry) return Lookup::Missing;
        out.key = key; out.built_in = true; out.source = path;
        if (read && !package.read(*entry, out.data)) { error = "cannot decode audit descriptor"; return Lookup::Error; }
        return Lookup::Found;
    };
    Result result; std::string error;
    for (uint32_t music : ids) {
        bool valid = validate(music, loader, result, error);
        CHECK(valid);
        std::printf("installed %08X: %s (%zu dependencies) %s\n", music, valid ? "PASS" : "FAIL",
                    result.dependencies.size(), error.c_str());
    }
}

int main(int argc, char** argv)
{
    native_records(); effective_provenance(); malformed();
    if (argc >= 2) {
        std::vector<uint32_t> ids;
        for (int i = 2; i < argc; ++i) ids.push_back(uint32_t(std::strtoul(argv[i], nullptr, 16)));
        if (ids.empty()) ids = {0x3DAFBC30, 0x2D67FCE4};
        audit(argv[1], ids);
    }
    std::printf("adventure music: %u failures\n", failures);
    return failures ? 1 : 0;
}
