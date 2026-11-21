// SDK lifecycle and fixed Windows process ledger. No native game calls.
#pragma once
#include "package_admission.h"
#include "sporekit.h"
#include <string>

namespace package_admission {
class ProcessBudget {
public:
    ProcessBudget()=default;
    ProcessBudget(const ProcessBudget&)=delete;
    ProcessBudget& operator=(const ProcessBudget&)=delete;
    bool init(const SkHost* host,SkMod* mod,std::string& error);
    void shutdown(); // only detach this view/subscriptions; retain the process owner handle
    bool begin(const std::wstring& profile_cache,const std::string& hash,Key root,uint32_t bytes,
               bool current_owned,Ticket& ticket,std::string& error);
    bool finish(const Ticket& ticket,bool success,std::string& error);
    Accounting accounting() const { return cached_; }
private:
    static void lifecycle(void* user,int event,void* arg);
    bool enter(std::string& error);
    void leave();
    const SkHost* host_=nullptr;
    SkMod* mod_=nullptr;
    Ledger* ledger_=nullptr;
    Core core_;
    Accounting cached_{};
    uint32_t thread_=0;
    uint32_t subscribed_=0;
    std::string unavailable_;
};
} // namespace package_admission
