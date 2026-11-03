// Actual selected-Saved gate, controlled native loader/collector/store endpoints.
// The real receipt codec and retained genuine Saved/dependency bytes are linked.
// No game, DLL, window or profile database is opened.
#include "../mods/coop/editor_saved_native.h"
#include "../mods/coop/editor_saved_dependencies_store.h"
#include "../sdk/include/sporekit_game.hpp"
#include "editor_saved_dependencies_fixture.h"
#include <cstdarg>
#include <deque>
#include <new>

static bool fail_ticket_allocation=false;
void* operator new(std::size_t size,const std::nothrow_t&)noexcept {
    if(fail_ticket_allocation){fail_ticket_allocation=false;return nullptr;}
    try{return ::operator new(size);}catch(...){return nullptr;}
}
void operator delete(void* p,const std::nothrow_t&)noexcept {::operator delete(p);}

namespace gate_test {
namespace saved=editor_saved_dependencies;
using saved_dependency_test::check;
using saved_dependency_test::Fixture;
struct Task {SkTaskFn function=nullptr;void* value=nullptr;};
alignas(void*) std::array<uint8_t,0xA8> controller{},replacement_controller{};
alignas(void*) std::array<uint8_t,0x30> root{},replacement_root{},parent{},database{};
std::array<void*,3> controller_vtable{};
std::array<void*,2> root_vtable{};
std::array<void*,11> database_vtable{};
void* parent_pointer=parent.data();
bool presenting=false,queue_failure=false,receipt_missing=false,receipt_corrupt=false;
bool uploaded_valid=true,registration_valid=true,inventory_changed=false,settled_changed=false;
bool dependency_changed=false,raw_failure=false;
bool selected_changed=false,selected_changed_late=false;
double now=1;
std::string kit="0.3.5";
std::deque<Task> tasks;
std::vector<std::string> events;
std::function<void()> controller_add_callback,root_add_callback,registration_callback,read_callback;
std::function<void()> direct_start_callback,direct_advance_callback,direct_reset_callback;
unsigned os_module_refs=0,sdk_module_refs=0,direct_native_depth=0,fail_os_pins=0;
bool module_loaded=true,fail_sdk_pin=false;
unsigned ctor_calls=0,dtor_calls=0,start_calls=0,advance_calls=0,reset_calls=0,idle_calls=0;
unsigned controller_adds=0,controller_releases=0,root_adds=0,root_releases=0;
unsigned registrations=0,reads=0,loads=0,publishes=0,hooks=0,removed_hooks=0;
unsigned selected_reads=0;
int hook_failure=-1;
std::unique_ptr<Fixture> genuine;
SkHost host{};
const std::wstring database_path=L"C:\\controlled-saved-gate\\EditorSaves.package";

void outside(const char* action){check(!presenting,std::string("outside Present: ")+action);events.emplace_back(action);}
void code_alive(){check(!direct_native_depth || module_loaded || os_module_refs || sdk_module_refs,"direct Native return keeps callback DLL mapped");}
struct DirectNative {DirectNative(){++direct_native_depth;code_alive();}~DirectNative(){code_alive();--direct_native_depth;}};
BOOL WINAPI retain_module(DWORD flags,LPCWSTR address,HMODULE* module){
    check(flags==GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS && address && module,"callback-address module retention");
    if(fail_os_pins){--fail_os_pins;return FALSE;}++os_module_refs;*module=HMODULE(0x4F1100);return TRUE;
}
BOOL WINAPI release_module(HMODULE module){
    check(!presenting,"module release outside Present");check(module==HMODULE(0x4F1100) && os_module_refs,"owned callback module ref");
    --os_module_refs;code_alive();return TRUE;
}
void* SK_THISCALL native_ctor(void*);
void SK_THISCALL native_dtor(void*);
bool SK_THISCALL native_start(void*,void*,bool,bool);
bool SK_THISCALL native_advance(void*,bool);
void SK_THISCALL native_reset(void*,bool,uint32_t);
void SK_THISCALL native_idle(void*);
int SK_THISCALL controller_add(void*);
int SK_THISCALL controller_release(void*);
int SK_THISCALL root_add(void*);
int SK_THISCALL root_release(void*);
const wchar_t* SK_THISCALL location(void*){outside("database-location");return database_path.c_str();}
void* SK_CDECL get_database(uint32_t id){outside("database-get");check(id==0x11AC199,"profile database ID");return database.data();}
const char* kit_version(){return kit.c_str();}
const char* executable(){return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";}
double clock(){return now;}
void log(SkMod*,int,const char*,...){ }
uintptr_t symbol(const char* name){
    const std::string key(name);
    if(key=="App::cScenarioMode::ptr")return uintptr_t(&parent_pointer);
    if(key=="Resource::GetDatabaseByID")return uintptr_t(get_database);
    const std::array<std::pair<const char*,uintptr_t>,6> functions{{
        {"LoadControllerCtor",uintptr_t(native_ctor)},{"LoadControllerDtor",uintptr_t(native_dtor)},
        {"LoadControllerStart",uintptr_t(native_start)},{"LoadControllerAdvance",uintptr_t(native_advance)},
        {"LoadControllerReset",uintptr_t(native_reset)},{"LoadControllerIdle",uintptr_t(native_idle)}}};
    for(const auto& [suffix,value]:functions)if(key==std::string("SporeCoop::Editor::")+suffix)return value;
    return 0;
}
int hook_create(SkMod*,void* address,void*,void** original){
    outside("hook-create");const auto index=hooks++;if(int(index)==hook_failure)return 1;*original=address;return 0;
}
int hook_remove(SkMod*,void*){outside("hook-remove");++removed_hooks;return 0;}
void enqueue(SkTaskFn,void*);
void execute_task(const Task& task){
    struct SdkRelease {~SdkRelease(){check(sdk_module_refs>0,"SDK queued callback owns a module pin");--sdk_module_refs;code_alive();}} release;
    task.function(task.value);
}
void dispatch_all(){outside("dispatch");while(!tasks.empty()){const auto task=tasks.front();tasks.pop_front();execute_task(task);}}
}

#define GetModuleHandleExW gate_test::retain_module
#define FreeLibrary gate_test::release_module
#include "../mods/coop/editor_saved_native.cpp"
#undef GetModuleHandleExW
#undef FreeLibrary

namespace gate_test {
namespace gate=editor_saved_native;
size_t pending_tasks(){return size_t(std::count_if(tasks.begin(),tasks.end(),[](const auto& task){return task.function==gate::dispatch;}));}
auto pending_task(){return std::find_if(tasks.begin(),tasks.end(),[](const auto& task){return task.function==gate::dispatch;});}
void execute_pending(){const auto found=pending_task();check(found!=tasks.end(),"pending gate task exists");const auto task=*found;tasks.erase(found);execute_task(task);}
void enqueue(SkTaskFn function,void* payload){
    if(function==gate::dispatch){
        check(skg::field<uint32_t>(controller.data(),0x1C)==1 && skg::field<uint32_t>(controller.data(),0x2C)==0,
              "native owns real pending phase0 before enqueue");
        check(skg::field<void*>(controller.data(),0x18)==root.data(),"native owns selected root before enqueue");
    }
    if(queue_failure)throw std::bad_alloc();
    if(fail_sdk_pin)throw std::runtime_error("controlled SDK cannot retain callback module");
    ++sdk_module_refs;
    try{tasks.push_back({function,payload});}catch(...){--sdk_module_refs;throw;}
}
void* SK_THISCALL native_ctor(void* self){++ctor_calls;skg::field<void**>(self,0)=controller_vtable.data();return self;}
void SK_THISCALL native_dtor(void*){++dtor_calls;}
void SK_THISCALL native_reset(void* self,bool,uint32_t){
    DirectNative callback;if(direct_reset_callback)direct_reset_callback();
    ++reset_calls;skg::field<void*>(self,0x18)=nullptr;skg::field<uint32_t>(self,0x1C)=0;
    skg::field<uint32_t>(self,0x2C)=6;skg::field<uint32_t>(self,0x90)=0;
}
bool SK_THISCALL native_advance(void* self,bool check_only){
    DirectNative callback;if(direct_advance_callback)direct_advance_callback();
    ++advance_calls;events.emplace_back(registrations?"checks-after-admission":"original-checks");
    if(!check_only)skg::field<uint32_t>(self,0x2C)=2;return true;
}
bool SK_THISCALL native_start(void* self,void* selected,bool,bool check_only){
    DirectNative callback;if(direct_start_callback)direct_start_callback();
    ++start_calls;gate::reset(self,false,1);
    skg::field<void*>(self,0x18)=selected;skg::field<uint32_t>(self,0x1C)=1;
    skg::field<uint32_t>(self,0x2C)=0;
    return gate::advance(self,check_only);
}
void SK_THISCALL native_idle(void*){++idle_calls;}
int SK_THISCALL controller_add(void*){outside("controller-add");++controller_adds;if(controller_add_callback)controller_add_callback();return 1;}
int SK_THISCALL controller_release(void*){outside("controller-release");++controller_releases;return 1;}
int SK_THISCALL root_add(void*){outside("root-add");++root_adds;if(root_add_callback)root_add_callback();return 1;}
int SK_THISCALL root_release(void*){outside("root-release");++root_releases;return 1;}
void reset_fixture(){
    controller_add_callback=root_add_callback=registration_callback=read_callback={};
    direct_start_callback=direct_advance_callback=direct_reset_callback={};
    presenting=false;module_loaded=true;gate::shutdown();dispatch_all();
    controller.fill(0);replacement_controller.fill(0);root.fill(0);replacement_root.fill(0);parent.fill(0);database.fill(0);
    controller_vtable={reinterpret_cast<void*>(native_dtor),reinterpret_cast<void*>(controller_add),reinterpret_cast<void*>(controller_release)};
    root_vtable={reinterpret_cast<void*>(root_add),reinterpret_cast<void*>(root_release)};
    database_vtable.fill(nullptr);database_vtable[0x28/4]=reinterpret_cast<void*>(location);
    for(auto* value:{root.data(),replacement_root.data()}){
        skg::field<void**>(value,0)=root_vtable.data();skg::field<dbpf::Key>(value,8)=genuine->inventory.saved;
    }
    skg::field<void**>(database.data(),0)=database_vtable.data();parent_pointer=parent.data();skg::field<void*>(parent.data(),0x1C)=controller.data();
    queue_failure=receipt_missing=receipt_corrupt=inventory_changed=settled_changed=dependency_changed=raw_failure=false;
    selected_changed=selected_changed_late=false;
    uploaded_valid=registration_valid=true;fail_ticket_allocation=false;now=1;kit="0.3.5";hook_failure=-1;
    controller_add_callback=root_add_callback=registration_callback=read_callback={};
    direct_start_callback=direct_advance_callback=direct_reset_callback={};
    os_module_refs=sdk_module_refs=direct_native_depth=fail_os_pins=0;module_loaded=true;fail_sdk_pin=false;
    ctor_calls=dtor_calls=start_calls=advance_calls=reset_calls=idle_calls=0;
    controller_adds=controller_releases=root_adds=root_releases=registrations=reads=loads=publishes=hooks=removed_hooks=0;
    selected_reads=0;
    events.clear();gate::deferred=gate::resumed=gate::restored=gate::cancelled=gate::failed=0;
    host={};host.abi_version=SK_ABI_VERSION;host.size=sizeof host;host.log=log;host.sym=symbol;
    host.exe_sha256=executable;host.kit_version=kit_version;host.time_seconds=clock;
    host.hook_create=hook_create;host.hook_remove=hook_remove;host.run_outside_frame=enqueue;
}
void initialize(){check(gate::init(&host,nullptr),"init");check(hooks==6,"six verified hooks");gate::construct(controller.data());dispatch_all();}
void begin(){
    presenting=true;check(gate::start(controller.data(),root.data(),true,false),"start pending");
    check(advance_calls==0 && pending_tasks()==1,"one deferred phase0, no checks yet");
    check(controller_adds==0 && root_adds==0 && registrations==0,"no gate native ownership or admission in Present");
    check(gate::advance(controller.data(),false) && pending_tasks()==1,"pending reentry neither advances nor requeues");
    gate::idle(controller.data());check(idle_calls==0,"native timeout suppressed while held");presenting=false;
}
void no_resume(){check(advance_calls==0 && registrations==0,"stale ticket performs no checks/admission");}
void balanced(){check(controller_adds==controller_releases && root_adds==root_releases,"temporary ownership balanced");}
}

namespace editor_native_save {
bool read_saved(const editor::ResourceKey& key,std::vector<uint8_t>& out,std::string& error){
    using namespace gate_test;outside("read-saved");++reads;
    check(key.instance==genuine->inventory.saved.instance && key.type==saved::kScenario,"exact Saved key");
    if(read_callback)read_callback();if(raw_failure){error="controlled missing raw";return false;}
    out=genuine->raw;if(settled_changed && reads>1)out.back()^=1;return true;
}
}
namespace adventure_collect {
bool selected_saved_references(void* selected,editor_saved_dependencies::Inventory& out,std::string&){
    using namespace gate_test;outside("selected-inventory");++selected_reads;check(selected==root.data(),"exact held selected root");
    out=genuine->inventory;if(selected_changed || (selected_changed_late && selected_reads>1))out.references[0].model^=1;return true;
}
bool saved_references(const dbpf::Key& key,const std::vector<uint8_t>& raw,editor_saved_dependencies::Inventory& out,std::string&){
    using namespace gate_test;outside("raw-inventory");check(key==genuine->inventory.saved && raw==genuine->raw,"raw selected issuer inputs");
    out=genuine->inventory;if(inventory_changed)out.references[0].model^=1;return true;
}
bool validate_uploaded_records(const dbpf::Key&,const std::vector<dbpf::Record>& records,std::string&){
    using namespace gate_test;outside("validate-records");check(records.size()==4,"dependency-only admission");return uploaded_valid;
}
bool saved_dependencies(const dbpf::Key& key,const std::vector<uint8_t>& raw,editor_saved_dependencies::Inventory& out,std::vector<dbpf::Record>& selected,std::string&){
    using namespace gate_test;outside("installed-inventory");check(key==genuine->inventory.saved && raw==genuine->raw,"installed selected issuer inputs");
    out=genuine->inventory;selected=genuine->selected;if(dependency_changed)selected[0].data.back()^=1;return true;
}
}
namespace adventure_assets {
bool register_records(const std::vector<dbpf::Record>& records,std::string&){
    using namespace gate_test;outside("register");++registrations;check(records.size()==4,"never mount transport root");
    if(registration_callback)registration_callback();return registration_valid;
}
}
namespace editor_saved_dependencies {
bool load(const std::filesystem::path&,const Domain& identity,Key selected,std::span<const uint8_t> raw,Receipt& out,std::string& error){
    using namespace gate_test;outside("load-receipt");++loads;if(receipt_missing){error="controlled missing receipt";return false;}
    auto receipt=genuine->receipt;receipt.domain=identity;Bytes encoded;
    check(encode(receipt,encoded,error),"real encode");if(receipt_corrupt)encoded.back()^=1;
    if(!decode(encoded,identity,out,error))return false;
    return out.inventory.saved==selected && out.inventory.raw_size==raw.size() && out.inventory.raw_hash==saved_dependency_test::digest(raw);
}
Publication publish(const std::filesystem::path&,const Receipt& receipt,std::string& error){
    using namespace gate_test;outside("publish-receipt");++publishes;Bytes bytes;Receipt decoded;
    check(encode(receipt,bytes,error) && decode(bytes,receipt.domain,decoded,error),"real publish codec");return Publication::Published;
}
}

int main(){
    using namespace gate_test;
    try{
        genuine=std::make_unique<Fixture>(saved_dependency_test::fixture());
        reset_fixture();initialize();begin();dispatch_all();
        check(gate::restored==1 && gate::resumed==1 && advance_calls==1 && registrations==1,"exact receipt restores then checks once");
        check(reads==2 && loads==1 && events.back()=="controller-release","raw reread and owning cleanup");balanced();
        for(unsigned kind=0;kind<7;++kind){
            reset_fixture();initialize();receipt_missing=kind==0;receipt_corrupt=kind==1;raw_failure=kind==2;
            inventory_changed=kind==3;uploaded_valid=kind!=4;registration_valid=kind!=5;dependency_changed=kind==6;
            begin();dispatch_all();check(gate::restored==0 && gate::failed==1 && advance_calls==1,"invalid/missing proof keeps original native checks");balanced();
        }
        reset_fixture();initialize();settled_changed=true;begin();dispatch_all();
        check(gate::restored==0 && advance_calls==1 && reads==2,"changed raw image does not claim restored");balanced();
        for(bool late:{false,true}){
            reset_fixture();initialize();selected_changed=!late;selected_changed_late=late;begin();dispatch_all();
            check(gate::restored==0 && advance_calls==1 && registrations==unsigned(late),"actual selected roster mismatch never claims restored");balanced();
        }
        // Weak identities must retire without any new native owner acquisition.
        for(unsigned kind=0;kind<8;++kind){
            reset_fixture();initialize();begin();
            auto* queued=static_cast<gate::Ticket*>(pending_task()->value);
            if(kind==0)++queued->module;
            if(kind==1)++queued->lifetime;
            if(kind==2)++queued->serial;
            if(kind==3)skg::field<void*>(parent.data(),0x1C)=replacement_controller.data();
            if(kind==4)parent_pointer=nullptr;
            if(kind==5)skg::field<void*>(controller.data(),0x18)=replacement_root.data();
            if(kind==6)skg::field<dbpf::Key>(root.data(),8).instance^=1;
            if(kind==7)skg::field<uint32_t>(controller.data(),0x90)=7;
            dispatch_all();no_resume();
            const bool cleanup_owned=kind==3 || kind==4 || kind==6;
            check(controller_adds==unsigned(cleanup_owned) && root_adds==0,"stale before forward ownership kind="+std::to_string(kind));
            if(cleanup_owned)check(reset_calls==2 && skg::field<uint32_t>(controller.data(),0x2C)==6,"retired parent/key still cancels exact owned phase0");
            balanced();
        }
        // Same-address destruction/construction cannot revive an old queued task.
        reset_fixture();initialize();begin();const auto old_life=gate::loaders[controller.data()].lifetime;
        gate::destroy(controller.data());gate::construct(controller.data());
        check(gate::loaders[controller.data()].lifetime!=old_life,"new birth at reused address");dispatch_all();no_resume();
        reset_fixture();initialize();begin();gate::reset(controller.data(),true,3);dispatch_all();no_resume();check(reset_calls==2,"ordinary cancellation stays original");
        // AddRef is callback-bearing: close controller identity before root AddRef.
        reset_fixture();initialize();begin();controller_add_callback=[] {gate::reset(controller.data(),true,3);};
        dispatch_all();no_resume();check(controller_adds==1 && root_adds==0,"controller callback expiry precedes root ownership");balanced();
        reset_fixture();initialize();begin();root_add_callback=[] {gate::reset(controller.data(),true,3);};
        dispatch_all();no_resume();balanced();
        // Reentrant unload must cancel the actual owned phase0 before returning.
        for(bool restart:{false,true}){
            reset_fixture();initialize();begin();bool fired=false;
            registration_callback=[&]{fired=true;gate::shutdown();if(restart){check(gate::init(&host,nullptr),"new module generation");gate::construct(controller.data());}};
            dispatch_all();check(fired && reset_calls==2 && advance_calls==0,"nested unload cancels and never resumes");balanced();
        }
        reset_fixture();initialize();begin();bool replaced_request=false;
        registration_callback=[&]{if(!replaced_request){replaced_request=true;check(gate::start(controller.data(),root.data(),true,false),"nested native request");}};
        execute_pending();
        check(replaced_request && advance_calls==0 && pending_tasks()==1,"reentrant registration retires old request without resume");
        dispatch_all();check(advance_calls==1 && gate::resumed==1 && gate::restored==1,"only new exact request resumes");balanced();
        reset_fixture();initialize();begin();gate::shutdown();
        check(skg::field<uint32_t>(controller.data(),0x1C)==0 && skg::field<uint32_t>(controller.data(),0x2C)==6,"unload cancels real phase0");
        const auto stopped_adds=controller_adds,stopped_releases=controller_releases;
        dispatch_all();no_resume();check(controller_adds==stopped_adds && controller_releases==stopped_releases && root_adds==0,"delayed post-unload task has zero native calls");balanced();
        // Expiry is checked outside-frame, never left for Native timeout OK.
        reset_fixture();initialize();begin();now=6.001;dispatch_all();
        check(gate::cancelled==1 && reset_calls==2 && advance_calls==0 && registrations==0,"five second expiry uses ordinary cancellation");balanced();
        reset_fixture();initialize();begin();now=6;dispatch_all();check(advance_calls==1,"exact five second boundary valid");balanced();
        // checkOnly and unrelated phase paths preserve the original native path.
        reset_fixture();initialize();presenting=true;check(gate::start(controller.data(),root.data(),false,true),"checkOnly passthrough");presenting=false;
        check(pending_tasks()==0 && advance_calls==1 && registrations==0 && controller_adds==0,"checkOnly never gates");dispatch_all();
        reset_fixture();initialize();skg::field<uint32_t>(controller.data(),0x2C)=2;
        check(gate::advance(controller.data(),false) && advance_calls==1 && pending_tasks()==0,"unrelated phase passthrough");dispatch_all();
        for(unsigned queue_error=0;queue_error<3;++queue_error){
            reset_fixture();initialize();queue_failure=queue_error==1;fail_sdk_pin=queue_error==2;fail_ticket_allocation=queue_error==0;presenting=true;
            check(gate::start(controller.data(),root.data(),true,false),"queue failure original result");presenting=false;
            check(pending_tasks()==0 && advance_calls==1 && registrations==0 && !gate::loaders[controller.data()].pending,"allocation/queue failure keeps original checks");
        }
        for(const char* version:{"0.3.4","0.2.99","bad"}){
            reset_fixture();kit=version;check(!gate::init(&host,nullptr) && hooks==0,"old Kit cannot enroll");
        }
        for(int failed_hook=0;failed_hook<6;++failed_hook){
            reset_fixture();hook_failure=failed_hook;check(!gate::init(&host,nullptr),"hook failure unavailable");
            check(removed_hooks==unsigned(failed_hook),"partial hook install unwound");
        }
        reset_fixture();initialize();std::string error;
        check(gate::persist(genuine->inventory.saved,genuine->raw,genuine->admitted,[]{return true;},error) && publishes==1,"persist only exact owned genuine closure");
        const auto before_publish=publishes;
        check(!gate::persist(genuine->inventory.saved,genuine->raw,genuine->admitted,[]{return false;},error) && publishes==before_publish,"expired persistence never publishes");
        for(bool reinitialize:{false,true})for(unsigned expire_at:{1u,2u}){
            reset_fixture();initialize();unsigned callbacks=0;bool fired=false;
            const auto fence=[&]{
                if(++callbacks==expire_at){fired=true;gate::shutdown();if(reinitialize){check(gate::init(&host,nullptr),"persist callback new module");gate::construct(controller.data());}}
                return true;
            };
            check(!gate::persist(genuine->inventory.saved,genuine->raw,genuine->admitted,fence,error),"persistence callback cannot adopt replacement module");
            check(fired && publishes==0,"post-callback module closure precedes publication");
        }
        // Direct Native hooks lack the SDK's queued-callback module pin. They
        // must keep their first OS ref until an SDK-pinned release helper runs.
        for(unsigned action=0;action<3;++action){
            reset_fixture();initialize();bool unloaded=false;
            const auto unload_inside_native=[&]{
                check(direct_native_depth>0 && os_module_refs>0,"direct hook acquired its own DLL ref before Native call");
                gate::shutdown();module_loaded=false;unloaded=true;
                dispatch_all(); // arbitrary nested native modal/message pump
                code_alive();check(os_module_refs>0,"active direct hook still retains its DLL after nested pump");
            };
            if(action==0){direct_start_callback=unload_inside_native;check(gate::start(controller.data(),root.data(),true,false),"direct Start returns after self unload");}
            if(action==1){direct_advance_callback=unload_inside_native;check(gate::advance(controller.data(),true),"direct Advance returns after self unload");}
            if(action==2){direct_reset_callback=unload_inside_native;gate::reset(controller.data(),true,3);}
            check(unloaded && direct_native_depth==0 && os_module_refs>0 && sdk_module_refs>0,"direct return transfers last release to SDK-owned task");
            direct_start_callback=direct_advance_callback=direct_reset_callback={};dispatch_all();
            check(os_module_refs==0 && sdk_module_refs==0,"final DLL release occurs only after callbacks return outside DLL");
        }
        reset_fixture();initialize();fail_sdk_pin=true;
        gate::idle(controller.data());
        check(os_module_refs==1 && sdk_module_refs==0 && tasks.empty(),"failed SDK second pin deliberately retains initial OS ref");
        fail_sdk_pin=false;gate::shutdown();module_loaded=false;
        check(os_module_refs==1,"enqueue failure cannot drop final ref from inside DLL");
        // A failed first OS pin makes safe unload unavailable until that Native
        // callback returns; Kit must keep the module and its hooks/data intact.
        reset_fixture();initialize();fail_os_pins=1;bool unload_blocked=false;
        const auto old_module=gate::module_generation;
        direct_start_callback=[&]{
            try{gate::shutdown();module_loaded=false;}
            catch(const std::runtime_error&){unload_blocked=true;}
            check(unload_blocked && gate::available && !gate::stopping && !gate::retiring && gate::module_generation==old_module,
                  "unretained direct callback rejects unload before data retirement");
        };
        check(gate::start(controller.data(),root.data(),true,false),"initial retention failure still completes original Native callback");
        direct_start_callback={};check(unload_blocked && gate::unretained_callbacks==0 && module_loaded,"failed first retention keeps caller DLL alive");
        gate::shutdown();module_loaded=false;dispatch_all();
        check(advance_calls==0 && os_module_refs==0 && sdk_module_refs==0,"later safe unload cancels deferred operation and releases remaining refs");
        module_loaded=true;gate::shutdown();dispatch_all();
        std::printf("editor Saved Native gate: %llu checks\n",static_cast<unsigned long long>(saved_dependency_test::checks));return 0;
    }catch(const std::exception& e){std::printf("FAIL: %s\n",e.what());return 1;}
}
