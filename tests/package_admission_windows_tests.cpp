// Isolated current-process mapping/SDK tests, no game or profile database.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../mods/coop/package_admission_windows.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>

namespace pa=package_admission;
static unsigned checks;
#define CHECK(...) do {++checks;if(!(__VA_ARGS__)){std::printf("FAIL %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(1);}} while(0)
struct Sub{SkEventFn fn=nullptr;void* user=nullptr;};static std::array<Sub,SK_EV_COUNT> subs{};
static bool fail_subscription;
int subscribe(SkMod*,int event,SkEventFn fn,void* user){if(fail_subscription&&event==SK_EV_POST_INIT)return -1;CHECK(event>=0&&event<SK_EV_COUNT&&!subs[event].fn);subs[event]={fn,user};return 0;}
void unsubscribe(SkMod*,int event,SkEventFn fn,void* user){CHECK(subs[event].fn==fn&&subs[event].user==user);subs[event]={};}
const char* executable(){return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";}
void fire(int event){CHECK(subs[event].fn);subs[event].fn(subs[event].user,event,nullptr);}
void boot(){fire(SK_EV_APP_PREINIT);fire(SK_EV_APP_INIT);fire(SK_EV_POST_INIT);}
std::string content(unsigned n){char text[65]{};std::snprintf(text,sizeof text,"%064x",n);return text;}
int main(int argc,char** argv)
{
    CHECK(argc==1||argc==2);const std::string name=argc==2?argv[1]:"reuse";std::string error;SkHost host{};
    host.subscribe=subscribe;host.unsubscribe=unsubscribe;host.exe_sha256=executable;
    DWORD baseline{};CHECK(GetProcessHandleCount(GetCurrentProcess(),&baseline));
    pa::ProcessBudget budget;
    if(name=="corrupt-mapping") {
        FILETIME c{},e{},k{},u{};CHECK(GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u));
        const auto creation=(uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;wchar_t mapping[160]{};
        std::swprintf(mapping,160,L"Local\\SporeCoopGA.NativePackages.%lu.%016llx",GetCurrentProcessId(),static_cast<unsigned long long>(creation));
        HANDLE owner=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,4096,mapping);CHECK(owner&&GetLastError()!=ERROR_ALREADY_EXISTS);
        CHECK(!budget.init(&host,nullptr,error));budget.shutdown();
        DWORD handles{};CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles)&&handles==baseline+1);CHECK(CloseHandle(owner));
        CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles)&&handles==baseline);
        std::printf("package admission Windows corrupt-mapping: %u checks passed; failed fresh handle/view released\n",checks);return 0;
    }
    if(name=="subscription-failure") {
        fail_subscription=true;CHECK(!budget.init(&host,nullptr,error));budget.shutdown();for(const auto& sub:subs)CHECK(!sub.fn);
        DWORD handles{};CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles)&&handles==baseline+1);
        std::printf("package admission Windows subscription-failure: %u checks passed; partial subscriptions released\n",checks);return 0;
    }
    CHECK(budget.init(&host,nullptr,error));
    pa::Ticket ticket;const std::wstring path=L"C:\\Synthetic-Admission-Fixture\\SporeCoop\\adventure-assets";
    if(name=="late"){
        CHECK(!budget.begin(path,content(1),{1,2,3},1,false,ticket,error));fire(SK_EV_POST_INIT);
        CHECK(!budget.begin(path,content(1),{1,2,3},1,false,ticket,error));CHECK(budget.accounting().count==0);
    }else if(name=="empty-reload"){
        boot();budget.shutdown();
        for(unsigned i=0;i<1000;++i){pa::ProcessBudget next;CHECK(next.init(&host,nullptr,error));CHECK(next.accounting().boot_stage==3);next.shutdown();
            DWORD handles{};CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles)&&handles==baseline+1);}
    }else{
        boot();CHECK(budget.begin(path,content(1),{1,2,3},77,false,ticket,error));
        CHECK(budget.accounting().count==1&&budget.accounting().bytes==77);
        if(name=="failure"){
            CHECK(!budget.finish(ticket,false,error));CHECK(budget.accounting().uncertain==1);
            CHECK(!budget.begin(path,content(2),{2,2,3},1,false,ticket,error));
        }else{
            CHECK(budget.finish(ticket,true,error));
            if(name=="reuse")for(unsigned i=0;i<1000;++i){CHECK(budget.begin(path,content(1),{1,2,3},77,true,ticket,error));CHECK(ticket.reused);CHECK(budget.finish(ticket,true,error));}
            else if(name=="profile"){
                CHECK(budget.begin(L"c:/synthetic-admission-fixture/sporecoop/adventure-assets/",content(1),{1,2,3},77,true,ticket,error));CHECK(budget.finish(ticket,true,error));
                CHECK(!budget.begin(L"C:\\Different-Fixture",content(1),{1,2,3},77,true,ticket,error));
                CHECK(!budget.begin(path,content(1),{1,2,3},77,true,ticket,error));CHECK(budget.accounting().flags&pa::kUncertain);
            }else if(name=="shutdown"){
                fire(SK_EV_SHUTDOWN);CHECK(!budget.begin(path,content(2),{2,2,3},1,false,ticket,error));
            }else if(name=="nonempty-reload"){
                budget.shutdown();pa::ProcessBudget next;CHECK(next.init(&host,nullptr,error));
                CHECK(next.accounting().count==1&&next.accounting().reloaded);
                CHECK(!next.begin(path,content(1),{1,2,3},77,true,ticket,error));
                CHECK(!next.begin(path,content(2),{2,2,3},1,false,ticket,error));next.shutdown();
            }else CHECK(name=="reuse");
        }
    }
    budget.shutdown();for(const auto& sub:subs)CHECK(!sub.fn);
    DWORD handles{};CHECK(GetProcessHandleCount(GetCurrentProcess(),&handles)&&handles==baseline+1);
    std::printf("package admission Windows %s: %u checks passed; process_owner_handles=1\n",name.c_str(),checks);
}
