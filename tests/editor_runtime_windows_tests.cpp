// Real runtime orchestration, UDP controller and durable store; native-only APIs
// are isolated in memory. No game process, profile or installed file is touched.
#include <windows.h>
#include <winioctl.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "editor_runtime.h"
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
#include "../updater/sha256.h"

// Two independent process-global runtime instances, with the production source
// unchanged. Native callbacks select the fixture's current simulated process.
namespace runtime_host { using coop_editor::Ctx; using coop_editor::View; using coop_editor::PlayerView; }
#define coop_editor runtime_host
#include "../mods/coop/editor_runtime.cpp"
#undef coop_editor
namespace runtime_guest { using coop_editor::Ctx; using coop_editor::View; using coop_editor::PlayerView; }
#define coop_editor runtime_guest
#include "../mods/coop/editor_runtime.cpp"
#undef coop_editor

namespace {
namespace fs = std::filesystem;
namespace es = editor_session;
namespace ew = editor::wire;
unsigned checks;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { std::printf("FAIL %d: %s\n", __LINE__, #__VA_ARGS__); std::exit(1); } } while (0)
int side;
bool dispatching;
uint64_t clock_ms;
uint64_t game_frame;
size_t dependency_bytes;
struct Task { SkTaskFn fn; void* user; };
std::array<std::deque<Task>,2> tasks;
std::array<net::Session,2> sessions;
constexpr dbpf::Key original_key{100,0x366A930D,0x408A0000},saved_key{101,0x366A930D,0x408A0000};
constexpr editor::ResourceKey avatar_key{9,0x00B1B104,0x408A0000};
constexpr editor::ObjectId placement{(4ull<<32)|1,1},class_id{(2ull<<32)|1,1},act_id{3ull<<32,1};
struct Native {
    editor_native::State state;
    editor_native::Snapshot snapshot;
    editor_native::Ctx callbacks;
    editor_native_save::Ctx save_callbacks;
    std::optional<adventure_native::EditorPreview> preview;
    bool active=false,cleanup=false,return_ready=true,save_busy=false,rebaseline_ready=true;
    unsigned cancels=0,stops=0,restores=0,ga_received=0,ga_delivered=0;
    unsigned save_as_calls=0,save_calls=0;
    unsigned rebaselines=0;
    unsigned structural_applies=0;uint64_t structural_frame=0;
    std::optional<std::vector<uint8_t>> persisted_bytes;
};
std::array<Native,2> native;
editor::Document seed() {
    editor::Document d;d.project[0]=1;d.epoch=7;d.next_id=5ull<<32;
    d.classes.insert(class_id);d.acts.insert(act_id);
    editor::Placement p;p.object=placement;p.class_id=class_id;p.act_id=act_id;d.placements.emplace(p.object,p);
    return d;
}
std::vector<uint8_t> body(const editor::Document& d,uint8_t opaque=0xA5) {
    editor::State state;state.document=d;std::vector<uint8_t> bytes;std::string error;
    CHECK(ew::encode_state(state,bytes,error));bytes.insert(bytes.begin(),opaque);return bytes;
}
void bind_native(Native& n) {
    n.snapshot.bindings.clear();uint32_t marker=0;
    for (const auto& [id,p]:n.snapshot.document.placements) { (void)p;n.snapshot.bindings.push_back({id,marker++}); }
}
void no_log(SkMod*,int,const char*,...) {}
uintptr_t no_sym(const char*) { return 0; }
double now_seconds() { return double(clock_ms)/1000; }
uint64_t frame_index() { return game_frame; }
void outside(SkTaskFn fn,void* user) { tasks[side].push_back({fn,user}); }
void run_tasks(unsigned p) {
    side=int(p);auto work=std::move(tasks[p]);tasks[p].clear();
    for (auto task:work) task.fn(task.user);
}
void network() {
    dispatching=true;
    for (unsigned p=0;p<2;++p) { side=int(p);sessions[p].update(); }
    dispatching=false;
}
void step(bool guest=true) {
    clock_ms+=5;++game_frame;network();side=0;runtime_host::tick(now_seconds());
    side=1;runtime_guest::tick(now_seconds());run_tasks(0);if (guest) run_tasks(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
template<class F> bool spin(F predicate,bool guest=true,unsigned limit=5000) {
    const auto begun=net::now_ms();
    while (uint32_t(net::now_ms()-begun)<limit) { step(guest);if (predicate()) return true; }
    std::printf("host phase%u: %s; guest phase%u: %s\n",unsigned(runtime_host::controller->view().phase),runtime_host::status.c_str(),
        unsigned(runtime_guest::controller->view().phase),runtime_guest::status.c_str());return false;
}
void complete_cleanup(unsigned p) {
    side=int(p);CHECK(native[p].cleanup);native[p].cleanup=false;
    if (native[p].preview) {auto notify=native[p].preview->return_editor;if(notify)notify();}
}
struct Party {
    fs::path directory;
    Party(size_t immutable_bytes=0,uint8_t initial_saved_opaque=0xA5) {
        dependency_bytes=immutable_bytes;
        clock_ms=10000;game_frame=1;dispatching=false;
        directory=fs::temp_directory_path()/("sporecoop-editor-runtime-tests-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(net::now_ms()));
        CHECK(!fs::exists(directory));CHECK(fs::create_directories(directory));
        for(unsigned p=0;p<2;++p) {
            side=int(p);native[p]={};auto& n=native[p];
            n.state.available=true;n.state.editing=p==0;n.state.ready=p==0;n.state.generation=1;
            n.snapshot.source={original_key.instance,original_key.type,original_key.group};n.snapshot.captain=avatar_key;
            n.snapshot.document=seed();n.snapshot.bytes=body(n.snapshot.document);bind_native(n);
            if(!p)n.persisted_bytes=body(n.snapshot.document,initial_saved_opaque);
            sessions[p].cb.custom=[p](uint8_t type,uint8_t from,net::Reader& r) {
                side=int(p);
                if(type==net::MSG_EDITOR) { if(p)runtime_guest::on_message(from,r);else runtime_host::on_message(from,r); }
                else if(type==net::MSG_ADVENTURE) { ++native[p].ga_received;if(p)runtime_guest::on_adventure_message(from,r);else runtime_host::on_adventure_message(from,r); }
            };
            sessions[p].cb.player_join=[p](const net::PlayerInfo& who) {side=int(p);if(p)runtime_guest::on_join(who.id);else runtime_host::on_join(who.id);};
            sessions[p].cb.player_leave=[p](uint8_t who,uint8_t) {side=int(p);if(p)runtime_guest::on_leave(who);else runtime_host::on_leave(who);};
            static SkHost host{};host.sym=no_sym;host.log=no_log;host.time_seconds=now_seconds;host.frame_index=frame_index;host.run_outside_frame=outside;
            coop_editor::Ctx ctx;ctx.session=&sessions[p];ctx.adventure_busy=[] {return false;};
            if(p) {CHECK(runtime_guest::init(&host,nullptr,ctx));runtime_guest::project_root=directory/"guest";}
            else {CHECK(runtime_host::init(&host,nullptr,ctx));runtime_host::project_root=directory/"host";}
        }
        std::string error;CHECK(sessions[0].host(0,"Host",error));
        CHECK(sessions[1].join("127.0.0.1:"+std::to_string(sessions[0].port()),"Guest",error));
        CHECK(spin([] {return sessions[0].players().size()==2&&sessions[1].connected();}));
        side=0;runtime_host::start_saved();CHECK(!runtime_host::frozen);
        native[0].persisted_bytes.reset();
        CHECK(spin([] {return runtime_guest::controller->view().phase==es::Phase::Offered;}));
        side=1;CHECK(runtime_guest::controller->accept_offer(error));
        CHECK(spin([] {return runtime_guest::controller->view().phase==es::Phase::Ready&&
            runtime_host::controller->view().members.at(1).phase==es::Phase::Ready&&!runtime_host::save_publication_pending;},true,90000));
        CHECK(runtime_host::store.healthy()&&runtime_guest::store.healthy());
    }
    ~Party() {
        for(unsigned p=0;p<2;++p) {side=int(p);if(p)runtime_guest::shutdown();else runtime_host::shutdown();tasks[p].clear();sessions[p].leave();}
        // Every deletion is confined to this fixture's freshly created temp dir.
        const auto parent=fs::weakly_canonical(fs::temp_directory_path());const auto resolved=fs::weakly_canonical(directory);
        CHECK(resolved.parent_path()==parent&&resolved.filename().string().starts_with("sporecoop-editor-runtime-tests-"));fs::remove_all(resolved);
    }
};
}

namespace game {uint32_t mode_id(){return native[side].state.editing||native[side].state.playtesting?modes::kScenario:modes::kMenu;}}
namespace editor_native {
bool init(const SkHost*,SkMod*,Ctx ctx){native[side].callbacks=std::move(ctx);return true;}
void shutdown(){}void tick(){}State state(){return native[side].state;}
editor::ResourceKey avatar(){return native[side].snapshot.captain;}bool avatar_locked(){return true;}
bool capture(Snapshot& value,std::string& error){CHECK(!dispatching);if(!native[side].state.ready){error="Injected native not ready";return false;}value=native[side].snapshot;return true;}
bool validate(const Snapshot& snapshot,const editor::Document& wanted,std::string& error){
    CHECK(!dispatching);editor::State parsed;
    if(snapshot.bytes.empty()||!ew::decode_state(std::vector<uint8_t>(snapshot.bytes.begin()+1,snapshot.bytes.end()),parsed,error))return false;
    const auto& d=parsed.document;
    if(d.classes!=wanted.classes||d.acts!=wanted.acts||d.placements.size()!=wanted.placements.size()){error="Injected persisted projection mismatch";return false;}
    for(const auto& [id,p]:d.placements){auto it=wanted.placements.find(id);if(it==wanted.placements.end()||p.class_id!=it->second.class_id||p.act_id!=it->second.act_id||p.position!=it->second.position||p.rotation!=it->second.rotation||p.scale!=it->second.scale){error="Injected persisted field mismatch";return false;}}
    return true;
}
bool supported_changes(const Gesture& gesture,std::string& error){CHECK(!dispatching);
    if(gesture.before.bytes.empty()||gesture.after.bytes.empty()||gesture.before.bytes.front()!=gesture.after.bytes.front()){
        error="Injected unsupported opaque authoring change";return false;
    }return true;
}
bool restore(const Snapshot& value,std::string&){CHECK(!dispatching);CHECK(native[side].state.ready);native[side].snapshot=value;native[side].snapshot.captain=avatar_key;++native[side].restores;return true;}
bool rebaseline(std::string& error){CHECK(!dispatching);auto& n=native[side];if(!n.rebaseline_ready||n.state.gesture){error="Injected native capture pending";return false;}
    CHECK(n.state.ready);++n.rebaselines;++n.state.generation;n.state.history_cursor=n.state.history_size=0;return true;}
bool apply(const editor::Document& doc,std::string& error){return apply(doc,editor::Change{},nullptr,error);}
bool apply(const editor::Document& doc,const editor::Change& change,const Snapshot* artifact,std::string&){CHECK(!dispatching);auto& n=native[side];if(!change.patch.objects.empty()){++n.structural_applies;n.structural_frame=game_frame;}n.snapshot.document=doc;n.snapshot.bytes=body(doc,artifact&&!artifact->bytes.empty()?artifact->bytes.front():n.snapshot.bytes.empty()?0xA5:n.snapshot.bytes.front());bind_native(n);return true;}
bool enter(const editor::ResourceKey& key,std::string&){auto& n=native[side];n.state.editing=n.state.ready=true;n.snapshot.source=key;return true;}
std::string status(){return "fake native";}
}
namespace editor_native_save {
bool init(const SkHost*,SkMod*,Ctx ctx){native[side].save_callbacks=std::move(ctx);return true;}
void shutdown(){}void tick(){}bool available(){return true;}bool busy(){return native[side].save_busy;}
bool save_as(std::string&){++native[side].save_as_calls;native[side].save_busy=true;return true;}
bool save(std::string&){++native[side].save_calls;native[side].save_busy=true;return true;}
bool read_saved(const editor::ResourceKey&,std::vector<uint8_t>& bytes,std::string&){CHECK(!dispatching);bytes=native[side].persisted_bytes.value_or(native[side].snapshot.bytes);return true;}
bool playtest(bool on,std::string&){CHECK(!dispatching);auto& n=native[side];if(on){n.state.playtesting=true;n.state.editing=n.state.ready=false;}
    else{CHECK(!n.cleanup);++n.stops;n.state.playtesting=false;n.state.editing=true;n.state.ready=n.return_ready;}return true;}
}
namespace adventure_assets {bool register_records(const std::vector<dbpf::Record>&,std::string&){CHECK(!dispatching);return true;}}
namespace adventure_collect {
Presentation presentation(const dbpf::Key&){return {"Native draft","Fixture",{}};}
bool collect_snapshot(const dbpf::Key& key,const std::vector<uint8_t>& bytes,std::vector<dbpf::Record>& records,std::string&){records={{key,bytes}};
    size_t left=dependency_bytes;uint32_t id=500;
    while(left){size_t size=std::min<size_t>(left,12u<<20);records.push_back({{id++,0x12345678,0x408A0000},std::vector<uint8_t>(size,uint8_t(id))});left-=size;}
    return true;}
}
namespace adventure_native {
bool set_editor_preview(EditorPreview value,std::string&){CHECK(!dispatching);CHECK(!native[side].preview);native[side].preview=std::move(value);return true;}
bool editor_preview(){return native[side].preview.has_value();}void clear_editor_preview(){native[side].preview.reset();}
}
namespace adventure {
bool active(){return native[side].active;}bool outcome_locked(){return false;}bool cleanup_pending(){return native[side].cleanup;}
bool cancel(const std::string&){auto& n=native[side];if(!n.active)return false;n.active=false;n.cleanup=true;++n.cancels;return true;}
void reset(){CHECK(!native[side].cleanup);native[side].active=false;}
std::string status(){return "fake GA";}
bool request_play(const dbpf::Key&,const dbpf::Key&){CHECK(!dispatching);native[side].active=true;
    outside([](void*){std::string error;CHECK(native[side].preview);CHECK(native[side].preview->launch(error));
        net::Writer w;w.u8(77);CHECK(sessions[0].send_reliable_to(1,net::MSG_ADVENTURE,w));},nullptr);return true;}
void on_message(uint8_t,net::Reader& r){CHECK(!dispatching);CHECK(native[side].preview);CHECK(r.u8()==77);CHECK(!r.left());
    ++native[side].ga_delivered;native[side].active=true;std::string error;CHECK(native[side].preview->launch(error));}
}

static void test_begin_ga_batch_and_end_returning() {
    Party party;std::string error;
    side=0;CHECK(runtime_host::controller->prepare_playtest(error));
    CHECK(spin([] {return runtime_host::controller->view().phase==es::Phase::Preparing&&runtime_guest::controller->view().phase==es::Phase::Preparing;}));
    side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));runtime_guest::controller->tick(clock_ms);
    side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
    CHECK(spin([] {return native[1].ga_received==1;},false));
    CHECK(runtime_guest::controller->view().phase==es::Phase::Preparing);CHECK(!native[1].preview&&native[1].ga_delivered==0);
    run_tasks(1);CHECK(native[1].preview&&native[1].ga_delivered==1);CHECK(runtime_guest::controller->view().phase==es::Phase::Playing);
    side=0;CHECK(runtime_host::controller->return_editor(error));
    CHECK(spin([] {return native[0].cleanup;},false));CHECK(native[0].stops==0);complete_cleanup(0);
    CHECK(spin([] {return runtime_host::controller->view().phase==es::Phase::Returning&&!native[0].preview;},false));
    CHECK(native[0].stops==1);side=0;runtime_host::end();step(false);
    CHECK(runtime_host::controller->view().phase==es::Phase::Returning);CHECK(runtime_host::end_after_return);
    CHECK(spin([] {return native[1].cleanup;}));CHECK(native[1].stops==0);native[1].return_ready=false;complete_cleanup(1);
    CHECK(spin([] {return native[1].stops==1;}));for(unsigned i=0;i<4;++i)step();CHECK(native[1].stops==1);
    CHECK(runtime_host::controller->view().phase==es::Phase::Returning&&native[1].preview);native[1].state.ready=true;
    CHECK(spin([] {return runtime_host::controller->view().phase==es::Phase::Idle&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    CHECK(native[0].stops==1&&native[1].stops==1&&!native[0].preview&&!native[1].preview);
    CHECK(sessions[0].connected()&&sessions[1].connected());
}
static void test_saved_resources_then_next_commit() {
    Party party;std::string error;
    side=1;CHECK(runtime_guest::controller->request_leases({{editor::ScopeKind::Placement,placement,{}}},15000,error));
    CHECK(spin([] {return !runtime_guest::controller->view().local_leases.empty();}));
    editor::Request request;request.base_revision=runtime_guest::controller->state().document.revision;request.summary="Transform immediately after native Save";
    for(const auto& lease:runtime_guest::controller->view().local_leases)request.leases.push_back({lease.scope,lease.token});
    editor::Mutation m;m.target=placement;m.field={editor::Field::Position,0};m.value=editor::Vec3{9,8,7};m.expected=0;request.mutations={m};
    side=1;CHECK(runtime_guest::controller->submit(request,error));runtime_guest::controller->tick(clock_ms);sessions[1].update();
    side=0;native[0].snapshot.source={saved_key.instance,saved_key.type,saved_key.group};native[0].snapshot.bytes=body(native[0].snapshot.document,0x5A);
    runtime_host::saved_native(native[0].snapshot.source);CHECK(runtime_host::save_publication_pending&&!runtime_host::frozen);
    CHECK(spin([] {return runtime_host::controller->state().document.revision==1;},false));
    CHECK(!runtime_host::save_publication_pending&&runtime_host::resources.root==saved_key);
    // Hold native dispatch while UDP advances so Resources, Saved and Decision
    // are consumed by the guest controller in one outside-frame turn.
    for(unsigned i=0;i<12;++i)step(false);CHECK(runtime_guest::controller->state().document.revision==0);
    CHECK(spin([] {return runtime_guest::controller->state().document.revision==1;}));
    CHECK(!runtime_guest::frozen&&runtime_guest::resources.root==saved_key&&!runtime_guest::pending_saved);
    CHECK(runtime_guest::current_native.source==editor::ResourceKey{saved_key.instance,saved_key.type,saved_key.group});
    CHECK(runtime_guest::current_native.bytes.front()==0x5A);CHECK(runtime_guest::store.durable_revision()==1);
    CHECK(runtime_guest::controller->state().document.placements.at(placement).position==editor::Vec3{9,8,7});
    const auto root=runtime_guest::project_root;const auto project=runtime_guest::store.snapshot().project;
    runtime_guest::store.close();editor_store::Store reopened;CHECK(reopened.open(root,project,error));
    editor::State state;editor_native::Snapshot snapshot;CHECK(editor_project::load_snapshot(reopened.snapshot(),state,snapshot,error));
    CHECK(state.document.revision==1&&snapshot.source==editor::ResourceKey{saved_key.instance,saved_key.type,saved_key.group});
    CHECK(snapshot.bytes==runtime_guest::current_native.bytes);reopened.close();
}
static void test_guest_recovery_requires_owned_native_copy() {
    Party party;std::string error;side=1;
    runtime_guest::close_session();sessions[1].leave();CHECK(sessions[1].host(0,"Recovered guest",error));
    const auto old_source=native[1].snapshot.source;
    runtime_guest::recover();CHECK(spin([] {return native[1].save_as_calls==1;}));
    CHECK(runtime_guest::controller->view().phase==es::Phase::Idle&&runtime_guest::authoring());CHECK(native[1].snapshot.source==old_source);
    CHECK(native[1].save_calls==0);for(unsigned i=0;i<5;++i)step();CHECK(native[1].save_as_calls==1);
    // Only the independently completed native Save As callback establishes the
    // new key. Recovery cannot start hosting or replace the imported source.
    side=1;const editor::ResourceKey copied{202,original_key.type,original_key.group};native[1].snapshot.source=copied;
    native[1].persisted_bytes=body(native[1].snapshot.document,0xE1);
    native[1].save_busy=false;native[1].rebaseline_ready=false;native[1].save_callbacks.saved(copied);
    CHECK(spin([]{return runtime_guest::recovery&&runtime_guest::recovery->native.source.instance==202&&!runtime_guest::recovery->saved_key;}));
    CHECK(runtime_guest::controller->view().phase==es::Phase::Idle&&!runtime_guest::recovery->needs_copy);
    const auto saved_restores=native[1].restores;
    for(unsigned i=0;i<5;++i)step();CHECK(native[1].restores==saved_restores&&native[1].save_as_calls==1);
    native[1].rebaseline_ready=true;
    CHECK(spin([] {return runtime_guest::controller->view().phase==es::Phase::Ready;}));
    CHECK(runtime_guest::resources.root==dbpf::Key{copied.instance,copied.type,copied.group});
    CHECK(runtime_guest::current_native.source==copied&&runtime_guest::store.healthy());
    CHECK(runtime_guest::current_native.bytes.front()==0xE1&&native[1].snapshot.bytes.front()==0xE1);
    CHECK(native[1].save_as_calls==1&&native[1].save_calls==0);CHECK(old_source!=copied);
}
static void test_pending_undo_keeps_ownership_across_foreign_commit() {
    Party party;std::string error;
    const editor::Scope position_scope{editor::ScopeKind::Field,placement,{editor::Field::Position,0}};
    side=1;CHECK(runtime_guest::controller->request_leases({position_scope},15000,error));
    CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
    runtime_guest::guarded_placements[placement]=runtime_guest::controller->state().document.placements.at(placement);
    editor_native::Gesture gesture;gesture.generation=native[1].state.generation;gesture.before=native[1].snapshot;gesture.after=gesture.before;
    gesture.after.document.placements.at(placement).position={3,2,1};gesture.after.bytes=body(gesture.after.document);
    native[1].snapshot=gesture.after;side=1;runtime_guest::captured(std::move(gesture));
    CHECK(runtime_guest::awaiting_operation&&runtime_guest::pending);
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&!runtime_guest::awaiting_operation;}));
    CHECK(spin([]{return runtime_guest::controller->view().local_leases.empty();}));
    side=1;CHECK(runtime_guest::controller->request_leases({position_scope},15000,error));
    CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
    const editor::Scope scale_scope{editor::ScopeKind::Field,placement,{editor::Field::Scale,0}};
    side=0;CHECK(runtime_host::controller->request_leases({scale_scope},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    side=1;runtime_guest::undo_redo(false);CHECK(runtime_guest::awaiting_operation&&!runtime_guest::pending);
    auto awaiting=*runtime_guest::awaiting_operation;auto serial=runtime_guest::sequence;
    runtime_guest::undo_redo(false);CHECK(runtime_guest::sequence==serial&&runtime_guest::awaiting_operation==awaiting);
    CHECK(!runtime_guest::lease({position_scope}));
    editor::Request request;request.summary="Foreign scale during local pending Undo";request.base_revision=1;
    for(const auto& held:runtime_host::controller->view().local_leases)request.leases.push_back({held.scope,held.token});
    editor::Mutation mutation;mutation.target=placement;mutation.field={editor::Field::Scale,0};mutation.value=2.f;request.mutations={mutation};
    side=0;CHECK(runtime_host::controller->submit(request,error));
    CHECK(spin([]{return runtime_host::controller->state().document.revision==2;},false));
    for(unsigned i=0;i<12;++i)step(false);
    CHECK(runtime_guest::controller->state().document.revision==1);run_tasks(1);
    CHECK(runtime_guest::controller->state().document.revision==2);
    CHECK(runtime_guest::awaiting_operation==awaiting&&!runtime_guest::controller->view().local_leases.empty());
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==3&&!runtime_guest::awaiting_operation;}));
    CHECK(runtime_guest::controller->state().document.placements.at(placement).position==editor::Vec3{});
    CHECK(runtime_guest::controller->state().document.placements.at(placement).scale==2.f);
    CHECK(spin([]{return runtime_guest::controller->view().local_leases.empty();}));
}
static uint64_t sent(unsigned p) {
    auto text=sessions[p].status();auto pos=text.find("bytes_sent=");CHECK(pos!=std::string::npos);
    return std::stoull(text.substr(pos+11));
}
static uint64_t object_bytes(const fs::path& directory) {
    uint64_t bytes=0;for(const auto& entry:fs::directory_iterator(directory/"objects"))bytes+=entry.file_size();return bytes;
}
static void test_large_closure_retained_once_and_delta_recovery() {
    Party party(24u<<20);std::string error;
    const auto base_hash=runtime_host::resource_hash,guest_hash=runtime_guest::resource_hash;
    CHECK(runtime_host::resources.base_root&&runtime_guest::resources.base_root);
    const auto native_base_hash=upd::sha256_hex(*runtime_host::resources.base_root);
    CHECK(upd::sha256_hex(*runtime_guest::resources.base_root)==native_base_hash);
    const auto initial=object_bytes(runtime_host::project_directory);
    CHECK(initial>(24u<<20));CHECK(runtime_host::resources.bundle.records.size()==3);
    CHECK(spin([]{return sessions[0].pending_to(1)==0&&sessions[1].pending_to(0)==0;},true,30000));
    const auto before_sent=sent(0),before_guest_sent=sent(1);
    side=0;CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Document,{},{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    for(unsigned edit=0;edit<20;++edit) {
        editor::Request request;request.summary="Create with unchanged large dependencies";request.base_revision=edit;
        for(const auto& held:runtime_host::controller->view().local_leases)request.leases.push_back({held.scope,held.token});
        editor::Mutation mutation;mutation.kind=editor::MutationKind::Create;mutation.initial.class_id=class_id;mutation.initial.act_id=act_id;
        mutation.initial.position={float(edit+1),0,0};request.mutations={mutation};
        side=0;CHECK(runtime_host::controller->submit(request,error));
        CHECK(spin([&]{return runtime_guest::controller->state().document.revision==edit+1;},true,30000));
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);CHECK(runtime_host::resource_hash==base_hash&&runtime_guest::resource_hash==guest_hash);
        CHECK(runtime_guest::resources.bundle.records.size()==3);
        CHECK(upd::sha256_hex(*runtime_host::resources.base_root)==native_base_hash&&upd::sha256_hex(*runtime_guest::resources.base_root)==native_base_hash);
    }
    side=0;native[0].snapshot.source={saved_key.instance,saved_key.type,saved_key.group};native[0].snapshot.bytes=body(native[0].snapshot.document,0x6C);
    runtime_host::saved_native(native[0].snapshot.source);CHECK(!runtime_host::frozen);
    CHECK(spin([]{return runtime_guest::resources.root==saved_key&&!runtime_host::save_publication_pending&&!runtime_guest::pending_saved;},true,30000));
    CHECK(runtime_host::resource_hash==base_hash&&runtime_guest::resource_hash==guest_hash);
    CHECK(object_bytes(runtime_host::project_directory)<initial+(2u<<20));
    CHECK(object_bytes(runtime_guest::project_directory)<initial+(2u<<20));
    CHECK(spin([]{return sessions[0].pending_to(1)==0&&sessions[1].pending_to(0)==0;},true,30000));
    const auto host_bytes=sent(0)-before_sent,guest_bytes=sent(1)-before_guest_sent;
    CHECK(host_bytes<(2u<<20)&&guest_bytes<(1u<<20));
    std::printf("runtime large closure: initial=%llu CAS=%llu host_bytes=%llu guest_bytes=%llu (20 structural edits + Save)\n",
        static_cast<unsigned long long>(initial),static_cast<unsigned long long>(object_bytes(runtime_host::project_directory)),
        static_cast<unsigned long long>(host_bytes),static_cast<unsigned long long>(guest_bytes));
    auto expected=runtime_host::current_native;auto bindings=expected.bindings;
    side=0;runtime_host::close_session();CHECK(spin([]{return !runtime_host::closing&&runtime_host::controller->view().phase==es::Phase::Idle;}));
    native[0].snapshot.bytes=body(seed(),0x11);native[0].snapshot.bindings.clear();
    runtime_host::recover();CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready&&!runtime_host::recovery;},true,30000));
    CHECK(runtime_host::current_native.source==expected.source&&runtime_host::current_native.bytes==expected.bytes);
    CHECK(runtime_host::current_native.bindings==bindings&&runtime_host::resources.bundle.records.size()==3);
    CHECK(runtime_host::resources.root==saved_key&&runtime_host::resource_hash==base_hash);
    CHECK(upd::sha256_hex(*runtime_host::resources.base_root)==native_base_hash);
    for(const auto& record:runtime_host::resources.bundle.records)if(record.key==saved_key)CHECK(record.data==expected.bytes);
}
static void test_pending_artifacts_obey_capacity_and_preserve_durable_base() {
    Party party;std::string error;side=0;
    std::vector<uint8_t> base;CHECK(runtime_host::read_blob(runtime_host::resource_hash,base,ew::kResourceBytes,error));
    uint64_t used=0;CHECK(runtime_host::artifact_usage(used,error));
    const auto pending_hash=upd::sha256_hex(std::vector<uint8_t>{7,9,11});
    const auto path=runtime_host::project_directory/"objects"/(pending_hash+".pending");
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    CHECK(file!=INVALID_HANDLE_VALUE);DWORD returned=0;
    CHECK(DeviceIoControl(file,FSCTL_SET_SPARSE,nullptr,0,nullptr,0,&returned,nullptr));
    LARGE_INTEGER length;length.QuadPart=runtime_host::kArtifactBudget-used;
    CHECK(SetFilePointerEx(file,length,nullptr,FILE_BEGIN)&&SetEndOfFile(file)&&FlushFileBuffers(file));
    CHECK(runtime_host::artifact_usage(used,error)&&used==runtime_host::kArtifactBudget);
    const std::vector<uint8_t> before(4096,0x33),after(4096,0x44);std::string hash;
    CHECK(!runtime_host::put_blob(before,hash,error));CHECK(error.find("budget")!=std::string::npos);
    CHECK(!runtime_host::reserve_artifacts({&before,&after},error));
    CHECK(fs::exists(path));CHECK(!fs::exists(path.parent_path()/(upd::sha256_hex(before)+".data")));
    CHECK(!fs::exists(path.parent_path()/(upd::sha256_hex(after)+".data")));CHECK(runtime_host::store.healthy());
    CloseHandle(file);error.clear();CHECK(runtime_host::reserve_artifacts({&before,&after},error));CHECK(!fs::exists(path));
    CHECK(runtime_host::put_blob(before,hash,error));std::string after_hash;CHECK(runtime_host::put_blob(after,after_hash,error));
    std::vector<uint8_t> recovered;CHECK(runtime_host::read_blob(hash,recovered,before.size(),error)&&recovered==before);
    CHECK(runtime_host::read_blob(after_hash,recovered,after.size(),error)&&recovered==after);
    CHECK(runtime_host::read_blob(runtime_host::resource_hash,recovered,ew::kResourceBytes,error)&&recovered==base);
}
static void test_missing_authored_captain_blocks_prepare() {
    Party party;side=0;runtime_host::refresh();CHECK(runtime_host::view().can_playtest);
    const auto before=native[0].snapshot.bytes;const auto restores=native[0].restores;
    const auto inspection=runtime_host::debug({"inspect"});
    CHECK(inspection.find("placements=1 classes=1 acts=1 act=0 bytes=")==0);
    CHECK(inspection.find("native_sha256="+upd::sha256_hex(before))!=std::string::npos);
    CHECK(inspection.find("document_semantic_sha256=")!=std::string::npos&&native[0].snapshot.bytes==before&&native[0].restores==restores);
    CHECK(native[0].save_callbacks.shared_active&&native[0].save_callbacks.shared_active());
    native[0].snapshot.captain={};runtime_host::refresh();CHECK(!runtime_host::view().can_playtest);
    CHECK(runtime_host::view().status.find("authored captain")!=std::string::npos);
    runtime_host::playtest();step();CHECK(runtime_host::controller->view().phase==es::Phase::Ready&&!runtime_host::frozen);
    side=0;runtime_host::close_session();CHECK(spin([]{return !runtime_host::closing;}));side=0;CHECK(!native[0].save_callbacks.shared_active());
    runtime_host::beginning=true;CHECK(native[0].save_callbacks.shared_active()&&runtime_host::authoring());runtime_host::beginning=false;
}
static void test_uncertain_maintenance_freezes_until_reopen() {
    Party party;side=0;const auto checkpoint=runtime_host::store.snapshot();
    const auto path=runtime_host::project_directory/"checkpoint.tmp";
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);CHECK(file!=INVALID_HANDLE_VALUE);
    runtime_host::gc_due=true;step();CHECK(runtime_host::frozen&&!runtime_host::store.healthy());
    CHECK(runtime_host::status.find("uncertain")!=std::string::npos);CHECK(runtime_host::controller->state().document.revision==0);
    CloseHandle(file);const auto root=runtime_host::project_root;runtime_host::store.close();
    editor_store::Store reopened;std::string error;CHECK(reopened.open(root,checkpoint.project,error));CHECK(reopened.snapshot()==checkpoint);
}
static void test_end_waits_for_native_settle_and_retires_once() {
    Party party;side=0;auto committed=runtime_host::current_native;auto restores=native[0].restores;
    native[0].state.gesture=true;native[0].snapshot.bytes=body(seed(),0x10);
    runtime_host::end();step();CHECK(runtime_host::closing&&runtime_host::controller->view().phase==es::Phase::Ready);
    CHECK(native[0].rebaselines==0&&native[0].restores==restores);side=0;CHECK(native[0].save_callbacks.shared_active());
    CHECK(!runtime_host::lease({{editor::ScopeKind::Document,{},{}}}));
    runtime_host::undo();runtime_host::begin();runtime_host::recover();step();CHECK(runtime_host::closing&&native[0].rebaselines==0);
    native[0].state.gesture=false;native[0].save_busy=true;step();CHECK(native[0].restores==restores);
    native[0].save_busy=false;native[0].rebaseline_ready=false;CHECK(spin([&]{return runtime_host::closing&&native[0].restores==restores+1;}));
    CHECK(native[0].snapshot.bytes==committed.bytes&&runtime_host::store.healthy());
    step();CHECK(native[0].restores==restores+1&&native[0].rebaselines==0);
    native[0].rebaseline_ready=true;CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&!runtime_host::closing;}));
    CHECK(native[0].rebaselines==1&&native[0].snapshot.bytes==committed.bytes);side=0;CHECK(!native[0].save_callbacks.shared_active());
    CHECK(runtime_host::native_generation==native[0].state.generation);
    runtime_host::refresh();CHECK(runtime_host::view().can_recover);
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Idle&&!runtime_guest::closing;}));
    CHECK(native[1].rebaselines==1&&!runtime_guest::store.healthy());
    runtime_host::end();step();CHECK(native[0].rebaselines==1);
}
static void test_end_does_not_touch_replaced_native_context() {
    Party party;side=0;const auto restores=native[0].restores;
    ++native[0].state.generation;native[0].snapshot.bytes=body(seed(),0x22);
    runtime_host::close_session();CHECK(spin([]{return !runtime_host::closing&&runtime_host::controller->view().phase==es::Phase::Idle;}));
    CHECK(native[0].rebaselines==0&&native[0].restores==restores&&native[0].snapshot.bytes.front()==0x22);
}
static void test_idle_controller_keeps_owned_native_cleanup_isolated() {
    Party party;side=1;runtime_guest::controller->end();native[1].state.gesture=true;runtime_guest::close_session();
    CHECK(runtime_guest::controller->view().phase==es::Phase::Idle&&runtime_guest::closing&&runtime_guest::authoring());
    CHECK(native[1].save_callbacks.shared_active()&&native[1].rebaselines==0);
    native[1].state.gesture=false;CHECK(spin([]{return !runtime_guest::closing;}));
    CHECK(native[1].rebaselines==1&&!runtime_guest::authoring());
}
static void test_delayed_gesture_cannot_restore_another_editor() {
    Party party;side=0;const auto restores=native[0].restores;auto generation=runtime_host::native_generation;
    editor_native::Gesture gesture;gesture.generation=generation;gesture.before=gesture.after=native[0].snapshot;
    ++native[0].state.generation;native[0].snapshot.bytes=body(seed(),0x41);
    CHECK(!native[0].callbacks.gate({placement})&&!native[0].callbacks.structure_gate(1024));
    runtime_host::captured(gesture);CHECK(runtime_host::frozen&&!runtime_host::pending&&!runtime_host::awaiting_operation);
    CHECK(native[0].restores==restores&&native[0].snapshot.bytes.front()==0x41);
}
static void accept_create_before_guest_dispatch() {
    std::string error;side=0;
    CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Document,{},{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    editor::Request request;request.summary="Accepted Create immediately before End";
    for(const auto& held:runtime_host::controller->view().local_leases)request.leases.push_back({held.scope,held.token});
    editor::Mutation m;m.kind=editor::MutationKind::Create;m.initial.class_id=class_id;m.initial.act_id=act_id;m.initial.position={4,5,6};request.mutations={m};
    side=0;CHECK(runtime_host::controller->submit(request,error));
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1;},false));
    CHECK(runtime_guest::controller->state().document.revision==0);
}
static void test_host_end_delivers_accepted_structural_checkpoint() {
    Party party;accept_create_before_guest_dispatch();side=0;
    const auto project=runtime_guest::project_id;const auto root=runtime_guest::project_root;const auto restores=native[0].restores;
    runtime_host::end();step(false);CHECK(runtime_host::closing&&native[0].rebaselines==0);
    CHECK(spin([]{return sessions[0].pending_to(1)==0;},false));
    CHECK(runtime_host::closing&&runtime_host::controller->state().document.revision==1);
    CHECK(runtime_guest::controller->state().document.revision==0&&native[0].restores==restores);
    CHECK(!runtime_host::controller->settled());
    CHECK(spin([]{return !runtime_host::closing&&!runtime_guest::closing&&runtime_host::controller->view().phase==es::Phase::Idle&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    CHECK(native[0].rebaselines==1&&native[1].rebaselines==1);
    CHECK(native[1].snapshot.document.placements.size()==2);
    editor_store::Store reopened;std::string error;CHECK(reopened.open(root,project,error));
    editor::State state;editor_native::Snapshot snapshot;CHECK(editor_project::load_snapshot(reopened.snapshot(),state,snapshot,error));
    CHECK(state.document.revision==1&&state.document.placements.size()==2&&snapshot.bytes==runtime_guest::current_native.bytes);
}
static void test_host_end_does_not_wait_for_departed_replica() {
    Party party;accept_create_before_guest_dispatch();side=0;runtime_host::end();step(false);
    CHECK(runtime_host::closing&&native[0].rebaselines==0);sessions[1].leave();
    CHECK(spin([]{return !runtime_host::closing&&runtime_host::controller->view().phase==es::Phase::Idle;},false));
    CHECK(native[0].rebaselines==1&&sessions[0].connected()&&sessions[0].players().size()==1);
}
static void test_host_end_waits_for_applied_same_revision_save() {
    Party party;side=0;const auto project=runtime_guest::project_id;const auto root=runtime_guest::project_root;
    native[1].state.gesture=true;
    native[0].snapshot.source={saved_key.instance,saved_key.type,saved_key.group};native[0].snapshot.bytes=body(native[0].snapshot.document,0x7D);
    runtime_host::saved_native(native[0].snapshot.source);runtime_host::end();step(false);
    CHECK(runtime_host::closing&&native[0].rebaselines==0);
    CHECK(spin([]{return sessions[0].pending_to(1)==0&&!runtime_host::save_publication_pending;},false));
    for(unsigned i=0;i<5;++i)step();
    CHECK(runtime_host::closing&&!runtime_host::controller->settled());
    CHECK(runtime_guest::resources.root==original_key&&runtime_guest::current_native.bytes.front()!=0x7D);
    CHECK(runtime_host::controller->state().document.revision==0&&runtime_guest::controller->state().document.revision==0);
    native[1].state.gesture=false;
    CHECK(spin([]{return !runtime_host::closing&&!runtime_guest::closing&&runtime_host::controller->view().phase==es::Phase::Idle&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    CHECK(runtime_guest::current_native.source==editor::ResourceKey{saved_key.instance,saved_key.type,saved_key.group});
    CHECK(runtime_guest::current_native.bytes.front()==0x7D&&native[1].rebaselines==1);
    editor_store::Store reopened;std::string error;CHECK(reopened.open(root,project,error));
    editor::State state;editor_native::Snapshot snapshot;CHECK(editor_project::load_snapshot(reopened.snapshot(),state,snapshot,error));
    CHECK(state.document.revision==0&&snapshot.bytes.front()==0x7D&&snapshot.source==runtime_guest::current_native.source);
}
static void test_native_frozen_guest_does_not_block_host_close() {
    Party party;accept_create_before_guest_dispatch();side=0;runtime_host::end();step(false);
    CHECK(runtime_host::closing);native[1].state.gesture=true;runtime_guest::frozen=true;runtime_guest::status="Injected native validation failure";
    CHECK(spin([]{return !runtime_host::closing&&runtime_host::controller->view().phase==es::Phase::Idle;}));
    CHECK(native[0].rebaselines==1&&sessions[0].players().size()==2&&sessions[1].connected());
    CHECK(runtime_guest::closing||runtime_guest::controller->view().phase==es::Phase::Frozen);
    side=1;CHECK(native[1].save_callbacks.shared_active());
    native[1].state.gesture=false;CHECK(spin([]{return !runtime_guest::closing&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
}
static uint64_t capture_local_create() {
    std::string error;side=0;CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Document,{},{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    side=0;editor_native::Gesture gesture;gesture.generation=native[0].state.generation;gesture.before=native[0].snapshot;gesture.after=gesture.before;
    auto p=gesture.after.document.placements.at(placement);p.object={(1ull<<32)|77,1};p.position={7,8,9};
    gesture.after.document.placements.emplace(p.object,p);gesture.after.bindings.push_back({p.object,1});gesture.after.bytes=body(gesture.after.document);
    native[0].snapshot=gesture.after;runtime_host::captured(std::move(gesture));
    CHECK(runtime_host::pending&&runtime_host::awaiting_operation&&!runtime_host::pending->submitted);
    CHECK(runtime_host::controller->state().document.revision==0&&native[0].snapshot.document.placements.size()==1);
    return game_frame;
}
static void same_frame_drain(unsigned p) {
    side=int(p);if(p)runtime_guest::tick(now_seconds());else runtime_host::tick(now_seconds());run_tasks(p);
}
static void test_native_recreation_waits_for_actual_next_frame() {
    Party party;const auto frame=capture_local_create();const auto applies=native[0].structural_applies;CHECK(applies==1);
    for(unsigned i=0;i<8;++i)same_frame_drain(0);
    CHECK(game_frame==frame&&runtime_host::pending&&!runtime_host::pending->submitted);
    CHECK(native[0].structural_applies==applies&&runtime_host::controller->state().document.revision==0);
    side=0;CHECK(!native[0].callbacks.structure_gate(1024));
    ++game_frame;same_frame_drain(0);
    CHECK(runtime_host::controller->state().document.revision==1&&!runtime_host::pending&&!runtime_host::awaiting_operation);
    CHECK(native[0].structural_applies==applies+1&&native[0].structural_frame>frame);
    for(unsigned i=0;i<4;++i)same_frame_drain(0);CHECK(native[0].structural_applies==applies+1);
}
static void test_held_native_gesture_is_cancelled_by_end() {
    Party party;capture_local_create();const auto applies=native[0].structural_applies;side=0;runtime_host::end();
    ++game_frame;same_frame_drain(0);CHECK(!runtime_host::pending&&!runtime_host::awaiting_operation);
    CHECK(spin([]{return !runtime_host::closing&&runtime_host::controller->view().phase==es::Phase::Idle;}));
    CHECK(native[0].structural_applies==applies&&native[0].snapshot.document.placements.size()==1);
}
static void test_recovery_retires_held_and_delayed_native_captures() {
    Party party;capture_local_create();
    auto old_gesture=runtime_host::pending->gesture;
    const auto old_epoch=runtime_host::controller->state().document.epoch;
    const auto old_generation=native[0].state.generation;
    const auto applies=native[0].structural_applies;
    native[0].rebaseline_ready=false;side=0;runtime_host::recover();
    ++game_frame;same_frame_drain(0);
    CHECK(!runtime_host::pending&&!runtime_host::awaiting_operation&&runtime_host::recovery);
    CHECK(runtime_host::controller->state().document.epoch==old_epoch&&native[0].rebaselines==0);
    CHECK(native[0].snapshot.document.placements.size()==1&&native[0].structural_applies==applies);
    const auto restored=native[0].restores;
    side=0;runtime_host::captured(old_gesture);
    CHECK(!runtime_host::frozen&&!runtime_host::pending&&!native[0].callbacks.structure_gate(1024));
    for(unsigned i=0;i<4;++i)same_frame_drain(0);
    CHECK(runtime_host::recovery&&native[0].restores==restored&&native[0].state.generation==old_generation);
    native[0].rebaseline_ready=true;same_frame_drain(0);
    CHECK(!runtime_host::recovery&&native[0].rebaselines==1&&!runtime_host::frozen);
    CHECK(runtime_host::controller->state().document.epoch!=old_epoch&&runtime_host::controller->state().document.revision==0);
    CHECK(native[0].state.generation==old_generation+1&&runtime_host::native_generation==native[0].state.generation);
    const auto recovered=native[0].snapshot.bytes;const auto after_restore=native[0].restores;
    // The physical editor is unchanged, but its old capture generation must
    // never cross the freshly recovered logical epoch or restore an old draft.
    side=0;runtime_host::captured(std::move(old_gesture));
    CHECK(runtime_host::frozen&&!runtime_host::pending&&!runtime_host::awaiting_operation);
    CHECK(native[0].restores==after_restore&&native[0].structural_applies==applies&&native[0].snapshot.bytes==recovered);
}
static void test_recovery_wait_does_not_restore_replaced_context() {
    {Party party;native[0].rebaseline_ready=false;side=0;runtime_host::recover();same_frame_drain(0);
    CHECK(runtime_host::recovery&&runtime_host::recovery->restored);
    const auto restored=native[0].restores;const auto applies=native[0].structural_applies;
    ++native[0].state.generation;native[0].snapshot.bytes=body(seed(),0x6C);same_frame_drain(0);
    CHECK(!runtime_host::recovery&&runtime_host::frozen&&native[0].rebaselines==0);
    CHECK(native[0].restores==restored&&native[0].structural_applies==applies&&native[0].snapshot.bytes.front()==0x6C);}
    {Party party;const auto restored=native[0].restores;
    ++native[0].state.generation;native[0].snapshot.bytes=body(seed(),0x6D);side=0;runtime_host::recover();same_frame_drain(0);
    CHECK(!runtime_host::recovery&&runtime_host::frozen&&native[0].rebaselines==0);
    CHECK(native[0].restores==restored&&native[0].snapshot.bytes.front()==0x6D);}
}
static void test_native_save_enrichment_survives_edits_save_and_recovery() {
    Party party(0,0xC1);std::string error;
    CHECK(native[0].snapshot.bytes.front()==0xC1&&native[1].snapshot.bytes.front()==0xC1);
    CHECK(runtime_host::resources.base_root->front()==0xC1&&runtime_guest::resources.base_root->front()==0xC1);
    side=1;CHECK(runtime_guest::controller->request_leases({{editor::ScopeKind::Field,placement,{editor::Field::Position,0}}},15000,error));
    CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
    runtime_guest::guarded_placements[placement]=runtime_guest::controller->state().document.placements.at(placement);
    editor_native::Gesture gesture;gesture.generation=native[1].state.generation;gesture.before=native[1].snapshot;gesture.after=gesture.before;
    gesture.after.document.placements.at(placement).position={6,7,8};gesture.after.bytes=body(gesture.after.document,gesture.before.bytes.front());
    native[1].snapshot=gesture.after;side=1;runtime_guest::captured(std::move(gesture));
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1&&runtime_guest::controller->state().document.revision==1;}));
    CHECK(native[0].snapshot.bytes.front()==0xC1&&native[1].snapshot.bytes.front()==0xC1);
    side=0;native[0].persisted_bytes=body(runtime_host::controller->state().document,0xD1);
    runtime_host::saved_native(native[0].snapshot.source);
    CHECK(!runtime_host::frozen&&native[0].snapshot.bytes.front()==0xD1&&runtime_host::current_native.bytes.front()==0xD1);
    CHECK(spin([]{return runtime_guest::current_native.bytes.front()==0xD1&&runtime_host::controller->settled();}));
    CHECK(native[1].snapshot.bytes.front()==0xD1&&runtime_guest::store.snapshot().native_snapshot.front()==0xD1);
    const auto old_epoch=runtime_host::controller->state().document.epoch;
    native[0].snapshot.bytes=body(seed(),0x11);side=0;runtime_host::recover();
    CHECK(spin([&]{return !runtime_host::recovery&&runtime_host::controller->state().document.epoch!=old_epoch;}));
    CHECK(native[0].snapshot.bytes.front()==0xD1&&runtime_host::current_native.bytes.front()==0xD1);
    CHECK(runtime_host::resources.base_root->front()==0xC1&&runtime_host::store.snapshot().native_snapshot.front()==0xD1);
}
static void test_invalid_completed_save_does_not_replace_live_draft() {
    Party party;side=0;const auto before=native[0].snapshot.bytes;const auto restored=native[0].restores;
    auto invalid=runtime_host::controller->state().document;invalid.placements.at(placement).position={99,0,0};
    native[0].persisted_bytes=body(invalid,0xE3);runtime_host::saved_native(native[0].snapshot.source);
    CHECK(runtime_host::frozen&&native[0].restores==restored&&native[0].snapshot.bytes==before);
    CHECK(runtime_host::store.snapshot().native_snapshot==before&&runtime_host::controller->state().document.revision==0);
}
static void test_opaque_gesture_changes_freeze_before_empty_semantic_return() {
    for(unsigned prior=0;prior<2;++prior){Party party;side=0;
    const auto canonical=runtime_host::current_native.bytes;const auto restored=native[0].restores;const auto applied=native[0].structural_applies;
    editor_native::Gesture gesture;gesture.generation=native[0].state.generation;gesture.before=native[0].snapshot;gesture.after=gesture.before;
    if(prior)gesture.before.bytes.front()=0xB2;
    gesture.after.bytes.front()=0xB2;native[0].snapshot=gesture.after;
    runtime_host::captured(std::move(gesture));
    CHECK(runtime_host::frozen&&!runtime_host::pending&&!runtime_host::awaiting_operation);
    CHECK(native[0].restores==restored+1&&native[0].structural_applies==applied&&native[0].snapshot.bytes==canonical);
    CHECK(runtime_host::controller->state().document.revision==0&&runtime_host::store.snapshot().native_snapshot==canonical);}
}
static void test_uncaptured_opaque_changes_cannot_save_or_playtest() {
    for(unsigned preview=0;preview<2;++preview){Party party;side=0;runtime_host::refresh();
    CHECK(runtime_host::view().can_save&&runtime_host::view().can_playtest);
    const auto canonical=runtime_host::current_native.bytes;const auto restored=native[0].restores;
    native[0].snapshot.bytes.front()=0xB3;
    const auto diagnosis=runtime_host::debug({"check"});CHECK(diagnosis.starts_with("DIVERGED:"));
    CHECK(native[0].restores==restored&&native[0].snapshot.bytes.front()==0xB3&&!runtime_host::frozen);
    if(preview)runtime_host::playtest();else runtime_host::save();same_frame_drain(0);
    CHECK(runtime_host::frozen&&native[0].restores==restored+1&&native[0].snapshot.bytes==canonical);
    CHECK(!native[0].save_calls&&!native[0].save_as_calls&&!native[0].preview&&!native[0].active);
    CHECK(runtime_host::controller->state().document.revision==0&&runtime_host::store.snapshot().native_snapshot==canonical);}
}
static void test_host_apply_rejects_uncaptured_opaque_changes() {
    Party party;std::string error;side=1;
    CHECK(runtime_guest::controller->request_leases({{editor::ScopeKind::Field,placement,{editor::Field::Position,0}}},15000,error));
    CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
    editor::Request request;request.summary="Move while host has an unsupported uncaptured change";request.base_revision=0;
    for(const auto& held:runtime_guest::controller->view().local_leases)request.leases.push_back({held.scope,held.token});
    editor::Mutation mutation;mutation.target=placement;mutation.field={editor::Field::Position,0};mutation.value=editor::Vec3{8,8,8};request.mutations={mutation};
    const auto canonical=runtime_host::current_native.bytes;native[0].snapshot.bytes.front()=0xB4;
    side=1;CHECK(runtime_guest::controller->submit(request,error));CHECK(spin([]{return runtime_host::frozen;}));
    CHECK(native[0].snapshot.bytes==canonical&&runtime_host::controller->state().document.revision==0);
    CHECK(runtime_host::store.snapshot().native_snapshot==canonical);
}
static void test_held_native_gesture_is_fenced_by_generation_or_disconnect() {
    {Party party;capture_local_create();const auto applies=native[0].structural_applies;const auto restores=native[0].restores;
    ++native[0].state.generation;native[0].snapshot.bytes=body(seed(),0x64);++game_frame;same_frame_drain(0);
    CHECK(!runtime_host::pending&&!runtime_host::awaiting_operation&&runtime_host::frozen);
    CHECK(native[0].structural_applies==applies&&native[0].restores==restores&&native[0].snapshot.bytes.front()==0x64);
    CHECK(runtime_host::controller->state().document.revision==0);}
    {Party party;capture_local_create();const auto applies=native[0].structural_applies;sessions[0].leave();
    ++game_frame;same_frame_drain(0);CHECK(!runtime_host::pending&&!runtime_host::awaiting_operation);
    side=0;runtime_host::session_ended();CHECK(spin([]{return !runtime_host::closing&&runtime_host::controller->view().phase==es::Phase::Idle;}));
    CHECK(native[0].structural_applies==applies&&native[0].snapshot.document.placements.size()==1);}
}
static void test_held_move_accepts_disjoint_remote_revision() {
    Party party;std::string error;
    accept_create_before_guest_dispatch();CHECK(spin([]{return runtime_guest::controller->state().document.revision==1;}));
    std::vector<editor::LeaseToken> release;for(const auto& held:runtime_host::controller->view().local_leases)release.push_back({held.scope,held.token});
    side=0;CHECK(runtime_host::controller->release_leases(release,error));CHECK(spin([]{return runtime_host::controller->view().local_leases.empty();}));
    editor::ObjectId other{};for(const auto& [id,p]:runtime_host::controller->state().document.placements){(void)p;if(id!=placement)other=id;}CHECK(other.id);
    side=1;CHECK(runtime_guest::controller->request_leases({{editor::ScopeKind::Field,placement,{editor::Field::Position,0}}},15000,error));
    side=0;CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Field,other,{editor::Field::Position,0}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty()&&!runtime_guest::controller->view().local_leases.empty();}));
    side=1;runtime_guest::guarded_placements[placement]=runtime_guest::controller->state().document.placements.at(placement);
    editor_native::Gesture gesture;gesture.generation=native[1].state.generation;gesture.before=native[1].snapshot;gesture.after=gesture.before;
    gesture.after.document.placements.at(placement).position={3,2,1};gesture.after.bytes=body(gesture.after.document);native[1].snapshot=gesture.after;
    runtime_guest::captured(std::move(gesture));CHECK(runtime_guest::pending&&!runtime_guest::pending->submitted);const auto frame=game_frame;
    editor::Request request;request.summary="Other placement moves while guest move waits one frame";request.base_revision=1;
    for(const auto& held:runtime_host::controller->view().local_leases)request.leases.push_back({held.scope,held.token});
    editor::Mutation m;m.target=other;m.field={editor::Field::Position,0};m.value=editor::Vec3{11,12,13};
    m.expected=editor::field_revision(runtime_host::controller->state().document.placements.at(other),m.field);request.mutations={m};
    side=0;CHECK(runtime_host::controller->submit(request,error));
    const auto begun=net::now_ms();
    while(runtime_guest::controller->state().document.revision==1&&net::now_ms()-begun<3000){network();same_frame_drain(0);same_frame_drain(1);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    CHECK(game_frame==frame&&runtime_guest::controller->state().document.revision==2);
    CHECK(runtime_guest::pending&&!runtime_guest::pending->submitted&&runtime_guest::pending->request.base_revision==1);
    CHECK(!runtime_guest::frozen&&native[1].snapshot.document.placements.at(other).position==editor::Vec3{11,12,13});
    ++game_frame;same_frame_drain(1);
    CHECK(spin([]{return runtime_host::controller->state().document.revision==3&&runtime_guest::controller->state().document.revision==3;}));
    CHECK(!runtime_guest::pending&&!runtime_guest::awaiting_operation&&!runtime_guest::frozen);
    CHECK(native[0].snapshot.document.placements.at(placement).position==editor::Vec3{3,2,1}&&native[0].snapshot.document.placements.at(other).position==editor::Vec3{11,12,13});
    CHECK(native[1].snapshot.document.placements.at(placement).position==editor::Vec3{3,2,1}&&native[1].snapshot.document.placements.at(other).position==editor::Vec3{11,12,13});
}
int main(int argc,char** argv){const bool quick=argc==2&&std::string(argv[1])=="--quick";if(argc>1&&!quick)return 2;test_begin_ga_batch_and_end_returning();test_saved_resources_then_next_commit();test_guest_recovery_requires_owned_native_copy();test_pending_undo_keeps_ownership_across_foreign_commit();test_pending_artifacts_obey_capacity_and_preserve_durable_base();test_missing_authored_captain_blocks_prepare();test_uncertain_maintenance_freezes_until_reopen();test_end_waits_for_native_settle_and_retires_once();test_end_does_not_touch_replaced_native_context();test_idle_controller_keeps_owned_native_cleanup_isolated();test_delayed_gesture_cannot_restore_another_editor();test_host_end_delivers_accepted_structural_checkpoint();test_host_end_does_not_wait_for_departed_replica();test_host_end_waits_for_applied_same_revision_save();test_native_frozen_guest_does_not_block_host_close();test_native_recreation_waits_for_actual_next_frame();test_held_native_gesture_is_cancelled_by_end();test_recovery_retires_held_and_delayed_native_captures();test_recovery_wait_does_not_restore_replaced_context();test_native_save_enrichment_survives_edits_save_and_recovery();test_invalid_completed_save_does_not_replace_live_draft();test_opaque_gesture_changes_freeze_before_empty_semantic_return();test_uncaptured_opaque_changes_cannot_save_or_playtest();test_host_apply_rejects_uncaptured_opaque_changes();test_held_native_gesture_is_fenced_by_generation_or_disconnect();test_held_move_accepts_disjoint_remote_revision();if(!quick)test_large_closure_retained_once_and_delta_recovery();std::printf("editor runtime: %u checks passed\n",checks);}
