#include "editor_runtime.h"
#include "editor_terrain_edit.h"

#include <windows.h>
#include <algorithm>
#include <bit>
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
#include "editor_saved_native.h"
#include "editor_creation.h"
#include "editor_creation_native.h"
#include "editor_metrics.h"
#include "editor_stage_metrics.h"
#include "editor_reopen_identity.h"
#include "editor_project.h"
#include "editor_placement_diagnostic.h"
#include "editor_asset_diagnostic.h"
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
    editor_native::CreationTerrainTicket terrain_ticket;
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
    bool entered_adventure=false,camera_resumed=false,terrain_resumed=false,return_failed=false;
    std::string return_error;
};
std::optional<LocalCreation> local_creation;
struct RejectionRestore {
    editor_native::GestureScope scope;
    editor_native::SceneOwner owner;
    editor_native::LocalView view;
    std::optional<editor::ObjectId> selected;
    editor::ObjectId act;
    uint64_t serial=0,transport=0,generation=0,started=0,scene_frame=0;
    editor::Revision scene_revision=0;
    bool view_restored=false,failed=false,verified=false;
    std::string reason;
    std::optional<editor::OperationId> continuation;
};
std::optional<RejectionRestore> rejection_restore;
uint64_t rejection_serial=0;
bool rejection_servicing=false;
// Authority for the external working-source/header pair is established only
// while attaching a checked scene, independently of a later pose release.
// This is not a cached semantic proof: every staged image is still validated.
struct SceneAliasReceipt {
    editor_native::SceneOwner owner;
    editor_native::GestureScope scope;
    editor::ResourceKey canonical_source;
    std::shared_ptr<const std::vector<uint8_t>> base_root;
    std::vector<dbpf::Record> dependencies;
    uint64_t generation=0,transport=0,serial=0;
    editor::Revision revision=0;
    uint32_t act=0;
    size_t charged=0;
};
struct SceneAliasAttachment : SceneAliasReceipt {
    editor_native::Snapshot expected;
    uint64_t frame=0,started=0,last_attempt=UINT64_MAX;
};
std::shared_ptr<const SceneAliasReceipt> scene_alias;
std::shared_ptr<SceneAliasAttachment> scene_alias_attachment;
uint64_t scene_alias_serial=0;
bool scene_alias_servicing=false;
void retire_scene_alias(){scene_alias.reset();scene_alias_attachment.reset();}
struct StagedCapture {
    editor_native::CaptureTicket ticket;
    editor_native::Snapshot canonical;
    editor::ResourceKey canonical_source;
    std::shared_ptr<const SceneAliasReceipt> alias;
    std::optional<editor_native::Snapshot> before;
    std::optional<editor_native::Gesture> completed;
    std::map<editor::ObjectId,editor::Placement> guards;
    std::vector<editor::LeaseToken> leases;
    editor::Request request;
    std::vector<dbpf::Record> dependencies;
    std::optional<editor::wire::Command> rejected_export;
    bool export_negative_sent=false;
    std::optional<editor::OperationId> cancel_pending;
    bool preserve_submitted=false;
    size_t charged=0,before_charge=0;
    uint64_t transport=0,started=0,last_attempt_frame=UINT64_MAX;
    std::string failure;
};
std::shared_ptr<StagedCapture> staged_capture;
bool staged_capture_servicing=false;
uint64_t canonical_work_frame=UINT64_MAX;
bool detached_scene() { return local_creation.has_value() || rejection_restore.has_value() || bool(staged_capture); }
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
bool runtime_draining=false;
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
    bool rejected=false;
    bool ending=false,result=false;
};
std::optional<ExportBarrier> exporting;
std::optional<editor_native::Snapshot> export_before;
struct TerrainHold {
    editor_native::TerrainTransitionKey key;
    editor_native::TerrainTransitionTicket ticket;
    editor_native::Snapshot entry;
    uint64_t transport=0;
    editor::Revision revision=0;
    std::optional<editor_native_save::SaveOperation> save;
    bool materializing=false,resumed=false,scene_restored=false,identity_restored=false,save_checked=false;
};
std::optional<TerrainHold> save_terrain,export_terrain,close_terrain;
std::optional<bool> export_return_result;
uint64_t terrain_transition_serial=0;
bool save_cancelling=false;
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
enum class DurablePhase : uint8_t { Encode, Flush };
using DurableEnqueue=void (*)(SkTaskFn,void*);
struct DurableWake {
    uint64_t domain=0,serial=0,transport=0,generation=0;
    uintptr_t controller=0,session=0;
    editor::wire::Context context;
    editor::Peer local;
    editor::OperationId operation;
    editor::Revision revision=0;
    editor_native::GestureScope scope;
    editor::ResourceKey source;
    DurablePhase phase=DurablePhase::Encode;
    bool attached=false,detached=false;
    bool operator==(const DurableWake&) const = default;
};
struct DurablePhaseTimes {
    uint64_t capture=0,begin=0,end=0,observed=0;
};
uint64_t durable_runtime_domain=0,durable_job_serial=0;
bool persist_completion_polling=false;
void SK_CDECL durable_wake(void*);
void post_durable_wake(const DurableWake& wake,DurableEnqueue enqueue) noexcept {
    if(!enqueue)return;
    try{
        auto payload=std::make_unique<DurableWake>(wake);
        enqueue(durable_wake,payload.get());
        (void)payload.release();
    }catch(...){ } // A failed enqueue leaves the existing update poll intact.
}
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
    std::shared_future<bool> task;
    DurableWake wake;
    DurableEnqueue enqueue=nullptr;
    DurablePhaseTimes encoding,flush;
    bool completion_error=false;
    bool flushing=false,hosting=false;
    // Join before any owned worker input/result member ends its lifetime,
    // including failed publication or partially initialized begin paths.
    ~DurableCommit() noexcept {if(task.valid())task.wait();}
};
std::unique_ptr<DurableCommit> durable_commit;
struct Pending { editor::Request request; editor_native::Gesture gesture; uint64_t reverted_frame = 0; bool submitted = false; std::vector<uint8_t> assets={}; bool tentative=false,staged=false,barrier_cancel=false; std::shared_ptr<StagedCapture> capture_owner={}; };
std::optional<Pending> pending;
std::optional<editor::OperationId> awaiting_operation;
void submit_pending_gesture();
void schedule();
struct RollbackFence {
    uint64_t generation=0;
    editor::ResourceKey source;
    std::vector<editor_native::Binding> bindings;
    bool exact_bindings=false;
    std::optional<editor_native::SceneOwner> owner={};
    editor_native::GestureScope scope={};
    std::function<bool()> current={};
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
    std::optional<TerrainHold> terrain;
    bool epoch_prepared=false,controller_started=false;
    editor_store::Snapshot staged;
    bool failed=false;
    std::string failure{};
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
struct OpenCreation {uint32_t type=0;std::optional<editor::ObjectId> target;uint32_t editor_id=0;};
std::deque<OpenCreation> creation_actions;
std::optional<bool> complexity_request;
std::optional<editor::ComplexityCapability> complexity_sample;
bool complexity_publication_failed=false;
std::string complexity_local_error;
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
    save_terrain.reset();export_terrain.reset();close_terrain.reset();export_return_result.reset();save_cancelling=false;
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
editor_creation_native::ModelFence model_fence(const editor_native::GestureScope& scope,uint64_t generation,
                                               editor::Revision revision,uint64_t transport){
    return {scope,generation,[revision,transport](const auto& expected,uint64_t expected_generation){
        if(!controller || !native_attached || !C.session || !C.session->connected() || closing || frozen || recovery ||
           transport_teardown || exporting || native_exit_requested || save_publication_pending ||
           C.session->transport_generation()!=transport || observed_transport!=transport)return false;
        const auto& view=controller->view();const auto& document=controller->state().document;
        const auto member=view.members.find(expected.author.slot);
        return view.phase==Phase::Ready && view.context.project==expected.project && view.context.epoch==expected.epoch &&
            document.project==expected.project && document.epoch==expected.epoch && document.revision==revision &&
            member!=view.members.end() && member->second.peer==expected.author && member->second.phase==Phase::Ready &&
            editor_native::state().generation==expected_generation;
    }};
}
bool admit_class_models(const editor::Document& before,const editor::Document& after,const editor::Change& change,std::string& error){
    std::set<editor::ResourceKey> keys;
    std::set<std::pair<editor::ObjectId,editor::ResourceKey>> prior_assets;
    for(const auto& [id,settings]:before.class_settings){
        (void)id;const auto asset=settings.properties.find(editor::kClassAssetResourceProperty);
        if(asset!=settings.properties.end())if(const auto* key=std::get_if<editor::ResourceKey>(&asset->second))prior_assets.emplace(settings.class_id,*key);
    }
    for(const auto& [id,settings]:after.class_settings){
        (void)id;
        const auto asset=settings.properties.find(editor::kClassAssetResourceProperty);
        if(asset==settings.properties.end())continue;
        // Act roster edits add class/act rows while keeping the same class-wide
        // model. Only new classes or changed content require fresh admission.
        const auto* key=std::get_if<editor::ResourceKey>(&asset->second);
        if(!key){error="A changed cast model has an invalid primary reference";return false;}
        if(prior_assets.contains({settings.class_id,*key}))continue;
        switch(key->type){
        case 0x2B978C46:case 0x2399BE55:case 0x24682294:case 0x476A98C7:keys.insert(*key);break;
        case 0x438F6347:case 0x3D97A8E4:error="This direct creation family is not supported by native Adventure assets";return false;
        default:break;
        }
    }
    if(keys.empty())return true;
    if(!controller || !C.session){error="Model admission has no current shared authority";return false;}
    const auto fence=model_fence({before.project,before.epoch,change.operation.author},editor_native::state().generation,
                                 before.revision,C.session->transport_generation());
    const auto current=[&]{return fence.current && fence.current(fence.scope,fence.generation);};
    if(!current()){error="The cast model admission belongs to an expired shared context";return false;}
    // Uploaded bytes have passed conflict/dependency admission, but remain
    // outside the canonical resource until this whole candidate is durable.
    // Registration is independent of the host's currently loaded editor.
    if(const auto uploaded=cast_uploads.find(change.operation);uploaded!=cast_uploads.end()){
        auto merged=resources;if(!merge_dependencies(merged,uploaded->second,error))return false;
        std::vector<dbpf::Record> added;
        for(const auto& record:merged.bundle.records)if(record.key!=merged.root &&
            std::none_of(resources.bundle.records.begin(),resources.bundle.records.end(),[&](const auto& old){return old.key==record.key;}))added.push_back(record);
        if(!register_native_records(uploaded->second.root,added,error))return false;
    }
    if(!current()){error="The shared context changed during model dependency registration";return false;}
    for(const auto& key:keys)if(!editor_creation_native::validate_model(key,fence,error))return false;
    if(!current()){error="The shared context changed during native model admission";return false;}
    return true;
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
bool begin_rejection_restore(const std::string&,editor_native::SceneOwner,std::string&);
bool stop_unsupported(std::string& error,editor_native::SceneOwner expected_owner) {
    guarded_placements.clear();guarded_settings.clear();guarded_authoring.clear();guarded_terrain.clear();
    std::string restore_error;
    if(!begin_rejection_restore(error,std::move(expected_owner),restore_error)){
        frozen=true;
        tell("Recover the committed draft: "+error+"; canonical restoration failed: "+restore_error);
    }
    return false;
}
editor_native::GestureScope current_projection_scope();
struct IntegrityOwner {
    editor_native::SceneOwner owner;
    editor_native::GestureScope scope;
    uint64_t transport=0,generation=0;
};
struct NativePosePreparation {
    IntegrityOwner identity;
    editor_native::PosePrepareTicket ticket;
    editor_native::Snapshot base;
    editor::Document before,after;
    editor::Change change;
    editor::ResourceKey canonical_source;
    std::shared_ptr<const SceneAliasReceipt> alias;
    std::shared_ptr<const std::vector<uint8_t>> root;
    std::vector<dbpf::Record> dependencies;
    std::string resource_hash;
    uintptr_t controller_identity=0,session_identity=0;
    uint64_t domain=0,frame=0,started=0;
    std::optional<editor_native::Snapshot> prepared;
};
std::shared_ptr<NativePosePreparation> native_pose_prepare;
bool pose_prepare_experiment=false;
const char* pose_prepare_route="disabled";
bool native_pose_context(const std::shared_ptr<NativePosePreparation>&,bool published=true,bool restoring=false);
void cancel_native_pose();
bool integrity_owner_current(const IntegrityOwner& pin,std::string& error){
    const auto current=[&]{return controller && C.session && C.session->connected() && !frozen && !closing && !recovery && !transport_teardown &&
        native_generation==pin.generation && observed_transport==pin.transport && C.session->transport_generation()==pin.transport &&
        current_projection_scope()==pin.scope;};
    if(current()){
        const auto scene=editor_native::creation_scene_state(pin.owner);
        if(scene.available && scene.owner && scene.generation==pin.generation && current())return true;
    }
    frozen=true;native_identity_lost=true;error="The native integrity check's original scene identity expired";
    tell("Recover the committed draft: "+error);return false;
}
bool capture_integrity_owner(IntegrityOwner& pin,std::string& error){
    pin.scope=current_projection_scope();pin.transport=observed_transport;pin.generation=native_generation;
    if(!pin.scope.epoch || !editor_native::capture_scene_owner(pin.owner,error)){
        frozen=true;native_identity_lost=true;
        if(error.empty())error="The native integrity check could not pin its original editor";
        tell("Recover the committed draft: "+error);return false;
    }
    return integrity_owner_current(pin,error);
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
    if(rejection_restore){error="Wait for the rejected edit's native restoration to be verified";return false;}
    if(detached_scene())return editor_native::validate(current_native,controller->state().document,error);
    IntegrityOwner owner;if(!capture_integrity_owner(owner,error))return false;
    if (editor_native::state().generation != native_generation) {
        frozen = true; error = "The native editor belongs to a different context"; tell(error); return false;
    }
    editor_native::Snapshot snapshot;
    if (!capture_recent(current_native,controller->state().document,snapshot,error))return false;
    std::string identity_error;if(!integrity_owner_current(owner,identity_error)){error=std::move(identity_error);return false;}
    const bool okay=native_integrity(snapshot, controller->state().document, error);
    if(!integrity_owner_current(owner,identity_error)){
        if(!error.empty())identity_error=error+"; "+identity_error;
        error=std::move(identity_error);tell("Recover the committed draft: "+error);return false;
    }
    return okay || stop_unsupported(error,owner.owner);
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
    if (!replace_root(next_resources, after, error)) return false;
    const bool dependencies_added=next_resources.bundle.records.size()!=resources.bundle.records.size();
    // Replicas retain native/state bytes directly. A full transfer envelope is
    // needed locally only when a new immutable dependency artifact is written.
    // The host also needs it for future joins; outgoing deltas are host-only.
    if (((host() || dependencies_added) && !editor_project::encode_resources(next_resources, envelope, error)) ||
        (host() && structural(change) && !root_delta(next_resources, delta, error))) return false;
    auto next_hash=resource_hash;
    if(dependencies_added && (!reserve_artifacts({&envelope},error) || !put_blob(envelope,next_hash,error)))return false;
    if(host() && cast_change(change)){
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
void observe_durable_phase(DurableCommit& job,bool okay) noexcept {
    auto& times=job.flushing?job.flush:job.encoding;
    if(times.observed)return;
    times.observed=stage_qpc();
    if(!times.capture || times.capture!=stage_metrics.capture_id() || !stage_metrics.recording() ||
       !times.begin || times.end<times.begin || times.observed<times.end)return;
    using Stage=editor_stage_metrics::Stage;
    const auto record=[&](Stage stage,uint64_t qpc,uint8_t success){
        stage_metrics.record(job.state.document.project,job.change.operation,job.state.document.revision,stage,qpc,success);
    };
    record(job.flushing?Stage::WorkerFlushBegin:Stage::WorkerEncodeBegin,times.begin,0);
    record(job.flushing?Stage::WorkerFlushEnd:Stage::WorkerEncodeEnd,times.end,okay?1:2);
    record(job.flushing?Stage::WorkerFlushObserved:Stage::WorkerEncodeObserved,times.observed,okay?1:2);
}
bool durable_wake_current(const DurableWake& wake){
    if(!controller || !durable_commit || wake!=durable_commit->wake || wake.domain!=durable_runtime_domain ||
       wake.controller!=reinterpret_cast<uintptr_t>(controller.get()) || wake.session!=reinterpret_cast<uintptr_t>(C.session) ||
       !C.session || !C.session->connected() || transport_teardown || frozen || closing || recovery ||
       observed_transport!=wake.transport || C.session->transport_generation()!=wake.transport ||
       wake.transport<=retired_transport || native_generation!=wake.generation || native_attached!=wake.attached ||
       detached_scene()!=wake.detached || current_native.source!=wake.source || current_projection_scope()!=wake.scope)return false;
    const auto& view=controller->view();
    return view.phase==Phase::Ready && view.context==wake.context && view.local==wake.local &&
        controller->state().document.project==wake.context.project && controller->state().document.epoch==wake.context.epoch &&
        controller->state().document.revision==wake.context.revision;
}
void SK_CDECL durable_wake(void* opaque){
    // Own the payload even after shutdown, normal polling or a later job has
    // retired this notification. Never retain the job across its completion.
    std::unique_ptr<DurableWake> wake(static_cast<DurableWake*>(opaque));
    if(!wake || persist_completion_polling)return;
    try{
        if(!durable_wake_current(*wake))return;
        auto& job=*durable_commit;
        // The worker posts before std::async makes its future ready. An early
        // window dispatch must fall back to update polling, never wait/spin.
        if(!job.task.valid() || job.task.wait_for(std::chrono::seconds(0))!=std::future_status::ready || !job.task.get())return;
        struct Poll {Poll(){persist_completion_polling=true;}~Poll(){persist_completion_polling=false;}} polling;
        auto* session=C.session;const auto transport=wake->transport;
        controller->poll_persistence(now_ms());
        if(C.session==session && session && observed_transport==transport && transport>retired_transport &&
           session->transport_generation()==transport)session->flush_pending(transport);
    }catch(...){ } // Existing outside-frame update handles every failure path.
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
    const auto serial=durable_job_serial==UINT64_MAX?0:++durable_job_serial;
    work->wake={durable_runtime_domain,serial,observed_transport,native_generation,
        reinterpret_cast<uintptr_t>(controller.get()),reinterpret_cast<uintptr_t>(C.session),controller->view().context,
        controller->view().local,change.operation,state.document.revision,current_projection_scope(),current_native.source,
        DurablePhase::Encode,native_attached,detached_scene()};
    work->enqueue=H&&serial?H->run_outside_frame:nullptr;
    work->encoding.capture=stage_metrics.recording()?stage_metrics.capture_id():0;
    const auto wake=work->wake;const auto enqueue=work->enqueue;
    auto* job=work.get();
    job->task=std::async(std::launch::async,[job,old_record_count,capability,copy_owned,provenance,wake,enqueue]{
        job->encoding.begin=stage_qpc();
        const auto execute=[&]{try{
            if(!remember_artifacts(job->state,job->change,job->before?*job->before:job->native,job->native,job->archive,job->error))return false;
            if(!replace_root(job->resources,job->native,job->error))return false;
            const bool dependencies_added=job->resources.bundle.records.size()!=old_record_count;
            if(((job->hosting || dependencies_added) && !editor_project::encode_resources(job->resources,job->envelope,job->error)) ||
               (job->hosting && structural(job->change)&&!root_delta(job->resources,job->delta,job->error)))return false;
            if(dependencies_added && !put_blob(job->envelope,job->hash,job->error))return false;
            if(job->hosting && cast_change(job->change) && !changed_resources(job->resources,job->retained_keys,job->delta,job->error))return false;
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
         catch(...){job->error="Encoding the durable editor bytes failed";return false;}};
        const bool okay=execute();job->encoding.end=stage_qpc();
        post_durable_wake(wake,enqueue);return okay;
    }).share();
    durable_commit=std::move(work);native_pose_prepare.reset();return true;
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
    if(job.completion_error){
        if(persist_completion_polling)return Result::Pending;
        error=job.error;const bool uncertain=job.flushing&&(!store.healthy() || store.durable_revision()>=job.state.document.revision);
        stage_stamp(job.state.document.project,job.change.operation,job.state.document.revision,editor_stage_metrics::Stage::PersistWorkEnd,2);
        durable_commit.reset();return uncertain?Result::Uncertain:Result::Rejected;
    }
    bool okay=false;
    try{okay=job.task.get();}catch(const std::exception& e){job.error=e.what();}
    observe_durable_phase(job,okay);
    if(!okay){
        if(persist_completion_polling)return Result::Pending;
        error=job.error;const bool uncertain=job.flushing&&(!store.healthy() || store.durable_revision()>=job.state.document.revision);
        stage_stamp(job.state.document.project,job.change.operation,job.state.document.revision,editor_stage_metrics::Stage::PersistWorkEnd,2);
        durable_commit.reset();return uncertain?Result::Uncertain:Result::Rejected;
    }
    if(!job.flushing){
        if(transport_teardown || !controller || controller->view().context.project!=job.state.document.project ||
           controller->view().context.epoch!=job.state.document.epoch){
            error="The prepared commit's participant context expired";
            if(persist_completion_polling){job.completion_error=true;job.error=error;return Result::Pending;}
            durable_commit.reset();return Result::Rejected;
        }
        // Allocate and validate publication before the irreversible durable
        // append. update_resources only stages bytes until finish_prepared.
        if(job.hosting&&(!job.publication || !controller->update_resources(std::move(*job.publication),job.state.document.revision,error))){
            if(persist_completion_polling){job.completion_error=true;job.error=error;return Result::Pending;}
            durable_commit.reset();return Result::Rejected;
        }
        job.flushing=true;auto* work=&job;
        job.wake.phase=DurablePhase::Flush;job.flush.capture=stage_metrics.recording()?stage_metrics.capture_id():0;
        const auto wake=job.wake;const auto enqueue=job.enqueue;
        stage_stamp(job.state.document.project,job.change.operation,job.state.document.revision,editor_stage_metrics::Stage::DurableBegin);
        job.task=std::async(std::launch::async,[work,wake,enqueue]{
            work->flush.begin=stage_qpc();
            const auto execute=[&]{try{
                if(!store.commit(work->saved,work->error))return false;
                if(work->state.document.revision%16==0 && !garbage_collect(work->maintenance_error)){
                    if(!store.healthy()){work->error=work->maintenance_error;return false;}
                }
                return true;
            }catch(const std::exception& e){work->error=e.what();return false;}
             catch(...){work->error="Flushing the durable editor bytes failed";return false;}};
            const bool okay=execute();work->flush.end=stage_qpc();
            post_durable_wake(wake,enqueue);return okay;
        }).share();
        return Result::Pending;
    }
    stage_stamp(job.state.document.project,job.change.operation,job.state.document.revision,editor_stage_metrics::Stage::DurableEnd,1);
    current_native=std::move(job.native);resources=std::move(job.resources);resource_hash=std::move(job.hash);
    history_artifacts=std::move(job.archive);cast_uploads.erase(job.change.operation);incoming_resources.reset();prepared_native.reset();
    if(rollback_fence && rollback_fence->owner)rollback_fence.reset();
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
    if(persist_completion_polling && durable_commit){
        durable_commit->completion_error=true;durable_commit->error=error;return editor_session::Durability::Pending;
    }
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
    auto staged_pose=native_pose_prepare;
    if(staged_capture && H->frame_index)canonical_work_frame=H->frame_index();
    editor_native::ValidationScope validation_scope;
    StageScope work(after,change.operation,editor_stage_metrics::Stage::ApplyWorkBegin,editor_stage_metrics::Stage::ApplyWorkEnd);
    before_apply_valid = false;
    rollback_fence.reset();
    prepared_native.reset();
    if(staged_pose && (!staged_pose->prepared || staged_pose->before!=before || staged_pose->after!=after || staged_pose->change!=change || !native_pose_context(staged_pose))){error="The staged native commit proof expired";return false;}
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
        if(live.local_dirty)live.local_dirty->dirty=true;
        live.document=after;prepared_native=std::move(live);pending->tentative=false;
        stage_stamp(after.project,change.operation,after.revision,editor_stage_metrics::Stage::SceneReady,1);
        work.okay=true;return true;
    }
    if(detached_scene())before_apply=current_native;
    else{
        IntegrityOwner owner;if(!capture_integrity_owner(owner,error))return false;
        if(staged_pose && owner.owner!=staged_pose->identity.owner){error="The staged native commit no longer owns its original scene";return false;}
        if(editor_native::state().generation!=native_generation){error="The native editor belongs to a different context";return false;}
        if(!capture_recent(current_native,before,before_apply,error))return false;
        std::string identity_error;if(!integrity_owner_current(owner,identity_error)){error=std::move(identity_error);return false;}
        const bool okay=native_integrity(before_apply,before,error);
        if(!integrity_owner_current(owner,identity_error)){
            if(!error.empty())identity_error=error+"; "+identity_error;
            error=std::move(identity_error);tell("Recover the committed draft: "+error);return false;
        }
        if(!okay)return stop_unsupported(error,owner.owner);
        if(staged_pose){
            if(!native_pose_context(staged_pose)){error="The staged native commit context changed";return false;}
            if(!same_native_authored(staged_pose->base,before_apply)){cancel_native_pose();staged_pose.reset();pose_prepare_route="live-baseline-fallback";}
        }
        rollback_fence=RollbackFence{native_generation,before_apply.source,before_apply.bindings,false};
        if(staged_pose){
            rollback_fence->owner=staged_pose->identity.owner;rollback_fence->scope=staged_pose->identity.scope;
            rollback_fence->current=[staged_pose]{return native_pose_context(staged_pose,false,true);};
        }
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
    if(staged_pose){prepared=std::move(*staged_pose->prepared);prepared.local_dirty=before_apply.local_dirty;}
    else { StageScope preparing(after,change.operation,editor_stage_metrics::Stage::NativePrepareBegin,editor_stage_metrics::Stage::NativePrepareEnd);
      if(!editor_native::prepare(before_apply,after,change,source,prepared,error))return false;preparing.okay=true; }
    if(prepared.local_dirty)prepared.local_dirty->dirty=true;
    bool applied=detached_scene();
    { StageScope native(after,change.operation,editor_stage_metrics::Stage::NativeApplyBegin,editor_stage_metrics::Stage::NativeApplyEnd);
      if(!applied)applied=staged_pose?editor_native::apply_prepared_checked(prepared,change,staged_pose->identity.generation,staged_pose->identity.owner,[staged_pose]{return native_pose_context(staged_pose);},error):editor_native::apply_prepared(prepared,change,error);native.okay=applied; }
    if(!applied)return false;
    if(staged_pose){
        if(!native_pose_context(staged_pose) || !integrity_owner_current(staged_pose->identity,error))return false;
        pose_prepare_route="applied";
    }
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
        if(rollback_fence->owner)restored=editor_native::restore_fenced(before_apply,rollback_fence->generation,*rollback_fence->owner,rollback_fence->scope,rollback_fence->current,error);
        else restored=editor_native::restore(before_apply,error);
    }
    if(restored && before_apply_valid && pending && pending->request.operation==change.operation)pending->tentative=false;
    return restored;
}
bool complexity_proven(){
    return controller && complexity_sample && complexity_sample->available && !complexity_publication_failed && controller->view().complexity_ready;
}
bool complexity_current(const editor::ComplexityCapability& value){
    if(!complexity_proven() || !C.session || !C.session->connected() || transport_teardown ||
        C.session->transport_generation()!=observed_transport || observed_transport<=retired_transport)return false;
    const auto& view=controller->view();const auto member=view.members.find(view.local.slot);
    return view.context.project!=editor::ProjectId{}&&view.context.epoch&&view.local.generation&&view.local.slot==C.session->my_id()&&
        member!=view.members.end()&&member->second.peer==view.local&&value==*complexity_sample;
}
bool complexity_authorized(bool required=false){
    if(!required&&(!controller||!editor::extended_complexity(controller->state().document)))return true;
    if(!complexity_sample||!complexity_current(*complexity_sample))return false;
    try{
        // Authorization observes globals again without publishing, changing the
        // owned consensus sample, or borrowing a Native scene pointer.
        editor::ComplexityCapability value;std::string error;
        return editor_native::complexity_capability(value,error)&&editor::valid_complexity(value)&&complexity_current(value);
    }catch(...){return false;}
}
bool complexity_held(){
    return controller && editor::extended_complexity(controller->state().document) && !complexity_proven();
}
std::string complexity_conflict(){
    if(!complexity_local_error.empty())return complexity_local_error;
    if(controller&&!controller->view().complexity_error.empty())return controller->view().complexity_error;
    return "Extended Complexity is waiting for identical current native tuning proofs from every participant.";
}
void refresh_complexity_capability(){
    editor::ComplexityCapability value;std::string error;
    const bool available=editor_native::complexity_capability(value,error);
    if(!editor::valid_complexity(value) || available!=value.available){
        value={};value.reason=editor::ComplexityReason::Changed;
        error="The native complexity proof changed while it was being sampled.";
    }
    complexity_sample=value;complexity_local_error=error.substr(0,512);
    // start() has staged a new recovery epoch, but its checkpoint is not yet
    // durable. The setter can publish immediately, so retain this local sample
    // until the checked checkpoint exists.
    if(!controller || (recovery&&recovery->controller_started))return;
    complexity_publication_failed=!controller->set_complexity_capability(value,error);
    if(complexity_publication_failed)complexity_local_error=error.substr(0,512);
}
bool lease(const std::vector<editor::Scope>& scopes,bool disabling_complexity=false) {
    if (!controller || staged_capture || rejection_restore || frozen || closing || recovery || durable_commit || awaiting_operation || controller->view().phase != Phase::Ready) return false;
    const bool disable_field=disabling_complexity && scopes.size()==1 && scopes.front()==editor::Scope{editor::ScopeKind::Field,editor_native::kProjectObject,
        {editor::Field::Property,editor_native::kExtendedComplexityProperty}};
    if(!disable_field&&!complexity_authorized())return false;
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
    if(!controller || !native_attached || detached_scene() || closing || exporting || pending_export_request || native_exit_requested)return false;
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
void log_unowned_placement_changes(const editor_native::Gesture& gesture) noexcept {
    try{
    if(!H || !H->log)return;
    const auto& before=gesture.before.document;const auto& after=gesture.after.document;
    const auto old_planet=before.settings.find(editor_native_terrain::kPlanetObject),new_planet=after.settings.find(editor_native_terrain::kPlanetObject);
    const bool planet_delta=(old_planet==before.settings.end())!=(new_planet==after.settings.end()) ||
        (old_planet!=before.settings.end() && new_planet!=after.settings.end() && old_planet->second.properties!=new_planet->second.properties);
    const bool terrain_delta=before.terrain!=after.terrain || planet_delta;
    const auto marker=[](const editor_native::Snapshot& snapshot,editor::ObjectId id){
        const auto found=std::find_if(snapshot.bindings.begin(),snapshot.bindings.end(),[&](const auto& binding){return binding.object==id;});
        return found==snapshot.bindings.end()?std::string("missing"):std::to_string(found->marker);
    };
    auto left=before.placements.begin(),right=after.placements.begin();unsigned placements=0;
    while(placements<8 && (left!=before.placements.end() || right!=after.placements.end())){
        const auto id=left==before.placements.end()?right->first:right==after.placements.end()?left->first:std::min(left->first,right->first);
        const editor::Placement* a=nullptr;const editor::Placement* b=nullptr;
        if(left!=before.placements.end() && left->first==id){a=&left->second;++left;}
        if(right!=after.placements.end() && right->first==id){b=&right->second;++right;}
        const auto differences=editor_placement_diagnostic::describe(a,b);if(differences.empty())continue;
        ++placements;const auto old_marker=marker(gesture.before,id),new_marker=marker(gesture.after,id);
        for(const auto& field:differences)H->log(M,SK_DEBUG,
            "Shared editor unowned placement diagnostic: generation=%llu terrain_delta=%u object=%llu:%u before_snapshot_marker=%s after_snapshot_marker=%s field=%s before=%s after=%s",
            static_cast<unsigned long long>(gesture.generation),unsigned(terrain_delta),static_cast<unsigned long long>(id.id),id.generation,
            old_marker.c_str(),new_marker.c_str(),field.field.c_str(),field.before.c_str(),field.after.c_str());
    }
    }catch(...){ } // Optional evidence must not replace the original rejection.
}
void log_capture_rejection(const char* stage,const editor_native::Gesture& gesture,const std::string& reason) noexcept {
    try{
        if(!H||!H->log)return;
        struct Record {const char* comparison;editor::ObjectId object;editor_placement_diagnostic::Difference field;};
        std::vector<Record> records;records.reserve(16);const auto detail=reason.substr(0,512);
        const auto compare=[&](const char* label,const editor_native::Snapshot& before,const editor_native::Snapshot& after){
            auto left=before.document.placements.begin(),right=after.document.placements.begin();unsigned fields=0;
            while(fields<8 && (left!=before.document.placements.end() || right!=after.document.placements.end())){
                const auto id=left==before.document.placements.end()?right->first:right==after.document.placements.end()?left->first:std::min(left->first,right->first);
                const editor::Placement* a=nullptr;const editor::Placement* b=nullptr;
                if(left!=before.document.placements.end()&&left->first==id){a=&left->second;++left;}
                if(right!=after.document.placements.end()&&right->first==id){b=&right->second;++right;}
                for(auto field:editor_placement_diagnostic::describe(a,b)){
                    if(fields==8)break;
                    ++fields;records.push_back({label,id,std::move(field)});
                }
            }
        };
        compare("canonical-before",current_native,gesture.before);compare("before-after",gesture.before,gesture.after);
        H->log(M,SK_DEBUG,"Shared editor capture rejection stage=%s generation=%llu reason=%s",stage,
            static_cast<unsigned long long>(gesture.generation),detail.c_str());
        for(const auto& record:records)H->log(M,SK_DEBUG,
            "Shared editor capture rejection value stage=%s comparison=%s object=%llu:%u field=%s before=%s after=%s",stage,record.comparison,
            static_cast<unsigned long long>(record.object.id),record.object.generation,record.field.field.c_str(),record.field.before.c_str(),record.field.after.c_str());
    }catch(...){ } // No diagnostic failure may change rejection or ownership.
}
editor_native::GestureScope current_gesture_scope(bool retained_import=false) {
    if (!controller || !native_attached || native_pose_prepare || staged_capture || rejection_restore || closing || recovery || transport_teardown || !C.session ||
        !C.session->connected() || C.session->transport_generation() != observed_transport) return {};
    const auto& view = controller->view();
    if(view.phase!=Phase::Ready || !complexity_authorized() || exporting || pending_export_request || native_exit_requested || save_publication_pending ||
       (!retained_import && awaiting_operation))return {};
    return {view.context.project, view.context.epoch, view.local};
}
editor_native::GestureScope current_projection_scope() {
    // An immutable read proof can outlive the local edit request or an owned
    // playtest hold. It never grants permission to start or mutate a gesture.
    if (!controller || !native_attached || closing || frozen || recovery || transport_teardown || !C.session ||
        !C.session->connected() || C.session->transport_generation() != observed_transport ||
        exporting || pending_export_request || native_exit_requested || save_publication_pending) return {};
    const auto& view=controller->view();
    if(view.phase!=Phase::Ready && view.phase!=Phase::Preparing && view.phase!=Phase::Playing && view.phase!=Phase::Returning)return {};
    const auto& document=controller->state().document;
    if(view.context.project==editor::ProjectId{} || !view.context.epoch || !view.local.generation ||
       view.local.slot!=C.session->my_id() || view.context.project!=document.project || view.context.epoch!=document.epoch)return {};
    return {view.context.project,view.context.epoch,view.local};
}
bool dependencies_current(const std::vector<dbpf::Record>& dependencies){
    for(const auto& expected:dependencies){
        const auto found=std::find_if(resources.bundle.records.begin(),resources.bundle.records.end(),
            [&](const auto& record){return record.key==expected.key;});
        if(found==resources.bundle.records.end() || found->data!=expected.data)return false;
    }
    return true;
}
bool capture_dependencies_current(const StagedCapture& pin){return dependencies_current(pin.dependencies);}
bool scene_alias_context(const SceneAliasReceipt& pin){
    if(!controller || !native_attached || !C.session || !C.session->connected() || closing || frozen || recovery || transport_teardown ||
       native_generation!=pin.generation || observed_transport!=pin.transport || C.session->transport_generation()!=pin.transport ||
       current_native.source!=pin.canonical_source || resources.root!=dbpf::Key{pin.canonical_source.instance,pin.canonical_source.type,pin.canonical_source.group} ||
       resources.base_root!=pin.base_root || current_native.act!=pin.act || !dependencies_current(pin.dependencies))return false;
    const auto& view=controller->view();const auto& doc=controller->state().document;const auto member=view.members.find(view.local.slot);
    return view.phase==Phase::Ready && view.context.project==pin.scope.project && view.context.epoch==pin.scope.epoch &&
        view.local==pin.scope.author && view.local.slot==C.session->my_id() && member!=view.members.end() && member->second.peer==view.local &&
        doc.project==pin.scope.project && doc.epoch==pin.scope.epoch && doc.revision>=pin.revision;
}
bool scene_alias_current(const std::shared_ptr<const SceneAliasReceipt>& pin){
    return pin && scene_alias==pin && scene_alias_context(*pin);
}
bool capture_context_current(const std::shared_ptr<StagedCapture>& pin,bool ending=false){
    if(!pin || staged_capture!=pin || !controller || !native_attached ||
       (!ending&&closing) || frozen || recovery || transport_teardown || local_creation || rejection_restore ||
       exporting || pending_export_request || native_exit_requested || save_publication_pending ||
       !C.session || !C.session->connected() || observed_transport!=pin->transport ||
       C.session->transport_generation()!=pin->transport || native_generation!=pin->ticket.generation ||
       current_native.source!=pin->canonical_source ||
       (pin->alias && (!scene_alias_current(pin->alias) || pin->ticket.owner!=pin->alias->owner || pin->canonical.source!=pin->alias->owner.source)) ||
       (!pin->alias && pin->canonical.source!=pin->canonical_source) ||
       (!ending&&current_projection_scope()!=pin->ticket.scope))return false;
    const auto& view=controller->view();const auto& ticket=pin->ticket;const auto member=view.members.find(ticket.scope.author.slot);
    if(view.phase!=Phase::Ready || view.context.project!=ticket.scope.project || view.context.epoch!=ticket.scope.epoch ||
       view.local!=ticket.scope.author || member==view.members.end() ||
       member->second.peer!=view.local || controller->state().document.revision<pin->canonical.document.revision ||
       !capture_dependencies_current(*pin))return false;
    return true;
}
editor_native::CaptureState capture_current(const editor_native::CaptureTicket& ticket){
    using State=editor_native::CaptureState;
    const auto pin=staged_capture;
    if(!pin || pin->ticket!=ticket || !pin->failure.empty() || !capture_context_current(pin) ||
       now_ms()-pin->started>180000 || complexity_held())return State::Retired;
    return durable_commit || pin->rejected_export?State::Wait:State::Ready;
}
size_t capture_snapshot_charge(const editor_native::Snapshot& image){
    size_t result=image.bytes.size()+editor::bytes(image.document)+image.bindings.size()*sizeof(editor_native::Binding);
    if(image.metadata){result+=sizeof(*image.metadata);for(const auto& text:image.metadata->text)result+=text.size()*sizeof(char16_t);}
    return result;
}
struct SceneAliasResources {
    dbpf::Key root;
    uint32_t act=0;
    std::vector<editor_native::Binding> bindings;
    std::optional<editor_native::Snapshot::Metadata> metadata;
    std::string hash;
    uint32_t bytes=0;
    const void* records=nullptr;
    size_t record_count=0;
    std::shared_ptr<const std::vector<uint8_t>> base_root;
};
SceneAliasResources scene_alias_resources(const editor_project::Resources& value){
    return {value.root,value.act,value.bindings,value.metadata,value.bundle.hash,value.bundle.bytes,
        value.bundle.records.data(),value.bundle.records.size(),value.base_root};
}
bool scene_alias_resources_current(const SceneAliasResources& pin,const editor_project::Resources& value){
    return pin.root==value.root && pin.act==value.act && pin.bindings==value.bindings && pin.metadata==value.metadata &&
        pin.hash==value.bundle.hash && pin.bytes==value.bundle.bytes && pin.records==value.bundle.records.data() &&
        pin.record_count==value.bundle.records.size() && pin.base_root==value.base_root;
}
struct SceneAliasOwner {
    editor_native::SceneOwner owner;uint64_t generation=0;
    const editor_session::Controller* controller=nullptr;
    const net::Session* session=nullptr;
    editor::wire::Context context{};
    editor::Peer peer{};
    editor_session::Phase phase=editor_session::Phase::Idle;
    editor::Revision revision=0;
    uint64_t transport=0,runtime_generation=0;
    const void* durable=nullptr;
    const void* capture=nullptr;
    std::optional<editor::OperationId> operation;
    std::optional<editor::OperationId> awaiting;
    std::optional<editor_creation::Scope> creation;
    std::optional<uint64_t> rejection;
    std::optional<Command> export_command,test_command;
    const void* save_ticket=nullptr;
    const void* export_ticket=nullptr;
    const void* close_ticket=nullptr;
    uint64_t export_request=0,test_request_serial=0;
    bool save_pending=false,native_exit=false;
    SceneAliasResources resources;
    std::optional<SceneAliasResources> incoming;
    editor::wire::Context incoming_context{};
    editor::ResourceKey canonical_source;
    bool attached=false,was_closing=false,was_frozen=false,was_recovering=false;
    SceneAliasOwner()=default;
    SceneAliasOwner(editor_native::SceneOwner value,uint64_t gen):owner(value),generation(gen){}
};
bool scene_alias_owner_context(const SceneAliasOwner& pin){
    return controller && controller.get()==pin.controller && C.session==pin.session && C.session && C.session->connected() &&
        !transport_teardown && C.session->transport_generation()==pin.transport && observed_transport==pin.transport &&
        native_generation==pin.runtime_generation && native_attached==pin.attached && closing==pin.was_closing && frozen==pin.was_frozen &&
        recovery.has_value()==pin.was_recovering && durable_commit.get()==pin.durable && staged_capture.get()==pin.capture &&
        (pending?std::optional{pending->request.operation}:std::nullopt)==pin.operation &&
        awaiting_operation==pin.awaiting && (local_creation?std::optional{local_creation->scope}:std::nullopt)==pin.creation &&
        (rejection_restore?std::optional{rejection_restore->serial}:std::nullopt)==pin.rejection &&
        (exporting?std::optional{exporting->command}:std::nullopt)==pin.export_command && test_request==pin.test_command &&
        (save_terrain?save_terrain->ticket.get():nullptr)==pin.save_ticket &&
        (export_terrain?export_terrain->ticket.get():nullptr)==pin.export_ticket && (close_terrain?close_terrain->ticket.get():nullptr)==pin.close_ticket &&
        pending_export_request==pin.export_request && pending_test_request==pin.test_request_serial &&
        save_publication_pending==pin.save_pending && native_exit_requested==pin.native_exit &&
        controller->view().phase==pin.phase && controller->view().context==pin.context && controller->view().local==pin.peer &&
        controller->state().document.revision==pin.revision && current_native.source==pin.canonical_source &&
        scene_alias_resources_current(pin.resources,resources) && incoming_resources.has_value()==pin.incoming.has_value() &&
        (!pin.incoming || (incoming_context==pin.incoming_context && scene_alias_resources_current(*pin.incoming,*incoming_resources)));
}
bool scene_alias_owner_current(const SceneAliasOwner& pin,std::string& error){
    if(!scene_alias_owner_context(pin)){error="The scene attachment's original shared context expired";return false;}
    const auto scene=editor_native::creation_scene_state(pin.owner);
    if(!scene_alias_owner_context(pin) || !scene.settled() || scene.generation!=pin.generation){
        error="The scene attachment's original native owner or readiness changed";return false;
    }
    return true;
}
std::optional<SceneAliasOwner> pin_scene_alias_owner(){
    try{
        if(!controller || !C.session || !C.session->connected())return {};
        SceneAliasOwner pin;pin.controller=controller.get();pin.session=C.session;pin.transport=observed_transport;
        pin.context=controller->view().context;pin.peer=controller->view().local;pin.phase=controller->view().phase;
        pin.revision=controller->state().document.revision;pin.runtime_generation=native_generation;pin.attached=native_attached;
        pin.was_closing=closing;pin.was_frozen=frozen;pin.was_recovering=recovery.has_value();pin.durable=durable_commit.get();pin.capture=staged_capture.get();
        pin.resources=scene_alias_resources(resources);pin.canonical_source=current_native.source;
        if(incoming_resources){pin.incoming=scene_alias_resources(*incoming_resources);pin.incoming_context=incoming_context;}
        pin.awaiting=awaiting_operation;if(local_creation)pin.creation=local_creation->scope;
        if(rejection_restore)pin.rejection=rejection_restore->serial;
        if(exporting)pin.export_command=exporting->command;
        pin.test_command=test_request;
        pin.save_ticket=save_terrain?save_terrain->ticket.get():nullptr;pin.export_ticket=export_terrain?export_terrain->ticket.get():nullptr;
        pin.close_ticket=close_terrain?close_terrain->ticket.get():nullptr;pin.export_request=pending_export_request;pin.test_request_serial=pending_test_request;
        pin.save_pending=save_publication_pending;pin.native_exit=native_exit_requested;
        if(pending)pin.operation=pending->request.operation;
        std::string ignored;if(!editor_native::capture_scene_owner(pin.owner,ignored) || !scene_alias_owner_context(pin))return {};
        const auto scene=editor_native::creation_scene_state(pin.owner);
        if(!scene_alias_owner_context(pin) || !scene.settled())return {};
        pin.generation=scene.generation;return pin;
    }catch(...){return {};}
}
void queue_scene_alias_attachment(const std::optional<SceneAliasOwner>& owner){
    retire_scene_alias();
    try{
        if(!owner || !controller || !H->frame_index || !C.session || !C.session->connected() ||
           !owner->owner.mode || !owner->owner.data || !owner->owner.resource ||
           owner->owner.source==current_native.source || scene_alias_serial==UINT64_MAX)return;
        constexpr size_t limit=64u<<20;
        size_t charged=capture_snapshot_charge(current_native);
        if(charged>limit)return;
        auto pin=std::make_shared<SceneAliasAttachment>();
        pin->owner=owner->owner;pin->generation=owner->generation;pin->canonical_source=current_native.source;
        pin->scope={current_native.document.project,current_native.document.epoch,controller->view().local};
        pin->transport=observed_transport;pin->serial=++scene_alias_serial;pin->revision=current_native.document.revision;
        pin->act=current_native.act;pin->base_root=resources.base_root;pin->expected=current_native;
        for(const auto& record:resources.bundle.records)if(record.key!=resources.root){
            if(record.data.size()>limit-charged)return;
            charged+=record.data.size();pin->dependencies.push_back(record);
        }
        pin->charged=charged-capture_snapshot_charge(current_native);
        pin->frame=H->frame_index();pin->started=now_ms();scene_alias_attachment=std::move(pin);
    }catch(...){retire_scene_alias();} // Optional staging authority never changes attachment acceptance.
}
void service_scene_alias_attachment(){
    const auto pin=scene_alias_attachment;
    if(!pin || scene_alias_servicing || !H || !H->frame_index)return;
    const auto frame=H->frame_index();
    if(frame<=pin->frame || frame==pin->last_attempt)return;
    struct Servicing {Servicing(){scene_alias_servicing=true;}~Servicing(){scene_alias_servicing=false;}} servicing;
    auto current=[&]{return scene_alias_attachment==pin && scene_alias_context(*pin) && !local_creation && !rejection_restore &&
        !staged_capture && !pending && !awaiting_operation && !durable_commit && !exporting && !pending_export_request &&
        !save_publication_pending && !native_exit_requested && current_native.source==pin->expected.source &&
        current_native.document==pin->expected.document && same_native_authored(current_native,pin->expected);};
    if(!scene_alias_context(*pin) || now_ms()-pin->started>180000){retire_scene_alias();return;}
    if(!current())return;
    pin->last_attempt=frame;
    try{
        const auto before=editor_native::creation_scene_state(pin->owner);
        if(!current())return;
        if(!before.owner || before.generation!=pin->generation){retire_scene_alias();return;}
        if(!before.settled())return;
        ProjectionScope projection(pin->expected.document);
        auto expected=pin->expected;expected.source=pin->owner.source;
        editor_native::Snapshot observed;std::string ignored;
        const bool proven=editor_native::capture_draft(observed,ignored) && observed.source==pin->owner.source &&
            observed.bindings==expected.bindings && editor_native::validate(observed,expected.document,ignored) &&
            supported_between(expected,observed,ignored);
        if(!current())return;
        const auto after=editor_native::creation_scene_state(pin->owner);
        if(!current())return;
        if(!proven || !after.owner || after.generation!=pin->generation){retire_scene_alias();return;}
        if(!after.settled())return;
        auto receipt=std::make_shared<SceneAliasReceipt>();
        receipt->owner=pin->owner;receipt->scope=pin->scope;receipt->generation=pin->generation;
        receipt->canonical_source=pin->canonical_source;receipt->base_root=pin->base_root;receipt->transport=pin->transport;
        receipt->serial=pin->serial;receipt->revision=pin->revision;receipt->act=pin->act;receipt->charged=pin->charged;
        receipt->dependencies=std::move(pin->dependencies);
        scene_alias=std::move(receipt);scene_alias_attachment.reset();
    }catch(...){retire_scene_alias();}
}
bool capture_begin(const editor_native::CaptureTicket& ticket,std::string& error){
    error.clear();
    try {
    const auto alias=ticket.owner.source==current_native.source?nullptr:scene_alias;
    // An external source mismatch needs an independently checked attachment
    // receipt. The release itself can never authorize a new alias.
    if(!controller || staged_capture || detached_scene() || pending || awaiting_operation || durable_commit ||
       !H->frame_index || !ticket.gesture || !ticket.owner.mode || !ticket.owner.data || !ticket.owner.resource ||
       ticket.generation!=native_generation || ticket.scope!=current_gesture_scope() ||
       (ticket.owner.source!=current_native.source && (!scene_alias_current(alias) || ticket.owner!=alias->owner)) ||
       ticket.act!=current_native.act)return false;
    constexpr size_t limit=64u<<20;
    size_t charged=capture_snapshot_charge(current_native)+editor::bytes(current_native.document);
    if(alias){if(alias->charged>limit || charged>limit-alias->charged)return false;charged+=alias->charged;}
    if(charged>limit)return false;
    auto pin=std::make_shared<StagedCapture>();pin->ticket=ticket;pin->transport=observed_transport;
    pin->started=now_ms();pin->canonical_source=current_native.source;pin->alias=alias;pin->canonical=current_native;
    if(alias)pin->canonical.source=alias->owner.source;
    pin->canonical.document=controller->state().document;
    pin->guards=guarded_placements;
    for(const auto& held:controller->view().local_leases)if(held.owner==ticket.scope.author && held.expires>now_ms())
        pin->leases.push_back({held.scope,held.token});
    for(const auto& record:resources.bundle.records)if(record.key!=resources.root){
        if(record.data.size()>limit-charged)return false;
        charged+=record.data.size();pin->dependencies.push_back(record);
    }
    if(ticket.scope!=current_gesture_scope() || !controller || !native_attached || staged_capture || detached_scene() ||
       pending || awaiting_operation || durable_commit || closing || frozen || recovery || transport_teardown ||
       exporting || pending_export_request || native_exit_requested || save_publication_pending || !C.session || !C.session->connected() ||
       observed_transport!=pin->transport || C.session->transport_generation()!=pin->transport || native_generation!=ticket.generation ||
       controller->view().phase!=Phase::Ready || controller->view().context.project!=ticket.scope.project ||
       controller->view().context.epoch!=ticket.scope.epoch || controller->view().local!=ticket.scope.author ||
       current_native.source!=pin->canonical_source || (alias && (!scene_alias_current(alias) || ticket.owner!=alias->owner)) ||
       controller->state().document!=pin->canonical.document ||
       !capture_dependencies_current(*pin))return false;
    pin->charged=charged;pin->request=request(editor::Intent::Forward);pin->request.leases=pin->leases;
    status="Checking the owned placement gesture across game frames";
    staged_capture=std::move(pin);last_presence_value.reset();
    const auto released=std::exchange(native_release_qpc,0);native_capture_begin_qpc=native_capture_end_qpc=0;
    try{if(stage_metrics.recording()){
        const auto& held=*staged_capture;using S=editor_stage_metrics::Stage;
        if(released)stage_metrics.record(held.canonical.document.project,held.request.operation,held.canonical.document.revision,S::GestureRelease,released);
        stage_metrics.record(held.canonical.document.project,held.request.operation,held.canonical.document.revision,S::CaptureBegin,stage_qpc());
    }}catch(...){ } // Optional diagnostics cannot retire a successfully published hold.
    return true;
    }catch(...){error.clear();return false;} // No hold was published; retain the ordinary capture path.
}
bool capture_before(const editor_native::CaptureTicket& ticket,const editor_native::Snapshot& before,std::string& error){
    using State=editor_native::CaptureState;
    const auto pin=staged_capture;const auto current=capture_current(ticket);
    if(current==State::Wait){error.clear();return false;}
    if(!pin || current!=State::Ready){error="The staged gesture's original context expired";return false;}
    const auto head=controller->state().document.revision;
    ProjectionScope projection(pin->canonical.document);
    const bool okay=before.source==ticket.owner.source && supported_between(pin->canonical,before,error) &&
        editor_native::validate(before,pin->canonical.document,error);
    const auto settled=capture_current(ticket);
    if(settled==State::Wait || (settled==State::Ready && controller->state().document.revision!=head)){
        error.clear();return false;
    }
    if(settled!=State::Ready){error="The staged gesture's original context expired during baseline validation";return false;}
    if(!okay){if(error.empty())error="The staged gesture started from a different native baseline";return false;}
    const auto charge=capture_snapshot_charge(before);
    if(charge>(64u<<20)-(pin->charged-pin->before_charge)){error="The staged baseline exceeds its owned proof limit";return false;}
    pin->before=before;pin->charged=pin->charged-pin->before_charge+charge;pin->before_charge=charge;
    error.clear();return true;
}
void capture_failed(const editor_native::CaptureTicket& ticket,const std::string& error){
    const auto pin=staged_capture;if(!pin || pin->ticket!=ticket)return;
    if(pin->failure.empty())pin->failure=error.empty()?"The staged native gesture lost its original proof":error;
    schedule();
}
void decline_staged_export(const std::shared_ptr<StagedCapture>& pin){
    if(!pin || !pin->rejected_export || pin->export_negative_sent || !capture_context_current(pin))return;
    auto reply=*pin->rejected_export;reply.kind=editor::wire::CommandKind::FinishExport;
    reply.actor=pin->ticket.scope.author;reply.accepted=false;
    std::string error;pin->export_negative_sent=controller->send_command(reply,error,0);
}
bool native_pose_context(const std::shared_ptr<NativePosePreparation>& pin,bool published,bool restoring){
    if(!pin || (restoring?(native_pose_prepare && native_pose_prepare!=pin):(published?native_pose_prepare!=pin:bool(native_pose_prepare))) || !controller ||
       reinterpret_cast<uintptr_t>(controller.get())!=pin->controller_identity || !C.session ||
       reinterpret_cast<uintptr_t>(C.session)!=pin->session_identity || durable_runtime_domain!=pin->domain ||
       !native_attached || frozen || closing || recovery || transport_teardown || detached_scene() ||
       (!restoring && durable_commit) || exporting || pending_export_request || native_exit_requested || save_publication_pending ||
       !C.session->connected() || observed_transport!=pin->identity.transport ||
       C.session->transport_generation()!=pin->identity.transport || native_generation!=pin->identity.generation ||
       current_projection_scope()!=pin->identity.scope || controller->view().phase!=Phase::Ready ||
       controller->state().document!=pin->before || current_native.document!=pin->before ||
       current_native.source!=pin->canonical_source || !same_native_authored(pin->base,current_native) ||
       resources.base_root!=pin->root || resources.bundle.hash!=pin->resource_hash ||
       (!restoring && now_ms()-pin->started>5000) ||
       !dependencies_current(pin->dependencies) || (pin->alias && !scene_alias_current(pin->alias)))return false;
    const auto& a=pin->base.local_dirty;const auto& b=current_native.local_dirty;
    return a.has_value()==b.has_value() && (!a || std::tie(a->saved_revision,a->dirty,a->imported)==std::tie(b->saved_revision,b->dirty,b->imported));
}
void cancel_native_pose(){
    const auto pin=std::exchange(native_pose_prepare,{});
    if(pin && pin->ticket.serial)editor_native::cancel_pose_prepare(pin->ticket);
}
editor_session::NativePreparation begin_native_pose(const editor::Document& before,const editor::Document& after,const editor::Change& change,std::string& error){
    using Result=editor_session::NativePreparation;using Native=editor_native::PosePrepareResult;
    const bool pose=!change.patch.fields.empty() && change.patch.objects.empty() && change.patch.classes.empty() && !change.patch.acts && change.patch.terrain.empty() &&
        std::all_of(change.patch.fields.begin(),change.patch.fields.end(),[&](const auto& row){return before.placements.contains(row.object) &&
            (row.field.field==editor::Field::Position || row.field.field==editor::Field::Rotation || row.field.field==editor::Field::Scale);});
    pose_prepare_route=!pose_prepare_experiment?"disabled":!pose?"not-pose":native_pose_prepare?"busy":detached_scene()?"detached":!native_attached?"not-attached":
        (pending && pending->tentative)?"adopt":!H || !H->frame_index?"no-frame":current_native.document!=before?"snapshot-shape":"checking";
    if(std::string_view(pose_prepare_route)!="checking")return Result::Fallback;
    constexpr size_t limit=64u<<20;
    const size_t charge=capture_snapshot_charge(current_native)+editor::bytes(after)+editor::bytes(change.patch)+editor::bytes(change.inverse);
    if(charge>limit){pose_prepare_route="budget";return Result::Fallback;}
    auto pin=std::make_shared<NativePosePreparation>();pin->base=current_native;pin->before=before;pin->after=after;pin->change=change;
    pin->canonical_source=current_native.source;pin->root=resources.base_root;pin->resource_hash=resources.bundle.hash;
    pin->controller_identity=reinterpret_cast<uintptr_t>(controller.get());pin->session_identity=reinterpret_cast<uintptr_t>(C.session);pin->domain=durable_runtime_domain;pin->frame=H->frame_index();pin->started=now_ms();
    size_t charged=charge;
    for(const auto& record:resources.bundle.records)if(record.key!=resources.root){
        if(record.data.size()>limit-charged){pose_prepare_route="budget";return Result::Fallback;}
        charged+=record.data.size();pin->dependencies.push_back(record);
    }
    if(!capture_integrity_owner(pin->identity,error))return Result::Error;
    if(pin->identity.owner.source!=pin->base.source){
        pin->alias=scene_alias;
        if(!scene_alias_current(pin->alias) || pin->alias->owner!=pin->identity.owner){pose_prepare_route="alias-unavailable";return Result::Fallback;}
        pin->base.source=pin->identity.owner.source;
    }
    if(!native_pose_context(pin,false)){error="The native commit preparation context expired";return Result::Error;}
    native_pose_prepare=pin;
    stage_stamp(after.project,change.operation,after.revision,editor_stage_metrics::Stage::NativePrepareBegin);
    const auto result=editor_native::begin_pose_prepare(pin->base,pin->after,pin->change,[pin]{return native_pose_context(pin);},pin->ticket,error);
    if(result==Native::Pending && native_pose_context(pin)){pose_prepare_route="pending";return Result::Pending;}
    cancel_native_pose();
    if(result==Native::Fallback){pose_prepare_route="native-fallback";error.clear();return Result::Fallback;}
    if(error.empty())error="The native pose preparation did not retain an exact pending proof";
    pose_prepare_route="error";return Result::Error;
}
editor_session::NativePreparation poll_native_pose(std::string& error){
    using Result=editor_session::NativePreparation;using Native=editor_native::PosePrepareResult;
    const auto pin=native_pose_prepare;
    if(!native_pose_context(pin)){cancel_native_pose();error="The pending native pose commit context expired";return Result::Error;}
    if(H->frame_index()<=pin->frame)return Result::Pending;
    editor_native::Snapshot prepared;
    const auto result=editor_native::finish_pose_prepare(pin->ticket,prepared,error);
    if(result==Native::Pending)return Result::Pending;
    if(result==Native::Ready && native_pose_context(pin) && prepared.document==pin->after && prepared.source==pin->base.source){
        pin->ticket={};
        pin->prepared=std::move(prepared);
        pose_prepare_route="ready";
        stage_stamp(pin->after.project,pin->change.operation,pin->after.revision,editor_stage_metrics::Stage::NativePrepareEnd,1);
        return Result::Ready;
    }
    cancel_native_pose();if(error.empty())error="The completed native pose commit proof expired";return Result::Error;
}
bool rejection_context(const RejectionRestore& pin){
    return rejection_restore && rejection_restore->serial==pin.serial && !rejection_restore->failed &&
        (!pin.continuation || (pending && !pending->submitted && !pending->tentative &&
            pending->request.operation==*pin.continuation && awaiting_operation==pin.continuation)) &&
        !closing && !recovery && !transport_teardown && !frozen && controller && native_attached && C.session &&
        C.session->connected() && observed_transport==pin.transport && C.session->transport_generation()==pin.transport &&
        current_projection_scope()==pin.scope && native_generation==pin.generation;
}
bool rejection_identity(const RejectionRestore& pin,std::string& error){
    if(!rejection_context(pin)){error="The rejected edit's shared restoration context expired";return false;}
    const auto scene=editor_native::creation_scene_state(pin.owner);
    if(!scene.available || !scene.owner || scene.generation!=pin.generation || !rejection_context(pin)){
        error="The rejected edit's native restoration owner changed";return false;
    }
    return true;
}
bool rejected_recovery_owner(const RejectionRestore& pin,std::string& error){
    // A failed proof intentionally empties projection_scope. Recovery may
    // replace its exact old scene only after a separate, explicit owner proof.
    auto current=[&]{
        if(!rejection_restore || rejection_restore->serial!=pin.serial || !rejection_restore->failed || !controller ||
           !native_attached || closing || recovery || transport_teardown || !C.session || !C.session->connected() ||
           observed_transport!=pin.transport || C.session->transport_generation()!=pin.transport || native_generation!=pin.generation)return false;
        const auto& view=controller->view();const auto& doc=controller->state().document;
        return (view.phase==Phase::Ready || view.phase==Phase::Frozen) &&
            view.context.project==pin.scope.project && view.context.epoch==pin.scope.epoch &&
            view.local==pin.scope.author && doc.project==pin.scope.project && doc.epoch==pin.scope.epoch;
    };
    if(!current()){error="The rejected edit's recovery context expired";return false;}
    const auto scene=editor_native::creation_scene_state(pin.owner);
    if(!scene.available || !scene.owner || scene.generation!=pin.generation || !current()){
        error="The rejected edit's original native recovery owner changed";return false;
    }
    return true;
}
void fail_rejection_restore(const RejectionRestore& pin,const std::string& error,bool expired=false){
    if(!rejection_restore || rejection_restore->serial!=pin.serial)return;
    rejection_restore->failed=true;frozen=true;
    if(pin.continuation && pending && pending->request.operation==*pin.continuation){pending.reset();awaiting_operation.reset();}
    if(expired)native_identity_lost=true;
    tell("Recover the committed draft: "+pin.reason+"; canonical restoration failed: "+error);
}
bool rejection_wait_durable(const RejectionRestore& pin){
    if(!durable_commit)return false;
    if(rejection_restore && rejection_restore->serial==pin.serial)rejection_restore->verified=false;
    return true;
}
bool begin_rejection_restore(const std::string& reason,editor_native::SceneOwner expected_owner,std::string& error){
    if(rejection_restore){error="Another rejected edit is already restoring its committed scene";return false;}
    if(!controller || !C.session || !H->frame_index || rejection_serial==UINT64_MAX){
        error="The rejected edit cannot pin its restoration frame and context";return false;
    }
    RejectionRestore pin;pin.scope=current_projection_scope();pin.transport=observed_transport;
    pin.generation=native_generation;pin.serial=++rejection_serial;pin.started=now_ms();pin.reason=reason;
    pin.owner=expected_owner;
    rejection_restore=pin; // Detach canonical work and block local input first.
    auto failed=[&]{if(rejection_restore && rejection_restore->serial==pin.serial)rejection_restore->failed=true;return false;};
    if(!pin.scope.epoch){error="The rejected edit has no current restoration read context";return failed();}
    const auto native=editor_native::state();
    if(!rejection_identity(pin,error)){native_identity_lost=true;return failed();}
    pin.act=logical_act(native.act);if(!native.selected.empty())pin.selected=native.selected.front();
    if(!editor_native::capture_view(pin.view,error))return failed();
    if(!rejection_identity(pin,error)){native_identity_lost=true;return failed();}
    const auto revision=controller->state().document.revision;
    auto snapshot=current_native;snapshot.document=controller->state().document;
    const auto act=std::find(snapshot.document.act_order.begin(),snapshot.document.act_order.end(),pin.act);
    snapshot.act=act!=snapshot.document.act_order.end()?uint32_t(act-snapshot.document.act_order.begin()):
        std::min<uint32_t>(native.act,uint32_t(snapshot.document.acts.size()-1));
    const auto current=[pin,revision]{return rejection_context(pin) && controller->state().document.revision==revision;};
    const bool restored=editor_native::restore_fenced(snapshot,pin.generation,pin.owner,pin.scope,current,error);
    std::string identity_error;
    if(!rejection_identity(pin,identity_error)){native_identity_lost=true;error=std::move(identity_error);return failed();}
    if(!restored && controller->state().document.revision==revision)return failed();
    pin.scene_revision=revision;pin.scene_frame=H->frame_index();rejection_restore=pin;
    // The rejected transaction has no authored state left to protect. Let
    // other participants acquire these objects while this local scene settles.
    std::vector<editor::LeaseToken> tokens;
    for(const auto& lease:controller->view().local_leases)tokens.push_back({lease.scope,lease.token});
    std::string release_error;if(!tokens.empty())controller->release_leases(tokens,release_error);
    requested_scopes.clear();controller->clear_preview();preview_active=false;last_presence_value.reset();
    waiting_history.reset();waiting_acts.reset();test_menu.reset();
    tell("The edit was rejected; verifying the committed native scene: "+reason);return true;
}
void service_rejection_restore(){
    if(!rejection_restore || rejection_restore->failed || durable_commit || rejection_servicing)return;
    struct Servicing {Servicing(){rejection_servicing=true;}~Servicing(){rejection_servicing=false;}} servicing;
    auto pin=*rejection_restore;std::string error;
    if(!rejection_identity(pin,error)){fail_rejection_restore(pin,error,true);return;}
    if(rejection_wait_durable(pin))return;
    if(now_ms()-pin.started>180000){fail_rejection_restore(pin,"Native restoration did not settle before its deadline");return;}
    const auto native=editor_native::state();
    if(!native.ready || native.gesture || editor_native_save::busy() || H->frame_index()<=pin.scene_frame)return;
    const auto scene=editor_native::creation_scene_state(pin.owner);
    if(!rejection_identity(pin,error)){fail_rejection_restore(pin,error,true);return;}
    if(rejection_wait_durable(pin))return;
    if(!scene.settled())return;
    auto snapshot=current_native;snapshot.document=controller->state().document;
    const auto revision=snapshot.document.revision;
    // Pure canonical preparation/persistence continued while away. Coalesce
    // every intervening revision into this one latest scene materialization.
    if(pin.scene_revision!=revision){
        auto act=std::find(snapshot.document.act_order.begin(),snapshot.document.act_order.end(),pin.act);
        snapshot.act=act!=snapshot.document.act_order.end()?uint32_t(act-snapshot.document.act_order.begin()):
            std::min<uint32_t>(native.act,uint32_t(snapshot.document.acts.size()-1));
        if(!rejection_identity(pin,error)){fail_rejection_restore(pin,error,true);return;}
        if(rejection_wait_durable(pin))return;
        if(!register_native_records(resources.root,resources.bundle.records,error)){
            fail_rejection_restore(pin,error);return;
        }
        if(!rejection_identity(pin,error)){fail_rejection_restore(pin,error,true);return;}
        if(rejection_wait_durable(pin))return;
        if(controller->state().document.revision!=revision)return;
        const auto current=[pin,revision]{return !durable_commit && rejection_context(pin) && controller->state().document.revision==revision;};
        const bool restored=editor_native::restore_fenced(snapshot,pin.generation,pin.owner,pin.scope,current,error);
        std::string identity_error;
        if(!rejection_identity(pin,identity_error)){fail_rejection_restore(pin,identity_error,true);return;}
        if(rejection_wait_durable(pin))return;
        if(controller->state().document.revision!=revision)return;
        if(!restored){fail_rejection_restore(pin,error);return;}
        pin.scene_revision=revision;pin.scene_frame=H->frame_index();pin.view_restored=false;pin.verified=false;
        rejection_restore=pin;return;
    }
    if(!pin.view_restored){
        if(!rejection_identity(pin,error)){fail_rejection_restore(pin,error,true);return;}
        if(rejection_wait_durable(pin))return;
        if(pin.selected && (snapshot.document.placements.contains(*pin.selected) ||
            (*pin.selected==editor_native::kCaptainObject && snapshot.document.settings.contains(*pin.selected)))){
            if(!editor_native::select_placement(*pin.selected,error)){fail_rejection_restore(pin,error);return;}
            if(!rejection_identity(pin,error)){fail_rejection_restore(pin,error,true);return;}
            if(rejection_wait_durable(pin))return;
        }
        if(controller->state().document.revision!=revision)return;
        const auto current=[pin,revision]{return !durable_commit && rejection_context(pin) && controller->state().document.revision==revision;};
        const bool view=editor_native::restore_view_fenced(pin.view,pin.generation,pin.owner,pin.scope,current,error);
        std::string identity_error;
        if(!rejection_identity(pin,identity_error)){fail_rejection_restore(pin,identity_error,true);return;}
        if(rejection_wait_durable(pin))return;
        if(controller->state().document.revision!=revision)return;
        if(!view){fail_rejection_restore(pin,error);return;}
        pin.view_restored=true;pin.scene_frame=H->frame_index();rejection_restore=pin;return;
    }
    // Verify after all ordinary/deferred view and selection callbacks and a
    // later game frame. Readiness is only a prerequisite, never scene proof.
    editor_native::Snapshot observed;
    const bool okay=editor_native::capture_draft(observed,error) &&
        editor_native::validate(observed,snapshot.document,error) && supported_between(snapshot,observed,error);
    std::string identity_error;
    if(!rejection_identity(pin,identity_error)){fail_rejection_restore(pin,identity_error,true);return;}
    if(rejection_wait_durable(pin))return;
    if(controller->state().document.revision!=revision)return;
    if(!okay){fail_rejection_restore(pin,error.empty()?"The settled native scene differs from the committed draft":error);return;}
    rendered_revision=revision;last_presence_value.reset();
    if(pin.continuation){
        rejection_restore->verified=true;
        // Submit in this same checked drain. The submission preflight closes
        // the copied owner/head again before releasing the local hold.
        submit_pending_gesture();return;
    }
    rejection_restore.reset();tell("The edit was rejected and the committed draft restored: "+pin.reason);
}
bool terrain_runtime_current(const editor_native::TerrainTransitionKey& key,uint64_t transport,editor::Revision revision,
                             bool allow_close=false,bool allow_recovery=false) {
    if(!controller || !C.session || !C.session->connected() || transport_teardown ||
       C.session->transport_generation()!=transport || observed_transport!=transport ||
       (closing&&!allow_close) || (recovery&&!allow_recovery) || (frozen&&!allow_recovery))return false;
    const auto& view=controller->view();const auto& document=controller->state().document;
    return view.phase==Phase::Ready && view.context.project==key.scope.project && view.context.epoch==key.scope.epoch &&
        view.local==key.scope.author && view.local.slot==C.session->my_id() && view.local.generation &&
        document.project==key.scope.project && document.epoch==key.scope.epoch && document.revision==revision;
}
editor_native::TerrainTransitionCurrent terrain_fence(const TerrainHold& hold,bool allow_close=false,bool allow_recovery=false) {
    const auto key=hold.key;const auto transport=hold.transport;const auto revision=hold.revision;
    return [=]{return terrain_runtime_current(key,transport,revision,allow_close,allow_recovery);};
}
bool terrain_scene_current(const TerrainHold& hold,std::string& error) {
    const auto key=hold.key;const auto transport=hold.transport;const auto revision=hold.revision;
    const auto current=[&]{return terrain_runtime_current(key,transport,revision,true,true);};
    if(!current() || !editor_native::saved_scene_current(key.owner,key.generation) || !current()){
        error="The native scene owned by this terrain transition was replaced";return false;
    }
    // A native Save wizard may rotate the source in this same owned resource.
    // Its numeric owner and generation must remain pinned before restoring it.
    return true;
}
bool capture_terrain_hold(TerrainHold& out,std::string& error) {
    const auto scope=current_projection_scope();
    if(!scope.epoch || terrain_transition_serial==UINT64_MAX){error="The adventure terrain transition cannot be pinned";return false;}
    TerrainHold hold;hold.key.scope=scope;hold.key.serial=++terrain_transition_serial;
    hold.key.generation=editor_native::state().generation;hold.transport=observed_transport;
    hold.revision=controller->state().document.revision;hold.entry=current_native;hold.entry.document=controller->state().document;
    if(!editor_native::capture_scene_owner(hold.key.owner,error) ||
       !editor_native::capture_terrain_transition(hold.key,hold.entry,terrain_fence(hold),hold.ticket,error))return false;
    editor_native::TerrainTransitionTicket refreshed;editor_native::Snapshot observed;
    if(!editor_native::refresh_terrain_transition(hold.ticket,hold.key,hold.entry,terrain_fence(hold),refreshed,observed,error))return false;
    hold.ticket=std::move(refreshed);hold.entry=std::move(observed);hold.entry.document=controller->state().document;
    out=std::move(hold);return true;
}
// A queued export can hold a newer revision than its original request. Only an
// exact witnessed read or an explicit canonical Load/materialize can refresh it.
bool refresh_terrain_hold(TerrainHold& hold,const editor_native::Snapshot& expected,bool allow_close,bool allow_recovery,std::string& error) {
    if(hold.materializing && !editor_native::state().ready){error.clear();return false;}
    hold.revision=expected.document.revision;
    const auto fence=terrain_fence(hold,allow_close,allow_recovery);
    editor_native::TerrainTransitionTicket refreshed;editor_native::Snapshot observed;
    if(editor_native::refresh_terrain_transition(hold.ticket,hold.key,expected,fence,refreshed,observed,error)){
        hold.ticket=std::move(refreshed);hold.entry=std::move(observed);hold.materializing=false;return true;
    }
    if(hold.materializing || !fence())return false;
    editor_native::TerrainTransitionReceipt receipt;
    receipt.kind=allow_recovery?editor_native::TerrainTransitionKind::Recovery:editor_native::TerrainTransitionKind::ExportRebuild;
    receipt.before=receipt.after=hold.key;
    if(!editor_native::restore_terrain_transition(hold.ticket,receipt,expected,fence,refreshed,error))return false;
    hold.ticket=std::move(refreshed);hold.entry=expected;hold.materializing=true;
    if(!editor_native::state().ready){error.clear();return false;}
    if(!editor_native::refresh_terrain_transition(hold.ticket,hold.key,expected,fence,refreshed,observed,error))return false;
    hold.ticket=std::move(refreshed);hold.entry=std::move(observed);hold.materializing=false;return true;
}
bool resume_terrain_hold(TerrainHold& hold,editor_native::TerrainTransitionReceipt receipt,
                         const editor_native::Snapshot& expected,bool allow_close,bool allow_recovery,std::string& error,
                         editor_native::Snapshot* observed=nullptr) {
    if(hold.resumed)return true;
    hold.key=receipt.after;hold.revision=expected.document.revision;
    if(!editor_native::resume_terrain_transition(hold.ticket,receipt,expected,terrain_fence(hold,allow_close,allow_recovery),error,observed))return false;
    hold.resumed=true;return true;
}
bool finish_export_terrain(std::string& error) {
    if(!export_terrain)return true;
    auto terrain=*export_terrain;
    auto expected=current_native;expected.document=controller->state().document;
    if(!terrain.resumed){
        if(!refresh_terrain_hold(terrain,expected,false,false,error)){
            if(export_terrain && export_terrain->key.serial==terrain.key.serial)*export_terrain=std::move(terrain);
            return false;
        }
        editor_native::TerrainTransitionReceipt receipt;
        receipt.kind=editor_native::TerrainTransitionKind::Export;receipt.before=receipt.after=terrain.key;
        receipt.after.generation=terrain.key.generation;
        if(!editor_native::capture_scene_owner(receipt.after.owner,error) ||
           !resume_terrain_hold(terrain,receipt,expected,false,false,error))return false;
    }
    export_terrain.reset();return true;
}
void service_cancelled_save() {
    if(!save_cancelling || !save_terrain || !controller || transport_teardown)return;
    auto terrain=*save_terrain;std::string error;
    if(!terrain_runtime_current(terrain.key,terrain.transport,terrain.revision)){
        frozen=true;save_cancelling=false;tell("The cancelled Save belongs to an expired editing context");return;
    }
    if(!terrain_scene_current(terrain,error)){
        frozen=true;save_cancelling=false;tell(error);return;
    }
    if(!editor_native::state().ready)return;
    if(!terrain.scene_restored){
        if(!editor_native::restore(terrain.entry,error)){
            frozen=true;save_cancelling=false;tell("The cancelled Save could not restore its working draft: "+error);return;
        }
        terrain.scene_restored=true;save_terrain=terrain;
        if(!editor_native::state().ready)return;
    }
    if(!terrain.identity_restored){
        if(!terrain_scene_current(terrain,error) ||
           !editor_native_save::restore_identity(terrain.key.owner.source,terrain.entry.local_dirty.value_or(editor_native::Snapshot::Dirty{}),error)){
            frozen=true;save_cancelling=false;tell("The cancelled Save could not restore its native identity: "+error);return;
        }
        terrain.identity_restored=true;save_terrain=terrain;
    }
    if(!refresh_terrain_hold(terrain,terrain.entry,false,false,error)){
        save_terrain=std::move(terrain);
        if(error.empty())return;
        frozen=true;save_cancelling=false;tell("The cancelled Save terrain restoration stopped: "+error);return;
    }
    editor_native::TerrainTransitionReceipt receipt;
    receipt.kind=editor_native::TerrainTransitionKind::Export;receipt.before=receipt.after=terrain.key;
    receipt.after.generation=terrain.key.generation;
    if(!editor_native::capture_scene_owner(receipt.after.owner,error) ||
       !resume_terrain_hold(terrain,receipt,terrain.entry,false,false,error)){
        frozen=true;save_cancelling=false;tell("The cancelled Save terrain restoration stopped: "+error);return;
    }
    save_terrain.reset();save_cancelling=false;save_publication_pending=false;
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
    if(staged_capture){error="Wait for the held placement gesture to finish";return Result::Reject;}
    if(!local_creation&&!complexity_authorized()){error=complexity_conflict();return Result::Reject;}
    if(local_creation){
        if(local_creation->scope!=scope){error="Another local creation is already active";return Result::Reject;}
        const auto member=controller->view().members.find(scope.author.slot);
        return member!=controller->view().members.end() && member->second.peer==scope.author &&
            member->second.presence.creation_serial==scope.serial && member->second.presence.creating_asset?Result::Ready:Result::Wait;
    }
    if(rejection_restore || exporting || pending_export_request || native_exit_requested || pending || awaiting_operation || durable_commit ||
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
    retire_scene_alias();local_creation=std::move(saved);
    const editor_native::GestureScope native_scope{scope.project,scope.epoch,scope.author};
    if(!editor_native::capture_creation_terrain(native_scope,scope.serial,local_creation->scene_owner,
           local_creation->entry_native,local_creation->terrain_ticket,error)){
        local_creation.reset();return Result::Reject;
    }
    status="Confirming your local creation session with the host";
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
    if(local_creation->return_failed){error=local_creation->return_error;return false;}
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
        const editor_native::GestureScope scope{local_creation->scope.project,local_creation->scope.epoch,local_creation->scope.author};
        if(!local_creation->entered_adventure && !local_creation->terrain_resumed){
            if(!editor_native::resume_creation_terrain(local_creation->terrain_ticket,scope,local_creation->scope.serial,
                   local_creation->scene_owner,local_creation->entry_native,native.generation,error))return false;
            // This ticket is one-use. A later transient capture/camera retry
            // must retain the successful transfer rather than consume it again.
            local_creation->terrain_resumed=true;
        }
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
    queue_scene_alias_attachment(SceneAliasOwner{local_creation->scene_owner,native_generation});
    return true;
}
void fail_creation_return(const std::string& error){
    if(!local_creation || local_creation->materialized || local_creation->return_failed)return;
    local_creation->return_failed=true;local_creation->return_error=error;frozen=true;
    tell("The creation was retained locally, but the committed adventure could not be restored: "+error);
}
bool return_creation(const editor_creation::Scope& scope,const editor_creation::Result& result,std::string& error){
    if(!local_creation || local_creation->scope!=scope){error="This creation result has no retained adventure view";return false;}
    auto& saved=*local_creation;
    if(saved.return_failed){error=saved.return_error;return false;}
    if(!creation_current(scope)){error="The creation was saved locally; its shared adventure session expired";return false;}
    if(!result.cancelled && !saved.submitted){
        if(durable_commit || pending || awaiting_operation || exporting || pending_export_request)return false;
        if(!complexity_authorized()){saved.error=complexity_conflict();}
        else if(result.request.target && !creation_lease(scope,*result.request.target)){saved.error="The asset content lease expired; the saved creation remains available locally";}
        else if(!result.request.target && !lease({{editor::ScopeKind::Structure,{},{}}}))return false;
        if(saved.error.empty()){
            editor::ResourceKey version;std::vector<dbpf::Record> records;
            const auto reserved=[](const editor::ResourceKey& key){
                const dbpf::Key native_key{key.instance,key.type,key.group};
                return editor_creation_native::resource_reserved(key) || std::any_of(resources.bundle.records.begin(),resources.bundle.records.end(),[&](const auto& row){return row.key==native_key;});
            };
            editor_native::Snapshot changed;std::vector<uint8_t> assets;
            const editor_native::GestureScope model_scope{scope.project,scope.epoch,scope.author};
            const editor_creation_native::ModelFence model_fence{model_scope,editor_native::state().generation,
                [scope](const editor_native::GestureScope& expected,uint64_t generation){
                    return expected==editor_native::GestureScope{scope.project,scope.epoch,scope.author} &&
                        creation_current(scope) && editor_native::state().generation==generation;
                }};
            if(!editor_creation_native::collect_version(scope.project,result.saved,reserved,version,records,saved.error) ||
               !register_native_records(resources.root,records,saved.error) ||
               !editor_creation_native::validate_model(version,model_fence,saved.error) ||
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
                else if(!complexity_authorized()){saved.error=complexity_conflict();}
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
bool staged_capture_identity(const std::shared_ptr<StagedCapture>& pin,std::string& error,bool ending=false){
    if(!capture_context_current(pin,ending)){error="The staged gesture's original shared context expired";return false;}
    const auto scene=editor_native::creation_scene_state(pin->ticket.owner);
    if(!scene.available || !scene.owner || scene.generation!=pin->ticket.generation || !capture_context_current(pin,ending)){
        error="The staged gesture's original native owner changed";return false;
    }
    return true;
}
void freeze_staged_capture(const std::shared_ptr<StagedCapture>& pin,const std::string& reason,bool expired){
    if(staged_capture!=pin)return;
    RejectionRestore retained;retained.scope=pin->ticket.scope;retained.owner=pin->ticket.owner;
    retained.generation=pin->ticket.generation;retained.transport=pin->transport;retained.started=pin->started;
    retained.reason=reason;retained.failed=true;
    if(rejection_serial!=UINT64_MAX)retained.serial=++rejection_serial;
    rejection_restore=std::move(retained);staged_capture.reset();frozen=true;
    if(expired)native_identity_lost=true;
    tell("Recover the committed draft: "+reason);
}
bool staged_request(const StagedCapture& pin,const editor_native::Gesture& gesture,editor::Request& out,std::string& error){
    const auto& before=gesture.before.document;const auto& after=gesture.after.document;
    if(gesture.clone_source || gesture.before.source!=gesture.after.source || gesture.before.bindings!=gesture.after.bindings ||
       gesture.before.act!=gesture.after.act || gesture.before.captain!=gesture.after.captain ||
       gesture.before.captain_locked!=gesture.after.captain_locked || gesture.before.metadata!=gesture.after.metadata ||
       before.schema!=after.schema || before.class_schema!=after.class_schema || before.settings_schema!=after.settings_schema ||
       before.placements.size()!=after.placements.size() || before.classes!=after.classes ||
       before.acts!=after.acts || before.act_order!=after.act_order || before.class_settings!=after.class_settings ||
       before.settings!=after.settings || before.terrain!=after.terrain){error="A staged pose changed a non-placement subtree";return false;}
    out=pin.request;out.summary="Edit placement";
    for(const auto& [id,prior]:before.placements){
        const auto next=after.placements.find(id);
        if(next==after.placements.end() || !pin.canonical.document.placements.contains(id)){
            error="A staged pose changed its placement roster";return false;
        }
        if(same_authored(prior,next->second))continue;
        const auto guard=pin.guards.find(id);
        if(guard==pin.guards.end() || prior.class_id!=next->second.class_id || prior.act_id!=next->second.act_id ||
           prior.protected_object!=next->second.protected_object){error="The staged pose has no original placement ownership";return false;}
        for(auto field:{editor::Field::Position,editor::Field::Rotation,editor::Field::Scale}){
            const editor::FieldKey key{field,0};const auto wanted=editor::value(next->second,key);
            if(editor::value(prior,key)==wanted)continue;
            editor::Mutation mutation;mutation.target=id;mutation.field=key;mutation.value=wanted;
            mutation.expected=editor::field_revision(guard->second,key);out.mutations.push_back(std::move(mutation));
        }
        std::set<uint32_t> properties;for(const auto& [key,value]:prior.properties){(void)value;properties.insert(key);}
        for(const auto& [key,value]:next->second.properties){(void)value;properties.insert(key);}
        for(auto property:properties){
            const editor::FieldKey key{editor::Field::Property,property};const auto wanted=editor::value(next->second,key);
            if(editor::value(prior,key)==wanted)continue;
            if(property!=editor_native::kRelativeScaleProperty && property!=editor_native::kElevationProperty &&
               property!=editor_native::kSoundPitchProperty && property!=editor_native::kSoundGainProperty &&
               property!=editor_native::kDestinationPositionProperty && property!=editor_native::kDestinationRotationProperty &&
               property!=editor_native::kDestinationScaleProperty){error="A staged pose changed an unsupported placement property";return false;}
            editor::Mutation mutation;mutation.target=id;mutation.field=key;mutation.value=wanted;
            mutation.expected=editor::field_revision(guard->second,key);out.mutations.push_back(std::move(mutation));
        }
    }
    if(out.mutations.empty()){error="The staged pose has no authored placement change";return false;}
    return true;
}
void service_staged_capture(){
    using State=editor_native::CaptureState;
    const auto pin=staged_capture;
    if(!pin || staged_capture_servicing || !H->frame_index)return;
    const auto frame=H->frame_index();
    if(pin->last_attempt_frame==frame || canonical_work_frame==frame)return;
    struct Servicing {Servicing(){staged_capture_servicing=true;}~Servicing(){staged_capture_servicing=false;}} servicing;
    std::string error;
    if(closing || transport_teardown){
        if(!staged_capture_identity(pin,error,true))native_identity_lost=true;
        staged_capture.reset();return;
    }
    if(!capture_context_current(pin)){
        freeze_staged_capture(pin,"The staged gesture's original context expired; its native scene was left intact",true);return;
    }
    if(durable_commit)return;
    if(pin->rejected_export && pin->failure.empty()){decline_staged_export(pin);return;}
    if(!staged_capture_identity(pin,error)){freeze_staged_capture(pin,error,true);return;}
    if(durable_commit)return;
    if(now_ms()-pin->started>180000 && pin->failure.empty())pin->failure="The staged gesture did not finish before its deadline";
    if(!pin->failure.empty()){
        pin->last_attempt_frame=frame;
        const auto native=editor_native::state();
        if(!staged_capture_identity(pin,error)){freeze_staged_capture(pin,error,true);return;}
        if(durable_commit || !native.ready || native.gesture)return;
        error=pin->failure;stop_unsupported(error,pin->ticket.owner);
        if(pin->cancel_pending && pending && pending->request.operation==*pin->cancel_pending){
            if(pin->preserve_submitted && pending->submitted){
                // Saved precedes a later Decision in the authenticated FIFO.
                // Retire only its local Native image; keep the submitted
                // request identity until its accepted/rejected Decision.
                pending->capture_owner.reset();pending->staged=false;
            }else{pending.reset();awaiting_operation.reset();}
        }
        staged_capture.reset();return;
    }
    if(!pin->completed)return;
    pin->last_attempt_frame=frame;
    const auto head=controller->state().document.revision;
    auto stable=[&]{return capture_current(pin->ticket)==State::Ready && controller->state().document.revision==head;};
    auto wait=[&]{return capture_current(pin->ticket)==State::Wait || (capture_current(pin->ticket)==State::Ready &&
        controller->state().document.revision!=head);};
    auto& gesture=*pin->completed;
    if(!pin->before || !same_native_authored(*pin->before,gesture.before) ||
       pin->before->source!=gesture.before.source || pin->before->document!=gesture.before.document){
        pin->failure="The staged gesture has no exact owned baseline receipt";return;
    }
    ProjectionScope projection(pin->canonical.document);
    if(!complexity_authorized()){pin->failure=complexity_conflict();return;}
    stage_stamp(pin->canonical.document.project,pin->request.operation,pin->canonical.document.revision,editor_stage_metrics::Stage::SupportedBegin);
    const bool supported=editor_native::supported_changes(gesture,error);
    stage_stamp(pin->canonical.document.project,pin->request.operation,pin->canonical.document.revision,editor_stage_metrics::Stage::SupportedEnd,supported?1:2);
    if(wait())return;
    if(!staged_capture_identity(pin,error)){freeze_staged_capture(pin,error,true);return;}
    if(wait())return;
    if(!stable())return;
    if(!supported){pin->failure=error.empty()?"The staged gesture lost its exact native support receipt":error;return;}
    editor::Request request;
    if(!staged_request(*pin,gesture,request,error)){pin->failure=std::move(error);return;}
    for(const auto& token:pin->leases)if(!std::any_of(controller->view().local_leases.begin(),controller->view().local_leases.end(),[&](const auto& held){
        return held.owner==pin->ticket.scope.author && held.scope==token.scope && held.token==token.token && held.expires>now_ms();
    })){pin->failure="The staged gesture's original editing lease expired";return;}
    editor_native::Snapshot live;
    const bool witnessed=capture_recent(gesture.after,gesture.after.document,live,error) &&
        editor_native::validate(live,gesture.after.document,error) && supported_between(gesture.after,live,error);
    if(wait())return;
    if(!staged_capture_identity(pin,error)){freeze_staged_capture(pin,error,true);return;}
    if(wait() || !stable())return;
    if(!witnessed){pin->failure=error.empty()?"The staged gesture's native after-image changed before submission":error;return;}
    std::optional<RejectionRestore> continuation;
    if(head!=pin->canonical.document.revision){
        if(rejection_serial==UINT64_MAX){pin->failure="Native restoration identities are exhausted";return;}
        RejectionRestore restore;restore.scope=pin->ticket.scope;restore.owner=pin->ticket.owner;restore.generation=pin->ticket.generation;
        restore.transport=pin->transport;restore.started=pin->started;restore.serial=++rejection_serial;
        restore.reason="The staged pose's latest canonical baseline";restore.continuation=request.operation;
        restore.scene_revision=pin->canonical.document.revision;restore.scene_frame=frame;
        const auto native=editor_native::state();restore.act=logical_act(native.act);
        if(!native.selected.empty())restore.selected=native.selected.front();
        const bool view=editor_native::capture_view(restore.view,error);
        if(wait())return;
        if(!staged_capture_identity(pin,error)){freeze_staged_capture(pin,error,true);return;}
        if(wait() || !stable())return;
        if(!view){pin->failure=error.empty()?"The staged pose could not retain its local view":error;return;}
        continuation=std::move(restore);
    }
    if(!stable())return;
    pending=Pending{std::move(request),std::move(gesture),frame,false};pending->tentative=!continuation;pending->staged=true;pending->capture_owner=pin;
    awaiting_operation=pending->request.operation;
    if(continuation)rejection_restore=std::move(continuation);
    staged_capture.reset();guarded_placements.clear();guarded_settings.clear();guarded_authoring.clear();guarded_terrain.clear();
    last_presence_value.reset();
    status=continuation?"Restoring the latest draft before submitting the held placement gesture":"Submitting the staged placement gesture";
}
void receive_staged_capture(editor_native::Gesture gesture){
    const auto pin=staged_capture;
    if(!pin || !gesture.capture || pin->ticket!=*gesture.capture)return;
    if(pin->completed){
        if(!same_native_authored(pin->completed->after,gesture.after) || pin->completed->after.document!=gesture.after.document)
            pin->failure="A staged gesture delivered more than one after-image";
        return;
    }
    const auto charge=capture_snapshot_charge(gesture.before)+capture_snapshot_charge(gesture.after);
    if(charge>(64u<<20)-pin->charged){pin->failure="The staged after-image exceeds its owned proof limit";schedule();return;}
    pin->charged+=charge;pin->completed=std::move(gesture);
    stage_stamp(pin->canonical.document.project,pin->request.operation,pin->canonical.document.revision,editor_stage_metrics::Stage::CaptureEnd,1);
    service_staged_capture();
}
void pump_staged_capture(){
    service_scene_alias_attachment();
    service_staged_capture();
    if(!H || !H->frame_index)return;
    if(!staged_capture || capture_current(staged_capture->ticket)!=editor_native::CaptureState::Ready ||
       canonical_work_frame!=H->frame_index())editor_native::service_capture();
}
bool cancel_capture_for_barrier(const char* reason,bool preserve_submitted){
    if(staged_capture){
        capture_failed(staged_capture->ticket,reason);return false;
    }
    if(pending && pending->staged){
        if(rejection_restore && rejection_restore->continuation==pending->request.operation){
            rejection_restore->continuation.reset();pending.reset();awaiting_operation.reset();return false;
        }
        if(pending->capture_owner){
            staged_capture=pending->capture_owner;staged_capture->cancel_pending=pending->request.operation;
            staged_capture->preserve_submitted=preserve_submitted;
            // Its original value-owned context remains the local hold even if
            // a later rejected Decision retires Pending before rollback runs.
            pending->barrier_cancel=true;pending->tentative=false;
            capture_failed(staged_capture->ticket,reason);
        }
        return false;
    }
    // Generic pending gestures retain checkpoint_resources' existing
    // restore_tentative path. Only staged Native ownership needs this new
    // cancellation; Saved must not wait for a later FIFO Decision.
    return !detached_scene() && (preserve_submitted || (!pending && !awaiting_operation)) && !durable_commit && !closing && !frozen && !recovery &&
        !exporting && !pending_export_request && !native_exit_requested && !save_publication_pending;
}
bool prepare_capture_ready(){
    // Canonical Decisions retain authoring_ready. Only the Native Test
    // transition waits for local rollback and pending operations to end.
    return cancel_capture_for_barrier("The held placement gesture was cancelled before playtest preparation",false);
}
bool saved_capture_ready(){
    // Saved must install before its later Decision/resource delta. Preserve
    // submitted operations without retaining their tentative Native image.
    return cancel_capture_for_barrier("The held placement gesture was cancelled before installing the host Save",true);
}
void captured(editor_native::Gesture gesture) {
    if(gesture.capture){receive_staged_capture(std::move(gesture));return;}
    if(staged_capture){capture_failed(staged_capture->ticket,"Another native gesture bypassed the staged placement hold");return;}
    const auto released=std::exchange(native_release_qpc,0);
    const auto capture_begin=std::exchange(native_capture_begin_qpc,0),capture_end=std::exchange(native_capture_end_qpc,0);
    if (!controller || !session_active() || frozen || closing || recovery) return;
    if(rejection_restore){fail_rejection_restore(*rejection_restore,"A native gesture bypassed the restoration hold");return;}
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
    const auto captured_context=controller->view().context;const auto captured_author=controller->view().local;
    const auto captured_revision=doc.revision,captured_transport=observed_transport;
    editor_native::SceneOwner captured_owner;
    if(!editor_native::capture_scene_owner(captured_owner,error) || captured_owner.source!=gesture.after.source){
        frozen=true;native_identity_lost=true;tell("The captured gesture could not pin its native editor: "+error);return;
    }
    std::string identity_error;
    auto captured_identity=[&](bool ready=true,editor_native::SceneResumeState* observed=nullptr){
        identity_error.clear();
        auto context=[&]{return controller && C.session && C.session->connected() && !closing && !recovery && !transport_teardown &&
            observed_transport==captured_transport && C.session->transport_generation()==captured_transport &&
            controller->view().context.project==captured_context.project && controller->view().context.epoch==captured_context.epoch &&
            controller->view().local==captured_author && controller->state().document.revision==captured_revision;};
        if(!context() || editor_native::state().generation!=gesture.generation)return false;
        if(!ready){
            const auto scene=editor_native::creation_scene_state(captured_owner);
            if(observed)*observed=scene;
            return scene.available && scene.owner && scene.generation==gesture.generation && context();
        }
        editor_native::SceneOwner current;
        return editor_native::capture_scene_owner(current,identity_error) && current==captured_owner && context() &&
            editor_native::state().generation==gesture.generation;
    };
    auto reject_captured=[&]{
        const auto reason=error;
        if(!captured_identity()){
            frozen=true;native_identity_lost=true;
            tell("The captured gesture's native editor identity expired; the current scene was left intact: "+reason+
                (!identity_error.empty() && identity_error!=reason?"; "+identity_error:std::string{}));return;
        }
        error=reason;
        stop_unsupported(error,captured_owner);
    };
    const auto supported_begin=stage_qpc();
    if(!complexity_authorized()){error=complexity_conflict();reject_captured();return;}
    if (!supported_between(current_native, gesture.before, error)) {
        log_capture_rejection(same_native_authored(current_native,gesture.before)?"current-before-typed":"current-before-supported",gesture,error);
        reject_captured(); return;
    }
    if(!editor_native::supported_changes(gesture,error)){
        log_capture_rejection("gesture-supported",gesture,error);
        reject_captured(); return;
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
    if(!canonicalized){reject_captured();return;}
    const auto supported_end=stage_qpc();
    const bool roster=gesture.after.document.act_order!=doc.act_order;
    if (gesture.before.document.classes != doc.classes || gesture.before.document.acts != doc.acts ||
        gesture.before.document.act_order!=doc.act_order) {
        error="The native gesture changed an unsupported act or started from a stale cast roster";reject_captured();
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
        if(!lease({{editor::ScopeKind::Authoring,{},{}}})){error="The act roster has no shared authoring hold";reject_captured();return;}
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
            error="The authored settings roster changed during a gesture";reject_captured();return;
        }
        if(prior->second.properties==next->second.properties)continue;
        auto guard=guarded_authoring.find(id);
        if(guard==guarded_authoring.end()){error="The authored settings gesture has no current ownership";reject_captured();return;}
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
        if(owned==guarded_terrain.end()){error="The changed terrain stamp has no record ownership";reject_captured();return;}
        editor::Mutation mutation;mutation.target=id;mutation.expected=owned->second.revision;
        if(next==gesture.after.document.terrain.end())mutation.kind=editor::MutationKind::DeleteTerrain;
        else if(!editor_terrain_edit::replacement(owned->second,record,next->second,mutation,error)){reject_captured();return;}
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
        if(guard==guarded_settings.end()){error="The cast gesture has no class ownership";reject_captured();return;}
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
            log_unowned_placement_changes(gesture);
            error="The native gesture has no current placement ownership";reject_captured();return;
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
                reject_captured(); return;
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
        if(!collect_cast_assets(gesture.after,assets,error)){reject_captured();return;}
    }
    const bool tentative=!editing_cast && assets.empty() && std::all_of(r.mutations.begin(),r.mutations.end(),[&](const auto& mutation){
        return mutation.kind==editor::MutationKind::Set && doc.placements.contains(mutation.target);
    });
    std::optional<RejectionRestore> inverse_hold;
    if(tentative){
        if(!editor_native::validate(gesture.before,doc,error)){log_capture_rejection("gesture-before-canonical",gesture,error);reject_captured();return;}
    }else{
        if(!captured_identity()){reject_captured();return;}
        if(!H->frame_index || rejection_serial==UINT64_MAX){
            error="Native gesture submission requires a restoration frame and serial";reject_captured();return;
        }
        RejectionRestore pin;pin.scope=current_projection_scope();pin.owner=captured_owner;pin.generation=gesture.generation;
        pin.transport=captured_transport;pin.serial=++rejection_serial;pin.started=now_ms();pin.scene_revision=captured_revision;
        pin.reason="The native gesture's inverse";pin.continuation=r.operation;
        const auto native=editor_native::state();pin.act=logical_act(native.act);
        if(!native.selected.empty())pin.selected=native.selected.front();
        if(!editor_native::capture_view(pin.view,error) || !captured_identity()){reject_captured();return;}
        // Structural gestures still retire native temporary objects before the
        // canonical allocation can recreate their slots on a later frame.
        stage_stamp(doc.project,r.operation,doc.revision,editor_stage_metrics::Stage::InverseBegin);
        const bool restored=editor_native::apply(doc,revert,&current_native,error);
        stage_stamp(doc.project,r.operation,doc.revision,editor_stage_metrics::Stage::InverseEnd,restored?1:2);
        if(!restored){frozen=true;tell("The native gesture could not return to committed state: "+error);return;}
        // Restore can legitimately queue terrain and local-view work. Its raw
        // owner must remain exact, while readiness waits for a later frame.
        editor_native::SceneResumeState restored_scene;
        if(!captured_identity(false,&restored_scene)){
            frozen=true;native_identity_lost=true;tell("The native inverse's original scene identity expired; the current scene was left intact");return;
        }
        const bool terrain=std::any_of(r.mutations.begin(),r.mutations.end(),[](const auto& mutation){
            return mutation.kind==editor::MutationKind::AppendTerrain || mutation.kind==editor::MutationKind::SetTerrain ||
                mutation.kind==editor::MutationKind::DeleteTerrain;
        });
        if(terrain || !restored_scene.settled()){pin.scene_frame=H->frame_index();inverse_hold=std::move(pin);}
    }
    if(!inverse_hold && !captured_identity()){reject_captured();return;}
    if (!H->frame_index) { frozen = true; tell("Native gesture submission requires the game frame counter"); return; }
    if(!inverse_hold && !complexity_authorized()){error=complexity_conflict();reject_captured();return;}
    pending = Pending{r, std::move(gesture), H->frame_index(), false};
    pending->assets=std::move(assets);
    pending->tentative=tentative;
    awaiting_operation = r.operation;
    if(inverse_hold){rejection_restore=std::move(inverse_hold);last_presence_value.reset();
        status="Waiting for the native inverse to settle before submitting the edit";}
    else status = tentative?"Submitting the placement gesture":"Waiting for the next game frame before submitting the placement gesture";
}
void submit_pending_gesture() {
    if(pending && pending->barrier_cancel)return;
    static bool submitting=false;
    if(submitting)return;
    struct Submitting {bool& flag;Submitting(bool& value):flag(value){flag=true;}~Submitting(){flag=false;}} submitting_scope(submitting);
    if (!pending || pending->submitted) return;
    std::optional<RejectionRestore> continuation;
    if(rejection_restore && rejection_restore->continuation==pending->request.operation){
        if(!rejection_restore->verified)return;
        continuation=*rejection_restore;
    }
    const auto captured_operation=pending->request.operation;
    const auto native = editor_native::state();
    if(!controller || !pending || pending->request.operation!=captured_operation)return;
    if(continuation && rejection_wait_durable(*continuation))return;
    const auto& r = pending->request;
    const auto& v = controller->view();
    std::string error;
    const bool changed_generation = r.project!=v.context.project || r.operation.author!=v.local ||
        r.operation.epoch != v.context.epoch || pending->gesture.generation != native_generation || native.generation != native_generation;
    const bool ending = std::any_of(actions.begin(), actions.end(), [](auto action) { return action == Action::End || action == Action::Recover; });
    if (closing || ending || frozen || recovery || !C.session || !C.session->connected() || v.phase != Phase::Ready ||
        r.project != v.context.project || r.operation.author != v.local || r.operation.author.slot != C.session->my_id() ||
        r.base_revision > controller->state().document.revision || changed_generation) {
        if(continuation){
            if(closing || ending || recovery)return;
            fail_rejection_restore(*continuation,"The held inverse's submission context expired",true);return;
        }
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
    if (!native.ready || native.gesture || !H->frame_index || ((!pending->tentative || pending->staged) && H->frame_index() <= pending->reverted_frame)) return;
    if(continuation){
        const auto pin=*continuation;
        const auto scene=editor_native::creation_scene_state(pin.owner);
        if(!rejection_identity(pin,error)){fail_rejection_restore(pin,error,true);return;}
        if(rejection_wait_durable(pin))return;
        if(!scene.settled())return;
        if(controller->state().document.revision!=pin.scene_revision){rejection_restore->verified=false;return;}
    }
    if(!complexity_authorized()){
        if(continuation)rejection_restore.reset();
        pending.reset();awaiting_operation.reset();tell(complexity_conflict());return;
    }
    if(continuation && rejection_wait_durable(*continuation))return;
    if(continuation)rejection_restore.reset();
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
    next.world_hints=next.active && !staged_capture && !rejection_restore && native.ready && native.world_visible && v.phase==Phase::Ready && !next.frozen && !recovery;
    next.waiting = bool(staged_capture) || rejection_restore.has_value() || closing || recovery.has_value() || awaiting_operation.has_value() || v.phase == Phase::Joining || v.phase == Phase::Syncing || v.phase == Phase::Preparing || v.phase == Phase::Returning;
    if (v.phase == Phase::Returning) next.status = "Waiting for every player to return to the committed draft";
    next.confirming = v.phase == Phase::Preparing && v.playtest_mode==editor::wire::PlaytestMode::Together;
    next.can_cancel = v.phase==Phase::Preparing && next.host && !closing && !recovery && !next.frozen;
    next.playtesting = v.phase == Phase::Playing;
    next.can_return = next.playtesting && (next.host || (v.playtest_mode==editor::wire::PlaytestMode::Solo && v.local==v.playtest_tester));
    if(v.playtest_mode==editor::wire::PlaytestMode::Solo && (v.phase==Phase::Preparing || v.phase==Phase::Playing)){
        const auto name=C.session?C.session->name_of(v.playtest_tester.slot):"A collaborator";
        next.status=name+" is testing solo. Shared editing is paused";next.waiting=true;
    }
    const bool editable_scene = native.ready && next.active && !native_pose_prepare && !detached_scene() && !durable_commit && !closing && !recovery && !exporting && !pending_export_request && !native_exit_requested && v.phase == Phase::Ready && !next.frozen && !awaiting_operation && !native.gesture && !editor_native_save::busy();
    const bool editing=editable_scene&&!complexity_held();
    const auto complexity=editor::value(doc,editor_native::kProjectObject,{editor::Field::Property,editor_native::kExtendedComplexityProperty});
    if(complexity)if(auto flag=std::get_if<bool>(&*complexity))next.extended_complexity=*flag;
    next.can_complexity=editable_scene && complexity.has_value() && !complexity_request &&
        (next.extended_complexity || (complexity_proven()&&editor_native::extended_complexity_available()));
    if(complexity_held()&&!next.frozen&&!recovery&&!closing){next.status=complexity_conflict();next.waiting=true;next.world_hints=false;}
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
    if(recovery&&recovery->failed&&!closing)next.status=recovery->failure;
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
bool checkpoint_resources(editor_project::Resources content, editor_native::Snapshot native, std::string& error,
                          const std::function<bool(const editor_native::Snapshot&,std::string&)>& restored={}) {
    const auto alias_owner=pin_scene_alias_owner();retire_scene_alias();
    if(!alias_owner){error="The saved scene could not pin its original native attachment";return false;}
    if(!restore_tentative(error))return false;
    auto local_dirty = native.local_dirty;
    if (!editor_project::native_snapshot(content, controller->state().document, native, error)) return false;
    native.local_dirty = local_dirty;
    // Save can normalize opaque authoring metadata as well as rotate the key.
    // Install those exact bytes on the saving host as well as its guests before
    // another gesture captures the working resource.
    if (!scene_alias_owner_current(*alias_owner,error) || !editor_native::restore(native, error)) return false;
    if(restored && !restored(native,error))return false;
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
    queue_scene_alias_attachment(alias_owner);
    return true;
}
bool persist_saved_dependencies(editor::ResourceKey key,const std::vector<uint8_t>& raw,
                                const std::vector<dbpf::Record>& admitted,std::string& error){
    if(!controller){error="The Saved dependency operation has no current draft";return false;}
    const auto runtime=durable_runtime_domain;
    const auto context=controller->view().context;const auto author=controller->view().local;
    const auto revision=controller->state().document.revision;
    const auto generation=editor_native::state().generation;
    editor_native::SceneOwner owner;
    if(!editor_native::capture_scene_owner(owner,error))return false;
    const auto current=[=]{
        const auto logical=[&]{return H && durable_runtime_domain==runtime && controller && controller->view().context==context &&
            controller->view().local==author && controller->state().document.revision==revision;};
        if(!logical())return false;
        // This producer closes owner+generation AFTER all Native getters; a
        // final plain state() would reopen a same-generation owner gap.
        const auto native=editor_native::creation_scene_state(owner);
        return logical() && native.owner && native.generation==generation;
    };
    return editor_saved_native::persist({key.instance,key.type,key.group},raw,admitted,current,error);
}
void saved_native(editor::ResourceKey key,const editor_native_save::SaveOperation* operation=nullptr) {
    if (!host() || !session_active()) return;
    std::string error;
    editor_native::Snapshot native;
    auto content = resources;
    const dbpf::Key root{key.instance, key.type, key.group};
    std::vector<dbpf::Record> records;
    std::optional<TerrainHold> terrain=save_terrain;
    if(terrain){
        if(!operation || !terrain->save || *terrain->save!=*operation || operation->after!=key ||
           !terrain_fence(*terrain)()) {frozen=true;tell("The native Save terrain receipt expired before publication");return;}
        // The native Save As wizard may update title/tags. Its completed live
        // sidecars must be observed; the ticket owns the terrain proof even if
        // this ordinary capture retires the old source's global map.
    }
    if (!editor_native::capture(native, error)) { frozen = true; tell(error); return; }
    save_publication_pending = true; native_copy_owned = true;
    native.source = key;
    if (!editor_native_save::read_saved(key, native.bytes, error)) { frozen = true; tell(error); return; }
    const auto genuine_saved=native.bytes;
    editor_native::TerrainTransitionReceipt receipt;
    if(terrain){
        receipt.kind=editor_native::TerrainTransitionKind::Save;receipt.before=receipt.after=terrain->key;
        receipt.after.owner.source=key;receipt.save_generation=operation->module_generation;receipt.save_serial=operation->serial;
        auto current=*terrain;current.key=receipt.after;
        if(!editor_native::normalize_saved_terrain(terrain->ticket,receipt,terrain->entry,native,terrain_fence(current),error)){
            frozen=true;tell("The native Save completed locally; sharing its terrain proof stopped: "+error);return;
        }
    }
    if (
        !editor_native::normalize_saved_grounding(native,controller->state().document,error) ||
        !editor_native::validate(native, controller->state().document, error)) { frozen = true; tell(error); return; }
    if (!adventure_collect::collect_snapshot(root, native.bytes, records, error)) { frozen = true; tell(error); return; }
    auto normalized = std::find_if(records.begin(), records.end(), [&](const auto& record) { return record.key == root; });
    if (normalized == records.end()) { frozen = true; tell("The saved editor root is missing"); return; }
    native.source = key; native.bytes = std::move(normalized->data);
    if (!persist_saved_dependencies(key,genuine_saved,content.bundle.records,error) ||
        !replace_root(content, native, error) ||
        !checkpoint_resources(std::move(content), std::move(native), error,[&](const auto& restored,std::string& reason){
            return !terrain || resume_terrain_hold(*terrain,receipt,restored,false,false,reason);
        }) ||
        !controller->publish_saved({controller->state().document.revision, key}, error)) {
        frozen = true; tell("The native save completed, but sharing its recovery checkpoint stopped: " + error); return;
    }
    save_terrain.reset();
    tell("The shared adventure draft was saved by Spore");
}
void start_saved() {
    reopen_saved.reset();
    retire_scene_alias();const auto alias_owner=pin_scene_alias_owner();
    if(!alias_owner){tell("The current native scene could not be pinned before shared attachment");return;}
    beginning = false;
    refresh_complexity_capability();
    std::string error;
    editor_native::Snapshot native;
    if (!editor_native::capture_draft(native, error)) { tell(error); return; }
    if(editor::extended_complexity(native.document)&&(!complexity_sample||!complexity_sample->available||complexity_publication_failed)){
        tell(complexity_conflict());return;
    }
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
        !scene_alias_owner_current(*alias_owner,error) || !register_native_records(content.root,content.bundle.records, error) ||
        !scene_alias_owner_current(*alias_owner,error) || !editor_native::restore(native, error)) { tell(error); return; }
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
    queue_scene_alias_attachment(alias_owner);
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
    for(const auto& entry:patch.terrain)scopes.push_back({editor::ScopeKind::Terrain,entry.object,{}});
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
    if(rejection_restore){
        // An explicit end/disconnect retires the unverified scene without
        // attempting another restore into an owner that may have expired.
        native_identity_lost=true;frozen=true;rejection_restore.reset();
    }
    waiting_history.reset();
    queued_exports.clear();queued_solo.reset();deferred_test.reset();creation_actions.clear();complexity_request.reset();
    pending_preparation_cancel.reset();
    preview_selection.reset();
    observed_lease_grants.clear();
    if (!closing) {
        reopen_saved.reset();close_terrain.reset();
        if(!staged_capture && current_projection_scope().epoch){TerrainHold terrain;std::string error;if(capture_terrain_hold(terrain,error))close_terrain=std::move(terrain);}
        closing = true; close_restored = false; close_preserve_offer = preserve_offer;
    }
    if(staged_capture){status="Retiring the held native capture before ending shared editing";return;}
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
        if(close_terrain&&!close_terrain->resumed){
            auto terrain=*close_terrain;editor_native::TerrainTransitionReceipt receipt;
            receipt.kind=editor_native::TerrainTransitionKind::Export;receipt.before=receipt.after=terrain.key;
            receipt.after.generation=native_generation;editor_native::Snapshot observed;
            if(!editor_native::capture_scene_owner(receipt.after.owner,error) ||
               !resume_terrain_hold(terrain,receipt,current_native,true,false,error,&observed)){
                frozen=true;tell("Ending shared editing is waiting for terrain provenance: "+error);return;
            }
            terrain.entry=std::move(observed);close_terrain=std::move(terrain);
        }
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
        const bool captured=close_terrain&&close_terrain->resumed?(observed=close_terrain->entry,true):editor_native::capture(observed,error);
        if (captured && observed.source == current_native.source &&
            editor_native::validate(observed, current_native.document, error) && supported_between(current_native, observed, error)) {
            // Native resource envelopes omit projected captain metadata. Retain
            // the verified live metadata and canonical logical namespace in the
            // proof pin, without rewriting any native or durable resource.
            observed.document = current_native.document; reopen_saved = std::move(observed);
        } else if (H) H->log(M, SK_INFO, "Saved-draft reopen proof was not pinned: %s", error.c_str());
    }
    if (!keep_offer) controller->end(); pending.reset(); awaiting_operation.reset(); requested_scopes.clear(); guarded_placements.clear(); incoming_resources.reset();
    local_creation.reset();staged_capture.reset();retire_scene_alias();
    guarded_settings.clear();guarded_authoring.clear();cast_uploads.clear();cast_upload_errors.clear();solo_preview.reset();solo_ready_scope.reset();pending_solo_return.reset();pending_solo_request=0;
    recovery.reset(); beginning = false; frozen = false; save_publication_pending = false;
    save_terrain.reset();export_terrain.reset();close_terrain.reset();export_return_result.reset();save_cancelling=false;
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
void fail_recovery(std::string reason){
    frozen=true;
    if(!recovery){tell(reason);return;}
    if(recovery->failed)return;
    recovery->failure=std::move(reason);recovery->failed=true;tell(recovery->failure);
}
void finish_recovery() {
    if (closing || !recovery || recovery->failed) return;
    refresh_complexity_capability();
    if(editor::extended_complexity(recovery->state.document)&&(!complexity_sample||!complexity_sample->available||complexity_publication_failed)){
        status=complexity_conflict();return;
    }
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
        if (!editor_native_save::read_saved(key, native.bytes, error)) { recovery.reset(); frozen = true; tell(error); return; }
        const auto genuine_saved=native.bytes;
        if (!editor_native::normalize_saved_grounding(native,recovery->state.document,error) ||
            !editor_native::validate(native, recovery->state.document, error)) { recovery.reset(); frozen = true; tell(error); return; }
        if (!adventure_collect::collect_snapshot(root, native.bytes, records, error)) { recovery.reset(); frozen = true; tell(error); return; }
        auto normalized = std::find_if(records.begin(), records.end(), [&](const auto& record) { return record.key == root; });
        if (normalized == records.end()) { recovery.reset(); frozen = true; tell("The recovered editor root is missing"); return; }
        native.source = key; native.bytes = std::move(normalized->data);
        if (!persist_saved_dependencies(key,genuine_saved,content.bundle.records,error) ||
            !replace_root(content, native, error) ||
            !editor_project::encode_resources(content, recovery->envelope, error) ||
            !editor_project::native_snapshot(content, recovery->state.document, native, error) ||
            !editor_native::restore(native, error)) {
            recovery.reset(); frozen = true; tell("The new native copy was saved; recovery could not be shared: " + error); return;
        }
        recovery->native = std::move(native); resources = std::move(content); native_copy_owned = true;
        recovery->saved_key.reset(); recovery->saving = recovery->needs_copy = false;
    }
    // Keep the previous checkpoint while the new epoch is materializing. A
    // successfully started controller is still held by recovery until its exact
    // native scene has passed the transition's independent strict readback.
    if(!recovery->epoch_prepared){
        if (!editor_native::rebaseline(error)) { status = "Waiting to retire native captures before recovery: " + error; return; }
        recovery->native_generation=editor_native::state().generation;native_generation=recovery->native_generation;
        controller->end_for_recovery();pending.reset();awaiting_operation.reset();guarded_placements.clear();
        std::random_device random;recovery->state.document.epoch=(uint64_t(random())<<32)|random();
        if(!recovery->state.document.epoch)recovery->state.document.epoch=1;
        recovery->state.retired_sequences.clear();sequence=0;recovery->native.document=recovery->state.document;
        if(!editor_project::store_snapshot(recovery->state,recovery->native,recovery->title,recovery->staged,error)){
            fail_recovery(error);return;
        }
        recovery->epoch_prepared=true;
    }
    if(!recovery->controller_started){
        if(!controller->start(recovery->state,recovery->envelope,recovery->title,error,recovery->resume)){
            fail_recovery(error);return;
        }
        local_capability=controller->view().local;recovery->controller_started=true;
    }
    TerrainHold terrain;
    if(recovery->terrain)terrain=*recovery->terrain;
    editor_native::TerrainTransitionKey target;
    target.scope={controller->view().context.project,controller->view().context.epoch,controller->view().local};
    target.serial=terrain.key.serial;
    if(!target.serial){
        if(terrain_transition_serial==UINT64_MAX){fail_recovery("Recovery terrain identity is exhausted");return;}
        target.serial=++terrain_transition_serial;
    }
    target.generation=recovery->native_generation;
    if(!context.world_visible || !editor_native::capture_scene_owner(target.owner,error))return;
    const auto transport=observed_transport;const auto revision=recovery->state.document.revision;
    const auto current=[=]{return terrain_runtime_current(target,transport,revision,false,true);};
    bool transferred=terrain.resumed;
    if(transferred && terrain.key!=target){fail_recovery("The recovered native terrain owner changed before checkpoint publication");return;}
    if(terrain.ticket&&!terrain.materializing&&!terrain.resumed){
        editor_native::TerrainTransitionReceipt receipt;
        receipt.kind=editor_native::TerrainTransitionKind::Recovery;receipt.before=terrain.key;receipt.after=target;
        terrain.key=target;terrain.transport=transport;terrain.revision=revision;
        transferred=editor_native::resume_terrain_transition(terrain.ticket,receipt,recovery->native,current,error);
        terrain.resumed=transferred;
    }
    if(!transferred){
        if(!terrain.materializing){
            // A lost or diverged old map cannot prove the current raw recipe.
            // Bootstrap from the verified canonical snapshot under a distinct
            // explicit recovery boundary, with no inferred old authored values.
            editor_native::TerrainTransitionReceipt receipt;
            receipt.kind=editor_native::TerrainTransitionKind::RecoveryBootstrap;receipt.before=receipt.after=target;
            editor_native::TerrainTransitionTicket prepared;
            if(!editor_native::restore_terrain_transition({},receipt,recovery->native,current,prepared,error)){
                fail_recovery("The prior recovery checkpoint was retained: "+error);return;
            }
            terrain.key=target;terrain.ticket=std::move(prepared);terrain.entry=recovery->native;
            terrain.transport=transport;terrain.revision=revision;terrain.materializing=true;recovery->terrain=std::move(terrain);
            status="Waiting for the recovered terrain and collision scene";return;
        }
        editor_native::TerrainTransitionReceipt receipt;
        receipt.kind=editor_native::TerrainTransitionKind::Recovery;receipt.before=terrain.key;receipt.after=target;
        if(!editor_native::resume_terrain_transition(terrain.ticket,receipt,recovery->native,current,error)){
            fail_recovery("The prior recovery checkpoint was retained: "+error);return;
        }
        terrain.resumed=true;terrain.materializing=false;
    }
    recovery->terrain=std::move(terrain);
    if(!current()){fail_recovery("The prior recovery checkpoint was retained because its new editing context expired");return;}
    recovery->staged.manifest=manifest(resource_hash,history_artifacts);
    if(!store.checkpoint(recovery->staged,error)){fail_recovery(error);return;}
    current_native=std::move(recovery->native);rendered_revision=current_native.document.revision;
    native_generation=recovery->native_generation;native_attached=true;native_identity_lost=false;frozen=false;
    recovery.reset();
    queue_scene_alias_attachment(SceneAliasOwner{target.owner,target.generation});
    refresh_complexity_capability();
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
    if(staged_capture || native_pose_prepare)return false;
    const auto n=editor_native::state();
    return !rejection_restore && n.ready && !n.gesture && n.generation==native_generation && !pending && !awaiting_operation &&
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
    exporting->ending=true;exporting->result=success;
    std::string error;
    if(!finish_export_terrain(error)){
        if(error.empty())return;
        frozen=true;success=false;tell("The export hold could not restore its terrain proof: "+error);
    }
    auto end=exporting->command;end.kind=CommandKind::EndExport;end.accepted=success;
    send_command(end);last_host_command=std::max(last_host_command,end.serial);exporting.reset();
    tell(success?"Local copy saved; shared editing resumed":"Local copy saving stopped; the shared draft is preserved");
}
void request_export(const Command& requested) {
    const auto& view=controller->view();
    if(!complexity_authorized()){
        auto reply=requested;reply.kind=CommandKind::EndExport;reply.accepted=false;send_command(reply,requested.actor.slot);tell(complexity_conflict());return;
    }
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
    TerrainHold terrain;std::string error;
    if(!capture_terrain_hold(terrain,error)){
        auto reply=requested;reply.kind=CommandKind::EndExport;reply.accepted=false;send_command(reply,requested.actor.slot);tell(error);return;
    }
    export_terrain=std::move(terrain);
    exporting=barrier;send_command(barrier.command);
    tell("Saving a collaborator's local copy; authoring is held briefly");
}
void finish_export(bool saved) {
    export_return_result=saved;
    std::string error;
    if(export_before) {
        const auto before=*export_before;
        if(saved && export_terrain && export_terrain->save && !export_terrain->save_checked){
            auto terrain=*export_terrain;const auto operation=*terrain.save;
            editor_native::TerrainTransitionReceipt receipt;
            receipt.kind=editor_native::TerrainTransitionKind::Save;receipt.before=receipt.after=terrain.key;
            receipt.after.owner.source=operation.after;receipt.save_generation=operation.module_generation;receipt.save_serial=operation.serial;
            editor_native::Snapshot copy;auto completed=terrain;completed.key=receipt.after;
            std::vector<uint8_t> genuine_saved;
            bool checked=editor_native::capture(copy,error) && editor_native_save::read_saved(operation.after,copy.bytes,error);
            if(checked)genuine_saved=copy.bytes;
            checked=checked && editor_native::normalize_saved_terrain(terrain.ticket,receipt,before,copy,terrain_fence(completed),error) &&
                editor_native::normalize_saved_grounding(copy,before.document,error) &&
                editor_native::validate(copy,before.document,error);
            const auto admitted=resources.bundle.records;
            if(!checked || !persist_saved_dependencies(operation.after,genuine_saved,admitted,error)){
                saved=false;tell("The local native copy was retained; terrain validation stopped: "+error);
            }
            if(export_terrain)export_terrain->save_checked=true;
            export_return_result=saved;
        }
        const auto source=export_terrain?export_terrain->key.owner.source:before.source;
        bool restored=editor_native::state().generation==export_generation &&
            (!export_terrain || terrain_scene_current(*export_terrain,error));
        if(restored && (!export_terrain || !export_terrain->scene_restored)){
            if(!editor_native::state().ready)return;
            restored=editor_native::restore(before,error);
            if(restored&&export_terrain)export_terrain->scene_restored=true;
        }
        if(restored&&!editor_native::state().ready)return;
        if(restored && (!export_terrain || !export_terrain->identity_restored)){
            restored=(!export_terrain || terrain_scene_current(*export_terrain,error)) &&
                editor_native_save::restore_identity(source,before.local_dirty.value_or(editor_native::Snapshot::Dirty{}),error);
            if(restored&&export_terrain)export_terrain->identity_restored=true;
        }
        if(restored&&!finish_export_terrain(error)){
            if(error.empty())return;
            restored=false;
        }
        if(!restored) {
            frozen=true;tell("The local copy operation could not restore the shared working draft: "+error);saved=false;
        }
        export_before.reset();
    }
    export_return_result.reset();
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
            reply.accepted=complexity_authorized() && !test_request && !exporting && !native_exit_requested && cached.can_playtest && check_native_integrity(error) &&
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
        if(value.kind==CommandKind::FinishExport && !value.accepted && !exporting->started){end_export(false);return;}
        if(value.kind==CommandKind::FinishExport && value.actor==exporting->command.actor && exporting->started)end_export(value.accepted);
        return;
    }
    if(from)return;
    if(value.kind==CommandKind::ExportQueued){
        if(value.actor==view.local && value.serial==pending_export_request){
            std::string error;if(!finish_export_terrain(error)){frozen=true;tell(error);}
            pending_export_request=0;tell("Save is queued until the host returns. You can continue editing");}return;
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
        if(staged_capture){
            last_host_command=value.serial;staged_capture->rejected_export=value;staged_capture->export_negative_sent=false;
            decline_staged_export(staged_capture);
            status="Finishing the held placement gesture before saving a local copy";return;
        }
        ExportBarrier barrier;barrier.command=value;
        if(!local_creation && !export_terrain){
            TerrainHold terrain;std::string error;
            if(!capture_terrain_hold(terrain,error)){barrier.rejected=true;tell(error);}else export_terrain=std::move(terrain);
        }
        last_host_command=value.serial;exporting=barrier;
        pending_export_request=0;tell("Saving a collaborator's local copy; authoring is held briefly");return;
    }
    if(value.kind==CommandKind::ExitEditor) {
        if(value.serial<=last_host_command)return;
        last_host_command=value.serial;remote_exit=true;native_exit_requested=native_exit_confirmed=true;return;
    }
    if(value.kind==CommandKind::EndExport && staged_capture && staged_capture->rejected_export){
        const auto& rejected=*staged_capture->rejected_export;
        if(value.serial==rejected.serial && value.actor==rejected.actor && value.revision==rejected.revision){
            staged_capture->rejected_export.reset();staged_capture->export_negative_sent=false;
            status="Checking the held placement gesture after the save hold ended";return;
        }
    }
    if(value.kind==CommandKind::EndExport && !exporting && pending_export_request==value.serial && value.actor==view.local) {
        std::string error;if(!finish_export_terrain(error)){frozen=true;tell(error);}
        pending_export_request=0;exit_after_save=false;tell("The host is busy; try saving your local copy again shortly");return;
    }
    if(!exporting || value.serial!=exporting->command.serial || value.actor!=exporting->command.actor)return;
    if(value.kind==CommandKind::BeginExport)exporting->started=true;
    if(value.kind==CommandKind::EndExport) {
        exporting->ending=true;exporting->result=value.accepted;
    }
}
void service_export() {
    if(export_return_result){finish_export(*export_return_result);return;}
    if(!exporting && host() && !local_creation && idle_native() && controller->settled()){
        for(auto it=queued_exports.begin();it!=queued_exports.end();){
            auto member=controller->view().members.find(it->first.slot);
            if(member==controller->view().members.end() || member->second.peer!=it->first){it=queued_exports.erase(it);continue;}
            if(member->second.presence.creating_asset || !member->second.presence.scene_ready){++it;continue;}
            const auto request=it->second;queued_exports.erase(it);request_export(request);break;
        }
    }
    if(!exporting)return;
    if(exporting->ending){
        const auto result=exporting->result;
        if(host()){end_export(result);return;}
        std::string error;
        if(!finish_export_terrain(error)){
            if(error.empty())return;
            frozen=true;tell("The export terrain restoration stopped: "+error);
        }
        exporting.reset();pending_export_request=0;
        if(exit_after_save){if(result&&!frozen)native_exit_requested=native_exit_confirmed=true;else exit_after_save=false;}
        tell(result?"Local copy saved; shared editing resumed":"Local copy saving was cancelled or failed; the shared draft is preserved");return;
    }
    if(host()) {
        if(!complexity_authorized()&&!exporting->started){end_export(false);return;}
        const auto& members=controller->view().members;
        for(uint8_t slot=0;slot<4;++slot)if((exporting->participants>>slot)&1) {
            auto it=members.find(slot);
            if(it==members.end() || it->second.phase!=Phase::Ready) {end_export(false);return;}
        }
    }
    if(!exporting->acknowledged && (idle_native() || (local_creation && !durable_commit && !awaiting_operation))) {
        if(export_terrain&&!local_creation&&!exporting->rejected){
            auto terrain=*export_terrain;auto expected=current_native;expected.document=controller->state().document;std::string error;
            if(!refresh_terrain_hold(terrain,expected,false,false,error)){
                if(export_terrain&&export_terrain->key.serial==terrain.key.serial)*export_terrain=std::move(terrain);
                if(error.empty())return;
                exporting->rejected=true;tell("The export revision could not be pinned: "+error);
            }else export_terrain=std::move(terrain);
        }
        exporting->acknowledged=true;
        if(host()){if(exporting->rejected){end_export(false);return;}exporting->held|=1;}
        else {auto held=exporting->command;held.kind=exporting->rejected?CommandKind::FinishExport:CommandKind::ExportHeld;
            held.actor=controller->view().local;held.accepted=false;send_command(held);}
    }
    if(host() && !exporting->started && exporting->held==exporting->participants && controller->settled()) {
        exporting->started=true;auto begin=exporting->command;begin.kind=CommandKind::BeginExport;send_command(begin);
    }
    if(exporting && exporting->started && !exporting->finished && !export_before && exporting->command.actor==controller->view().local && idle_native()) {
        std::string error;editor_native::Snapshot before;
        if(!complexity_authorized()){tell(complexity_conflict());finish_export(false);return;}
        if(exporting->rejected || !export_terrain){tell("The local copy has no verified held terrain receipt");finish_export(false);return;}
        auto terrain=*export_terrain;auto expected=current_native;expected.document=controller->state().document;
        if(!refresh_terrain_hold(terrain,expected,false,false,error)){
            export_terrain=std::move(terrain);if(error.empty())return;tell(error);finish_export(false);return;
        }
        before=terrain.entry;export_terrain=std::move(terrain);export_generation=native_generation;export_before=std::move(before);
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
    if(staged_capture && action!=Action::End){tell("Wait for the held placement gesture to finish");return;}
    if(rejection_restore && action!=Action::End && action!=Action::Recover){
        tell("Wait for the rejected edit's native restoration to be verified");return;
    }
    // These actions can be queued while their toolbar's previous-frame state
    // was still enabled. Use the current cohort proof at execution as well.
    if(!complexity_authorized() && (action==Action::Undo || action==Action::Redo || action==Action::Save || action==Action::SaveExit ||
        action==Action::Solo || action==Action::ChooseTest || action==Action::MenuSolo || action==Action::MenuTogether ||
        action==Action::Ready || action==Action::ApproveTest ||
        (action==Action::Playtest && !(controller->view().phase==Phase::Preparing&&cached.ready)))){
        tell(complexity_conflict());return;
    }
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
                TerrainHold terrain;if(!capture_terrain_hold(terrain,error)){tell(error);break;}
                auto request=command(CommandKind::RequestExport,controller->view().local);
                export_terrain=std::move(terrain);
                if(send_command(request))pending_export_request=request.serial;else export_terrain.reset();
            }
            break;
        }
        test_request.reset();
        if (cached.can_save && controller->view().phase == Phase::Ready && !frozen && !recovery &&
            native.ready && !native.gesture && native.generation == native_generation && !editor_native_save::busy() &&
            check_native_integrity(error)){
            TerrainHold terrain;
            if(!capture_terrain_hold(terrain,error)){tell(error);break;}
            save_terrain=std::move(terrain);
            if(!(native_copy_owned ? editor_native_save::save(error) : editor_native_save::save_as(error))){save_terrain.reset();tell(error);}
        }
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
        auto rejected_owner=[&]{
            if(!rejection_restore)return true;
            if(!rejection_restore->failed){error="Wait for the rejected edit's native restoration to be verified";return false;}
            const auto pin=*rejection_restore;
            if(rejected_recovery_owner(pin,error))return true;
            native_identity_lost=true;return false;
        };
        if(!rejected_owner()){tell(error);break;}
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
            !replace_root(resources, native, error) || !editor_project::encode_resources(resources, envelope, error)) { tell(error); break; }
        if(!rejected_owner()){tell(error);break;}
        if(!register_native_records(resources.root,resources.bundle.records,error)){tell(error);break;}
        if(!rejected_owner()){tell(error);break;}
        const auto resume = recovered_capability.slot == 0 ? std::optional<editor::Peer>(recovered_capability) : std::nullopt;
        if(!editor_native::validate_entry(native.bytes,native.act,error)){tell(error);break;}
        if(!rejected_owner()){tell(error);break;}
        const auto editor_state=editor_native::state();
        if(!rejected_owner()){tell(error);break;}
        if (!editor_state.editing && !editor_native::enter(native.source, error)) { tell(error); break; }
        native_copy_owned = owned;
        std::optional<TerrainHold> retained;
        if(current_projection_scope().epoch){TerrainHold terrain;std::string proof_error;if(capture_terrain_hold(terrain,proof_error))retained=std::move(terrain);}
        if(!rejected_owner()){tell(error);break;}
        rejection_restore.reset();
        recovery = Recovery{std::move(recovered), std::move(native), std::move(envelope), checkpoint.metadata, resume,
            false,!owned,false,std::nullopt,0,std::nullopt,false,false,{}};
        recovery->terrain=std::move(retained);
        status = "Loading the durable adventure draft"; finish_recovery(); break;
    }
    }
}
void SK_CDECL drain(void*) {
    queued = false;
    // A nested message pump consumes its queued task but cannot reenter the
    // Controller or Native work. The next frame/pulse retries without reposting.
    if (runtime_draining || editor_native::service_busy() || !controller) return;
    editor_service_pulse::ServiceGuard draining(runtime_draining);
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
        refresh_complexity_capability();
        // The single in-flight commit owns Store until the worker has joined.
        // Transport and keepalives continue, while scene replacement, Save and
        // recovery maintenance wait for a known durable outcome.
        if(durable_commit){controller->tick(now_ms());if(durable_commit){pump_staged_capture();refresh();return;}}
        if (transport_teardown) { pump_staged_capture();service_transport_teardown(); refresh(); return; }
        submit_pending_gesture();
        // start() stages joined players locally. Do not publish the new epoch,
        // baseline, or claimed durability before the recovery checkpoint exists.
        if(!(recovery&&recovery->controller_started))controller->tick(now_ms());
        if(durable_commit || native_pose_prepare){pump_staged_capture();refresh();return;}
        service_rejection_restore();
        if(!creation_actions.empty() && !complexity_held() && !editor_creation::active() && !detached_scene() && !durable_commit && controller->view().phase==Phase::Ready){
            const auto action=creation_actions.front();creation_actions.pop_front();
            editor::ResourceKey source{0,action.type,0};std::string error;
            if(action.target){
                for(const auto& [id,row]:controller->state().document.class_settings)if(row.class_id==*action.target){
                    (void)id;auto field=row.properties.find(editor::kClassAssetResourceProperty);
                    if(field!=row.properties.end())if(auto key=std::get_if<editor::ResourceKey>(&field->second))source=*key;
                }
                if(!source.instance)error="The selected cast entry has no editable creation content";
            }
            if(error.empty() && !editor_creation::open(source.type,source,action.target,error,action.editor_id) && error.empty())error="The native creation editor is unavailable";
            if(!error.empty())tell(error);
        }
        if(local_creation && !local_creation->return_failed && !frozen && !editor_creation::active() && !durable_commit){
            std::string error;
            if(materialize_creation_return(error))local_creation.reset();
            else if(!error.empty())fail_creation_return(error);
        }
        observe_lease_grants();
        if(complexity_request && controller->view().phase==Phase::Ready && !pending && !awaiting_operation && !durable_commit && !detached_scene() && !frozen && !closing){
            if(*complexity_request && (!complexity_authorized(true) || !editor_native::extended_complexity_available())){
                complexity_request.reset();tell(complexity_conflict());
            }
        }
        if(complexity_request && controller->view().phase==Phase::Ready && !pending && !awaiting_operation && !durable_commit && !detached_scene() && !frozen && !closing){
            const editor::FieldKey field{editor::Field::Property,editor_native::kExtendedComplexityProperty};
            const auto& doc=controller->state().document;
            if(doc.settings.contains(editor_native::kProjectObject) && lease({{editor::ScopeKind::Field,editor_native::kProjectObject,field}},!*complexity_request)){
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
            saved_capture_ready() && editor_native::state().ready && !editor_native::state().gesture && !editor_native_save::busy() &&
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
        service_cancelled_save();service_export();service_native_exit();service_automatic_follow();
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
                    const bool keep=staged_capture || native.gesture || pending || awaiting_operation ||
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
            presence.pending = bool(staged_capture) || awaiting_operation.has_value() || rejection_restore.has_value() || complexity_held();
            presence.scene_revision=rendered_revision;presence.scene_ready=complexity_authorized() && !detached_scene() && native.ready && !native.gesture && !durable_commit && !(pending && pending->tentative);
            if(local_creation){presence.creating_asset=true;presence.creation_serial=local_creation->scope.serial;
                presence.creation_target=local_creation->request.target.value_or(editor::ObjectId{});
                presence.act=controller->state().document.acts.contains(local_creation->authored_act)?local_creation->authored_act:logical_act(0);presence.selected={};presence.editing=false;}
            if (now_ms() - last_presence >= 300 || !last_presence_value || *last_presence_value != presence) {
                last_presence = now_ms(); std::string error;
                if(controller->set_presence(presence,error))last_presence_value=presence;
            }
            if (now_ms()-last_preview>=100) {
                last_preview=now_ms();editor_native::LivePreview hint;std::string error;
                if(!recovery && !rejection_restore && !awaiting_operation && native.generation==native_generation &&
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
        pump_staged_capture();
        refresh();
    } catch (const std::exception& e) { frozen = true; tell(std::string("Shared editing stopped safely: ") + e.what()); refresh(); }
    // A local native failure must be visible to the host. Otherwise a connected
    // frozen replica can keep sending transport keepalives without ever covering
    // the accepted edit that a graceful host close is waiting to deliver.
    if (frozen && !recovery && session_active() && controller->view().phase != Phase::Frozen) {
        controller->freeze_local(status); refresh();
    }
}
void schedule() { if (H && controller && !queued && !runtime_draining) { queued = true; H->run_outside_frame(drain, nullptr); } }
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
    if(controller || durable_commit || native_pose_prepare || durable_runtime_domain==UINT64_MAX)return false;
    ++durable_runtime_domain;
    char pose_option[2]{};
    pose_prepare_experiment=GetEnvironmentVariableA("SPORECOOP_EDITOR_TWO_FRAME_POSE",pose_option,sizeof(pose_option))==1 && pose_option[0]=='1';
    pose_prepare_route=pose_prepare_experiment?"not-enrolled":"disabled";
    frozen=beginning=closing=gc_due=recovery_available=scanned_recovery=native_identity_lost=false;
    rejection_restore.reset();staged_capture.reset();retire_scene_alias();scene_alias_servicing=false;staged_capture_servicing=false;canonical_work_frame=UINT64_MAX;
    rejection_servicing=false;
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
    save_terrain.reset();export_terrain.reset();close_terrain.reset();export_return_result.reset();save_cancelling=false;terrain_transition_serial=0;
    command_serial=last_host_command=export_generation=handoff_prompt_serial=0;seen_commands={};seen_command_peers={};source_creation={};
    native_playtesting=return_started=disconnected_return=end_after_return=false;return_error.clear();
    preview_selection.reset();pending_preview_return.reset();pending_preparation_cancel.reset();
    if (!C.session) return false;
    observed_transport = C.session->transport_generation(); retired_transport = 0; transport_teardown.reset(); deferred_offer.reset(); settled_cancellation.reset();
    queued_exports.clear();queued_solo.reset();deferred_test.reset();creation_actions.clear();complexity_request.reset();
    complexity_sample.reset();complexity_publication_failed=false;complexity_local_error.clear();
    editor_session::Callbacks callbacks;
    callbacks.command=receive_command;
    callbacks.lease_denied=[](const auto& scopes){waiting_history.reset();waiting_acts.reset();editor_native::deny_input(scopes);};
    callbacks.authoring_held=[] {return exporting.has_value() || native_exit_requested;};
    callbacks.trace=[](editor_session::TracePoint point,const editor::ProjectId& project,const editor::OperationId& operation,editor::Revision revision) noexcept {
        using P=editor_session::TracePoint;using S=editor_stage_metrics::Stage;
        const auto stage=point==P::HostRequestReceived?S::HostRequestReceived:point==P::HostDecisionQueued?S::HostDecisionQueued:S::DecisionDecoded;
        stage_stamp(project,operation,revision,stage);
    };
    callbacks.host_hooks.validate = [](const auto& before, const auto& after, const auto& change, std::string& error) {
        if(staged_capture && H->frame_index)canonical_work_frame=H->frame_index();
        StageScope span(after,change.operation,editor_stage_metrics::Stage::ValidateBegin,editor_stage_metrics::Stage::ValidateEnd);
        auto native = editor_native::state();
        if (closing || frozen || recovery || exporting || native_exit_requested || save_publication_pending ||
            (!detached_scene() && (native.generation!=native_generation || editor_native_save::busy() || !native.ready || native.gesture))) {
            error="The canonical draft is not currently editable";return false;
        }
         if(editor::extended_complexity(after)&&!complexity_authorized(true)){error=complexity_conflict();return false;}
        if(cast_change(change) && !admit_class_models(before,after,change,error))return false;
         if(editor::extended_complexity(after)&&!complexity_authorized(true)){error=complexity_conflict();return false;}
        span.okay=true;return true;
    };
    callbacks.host_hooks.apply = apply; callbacks.host_hooks.rollback = rollback; callbacks.host_hooks.persist = persist;
    callbacks.begin_persist=begin_persist;callbacks.poll_persist=poll_persist;callbacks.settle_persist=settle_persist;
    callbacks.native_prepare_enabled=[] {return pose_prepare_experiment;};
    callbacks.begin_native_prepare=begin_native_pose;callbacks.poll_native_prepare=poll_native_pose;callbacks.cancel_native_prepare=cancel_native_pose;
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
    callbacks.prepare_ready=prepare_capture_ready;
    callbacks.saved_ready=saved_capture_ready;
    callbacks.rendered_revision=[]()->std::optional<editor::Revision>{
        const auto native=editor_native::state();
        if(!complexity_authorized() || detached_scene() || durable_commit || (pending && pending->tentative) || !native.ready || native.gesture || native.generation!=native_generation)return {};
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
    callbacks.baseline_ready = [](const editor::State& state) {
        const auto native=editor_native::state();
        return (!editor::extended_complexity(state.document)||complexity_authorized(true)) && native.ready && !native.gesture && !editor_native_save::busy();
    };
    callbacks.apply_baseline = [](const editor::State& state, std::string& error) {
        if(editor::extended_complexity(state.document)&&!complexity_authorized(true)){error=complexity_conflict();return false;}
        ProjectionScope projection(state.document);
        reopen_saved.reset();
        if (!incoming_resources || incoming_context.project != state.document.project || incoming_context.epoch != state.document.epoch || incoming_context.revision != state.document.revision) {
            error = "The editor baseline has no matching verified resources"; return false;
        }
        editor_native::Snapshot native;
        if (!supported_act(incoming_resources->act, error) || !supported_native_act(error) ||
            !editor_project::native_snapshot(*incoming_resources, state.document, native, error)) return false;
        if (!editor_native::state().editing && !editor_native::enter(native.source, error)) return false;
        const auto alias_owner=pin_scene_alias_owner();
        if(!alias_owner){error="The joined scene could not pin its original native attachment";return false;}
        if(!scene_alias_owner_current(*alias_owner,error) || !register_native_records(incoming_resources->root,incoming_resources->bundle.records,error) ||
           !scene_alias_owner_current(*alias_owner,error))return false;
        // Baseline readiness proves the native entry finished. Verify the loaded
        // planet before restoring so an identical recipe keeps its existing
        // terrain and collision instead of scheduling a second regeneration.
        editor_native::Snapshot loaded;
        if (!editor_native::capture_draft(loaded, error)) return false;
        // restore installs and validates this exact document. A terrain rebuild
        // can still be loading after it returns; scene readiness is acknowledged
        // separately rather than applying the same document a second time.
        if (!scene_alias_owner_current(*alias_owner,error) || !editor_native::restore(native, error)) return false;
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
        queue_scene_alias_attachment(alias_owner);
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
        if (saved_capture_ready() && incoming_resources && incoming_context.revision == controller->state().document.revision &&
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
    native.projection_scope = [] { return current_projection_scope(); };
    native.complexity_current=[](const editor::ComplexityCapability& value){return complexity_current(value);};
    native.canonical_revision=[]()->std::optional<editor::Revision>{
        if(!current_projection_scope().epoch)return {};
        return controller->state().document.revision;
    };
    native.creation_return_current=[](const editor_native::GestureScope& scope,uint64_t serial){
        return local_creation && !local_creation->materialized && local_creation->scope.serial==serial &&
            local_creation->scope.project==scope.project && local_creation->scope.epoch==scope.epoch && local_creation->scope.author==scope.author &&
            creation_current(local_creation->scope);
    };
    native.import_scope = [] { return current_gesture_scope(true); };
    native.cancelled = cancelled_gesture;
    native.gesture = captured;
    native.capture_begin=capture_begin;native.capture_current=capture_current;
    native.capture_before=capture_before;native.capture_failed=capture_failed;
    native.history = [](bool redo) { action(redo ? Action::Redo : Action::Undo); };
    native.history_available = [](bool redo) { return !staged_capture && !rejection_restore && !complexity_held() && (redo ? cached.can_redo : cached.can_undo); };
    native.failure = [](const std::string& error) { if (!transport_teardown) { frozen = true; tell(error); } };
    native.notice = [](const std::string& message) { if (!transport_teardown) tell(message); };
    native.gate = [](const std::vector<editor::ObjectId>& selection) {
        if(staged_capture)return false;
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
        if(!controller || controller->view().phase!=Phase::Ready || !complexity_authorized() || detached_scene() || closing || exporting || pending_export_request || native_exit_requested || pending || awaiting_operation || durable_commit || frozen || recovery)return false;
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
        return controller && controller->view().phase==Phase::Ready && complexity_authorized() && !detached_scene() && !durable_commit && !closing && !recovery && !pending && !awaiting_operation &&
            !exporting && !pending_export_request && !native_exit_requested && !editor_native_save::busy() && !editor_native::state().gesture;
    };
    native.transaction_pending = [] { return bool(native_pose_prepare) || bool(staged_capture) || complexity_held() || rejection_restore.has_value() || pending.has_value() || awaiting_operation.has_value() || bool(durable_commit); };
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
    saving.preparing=[](const editor_native_save::SaveOperation& operation,std::string& error){
        if(!recovery&&!handoff&&!complexity_authorized()){error=complexity_conflict();return false;}
        auto* hold=export_before&&export_terrain?&*export_terrain:save_terrain?&*save_terrain:
            recovery&&recovery->terrain?&*recovery->terrain:nullptr;
        if(!hold){
            if(session_active()&&!handoff&&!beginning){error="The native Save has no current terrain transition receipt";return false;}
            return true;
        }
        if(!operation.module_generation || !operation.serial || operation.after!=editor::ResourceKey{} ||
           operation.owner!=hold->key.owner || operation.before!=hold->key.owner.source ||
           operation.native_generation!=hold->key.generation || !terrain_fence(*hold,false,recovery.has_value())()){
            error="The native Save does not match its held terrain transition";return false;
        }
        hold->save=operation;retire_scene_alias();return true;
    };
    auto completed_save=[](editor::ResourceKey key,const editor_native_save::SaveOperation* operation) {
        if(operation){
            auto* hold=export_before&&export_terrain?&*export_terrain:save_terrain?&*save_terrain:
                recovery&&recovery->terrain?&*recovery->terrain:nullptr;
            if(hold){
                auto prepared=*operation;prepared.after={};
                if(!hold->save || *hold->save!=prepared || operation->after!=key){
                    frozen=true;tell("The completed native Save does not match its terrain operation receipt");return;
                }
                hold->save=*operation;
            }
        }
        if(export_before){finish_export(true);return;}
        if(handoff && handoff_saving){handoff_saving=false;native_exit_requested=native_exit_confirmed=true;return;}
        if (transport_teardown || (native_attached && editor_native::state().generation != native_generation)) return;
        if (recovery && recovery->saving) { recovery->saved_key = key; return; }
        if (beginning) { start_saved(); return; }
        saved_native(key,operation);
        if(exit_after_save && !frozen)native_exit_requested=native_exit_confirmed=true;
    };
    saving.saved=[completed_save](editor::ResourceKey key){completed_save(key,nullptr);};
    saving.saved_operation=[completed_save](const editor_native_save::SaveOperation& operation){completed_save(operation.after,&operation);};
    saving.cancelled = [] { if(export_before)finish_export(false);else if(save_terrain){save_cancelling=true;save_publication_pending=true;}if(handoff)++handoff_prompt_serial;handoff_saving=false;exit_after_save=false; if (transport_teardown) return; beginning = false; if (recovery && recovery->saving) { recovery.reset(); frozen = true; } status = "Native save cancelled; the draft is preserved"; };
    saving.error = [](const std::string& error) { if(export_before)finish_export(false);else if(save_terrain){save_cancelling=true;save_publication_pending=true;}if(handoff)++handoff_prompt_serial;handoff_saving=false;exit_after_save=false; if (transport_teardown) return; beginning = false; if (recovery && recovery->saving) { recovery.reset(); frozen = true; } tell(error); };
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
    if(!editor_saved_native::init(H,M))H->log(M,SK_WARN,"Saved dependency restoration requires SporeKit 0.3.5 and verified loader symbols");
    editor_creation::Ctx creation;
    creation.scope=[]()->std::optional<editor_native::GestureScope>{
        // Retain interception while held, so prepare_creation can reject a
        // native New/Edit request instead of passing it through as unshared.
        if(staged_capture || !complexity_authorized()){const auto value=current_projection_scope();return value.epoch?std::optional(value):std::nullopt;}
        const auto value=current_gesture_scope(true);return value.epoch?std::optional(value):std::nullopt;
    };
    creation.prepare=prepare_creation;creation.current=creation_current;creation.lease_valid=creation_lease;creation.returned=return_creation;
    creation.notice=[](const std::string& value){tell(value);};
    creation.finished=[](const editor_creation::Scope& scope,bool okay,const std::string& reason){
        if(!local_creation || local_creation->scope!=scope || local_creation->return_failed)return;
        std::vector<editor::LeaseToken> tokens;
        for(const auto& item:controller->view().local_leases)if(item.scope.kind==editor::ScopeKind::Content &&
            local_creation->request.target && item.scope.object==*local_creation->request.target)tokens.push_back({item.scope,item.token});
        std::string error;if(!tokens.empty())controller->release_leases(tokens,error);
        if(local_creation->materialized)local_creation.reset();
        else if(!okay && !reason.empty()){fail_creation_return(reason);schedule();return;}
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
    editor_saved_native::shutdown();
    // Retire notifications before joining workers. Enqueued callbacks may run
    // after this runtime and its Controller have been replaced.
    if(durable_runtime_domain!=UINT64_MAX)++durable_runtime_domain;
    // Settle while the exact controller, creation scope and Store still exist.
    // Failure can require native rollback, so retire native adapters last.
    if(controller){controller->end();controller.reset();}
    cancel_native_pose();
    pending.reset();awaiting_operation.reset();waiting_acts.reset();rollback_fence.reset();before_apply_valid=false;
    save_terrain.reset();export_terrain.reset();close_terrain.reset();export_return_result.reset();save_cancelling=false;
    guarded_placements.clear();guarded_terrain.clear();requested_scopes.clear();lease_use.clear();
    editor_creation::shutdown();local_creation.reset();rejection_restore.reset();staged_capture.reset();retire_scene_alias();prepared_native.reset();rendered_revision=0;
    waiting_history.reset();authoring_projection_override.reset();test_menu.reset();guarded_settings.clear();guarded_authoring.clear();cast_uploads.clear();cast_upload_errors.clear();
    solo_preview.reset();solo_ready_scope.reset();pending_solo_return.reset();pending_solo_request=0;
    native_playtesting=return_started=disconnected_return=end_after_return=false;return_error.clear();settled_cancellation.reset();
    transport_teardown.reset();deferred_offer.reset();observed_transport=retired_transport=0;reopen_saved.reset();projection_override.reset();
    store.close();adventure_native::clear_editor_preview();editor_native_save::shutdown();editor_native::shutdown();actions.clear();creation_actions.clear();complexity_request.reset();queued_exports.clear();queued_solo.reset();deferred_test.reset();
    complexity_sample.reset();complexity_publication_failed=false;complexity_local_error.clear();
    pending_preview_return.reset();pending_preparation_cancel.reset();preview_selection.reset();observed_lease_grants.clear();
    queued=false;closing=native_attached=close_restored=close_preserve_offer=false;H=nullptr;
}
std::optional<editor_service_pulse::Scope> service_pulse_scope() {
    if (!H || !controller || !C.session || !C.session->connected() || !native_attached ||
        frozen || closing || recovery || transport_teardown || exporting || pending_export_request ||
        native_exit_requested || save_publication_pending || observed_transport <= retired_transport ||
        C.session->transport_generation()!=observed_transport) return std::nullopt;
    const auto& view=controller->view();const auto& doc=controller->state().document;
    if(view.phase!=Phase::Ready || view.context.project!=doc.project || view.context.epoch!=doc.epoch ||
        !view.local.generation || view.local.slot!=C.session->my_id())return std::nullopt;
    const auto member=view.members.find(view.local.slot);
    if(member==view.members.end() || member->second.peer!=view.local)return std::nullopt;
    editor_service_pulse::Scope result;
    result.words={durable_runtime_domain,observed_transport,uintptr_t(controller.get()),uintptr_t(C.session),
        doc.epoch,0,0,uint64_t(view.local.slot)|(uint64_t(view.local.generation)<<8),native_generation,
        current_native.source.group,current_native.source.instance,current_native.source.type,uint64_t(C.session->role())};
    for(size_t i=0;i<doc.project.size();++i)result.words[5+i/8]|=uint64_t(doc.project[i])<<((i%8)*8);
    return result;
}
bool service_pulse_idle(){return !runtime_draining && !editor_native::service_busy();}
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
    if(!controller || !complexity_authorized() || detached_scene() || value<0 || value>3 || !cached.can_save)return;
    waiting_acts=WaitingActs{TestMenu{current_gesture_scope(),native_generation,controller->state().document.revision,editor_native::state().act},value,now_ms()};schedule();
}
void create_asset(uint32_t type,uint32_t editor_id){if(!detached_scene() && creation_actions.size()<8){creation_actions.push_back({type,{},editor_id});schedule();}}
void edit_asset(editor::ObjectId class_id){if(!detached_scene() && creation_actions.size()<8){creation_actions.push_back({0,class_id});schedule();}}
void set_extended_complexity(bool value){complexity_request=value;schedule();}
void resolve_join(int choice) {action(choice==0?Action::HandoffSave:choice==1?Action::HandoffDiscard:Action::HandoffCancel);}
std::string debug(const std::vector<std::string>& args) {
    if(args.size()==2 && args[0]=="complexity" && (args[1]=="sample" || args[1]=="observation")){
        if(!controller || !native_attached || frozen || closing || recovery || durable_commit ||
           controller->view().phase!=Phase::Ready)return "complexity_observation state=unavailable error=No settled shared editor";
        const auto* pinned=controller.get();const auto scope=current_projection_scope();
        const auto revision=controller->state().document.revision,transport=observed_transport;
        const auto current=[&]{return controller.get()==pinned && C.session && C.session->connected() &&
            C.session->transport_generation()==transport && observed_transport==transport &&
            current_projection_scope()==scope && controller->state().document.revision==revision;};
        std::string error;
        if(args[1]=="sample"){
            const auto serial=editor_native::sample_complexity(scope,revision,error);
            if(!current())return "complexity_observation state=expired error=Shared context changed";
            return "complexity_observation state="+std::string(serial?"pending":"unavailable")+" serial="+std::to_string(serial)+
                " revision="+std::to_string(revision)+" error="+error;
        }
        editor_native::ComplexityObservation value;
        const auto state=editor_native::complexity_observation(scope,revision,value,error);
        if(!current())return "complexity_observation state=expired error=Shared context changed";
        static constexpr const char* labels[]={"disarmed","pending","ready","expired"};
        std::string result="complexity_observation state="+std::string(labels[size_t(state)])+" revision="+std::to_string(revision);
        if(state==editor_native::ComplexityObservationState::Ready){
            char fields[768];snprintf(fields,sizeof fields,
                " serial=%llu frame=%llu qpc=%llu generation=%llu markers=%u extended=%u weighted=%.9g native_marker=%.9g extended_marker=%.9g original=%.9g effective=%.9g weighted_headroom=%.9g effective_headroom=%.9g",
                (unsigned long long)value.serial,(unsigned long long)value.frame,(unsigned long long)value.qpc,(unsigned long long)value.generation,
                value.markers,unsigned(value.extended),double(value.weighted),double(value.native_marker),double(value.extended_marker),
                double(value.original),double(value.effective),double((value.extended?4.f:1.f)-value.weighted),double(1.f-value.effective));
            result+=fields;
            for(const auto& [name,number]:{std::pair{"weighted",value.weighted},std::pair{"native_marker",value.native_marker},
                std::pair{"extended_marker",value.extended_marker},std::pair{"original",value.original},std::pair{"effective",value.effective},
                std::pair{"weighted_headroom",(value.extended?4.f:1.f)-value.weighted},std::pair{"effective_headroom",1.f-value.effective}})
                result+=" "+std::string(name)+"_bits="+editor_placement_diagnostic::hex(std::bit_cast<uint32_t>(number),8);
            result+=" counts=";
            for(size_t i=0;i<value.counts.size();++i){if(i)result+=',';result+=std::to_string(value.counts[i]);}
            result+=" words=";
            for(size_t i=0;i<value.tuning.size();++i){if(i)result+=',';result+=editor_placement_diagnostic::hex(value.tuning[i],8);}
        }
        return result+" error="+error;
    }
    if(args.size()==1&&args[0]=="complexity"){
        if(!controller)return "complexity context_current=0 unavailable=no-controller";
        const auto& view=controller->view();const auto& doc=controller->state().document;
        const auto member=view.members.find(view.local.slot);
        const bool peer=member!=view.members.end()&&member->second.peer==view.local;
        const bool current=C.session&&C.session->connected()&&C.session->transport_generation()==observed_transport&&
            observed_transport>retired_transport&&view.local.generation&&view.local.slot==C.session->my_id()&&peer&&
            view.context.project==doc.project&&view.context.epoch==doc.epoch&&view.context.revision==doc.revision;
        const auto value=complexity_sample.value_or(editor::ComplexityCapability{});
        const auto claim=peer?member->second.complexity_serial:0;
        char prefix[512];snprintf(prefix,sizeof prefix,
            "complexity context_current=%u project=%s epoch=%llu peer=%u:%u transport=%llu ready=%u extended=%u policy=%08X formula=%08X available=%u reason=%u claim_serial=%llu words=",
            unsigned(current),editor_store::project_name(view.context.project).c_str(),static_cast<unsigned long long>(view.context.epoch),
            unsigned(view.local.slot),view.local.generation,static_cast<unsigned long long>(observed_transport),unsigned(current&&complexity_proven()),
            unsigned(editor::extended_complexity(doc)),value.policy,value.formula,unsigned(value.available),unsigned(value.reason),static_cast<unsigned long long>(claim));
        std::string result=prefix;
        for(size_t i=0;i<value.words.size();++i){if(i)result+=',';result+=editor_placement_diagnostic::hex(value.words[i],8);}
        auto error=complexity_proven()?std::string{}:complexity_conflict().substr(0,256);
        for(char& c:error)if(c=='\r'||c=='\n'||c=='\t')c=' ';
        return result+" error="+error;
    }
    if(args.size()==1 && args[0]=="creations")return editor_creation::diagnostic();
    if(!args.empty() && args[0]=="model"){
        const std::string usage="Use coop.editor model GROUP!INSTANCE.TYPE (eight hex digits each)";
        if(args.size()!=2 || args[1].size()!=26 || args[1][8]!='!' || args[1][17]!='.')return usage;
        auto hex=[](const std::string& text,uint32_t& value){
            value=0;for(const char c:text){unsigned digit;
                if(c>='0'&&c<='9')digit=unsigned(c-'0');else if(c>='a'&&c<='f')digit=unsigned(c-'a'+10);
                else if(c>='A'&&c<='F')digit=unsigned(c-'A'+10);else return false;
                value=(value<<4)|digit;
            }return true;
        };
        editor::ResourceKey key;
        if(!hex(args[1].substr(0,8),key.group) || !hex(args[1].substr(9,8),key.instance) || !hex(args[1].substr(18,8),key.type))return usage;
        if(!controller || !C.session)return "Model admission unavailable: no shared context";
        const auto scope=editor_native::GestureScope{controller->view().context.project,controller->view().context.epoch,controller->view().local};
        const auto fence=model_fence(scope,editor_native::state().generation,controller->state().document.revision,C.session->transport_generation());
        if(!fence.current(fence.scope,fence.generation))return "Model admission unavailable: shared context is not settled";
        editor_creation_native::ModelAdmission result;std::string error;
        const bool inspected=editor_creation_native::inspect_model(key,fence,result,error);
        return editor_creation_native::model_admission_status(result)+(inspected?"":" error="+error.substr(0,256));
    }
    if(!args.empty() && (args[0]=="assets" || args[0]=="asset")){
        if(!controller)return "Shared assets unavailable";
        editor_asset_diagnostic::Query query;
        if(!editor_asset_diagnostic::query(args,query))return "Use coop.editor assets [first count] (count 1..24), or coop.editor asset <class-id:generation>";
        const auto& doc=controller->state().document;std::string hash,error;
        if(!editor_session::document_hash(doc,hash,error))return "Shared assets unavailable: "+error;
        return editor_asset_diagnostic::format(doc,controller->view().locally_durable,hash,query);
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
        "\npose_prepare_experiment="+std::to_string(pose_prepare_experiment)+" native_pending="+std::to_string(bool(native_pose_prepare))+" pose_prepare_route="+pose_prepare_route+
        "\n" + editor_native::status() + "\n" + adventure_assets::status()+"\n"+editor_saved_native::status();
}
} // namespace coop_editor
