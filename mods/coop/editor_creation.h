// Participant-local native creation sessions. Native work runs outside Present.
#pragma once
#include "editor_native.h"
#include "sporekit_game.hpp"
#include <optional>

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
          std::optional<editor::ObjectId> target,std::string& error);
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
