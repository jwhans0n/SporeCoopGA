#include "package_admission.h"
#include <cstring>
#include <limits>

namespace package_admission {
namespace {
constexpr const char* restart="Imported-resource ownership is unknown. Restart Spore before importing more content. Saved drafts are preserved.";
bool same_identity(const Identity& a,const Identity& b)
{
    return a.pid==b.pid && a.creation==b.creation && a.executable==b.executable && !a.reserved && !b.reserved;
}
bool valid_key(const Key& key) { return key.instance && key.type; }
bool valid_ledger(const Ledger& p)
{
    if(p.magic!=kMagic || p.version!=kVersion || p.size!=sizeof(Ledger) || !p.identity.pid || !p.identity.creation ||
       p.identity.reserved || !p.owner_handle || p.count>kMaxPackages || p.bytes>kMaxBytes || p.boot_stage>3 ||
       p.profile_bound>1 || p.flags&~(kUncertain|kRetired) || p.attached_module>p.module_counter ||
       p.active_reused>1 || (p.active_ticket && (!p.active_module || p.active_index>=p.count ||
       p.active_module!=p.attached_module || p.active_ticket>p.ticket_counter))) return false;
    uint32_t bytes=0;
    for(uint32_t i=0;i<p.count;++i) {
        const auto& item=p.entries[i];
        if(!valid_key(item.root) || !item.bytes || item.bytes>kMaxBundleBytes || !item.module || item.module>p.module_counter ||
           (item.state!=State::Reserved && item.state!=State::Success && item.state!=State::Uncertain) ||
           item.bytes>kMaxBytes-bytes) return false;
        bytes+=item.bytes;
        for(uint32_t j=0;j<i;++j) if(p.entries[j].hash==item.hash) return false;
    }
    return bytes==p.bytes && (!p.count || p.profile_bound);
}
}
void Core::initialize(Ledger& p,const Identity& identity,uint64_t owner_handle)
{
    std::memset(&p,0,sizeof p);
    p.magic=kMagic;p.version=kVersion;p.size=sizeof p;p.identity=identity;p.owner_handle=owner_handle;
}
bool Core::attach(Ledger& p,const Identity& identity,const char*& error)
{
    error=nullptr;
    if(ledger_ || !valid_ledger(p) || !same_identity(p.identity,identity) || p.attached_module ||
       p.module_counter==std::numeric_limits<uint32_t>::max()) {error=restart;return false;}
    ledger_=&p;module_=++p.module_counter;p.attached_module=module_;reloaded_=p.count!=0;
    return true;
}
bool Core::valid(const char*& error) const
{
    if(!ledger_ || !valid_ledger(*ledger_) || ledger_->attached_module!=module_) {error=restart;return false;}
    return true;
}
void Core::detach()
{
    if(ledger_ && ledger_->attached_module==module_) {
        if(ledger_->active_ticket) {
            ledger_->flags|=kUncertain;
            if(ledger_->active_index<ledger_->count && ledger_->active_index<kMaxPackages)
                ledger_->entries[ledger_->active_index].state=State::Uncertain;
        }
        // A torn operation remains charged. Clear its transient owner only after latching uncertainty.
        ledger_->active_ticket=0;ledger_->active_module=0;ledger_->active_index=0;ledger_->active_reused=0;
        ledger_->attached_module=0;
    }
    ledger_=nullptr;module_=0;reloaded_=false;
}
bool Core::event(Event event,const char*& error)
{
    error=nullptr;if(!valid(error))return false;
    auto& p=*ledger_;
    if(event==Event::Shutdown){
        p.flags|=kRetired;
        if(p.active_ticket){p.flags|=kUncertain;p.entries[p.active_index].state=State::Uncertain;}
        p.active_ticket=0;p.active_module=0;p.active_index=0;p.active_reused=0;
        return true;
    }
    const Event expected[]={Event::PreInit,Event::AppInit,Event::PostInit};
    if(p.boot_stage>=3 || event!=expected[p.boot_stage]) {p.flags|=kUncertain;error=restart;return false;}
    ++p.boot_stage;return true;
}
bool Core::bind_profile(const Hash& hash,const char*& error)
{
    error=nullptr;if(!valid(error))return false;
    auto& p=*ledger_;
    if(p.profile_bound && p.profile!=hash){p.flags|=kUncertain;error=restart;return false;}
    if(!p.profile_bound){p.profile=hash;p.profile_bound=1;}
    return true;
}
bool Core::begin(const Hash& hash,const Key& root,uint32_t bytes,bool current_owned,Ticket& ticket,const char*& error)
{
    ticket={};error=nullptr;if(!valid(error))return false;
    auto& p=*ledger_;
    if(p.boot_stage!=3 || !p.profile_bound || p.flags) {error=restart;return false;}
    if(reloaded_) {error="Native imports cannot safely resume after mod reload. Restart Spore; saved drafts are preserved.";return false;}
    if(!valid_key(root) || !bytes || bytes>kMaxBundleBytes){error="Invalid native package admission identity or payload size.";return false;}
    if(p.active_ticket){error="A native import has not completed. Retry after it settles, or restart Spore if it was interrupted.";return false;}
    if(p.ticket_counter==std::numeric_limits<uint64_t>::max()){p.flags|=kUncertain;error=restart;return false;}
    uint32_t index=p.count;bool reused=false;
    for(uint32_t i=0;i<p.count;++i) if(p.entries[i].hash==hash) {
        const auto& entry=p.entries[i];
        if(entry.root!=root || entry.bytes!=bytes || entry.module!=module_ || entry.state!=State::Success || !current_owned) {
            error=restart;return false;
        }
        index=i;reused=true;break;
    }
    if(!reused) {
        if(current_owned){error=restart;return false;}
        if(p.count==kMaxPackages || bytes>kMaxBytes-p.bytes) {
            error="This Spore process reached its imported-resource limit. Save your draft and restart Spore to import more content.";
            return false;
        }
        p.entries[index]={hash,root,bytes,module_,State::Reserved};++p.count;p.bytes+=bytes;
    }
    p.active_ticket=++p.ticket_counter;p.active_module=module_;p.active_index=index;p.active_reused=reused?1u:0u;
    ticket={p.active_ticket,module_,index,reused};return true;
}
bool Core::finish(const Ticket& ticket,bool success,const char*& error)
{
    error=nullptr;if(!valid(error))return false;
    auto& p=*ledger_;
    if(!ticket.serial || p.active_ticket!=ticket.serial || p.active_module!=ticket.module || ticket.module!=module_ ||
       p.active_index!=ticket.index || p.active_reused!=(ticket.reused?1u:0u)) {error="Stale native package admission completion.";return false;}
    auto& entry=p.entries[ticket.index];
    success=success && !(p.flags&(kUncertain|kRetired));
    entry.state=success?State::Success:State::Uncertain;
    if(!success)p.flags|=kUncertain;
    p.active_ticket=0;p.active_module=0;p.active_index=0;p.active_reused=0;
    if(!success){error="A native package import could not complete. Its admission remains charged; restart Spore before importing more content.";return false;}
    return true;
}
Accounting Core::accounting() const
{
    Accounting result{};const char* error=nullptr;if(!valid(error))return result;
    const auto& p=*ledger_;result.count=p.count;result.bytes=p.bytes;result.boot_stage=p.boot_stage;result.flags=p.flags;result.reloaded=reloaded_;
    for(uint32_t i=0;i<p.count;++i){result.successful+=p.entries[i].state==State::Success;result.uncertain+=p.entries[i].state==State::Uncertain;}
    return result;
}
} // namespace package_admission
