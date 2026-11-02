// Participant-local native creation sessions. Native work runs outside Present.
#pragma once
#include "editor_native.h"
#include "sporekit_game.hpp"
#include <optional>
#include <span>

namespace editor_creation {
using Submit = bool(SK_CDECL*)(void*);
enum class PrepareResult : uint8_t { Wait, Ready, Reject };
struct Scope {
    editor::ProjectId project{};
    uint64_t epoch=0;
    editor::Peer author;
    uint64_t serial=0;
    bool operator==(const Scope&) const = default;
};
struct Request {
    uint32_t editor_id=0;
    editor::ResourceKey source;
    std::optional<editor::ObjectId> target;
};
struct Result {
    Request request;
    editor::ResourceKey saved;
    bool cancelled=false;
};
struct Variant {uint32_t editor_id;const char* name;};
// Exact GA AssetBrowser category New-editor IDs (968AE1B0), rather than the
// generic family fallbacks. Specialized configs constrain the native part list.
inline std::span<const Variant> new_variants(uint32_t resource_type){
    static constexpr std::array<Variant,1> creatures{{{0x465C50BA,"Creature"}}};
    static constexpr std::array<Variant,4> buildings{{
        {0x99E92F05,"City hall"},{0x4E3F7777,"House"},
        {0x8707BE7D,"Factory"},{0x72C49181,"Entertainment"}}};
    static constexpr std::array<Variant,9> vehicles{{
        {0x7D433FAD,"Military land"},{0x8F963DCB,"Military water"},{0x441CD3E6,"Military air"},
        {0xF670AA43,"Economic land"},{0x2A5147A9,"Economic water"},{0x1A4E0708,"Economic air"},
        {0x9AD7D4AA,"Cultural land"},{0x1F2A25B6,"Cultural water"},{0x449C040F,"Cultural air"}}};
    static constexpr std::array<Variant,1> spaceships{{{0x96B24187,"Spaceship"}}};
    switch(resource_type){
    case 0x2B978C46:return creatures;case 0x2399BE55:return buildings;
    case 0x24682294:return vehicles;case 0x476A98C7:return spaceships;
    default:return {};
    }
}
struct Ctx {
    std::function<std::optional<editor_native::GestureScope>()> scope;
    // May resolve target and acquire its content lease. Ready pins the local
    // adventure view and enables detached canonical editing before native entry.
    std::function<PrepareResult(const Scope&,Request&,std::string&)> prepare;
    std::function<bool(const Scope&)> current;
    std::function<bool(const Scope&,editor::ObjectId)> lease_valid;
    // False with empty error waits for native return/import/materialization.
    // False with a reason retains the saved local creation for explicit retry.
    std::function<bool(const Scope&,const Result&,std::string&)> returned;
    std::function<void(const Scope&,bool,const std::string&)> finished;
    std::function<void(const std::string&)> notice;
};
bool init(const SkHost*,SkMod*,Ctx);
void shutdown();
void tick();
// Called by the existing Submit hook. Retains the request while ownership or
// the canonical draft settles and calls the supplied original exactly once.
bool intercept(void* request,Submit original);
// Outside Present: create a native request for a supported resource family.
bool open(uint32_t resource_type,const editor::ResourceKey& source,
          std::optional<editor::ObjectId> target,std::string& error,uint32_t editor_id=0);
bool active();
bool designing();
bool available();
std::optional<Scope> scope();
std::optional<Result> last_saved();
// Cancels only a not-yet-submitted request. A live native editor retains its
// ordinary Done/Cancel route, including after party disconnect.
void cancel_pending();
std::string diagnostic();
} // namespace editor_creation
