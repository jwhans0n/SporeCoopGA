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
editor::ProjectId project_id;
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
enum class Action { Begin, Undo, Redo, Save, Playtest, Ready, Unready, Recover, End, Return };
std::deque<Action> actions;
std::vector<editor::Scope> requested_scopes;

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
    if (!store.commit(saved, error)) {
        if (!store.healthy()) throw std::runtime_error("Editor disk acceptance is uncertain; reopen the recovery checkpoint: " + error);
        return false;
    }
    // All allocations and publication payloads were staged before durable I/O.
    current_native = std::move(after); resources = std::move(next_resources);
    history_artifacts.swap(archive);
    incoming_resources.reset(); gc_due = state.document.revision % 16 == 0;
    return true;
}
bool apply(const editor::Document& before, const editor::Document& after, const editor::Change& change, std::string& error) {
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
    if (!editor_native::apply(after, change, source, error)) return false;
    return true;
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
            if (placement.properties != next->second.properties) { frozen = true; tell("An unregistered native property edit was blocked. Recover the draft."); return; }
        }
    }
    for (const auto& [id, placement] : gesture.after.document.placements) if (!doc.placements.contains(id)) {
        editor::Mutation m; m.kind = editor::MutationKind::Create; m.initial = placement;
        m.initial.object = {}; m.initial.revision = 0; m.initial.field_revisions.clear();
        if (gesture.clone_source) {
            auto source = guarded_placements.find(*gesture.clone_source);
            if (source == guarded_placements.end()) { frozen = true; tell("The native clone source has no editing ownership. Recover the draft."); return; }
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
    if (!controller->submit(std::move(request), error)) { pending.reset(); awaiting_operation.reset(); tell(error); }
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
        ((next.host && native.ready && editor_native_save::available()) || (next.offered && game::mode_id() == modes::kMenu));
    next.revision = doc.revision; next.durable_revision = v.locally_durable;
    next.title = v.name.empty() ? "Shared Adventure Editor" : v.name;
    next.status = status.empty() ? v.error : status;
    next.selection = native.selected.empty() ? "No placement selected" : "Placement " + std::to_string(native.selected.front().id);
    next.frozen = frozen || v.phase == Phase::Frozen;
    next.waiting = closing || recovery.has_value() || awaiting_operation.has_value() || v.phase == Phase::Joining || v.phase == Phase::Syncing || v.phase == Phase::Preparing || v.phase == Phase::Returning;
    if (v.phase == Phase::Returning) next.status = "Waiting for every player to return to the committed draft";
    next.confirming = v.phase == Phase::Preparing;
    next.playtesting = v.phase == Phase::Playing;
    next.can_return = next.playtesting && next.host;
    bool editing = next.active && !closing && !recovery && v.phase == Phase::Ready && !next.frozen && !awaiting_operation && !native.gesture && !editor_native_save::busy();
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
        player.selection = member.presence.selected.id ? "Placement " + std::to_string(member.presence.selected.id) : "Browsing";
        player.status = member.phase == Phase::Ready ? "Editing" : member.phase == Phase::Syncing ? "Receiving draft" : member.phase == Phase::Playing ? "Playtesting" : member.phase == Phase::Returning ? "Returning to draft" : "Waiting";
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
    beginning = false;
    std::string error;
    editor_native::Snapshot native;
    if (!editor_native::capture(native, error) || !native.source.instance || native.source.type != 0x366A930D) { tell(error.empty() ? "Save a new native adventure draft before sharing" : error); return; }
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
    if (!controller || closing || controller->view().phase != Phase::Ready || awaiting_operation || frozen) return;
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
    if (!controller->submit(std::move(r), error)) { awaiting_operation.reset(); tell(error); }
}
void close_session(bool preserve_offer = false) {
    if (!closing) { closing = true; close_restored = false; close_preserve_offer = preserve_offer; }
    close_preserve_offer |= preserve_offer || controller->view().phase == Phase::Offered;
    // An accepted edit remains owned until active replicas have applied and
    // durably covered it. Keep pumping the controller and party transport while
    // authoring is held; reliable receipt alone does not prove application.
    if (host() && session_active() && !controller->settled()) {
        status = "Delivering the last committed edit before ending shared editing"; return;
    }
    const auto native = editor_native::state();
    // Keep native input/history guards installed until the owned editor has
    // settled. A different generation belongs to a different native context.
    if (editor_native_save::busy() || (native_attached && native.generation == native_generation && native.editing &&
        (!native.ready || native.gesture))) {
        status = "Finishing the native gesture or save before ending shared editing"; return;
    }
    if (native_attached && native.generation == native_generation && native.editing) {
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
    const bool keep_offer = close_preserve_offer;
    if (!keep_offer) controller->end(); pending.reset(); awaiting_operation.reset(); requested_scopes.clear(); guarded_placements.clear(); incoming_resources.reset();
    recovery.reset(); beginning = false; frozen = false; save_publication_pending = false;
    end_after_return = false; disconnected_return = false; pending_saved.reset();
    adventure_messages.clear(); adventure_message_bytes = 0;
    store.close(); closing = false; native_attached = close_restored = close_preserve_offer = false;
    tell(keep_offer ? "The previous playtest ended. Save your draft and return to Galaxy to join the new editing offer." :
        "Shared editing ended. The private native draft and durable recovery files are preserved.");
}
bool return_ready(const editor::wire::Control& control) {
    using K = editor::wire::ControlKind;
    if (control.kind != K::HostReturnEditor && control.kind != K::HostCancelPlaytest) return true;
    if (!adventure_native::editor_preview()) return editor_native::state().ready;
    if (adventure::active() || adventure::outcome_locked()) {
        adventure::cancel("The party is returning to the adventure draft");
        return false;
    }
    if (adventure::cleanup_pending()) return false;
    if (!return_started) {
        return_started = true;
        if (editor_native::state().playtesting && !editor_native_save::playtest(false, return_error)) return true;
    }
    return !return_error.empty() || editor_native::state().ready;
}
bool finish_return(std::string& error) {
    if (!return_error.empty()) { error = return_error; return false; }
    if (!editor_native::restore(current_native, error)) return false;
    adventure::reset();
    adventure_native::clear_editor_preview();
    native_playtesting = false; return_started = false; return_error.clear();
    native_generation = editor_native::state().generation;
    status = "Returned to the committed adventure draft";
    return true;
}
void finish_recovery() {
    if (closing || !recovery) return;
    const auto context = editor_native::state();
    if (!context.ready) return;
    if (!recovery->native_generation) recovery->native_generation = native_attached ? native_generation : context.generation;
    if (context.generation != recovery->native_generation) {
        recovery.reset(); frozen = true; tell("Recovery stopped because the native editor context changed"); return;
    }
    if (context.gesture || editor_native_save::busy()) return;
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
void execute(Action action) {
    if (closing && action != Action::End) return;
    std::string error;
    switch (action) {
    case Action::Begin:
        if (controller->view().phase == Phase::Offered) {
            if (game::mode_id() != modes::kMenu) { tell("Save your local draft and return to Galaxy before joining shared editing"); break; }
            if (!controller->accept_offer(error)) tell(error);
        }
        else if (host() && !session_active() && editor_native::state().editing && !beginning && (!C.adventure_busy || !C.adventure_busy())) {
            beginning = true;
            if (!editor_native_save::save_as(error)) { beginning = false; tell(error); }
            else status = "Save a new native draft to start shared editing";
        }
        break;
    case Action::Undo: undo_redo(false); break;
    case Action::Redo: undo_redo(true); break;
    case Action::Save:
        if (cached.can_save && check_native_integrity(error) && !(native_copy_owned ? editor_native_save::save(error) : editor_native_save::save_as(error))) tell(error);
        break;
    case Action::Playtest:
        if (controller->view().phase == Phase::Playing) { if (host() && !controller->return_editor(error)) tell(error); }
        else if (cached.can_playtest && !pending && !save_publication_pending && !editor_native_save::busy() &&
                 editor_native::state().ready && !editor_native::state().gesture && check_native_integrity(error) && !controller->prepare_playtest(error)) tell(error);
        break;
    case Action::Ready: case Action::Unready: if (!controller->ready_playtest(action == Action::Ready, error)) tell(error); break;
    case Action::End:
        if (adventure_native::editor_preview() || controller->view().phase == Phase::Preparing || controller->view().phase == Phase::Playing || controller->view().phase == Phase::Returning) {
            end_after_return = true;
            if (host() && controller->view().phase != Phase::Returning) { if (!controller->cancel_playtest(error)) tell(error); }
            else adventure::cancel("A collaborator ended the playtest");
        } else close_session();
        break;
    case Action::Return:
        if (host() && controller->view().phase == Phase::Playing) { if (!controller->return_editor(error)) tell(error); }
        else if (!host() && !disconnected_return) tell("The host returns the party to the adventure draft");
        break;
    case Action::Recover: {
        if (!host() || !recovery_available) break;
        if (!store.healthy() && !store.open(project_root, project_id, error)) { tell(error); break; }
        auto checkpoint = store.snapshot();
        editor::State recovered; editor_native::Snapshot native;
        editor::Peer recovered_capability; bool owned = false;
        if (!editor_project::load_snapshot(checkpoint, recovered, native, error) || !parse_manifest(checkpoint.manifest, resource_hash, history_artifacts, error, &recovered_capability, &owned)) { tell(error); break; }
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
        submit_pending_gesture();
        controller->tick(now_ms());
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
            if (now_ms() - last_presence >= 300) {
                last_presence = now_ms(); std::string error;
                editor::wire::Presence presence;
                if (!native.selected.empty()) presence.selected = native.selected.front();
                presence.act = {(3ull << 32) | native.act, 1}; presence.editing = native.gesture;
                controller->set_presence(presence, error);
            }
        }
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
void action(Action value) { if (actions.size() < 16) { actions.push_back(value); schedule(); } }
} // namespace

bool init(const SkHost* host_api, SkMod* mod, Ctx context) {
    H = host_api; M = mod; C = std::move(context);
    if (!C.session) return false;
    editor_session::Callbacks callbacks;
    callbacks.host_hooks.validate = [](const auto&, const auto&, const auto&, std::string& error) {
        auto native = editor_native::state();
        if (closing || frozen || recovery || native.generation != native_generation || editor_native_save::busy() || save_publication_pending || !native.ready || native.gesture) { error = "The native draft is not currently editable"; return false; } return true;
    };
    callbacks.host_hooks.apply = apply; callbacks.host_hooks.rollback = rollback; callbacks.host_hooks.persist = persist;
    callbacks.apply_committed = apply; callbacks.rollback_committed = rollback; callbacks.persist_guest = persist;
    callbacks.authoring_ready = [] {
        auto native = editor_native::state();
        return !closing && !frozen && !recovery && native.generation == native_generation && !save_publication_pending && !editor_native_save::busy() && native.ready && !native.gesture;
    };
    callbacks.apply_resources = [](const std::vector<uint8_t>& bytes, const editor::wire::Context& scope, std::string& error) {
        editor_project::Resources content;
        const bool baseline = controller->view().phase == Phase::Joining || controller->view().phase == Phase::Syncing;
        if (!editor_project::decode_resources(bytes, content, error, baseline ? nullptr : resources.base_root)) return false;
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
        if (!incoming_resources || incoming_context.project != state.document.project || incoming_context.epoch != state.document.epoch || incoming_context.revision != state.document.revision) {
            error = "The editor baseline has no matching verified resources"; return false;
        }
        editor_native::Snapshot native;
        if (!editor_project::native_snapshot(*incoming_resources, state.document, native, error) ||
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
            if (!check_native_integrity(error)) return false;
            const auto context = controller->view().context;
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
            preview.return_editor = [] { action(Action::Return); };
            return_started = false; return_error.clear();
            const auto authored_avatar = preview.authored_avatar;
            if (!adventure_native::set_editor_preview(std::move(preview), error)) return false;
            if (host() && !adventure::request_play(resources.root, authored_avatar)) {
                error = adventure::status(); adventure_native::clear_editor_preview(); return false;
            }
        }
        if (control.kind == K::HostReturnEditor || control.kind == K::HostCancelPlaytest) {
            if (!finish_return(error)) return false;
        }
        if (C.open_panel) C.open_panel();
        return true;
    };
    controller = std::make_unique<editor_session::Controller>(*C.session, std::move(callbacks));
    editor_native::Ctx native;
    native.active = [] { return session_active() || recovery.has_value() || closing || native_attached; };
    native.gesture = captured;
    native.history = [](bool redo) { action(redo ? Action::Redo : Action::Undo); };
    native.history_available = [](bool redo) { return redo ? cached.can_redo : cached.can_undo; };
    native.failure = [](const std::string& error) { frozen = true; tell(error); };
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
        if (recovery && recovery->saving) { recovery->saved_key = key; return; }
        if (beginning) { start_saved(); return; }
        saved_native(key);
    };
    saving.cancelled = [] { beginning = false; if (recovery && recovery->saving) { recovery.reset(); frozen = true; } status = "Native save cancelled; the draft is preserved"; };
    saving.error = [](const std::string& error) { beginning = false; if (recovery && recovery->saving) { recovery.reset(); frozen = true; } tell(error); };
    saving.owned_preview = adventure_native::editor_preview;
    saving.return_requested = [] { action(Action::Return); };
    editor_native_save::init(H, M, std::move(saving));
    refresh(); return available;
}
void tick(double) { editor_native::tick(); editor_native_save::tick(); schedule(); }
void on_message(uint8_t from, net::Reader& r) { if (controller) controller->on_message(from, r); schedule(); }
void on_adventure_message(uint8_t from, net::Reader& r) {
    const size_t size = r.left();
    if (!controller || !session_active()) { r.skip(size); return; }
    const auto& v = controller->view();
    if (v.phase != Phase::Preparing && v.phase != Phase::Playing && v.phase != Phase::Returning) { r.skip(size); return; }
    if (size > net::kMaxPacket || adventure_messages.size() >= 512 || size > (1u << 20) - adventure_message_bytes) {
        r.skip(size); frozen = true; tell("Editor playtest messages exceeded the bounded preparation queue"); action(Action::End); return;
    }
    AdventureMessage message{v.context, v.playtest_round, from, {r.cur(), r.cur() + size}};
    r.skip(size); adventure_message_bytes += size; adventure_messages.push_back(std::move(message)); schedule();
}
void on_join(uint8_t id) { if (controller) controller->on_join(id); schedule(); }
void on_leave(uint8_t id) { if (controller) controller->on_leave(id); schedule(); }
void session_ended() {
    if (!controller || closing) return;
    if (adventure_native::editor_preview()) { disconnected_return = true; schedule(); }
    else close_session();
    refresh();
}
void mode_changed(uint32_t, uint32_t) { schedule(); }
void shutdown() { if (controller) { controller->end(); controller.reset(); } store.close(); adventure_native::clear_editor_preview(); editor_native_save::shutdown(); editor_native::shutdown(); actions.clear(); queued = false; closing = native_attached = close_restored = close_preserve_offer = false; H = nullptr; }
bool active() { return cached.active; }
bool authoring() {
    if (!controller || (controller->view().phase == Phase::Playing && native_playtesting)) return false;
    return session_active() || native_attached || closing || recovery.has_value() || beginning;
}
bool playtest_owned() { return adventure_native::editor_preview(); }
View view() { return cached; }
void begin() { action(Action::Begin); }
void undo() { action(Action::Undo); }
void redo() { action(Action::Redo); }
void save() { action(Action::Save); }
void playtest() { action(Action::Playtest); }
void set_ready(bool value) { action(value ? Action::Ready : Action::Unready); }
void recover() { action(Action::Recover); }
void end() { action(Action::End); }
std::string debug(const std::vector<std::string>& args) {
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
            result += std::to_string(id.id) + ":" + std::to_string(id.generation) + " class=" + std::to_string(p.class_id.id) +
                " pos=" + std::to_string(p.position.x) + "," + std::to_string(p.position.y) + "," + std::to_string(p.position.z) +
                " scale=" + std::to_string(p.scale) + (projected ? " screen=" + std::to_string(xy[0]) + "," + std::to_string(xy[1]) : " screen=hidden") + "\n";
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
        else if (args[0] == "ready") set_ready(args.size() < 2 || args[1] != "0");
        else if (args[0] == "recover") recover();
        else if (args[0] == "end") end();
        else if (args[0] == "return") action(Action::Return);
    }
    refresh();
    return cached.title + "\n" + cached.status + "\nrevision=" + std::to_string(cached.revision) +
        " durable=" + std::to_string(cached.durable_revision) + " frozen=" + std::to_string(cached.frozen) +
        " phase=" + std::to_string(controller ? uint8_t(controller->view().phase) : 0) +
        " project=" + (controller ? editor_store::project_name(controller->view().context.project) : "") +
        " capability=" + std::to_string(controller ? controller->view().local.slot : 255) + ":" + std::to_string(controller ? controller->view().local.generation : 0) +
        "\n" + editor_native::status();
}
} // namespace coop_editor
