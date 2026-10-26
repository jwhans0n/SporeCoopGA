// Actual native bridge boundaries with separate logical and local draft keys.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include "adventure.h"
#include "adventure_captains.h"
#include "adventure_catalog.h"
#include "adventure_collect.h"
#include "adventure_control.h"
#include "adventure_results.h"
#include "adventure_world.h"
#include "adventure_relationships.h"
#include "adventure_social.h"
#include "adventure_flow.h"
#include "adventure_intro_gate.h"
#include "adventure_pause.h"
#include "adventure_pause_native.h"
#include "game.h"
#include "modes.h"
#include "coop_ui.h"
#include "editor_native.h"
#include "sporekit_game.hpp"
// Give unrelated adapter entry points internal linkage so this focused fixture
// links only the production paths it exercises, without permissive unresolveds.
#define adventure_native bridge_native
namespace {
#include "../mods/coop/adventure_native.cpp"
}

namespace {
unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
std::array<uint8_t,0xE0> t_mode;
std::array<uint8_t,0x128> t_data;
std::array<uint8_t,0x2CB0> t_resource;
std::array<uint8_t,0x110> t_play;
std::array<uint8_t,0x371> t_loading;
std::array<uint8_t,0x160> t_cinematic;
void* t_mode_pointer = t_mode.data();
std::deque<std::pair<SkTaskFn,void*>> t_tasks;
net::Session t_session;
ga::Descriptor t_descriptor;
editor_native::Snapshot t_snapshot;
bool t_active=false,t_outcome=false,t_cleanup=false,t_current=true,t_can_ready=true,t_ready=false,t_have_descriptor=true;
bool t_running=false;
adventure_flow::Scene t_scene;
int t_launches=0,t_returns=0,t_ready_calls=0,t_closes=0,t_statistics=0,t_intro_confirms=0;
constexpr dbpf::Key t_logical{91,0x366A930D,0x11111111},t_local{42,0x366A930D,0x22222222},t_avatar{7,0x00B1B104,0x33333333};
void* SK_CDECL t_loading_get() { return t_loading.data(); }
void* SK_CDECL t_cinematic_get() { return t_cinematic.data(); }
uintptr_t t_sym(const char* name) {
    if (std::string(name)=="App::cScenarioMode::ptr") return uintptr_t(&t_mode_pointer);
    if (std::string(name)=="SporeCoop::Adventure::LoadingScreenGet") return uintptr_t(t_loading_get);
    if (std::string(name)=="Simulator::cCinematicManager::Get") return uintptr_t(t_cinematic_get);
    return 0;
}
void t_outside(SkTaskFn fn,void* user) { t_tasks.emplace_back(fn,user); }
void drain() { auto tasks=std::move(t_tasks);t_tasks.clear();for (auto [fn,user]:tasks) fn(user); }
void SK_THISCALL t_statistic(void*,uint32_t,const dbpf::Key*,const int64_t*,uint32_t) { ++t_statistics; }
adventure_native::EditorPreview config() {
    adventure_native::EditorPreview p;
    p.project[0]=1;p.epoch=73;p.round=4;p.revision=0;p.adventure=t_logical;p.authored_avatar=t_avatar;
    p.current=[] { return t_current; };
    p.launch=[](std::string&) { ++t_launches;skg::field<int>(t_mode.data(),0xCC)=2;return true; };
    p.return_editor=[] { ++t_returns; };
    return p;
}
void reset() {
    adventure_native::clear_editor_preview();drain();
    t_mode.fill(0);t_data.fill(0);t_resource.fill(0);t_play.fill(0);t_loading.fill(0);
    t_cinematic.fill(0);adventure_native::g_intro.reset();
    t_mode_pointer=t_mode.data();
    skg::field<void*>(t_mode.data(),0x74)=t_data.data();skg::field<void*>(t_mode.data(),0x78)=t_play.data();
    skg::field<int>(t_mode.data(),0xCC)=1;skg::field<void*>(t_data.data(),0x10)=t_resource.data();
    skg::field<bool>(t_data.data(),0x124)=true;skg::field<dbpf::Key>(t_resource.data(),8)=t_local;
    t_snapshot={};t_snapshot.source={t_local.instance,t_local.type,t_local.group};
    t_snapshot.captain={t_avatar.instance,t_avatar.type,t_avatar.group};t_snapshot.bytes={1,2,3};
    t_descriptor={};t_descriptor.run_id=8;t_descriptor.adventure=t_logical;t_descriptor.locked_captain=t_avatar;
    t_active=t_outcome=t_cleanup=t_ready=false;t_current=t_can_ready=t_have_descriptor=true;
    t_launches=t_returns=t_ready_calls=t_closes=t_statistics=t_intro_confirms=0;
    t_running=false;t_scene={};
    static SkHost host{};host.sym=t_sym;host.run_outside_frame=t_outside;
    adventure_native::H=&host;adventure_native::C.sess=&t_session;adventure_native::g_symbols=true;
    adventure_native::original_statistic=t_statistic;
}
}
namespace game { uint32_t mode_id(){return modes::kScenario;} void* avatar(){return t_resource.data();} }
namespace adventure_collect { bool available(){return true;} }
namespace adventure_captains { bool available(){return true;} }
namespace adventure_world { bool available(){return true;} }
namespace adventure_relationships { bool available(){return true;} }
namespace adventure_social { bool available(){return true;} }
namespace editor_native { bool capture(Snapshot& value,std::string& error){value=t_snapshot;error.clear();return true;} }
namespace coop_ui { void close_for_adventure_launch(){++t_closes;} }
namespace adventure_flow {
Scene scene(){return t_scene;}
void confirm_intro(){++t_intro_confirms;}
}
namespace adventure {
bool active(){return t_active;}
bool running(){return t_running;}
bool outcome_locked(){return t_outcome;}
bool cleanup_pending(){return t_cleanup;}
const ga::Descriptor* current(){return t_have_descriptor?&t_descriptor:nullptr;}
bool can_ready(){return t_can_ready;}
bool ready(){return t_ready;}
bool set_ready(bool value){++t_ready_calls;t_ready=value;return true;}
}
int main() {
    std::string error;
    CHECK(t_session.host(0,"native-editor-bridge-test",error));
    reset();auto p=config();p.project={};CHECK(!adventure_native::set_editor_preview(p,error));
    reset();t_active=true;CHECK(!adventure_native::set_editor_preview(config(),error));
    reset();t_outcome=true;CHECK(!adventure_native::set_editor_preview(config(),error));
    reset();t_cleanup=true;CHECK(!adventure_native::set_editor_preview(config(),error));
    reset();t_snapshot.captain.instance++;CHECK(!adventure_native::set_editor_preview(config(),error));
    reset();bool cache_only=true;CHECK(adventure_native::content_registration(8,t_logical,cache_only,error)&&!cache_only);
    CHECK(adventure_native::set_editor_preview(config(),error));
    CHECK(adventure_native::content_registration(8,t_logical,cache_only,error)&&cache_only);
    CHECK(!adventure_native::content_registration(9,t_logical,cache_only,error)&&!cache_only);
    CHECK(!adventure_native::content_registration(8,t_local,cache_only,error));
    t_current=false;CHECK(!adventure_native::content_registration(8,t_logical,cache_only,error));
    reset();CHECK(adventure_native::set_editor_preview(config(),error));CHECK(adventure_native::editor_preview());
    CHECK(!adventure_native::set_editor_preview(config(),error));CHECK(!adventure_native::loaded(t_logical));
    CHECK(adventure_native::launch(t_logical,t_avatar,error));CHECK(t_launches==1&&t_closes==1);
    CHECK(adventure_native::loaded(t_logical));CHECK(!adventure_native::loaded(t_local));
    skg::field<bool>(t_data.data(),0x124)=false;CHECK(adventure_native::loaded(t_logical));
    skg::field<bool>(t_loading.data(),0x370)=true;CHECK(!adventure_native::loaded(t_logical));skg::field<bool>(t_loading.data(),0x370)=false;
    skg::field<int>(t_mode.data(),0xD0)=1;CHECK(!adventure_native::loaded(t_logical));skg::field<int>(t_mode.data(),0xD0)=0;
    adventure_native::g_initializing=1;CHECK(!adventure_native::loaded(t_logical));adventure_native::g_initializing=0;
    skg::field<dbpf::Key>(t_resource.data(),8)={43,t_local.type,t_local.group};CHECK(!adventure_native::loaded(t_logical));
    skg::field<dbpf::Key>(t_resource.data(),8)=t_local;t_descriptor.run_id++;CHECK(!adventure_native::loaded(t_logical));
    reset();CHECK(adventure_native::set_editor_preview(config(),error));t_current=false;CHECK(!adventure_native::launch(t_logical,t_avatar,error));CHECK(!t_launches);
    reset();CHECK(adventure_native::set_editor_preview(config(),error));p=config();
    CHECK(!adventure_native::launch(t_local,t_avatar,error));CHECK(!t_launches);
    reset();CHECK(adventure_native::set_editor_preview(config(),error));
    adventure_native::schedule_preview_ready();adventure_native::schedule_preview_ready();CHECK(t_tasks.size()==1);drain();CHECK(t_ready_calls==1&&t_ready);
    adventure_native::schedule_preview_ready();CHECK(t_tasks.empty());
    reset();CHECK(adventure_native::set_editor_preview(config(),error));adventure_native::schedule_preview_ready();
    t_descriptor.run_id++;drain();CHECK(!t_ready_calls);
    reset();CHECK(adventure_native::set_editor_preview(config(),error));adventure_native::schedule_preview_ready();
    adventure_native::clear_editor_preview();CHECK(adventure_native::set_editor_preview(config(),error));drain();CHECK(!t_ready_calls);
    reset();CHECK(adventure_native::set_editor_preview(config(),error));adventure_native::schedule_preview_ready();t_current=false;drain();CHECK(!t_ready_calls);
    reset();CHECK(adventure_native::set_editor_preview(config(),error));adventure_native::preview_return();adventure_native::preview_return();CHECK(t_returns==1);
    adventure_native::schedule_preview_ready();CHECK(t_tasks.empty());CHECK(!adventure_native::launch(t_logical,t_avatar,error));
    reset();p=config();p.launch=[](std::string&){adventure_native::clear_editor_preview();return true;};
    CHECK(adventure_native::set_editor_preview(p,error));CHECK(!adventure_native::launch(t_logical,t_avatar,error));CHECK(!adventure_native::editor_preview());
    reset();CHECK(adventure_native::set_editor_preview(config(),error));CHECK(adventure_native::launch(t_logical,t_avatar,error));
    adventure_native::record_statistic(nullptr,0x2D59837B,&t_logical,nullptr,0);
    adventure_native::record_statistic(nullptr,0x706CCB27,&t_local,nullptr,0);CHECK(!t_statistics);
    dbpf::Key other{3,t_local.type,t_local.group};adventure_native::record_statistic(nullptr,0x706CCB27,&other,nullptr,0);CHECK(t_statistics==1);
    adventure_native::record_statistic(nullptr,99,&t_local,nullptr,0);CHECK(t_statistics==2);
    // Editor Test has no beam-down Continue. It may acknowledge that absent
    // cinematic only after the confirmed round enters actual native Playing.
    reset();adventure_native::schedule_preview_intro();CHECK(t_tasks.empty());
    CHECK(adventure_native::set_editor_preview(config(),error));
    CHECK(adventure_native::launch(t_logical,t_avatar,error));
    t_scene={t_descriptor.run_id,1,adventure_flow::Kind::Intro};
    skg::field<int>(t_play.data(),0x90)=3;
    adventure_native::schedule_preview_intro();CHECK(t_tasks.empty());
    t_running=true;skg::field<int>(t_play.data(),0x90)=2;
    adventure_native::schedule_preview_intro();CHECK(t_tasks.empty());
    skg::field<int>(t_play.data(),0x90)=3;
    skg::field<bool>(t_loading.data(),0x370)=true;
    adventure_native::schedule_preview_intro();CHECK(t_tasks.empty());
    skg::field<bool>(t_loading.data(),0x370)=false;
    t_scene.kind=adventure_flow::Kind::Talk;
    adventure_native::schedule_preview_intro();CHECK(t_tasks.empty());
    t_scene.kind=adventure_flow::Kind::Intro;t_scene.run++;
    adventure_native::schedule_preview_intro();CHECK(t_tasks.empty());
    t_scene.run--;t_current=false;
    adventure_native::schedule_preview_intro();CHECK(t_tasks.empty());
    t_current=true;
    skg::field<uint32_t>(t_cinematic.data(),0x2C)=1;
    adventure_native::schedule_preview_intro();CHECK(t_tasks.empty());
    skg::field<uint32_t>(t_cinematic.data(),0x2C)=0;
    adventure_native::g_intro.legacy_message(t_descriptor.run_id);
    adventure_native::schedule_preview_intro();CHECK(t_tasks.empty());
    adventure_native::g_intro.reset();
    adventure_native::schedule_preview_intro();adventure_native::schedule_preview_intro();CHECK(t_tasks.size()==1);
    t_current=false;drain();CHECK(!t_intro_confirms);
    t_current=true;adventure_native::schedule_preview_intro();drain();CHECK(t_intro_confirms==1);
    adventure_native::schedule_preview_intro();CHECK(t_tasks.empty());
    reset();CHECK(adventure_native::set_editor_preview(config(),error));
    CHECK(adventure_native::launch(t_logical,t_avatar,error));
    t_running=true;t_scene={t_descriptor.run_id,1,adventure_flow::Kind::Intro};
    skg::field<int>(t_play.data(),0x90)=3;
    adventure_native::schedule_preview_intro();CHECK(t_tasks.size()==1);
    adventure_native::clear_editor_preview();drain();CHECK(!t_intro_confirms);
    adventure_native::clear_editor_preview();t_session.leave();drain();
    std::printf("adventure editor preview: %u checks passed\n",checks);
}
