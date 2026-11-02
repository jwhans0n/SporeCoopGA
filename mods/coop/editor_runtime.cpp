#include "editor_runtime.h"

#include <windows.h>
#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <random>
#include <set>
#include <stdexcept>
#include <utility>
#include <tuple>

#include "adventure_assets.h"
#include "adventure_collect.h"
#include "adventure_native.h"
#include "editor_native.h"
#include "editor_native_save.h"
#include "editor_creation.h"
#include "editor_creation_native.h"
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
void edit_acts(int);
namespace {
namespace fs = std::filesystem;
using Phase = editor_session::Phase;
const SkHost* H;
SkMod* M;
Ctx C;
std::unique_ptr<editor_session::Controller> controller;
editor::ObjectId logical_act(uint32_t index){
    if(controller && index<controller->state().document.act_order.size())return controller->state().document.act_order[index];
    return {(3ull<<32)|index,1};
}
uint32_t native_act(editor::ObjectId id){
    if(controller && !controller->state().document.act_order.empty()){
        const auto& order=controller->state().document.act_order;
        return uint32_t(std::find(order.begin(),order.end(),id)-order.begin());
    }
    return uint32_t(id.id);
}
editor_store::Store store;
editor_native::Snapshot current_native, before_apply;
std::optional<editor_native::Snapshot> prepared_native;
editor::Revision rendered_revision=0;
struct LocalCreation {
    editor_creation::Scope scope;
    editor_creation::Request request;
    editor_native::LocalView view;
    editor_native::SceneOwner scene_owner;
    editor_native::Snapshot entry_native;
    std::optional<editor::ObjectId> selected;
    editor::ObjectId authored_act;
    uint32_t act=0;
    uint64_t entry_generation=0,returned_generation=0;
    bool submitted=false,materialized=false;
    editor::Revision content_revision=0;
    std::optional<editor::OperationId> operation;
    std::optional<bool> accepted;
    std::string error;
    std::optional<editor::Revision> scene_revision;
    uint64_t scene_frame=0;
    uint64_t resume_started=0;
    bool entered_adventure=false,camera_resumed=false;
};
std::optional<LocalCreation> local_creation;
bool detached_scene() { return local_creation.has_value(); }
std::optional<editor_native::Snapshot> reopen_saved;
std::optional<bool> projection_override;
std::optional<bool> extended_projection_override,cast_projection_override;
std::optional<bool> authoring_projection_override;
struct ProjectionScope {
    std::optional<bool> before = projection_override;
    std::optional<bool> before_extended=extended_projection_override,before_cast=cast_projection_override;
    std::optional<bool> before_authoring=authoring_projection_override;
    explicit ProjectionScope(const editor::Document& document) {
        projection_override = document.schema.contains(editor_native::kRelativeScaleProperty);
        extended_projection_override=document.schema.contains(editor_native::kElevationProperty);
        cast_projection_override=!document.class_schema.empty();
        authoring_projection_override=!document.settings_schema.empty();
    }
    ~ProjectionScope() { projection_override = before;extended_projection_override=before_extended;cast_projection_override=before_cast;authoring_projection_override=before_authoring; }
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
bool native_attached, close_restored, close_preserve_offer, native_identity_lost;
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
std::map<editor::Scope,uint64_t> lease_use;
editor::wire::Context observed_lease_context;
editor::Peer observed_lease_peer;
std::set<std::pair<editor::Scope, uint64_t>> observed_lease_grants;
uint64_t last_preview;
uint64_t native_release_qpc=0;
uint64_t native_capture_begin_qpc=0,native_capture_end_qpc=0;
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
    if (act<8) return true;
    error = "The native Adventure Editor supports up to eight acts.";
    return false;
}
bool supported_native_act(std::string& error) {
    const auto native = editor_native::state();
    return !native.editing || supported_act(native.act, error);
}
editor::Peer local_capability;
std::map<editor::ObjectId, editor::Placement> guarded_placements;
std::map<editor::ObjectId, editor::ClassActSettings> guarded_settings;
std::map<editor::ObjectId, editor::AuthoringSettings> guarded_authoring;
std::map<editor::ObjectId, editor::TerrainRecord> guarded_terrain;
std::map<editor::OperationId,editor_project::Resources> cast_uploads;
std::map<editor::OperationId,std::string> cast_upload_errors;
std::string status, resource_hash;
editor::ResourceKey source_creation;
uint64_t automatic_generation=0, automatic_transport=0;
uint64_t retained_editor_generation=0;
uint64_t native_exit_generation=0;
bool follow_failed=false, native_exit_requested=false, native_exit_confirmed=false;
bool exit_after_save=false, exit_broadcast=false, remote_exit=false, handoff=false;
bool exit_after_return=false;
using Command=editor::wire::Command;
using CommandKind=editor::wire::CommandKind;
std::map<editor::Peer,Command> queued_exports;
struct ExportBarrier {
    Command command;
    uint8_t participants=0, held=0;
    bool acknowledged=false, started=false, finished=false;
};
std::optional<ExportBarrier> exporting;
std::optional<editor_native::Snapshot> export_before;
uint64_t export_generation=0, command_serial=0;
std::array<uint64_t,4> seen_commands{};
std::array<editor::Peer,4> seen_command_peers{};
std::optional<Command> test_request;
uint64_t pending_export_request=0,pending_test_request=0;
uint64_t handoff_prompt_serial=0;
bool handoff_saving=false;
struct Artifact { std::string before, after; };
struct ResourceKeyLess {
    bool operator()(const dbpf::Key& a,const dbpf::Key& b)const{return std::tie(a.group,a.type,a.instance)<std::tie(b.group,b.type,b.instance);}
};
using ResourceKeys=std::set<dbpf::Key,ResourceKeyLess>;
bool parse_manifest(const std::vector<uint8_t>&, std::string&, std::map<editor::OperationId, Artifact>&, std::string&, editor::Peer* = nullptr, bool* = nullptr);
std::map<editor::OperationId, Artifact> history_artifacts;
struct DurableCommit {
    editor::State state;
    editor::Change change;
    editor_native::Snapshot native;
    std::optional<editor_native::Snapshot> before;
    editor_project::Resources resources;
    std::map<editor::OperationId,Artifact> archive;
    std::string hash,error,title,maintenance_error;
    std::vector<uint8_t> envelope,delta;
    ResourceKeys retained_keys;
    editor_store::Snapshot saved;
    std::optional<editor_session::PreparedResources> publication;
    std::future<bool> task;
    bool flushing=false,hosting=false;
};
std::unique_ptr<DurableCommit> durable_commit;
struct Pending { editor::Request request; editor_native::Gesture gesture; uint64_t reverted_frame = 0; bool submitted = false; std::vector<uint8_t> assets={}; bool tentative=false; };
std::optional<Pending> pending;
std::optional<editor::OperationId> awaiting_operation;
struct RollbackFence {
    uint64_t generation=0;
    editor::ResourceKey source;
    std::vector<editor_native::Binding> bindings;
    bool exact_bindings=false;
};
std::optional<RollbackFence> rollback_fence;
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
enum class Action { Begin, Undo, Redo, Save, Playtest, Solo, Ready, Unready, Recover, End, Return, CancelPreparation, NativeExit, SaveExit, ApproveTest, DeclineTest, HandoffSave, HandoffDiscard, HandoffCancel, ChooseTest, MenuSolo, MenuTogether, MenuDismiss };
struct DeferredTest {Action action;editor_native::GestureScope scope;};
std::optional<DeferredTest> deferred_test;
std::optional<Command> queued_solo;
bool creation_sessions_active(){
    if(local_creation)return true;
    return controller && std::any_of(controller->view().members.begin(),controller->view().members.end(),[](const auto& member){return member.second.presence.creating_asset;});
}
struct TestMenu { editor_native::GestureScope scope;uint64_t generation=0;editor::Revision revision=0;uint32_t act=0; };
std::optional<TestMenu> test_menu;
struct WaitingActs { TestMenu identity;int action=0;uint64_t started=0; };
std::optional<WaitingActs> waiting_acts;
std::deque<Action> actions;
struct OpenCreation {uint32_t type=0;std::optional<editor::ObjectId> target;};
std::deque<OpenCreation> creation_actions;
std::optional<bool> complexity_request;
struct WaitingHistory {
    editor_native::GestureScope scope;
    uint64_t generation=0,started=0;
    editor::OperationId gesture;
    editor::Revision transition=0;
    bool redo=false;
};
std::optional<WaitingHistory> waiting_history;
struct PreviewReturnScope {
    editor::ProjectId project;
    uint64_t epoch = 0, round = 0;
    editor::Revision revision = 0;
};
std::optional<PreviewReturnScope> pending_preview_return;
std::optional<PreviewReturnScope> pending_preparation_cancel;
std::optional<PreviewReturnScope> pending_solo_return;
struct PreviewSelection {
    PreviewReturnScope scope;
    editor_native::LocalView view;
    uint64_t view_wait_started = 0;
    uint64_t native_owner_generation = 0, returned_generation = 0;
    std::optional<editor::ObjectId> selected;
    uint32_t act=0;
    bool scene_restored=false;
    uint64_t scene_frame=0;
};
std::optional<PreviewSelection> preview_selection;
struct SoloPreview {
    PreviewReturnScope scope;
    editor_native::Snapshot snapshot;
    editor_native::LocalView view;
    std::optional<editor::ObjectId> selected;
    uint64_t generation=0,returned_frame=0,view_wait_started=0;
    bool scene_restored=false;
};
std::optional<SoloPreview> solo_preview;
std::optional<PreviewReturnScope> solo_ready_scope;
uint64_t pending_solo_request=0;
std::vector<editor::Scope> requested_scopes;

// Packet dispatch only retires cached ownership. Native restoration waits for
// the outside-frame drain, including when explicit Leave precedes role change.
void retire_transport(bool explicit_stop) {
    waiting_history.reset();
    if (!controller || !C.session) return;
    if (!transport_teardown) {
        transport_teardown = TransportTeardown{observed_transport, native_generation,
            controller->view().context, controller->view().local};
        if (!closing) { closing = true; close_restored = false; }
    }
    retired_transport = std::max(retired_transport, explicit_stop ? C.session->transport_generation() : observed_transport);
    if (deferred_offer && deferred_offer->transport <= retired_transport) deferred_offer.reset();
    close_preserve_offer = false; reopen_saved.reset();
    actions.clear(); if(!pending || !pending->tentative)pending.reset(); awaiting_operation.reset();
    queued_exports.clear();queued_solo.reset();deferred_test.reset();creation_actions.clear();complexity_request.reset();
    pending_preview_return.reset(); pending_preparation_cancel.reset();
    requested_scopes.clear(); guarded_placements.clear(); observed_lease_grants.clear();
    adventure_messages.clear(); adventure_message_bytes = 0;
    beginning = false; recovery.reset(); pending_saved.reset(); incoming_resources.reset();
    exporting.reset();test_request.reset();pending_export_request=pending_test_request=0;
    native_exit_requested=native_exit_confirmed=exit_after_save=exit_broadcast=remote_exit=handoff=handoff_saving=false;
    exit_after_return=false;
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
bool register_native_records(const dbpf::Key& root,const std::vector<dbpf::Record>& records,std::string& error){
    return adventure_collect::validate_uploaded_records(root,records,error) && adventure_assets::register_records(records,error);
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
    if(native.metadata)content.metadata=native.metadata;
    return true;
}
bool root_delta(const editor_project::Resources& content, std::vector<uint8_t>& bytes, std::string& error) {
    return editor_project::encode_delta_resources(content, bytes, error);
}
bool changed_resources(const editor_project::Resources& content,const ResourceKeys& retained,std::vector<uint8_t>& bytes,std::string& error){
    auto changed=content;
    std::erase_if(changed.bundle.records,[&](const auto& row){return row.key!=changed.root && retained.contains(row.key);});
    // Existing peers already own the immutable closure. Behavior, text, crew
    // capacity and placement edits need only the root delta. New dependencies
    // travel once with the operation that admitted them.
    return changed.bundle.records.size()==1?editor_project::encode_delta_resources(changed,bytes,error):editor_project::encode_resources(changed,bytes,error);
}
bool merge_dependencies(editor_project::Resources& target,const editor_project::Resources& source,std::string& error){
    if(source.root!=target.root){error="Cast content belongs to another shared draft";return false;}
    auto records=target.bundle.records;
    for(const auto& record:source.bundle.records){
        if(record.key==source.root)continue;
        auto existing=std::find_if(records.begin(),records.end(),[&](const auto& item){return item.key==record.key;});
        if(existing!=records.end()){
            if(existing->data!=record.data){error="An imported asset conflicts with an existing resource key";return false;}
        }else records.push_back(record);
    }
    net::adventure_content::Bundle bundle;
    if(!net::adventure_content::make_bundle(target.root,std::move(records),bundle,error))return false;
    target.bundle=std::move(bundle);return true;
}
bool collect_cast_assets(const editor_native::Snapshot& native,std::vector<uint8_t>& envelope,std::string& error){
    std::vector<dbpf::Record> records;
    if(!adventure_collect::collect_snapshot(resources.root,native.bytes,records,error))return false;
    editor_project::Resources collected=resources;
    if(!net::adventure_content::make_bundle(resources.root,std::move(records),collected.bundle,error))return false;
    auto content=resources;
    if(!merge_dependencies(content,collected,error))return false;
    auto snapshot=native;snapshot.source={resources.root.instance,resources.root.type,resources.root.group};
    if(!replace_root(content,snapshot,error))return false;
    std::erase_if(content.bundle.records,[&](const auto& record){return record.key!=content.root && std::any_of(resources.bundle.records.begin(),resources.bundle.records.end(),[&](const auto& old){return old.key==record.key;});});
    net::adventure_content::Bundle delta;
    if(!net::adventure_content::make_bundle(content.root,std::move(content.bundle.records),delta,error))return false;
    content.bundle=std::move(delta);content.base_root.reset();
    return editor_project::encode_resources(content,envelope,error);
}
bool canonicalize_crew_assets(editor_native::Gesture& gesture,std::string& error){
    const editor::FieldKey field{editor::Field::Property,editor_native::kCrewRosterProperty};
    if(editor::value(gesture.before.document,editor_native::kCrewObject,field)==
       editor::value(gesture.after.document,editor_native::kCrewObject,field))return true;
    if(!controller || !C.session || !C.session->connected()){
        error="Crew content versioning has no active shared authority";return false;
    }
    const auto context=controller->view().context;
    const auto author=controller->view().local;
    if(context.project==editor::ProjectId{} || !context.epoch || controller->view().phase!=Phase::Ready ||
       context.project!=controller->state().document.project || context.epoch!=controller->state().document.epoch ||
       !author.generation || author.slot!=C.session->my_id() || C.session->transport_generation()!=observed_transport){
        error="Crew content versioning has no current canonical context";return false;
    }
    std::vector<editor::ResourceKey> before,after;
    if(!editor_native::crew_assets(gesture.before.document,before,error) ||
       !editor_native::crew_assets(gesture.after.document,after,error))return false;
    std::map<editor::ResourceKey,editor::ResourceKey> versions;
    std::set<editor::ResourceKey> examined;
    std::vector<dbpf::Record> admitted;
    const auto reserved=[&](const editor::ResourceKey& key){
        const dbpf::Key native_key{key.instance,key.type,key.group};
        return editor_creation_native::resource_reserved(key) ||
            std::any_of(resources.bundle.records.begin(),resources.bundle.records.end(),[&](const auto& row){return row.key==native_key;}) ||
            std::any_of(admitted.begin(),admitted.end(),[&](const auto& row){return row.key==native_key;}) ||
            std::any_of(versions.begin(),versions.end(),[&](const auto& row){return row.second==key;});
    };
    for(const auto& key:after){
        // Removal/reordering and a lock or capacity edit keep the exact
        // already-admitted identities, including duplicate crew members.
        if(std::find(before.begin(),before.end(),key)!=before.end() || !examined.insert(key).second)continue;
        if(!key.instance || !key.type){error="The selected crew member has an incomplete resource identity";return false;}
        std::vector<dbpf::Record> source,records;editor::ResourceKey version;
        if(!adventure_collect::collect_creation({key.instance,key.type,key.group},source,error))return false;
        if(source.empty())continue; // Verified stock resources stay native.
        // Native projections carry authored values, not the session identity.
        if(!editor_creation_native::immutable_version(context.project,key,source,reserved,version,records,error))return false;
        for(auto& record:records){
            auto prior=std::find_if(admitted.begin(),admitted.end(),[&](const auto& row){return row.key==record.key;});
            if(prior!=admitted.end()){
                if(prior->data!=record.data){error="The selected crew assets have conflicting dependencies";return false;}
            }else admitted.push_back(std::move(record));
        }
        versions.emplace(key,version);
    }
    if(versions.empty())return true;
    std::vector<editor_native::CrewAssetRemap> edits;
    for(uint32_t slot=0;slot<after.size();++slot)if(auto version=versions.find(after[slot]);version!=versions.end())
        edits.push_back({slot,after[slot],version->second});
    // Admission may populate immutable resource caches, but never the live
    // draft or history. Only the checked private snapshot receives aliases.
    if(!register_native_records(resources.root,admitted,error))return false;
    editor_native::Snapshot normalized;
    if(!editor_native::remap_crew_assets(gesture.after,edits,normalized,error))return false;
    gesture.after=std::move(normalized);return true;
}
bool merge_delta(editor_project::Resources delta, editor_project::Resources& merged, std::string& error) {
    auto root=std::find_if(delta.bundle.records.begin(),delta.bundle.records.end(),[&](const auto& row){return row.key==delta.root;});
    if(root==delta.bundle.records.end()){error="The editor update has no native root";return false;}
    editor_native::Snapshot native;
    native.source = {delta.root.instance, delta.root.type, delta.root.group};
    native.act = delta.act; native.bindings = std::move(delta.bindings); native.bytes = root->data;
    native.metadata=std::move(delta.metadata);
    auto candidate = resources;
    if (!replace_root(candidate, native, error) || !merge_dependencies(candidate,delta,error)) return false;
    merged = std::move(candidate); return true;
}
std::vector<uint8_t> manifest(const std::string& assets, const std::map<editor::OperationId, Artifact>& history) {
    net::Writer w; w.u32(0x33484445); w.str8(assets); w.u8(local_capability.slot); w.u32(local_capability.generation); w.u8(native_copy_owned);
    w.u32(source_creation.instance); w.u32(source_creation.type); w.u32(source_creation.group); w.u32(uint32_t(history.size()));
    for (const auto& [operation, artifact] : history) {
        w.u8(operation.author.slot); w.u32(operation.author.generation); u64(w, operation.sequence); u64(w, operation.epoch);
        w.str8(artifact.before); w.str8(artifact.after);
    }
    return std::move(w.data);
}
bool parse_manifest(const std::vector<uint8_t>& bytes, std::string& assets, std::map<editor::OperationId, Artifact>& history, std::string& error, editor::Peer* local, bool* owned) {
    net::Reader r(bytes.data(), bytes.size());
    const auto magic=r.u32();
    if (magic != 0x32484445 && magic != 0x33484445) { error = "Unsupported editor recovery manifest"; return false; }
    auto hash = r.str8(); editor::Peer capability{r.u8(), r.u32()}; uint8_t copy_owned = r.u8();
    editor::ResourceKey provenance;
    if(magic==0x33484445)provenance={r.u32(),r.u32(),r.u32()};
    uint32_t count = r.u32();
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
    assets = std::move(hash); history = std::move(result); if (local) *local = capability;
    if (owned) { *owned = copy_owned; source_creation=provenance; } return true;
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
bool structural(const editor::Change& change) { return !change.patch.objects.empty() || change.patch.acts.has_value(); }
bool expand_legacy_state(editor::State& state,editor_native::Snapshot& native,std::string& error){
    auto candidate=state;
    if(!editor_native::canonicalize_cast_state(candidate,error))return false;
    if(candidate.document.act_order.empty() && !candidate.document.settings_schema.empty()){
        for(auto act:candidate.document.acts)candidate.document.act_order.push_back(act);
    }
    if(state.document.schema.contains(editor_native::kElevationProperty) && state.document.schema.contains(editor_native::kSoundPitchProperty) &&
       candidate.document.class_schema==editor_native::cast_property_schema() &&
       candidate.document.settings_schema==editor_native::authoring_property_schema() &&
       candidate.document.settings.contains(editor_native::kCrewObject) &&
       candidate.document.settings.contains(editor_native_terrain::kPlanetObject) &&
       candidate.document.settings.contains(editor_native::kProjectObject)){
        if(!editor_native::validate(native,candidate.document,error))return false;
        state=std::move(candidate);native.document=state.document;return true;
    }
    const auto old_schema=state.document.schema;
    const bool relative=old_schema.contains(editor_native::kRelativeScaleProperty);
    editor_native::Snapshot expanded;
    if(!editor_native::expand_snapshot(native,relative,expanded,error) || expanded.document.classes!=candidate.document.classes || expanded.document.acts!=candidate.document.acts)return false;
    auto fill=[&](editor::Placement& placement,const editor_native::Snapshot& source)->bool{
        auto found=source.document.placements.find(placement.object);
        if(found==source.document.placements.end() || found->second.class_id!=placement.class_id || found->second.position!=placement.position ||
           found->second.rotation!=placement.rotation || found->second.scale!=placement.scale)return false;
        for(const auto& [key,value]:found->second.properties)if(!old_schema.contains(key))placement.properties[key]=value;
        return true;
    };
    for(auto& [id,placement]:candidate.document.placements){(void)id;if(!fill(placement,expanded)){error="The legacy checkpoint has mismatched native placements";return false;}}
    for(auto& gesture:candidate.history){
        if(gesture.undo.objects.empty() && gesture.redo.objects.empty())continue;
        auto artifact=history_artifacts.find(gesture.operation);
        if(artifact==history_artifacts.end()){error="Legacy structural history has no owned native artifact";return false;}
        editor_native::Snapshot prior,next,prior_expanded,next_expanded;
        if(!load_artifact(artifact->second.before,prior,error) || !load_artifact(artifact->second.after,next,error) ||
           !editor_native::expand_snapshot(prior,relative,prior_expanded,error) || !editor_native::expand_snapshot(next,relative,next_expanded,error))return false;
        for(auto* patch:{&gesture.undo,&gesture.redo})for(auto& object:patch->objects)for(auto* placement:{&object.before,&object.after})if(*placement){
            auto copy=**placement;
            if(!fill(copy,prior_expanded) && !fill(copy,next_expanded)){error="Legacy Undo cannot be matched to its exact native placement artifact";return false;}
            **placement=std::move(copy);
        }
    }
    candidate.document.schema=expanded.document.schema;
    for(const auto& [property,spec]:candidate.document.settings_schema){
        auto found=expanded.document.settings_schema.find(property);
        if(found==expanded.document.settings_schema.end() || found->second!=spec){error="The recovery settings schema conflicts with its verified native snapshot";return false;}
    }
    for(const auto& [id,settings]:expanded.document.settings){
        auto prior=candidate.document.settings.find(id);
        if(prior==candidate.document.settings.end())candidate.document.settings.emplace(id,settings);
        else{
            if(prior->second.act_id!=settings.act_id || prior->second.kind!=settings.kind){error="The recovery settings identity differs from its owned native snapshot";return false;}
            for(const auto& [key,value]:prior->second.properties){
                auto found=settings.properties.find(key);
                if(found==settings.properties.end() || found->second!=value){error="The recovery settings differ from their verified native snapshot";return false;}
            }
            for(const auto& [key,value]:settings.properties)prior->second.properties.try_emplace(key,value);
        }
    }
    candidate.document.settings_schema=expanded.document.settings_schema;
    if(candidate.document.terrain.empty()){
        candidate.document.terrain=expanded.document.terrain;candidate.document.next_terrain_order=expanded.document.next_terrain_order;
    }else if(candidate.document.terrain!=expanded.document.terrain){error="The recovery terrain differs from its verified native recipe";return false;}
    if(candidate.document.act_order.empty())candidate.document.act_order=expanded.document.act_order;
    if(candidate.document.class_schema.empty()){
        candidate.document.class_schema=expanded.document.class_schema;
        candidate.document.class_settings=expanded.document.class_settings;
    }else{
        if(candidate.document.class_schema!=expanded.document.class_schema || candidate.document.class_settings.size()!=expanded.document.class_settings.size()){
            error="The recovery cast schema differs from its owned native snapshot";return false;
        }
        for(const auto& [id,settings]:candidate.document.class_settings){
            auto found=expanded.document.class_settings.find(id);
            if(found==expanded.document.class_settings.end() || settings.class_id!=found->second.class_id || settings.act_id!=found->second.act_id || settings.properties!=found->second.properties){
                error="The recovery cast data differs from its owned native snapshot";return false;
            }
        }
        // An additive placement upgrade must retain existing cast revisions
        // and Undo preconditions, including classes with no placed instances.
    }
    for(const auto& reference:expanded.document.external_references)
        if(expanded.document.classes.contains(reference.target) &&
           std::find(candidate.document.external_references.begin(),candidate.document.external_references.end(),reference)==candidate.document.external_references.end())
            candidate.document.external_references.push_back(reference);
    candidate.document.next_class=expanded.document.next_class;
    candidate.document.next_id=std::max(candidate.document.next_id,expanded.document.next_id);
    editor::Coordinator checked;
    if(!checked.reset(candidate,candidate.document.epoch,error))return false;
    expanded.document=candidate.document;
    state=std::move(candidate);native=std::move(expanded);return true;
}
bool cast_change(const editor::Change& change){
    return change.patch.acts.has_value() || !change.patch.classes.empty() || std::any_of(change.patch.fields.begin(),change.patch.fields.end(),[](const auto& field){return field.field==editor::FieldKey{editor::Field::Property,editor_native::kCastBlockProperty} ||
        field.field==editor::FieldKey{editor::Field::Property,editor::kClassAssetResourceProperty} ||
        (field.object==editor_native::kCrewObject && field.field==editor::FieldKey{editor::Field::Property,editor_native::kCrewRosterProperty}) ||
        (field.object==editor_native::kCaptainObject && field.field==editor::FieldKey{editor::Field::Property,editor_native::kCaptainAssetProperty});});
}
bool same_native_authored(const editor_native::Snapshot& before,const editor_native::Snapshot& after){
    return before.act==after.act && before.bindings==after.bindings && before.bytes==after.bytes &&
        before.captain==after.captain && before.captain_locked==after.captain_locked && before.metadata==after.metadata;
}
bool supported_between(const editor_native::Snapshot& before, const editor_native::Snapshot& after, std::string& error) {
    // Equal owned bytes and authored sidecars cannot hide an opaque mutation.
    // Still verify their typed projection against the caller's expected draft.
    if(same_native_authored(before,after))return editor_native::validate(after,before.document,error);
    editor_native::Gesture check; check.generation = native_generation; check.before = before; check.after = after;
    return editor_native::supported_changes(check, error);
}
bool stop_unsupported(std::string& error) {
    guarded_placements.clear();guarded_settings.clear();guarded_authoring.clear();guarded_terrain.clear();
    std::string restore_error;
    if (!editor_native::restore(current_native, restore_error)){frozen=true;error += "; canonical restoration failed: " + restore_error;}
    else {
        std::vector<editor::LeaseToken> tokens;
        for(const auto& lease:controller->view().local_leases)tokens.push_back({lease.scope,lease.token});
        if(!tokens.empty())controller->release_leases(tokens,restore_error);
        requested_scopes.clear();
    }
    tell(std::string(frozen?"Recover the committed draft: ":"The edit was rejected and the committed draft restored: ")+error);
    return false;
}
bool native_integrity(const editor_native::Snapshot& snapshot, const editor::Document& document, std::string& error) {
    // Validate against the supplied projection even when native bytes match:
    // captain metadata and the logical wanted document are separate inputs.
    if (!editor_native::validate(snapshot, document, error)) return false;
    if (same_native_authored(current_native,snapshot)) return true;
    return supported_between(current_native, snapshot, error);
}
bool capture_recent(const editor_native::Snapshot& expected,const editor::Document& wanted,
                    editor_native::Snapshot& out,std::string& error){
    const auto matched=editor_native::recent_matches(expected,wanted,native_generation,out,error);
    if(matched==editor_native::RecentMatches::Match)return true;
    if(matched==editor_native::RecentMatches::Error){
        native_identity_lost=true;frozen=true;
        tell("Recover the committed draft: "+error);return false;
    }
    return editor_native::capture(out,error);
}
bool check_native_integrity(std::string& error) {
    if(detached_scene())return editor_native::validate(current_native,controller->state().document,error);
    if (editor_native::state().generation != native_generation) {
        frozen = true; error = "The native editor belongs to a different context"; tell(error); return false;
    }
    editor_native::Snapshot snapshot;
    if (!capture_recent(current_native,controller->state().document,snapshot,error))return false;
    if (!native_integrity(snapshot, controller->state().document, error)) return stop_unsupported(error);
    return true;
}
bool remember_artifacts(const editor::State& state, const editor::Change& change, const editor_native::Snapshot& before,const editor_native::Snapshot& after,
                        std::map<editor::OperationId, Artifact>& archive, std::string& error) {
    if (structural(change) && change.intent == editor::Intent::Forward) {
        std::vector<std::vector<uint8_t>> bytes(2); Artifact entry;
        if (!artifact_bytes(before, bytes[0], error) || !artifact_bytes(after, bytes[1], error) ||
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
    if(!prepared_native || prepared_native->document!=state.document){error="The canonical native commit was not prepared";return false;}
    auto after=*prepared_native;
    after.document = state.document;
    after.source = {resources.root.instance, resources.root.type, resources.root.group};
    auto archive = history_artifacts;
    if (!remember_artifacts(state, change, before_apply,after, archive, error)) return false;
    auto next_resources = resources;
    if(cast_change(change)){
        auto uploaded=cast_uploads.find(change.operation);
        const auto* extra=host() && uploaded!=cast_uploads.end()?&uploaded->second:incoming_resources?&*incoming_resources:nullptr;
        if(extra && !merge_dependencies(next_resources,*extra,error))return false;
    }
    std::vector<uint8_t> envelope, delta;
    if (!replace_root(next_resources, after, error) || !editor_project::encode_resources(next_resources, envelope, error) ||
        (structural(change) && !root_delta(next_resources, delta, error))) return false;
    auto next_hash=resource_hash;
    if(next_resources.bundle.records.size()!=resources.bundle.records.size() && (!reserve_artifacts({&envelope},error) || !put_blob(envelope,next_hash,error)))return false;
    if(cast_change(change)){
        ResourceKeys retained;for(const auto& row:resources.bundle.records)retained.insert(row.key);
        if(!changed_resources(next_resources,retained,delta,error))return false;
    }
    if (host() && !controller->update_resources(std::move(envelope), state.document.revision, error, structural(change)||cast_change(change), std::move(delta))) return false;
    editor_store::Snapshot saved;
    if (!editor_project::store_snapshot(state, after, controller->view().name, saved, error)) return false;
    // The immutable dependency closure is retained once. Current scenario bytes
    // and stable marker bindings are already part of the durable snapshot.
    saved.manifest = manifest(next_hash, archive);
    bool committed=false;
    { StageScope durable(state.document,change.operation,editor_stage_metrics::Stage::DurableBegin,editor_stage_metrics::Stage::DurableEnd);
      committed=store.commit(saved,error);durable.okay=committed; }
    if (!committed) {
        if (!store.healthy()) throw std::runtime_error("Editor disk acceptance is uncertain; reopen the recovery checkpoint: " + error);
        return false;
    }
    // All allocations and publication payloads were staged before durable I/O.
    current_native = std::move(after); resources = std::move(next_resources);
    prepared_native.reset();
    if(!detached_scene())rendered_revision=state.document.revision;
    resource_hash=std::move(next_hash);cast_uploads.erase(change.operation);
    history_artifacts.swap(archive);
    incoming_resources.reset(); gc_due = state.document.revision % 16 == 0;
    work.okay=true;return true;
}
bool begin_persist_owned(const editor::State& state,const editor::Change& change,std::string& error){
    if(durable_commit || !prepared_native || prepared_native->document!=state.document){error="The canonical commit is not ready for durable preparation";return false;}
    stage_stamp(state.document.project,change.operation,state.document.revision,editor_stage_metrics::Stage::PersistWorkBegin);
    auto work=std::make_unique<DurableCommit>();work->state=state;work->change=change;
    work->native=*prepared_native;work->native.source={resources.root.instance,resources.root.type,resources.root.group};
    work->resources=resources;work->archive=history_artifacts;work->hash=resource_hash;work->title=controller->view().name;
    for(const auto& row:resources.bundle.records)work->retained_keys.insert(row.key);
    // Native objects have already been serialized and released on the game
    // thread. Only owned containers are handed to the worker below.
    if(structural(change) && change.intent==editor::Intent::Forward)work->before=before_apply;
    if(cast_change(change)){
        auto uploaded=cast_uploads.find(change.operation);
        const auto* extra=host()&&uploaded!=cast_uploads.end()?&uploaded->second:incoming_resources?&*incoming_resources:nullptr;
        if(extra && !merge_dependencies(work->resources,*extra,error))return false;
    }
    work->hosting=host();const auto old_record_count=resources.bundle.records.size();
    // Manifest inputs cannot depend on a live editor or participant context.
    const auto capability=local_capability;const auto copy_owned=native_copy_owned;const auto provenance=source_creation;
    auto* job=work.get();
    job->task=std::async(std::launch::async,[job,old_record_count,capability,copy_owned,provenance]{
        try{
            if(!remember_artifacts(job->state,job->change,job->before?*job->before:job->native,job->native,job->archive,job->error))return false;
            if(!replace_root(job->resources,job->native,job->error) ||
               !editor_project::encode_resources(job->resources,job->envelope,job->error) ||
               (structural(job->change)&&!root_delta(job->resources,job->delta,job->error)))return false;
            if(job->resources.bundle.records.size()!=old_record_count && !put_blob(job->envelope,job->hash,job->error))return false;
            if(cast_change(job->change) && !changed_resources(job->resources,job->retained_keys,job->delta,job->error))return false;
            if(!editor_project::store_snapshot(job->state,job->native,job->title,job->saved,job->error))return false;
            net::Writer w;w.u32(0x33484445);w.str8(job->hash);w.u8(capability.slot);w.u32(capability.generation);w.u8(copy_owned);
            w.u32(provenance.instance);w.u32(provenance.type);w.u32(provenance.group);w.u32(uint32_t(job->archive.size()));
            for(const auto& [operation,artifact]:job->archive){w.u8(operation.author.slot);w.u32(operation.author.generation);u64(w,operation.sequence);u64(w,operation.epoch);w.str8(artifact.before);w.str8(artifact.after);}
            job->saved.manifest=std::move(w.data);
            if(job->hosting){
                job->publication=editor_session::prepare_resources(std::move(job->envelope),std::move(job->delta),structural(job->change)||cast_change(job->change),job->error);
                if(!job->publication)return false;
            }
            return true;
        }catch(const std::exception& e){job->error=e.what();return false;}
    });
    durable_commit=std::move(work);return true;
}
bool begin_persist(const editor::State& state,const editor::Change& change,std::string& error){
    try{return begin_persist_owned(state,change,error);}
    catch(const std::exception& e){error=e.what();return false;}
    catch(...){error="Preparing the durable editor worker failed";return false;}
}
editor_session::Durability poll_persist_owned(std::string& error){
    using Result=editor_session::Durability;
    if(!durable_commit){error="No durable operation is pending";return Result::Uncertain;}
    auto& job=*durable_commit;
    if(job.task.wait_for(std::chrono::seconds(0))!=std::future_status::ready)return Result::Pending;
    bool okay=false;
    try{okay=job.task.get();}catch(const std::exception& e){job.error=e.what();}
    if(!okay){
        error=job.error;const bool uncertain=job.flushing&&(!store.healthy() || store.durable_revision()>=job.state.document.revision);
        stage_stamp(job.state.document.project,job.change.operation,job.state.document.revision,editor_stage_metrics::Stage::PersistWorkEnd,2);
        durable_commit.reset();return uncertain?Result::Uncertain:Result::Rejected;
    }
    if(!job.flushing){
        if(transport_teardown || !controller || controller->view().context.project!=job.state.document.project ||
           controller->view().context.epoch!=job.state.document.epoch){error="The prepared commit's participant context expired";durable_commit.reset();return Result::Rejected;}
        // Allocate and validate publication before the irreversible durable
        // append. update_resources only stages bytes until finish_prepared.
        if(job.hosting&&(!job.publication || !controller->update_resources(std::move(*job.publication),job.state.document.revision,error))){durable_commit.reset();return Result::Rejected;}
        job.flushing=true;auto* work=&job;
        stage_stamp(job.state.document.project,job.change.operation,job.state.document.revision,editor_stage_metrics::Stage::DurableBegin);
        job.task=std::async(std::launch::async,[work]{
            try{
                if(!store.commit(work->saved,work->error))return false;
                if(work->state.document.revision%16==0 && !garbage_collect(work->maintenance_error)){
                    if(!store.healthy()){work->error=work->maintenance_error;return false;}
                }
                return true;
            }catch(const std::exception& e){work->error=e.what();return false;}
        });
        return Result::Pending;
    }
    stage_stamp(job.state.document.project,job.change.operation,job.state.document.revision,editor_stage_metrics::Stage::DurableEnd,1);
    current_native=std::move(job.native);resources=std::move(job.resources);resource_hash=std::move(job.hash);
    history_artifacts=std::move(job.archive);cast_uploads.erase(job.change.operation);incoming_resources.reset();prepared_native.reset();
    if(!detached_scene())rendered_revision=job.state.document.revision;
    gc_due=false;
    if(!job.maintenance_error.empty()){frozen=true;tell("Editor recovery maintenance stopped: "+job.maintenance_error);}
    stage_stamp(job.state.document.project,job.change.operation,job.state.document.revision,editor_stage_metrics::Stage::PersistWorkEnd,1);
    durable_commit.reset();return Result::Durable;
}
editor_session::Durability poll_persist(std::string& error){
    try{return poll_persist_owned(error);}
    catch(const std::exception& e){error=e.what();}
    catch(...){error="The durable editor worker failed";}
    bool uncertain=false;
    if(durable_commit){
        auto& work=*durable_commit;
        if(work.task.valid())try{work.task.wait();(void)work.task.get();}catch(...){}
        uncertain=work.flushing && (!store.healthy() || store.durable_revision()>=work.state.document.revision);
        durable_commit.reset();
    }
    return uncertain?editor_session::Durability::Uncertain:editor_session::Durability::Rejected;
}
editor_session::Durability settle_persist(std::string& error){
    // Only explicit teardown calls this join. Every native object was released
    // before the worker started; the game thread owns final publication.
    while(durable_commit){
        if(durable_commit->task.valid())durable_commit->task.wait();
        const auto result=poll_persist(error);
        if(result!=editor_session::Durability::Pending)return result;
    }
    error="The durable operation disappeared during teardown";return editor_session::Durability::Uncertain;
}
bool tentative_integrity(const editor_native::Snapshot& live,const editor::Document& document,std::string& error) {
    if(!pending || live.source!=pending->gesture.after.source || live.bindings!=pending->gesture.after.bindings){
        native_identity_lost=true;
        error="The tentative gesture's native identity changed";return false;
    }
    if(!editor_native::validate(live,document,error))return false;
    if(same_native_authored(pending->gesture.after,live))return true;
    return supported_between(pending->gesture.after,live,error);
}
bool restore_tentative(std::string& error) {
    if(!pending || !pending->tentative)return true;
    const auto native=editor_native::state();
    if(!controller || detached_scene() || native.generation!=native_generation ||
       pending->request.project!=controller->view().context.project ||
       pending->request.operation.epoch!=controller->view().context.epoch ||
       pending->request.operation.author!=controller->view().local ||
       pending->gesture.generation!=native_generation){
        native_identity_lost=true;
        error="The tentative gesture no longer has its ready native editor";return false;
    }
    if(!native.ready || native.gesture){error="The tentative gesture's native editor is still busy";return false;}
    editor_native::Snapshot live;
    if(!editor_native::capture(live,error) || !tentative_integrity(live,pending->gesture.after.document,error) ||
       !editor_native::validate(current_native,controller->state().document,error)){
        if(error.empty())error="The tentative gesture's native identity changed";
        return false;
    }
    stage_stamp(controller->state().document.project,pending->request.operation,controller->state().document.revision,editor_stage_metrics::Stage::InverseBegin);
    const bool restored=editor_native::apply(controller->state().document,{},&current_native,error);
    stage_stamp(controller->state().document.project,pending->request.operation,controller->state().document.revision,editor_stage_metrics::Stage::InverseEnd,restored?1:2);
    if(restored)pending->tentative=false;
    return restored;
}
bool apply(const editor::Document& before, const editor::Document& after, const editor::Change& change, std::string& error) {
    editor_native::ValidationScope validation_scope;
    StageScope work(after,change.operation,editor_stage_metrics::Stage::ApplyWorkBegin,editor_stage_metrics::Stage::ApplyWorkEnd);
    before_apply_valid = false;
    rollback_fence.reset();
    prepared_native.reset();
    if (frozen) {error="The canonical editor is frozen";return false;}
    const bool adopt=pending && pending->tentative && pending->request.operation==change.operation &&
        change.intent==editor::Intent::Forward && pending->request.project==after.project &&
        pending->request.operation.author==controller->view().local && pending->request.project==controller->view().context.project &&
        pending->request.operation.epoch==controller->view().context.epoch &&
        pending->request.operation.epoch==after.epoch && pending->request.base_revision==before.revision &&
        pending->gesture.generation==native_generation && !detached_scene() && !structural(change);
    if(pending && pending->tentative && !adopt && !restore_tentative(error)){
        frozen=true;tell("Recover the committed draft: "+error);return false;
    }
    if(adopt){
        const auto native=editor_native::state();
        if(native.generation!=native_generation || !native.ready || native.gesture){
            error="The accepted gesture no longer has its ready native editor";return false;
        }
        editor_native::Snapshot live;
        if(!capture_recent(pending->gesture.after,after,live,error) || !tentative_integrity(live,after,error)){
            if(error.empty())error="The accepted gesture's native identity changed";
            return false;
        }
        before_apply=current_native;before_apply.document=before;
        rollback_fence=RollbackFence{native.generation,live.source,live.bindings,true};
        before_apply_valid=true;
        // No native mutation is needed: this is the same owned, checked gesture
        // accepted by the coordinator. Persistence still owns confirmation.
        stage_stamp(after.project,change.operation,after.revision,editor_stage_metrics::Stage::InverseBegin);
        stage_stamp(after.project,change.operation,after.revision,editor_stage_metrics::Stage::InverseEnd,1);
        stage_stamp(after.project,change.operation,after.revision,editor_stage_metrics::Stage::NativeApplyBegin);
        stage_stamp(after.project,change.operation,after.revision,editor_stage_metrics::Stage::NativeApplyEnd,1);
        live.document=after;prepared_native=std::move(live);pending->tentative=false;
        stage_stamp(after.project,change.operation,after.revision,editor_stage_metrics::Stage::SceneReady,1);
        work.okay=true;return true;
    }
    if(detached_scene())before_apply=current_native;
    else{
        if(editor_native::state().generation!=native_generation){error="The native editor belongs to a different context";return false;}
        if(!capture_recent(current_native,before,before_apply,error))return false;
        if(!native_integrity(before_apply,before,error))return stop_unsupported(error);
        rollback_fence=RollbackFence{native_generation,before_apply.source,before_apply.bindings,false};
    }
    before_apply_valid = true;
    before_apply.document = before;
    if(cast_change(change)){
        auto uploaded=cast_uploads.find(change.operation);
        const auto* content=host() && uploaded!=cast_uploads.end()?&uploaded->second:incoming_resources?&*incoming_resources:nullptr;
        if(host() && change.intent==editor::Intent::Forward && !content){
            auto rejected=cast_upload_errors.find(change.operation);
            if(rejected!=cast_upload_errors.end()){error=std::move(rejected->second);cast_upload_errors.erase(rejected);}
            else error="The cast operation has no validated dependency upload";
            return false;
        }
        if(content){
            auto merged=resources;if(!merge_dependencies(merged,*content,error))return false;
            std::vector<dbpf::Record> added;
            for(const auto& record:merged.bundle.records)if(record.key!=merged.root && std::none_of(resources.bundle.records.begin(),resources.bundle.records.end(),[&](const auto& old){return old.key==record.key;}))added.push_back(record);
            if(!detached_scene() && !register_native_records(content->root,added,error))return false;
        }
    }
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
        }else if(host() && change.patch.acts){
            auto uploaded=cast_uploads.find(change.operation);
            if(uploaded==cast_uploads.end() || !editor_project::native_snapshot(uploaded->second,after,artifact,error))return false;
            source=&artifact;
        }
    }
    editor_native::Snapshot prepared;
    if(!editor_native::prepare(before_apply,after,change,source,prepared,error))return false;
    bool applied=detached_scene();
    { StageScope native(after,change.operation,editor_stage_metrics::Stage::NativeApplyBegin,editor_stage_metrics::Stage::NativeApplyEnd);
      if(!applied)applied=editor_native::apply_prepared(prepared,change,error);native.okay=applied; }
    if(!applied)return false;
    if(!detached_scene() && editor_native::state().ready)stage_stamp(after.project,change.operation,after.revision,editor_stage_metrics::Stage::SceneReady,1);
    prepared_native=std::move(prepared);
    if (pending && pending->request.operation == change.operation && !change.allocated.empty() && after.placements.contains(change.allocated.front())) {
        std::string selection_error;
        // The inverse removes the native temporary selection. Restore the new
        // stable selection for its author, as ordinary placement does. Other
        // players retain their own selection and native overlapping-icon groups.
        if (!editor_native::select_placement(change.allocated.front(), selection_error) && H)
            H->log(M, SK_INFO, "Shared editor local selection: %s", selection_error.c_str());
    }
    work.okay=true;return true;
}
bool rollback(const editor::Document&, const editor::Document&, const editor::Change& change, std::string& error) {
    prepared_native.reset();
    bool restored=detached_scene() || !before_apply_valid;
    if(!restored){
        const auto native=editor_native::state();
        if(!rollback_fence || native.generation!=rollback_fence->generation || !native.ready || native.gesture){
            if(rollback_fence && native.generation!=rollback_fence->generation)native_identity_lost=true;
            error="The failed operation no longer owns its ready native editor";return false;
        }
        editor_native::Snapshot live;
        if(!editor_native::capture(live,error))return false;
        if(live.source!=rollback_fence->source || (rollback_fence->exact_bindings && live.bindings!=rollback_fence->bindings)){
            native_identity_lost=true;
            error="The failed operation's native resource identity changed";return false;
        }
        restored=editor_native::restore(before_apply,error);
    }
    if(restored && before_apply_valid && pending && pending->request.operation==change.operation)pending->tentative=false;
    return restored;
}
bool lease(const std::vector<editor::Scope>& scopes) {
    if (!controller || frozen || closing || recovery || durable_commit || awaiting_operation || controller->view().phase != Phase::Ready) return false;
    const auto& held = controller->view().local_leases;
    const auto local = controller->view().local;
    uint64_t time = now_ms();
    last_lease_use = time;
    for(const auto& scope:scopes)lease_use[scope]=time;
    bool complete = std::all_of(scopes.begin(), scopes.end(), [&](const auto& scope) {
        return std::any_of(held.begin(), held.end(), [&](const auto& item) {
            return item.owner == local && item.expires > time && (item.scope == scope || item.scope.kind == editor::ScopeKind::Document ||
                (item.scope.kind==editor::ScopeKind::Authoring && editor::is_authoring_scope(scope)));
        });
    });
    if (complete) return true;
    if (requested_scopes != scopes || time - last_request >= 2000) {
        requested_scopes = scopes; last_request = time; std::string error;
        if (!controller->request_leases(scopes, 15000, error)) tell(error);
        else status = "Waiting for editing ownership";
    }
    return false;
}
void observe_lease_grants() {
    const auto& view = controller->view();
    if (view.phase != Phase::Ready || view.context.project != observed_lease_context.project ||
        view.context.epoch != observed_lease_context.epoch || view.local != observed_lease_peer)
        {observed_lease_grants.clear();lease_use.clear();}
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
        if (!observed_lease_grants.contains(key)) {last_lease_use = now_ms();lease_use[held.scope]=now_ms();}
    }
    observed_lease_grants = std::move(current);
}
bool authoring_gate(editor::ObjectId id){
    if(!controller || !native_attached || local_creation || closing || exporting || pending_export_request || native_exit_requested)return false;
    const auto state=editor_native::state();const auto& doc=controller->state().document;
    auto settings=doc.settings.find(id);
    if(!state.ready || state.generation!=native_generation || settings==doc.settings.end() ||
       !lease({{editor::ScopeKind::Settings,id,{}}}))return false;
    if(!state.gesture && !pending)guarded_authoring.insert_or_assign(id,settings->second);else guarded_authoring.try_emplace(id,settings->second);
    return true;
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
editor_native::GestureScope current_gesture_scope(bool retained_import=false) {
    if (!controller || !native_attached || closing || recovery || transport_teardown || !C.session ||
        !C.session->connected() || C.session->transport_generation() != observed_transport) return {};
    const auto& view = controller->view();
    if(view.phase!=Phase::Ready || exporting || pending_export_request || native_exit_requested || save_publication_pending ||
       (!retained_import && awaiting_operation))return {};
    return {view.context.project, view.context.epoch, view.local};
}
bool creation_current(const editor_creation::Scope& scope){
    if(!controller || !C.session || !C.session->connected() || closing || frozen || recovery || transport_teardown)return false;
    const auto& view=controller->view();
    return scope.project==view.context.project && scope.epoch==view.context.epoch && scope.author==view.local && view.phase==Phase::Ready;
}
bool creation_lease(const editor_creation::Scope& scope,editor::ObjectId target){
    if(!creation_current(scope))return false;
    return std::any_of(controller->view().local_leases.begin(),controller->view().local_leases.end(),[&](const auto& item){
        return item.owner==scope.author && item.expires>now_ms() &&
            (item.scope.kind==editor::ScopeKind::Document || (item.scope.kind==editor::ScopeKind::Content && item.scope.object==target));
    });
}
editor_creation::PrepareResult prepare_creation(const editor_creation::Scope& scope,editor_creation::Request& input,std::string& error){
    using Result=editor_creation::PrepareResult;
    if(!creation_current(scope)){error="The creation request belongs to an expired shared adventure";return Result::Reject;}
    if(local_creation){
        if(local_creation->scope!=scope){error="Another local creation is already active";return Result::Reject;}
        const auto member=controller->view().members.find(scope.author.slot);
        return member!=controller->view().members.end() && member->second.peer==scope.author &&
            member->second.presence.creation_serial==scope.serial && member->second.presence.creating_asset?Result::Ready:Result::Wait;
    }
    if(exporting || pending_export_request || native_exit_requested || pending || awaiting_operation || durable_commit ||
       editor_native_save::busy() || !controller->settled())return Result::Wait;
    const auto native=editor_native::state();
    if(!native.ready || native.gesture || native.generation!=native_generation)return Result::Wait;
    if(!input.target && input.source.instance){
        if(auto selected=editor_creation_native::selected_cast())for(const auto& [id,row]:controller->state().document.class_settings)if(row.class_id==*selected){
            (void)id;auto value=row.properties.find(editor::kClassAssetResourceProperty);
            if(value!=row.properties.end() && value->second==editor::Value{input.source})input.target=*selected;
        }
    }
    if(!input.target && input.source.instance){
        input.target=editor_creation_native::resolve_cast_asset(controller->state().document,input.source);
        if(!input.target){
            size_t matches=0;
            for(const auto& [id,row]:controller->state().document.class_settings){(void)id;
                auto value=row.properties.find(editor::kClassAssetResourceProperty);
                if(value!=row.properties.end() && value->second==editor::Value{input.source})++matches;
            }
            if(matches>1){error="Select the exact cast entry before editing content shared by multiple entries";return Result::Reject;}
        }
    }
    if(input.target && !lease({{editor::ScopeKind::Content,*input.target,{}}}))return Result::Wait;
    if(input.target){
        bool matches=false;
        for(const auto& [id,row]:controller->state().document.class_settings)if(row.class_id==*input.target){
            const auto source=editor::value(controller->state().document,id,{editor::Field::Property,editor::kClassAssetResourceProperty});
            matches=source && *source==editor::Value{input.source};
        }
        if(!matches){
            std::vector<editor::LeaseToken> tokens;
            for(const auto& item:controller->view().local_leases)if(item.scope.kind==editor::ScopeKind::Content && item.scope.object==*input.target)tokens.push_back({item.scope,item.token});
            if(!tokens.empty())controller->release_leases(tokens,error);requested_scopes.clear();
            error="This asset's content changed while ownership was pending. Reopen its current version";return Result::Reject;
        }
    }
    if(!check_native_integrity(error))return Result::Reject;
    LocalCreation saved;saved.scope=scope;saved.request=input;saved.act=native.act;saved.authored_act=logical_act(native.act);saved.entry_generation=native_generation;
    if(!editor_native::capture_view(saved.view,error) || !editor_native::capture_scene_owner(saved.scene_owner,error))return Result::Reject;
    saved.entry_native=current_native;
    if(!native.selected.empty())saved.selected=native.selected.front();
    if(input.target)for(const auto& [id,settings]:controller->state().document.class_settings)if(settings.class_id==*input.target)
        saved.content_revision=editor::field_revision(controller->state().document,id,{editor::Field::Property,editor::kClassAssetResourceProperty});
    local_creation=std::move(saved);status="Confirming your local creation session with the host";
    editor::wire::Presence presence;presence.act=local_creation->authored_act;presence.creating_asset=true;presence.creation_serial=scope.serial;
    presence.creation_target=input.target.value_or(editor::ObjectId{});presence.scene_revision=rendered_revision;presence.scene_ready=false;
    if(!controller->set_presence(presence,error)){local_creation.reset();return Result::Reject;}
    last_presence=now_ms();last_presence_value=presence;
    // Entry waits for the host's reliable echo. Host Play cannot overtake this
    // announcement and strand a design behind a group authoring hold.
    return Result::Wait;
}
bool materialize_creation_return(std::string& error){
    if(!local_creation || durable_commit)return false;
    if(!controller || !resources.root.instance || current_native.bytes.empty() || controller->state().document.acts.empty() ||
       controller->state().document.project!=local_creation->scope.project || controller->state().document.epoch!=local_creation->scope.epoch){
        error="The creation is saved locally. Reopen the retained adventure recovery checkpoint to continue";return false;
    }
    if(!local_creation->resume_started)local_creation->resume_started=now_ms();
    if(now_ms()-local_creation->resume_started>180000){
        error="The native adventure resume did not finish; the saved creation and committed draft are retained";return false;
    }
    auto native=editor_native::state();
    if(!native.editing){
        if(!local_creation->returned_generation){
            local_creation->entry_native=current_native;
            if(!register_native_records(resources.root,resources.bundle.records,error) || !editor_native::enter(current_native.source,error))return false;
            local_creation->returned_generation=UINT64_MAX;local_creation->entered_adventure=true;
        }else if(local_creation->returned_generation!=UINT64_MAX){
            error="The creation return's native adventure scene was replaced";
        }
        return false;
    }
    if(!native.ready || native.gesture)return false;
    if(local_creation->returned_generation && local_creation->returned_generation!=UINT64_MAX && native.generation!=local_creation->returned_generation){
        error="The creation return scene was replaced before restoration completed";return false;
    }
    if(!local_creation->camera_resumed){
        auto scene=editor_native::creation_scene_state(local_creation->scene_owner);
        // Native OnEnter restarts initialization before resuming its toolbar,
        // history and world. Resource readiness alone does not prove return.
        if(!scene.available || !scene.ready || scene.initialization!=4 || scene.enter_pending || scene.transitioning || !scene.world_visible){
            status="Returning to the adventure; waiting for native scene initialization (state "+std::to_string(scene.initialization)+
                ", entry "+std::to_string(scene.enter_pending)+", transition "+std::to_string(scene.transitioning)+", toolbar "+std::to_string(scene.world_visible)+")";
            return false;
        }
        if(!scene.owner && !local_creation->entered_adventure){error="The creation returned to a different native adventure owner";return false;}
        editor_native::Snapshot loaded;
        ProjectionScope projection(local_creation->entry_native.document);
        if(!editor_native::capture_draft(loaded,error))return false;
        loaded.bindings=local_creation->entry_native.bindings;
        if((local_creation->entered_adventure && loaded.source!=local_creation->entry_native.source) ||
           !editor_native::validate(loaded,local_creation->entry_native.document,error)){
            if(error.empty())error="The returned native adventure differs from its owned entry snapshot";
            return false;
        }
        if(local_creation->entered_adventure && !editor_native::capture_scene_owner(local_creation->scene_owner,error))return false;
        const editor_native::GestureScope scope{local_creation->scope.project,local_creation->scope.epoch,local_creation->scope.author};
        if(!editor_native::resume_creation_scene_camera(scope,local_creation->scope.serial,native.generation,local_creation->scene_owner,error))return false;
        local_creation->camera_resumed=true;local_creation->returned_generation=native.generation;
    }
    if(!local_creation->scene_revision || *local_creation->scene_revision!=controller->state().document.revision){
        auto snapshot=current_native;
        const auto& order=controller->state().document.act_order;
        auto act=std::find(order.begin(),order.end(),local_creation->authored_act);
        snapshot.act=act!=order.end()?uint32_t(act-order.begin()):std::min<uint32_t>(local_creation->act,uint32_t(controller->state().document.acts.size()-1));
        if(!register_native_records(resources.root,resources.bundle.records,error) || !editor_native::restore(snapshot,error))return false;
        native_generation=editor_native::state().generation;local_creation->returned_generation=native_generation;
        editor_native::bind(current_native.bindings);editor_native::bind_acts(controller->state().document);
        local_creation->scene_revision=controller->state().document.revision;local_creation->scene_frame=H->frame_index?H->frame_index():0;
        status="Restoring adventure revision "+std::to_string(*local_creation->scene_revision)+"; waiting for native scene readiness";return false;
    }
    const auto resumed=editor_native::creation_scene_state(local_creation->scene_owner);
    if(!resumed.owner){error="The creation return's native adventure owner changed during restoration";return false;}
    if(!resumed.settled())return false;
    if(H->frame_index && H->frame_index()<=local_creation->scene_frame)return false;
    editor_native::Snapshot verified;
    if(!editor_native::capture(verified,error) || !editor_native::validate(verified,controller->state().document,error))return false;
    if(local_creation->selected && (controller->state().document.placements.contains(*local_creation->selected) ||
       (*local_creation->selected==editor_native::kCaptainObject && controller->state().document.settings.contains(editor_native::kCaptainObject))) &&
       !editor_native::select_placement(*local_creation->selected,error))return false;
    if(!editor_native::restore_view(local_creation->view,native_generation,error))return false;
    rendered_revision=controller->state().document.revision;local_creation->materialized=true;
    return true;
}
bool return_creation(const editor_creation::Scope& scope,const editor_creation::Result& result,std::string& error){
    if(!local_creation || local_creation->scope!=scope){error="This creation result has no retained adventure view";return false;}
    auto& saved=*local_creation;
    if(!creation_current(scope)){error="The creation was saved locally; its shared adventure session expired";return false;}
    if(!result.cancelled && !saved.submitted){
        if(durable_commit || pending || awaiting_operation || exporting || pending_export_request)return false;
        if(result.request.target && !creation_lease(scope,*result.request.target)){saved.error="The asset content lease expired; the saved creation remains available locally";}
        else if(!result.request.target && !lease({{editor::ScopeKind::Structure,{},{}}}))return false;
        if(saved.error.empty()){
            editor::ResourceKey version;std::vector<dbpf::Record> records;
            const auto reserved=[](const editor::ResourceKey& key){
                const dbpf::Key native_key{key.instance,key.type,key.group};
                return editor_creation_native::resource_reserved(key) || std::any_of(resources.bundle.records.begin(),resources.bundle.records.end(),[&](const auto& row){return row.key==native_key;});
            };
            editor_native::Snapshot changed;std::vector<uint8_t> assets;
            if(!editor_creation_native::collect_version(scope.project,result.saved,reserved,version,records,saved.error) ||
               !register_native_records(resources.root,records,saved.error) ||
               !editor_creation_native::prepare_cast_save(current_native,version,result.request.target,changed,saved.error) ||
               !collect_cast_assets(changed,assets,saved.error)){}
            else{
                auto operation=request(editor::Intent::Forward);operation.summary=result.request.target?"Replace shared asset content":"Create adventure asset";
                for(const auto& [id,settings]:changed.document.class_settings){
                    auto prior=controller->state().document.class_settings.find(id);
                    editor::Mutation mutation;
                    if(prior==controller->state().document.class_settings.end()){
                        mutation.kind=editor::MutationKind::CreateClass;mutation.class_initial=settings;
                        mutation.class_initial->revision=0;mutation.class_initial->field_revisions.clear();
                    }else if(result.request.target && settings.class_id==*result.request.target){
                        mutation.target=id;mutation.field={editor::Field::Property,editor::kClassAssetResourceProperty};
                        mutation.value=editor::value(changed.document,id,mutation.field);mutation.expected=saved.content_revision;
                    }else continue;
                    operation.mutations.push_back(std::move(mutation));
                }
                if(operation.mutations.size()!=1)saved.error="The saved creation did not produce exactly one cast content change";
                else if(controller->upload_assets(operation.operation,assets,saved.error) && controller->submit(operation,saved.error)){
                    saved.operation=operation.operation;saved.submitted=true;awaiting_operation=operation.operation;return false;
                }
            }
        }
        saved.submitted=true;
    }
    if(saved.operation && !saved.accepted.has_value())return false;
    if(!materialize_creation_return(error))return false;
    error=saved.error;return error.empty();
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
    if (durable_commit || pending || awaiting_operation || !native.ready || native.gesture || editor_native_save::busy()) {
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
    const auto released=std::exchange(native_release_qpc,0);
    const auto capture_begin=std::exchange(native_capture_begin_qpc,0),capture_end=std::exchange(native_capture_end_qpc,0);
    if (!controller || !session_active() || frozen || closing || recovery) return;
    if(durable_commit){frozen=true;tell("A native gesture bypassed the durable commit hold. Recover the committed draft");return;}
    if (gesture.generation != native_generation || editor_native::state().generation != native_generation) {
        frozen = true; tell("A delayed gesture belongs to a different native editor. The current editor was left intact."); return;
    }
    if (awaiting_operation) { frozen = true; tell("Another native gesture arrived before the pending operation settled; recover the committed draft"); return; }
    // Both the initial integrity proof and tentative setup validate the same
    // owned before image. Keep its exact proof only for this synchronous call.
    editor_native::ValidationScope validation_scope;
    const auto& doc = controller->state().document;
    std::string error;
    const auto supported_begin=stage_qpc();
    if (!supported_between(current_native, gesture.before, error) || !editor_native::supported_changes(gesture, error)) {
        stop_unsupported(error); return;
    }
    // First prove the actual native gesture. Project-owned creation identities
    // are then a separate allowlisted transformation of its owned after copy.
    const auto version_context=controller->view().context;const auto version_author=controller->view().local;
    const bool canonicalized=canonicalize_crew_assets(gesture,error);
    if(!controller || controller->view().context.project!=version_context.project || controller->view().context.epoch!=version_context.epoch ||
       controller->view().local!=version_author || !C.session || C.session->transport_generation()!=observed_transport ||
       gesture.generation!=editor_native::state().generation){
        frozen=true;native_identity_lost=true;tell("The crew gesture's shared editor context expired. The current native scene was left intact.");return;
    }
    if(!canonicalized){stop_unsupported(error);return;}
    const auto supported_end=stage_qpc();
    const bool roster=gesture.after.document.act_order!=doc.act_order;
    if (gesture.before.document.classes != doc.classes || gesture.before.document.acts != doc.acts ||
        gesture.before.document.act_order!=doc.act_order) {
        error="The native gesture changed an unsupported act or started from a stale cast roster";stop_unsupported(error);
        return;
    }
    auto r = request(editor::Intent::Forward); r.summary = "Edit placement";
    if(released && stage_metrics.recording())stage_metrics.record(doc.project,r.operation,doc.revision,editor_stage_metrics::Stage::GestureRelease,released);
    if(stage_metrics.recording()){
        using S=editor_stage_metrics::Stage;
        if(capture_begin)stage_metrics.record(doc.project,r.operation,doc.revision,S::CaptureBegin,capture_begin);
        if(capture_end)stage_metrics.record(doc.project,r.operation,doc.revision,S::CaptureEnd,capture_end,1);
        stage_metrics.record(doc.project,r.operation,doc.revision,S::SupportedBegin,supported_begin);
        stage_metrics.record(doc.project,r.operation,doc.revision,S::SupportedEnd,supported_end,1);
    }
    if(roster){
        if(!lease({{editor::ScopeKind::Authoring,{},{}}})){error="The act roster has no shared authoring hold";stop_unsupported(error);return;}
        editor::Mutation mutation;mutation.kind=editor::MutationKind::SetActs;mutation.expected=doc.act_revision;
        mutation.act_roster=editor::act_roster(gesture.after.document);
        for(auto& row:*mutation.act_roster){
            auto prior=doc.settings.find(row.object);
            if(prior!=doc.settings.end()){row.revision=prior->second.revision;row.field_revisions=prior->second.field_revisions;}
        }
        r.mutations.push_back(std::move(mutation));r.summary="Edit adventure acts";
    }
    editor::Change revert;
    bool editing_cast=roster;
    for(const auto& [id,settings]:doc.settings){
        if(roster && settings.kind==editor::SettingsKind::Act)continue;
        auto prior=gesture.before.document.settings.find(id),next=gesture.after.document.settings.find(id);
        if(prior==gesture.before.document.settings.end() || next==gesture.after.document.settings.end()){
            error="The authored settings roster changed during a gesture";stop_unsupported(error);return;
        }
        if(prior->second.properties==next->second.properties)continue;
        auto guard=guarded_authoring.find(id);
        if(guard==guarded_authoring.end()){error="The authored settings gesture has no current ownership";stop_unsupported(error);return;}
        std::set<uint32_t> keys;
        for(const auto& [key,v]:prior->second.properties){(void)v;keys.insert(key);}
        for(const auto& [key,v]:next->second.properties){(void)v;keys.insert(key);}
        for(auto key:keys){
            const editor::FieldKey field{editor::Field::Property,key};
            const auto before=editor::value(gesture.before.document,id,field),after=editor::value(gesture.after.document,id,field);
            if(before==after)continue;
            editor::Mutation mutation;mutation.target=id;mutation.field=field;mutation.value=after;
            auto revision=guard->second.field_revisions.find(field);mutation.expected=revision==guard->second.field_revisions.end()?0:revision->second;
            r.mutations.push_back(std::move(mutation));
            if((id==editor_native::kCaptainObject && key==editor_native::kCaptainAssetProperty) || id==editor_native::kCrewObject)editing_cast=true;
        }
        r.summary=settings.kind==editor::SettingsKind::Act?"Edit act settings":settings.kind==editor::SettingsKind::Crew?"Edit adventure crew":
            settings.kind==editor::SettingsKind::Planet?"Edit planet settings":settings.kind==editor::SettingsKind::Project?"Edit adventure settings":"Edit adventure captain";
    }
    for(const auto& [id,record]:gesture.before.document.terrain){
        auto next=gesture.after.document.terrain.find(id);
        if(next!=gesture.after.document.terrain.end() && next->second==record)continue;
        auto owned=guarded_terrain.find(id);
        if(owned==guarded_terrain.end()){error="The changed terrain stamp has no record ownership";stop_unsupported(error);return;}
        editor::Mutation mutation;mutation.target=id;mutation.expected=owned->second.revision;
        if(next==gesture.after.document.terrain.end())mutation.kind=editor::MutationKind::DeleteTerrain;
        else{mutation.kind=editor::MutationKind::SetTerrain;mutation.terrain=next->second;}
        r.mutations.push_back(std::move(mutation));r.summary="Terraform adventure";
    }
    for(const auto& [id,record]:gesture.after.document.terrain)if(!gesture.before.document.terrain.contains(id)){
        editor::Mutation mutation;mutation.kind=editor::MutationKind::AppendTerrain;mutation.terrain=record;
        mutation.terrain->object={};mutation.terrain->order=0;mutation.terrain->revision=0;
        r.mutations.push_back(std::move(mutation));r.summary="Terraform adventure";
    }
    for(const auto& [id,settings]:doc.class_settings){
        auto next=gesture.after.document.class_settings.find(id);
        if(next!=gesture.after.document.class_settings.end() && next->second.properties==settings.properties)continue;
        auto guard=guarded_settings.find(id);
        if(guard==guarded_settings.end()){error="The cast gesture has no class ownership";stop_unsupported(error);return;}
        if(next==gesture.after.document.class_settings.end()){
            editor::Mutation mutation;mutation.kind=editor::MutationKind::DeleteClass;mutation.target=settings.class_id;mutation.expected=guard->second.revision;
            r.mutations.push_back(std::move(mutation));
        }else for(const auto& [key,value]:next->second.properties){
            const editor::FieldKey field{editor::Field::Property,key};
            if(editor::value(doc,id,field)==std::optional<editor::Value>{value})continue;
            editor::Mutation mutation;mutation.target=id;mutation.field=field;mutation.value=value;mutation.expected=editor::field_revision(doc,id,field);
            r.mutations.push_back(std::move(mutation));
        }
        editing_cast=true;
    }
    for(const auto& [id,settings]:gesture.after.document.class_settings)if(!doc.class_settings.contains(id)){
        editor::Mutation mutation;mutation.kind=editor::MutationKind::CreateClass;mutation.class_initial=settings;
        mutation.class_initial->revision=0;mutation.class_initial->field_revisions.clear();r.mutations.push_back(std::move(mutation));editing_cast=true;
    }
    for (const auto& [id, placement] : doc.placements) {
        auto prior = gesture.before.document.placements.find(id), next = gesture.after.document.placements.find(id);
        if (prior != gesture.before.document.placements.end() && next != gesture.after.document.placements.end() && same_authored(prior->second, next->second)) continue;
        auto guard = guarded_placements.find(id);
        if (prior == gesture.before.document.placements.end() || guard == guarded_placements.end()) {
            error="The native gesture has no current placement ownership";stop_unsupported(error);return;
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
    guarded_placements.clear();guarded_settings.clear();guarded_authoring.clear();guarded_terrain.clear();
    if (r.mutations.empty()) return;
    std::vector<uint8_t> assets;
    if(editing_cast){
        if(r.summary=="Edit placement" || r.summary=="Create placement" || r.summary=="Delete placement")r.summary="Edit cast";
        if(!collect_cast_assets(gesture.after,assets,error)){stop_unsupported(error);return;}
    }
    const bool tentative=!editing_cast && assets.empty() && std::all_of(r.mutations.begin(),r.mutations.end(),[&](const auto& mutation){
        return mutation.kind==editor::MutationKind::Set && doc.placements.contains(mutation.target);
    });
    if(tentative){
        if(!editor_native::validate(gesture.before,doc,error)){stop_unsupported(error);return;}
    }else{
        // Structural gestures still retire native temporary objects before the
        // canonical allocation can recreate their slots on a later frame.
        stage_stamp(doc.project,r.operation,doc.revision,editor_stage_metrics::Stage::InverseBegin);
        const bool restored=editor_native::apply(doc,revert,&current_native,error);
        stage_stamp(doc.project,r.operation,doc.revision,editor_stage_metrics::Stage::InverseEnd,restored?1:2);
        if(!restored){frozen=true;tell("The native gesture could not return to committed state: "+error);return;}
    }
    if (!H->frame_index) { frozen = true; tell("Native gesture submission requires the game frame counter"); return; }
    pending = Pending{r, std::move(gesture), H->frame_index(), false};
    pending->assets=std::move(assets);
    pending->tentative=tentative;
    awaiting_operation = r.operation;
    status = tentative?"Submitting the placement gesture":"Waiting for the next game frame before submitting the placement gesture";
}
void submit_pending_gesture() {
    if (!pending || pending->submitted) return;
    const auto& r = pending->request;
    const auto& v = controller->view();
    const auto native = editor_native::state();
    std::string error;
    const bool changed_generation = r.project!=v.context.project || r.operation.author!=v.local ||
        r.operation.epoch != v.context.epoch || pending->gesture.generation != native_generation || native.generation != native_generation;
    const bool ending = std::any_of(actions.begin(), actions.end(), [](auto action) { return action == Action::End || action == Action::Recover; });
    if (closing || ending || frozen || recovery || !C.session || !C.session->connected() || v.phase != Phase::Ready ||
        r.project != v.context.project || r.operation.author != v.local || r.operation.author.slot != C.session->my_id() ||
        r.base_revision > controller->state().document.revision || changed_generation) {
        if((closing || ending) && pending->tentative && !changed_generation)return;
        if(changed_generation && pending->tentative)native_identity_lost=true;
        if(pending->tentative && !changed_generation && !closing && !ending && !restore_tentative(error)){
            frozen=true;tell("Recover the committed draft: "+error);
        }
        pending.reset(); awaiting_operation.reset();
        if (changed_generation && !closing && !ending) { frozen = true; tell("A held gesture expired with its editor context; no operation was submitted"); }
        return;
    }
    // Native inverse deletion queues noun retirement until the ordinary game
    // update. Recreating the same slot in this outside-frame pump is too early.
    // Repeated callbacks within one rendered frame must not advance this gate.
    if (!native.ready || native.gesture || !H->frame_index || (!pending->tentative && H->frame_index() <= pending->reverted_frame)) return;
    auto request = r;
    pending->submitted = true;
    const auto operation=request.operation;
    stage_stamp(controller->state().document.project,operation,controller->state().document.revision,editor_stage_metrics::Stage::LocalSubmit);
    confirmation_metrics.begin(operation,H->time_seconds());
    if(!pending->assets.empty() && !controller->upload_assets(operation,pending->assets,error)){
        confirmation_metrics.cancel(operation);pending.reset();awaiting_operation.reset();tell(error);return;
    }
    if (!controller->submit(std::move(request), error)) {
        confirmation_metrics.cancel(operation);
        std::string restoration;
        if(!restore_tentative(restoration)){frozen=true;error+="; recover the committed draft: "+restoration;}
        pending.reset();awaiting_operation.reset();tell(error);
    }
    else status = "Waiting for the host to commit the placement gesture";
}
void refresh() {
    View next;
    if (!controller) { cached = next; return; }
    const auto& v = controller->view(); const auto& doc = controller->state().document;
    auto native = editor_native::state();
    next.host = host(); next.offered = v.phase == Phase::Offered; next.active = session_active() || closing;
    next.native_editing=native.editing;next.join_prompt=handoff && !native_exit_requested && !handoff_saving;
    next.test_request=test_request.has_value();next.retry=follow_failed || (!next.host && (frozen || v.phase==Phase::Frozen));
    next.exporting=exporting.has_value() || pending_export_request;
    if(test_request && C.session)next.requester=C.session->name_of(test_request->actor.slot);
    next.prompt_identity=editor_store::project_name(v.context.project)+":"+std::to_string(v.context.epoch)+":"+
        std::to_string(v.playtest_round)+":"+std::to_string(native.generation)+":"+
        std::to_string(observed_transport)+":"+std::to_string(handoff_prompt_serial)+":"+(test_request?std::to_string(test_request->actor.slot)+":"+
        std::to_string(test_request->actor.generation)+":"+std::to_string(test_request->serial):"0");
    next.visible = C.session && C.session->connected();
    next.can_begin = !next.active && !closing && !beginning && (!C.adventure_busy || !C.adventure_busy()) &&
        native.generation!=native_exit_generation &&
        ((next.host && native.ready && native.act<8 && editor_native_save::available()) || (next.offered && game::mode_id() == modes::kMenu));
    next.revision = doc.revision; next.durable_revision = v.locally_durable;
    next.rendered_revision=rendered_revision;
    next.title = v.name.empty() ? "Shared Adventure Editor" : v.name;
    next.status = status.empty() ? v.error : status;
    if (!next.active && next.host && native.editing && native.act>=8)
        supported_act(native.act, next.status);
    auto identity=[](editor::ObjectId id) { return id==editor_native::kCaptainObject?std::string("Captain"):"Placement " + std::to_string(id.id) + ":" + std::to_string(id.generation); };
    next.selection = native.selected.empty() ? "No placement selected" : identity(native.selected.front());
    next.frozen = frozen || v.phase == Phase::Frozen;
    next.world_hints=next.active && native.ready && native.world_visible && v.phase==Phase::Ready && !next.frozen && !recovery;
    next.waiting = closing || recovery.has_value() || awaiting_operation.has_value() || v.phase == Phase::Joining || v.phase == Phase::Syncing || v.phase == Phase::Preparing || v.phase == Phase::Returning;
    if (v.phase == Phase::Returning) next.status = "Waiting for every player to return to the committed draft";
    next.confirming = v.phase == Phase::Preparing && v.playtest_mode==editor::wire::PlaytestMode::Together;
    next.can_cancel = v.phase==Phase::Preparing && next.host && !closing && !recovery && !next.frozen;
    next.playtesting = v.phase == Phase::Playing;
    next.can_return = next.playtesting && (next.host || (v.playtest_mode==editor::wire::PlaytestMode::Solo && v.local==v.playtest_tester));
    if(v.playtest_mode==editor::wire::PlaytestMode::Solo && (v.phase==Phase::Preparing || v.phase==Phase::Playing)){
        const auto name=C.session?C.session->name_of(v.playtest_tester.slot):"A collaborator";
        next.status=name+" is testing solo. Shared editing is paused";next.waiting=true;
    }
    bool editing = native.ready && next.active && !local_creation && !durable_commit && !closing && !recovery && !exporting && !pending_export_request && !native_exit_requested && v.phase == Phase::Ready && !next.frozen && !awaiting_operation && !native.gesture && !editor_native_save::busy();
    const auto complexity=editor::value(doc,editor_native::kProjectObject,{editor::Field::Property,editor_native::kExtendedComplexityProperty});
    next.can_complexity=editing && complexity.has_value() && !complexity_request && editor_native::extended_complexity_available();
    if(complexity)if(auto flag=std::get_if<bool>(&*complexity))next.extended_complexity=*flag;
    next.can_move_act_left=editing && native.act>0 && !doc.act_order.empty();
    next.can_move_act_right=editing && native.act+1<doc.act_order.size();
    for (const auto& gesture : controller->state().history) if (gesture.operation.author == v.local) {
        next.can_undo = next.can_undo || gesture.applied;
        next.can_redo = next.can_redo || (!gesture.applied && gesture.redo_available);
    }
    next.can_undo &= editing; next.can_redo &= editing;
    next.can_save = editing && !save_publication_pending;
    const auto authored_avatar = editor_native::avatar();
    next.can_playtest = editing && !save_publication_pending && authored_avatar.instance && authored_avatar.type &&
        std::all_of(v.members.begin(), v.members.end(), [&](const auto& entry) { return entry.second.phase==Phase::Ready &&
            !entry.second.presence.creating_asset && entry.second.presence.scene_ready && entry.second.presence.scene_revision==doc.revision; });
    next.test_menu=test_menu && next.can_playtest && test_menu->scope==current_gesture_scope() && test_menu->generation==native.generation && test_menu->revision==doc.revision && test_menu->act==native.act;
    if(test_menu && !next.test_menu)test_menu.reset();
    if (editing && next.host && (!authored_avatar.instance || !authored_avatar.type))
        next.status = "Choose an authored adventure captain before playtesting.";
    next.can_recover = next.host && !closing && recovery_available && !recovery && (next.frozen || !next.active) &&
        (game::mode_id() == modes::kMenu || (native.editing && native.generation == native_generation));
    if (v.saved) { next.natively_saved = true; next.saved_revision = v.saved->saved_revision; }
    for (const auto& [slot, member] : v.members) {
        constexpr uint32_t colors[]={0xFF80C8FF,0xFFFFB060,0xFFB79CFF,0xFF70DF98};
        const auto color=colors[slot%4];
        PlayerView player; player.id = slot; player.name = member.name; player.host = slot == 0; player.local = slot == v.local.slot;
        player.ready = member.ready; player.durable_revision = member.coverage.durable_revision; player.applied_revision = member.applied_revision;
        player.selection = member.presence.selected.id ? identity(member.presence.selected) : "No placement selected";
        player.pending = member.presence.pending;
        player.owns_selection = (member.presence.ownership & editor::wire::kOwnSelection) != 0;
        for(const auto& scope:member.presence.owned_scopes)if(scope.kind==editor::ScopeKind::Class){
            const auto label=member.name+": asset "+std::to_string(uint32_t(scope.object.id))+" locked";
            player.selection+=("; asset "+std::to_string(uint32_t(scope.object.id))+" locked");
            if(!player.local && next.active && v.phase==Phase::Ready)next.locks.push_back(label);
        }
        for(const auto& scope:member.presence.owned_scopes)if(scope.kind==editor::ScopeKind::Settings){
            auto settings=doc.settings.find(scope.object);if(settings==doc.settings.end())continue;
            const auto name=settings->second.kind==editor::SettingsKind::Captain?std::string("Captain"):"Act "+std::to_string(native_act(settings->second.act_id)+1);
            player.selection+="; "+name+" locked";
            if(!player.local && next.active && v.phase==Phase::Ready)next.locks.push_back(member.name+": "+name+" locked");
        }
        if(!player.local && next.active && v.phase==Phase::Ready && member.phase==Phase::Ready){
            for(const auto& [id,p]:doc.placements){
                const bool selected=member.presence.selected==id;
                const bool locked=std::any_of(member.presence.owned_scopes.begin(),member.presence.owned_scopes.end(),[&](const editor::Scope& s){
                    return s.kind==editor::ScopeKind::Document || (s.kind==editor::ScopeKind::Class && s.object==p.class_id) ||
                        ((s.kind==editor::ScopeKind::Placement || s.kind==editor::ScopeKind::Field) && s.object==id);
                });
                if((!selected && !locked) || (doc.settings.empty() && p.act_id!=editor::ObjectId{(3ull<<32)|native.act,1}) || next.selections.size()>=128)continue;
                TransformHint hint;hint.label=member.name+(locked?" (locked)":" (selected)");hint.color=color;hint.locked=locked;
                hint.position={p.position.x,p.position.y,p.position.z};hint.rotation={p.rotation.x,p.rotation.y,p.rotation.z,p.rotation.w};hint.scale=p.scale;
                next.selections.push_back(std::move(hint));
                if(p.properties.contains(editor_native::kDestinationPositionProperty)){
                    const auto pos=std::get<editor::Vec3>(p.properties.at(editor_native::kDestinationPositionProperty));
                    auto exit=next.selections.back();exit.position={pos.x,pos.y,pos.z};exit.label+=" exit";next.selections.push_back(std::move(exit));
                }
            }
            auto captain=doc.settings.find(editor_native::kCaptainObject);
            const bool selected=member.presence.selected==editor_native::kCaptainObject;
            const bool locked=std::any_of(member.presence.owned_scopes.begin(),member.presence.owned_scopes.end(),[](const editor::Scope& scope){
                return scope.kind==editor::ScopeKind::Document || ((scope.kind==editor::ScopeKind::Settings || scope.kind==editor::ScopeKind::Field) && scope.object==editor_native::kCaptainObject);
            });
            if(captain!=doc.settings.end() && (selected || locked) && next.selections.size()<128){
                const auto& props=captain->second.properties;
                if(props.contains(editor_native::kCaptainPositionProperty) && props.contains(editor_native::kCaptainRotationProperty) && props.contains(editor_native::kCaptainScaleProperty)){
                    const auto p=std::get<editor::Vec3>(props.at(editor_native::kCaptainPositionProperty));
                    const auto q=std::get<editor::Rotation>(props.at(editor_native::kCaptainRotationProperty));
                    TransformHint hint;hint.label=member.name+": Captain"+(locked?" (locked)":" (selected)");hint.color=color;hint.locked=locked;
                    hint.position={p.x,p.y,p.z};hint.rotation={q.x,q.y,q.z,q.w};hint.scale=std::get<float>(props.at(editor_native::kCaptainScaleProperty));next.selections.push_back(std::move(hint));
                }
            }
        }
        auto phase=member.phase;
        if (!next.host && !player.local && member.presence.author==member.peer) {
            using P=editor::wire::PresencePhase;
            switch(member.presence.phase){
            case P::Ready:case P::Creating: phase=Phase::Ready;break;
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
                player.owns_selection ? "Owns selected placement" : (member.presence.ownership & editor::wire::kOwnDocument) ? "Owns draft structure" : "Browsing";
            if(!player.owns_selection && std::any_of(member.presence.owned_scopes.begin(),member.presence.owned_scopes.end(),[](const auto& s){return s.kind==editor::ScopeKind::Class;}))
                player.status=player.pending?"Commit pending":member.presence.editing?"Editing asset properties":"Viewing locked asset properties";
            if(member.presence.creating_asset)player.status=member.presence.creation_target==editor::ObjectId{}?"Creating an asset":"Editing shared asset content";
            break;
        case Phase::Joining: case Phase::Syncing: player.status="Receiving draft";break;
        case Phase::Preparing: player.status=player.ready ? "Playtest ready" : "Confirm playtest";break;
        case Phase::Playing: player.status=v.playtest_mode==editor::wire::PlaytestMode::Together || member.peer==v.playtest_tester?"Playtesting":"Waiting for solo tester";break;
        case Phase::Returning: player.status="Returning to draft";break;
        case Phase::Frozen: player.status="Needs recovery";break;
        case Phase::Disconnected: player.status="Disconnected";break;
        case Phase::Offered: player.status="Invited to edit";break;
        case Phase::Idle: player.status="Not editing";break;
        }
        if(member.presence.act.id && doc.acts.contains(member.presence.act) && phase==Phase::Ready)player.status+="; Act "+std::to_string(native_act(member.presence.act)+1);
        if (!player.local && v.phase==Phase::Ready && member.preview && member.preview->expires>now_ms()) {
            const auto& hint=member.preview->value;
            if (hint.creating || doc.placements.contains(hint.object) || hint.object==editor_native::kCaptainObject) {
                TransformHint shown;
                shown.color=color;
                size_t name_bytes=std::min<size_t>(20,member.name.size());
                while(name_bytes && name_bytes<member.name.size() &&
                      (uint8_t(member.name[name_bytes])&0xC0)==0x80)--name_bytes;
                shown.label=member.name.substr(0,name_bytes)+(hint.creating?" (placing)":" (moving)");
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
    if(!restore_tentative(error))return false;
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
        !editor_native::normalize_saved_grounding(native,controller->state().document,error) ||
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
    if (!editor_native::capture_draft(native, error)) { tell(error); return; }
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
    if (!editor_native::validate(native, state.document, error)) { tell(error); return; }
    const auto original_source=native.source;
    // A private transport key is not a published creation or permission to
    // replace one. Native Save As allocates its own identity later.
    bool unused=false;
    const auto group=editor_native::creation_group();
    if(!group){tell("The native adventure creation group is unavailable");return;}
    for(unsigned attempt=0;attempt<32 && !unused;++attempt) {
        native.source={uint32_t(random())|1u,0x366A930D,group};
        unused=adventure_collect::unused_snapshot_key({native.source.instance,native.source.type,native.source.group});
    }
    if(!unused){tell("Cannot reserve a private adventure snapshot identity");return;}
    editor_project::Resources content;
    content.root = {native.source.instance, native.source.type, native.source.group}; content.act = native.act; content.bindings = native.bindings;
    content.metadata=native.metadata;
    std::vector<dbpf::Record> records;
    const dbpf::Key collection_key=original_source.instance && original_source.type==0x366A930D ?
        dbpf::Key{original_source.instance,original_source.type,original_source.group}:content.root;
    if (!adventure_collect::collect_snapshot(collection_key, native.bytes, records, error)) { tell(error); return; }
    for(auto& record:records) {
        if(record.key==collection_key)record.key=content.root;
        else if(record.key.instance==collection_key.instance &&
            (record.key.group==collection_key.group || record.key.group==0x40A00200 || record.key.group==0x408AE500)) {
            // These keys belong to the original adventure's companions, not to
            // a class creation. Preserve their bytes under the private root.
            const auto type=record.key.type;
            if(type==0x02D5C9AF || type==0x030BDEE3 || type==0x2F7D0004 || type==0xFD60F0C1 || type==0x05528D58 || type==0x00B1B104) {
                record.key.instance=content.root.instance;
                if(record.key.group==collection_key.group)record.key.group=content.root.group;
            }
        }
    }
    if(!net::adventure_content::make_bundle(content.root,std::move(records),content.bundle,error)){tell(error);return;}
    for (const auto& record : content.bundle.records) if (record.key == content.root)
        content.base_root = std::make_shared<const std::vector<uint8_t>>(record.data);
    std::vector<uint8_t> envelope;
    if (!editor_project::encode_resources(content, envelope, error) || !editor_project::native_snapshot(content, state.document, native, error) ||
        !register_native_records(content.root,content.bundle.records, error) || !editor_native::restore(native, error)) { tell(error); return; }
    std::string title = adventure_collect::presentation(collection_key).name;
    if (title.empty()) title = "Shared adventure draft";
    store.close(); history_artifacts.clear(); local_capability = {}; native_copy_owned = false; source_creation=original_source;
    if (!create_store(state, native, envelope, title, error)) { tell(error); return; }
    resources = std::move(content); current_native = std::move(native);rendered_revision=state.document.revision; sequence = 0; frozen = false;
    native_attached = true; native_identity_lost=false;
    native_generation = editor_native::state().generation;
    if (!controller->start(std::move(state), std::move(envelope), title, error)) { store.close(); tell(error); return; }
    local_capability = controller->view().local;
    seen_commands={};seen_command_peers={};last_presence_value.reset();
    auto checkpoint = store.snapshot(); checkpoint.manifest = manifest(resource_hash, history_artifacts);
    if (!store.checkpoint(checkpoint, error)) { frozen = true; tell(error); return; }
    save_publication_pending = false;
    tell("Shared editing started. Each player can browse and edit acts independently.");
    if(C.close_panel)C.close_panel();
}
void undo_redo(bool redo,std::optional<editor::OperationId> pinned={}) {
    if (!controller || closing || recovery || controller->view().phase != Phase::Ready || awaiting_operation || frozen ||
        save_publication_pending || exporting || pending_export_request || native_exit_requested || editor_native_save::busy()) return;
    // Cached toolbar availability can predate the native input event. Recheck
    // ownership and transitions before acquiring leases or queuing history work.
    const auto native = editor_native::state();
    if (!native.editing || !native.ready || native.gesture || native.generation != native_generation) return;
    const auto& state = controller->state(); const editor::Gesture* target = nullptr;
    for (const auto& gesture : state.history) if (gesture.operation.author == controller->view().local) {
        if(pinned && gesture.operation!=*pinned)continue;
        if (!redo && gesture.applied && (!target || gesture.original_revision > target->original_revision)) target = &gesture;
        if (redo && !gesture.applied && gesture.redo_available && (!target || gesture.transition_revision > target->transition_revision)) target = &gesture;
    }
    if (!target) { tell(redo ? "There is no action to redo" : "There is no action to undo"); return; }
    std::vector<editor::Scope> scopes;
    const auto& patch = redo ? target->redo : target->undo;
    if(patch.acts)scopes.push_back({editor::ScopeKind::Authoring,{},{}});
    else {
    for (const auto& object : patch.objects) scopes.push_back({editor::ScopeKind::Placement, object.object, {}});
    for (const auto& field : patch.fields){
        auto authored=state.document.settings.find(field.object);
        if(authored!=state.document.settings.end()){
            const bool individual=authored->second.kind==editor::SettingsKind::Planet || authored->second.kind==editor::SettingsKind::Project;
            scopes.push_back({individual?editor::ScopeKind::Field:editor::ScopeKind::Settings,field.object,individual?field.field:editor::FieldKey{}});continue;
        }
        auto settings=state.document.class_settings.find(field.object);
        scopes.push_back(settings==state.document.class_settings.end()?editor::Scope{editor::ScopeKind::Field,field.object,field.field}:
            editor::Scope{field.field==editor::FieldKey{editor::Field::Property,editor::kClassAssetResourceProperty}?editor::ScopeKind::Content:editor::ScopeKind::Class,settings->second.class_id,{}});
    }
    if(!patch.classes.empty()){
        scopes.push_back({editor::ScopeKind::Structure,{},{}});
        for(const auto& entry:patch.classes)if(entry.before){scopes.push_back({editor::ScopeKind::Class,entry.object,{}});
            if(state.document.class_schema.contains(editor::kClassAssetResourceProperty))scopes.push_back({editor::ScopeKind::Content,entry.object,{}});}
    }
    for(const auto& entry:patch.terrain)if(entry.before)scopes.push_back({editor::ScopeKind::Terrain,entry.object,{}});
    }
    std::sort(scopes.begin(),scopes.end());scopes.erase(std::unique(scopes.begin(),scopes.end()),scopes.end());
    if (!lease(scopes)) {
        if(!waiting_history || waiting_history->gesture!=target->operation || waiting_history->redo!=redo)
            waiting_history=WaitingHistory{current_gesture_scope(),native.generation,now_ms(),target->operation,target->transition_revision,redo};
        return;
    }
    waiting_history.reset();
    auto r = request(redo ? editor::Intent::Redo : editor::Intent::Undo);
    std::string error;
    awaiting_operation = r.operation;
    stage_stamp(controller->state().document.project,r.operation,controller->state().document.revision,editor_stage_metrics::Stage::LocalSubmit);
    if (!controller->submit(std::move(r), error)) { awaiting_operation.reset(); tell(error); }
}
void close_session(bool preserve_offer = false) {
    waiting_history.reset();
    queued_exports.clear();queued_solo.reset();deferred_test.reset();creation_actions.clear();complexity_request.reset();
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
    if(pending && pending->tentative && !native_identity_lost){
        if(native.generation!=native_generation || !teardown_scope_matches())native_identity_lost=true;
        else {
            if(editor_native_save::busy() || !native.ready || native.gesture){
                status="Finishing the native gesture or save before ending shared editing";return;
            }
            std::string error;
            if(!restore_tentative(error) && !native_identity_lost){
                frozen=true;tell("Ending shared editing is waiting for canonical restoration: "+error);return;
            }
        }
    }
    const bool owned_context = native_attached && !native_identity_lost && native.generation == native_generation && teardown_scope_matches();
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
    if(owned_context && editor_native::state().editing)retained_editor_generation=editor_native::state().generation;
    const auto saved = controller->saved_coverage();
    if (!keep_offer && !frozen && owned_context && native_copy_owned && native.ready && native.editing &&
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
    local_creation.reset();
    guarded_settings.clear();guarded_authoring.clear();cast_uploads.clear();cast_upload_errors.clear();solo_preview.reset();solo_ready_scope.reset();pending_solo_return.reset();pending_solo_request=0;
    recovery.reset(); beginning = false; frozen = false; save_publication_pending = false;
    end_after_return = false; disconnected_return = false; pending_saved.reset();
    pending_preview_return.reset();
    adventure_messages.clear(); adventure_message_bytes = 0;
    store.close(); closing = false; native_attached = close_restored = close_preserve_offer = false;
    if(host() && C.session) {
        automatic_generation=editor_native::state().generation;
        automatic_transport=C.session->transport_generation();
    }
    tell(keep_offer ? "The previous playtest ended. Save your draft and return to Galaxy to join the new editing offer." :
        "Shared editing ended. The private native draft and durable recovery files are preserved.");
}
bool solo_scene_ready(std::string& error){
    if(!solo_preview)return true;
    auto& solo=*solo_preview;
    if(!solo.scene_restored){
        if(!editor_native::state().ready)return false;
        if(!editor_native::restore(solo.snapshot,error))return false;
        solo.scene_restored=true;solo.generation=editor_native::state().generation;solo.returned_frame=H->frame_index();
    }
    return editor_native::state().ready && H->frame_index()>solo.returned_frame;
}
bool return_ready(const editor::wire::Control& control) {
    using K = editor::wire::ControlKind;
    if (control.kind != K::HostReturnEditor && control.kind != K::HostCancelPlaytest) return true;
    if(controller && controller->view().playtest_mode==editor::wire::PlaytestMode::Solo){
        if(!solo_preview)return editor_native::state().ready;
        if(!return_error.empty())return true;
        auto& solo=*solo_preview;
        if(!same_preview_context(solo.scope) || editor_native::state().generation!=solo.generation){return_error="The solo preview belongs to a replaced editor context";return true;}
        if(editor_native::state().playtesting){
            if(!editor_native_save::playtest(false,return_error))return true;
            solo.generation=editor_native::state().generation;solo.returned_frame=H->frame_index();return false;
        }
        if(solo.returned_frame && H->frame_index()<=solo.returned_frame)return false;
        if(!editor_native::state().ready)return false;
        editor_native::LocalView view;std::string error;
        if(!solo_scene_ready(error)){if(!error.empty()){return_error=error;return true;}return false;}
        if(editor_native::capture_view(view,error)){solo.view_wait_started=0;return true;}
        if(!solo.view_wait_started)solo.view_wait_started=now_ms();
        if(now_ms()-solo.view_wait_started>=5000){return_error="The native editor camera did not become available after solo testing: "+error;return true;}
        return false;
    }
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
    if(!preview_selection->scene_restored){
        if(!editor_native::restore(current_native,return_error))return true;
        preview_selection->scene_restored=true;preview_selection->returned_generation=editor_native::state().generation;
        preview_selection->scene_frame=H->frame_index();return false;
    }
    if(H->frame_index()<=preview_selection->scene_frame)return false;
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
bool finish_solo(std::string& error){
    if(!return_error.empty()){error=return_error;return false;}
    if(!solo_preview)return true;
    const auto& solo=*solo_preview;
    if(editor_native::state().generation!=solo.generation){error="The solo preview editor changed before restoration";return false;}
    if(!solo_scene_ready(error))return false;
    if(!editor_native::select_act(solo.snapshot.act,error) ||
       (solo.selected && !editor_native::select_placement(*solo.selected,error)) ||
       !editor_native::restore_view(solo.view,solo.generation,error))return false;
    solo_preview.reset();solo_ready_scope.reset();native_playtesting=false;
    native_generation=editor_native::state().generation;return_error.clear();
    status="Solo test finished. Shared editing resumed";return true;
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
    if ((!preview_selection || !preview_selection->scene_restored) && !editor_native::restore(current_native, error)) return false;
    const auto return_generation=editor_native::state().generation;
    if(preview_selection && !editor_native::select_act(preview_selection->act,error))return false;
    // Native Stop clears the editor selection. Preserve the independent local
    // choice made immediately before this round, not a committed snapshot's UI.
    if (selected && (current_native.document.placements.contains(*selected) || *selected==editor_native::kCaptainObject) &&
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
    if(solo_preview){
        if(editor_native::state().generation!=solo_preview->generation){solo_preview.reset();}
        else {
            if(editor_native::state().playtesting){
                if(!editor_native_save::playtest(false,return_error)){frozen=true;tell(return_error);return;}
                solo_preview->generation=editor_native::state().generation;solo_preview->returned_frame=H->frame_index();return;
            }
            if(!editor_native::state().ready || H->frame_index()<=solo_preview->returned_frame)return;
            std::string error;if(!finish_solo(error)){if(!error.empty()){frozen=true;tell(error);}return;}
        }
    }
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
    // Rehosting a durable private snapshot does not publish a native creation.
    // Keep ownership false so its first explicit Save still uses Save As.
    if (recovery->needs_copy) native_copy_owned=false;
    if (recovery->saved_key) {
        editor_native::Snapshot native;
        auto content = resources;
        auto key = *recovery->saved_key;
        const dbpf::Key root{key.instance, key.type, key.group};
        std::vector<dbpf::Record> records;
        if (!editor_native::capture(native, error)) { recovery.reset(); frozen = true; tell(error); return; }
        native.source = key;
        if (!editor_native_save::read_saved(key, native.bytes, error) ||
            !editor_native::normalize_saved_grounding(native,recovery->state.document,error) ||
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
    current_native = std::move(work.native);rendered_revision=current_native.document.revision; native_generation = editor_native::state().generation;
    native_attached = true; native_identity_lost=false;
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
uint64_t last_host_command=0;
bool idle_native() {
    const auto n=editor_native::state();
    return n.ready && !n.gesture && n.generation==native_generation && !pending && !awaiting_operation &&
        !editor_native_save::busy() && !save_publication_pending && !pending_saved;
}
bool send_command(Command command,uint8_t to=255) {
    std::string error;
    if(controller->send_command(command,error,to))return true;
    tell(error);return false;
}
Command command(CommandKind kind,editor::Peer actor,uint64_t serial=0,bool accepted=false) {
    return {kind,serial?serial:++command_serial,controller->state().document.revision,actor,accepted};
}
void end_export(bool success) {
    if(!exporting)return;
    auto end=exporting->command;end.kind=CommandKind::EndExport;end.accepted=success;
    send_command(end);last_host_command=std::max(last_host_command,end.serial);exporting.reset();
    tell(success?"Local copy saved; shared editing resumed":"Local copy saving stopped; the shared draft is preserved");
}
void request_export(const Command& requested) {
    const auto& view=controller->view();
    if(host() && local_creation && !exporting && !native_exit_requested){
        queued_exports.insert_or_assign(requested.actor,requested);
        auto reply=requested;reply.kind=CommandKind::ExportQueued;send_command(reply,requested.actor.slot);
        tell("The adventure Save is queued until the host returns; shared editing can continue");return;
    }
    if(!host() || exporting || native_exit_requested || !idle_native() ||
        std::any_of(view.members.begin(),view.members.end(),[&](const auto& p){return p.second.phase!=Phase::Ready || p.second.applied_revision!=view.context.revision;})) {
        auto reply=requested;reply.kind=CommandKind::EndExport;reply.accepted=false;send_command(reply,requested.actor.slot);return;
    }
    ExportBarrier barrier;barrier.command=command(CommandKind::HoldExport,requested.actor);
    for(const auto& [slot,member]:view.members){(void)member;barrier.participants|=uint8_t(1u<<slot);}
    exporting=barrier;send_command(barrier.command);
    tell("Saving a collaborator's local copy; authoring is held briefly");
}
void finish_export(bool saved) {
    std::string error;
    if(export_before) {
        const auto before=std::move(*export_before);export_before.reset();
        if(editor_native::state().generation!=export_generation || !editor_native::restore(before,error) ||
            !editor_native_save::restore_identity(before.source,before.local_dirty.value_or(editor_native::Snapshot::Dirty{}),error) ||
            !check_native_integrity(error)) {
            frozen=true;tell("The local copy operation could not restore the shared working draft: "+error);saved=false;
        }
    }
    if(!saved)exit_after_save=false;
    if(exporting) {
        auto done=exporting->command;done.kind=CommandKind::FinishExport;done.actor=controller->view().local;done.accepted=saved;
        if(host())end_export(saved);else {send_command(done);exporting->finished=true;}
    }
    if(saved)tell("A local adventure copy was saved; the shared draft is unchanged");
}
void receive_command(uint8_t from,const Command& value) {
    const auto& view=controller->view();
    if(host()) {
        auto member=view.members.find(from);
        if(!from || member==view.members.end() || member->second.peer!=value.actor ||
           (member->second.phase!=Phase::Ready && !(value.kind==CommandKind::ReturnSolo && member->second.phase==Phase::Playing)))return;
        if(seen_command_peers[from]!=member->second.peer){seen_command_peers[from]=member->second.peer;seen_commands[from]=0;}
        if(value.kind==CommandKind::ReturnSolo){
            if(view.playtest_mode==editor::wire::PlaytestMode::Solo && view.phase==Phase::Playing && value.actor==view.playtest_tester && value.serial==view.playtest_round){std::string error;if(!controller->return_editor(error))tell(error);}
            return;
        }
        if(value.kind==CommandKind::RequestSolo){
            if(value.serial<=seen_commands[from])return;
            seen_commands[from]=value.serial;
            if(creation_sessions_active()){queued_solo=value;tell("Solo testing is queued until creation editors finish or cancel");return;}
            auto reply=value;reply.kind=CommandKind::ReplySolo;
            std::string error;
            reply.accepted=!test_request && !exporting && !native_exit_requested && cached.can_playtest && check_native_integrity(error) &&
                controller->prepare_playtest(error,editor::wire::PlaytestMode::Solo,value.actor);
            send_command(reply,from);if(!error.empty())tell(error);return;
        }
        if(value.kind==CommandKind::RequestPlaytest || value.kind==CommandKind::RequestExport) {
            if(value.serial<=seen_commands[from])return;
            seen_commands[from]=value.serial;
            if(value.kind==CommandKind::RequestExport){request_export(value);return;}
            if(test_request || exporting || native_exit_requested) {
                auto reply=value;reply.kind=CommandKind::ReplyPlaytest;send_command(reply,from);
            } else {test_request=value;tell(C.session->name_of(from)+" requests a group playtest");}
            return;
        }
        if(!exporting || value.serial!=exporting->command.serial || value.revision!=exporting->command.revision)return;
        if(value.kind==CommandKind::ExportHeld)exporting->held|=uint8_t(1u<<from);
        if(value.kind==CommandKind::FinishExport && value.actor==exporting->command.actor && exporting->started)end_export(value.accepted);
        return;
    }
    if(from)return;
    if(value.kind==CommandKind::ExportQueued){
        if(value.actor==view.local && value.serial==pending_export_request){pending_export_request=0;tell("Save is queued until the host returns. You can continue editing");}return;
    }
    if(value.kind==CommandKind::ReplySolo){
        if(value.actor!=view.local || value.serial!=pending_solo_request)return;
        pending_solo_request=0;if(!value.accepted)tell("The shared draft is busy. Try solo Play after edits settle");return;
    }
    if(value.kind==CommandKind::ReplyPlaytest) {
        if(value.actor!=view.local || value.serial!=pending_test_request)return;
        pending_test_request=0;tell(value.accepted?"The host approved the group playtest":"The host declined or could not start the playtest");return;
    }
    if(value.kind==CommandKind::HoldExport) {
        if(value.serial<=last_host_command || exporting || native_exit_requested)return;
        last_host_command=value.serial;ExportBarrier barrier;barrier.command=value;exporting=barrier;
        pending_export_request=0;tell("Saving a collaborator's local copy; authoring is held briefly");return;
    }
    if(value.kind==CommandKind::ExitEditor) {
        if(value.serial<=last_host_command)return;
        last_host_command=value.serial;remote_exit=true;native_exit_requested=native_exit_confirmed=true;return;
    }
    if(value.kind==CommandKind::EndExport && !exporting && pending_export_request==value.serial && value.actor==view.local) {
        pending_export_request=0;exit_after_save=false;tell("The host is busy; try saving your local copy again shortly");return;
    }
    if(!exporting || value.serial!=exporting->command.serial || value.actor!=exporting->command.actor)return;
    if(value.kind==CommandKind::BeginExport)exporting->started=true;
    if(value.kind==CommandKind::EndExport) {
        exporting.reset();pending_export_request=0;
        if(exit_after_save) {if(value.accepted)native_exit_requested=native_exit_confirmed=true;else exit_after_save=false;}
        tell(value.accepted?"Local copy saved; shared editing resumed":"Local copy saving was cancelled or failed; the shared draft is preserved");
    }
}
void service_export() {
    if(!exporting && host() && !local_creation && idle_native() && controller->settled()){
        for(auto it=queued_exports.begin();it!=queued_exports.end();){
            auto member=controller->view().members.find(it->first.slot);
            if(member==controller->view().members.end() || member->second.peer!=it->first){it=queued_exports.erase(it);continue;}
            if(member->second.presence.creating_asset || !member->second.presence.scene_ready){++it;continue;}
            const auto request=it->second;queued_exports.erase(it);request_export(request);break;
        }
    }
    if(!exporting)return;
    if(host()) {
        const auto& members=controller->view().members;
        for(uint8_t slot=0;slot<4;++slot)if((exporting->participants>>slot)&1) {
            auto it=members.find(slot);
            if(it==members.end() || it->second.phase!=Phase::Ready) {end_export(false);return;}
        }
    }
    if(!exporting->acknowledged && (idle_native() || (local_creation && !durable_commit && !awaiting_operation))) {
        exporting->acknowledged=true;
        if(host())exporting->held|=1;
        else {auto held=exporting->command;held.kind=CommandKind::ExportHeld;held.actor=controller->view().local;send_command(held);}
    }
    if(host() && !exporting->started && exporting->held==exporting->participants && controller->settled()) {
        exporting->started=true;auto begin=exporting->command;begin.kind=CommandKind::BeginExport;send_command(begin);
    }
    if(exporting && exporting->started && !exporting->finished && !export_before && exporting->command.actor==controller->view().local && idle_native()) {
        std::string error;editor_native::Snapshot before;
        if(!editor_native::capture(before,error)){tell(error);finish_export(false);return;}
        before.document=controller->state().document;export_generation=native_generation;export_before=std::move(before);
        if(!editor_native_save::save_as(error)){tell(error);finish_export(false);}
    }
}
void service_automatic_follow() {
    if(!C.automatic_follow || !C.session || !C.session->connected() || closing || recovery || frozen || native_attached ||
        editor_native_save::busy() || native_exit_requested || handoff || session_active() ||
        (C.adventure_busy && C.adventure_busy()))return;
    const auto n=editor_native::state();const auto transport=C.session->transport_generation();
    // Native Exit queues its application transition. Do not interpret its last
    // Build frame as a newly opened draft, including across a party reconnect.
    if(n.editing && n.generation==native_exit_generation)return;
    if(!n.editing)native_exit_generation=0;
    if(host() && n.ready && !n.gesture && n.act<8) {
        if(automatic_generation==n.generation && automatic_transport==transport)return;
        automatic_generation=n.generation;automatic_transport=transport;follow_failed=false;
        start_saved();follow_failed=!native_attached;return;
    }
    if(!host() && controller->view().phase==Phase::Offered) {
        if(follow_failed)return;
        if(n.editing && n.generation!=retained_editor_generation){handoff=true;++handoff_prompt_serial;return;}
        if(game::mode_id()==modes::kMenu || (n.ready && n.generation==retained_editor_generation)) {
            std::string error;if(!controller->accept_offer(error)){follow_failed=true;tell(error);}
        }
    }
}
void service_native_exit() {
    if(!native_exit_requested || !native_exit_confirmed || editor_native_save::busy())return;
    if(handoff) {
        std::string error;
        const auto retiring_generation=editor_native::state().generation;
        if(!editor_native_save::complete_exit(error)){tell(error);return;}
        native_exit_generation=retiring_generation;
        handoff=false;native_exit_requested=native_exit_confirmed=false;handoff_saving=false;return;
    }
    if(native_attached) {
        if(!idle_native() || (host() && !controller->settled()))return;
        if(host() && !exit_broadcast) {
            auto leave=command(CommandKind::ExitEditor,controller->view().local);
            if(!send_command(leave))return;exit_broadcast=true;return;
        }
        if(host() && !controller->settled())return;
        close_session();if(closing)return;
    }
    std::string error;
    const auto retiring_generation=editor_native::state().generation;
    if(!editor_native_save::complete_exit(error)){tell(error);return;}
    native_exit_generation=retiring_generation;
    retained_editor_generation=0;
    const bool leave_party=!host() && !remote_exit;
    native_exit_requested=native_exit_confirmed=exit_after_save=exit_broadcast=remote_exit=false;
    if(leave_party && C.leave_party)C.leave_party();
}
void execute(Action action) {
    if (closing && action != Action::End) return;
    if(controller->view().phase==Phase::Ready && creation_sessions_active() &&
       (action==Action::Solo || action==Action::ChooseTest || action==Action::Playtest || action==Action::ApproveTest)){
        deferred_test=DeferredTest{action,current_gesture_scope(true)};
        tell("Playtest is queued until creation editors finish or cancel; shared editing can continue");return;
    }
    std::string error;
    switch (action) {
    case Action::Begin:
        follow_failed=false;automatic_generation=automatic_transport=0;
        if(editor_native::state().generation==native_exit_generation)break;
        if(!host() && (frozen || controller->view().phase==Phase::Frozen)) {
            frozen=false;close_session();break;
        }
        reopen_saved.reset();
        if (controller->view().phase == Phase::Offered) {
            if (game::mode_id() != modes::kMenu) { tell("Save your local draft and return to Galaxy before joining shared editing"); break; }
            if (!controller->accept_offer(error)) tell(error);
        }
        else if (host() && !session_active() && editor_native::state().editing && !beginning && (!C.adventure_busy || !C.adventure_busy())) {
            if (!supported_native_act(error)) { tell(error); break; }
            start_saved();
        }
        break;
    case Action::Undo: undo_redo(false); break;
    case Action::Redo: undo_redo(true); break;
    case Action::Save: {
        const auto native = editor_native::state();
        if(!host()) {
            if(cached.can_save && !exporting && !pending_export_request) {
                auto request=command(CommandKind::RequestExport,controller->view().local);
                if(send_command(request))pending_export_request=request.serial;
            }
            break;
        }
        test_request.reset();
        if (cached.can_save && controller->view().phase == Phase::Ready && !frozen && !recovery &&
            native.ready && !native.gesture && native.generation == native_generation && !editor_native_save::busy() &&
            check_native_integrity(error) && !(native_copy_owned ? editor_native_save::save(error) : editor_native_save::save_as(error))) tell(error);
        break;
    }
    case Action::Solo:
        if(!cached.can_playtest || pending || exporting || pending_export_request || native_exit_requested)break;
        if(host()){
            if(check_native_integrity(error) && !controller->prepare_playtest(error,editor::wire::PlaytestMode::Solo,controller->view().local))tell(error);
        }else if(!pending_solo_request){
            auto request=command(CommandKind::RequestSolo,controller->view().local);
            if(send_command(request))pending_solo_request=request.serial;
        }
        break;
    case Action::ChooseTest:
        if(cached.can_playtest){
            if(test_menu)test_menu.reset();
            else test_menu=TestMenu{current_gesture_scope(),native_generation,controller->state().document.revision,editor_native::state().act};
        }
        break;
    case Action::MenuSolo: case Action::MenuTogether:
        if(test_menu && cached.test_menu && test_menu->scope==current_gesture_scope() && test_menu->generation==native_generation &&
           test_menu->revision==controller->state().document.revision && test_menu->act==editor_native::state().act){
            test_menu.reset();execute(action==Action::MenuSolo?Action::Solo:Action::Playtest);
        }
        break;
    case Action::MenuDismiss:test_menu.reset();break;
    case Action::Playtest:
        if(controller->view().phase==Phase::Preparing) {
            if(!controller->ready_playtest(!cached.ready,error))tell(error);break;
        }
        if(!host()) {
            if(controller->view().phase==Phase::Ready && !exporting && !pending_export_request && !native_exit_requested) {
                auto request=command(CommandKind::RequestPlaytest,controller->view().local);
                if(send_command(request))pending_test_request=request.serial;
            }
            break;
        }
        if (controller->view().phase == Phase::Playing) { if (host() && !controller->return_editor(error)) tell(error); }
        else if (cached.can_playtest && !pending && !save_publication_pending && !editor_native_save::busy() &&
                 editor_native::state().ready && !editor_native::state().gesture && check_native_integrity(error) && !controller->prepare_playtest(error,editor::wire::PlaytestMode::Together,{},logical_act(editor_native::state().act))) tell(error);
        break;
    case Action::ApproveTest: case Action::DeclineTest:
        if(host() && test_request) {
            auto reply=*test_request;test_request.reset();reply.kind=CommandKind::ReplyPlaytest;
            reply.revision=controller->state().document.revision;
            reply.accepted=action==Action::ApproveTest && cached.can_playtest && !exporting &&
                check_native_integrity(error) && controller->prepare_playtest(error,editor::wire::PlaytestMode::Together,{},logical_act(editor_native::state().act));
            send_command(reply,reply.actor.slot);if(!error.empty())tell(error);
        }
        break;
    case Action::SaveExit:
        if(cached.can_save) {exit_after_save=true;execute(Action::Save);}
        break;
    case Action::NativeExit:
        test_request.reset();
        if(controller->view().playtest_mode==editor::wire::PlaytestMode::Solo && controller->view().phase==Phase::Playing){
            exit_after_return=true;
            if(host()){if(!controller->cancel_playtest(error))tell(error);}
            else if(controller->view().local==controller->view().playtest_tester)execute(Action::Return);
            else {if(C.leave_party)C.leave_party();}
            break;
        }
        if(controller->view().phase==Phase::Preparing || controller->view().phase==Phase::Returning) {
            if(host() || controller->view().phase==Phase::Returning) {
                exit_after_return=true;
                if(controller->view().phase==Phase::Preparing && !controller->cancel_playtest(error))tell(error);
                break;
            }
            // A guest cancels its participation before showing the ordinary exit
            // prompt. Its scoped Leave cancels this immutable group round.
            close_session();
            if(closing)break;
            native_exit_requested=true;
            if(!editor_native_save::request_exit(error)){native_exit_requested=false;tell(error);}
            break;
        }
        if(!exporting && !pending_export_request && !native_exit_requested && idle_native()) {
            native_exit_requested=true;
            if(!editor_native_save::request_exit(error)) {native_exit_requested=false;tell(error);}
        }
        break;
    case Action::HandoffSave:
        if(handoff && !handoff_saving && !editor_native_save::busy()) {
            handoff_saving=true;
            if(!editor_native_save::save_as(error)){handoff_saving=false;++handoff_prompt_serial;tell(error);}
        }
        break;
    case Action::HandoffDiscard:
        if(handoff && !handoff_saving && !native_exit_requested) {
            native_exit_requested=true;
            if(!editor_native_save::request_exit(error)){native_exit_requested=false;++handoff_prompt_serial;tell(error);}
        }
        break;
    case Action::HandoffCancel:
        handoff=handoff_saving=false;
        if(C.leave_party)C.leave_party();
        break;
    case Action::CancelPreparation:
        if (host() && (controller->view().phase == Phase::Preparing || controller->view().phase == Phase::Playing) &&
            !controller->cancel_playtest(error)) tell(error);
        break;
    case Action::Ready: case Action::Unready: if (!controller->ready_playtest(action == Action::Ready, error)) tell(error); break;
    case Action::End:
        if(controller->view().playtest_mode==editor::wire::PlaytestMode::Solo && controller->view().phase==Phase::Playing){
            end_after_return=true;
            if(host()){if(!controller->cancel_playtest(error))tell(error);}
            else if(controller->view().local==controller->view().playtest_tester)execute(Action::Return);
            else close_session();
            break;
        }
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
        if(controller->view().playtest_mode==editor::wire::PlaytestMode::Solo){
            const auto& view=controller->view();
            if(host() && view.phase==Phase::Playing){if(!controller->return_editor(error))tell(error);}
            else if(!host() && view.phase==Phase::Playing && view.local==view.playtest_tester && solo_preview && same_preview_context(solo_preview->scope)){
                auto returned=command(CommandKind::ReturnSolo,view.local);returned.serial=view.playtest_round;send_command(returned);
            }
            break;
        }
        if (!adventure_native::editor_preview()) break;
        if (host() && controller->view().phase == Phase::Playing) { if (!controller->return_editor(error)) tell(error); }
        else if (host() && controller->view().phase == Phase::Preparing) { if (!controller->cancel_playtest(error)) tell(error); }
        else if (!host() && !disconnected_return && (controller->view().phase == Phase::Playing || controller->view().phase == Phase::Preparing))
            tell("The host returns the party to the adventure draft");
        break;
    case Action::Recover: {
        if(editor_creation::active()){tell("Finish or cancel the current creation before recovering an adventure draft");break;}
        reopen_saved.reset();
        if (!host() || !recovery_available) break;
        if(native_identity_lost){tell("Return to Galaxy before recovering this draft into a different native editor");break;}
        if(!restore_tentative(error)){frozen=true;tell("Recover the committed draft: "+error);break;}
        if (!store.healthy() && !store.open(project_root, project_id, error)) { tell(error); break; }
        auto checkpoint = store.snapshot();
        editor::State recovered; editor_native::Snapshot native;
        editor::Peer recovered_capability; bool owned = false;
        if (!editor_project::load_snapshot(checkpoint, recovered, native, error) ||
            !supported_act(native.act, error) || !supported_native_act(error) ||
            !parse_manifest(checkpoint.manifest, resource_hash, history_artifacts, error, &recovered_capability, &owned)) { tell(error); break; }
        if(!expand_legacy_state(recovered,native,error)){tell("The legacy checkpoint was preserved: "+error);break;}
        std::vector<uint8_t> envelope;
        if (!read_blob(resource_hash, envelope, editor::wire::kResourceBytes, error) || !editor_project::decode_resources(envelope, resources, error) ||
            !replace_root(resources, native, error) || !editor_project::encode_resources(resources, envelope, error) ||
            !register_native_records(resources.root,resources.bundle.records, error)) { tell(error); break; }
        const auto resume = recovered_capability.slot == 0 ? std::optional<editor::Peer>(recovered_capability) : std::nullopt;
        if(!editor_native::validate_entry(native.bytes,native.act,error)){tell(error);break;}
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
    // Controller work runs after the frame's receive/send pass. Publish its
    // queued messages immediately without another receive pass or callbacks.
    // The captured transport cannot send through a restarted Session.
    struct SendPending {
        net::Session* session = C.session;
        uint64_t transport = session ? session->transport_generation() : 0;
        ~SendPending() {
            try {
                if (C.session == session && session && transport == observed_transport && transport > retired_transport)
                    session->flush_pending(transport);
            } catch (...) { /* Transport allocation failure retries on update(). */ }
        }
    } send_pending;
    try {
        observe_transport();
        // The single in-flight commit owns Store until the worker has joined.
        // Transport and keepalives continue, while scene replacement, Save and
        // recovery maintenance wait for a known durable outcome.
        if(durable_commit){controller->tick(now_ms());if(durable_commit){refresh();return;}}
        if (transport_teardown) { service_transport_teardown(); refresh(); return; }
        submit_pending_gesture();
        controller->tick(now_ms());
        if(durable_commit){refresh();return;}
        if(!creation_actions.empty() && !editor_creation::active() && !local_creation && !durable_commit && controller->view().phase==Phase::Ready){
            const auto action=creation_actions.front();creation_actions.pop_front();
            editor::ResourceKey source{0,action.type,0};std::string error;
            if(action.target){
                for(const auto& [id,row]:controller->state().document.class_settings)if(row.class_id==*action.target){
                    (void)id;auto field=row.properties.find(editor::kClassAssetResourceProperty);
                    if(field!=row.properties.end())if(auto key=std::get_if<editor::ResourceKey>(&field->second))source=*key;
                }
                if(!source.instance)error="The selected cast entry has no editable creation content";
            }
            if(error.empty() && !editor_creation::open(source.type,source,action.target,error) && error.empty())error="The native creation editor is unavailable";
            if(!error.empty())tell(error);
        }
        if(local_creation && !editor_creation::active() && !durable_commit){
            std::string error;
            if(materialize_creation_return(error))local_creation.reset();
            else if(!error.empty()){frozen=true;tell("The creation was retained locally, but the committed adventure could not be restored: "+error);}
        }
        observe_lease_grants();
        if(complexity_request && controller->view().phase==Phase::Ready && !pending && !awaiting_operation && !durable_commit && !local_creation && !frozen && !closing){
            if(*complexity_request && !editor_native::extended_complexity_available()){
                complexity_request.reset();tell("Extended Complexity is unavailable because native tuning conflicts with an installed override");
            }
        }
        if(complexity_request && controller->view().phase==Phase::Ready && !pending && !awaiting_operation && !durable_commit && !local_creation && !frozen && !closing){
            const editor::FieldKey field{editor::Field::Property,editor_native::kExtendedComplexityProperty};
            const auto& doc=controller->state().document;
            if(doc.settings.contains(editor_native::kProjectObject) && lease({{editor::ScopeKind::Field,editor_native::kProjectObject,field}})){
                std::string error;
                if(check_native_integrity(error)){
                    auto operation=request(editor::Intent::Forward);operation.summary="Change adventure complexity budget";
                    editor::Mutation mutation;mutation.target=editor_native::kProjectObject;mutation.field=field;mutation.value=*complexity_request;
                    mutation.expected=editor::field_revision(doc,mutation.target,field);operation.mutations.push_back(std::move(mutation));
                    if(controller->submit(operation,error))awaiting_operation=operation.operation;
                }
                complexity_request.reset();if(!error.empty())tell(error);
            }
        }
        if(waiting_history){
            const auto held=*waiting_history;
            const auto& history=controller->state().history;
            const bool same=held.scope==current_gesture_scope() && held.generation==editor_native::state().generation && now_ms()-held.started<=2000 &&
                std::any_of(history.begin(),history.end(),[&](const auto& item){return item.operation==held.gesture && item.transition_revision==held.transition && item.applied!=held.redo;});
            if(!same || frozen || closing || awaiting_operation)waiting_history.reset();
            else undo_redo(held.redo,held.gesture);
        }
        if(waiting_acts){
            const auto held=*waiting_acts;const auto native=editor_native::state();
            if(held.identity.scope!=current_gesture_scope() || held.identity.generation!=native.generation || held.identity.revision!=controller->state().document.revision ||
               held.identity.act!=native.act || now_ms()-held.started>2000 || frozen || closing || awaiting_operation || controller->view().phase!=Phase::Ready)waiting_acts.reset();
            else if(native.ready && !native.gesture && lease({{editor::ScopeKind::Authoring,{},{}}})){
                waiting_acts.reset();const auto& doc=controller->state().document;
                guarded_settings=doc.class_settings;guarded_authoring=doc.settings;guarded_placements=doc.placements;
                std::string error;if(!editor_native::edit_acts(held.action,doc,error))tell(error);
            }
        }
        if(controller->view().phase==Phase::Preparing && controller->view().playtest_mode==editor::wire::PlaytestMode::Solo){
            const auto& view=controller->view();
            if((!solo_ready_scope || !same_preview_context(*solo_ready_scope)) && editor_native::state().ready && !pending && !awaiting_operation && !editor_native::state().gesture){
                std::string error;
                if(check_native_integrity(error) && controller->ready_playtest(true,error))solo_ready_scope=PreviewReturnScope{view.context.project,view.context.epoch,view.playtest_round,view.playtest_revision};
            }
        }
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
        if(deferred_test && !creation_sessions_active() && cached.can_playtest && controller->settled()){
            const auto waiting=*deferred_test;deferred_test.reset();
            if(waiting.scope==current_gesture_scope(true))work.push_back(waiting.action);
        }
        if(queued_solo && host() && !creation_sessions_active() && cached.can_playtest && controller->settled()){
            auto waiting=*queued_solo;queued_solo.reset();auto member=controller->view().members.find(waiting.actor.slot);
            if(member!=controller->view().members.end() && member->second.peer==waiting.actor){
                std::string error;waiting.kind=CommandKind::ReplySolo;waiting.revision=controller->state().document.revision;
                waiting.accepted=check_native_integrity(error) && controller->prepare_playtest(error,editor::wire::PlaytestMode::Solo,waiting.actor);
                send_command(waiting,waiting.actor.slot);if(!error.empty())tell(error);
            }
        }
        for (auto action : work) execute(action);
        auto cancelling = std::move(pending_preparation_cancel); pending_preparation_cancel.reset();
        if (cancelling && current_preparation_cancel(*cancelling, true)) execute(Action::CancelPreparation);
        auto returning = std::move(pending_preview_return); pending_preview_return.reset();
        if (returning && current_preview_return(*returning)) execute(Action::Return);
        auto solo_return=std::move(pending_solo_return);pending_solo_return.reset();
        if(solo_return && controller->view().playtest_mode==editor::wire::PlaytestMode::Solo && same_preview_context(*solo_return))execute(Action::Return);
        service_export();service_native_exit();service_automatic_follow();
        if(exit_after_return && controller->view().phase==Phase::Ready && !adventure_native::editor_preview()) {
            exit_after_return=false;execute(Action::NativeExit);
        }
        if (session_active() && !closing && controller->view().phase == Phase::Ready && !frozen) {
            auto native = editor_native::state();
            if (!recovery && !detached_scene() && (!native.editing || native.generation != native_generation)) { frozen = true; tell("The native editor changed context. Recover or end shared editing."); }
            if (now_ms() - last_renew >= 4000) {
                last_renew = now_ms(); std::vector<editor::LeaseToken> tokens,unused;
                for (const auto& item : controller->view().local_leases){
                    const auto used=lease_use.find(item.scope);
                    const bool keep=native.gesture || pending || awaiting_operation ||
                        (local_creation && local_creation->request.target && item.scope.kind==editor::ScopeKind::Content && item.scope.object==*local_creation->request.target) ||
                        (used!=lease_use.end() && now_ms()-used->second<5000);
                    (keep?tokens:unused).push_back({item.scope,item.token});
                }
                std::string error;
                if(!tokens.empty())controller->renew_leases(tokens,15000,error);
                if(!unused.empty()){controller->release_leases(unused,error);requested_scopes.clear();}
            }
            editor::wire::Presence presence;
            if (!native.selected.empty() && (controller->state().document.placements.contains(native.selected.front()) ||
                (native.selected.front()==editor_native::kCaptainObject && controller->state().document.settings.contains(editor_native::kCaptainObject))))presence.selected=native.selected.front();
            presence.act = logical_act(native.act); presence.editing = native.gesture;
            presence.pending = awaiting_operation.has_value();
            presence.scene_revision=rendered_revision;presence.scene_ready=!detached_scene() && native.ready && !native.gesture && !durable_commit && !(pending && pending->tentative);
            if(local_creation){presence.creating_asset=true;presence.creation_serial=local_creation->scope.serial;
                presence.creation_target=local_creation->request.target.value_or(editor::ObjectId{});
                presence.act=controller->state().document.acts.contains(local_creation->authored_act)?local_creation->authored_act:logical_act(0);presence.selected={};presence.editing=false;}
            if (now_ms() - last_presence >= 300 || !last_presence_value || *last_presence_value != presence) {
                last_presence = now_ms(); std::string error;
                if(controller->set_presence(presence,error))last_presence_value=presence;
            }
            if (now_ms()-last_preview>=100) {
                last_preview=now_ms();editor_native::LivePreview hint;std::string error;
                if(!recovery && !awaiting_operation && native.generation==native_generation &&
                   editor_native::selection_preview(hint) &&
                   controller->set_preview(hint.object,hint.act,hint.pose,error,hint.class_id,hint.creating,hint.destination))preview_active=true;
                else if(preview_active){controller->clear_preview();preview_active=false;}
            }
        }
        else if(preview_active){controller->clear_preview();preview_active=false;last_presence_value.reset();}
        std::vector<editor_native::LivePreview> visual_previews;
        if(!frozen && !recovery && controller->view().phase==Phase::Ready)for(const auto& [slot,member]:controller->view().members){
            if(slot==controller->view().local.slot || !member.preview || member.preview->expires<=now_ms())continue;
            const auto& p=member.preview->value;
            visual_previews.push_back({p.author,p.object,p.act,p.class_id,p.pose,p.creating,p.destination});
        }
        editor_native::update_previews(visual_previews);
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
            if(view.playtest_mode==editor::wire::PlaytestMode::Solo){pending_solo_return=PreviewReturnScope{view.context.project,view.context.epoch,view.playtest_round,view.playtest_revision};schedule();}
            else request_preview_return({view.context.project, view.context.epoch, view.playtest_round, view.playtest_revision});
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
    if(controller || durable_commit)return false;
    frozen=beginning=closing=gc_due=recovery_available=scanned_recovery=native_identity_lost=false;
    recovery.reset();prepared_native.reset();incoming_resources.reset();pending_saved.reset();
    last_presence_value.reset();preview_active=false;
    last_renew=last_presence=last_request=last_lease_use=last_preview=0;
    native_release_qpc=native_capture_begin_qpc=native_capture_end_qpc=0;
    observed_lease_context={};observed_lease_peer={};observed_lease_grants.clear();
    pending.reset();awaiting_operation.reset();waiting_acts.reset();rollback_fence.reset();before_apply_valid=false;
    guarded_placements.clear();guarded_terrain.clear();requested_scopes.clear();lease_use.clear();
    waiting_history.reset();
    H = host_api; M = mod; C = std::move(context); reopen_saved.reset(); projection_override.reset();
    solo_preview.reset();solo_ready_scope.reset();pending_solo_return.reset();pending_solo_request=0;
    guarded_settings.clear();guarded_authoring.clear();cast_uploads.clear();cast_upload_errors.clear();extended_projection_override.reset();cast_projection_override.reset();authoring_projection_override.reset();test_menu.reset();
    automatic_generation=automatic_transport=retained_editor_generation=native_exit_generation=0;follow_failed=false;
    native_exit_requested=native_exit_confirmed=exit_after_save=exit_broadcast=remote_exit=handoff=handoff_saving=false;
    exit_after_return=false;
    exporting.reset();export_before.reset();test_request.reset();pending_export_request=pending_test_request=0;
    command_serial=last_host_command=export_generation=handoff_prompt_serial=0;seen_commands={};seen_command_peers={};source_creation={};
    native_playtesting=return_started=disconnected_return=end_after_return=false;return_error.clear();
    preview_selection.reset();pending_preview_return.reset();pending_preparation_cancel.reset();
    if (!C.session) return false;
    observed_transport = C.session->transport_generation(); retired_transport = 0; transport_teardown.reset(); deferred_offer.reset(); settled_cancellation.reset();
    queued_exports.clear();queued_solo.reset();deferred_test.reset();creation_actions.clear();complexity_request.reset();
    editor_session::Callbacks callbacks;
    callbacks.command=receive_command;
    callbacks.lease_denied=[](const auto& scopes){waiting_history.reset();waiting_acts.reset();editor_native::deny_input(scopes);};
    callbacks.authoring_held=[] {return exporting.has_value() || native_exit_requested;};
    callbacks.trace=[](editor_session::TracePoint point,const editor::ProjectId& project,const editor::OperationId& operation,editor::Revision revision) noexcept {
        using P=editor_session::TracePoint;using S=editor_stage_metrics::Stage;
        const auto stage=point==P::HostRequestReceived?S::HostRequestReceived:point==P::HostDecisionQueued?S::HostDecisionQueued:S::DecisionDecoded;
        stage_stamp(project,operation,revision,stage);
    };
    callbacks.host_hooks.validate = [](const auto&, const auto& after, const auto& change, std::string& error) {
        StageScope span(after,change.operation,editor_stage_metrics::Stage::ValidateBegin,editor_stage_metrics::Stage::ValidateEnd);
        auto native = editor_native::state();
        if (closing || frozen || recovery || exporting || native_exit_requested || save_publication_pending ||
            (!detached_scene() && (native.generation!=native_generation || editor_native_save::busy() || !native.ready || native.gesture))) {
            error="The canonical draft is not currently editable";return false;
        }span.okay=true;return true;
    };
    callbacks.host_hooks.apply = apply; callbacks.host_hooks.rollback = rollback; callbacks.host_hooks.persist = persist;
    callbacks.begin_persist=begin_persist;callbacks.poll_persist=poll_persist;callbacks.settle_persist=settle_persist;
    callbacks.assets = [](editor::OperationId operation,const std::vector<uint8_t>& bytes,std::string& error){
        if(!controller || operation.epoch!=controller->view().context.epoch)return false;
        if(controller->state().decisions.contains(operation))return true;
        // One retained result per current author, fenced by the full operation.
        // Its ordered Request receives the original admission failure.
        std::erase_if(cast_upload_errors,[&](const auto& item){return item.first.author==operation.author;});
        auto rejected=[&]{cast_upload_errors.emplace(operation,error.empty()?"The asset dependency upload was rejected":error.substr(0,256));return false;};
        editor_project::Resources content;
        if(!editor_project::decode_resources(bytes,content,error))return rejected();
        if(!adventure_collect::validate_uploaded_records(content.root,content.bundle.records,error))return rejected();
        auto merged=resources;if(!merge_dependencies(merged,content,error))return rejected();
        std::erase_if(cast_uploads,[&](const auto& item){return item.first.author==operation.author;});
        uint64_t staged=content.bundle.bytes;
        for(const auto& [id,value]:cast_uploads){(void)id;staged+=value.bundle.bytes;}
        if(cast_uploads.size()>=4 || staged>editor::wire::kResourceBytes){error="The cast dependency staging queue is full";return rejected();}
        cast_uploads.emplace(operation,std::move(content));return true;
    };
    callbacks.apply_committed = apply; callbacks.rollback_committed = rollback; callbacks.persist_guest = persist;
    callbacks.authoring_ready = [] {
        auto native = editor_native::state();
        return !closing && !frozen && !recovery && !save_publication_pending &&
            (detached_scene() || (native.generation==native_generation && !editor_native_save::busy() && native.ready && !native.gesture));
    };
    callbacks.rendered_revision=[]()->std::optional<editor::Revision>{
        const auto native=editor_native::state();
        if(detached_scene() || durable_commit || (pending && pending->tentative) || !native.ready || native.gesture || native.generation!=native_generation)return {};
        return rendered_revision;
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
            const auto root=std::find_if(content.bundle.records.begin(),content.bundle.records.end(),[&](const auto& r){return r.key==content.root;});
            if(root==content.bundle.records.end() || !editor_native::validate_entry(root->data,content.act,error))return false;
            if(C.close_panel)C.close_panel();
            if (!register_native_records(content.root,content.bundle.records, error)) return false;
            if (!editor_native::state().editing && !editor_native::enter({content.root.instance, content.root.type, content.root.group}, error)) return false;
        } else {
            editor_project::Resources merged;
            if (!merge_delta(std::move(content), merged, error)) return false;
            content = std::move(merged);
        }
        incoming_resources = std::move(content); incoming_context = scope; return true;
    };
    callbacks.baseline_ready = [](const editor::State&) {
        const auto native=editor_native::state();
        return native.ready && !native.gesture && !editor_native_save::busy();
    };
    callbacks.apply_baseline = [](const editor::State& state, std::string& error) {
        ProjectionScope projection(state.document);
        reopen_saved.reset();
        if (!incoming_resources || incoming_context.project != state.document.project || incoming_context.epoch != state.document.epoch || incoming_context.revision != state.document.revision) {
            error = "The editor baseline has no matching verified resources"; return false;
        }
        editor_native::Snapshot native;
        if (!supported_act(incoming_resources->act, error) || !supported_native_act(error) ||
            !editor_project::native_snapshot(*incoming_resources, state.document, native, error) ||
            !register_native_records(incoming_resources->root,incoming_resources->bundle.records, error)) return false;
        if (!editor_native::state().editing && !editor_native::enter(native.source, error)) return false;
        // Baseline readiness proves the native entry finished. Verify the loaded
        // planet before restoring so an identical recipe keeps its existing
        // terrain and collision instead of scheduling a second regeneration.
        editor_native::Snapshot loaded;
        if (!editor_native::capture_draft(loaded, error)) return false;
        // restore installs and validates this exact document. A terrain rebuild
        // can still be loading after it returns; scene readiness is acknowledged
        // separately rather than applying the same document a second time.
        if (!editor_native::restore(native, error)) return false;
        std::vector<uint8_t> envelope;
        if (!editor_project::encode_resources(*incoming_resources, envelope, error)) return false;
        store.close();
        local_capability = controller->view().local; native_copy_owned = false;
        last_host_command=0;source_creation={};exporting.reset();test_request.reset();pending_export_request=pending_test_request=0;
        if (!create_store(state, native, envelope, "Shared adventure draft", error)) return false;
        current_native = std::move(native); resources = std::move(*incoming_resources); incoming_resources.reset();
        rendered_revision=state.document.revision;
        native_attached = true; native_identity_lost=false;
        history_artifacts.clear(); sequence = 0; frozen = false; native_generation = editor_native::state().generation;
        tell("Joined the shared placement draft"); return true;
    };
    callbacks.decision = [](const editor::Decision& decision) {
        if(decision.accepted()&&decision.change&&awaiting_operation&&decision.change->operation==*awaiting_operation)
            stage_stamp(controller->state().document.project,decision.change->operation,decision.revision,editor_stage_metrics::Stage::OwnConfirmed);
        confirmation_metrics.complete(decision,H->time_seconds());
        const bool own = awaiting_operation && (!decision.change || decision.change->operation == *awaiting_operation);
        if(local_creation && local_creation->operation && own){
            local_creation->accepted=decision.accepted();
            if(!decision.accepted())local_creation->error=decision.reason;
        }
        bool restoration_failed=false;
        if (own) {
            std::string restoration;
            if(!decision.accepted() && !restore_tentative(restoration)){
                frozen=true;restoration_failed=true;tell("Recover the committed draft: "+restoration);
            }
            pending.reset();awaiting_operation.reset();requested_scopes.clear();
            guarded_placements.clear();guarded_settings.clear();guarded_authoring.clear();
        }
        if (decision.accepted()) status = "Placement gesture committed";
        else if(!restoration_failed)tell(decision.reason);
        // Structural ownership lasts for the operation, not the whole session.
        std::vector<editor::LeaseToken> release;
        for (const auto& item : controller->view().local_leases) if (own &&
            !(local_creation && local_creation->request.target && item.scope.kind==editor::ScopeKind::Content && item.scope.object==*local_creation->request.target))
            release.push_back({item.scope, item.token});
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
        if(controller->view().playtest_mode==editor::wire::PlaytestMode::Solo){
            if(control.kind==K::HostBeginPlaytest && controller->view().local==controller->view().playtest_tester){
                if(!check_native_integrity(error))return false;
                SoloPreview local;const auto& view=controller->view();
                local.scope={view.context.project,view.context.epoch,view.playtest_round,view.playtest_revision};
                if(!editor_native::capture(local.snapshot,error) || !editor_native::capture_view(local.view,error))return false;
                const auto selected=editor_native::state().selected;if(!selected.empty())local.selected=selected.front();
                local.generation=editor_native::state().generation;
                return_error.clear();solo_preview=std::move(local);
                if(!editor_native_save::playtest(true,error)){solo_preview.reset();return false;}
                solo_preview->generation=editor_native::state().generation;native_playtesting=true;
                if(C.close_panel)C.close_panel();
            }
            if(control.kind==K::HostReturnEditor || control.kind==K::HostCancelPlaytest)return finish_solo(error);
            return true;
        }
        if (control.kind == K::HostBeginPlaytest) {
            preview_selection.reset();
            if (!check_native_integrity(error)) return false;
            const auto context = controller->view().context;
            PreviewSelection local{{context.project, context.epoch, control.serial, control.revision}, {}, 0, native_generation, 0, std::nullopt};
            local.act=editor_native::state().act;
            if (!editor_native::capture_view(local.view,error)) return false;
            const auto selected = editor_native::state().selected;
            if (selected.size() > 1) { error = "The native preview supports one local placement selection"; return false; }
            if (!selected.empty() && controller->state().document.placements.contains(selected.front())) local.selected = selected.front();
            if(!selected.empty() && selected.front()==editor_native::kCaptainObject)local.selected=selected.front();
            if(!controller->state().document.acts.contains(control.start_act) || !editor_native::select_act(native_act(control.start_act),error))return false;
            adventure_native::EditorPreview preview;
            preview.project = context.project; preview.epoch = context.epoch; preview.round = control.serial; preview.revision = control.revision;
            preview.adventure = resources.root;
            preview.start_act=native_act(control.start_act);
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
        return true;
    };
    controller = std::make_unique<editor_session::Controller>(*C.session, std::move(callbacks));
    editor_native::Ctx native;
    native.gesture_released=[] {native_release_qpc=stage_qpc();};
    native.gesture_capture=[](bool begin){(begin?native_capture_begin_qpc:native_capture_end_qpc)=stage_qpc();};
    native.active = [] { return session_active() || recovery.has_value() || closing || native_attached; };
    native.relative_scale_tracked = [] {
        if (projection_override) return *projection_override;
        if (recovery) return recovery->state.document.schema.contains(editor_native::kRelativeScaleProperty);
        return !native_attached || current_native.document.schema.contains(editor_native::kRelativeScaleProperty);
    };
    native.extended_poses_tracked = [] {
        if(extended_projection_override)return *extended_projection_override;
        if(recovery)return recovery->state.document.schema.contains(editor_native::kElevationProperty);
        return !native_attached || current_native.document.schema.contains(editor_native::kElevationProperty);
    };
    native.cast_tracked = [] {
        if(cast_projection_override)return *cast_projection_override;
        if(recovery)return !recovery->state.document.class_schema.empty();
        return !native_attached || !current_native.document.class_schema.empty();
    };
    native.authoring_tracked = [] {
        if(authoring_projection_override)return *authoring_projection_override;
        if(recovery)return !recovery->state.document.settings_schema.empty();
        return !native_attached || !current_native.document.settings_schema.empty();
    };
    native.gesture_scope = [] { return current_gesture_scope(); };
    native.creation_return_current=[](const editor_native::GestureScope& scope,uint64_t serial){
        return local_creation && !local_creation->materialized && local_creation->scope.serial==serial &&
            local_creation->scope.project==scope.project && local_creation->scope.epoch==scope.epoch && local_creation->scope.author==scope.author &&
            creation_current(local_creation->scope);
    };
    native.import_scope = [] { return current_gesture_scope(true); };
    native.cancelled = cancelled_gesture;
    native.gesture = captured;
    native.history = [](bool redo) { action(redo ? Action::Redo : Action::Undo); };
    native.history_available = [](bool redo) { return redo ? cached.can_redo : cached.can_undo; };
    native.failure = [](const std::string& error) { if (!transport_teardown) { frozen = true; tell(error); } };
    native.notice = [](const std::string& message) { if (!transport_teardown) tell(message); };
    native.gate = [](const std::vector<editor::ObjectId>& selection) {
        const auto state = editor_native::state();
        if (closing || local_creation || exporting || pending_export_request || native_exit_requested || !state.ready || state.generation != native_generation) return false;
        // Native selection also observes a cursor-held, unallocated placement.
        // Its structure/class capability already guards it; it has no placement
        // identity that the coordinator could legitimately lease yet.
        if(!controller || std::any_of(selection.begin(),selection.end(),[](editor::ObjectId id){return !controller->state().document.placements.contains(id) &&
            !(id==editor_native::kCaptainObject && controller->state().document.settings.contains(id));}))return false;
        std::vector<editor::Scope> scopes;
        for (auto id : selection) scopes.push_back({id==editor_native::kCaptainObject?editor::ScopeKind::Settings:editor::ScopeKind::Placement, id, {}});
        if (scopes.empty() || !lease(scopes)) return false;
        for (auto id : selection) {
            if(id==editor_native::kCaptainObject){
                const auto& captain=controller->state().document.settings.at(id);
                if(!state.gesture && !pending)guarded_authoring.insert_or_assign(id,captain);else guarded_authoring.try_emplace(id,captain);
                continue;
            }
            auto it = controller->state().document.placements.find(id);
            if (it == controller->state().document.placements.end()) return false;
            if(!state.gesture && !pending)guarded_placements.insert_or_assign(id,it->second);
            else guarded_placements.try_emplace(id, it->second);
        }
        return true;
    };
    native.structure_gate = [](uint32_t index) {
        const auto state = editor_native::state();
        std::vector<editor::Scope> scopes{{editor::ScopeKind::Structure, {}, {}}};
        // UINT32_MAX is the picker/import route, which has no existing class.
        if(index!=UINT32_MAX)scopes.push_back({editor::ScopeKind::Class,{(2ull<<32)|index,1},{}});
        return !closing && !local_creation && !exporting && !pending_export_request && !native_exit_requested && state.ready && state.generation == native_generation && lease(scopes);
    };
    native.class_gate = [](uint32_t index){
        if(index==UINT32_MAX-1)return authoring_gate(editor_native::kCaptainObject);
        if(!controller || !native_attached)return false;
        const auto state=editor_native::state();
        const editor::ObjectId class_id{(2ull<<32)|index,1};
        if(closing || exporting || pending_export_request || native_exit_requested || !state.ready || state.generation!=native_generation ||
           !controller->state().document.classes.contains(class_id) || !lease({{editor::ScopeKind::Class,class_id,{}}}))return false;
        for(const auto& [id,settings]:controller->state().document.class_settings)if(settings.class_id==class_id){
            if(!state.gesture && !pending)guarded_settings.insert_or_assign(id,settings);
            else guarded_settings.try_emplace(id,settings);
        }
        // Class ownership also covers its placements. Native disguise/model
        // changes can update their scale companions while rebuilding actors;
        // capture those changes in the same owned class transaction.
        for(const auto& [id,p]:controller->state().document.placements)if(p.class_id==class_id){
            if(!state.gesture && !pending)guarded_placements.insert_or_assign(id,p);
            else guarded_placements.try_emplace(id,p);
        }
        return true;
    };
    native.remove_class_gate = [](uint32_t index){
        if(!controller || !native_attached || local_creation || closing || exporting || pending_export_request || native_exit_requested)return false;
        const auto state=editor_native::state();
        if(!state.ready || state.generation!=native_generation)return false;
        const auto& doc=controller->state().document;
        const editor::ObjectId class_id{(2ull<<32)|index,1};
        if(!doc.classes.contains(class_id))return false;
        std::set<editor::ObjectId> removed{class_id};
        std::vector<editor::Scope> scopes{{editor::ScopeKind::Structure,{},{}},{editor::ScopeKind::Class,class_id,{}}};
        if(doc.class_schema.contains(editor::kClassAssetResourceProperty))scopes.push_back({editor::ScopeKind::Content,class_id,{}});
        for(const auto& [id,p]:doc.placements)if(p.class_id==class_id){
            if(p.protected_object)return false;
            removed.insert(id);scopes.push_back({editor::ScopeKind::Placement,id,{}});
        }
        for(const auto& ref:doc.external_references)if(removed.contains(ref.target))return false;
        const auto references_removed=[&](const auto& properties){
            for(const auto& [key,value]:properties){
                (void)key;
                if(auto* ref=std::get_if<editor::ObjectId>(&value);ref && removed.contains(*ref))return true;
                if(auto* block=std::get_if<editor::StructuredValue>(&value))
                    for(auto ref:block->references)if(removed.contains(ref))return true;
            }
            return false;
        };
        for(const auto& [id,settings]:doc.class_settings)
            if(settings.class_id!=class_id && references_removed(settings.properties))return false;
        for(const auto& [id,settings]:doc.settings)
            if(references_removed(settings.properties))return false;
        for(const auto& [id,p]:doc.placements)
            if(p.class_id!=class_id && references_removed(p.properties))return false;
        if(!lease(scopes))return false;
        for(const auto& [id,p]:doc.placements)if(p.class_id==class_id)guarded_placements.insert_or_assign(id,p);
        for(const auto& [id,settings]:doc.class_settings)if(settings.class_id==class_id)guarded_settings.insert_or_assign(id,settings);
        return true;
    };
    native.validate_dependencies = [](const editor_native::Snapshot& snapshot,std::string& error){
        std::vector<dbpf::Record> records;
        return adventure_collect::collect_snapshot(resources.root,snapshot.bytes,records,error);
    };
    native.settings_gate=authoring_gate;
    native.terrain_gate=[](const std::vector<editor::ObjectId>& records,const std::vector<uint32_t>& fields){
        if(!controller || controller->view().phase!=Phase::Ready || local_creation || closing || exporting || pending_export_request || native_exit_requested || pending || awaiting_operation || durable_commit || frozen || recovery)return false;
        const auto native=editor_native::state();if(!native.ready || native.generation!=native_generation)return false;
        const auto& document=controller->state().document;std::vector<editor::Scope> scopes;
        for(auto id:records){if(!document.terrain.contains(id))return false;scopes.push_back({editor::ScopeKind::Terrain,id,{}});}
        auto planet=document.settings.find(editor_native_terrain::kPlanetObject);
        for(auto field:fields){
            if(planet==document.settings.end() || !document.settings_schema.contains(field))return false;
            scopes.push_back({editor::ScopeKind::Field,editor_native_terrain::kPlanetObject,{editor::Field::Property,field}});
        }
        // Native recipe loads can replace more records than one lease packet.
        // This temporary authored-data hold still excludes asset Content leases.
        if(scopes.size()>200)scopes={{editor::ScopeKind::Authoring,{},{}}};
        if(!scopes.empty() && !lease(scopes))return false;
        for(auto id:records)guarded_terrain.try_emplace(id,document.terrain.at(id));
        if(!fields.empty())guarded_authoring.try_emplace(planet->first,planet->second);
        return true;
    };
    native.act_action=edit_acts;
    native.navigation_allowed=[] {
        return controller && controller->view().phase==Phase::Ready && !local_creation && !durable_commit && !closing && !recovery && !pending && !awaiting_operation &&
            !exporting && !pending_export_request && !native_exit_requested && !editor_native_save::busy() && !editor_native::state().gesture;
    };
    native.transaction_pending = [] { return pending.has_value() || awaiting_operation.has_value() || bool(durable_commit); };
    native.object_revision = [](editor::ObjectId object)->std::optional<editor::Revision> {
        if(!controller)return std::nullopt;
        const auto& placements=controller->state().document.placements;
        auto settings=controller->state().document.settings.find(object);if(settings!=controller->state().document.settings.end())return settings->second.revision;
        auto found=placements.find(object);
        if(found!=placements.end())return found->second.revision;
        auto terrain=controller->state().document.terrain.find(object);if(terrain!=controller->state().document.terrain.end())return terrain->second.revision;
        for(const auto& [id,settings]:controller->state().document.class_settings){(void)id;if(settings.class_id==object)return settings.revision;}
        return std::nullopt;
    };
    bool available = editor_native::init(H, M, std::move(native));
    editor_native_save::Ctx saving;
    saving.shared_active = [] { return session_active() || recovery.has_value() || beginning || closing || native_attached || handoff; };
    saving.saved = [](editor::ResourceKey key) {
        if(export_before){finish_export(true);return;}
        if(handoff && handoff_saving){handoff_saving=false;native_exit_requested=native_exit_confirmed=true;return;}
        if (transport_teardown || (native_attached && editor_native::state().generation != native_generation)) return;
        if (recovery && recovery->saving) { recovery->saved_key = key; return; }
        if (beginning) { start_saved(); return; }
        saved_native(key);
        if(exit_after_save && !frozen)native_exit_requested=native_exit_confirmed=true;
    };
    saving.cancelled = [] { if(export_before)finish_export(false);if(handoff)++handoff_prompt_serial;handoff_saving=false;exit_after_save=false; if (transport_teardown) return; beginning = false; if (recovery && recovery->saving) { recovery.reset(); frozen = true; } status = "Native save cancelled; the draft is preserved"; };
    saving.error = [](const std::string& error) { if(export_before)finish_export(false);if(handoff)++handoff_prompt_serial;handoff_saving=false;exit_after_save=false; if (transport_teardown) return; beginning = false; if (recovery && recovery->saving) { recovery.reset(); frozen = true; } tell(error); };
    saving.owned_preview = []{return adventure_native::editor_preview() || solo_preview.has_value();};
    saving.return_requested = [] { action(Action::Return); };
    saving.request = [](editor_native_save::Request request) {
        using R=editor_native_save::Request;
        action(request==R::Save?Action::Save:request==R::SaveExit?Action::SaveExit:
            request==R::Exit?Action::NativeExit:request==R::Play?Action::ChooseTest:Action::Return);
    };
    saving.exit_confirmed=[] {native_exit_confirmed=true;schedule();};
    saving.exit_cancelled=[] {if(handoff)++handoff_prompt_serial;native_exit_requested=native_exit_confirmed=exit_after_save=false;schedule();};
    editor_native_save::init(H, M, std::move(saving));
    editor_creation::Ctx creation;
    creation.scope=[]()->std::optional<editor_native::GestureScope>{
        const auto value=current_gesture_scope(true);return value.epoch?std::optional(value):std::nullopt;
    };
    creation.prepare=prepare_creation;creation.current=creation_current;creation.lease_valid=creation_lease;creation.returned=return_creation;
    creation.notice=[](const std::string& value){tell(value);};
    creation.finished=[](const editor_creation::Scope& scope,bool okay,const std::string& reason){
        if(!local_creation || local_creation->scope!=scope)return;
        std::vector<editor::LeaseToken> tokens;
        for(const auto& item:controller->view().local_leases)if(item.scope.kind==editor::ScopeKind::Content &&
            local_creation->request.target && item.scope.object==*local_creation->request.target)tokens.push_back({item.scope,item.token});
        std::string error;if(!tokens.empty())controller->release_leases(tokens,error);
        if(local_creation->materialized)local_creation.reset();
        status=okay?reason:"The creation remains saved locally: "+reason;schedule();
    };
    editor_creation::init(H,M,std::move(creation));
    refresh(); return available;
}
void tick(double) { editor_native::tick(); editor_native_save::tick();editor_creation::tick(); schedule(); }
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
void shutdown() {
    // Settle while the exact controller, creation scope and Store still exist.
    // Failure can require native rollback, so retire native adapters last.
    if(controller){controller->end();controller.reset();}
    pending.reset();awaiting_operation.reset();waiting_acts.reset();rollback_fence.reset();before_apply_valid=false;
    guarded_placements.clear();guarded_terrain.clear();requested_scopes.clear();lease_use.clear();
    editor_creation::shutdown();local_creation.reset();prepared_native.reset();rendered_revision=0;
    waiting_history.reset();authoring_projection_override.reset();test_menu.reset();guarded_settings.clear();guarded_authoring.clear();cast_uploads.clear();cast_upload_errors.clear();
    solo_preview.reset();solo_ready_scope.reset();pending_solo_return.reset();pending_solo_request=0;
    native_playtesting=return_started=disconnected_return=end_after_return=false;return_error.clear();settled_cancellation.reset();
    transport_teardown.reset();deferred_offer.reset();observed_transport=retired_transport=0;reopen_saved.reset();projection_override.reset();
    store.close();adventure_native::clear_editor_preview();editor_native_save::shutdown();editor_native::shutdown();actions.clear();creation_actions.clear();complexity_request.reset();queued_exports.clear();queued_solo.reset();deferred_test.reset();
    pending_preview_return.reset();pending_preparation_cancel.reset();preview_selection.reset();observed_lease_grants.clear();
    queued=false;closing=native_attached=close_restored=close_preserve_offer=false;H=nullptr;
}
bool active() { return cached.active; }
bool authoring() {
    if (!controller || (controller->view().phase == Phase::Playing && native_playtesting && controller->view().playtest_mode==editor::wire::PlaytestMode::Together)) return false;
    return session_active() || native_attached || closing || recovery.has_value() || beginning;
}
bool playtest_owned() { return adventure_native::editor_preview(); }
View view() { return cached; }
const std::vector<TransformHint>& transform_hints() { return cached.previews; }
const std::vector<TransformHint>& selection_hints() { return cached.selections; }
const std::vector<std::string>& lock_hints() { return cached.locks; }
bool world_hints_visible() { return cached.world_hints; }
void begin() { action(Action::Begin); }
void undo() { action(Action::Undo); }
void redo() { action(Action::Redo); }
void save() { action(Action::Save); }
void playtest() { action(Action::Playtest); }
void return_to_editor() { action(Action::Return); }
void cancel_playtest() { action(Action::CancelPreparation); }
void set_ready(bool value) { action(value ? Action::Ready : Action::Unready); }
void recover() { action(Action::Recover); }
void end() { action(Action::End); }
void approve_playtest(bool approve) {action(approve?Action::ApproveTest:Action::DeclineTest);}
void choose_test(int choice){action(choice==0?Action::MenuSolo:choice==1?Action::MenuTogether:Action::MenuDismiss);}
void edit_acts(int value){
    if(value==-1){waiting_acts.reset();return;}
    if(!controller || value<0 || value>3 || !cached.can_save)return;
    waiting_acts=WaitingActs{TestMenu{current_gesture_scope(),native_generation,controller->state().document.revision,editor_native::state().act},value,now_ms()};schedule();
}
void create_asset(uint32_t type){if(creation_actions.size()<8){creation_actions.push_back({type,{}});schedule();}}
void edit_asset(editor::ObjectId class_id){if(creation_actions.size()<8){creation_actions.push_back({0,class_id});schedule();}}
void set_extended_complexity(bool value){complexity_request=value;schedule();}
void resolve_join(int choice) {action(choice==0?Action::HandoffSave:choice==1?Action::HandoffDiscard:Action::HandoffCancel);}
std::string debug(const std::vector<std::string>& args) {
    if(args.size()==1 && args[0]=="creations")return editor_creation::diagnostic();
    if((args.size()==1 && args[0]=="assets") || (args.size()==2 && args[0]=="asset")){
        if(!controller)return "Shared assets unavailable";
        std::optional<editor::ObjectId> only;
        if(args[0]=="asset"){
            const auto& text=args[1];const auto separator=text.find(':');
            auto decimal=[](const std::string& digits,uint64_t maximum,uint64_t& out){
                if(digits.empty())return false;out=0;
                for(const auto digit:digits){
                    if(digit<'0'||digit>'9'||out>(maximum-uint64_t(digit-'0'))/10)return false;
                    out=out*10+uint64_t(digit-'0');
                }
                return out!=0;
            };
            uint64_t id=0,generation=0;
            if(text.size()>32 || separator==std::string::npos ||
               !decimal(text.substr(0,separator),UINT64_MAX,id) || !decimal(text.substr(separator+1),UINT32_MAX,generation))
                return "Use coop.editor asset <class-id:generation>";
            only=editor::ObjectId{id,uint32_t(generation)};
        }
        const auto& doc=controller->state().document;std::string hash,error;
        if(!editor_session::document_hash(doc,hash,error))return "Shared assets unavailable: "+error;
        std::string result="revision="+std::to_string(doc.revision)+" durable="+std::to_string(controller->view().locally_durable)+" canonicaldocumenthash="+hash+"\n";
        bool found=false;
        for(const auto& [id,row]:doc.class_settings){
            if(only && row.class_id!=*only)continue;
            const auto content=editor::value(doc,id,{editor::Field::Property,editor::kClassAssetResourceProperty});
            const auto* key=content?std::get_if<editor::ResourceKey>(&*content):nullptr;if(!key)continue;
            auto block=row.properties.find(editor_native::kCastBlockProperty);
            const auto* behavior=block==row.properties.end()?nullptr:std::get_if<editor::StructuredValue>(&block->second);if(!behavior)continue;
            char text[192];snprintf(text,sizeof text,"asset class=%llu:%u settings=%llu:%u key=%08X!%08X.%08X content_revision=%llu behavior_sha256=",
                static_cast<unsigned long long>(row.class_id.id),row.class_id.generation,static_cast<unsigned long long>(id.id),id.generation,key->group,key->instance,key->type,
                static_cast<unsigned long long>(editor::field_revision(doc,id,{editor::Field::Property,editor::kClassAssetResourceProperty})));
            const auto line=std::string(text)+upd::sha256_hex(behavior->bytes)+"\n";
            if(result.size()+line.size()>12u*1024){result+="truncated=1\n";break;}result+=line;
            found=true;
            if(only)break;
        }
        if(only && !found)result+="asset_not_found=1\n";
        return result;
    }
    if(durable_commit && args.size()==1 && args[0]=="accounting")return "commit_worker=1 phase="+std::string(durable_commit->flushing?"flush":"encode")+
        " canonical_revision="+std::to_string(controller->state().document.revision)+" prepared_revision="+std::to_string(durable_commit->state.document.revision);
    if(args.size()==1 && args[0]=="visuals")return editor_native::visual_status();
    if(args.size()==1 && args[0]=="acts")return editor_native::authoring_status();
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
        add("prepared_history", a.prepared_history);
        add("prepared_decisions", a.prepared_decisions);
        add("prepared_estimate", a.prepared_estimate);
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
            static const char* names[] = {"Document", "Class", "Act", "Placement", "Field", "Structure", "Settings", "Terrain", "Content", "Authoring"};
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
            if(auto found=p.properties.find(editor_native::kElevationProperty);found!=p.properties.end()){
                if(const auto* value=std::get_if<float>(&found->second)){
                    char text[48];snprintf(text,sizeof text," elevation=%.9g",double(*value));relative+=text;
                }
            }
            for(auto [key,label]:{std::pair{editor_native::kSoundPitchProperty," pitch="},std::pair{editor_native::kSoundGainProperty," gain="}}){
                if(auto found=p.properties.find(key);found!=p.properties.end())if(const auto* value=std::get_if<float>(&found->second)){
                    char text[48];snprintf(text,sizeof text,"%.9g",double(*value));relative+=label;relative+=text;
                }
            }
            if(auto found=p.properties.find(editor_native::kDestinationPositionProperty);found!=p.properties.end()){
                if(const auto* value=std::get_if<editor::Vec3>(&found->second)){
                    char text[192];float destination_xy[2]{};
                    const bool visible=H->world_to_screen && H->world_to_screen(&value->x,destination_xy) && std::isfinite(destination_xy[0]) && std::isfinite(destination_xy[1]);
                    snprintf(text,sizeof text," destination=%.9g,%.9g,%.9g",double(value->x),double(value->y),double(value->z));relative+=text;
                    if(visible){snprintf(text,sizeof text," destination_screen=%.6f,%.6f",double(destination_xy[0]),double(destination_xy[1]));relative+=text;}
                    else relative+=" destination_screen=hidden";
                }
                if(auto rotation=p.properties.find(editor_native::kDestinationRotationProperty);rotation!=p.properties.end()){
                    if(const auto* value=std::get_if<editor::Rotation>(&rotation->second)){
                        char text[192];snprintf(text,sizeof text," destination_rotation=%.9g,%.9g,%.9g,%.9g",double(value->x),double(value->y),double(value->z),double(value->w));relative+=text;
                    }
                }
                if(auto scale=p.properties.find(editor_native::kDestinationScaleProperty);scale!=p.properties.end()){
                    if(const auto* value=std::get_if<float>(&scale->second)){
                        char text[48];snprintf(text,sizeof text," destination_scale=%.9g",double(*value));relative+=text;
                    }
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
        " epoch=" + std::to_string(controller ? controller->view().context.epoch : 0) +
        " capability=" + std::to_string(controller ? controller->view().local.slot : 255) + ":" + std::to_string(controller ? controller->view().local.generation : 0) +
        " automatic="+std::to_string(C.automatic_follow)+" exporting="+std::to_string(exporting.has_value())+
        " exiting="+std::to_string(native_exit_requested)+" join_prompt="+std::to_string(cached.join_prompt)+
        " test_request="+std::to_string(cached.test_request)+" retry="+std::to_string(cached.retry)+
        " can_undo="+std::to_string(cached.can_undo)+" can_redo="+std::to_string(cached.can_redo)+
        "\nselection=" + cached.selection + " previews=" + std::to_string(cached.previews.size()) +
        " highlights="+std::to_string(cached.selections.size())+" asset_locks="+std::to_string(cached.locks.size())+
        " lease_idle_ms="+std::to_string(now_ms()>=last_lease_use?now_ms()-last_lease_use:0)+
        "\n" + editor_native::status() + "\n" + adventure_assets::status();
}
} // namespace coop_editor
