// Owned Saved dependency values/codec. No Native provenance is minted here.
#pragma once
#include "net/dbpf.h"
#include <array>
#include <span>
#include <string_view>

namespace editor_saved_dependencies {
using Bytes=std::vector<uint8_t>;
using Key=dbpf::Key;
// Match the existing ResourceKey policy: instance/type identify a nonempty key;
// group zero is a valid namespace, not an empty-resource sentinel.
using Record=dbpf::Record;
inline constexpr uint32_t kPolicy=1,kScenario=0x366A930D,kMaxRecords=2048,kMaxRecordBytes=16u<<20,kMaxPayloadBytes=128u<<20;
inline constexpr uint32_t kMaxReceiptBytes=kMaxPayloadBytes+(4u<<20),kMaxReferences=1+32+1024*3,kMaxCompanions=8;
inline constexpr std::array<std::string_view,13> kStockDatabases{
    "spore_game.package","spore_content.package","spore_graphics.package","spore_audio1.package","spore_audio2.package",
    "patchdata.package","spore_pack_03.package","spore_ep1_data.package","spore_ep1_content_01.package",
    "spore_ep1_content_02.package","spore_ep1_locale_01.package","spore_ep1_locale_02.package","boosterpack_01.package"};
struct Domain {uint32_t policy=kPolicy;std::string executable,profile;bool operator==(const Domain&)const=default;};
enum class Kind:uint32_t {Captain=0,Crew=1,Class=2};
enum class Origin:uint32_t {Empty=0,Stock=1,Immutable=2};
struct Reference {
    Kind kind=Kind::Captain;uint32_t index=0,slot=0;
    Key raw;uint64_t server=UINT64_MAX;uint32_t machine=0,model=UINT32_MAX;
    Key effective;Origin origin=Origin::Empty;uint32_t stock_database=UINT32_MAX;
    std::vector<Key> records;
};
struct Inventory {
    Key saved;uint32_t raw_size=0;std::string raw_hash;
    uint32_t crew_count=0;std::vector<uint32_t> classes;std::vector<Reference> references;
};
struct Receipt {Domain domain;Inventory inventory;std::vector<Record> records;};
// Inventory must come from the separately verified Native issuer. All reference
// slots and exact raw-image binding are checked; stock tags remain observations,
// never authority inferred from missing custom bytes. Every output is atomic.
bool make(const Domain&,Key saved,std::span<const uint8_t> genuine_raw,const Inventory&,
          const std::vector<Record>& selected,const std::vector<Record>& admitted,Receipt&,std::string&);
bool encode(const Receipt&,Bytes&,std::string&);
bool decode(std::span<const uint8_t>,const Domain&,Receipt&,std::string&);
enum class Installed:uint32_t {Missing=0,Exact=1,Unknown=2};
struct Resident {Key requested,effective;Installed state=Installed::Unknown;Bytes data;};
// Collision facts only. This API cannot claim current-module package ownership,
// reuse, or a Native budget reservation. Existing ProcessBudget remains final.
struct CollisionPlan {uint32_t payload_bytes=0;std::vector<Key> missing;};
bool inspect_collisions(const Receipt&,const std::vector<Resident>&,CollisionPlan&,std::string&);
}
