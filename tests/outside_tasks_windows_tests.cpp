// Actual SDK queue, command marshalling and pump, with passive game endpoints.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "../sporekit/core.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <atomic>
static bool presenting=false;
static uint64_t clock_value=0;
static std::atomic<unsigned> posted=0;
static BOOL WINAPI controlled_counter(LARGE_INTEGER* out){out->QuadPart=clock_value;return TRUE;}
static BOOL WINAPI controlled_frequency(LARGE_INTEGER* out){out->QuadPart=1000000;return TRUE;}
static BOOL WINAPI controlled_post(HWND,UINT,WPARAM,LPARAM){++posted;return TRUE;}
namespace sk {
bool in_frame(){return presenting;}
void* game_window(){return reinterpret_cast<void*>(1);}
uint64_t frame_index(){return 12;}
SkMod g_kit_mod;
void logf(SkMod*,int,const char*,...){ }
std::string json_escape(const std::string& value){return value;}
}
#define QueryPerformanceCounter controlled_counter
#define QueryPerformanceFrequency controlled_frequency
#define PostMessageW controlled_post
#include "../sporekit/console.cpp"
#undef QueryPerformanceCounter
#undef QueryPerformanceFrequency
#undef PostMessageW
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    sk::set_main_thread();
    unsigned feeds=0,other=0;
    std::function<void()> feed=[&]{if(++feeds<200)sk::run_outside_frame(feed);};
    sk::run_outside_frame(feed);sk::run_outside_frame([&]{++other;});
    sk::pump_outside_tasks();
    CHECK(feeds==1&&other==1&&sk::g_out_tasks.size()==1);
    CHECK(posted==4); // Initial two, self-requeue, then the remaining-work wake.
    while(sk::g_out_tasks.size())sk::pump_outside_tasks();CHECK(feeds==200);
    sk::outside_pump_metrics("start");
    sk::run_outside_frame([&]{clock_value+=37000;});sk::run_outside_frame([&]{++other;});
    sk::pump_outside_tasks();const auto measured=sk::outside_pump_metrics("stop");
    CHECK(other==1&&sk::g_out_tasks.size()==1&&measured.calls==1&&measured.peak.processed==1&&measured.budget_hits==1);
    sk::pump_outside_tasks();CHECK(other==2&&!sk::g_out_tasks.size());
    sk::run_outside_frame([&]{++other;});presenting=true;sk::pump_outside_tasks();
    CHECK(other==2&&sk::g_out_tasks.size()==1);presenting=false;sk::pump_outside_tasks();CHECK(other==3);
    auto owned=std::make_shared<int>(1);std::weak_ptr<int> weak=owned;
    sk::run_outside_frame([&,owned]{CHECK(!weak.expired());sk::run_outside_frame([&,owned]{CHECK(!weak.expired());++other;});});owned.reset();
    sk::pump_outside_tasks();CHECK(!weak.expired()&&sk::g_out_tasks.size()==1);
    sk::pump_outside_tasks();CHECK(weak.expired()&&other==4);
    std::vector<int> order;
    sk::run_outside_frame([&]{order.push_back(1);sk::run_outside_frame([&]{order.push_back(3);sk::run_outside_frame([&]{order.push_back(5);});});
        sk::pump_outside_tasks();order.push_back(4);});
    sk::run_outside_frame([&]{order.push_back(2);});sk::pump_outside_tasks();
    CHECK(order==std::vector<int>({1,2,3,4})&&sk::g_out_tasks.size()==1);sk::pump_outside_tasks();CHECK(order.back()==5);
    SkMod module;
    sk::command_add(&module,"fixture.command","",[](void*,int,const char* const*,SkOut* out){reinterpret_cast<sk::Out*>(out)->text="completed";return 0;},nullptr,SK_CMD_OUTSIDE_FRAME);
    int rc=-1;std::string reply;
    std::thread worker([&]{rc=sk::command_exec_line("fixture.command",reply,false);});
    const auto wait=GetTickCount();while(!sk::g_out_tasks.size()&&GetTickCount()-wait<2000)Sleep(1);
    CHECK(sk::g_out_tasks.size()==1);sk::pump_outside_tasks();worker.join();CHECK(!rc&&reply=="completed");
    sk::run_outside_frame([&]{std::string nested;CHECK(sk::command_exec_line("fixture.command",nested,true)==0&&nested=="completed");});sk::pump_outside_tasks();
    rc=-1;reply.clear();
    std::thread cancelled([&]{rc=sk::command_exec_line("fixture.command",reply,false);});
    const auto cancel_wait=GetTickCount();while(!sk::g_out_tasks.size()&&GetTickCount()-cancel_wait<2000)Sleep(1);
    CHECK(sk::g_out_tasks.size()==1);sk::commands_remove_all(&module);sk::pump_outside_tasks();cancelled.join();
    CHECK(rc==-1&&reply=="command vanished"&&sk::g_out_tasks.size()==0);
    std::printf("SDK outside pump: %u checks passed\n",checks);
}
