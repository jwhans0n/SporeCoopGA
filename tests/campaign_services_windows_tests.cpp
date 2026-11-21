// Actual campaign save/busy glue with controlled scene and file endpoints.
// No game, profile, window, native hook or UDP socket is opened.
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <process.h>
#include <string>
#include <vector>
#include "../mods/coop/coop_state.h"
#include "../mods/coop/autosave_glue.h"
#include "../mods/coop/store.h"

static unsigned checks;
#define CHECK(x) do { ++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);} } while(0)
namespace fixture {
bool authoring=false,creation=false,adventure=false,flow=false,presenting=false;
uint32_t mode=stage_epoch::kCreature,epoch=1;
double now=100;
unsigned save_calls=0,collections=0,busy_reads=0,pauses=0,resumes=0,feed_lines=0;
std::string key="c:\\spore\\games\\game3\\planet.spo",name="planet.spo";
std::deque<std::pair<SkTaskFn,void*>> tasks;
struct Transport:net::Transport {
    static inline std::map<uint16_t,Transport*> ports;
    uint16_t port=0;
    std::deque<std::pair<net::Endpoint,std::vector<uint8_t>>> input;
    bool open(uint16_t p,std::string*) override {port=p?p:51003;ports[port]=this;return true;}
    void close() override {ports.erase(port);input.clear();}
    bool send_to(const net::Endpoint& to,const void* data,size_t size) override {
        auto found=ports.find(to.port);if(found==ports.end())return true;
        net::Endpoint from{to.ip,port};const auto* bytes=static_cast<const uint8_t*>(data);
        found->second->input.push_back({from,{bytes,bytes+size}});return true;
    }
    int recv_from(net::Endpoint& from,void* data,size_t cap) override {
        if(input.empty())return 0;
        auto packet=std::move(input.front());input.pop_front();CHECK(packet.second.size()<=cap);
        from=packet.first;std::memcpy(data,packet.second.data(),packet.second.size());return int(packet.second.size());
    }
    uint16_t local_port() const override {return port;}
};
void queue(SkTaskFn fn,void* data){tasks.push_back({fn,data});}
void drain(){presenting=false;for(unsigned i=0;!tasks.empty()&&i<30;++i){auto [fn,data]=tasks.front();tasks.pop_front();fn(data);}CHECK(tasks.empty());}
void log(SkMod*,int,const char*,...){ }
int command_add(SkMod*,const char*,const char*,SkCommandFn,void*,int){return 0;}
void out(SkOut*,const char*,...){ }
HRESULT WINAPI appdata(HWND,int,HANDLE,DWORD,LPWSTR){CHECK(false);return E_FAIL;}
void* SK_CDECL time_manager(){CHECK(!presenting);static std::array<uint8_t,32> manager{};return manager.data();}
int SK_THISCALL pause(void*,uint32_t){CHECK(!presenting);++pauses;return 0;}
int SK_THISCALL resume(void*,uint32_t){CHECK(!presenting);++resumes;return 0;}
uintptr_t sym(const char* value){
    const std::string s=value;
    return s=="Simulator::cGameTimeManager::Get"?uintptr_t(time_manager):
        s=="Simulator::cGameTimeManager::Pause"?uintptr_t(pause):
        s=="Simulator::cGameTimeManager::Resume"?uintptr_t(resume):0;
}
SkHost host=[] {SkHost h{};h.time_seconds=[] {return now;};h.run_outside_frame=queue;h.log=log;
    h.command_add=command_add;h.out_printf=out;h.sym=sym;return h;}();
}

#define SHGetFolderPathW fixture::appdata
#include "../mods/coop/store.cpp"
#undef SHGetFolderPathW
#include "../mods/coop/autosave.cpp"
#include "../mods/coop/host_busy.cpp"

namespace fix {bool reg(const char*,const char*,bool*,const char*,bool){return true;}}
namespace coop_editor {bool authoring(){return fixture::authoring;}}
namespace editor_creation {bool active(){return fixture::creation;}}
namespace adventure {bool active(){return fixture::adventure;}}
namespace adventure_flow {bool held(){return fixture::flow;}}
namespace game {
uint32_t mode_id(){return fixture::mode;}
uint32_t stage_epoch(uint32_t){return fixture::epoch;}
bool leaving_stage(){return false;}
}
namespace modes {
std::string current_world_name(){return fixture::name;}
uint32_t current_world(){return 1;}
void set_cell_hold(bool){CHECK(!fixture::presenting);}
}
namespace coop_ui {bool russian(){return false;}bool game_modal_up(){CHECK(!fixture::presenting);return false;}}
namespace settings {int get(Id){return 1;}}
namespace spaceext {bool rivals(){return false;}}
namespace world {
std::string current_game_key(){CHECK(!fixture::presenting);return fixture::key;}
bool save_game(std::string&){CHECK(!fixture::presenting);++fixture::save_calls;return true;}
bool cinematic_playing(){return false;}
bool save_busy(){return false;}
bool saved_box_up(){return false;}
std::vector<std::pair<uint32_t,int>> pause_reasons(){return {{busy_model::kReasonModal,1}};}
}
const SkHost* H=&fixture::host;SkMod* M=nullptr;
namespace coop_core {
net::Session g_sess;uint32_t g_mode=stage_epoch::kCreature;
unsigned game_busy(){CHECK(!fixture::presenting);++fixture::busy_reads;return busy::kPaused;}
bool editor_coming(double){return false;}
bool guest_space_other(){return false;}bool guest_elsewhere(){return false;}
char host_out_of_stage(){return 0;}
bool stage_mode(uint32_t mode){return stage_epoch::index_of(mode)>=0;}
void feed(const std::string&){++fixture::feed_lines;}
}

int main(int argc,char** argv){
    if(argc==1) {
        const char* cases[]={"queued-hold","queued-creation","queued-adventure","queued-flow","queued-mode",
            "queued-world","queued-epoch","queued-session","store-access","store-after-save","host-busy-frame",
            "host-busy-authoring","queued-campaign","host-busy-guest-first-message","host-busy-guest-return",
            "store-lookup-frame","store-pending-hold","queued-roundtrip","store-pending-world","campaign-stage-policy"};
        for(const char* test:cases){const char* args[]={argv[0],test,nullptr};CHECK(_spawnv(_P_WAIT,argv[0],args)==0);}
        std::printf("campaign services: %u isolated cases passed\n",checks);return 0;
    }
    CHECK(argc==2);using namespace fixture;const std::string test=argv[1];
    Transport transport;coop_core::g_sess.set_transport(&transport);std::string error;
    CHECK(coop_core::g_sess.host(51001,"Host",error));
    store::H=&host;store::C.sess=&coop_core::g_sess;
    store::g_cur.reset("game3/planet.spo");store::g_open=true;store::g_serial=7;
    store::g_cur.put("host","Host","campaign.record",1,{7,8,9},1);
    store::features().push_back({"fixture",[](pstore::Store&,int64_t){++collections;},nullptr});
    autosave::init(&host,nullptr,&coop_core::g_sess,{});autosave::g_test_interval=1;
    if(test=="store-access") {
        const auto before=store::g_cur.text();authoring=true;
        CHECK(store::world()==nullptr&&store::world_name().empty());
        store::tick();CHECK(store::g_cur.text()==before);
        authoring=false;CHECK(store::world()&&store::g_cur.text()==before);
    } else if(test=="store-after-save") {
        creation=true;store::after_save();CHECK(!collections&&!store::saves());
        creation=false;fixture::adventure=true;store::after_save();CHECK(!collections&&!store::saves());
        fixture::adventure=false;flow=true;store::after_save();CHECK(!collections&&!store::saves());
    } else if(test=="host-busy-frame") {
        presenting=true;coop_core::host_busy_tick(now);CHECK(!busy_reads&&!tasks.empty());
        drain();CHECK(busy_reads==1);
    } else if(test=="host-busy-authoring") {
        authoring=true;coop_core::host_busy_tick(now);drain();
        CHECK(!busy_reads&&!coop_core::g_ann.told&&!host_busy::waiting()&&!host_busy::takes_tribe_pause());
    } else if(test=="host-busy-guest-first-message"||test=="host-busy-guest-return") {
        Transport remote_transport;net::Session remote;remote.set_transport(&remote_transport);
        coop_core::g_sess.leave();CHECK(remote.host(51002,"Remote host",error));
        CHECK(coop_core::g_sess.join("127.0.0.1:51002","Guest",error));
        for(unsigned i=0;i<100&&!coop_core::g_sess.connected();++i){coop_core::g_sess.update();remote.update();}
        CHECK(coop_core::g_sess.connected());
        if(test=="host-busy-guest-return"){coop_core::host_busy_tick(now);drain();}
        const auto bytes=busy_model::encode({1,busy_model::State::Paused,busy_model::kWhyModal,mode,1,1});
        net::Reader message(bytes.data(),bytes.size());coop_core::host_busy_message(0,message);
        presenting=true;coop_core::host_busy_tick(now);CHECK(!pauses);drain();CHECK(pauses==1&&host_busy::waiting());
        creation=true;mode=stage_epoch::kEditor;coop_core::g_mode=mode;
        coop_core::host_busy_tick(now);drain();CHECK(!host_busy::waiting()&&!resumes);
        creation=false;mode=stage_epoch::kCreature;coop_core::g_mode=mode;
        coop_core::host_busy_tick(now);drain();CHECK(!host_busy::waiting()&&resumes==1);
        coop_core::host_busy_tick(now);drain();CHECK(resumes==1);
    } else if(test=="store-lookup-frame") {
        presenting=true;store::tick();CHECK(!tasks.empty());drain();CHECK(store::world());
    } else if(test=="store-pending-hold") {
        store::after_save();CHECK(!collections&&!tasks.empty());authoring=true;drain();CHECK(!collections);
    } else if(test=="store-pending-world") {
        store::after_save();CHECK(!collections&&!tasks.empty());key="c:\\spore\\games\\game4\\other.spo";
        drain();CHECK(!collections);
    } else if(test=="campaign-stage-policy") {
        for(const auto value:{stage_epoch::kCell,stage_epoch::kCreature,stage_epoch::kTribe,stage_epoch::kCiv,stage_epoch::kSpace}) {
            mode=value;CHECK(store::world());
        }
        for(const auto value:{stage_epoch::kEdit,stage_epoch::kEditor,stage_epoch::kScenario,modes::kMenu,modes::kLoad,0u}) {
            mode=value;CHECK(!store::world()&&store::world_name().empty());
        }
    } else {
        autosave::tick();now+=2;autosave::tick();CHECK(!tasks.empty()&&!save_calls);
        if(test=="queued-roundtrip") {
            authoring=true;autosave::tick();authoring=false;autosave::tick();now+=2;autosave::tick();
            CHECK(tasks.size()==2);
        }
        else if(test=="queued-hold")authoring=true;
        else if(test=="queued-creation")creation=true;
        else if(test=="queued-adventure")fixture::adventure=true;
        else if(test=="queued-flow")flow=true;
        else if(test=="queued-mode")mode=stage_epoch::kScenario;
        else if(test=="queued-world")key="c:\\spore\\games\\game4\\other.spo";
        else if(test=="queued-epoch")++epoch;
        else if(test=="queued-session"){coop_core::g_sess.leave();CHECK(coop_core::g_sess.host(51001,"Host",error));}
        else CHECK(test=="queued-campaign");
        drain();CHECK(save_calls==((test=="queued-campaign"||test=="queued-roundtrip")?1u:0u));
    }
    std::printf("campaign service %s: %u checks\n",test.c_str(),checks);return 0;
}
