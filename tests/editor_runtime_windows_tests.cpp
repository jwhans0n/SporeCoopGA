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
#include "editor_metrics.h"
#include "game.h"
#include "modes.h"
#include "sporekit_game.hpp"
#include "../updater/sha256.h"

// Two independent process-global runtime instances, with the production source
// unchanged. Native callbacks select the fixture's current simulated process.
namespace runtime_host { using coop_editor::Ctx; using coop_editor::View; using coop_editor::PlayerView; using coop_editor::TransformHint; }
#define coop_editor runtime_host
#include "../mods/coop/editor_runtime.cpp"
#undef coop_editor
namespace runtime_guest { using coop_editor::Ctx; using coop_editor::View; using coop_editor::PlayerView; using coop_editor::TransformHint; }
#define coop_editor runtime_guest
#include "../mods/coop/editor_runtime.cpp"
#undef coop_editor

namespace editor_native { void deny_input(const std::vector<editor::Scope>&){} }

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
    editor_native::LocalView view;
    bool view_ready=true,refuse_view_restore=false,preview_owned=true;
    unsigned view_captures=0,view_restores=0;
    editor_native_save::Ctx save_callbacks;
    std::optional<adventure_native::EditorPreview> preview;
    bool active=false,cleanup=false,return_ready=true,save_busy=false,rebaseline_ready=true;
    bool silently_refuse_apply=false,partially_apply=false,refuse_restore=false;
    bool bump_generation_on_stop=false,preserve_live_resource_key_on_restore=true,deferred_native_exit=false;
    bool focus_on_selection=false;
    unsigned cancels=0,stops=0,restores=0,ga_received=0,ga_delivered=0;
    unsigned save_as_calls=0,save_calls=0;
    unsigned rebaselines=0;
    unsigned structural_applies=0;uint64_t structural_frame=0;
    unsigned selections=0,enters=0,registrations=0;
    unsigned validations=0,opaque_checks=0;
    std::optional<std::vector<uint8_t>> persisted_bytes;
};
std::array<Native,2> native;
bool seed_cast_settings=false;
bool seed_act_settings=false;
bool seed_creation_settings=false;
std::array<bool,2> creation_active{};
std::array<std::vector<dbpf::Record>,2> creation_records;
bool creation_content_enabled=false;
std::array<bool,2> upload_collision{};
uint32_t creation_version=1;
editor::Document seed() {
    editor::Document d;d.project[0]=1;d.epoch=7;d.next_id=5ull<<32;
    d.classes.insert(class_id);d.acts.insert(act_id);d.schema=editor_native::relative_scale_schema();
    editor::Placement p;p.object=placement;p.class_id=class_id;p.act_id=act_id;p.properties[editor_native::kRelativeScaleProperty]=.5f;d.placements.emplace(p.object,p);
    if(seed_cast_settings || seed_creation_settings){
        d.next_id=6ull<<32;d.class_schema=editor_native::cast_property_schema();
        editor::ClassActSettings settings;settings.object={(5ull<<32)|1,1};settings.class_id=class_id;settings.act_id=act_id;
        settings.properties[editor_native::kCastBlockProperty]=editor::StructuredValue{editor_native::kCastBlockFormat,{1,2,3},{}};
        settings.properties[editor::kClassAssetResourceProperty]=editor::ResourceKey{9,0x2B978C46,0x408A0000};
        d.class_settings.emplace(settings.object,std::move(settings));
        if(seed_creation_settings){d.next_class=2;d.next_id=9ull<<32;}
    }
    if(seed_act_settings){
        d.next_id=9ull<<32;d.next_class=81;d.class_schema=editor_native::cast_property_schema();
        for(unsigned i=1;i<=80;++i){
            editor::ClassActSettings row;row.class_id={(2ull<<32)|i,1};row.object={(5ull<<32)|i,1};row.act_id=act_id;
            row.properties[editor_native::kCastBlockProperty]=editor::StructuredValue{editor_native::kCastBlockFormat,{0},{}};
            row.properties[editor::kClassAssetResourceProperty]=editor::ResourceKey{i,0x2B978C46,0x408A0000};
            d.classes.insert(row.class_id);d.class_settings[row.object]=row;
        }
    }
    return d;
}
std::vector<uint8_t> body(const editor::Document& d,uint8_t opaque=0xA5) {
    editor::State state;state.document=d;std::vector<uint8_t> bytes;std::string error;
    if(!ew::encode_state(state,bytes,error)){std::printf("Fixture snapshot encoding failed: %s\n",error.c_str());CHECK(false);}
    bytes.insert(bytes.begin(),opaque);return bytes;
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
        unsigned(runtime_guest::controller->view().phase),runtime_guest::status.c_str());
    const auto ha=runtime_host::controller->accounting(),ga=runtime_guest::controller->accounting();
    std::printf("runtime revisions host=%llu guest=%llu durable=%llu/%llu held=%zu/%zu deferred=%zu/%zu pending=%u/%u awaiting=%u/%u errors=%s | %s\n",
        static_cast<unsigned long long>(runtime_host::controller->state().document.revision),static_cast<unsigned long long>(runtime_guest::controller->state().document.revision),
        static_cast<unsigned long long>(runtime_host::store.durable_revision()),static_cast<unsigned long long>(runtime_guest::store.durable_revision()),
        ha.held_count,ga.held_count,ha.deferred_count,ga.deferred_count,unsigned(runtime_host::pending.has_value()),unsigned(runtime_guest::pending.has_value()),
        unsigned(runtime_host::awaiting_operation.has_value()),unsigned(runtime_guest::awaiting_operation.has_value()),runtime_host::controller->view().error.c_str(),runtime_guest::controller->view().error.c_str());return false;
}
void complete_cleanup(unsigned p) {
    side=int(p);CHECK(native[p].cleanup);native[p].cleanup=false;
    if (native[p].preview) {auto notify=native[p].preview->return_editor;if(notify)notify();}
}
// A completed native save changes the live resource key before its callback.
void simulate_host_saved(editor::ResourceKey key) {
    native[0].snapshot.source=key;runtime_host::saved_native(key);
}
struct Party {
    fs::path directory;
    Party(size_t immutable_bytes=0,uint8_t initial_saved_opaque=0xA5,bool unsaved=false,bool automatic=false,bool guest_draft=false,uint32_t acts=1,uint32_t host_act=0) {
        dependency_bytes=immutable_bytes;
        clock_ms=10000;game_frame=1;dispatching=false;
        directory=fs::temp_directory_path()/("sporecoop-editor-runtime-tests-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(net::now_ms()));
        CHECK(!fs::exists(directory));CHECK(fs::create_directories(directory));
        for(unsigned p=0;p<2;++p) {
            upload_collision[p]=false;
            side=int(p);native[p]={};auto& n=native[p];
            n.state.available=true;n.state.editing=p==0 || guest_draft;n.state.ready=n.state.editing;n.state.generation=1;
            n.view.target={500,float(p),0};n.view.rotation={0,0,0,1};n.view.orbit={float(p),.6f,123};
            n.snapshot.source={original_key.instance,original_key.type,original_key.group};n.snapshot.captain=avatar_key;
            if(unsaved && !p)n.snapshot.source={};
            n.snapshot.document=seed();
            for(uint32_t act=1;act<acts;++act)n.snapshot.document.acts.insert({(3ull<<32)|act,1});
            if(seed_act_settings || seed_creation_settings){
                auto& d=n.snapshot.document;d.settings_schema=editor_native::authoring_property_schema();
                for(auto act:d.acts){
                    editor::AuthoringSettings row;row.object=editor_native::act_settings_id(uint32_t(act.id));row.act_id=act;
                    row.properties[editor_native::kActBlockProperty]=editor::StructuredValue{editor_native::kActBlockFormat,{uint8_t(act.id+1)},{}};
                    d.settings.emplace(row.object,row);d.act_order.push_back(act);
                }
                editor::AuthoringSettings captain;captain.object=editor_native::kCaptainObject;captain.kind=editor::SettingsKind::Captain;
                captain.properties={{editor_native::kCaptainAssetProperty,editor::ResourceKey{avatar_key.instance,avatar_key.type,avatar_key.group}},
                    {editor_native::kCaptainPositionProperty,editor::Vec3{}},{editor_native::kCaptainRotationProperty,editor::Rotation{}},
                    {editor_native::kCaptainScaleProperty,1.f},{editor_native::kCaptainLockedProperty,false},{editor_native::kCaptainHealthProperty,10.f},{editor_native::kCaptainInvulnerableProperty,false}};
                d.settings.emplace(captain.object,std::move(captain));
            }
            n.snapshot.act=n.state.act=p?0:host_act;
            n.snapshot.bytes=body(n.snapshot.document);bind_native(n);
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
            ctx.automatic_follow=automatic;
            ctx.leave_party=[p] {if(p)runtime_guest::session_ended();else runtime_host::session_ended();sessions[p].leave();};
            if(p) {CHECK(runtime_guest::init(&host,nullptr,ctx));runtime_guest::project_root=directory/"guest";}
            else {CHECK(runtime_host::init(&host,nullptr,ctx));runtime_host::project_root=directory/"host";}
        }
        std::string error;CHECK(sessions[0].host(0,"Host",error));
        CHECK(sessions[1].join("127.0.0.1:"+std::to_string(sessions[0].port()),"Guest",error));
        CHECK(spin([] {return sessions[0].players().size()==2&&sessions[1].connected();}));
        if(!automatic) {
            side=0;runtime_host::start_saved();CHECK(!runtime_host::frozen);
            // Existing lifecycle fixtures begin at an explicitly saved revision.
            if(!unsaved){simulate_host_saved(native[0].snapshot.source);CHECK(!runtime_host::frozen);}
        }
        native[0].persisted_bytes.reset();
        if(guest_draft && automatic) {
            CHECK(spin([] {return runtime_guest::view().join_prompt;}));return;
        }
        if(!automatic) {
            CHECK(spin([] {return runtime_guest::controller->view().phase==es::Phase::Offered;}));
            side=1;CHECK(runtime_guest::controller->accept_offer(error));
        }
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
ValidationScope::ValidationScope()=default;ValidationScope::~ValidationScope()=default;
void bind(const std::vector<Binding>& bindings){native[side].snapshot.bindings=bindings;}
bool prepare(const Snapshot& base,const editor::Document& document,const editor::Change&,const Snapshot* artifact,Snapshot& out,std::string&){
    CHECK(!dispatching);out=base;out.document=document;
    out.bytes=body(document,artifact&&!artifact->bytes.empty()?artifact->bytes.front():base.bytes.front());
    out.bindings.clear();uint32_t index=0;for(const auto& [id,p]:document.placements){(void)p;out.bindings.push_back({id,index++});}return true;
}
bool apply_prepared(const Snapshot& prepared,const editor::Change& change,std::string& error){
    if(!apply(prepared.document,change,&prepared,error))return false;
    return validate(native[side].snapshot,prepared.document,error);
}
bool canonicalize_cast_state(editor::State&,std::string&){return true;}
bool capture_draft(Snapshot& value,std::string& error){return capture(value,error);}
bool expand_snapshot(const Snapshot& input,bool,Snapshot& out,std::string& error){
    // This double's owned bytes already contain its complete semantic model.
    // It does not model additional native fields or establish native migration.
    out=input;
    if(out.document.acts.empty()){
        editor::State parsed;
        if(input.bytes.empty() || !ew::decode_state(std::vector<uint8_t>(input.bytes.begin()+1,input.bytes.end()),parsed,error))return false;
        out.document=parsed.document;
    }
    return true;
}
uint32_t creation_group(){return 0x408A0000;}
bool extended_complexity_available(){return true;}
bool validate_entry(const std::vector<uint8_t>& bytes,uint32_t act,std::string& error){
    editor::State parsed;
    return !bytes.empty() && ew::decode_state(std::vector<uint8_t>(bytes.begin()+1,bytes.end()),parsed,error) &&
        parsed.document.acts.contains({(3ull<<32)|act,1});
}
bool init(const SkHost*,SkMod*,Ctx ctx){native[side].callbacks=std::move(ctx);return true;}
void bind_acts(const editor::Document&){}
bool edit_acts(int action,const editor::Document& document,std::string& error){
    CHECK(!dispatching);auto& n=native[side];
    if(document.act_order.empty()){error="Roster fixture not configured";return false;}
    Gesture gesture;gesture.generation=n.state.generation;gesture.before=n.snapshot;gesture.after=n.snapshot;
    auto& d=gesture.after.document;auto index=n.state.act;
    if(action==0){
        editor::AuthoringSettings row;row.act_id={document.next_id,1};row.object={document.next_id+1,1};
        row.properties[kActBlockProperty]=editor::StructuredValue{kActBlockFormat,{42},{}};
        d.next_id+=2;d.settings[row.object]=row;d.acts.insert(row.act_id);d.act_order.insert(d.act_order.begin()+index+1,row.act_id);
    }else if(action==1){
        const auto removed=d.act_order[index];d.act_order.erase(d.act_order.begin()+index);d.acts.erase(removed);
        std::erase_if(d.settings,[&](const auto& row){return row.second.act_id==removed;});
    }else std::swap(d.act_order[index],d.act_order[action==2?index-1:index+1]);
    for(auto& [id,c]:d.class_settings){
        (void)id;auto& bytes=std::get<editor::StructuredValue>(c.properties.at(kCastBlockProperty)).bytes;bytes.clear();
        for(auto act:d.act_order)bytes.push_back(uint8_t(act.id));c.act_id=*d.acts.begin();
    }
    for(auto& [id,p]:d.placements){(void)id;p.act_id=*d.acts.begin();}
    gesture.after.bytes=body(d);n.callbacks.gesture(std::move(gesture));return true;
}
void shutdown(){}void tick(){}State state(){return native[side].state;}
editor::ResourceKey avatar(){return native[side].snapshot.captain;}bool avatar_locked(){return true;}
bool capture(Snapshot& value,std::string& error){CHECK(!dispatching);if(!native[side].state.ready){error="Injected native not ready";return false;}value=native[side].snapshot;
    if(native[side].callbacks.relative_scale_tracked&&!native[side].callbacks.relative_scale_tracked()){
        value.document.schema.clear();for(auto& [id,p]:value.document.placements){(void)id;p.properties.clear();}
    }return true;}
bool capture_view(LocalView& value,std::string& error){CHECK(!dispatching);auto& n=native[side];
    if(!n.state.ready||n.state.gesture||!n.view_ready){error="Injected local camera is not ready";return false;}
    ++n.view_captures;value=n.view;return true;}
bool restore_view(const LocalView& value,uint64_t generation,std::string& error){CHECK(!dispatching);auto& n=native[side];
    if(!n.state.ready||n.state.gesture||!n.view_ready||n.state.generation!=generation||n.refuse_view_restore){error="Injected local camera restore failed";return false;}
    ++n.view_restores;n.view=value;return true;}
bool validate(const Snapshot& snapshot,const editor::Document& wanted,std::string& error){
    ++native[side].validations;
    CHECK(!dispatching);editor::State parsed;
    if(snapshot.bytes.empty()||!ew::decode_state(std::vector<uint8_t>(snapshot.bytes.begin()+1,snapshot.bytes.end()),parsed,error))return false;
    const auto& d=parsed.document;
    if(d.classes!=wanted.classes||d.acts!=wanted.acts||d.placements.size()!=wanted.placements.size()){error="Injected persisted projection mismatch";return false;}
    auto binding=snapshot.bindings.begin();
    for(const auto& [id,p]:d.placements){(void)id;if(binding==snapshot.bindings.end()){error="Injected missing binding";return false;}
        auto it=wanted.placements.find(binding++->object);if(it==wanted.placements.end()||p.class_id!=it->second.class_id||p.act_id!=it->second.act_id||p.position!=it->second.position||p.rotation!=it->second.rotation||p.scale!=it->second.scale||p.properties!=it->second.properties){error="Injected persisted field mismatch";return false;}}
    return true;
}
bool normalize_saved_grounding(Snapshot& snapshot,const editor::Document& wanted,std::string& error){return validate(snapshot,wanted,error);}
bool supported_changes(const Gesture& gesture,std::string& error){CHECK(!dispatching);
    ++native[side].opaque_checks;
    if(gesture.before.bytes.empty()||gesture.after.bytes.empty()||gesture.before.bytes.front()!=gesture.after.bytes.front()){
        error="Injected unsupported opaque authoring change";return false;
    }return true;
}
bool restore(const Snapshot& value,std::string& error){CHECK(!dispatching);CHECK(native[side].state.ready);
    if(native[side].refuse_restore){error="Injected native restore failure";return false;}
    if(native[side].callbacks.relative_scale_tracked)CHECK(native[side].callbacks.relative_scale_tracked()==!value.document.schema.empty());const auto live_key=native[side].snapshot.source;native[side].snapshot=value;native[side].state.act=value.act;if(native[side].preserve_live_resource_key_on_restore)native[side].snapshot.source=live_key;native[side].snapshot.captain=avatar_key;++native[side].restores;return true;}
bool rebaseline(std::string& error){CHECK(!dispatching);auto& n=native[side];if(!n.rebaseline_ready||n.state.gesture){error="Injected native capture pending";return false;}
    CHECK(n.state.ready);++n.rebaselines;++n.state.generation;n.state.history_cursor=n.state.history_size=0;return true;}
bool apply(const editor::Document& doc,std::string& error){return apply(doc,editor::Change{},nullptr,error);}
bool apply(const editor::Document& doc,const editor::Change& change,const Snapshot* artifact,std::string&){CHECK(!dispatching);auto& n=native[side];if(!change.patch.objects.empty()){++n.structural_applies;n.structural_frame=game_frame;}
    if(n.silently_refuse_apply)return true;
    n.snapshot.document=doc;if(n.partially_apply&&!n.snapshot.document.placements.empty())n.snapshot.document.placements.begin()->second.position.x+=1;
    n.snapshot.bytes=body(n.snapshot.document,artifact&&!artifact->bytes.empty()?artifact->bytes.front():n.snapshot.bytes.empty()?0xA5:n.snapshot.bytes.front());bind_native(n);return true;}
bool enter(const editor::ResourceKey& key,std::string&){auto& n=native[side];++n.enters;n.state.editing=n.state.ready=true;n.snapshot.source=key;return true;}
bool select_placement(editor::ObjectId id,std::string&){CHECK(!dispatching);auto& n=native[side];if(!n.state.ready||n.state.gesture||
    (!n.snapshot.document.placements.contains(id) && !(id==kCaptainObject && n.snapshot.document.settings.contains(id))))return false;
    n.state.selected={id};if(n.focus_on_selection)n.view.target[0]+=20;++n.selections;return true;}
bool select_act(uint32_t act,std::string&){CHECK(!dispatching);auto& n=native[side];if(!n.state.ready||n.state.gesture)return false;n.state.act=act;n.snapshot.act=act;return true;}
bool selection_pose(editor::ObjectId& id,editor::ObjectId& act,editor::Pose& pose){CHECK(!dispatching);auto& n=native[side];
    if(!n.state.ready||!n.state.gesture||n.state.selected.empty())return false;auto found=n.snapshot.document.placements.find(n.state.selected.front());
    if(found==n.snapshot.document.placements.end())return false;id=found->first;act=found->second.act_id;pose={found->second.position,found->second.rotation,found->second.scale};return true;}
bool selection_preview(LivePreview& value){
    if(!selection_pose(value.object,value.act,value.pose))return false;
    value.class_id=native[side].snapshot.document.placements.at(value.object).class_id;return true;
}
void update_previews(const std::vector<LivePreview>&){CHECK(!dispatching);}
std::string visual_status(){CHECK(!dispatching);return "controlled visual status";}
std::string authoring_status(){CHECK(!dispatching);return "controlled authoring status";}
std::string status(){return "fake native";}
}
namespace editor_native_save {
bool init(const SkHost*,SkMod*,Ctx ctx){native[side].save_callbacks=std::move(ctx);return true;}
void shutdown(){}void tick(){}bool available(){return true;}bool busy(){return native[side].save_busy;}
bool save_as(std::string&){++native[side].save_as_calls;native[side].save_busy=true;return true;}
bool save(std::string&){++native[side].save_calls;native[side].save_busy=true;return true;}
bool read_saved(const editor::ResourceKey&,std::vector<uint8_t>& bytes,std::string&){CHECK(!dispatching);bytes=native[side].persisted_bytes.value_or(native[side].snapshot.bytes);return true;}
bool playtest(bool on,std::string&){CHECK(!dispatching);auto& n=native[side];if(on){n.state.playtesting=true;n.state.editing=n.state.ready=false;}
    else{CHECK(!n.cleanup);++n.stops;n.state.playtesting=false;n.state.editing=true;n.state.ready=n.return_ready;n.state.selected.clear();n.view.target[0]+=100;n.view.orbit[1]=0;if(n.bump_generation_on_stop)++n.state.generation;}return true;}
bool request_exit(std::string&){CHECK(!dispatching);native[side].save_callbacks.exit_confirmed();return true;}
bool complete_exit(std::string&){CHECK(!dispatching);if(!native[side].deferred_native_exit){native[side].state.ready=native[side].state.editing=false;++native[side].state.generation;}return true;}
bool restore_identity(editor::ResourceKey source,editor_native::Snapshot::Dirty dirty,std::string&){CHECK(!dispatching);native[side].snapshot.source=source;native[side].snapshot.local_dirty=dirty;return true;}
}
namespace adventure_assets {bool register_records(const std::vector<dbpf::Record>& records,std::string&){CHECK(!dispatching);++native[side].registrations;
    if(creation_content_enabled){for(const auto& row:records)if(row.key.type==0x1A99B06B){
        auto& saved=creation_records[side];auto found=std::find_if(saved.begin(),saved.end(),[&](const auto& old){return old.key==row.key;});
        if(found==saved.end())saved.push_back(row);else CHECK(found->data==row.data);
    }}
    return true;}std::string status(){return "fixture registered=0";}}
namespace adventure_collect {
bool unused_snapshot_key(const dbpf::Key&){return true;}
bool validate_uploaded_records(const dbpf::Key&,const std::vector<dbpf::Record>&,std::string& error){
    if(!upload_collision[side])return true;
    error="An uploaded asset conflicts with an installed resource: fixture";return false;
}
Presentation presentation(const dbpf::Key&){return {"Native draft","Fixture",{}};}
bool collect_snapshot(const dbpf::Key& key,const std::vector<uint8_t>& bytes,std::vector<dbpf::Record>& records,std::string&){records={{key,bytes}};
    size_t left=dependency_bytes;uint32_t id=500;
    while(left){size_t size=std::min<size_t>(left,12u<<20);records.push_back({{id++,0x12345678,0x408A0000},std::vector<uint8_t>(size,uint8_t(id))});left-=size;}
    if(creation_content_enabled)for(const auto& row:creation_records[side])records.push_back(row);
    return true;}
}
namespace editor_creation {
std::array<Ctx,2> fixture_contexts;
bool init(const SkHost*,SkMod*,Ctx context){fixture_contexts[side]=std::move(context);creation_active[side]=false;creation_records[side].clear();return true;}
void shutdown(){fixture_contexts[side]={};creation_active[side]=false;}void tick(){}bool active(){return creation_active[side];}bool designing(){return active();}
bool open(uint32_t,const editor::ResourceKey&,std::optional<editor::ObjectId>,std::string& error){error="Creation UI fixture is not configured";return false;}
std::string diagnostic(){return "controlled creation session";}
}
namespace editor_creation_native {
std::optional<editor::ObjectId> selected_cast(){return {};}
bool resource_reserved(const editor::ResourceKey&){return false;}
std::optional<editor::ObjectId> resolve_cast_asset(const editor::Document& d,const editor::ResourceKey& key){
    for(const auto& [id,row]:d.class_settings){(void)id;const auto value=row.properties.find(editor::kClassAssetResourceProperty);
        if(value!=row.properties.end() && value->second==editor::Value{key})return row.class_id;}return {};
}
bool collect_version(const editor::ProjectId&,const editor::ResourceKey& saved,const std::function<bool(const editor::ResourceKey&)>& reserved,
                     editor::ResourceKey& version,std::vector<dbpf::Record>& records,std::string& error){
    CHECK(!dispatching);if(!creation_content_enabled){error="Creation version fixture is not configured";return false;}
    version={0x7D000000u|creation_version++,saved.type,saved.group};CHECK(!reserved(version));
    records={{{version.instance,0x1A99B06B,version.group},{0x42,uint8_t(saved.instance)}}};error.clear();return true;
}
bool prepare_cast_save(const editor_native::Snapshot& base,const editor::ResourceKey& saved,std::optional<editor::ObjectId> target,editor_native::Snapshot& out,std::string& error){
    CHECK(!dispatching);if(!creation_content_enabled){error="Creation cast fixture is not configured";return false;}out=base;
    auto& d=out.document;
    if(target){
        bool found=false;for(auto& [id,row]:d.class_settings)if(row.class_id==*target){(void)id;row.properties[editor::kClassAssetResourceProperty]=saved;found=true;}
        if(!found){error="Missing fixture content target";return false;}
    }else{
        editor::ClassActSettings row;row.object={(5ull<<32)|d.next_class,1};row.class_id={(2ull<<32)|d.next_class++,1};row.act_id=*d.acts.begin();
        row.properties[editor_native::kCastBlockProperty]=editor::StructuredValue{editor_native::kCastBlockFormat,{17},{}};
        row.properties[editor::kClassAssetResourceProperty]=saved;d.classes.insert(row.class_id);d.class_settings[row.object]=row;
    }
    out.bytes=body(d,base.bytes.front());error.clear();return true;
}
}
namespace adventure_native {
bool set_editor_preview(EditorPreview value,std::string&){CHECK(!dispatching);CHECK(!native[side].preview);native[side].preview=std::move(value);return true;}
bool editor_preview(){return native[side].preview.has_value();}
bool editor_preview_matches(const std::array<uint8_t,16>& project,uint64_t epoch,uint64_t round,uint64_t revision){
    const auto& p=native[side].preview;return p&&p->project==project&&p->epoch==epoch&&p->round==round&&p->revision==revision;}
bool editor_preview_owner(){CHECK(!dispatching);return native[side].preview&&native[side].preview_owned;}
void clear_editor_preview(){native[side].preview.reset();}
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
static void test_guest_end_during_unlaunched_preparation_is_retryable() {
    Party party;std::string error;const auto canonical=runtime_host::current_native.bytes;
    side=0;CHECK(runtime_host::controller->prepare_playtest(error));
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Preparing&&runtime_guest::controller->view().phase==es::Phase::Preparing;}));
    CHECK(!native[0].preview&&!native[1].preview&&!native[0].active&&!native[1].active);
    side=1;runtime_guest::end();
    CHECK(spin([]{return !runtime_guest::closing&&runtime_guest::controller->view().phase==es::Phase::Offered&&runtime_host::controller->view().phase==es::Phase::Ready;}));
    CHECK(sessions[0].players().size()==2&&sessions[1].connected());
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen&&runtime_host::current_native.bytes==canonical);
    CHECK(native[0].cancels==0&&native[1].cancels==0&&!native[0].preview&&!native[1].preview);
    side=1;CHECK(runtime_guest::controller->accept_offer(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Ready&&runtime_host::controller->settled();}));
    CHECK(runtime_guest::current_native.bytes==canonical&&runtime_guest::store.healthy());
    side=0;CHECK(runtime_host::controller->prepare_playtest(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Preparing;}));
    CHECK(!runtime_host::controller->view().members.at(0).ready&&!runtime_host::controller->view().members.at(1).ready);
}
static void test_drag_preview_bridge_never_authors_remote_state() {
    Party party;std::string error;
    const auto canonical=runtime_host::current_native.bytes,guest_native=runtime_guest::current_native.bytes,guest_live=native[1].snapshot.bytes;
    const auto host_checkpoint=runtime_host::store.snapshot(),guest_checkpoint=runtime_guest::store.snapshot();
    side=0;CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Placement,placement,{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    native[0].state.selected={placement};native[0].state.gesture=true;
    native[0].snapshot.document.placements.at(placement).position={12,13,14};
    native[0].snapshot.document.placements.at(placement).scale=2;
    CHECK(spin([]{return runtime_guest::transform_hints().size()==1;}));
    const auto hint=runtime_guest::transform_hints().front();
    CHECK(hint.position.x==12&&hint.position.y==13&&hint.position.z==14&&hint.scale==2);
    CHECK(hint.label.find("Host")!=std::string::npos&&hint.label.find("moving")!=std::string::npos);
    CHECK(!runtime_guest::selection_hints().empty()&&runtime_guest::selection_hints().front().locked);
    CHECK(runtime_host::transform_hints().empty());
    CHECK(native[1].snapshot.bytes==guest_live&&runtime_guest::current_native.bytes==guest_native);
    CHECK(runtime_host::current_native.bytes==canonical&&runtime_host::controller->state().document.revision==0&&runtime_guest::controller->state().document.revision==0);
    CHECK(runtime_host::store.snapshot()==host_checkpoint&&runtime_guest::store.snapshot()==guest_checkpoint);
    const auto& guest_view=runtime_guest::view();auto row=std::find_if(guest_view.players.begin(),guest_view.players.end(),[](const auto& player){return player.host;});
    CHECK(row!=guest_view.players.end()&&row->owns_selection&&row->status=="Editing owned placement; Act 1");
    native[0].state.gesture=false;native[0].snapshot.document=runtime_host::controller->state().document;
    CHECK(spin([]{return runtime_guest::transform_hints().empty();}));
    CHECK(runtime_host::store.snapshot()==host_checkpoint&&runtime_guest::store.snapshot()==guest_checkpoint);
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
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
    simulate_host_saved(native[0].snapshot.source);CHECK(runtime_host::save_publication_pending&&!runtime_host::frozen);
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
    native[1].rebaseline_ready=false;runtime_guest::recover();
    CHECK(spin([] {return runtime_guest::recovery&&runtime_guest::recovery->restored;}));
    CHECK(!native[1].save_as_calls&&!native[1].save_calls&&native[1].snapshot.source==old_source);
    native[1].rebaseline_ready=true;
    CHECK(spin([] {return runtime_guest::controller->view().phase==es::Phase::Ready;}));
    CHECK(!runtime_guest::native_copy_owned&&!runtime_guest::controller->view().saved);
    // Recovery itself remains private. Its first explicit native Save must copy.
    side=1;runtime_guest::save();CHECK(spin([]{return native[1].save_busy;}));
    CHECK(native[1].save_as_calls==1&&!native[1].save_calls);
    const editor::ResourceKey copied{202,original_key.type,original_key.group};native[1].snapshot.source=copied;
    native[1].persisted_bytes=body(native[1].snapshot.document,0xE1);native[1].save_busy=false;native[1].save_callbacks.saved(copied);
    CHECK(spin([]{return runtime_guest::controller->saved_coverage().current;}));
    CHECK(runtime_guest::resources.root==dbpf::Key{copied.instance,copied.type,copied.group});
    CHECK(runtime_guest::current_native.source==copied&&runtime_guest::store.healthy()&&runtime_guest::native_copy_owned);
    CHECK(runtime_guest::current_native.bytes.front()==0xE1&&old_source!=copied);
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
    CHECK(spin([]{return runtime_guest::controller->state().document.revision>=2;}));
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
        editor::Mutation mutation;mutation.kind=editor::MutationKind::Create;mutation.initial.class_id=class_id;mutation.initial.act_id=act_id;mutation.initial.properties[editor_native::kRelativeScaleProperty]=.5f;
        mutation.initial.position={float(edit+1),0,0};request.mutations={mutation};
        side=0;CHECK(runtime_host::controller->submit(request,error));
        CHECK(spin([&]{return runtime_guest::controller->state().document.revision==edit+1;},true,30000));
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);CHECK(runtime_host::resource_hash==base_hash&&runtime_guest::resource_hash==guest_hash);
        CHECK(runtime_guest::resources.bundle.records.size()==3);
        CHECK(upd::sha256_hex(*runtime_host::resources.base_root)==native_base_hash&&upd::sha256_hex(*runtime_guest::resources.base_root)==native_base_hash);
    }
    side=0;native[0].snapshot.source={saved_key.instance,saved_key.type,saved_key.group};native[0].snapshot.bytes=body(native[0].snapshot.document,0x6C);
    simulate_host_saved(native[0].snapshot.source);CHECK(!runtime_host::frozen);
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
    side=0;runtime_host::recover();CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready&&!runtime_host::recovery;},true,30000));
    CHECK(native[0].state.generation==runtime_host::native_generation);
    CHECK(runtime_host::current_native.source==expected.source&&runtime_host::current_native.bytes==expected.bytes);
    CHECK(runtime_host::current_native.bindings==bindings&&runtime_host::resources.bundle.records.size()==3);
    CHECK(runtime_host::resources.root==saved_key&&runtime_host::resource_hash==base_hash);
    CHECK(upd::sha256_hex(*runtime_host::resources.base_root)==native_base_hash);
    for(const auto& record:runtime_host::resources.bundle.records)if(record.key==saved_key)CHECK(record.data==expected.bytes);
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Offered&&!runtime_guest::closing&&sessions[0].pending_to(1)==0&&sessions[1].pending_to(0)==0;},true,30000));
    const auto rejoin_host=sent(0),rejoin_guest=sent(1);
    side=1;CHECK(runtime_guest::controller->accept_offer(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Ready&&runtime_host::controller->settled()&&sessions[0].pending_to(1)==0&&sessions[1].pending_to(0)==0;},true,30000));
    const auto rejoin_bytes=sent(0)-rejoin_host,inventory_bytes=sent(1)-rejoin_guest;
    CHECK(rejoin_bytes<(1u<<20)&&inventory_bytes<(1u<<20));
    CHECK(runtime_guest::current_native.bytes==expected.bytes&&runtime_guest::current_native.bindings==bindings);
    CHECK(runtime_guest::resources.bundle.records.size()==3&&runtime_guest::store.healthy());
    CHECK(upd::sha256_hex(*runtime_guest::resources.base_root)==native_base_hash);
    std::printf("runtime cached join: host_bytes=%llu guest_bytes=%llu (24 MiB dependency closure retained)\n",
        static_cast<unsigned long long>(rejoin_bytes),static_cast<unsigned long long>(inventory_bytes));
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
    const auto state_before_audit=runtime_host::controller->state();
    const auto durable_before_audit=runtime_host::store.snapshot();
    const auto page=runtime_host::debug({"inspect","bytes","0","4096"});
    const auto page_count=std::min(size_t(4096),before.size());
    CHECK(page.starts_with("NATIVE_BYTES size="+std::to_string(before.size())+" offset=0 count="+std::to_string(page_count)+" sha256="+upd::sha256_hex(before)+"\nhex="));
    CHECK(page.size()<12u*1024);
    const auto hex_start=page.find("\nhex=")+5;
    CHECK(page.size()-hex_start==page_count*2+1);
    const char* hex="0123456789abcdef";
    for(size_t i=0;i<page_count;++i){CHECK(page[hex_start+i*2]==hex[before[i]>>4]);CHECK(page[hex_start+i*2+1]==hex[before[i]&15]);}
    const auto tail=runtime_host::debug({"inspect","bytes",std::to_string(before.size()),"4096"});
    CHECK(tail.ends_with("\nhex=\n")&&tail.find(" count=0 ")!=std::string::npos);
    CHECK(runtime_host::debug({"inspect","bytes",std::to_string(before.size()+1),"1"}).starts_with("NATIVE_BYTES_REJECTED"));
    for(const auto& args:std::vector<std::vector<std::string>>{{"inspect","bytes","0","0"},{"inspect","bytes","0","4097"},{"inspect","bytes","-1","1"},{"inspect","bytes","134217729","1"},{"inspect","bytes","42949672960","1"},{"inspect","bytes","x","1"},{"inspect","bytes","0","1","extra"}})
        CHECK(runtime_host::debug(args).starts_with("Usage:"));
    CHECK(native[0].snapshot.bytes==before&&native[0].restores==restores);
    CHECK(runtime_host::controller->state()==state_before_audit&&runtime_host::store.snapshot()==durable_before_audit);
    CHECK(native[0].save_callbacks.shared_active&&native[0].save_callbacks.shared_active());
    native[0].snapshot.captain={};runtime_host::refresh();CHECK(!runtime_host::view().can_playtest);
    CHECK(runtime_host::view().status.find("authored adventure captain")!=std::string::npos);
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
    side=0;runtime_host::undo();runtime_host::begin();runtime_host::recover();step();CHECK(runtime_host::closing&&native[0].rebaselines==0);
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
    side=0;runtime_host::end();step();CHECK(native[0].rebaselines==1);
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
    editor::Mutation m;m.kind=editor::MutationKind::Create;m.initial.class_id=class_id;m.initial.act_id=act_id;m.initial.properties[editor_native::kRelativeScaleProperty]=.5f;m.initial.position={4,5,6};request.mutations={m};
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
    simulate_host_saved(native[0].snapshot.source);runtime_host::end();step(false);
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
    Party party;runtime_host::confirmation_metrics.reset();const auto frame=capture_local_create();const auto applies=native[0].structural_applies;CHECK(applies==1);
    for(unsigned i=0;i<8;++i)same_frame_drain(0);
    CHECK(game_frame==frame&&runtime_host::pending&&!runtime_host::pending->submitted);
    CHECK(native[0].structural_applies==applies&&runtime_host::controller->state().document.revision==0);
    side=0;CHECK(!native[0].callbacks.structure_gate(1024));
    ++game_frame;same_frame_drain(0);
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1&&!runtime_host::pending&&!runtime_host::awaiting_operation;},false));
    CHECK(runtime_host::controller->state().document.revision==1&&!runtime_host::pending&&!runtime_host::awaiting_operation);
    CHECK(native[0].structural_applies==applies+1&&native[0].structural_frame>frame);
    CHECK(native[0].state.selected.size()==1&&native[0].state.selected.front()!=placement);
    CHECK(runtime_host::controller->state().document.placements.contains(native[0].state.selected.front()));
    CHECK(native[1].state.selected.empty());
    CHECK(runtime_host::confirmation_metrics.report().starts_with("samples=1 "));
    CHECK(runtime_guest::confirmation_metrics.report().starts_with("samples=0 "));
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
    // The immutable base is the unsaved live draft; a subsequent native save
    // enriches current bytes without replacing that delta reconstruction base.
    CHECK(runtime_host::resources.base_root->front()==0xA5&&runtime_guest::resources.base_root->front()==0xA5);
    side=1;CHECK(runtime_guest::controller->request_leases({{editor::ScopeKind::Field,placement,{editor::Field::Position,0}}},15000,error));
    CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
    runtime_guest::guarded_placements[placement]=runtime_guest::controller->state().document.placements.at(placement);
    editor_native::Gesture gesture;gesture.generation=native[1].state.generation;gesture.before=native[1].snapshot;gesture.after=gesture.before;
    gesture.after.document.placements.at(placement).position={6,7,8};gesture.after.bytes=body(gesture.after.document,gesture.before.bytes.front());
    native[1].snapshot=gesture.after;side=1;runtime_guest::captured(std::move(gesture));
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1&&runtime_guest::controller->state().document.revision==1;}));
    CHECK(native[0].snapshot.bytes.front()==0xC1&&native[1].snapshot.bytes.front()==0xC1);
    side=0;native[0].persisted_bytes=body(runtime_host::controller->state().document,0xD1);
    simulate_host_saved(native[0].snapshot.source);
    CHECK(!runtime_host::frozen&&native[0].snapshot.bytes.front()==0xD1&&runtime_host::current_native.bytes.front()==0xD1);
    CHECK(spin([]{return runtime_guest::current_native.bytes.front()==0xD1&&runtime_host::controller->settled();}));
    CHECK(native[1].snapshot.bytes.front()==0xD1&&runtime_guest::store.snapshot().native_snapshot.front()==0xD1);
    const auto old_epoch=runtime_host::controller->state().document.epoch;
    native[0].snapshot.bytes=body(seed(),0x11);side=0;runtime_host::recover();
    CHECK(spin([&]{return !runtime_host::recovery&&runtime_host::controller->state().document.epoch!=old_epoch;}));
    CHECK(native[0].snapshot.bytes.front()==0xD1&&runtime_host::current_native.bytes.front()==0xD1);
    CHECK(runtime_host::resources.base_root->front()==0xA5&&runtime_host::store.snapshot().native_snapshot.front()==0xD1);
}
static void test_invalid_completed_save_does_not_replace_live_draft() {
    Party party;side=0;const auto before=native[0].snapshot.bytes;const auto restored=native[0].restores;
    auto invalid=runtime_host::controller->state().document;invalid.placements.at(placement).position={99,0,0};
    native[0].persisted_bytes=body(invalid,0xE3);simulate_host_saved(native[0].snapshot.source);
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
    CHECK(!runtime_host::frozen&&!runtime_host::pending&&!runtime_host::awaiting_operation);
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
    CHECK(!runtime_host::frozen&&native[0].restores==restored+1&&native[0].snapshot.bytes==canonical);
    CHECK(!native[0].save_calls&&!native[0].save_as_calls&&!native[0].preview&&!native[0].active);
    CHECK(runtime_host::controller->state().document.revision==0&&runtime_host::store.snapshot().native_snapshot==canonical);}
}
static void test_identical_native_snapshot_validation() {
    Party party;side=0;std::string error;
    const auto canonical=native[0].snapshot;
    const auto wanted=runtime_host::controller->state().document;
    auto validates=native[0].validations,opaque=native[0].opaque_checks;
    CHECK(runtime_host::native_integrity(canonical,wanted,error));
    CHECK(native[0].validations==validates+1&&native[0].opaque_checks==opaque);
    // Identical bytes still need complete projection validation before the
    // opaque comparison can be omitted.
    auto mismatch=wanted;mismatch.placements.at(placement).position.x+=1;
    CHECK(!runtime_host::native_integrity(canonical,mismatch,error));
    CHECK(native[0].opaque_checks==opaque);
    auto malformed=canonical;malformed.bytes.resize(1);
    CHECK(!runtime_host::native_integrity(malformed,wanted,error));
    CHECK(native[0].opaque_checks==opaque);
    auto changed=canonical;changed.bytes.front()^=1;
    CHECK(!runtime_host::native_integrity(changed,wanted,error));
    CHECK(native[0].opaque_checks==++opaque);
    // The stub accepts these projections, so count the full opaque guard to
    // verify that equal bytes alone never bypass changed bindings or act.
    changed=canonical;++changed.bindings.front().marker;
    runtime_host::native_integrity(changed,wanted,error);
    CHECK(native[0].opaque_checks==++opaque);
    changed=canonical;++changed.act;
    runtime_host::native_integrity(changed,wanted,error);
    CHECK(native[0].opaque_checks==++opaque);
    CHECK(native[0].snapshot.bytes==canonical.bytes&&!runtime_host::frozen);
}
static void test_host_apply_rejects_uncaptured_opaque_changes() {
    test_identical_native_snapshot_validation();
    Party party;std::string error;side=1;
    CHECK(runtime_guest::controller->request_leases({{editor::ScopeKind::Field,placement,{editor::Field::Position,0}}},15000,error));
    CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
    editor::Request request;request.summary="Move while host has an unsupported uncaptured change";request.base_revision=0;
    for(const auto& held:runtime_guest::controller->view().local_leases)request.leases.push_back({held.scope,held.token});
    editor::Mutation mutation;mutation.target=placement;mutation.field={editor::Field::Position,0};mutation.value=editor::Vec3{8,8,8};request.mutations={mutation};
    const auto canonical=runtime_host::current_native.bytes;native[0].snapshot.bytes.front()=0xB4;
    side=1;CHECK(runtime_guest::controller->submit(request,error));CHECK(spin([]{return runtime_guest::status.find("unsupported")!=std::string::npos;}));
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    CHECK(native[0].snapshot.bytes==canonical&&runtime_host::controller->state().document.revision==0);
    CHECK(runtime_host::store.snapshot().native_snapshot==canonical);
}
static void test_host_native_success_without_projection_cannot_commit() {
    for(unsigned partial=0;partial<2;++partial){Party party;std::string error;
    side=1;CHECK(runtime_guest::controller->request_leases({{editor::ScopeKind::Document,{},{}}},15000,error));
    CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
    const auto checkpoint=runtime_host::store.snapshot(),guest_checkpoint=runtime_guest::store.snapshot();
    const auto host_before=native[0].snapshot.bytes,guest_before=native[1].snapshot.bytes;const auto restored=native[0].restores;
    editor::Request request;request.summary="Native reports success without completing the mutation";request.base_revision=0;
    for(const auto& held:runtime_guest::controller->view().local_leases)request.leases.push_back({held.scope,held.token});
    editor::Mutation mutation;mutation.target=placement;
    if(partial){mutation.field={editor::Field::Position,0};mutation.value=editor::Vec3{3,4,5};native[0].partially_apply=true;}
    else{mutation.kind=editor::MutationKind::Delete;native[0].silently_refuse_apply=true;}
    request.mutations={mutation};side=1;CHECK(runtime_guest::controller->submit(request,error));
    CHECK(spin([]{return runtime_guest::status.find("Injected persisted")!=std::string::npos;}));
    CHECK(runtime_host::controller->state().document.revision==0&&runtime_guest::controller->state().document.revision==0);
    CHECK(runtime_host::store.snapshot()==checkpoint&&runtime_guest::store.snapshot()==guest_checkpoint);
    CHECK(runtime_host::store.healthy()&&runtime_guest::store.healthy()&&!runtime_host::frozen&&!runtime_guest::frozen);
    CHECK(native[0].restores==restored+1&&native[0].snapshot.bytes==host_before&&native[1].snapshot.bytes==guest_before);
    CHECK(runtime_host::controller->state().history.empty()&&runtime_guest::controller->state().history.empty());
    native[0].silently_refuse_apply=native[0].partially_apply=false;}
}
static void test_guest_native_success_without_projection_freezes_only_replica() {
    Party party;std::string error;side=0;
    CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Document,{},{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    const auto guest_checkpoint=runtime_guest::store.snapshot();const auto guest_before=native[1].snapshot.bytes;const auto restored=native[1].restores;
    editor::Request request;request.summary="Durable host delete that the guest native API silently refuses";request.base_revision=0;
    for(const auto& held:runtime_host::controller->view().local_leases)request.leases.push_back({held.scope,held.token});
    editor::Mutation mutation;mutation.kind=editor::MutationKind::Delete;mutation.target=placement;request.mutations={mutation};
    native[1].silently_refuse_apply=true;side=0;CHECK(runtime_host::controller->submit(request,error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Frozen;}));
    CHECK(runtime_host::controller->state().document.revision==1&&runtime_host::store.durable_revision()==1);
    CHECK(runtime_host::controller->state().document.placements.empty()&&native[0].snapshot.document.placements.empty());
    CHECK(!runtime_host::frozen&&runtime_host::store.healthy());
    CHECK(runtime_guest::controller->state().document.revision==0&&runtime_guest::store.snapshot()==guest_checkpoint);
    CHECK(runtime_guest::store.healthy()&&native[1].restores==restored+1&&native[1].snapshot.bytes==guest_before);
    native[1].silently_refuse_apply=false;
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
static void test_history_entry_rechecks_actual_native_holds() {
    Party party;std::string error;capture_local_create();++game_frame;same_frame_drain(0);
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&runtime_host::controller->settled();}));
    side=0;CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Document,{},{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    const auto state=runtime_host::controller->state();const auto checkpoint=runtime_host::store.snapshot();
    const auto bytes=native[0].snapshot.bytes;const auto sequence=runtime_host::sequence;const auto restores=native[0].restores;
    auto unchanged=[&]{
        CHECK(!runtime_host::awaiting_operation&&!runtime_host::pending&&runtime_host::sequence==sequence);
        CHECK(runtime_host::controller->state()==state&&runtime_host::store.snapshot()==checkpoint);
        CHECK(native[0].snapshot.bytes==bytes&&native[0].restores==restores);
    };
    native[0].state.gesture=true;side=0;runtime_host::undo_redo(false);unchanged();native[0].state.gesture=false;
    native[0].save_busy=true;side=0;runtime_host::undo_redo(false);unchanged();native[0].save_busy=false;
    native[0].state.ready=false;side=0;runtime_host::undo_redo(false);unchanged();native[0].state.ready=true;
    native[0].state.editing=false;side=0;runtime_host::undo_redo(false);unchanged();native[0].state.editing=true;
    ++native[0].state.generation;side=0;runtime_host::undo_redo(false);unchanged();--native[0].state.generation;
    runtime_host::recovery.emplace();side=0;runtime_host::undo_redo(false);unchanged();runtime_host::recovery.reset();
    runtime_host::save_publication_pending=true;side=0;runtime_host::undo_redo(false);unchanged();runtime_host::save_publication_pending=false;
    side=0;runtime_host::undo_redo(false);CHECK(runtime_host::awaiting_operation);
    CHECK(spin([]{return runtime_host::controller->state().document.revision==2&&runtime_guest::controller->state().document.revision==2&&!runtime_host::awaiting_operation;}));
    CHECK(!runtime_host::controller->state().history.back().applied&&!runtime_host::frozen);
    side=0;CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Document,{},{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    const auto undone=runtime_host::controller->state();const auto redo_sequence=runtime_host::sequence;
    native[0].state.gesture=true;side=0;runtime_host::undo_redo(true);
    CHECK(!runtime_host::awaiting_operation&&runtime_host::sequence==redo_sequence&&runtime_host::controller->state()==undone);
    native[0].state.gesture=false;side=0;runtime_host::undo_redo(true);CHECK(runtime_host::awaiting_operation);
    CHECK(spin([]{return runtime_host::controller->state().document.revision==3&&runtime_guest::controller->state().document.revision==3&&!runtime_host::awaiting_operation;}));
    CHECK(runtime_host::controller->state().history.back().applied&&!runtime_host::frozen);
}
static void test_preview_return_callbacks_are_scoped_to_one_round() {
    Party party;std::string error;
    auto prepare=[] {
        std::string error;side=0;CHECK(runtime_host::controller->prepare_playtest(error));
        CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Preparing&&runtime_guest::controller->view().phase==es::Phase::Preparing;}));
    };
    auto launch=[] {
        std::string error;side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));
        side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
        CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Playing&&runtime_guest::controller->view().phase==es::Phase::Playing&&native[0].state.playtesting&&native[1].state.playtesting;}));
    };
    auto returned=[] {
        CHECK(spin([]{
            for(unsigned p=0;p<2;++p)if(native[p].cleanup)complete_cleanup(p);
            return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;
        }));
        CHECK(!native[0].preview&&!native[1].preview&&!native[0].cleanup&&!native[1].cleanup);
    };
    prepare();launch();
    const auto old_host=native[0].preview->return_editor,old_guest=native[1].preview->return_editor;
    side=0;old_host();CHECK(runtime_host::pending_preview_return);
    const auto old_queued=*runtime_host::pending_preview_return;
    returned();
    const auto host_status=runtime_host::status,guest_status=runtime_guest::status;
    side=0;runtime_host::debug({"return"});old_host();native[0].save_callbacks.return_requested();
    side=1;runtime_guest::debug({"return"});old_guest();native[1].save_callbacks.return_requested();step();
    CHECK(runtime_host::status==host_status&&runtime_guest::status==guest_status);
    CHECK(!runtime_host::pending_preview_return&&!runtime_guest::pending_preview_return);
    prepare();
    side=0;old_host();side=1;old_guest();step();
    CHECK(runtime_host::controller->view().phase==es::Phase::Preparing&&runtime_guest::controller->view().phase==es::Phase::Preparing);
    CHECK(!runtime_host::pending_preview_return&&!runtime_guest::pending_preview_return);
    launch();
    const auto cancels0=native[0].cancels,cancels1=native[1].cancels;
    side=0;old_host();side=1;old_guest();
    CHECK(!runtime_host::pending_preview_return&&!runtime_guest::pending_preview_return);
    // A scoped request can also outlive its original queue turn. Dispatch must
    // revalidate it even when this fixture delays that owned work explicitly.
    runtime_host::pending_preview_return=old_queued;side=0;runtime_host::schedule();step();
    CHECK(!runtime_host::pending_preview_return);
    CHECK(runtime_host::controller->view().phase==es::Phase::Playing&&runtime_guest::controller->view().phase==es::Phase::Playing);
    CHECK(native[0].cancels==cancels0&&native[1].cancels==cancels1&&!native[0].cleanup&&!native[1].cleanup);
    side=1;native[1].preview->return_editor();step();
    CHECK(runtime_host::controller->view().phase==es::Phase::Playing&&runtime_guest::controller->view().phase==es::Phase::Playing);
    side=0;native[0].save_callbacks.return_requested();returned();
    CHECK(native[0].cancels==cancels0+1&&native[1].cancels==cancels1+1);
}
static void test_lease_diagnostic_is_bounded_and_read_only() {
    Party party;std::string error;side=0;
    CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Document,{},{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    const auto state=runtime_host::controller->state(),guest_state=runtime_guest::controller->state();
    const auto checkpoint=runtime_host::store.snapshot();const auto bytes=native[0].snapshot.bytes;
    const auto restores=native[0].restores;
    side=0;const auto diagnostic=runtime_host::debug({"leases"});
    CHECK(diagnostic.size()<=12u*1024&&diagnostic.find("lease scope=Document object=0:0 token=")!=std::string::npos);
    CHECK(diagnostic.find("remaining_ms=")!=std::string::npos&&diagnostic.find("member peer=1:")!=std::string::npos);
    CHECK(diagnostic.find("ownership=")!=std::string::npos&&diagnostic.find("pending=")!=std::string::npos&&diagnostic.find("preview_token=")!=std::string::npos);
    CHECK(runtime_host::controller->state()==state&&runtime_guest::controller->state()==guest_state&&runtime_host::store.snapshot()==checkpoint);
    CHECK(native[0].snapshot.bytes==bytes&&native[0].restores==restores);
}
static void test_preview_restores_latest_independent_selection_before_ack() {
    Party party;std::string error;capture_local_create();++game_frame;same_frame_drain(0);
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&runtime_host::controller->settled()&&!runtime_host::awaiting_operation;}));
    editor::ObjectId created{};for(const auto& [id,p]:runtime_host::controller->state().document.placements){(void)p;if(id!=placement)created=id;}
    CHECK(created.id);
    // These independent choices happen after the last authored gesture. They
    // cannot be recovered from the committed resource or another peer's state.
    side=0;CHECK(editor_native::select_placement(placement,error));
    side=1;CHECK(editor_native::select_placement(created,error));
    const auto selected0=native[0].selections,selected1=native[1].selections;
    native[0].view.target={500,11,12};native[0].view.orbit={.25f,.73f,61};
    native[1].view.target={500,21,22};native[1].view.orbit={-.25f,.47f,91};
    const auto view0=native[0].view,view1=native[1].view;
    const auto canonical=runtime_host::controller->state().document;
    const auto bytes0=runtime_host::current_native.bytes,bytes1=runtime_guest::current_native.bytes;
    native[0].bump_generation_on_stop=native[1].bump_generation_on_stop=true;
    auto launch=[] {
        std::string error;side=0;CHECK(runtime_host::controller->prepare_playtest(error));
        CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Preparing;}));
        side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));
        side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
        CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Playing&&runtime_guest::controller->view().phase==es::Phase::Playing&&native[0].state.playtesting&&native[1].state.playtesting;}));
    };
    auto settle_cleanup=[] {
        for(unsigned p=0;p<2;++p)if(native[p].cleanup)complete_cleanup(p);
    };
    launch();
    CHECK(runtime_host::preview_selection&&runtime_host::preview_selection->selected==placement);
    CHECK(runtime_guest::preview_selection&&runtime_guest::preview_selection->selected==created);
    const auto old_host=native[0].preview->return_editor,old_guest=native[1].preview->return_editor;
    // A delayed guest editor load must hold the group ACK. Its selected marker
    // is restored only after its own ready gate, even across native generation.
    native[1].return_ready=false;side=0;old_host();
    CHECK(spin([&]{settle_cleanup();return native[0].stops==1&&native[1].stops==1;}));
    CHECK(runtime_host::controller->view().phase==es::Phase::Returning&&runtime_guest::controller->view().phase==es::Phase::Returning);
    CHECK(native[0].state.selected==std::vector<editor::ObjectId>{placement});
    CHECK(native[1].state.selected.empty()&&native[0].selections==selected0+1&&native[1].selections==selected1);
    native[1].state.ready=true;native[1].view_ready=false;
    for(unsigned i=0;i<20;++i)step();
    CHECK(runtime_host::controller->view().phase==es::Phase::Returning&&native[1].view_restores==0);
    native[1].view_ready=true;
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;}));
    CHECK(native[1].state.selected==std::vector<editor::ObjectId>{created});
    CHECK(native[0].selections==selected0+1&&native[1].selections==selected1+1);
    CHECK(!runtime_host::preview_selection&&!runtime_guest::preview_selection);
    CHECK(native[0].view==view0&&native[1].view==view1&&native[0].view_restores==1&&native[1].view_restores==1);
    CHECK(runtime_host::native_generation==native[0].state.generation&&runtime_guest::native_generation==native[1].state.generation);
    CHECK(native[0].snapshot.bytes==bytes0&&native[1].snapshot.bytes==bytes1);
    CHECK(runtime_host::controller->state().document==canonical&&runtime_guest::controller->state().document==canonical);
    native[1].return_ready=true;
    side=0;old_host();side=1;old_guest();for(unsigned i=0;i<3;++i)step();
    CHECK(native[0].selections==selected0+1&&native[1].selections==selected1+1);
    side=0;CHECK(editor_native::select_placement(created,error));side=1;CHECK(editor_native::select_placement(placement,error));
    const auto second0=native[0].selections,second1=native[1].selections;
    launch();side=0;old_host();side=1;old_guest();step();
    CHECK(runtime_host::controller->view().phase==es::Phase::Playing&&runtime_guest::controller->view().phase==es::Phase::Playing);
    CHECK(native[0].selections==second0&&native[1].selections==second1);
    side=0;native[0].preview->return_editor();
    CHECK(spin([&]{settle_cleanup();return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;}));
    CHECK(native[0].state.selected==std::vector<editor::ObjectId>{created}&&native[0].selections==second0+1);
    CHECK(native[1].state.selected==std::vector<editor::ObjectId>{placement}&&native[1].selections==second1+1);
    CHECK(!runtime_host::preview_selection&&!runtime_guest::preview_selection);
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    side=0;CHECK(editor_native::select_placement(created,error));
    const auto cancelled0=native[0].selections,cancelled1=native[1].selections;
    launch();side=0;CHECK(runtime_host::controller->cancel_playtest(error));
    CHECK(spin([&]{settle_cleanup();return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;}));
    CHECK(native[0].state.selected==std::vector<editor::ObjectId>{created});
    CHECK(native[1].state.selected==std::vector<editor::ObjectId>{placement});
    CHECK(native[0].selections==cancelled0+1&&native[1].selections==cancelled1+1);
    CHECK(!runtime_host::preview_selection&&!runtime_guest::preview_selection);
}
static void test_preview_local_view_none_selected_and_stale_pin() {
    Party party;std::string error;
    native[0].state.selected.clear();native[1].state.selected.clear();
    native[0].view.target={500,41,42};native[0].view.orbit={.41f,.81f,141};
    native[1].view.target={500,51,52};native[1].view.orbit={.51f,.91f,151};
    const auto a=native[0].view,b=native[1].view;
    auto launch=[] {
        std::string error;side=0;CHECK(runtime_host::controller->prepare_playtest(error));
        CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Preparing;}));
        side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));
        side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
        CHECK(spin([]{return native[0].state.playtesting&&native[1].state.playtesting;}));
    };
    auto clean=[] {for(unsigned p=0;p<2;++p)if(native[p].cleanup)complete_cleanup(p);};
    launch();side=0;native[0].preview->return_editor();
    CHECK(spin([&]{clean();return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;}));
    CHECK(native[0].view==a&&native[1].view==b&&native[0].state.selected.empty()&&native[1].state.selected.empty());
    CHECK(native[0].selections==0&&native[1].selections==0);
    launch();CHECK(runtime_host::preview_selection);++runtime_host::preview_selection->scope.round;
    const auto restores=native[0].restores,views=native[0].view_restores;
    side=0;native[0].preview->return_editor();
    CHECK(spin([&]{clean();return runtime_host::controller->view().phase==es::Phase::Frozen||runtime_host::controller->view().phase==es::Phase::Idle;}));
    CHECK(native[0].restores>=restores&&native[0].view_restores==views);
    // A replacement native owner in the SAME logical round is different:
    // reject before module Stop, canonical restoration or a camera write.
}
static void test_preview_native_owner_replacement_never_writes_view() {
    Party party;std::string error;side=0;CHECK(runtime_host::controller->prepare_playtest(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Preparing;}));
    side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));
    side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
    CHECK(spin([]{return native[0].state.playtesting&&native[1].state.playtesting;}));
    const auto restores=native[0].restores,views=native[0].view_restores,stops=native[0].stops;
    ++native[0].state.generation;side=0;native[0].preview->return_editor();
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Frozen;}));
    CHECK(native[0].restores==restores&&native[0].view_restores==views&&native[0].stops==stops);
}
static void test_conflicting_native_history_releases_document_ownership() {
    Party party;std::string error;capture_local_create();++game_frame;same_frame_drain(0);
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1&&runtime_guest::controller->state().document.revision==1&&!runtime_host::awaiting_operation;}));
    editor::ObjectId created{};
    for(const auto& [id,value]:runtime_host::controller->state().document.placements){(void)value;if(id!=placement)created=id;}
    CHECK(created.id&&runtime_host::controller->state().history.size()==1);
    auto& host_controller=*runtime_host::controller;auto& guest_controller=*runtime_guest::controller;
    auto controller=[&](unsigned p)->es::Controller&{return p?guest_controller:host_controller;};
    auto sequence=[](unsigned p){return p?runtime_guest::sequence:runtime_host::sequence;};
    auto awaiting=[](unsigned p){return p?runtime_guest::awaiting_operation.has_value():runtime_host::awaiting_operation.has_value();};
    auto pending=[](unsigned p){return p?runtime_guest::pending.has_value():runtime_host::pending.has_value();};
    auto unleased=[&]{return host_controller.view().local_leases.empty()&&guest_controller.view().local_leases.empty();};
    auto move=[&](unsigned p,editor::Vec3 position){
        CHECK(spin(unleased));
        CHECK(spin([&]{side=int(p);return native[p].callbacks.gate({created});}));
        side=int(p);editor_native::Gesture gesture;gesture.generation=native[p].state.generation;
        gesture.before=native[p].snapshot;gesture.after=gesture.before;
        gesture.after.document.placements.at(created).position=position;gesture.after.bytes=body(gesture.after.document);
        native[p].snapshot=gesture.after;const auto before=host_controller.state().document.revision;
        const auto operation=sequence(p)+1;
        if(p)runtime_guest::captured(std::move(gesture));else runtime_host::captured(std::move(gesture));
        CHECK(pending(p)&&awaiting(p)&&sequence(p)==operation);
        CHECK(spin([&]{return host_controller.state().document.revision==before+1&&guest_controller.state().document.revision==before+1&&!awaiting(p)&&!pending(p);}));
        CHECK(spin(unleased));
        CHECK(native[0].snapshot.document.placements.at(created).position==position&&native[1].snapshot.document.placements.at(created).position==position);
    };
    auto undo=[&](unsigned p){
        const auto initial=sequence(p);side=int(p);native[p].callbacks.history(false);
        CHECK(spin([&]{return sequence(p)>initial||!controller(p).view().local_leases.empty();}));
        if(sequence(p)==initial){side=int(p);native[p].callbacks.history(false);}
        CHECK(spin([&]{return sequence(p)==initial+1&&!awaiting(p);}));
        CHECK(!pending(p)&&!runtime_host::frozen&&!runtime_guest::frozen);
        CHECK(spin(unleased));
    };
    const editor::Vec3 host_position{10,11,12},guest_position{20,21,22};
    move(0,host_position);move(1,guest_position);
    CHECK(host_controller.state().document.revision==3&&host_controller.state().history.size()==3);
    const auto before_rejected=host_controller.state().document;const auto history=host_controller.state().history;
    const auto checkpoint=runtime_host::store.snapshot();
    undo(0);
    CHECK(host_controller.state().document==before_rejected&&host_controller.state().history==history&&runtime_host::store.snapshot()==checkpoint);
    CHECK(runtime_host::status.find("no older action was selected")!=std::string::npos);
    undo(1);
    CHECK(spin([&]{return host_controller.state().document.revision==4&&guest_controller.state().document.revision==4;}));
    CHECK(host_controller.state().document.placements.at(created).position==host_position);
    CHECK(!host_controller.state().history.back().applied&&host_controller.state().history.back().redo_available);
    const auto restored=host_controller.state().document;const auto restored_history=host_controller.state().history;
    // Another author's Undo restores the value, but does not rewrite A's stale
    // field precondition or permit skipping A's latest original Move.
    undo(0);
    CHECK(host_controller.state().document==restored&&host_controller.state().history==restored_history);
    CHECK(runtime_host::status.find("no older action was selected")!=std::string::npos);
    // One further native Undo retains that exact request through its lease
    // latency, rejects its stale field precondition, and releases the scope.
    const auto abandoned_sequence=runtime_host::sequence;
    side=0;native[0].callbacks.history(false);
    CHECK(spin([&]{return runtime_host::sequence==abandoned_sequence+1&&!awaiting(0)&&!runtime_host::waiting_history&&unleased();}));
    CHECK(!pending(0)&&host_controller.state().document==restored);
    const auto idle=clock_ms;
    while(clock_ms-idle<16000){clock_ms+=195;step();}
    CHECK(unleased()&&!pending(0)&&!pending(1)&&!awaiting(0)&&!awaiting(1));
    CHECK(!native[0].state.gesture&&!native[1].state.gesture&&!runtime_host::frozen&&!runtime_guest::frozen);
    CHECK(host_controller.view().phase==es::Phase::Ready&&guest_controller.view().phase==es::Phase::Ready);
    CHECK(host_controller.state().document==restored&&guest_controller.state().document==restored);
    const editor::Scope document_scope{editor::ScopeKind::Document,{},{}};
    side=1;CHECK(guest_controller.request_leases({document_scope},15000,error));
    CHECK(spin([&]{return std::any_of(guest_controller.view().local_leases.begin(),guest_controller.view().local_leases.end(),[&](const auto& held){return held.scope==document_scope&&held.owner==guest_controller.view().local&&held.expires>clock_ms;});}));
    std::vector<editor::LeaseToken> tokens;for(const auto& held:guest_controller.view().local_leases)tokens.push_back({held.scope,held.token});
    side=1;CHECK(guest_controller.release_leases(tokens,error));CHECK(spin(unleased));
    // Release quarantines the local token immediately. Complete its reliable
    // delivery and host controller turn before this direct cross-peer handoff;
    // an empty local View alone no longer means the host has released it.
    const auto release_started=clock_ms;step();
    CHECK(spin([]{return sessions[1].pending_to(0)==0;}));step();
    CHECK(clock_ms-release_started<15000);
    side=0;CHECK(host_controller.request_leases({document_scope},15000,error));
    CHECK(spin([&]{return std::any_of(host_controller.view().local_leases.begin(),host_controller.view().local_leases.end(),[&](const auto& held){return held.scope==document_scope&&held.owner==host_controller.view().local&&held.expires>clock_ms;});}));
    CHECK(!pending(0)&&!pending(1)&&!awaiting(0)&&!awaiting(1));
}
static void test_delayed_lease_grant_gets_observed_idle_grace() {
    Party party;
    side=1;CHECK(!native[1].callbacks.gate({placement}));
    const auto requested=runtime_guest::last_lease_use;
    runtime_guest::tick(now_seconds());run_tasks(1);
    auto transport_only=[] {
        for(unsigned i=0;i<8;++i){network();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    };
    transport_only();clock_ms+=6000;
    side=0;runtime_host::tick(now_seconds());run_tasks(0);transport_only();
    runtime_guest::last_renew=clock_ms-4001;
    side=1;runtime_guest::tick(now_seconds());run_tasks(1);
    CHECK(runtime_guest::controller->view().local_leases.size()==1);
    CHECK(runtime_guest::last_lease_use==clock_ms&&clock_ms-requested>=6000);
    const auto granted=runtime_guest::controller->view().local_leases.front();
    CHECK(granted.expires>clock_ms&&runtime_guest::observed_lease_grants.size()==1);
    CHECK(native[1].callbacks.gate({placement}));
    for(unsigned i=0;i<8;++i)step();
    CHECK(runtime_guest::controller->view().local_leases.size()==1);
    side=1;editor_native::Gesture gesture;gesture.generation=native[1].state.generation;
    gesture.before=native[1].snapshot;gesture.after=gesture.before;
    gesture.after.document.placements.at(placement).position={9,8,7};
    gesture.after.bytes=body(gesture.after.document);native[1].snapshot=gesture.after;
    runtime_guest::captured(std::move(gesture));
    CHECK(runtime_guest::pending&&runtime_guest::awaiting_operation);
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1&&
        runtime_guest::controller->state().document.revision==1&&!runtime_guest::awaiting_operation;}));
    CHECK(spin([]{return runtime_guest::controller->view().local_leases.empty();}));
    // An unused grant gets a finite grace. Renewing the same token preserves
    // its original observed-use time rather than holding ownership forever.
    side=1;CHECK(!native[1].callbacks.gate({placement}));
    CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
    const auto idle_observed=runtime_guest::last_lease_use;
    const auto idle_token=runtime_guest::controller->view().local_leases.front().token;
    clock_ms+=4000;runtime_guest::last_renew=clock_ms-4001;
    for(unsigned i=0;i<8;++i)step();
    CHECK(runtime_guest::last_lease_use==idle_observed);
    CHECK(runtime_guest::controller->view().local_leases.size()==1&&
        runtime_guest::controller->view().local_leases.front().token==idle_token);
    clock_ms+=2000;runtime_guest::last_renew=clock_ms-4001;
    CHECK(spin([]{return runtime_guest::controller->view().local_leases.empty();}));
    step();CHECK(runtime_guest::observed_lease_grants.empty());
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen&&!runtime_guest::awaiting_operation);
    side=1;runtime_guest::end();
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Idle&&!runtime_guest::closing;}));
    step();CHECK(runtime_guest::observed_lease_grants.empty());
}

static void test_preparation_cancel_preserves_draft_and_is_round_scoped() {
    Party party;std::string error;capture_local_create();++game_frame;same_frame_drain(0);
    CHECK(spin([]{return runtime_host::controller->settled()&&runtime_guest::controller->state().document.revision==1;}));
    editor::ObjectId created{};for(const auto& [id,p]:runtime_host::controller->state().document.placements){(void)p;if(id!=placement)created=id;}
    side=0;CHECK(editor_native::select_placement(placement,error));side=1;CHECK(editor_native::select_placement(created,error));
    const auto canonical=runtime_host::controller->state();
    const auto checkpoint0=runtime_host::store.snapshot(),checkpoint1=runtime_guest::store.snapshot();
    const auto bytes0=native[0].snapshot.bytes,bytes1=native[1].snapshot.bytes;
    const auto restores0=native[0].restores,restores1=native[1].restores;
    auto prepare=[] {
        side=0;runtime_host::playtest();
        CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Preparing&&runtime_guest::controller->view().phase==es::Phase::Preparing;}));
        CHECK(runtime_host::view().can_cancel&&!runtime_guest::view().can_cancel);
        for(const auto& [id,member]:runtime_host::controller->view().members){(void)id;CHECK(!member.ready);}
    };
    auto unchanged=[&] {
        CHECK(runtime_host::controller->state()==canonical&&runtime_guest::controller->state()==canonical);
        CHECK(runtime_host::store.snapshot()==checkpoint0&&runtime_guest::store.snapshot()==checkpoint1);
        CHECK(native[0].snapshot.bytes==bytes0&&native[1].snapshot.bytes==bytes1);
        CHECK(native[0].state.selected==std::vector<editor::ObjectId>{placement}&&native[1].state.selected==std::vector<editor::ObjectId>{created});
        CHECK(native[0].restores==restores0&&native[1].restores==restores1);
        CHECK(native[0].stops==0&&native[1].stops==0&&!native[0].preview&&!native[1].preview);
        CHECK(sessions[0].connected()&&sessions[1].connected()&&!runtime_host::frozen&&!runtime_guest::frozen);
    };
    prepare();side=1;runtime_guest::cancel_playtest();CHECK(!runtime_guest::pending_preparation_cancel);step();
    CHECK(runtime_host::controller->view().phase==es::Phase::Preparing);
    side=0;runtime_host::debug({"cancel"});CHECK(runtime_host::pending_preparation_cancel);
    const auto old_scope=*runtime_host::pending_preparation_cancel;
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;}));unchanged();
    prepare();side=1;runtime_guest::set_ready(true);
    CHECK(spin([]{return runtime_host::controller->view().members.at(1).ready;}));
    // Old queued input from the first round cannot cancel this new preparation.
    runtime_host::pending_preparation_cancel=old_scope;side=0;runtime_host::schedule();step();
    CHECK(runtime_host::controller->view().phase==es::Phase::Preparing&&!runtime_host::pending_preparation_cancel);
    side=0;runtime_host::cancel_playtest();
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;}));unchanged();
    // Capture Cancel before the final Ready, then hold only its owned queue
    // while HostBegin wins the controller turn and the guest is still loading.
    prepare();side=0;runtime_host::cancel_playtest();CHECK(runtime_host::pending_preparation_cancel);
    const auto loading_cancel=*runtime_host::pending_preparation_cancel;runtime_host::pending_preparation_cancel.reset();
    side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));runtime_guest::controller->tick(clock_ms);
    side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Playing&&native[0].state.playtesting;},false));
    CHECK(runtime_guest::controller->view().phase==es::Phase::Preparing&&!native[1].preview);
    side=0;runtime_host::cancel_playtest();CHECK(!runtime_host::pending_preparation_cancel);
    runtime_host::pending_preparation_cancel=loading_cancel;runtime_host::schedule();
    CHECK(spin([]{for(unsigned p=0;p<2;++p)if(native[p].cleanup)complete_cleanup(p);return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;}));
    CHECK(native[0].stops==1&&native[1].stops==1&&runtime_host::controller->state()==canonical&&runtime_guest::controller->state()==canonical);
    CHECK(native[0].state.selected==std::vector<editor::ObjectId>{placement}&&native[1].state.selected==std::vector<editor::ObjectId>{created});
    prepare();side=0;runtime_host::set_ready(true);side=1;runtime_guest::set_ready(true);
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Playing&&runtime_guest::controller->view().phase==es::Phase::Playing;}));
    runtime_host::pending_preparation_cancel=old_scope;side=0;runtime_host::schedule();step();
    CHECK(runtime_host::controller->view().phase==es::Phase::Playing&&!runtime_host::pending_preparation_cancel);
    side=0;native[0].preview->return_editor();
    CHECK(spin([]{for(unsigned p=0;p<2;++p)if(native[p].cleanup)complete_cleanup(p);return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;}));
    CHECK(runtime_host::controller->state()==canonical&&runtime_guest::controller->state()==canonical);
    CHECK(native[0].state.selected==std::vector<editor::ObjectId>{placement}&&native[1].state.selected==std::vector<editor::ObjectId>{created});
    prepare();side=0;runtime_host::cancel_playtest();CHECK(runtime_host::pending_preparation_cancel);
    runtime_host::end();CHECK(!runtime_host::pending_preparation_cancel);
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    // Recovery deliberately rehosts the durable project under a new epoch.
    // Neither an old queued scope nor a recovery action can carry Cancel over.
    runtime_host::pending_preparation_cancel=old_scope;side=0;runtime_host::recover();
    CHECK(!runtime_host::pending_preparation_cancel);
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Offered;}));
    CHECK(runtime_host::controller->view().context.epoch!=old_scope.epoch);
    side=1;CHECK(runtime_guest::controller->accept_offer(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Ready&&runtime_host::controller->settled();}));
    prepare();runtime_host::pending_preparation_cancel=old_scope;side=0;runtime_host::schedule();step();
    CHECK(runtime_host::controller->view().phase==es::Phase::Preparing&&!runtime_host::pending_preparation_cancel);
    runtime_host::cancel_playtest();CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;}));
}


static void test_relative_scale_only_capture_undo_redo_and_recovery() {
    Party party;std::string error;side=0;
    const editor::FieldKey field{editor::Field::Property,editor_native::kRelativeScaleProperty};
    CHECK(native[0].callbacks.relative_scale_tracked&&native[0].callbacks.relative_scale_tracked());
    CHECK(native[0].callbacks.gate({placement})==false);
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    side=0;CHECK(native[0].callbacks.gate({placement}));
    editor_native::Gesture gesture;gesture.generation=native[0].state.generation;
    gesture.before=native[0].snapshot;gesture.after=gesture.before;
    gesture.after.document.placements.at(placement).properties[editor_native::kRelativeScaleProperty]=.479587317f;
    gesture.after.bytes=body(gesture.after.document);native[0].snapshot=gesture.after;
    runtime_host::captured(std::move(gesture));
    CHECK(runtime_host::pending&&runtime_host::pending->request.mutations.size()==1);
    CHECK(runtime_host::pending->request.mutations.front().field==field);
    CHECK(runtime_host::pending->request.mutations.front().expected==0);
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1&&runtime_guest::controller->state().document.revision==1&&!runtime_host::awaiting_operation;}));
    auto check=[&](float expected,editor::Revision revision){
        for(unsigned p=0;p<2;++p){const auto& document=p?runtime_guest::controller->state().document:runtime_host::controller->state().document;
            CHECK(document.revision==revision&&document.placements.at(placement).scale==1);
            CHECK(std::get<float>(document.placements.at(placement).properties.at(editor_native::kRelativeScaleProperty))==expected);
            CHECK(editor::field_revision(document.placements.at(placement),field)==revision);
        }
    };
    check(.479587317f,1);
    side=0;const auto restored=native[0].restores;const auto native_bytes=native[0].snapshot.bytes;
    const auto checkpoint=runtime_host::store.snapshot();const auto diagnostic=runtime_host::debug({"objects"});
    const auto key=diagnostic.find(" relative_scale=");CHECK(key!=std::string::npos);
    CHECK(std::strtof(diagnostic.c_str()+key+16,nullptr)==.479587317f);
    CHECK(native[0].restores==restored&&native[0].snapshot.bytes==native_bytes&&runtime_host::store.snapshot()==checkpoint);
    CHECK(spin([]{return runtime_host::controller->view().local_leases.empty()&&runtime_host::controller->settled();}));
    side=0;runtime_host::undo();CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    side=0;runtime_host::undo();CHECK(spin([]{return runtime_guest::controller->state().document.revision==2&&!runtime_host::awaiting_operation;}));check(.5f,2);
    CHECK(spin([]{return runtime_host::controller->view().local_leases.empty()&&runtime_host::controller->settled();}));
    side=0;runtime_host::redo();CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    side=0;runtime_host::redo();CHECK(spin([]{return runtime_guest::controller->state().document.revision==3&&!runtime_host::awaiting_operation;}));check(.479587317f,3);
    const auto old_epoch=runtime_host::controller->state().document.epoch;
    side=0;runtime_host::end();CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    side=0;runtime_host::recover();CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Offered;}));
    CHECK(runtime_host::controller->state().document.epoch!=old_epoch);
    CHECK(runtime_host::controller->state().history.size()==1);
    CHECK(std::get<float>(runtime_host::current_native.document.placements.at(placement).properties.at(editor_native::kRelativeScaleProperty))==.479587317f);
    side=1;CHECK(runtime_guest::controller->accept_offer(error));CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Ready&&runtime_host::controller->settled();}));check(.479587317f,3);
}
static void test_legacy_recovery_keeps_schema_history_and_scoped_capture() {
    {
        Party party;side=0;std::string error;
        auto state=runtime_host::controller->state();state.document.revision=4;state.document.next_id=6ull<<32;
        state.document.schema=editor_native::placement_property_schema();state.document.schema.erase(editor_native::kSoundPitchProperty);state.document.schema.erase(editor_native::kSoundGainProperty);
        state.document.class_schema=editor_native::cast_property_schema();
        editor::ClassActSettings settings;settings.object={5ull<<32,1};settings.class_id=*state.document.classes.begin();settings.act_id=*state.document.acts.begin();settings.revision=4;
        const editor::FieldKey field{editor::Field::Property,editor_native::kCastBlockProperty};
        settings.properties[field.property]=editor::StructuredValue{editor_native::kCastBlockFormat,{1,2,3},{}};settings.field_revisions[field]=4;
        settings.properties[editor::kClassAssetResourceProperty]=editor::ResourceKey{9,0x2B978C46,0x408A0000};
        state.document.class_settings[settings.object]=settings;
        editor::Gesture gesture;gesture.operation={runtime_host::controller->view().local,1,state.document.epoch};gesture.original_revision=gesture.transition_revision=4;gesture.applied=true;gesture.summary="Retained cast edit";
        auto old=std::get<editor::StructuredValue>(settings.properties.at(field.property));old.bytes={3,2,1};
        gesture.undo.fields.push_back({settings.object,field,settings.properties.at(field.property),editor::Value{old},4});state.history.push_back(gesture);
        auto snapshot=native[0].snapshot;snapshot.document=state.document;snapshot.document.schema=editor_native::placement_property_schema();
        snapshot.document.class_settings.at(settings.object).revision=0;snapshot.document.class_settings.at(settings.object).field_revisions.clear();
        for(auto& [id,p]:snapshot.document.placements){(void)id;p.properties[editor_native::kSoundPitchProperty]=1.f;p.properties[editor_native::kSoundGainProperty]=.375f;}
        const auto before=state;CHECK(runtime_host::expand_legacy_state(state,snapshot,error));
        CHECK(state.document.class_settings==before.document.class_settings&&state.history==before.history);
        CHECK(state.document.schema.contains(editor_native::kSoundPitchProperty));
        CHECK(state.document.placements.at(placement).properties.at(editor_native::kSoundGainProperty)==editor::Value{.375f});
    }
    Party party;std::string error;side=0;
    CHECK(!native[0].callbacks.gate({placement}));CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    side=0;CHECK(native[0].callbacks.gate({placement}));
    editor_native::Gesture original;original.generation=native[0].state.generation;original.before=native[0].snapshot;original.after=original.before;
    original.after.document.placements.at(placement).position.y=2;original.after.bytes=body(original.after.document);native[0].snapshot=original.after;runtime_host::captured(std::move(original));
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&runtime_host::controller->settled();}));
    side=0;runtime_host::end();
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    editor_store::Store old;CHECK(old.open(runtime_host::project_root,runtime_host::project_id,error));
    auto checkpoint=old.snapshot();editor::State state;editor_native::Snapshot snapshot;
    CHECK(editor_project::load_snapshot(checkpoint,state,snapshot,error));
    state.document.schema.clear();for(auto& [id,p]:state.document.placements){(void)id;p.properties.clear();}
    snapshot.document=state.document;snapshot.bytes=body(state.document);std::vector<uint8_t> old_mapping;
    CHECK(editor_project::store_snapshot(state,snapshot,"Legacy C13 draft",checkpoint,error));
    checkpoint.manifest=old.snapshot().manifest;CHECK(old.checkpoint(checkpoint,error));old.close();
    side=0;runtime_host::recover();
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Offered;}));
    CHECK(runtime_host::controller->state().document.schema.empty()&&runtime_host::controller->state().history.size()==1);
    CHECK(native[0].callbacks.relative_scale_tracked&&!native[0].callbacks.relative_scale_tracked());
    side=1;CHECK(runtime_guest::controller->accept_offer(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Ready&&runtime_host::controller->settled();}));
    side=1;CHECK(!native[1].callbacks.relative_scale_tracked());
    CHECK(runtime_guest::current_native.document.schema.empty()&&!runtime_guest::projection_override);
    CHECK(runtime_guest::controller->state().document.placements.at(placement).properties.empty());
    CHECK(runtime_guest::debug({"objects"}).find("relative_scale=")==std::string::npos);
    side=0;CHECK(native[0].callbacks.gate({placement})==false);CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    side=0;CHECK(native[0].callbacks.gate({placement}));
    editor_native::Gesture gesture;gesture.generation=native[0].state.generation;gesture.before=native[0].snapshot;gesture.after=gesture.before;
    gesture.after.document.placements.at(placement).position.x=4;gesture.after.bytes=body(gesture.after.document);native[0].snapshot=gesture.after;runtime_host::captured(std::move(gesture));
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==2&&!runtime_host::awaiting_operation;}));
    CHECK(runtime_host::controller->state().document.schema.empty()&&runtime_host::controller->state().history.size()==2);
    CHECK(runtime_guest::controller->state().document.placements.at(placement).position.x==4);
    CHECK(spin([]{return runtime_host::controller->view().local_leases.empty()&&runtime_host::controller->settled();}));
    side=0;runtime_host::undo();CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    side=0;runtime_host::undo();CHECK(spin([]{return runtime_guest::controller->state().document.revision==3&&!runtime_host::awaiting_operation;}));
    CHECK(runtime_host::controller->state().document.schema.empty()&&runtime_guest::controller->state().document.schema.empty());
    CHECK(runtime_guest::controller->state().document.placements.at(placement).position==editor::Vec3{0,2,0});
}
static void test_verify_reopen_read_only_exact_saved_identity() {
    Party party;std::string error;side=0;
    CHECK(spin([]{return runtime_host::controller->settled();}));
    auto coverage=runtime_host::controller->saved_coverage();CHECK(coverage.current&&coverage.revision==0&&coverage.expected==2&&coverage.applied==2);
    const auto status=runtime_host::debug({"status"});CHECK(status.find("saved_revision=0 saved_applied=2 saved_expected=2 settled=1")!=std::string::npos);
    CHECK(runtime_host::debug({"verify-reopen"}).starts_with("REOPEN_REJECTED"));
    runtime_host::end();CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    CHECK(runtime_host::reopen_saved&&!runtime_guest::reopen_saved);
    CHECK(runtime_host::reopen_saved->captain==avatar_key);
    side=0;const auto before=native[0].snapshot;const auto restores=native[0].restores,saves=native[0].save_calls,saveas=native[0].save_as_calls;
    const auto expected="REOPEN_VERIFIED revision=0 source=408A0000!00000064.366A930D";
    CHECK(runtime_host::debug({"verify-reopen"})==expected);
    CHECK(native[0].restores==restores&&native[0].save_calls==saves&&native[0].save_as_calls==saveas&&native[0].snapshot.bytes==before.bytes);
    auto renamed=native[0].snapshot;auto p=renamed.document.placements.extract(placement);const editor::ObjectId reopened{0x12345,1};p.key()=reopened;p.mapped().object=reopened;renamed.document.placements.insert(std::move(p));renamed.bindings.front().object=reopened;
    native[0].snapshot=renamed;CHECK(runtime_host::debug({"verify-reopen"})==expected);CHECK(native[0].snapshot.document.placements.contains(reopened));
    native[0].snapshot=before;native[0].snapshot.source.instance++;CHECK(runtime_host::debug({"verify-reopen"}).starts_with("REOPEN_REJECTED"));
    native[0].snapshot=before;native[0].snapshot.bytes[0]^=1;CHECK(runtime_host::debug({"verify-reopen"}).starts_with("REOPEN_REJECTED"));
    native[0].snapshot=before;native[0].snapshot.document.placements.at(placement).position.x=7;native[0].snapshot.bytes=body(native[0].snapshot.document);CHECK(runtime_host::debug({"verify-reopen"}).starts_with("REOPEN_REJECTED"));
    CHECK(native[0].restores==restores&&native[0].save_calls==saves&&native[0].save_as_calls==saveas);
    native[0].snapshot=before;native[0].snapshot.document.placements.at(placement).properties[editor_native::kRelativeScaleProperty]=.31f;native[0].snapshot.bytes=body(native[0].snapshot.document);CHECK(runtime_host::debug({"verify-reopen"}).starts_with("REOPEN_REJECTED"));
    native[0].snapshot=before;++native[0].state.generation;CHECK(runtime_host::debug({"verify-reopen"})==expected);
    CHECK(!runtime_host::frozen&&native[0].restores==restores&&native[0].save_calls==saves&&native[0].save_as_calls==saveas);
    side=1;CHECK(runtime_guest::debug({"verify-reopen"}).starts_with("REOPEN_REJECTED"));
}
static void test_unsaved_end_does_not_pin_reopen_and_partial_saved_ack_is_not_complete() {
    Party party;capture_local_create();++game_frame;same_frame_drain(0);
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&runtime_host::controller->settled();}));
    side=0;const auto old=runtime_host::controller->saved_coverage();CHECK(!old.current&&old.revision==0);
    runtime_host::end();CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    CHECK(!runtime_host::reopen_saved);side=0;const auto restores=native[0].restores;CHECK(runtime_host::debug({"verify-reopen"}).starts_with("REOPEN_REJECTED"));CHECK(native[0].restores==restores);
}


static void test_saved_coverage_requires_guest_native_completion_before_pin() {
    Party party;side=1;native[1].state.gesture=true;side=0;
    simulate_host_saved({saved_key.instance,saved_key.type,saved_key.group});
    CHECK(spin([]{const auto s=runtime_host::controller->saved_coverage();return s.current&&s.expected==2&&s.applied==1;},false));
    CHECK(!runtime_host::controller->settled());side=0;runtime_host::end();step(false);
    CHECK(runtime_host::closing&&!runtime_host::reopen_saved);
    CHECK(runtime_host::debug({"status"}).find("saved_applied=1 saved_expected=2 settled=0")!=std::string::npos);
    native[1].state.gesture=false;
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    CHECK(runtime_host::reopen_saved&&runtime_host::reopen_saved->source.instance==saved_key.instance);
    side=0;const auto restores=native[0].restores;CHECK(runtime_host::debug({"verify-reopen"})=="REOPEN_VERIFIED revision=0 source=408A0000!00000065.366A930D");CHECK(native[0].restores==restores);
}


static void test_clone_preserves_companion_and_rejects_compound_property_change() {
    Party party;std::string error;side=0;
    CHECK(!native[0].callbacks.gate({placement}));CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    side=0;CHECK(native[0].callbacks.gate({placement}));
    editor_native::Gesture scale;scale.generation=native[0].state.generation;scale.before=native[0].snapshot;scale.after=scale.before;
    scale.after.document.placements.at(placement).properties[editor_native::kRelativeScaleProperty]=.47f;
    scale.after.bytes=body(scale.after.document);native[0].snapshot=scale.after;runtime_host::captured(std::move(scale));
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&!runtime_host::awaiting_operation&&runtime_host::controller->settled();}));
    CHECK(spin([]{return runtime_host::controller->view().local_leases.empty();}));side=0;
    CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Document,{},{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));side=0;
    runtime_host::guarded_placements[placement]=runtime_host::controller->state().document.placements.at(placement);
    editor_native::Gesture clone;clone.generation=native[0].state.generation;clone.before=native[0].snapshot;clone.after=clone.before;clone.clone_source=placement;
    auto p=clone.after.document.placements.at(placement);p.object={12345,1};p.position.x=3;
    clone.after.document.placements.emplace(p.object,p);clone.after.bindings.push_back({p.object,1});clone.after.bytes=body(clone.after.document);
    native[0].snapshot=clone.after;runtime_host::captured(std::move(clone));
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==2&&!runtime_host::awaiting_operation&&runtime_host::controller->settled();}));
    for(const auto& [id,value]:runtime_host::controller->state().document.placements){(void)id;CHECK(std::get<float>(value.properties.at(editor_native::kRelativeScaleProperty))==.47f);CHECK(value.scale==1);}
    CHECK(spin([]{return runtime_host::controller->view().local_leases.empty();}));side=0;
    CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Document,{},{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));side=0;
    runtime_host::guarded_placements[placement]=runtime_host::controller->state().document.placements.at(placement);
    editor_native::Gesture compound;compound.generation=native[0].state.generation;compound.before=native[0].snapshot;compound.after=compound.before;compound.clone_source=placement;
    p=compound.after.document.placements.at(placement);p.object={12346,1};p.position.x=4;p.properties[editor_native::kRelativeScaleProperty]=.3f;
    compound.after.document.placements.emplace(p.object,p);compound.after.bindings.push_back({p.object,2});compound.after.bytes=body(compound.after.document);
    native[0].snapshot=compound.after;const auto canonical=runtime_host::controller->state();runtime_host::captured(std::move(compound));
    CHECK(!runtime_host::frozen&&!runtime_host::pending&&runtime_host::controller->state()==canonical);
    CHECK(native[0].snapshot.document.placements.size()==2&&runtime_host::status.find("Clone and Scale separately")!=std::string::npos);
    CHECK(runtime_guest::controller->state()==canonical);
}


static void test_class_owned_placement_companion_transaction() {
    Party party;side=0;
    CHECK(!native[0].callbacks.class_gate(1));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));side=0;
    CHECK(native[0].callbacks.class_gate(1));CHECK(runtime_host::guarded_placements.contains(placement));
    const auto before=native[0].snapshot;
    editor_native::Gesture edit;edit.generation=native[0].state.generation;edit.before=before;edit.after=before;
    edit.after.document.placements.at(placement).scale=2;
    edit.after.document.placements.at(placement).properties[editor_native::kRelativeScaleProperty]=.75f;
    edit.after.bytes=body(edit.after.document);native[0].snapshot=edit.after;runtime_host::captured(std::move(edit));
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==1 && !runtime_host::awaiting_operation;}));
    CHECK(!runtime_host::frozen && runtime_host::controller->state().history.size()==1);
    side=0;native[0].callbacks.history(false);
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==2 && !runtime_host::awaiting_operation;}));
    CHECK(runtime_host::controller->state().document.placements.at(placement).scale==1);
    CHECK(std::get<float>(runtime_host::controller->state().document.placements.at(placement).properties.at(editor_native::kRelativeScaleProperty))==.5f);
}
static void test_unowned_native_gesture_restores_without_freezing() {
    Party party;side=0;
    const auto canonical=runtime_host::controller->state();
    const auto before=native[0].snapshot;const auto restores=native[0].restores;
    editor_native::Gesture move;move.generation=native[0].state.generation;move.before=before;move.after=before;
    move.after.document.placements.at(placement).position.x+=4;
    move.after.bytes=body(move.after.document);native[0].snapshot=move.after;
    runtime_host::captured(std::move(move));
    CHECK(!runtime_host::frozen && !runtime_host::pending && !runtime_host::awaiting_operation);
    CHECK(runtime_host::controller->state()==canonical && runtime_guest::controller->state()==canonical);
    CHECK(native[0].restores==restores+1 && native[0].snapshot.document==before.document);
    CHECK(runtime_host::status.find("no current placement ownership")!=std::string::npos);
}
static void test_legacy_pending_recovery_mode_overrides_detached_old_context() {
    Party party;side=0;
    auto work=runtime_host::Recovery{};work.state.document=seed();work.state.document.schema.clear();
    for(auto& [id,p]:work.state.document.placements){(void)id;p.properties.clear();}
    work.saving=true;runtime_host::recovery=work;runtime_host::native_attached=false;
    CHECK(!native[0].callbacks.relative_scale_tracked());
    {runtime_host::ProjectionScope incoming(seed());CHECK(native[0].callbacks.relative_scale_tracked());}
    CHECK(!native[0].callbacks.relative_scale_tracked());
    runtime_host::recovery.reset();CHECK(native[0].callbacks.relative_scale_tracked());
    runtime_host::native_attached=true;CHECK(native[0].callbacks.relative_scale_tracked());
    CHECK(native[0].callbacks.notice);native[0].callbacks.notice("Legacy Scale is held until a new shared copy is started");CHECK(!runtime_host::frozen);
}


static void guest_reoffer(bool hold_native) {
    side=1;native[1].state.gesture=hold_native;runtime_guest::controller->end();runtime_guest::close_session();
    side=0;runtime_host::on_join(1);
    CHECK(spin([=]{return runtime_guest::controller->view().phase==es::Phase::Offered&&runtime_guest::closing==hold_native;}));
}
static void test_transport_teardown_retires_offered_before_role_change() {
    Party party;guest_reoffer(false);side=1;
    CHECK(sessions[1].role()==net::Role::Client&&runtime_guest::controller->view().phase==es::Phase::Offered);
    native[1].state.editing=native[1].state.ready=false;
    const auto restores=native[1].restores,rebaselines=native[1].rebaselines;
    runtime_guest::session_ended();CHECK(sessions[1].role()==net::Role::Client);sessions[1].leave("explicit user leave");
    step();
    CHECK(runtime_guest::controller->view().phase==es::Phase::Idle&&!runtime_guest::closing);
    std::string error;CHECK(!runtime_guest::controller->accept_offer(error));
    CHECK(native[1].restores==restores&&native[1].rebaselines==rebaselines);
    CHECK(runtime_guest::recovery_available&&runtime_host::controller->view().phase==es::Phase::Ready);
}

static void test_transport_teardown_revokes_offer_during_async_close() {
    Party party;guest_reoffer(true);side=1;
    const auto canonical=runtime_guest::current_native.bytes;const auto restores=native[1].restores,rebaselines=native[1].rebaselines;
    CHECK(runtime_guest::close_preserve_offer&&runtime_guest::native_attached);
    runtime_guest::undo();runtime_guest::begin();runtime_guest::recover();
    runtime_guest::session_ended();runtime_guest::session_ended();sessions[1].leave("explicit teardown during reoffer");step();
    CHECK(runtime_guest::closing&&!runtime_guest::close_preserve_offer&&runtime_guest::actions.empty());
    CHECK(native[1].restores==restores&&native[1].rebaselines==rebaselines);
    native[1].state.gesture=false;
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Idle&&!runtime_guest::closing;}));
    CHECK(native[1].snapshot.bytes==canonical&&native[1].restores==restores+1&&native[1].rebaselines==rebaselines+1);
    for(unsigned i=0;i<5;++i)step();CHECK(native[1].restores==restores+1&&runtime_guest::actions.empty());
    CHECK(runtime_guest::recovery_available&&!runtime_guest::store.healthy());
    CHECK(runtime_host::controller->view().phase==es::Phase::Ready&&sessions[0].role()==net::Role::Host);
}
static void test_transport_teardown_defers_only_authenticated_new_transport_offer() {
    Party party;guest_reoffer(true);side=1;const auto old=runtime_guest::controller->view();
    const auto old_transport=sessions[1].transport_generation();runtime_guest::session_ended();sessions[1].leave();
    side=0;runtime_host::session_ended();sessions[0].leave();
    CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&!runtime_host::closing;}));
    CHECK(runtime_guest::closing);std::string error;
    CHECK(sessions[0].host(0,"New host transport",error));
    CHECK(sessions[1].join("127.0.0.1:"+std::to_string(sessions[0].port()),"Fresh guest",error));
    CHECK(spin([]{return sessions[1].connected()&&sessions[0].players().size()==2;}));
    CHECK(sessions[1].my_id()==old.local.slot&&sessions[1].transport_generation()>old_transport);
    side=0;runtime_host::start_saved();CHECK(!runtime_host::frozen);
    CHECK(spin([]{return runtime_guest::deferred_offer.has_value();}));
    const auto pending=*runtime_guest::deferred_offer;
    CHECK(pending.context.project!=old.context.project&&pending.transport==sessions[1].transport_generation());
    CHECK(runtime_guest::controller->view().context==old.context&&runtime_guest::closing);
    // A malformed capability envelope, wrong Session peer and another valid
    // Offer cannot replace the one pinned to this authenticated transport.
    auto bad=pending.bytes;bad[1]^=1;side=1;net::Reader r1(bad.data(),bad.size());runtime_guest::on_message(0,r1);
    net::Reader r2(pending.bytes.data(),pending.bytes.size());runtime_guest::on_message(1,r2);
    CHECK(runtime_guest::deferred_offer->bytes==pending.bytes);
    auto late=native[1].callbacks.gesture;editor_native::Gesture gesture;gesture.generation=native[1].state.generation;gesture.before=gesture.after=native[1].snapshot;
    const auto restores=native[1].restores;late(gesture);CHECK(native[1].restores==restores);
    native[1].state.gesture=false;
    CHECK(spin([&]{return !runtime_guest::closing&&runtime_guest::controller->view().phase==es::Phase::Offered&&runtime_guest::controller->view().context==pending.context;}));
    CHECK(runtime_guest::controller->view().local==pending.assigned&&!runtime_guest::deferred_offer);
    native[1].state.editing=native[1].state.ready=false;side=1;CHECK(runtime_guest::controller->accept_offer(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Ready&&runtime_host::controller->settled();}));
    CHECK(runtime_guest::current_native.document.project==pending.context.project&&!runtime_guest::frozen);
    CHECK(runtime_guest::native_generation==native[1].state.generation);
}
static void test_transport_teardown_replaced_native_context_is_untouched() {
    Party party;guest_reoffer(true);side=1;
    runtime_guest::session_ended();sessions[1].leave();
    ++native[1].state.generation;native[1].state.gesture=false;native[1].save_busy=true;
    native[1].snapshot.bytes=body(seed(),0x37);const auto restores=native[1].restores,rebaselines=native[1].rebaselines;
    CHECK(spin([]{return !runtime_guest::closing&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    CHECK(native[1].snapshot.bytes.front()==0x37&&native[1].restores==restores&&native[1].rebaselines==rebaselines);
    CHECK(runtime_guest::recovery_available);
}
static void test_transport_teardown_remote_host_disconnect_order_and_preview_cleanup() {
    Party party;std::string error;side=0;
    native[0].view.orbit={.31f,.71f,131};native[1].view.orbit={.41f,.81f,141};
    native[0].state.selected.clear();native[1].state.selected={placement};
    const auto view0=native[0].view,view1=native[1].view;
    native[0].bump_generation_on_stop=native[1].bump_generation_on_stop=true;
    CHECK(runtime_host::controller->prepare_playtest(error));CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Preparing;}));
    side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
    CHECK(spin([]{return native[0].state.playtesting&&native[1].state.playtesting&&runtime_guest::controller->view().phase==es::Phase::Playing;}));
    bool disconnected=false;uint64_t generation=0;sessions[1].cb.disconnected=[&](const std::string&){side=1;disconnected=true;generation=sessions[1].transport_generation();CHECK(sessions[1].role()==net::Role::None);runtime_guest::session_ended();};
    const auto old_transport=sessions[1].transport_generation();side=0;runtime_host::session_ended();sessions[0].leave("host transport closes");
    CHECK(spin([&]{return disconnected&&native[1].cleanup&&native[0].cleanup;}));
    CHECK(generation>old_transport&&runtime_guest::closing&&native[1].stops==0);
    const auto before=native[1].restores;complete_cleanup(1);complete_cleanup(0);
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Idle&&runtime_host::controller->view().phase==es::Phase::Idle&&!runtime_guest::closing&&!runtime_host::closing;}));
    CHECK(native[1].cancels==1&&native[1].stops==1&&!native[1].preview&&native[1].restores==before+2);
    CHECK(native[0].stops==1&&!native[0].preview&&runtime_guest::recovery_available);
    CHECK(native[0].view==view0&&native[1].view==view1&&native[0].view_restores==1&&native[1].view_restores==1);
    CHECK(native[0].state.selected.empty()&&native[1].state.selected==std::vector<editor::ObjectId>{placement});
    CHECK(runtime_guest::adventure_messages.empty());
}
static void test_transport_teardown_late_offer_dropped_on_second_leave() {
    Party party;guest_reoffer(true);side=1;runtime_guest::session_ended();sessions[1].leave();
    std::string error;CHECK(sessions[1].join("127.0.0.1:"+std::to_string(sessions[0].port()),"Again",error));
    CHECK(spin([]{return sessions[1].connected()&&runtime_guest::deferred_offer.has_value();}));
    side=1;runtime_guest::session_ended();sessions[1].leave();CHECK(!runtime_guest::deferred_offer);
    native[1].state.gesture=false;CHECK(spin([]{return !runtime_guest::closing&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    std::string rejected;CHECK(!runtime_guest::controller->accept_offer(rejected));
}
static void test_transport_teardown_replaced_preview_cannot_stop_new_editor() {
    Party party;std::string error;side=0;CHECK(runtime_host::controller->prepare_playtest(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Preparing;}));
    side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
    CHECK(spin([]{return native[0].state.playtesting&&native[1].state.playtesting;}));
    side=1;runtime_guest::session_ended();sessions[1].leave();++native[1].state.generation;
    native[1].state.playtesting=false;native[1].state.editing=native[1].state.ready=true;native[1].snapshot.bytes=body(seed(),0x38);
    CHECK(spin([]{return native[1].cleanup;}));const auto restores=native[1].restores;complete_cleanup(1);
    CHECK(spin([]{return !runtime_guest::closing&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
    CHECK(native[1].stops==0&&native[1].restores==restores&&native[1].snapshot.bytes.front()==0x38);
    CHECK(!native[1].preview&&!native[1].cleanup&&runtime_guest::recovery_available);
    // Settle the surviving host's normal membership-cancel path too.
    CHECK(spin([]{return native[0].cleanup;}));complete_cleanup(0);CHECK(spin([]{return !native[0].preview;}));
}
static void test_owned_end_keeps_local_view_and_selection() {
    Party party;std::string error;
    native[0].view.orbit={.32f,.72f,132};native[1].view.orbit={.42f,.82f,142};
    native[0].state.selected={placement};native[1].state.selected.clear();
    const auto a=native[0].view,b=native[1].view;
    side=0;CHECK(runtime_host::controller->prepare_playtest(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Preparing;}));
    side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
    CHECK(spin([]{return native[0].state.playtesting&&native[1].state.playtesting;}));
    side=0;runtime_host::end();
    CHECK(spin([]{for(unsigned p=0;p<2;++p)if(native[p].cleanup)complete_cleanup(p);return runtime_host::controller->view().phase==es::Phase::Idle&&!native[0].preview&&!native[1].preview;}));
    CHECK(native[0].view==a&&native[1].view==b&&native[0].view_restores==1&&native[1].view_restores==1);
    CHECK(native[0].state.selected==std::vector<editor::ObjectId>{placement}&&native[1].state.selected.empty());
}
static void test_bound_preview_cleanup_requires_exact_round_and_native_owner() {
    for(unsigned mismatch=0;mismatch<3;++mismatch){
        Party party;std::string error;side=0;CHECK(runtime_host::controller->prepare_playtest(error));
        CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Preparing;}));
        side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
        CHECK(spin([]{return native[0].state.playtesting&&native[1].state.playtesting;}));
        const auto stops=native[0].stops,restores=native[0].restores,views=native[0].view_restores;
        if(mismatch==0)++native[0].preview->round;
        if(mismatch==1)native[0].preview_owned=false;
        if(mismatch==2)++native[0].state.generation;
        side=0;native[0].preview->return_editor();
        CHECK(spin([]{for(unsigned p=0;p<2;++p)if(native[p].cleanup)complete_cleanup(p);return runtime_host::controller->view().phase==es::Phase::Frozen;}));
        CHECK(native[0].stops==stops&&native[0].restores==restores&&native[0].view_restores==views);
    }
}
static void test_all_transport_teardown_cases() {
    test_transport_teardown_retires_offered_before_role_change();
    test_transport_teardown_revokes_offer_during_async_close();
    test_transport_teardown_defers_only_authenticated_new_transport_offer();
    test_transport_teardown_replaced_native_context_is_untouched();
    test_transport_teardown_remote_host_disconnect_order_and_preview_cleanup();test_owned_end_keeps_local_view_and_selection();test_bound_preview_cleanup_requires_exact_round_and_native_owner();
    test_transport_teardown_late_offer_dropped_on_second_leave();
    test_transport_teardown_replaced_preview_cannot_stop_new_editor();
}

static editor_native::CancellationTicket cancellation_ticket(unsigned p,uint64_t gesture,uint64_t serial) {
    side=int(p);CHECK(native[p].callbacks.gesture_scope&&native[p].callbacks.cancelled);
    return {native[p].callbacks.gesture_scope(),native[p].state.generation,gesture,serial};
}
static void test_native_cancellation_is_not_history_and_keeps_latest_canonical() {
    Party party;std::string error;capture_local_create();
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&!runtime_host::awaiting_operation&&runtime_host::controller->settled()&&runtime_host::controller->view().local_leases.empty();}));
    side=0;const auto ticket=cancellation_ticket(0,1,1);
    CHECK(runtime_host::controller->state().history.size()==1&&runtime_host::controller->state().history.front().applied);
    // The native before copy predates a disjoint accepted guest change. A
    // rejected native Scale must restore the current canonical draft, not erase
    // that already committed peer operation or consume either author's Undo.
    const auto before=native[0].snapshot;
    CHECK(runtime_guest::controller->request_leases({{editor::ScopeKind::Placement,placement,{}}},15000,error));
    CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
    side=1;CHECK(native[1].callbacks.gate({placement}));
    editor_native::Gesture move;move.generation=native[1].state.generation;move.before=native[1].snapshot;move.after=move.before;
    move.after.document.placements.at(placement).position={8,9,10};move.after.bytes=body(move.after.document);
    native[1].snapshot=move.after;runtime_guest::captured(std::move(move));
    CHECK(spin([]{return runtime_host::controller->state().document.revision==2&&!runtime_guest::awaiting_operation&&runtime_host::controller->settled();}));
    side=0;const auto canonical=runtime_host::controller->state();const auto checkpoint=runtime_host::store.snapshot();const auto sequence=runtime_host::sequence;
    // Acquire a local guard as the native rejected gesture would have done.
    CHECK(spin([]{return runtime_guest::controller->view().local_leases.empty();}));side=0;
    CHECK(runtime_host::controller->request_leases({{editor::ScopeKind::Placement,placement,{}}},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));side=0;
    runtime_host::guarded_placements[placement]=canonical.document.placements.at(placement);
    native[0].snapshot=before;native[0].state.selected={placement};const auto restores=native[0].restores;
    CHECK(native[0].callbacks.cancelled(ticket,error));
    CHECK(native[0].snapshot.document.placements.at(placement).position==editor::Vec3{8,9,10});
    CHECK(runtime_host::controller->state()==canonical&&runtime_host::store.snapshot()==checkpoint&&runtime_host::sequence==sequence);
    CHECK(!runtime_host::pending&&!runtime_host::awaiting_operation&&runtime_host::actions.empty()&&runtime_host::guarded_placements.empty());
    CHECK(native[0].state.selected==std::vector<editor::ObjectId>{placement}&&native[0].restores==restores+1&&!runtime_host::frozen);
    CHECK(native[0].callbacks.cancelled(ticket,error)&&native[0].restores==restores+1);
    auto older=ticket;older.gesture=0;older.serial=2;CHECK(!native[0].callbacks.cancelled(older,error));
    CHECK(runtime_host::controller->state()==canonical&&runtime_guest::controller->state()==canonical);
    CHECK(spin([]{return runtime_host::controller->view().local_leases.empty();}));
    side=0;CHECK(!native[0].callbacks.gate({placement}));CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));
    side=0;CHECK(native[0].callbacks.gate({placement}));
}
static void test_native_cancellation_scope_and_failed_restore_are_safe() {
    Party party;std::string error;const auto ticket=cancellation_ticket(1,7,9);side=1;
    const auto canonical=runtime_guest::controller->state();const auto checkpoint=runtime_guest::store.snapshot();
    const auto restores=native[1].restores;auto stale=ticket;++stale.scope.epoch;
    CHECK(native[1].callbacks.cancelled(stale,error)&&native[1].restores==restores&&!runtime_guest::frozen);
    stale=ticket;++stale.scope.author.generation;CHECK(native[1].callbacks.cancelled(stale,error)&&native[1].restores==restores);
    ++native[1].state.generation;native[1].snapshot.bytes=body(seed(),0x5F);
    CHECK(native[1].callbacks.cancelled(ticket,error)&&native[1].restores==restores&&native[1].snapshot.bytes.front()==0x5F);
    --native[1].state.generation;native[1].snapshot=runtime_guest::current_native;
    native[1].refuse_restore=true;CHECK(!native[1].callbacks.cancelled(ticket,error));CHECK(error.find("restore failure")!=std::string::npos);
    // The real Native caller promotes false to its existing failure path.
    native[1].callbacks.failure(error);CHECK(runtime_guest::frozen);
    CHECK(runtime_guest::controller->state()==canonical&&runtime_guest::store.snapshot()==checkpoint&&native[1].restores==restores);
    CHECK(!runtime_guest::pending&&!runtime_guest::awaiting_operation&&runtime_guest::actions.empty());
    native[1].refuse_restore=false;
    // Complete the normal user End after observing the frozen failure. This
    // fixture hosts several process-global Runtime copies in one executable.
    side=1;runtime_guest::end();CHECK(spin([]{return !runtime_guest::native_attached&&!runtime_guest::closing&&!runtime_guest::frozen;}));
}
static void test_native_cancellation_stale_after_teardown_and_recovery_is_inert() {
    Party party;std::string error;const auto old=cancellation_ticket(0,2,3);side=0;
    runtime_host::end();CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&!runtime_host::closing;}));
    side=0;const auto restores=native[0].restores;CHECK(native[0].callbacks.cancelled(old,error)&&native[0].restores==restores);
    runtime_host::recover();CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready&&!runtime_host::recovery;}));
    side=0;CHECK(runtime_host::controller->view().context.epoch!=old.scope.epoch||native[0].state.generation!=old.generation);
    const auto recovered=native[0].snapshot;const auto after=native[0].restores;
    CHECK(native[0].callbacks.cancelled(old,error)&&native[0].restores==after&&native[0].snapshot.bytes==recovered.bytes&&!runtime_host::frozen);
    const auto fresh=cancellation_ticket(0,8,10);runtime_host::session_ended();
    CHECK(native[0].callbacks.cancelled(fresh,error)&&native[0].restores==after);
}

static void test_pending_native_cancellation_disables_actions_without_freezing() {
    Party party;capture_local_create();CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&!runtime_host::awaiting_operation&&runtime_host::controller->settled();}));
    side=0;runtime_host::refresh();CHECK(runtime_host::view().can_save&&runtime_host::view().can_undo&&runtime_host::view().can_playtest);
    const auto canonical=runtime_host::controller->state(),guest=runtime_guest::controller->state();const auto sequence=runtime_host::sequence;
    const auto saves=native[0].save_calls,saveas=native[0].save_as_calls,restores=native[0].restores;
    // Native has completed the invalid entry's finalizer but is still waiting
    // for its cancellation callback. Cached toolbar state predates that hold.
    native[0].state.ready=false;native[0].state.gesture=false;
    runtime_host::save();runtime_host::undo();runtime_host::playtest();step();
    CHECK(native[0].save_calls==saves&&native[0].save_as_calls==saveas&&native[0].restores==restores);
    CHECK(!runtime_host::frozen&&!runtime_host::pending&&!runtime_host::awaiting_operation&&runtime_host::sequence==sequence);
    CHECK(runtime_host::controller->state()==canonical&&runtime_guest::controller->state()==guest);
    CHECK(!runtime_host::view().can_save&&!runtime_host::view().can_undo&&!runtime_host::view().can_redo&&!runtime_host::view().can_playtest);
    native[0].state.ready=true;side=0;auto ticket=cancellation_ticket(0,1,1);std::string error;
    CHECK(native[0].callbacks.cancelled(ticket,error));runtime_host::refresh();
    CHECK(runtime_host::view().can_save&&runtime_host::view().can_undo);
    CHECK(spin([]{side=0;runtime_host::refresh();return runtime_host::view().can_playtest;}));
    CHECK(!runtime_host::frozen&&runtime_host::controller->state()==canonical&&runtime_host::sequence==sequence);
}
static void test_all_native_cancellations() {
    test_pending_native_cancellation_disables_actions_without_freezing();
    test_native_cancellation_is_not_history_and_keeps_latest_canonical();
    test_native_cancellation_scope_and_failed_restore_are_safe();
    test_native_cancellation_stale_after_teardown_and_recovery_is_inert();
}
static void end_act_fixture() {
    side=0;runtime_host::end();
    CHECK(spin([]{return !runtime_host::closing&&!runtime_host::native_attached&&
        runtime_host::controller->view().phase==es::Phase::Idle&&runtime_guest::controller->view().phase==es::Phase::Idle;}));
}
static void test_act_limit_begin_and_async_saved_recheck() {
    Party party;std::string error;end_act_fixture();side=0;
    const auto before=native[0].snapshot;const auto saves=native[0].save_as_calls,restores=native[0].restores,registrations=native[0].registrations;
    native[0].state.act=8;runtime_host::refresh();CHECK(!runtime_host::cached.can_begin);
    CHECK(runtime_host::cached.status.find("eight acts")!=std::string::npos);
    runtime_host::execute(runtime_host::Action::Begin);
    CHECK(!runtime_host::beginning&&native[0].save_as_calls==saves&&!native[0].save_busy&&!runtime_host::frozen);
    CHECK(native[0].snapshot.bytes==before.bytes&&native[0].restores==restores&&native[0].registrations==registrations);
    // Capture rechecks the actual resource act even when cached state says a supported act.
    native[0].state.act=0;runtime_host::refresh();CHECK(runtime_host::cached.can_begin);
    native[0].snapshot.act=8;native[0].snapshot.document.acts.insert({(3ull<<32)|8,1});
    for(auto& [id,p]:native[0].snapshot.document.placements){(void)id;p.act_id={(3ull<<32)|8,1};}
    native[0].snapshot.bytes=body(native[0].snapshot.document);
    const auto rejected=native[0].snapshot.bytes;runtime_host::execute(runtime_host::Action::Begin);
    CHECK(!runtime_host::beginning&&!runtime_host::native_attached&&!runtime_host::frozen);
    CHECK(native[0].snapshot.bytes==rejected&&native[0].save_as_calls==saves);
    CHECK(native[0].restores==restores&&native[0].registrations==registrations);
    native[0].snapshot=before;runtime_host::execute(runtime_host::Action::Begin);
    CHECK(runtime_host::native_attached&&!native[0].save_busy&&native[0].save_as_calls==saves);

}
static void test_act_limit_recovery_rejects_before_native_or_save() {
    Party party;std::string error;side=0;
    auto state=runtime_host::controller->state();auto snapshot=runtime_host::current_native;
    const auto manifest=runtime_host::store.snapshot().manifest;end_act_fixture();side=0;
    CHECK(runtime_host::store.open(runtime_host::project_root,runtime_host::project_id,error));
    state.document.acts.insert({(3ull<<32)|8,1});
    for(auto& [id,p]:state.document.placements){(void)id;p.act_id={(3ull<<32)|8,1};}
    snapshot.act=8;snapshot.document=state.document;snapshot.bytes=body(state.document);
    editor_store::Snapshot checkpoint;CHECK(!editor_project::store_snapshot(state,snapshot,"Unsupported ninth-act draft",checkpoint,error));
    CHECK(!error.empty());(void)manifest;
    const auto before=runtime_host::store.snapshot();const auto live=native[0].snapshot.bytes;
    const auto enters=native[0].enters,restores=native[0].restores,saves=native[0].save_as_calls,registrations=native[0].registrations;
    // An unsupported context cannot become a recovery checkpoint.
    CHECK(!runtime_host::recovery&&!runtime_host::native_attached&&!runtime_host::frozen);
    CHECK(runtime_host::store.snapshot()==before&&native[0].snapshot.bytes==live);
    CHECK(native[0].enters==enters&&native[0].restores==restores&&native[0].save_as_calls==saves&&native[0].registrations==registrations);
}
static void test_act_limit_recovery_checks_loaded_context_before_restore() {
    Party party;std::string error;end_act_fixture();side=0;
    const auto before=native[0].snapshot.bytes;const auto restores=native[0].restores,saves=native[0].save_as_calls;
    runtime_host::recovery_available=true;
    // A mismatched already-open ordinary editor must remain intact.
    native[0].state.act=8;runtime_host::execute(runtime_host::Action::Recover);
    CHECK(!runtime_host::recovery&&native[0].snapshot.bytes==before&&native[0].restores==restores&&native[0].save_as_calls==saves);
    // Metadata is a supported act, but this controlled asynchronous native entry lands in
    // a different act. Check it again before restoring or opening a copy wizard.
    native[0].state.editing=native[0].state.ready=false;const auto enters=native[0].enters;
    runtime_host::execute(runtime_host::Action::Recover);
    CHECK(native[0].enters==enters+1&&!runtime_host::recovery&&!runtime_host::native_attached&&!runtime_host::frozen);
    CHECK(native[0].snapshot.bytes==before&&native[0].restores==restores&&native[0].save_as_calls==saves);
}
static void test_act_limit_nonzero_baseline_rejects_before_native_entry() {
    Party party;std::string error;side=0;
    auto state=runtime_host::controller->state();auto resources=runtime_host::resources;
    end_act_fixture();side=0;
    state.document.acts.insert({(3ull<<32)|8,1});
    for(auto& [id,p]:state.document.placements){(void)id;p.act_id={(3ull<<32)|8,1};}
    resources.act=8;std::vector<dbpf::Record> records{{resources.root,body(state.document)}};
    CHECK(net::adventure_content::make_bundle(resources.root,std::move(records),resources.bundle,error));
    resources.base_root=std::make_shared<const std::vector<uint8_t>>(resources.bundle.records.front().data);
    std::vector<uint8_t> envelope;CHECK(!editor_project::encode_resources(resources,envelope,error));
    CHECK(!error.empty());
    // Reject an unsupported act at the resource boundary before offering it.
    side=1;
    native[1].state.editing=native[1].state.ready=false;
    const auto live=native[1].snapshot.bytes;const auto enters=native[1].enters,restores=native[1].restores,registrations=native[1].registrations;
    CHECK(runtime_guest::controller->view().phase==es::Phase::Idle);
    CHECK(native[1].snapshot.bytes==live&&native[1].enters==enters&&native[1].restores==restores&&native[1].registrations==registrations);
    CHECK(!native[1].state.editing&&!runtime_guest::native_attached);
}
static void test_independent_acts_share_one_group_start_and_restore_local_views() {
    Party party(0,0xA5,false,false,false,4,1);std::string error;
    CHECK(runtime_host::controller->state().document.acts.size()==4);
    side=1;CHECK(editor_native::select_act(2,error));
    const auto canonical=runtime_host::controller->state();
    const auto host_view=native[0].view,guest_view=native[1].view;
    CHECK(native[0].state.act==1&&native[1].state.act==2);
    side=0;CHECK(runtime_host::controller->prepare_playtest(error,ew::PlaytestMode::Together,{},editor::ObjectId{(3ull<<32)|3,1}));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Preparing;}));
    CHECK(runtime_host::controller->view().playtest_act==(editor::ObjectId{(3ull<<32)|3,1}));
    CHECK(runtime_guest::controller->view().playtest_act==runtime_host::controller->view().playtest_act);
    side=1;CHECK(runtime_guest::controller->ready_playtest(true,error));
    side=0;CHECK(runtime_host::controller->ready_playtest(true,error));
    CHECK(spin([]{return native[0].state.playtesting&&native[1].state.playtesting;}));
    CHECK(native[0].state.act==3&&native[1].state.act==3);
    CHECK(runtime_host::preview_selection->act==1&&runtime_guest::preview_selection->act==2);
    side=0;native[0].preview->return_editor();
    CHECK(spin([]{
        for(unsigned p=0;p<2;++p)if(native[p].cleanup)complete_cleanup(p);
        return runtime_host::controller->view().phase==es::Phase::Ready&&runtime_guest::controller->view().phase==es::Phase::Ready;
    }));
    CHECK(native[0].state.act==1&&native[1].state.act==2);
    CHECK(native[0].view==host_view&&native[1].view==guest_view);
    CHECK(runtime_host::controller->state()==canonical&&runtime_guest::controller->state()==canonical);
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}
static void test_roster_history_uses_one_authoring_hold();
static void test_captain_pose_history_has_one_settings_owner();
static void test_all_act_boundaries() {
    test_roster_history_uses_one_authoring_hold();
    test_captain_pose_history_has_one_settings_owner();
    test_independent_acts_share_one_group_start_and_restore_local_views();
    test_act_limit_begin_and_async_saved_recheck();
    test_act_limit_recovery_rejects_before_native_or_save();
    test_act_limit_recovery_checks_loaded_context_before_restore();
    test_act_limit_nonzero_baseline_rejects_before_native_entry();
}
static void test_roster_history_uses_one_authoring_hold(){
    seed_act_settings=true;
    {
        Party party(0,0xA5,false,false,false,4);std::string error;
        side=1;runtime_guest::edit_acts(0);
        CHECK(spin([]{return runtime_host::controller->state().document.revision==1 && runtime_guest::controller->state().document.revision==1 && !runtime_guest::awaiting_operation;},true,15000));
        CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
        CHECK(runtime_host::controller->state().document.act_order.size()==5);
        const auto& history=runtime_guest::controller->state().history;
        CHECK(history.size()==1 && history.back().undo.acts && history.back().undo.fields.size()==80);
        for(bool redo:{false,true}){
            side=1;std::vector<editor::LeaseToken> held;
            for(const auto& lease:runtime_guest::controller->view().local_leases)held.push_back({lease.scope,lease.token});
            if(!held.empty())CHECK(runtime_guest::controller->release_leases(held,error));
            CHECK(spin([]{return runtime_guest::controller->view().local_leases.empty();}));
            side=1;runtime_guest::undo_redo(redo);
            CHECK(runtime_guest::requested_scopes==(std::vector<editor::Scope>{{editor::ScopeKind::Authoring,{},{}}}));
            const auto revision=redo?3u:2u;
            CHECK(spin([&]{return runtime_host::controller->state().document.revision==revision && runtime_guest::controller->state().document.revision==revision && !runtime_guest::awaiting_operation;},true,15000));
            CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
            CHECK(runtime_host::controller->state().document.act_order.size()==(redo?5u:4u));
            CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
        }
    }
    seed_act_settings=false;
}
static void test_captain_pose_history_has_one_settings_owner(){
    using namespace editor_native;seed_act_settings=true;
    {
        Party party;std::string error;side=1;
        CHECK(!runtime_guest::authoring_gate(kCaptainObject));
        CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
        side=1;CHECK(runtime_guest::authoring_gate(kCaptainObject));
        Gesture gesture;gesture.generation=native[1].state.generation;gesture.before=native[1].snapshot;gesture.after=gesture.before;
        auto& properties=gesture.after.document.settings.at(kCaptainObject).properties;
        properties[kCaptainPositionProperty]=editor::Vec3{1,2,3};properties[kCaptainRotationProperty]=editor::Rotation{0,0,1,0};
        gesture.after.bytes=body(gesture.after.document);native[1].snapshot=gesture.after;runtime_guest::captured(std::move(gesture));
        CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&!runtime_guest::awaiting_operation;}));
        CHECK(runtime_guest::controller->state().history.back().undo.fields.size()==2);
        for(bool redo:{false,true}){
            side=1;std::vector<editor::LeaseToken> held;
            for(const auto& lease:runtime_guest::controller->view().local_leases)held.push_back({lease.scope,lease.token});
            if(!held.empty())CHECK(runtime_guest::controller->release_leases(held,error));
            CHECK(spin([]{return runtime_guest::controller->view().local_leases.empty();}));
            side=1;runtime_guest::undo_redo(redo);
            CHECK(runtime_guest::requested_scopes==(std::vector<editor::Scope>{{editor::ScopeKind::Settings,kCaptainObject,{}}}));
            CHECK(spin([&]{return runtime_guest::controller->state().document.revision==(redo?3u:2u)&&!runtime_guest::awaiting_operation;}));
            const auto value=editor::value(runtime_guest::controller->state().document,kCaptainObject,{editor::Field::Property,kCaptainPositionProperty});
            CHECK(value==editor::Value{redo?editor::Vec3{1,2,3}:editor::Vec3{}});
            CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
        }
    }
    seed_act_settings=false;
}

static void test_bounded_stage_diagnostics() {
    Party party;std::string error;using S=editor_stage_metrics::Stage;
    side=0;CHECK(runtime_host::debug({"stages","reset"}).find("clock=win32_qpc")!=std::string::npos);
    side=1;CHECK(runtime_guest::debug({"stages","reset"}).find("clock=win32_qpc")!=std::string::npos);
    CHECK(runtime_host::stage_metrics.recording()&&runtime_guest::stage_metrics.recording());
    CHECK(runtime_guest::controller->request_leases({{editor::ScopeKind::Placement,placement,{}}},15000,error));
    CHECK(spin([]{return !runtime_guest::controller->view().local_leases.empty();}));
    runtime_guest::guarded_placements[placement]=runtime_guest::controller->state().document.placements.at(placement);
    editor_native::Gesture gesture;gesture.generation=native[1].state.generation;gesture.before=native[1].snapshot;gesture.after=gesture.before;
    gesture.after.document.placements.at(placement).position={3,2,1};gesture.after.bytes=body(gesture.after.document);
    native[1].snapshot=gesture.after;side=1;runtime_guest::captured(std::move(gesture));
    CHECK(runtime_guest::awaiting_operation.has_value());const auto operation=*runtime_guest::awaiting_operation;
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&!runtime_guest::awaiting_operation;}));
    side=0;runtime_host::debug({"stages","stop"});side=1;runtime_guest::debug({"stages","stop"});
    CHECK(!runtime_host::stage_metrics.recording()&&!runtime_guest::stage_metrics.recording());
    CHECK(!runtime_host::stage_metrics.overflow()&&!runtime_guest::stage_metrics.overflow());
    const auto project=runtime_host::controller->state().document.project;
    const auto first=[&](const auto& metrics,S stage){uint64_t qpc=0;for(size_t i=0;i<metrics.size();++i){auto event=metrics.at(i);CHECK(event&&event->project==project&&event->operation==operation);if(event->stage==stage&&!qpc)qpc=event->qpc;}CHECK(qpc);return qpc;};
    uint64_t previous=0;
    for(auto stage:{S::HostRequestReceived,S::ValidateBegin,S::ValidateEnd,S::ApplyWorkBegin,S::NativeApplyBegin,S::NativeApplyEnd,S::ApplyWorkEnd,S::PersistWorkBegin,S::DurableBegin,S::DurableEnd,S::PersistWorkEnd,S::HostDecisionQueued}){
        auto now=first(runtime_host::stage_metrics,stage);CHECK(now>=previous);previous=now;}
    previous=0;
    for(auto stage:{S::LocalSubmit,S::DecisionDecoded,S::ApplyWorkBegin,S::NativeApplyBegin,S::NativeApplyEnd,S::ApplyWorkEnd,S::PersistWorkBegin,S::DurableBegin,S::DurableEnd,S::PersistWorkEnd,S::OwnConfirmed}){
        auto now=first(runtime_guest::stage_metrics,stage);CHECK(now>=previous);previous=now;}
    CHECK(first(runtime_guest::stage_metrics,S::LocalSubmit)<=first(runtime_host::stage_metrics,S::HostRequestReceived));
    CHECK(first(runtime_host::stage_metrics,S::HostDecisionQueued)<=first(runtime_guest::stage_metrics,S::DecisionDecoded));
    const auto before=runtime_guest::stage_metrics.size();
    CHECK(runtime_guest::debug({"stages","read","0","2"}).find("end=2")!=std::string::npos);
    CHECK(runtime_guest::debug({"stages","read","4097"}).starts_with("Usage:"));
    CHECK(runtime_guest::debug({"stages","read","0","129"}).starts_with("Usage:"));
    CHECK(runtime_guest::stage_metrics.size()==before&&!runtime_guest::stage_metrics.recording());
    CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
}

static void test_passive_accounting_does_not_touch_native_or_durable_state() {
    Party party;capture_local_create();CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&!runtime_host::awaiting_operation&&runtime_host::controller->settled();}));
    side=0;const auto canonical=runtime_host::controller->state();const auto snapshot=runtime_host::store.snapshot();
    const auto bytes=native[0].snapshot.bytes;const auto restores=native[0].restores,saves=native[0].save_calls;const auto queued=tasks[0].size();
    native[0].state.ready=false;dispatching=true;
    const auto first=runtime_host::debug({"accounting"}),second=runtime_host::debug({"accounting"});dispatching=false;
    CHECK(first==second&&first.size()<8192&&first.find("not allocator or process bytes")!=std::string::npos);
    CHECK(first.find("\nhistory=1\n")!=std::string::npos&&first.find("\ndecisions=1\n")!=std::string::npos);
    CHECK(first.find("\ntransport_connections=1\n")!=std::string::npos);
    CHECK(first.find("\ncoordinator_decisions=1\n")!=std::string::npos&&first.find("\ndecision_limit=1024\n")!=std::string::npos);
    CHECK(first.find("\nstore_document_size="+std::to_string(snapshot.document.size())+"\n")!=std::string::npos);
    CHECK(first.find("\nnative_current_size="+std::to_string(runtime_host::current_native.bytes.size())+"\n")!=std::string::npos);
    CHECK(runtime_host::controller->state()==canonical&&runtime_host::store.snapshot()==snapshot);
    CHECK(native[0].snapshot.bytes==bytes&&native[0].restores==restores&&native[0].save_calls==saves&&tasks[0].size()==queued);
    CHECK(!runtime_host::frozen);native[0].state.ready=true;
    runtime_host::end();CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&!runtime_host::closing;}));
    side=0;const auto detached=runtime_host::debug({"accounting"});
    CHECK(detached.find("\nhistory=0\n")!=std::string::npos&&detached.find("\ncoordinator_history=1\n")!=std::string::npos);
}
static void test_saved_root_diagnostic_distinguishes_native_local_alias() {
    Party party;side=0;
    native[0].preserve_live_resource_key_on_restore=native[1].preserve_live_resource_key_on_restore=true;
    const auto guest_alias=native[1].snapshot.source;
    native[0].snapshot.source={saved_key.instance,saved_key.type,saved_key.group};
    simulate_host_saved(native[0].snapshot.source);
    CHECK(spin([]{const auto saved=runtime_host::controller->saved_coverage();return saved.current&&saved.applied==2&&saved.expected==2&&runtime_host::controller->settled();}));
    CHECK(native[0].snapshot.source==editor::ResourceKey{saved_key.instance,saved_key.type,saved_key.group});
    CHECK(native[1].snapshot.source==guest_alias&&guest_alias!=native[0].snapshot.source);
    CHECK(runtime_host::current_native.source==runtime_guest::current_native.source);
    CHECK(runtime_guest::current_native.source==editor::ResourceKey{saved_key.instance,saved_key.type,saved_key.group});
    CHECK(native[0].snapshot.bytes==native[1].snapshot.bytes);
    const auto host_state=runtime_host::controller->state(),guest_state=runtime_guest::controller->state();
    const auto host_store=runtime_host::store.snapshot(),guest_store=runtime_guest::store.snapshot();
    for(side=0;side<2;++side){
        const auto before=native[side].snapshot;const auto restores=native[side].restores,saves=native[side].save_calls,saveas=native[side].save_as_calls;
        const auto text=side?runtime_guest::debug({"status"}):runtime_host::debug({"status"});
        CHECK(text.find("saved_source_root=408A0000!00000065.366A930D")!=std::string::npos);
        CHECK(native[side].snapshot.source==before.source&&native[side].snapshot.bytes==before.bytes);
        CHECK(native[side].restores==restores&&native[side].save_calls==saves&&native[side].save_as_calls==saveas);
    }
    CHECK(runtime_host::controller->state()==host_state&&runtime_guest::controller->state()==guest_state);
    CHECK(runtime_host::store.snapshot()==host_store&&runtime_guest::store.snapshot()==guest_store);
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}


static void complete_native_save(unsigned player,editor::ResourceKey key) {
    side=int(player);auto& n=native[player];CHECK(n.save_busy);
    n.save_busy=false;n.snapshot.source=key;n.save_callbacks.saved(key);
}
static void test_automatic_unsaved_start_and_native_host_save() {
    Party party(0,0xA5,true,true);
    CHECK(runtime_host::active()&&runtime_guest::active());
    CHECK(!runtime_host::controller->view().saved&&!runtime_guest::controller->view().saved);
    CHECK(!native[0].save_calls&&!native[0].save_as_calls&&!native[1].save_as_calls);
    CHECK(runtime_host::resources.root.instance&&runtime_host::resources.root.group==editor_native::creation_group());
    CHECK(runtime_host::resources.root==runtime_guest::resources.root);
    CHECK(runtime_host::source_creation==editor::ResourceKey{}&&!runtime_host::native_copy_owned);
    CHECK(runtime_host::store.healthy()&&runtime_guest::store.healthy());
    side=0;native[0].save_callbacks.request(editor_native_save::Request::Save);
    CHECK(spin([]{return native[0].save_busy;}));CHECK(native[0].save_as_calls==1&&!native[0].save_calls);
    complete_native_save(0,{saved_key.instance,saved_key.type,saved_key.group});
    CHECK(spin([]{return runtime_host::controller->saved_coverage().applied==2;}));
    CHECK(runtime_host::native_copy_owned&&runtime_host::resources.root==saved_key);
    side=0;native[0].save_callbacks.request(editor_native_save::Request::Save);
    CHECK(spin([]{return native[0].save_busy;}));CHECK(native[0].save_calls==1);
    native[0].save_busy=false;native[0].save_callbacks.cancelled();
    CHECK(spin([]{return !runtime_host::frozen&&!runtime_guest::frozen;}));
}
static void test_native_guest_export_isolation_and_cancel() {
    Party party(0,0xA5,true,true);
    const auto root=runtime_host::resources.root;
    const auto doc=runtime_host::controller->state().document;
    const auto guest_identity=native[1].snapshot.source;
    side=1;native[1].save_callbacks.request(editor_native_save::Request::Save);
    CHECK(spin([]{return native[1].save_busy;}));
    CHECK(runtime_host::exporting&&runtime_guest::exporting);
    CHECK(native[1].save_as_calls==1&&!native[0].save_calls&&!native[0].save_as_calls);
    // An exporter cannot grant completion for a different peer or serial.
    side=0;auto wrong=runtime_host::exporting->command;wrong.kind=editor::wire::CommandKind::FinishExport;
    wrong.actor=runtime_host::controller->view().local;wrong.accepted=true;runtime_host::receive_command(1,wrong);
    CHECK(runtime_host::exporting.has_value());
    complete_native_save(1,{909,0x366A930D,0x408A0000});
    CHECK(spin([]{return !runtime_host::exporting&&!runtime_guest::exporting;}));
    CHECK(runtime_host::resources.root==root&&runtime_guest::resources.root==root);
    CHECK(runtime_host::controller->state().document==doc&&runtime_guest::controller->state().document==doc);
    CHECK(!runtime_host::controller->view().saved&&!runtime_guest::controller->view().saved);
    CHECK(native[1].snapshot.source==guest_identity&&!runtime_guest::native_copy_owned);
    side=1;native[1].save_callbacks.request(editor_native_save::Request::Save);
    CHECK(spin([]{return native[1].save_busy;}));native[1].save_busy=false;native[1].save_callbacks.cancelled();
    CHECK(spin([]{return !runtime_host::exporting&&!runtime_guest::exporting;}));
    CHECK(runtime_host::controller->state().document==doc&&runtime_guest::controller->state().document==doc);
}
static void test_native_guest_playtest_request_requires_host_and_all_ready() {
    Party party(0,0xA5,true,true);
    side=1;runtime_guest::playtest(); // The separate Test Together action.
    CHECK(spin([]{return runtime_host::view().test_request;}));
    CHECK(runtime_host::controller->view().phase==es::Phase::Ready&&!native[0].preview&&!native[1].preview);
    side=0;runtime_host::approve_playtest(true);
    CHECK(spin([]{return runtime_host::view().confirming&&runtime_guest::view().confirming;}));
    CHECK(spin([]{return !runtime_guest::pending_test_request;}));
    CHECK(!runtime_host::view().ready&&!runtime_guest::view().ready);
    side=1;runtime_guest::set_ready(true);
    CHECK(spin([]{return runtime_guest::view().ready;}));CHECK(!native[0].preview&&!native[1].preview);
    side=0;runtime_host::set_ready(true);
    CHECK(spin([]{return native[0].state.playtesting&&native[1].state.playtesting;}));
}
static void test_native_solo_play_holds_and_restores_without_coop(){
    for(unsigned tester=0;tester<2;++tester){
        Party party(0,0xA5,true,true);
        const auto state=runtime_host::controller->state();
        const auto views=std::array{native[0].view,native[1].view};
        side=int(tester);native[tester].save_callbacks.request(editor_native_save::Request::Play);
        CHECK(spin([&]{return tester?runtime_guest::view().test_menu:runtime_host::view().test_menu;}));
        CHECK(!native[0].state.playtesting&&!native[1].state.playtesting);
        side=int(tester);if(tester)runtime_guest::choose_test(0);else runtime_host::choose_test(0);
        CHECK(spin([&]{return runtime_host::controller->view().phase==es::Phase::Playing && runtime_guest::controller->view().phase==es::Phase::Playing && native[tester].state.playtesting;}));
        CHECK(!native[1-tester].state.playtesting && native[1-tester].state.ready);
        CHECK(!native[0].preview && !native[1].preview && !native[0].active && !native[1].active);
        side=0;CHECK(runtime_host::authoring());side=1;CHECK(runtime_guest::authoring());
        CHECK(!runtime_host::view().confirming && !runtime_guest::view().confirming);
        side=int(1-tester);CHECK(!native[1-tester].callbacks.gate({placement}));
        const auto old_round=runtime_host::controller->view().playtest_round;
        const auto& held=runtime_host::controller->view();
        const runtime_host::PreviewReturnScope old_scope{held.context.project,held.context.epoch,held.playtest_round,held.playtest_revision};
        side=int(tester);native[tester].save_callbacks.return_requested();
        CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready && runtime_guest::controller->view().phase==es::Phase::Ready;}));
        CHECK(native[0].view==views[0] && native[1].view==views[1]);
        CHECK(runtime_host::controller->state()==state && runtime_guest::controller->state()==state);
        CHECK(native[tester].stops==1 && native[1-tester].stops==0);
        side=int(tester);native[tester].save_callbacks.request(editor_native_save::Request::Play);
        CHECK(spin([&]{return tester?runtime_guest::view().test_menu:runtime_host::view().test_menu;}));
        side=int(tester);if(tester)runtime_guest::choose_test(0);else runtime_host::choose_test(0);
        CHECK(spin([&]{return native[tester].state.playtesting && runtime_host::controller->view().playtest_round>old_round;}));
        side=0;runtime_host::pending_solo_return=runtime_host::PreviewReturnScope{old_scope.project,old_scope.epoch,old_scope.round,old_scope.revision};
        step();CHECK(native[tester].state.playtesting && native[tester].stops==1);
        side=int(tester);native[tester].save_callbacks.return_requested();
        CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Ready && runtime_guest::controller->view().phase==es::Phase::Ready;}));
        CHECK(!runtime_host::frozen && !runtime_guest::frozen);
    }
}
static void test_solo_tester_disconnect_releases_party_hold(){
    Party party(0,0xA5,true,true);
    const auto document=runtime_host::controller->state().document;
    side=1;native[1].save_callbacks.request(editor_native_save::Request::Play);
    CHECK(spin([]{return runtime_guest::view().test_menu;}));side=1;runtime_guest::choose_test(0);
    CHECK(spin([]{return native[1].state.playtesting && runtime_host::controller->view().phase==es::Phase::Playing;}));
    side=1;runtime_guest::session_ended();sessions[1].leave();
    CHECK(spin([]{return native[1].state.ready && runtime_host::controller->view().phase==es::Phase::Ready && !runtime_guest::transport_teardown;}));
    CHECK(runtime_host::controller->state().document==document && !runtime_host::frozen && native[1].stops==1);
}
static void test_automatic_handoff_preserves_local_draft_on_cancel() {
    Party party(0,0xA5,true,true,true);
    const auto local=native[1].snapshot;
    CHECK(runtime_guest::view().join_prompt&&!runtime_guest::native_attached);
    const auto prior_prompt=runtime_guest::view().prompt_identity;
    side=1;native[1].save_callbacks.exit_cancelled();
    CHECK(spin([&]{return runtime_guest::view().join_prompt && runtime_guest::view().prompt_identity!=prior_prompt;}));
    side=1;runtime_guest::resolve_join(2);
    CHECK(spin([]{return !sessions[1].connected();}));
    CHECK(native[1].snapshot.bytes==local.bytes&&native[1].snapshot.source==local.source&&native[1].state.editing);
    CHECK(!native[1].save_calls&&!native[1].save_as_calls);
}
static void test_automatic_handoff_discard_then_join() {
    Party party(0,0xA5,true,true,true);
    side=1;runtime_guest::resolve_join(1);
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Ready&&runtime_guest::native_attached;}));
    CHECK(sessions[1].connected()&&!native[1].save_calls&&!native[1].save_as_calls);
    CHECK(runtime_guest::resources.root==runtime_host::resources.root);
}
static void test_native_exit_host_keeps_party_guest_leaves() {
    {
        Party party(0,0xA5,true,true);
        side=0;native[0].save_callbacks.request(editor_native_save::Request::Exit);
        CHECK(spin([]{return !native[0].state.editing&&!native[1].state.editing;}));
        CHECK(sessions[0].connected()&&sessions[1].connected());
    }
    {
        Party party(0,0xA5,true,true);
        side=1;native[1].save_callbacks.request(editor_native_save::Request::Exit);
        CHECK(spin([]{return !sessions[1].connected()&&!native[1].state.editing;}));
        CHECK(native[0].state.editing&&runtime_host::active());
    }
}
static void test_retained_cast_import_owner_scope() {
    Party party;side=0;
    const auto owner=native[0].callbacks.import_scope();
    CHECK(owner.epoch && owner==native[0].callbacks.gesture_scope());
    CHECK(!native[0].callbacks.transaction_pending());
    runtime_host::awaiting_operation=editor::OperationId{owner.author,1,owner.epoch};
    CHECK(native[0].callbacks.transaction_pending());
    CHECK(!native[0].callbacks.gesture_scope().epoch);
    CHECK(native[0].callbacks.import_scope()==owner);
    runtime_host::save_publication_pending=true;CHECK(!native[0].callbacks.import_scope().epoch);
    runtime_host::save_publication_pending=false;
    runtime_host::closing=true;CHECK(!native[0].callbacks.import_scope().epoch);
    runtime_host::closing=false;
    runtime_host::native_attached=false;CHECK(!native[0].callbacks.import_scope().epoch);
    runtime_host::native_attached=true;runtime_host::awaiting_operation.reset();
    CHECK(!native[0].callbacks.transaction_pending());
    CHECK(native[0].callbacks.import_scope()==owner && native[0].callbacks.gesture_scope()==owner);
}
static void test_cast_property_undo_then_remove_refreshes_ownership_snapshot(){
    seed_cast_settings=true;Party party;seed_cast_settings=false;side=0;
    const editor::ObjectId settings_id{(5ull<<32)|1,1};
    CHECK(!native[0].callbacks.class_gate(1));CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));side=0;
    CHECK(native[0].callbacks.class_gate(1));
    editor_native::Gesture edit;edit.generation=native[0].state.generation;edit.before=native[0].snapshot;edit.after=edit.before;
    std::get<editor::StructuredValue>(edit.after.document.class_settings.at(settings_id).properties.at(editor_native::kCastBlockProperty)).bytes={3,2,1};
    edit.after.bytes=body(edit.after.document);native[0].snapshot=edit.after;runtime_host::captured(std::move(edit));
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==1&&!runtime_host::awaiting_operation;}));
    CHECK(spin([]{return runtime_host::controller->view().local_leases.empty();}));side=0;
    CHECK(!native[0].callbacks.class_gate(1));CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));side=0;
    CHECK(native[0].callbacks.class_gate(1));CHECK(runtime_host::guarded_settings.at(settings_id).revision==1);
    runtime_host::undo();CHECK(spin([]{return runtime_guest::controller->state().document.revision==2&&!runtime_host::awaiting_operation;}));
    CHECK(spin([]{side=0;return native[0].callbacks.remove_class_gate(1);}));side=0;
    CHECK(runtime_host::guarded_settings.at(settings_id).revision==2);
    editor_native::Gesture remove;remove.generation=native[0].state.generation;remove.before=native[0].snapshot;remove.after=remove.before;
    remove.after.document.classes.erase(class_id);remove.after.document.class_settings.erase(settings_id);remove.after.document.placements.clear();
    remove.after.bytes=body(remove.after.document);native[0].snapshot=remove.after;runtime_host::captured(std::move(remove));
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==3&&!runtime_host::awaiting_operation;}));
    CHECK(runtime_host::controller->state().document.classes.empty()&&runtime_guest::controller->state().document.placements.empty());
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}
static void test_all_native_flow_cases() {
    {
        Party party;side=0;const auto requests=runtime_host::controller->accounting().pending_leases;
        CHECK(!native[0].callbacks.gate({{123456,1}}));
        CHECK(runtime_host::controller->accounting().pending_leases==requests);
    }
    test_cast_property_undo_then_remove_refreshes_ownership_snapshot();
    test_retained_cast_import_owner_scope();
    test_native_solo_play_holds_and_restores_without_coop();test_solo_tester_disconnect_releases_party_hold();
    test_automatic_unsaved_start_and_native_host_save();
    test_native_guest_export_isolation_and_cancel();
    test_native_guest_playtest_request_requires_host_and_all_ready();
    test_automatic_handoff_preserves_local_draft_on_cancel();
    test_automatic_handoff_discard_then_join();
    test_native_exit_host_keeps_party_guest_leaves();
    {
        Party party(0,0xA5,true,true);native[0].deferred_native_exit=true;
        side=0;native[0].save_callbacks.request(editor_native_save::Request::Exit);
        CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Idle&&!runtime_host::native_attached&&!runtime_host::native_exit_requested;}));
        CHECK(native[0].state.editing);const auto registrations=native[0].registrations;
        for(unsigned i=0;i<12;++i)step();
        CHECK(runtime_host::controller->view().phase==es::Phase::Idle&&!runtime_host::frozen&&native[0].registrations==registrations);
        side=0;runtime_host::refresh();CHECK(!runtime_host::cached.can_begin);
        native[0].state.editing=native[0].state.ready=false;++native[0].state.generation;step();
        CHECK(!runtime_host::native_exit_generation&&!runtime_host::frozen);
    }
}

// Production runtime callbacks, UDP ownership and durable storage are exercised
// here. Native creator scenes and content bytes remain controlled test doubles.
struct CreationFixture {
    unsigned player;
    editor_creation::Scope scope;
    editor_creation::Request request;
};
static CreationFixture creation_request(unsigned player,uint32_t family,std::optional<editor::ObjectId> target={}){
    CHECK(spin([]{return runtime_host::controller->settled() && !runtime_host::durable_commit && !runtime_guest::durable_commit &&
        !runtime_host::awaiting_operation && !runtime_guest::awaiting_operation;}));
    side=int(player);auto& context=editor_creation::fixture_contexts[player];const auto owner=context.scope();CHECK(owner.has_value());
    static uint64_t serial=100;
    CreationFixture value{player,{owner->project,owner->epoch,owner->author,++serial},{0,{0,family,0},target}};
    if(target){
        const auto& document=player?runtime_guest::controller->state().document:runtime_host::controller->state().document;
        for(const auto& [id,row]:document.class_settings)if(row.class_id==*target){(void)id;value.request.source=std::get<editor::ResourceKey>(row.properties.at(editor::kClassAssetResourceProperty));}
        CHECK(value.request.source.instance);
    }
    creation_active[player]=true;return value;
}
static void creation_enter(CreationFixture& value){
    side=int(value.player);auto& context=editor_creation::fixture_contexts[value.player];std::string error;
    CHECK(context.prepare(value.scope,value.request,error)==editor_creation::PrepareResult::Wait);
    CHECK(error.empty());
    CHECK(spin([&]{side=int(value.player);return context.prepare(value.scope,value.request,error)!=editor_creation::PrepareResult::Wait;}));
    CHECK(error.empty());CHECK(context.current(value.scope));
    if(value.request.target)CHECK(context.lease_valid(value.scope,*value.request.target));
    auto& n=native[value.player];n.state.editing=n.state.ready=false;++n.state.generation;n.state.selected.clear();
    n.view.target={-50,-60,-70};n.view.orbit={1,2,3};
}
static void creation_return(CreationFixture& value,bool cancelled=true,std::string* failure=nullptr){
    const editor::ResourceKey saved{0x29001000u+value.player,value.request.source.type,0x408A0000};
    editor_creation::Result result{value.request,saved,cancelled};auto& context=editor_creation::fixture_contexts[value.player];
    bool okay=false;std::string error;
    if(cancelled){
        const auto views=native[value.player].view_restores;
        // Enter/restore cannot acknowledge a usable scene in the same frame.
        for(unsigned same_frame=0;same_frame<3;++same_frame){side=int(value.player);CHECK(!context.returned(value.scope,result,error) && error.empty());}
        CHECK(native[value.player].view_restores==views);
    }
    CHECK(spin([&]{side=int(value.player);error.clear();okay=context.returned(value.scope,result,error);return okay||!error.empty();}));
    if(failure)*failure=error;else CHECK(okay && error.empty());
    side=int(value.player);creation_active[value.player]=false;context.finished(value.scope,okay,error);
    CHECK(spin([&]{return value.player?!runtime_guest::local_creation:!runtime_host::local_creation;}));
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}
static void creation_field(unsigned player,editor::ObjectId target,editor::FieldKey field,editor::Value value,editor::Scope scope){
    side=int(player);auto* controller=player?runtime_guest::controller.get():runtime_host::controller.get();std::string error;
    CHECK(controller->request_leases({scope},15000,error));
    CHECK(spin([&]{return std::any_of(controller->view().local_leases.begin(),controller->view().local_leases.end(),[&](const auto& held){return held.scope==scope;});}));
    auto request=player?runtime_guest::request(editor::Intent::Forward):runtime_host::request(editor::Intent::Forward);
    request.summary="Creation concurrency fixture edit";
    editor::Mutation mutation;mutation.target=target;mutation.field=field;mutation.value=std::move(value);
    mutation.expected=editor::field_revision(controller->state().document,target,field);request.mutations={std::move(mutation)};
    if(controller->state().document.class_settings.contains(target)){
        auto changed=player?runtime_guest::current_native:runtime_host::current_native;
        changed.document.class_settings.at(target).properties[field.property]=*request.mutations.front().value;
        changed.bytes=body(changed.document,changed.bytes.front());std::vector<uint8_t> envelope;side=int(player);
        CHECK(player?runtime_guest::collect_cast_assets(changed,envelope,error):runtime_host::collect_cast_assets(changed,envelope,error));
        CHECK(controller->upload_assets(request.operation,envelope,error));
    }
    const auto revision=request.base_revision+1;side=int(player);CHECK(controller->submit(request,error));
    CHECK(spin([&]{return runtime_host::controller->state().document.revision==revision && runtime_guest::controller->state().document.revision==revision &&
        !runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
    std::vector<editor::LeaseToken> release;for(const auto& held:controller->view().local_leases)if(held.scope==scope)release.push_back({held.scope,held.token});
    side=int(player);CHECK(controller->release_leases(release,error));
    CHECK(spin([&]{return std::none_of(controller->view().local_leases.begin(),controller->view().local_leases.end(),[&](const auto& held){return held.scope==scope;});}));
}
static void test_creation_guest_entry_waits_for_host_echo(){
    seed_creation_settings=true;Party party;seed_creation_settings=false;
    auto value=creation_request(1,0x2399BE55);std::string error;side=1;
    auto& context=editor_creation::fixture_contexts[1];
    CHECK(context.prepare(value.scope,value.request,error)==editor_creation::PrepareResult::Wait);
    CHECK(runtime_guest::local_creation && native[1].state.ready);
    CHECK(context.prepare(value.scope,value.request,error)==editor_creation::PrepareResult::Wait);
    CHECK(!runtime_host::controller->view().members.at(1).presence.creating_asset);
    CHECK(spin([&]{side=1;return context.prepare(value.scope,value.request,error)==editor_creation::PrepareResult::Ready;}));
    const auto& echo=runtime_host::controller->view().members.at(1).presence;
    if(!echo.creating_asset || echo.creation_serial!=value.scope.serial || echo.scene_ready)
        std::printf("creation echo creating=%u serial=%llu expected=%llu scene_ready=%u guest_serial=%llu\n",unsigned(echo.creating_asset),static_cast<unsigned long long>(echo.creation_serial),static_cast<unsigned long long>(value.scope.serial),unsigned(echo.scene_ready),static_cast<unsigned long long>(runtime_guest::controller->view().members.at(1).presence.creation_serial));
    CHECK(echo.creating_asset && echo.creation_serial==value.scope.serial && !echo.scene_ready);
    CHECK(runtime_host::controller->view().phase==es::Phase::Ready);
    native[1].state.editing=native[1].state.ready=false;++native[1].state.generation;creation_return(value);
    CHECK(runtime_host::controller->state().history.empty() && runtime_host::controller->state().document.revision==0);
    CHECK(runtime_host::controller->state().document.classes.size()==1 && runtime_guest::controller->state().document.classes.size()==1);
}
static void test_creations_return_independently_to_latest_act_and_view(){
    seed_creation_settings=true;Party party(0,0xA5,false,false,false,3,1);seed_creation_settings=false;
    native[0].state.selected={placement};native[0].focus_on_selection=true;const auto view=native[0].view;
    auto host=creation_request(0,0x2B978C46,class_id);creation_enter(host);
    native[1].state.selected={editor_native::kCaptainObject};const auto guest_view=native[1].view;
    auto guest=creation_request(1,0x2399BE55);creation_enter(guest);
    const auto host_restores=native[0].restores;const auto host_scene=native[0].snapshot.bytes;
    CHECK(runtime_host::controller->view().phase==es::Phase::Ready && runtime_guest::controller->view().phase==es::Phase::Ready);
    CHECK(runtime_host::controller->state().history.empty());creation_return(guest);
    CHECK(runtime_host::controller->state().document.classes.size()==1 && runtime_host::controller->state().document.revision==0);
    CHECK(creation_active[0] && !native[0].state.ready && native[1].state.ready);
    CHECK(native[1].state.selected==std::vector<editor::ObjectId>{editor_native::kCaptainObject} && native[1].view==guest_view);
    creation_field(1,placement,{editor::Field::Position,0},editor::Vec3{31,2,9},{editor::ScopeKind::Placement,placement,{}});
    CHECK(native[0].restores==host_restores && native[0].snapshot.bytes==host_scene);
    CHECK(runtime_host::store.durable_revision()==1 && runtime_guest::store.durable_revision()==1);
    side=1;std::string act_error;CHECK(editor_native::select_act(0,act_error));runtime_guest::edit_acts(3);
    CHECK(spin([]{return runtime_guest::controller->state().document.revision==2 && runtime_host::controller->settled() && !runtime_guest::awaiting_operation;}));
    CHECK(runtime_host::controller->state().document.act_order.front()==editor::ObjectId{(3ull<<32)|1,1});
    CHECK(!native[0].state.ready && creation_active[0]);creation_return(host);
    CHECK(native[0].state.act==0 && native[0].snapshot.act==0);
    CHECK(native[0].state.selected==std::vector<editor::ObjectId>{placement} && native[0].view==view);
    CHECK(native[0].snapshot.document.placements.at(placement).position==editor::Vec3{31,2,9});
    CHECK(runtime_host::controller->state().history.size()==2 && runtime_host::controller->state()==runtime_guest::controller->state());
    CHECK(spin([]{return !runtime_host::controller->view().members.at(0).presence.creating_asset && !runtime_host::controller->view().members.at(1).presence.creating_asset;}));
}
static void test_creation_content_preserves_later_behavior_and_lease(){
    seed_creation_settings=true;creation_content_enabled=true;creation_version=1;Party party;seed_creation_settings=false;
    const editor::ObjectId settings{(5ull<<32)|1,1};const auto source=std::get<editor::ResourceKey>(runtime_host::controller->state().document.class_settings.at(settings).properties.at(editor::kClassAssetResourceProperty));
    auto host=creation_request(0,source.type,class_id);creation_enter(host);
    for(unsigned renewal=0;renewal<4;++renewal){
        clock_ms+=4001;step();
        CHECK(spin([&]{side=0;return editor_creation::fixture_contexts[0].lease_valid(host.scope,class_id) &&
            std::any_of(runtime_host::controller->view().local_leases.begin(),runtime_host::controller->view().local_leases.end(),[&](const auto& held){return held.scope.kind==editor::ScopeKind::Content && held.expires>clock_ms+10000;});}));
    }
    const editor::StructuredValue behavior{editor_native::kCastBlockFormat,{9,8,7},{}};
    creation_field(1,settings,{editor::Field::Property,editor_native::kCastBlockProperty},behavior,{editor::ScopeKind::Class,class_id,{}});
    side=0;CHECK(editor_creation::fixture_contexts[0].lease_valid(host.scope,class_id));
    CHECK(runtime_host::rendered_revision==0 && runtime_guest::rendered_revision==1);
    creation_return(host,false);
    CHECK(spin([]{return runtime_host::controller->settled() && runtime_guest::controller->state().document.revision==2;}));
    const auto& properties=runtime_host::controller->state().document.class_settings.at(settings).properties;
    const auto version=std::get<editor::ResourceKey>(properties.at(editor::kClassAssetResourceProperty));
    CHECK(version!=source && version.instance>>24==0x7D && properties.at(editor_native::kCastBlockProperty)==editor::Value{behavior});
    CHECK(runtime_host::controller->state().history.size()==2 && runtime_host::store.durable_revision()==2 && runtime_guest::store.durable_revision()==2);
    CHECK(runtime_host::controller->state()==runtime_guest::controller->state() && native[0].state.ready);
    creation_content_enabled=false;
}
static void test_creation_parallel_saves_allocate_canonical_content(){
    seed_creation_settings=true;creation_content_enabled=true;creation_version=1;Party party;seed_creation_settings=false;
    auto host=creation_request(0,0x2B978C46);creation_enter(host);
    auto guest=creation_request(1,0x2399BE55);creation_enter(guest);
    const auto before=native[0].snapshot.bytes;const auto restores=native[0].restores;
    creation_return(guest,false);
    CHECK(runtime_host::controller->state().document.revision==1 && runtime_host::controller->state().document.classes.size()==2);
    CHECK(native[0].snapshot.bytes==before && native[0].restores==restores && creation_active[0]);
    CHECK(runtime_host::store.durable_revision()==1 && runtime_guest::store.durable_revision()==1);
    const editor::ObjectId guest_class{(2ull<<32)|2,1},guest_settings{(5ull<<32)|2,1};
    const auto guest_key=std::get<editor::ResourceKey>(runtime_host::controller->state().document.class_settings.at(guest_settings).properties.at(editor::kClassAssetResourceProperty));
    CHECK(runtime_host::controller->state().document.classes.contains(guest_class) && guest_key.type==0x2399BE55 && guest_key.instance>>24==0x7D);
    creation_field(1,placement,{editor::Field::Position,0},editor::Vec3{6,5,4},{editor::ScopeKind::Placement,placement,{}});
    creation_return(host,false);
    CHECK(runtime_host::controller->state().document.revision==3 && runtime_host::controller->state().document.classes.size()==3);
    CHECK(runtime_host::controller->state().history.size()==3 && runtime_host::store.durable_revision()==3 && runtime_guest::store.durable_revision()==3);
    CHECK(runtime_host::controller->state()==runtime_guest::controller->state() && native[0].state.ready && native[1].state.ready);
    CHECK(std::count_if(runtime_host::resources.bundle.records.begin(),runtime_host::resources.bundle.records.end(),[](const auto& row){return row.key.type==0x1A99B06B;})==2);
    side=0;runtime_host::undo();CHECK(spin([]{return runtime_guest::controller->state().document.revision==4 && !runtime_host::awaiting_operation;}));
    CHECK(runtime_host::controller->state().document.classes.size()==2 && runtime_host::controller->state().document.classes.contains(guest_class));
    CHECK(runtime_host::controller->state().document.placements.at(placement).position==editor::Vec3{6,5,4});
    side=0;runtime_host::redo();CHECK(spin([]{return runtime_guest::controller->state().document.revision==5 && !runtime_host::awaiting_operation;}));
    CHECK(runtime_host::controller->state().document.classes.size()==3 && runtime_host::controller->state()==runtime_guest::controller->state());
    creation_content_enabled=false;
}
static void test_creation_stale_pending_source_and_lost_content_lease(){
    seed_creation_settings=true;Party party;seed_creation_settings=false;
    const editor::ObjectId settings{(5ull<<32)|1,1};auto guest=creation_request(1,0x2B978C46,class_id);std::string error;
    const editor::Scope content{editor::ScopeKind::Content,class_id,{}};side=0;CHECK(runtime_host::controller->request_leases({content},15000,error));
    CHECK(spin([]{return !runtime_host::controller->view().local_leases.empty();}));side=1;
    CHECK(editor_creation::fixture_contexts[1].prepare(guest.scope,guest.request,error)==editor_creation::PrepareResult::Wait);
    creation_field(0,settings,{editor::Field::Property,editor::kClassAssetResourceProperty},editor::ResourceKey{42,0x2B978C46,0x408A0000},content);
    side=1;CHECK(runtime_guest::controller->request_leases({content},15000,error));
    CHECK(spin([&]{return std::any_of(runtime_guest::controller->view().local_leases.begin(),runtime_guest::controller->view().local_leases.end(),[&](const auto& held){return held.scope==content;});}));
    CHECK(spin([&]{side=1;error.clear();return editor_creation::fixture_contexts[1].prepare(guest.scope,guest.request,error)==editor_creation::PrepareResult::Reject;}));
    CHECK(error.find("changed while ownership was pending")!=std::string::npos && !runtime_guest::local_creation);
    creation_active[1]=false;
    CHECK(spin([]{return runtime_guest::controller->view().local_leases.empty();}));
    auto host=creation_request(0,0x2B978C46,class_id);creation_enter(host);
    std::vector<editor::LeaseToken> tokens;for(const auto& held:runtime_host::controller->view().local_leases)if(held.scope==content)tokens.push_back({held.scope,held.token});
    side=0;CHECK(runtime_host::controller->release_leases(tokens,error));
    CHECK(spin([&]{side=0;return !editor_creation::fixture_contexts[0].lease_valid(host.scope,class_id);}));
    std::string rejection;creation_return(host,false,&rejection);
    CHECK(rejection.find("content lease expired")!=std::string::npos && runtime_host::controller->state().document.revision==1);
    CHECK(runtime_host::controller->state().history.size()==1 && runtime_guest::store.durable_revision()==1);
}
static void test_creation_save_and_play_holds_wait_for_local_return(){
    {
        seed_creation_settings=true;Party party;seed_creation_settings=false;
        auto host=creation_request(0,0x2B978C46);creation_enter(host);
        side=1;native[1].save_callbacks.request(editor_native_save::Request::Save);
        CHECK(spin([]{return !runtime_host::queued_exports.empty() && !runtime_guest::pending_export_request;}));
        CHECK(!runtime_host::exporting && !runtime_guest::exporting && !native[1].save_busy && !runtime_guest::pending_export_request);
        creation_field(1,placement,{editor::Field::Position,0},editor::Vec3{7,6,5},{editor::ScopeKind::Placement,placement,{}});
        creation_return(host);
        CHECK(spin([]{return native[1].save_busy && runtime_host::exporting && runtime_guest::exporting;}));
        side=1;native[1].save_busy=false;native[1].save_callbacks.cancelled();
        CHECK(spin([]{return !runtime_host::exporting&&!runtime_guest::exporting;}));
        CHECK(runtime_host::controller->state().document.revision==1 && runtime_guest::store.durable_revision()==1);
    }
    {
        seed_creation_settings=true;Party party;seed_creation_settings=false;
        auto host=creation_request(0,0x2B978C46);creation_enter(host);
        side=1;runtime_guest::playtest();CHECK(spin([]{return runtime_guest::deferred_test.has_value();}));
        CHECK(runtime_host::controller->view().phase==es::Phase::Ready && runtime_guest::controller->view().phase==es::Phase::Ready);
        CHECK(!native[0].state.playtesting && !native[1].state.playtesting);
        creation_return(host);
        CHECK(spin([]{return runtime_host::test_request.has_value();}));
        CHECK(!native[0].state.playtesting && !native[1].state.playtesting);
        side=0;runtime_host::approve_playtest(false);
        CHECK(spin([]{return !runtime_host::test_request && !runtime_guest::pending_test_request;}));
        CHECK(runtime_host::controller->state().document.revision==0 && runtime_host::controller->state().history.empty());
    }
}
static void test_creation_host_resource_collision_restores_without_partial_cast(){
    seed_creation_settings=true;creation_content_enabled=true;Party party;seed_creation_settings=false;
    auto host=creation_request(0,0x2B978C46);creation_enter(host);
    auto guest=creation_request(1,0x2399BE55);creation_enter(guest);
    const auto before=runtime_host::controller->state();const auto host_scene=native[0].snapshot.bytes;
    upload_collision[0]=true;std::string error;creation_return(guest,false,&error);
    CHECK(error.find("validated dependency upload")!=std::string::npos || error.find("conflicts with an installed resource")!=std::string::npos);
    CHECK(runtime_host::controller->state().document==before.document && runtime_guest::controller->state().document==before.document);
    CHECK(runtime_host::controller->state().history==before.history && runtime_guest::controller->state().history==before.history);
    CHECK(runtime_host::store.durable_revision()==0 && runtime_guest::store.durable_revision()==0);
    CHECK(!creation_records[1].empty() && native[1].state.ready);
    CHECK(creation_active[0] && native[0].snapshot.bytes==host_scene);
    upload_collision[0]=false;creation_return(host);creation_content_enabled=false;
}
static void test_all_creation_runtime_cases(){
    test_creation_guest_entry_waits_for_host_echo();
    test_creations_return_independently_to_latest_act_and_view();
    test_creation_content_preserves_later_behavior_and_lease();
    test_creation_parallel_saves_allocate_canonical_content();
    test_creation_stale_pending_source_and_lost_content_lease();
    test_creation_save_and_play_holds_wait_for_local_return();
    test_creation_host_resource_collision_restores_without_partial_cast();
}

int main(int argc,char** argv){if(argc==2&&std::string(argv[1])=="--creations-only"){test_all_creation_runtime_cases();std::printf("creation runtime: %u checks passed\n",checks);return 0;}if(argc==2&&std::string(argv[1])=="--native-flow-only"){test_all_native_flow_cases();std::printf("native editor flow: %u checks passed\n",checks);return 0;}if(argc==2&&std::string(argv[1])=="--view-only"){test_transport_teardown_remote_host_disconnect_order_and_preview_cleanup();test_owned_end_keeps_local_view_and_selection();test_bound_preview_cleanup_requires_exact_round_and_native_owner();test_preview_restores_latest_independent_selection_before_ack();test_preview_local_view_none_selected_and_stale_pin();test_preview_native_owner_replacement_never_writes_view();std::printf("local editor view: %u checks passed\n",checks);return 0;}if(argc==2&&std::string(argv[1])=="--stages-only"){test_bounded_stage_diagnostics();std::printf("bounded stage runtime: %u checks passed\n",checks);return 0;}if(argc==2&&std::string(argv[1])=="--act0-only"){test_all_act_boundaries();std::printf("act boundary: %u checks passed\n",checks);return 0;}const bool quick=argc==2&&std::string(argv[1])=="--quick";if(argc>1&&!quick&&std::string(argv[1])!="--teardown-only"&&std::string(argv[1])!="--cancellation-only"&&std::string(argv[1])!="--accounting-only"&&std::string(argv[1])!="--saved-root-only")return 2;if(argc==2&&std::string(argv[1])=="--saved-root-only"){test_saved_root_diagnostic_distinguishes_native_local_alias();std::printf("logical saved-root diagnostic: %u checks passed\n",checks);return 0;}if(argc==2&&std::string(argv[1])=="--accounting-only"){test_passive_accounting_does_not_touch_native_or_durable_state();std::printf("runtime passive accounting: %u checks passed\n",checks);return 0;}if(argc==2&&std::string(argv[1])=="--cancellation-only"){test_all_native_cancellations();std::printf("native cancellation: %u checks passed\n",checks);return 0;}if(argc==2&&std::string(argv[1])=="--teardown-only"){test_all_transport_teardown_cases();std::printf("transport teardown: %u checks passed\n",checks);return 0;}test_all_creation_runtime_cases();test_saved_root_diagnostic_distinguishes_native_local_alias();test_passive_accounting_does_not_touch_native_or_durable_state();test_all_act_boundaries();test_all_native_cancellations();test_all_transport_teardown_cases();test_class_owned_placement_companion_transaction();test_unowned_native_gesture_restores_without_freezing();test_legacy_pending_recovery_mode_overrides_detached_old_context();test_clone_preserves_companion_and_rejects_compound_property_change();test_saved_coverage_requires_guest_native_completion_before_pin();test_relative_scale_only_capture_undo_redo_and_recovery();test_legacy_recovery_keeps_schema_history_and_scoped_capture();test_verify_reopen_read_only_exact_saved_identity();test_unsaved_end_does_not_pin_reopen_and_partial_saved_ack_is_not_complete();test_preparation_cancel_preserves_draft_and_is_round_scoped();test_delayed_lease_grant_gets_observed_idle_grace();test_begin_ga_batch_and_end_returning();test_history_entry_rechecks_actual_native_holds();test_preview_return_callbacks_are_scoped_to_one_round();test_preview_restores_latest_independent_selection_before_ack();test_preview_local_view_none_selected_and_stale_pin();test_preview_native_owner_replacement_never_writes_view();test_lease_diagnostic_is_bounded_and_read_only();test_conflicting_native_history_releases_document_ownership();test_guest_end_during_unlaunched_preparation_is_retryable();test_drag_preview_bridge_never_authors_remote_state();test_saved_resources_then_next_commit();test_guest_recovery_requires_owned_native_copy();test_pending_undo_keeps_ownership_across_foreign_commit();test_pending_artifacts_obey_capacity_and_preserve_durable_base();test_missing_authored_captain_blocks_prepare();test_uncertain_maintenance_freezes_until_reopen();test_end_waits_for_native_settle_and_retires_once();test_end_does_not_touch_replaced_native_context();test_idle_controller_keeps_owned_native_cleanup_isolated();test_delayed_gesture_cannot_restore_another_editor();test_host_end_delivers_accepted_structural_checkpoint();test_host_end_does_not_wait_for_departed_replica();test_host_end_waits_for_applied_same_revision_save();test_native_frozen_guest_does_not_block_host_close();test_native_recreation_waits_for_actual_next_frame();test_held_native_gesture_is_cancelled_by_end();test_recovery_retires_held_and_delayed_native_captures();test_recovery_wait_does_not_restore_replaced_context();test_native_save_enrichment_survives_edits_save_and_recovery();test_invalid_completed_save_does_not_replace_live_draft();test_opaque_gesture_changes_freeze_before_empty_semantic_return();test_uncaptured_opaque_changes_cannot_save_or_playtest();test_host_apply_rejects_uncaptured_opaque_changes();test_host_native_success_without_projection_cannot_commit();test_guest_native_success_without_projection_freezes_only_replica();test_held_native_gesture_is_fenced_by_generation_or_disconnect();test_held_move_accepts_disjoint_remote_revision();if(!quick)test_large_closure_retained_once_and_delta_recovery();std::printf("editor runtime: %u checks passed\n",checks);}
