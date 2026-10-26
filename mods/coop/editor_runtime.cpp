#include "editor_runtime.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>

#include "adventure_assets.h"
#include "adventure_collect.h"
#include "adventure_native.h"
#include "editor_native.h"
#include "editor_native_save.h"
#include "editor_metrics.h"
#include "editor_stage_metrics.h"
#include "editor_reopen_identity.h"
#include "editor_project.h"
#include "editor_session.h"
#include "editor_store.h"
#include "game.h"
#include "modes.h"
#include "sporekit_game.hpp"
#include "../../updater/sha256.h"

namespace coop_editor {
namespace {
namespace fs = std::filesystem;
using Phase = editor_session::Phase;
const SkHost* H;
SkMod* M;
Ctx C;
std::unique_ptr<editor_session::Controller> controller;
editor_store::Store store;
editor_native::Snapshot current_native, before_apply;
std::optional<editor_native::Snapshot> reopen_saved;
std::optional<bool> projection_override;
struct ProjectionScope {
    std::optional<bool> before = projection_override;
    explicit ProjectionScope(const editor::Document& document) {
        projection_override = document.schema.contains(editor_native::kRelativeScaleProperty);
    }
    ~ProjectionScope() { projection_override = before; }
};
editor_project::Resources resources;
std::optional<editor_project::Resources> incoming_resources;
std::optional<editor::wire::Saved> pending_saved;
editor::wire::Context incoming_context;
fs::path project_root, project_directory;
View cached;
bool queued, frozen, beginning, closing, gc_due, recovery_available, scanned_recovery;
bool native_playtesting, return_started, end_after_return, disconnected_return;
bool save_publication_pending, before_apply_valid, native_copy_owned;
bool native_attached, close_restored, close_preserve_offer;
uint64_t observed_transport, retired_transport;
struct TransportTeardown {
    uint64_t transport = 0, native_generation = 0;
    editor::wire::Context context;
    editor::Peer local;
};
struct DeferredOffer {
    uint64_t transport = 0;
    editor::wire::Context context;
    editor::Peer host, assigned;
    std::vector<uint8_t> bytes;
};
std::optional<TransportTeardown> transport_teardown;
std::optional<DeferredOffer> deferred_offer;
std::optional<editor_native::CancellationTicket> settled_cancellation;
std::string return_error;
struct AdventureMessage {
    editor::wire::Context context;
    uint64_t round = 0;
    uint8_t from = 0;
    std::vector<uint8_t> bytes;
};
std::deque<AdventureMessage> adventure_messages;
size_t adventure_message_bytes;
uint64_t sequence, native_generation, last_renew, last_presence, last_request, last_lease_use;
editor::wire::Context observed_lease_context;
editor::Peer observed_lease_peer;
std::set<std::pair<editor::Scope, uint64_t>> observed_lease_grants;
uint64_t last_preview;
bool preview_active;
std::optional<editor::wire::Presence> last_presence_value;
editor_metrics::Confirmations confirmation_metrics;
editor_stage_metrics::Buffer<> stage_metrics;
uint64_t stage_qpc() noexcept {
    LARGE_INTEGER value{};return QueryPerformanceCounter(&value)&&value.QuadPart>0?uint64_t(value.QuadPart):0;
}
void stage_stamp(const editor::ProjectId& project,const editor::OperationId& operation,editor::Revision revision,
                 editor_stage_metrics::Stage stage,uint8_t success=0) noexcept {
    if(stage_metrics.recording())stage_metrics.record(project,operation,revision,stage,stage_qpc(),success);
}
struct StageScope {
    editor::ProjectId project;editor::OperationId operation;editor::Revision revision;
    editor_stage_metrics::Stage end;bool okay=false;
    StageScope(const editor::Document& document,const editor::OperationId& op,
               editor_stage_metrics::Stage begin,editor_stage_metrics::Stage finish) noexcept:
        project(document.project),operation(op),revision(document.revision),end(finish){stage_stamp(project,operation,revision,begin);}
    ~StageScope(){stage_stamp(project,operation,revision,end,okay?1:2);}
};

editor::ProjectId project_id;

bool supported_act(uint32_t act, std::string& error) {
    if (!act) return true;
    error = "Shared placement editing currently requires the first act (Act 1). Other acts remain available in the ordinary editor.";
    return false;
}
bool supported_native_act(std::string& error) {
    const auto native = editor_native::state();
    return !native.editing || supported_act(native.act, error);
}
editor::Peer local_capability;
std::map<editor::ObjectId, editor::Placement> guarded_placements;
std::string status, resource_hash;
struct Artifact { std::string before, after; };
bool parse_manifest(const std::vector<uint8_t>&, std::string&, std::map<editor::OperationId, Artifact>&, std::string&, editor::Peer* = nullptr, bool* = nullptr);
std::map<editor::OperationId, Artifact> history_artifacts;
struct Pending { editor::Request request; editor_native::Gesture gesture; uint64_t reverted_frame = 0; bool submitted = false; };
std::optional<Pending> pending;
std::optional<editor::OperationId> awaiting_operation;
struct Recovery {
    editor::State state;
    editor_native::Snapshot native;
    std::vector<uint8_t> envelope;
    std::string title;
    std::optional<editor::Peer> resume;
    bool restored = false, needs_copy = false, saving = false;
    std::optional<editor::ResourceKey> saved_key;
    uint64_t native_generation = 0;
};
std::optional<Recovery> recovery;
enum class Action { Begin, Undo, Redo, Save, Playtest, Ready, Unready, Recover, End, Return, CancelPreparation };
std::deque<Action> actions;
struct PreviewReturnScope {
    editor::ProjectId project;
    uint64_t epoch = 0, round = 0;
    editor::Revision revision = 0;
};
std::optional<PreviewReturnScope> pending_preview_return;
std::optional<PreviewReturnScope> pending_preparation_cancel;
struct PreviewSelection {
    PreviewReturnScope scope;
    editor_native::LocalView view;
    uint64_t view_wait_started = 0;
    uint64_t native_owner_generation = 0, returned_generation = 0;
    std::optional<editor::ObjectId> selected;
};
std::optional<PreviewSelection> preview_selection;
std::vector<editor::Scope> requested_scopes;

// Packet dispatch only retires cached ownership. Native restoration waits for
// the outside-frame drain, including when explicit Leave precedes role change.
void retire_transport(bool explicit_stop) {
    if (!controller || !C.session) return;
    if (!transport_teardown) {
        transport_teardown = TransportTeardown{observed_transport, native_generation,
            controller->view().context, controller->view().local};
        if (!closing) { closing = true; close_restored = false; }
    }
    retired_transport = std::max(retired_transport, explicit_stop ? C.session->transport_generation() : observed_transport);
    if (deferred_offer && deferred_offer->transport <= retired_transport) deferred_offer.reset();
    close_preserve_offer = false; reopen_saved.reset();
    actions.clear(); pending.reset(); awaiting_operation.reset();
    pending_preview_return.reset(); pending_preparation_cancel.reset();
    requested_scopes.clear(); guarded_placements.clear(); observed_lease_grants.clear();
    adventure_messages.clear(); adventure_message_bytes = 0;
    beginning = false; recovery.reset(); pending_saved.reset(); incoming_resources.reset();
    save_publication_pending = false; end_after_return = false;
}
void observe_transport() {
    if (!C.session || transport_teardown || C.session->transport_generation() == observed_transport) return;
    if (controller && (controller->view().phase != Phase::Idle || native_attached || closing || recovery || beginning || adventure_native::editor_preview()))
        retire_transport(false);
    else observed_transport = C.session->transport_generation();
}
bool authenticate_offer(uint8_t from, const uint8_t* bytes, size_t size, DeferredOffer& out) {
    if (!C.session || from || C.session->role() != net::Role::Client || !C.session->connected() ||
        size <= 9 || size > editor::wire::kPacketBytes + 9) return false;
    net::Reader copy(bytes, size);
    editor::Peer sender{copy.u8(), copy.u32()}; const auto recipient = copy.u32();
    editor::wire::Message message; std::string error;
    if (sender.slot || !sender.generation || !recipient || !editor::wire::read(copy, message, sender, true, error)) return false;
    const auto* offer = std::get_if<editor::wire::Offer>(&message.payload);
    if (!offer || offer->host != sender || offer->assigned.slot != C.session->my_id() || offer->assigned.generation != recipient) return false;
    out = {C.session->transport_generation(), message.context, sender, offer->assigned, {bytes, bytes + size}};
    return true;
}
bool teardown_scope_matches() {
    return !transport_teardown || (controller && controller->view().context == transport_teardown->context &&
        controller->view().local == transport_teardown->local);
}
void tell(const std::string& text) {
    status = text;
    if (H) H->log(M, SK_INFO, "editor: %s", text.c_str());
    if (C.feed) C.feed(text);
}
bool session_active() {
    if (!controller) return false;
    auto phase = controller->view().phase;
    return phase != Phase::Idle && phase != Phase::Disconnected && phase != Phase::Offered;
}
bool host() { return C.session && C.session->role() == net::Role::Host; }
bool same_preview_context(const PreviewReturnScope& scope) {
    if (!controller) return false;
    const auto& view = controller->view();
    return view.context.project == scope.project && view.context.epoch == scope.epoch &&
        view.playtest_round == scope.round && view.playtest_revision == scope.revision &&
        controller->state().document.revision == scope.revision;
}
bool owned_preview_context() {
    if(!preview_selection || !adventure_native::editor_preview())return false;
    const auto& scope=preview_selection->scope;
    const auto& document=current_native.document;
    return document.project==scope.project && document.epoch==scope.epoch && document.revision==scope.revision &&
        adventure_native::editor_preview_matches(scope.project,scope.epoch,scope.round,scope.revision);
}
uint64_t preview_native_generation() {
    if(!preview_selection)return 0;
    return return_started?preview_selection->returned_generation:preview_selection->native_owner_generation;
}
uint64_t now_ms() { return uint64_t(std::max(0.0, H->time_seconds()) * 1000); }
void u64(net::Writer& w, uint64_t v) { w.u32(uint32_t(v)); w.u32(uint32_t(v >> 32)); }
uint64_t u64(net::Reader& r) { uint64_t lo = r.u32(); return lo | (uint64_t(r.u32()) << 32); }
bool paths(std::string& error) {
    if (!project_root.empty()) return true;
    auto address = H->sym("Resource::GetDatabaseByID");
    void* db = address ? skg::cdecl_call<void*>(address, 0x11AC199u) : nullptr;
    const wchar_t* location = db ? skg::vcall<const wchar_t*>(db, 0x28 / 4) : nullptr;
    std::wstring value;
    for (size_t i = 0; location && i < 1000; ++i) {
        if (!skg::readable(location + i, sizeof(wchar_t))) break;
        if (!location[i]) { project_root = fs::path(value).parent_path() / L"SporeCoop" / L"editor-projects"; return true; }
        value += location[i];
    }
    error = "The native profile directory is unavailable"; return false;
}
void discover_recovery() {
    if (scanned_recovery || recovery_available || session_active()) return;
    std::string error;
    if (!paths(error)) return;
    scanned_recovery = true;
    std::error_code ec;
    if (!fs::exists(project_root, ec)) return;
    fs::file_time_type latest = fs::file_time_type::min();
    for (const auto& entry : fs::directory_iterator(project_root, ec)) {
        const auto name = entry.path().filename().string();
        if (name.size() != 32 || !std::all_of(name.begin(), name.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); })) continue;
        auto time = fs::last_write_time(entry.path() / "checkpoint.bin", ec);
        if (ec) { ec.clear(); continue; }
        if (time <= latest) continue;
        latest = time; project_directory = entry.path();
        auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
        for (size_t i = 0; i < project_id.size(); ++i) project_id[i] = uint8_t((digit(name[i * 2]) << 4) | digit(name[i * 2 + 1]));
        recovery_available = true;
    }
}
bool safe_path(const fs::path& value, std::string& error) {
    for (auto p = fs::absolute(value);; p = p.parent_path()) {
        DWORD attrs = GetFileAttributesW(p.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
            error = "Editor project paths cannot contain reparse points"; return false;
        }
        if (p == p.parent_path()) break;
    }
    return true;
}
bool read_blob(const std::string& hash, std::vector<uint8_t>& out, size_t limit, std::string& error) {
    if (!net::adventure_content::valid_hash(hash)) { error = "Invalid editor artifact hash"; return false; }
    auto path = project_directory / "objects" / (hash + ".data");
    if (!safe_path(path, error)) return false;
    std::error_code ec; uint64_t size = fs::file_size(path, ec);
    if (ec || size > limit) { error = "An editor recovery artifact is missing or oversized"; return false; }
    std::vector<uint8_t> bytes(size_t(size), uint8_t{});
    std::ifstream file(path, std::ios::binary);
    if (!file || (size && !file.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size))) || upd::sha256_hex(bytes) != hash) {
        error = "An editor recovery artifact failed verification"; return false;
    }
    out = std::move(bytes); return true;
}
constexpr uint64_t kArtifactBudget = 384ull << 20;
bool artifact_usage(uint64_t& total, std::string& error) {
    total = 0;
    auto directory = project_directory / "objects";
    if (!safe_path(directory, error)) return false;
    std::error_code ec;
    if (!fs::exists(directory, ec)) { if (!ec) return true; }
    else for (const auto& entry : fs::directory_iterator(directory, ec)) {
        auto extension = entry.path().extension();
        if (extension != ".data" && extension != ".pending") continue;
        if (!safe_path(entry.path(), error)) return false;
        uint64_t size = entry.file_size(ec);
        if (ec || size > kArtifactBudget || total > kArtifactBudget - size) {
            error = "Editor recovery artifact budget is full"; return false;
        }
        total += size;
    }
    if (ec) { error = "Cannot inspect the editor recovery artifact directory"; return false; }
    return true;
}
bool put_blob(const std::vector<uint8_t>& bytes, std::string& hash, std::string& error) {
    if (bytes.empty() || bytes.size() > editor::wire::kResourceBytes || project_directory.empty()) { error = "Invalid editor artifact size or directory"; return false; }
    auto directory = project_directory / "objects";
    if (!safe_path(directory, error)) return false;
    std::error_code ec; fs::create_directories(directory, ec);
    if (ec) { error = "Cannot create the editor recovery artifact directory"; return false; }
    auto digest = upd::sha256_hex(bytes);
    auto target = directory / (digest + ".data");
    if (fs::exists(target, ec)) {
        std::vector<uint8_t> existing;
        if (!read_blob(digest, existing, bytes.size(), error)) return false;
        hash = std::move(digest); return true;
    }
    // A bounded private store. Refuse additional writes without deleting retained
    // history or an artifact that another recovery reader may still need.
    uint64_t total;
    if (!artifact_usage(total, error)) return false;
    if (bytes.size() > kArtifactBudget - total) { error = "Editor recovery artifact budget is full"; return false; }
    auto temporary = directory / (digest + ".pending");
    if (!safe_path(target, error) || !safe_path(temporary, error)) return false;
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = "Cannot write an editor recovery artifact"; return false; }
    size_t offset = 0; bool okay = true;
    while (okay && offset < bytes.size()) {
        DWORD written = 0;
        okay = WriteFile(file, bytes.data() + offset, DWORD(std::min<size_t>(1u << 20, bytes.size() - offset)), &written, nullptr) && written;
        offset += written;
    }
    okay = okay && FlushFileBuffers(file); CloseHandle(file);
    if (!okay || !MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        // This writer owns only this hash-named temporary, never a durable
        // artifact. A failed rename must not silently consume the budget.
        DeleteFileW(temporary.c_str());
        error = "Cannot flush an editor recovery artifact"; return false;
    }
    hash = std::move(digest); return true;
}
bool garbage_collect(std::string& error) {
    if (!store.healthy()) return false;
    std::string assets; std::map<editor::OperationId, Artifact> history;
    // Defined below; the stored manifest is the durable ownership authority.
    if (!parse_manifest(store.snapshot().manifest, assets, history, error)) return false;
    if (!store.checkpoint(store.snapshot(), error)) {
        if (!store.healthy()) throw std::runtime_error("Editor recovery maintenance is uncertain; reopen the checkpoint: " + error);
        return false;
    }
    std::set<std::string> keep{assets};
    for (const auto& [operation, artifact] : history) { (void)operation; keep.insert(artifact.before); keep.insert(artifact.after); }
    auto directory = project_directory / "objects";
    if (!safe_path(directory, error)) return false;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        auto hash = entry.path().stem().string();
        const auto extension = entry.path().extension();
        if (!net::adventure_content::valid_hash(hash) ||
            (extension != ".pending" && (extension != ".data" || keep.contains(hash)))) continue;
        if (!safe_path(entry.path(), error)) return false;
        fs::remove(entry.path(), ec);
        if (ec) { error = "Cannot remove an unreferenced editor artifact"; return false; }
    }
    return !ec;
}
bool reserve_artifacts(const std::vector<const std::vector<uint8_t>*>& artifacts, std::string& error) {
    auto required = [&]() {
        uint64_t additional = 0; std::set<std::string> unique;
        for (const auto* artifact : artifacts) {
            const auto& bytes = *artifact;
            auto hash = upd::sha256_hex(bytes);
            if (!unique.insert(hash).second) continue;
            std::error_code ec;
            if (fs::exists(project_directory / "objects" / (hash + ".data"), ec)) continue;
            if (ec || bytes.size() > kArtifactBudget - additional) return kArtifactBudget + 1;
            additional += bytes.size();
        }
        return additional;
    };
    uint64_t total = 0, additional = required();
    if (artifact_usage(total, error) && additional <= kArtifactBudget - total) return true;
    // Compact only before staging an entire gesture. Collecting between its
    // before/after writes could delete a staged artifact before it is retained.
    error.clear();
    if (!garbage_collect(error) || !artifact_usage(total, error) || (additional = required()) > kArtifactBudget - total) {
        if (error.empty()) error = "Editor recovery artifact budget is full";
        return false;
    }
    return true;
}
bool replace_root(editor_project::Resources& content, const editor_native::Snapshot& native, std::string& error) {
    dbpf::Key key{native.source.instance, native.source.type, native.source.group};
    if (!key.instance || key.type != 0x366A930D || native.bytes.empty()) { error = "Invalid current editor root"; return false; }
    auto records = content.bundle.records;
    std::erase_if(records, [&](const auto& record) { return record.key == content.root; });
    if (std::any_of(records.begin(), records.end(), [&](const auto& record) { return record.key == key; })) {
        error = "The current editor root collides with a retained dependency"; return false;
    }
    records.push_back({key, native.bytes});
    net::adventure_content::Bundle bundle;
    if (!net::adventure_content::make_bundle(key, std::move(records), bundle, error)) return false;
    content.root = key; content.act = native.act; content.bindings = native.bindings; content.bundle = std::move(bundle);
    return true;
}
bool root_delta(const editor_project::Resources& content, std::vector<uint8_t>& bytes, std::string& error) {
    return editor_project::encode_delta_resources(content, bytes, error);
}
bool merge_delta(editor_project::Resources delta, editor_project::Resources& merged, std::string& error) {
    if (delta.bundle.records.size() != 1 || delta.bundle.records.front().key != delta.root) {
        error = "A live editor resource update must contain exactly the current adventure root"; return false;
    }
    editor_native::Snapshot native;
    native.source = {delta.root.instance, delta.root.type, delta.root.group};
    native.act = delta.act; native.bindings = std::move(delta.bindings); native.bytes = std::move(delta.bundle.records.front().data);
    auto candidate = resources;
    if (!replace_root(candidate, native, error)) return false;
    merged = std::move(candidate); return true;
}
std::vector<uint8_t> manifest(const std::string& assets, const std::map<editor::OperationId, Artifact>& history) {
    net::Writer w; w.u32(0x32484445); w.str8(assets); w.u8(local_capability.slot); w.u32(local_capability.generation); w.u8(native_copy_owned); w.u32(uint32_t(history.size()));
    for (const auto& [operation, artifact] : history) {
        w.u8(operation.author.slot); w.u32(operation.author.generation); u64(w, operation.sequence); u64(w, operation.epoch);
        w.str8(artifact.before); w.str8(artifact.after);
    }
    return std::move(w.data);
}
bool parse_manifest(const std::vector<uint8_t>& bytes, std::string& assets, std::map<editor::OperationId, Artifact>& history, std::string& error, editor::Peer* local, bool* owned) {
    net::Reader r(bytes.data(), bytes.size());
    if (r.u32() != 0x32484445) { error = "Unsupported editor recovery manifest"; return false; }
    auto hash = r.str8(); editor::Peer capability{r.u8(), r.u32()}; uint8_t copy_owned = r.u8(); uint32_t count = r.u32();
    if (copy_owned > 1 || !net::adventure_content::valid_hash(hash) || count > editor::Limits{}.history ||
        !((capability.slot < 4 && capability.generation) || capability == editor::Peer{})) { error = "Invalid editor recovery manifest"; return false; }
    std::map<editor::OperationId, Artifact> result;
    for (uint32_t i = 0; i < count; ++i) {
        editor::OperationId operation; operation.author = {r.u8(), r.u32()}; operation.sequence = u64(r); operation.epoch = u64(r);
        Artifact artifact{r.str8(), r.str8()};
        if (operation.author.slot >= 4 || !operation.author.generation || !operation.sequence || !operation.epoch ||
            !net::adventure_content::valid_hash(artifact.before) || !net::adventure_content::valid_hash(artifact.after) ||
            !result.emplace(operation, artifact).second) { error = "Invalid native history artifact identity"; return false; }
    }
    if (!r.ok() || r.left()) { error = "Truncated editor recovery manifest"; return false; }
    assets = std::move(hash); history = std::move(result); if (local) *local = capability; if (owned) *owned = copy_owned; return true;
}
bool artifact_bytes(const editor_native::Snapshot& native, std::vector<uint8_t>& bytes, std::string& error) {
    std::vector<uint8_t> mapping;
    if (native.bytes.empty() || native.bytes.size() > net::adventure_content::kMaxRecordBytes || !editor_project::encode_mapping(native, mapping, error)) return false;
    net::Writer w; w.u32(0x31414445); w.u32(uint32_t(mapping.size())); w.raw(mapping.data(), mapping.size());
    w.u32(uint32_t(native.bytes.size())); w.raw(native.bytes.data(), native.bytes.size()); bytes = std::move(w.data); return true;
}
bool load_artifact(const std::string& hash, editor_native::Snapshot& native, std::string& error) {
    std::vector<uint8_t> bytes;
    if (!read_blob(hash, bytes, net::adventure_content::kMaxRecordBytes + editor_store::kMappingBytes + 12, error)) return false;
    net::Reader r(bytes.data(), bytes.size());
    if (r.u32() != 0x31414445) { error = "Unsupported native history artifact"; return false; }
    uint32_t size = r.u32();
    if (size > editor_store::kMappingBytes || size > r.left()) { error = "Invalid native artifact mapping"; return false; }
    std::vector<uint8_t> mapping(r.cur(), r.cur() + size); r.skip(size);
    editor_native::Snapshot candidate;
    if (!editor_project::decode_mapping(mapping, candidate, error)) return false;
    size = r.u32();
    if (!r.ok() || size != r.left() || size > net::adventure_content::kMaxRecordBytes) { error = "Invalid native history artifact length"; return false; }
    candidate.bytes.assign(r.cur(), r.cur() + size); native = std::move(candidate); return true;
}
bool structural(const editor::Change& change) { return !change.patch.objects.empty(); }
bool supported_between(const editor_native::Snapshot& before, const editor_native::Snapshot& after, std::string& error) {
    editor_native::Gesture check; check.generation = native_generation; check.before = before; check.after = after;
    return editor_native::supported_changes(check, error);
}
bool stop_unsupported(std::string& error) {
    frozen = true; guarded_placements.clear();
    std::string restore_error;
    if (!editor_native::restore(current_native, restore_error)) error += "; canonical restoration failed: " + restore_error;
    tell("Unsupported native authoring was blocked; recover the committed draft: " + error);
    return false;
}
bool native_integrity(const editor_native::Snapshot& snapshot, const editor::Document& document, std::string& error) {
    return editor_native::validate(snapshot, document, error) && supported_between(current_native, snapshot, error);
}
bool check_native_integrity(std::string& error) {
    if (editor_native::state().generation != native_generation) {
        frozen = true; error = "The native editor belongs to a different context"; tell(error); return false;
    }
    editor_native::Snapshot snapshot;
    if (!editor_native::capture(snapshot, error) || !native_integrity(snapshot, controller->state().document, error)) return stop_unsupported(error);
    return true;
}
bool remember_artifacts(const editor::State& state, const editor::Change& change, const editor_native::Snapshot& after,
                        std::map<editor::OperationId, Artifact>& archive, std::string& error) {
    if (structural(change) && change.intent == editor::Intent::Forward) {
        std::vector<std::vector<uint8_t>> bytes(2); Artifact entry;
        if (!artifact_bytes(before_apply, bytes[0], error) || !artifact_bytes(after, bytes[1], error) ||
            !reserve_artifacts({&bytes[0], &bytes[1]}, error) || !put_blob(bytes[0], entry.before, error) || !put_blob(bytes[1], entry.after, error)) return false;
        archive[change.gesture] = std::move(entry);
    }
    std::set<editor::OperationId> retained;
    for (const auto& gesture : state.history) retained.insert(gesture.operation);
    std::erase_if(archive, [&](const auto& entry) { return !retained.contains(entry.first); });
    return true;
}
bool persist(const editor::State& state, const editor::Change& change, std::string& error) {
    StageScope work(state.document,change.operation,editor_stage_metrics::Stage::PersistWorkBegin,editor_stage_metrics::Stage::PersistWorkEnd);
    editor_native::Snapshot after;
    if (!editor_native::capture(after, error)) return false;
    if (!editor_native::validate(after, state.document, error)) return false;
    after.document = state.document;
    after.source = {resources.root.instance, resources.root.type, resources.root.group};
    auto archive = history_artifacts;
    if (!remember_artifacts(state, change, after, archive, error)) return false;
    auto next_resources = resources;
    std::vector<uint8_t> envelope, delta;
    if (!replace_root(next_resources, after, error) || !editor_project::encode_resources(next_resources, envelope, error) ||
        (structural(change) && !root_delta(next_resources, delta, error))) return false;
    if (host() && !controller->update_resources(std::move(envelope), state.document.revision, error, structural(change), std::move(delta))) return false;
    editor_store::Snapshot saved;
    if (!editor_project::store_snapshot(state, after, controller->view().name, saved, error)) return false;
    // The immutable dependency closure is retained once. Current scenario bytes
    // and stable marker bindings are already part of the durable snapshot.
    saved.manifest = manifest(resource_hash, archive);
    bool committed=false;
    { StageScope durable(state.document,change.operation,editor_stage_metrics::Stage::DurableBegin,editor_stage_metrics::Stage::DurableEnd);
      committed=store.commit(saved,error);durable.okay=committed; }
    if (!committed) {
        if (!store.healthy()) throw std::runtime_error("Editor disk acceptance is uncertain; reopen the recovery checkpoint: " + error);
        return false;
    }
    // All allocations and publication payloads were staged before durable I/O.
    current_native = std::move(after); resources = std::move(next_resources);
    history_artifacts.swap(archive);
    incoming_resources.reset(); gc_due = state.document.revision % 16 == 0;
    work.okay=true;return true;
}
bool apply(const editor::Document& before, const editor::Document& after, const editor::Change& change, std::string& error) {
    StageScope work(after,change.operation,editor_stage_metrics::Stage::ApplyWorkBegin,editor_stage_metrics::Stage::ApplyWorkEnd);
    before_apply_valid = false;
    if (editor_native::state().generation != native_generation) { error = "The native editor belongs to a different context"; return false; }
    if (frozen || !editor_native::capture(before_apply, error)) return false;
    if (!native_integrity(before_apply, before, error)) return stop_unsupported(error);
    before_apply_valid = true;
    before_apply.document = before;
    editor_native::Snapshot artifact;
    const editor_native::Snapshot* source = nullptr;
    if (structural(change)) {
        if (!host() && incoming_resources && incoming_context.revision == change.revision) {
            // Templates/assets are immutable in the placement preview. The
            // owned scenario artifact needs no new resident database per edit.
            if (!editor_project::native_snapshot(*incoming_resources, after, artifact, error)) return false;
            source = &artifact;
        } else if (change.intent != editor::Intent::Forward) {
            auto it = history_artifacts.find(change.gesture);
            if (it == history_artifacts.end()) { error = "This gesture's native history is unavailable"; return false; }
            if (!load_artifact(change.intent == editor::Intent::Undo ? it->second.before : it->second.after, artifact, error)) return false;
            source = &artifact;
        } else if (pending && pending->request.operation == change.operation) {
            artifact = pending->gesture.after;
            size_t next = 0;
            for (auto& binding : artifact.bindings) if (!before.placements.contains(binding.object)) {
                if (next >= change.allocated.size()) { error = "Native creation identities do not match the accepted gesture"; return false; }
                binding.object = change.allocated[next++];
            }
            if (next != change.allocated.size()) { error = "Native creation allocation count differs"; return false; }
            source = &artifact;
        }
    }
    bool applied=false;
    { StageScope native(after,change.operation,editor_stage_metrics::Stage::NativeApplyBegin,editor_stage_metrics::Stage::NativeApplyEnd);
      applied=editor_native::apply(after,change,source,error);native.okay=applied; }
    if(!applied)return false;
    if (pending && pending->request.operation == change.operation && !change.allocated.empty()) {
        std::string selection_error;
        // The inverse removes the native temporary selection. Restore the new
        // stable selection for its author, as ordinary placement does. Other
        // players retain their own selection and native overlapping-icon groups.
        if (!editor_native::select_placement(change.allocated.front(), selection_error) && H)
            H->log(M, SK_INFO, "Shared editor local selection: %s", selection_error.c_str());
    }
    work.okay=true;return true;
}
bool rollback(const editor::Document&, const editor::Document&, const editor::Change&, std::string& error) {
    return !before_apply_valid || editor_native::restore(before_apply, error);
}
bool lease(const std::vector<editor::Scope>& scopes) {
    if (!controller || frozen || closing || recovery || awaiting_operation || controller->view().phase != Phase::Ready) return false;
    const auto& held = controller->view().local_leases;
    const auto local = controller->view().local;
    uint64_t time = now_ms();
    last_lease_use = time;
    bool complete = std::all_of(scopes.begin(), scopes.end(), [&](const auto& scope) {
        return std::any_of(held.begin(), held.end(), [&](const auto& item) {
            return item.owner == local && item.expires > time && (item.scope == scope || item.scope.kind == editor::ScopeKind::Document);
        });
    });
    if (complete) return true;
    if (requested_scopes != scopes || time - last_request >= 2000) {
        requested_scopes = scopes; last_request = time; std::string error;
        if (!controller->request_leases(scopes, 15000, error)) tell(error);
        else status = "Waiting for editing ownership. Repeat the gesture when ownership is granted.";
    }
    return false;
}
void observe_lease_grants() {
    const auto& view = controller->view();
    if (view.phase != Phase::Ready || view.context.project != observed_lease_context.project ||
        view.context.epoch != observed_lease_context.epoch || view.local != observed_lease_peer)
        observed_lease_grants.clear();
    observed_lease_context = view.context;
    observed_lease_peer = view.local;
    std::set<std::pair<editor::Scope, uint64_t>> current;
    if (view.phase == Phase::Ready) for (const auto& held : view.local_leases) {
        if (held.owner != view.local || held.expires <= now_ms()) continue;
        const auto key = std::make_pair(held.scope, held.token);
        current.insert(key);
        // A queued request can wait longer than the unused-ownership timeout.
        // Start that grace when its valid grant becomes locally usable. Renewal
        // of an already observed token does not extend idle ownership.
        if (!observed_lease_grants.contains(key)) last_lease_use = now_ms();
    }
    observed_lease_grants = std::move(current);
}
editor::Request request(editor::Intent intent) {
    const auto& doc = controller->state().document;
    editor::Request r; r.project = doc.project; r.epoch = doc.epoch;
    r.operation = {controller->view().local, ++sequence, doc.epoch}; r.base_revision = doc.revision; r.intent = intent;
    for (const auto& held : controller->view().local_leases) r.leases.push_back({held.scope, held.token});
    return r;
}
bool same_authored(const editor::Placement& a, const editor::Placement& b) {
    return a.class_id == b.class_id && a.act_id == b.act_id && a.protected_object == b.protected_object &&
        a.position == b.position && a.rotation == b.rotation && a.scale == b.scale && a.properties == b.properties;
}
editor_native::GestureScope current_gesture_scope() {
    if (!controller || !native_attached || closing || recovery || transport_teardown || !C.session ||
        !C.session->connected() || C.session->transport_generation() != observed_transport) return {};
    const auto& view = controller->view();
    return {view.context.project, view.context.epoch, view.local};
}
bool cancelled_gesture(const editor_native::CancellationTicket& ticket, std::string& error) {
    // Native already rolled its rejected entry back to an independent before
    // copy. This is not a user Undo and must never submit a history operation.
    // Late tickets from another editor, transport or logical project are inert.
    if (!controller || !native_attached || closing || frozen || recovery || transport_teardown ||
        !session_active() || ticket.scope != current_gesture_scope() || ticket.generation != native_generation) return true;
    const auto native = editor_native::state();
    if (native.generation != ticket.generation || adventure_native::editor_preview() ||
        (controller->view().phase != Phase::Ready && controller->view().phase != Phase::Preparing)) return true;
    if (!ticket.gesture || !ticket.serial) { error = "The native cancellation ticket is incomplete"; return false; }
    if (settled_cancellation && settled_cancellation->scope == ticket.scope && settled_cancellation->generation == ticket.generation &&
        (ticket.serial <= settled_cancellation->serial || ticket.gesture <= settled_cancellation->gesture)) return true;
    if (pending || awaiting_operation || !native.ready || native.gesture || editor_native_save::busy()) {
        error = "The cancelled native gesture could not settle while another operation is pending"; return false;
    }
    if (!editor_native::validate(current_native, controller->state().document, error) ||
        !editor_native::restore(current_native, error)) return false;
    if (native.selected.size() == 1 && controller->state().document.placements.contains(native.selected.front()) &&
        !editor_native::select_placement(native.selected.front(), error)) return false;
    std::vector<editor::LeaseToken> release;
    for (const auto& held : controller->view().local_leases) if (held.owner == controller->view().local)
        release.push_back({held.scope, held.token});
    if (!release.empty() && !controller->release_leases(release, error)) return false;
    guarded_placements.clear(); requested_scopes.clear(); last_request = 0;
    controller->clear_preview(); preview_active = false; last_presence_value.reset();
    settled_cancellation = ticket;
    tell("Spore cancelled the invalid placement gesture. The latest committed draft is restored.");
    return true;
}
void captured(editor_native::Gesture gesture) {
    if (!controller || !session_active() || frozen || closing || recovery) return;
    if (gesture.generation != native_generation || editor_native::state().generation != native_generation) {
        frozen = true; tell("A delayed gesture belongs to a different native editor. The current editor was left intact."); return;
    }
    if (awaiting_operation) { frozen = true; tell("Another native gesture arrived before the pending operation settled; recover the committed draft"); return; }
    const auto& doc = controller->state().document;
    std::string error;
    if (!supported_between(current_native, gesture.before, error) || !editor_native::supported_changes(gesture, error)) {
        stop_unsupported(error); return;
    }
    if (gesture.before.document.classes != doc.classes || gesture.after.document.classes != doc.classes ||
        gesture.before.document.acts != doc.acts || gesture.after.document.acts != doc.acts || gesture.after.act != current_native.act) {
        frozen = true;
        if (!editor_native::restore(current_native, error)) tell("Unsupported authoring changed the draft; recovery is required: " + error);
        else tell("Shared editing is frozen after an unsupported template or act edit. Recover the committed draft.");
        return;
    }
    auto r = request(editor::Intent::Forward); r.summary = "Edit placement";
    editor::Change revert;
    for (const auto& [id, placement] : doc.placements) {
        auto prior = gesture.before.document.placements.find(id), next = gesture.after.document.placements.find(id);
        if (prior != gesture.before.document.placements.end() && next != gesture.after.document.placements.end() && same_authored(prior->second, next->second)) continue;
        auto guard = guarded_placements.find(id);
        if (prior == gesture.before.document.placements.end() || guard == guarded_placements.end()) {
            frozen = true; tell("The native gesture did not start from the committed placement state. Recover the draft."); return;
        }
        if (next == gesture.after.document.placements.end()) {
            editor::Mutation m; m.kind = editor::MutationKind::Delete; m.target = id; m.expected = guard->second.revision;
            r.mutations.push_back(m); revert.patch.objects.push_back({id, std::nullopt, placement, 0}); r.summary = "Delete placement";
        } else {
            for (editor::Field field : {editor::Field::Position, editor::Field::Rotation, editor::Field::Scale}) {
                editor::FieldKey key{field, 0};
                if (editor::value(prior->second, key) != editor::value(next->second, key)) {
                    editor::Mutation m; m.target = id; m.field = key; m.value = editor::value(next->second, key); m.expected = editor::field_revision(guard->second, key);
                    r.mutations.push_back(m);
                }
            }
            std::set<uint32_t> properties;
            for (const auto& [property, value] : prior->second.properties) { (void)value; properties.insert(property); }
            for (const auto& [property, value] : next->second.properties) { (void)value; properties.insert(property); }
            for (auto property : properties) {
                const editor::FieldKey key{editor::Field::Property, property};
                const auto before = editor::value(prior->second, key), after = editor::value(next->second, key);
                if (before == after) continue;
                if (!doc.schema.contains(property)) { frozen = true; tell("An unregistered native property edit was blocked. Recover the draft."); return; }
                editor::Mutation mutation; mutation.target = id; mutation.field = key;
                mutation.value = after; mutation.expected = editor::field_revision(guard->second, key);
                r.mutations.push_back(std::move(mutation));
            }
        }
    }
    for (const auto& [id, placement] : gesture.after.document.placements) if (!doc.placements.contains(id)) {
        editor::Mutation m; m.kind = editor::MutationKind::Create; m.initial = placement;
        m.initial.object = {}; m.initial.revision = 0; m.initial.field_revisions.clear();
        if (gesture.clone_source) {
            auto source = guarded_placements.find(*gesture.clone_source);
            if (source == guarded_placements.end()) { frozen = true; tell("The native clone source has no editing ownership. Recover the draft."); return; }
            if (placement.properties != source->second.properties) {
                error = "Cloning and changing a placement property in one gesture is unsupported; repeat Clone and Scale separately";
                stop_unsupported(error); return;
            }
            m.kind = editor::MutationKind::Clone; m.target = *gesture.clone_source; m.initial = {};
            m.expected = source->second.revision;
            m.clone_pose = editor::Pose{placement.position, placement.rotation, placement.scale};
        }
        r.mutations.push_back(m); revert.patch.objects.push_back({id, placement, std::nullopt, 0}); r.summary = "Create placement";
    }
    guarded_placements.clear();
    if (r.mutations.empty()) return;
    // The native gesture supplies the transaction, not an extra committed apply.
    // Restore the latest canonical placement state before requesting acceptance.
    if (!editor_native::apply(doc, revert, &current_native, error)) {
        frozen = true; tell("The native gesture could not return to committed state: " + error); return;
    }
    if (!H->frame_index) { frozen = true; tell("Native gesture submission requires the game frame counter"); return; }
    pending = Pending{r, std::move(gesture), H->frame_index(), false};
    awaiting_operation = r.operation;
    status = "Waiting for the next game frame before submitting the placement gesture";
}
void submit_pending_gesture() {
    if (!pending || pending->submitted) return;
    const auto& r = pending->request;
    const auto& v = controller->view();
    const auto native = editor_native::state();
    const bool changed_generation = r.operation.epoch != v.context.epoch || pending->gesture.generation != native_generation || native.generation != native_generation;
    const bool ending = std::any_of(actions.begin(), actions.end(), [](auto action) { return action == Action::End || action == Action::Recover; });
    if (closing || ending || frozen || recovery || !C.session || !C.session->connected() || v.phase != Phase::Ready ||
        r.project != v.context.project || r.operation.author != v.local || r.operation.author.slot != C.session->my_id() ||
        r.base_revision > controller->state().document.revision || changed_generation) {
        pending.reset(); awaiting_operation.reset();
        if (changed_generation && !closing && !ending) { frozen = true; tell("A held gesture expired with its editor context; no operation was submitted"); }
        return;
    }
    // Native inverse deletion queues noun retirement until the ordinary game
    // update. Recreating the same slot in this outside-frame pump is too early.
    // Repeated callbacks within one rendered frame must not advance this gate.
    if (!native.ready || native.gesture || !H->frame_index || H->frame_index() <= pending->reverted_frame) return;
    auto request = r;
    pending->submitted = true;
    std::string error;
    const auto operation=request.operation;
    stage_stamp(controller->state().document.project,operation,controller->state().document.revision,editor_stage_metrics::Stage::LocalSubmit);
    confirmation_metrics.begin(operation,H->time_seconds());
    if (!controller->submit(std::move(request), error)) { confirmation_metrics.cancel(operation);pending.reset(); awaiting_operation.reset(); tell(error); }
    else status = "Waiting for the host to commit the placement gesture";
}
void refresh() {
    View next;
    if (!controller) { cached = next; return; }
    const auto& v = controller->view(); const auto& doc = controller->state().document;
    auto native = editor_native::state();
    next.host = host(); next.offered = v.phase == Phase::Offered; next.active = session_active() || closing;
    next.visible = C.session && C.session->connected();
    next.can_begin = !next.active && !closing && !beginning && (!C.adventure_busy || !C.adventure_busy()) &&
        ((next.host && native.ready && native.act == 0 && editor_native_save::available()) || (next.offered && game::mode_id() == modes::kMenu));
    next.revision = doc.revision; next.durable_revision = v.locally_durable;
    next.title = v.name.empty() ? "Shared Adventure Editor" : v.name;
    next.status = status.empty() ? v.error : status;
    if (!next.active && next.host && native.editing && native.act != 0)
        supported_act(native.act, next.status);
    auto identity=[](editor::ObjectId id) { return "Placement " + std::to_string(id.id) + ":" + std::to_string(id.generation); };
    next.selection = native.selected.empty() ? "No placement selected" : identity(native.selected.front());
    next.frozen = frozen || v.phase == Phase::Frozen;
    next.waiting = closing || recovery.has_value() || awaiting_operation.has_value() || v.phase == Phase::Joining || v.phase == Phase::Syncing || v.phase == Phase::Preparing || v.phase == Phase::Returning;
    if (v.phase == Phase::Returning) next.status = "Waiting for every player to return to the committed draft";
    next.confirming = v.phase == Phase::Preparing;
    next.can_cancel = next.confirming && next.host && !closing && !recovery && !next.frozen;
    next.playtesting = v.phase == Phase::Playing;
    next.can_return = next.playtesting && next.host;
    bool editing = native.ready && next.active && !closing && !recovery && v.phase == Phase::Ready && !next.frozen && !awaiting_operation && !native.gesture && !editor_native_save::busy();
    for (const auto& gesture : controller->state().history) if (gesture.operation.author == v.local) {
        next.can_undo = next.can_undo || gesture.applied;
        next.can_redo = next.can_redo || (!gesture.applied && gesture.redo_available);
    }
    next.can_undo &= editing; next.can_redo &= editing;
    next.can_save = editing && next.host && !save_publication_pending;
    const auto authored_avatar = editor_native::avatar();
    next.can_playtest = editing && next.host && !save_publication_pending && authored_avatar.instance && authored_avatar.type &&
        std::all_of(v.members.begin(), v.members.end(), [](const auto& entry) { return entry.second.phase == Phase::Ready; });
    if (editing && next.host && (!authored_avatar.instance || !authored_avatar.type))
        next.status = "An authored captain is required for playtesting. End sharing to configure the captain in Spore.";
    next.can_recover = next.host && !closing && recovery_available && !recovery && (next.frozen || !next.active) &&
        (game::mode_id() == modes::kMenu || (native.editing && native.generation == native_generation));
    if (v.saved) { next.natively_saved = true; next.saved_revision = v.saved->saved_revision; }
    for (const auto& [slot, member] : v.members) {
        PlayerView player; player.id = slot; player.name = member.name; player.host = slot == 0; player.local = slot == v.local.slot;
        player.ready = member.ready; player.durable_revision = member.coverage.durable_revision; player.applied_revision = member.applied_revision;
        player.selection = member.presence.selected.id ? identity(member.presence.selected) : "No placement selected";
        player.pending = member.presence.pending;
        player.owns_selection = (member.presence.ownership & editor::wire::kOwnSelection) != 0;
        auto phase=member.phase;
        if (!next.host && !player.local && member.presence.author==member.peer) {
            using P=editor::wire::PresencePhase;
            switch(member.presence.phase){
            case P::Ready: phase=Phase::Ready;break;
            case P::Syncing: phase=Phase::Syncing;break;
            case P::Preparing: phase=Phase::Preparing;break;
            case P::Playing: phase=Phase::Playing;break;
            case P::Returning: phase=Phase::Returning;break;
            case P::Frozen: phase=Phase::Frozen;break;
            case P::Disconnected: phase=Phase::Disconnected;break;
            case P::Offered: phase=Phase::Offered;break;
            }
        }
        switch(phase){
        case Phase::Ready:
            player.status=player.pending ? "Commit pending" : member.presence.editing ? player.owns_selection ? "Editing owned placement" : "Editing draft structure" :
                player.owns_selection ? "Owns selected placement" : (member.presence.ownership & editor::wire::kOwnDocument) ? "Owns draft structure" : "Browsing";break;
        case Phase::Joining: case Phase::Syncing: player.status="Receiving draft";break;
        case Phase::Preparing: player.status=player.ready ? "Playtest ready" : "Confirm playtest";break;
        case Phase::Playing: player.status="Playtesting";break;
        case Phase::Returning: player.status="Returning to draft";break;
        case Phase::Frozen: player.status="Needs recovery";break;
        case Phase::Disconnected: player.status="Disconnected";break;
        case Phase::Offered: player.status="Invited to edit";break;
        case Phase::Idle: player.status="Not editing";break;
        }
        if (!player.local && v.phase==Phase::Ready && member.preview && member.preview->expires>now_ms()) {
            const auto& hint=member.preview->value;
            if (doc.placements.contains(hint.object)) {
                TransformHint shown;
                size_t name_bytes=std::min<size_t>(20,member.name.size());
                while(name_bytes && name_bytes<member.name.size() &&
                      (uint8_t(member.name[name_bytes])&0xC0)==0x80)--name_bytes;
                shown.label=member.name.substr(0,name_bytes)+": "+identity(hint.object)+" (preview)";
                shown.position={hint.pose.position.x,hint.pose.position.y,hint.pose.position.z};
                shown.rotation={hint.pose.rotation.x,hint.pose.rotation.y,hint.pose.rotation.z,hint.pose.rotation.w};
                shown.scale=hint.pose.scale;next.previews.push_back(std::move(shown));
            }
        }
        next.players.push_back(std::move(player));
        if (slot == v.local.slot) next.ready = member.ready;
    }
    cached = std::move(next);
}
bool create_store(const editor::State& state, const editor_native::Snapshot& native, const std::vector<uint8_t>& envelope, const std::string& title, std::string& error) {
    if (!paths(error)) return false;
    project_directory = project_root / editor_store::project_name(state.document.project);
    editor_store::Snapshot saved;
    if (!editor_project::store_snapshot(state, native, title, saved, error)) return false;
    bool okay = false;
    std::error_code ec;
    if (fs::exists(project_directory / "checkpoint.bin", ec)) {
        if (!store.open(project_root, state.document.project, error)) return false;
        if (store.snapshot().revision > state.document.revision && store.snapshot().epoch == state.document.epoch) {
            error = "The received draft is older than this profile's durable checkpoint"; return false;
        }
        okay = true;
    } else okay = store.create(project_root, saved, error);
    if (!okay) return false;
    std::string hash;
    if (!reserve_artifacts({&envelope}, error) || !put_blob(envelope, hash, error)) return false;
    saved.manifest = manifest(hash, {});
    if (!store.adopt_checkpoint(saved, error)) return false;
    resource_hash = std::move(hash); project_id = state.document.project; recovery_available = true;
    return true;
}
bool checkpoint_resources(editor_project::Resources content, editor_native::Snapshot native, std::string& error) {
    auto local_dirty = native.local_dirty;
    if (!editor_project::native_snapshot(content, controller->state().document, native, error)) return false;
    native.local_dirty = local_dirty;
    // Save can normalize opaque authoring metadata as well as rotate the key.
    // Install those exact bytes on the saving host as well as its guests before
    // another gesture captures the working resource.
    if (!editor_native::restore(native, error)) return false;
    std::vector<uint8_t> envelope, delta;
    if (!editor_project::encode_resources(content, envelope, error) || !root_delta(content, delta, error)) return false;
    editor_store::Snapshot saved;
    if (!editor_project::store_snapshot(controller->state(), native, controller->view().name, saved, error)) return false;
    saved.manifest = manifest(resource_hash, history_artifacts);
    if (!store.checkpoint(saved, error)) return false;
    if (host() && !controller->update_resources(std::move(envelope), saved.revision, error, true, std::move(delta))) {
        frozen = true; return false;
    }
    current_native = std::move(native); resources = std::move(content);
    return true;
}
void saved_native(editor::ResourceKey key) {
    if (!host() || !session_active()) return;
    std::string error;
    editor_native::Snapshot native;
    auto content = resources;
    const dbpf::Key root{key.instance, key.type, key.group};
    std::vector<dbpf::Record> records;
    if (!editor_native::capture(native, error)) { frozen = true; tell(error); return; }
    save_publication_pending = true; native_copy_owned = true;
    native.source = key;
    if (!editor_native_save::read_saved(key, native.bytes, error) ||
        !editor_native::validate(native, controller->state().document, error)) { frozen = true; tell(error); return; }
    if (!adventure_collect::collect_snapshot(root, native.bytes, records, error)) { frozen = true; tell(error); return; }
    auto normalized = std::find_if(records.begin(), records.end(), [&](const auto& record) { return record.key == root; });
    if (normalized == records.end()) { frozen = true; tell("The saved editor root is missing"); return; }
    native.source = key; native.bytes = std::move(normalized->data);
    if (!replace_root(content, native, error) ||
        !checkpoint_resources(std::move(content), std::move(native), error) ||
        !controller->publish_saved({controller->state().document.revision, key}, error)) {
        frozen = true; tell("The native save completed, but sharing its recovery checkpoint stopped: " + error); return;
    }
    tell("The shared adventure draft was saved by Spore");
}
void start_saved() {
    reopen_saved.reset();
    beginning = false;
    std::string error;
    editor_native::Snapshot native;
    if (!editor_native::capture(native, error) || !native.source.instance || native.source.type != 0x366A930D) { tell(error.empty() ? "Save a new native adventure draft before sharing" : error); return; }
    if (!supported_act(native.act, error)) { tell(error); return; }
    bool tracked = false;
    if (!editor_native::relative_scale_mode(native.document, tracked, error) || !tracked) {
        tell(error.empty() ? "The native draft lacks the required continuous-scale projection; sharing did not start" : error); return;
    }
    editor::State state; state.document = native.document;
    std::random_device random;
    for (auto& byte : state.document.project) byte = uint8_t(random());
    state.document.epoch = (uint64_t(random()) << 32) | random();
    if (!state.document.epoch) state.document.epoch = 1;
    native.document = state.document;
    if (!editor_native_save::read_saved(native.source, native.bytes, error) ||
        !editor_native::validate(native, state.document, error)) { tell(error); return; }
    editor_project::Resources content;
    content.root = {native.source.instance, native.source.type, native.source.group}; content.act = native.act; content.bindings = native.bindings;
    std::vector<dbpf::Record> records;
    if (!adventure_collect::collect_snapshot(content.root, native.bytes, records, error) ||
        !net::adventure_content::make_bundle(content.root, std::move(records), content.bundle, error)) { tell(error); return; }
    for (const auto& record : content.bundle.records) if (record.key == content.root)
        content.base_root = std::make_shared<const std::vector<uint8_t>>(record.data);
    std::vector<uint8_t> envelope;
    if (!editor_project::encode_resources(content, envelope, error) || !editor_project::native_snapshot(content, state.document, native, error) ||
        !adventure_assets::register_records(content.bundle.records, error) || !editor_native::restore(native, error)) { tell(error); return; }
    std::string title = adventure_collect::presentation(content.root).name;
    if (title.empty()) title = "Shared adventure draft";
    store.close(); history_artifacts.clear(); local_capability = {}; native_copy_owned = true;
    if (!create_store(state, native, envelope, title, error)) { tell(error); return; }
    resources = std::move(content); current_native = std::move(native); sequence = 0; frozen = false;
    native_attached = true;
    native_generation = editor_native::state().generation;
    if (!controller->start(std::move(state), std::move(envelope), title, error)) { store.close(); tell(error); return; }
    local_capability = controller->view().local;
    auto checkpoint = store.snapshot(); checkpoint.manifest = manifest(resource_hash, history_artifacts);
    if (!store.checkpoint(checkpoint, error)) { frozen = true; tell(error); return; }
    save_publication_pending = true;
    if (!controller->publish_saved({controller->state().document.revision, current_native.source}, error)) { frozen = true; tell(error); return; }
    tell("Shared placement editing started. Templates, acts, terrain and nested editors are read-only.");
    if (C.open_panel) C.open_panel();
}
void undo_redo(bool redo) {
    if (!controller || closing || recovery || controller->view().phase != Phase::Ready || awaiting_operation || frozen ||
        save_publication_pending || editor_native_save::busy()) return;
    // Cached toolbar availability can predate the native input event. Recheck
    // ownership and transitions before acquiring leases or queuing history work.
    const auto native = editor_native::state();
    if (!native.editing || !native.ready || native.gesture || native.generation != native_generation) return;
    const auto& state = controller->state(); const editor::Gesture* target = nullptr;
    for (const auto& gesture : state.history) if (gesture.operation.author == controller->view().local) {
        if (!redo && gesture.applied && (!target || gesture.original_revision > target->original_revision)) target = &gesture;
        if (redo && !gesture.applied && gesture.redo_available && (!target || gesture.transition_revision > target->transition_revision)) target = &gesture;
    }
    if (!target) { tell(redo ? "There is no action to redo" : "There is no action to undo"); return; }
    std::vector<editor::Scope> scopes;
    const auto& patch = redo ? target->redo : target->undo;
    for (const auto& object : patch.objects) scopes.push_back({editor::ScopeKind::Placement, object.object, {}});
    for (const auto& field : patch.fields) scopes.push_back({editor::ScopeKind::Field, field.object, field.field});
    if (!lease(scopes)) return;
    auto r = request(redo ? editor::Intent::Redo : editor::Intent::Undo);
    std::string error;
    awaiting_operation = r.operation;
    stage_stamp(controller->state().document.project,r.operation,controller->state().document.revision,editor_stage_metrics::Stage::LocalSubmit);
    if (!controller->submit(std::move(r), error)) { awaiting_operation.reset(); tell(error); }
}
void close_session(bool preserve_offer = false) {
    pending_preparation_cancel.reset();
    preview_selection.reset();
    observed_lease_grants.clear();
    if (!closing) { reopen_saved.reset(); closing = true; close_restored = false; close_preserve_offer = preserve_offer; }
    close_preserve_offer = !transport_teardown && (close_preserve_offer || preserve_offer || controller->view().phase == Phase::Offered);
    // An accepted edit remains owned until active replicas have applied and
    // durably covered it. Keep pumping the controller and party transport while
    // authoring is held; reliable receipt alone does not prove application.
    if (!transport_teardown && host() && session_active() && !controller->settled()) {
        status = "Delivering the last committed edit before ending shared editing"; return;
    }
    const auto native = editor_native::state();
    // Keep native input/history guards installed until the owned editor has
    // settled. A different generation belongs to a different native context.
    const bool owned_context = native_attached && native.generation == native_generation && teardown_scope_matches();
    if ((editor_native_save::busy() && (!transport_teardown || owned_context)) || (owned_context && native.editing &&
        (!native.ready || native.gesture))) {
        status = "Finishing the native gesture or save before ending shared editing"; return;
    }
    if (owned_context && native.editing) {
        std::string error;
        if (!close_restored) {
            if (!editor_native::restore(current_native, error)) { frozen = true; tell("Ending shared editing is waiting for canonical restoration: " + error); return; }
            close_restored = true;
        }
        if (!editor_native::rebaseline(error)) {
            const auto waiting = "Ending shared editing is waiting for native history retirement: " + error;
            if (status != waiting) tell(waiting);
            return;
        }
        // Rebaselining retires callbacks in the same native editor. Remember
        // that generation so its preserved durable draft can be recovered here.
        native_generation = editor_native::state().generation;
    }
    const bool keep_offer = !transport_teardown && close_preserve_offer && C.session && C.session->connected();
    const auto saved = controller->saved_coverage();
    if (!keep_offer && !frozen && native_copy_owned && native.ready && native.editing &&
        editor_native::state().generation == native_generation && controller->view().phase == Phase::Ready &&
        !save_publication_pending && !pending_saved && saved.current && saved.expected &&
        saved.applied == saved.expected && saved.revision == current_native.document.revision &&
        controller->view().saved && controller->view().saved->resource == current_native.source && controller->settled()) {
        editor_native::Snapshot observed;
        std::string error;
        if (editor_native::capture(observed, error) && observed.source == current_native.source &&
            editor_native::validate(observed, current_native.document, error) && supported_between(current_native, observed, error)) {
            // Native resource envelopes omit projected captain metadata. Retain
            // the verified live metadata and canonical logical namespace in the
            // proof pin, without rewriting any native or durable resource.
            observed.document = current_native.document; reopen_saved = std::move(observed);
        } else if (H) H->log(M, SK_INFO, "Saved-draft reopen proof was not pinned: %s", error.c_str());
    }
    if (!keep_offer) controller->end(); pending.reset(); awaiting_operation.reset(); requested_scopes.clear(); guarded_placements.clear(); incoming_resources.reset();
    recovery.reset(); beginning = false; frozen = false; save_publication_pending = false;
    end_after_return = false; disconnected_return = false; pending_saved.reset();
    pending_preview_return.reset();
    adventure_messages.clear(); adventure_message_bytes = 0;
    store.close(); closing = false; native_attached = close_restored = close_preserve_offer = false;
    tell(keep_offer ? "The previous playtest ended. Save your draft and return to Galaxy to join the new editing offer." :
        "Shared editing ended. The private native draft and durable recovery files are preserved.");
}
bool return_ready(const editor::wire::Control& control) {
    using K = editor::wire::ControlKind;
    if (control.kind != K::HostReturnEditor && control.kind != K::HostCancelPlaytest) return true;
    if (!adventure_native::editor_preview()) return editor_native::state().ready;
    if (!return_error.empty()) return true;
    if (!owned_preview_context()) {
        return_error="The retained local view does not belong to the bound Test round";return true;
    }
    {
        const auto expected=preview_native_generation();
        if (!expected || editor_native::state().generation!=expected) {
            return_error="The native editor context changed during this Test round";return true;
        }
    }
    if (adventure::active() || adventure::outcome_locked()) {
        adventure::cancel("The party is returning to the adventure draft");
        return false;
    }
    if (adventure::cleanup_pending()) return false;
    if (!return_started) {
        if(!adventure_native::editor_preview_owner()) {
            return_error="The bound native editor owner changed before returning from Test";return true;
        }
        return_started = true;
        if (editor_native::state().playtesting && !editor_native_save::playtest(false, return_error)) return true;
        if (preview_selection) preview_selection->returned_generation=editor_native::state().generation;
    }
    if (!return_error.empty()) return true;
    if (!editor_native::state().ready) return false;
    // Camera activation can lag native Stop. Retry without acknowledging the
    // return or choosing a default/selected-marker camera in the meantime.
    editor_native::LocalView ready_view;std::string waiting;
    if(editor_native::capture_view(ready_view,waiting)) {
        if(preview_selection)preview_selection->view_wait_started=0;
        return true;
    }
    if(!owned_preview_context()) {
        return_error="The local camera pin does not belong to this Test round";return true;
    }
    if(!preview_selection->view_wait_started)preview_selection->view_wait_started=now_ms();
    if(now_ms()-preview_selection->view_wait_started>=5000) {
        return_error="The native editor camera did not become available after Test: "+waiting;return true;
    }
    if(status!=waiting)tell(waiting);
    return false;
}
bool finish_return(std::string& error, bool confirmed_group_return = false) {
    if (!return_error.empty()) { error = return_error; return false; }
    std::optional<editor::ObjectId> selected;
    std::optional<editor_native::LocalView> view;
    if (adventure_native::editor_preview()) {
        if (!owned_preview_context() || (confirmed_group_return && !same_preview_context(preview_selection->scope))) {
            error="The local camera pin does not belong to this Test round";return false;
        }
        if(!preview_selection->returned_generation || editor_native::state().generation!=preview_selection->returned_generation) {
            error="The native editor context changed before restoring this Test round";return false;
        }
        view=preview_selection->view;
    }
    if (view) selected=preview_selection->selected;
    const auto return_generation=editor_native::state().generation;
    if (!editor_native::restore(current_native, error)) return false;
    // Native Stop clears the editor selection. Preserve the independent local
    // choice made immediately before this round, not a committed snapshot's UI.
    if (selected && current_native.document.placements.contains(*selected) &&
        !editor_native::select_placement(*selected, error)) return false;
    // Selection can itself focus/adjust the native view. Restore the actual
    // pre-Test local camera last, including when no marker was selected.
    if (view && !editor_native::restore_view(*view,return_generation,error)) return false;
    preview_selection.reset();
    adventure::reset();
    adventure_native::clear_editor_preview();
    native_playtesting = false; return_started = false; return_error.clear();
    native_generation = editor_native::state().generation;
    status = "Returned to the committed adventure draft";
    return true;
}
void service_transport_teardown() {
    if (!transport_teardown) return;
    if (adventure_native::editor_preview()) {
        // A replacement native context is not the editor owned by this cleanup.
        // Still finish our GA lease, without stopping or restoring that context.
        const auto expected=owned_preview_context()?preview_native_generation():transport_teardown->native_generation;
        if (!teardown_scope_matches() || !owned_preview_context() || !expected || editor_native::state().generation != expected) {
            if (adventure::active() || adventure::outcome_locked()) { adventure::cancel("The editor party transport ended"); return; }
            if (adventure::cleanup_pending()) return;
            adventure::reset(); adventure_native::clear_editor_preview();preview_selection.reset();
            native_playtesting = return_started = false; return_error.clear();
        } else {
            disconnected_return = true;
            const bool ready=return_ready({editor::wire::ControlKind::HostReturnEditor, 1, 0, false, 0});
            // Only the synchronously authorized Stop may advance this cleanup
            // lease. Retain the original comparison while Stop has not run.
            if (return_started && owned_preview_context() && preview_selection->returned_generation)
                transport_teardown->native_generation=preview_selection->returned_generation;
            if(!ready)return;
            if (editor_native::state().generation != transport_teardown->native_generation) return;
            std::string error;
            if (!finish_return(error)) { frozen = true; tell(error); return; }
        }
    }
    close_session(false);
    if (closing) return;
    transport_teardown.reset(); observed_transport = C.session->transport_generation();
    auto offered = std::move(deferred_offer); deferred_offer.reset();
    if (offered && offered->transport == observed_transport && offered->transport > retired_transport) {
        DeferredOffer checked;
        if (authenticate_offer(0, offered->bytes.data(), offered->bytes.size(), checked) &&
            checked.context == offered->context && checked.host == offered->host && checked.assigned == offered->assigned) {
            net::Reader reader(offered->bytes.data(), offered->bytes.size()); controller->on_message(0, reader);
        }
    }
}
void finish_recovery() {
    if (closing || !recovery) return;
    const auto context = editor_native::state();
    if (!context.ready) return;
    std::string act_error;
    if (!supported_act(recovery->native.act, act_error) || !supported_native_act(act_error)) {
        recovery.reset(); tell(act_error); return;
    }
    if (!recovery->native_generation) recovery->native_generation = native_attached ? native_generation : context.generation;
    if (context.generation != recovery->native_generation) {
        recovery.reset(); frozen = true; tell("Recovery stopped because the native editor context changed"); return;
    }
    if (context.gesture || editor_native_save::busy()) return;
    ProjectionScope projection(recovery->state.document);
    std::string error;
    if (!recovery->restored) {
        if (!editor_native::restore(recovery->native, error)) { recovery.reset(); frozen = true; tell(error); return; }
        recovery->restored = true;
    }
    // A received resource is not a locally owned native draft. Do not rehost it
    // until Spore has created and independently read back a new Save As copy.
    if (recovery->needs_copy && !recovery->saved_key) {
        if (!recovery->saving) {
            recovery->saving = true;
            if (!editor_native_save::save_as(error)) { recovery.reset(); frozen = true; tell(error); }
            else status = "Save a new native copy before rehosting the recovered draft";
        }
        return;
    }
    if (recovery->saved_key) {
        editor_native::Snapshot native;
        auto content = resources;
        auto key = *recovery->saved_key;
        const dbpf::Key root{key.instance, key.type, key.group};
        std::vector<dbpf::Record> records;
        if (!editor_native::capture(native, error)) { recovery.reset(); frozen = true; tell(error); return; }
        native.source = key;
        if (!editor_native_save::read_saved(key, native.bytes, error) ||
            !editor_native::validate(native, recovery->state.document, error)) { recovery.reset(); frozen = true; tell(error); return; }
        if (!adventure_collect::collect_snapshot(root, native.bytes, records, error)) { recovery.reset(); frozen = true; tell(error); return; }
        auto normalized = std::find_if(records.begin(), records.end(), [&](const auto& record) { return record.key == root; });
        if (normalized == records.end()) { recovery.reset(); frozen = true; tell("The recovered editor root is missing"); return; }
        native.source = key; native.bytes = std::move(normalized->data);
        if (!replace_root(content, native, error) ||
            !editor_project::encode_resources(content, recovery->envelope, error) ||
            !editor_project::native_snapshot(content, recovery->state.document, native, error) ||
            !editor_native::restore(native, error)) {
            recovery.reset(); frozen = true; tell("The new native copy was saved; recovery could not be shared: " + error); return;
        }
        recovery->native = std::move(native); resources = std::move(content); native_copy_owned = true;
        recovery->saved_key.reset(); recovery->saving = recovery->needs_copy = false;
    }
    // Recovery creates a new logical epoch in this same native editor. Retire
    // queued captures and old native history before that epoch becomes active.
    if (!editor_native::rebaseline(error)) { status = "Waiting to retire native captures before recovery: " + error; return; }
    native_generation = editor_native::state().generation;
    auto work = std::move(*recovery); recovery.reset();
    controller->end(); frozen = false; pending.reset(); awaiting_operation.reset(); guarded_placements.clear();
    std::random_device random; work.state.document.epoch = (uint64_t(random()) << 32) | random();
    if (!work.state.document.epoch) work.state.document.epoch = 1;
    work.state.retired_sequences.clear(); sequence = 0;
    work.native.document = work.state.document;
    editor_store::Snapshot saved;
    if (!editor_project::store_snapshot(work.state, work.native, work.title, saved, error)) { frozen = true; tell(error); return; }
    saved.manifest = manifest(resource_hash, history_artifacts);
    if (!store.checkpoint(saved, error) || !controller->start(std::move(work.state), std::move(work.envelope), work.title, error, work.resume)) { frozen = true; tell(error); return; }
    local_capability = controller->view().local; saved.manifest = manifest(resource_hash, history_artifacts);
    if (!store.checkpoint(saved, error)) { frozen = true; tell(error); return; }
    current_native = std::move(work.native); native_generation = editor_native::state().generation;
    native_attached = true;
    tell("Recovered the last durable draft in a new editing session");
}
bool current_preview_return(const PreviewReturnScope& request) {
    if (!controller || !adventure_native::editor_preview()) return false;
    const auto& view = controller->view();
    const bool returning = view.phase == Phase::Preparing || view.phase == Phase::Playing || view.phase == Phase::Returning;
    return returning && same_preview_context(request);
}
bool current_preparation_cancel(const PreviewReturnScope& request, bool queued = false) {
    return controller && host() && !closing && !frozen && !recovery &&
        (controller->view().phase == Phase::Preparing || (queued && controller->view().phase == Phase::Playing)) &&
        same_preview_context(request);
}
void execute(Action action) {
    if (closing && action != Action::End) return;
    std::string error;
    switch (action) {
    case Action::Begin:
        reopen_saved.reset();
        if (controller->view().phase == Phase::Offered) {
            if (game::mode_id() != modes::kMenu) { tell("Save your local draft and return to Galaxy before joining shared editing"); break; }
            if (!controller->accept_offer(error)) tell(error);
        }
        else if (host() && !session_active() && editor_native::state().editing && !beginning && (!C.adventure_busy || !C.adventure_busy())) {
            if (!supported_native_act(error)) { tell(error); break; }
            beginning = true;
            if (!editor_native_save::save_as(error)) { beginning = false; tell(error); }
            else status = "Save a new native draft to start shared editing";
        }
        break;
    case Action::Undo: undo_redo(false); break;
    case Action::Redo: undo_redo(true); break;
    case Action::Save: {
        const auto native = editor_native::state();
        if (cached.can_save && controller->view().phase == Phase::Ready && !frozen && !recovery &&
            native.ready && !native.gesture && native.generation == native_generation && !editor_native_save::busy() &&
            check_native_integrity(error) && !(native_copy_owned ? editor_native_save::save(error) : editor_native_save::save_as(error))) tell(error);
        break;
    }
    case Action::Playtest:
        if (controller->view().phase == Phase::Playing) { if (host() && !controller->return_editor(error)) tell(error); }
        else if (cached.can_playtest && !pending && !save_publication_pending && !editor_native_save::busy() &&
                 editor_native::state().ready && !editor_native::state().gesture && check_native_integrity(error) && !controller->prepare_playtest(error)) tell(error);
        break;
    case Action::CancelPreparation:
        if (host() && (controller->view().phase == Phase::Preparing || controller->view().phase == Phase::Playing) &&
            !controller->cancel_playtest(error)) tell(error);
        break;
    case Action::Ready: case Action::Unready: if (!controller->ready_playtest(action == Action::Ready, error)) tell(error); break;
    case Action::End:
        if (!host() && controller->view().phase == Phase::Preparing && !adventure_native::editor_preview()) {
            // No GA attempt exists before HostBegin. Leave the editor directly;
            // its scoped Leave cancels the host's preparation and offers rejoin.
            close_session();
        } else if (adventure_native::editor_preview() || controller->view().phase == Phase::Preparing || controller->view().phase == Phase::Playing || controller->view().phase == Phase::Returning) {
            end_after_return = true;
            if (host() && controller->view().phase != Phase::Returning) { if (!controller->cancel_playtest(error)) tell(error); }
            else adventure::cancel("A collaborator ended the playtest");
        } else close_session();
        break;
    case Action::Return:
        if (!adventure_native::editor_preview()) break;
        if (host() && controller->view().phase == Phase::Playing) { if (!controller->return_editor(error)) tell(error); }
        else if (host() && controller->view().phase == Phase::Preparing) { if (!controller->cancel_playtest(error)) tell(error); }
        else if (!host() && !disconnected_return && (controller->view().phase == Phase::Playing || controller->view().phase == Phase::Preparing))
            tell("The host returns the party to the adventure draft");
        break;
    case Action::Recover: {
        reopen_saved.reset();
        if (!host() || !recovery_available) break;
        if (!store.healthy() && !store.open(project_root, project_id, error)) { tell(error); break; }
        auto checkpoint = store.snapshot();
        editor::State recovered; editor_native::Snapshot native;
        editor::Peer recovered_capability; bool owned = false;
        if (!editor_project::load_snapshot(checkpoint, recovered, native, error) ||
            !supported_act(native.act, error) || !supported_native_act(error) ||
            !parse_manifest(checkpoint.manifest, resource_hash, history_artifacts, error, &recovered_capability, &owned)) { tell(error); break; }
        std::vector<uint8_t> envelope;
        if (!read_blob(resource_hash, envelope, editor::wire::kResourceBytes, error) || !editor_project::decode_resources(envelope, resources, error) ||
            !replace_root(resources, native, error) || !editor_project::encode_resources(resources, envelope, error) ||
            !adventure_assets::register_records(resources.bundle.records, error)) { tell(error); break; }
        const auto resume = recovered_capability.slot == 0 ? std::optional<editor::Peer>(recovered_capability) : std::nullopt;
        if (!editor_native::state().editing && !editor_native::enter(native.source, error)) { tell(error); break; }
        native_copy_owned = owned;
        recovery = Recovery{std::move(recovered), std::move(native), std::move(envelope), checkpoint.metadata, resume, false, !owned, false, std::nullopt};
        status = "Loading the durable adventure draft"; finish_recovery(); break;
    }
    }
}
void SK_CDECL drain(void*) {
    queued = false;
    if (!controller) return;
    try {
        observe_transport();
        if (transport_teardown) { service_transport_teardown(); refresh(); return; }
        submit_pending_gesture();
        controller->tick(now_ms());
        observe_lease_grants();
        if (preview_selection && !adventure_native::editor_preview() && (!same_preview_context(preview_selection->scope) ||
            (controller->view().phase != Phase::Preparing && controller->view().phase != Phase::Playing &&
             controller->view().phase != Phase::Returning))) preview_selection.reset();
        while (!adventure_messages.empty()) {
            auto& message = adventure_messages.front(); const auto& v = controller->view();
            bool matching = message.context.project == v.context.project && message.context.epoch == v.context.epoch && message.round == v.playtest_round;
            if (matching && !adventure_native::editor_preview() && v.phase == Phase::Preparing) break;
            auto ready = std::move(message); adventure_messages.pop_front(); adventure_message_bytes -= ready.bytes.size();
            if (matching && adventure_native::editor_preview()) {
                net::Reader reader(ready.bytes.data(), ready.bytes.size()); adventure::on_message(ready.from, reader);
            }
        }
        const auto phase = controller->view().phase;
        if (!adventure_native::editor_preview() && native_attached && !session_active() && !closing) close_session(phase == Phase::Offered);
        if (adventure_native::editor_preview() && (phase == Phase::Idle || phase == Phase::Offered || phase == Phase::Disconnected || phase == Phase::Frozen)) disconnected_return = true;
        native_playtesting = editor_native::state().playtesting;
        if (disconnected_return && return_ready({editor::wire::ControlKind::HostReturnEditor, 1, 0, false, 0})) {
            std::string error;
            if (!finish_return(error)) { frozen = true; tell(error); }
            else { disconnected_return = false; close_session(controller->view().phase == Phase::Offered); }
        }
        if (end_after_return && controller->view().phase == Phase::Ready && !adventure_native::editor_preview()) { end_after_return = false; close_session(); }
        if (closing) close_session(close_preserve_offer);
        discover_recovery();
        finish_recovery();
        if (!host() && pending_saved && incoming_resources && controller->view().phase == Phase::Ready &&
            !closing && !frozen && editor_native::state().ready && !editor_native::state().gesture && !editor_native_save::busy() &&
            incoming_context.revision == controller->state().document.revision &&
            incoming_resources->root == dbpf::Key{pending_saved->resource.instance, pending_saved->resource.type, pending_saved->resource.group}) {
            std::string error;
            if (!checkpoint_resources(std::move(*incoming_resources), current_native, error) ||
                !controller->acknowledge_saved(*pending_saved, error)) { frozen = true; tell(error); }
            incoming_resources.reset(); pending_saved.reset();
        }
        if (gc_due && !closing && !pending && store.healthy()) {
            gc_due = false; std::string error;
            if (!garbage_collect(error)) { frozen = true; tell("Editor recovery maintenance stopped: " + error); }
        }
        auto work = std::move(actions); actions.clear();
        for (auto action : work) execute(action);
        auto cancelling = std::move(pending_preparation_cancel); pending_preparation_cancel.reset();
        if (cancelling && current_preparation_cancel(*cancelling, true)) execute(Action::CancelPreparation);
        auto returning = std::move(pending_preview_return); pending_preview_return.reset();
        if (returning && current_preview_return(*returning)) execute(Action::Return);
        if (session_active() && !closing && controller->view().phase == Phase::Ready && !frozen) {
            auto native = editor_native::state();
            if (!recovery && (!native.editing || native.generation != native_generation)) { frozen = true; tell("The native editor changed context. Recover or end shared editing."); }
            if (now_ms() - last_renew >= 4000) {
                last_renew = now_ms(); std::vector<editor::LeaseToken> tokens;
                for (const auto& item : controller->view().local_leases) tokens.push_back({item.scope, item.token});
                std::string error;
                if (!tokens.empty()) {
                    if (native.gesture || awaiting_operation || now_ms() - last_lease_use < 5000) controller->renew_leases(tokens, 15000, error);
                    else { controller->release_leases(tokens, error); requested_scopes.clear(); guarded_placements.clear(); }
                }
            }
            editor::wire::Presence presence;
            if (!native.selected.empty()) presence.selected = native.selected.front();
            presence.act = {(3ull << 32) | native.act, 1}; presence.editing = native.gesture;
            presence.pending = awaiting_operation.has_value();
            if (now_ms() - last_presence >= 300 || !last_presence_value || *last_presence_value != presence) {
                last_presence = now_ms(); std::string error;
                if(controller->set_presence(presence,error))last_presence_value=presence;
            }
            if (now_ms()-last_preview>=100) {
                last_preview=now_ms();editor::ObjectId object,act;editor::Pose pose;std::string error;
                if(!recovery && !awaiting_operation && native.generation==native_generation &&
                   editor_native::selection_pose(object,act,pose) && controller->state().document.placements.contains(object) &&
                   controller->set_preview(object,act,pose,error))preview_active=true;
                else if(preview_active){controller->clear_preview();preview_active=false;}
            }
        }
        else if(preview_active){controller->clear_preview();preview_active=false;last_presence_value.reset();}
        refresh();
    } catch (const std::exception& e) { frozen = true; tell(std::string("Shared editing stopped safely: ") + e.what()); refresh(); }
    // A local native failure must be visible to the host. Otherwise a connected
    // frozen replica can keep sending transport keepalives without ever covering
    // the accepted edit that a graceful host close is waiting to deliver.
    if (frozen && session_active() && controller->view().phase != Phase::Frozen) {
        controller->freeze_local(status); refresh();
    }
}
void schedule() { if (H && controller && !queued) { queued = true; H->run_outside_frame(drain, nullptr); } }
void request_preview_return(const PreviewReturnScope& request) {
    if (current_preview_return(request)) { pending_preview_return = request; schedule(); }
}
void action(Action value) {
    if (transport_teardown || (C.session && C.session->transport_generation() <= retired_transport)) return;
    if (value == Action::Return) {
        if (controller) {
            const auto& view = controller->view();
            request_preview_return({view.context.project, view.context.epoch, view.playtest_round, view.playtest_revision});
        }
    } else if (value == Action::CancelPreparation) {
        if (controller) {
            const auto& view = controller->view();
            PreviewReturnScope scope{view.context.project, view.context.epoch, view.playtest_round, view.playtest_revision};
            if (current_preparation_cancel(scope)) { pending_preparation_cancel = scope; schedule(); }
        }
    } else if (actions.size() < 16) {
        if (value == Action::End || value == Action::Recover) {
            if(!adventure_native::editor_preview())preview_selection.reset(); pending_preparation_cancel.reset();
        }
        actions.push_back(value); schedule();
    }
}
} // namespace

bool init(const SkHost* host_api, SkMod* mod, Ctx context) {
    H = host_api; M = mod; C = std::move(context); reopen_saved.reset(); projection_override.reset();
    native_playtesting=return_started=disconnected_return=end_after_return=false;return_error.clear();
    preview_selection.reset();pending_preview_return.reset();pending_preparation_cancel.reset();
    if (!C.session) return false;
    observed_transport = C.session->transport_generation(); retired_transport = 0; transport_teardown.reset(); deferred_offer.reset(); settled_cancellation.reset();
    editor_session::Callbacks callbacks;
    callbacks.trace=[](editor_session::TracePoint point,const editor::ProjectId& project,const editor::OperationId& operation,editor::Revision revision) noexcept {
        using P=editor_session::TracePoint;using S=editor_stage_metrics::Stage;
        const auto stage=point==P::HostRequestReceived?S::HostRequestReceived:point==P::HostDecisionQueued?S::HostDecisionQueued:S::DecisionDecoded;
        stage_stamp(project,operation,revision,stage);
    };
    callbacks.host_hooks.validate = [](const auto&, const auto& after, const auto& change, std::string& error) {
        StageScope span(after,change.operation,editor_stage_metrics::Stage::ValidateBegin,editor_stage_metrics::Stage::ValidateEnd);
        auto native = editor_native::state();
        if (closing || frozen || recovery || native.generation != native_generation || editor_native_save::busy() || save_publication_pending || !native.ready || native.gesture) { error = "The native draft is not currently editable"; return false; } span.okay=true;return true;
    };
    callbacks.host_hooks.apply = apply; callbacks.host_hooks.rollback = rollback; callbacks.host_hooks.persist = persist;
    callbacks.apply_committed = apply; callbacks.rollback_committed = rollback; callbacks.persist_guest = persist;
    callbacks.authoring_ready = [] {
        auto native = editor_native::state();
        return !closing && !frozen && !recovery && native.generation == native_generation && !save_publication_pending && !editor_native_save::busy() && native.ready && !native.gesture;
    };
    callbacks.prepare_join = [](editor::wire::Join& join,std::string& error) {
        if(!resources.root.instance || resources.bundle.records.empty())return true;
        editor_project::JoinInventory inventory;
        if(!editor_project::make_inventory(resources,inventory,error)){
            if(H)H->log(M,SK_INFO,"Ignoring unverified editor join cache: %s",error.c_str());
            error.clear();return true;
        }
        join.base_hash=inventory.base_hash;join.base_size=inventory.base_size;
        for(const auto& entry:inventory.records)join.records.push_back({{entry.key.instance,entry.key.type,entry.key.group},entry.size,entry.hash});
        return true;
    };
    callbacks.resources_for_join = [](const std::vector<uint8_t>& pinned,const editor::wire::Join& join,std::vector<uint8_t>& envelope,std::string& error) {
        editor_project::Resources content;
        if(!editor_project::decode_resources(pinned,content,error))return false;
        editor_project::JoinInventory inventory;inventory.base_hash=join.base_hash;inventory.base_size=join.base_size;
        for(const auto& entry:join.records)inventory.records.push_back({{entry.key.instance,entry.key.type,entry.key.group},entry.size,entry.hash});
        return editor_project::encode_join_resources(content,inventory,envelope,error);
    };
    callbacks.apply_resources = [](const std::vector<uint8_t>& bytes, const editor::wire::Context& scope, std::string& error) {
        editor_project::Resources content;
        const bool baseline = controller->view().phase == Phase::Joining || controller->view().phase == Phase::Syncing;
        if (baseline ? !editor_project::decode_join_resources(bytes,resources,content,error) :
            !editor_project::decode_resources(bytes,content,error,resources.base_root)) return false;
        if (!supported_act(content.act, error) || (baseline && !supported_native_act(error))) return false;
        if (baseline) {
            if (!adventure_assets::register_records(content.bundle.records, error)) return false;
            if (!editor_native::state().editing && !editor_native::enter({content.root.instance, content.root.type, content.root.group}, error)) return false;
        } else {
            editor_project::Resources merged;
            if (!merge_delta(std::move(content), merged, error)) return false;
            content = std::move(merged);
        }
        incoming_resources = std::move(content); incoming_context = scope; return true;
    };
    callbacks.baseline_ready = [](const editor::State&) { return editor_native::state().ready; };
    callbacks.apply_baseline = [](const editor::State& state, std::string& error) {
        ProjectionScope projection(state.document);
        reopen_saved.reset();
        if (!incoming_resources || incoming_context.project != state.document.project || incoming_context.epoch != state.document.epoch || incoming_context.revision != state.document.revision) {
            error = "The editor baseline has no matching verified resources"; return false;
        }
        editor_native::Snapshot native;
        if (!supported_act(incoming_resources->act, error) || !supported_native_act(error) ||
            !editor_project::native_snapshot(*incoming_resources, state.document, native, error) ||
            !adventure_assets::register_records(incoming_resources->bundle.records, error)) return false;
        if (!editor_native::state().editing && !editor_native::enter(native.source, error)) return false;
        if (!editor_native::restore(native, error) || !editor_native::apply(state.document, error)) return false;
        std::vector<uint8_t> envelope;
        if (!editor_project::encode_resources(*incoming_resources, envelope, error)) return false;
        store.close();
        local_capability = controller->view().local; native_copy_owned = false;
        if (!create_store(state, native, envelope, "Shared adventure draft", error)) return false;
        current_native = std::move(native); resources = std::move(*incoming_resources); incoming_resources.reset();
        native_attached = true;
        history_artifacts.clear(); sequence = 0; frozen = false; native_generation = editor_native::state().generation;
        tell("Joined the shared placement draft"); return true;
    };
    callbacks.decision = [](const editor::Decision& decision) {
        if(decision.accepted()&&decision.change&&awaiting_operation&&decision.change->operation==*awaiting_operation)
            stage_stamp(controller->state().document.project,decision.change->operation,decision.revision,editor_stage_metrics::Stage::OwnConfirmed);
        confirmation_metrics.complete(decision,H->time_seconds());
        const bool own = awaiting_operation && (!decision.change || decision.change->operation == *awaiting_operation);
        if (own) { pending.reset(); awaiting_operation.reset(); requested_scopes.clear(); }
        if (decision.accepted()) status = "Placement gesture committed";
        else tell(decision.reason);
        // Structural ownership lasts for the operation, not the whole session.
        std::vector<editor::LeaseToken> release;
        for (const auto& item : controller->view().local_leases) if (own) release.push_back({item.scope, item.token});
        std::string error;
        if (!release.empty()) controller->release_leases(release, error);
    };
    callbacks.error = [](const std::string& error) { tell(error); };
    callbacks.saved = [](const editor::wire::Saved& saved) {
        if (host()) { save_publication_pending = false; return; }
        pending_saved = saved;
        if (incoming_resources && incoming_context.revision == controller->state().document.revision &&
            incoming_resources->root == dbpf::Key{saved.resource.instance, saved.resource.type, saved.resource.group}) {
            std::string error;
            if (!checkpoint_resources(std::move(*incoming_resources), current_native, error) ||
                !controller->acknowledge_saved(saved, error)) throw std::runtime_error(error);
            incoming_resources.reset(); pending_saved.reset();
        }
    };
    callbacks.control_ready = return_ready;
    callbacks.control = [](const editor::wire::Control& control, std::string& error) {
        using K = editor::wire::ControlKind;
        if (control.kind == K::HostBeginPlaytest) {
            preview_selection.reset();
            if (!check_native_integrity(error)) return false;
            const auto context = controller->view().context;
            PreviewSelection local{{context.project, context.epoch, control.serial, control.revision}, {}, 0, native_generation, 0, std::nullopt};
            if (!editor_native::capture_view(local.view,error)) return false;
            const auto selected = editor_native::state().selected;
            if (selected.size() > 1) { error = "The native preview supports one local placement selection"; return false; }
            if (!selected.empty() && controller->state().document.placements.contains(selected.front())) local.selected = selected.front();
            adventure_native::EditorPreview preview;
            preview.project = context.project; preview.epoch = context.epoch; preview.round = control.serial; preview.revision = control.revision;
            preview.adventure = resources.root;
            auto avatar = editor_native::avatar(); preview.authored_avatar = {avatar.instance, avatar.type, avatar.group};
            preview.current = [context, round = control.serial, revision = control.revision] {
                const auto& current = controller->view();
                return current.context.project == context.project && current.context.epoch == context.epoch &&
                    current.playtest_round == round && current.playtest_revision == revision && controller->state().document.revision == revision;
            };
            preview.launch = [](std::string& failure) {
                bool okay = editor_native_save::playtest(true, failure);
                native_playtesting = okay; if (okay && C.close_panel) C.close_panel(); return okay;
            };
            preview.return_editor = [scope = PreviewReturnScope{context.project, context.epoch, control.serial, control.revision}] {
                request_preview_return(scope);
            };
            return_started = false; return_error.clear();
            const auto authored_avatar = preview.authored_avatar;
            if (!adventure_native::set_editor_preview(std::move(preview), error)) return false;
            preview_selection = std::move(local);
            if (host() && !adventure::request_play(resources.root, authored_avatar)) {
                error = adventure::status(); preview_selection.reset(); adventure_native::clear_editor_preview(); return false;
            }
        }
        if (control.kind == K::HostReturnEditor || control.kind == K::HostCancelPlaytest) {
            pending_preparation_cancel.reset();
            bool preparation_only = false;
            if (control.kind == K::HostCancelPlaytest && !adventure_native::editor_preview() &&
                !native_playtesting && !return_started && !adventure::active() &&
                !adventure::outcome_locked() && !adventure::cleanup_pending()) {
                const auto native = editor_native::state();
                if (native_attached && native.editing && native.ready && !native.playtesting && !native.gesture) {
                    if (!check_native_integrity(error)) return false;
                    preview_selection.reset();
                    status = "Playtest preparation cancelled. The adventure draft is unchanged";
                    preparation_only = true;
                }
            }
            if (!preparation_only && !finish_return(error, true)) return false;
        }
        if (C.open_panel) C.open_panel();
        return true;
    };
    controller = std::make_unique<editor_session::Controller>(*C.session, std::move(callbacks));
    editor_native::Ctx native;
    native.active = [] { return session_active() || recovery.has_value() || closing || native_attached; };
    native.relative_scale_tracked = [] {
        if (projection_override) return *projection_override;
        if (recovery) return recovery->state.document.schema.contains(editor_native::kRelativeScaleProperty);
        return !native_attached || current_native.document.schema.contains(editor_native::kRelativeScaleProperty);
    };
    native.gesture_scope = current_gesture_scope;
    native.cancelled = cancelled_gesture;
    native.gesture = captured;
    native.history = [](bool redo) { action(redo ? Action::Redo : Action::Undo); };
    native.history_available = [](bool redo) { return redo ? cached.can_redo : cached.can_undo; };
    native.failure = [](const std::string& error) { if (!transport_teardown) { frozen = true; tell(error); } };
    native.notice = [](const std::string& message) { if (!transport_teardown) tell(message); };
    native.gate = [](const std::vector<editor::ObjectId>& selection) {
        const auto state = editor_native::state();
        if (closing || !state.ready || state.generation != native_generation) return false;
        std::vector<editor::Scope> scopes;
        for (auto id : selection) scopes.push_back({editor::ScopeKind::Placement, id, {}});
        if (scopes.empty() || !lease(scopes)) return false;
        for (auto id : selection) {
            auto it = controller->state().document.placements.find(id);
            if (it == controller->state().document.placements.end()) return false;
            guarded_placements.try_emplace(id, it->second);
        }
        return true;
    };
    native.structure_gate = [](uint32_t) {
        const auto state = editor_native::state();
        return !closing && state.ready && state.generation == native_generation && lease({{editor::ScopeKind::Document, {}, {}}});
    };
    bool available = editor_native::init(H, M, std::move(native));
    editor_native_save::Ctx saving;
    saving.shared_active = [] { return session_active() || recovery.has_value() || beginning || closing || native_attached; };
    saving.saved = [](editor::ResourceKey key) {
        if (transport_teardown || (native_attached && editor_native::state().generation != native_generation)) return;
        if (recovery && recovery->saving) { recovery->saved_key = key; return; }
        if (beginning) { start_saved(); return; }
        saved_native(key);
    };
    saving.cancelled = [] { if (transport_teardown) return; beginning = false; if (recovery && recovery->saving) { recovery.reset(); frozen = true; } status = "Native save cancelled; the draft is preserved"; };
    saving.error = [](const std::string& error) { if (transport_teardown) return; beginning = false; if (recovery && recovery->saving) { recovery.reset(); frozen = true; } tell(error); };
    saving.owned_preview = adventure_native::editor_preview;
    saving.return_requested = [] { action(Action::Return); };
    editor_native_save::init(H, M, std::move(saving));
    refresh(); return available;
}
void tick(double) { editor_native::tick(); editor_native_save::tick(); schedule(); }
void on_message(uint8_t from, net::Reader& r) {
    observe_transport();
    if (transport_teardown) {
        DeferredOffer offer;
        if (C.session->transport_generation() > retired_transport &&
            (!deferred_offer || deferred_offer->transport < C.session->transport_generation()) &&
            authenticate_offer(from, r.cur(), r.left(), offer)) deferred_offer = std::move(offer);
        r.skip(r.left());
    } else if (controller && C.session && C.session->connected() && C.session->transport_generation() > retired_transport) controller->on_message(from, r);
    else r.skip(r.left());
    schedule();
}
void on_adventure_message(uint8_t from, net::Reader& r) {
    const size_t size = r.left();
    observe_transport();
    if (transport_teardown || !controller || !session_active()) { r.skip(size); return; }
    const auto& v = controller->view();
    if (v.phase != Phase::Preparing && v.phase != Phase::Playing && v.phase != Phase::Returning) { r.skip(size); return; }
    if (size > net::kMaxPacket || adventure_messages.size() >= 512 || size > (1u << 20) - adventure_message_bytes) {
        r.skip(size); frozen = true; tell("Editor playtest messages exceeded the bounded preparation queue"); action(Action::End); return;
    }
    AdventureMessage message{v.context, v.playtest_round, from, {r.cur(), r.cur() + size}};
    r.skip(size); adventure_message_bytes += size; adventure_messages.push_back(std::move(message)); schedule();
}
void on_join(uint8_t id) { observe_transport(); if (controller && !transport_teardown) controller->on_join(id); schedule(); }
void on_leave(uint8_t id) { observe_transport(); if (controller && !transport_teardown) controller->on_leave(id); schedule(); }
void session_ended() {
    retire_transport(true); schedule();
}
void mode_changed(uint32_t, uint32_t) { schedule(); }
void shutdown() { native_playtesting=return_started=disconnected_return=end_after_return=false;return_error.clear();settled_cancellation.reset(); transport_teardown.reset(); deferred_offer.reset(); observed_transport = retired_transport = 0; reopen_saved.reset(); projection_override.reset(); if (controller) { controller->end(); controller.reset(); } store.close(); adventure_native::clear_editor_preview(); editor_native_save::shutdown(); editor_native::shutdown(); actions.clear(); pending_preview_return.reset(); pending_preparation_cancel.reset(); preview_selection.reset(); observed_lease_grants.clear(); queued = false; closing = native_attached = close_restored = close_preserve_offer = false; H = nullptr; }
bool active() { return cached.active; }
bool authoring() {
    if (!controller || (controller->view().phase == Phase::Playing && native_playtesting)) return false;
    return session_active() || native_attached || closing || recovery.has_value() || beginning;
}
bool playtest_owned() { return adventure_native::editor_preview(); }
View view() { return cached; }
const std::vector<TransformHint>& transform_hints() { return cached.previews; }
void begin() { action(Action::Begin); }
void undo() { action(Action::Undo); }
void redo() { action(Action::Redo); }
void save() { action(Action::Save); }
void playtest() { action(Action::Playtest); }
void cancel_playtest() { action(Action::CancelPreparation); }
void set_ready(bool value) { action(value ? Action::Ready : Action::Unready); }
void recover() { action(Action::Recover); }
void end() { action(Action::End); }
std::string debug(const std::vector<std::string>& args) {
    if (args.size() == 1 && args[0] == "accounting") {
        if (!controller) return "Editor accounting unavailable";
        char text[8192]; size_t used = 0;
        auto add = [&](const char* name, size_t value) {
            if (used + 80 >= sizeof text) return;
            const int count = snprintf(text + used, sizeof text - used, "%s=%llu\n", name, static_cast<unsigned long long>(value));
            if (count > 0 && size_t(count) < sizeof text - used) used += size_t(count);
        };
        const auto a = controller->accounting();
        add("history", a.history);
        add("decisions", a.decisions);
        add("decision_order", a.decision_order);
        add("state_estimate", a.state_estimate);
        add("coordinator_history", a.coordinator_history);
        add("coordinator_decisions", a.coordinator_decisions);
        add("coordinator_estimate", a.coordinator_estimate);
        add("replica_history", a.replica_history);
        add("replica_decisions", a.replica_decisions);
        add("replica_estimate", a.replica_estimate);
        add("tombstones", a.tombstones);
        add("retired_sequences", a.retired_sequences);
        add("truncated_authors", a.truncated_authors);
        add("leases", a.leases);
        add("local_leases", a.local_leases);
        add("channels", a.channels);
        add("hashes", a.hashes);
        add("completed_transfers", a.completed_transfers);
        add("lease_decisions", a.lease_decisions);
        add("released_scope_fences", a.released_scope_fences);
        add("lease_reply_capacity", a.lease_reply_capacity);
        add("completed_hash_capacity", a.completed_hash_capacity);
        add("revision_hash_capacity", a.revision_hash_capacity);
        add("input_count", a.input_count);
        add("input_bytes", a.input_bytes);
        add("local_count", a.local_count);
        add("local_bytes", a.local_bytes);
        add("queued_packets", a.queued_packets);
        add("queued_transfers", a.queued_transfers);
        add("output_bytes", a.output_bytes);
        add("transfer_reference_size", a.transfer_reference_size);
        add("transfer_reference_capacity", a.transfer_reference_capacity);
        add("pending_leases", a.pending_leases);
        add("release_fences", a.release_fences);
        add("lease_retirements", a.lease_retirements);
        add("lease_stamps", a.lease_stamps);
        add("deferred_count", a.deferred_count);
        add("deferred_bytes", a.deferred_bytes);
        add("held_count", a.held_count);
        add("held_bytes", a.held_bytes);
        add("resource_size", a.resource_size);
        add("resource_capacity", a.resource_capacity);
        add("baseline_size", a.baseline_size);
        add("baseline_capacity", a.baseline_capacity);
        add("staged_size", a.staged_size);
        add("staged_capacity", a.staged_capacity);
        add("receiving", a.receiving);
        add("receiver_data_size", a.receiver_data_size);
        add("receiver_data_capacity", a.receiver_data_capacity);
        add("receiver_chunk_capacity", a.receiver_chunk_capacity);
        add("transport_connections", a.transport.connections);
        add("transport_sent_packets", a.transport.sent_packets);
        add("transport_sent_id_capacity", a.transport.sent_id_capacity);
        add("transport_reliable_out", a.transport.reliable_out);
        add("transport_reliable_in", a.transport.reliable_in);
        add("transport_unreliable_out", a.transport.unreliable_out);
        add("transport_unreliable_slots_capacity", a.transport.unreliable_slots_capacity);
        add("transport_payload_size", a.transport.payload_size);
        add("transport_payload_capacity", a.transport.payload_capacity);
        add("transport_packet_history_limit_per_connection", 1024);
        add("history_limit", editor::Limits{}.history); add("decision_limit", editor::Limits{}.decisions);
        add("hash_limit", editor_session::Config{}.catchup_decisions + 16); add("lease_reply_limit_per_channel", 64);
        add("native_current_size", current_native.bytes.size()); add("native_current_capacity", current_native.bytes.capacity());
        add("native_before_size", before_apply.bytes.size()); add("native_before_capacity", before_apply.bytes.capacity());
        add("native_before_valid", before_apply_valid ? 1 : 0);
        add("native_reopen_capacity", reopen_saved ? reopen_saved->bytes.capacity() : 0);
        add("artifact_count", history_artifacts.size()); add("resource_records", resources.bundle.records.size());
        size_t resource_size = 0, resource_capacity = 0;
        for (const auto& record : resources.bundle.records) { resource_size += record.data.size(); resource_capacity += record.data.capacity(); }
        add("record_data_size", resource_size); add("record_data_capacity", resource_capacity);
        add("immutable_base_capacity", resources.base_root ? resources.base_root->capacity() : 0);
        const auto& snapshot = store.snapshot();
        add("store_native_size", snapshot.native_snapshot.size()); add("store_native_capacity", snapshot.native_snapshot.capacity());
        add("store_document_size", snapshot.document.size()); add("store_document_capacity", snapshot.document.capacity());
        add("store_mapping_capacity", snapshot.id_mapping.capacity()); add("store_manifest_capacity", snapshot.manifest.capacity());
        add("recovery_native_capacity", recovery ? recovery->native.bytes.capacity() : 0);
        add("recovery_envelope_capacity", recovery ? recovery->envelope.capacity() : 0);
        add("adventure_queue_count", adventure_messages.size()); add("adventure_queue_bytes", adventure_message_bytes);
        add("pending_native_capacity", pending ? pending->gesture.before.bytes.capacity() + pending->gesture.after.bytes.capacity() : 0);
        add("actions", actions.size()); add("guarded_placements", guarded_placements.size());
        add("observed_grants", observed_lease_grants.size()); add("requested_scopes", requested_scopes.size());
        return std::string("Passive editor container counts/capacities; shared references can alias; estimates are not allocator or process bytes.\n") + std::string(text, used);
    }
    if(!args.empty()&&args[0]=="stages") {
        if(args.size()==2&&args[1]=="reset") {
            LARGE_INTEGER frequency{};
            if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0||!stage_metrics.reset(GetCurrentProcessId(),uint64_t(frequency.QuadPart),stage_qpc()))return "Stage diagnostics clock unavailable";
            return stage_metrics.report(0,0);
        }
        if(args.size()==2&&args[1]=="stop"){stage_metrics.stop();return stage_metrics.report(0,0);}
        if(args.size()>=2&&args[1]=="read"&&args.size()<=4){
            auto number=[](const std::string& value,size_t limit,size_t& result){if(value.empty()||value.size()>5)return false;size_t n=0;for(char c:value){if(c<'0'||c>'9')return false;n=n*10+size_t(c-'0');if(n>limit)return false;}result=n;return true;};
            size_t offset=0,count=32;
            if((args.size()>2&&!number(args[2],4096,offset))||(args.size()>3&&!number(args[3],32,count)))return "Usage: coop.editor stages read [offset0..4096] [count0..32]";
            return stage_metrics.report(offset,count);
        }
        return "Usage: coop.editor stages reset|stop|read [offset] [count]";
    }
    if (args.size() == 1 && args[0] == "verify-reopen") {
        const auto native = editor_native::state();
        if (!controller || controller->view().phase != Phase::Idle || session_active() || native_attached || closing ||
            beginning || recovery || frozen || pending || awaiting_operation || pending_saved ||
            editor_native_save::busy() || adventure_native::editor_preview() || adventure::cleanup_pending() ||
            !native.editing || !native.ready || native.playtesting || native.gesture)
            return "REOPEN_REJECTED: Verification requires a detached idle native editor";
        if (!reopen_saved) return "REOPEN_REJECTED: End a fully acknowledged native save at the latest canonical revision first";
        ProjectionScope projection(reopen_saved->document);
        editor_native::Snapshot observed, mapped;
        std::string error;
        if (!editor_native::capture(observed, error) ||
            !editor_reopen_probe::remap_reopened(*reopen_saved, observed, mapped, error) ||
            !editor_native::validate(mapped, reopen_saved->document, error) || !supported_between(*reopen_saved, mapped, error))
            return "REOPEN_REJECTED: " + error;
        char source[40];
        snprintf(source, sizeof source, "%08X!%08X.%08X", mapped.source.group, mapped.source.instance, mapped.source.type);
        return "REOPEN_VERIFIED revision=" + std::to_string(reopen_saved->document.revision) + " source=" + source;
    }
    if (controller && args.size() == 1 && args[0] == "leases") {
        const auto& view = controller->view();
        auto object = [](editor::ObjectId id) { return std::to_string(id.id) + ":" + std::to_string(id.generation); };
        auto scope = [&](const editor::Scope& value) {
            static const char* names[] = {"Document", "Class", "Act", "Placement", "Field"};
            const auto kind = size_t(value.kind);
            return std::string(kind < std::size(names) ? names[kind] : "Unknown") + " object=" + object(value.object);
        };
        std::string result = "local=" + std::to_string(view.local.slot) + ":" + std::to_string(view.local.generation) +
            " phase=" + std::to_string(uint8_t(view.phase)) + " revision=" + std::to_string(view.context.revision) + "\n";
        auto append = [&](const std::string& line) {
            if (result.size() + line.size() + 16 > 12u * 1024) return false;
            result += line; return true;
        };
        const auto now = now_ms();
        for (const auto& lease : view.local_leases) {
            if (!append("lease scope=" + scope(lease.scope) + " token=" + std::to_string(lease.token) +
                " remaining_ms=" + std::to_string(lease.expires > now ? lease.expires - now : 0) + "\n")) {
                result += "truncated=1\n"; return result;
            }
        }
        for (const auto& [id, member] : view.members) {
            (void)id;
            if (!append("member peer=" + std::to_string(member.peer.slot) + ":" + std::to_string(member.peer.generation) +
                " phase=" + std::to_string(uint8_t(member.phase)) + " selected=" + object(member.presence.selected) +
                " ownership=" + std::to_string(member.presence.ownership) + " pending=" + std::to_string(member.presence.pending) +
                " preview_scope=" + scope(member.presence.preview_lease.scope) +
                " preview_token=" + std::to_string(member.presence.preview_lease.token) + "\n")) {
                result += "truncated=1\n"; break;
            }
        }
        return result;
    }
    if (!args.empty() && args[0]=="timing") {
        if(args.size()==2 && args[1]=="reset")confirmation_metrics.reset();
        else if(args.size()==2 && args[1]=="stop")confirmation_metrics.stop();
        else if(args.size()!=1)return "Usage: coop.editor timing [reset|stop]";
        return confirmation_metrics.report();
    }
    if (controller && !args.empty() && args[0] == "history") {
        const auto local = controller->view().local;
        std::string result = "local=" + std::to_string(local.slot) + ":" + std::to_string(local.generation) +
            " count=" + std::to_string(controller->state().history.size()) + "\n";
        for (const auto& gesture : controller->state().history)
            result += "author=" + std::to_string(gesture.operation.author.slot) + ":" + std::to_string(gesture.operation.author.generation) +
                " sequence=" + std::to_string(gesture.operation.sequence) + " epoch=" + std::to_string(gesture.operation.epoch) +
                " revision=" + std::to_string(gesture.original_revision) + " applied=" + std::to_string(gesture.applied) +
                " redo=" + std::to_string(gesture.redo_available) + " " + gesture.summary + "\n";
        return result;
    }
    if (controller && args.size() > 1 && args[0] == "inspect") {
        const std::string usage = "Usage: coop.editor inspect bytes OFFSET COUNT(1..4096)";
        auto number = [](const std::string& value, size_t limit, size_t& result) {
            if (value.empty() || value.size() > 9) return false;
            size_t parsed = 0;
            for (char c : value) {
                if (c < '0' || c > '9') return false;
                parsed = parsed * 10 + size_t(c - '0');
                if (parsed > limit) return false;
            }
            result = parsed; return true;
        };
        size_t offset = 0, count = 0;
        if (args.size() != 4 || args[1] != "bytes" || !number(args[2], 128u << 20, offset) ||
            !number(args[3], 4096, count) || !count) return usage;
        editor_native::Snapshot native;
        std::string error;
        if (!editor_native::capture(native, error)) return error;
        if (offset > native.bytes.size()) return "NATIVE_BYTES_REJECTED: Offset exceeds the captured resource";
        count = std::min(count, native.bytes.size() - offset);
        std::string result = "NATIVE_BYTES size=" + std::to_string(native.bytes.size()) +
            " offset=" + std::to_string(offset) + " count=" + std::to_string(count) +
            " sha256=" + upd::sha256_hex(native.bytes) + "\nhex=";
        static constexpr char digits[] = "0123456789abcdef";
        result.reserve(result.size() + count * 2 + 1);
        for (size_t i = offset; i < offset + count; ++i) {
            result += digits[native.bytes[i] >> 4]; result += digits[native.bytes[i] & 15];
        }
        return result + "\n";
    }
    if (controller && !args.empty() && (args[0] == "objects" || args[0] == "check" || args[0] == "roundtrip" || args[0] == "inspect")) {
        std::string error;
        editor_native::Snapshot native;
        if (!editor_native::capture(native, error)) return error;
        if (args[0] == "inspect") {
            // Native projection has no collaboration identity. Fix only this
            // copied checksum namespace so standalone and shared inspection can
            // both serialize it; the native resource remains untouched.
            auto projection = native.document;
            projection.project = {}; projection.project[0] = 1;
            projection.epoch = 1; projection.revision = 0;
            std::string semantic;
            if (!editor_session::document_hash(projection, semantic, error)) return "Native projection inspection failed: " + error;
            char source[40];
            snprintf(source, sizeof source, "%08X!%08X.%08X", native.source.group, native.source.instance, native.source.type);
            return "placements=" + std::to_string(native.document.placements.size()) +
                " classes=" + std::to_string(native.document.classes.size()) + " acts=" + std::to_string(native.document.acts.size()) +
                " act=" + std::to_string(native.act) + " bytes=" + std::to_string(native.bytes.size()) + " source=" + source +
                "\nnative_sha256=" + upd::sha256_hex(native.bytes) + "\ndocument_semantic_sha256=" + semantic;
        }
        if (args[0] == "roundtrip") {
            auto before_hash = upd::sha256_hex(native.bytes);
            if (session_active()) native.document = controller->state().document;
            if (!editor_native::restore(native, error) || !editor_native::capture(native, error)) return error;
            return "roundtrip=" + std::string(before_hash == upd::sha256_hex(native.bytes) ? "identical" : "changed") + " bytes=" + std::to_string(native.bytes.size());
        }
        if (args[0] == "check") {
            if (editor_native::state().generation != native_generation) return "DIVERGED: The native editor belongs to a different context";
            if (!native_integrity(native, controller->state().document, error)) return "DIVERGED: " + error;
            std::string hash;
            if (!editor_session::document_hash(controller->state().document, hash, error)) return error;
            return "CONVERGED " + hash;
        }
        std::string result;
        for (const auto& [id, p] : native.document.placements) {
            float xy[2]{};
            const bool projected = H->world_to_screen && H->world_to_screen(&p.position.x, xy) && std::isfinite(xy[0]) && std::isfinite(xy[1]);
            std::string relative;
            auto property = p.properties.find(editor_native::kRelativeScaleProperty);
            if (native.document.schema.contains(editor_native::kRelativeScaleProperty) && property != p.properties.end()) {
                if (const auto* value = std::get_if<float>(&property->second)) {
                    char text[48]; snprintf(text, sizeof text, " relative_scale=%.9g", double(*value)); relative = text;
                }
            }
            result += std::to_string(id.id) + ":" + std::to_string(id.generation) + " class=" + std::to_string(p.class_id.id) +
                " pos=" + std::to_string(p.position.x) + "," + std::to_string(p.position.y) + "," + std::to_string(p.position.z) +
                " scale=" + std::to_string(p.scale) + (projected ? " screen=" + std::to_string(xy[0]) + "," + std::to_string(xy[1]) : " screen=hidden") +
                " rotation=" + std::to_string(p.rotation.x) + "," + std::to_string(p.rotation.y) + "," + std::to_string(p.rotation.z) + "," + std::to_string(p.rotation.w) + relative + "\n";
            if (result.size() > 12000) break;
        }
        return result.empty() ? "No placements" : result;
    }
    if (!args.empty()) {
        if (args[0] == "begin" || args[0] == "join") begin();
        else if (args[0] == "undo") undo();
        else if (args[0] == "redo") redo();
        else if (args[0] == "save") save();
        else if (args[0] == "playtest") playtest();
        else if (args[0] == "cancel") cancel_playtest();
        else if (args[0] == "ready") set_ready(args.size() < 2 || args[1] != "0");
        else if (args[0] == "recover") recover();
        else if (args[0] == "end") end();
        else if (args[0] == "return") action(Action::Return);
    }
    refresh();
    const auto saved = controller ? controller->saved_coverage() : editor_session::SavedCoverage{};
    const auto saved_root = controller && controller->view().saved ? controller->view().saved->resource : editor::ResourceKey{};
    char saved_source_root[32];
    snprintf(saved_source_root, sizeof saved_source_root, "%08X!%08X.%08X", saved_root.group, saved_root.instance, saved_root.type);
    return cached.title + "\n" + cached.status + "\nrevision=" + std::to_string(cached.revision) +
        " durable=" + std::to_string(cached.durable_revision) + " frozen=" + std::to_string(cached.frozen) +
        " saved_revision=" + std::to_string(saved.revision) + " saved_applied=" + std::to_string(saved.applied) +
        " saved_expected=" + std::to_string(saved.expected) + " settled=" + std::to_string(controller && controller->settled()) +
        " saved_source_root=" + std::string(saved_source_root) +
        " phase=" + std::to_string(controller ? uint8_t(controller->view().phase) : 0) +
        " project=" + (controller ? editor_store::project_name(controller->view().context.project) : "") +
        " capability=" + std::to_string(controller ? controller->view().local.slot : 255) + ":" + std::to_string(controller ? controller->view().local.generation : 0) +
        "\nselection=" + cached.selection + " previews=" + std::to_string(cached.previews.size()) +
        "\n" + editor_native::status() + "\n" + adventure_assets::status();
}
} // namespace coop_editor
