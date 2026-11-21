#include "package_admission_windows.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
#include <cstring>
#include "../../updater/sha256.h"

namespace package_admission {
namespace {
constexpr int events[]={SK_EV_APP_PREINIT,SK_EV_APP_INIT,SK_EV_POST_INIT,SK_EV_SHUTDOWN};
constexpr const char* restart="Cannot establish process-lifetime imported-resource ownership. Restart Spore before importing content.";
bool parse_hash(const char* text,Hash& out)
{
    if(!text || std::strlen(text)!=64)return false;
    auto digit=[](char c)->int {if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    for(size_t i=0;i<out.size();++i){const int a=digit(text[2*i]),b=digit(text[2*i+1]);if(a<0||b<0)return false;out[i]=uint8_t(a*16+b);}
    return true;
}
bool profile_hash(const std::wstring& path,Hash& result)
{
    // Existing verified cache path is absolute. Canonicalize spelling only; do not open a profile/database.
    if(path.empty() || path.size()>1000)return false;
    wchar_t full[1024]{};const DWORD size=GetFullPathNameW(path.c_str(),1024,full,nullptr);
    if(!size || size>=1024)return false;
    for(DWORD i=0;i<size;++i)if(full[i]==L'/')full[i]=L'\\';
    while(size_t length=std::wcslen(full)){if(length<=3||full[length-1]!=L'\\')break;full[length-1]=0;}
    wchar_t lower[1024]{};
    const int count=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,full,-1,lower,1024,nullptr,nullptr,0);
    if(count<=1 || count>1024)return false;
    const auto hex=upd::sha256_hex(reinterpret_cast<const uint8_t*>(lower),size_t(count-1)*sizeof(wchar_t));
    return parse_hash(hex.c_str(),result);
}
}
bool ProcessBudget::enter(std::string& error)
{
    if(!ledger_ || !unavailable_.empty() || GetCurrentThreadId()!=thread_){error=unavailable_.empty()?restart:unavailable_;return false;}
    if(InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&ledger_->update_lock),1,0)!=0){error="Imported-resource admission is busy. Retry after the current import settles.";return false;}
    return true;
}
void ProcessBudget::leave(){InterlockedExchange(reinterpret_cast<volatile LONG*>(&ledger_->update_lock),0);}
bool ProcessBudget::init(const SkHost* host,SkMod* mod,std::string& error)
{
    error.clear();
    if(ledger_){if(host==host_&&mod==mod_&&unavailable_.empty())return true;error=unavailable_.empty()?restart:unavailable_;return false;}
    if(host_){error=unavailable_.empty()?restart:unavailable_;return false;}
    host_=host;mod_=mod;thread_=GetCurrentThreadId();
    Identity identity{};identity.pid=GetCurrentProcessId();
    FILETIME creation{},exit{},kernel{},user{};
    if(!host || !host->subscribe || !host->unsubscribe || !host->exe_sha256 ||
       !GetProcessTimes(GetCurrentProcess(),&creation,&exit,&kernel,&user) ||
       !parse_hash(host->exe_sha256(),identity.executable)) {unavailable_=error=restart;return false;}
    identity.creation=(uint64_t(creation.dwHighDateTime)<<32)|creation.dwLowDateTime;
    wchar_t name[160]{};
    std::swprintf(name,160,L"Local\\SporeCoopGA.NativePackages.%lu.%016llx",identity.pid,
                  static_cast<unsigned long long>(identity.creation));
    HANDLE fresh=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,4096,name);
    const bool existed=GetLastError()==ERROR_ALREADY_EXISTS;
    if(!fresh){unavailable_=error=restart;return false;}
    ledger_=static_cast<Ledger*>(MapViewOfFile(fresh,FILE_MAP_ALL_ACCESS,0,0,4096));
    if(!ledger_){CloseHandle(fresh);unavailable_=error=restart;return false;}
    if(!existed)Core::initialize(*ledger_,identity,reinterpret_cast<uintptr_t>(fresh));
    // On first creation, retain fresh as the single process owner. Subsequent fresh handles close now.
    if(existed)CloseHandle(fresh);
    DWORD flags{};const bool handle_width=ledger_->owner_handle<=UINTPTR_MAX;const HANDLE owner=reinterpret_cast<HANDLE>(uintptr_t(ledger_->owner_handle));
    const auto* owner_view=handle_width&&GetHandleInformation(owner,&flags)?static_cast<const Ledger*>(MapViewOfFile(owner,FILE_MAP_READ,0,0,sizeof(Ledger))):nullptr;
    const bool owner_valid=owner_view && owner_view->magic==kMagic && owner_view->identity.pid==identity.pid &&
        owner_view->identity.creation==identity.creation && owner_view->owner_handle==ledger_->owner_handle;
    if(owner_view)UnmapViewOfFile(owner_view);
    const char* why=nullptr;
    if(!owner_valid || !enter(error)){if(error.empty())error=restart;unavailable_=error;UnmapViewOfFile(ledger_);ledger_=nullptr;return false;}
    const bool attached=core_.attach(*ledger_,identity,why);cached_=core_.accounting();leave();
    if(!attached){unavailable_=error=why?why:restart;UnmapViewOfFile(ledger_);ledger_=nullptr;return false;}
    for(size_t i=0;i<4;++i){
        if(host->subscribe(mod,events[i],lifecycle,this)!=0){unavailable_=error="Cannot subscribe to the resource ownership lifecycle. Restart Spore.";shutdown();return false;}
        subscribed_|=1u<<i;
    }
    return true;
}
void ProcessBudget::shutdown()
{
    if(host_&&host_->unsubscribe)for(size_t i=0;i<4;++i)if(subscribed_&(1u<<i))host_->unsubscribe(mod_,events[i],lifecycle,this);
    subscribed_=0;
    // Even a local startup error must detach its own attached module without closing the process owner.
    if(ledger_){
        if(GetCurrentThreadId()==thread_ && InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(&ledger_->update_lock),1,0)==0){core_.detach();leave();}
        UnmapViewOfFile(ledger_);ledger_=nullptr;
    }
    host_=nullptr;mod_=nullptr;thread_=0;
}
void ProcessBudget::lifecycle(void* user,int event,void*)
{
    auto& self=*static_cast<ProcessBudget*>(user);std::string error;
    if(!self.enter(error)){self.unavailable_=error;return;}
    const Event kind=event==SK_EV_APP_PREINIT?Event::PreInit:event==SK_EV_APP_INIT?Event::AppInit:
                     event==SK_EV_POST_INIT?Event::PostInit:Event::Shutdown;
    const char* why=nullptr;const bool okay=self.core_.event(kind,why);self.cached_=self.core_.accounting();self.leave();
    if(!okay)self.unavailable_=why?why:restart;
}
bool ProcessBudget::begin(const std::wstring& profile_cache,const std::string& hash,Key root,uint32_t bytes,
                          bool current_owned,Ticket& ticket,std::string& error)
{
    ticket={};error.clear();Hash content{},profile{};
    if(!parse_hash(hash.c_str(),content)||!profile_hash(profile_cache,profile)){error="Cannot validate imported-resource content/profile identity.";return false;}
    if(!enter(error))return false;
    const char* why=nullptr;const bool okay=core_.bind_profile(profile,why)&&core_.begin(content,root,bytes,current_owned,ticket,why);
    cached_=core_.accounting();leave();if(!okay)error=why?why:restart;return okay;
}
bool ProcessBudget::finish(const Ticket& ticket,bool success,std::string& error)
{
    if(!enter(error))return false;
    const char* why=nullptr;const bool okay=core_.finish(ticket,success,why);cached_=core_.accounting();leave();
    if(!okay)error=why?why:restart;return okay;
}
} // namespace package_admission
