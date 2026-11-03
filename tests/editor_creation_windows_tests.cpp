// Production creation lifecycle and detached cast helper, actual i686 ABI.
// Controlled native objects prove ownership/routing, not running-game support.
#include "../mods/coop/editor_creation.cpp"
#include "../mods/coop/editor_creation_native.cpp"
#include "../mods/coop/editor_creation_png.cpp"
#include <cstdio>
#include <cstdarg>
#include <cstdlib>
#include <deque>
#include <set>
#include <stdexcept>

static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
namespace {
std::array<void*,3> refs_vt{};
std::array<uint8_t,0x9C> request{};
std::array<uint8_t,0x1D0> native_editor{};
std::array<uint8_t,0x48> native_result{};
std::array<uint8_t,0x2CB0> owned_resource{};
std::vector<uint8_t> owned_classes;
std::deque<std::pair<SkTaskFn,void*>> tasks;
struct NativeMessage { bool result;void* object; };
std::deque<NativeMessage> native_messages;
editor_creation::Notify notify_hook=nullptr;
editor_creation_native::Assign assign_hook=nullptr;
uint32_t mode=modes::kScenario;
double now=1;
unsigned submissions=0,native_returns=0,native_imports=0,finished=0,returned=0,live_class_writes=0;
unsigned native_result_deliveries=0,native_mode_returns=0;
unsigned owned_edits=0,native_class_imports=0,creation_collections=0;
uint32_t native_existing_editor=0,submitted_request_editor=0,submitted_editor=0;
editor::ResourceKey submitted_source;
bool grant=false,live_scope=true,lease=true,return_ready=false,submit_ok=true,reject=false;
bool allocation_failure=false;
std::function<void(char)> request_callback;
bool returned_cancelled=false;
editor::ResourceKey returned_saved;
std::set<void*> allocations;
editor_native::GestureScope owner;
std::vector<dbpf::Record> collected;
std::vector<std::string> admission_logs;
void admission_log(SkMod*,int,const char* format,...){
    std::array<char,1024> text{};va_list args;va_start(args,format);
    const auto size=std::vsnprintf(text.data(),text.size(),format,args);va_end(args);
    CHECK(size>=0 && size<int(text.size()));admission_logs.emplace_back(text.data());
}
editor::ResourceKey source{0x29001234,0x2B978C46,0x40626200};
std::array<void*,8> admission_manager_vt{};
std::array<void*,4> admission_model_vt{};
void* admission_manager=nullptr;
std::array<uint8_t,0xAC> admission_models[2];
std::array<uint32_t,4> admission_mask{1,2,4,8};
std::array<std::array<uint32_t,4>,2> admission_errors{};
bool admission_loaded[2]{true,true},admission_object[2]{true,true},admission_cast[2]{true,true};
unsigned admission_loads[2]{},admission_releases[2]{},admission_validates[2]{};
uint64_t admission_generation=7;
std::function<void(char)> admission_callback;
bool admission_rekey=false;
struct AdmissionMask {uint32_t words[4];};
unsigned admission_index(void* p){CHECK(p==admission_models[0].data() || p==admission_models[1].data());return p==admission_models[1].data();}
int SK_THISCALL admission_release(void* p){const auto n=admission_index(p);++admission_releases[n];CHECK(skg::field<int>(p,8)==1);skg::field<int>(p,8)=0;if(admission_callback)admission_callback('R');return 0;}
void* SK_THISCALL admission_query(void* p,uint32_t type){CHECK(type==0x03C609F8);const auto n=admission_index(p);if(admission_callback)admission_callback('Q');return admission_cast[n]?p:nullptr;}
bool admission_get(unsigned n,const editor::ResourceKey* key,void** out,void* a,void* b,void* c,void* d){
    CHECK(key&&key->instance&&key->type&&key->group&&!a&&!b&&!c&&!d);++admission_loads[n];
    if(admission_object[n]){*out=admission_models[n].data();CHECK(!skg::field<int>(*out,8));skg::field<int>(*out,8)=1;}else *out=nullptr;
    if(admission_rekey)const_cast<editor::ResourceKey*>(key)->instance^=1;
    if(admission_callback)admission_callback(n?'P':'L');return admission_loaded[n];
}
bool SK_THISCALL admission_cached(void*,const editor::ResourceKey* key,void** out,void* a,void* b,void* c,void* d){return admission_get(0,key,out,a,b,c,d);}
bool SK_THISCALL admission_private(void*,const editor::ResourceKey* key,void** out,void* a,void* b,void* c,void* d){return admission_get(1,key,out,a,b,c,d);}
uint32_t* SK_THISCALL admission_validate(void* p,uint32_t* out,AdmissionMask mask,bool first){
    const auto n=admission_index(p);CHECK(first&&std::equal(admission_mask.begin(),admission_mask.end(),mask.words));++admission_validates[n];
    std::copy(admission_errors[n].begin(),admission_errors[n].end(),out);if(admission_callback)admission_callback('V');return out;
}
int SK_THISCALL ref(void* p){return ++skg::field<int>(p,8);}
int SK_THISCALL unref(void* p){auto n=--skg::field<int>(p,8);CHECK(n>=0);if(!n && allocations.erase(p))std::free(p);return n;}
void SK_THISCALL native_notify(void* e,void* r){
    // Actual 57A737 and 57A85B call IMessageManager+18, MessagePost. Retain the
    // result until its listener runs after NotifyResult has released the editor
    // references. NotifyResult itself never inspects the cancellation byte.
    ++native_returns;ref(r);native_messages.push_back({true,r});
    native_messages.push_back({false,nullptr});
    auto* req=skg::field<void*>(e,0x1CC);skg::field<void*>(e,0x1CC)=nullptr;
    if(req)unref(req);unref(r);
}
void pump_native_message(){
    CHECK(!native_messages.empty());auto message=native_messages.front();native_messages.pop_front();
    if(message.result){
        CHECK(skg::field<int>(message.object,8)>0);++native_result_deliveries;
        if(!skg::field<bool>(message.object,0x44))++native_imports;
        unref(message.object);
    }else{++native_mode_returns;mode=modes::kScenario;}
}
void pump_native_messages(){while(!native_messages.empty())pump_native_message();}
void SK_THISCALL native_assign(void*,uint32_t,const editor::ResourceKey*){++live_class_writes;}
void row_pointers(){skg::field<void*>(owned_resource.data(),0x2BF4)=owned_classes.data();skg::field<void*>(owned_resource.data(),0x2BF8)=owned_classes.data()+owned_classes.size();}
uint8_t* SK_THISCALL get_class(void* owner,uint32_t index){
    CHECK(skg::field<void*>(owner,0x10)==owned_resource.data());
    for(size_t at=0;at<owned_classes.size();at+=0x27E8)if(skg::field<uint32_t>(owned_classes.data()+at,0)==index)return owned_classes.data()+at+8;
    return nullptr;
}
uint32_t SK_CDECL model_type(const editor::ResourceKey* key){CHECK(key && key->instance);return 7;}
uint32_t SK_THISCALL native_import(void* owner,const editor::ResourceKey* key){
    ++native_class_imports;
    CHECK(skg::field<void*>(owner,0x10)==owned_resource.data());
    auto index=skg::field<uint32_t>(owned_resource.data(),0x2C0C)++;
    const auto at=owned_classes.size();owned_classes.resize(at+0x27E8);row_pointers();
    skg::field<uint32_t>(owned_classes.data()+at,0)=index;
    assign_hook(owner,index,key);return index;
}
bool SK_CDECL submit(void* req){
    ++submissions;if(!submit_ok)return false;
    submitted_request_editor=skg::field<uint32_t>(req,0xC);
    submitted_source=skg::field<editor::ResourceKey>(req,0x10);
    // Native Submit5A930F only calls its subtype resolver for FFFFFFFF.
    // Controlled metadata supplies the editor variant; the adapter must defer
    // to that native decision rather than replacing it with its family default.
    if(submitted_request_editor==UINT32_MAX){
        CHECK(submitted_source.instance && native_existing_editor);
        skg::field<uint32_t>(req,0xC)=native_existing_editor;
    }
    submitted_editor=skg::field<uint32_t>(req,0xC);
    ref(req);skg::field<void*>(native_editor.data(),0x1CC)=req;mode=modes::kEditor;return true;
}
bool SK_CDECL submit_entry(void* req){if(request_callback)request_callback('S');return editor_creation::intercept(req,submit);}
void* SK_CDECL allocate(size_t size,const char*,int,int,int,int){if(allocation_failure)return nullptr;auto* p=std::calloc(1,size);if(p)allocations.insert(p);if(request_callback)request_callback('A');return p;}
void* SK_THISCALL ctor(void* p){skg::field<void*>(p,0)=refs_vt.data();skg::field<uint32_t>(p,0xC)=UINT32_MAX;if(request_callback)request_callback('C');return p;}
uintptr_t sym(const char* name){
    const std::string key=name;
    if(key=="SporeCoop::CreationEditor::NotifyResult")return uintptr_t(native_notify);
    if(key=="SporeCoop::CreationEditor::SetClassAsset")return uintptr_t(native_assign);
    if(key=="SporeCoop::CreationEditor::GetClass")return uintptr_t(get_class);
    if(key=="SporeCoop::CreationEditor::AssetModelType")return uintptr_t(model_type);
    if(key=="SporeCoop::Editor::ImportClass")return uintptr_t(native_import);
    if(key=="SporeCoop::CreationEditor::RequestCtor")return uintptr_t(ctor);
    if(key=="SporeCoop::CreationEditor::Allocate")return uintptr_t(allocate);
    if(key=="Editors::EditorRequest::Submit")return uintptr_t(submit_entry);
    if(key=="Resource::ResourceManager_ptr")return uintptr_t(&admission_manager);
    if(key=="SporeCoop::CreationEditor::ValidateModel")return uintptr_t(admission_validate);
    if(key=="SporeCoop::CreationEditor::AdventureValidationMask")return uintptr_t(admission_mask.data());
    return 0;
}
int hook(SkMod*,void* address,void* detour,void** original){
    *original=address;
    if(address==reinterpret_cast<void*>(native_notify))notify_hook=reinterpret_cast<editor_creation::Notify>(detour);
    else if(address==reinterpret_cast<void*>(native_assign))assign_hook=reinterpret_cast<editor_creation_native::Assign>(detour);
    else CHECK(false);
    return 0;
}
void queue(SkTaskFn fn,void* data){tasks.push_back({fn,data});}
void pump(){auto work=std::move(tasks);tasks.clear();for(auto [fn,data]:work)fn(data);}
void step(){pump_native_messages();editor_creation::tick();pump();}
void initialize(){
    pump_native_messages();
    editor_creation::shutdown();tasks.clear();
    CHECK(allocations.empty());allocation_failure=false;request_callback={};
    request.fill(0);native_editor.fill(0);native_result.fill(0);
    refs_vt={nullptr,reinterpret_cast<void*>(ref),reinterpret_cast<void*>(unref)};
    skg::field<void*>(request.data(),0)=refs_vt.data();skg::field<int>(request.data(),8)=1;
    skg::field<uint32_t>(request.data(),0xC)=0x465C50BA;skg::field<editor::ResourceKey>(request.data(),0x10)=source;
    grant=false;live_scope=lease=true;return_ready=reject=returned_cancelled=false;submit_ok=true;mode=modes::kScenario;now=1;
    submissions=native_returns=native_imports=finished=returned=live_class_writes=0;
    native_result_deliveries=native_mode_returns=0;returned_saved={};
    owned_edits=native_class_imports=creation_collections=0;
    native_existing_editor=submitted_request_editor=submitted_editor=0;submitted_source={};
    owner={};owner.project[0]=9;owner.epoch=12;owner.author={1,3};
    static SkHost host{};host.sym=sym;host.hook_create=hook;host.time_seconds=[](){return now;};host.run_outside_frame=queue;host.log=admission_log;
    host.exe_sha256=[](){return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";};
    editor_creation::Ctx context;
    context.scope=[](){return std::optional<editor_native::GestureScope>{owner};};
    context.current=[](const auto& scope){return live_scope && scope.project==owner.project && scope.epoch==owner.epoch && scope.author==owner.author;};
    context.prepare=[](const auto&,auto& input,std::string& error){input.target=editor::ObjectId{(2ull<<32)|7,1};if(reject){error="denied";return editor_creation::PrepareResult::Reject;}return grant?editor_creation::PrepareResult::Ready:editor_creation::PrepareResult::Wait;};
    context.lease_valid=[](const auto&,auto){return lease;};
    context.returned=[](const auto&,const auto& result,std::string&){++returned;returned_cancelled=result.cancelled;returned_saved=result.saved;return return_ready;};
    context.finished=[](const auto&,bool,const std::string&){++finished;};
    CHECK(editor_creation::init(&host,nullptr,std::move(context)));
}
void start(){CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());grant=true;step();CHECK(submissions==1 && editor_creation::designing());}
void result_key(editor::ResourceKey saved,bool cancel=false){
    native_result.fill(0);skg::field<void*>(native_result.data(),0)=refs_vt.data();skg::field<int>(native_result.data(),8)=1;
    skg::field<editor::ResourceKey>(native_result.data(),0x18)=saved;
    skg::field<bool>(native_result.data(),0x44)=cancel;
    notify_hook(native_editor.data(),native_result.data());
}
void result(bool cancel=false,bool unchanged=false){result_key(unchanged?source:editor::ResourceKey{0x29004321,source.type,source.group},cancel);}
std::vector<uint8_t> model(){return {0xD3,0x7E,0x65,0x48,1,2,3,4,5,6,7,8};}
bool same_records(const std::vector<dbpf::Record>& a,const std::vector<dbpf::Record>& b){
    return a.size()==b.size() && std::equal(a.begin(),a.end(),b.begin(),[](const auto& x,const auto& y){return x.key==y.key && x.data==y.data;});
}
std::vector<uint8_t> metadata(){std::vector<uint8_t> data(0x24);editor_creation_native::putbe(data.data(),13);editor_creation_native::putbe(data.data()+0xC,source.type);editor_creation_native::putbe(data.data()+0x10,source.group);editor_creation_native::putbe(data.data()+0x14,source.instance);data[0x20]=0xAA;return data;}
}
namespace game {uint32_t mode_id(){return mode;}}
namespace editor_creation_ui {void init(const SkHost*){}void tick(){}void shutdown(){}}
namespace adventure_collect {bool collect_creation(const dbpf::Key&,std::vector<dbpf::Record>& records,std::string&){++creation_collections;records=collected;return true;}}
namespace editor_native {
State state(){State result;result.generation=admission_generation;return result;}
bool edit_owned_snapshot(const Snapshot& base,const std::function<bool(void*,std::string&)>& edit,Snapshot& out,std::string& error){
    ++owned_edits;
    owned_resource.fill(0);owned_classes.clear();
    for(const auto& [id,settings]:base.document.class_settings){
        (void)id;const auto at=owned_classes.size();owned_classes.resize(at+0x27E8);
        auto* row=owned_classes.data()+at;skg::field<uint32_t>(row,0)=uint32_t(settings.class_id.id);
        skg::field<editor::ResourceKey>(row,8)=std::get<editor::ResourceKey>(settings.properties.at(editor::kClassAssetResourceProperty));
        row[0x180]=0xA5;
    }
    row_pointers();skg::field<uint32_t>(owned_resource.data(),0x2C0C)=base.document.next_class;
    if(!edit(owned_resource.data(),error))return false;
    out=base;
    for(size_t at=0;at<owned_classes.size();at+=0x27E8){
        auto* row=owned_classes.data()+at;auto index=skg::field<uint32_t>(row,0);
        editor::ObjectId cls{(2ull<<32)|index,1},setting{(5ull<<32)|index,1};
        auto& target=out.document.class_settings[setting];target.object=setting;target.class_id=cls;
        target.properties[editor::kClassAssetResourceProperty]=skg::field<editor::ResourceKey>(row,8);
        out.document.classes.insert(cls);
    }
    return true;
}
}
void test_native_model_admission(){
    initialize();
    admission_manager_vt={};admission_manager_vt[3]=reinterpret_cast<void*>(admission_cached);admission_manager_vt[7]=reinterpret_cast<void*>(admission_private);
    void* manager_vtable=admission_manager_vt.data();admission_manager=&manager_vtable;
    admission_model_vt={nullptr,reinterpret_cast<void*>(admission_release),nullptr,reinterpret_cast<void*>(admission_query)};
    const auto reset_admission=[](){
        live_scope=true;admission_generation=7;admission_callback={};admission_mask={1,2,4,8};admission_errors={};admission_rekey=false;
        for(unsigned n=0;n<2;++n){admission_loaded[n]=admission_object[n]=admission_cast[n]=true;
            admission_loads[n]=admission_releases[n]=admission_validates[n]=0;admission_models[n].fill(0);
            skg::field<void*>(admission_models[n].data(),0)=admission_model_vt.data();skg::field<uint32_t>(admission_models[n].data(),0x18)=0x7D433FAD;
        }
    };
    const editor_creation_native::ModelFence fence{owner,7,[](const editor_native::GestureScope& scope,uint64_t generation){return live_scope&&scope==owner&&generation==admission_generation;}};
    const editor::ResourceKey alias{0x7D3EB486,0x24682294,0x40646200};std::string error;
    for(unsigned reason=0;reason<6;++reason){
        reset_admission();
        if(reason==1)admission_loaded[0]=admission_object[0]=false;
        if(reason==2)admission_cast[0]=false;
        if(reason==3)admission_errors[0][3]=0x80000000;
        if(reason==4)admission_loaded[1]=admission_object[1]=false;
        if(reason==5)admission_loaded[0]=false; // Failed native load may still transfer an acquired ref.
        editor_creation_native::ModelAdmission value;CHECK(editor_creation_native::inspect_model(alias,fence,value,error));
        CHECK(value.key==alias&&value.mask==admission_mask&&value.cached.loaded==admission_loaded[0]&&value.private_copy.loaded==admission_loaded[1]);
        CHECK(editor_creation_native::model_admitted(value,error)==(reason==0||reason==4));
        CHECK(admission_releases[0]==unsigned(admission_object[0])&&admission_releases[1]==unsigned(admission_object[1]));
        CHECK(!skg::field<int>(admission_models[0].data(),8)&&!skg::field<int>(admission_models[1].data(),8));
        CHECK(editor_creation_native::model_admission_status(value).size()<384);
        CHECK(!native_class_imports&&!owned_edits&&!live_class_writes); // Read-only diagnostics never allocate a cast.
    }
    for(char point:{'L','P','Q','V','R'})for(unsigned reason=0;reason<3;++reason){
        reset_admission();editor_creation_native::ModelAdmission value;value.key={1,2,3};
        admission_callback=[point,reason](char event){if(event!=point)return;
            if(reason==0)live_scope=false;else if(reason==1)++admission_generation;else admission_mask[3]^=1;};
        CHECK(!editor_creation_native::inspect_model(alias,fence,value,error));
        CHECK(value.key==editor::ResourceKey({1,2,3})&&!error.empty());
        CHECK(admission_releases[0]==admission_loads[0]&&admission_releases[1]==admission_loads[1]);
        CHECK(!native_class_imports&&!owned_edits&&!live_class_writes);
    }
    reset_admission();admission_logs.clear();admission_loaded[0]=admission_object[0]=false;
    CHECK(!editor_creation_native::validate_model(alias,fence,error)&&error.size()<256);
    CHECK(error.find("cached=0/0/0")!=std::string::npos&&error.find("private=1/1/1")!=std::string::npos);
    CHECK(admission_logs.size()==1 && admission_logs.back().find("observed=1 accepted=0 key=40646200!7D3EB486.24682294")!=std::string::npos);
    CHECK(admission_logs.back().find("caller_mode=canonical serial=0")!=std::string::npos);
    CHECK(admission_loads[0]==1&&admission_loads[1]==1&&admission_releases[0]==0&&admission_releases[1]==1);
    reset_admission();CHECK(editor_creation_native::validate_model(alias,fence,error));
    CHECK(admission_logs.size()==2 && admission_logs.back().find("observed=1 accepted=1")!=std::string::npos);
    CHECK(admission_logs.back().ends_with(" error="));
    CHECK(admission_logs.back().find("cached=1/1/1 subtype=7D433FAD errors=00000000,00000000,00000000,00000000 private=1/1/1")!=std::string::npos);
    CHECK(admission_loads[0]==1&&admission_loads[1]==1&&admission_releases[0]==1&&admission_releases[1]==1);
    const auto first=admission_logs.front();reset_admission();admission_errors[0][3]=0x80000000;
    CHECK(!editor_creation_native::validate_model(alias,fence,error));
    CHECK(admission_logs.size()==3&&admission_logs.front()==first);
    CHECK(admission_logs.back().find("errors=00000000,00000000,00000000,80000000")!=std::string::npos);
    reset_admission();admission_rekey=true;editor_creation_native::ModelAdmission rekeyed;rekeyed.key={1,2,3};
    CHECK(!editor_creation_native::inspect_model(alias,fence,rekeyed,error)&&rekeyed.key==editor::ResourceKey({1,2,3}));
    CHECK(admission_releases[0]==1&&admission_loads[1]==0&&admission_validates[0]==0);
    for(char point:{'L','P','Q','V'}){
        reset_admission();admission_callback=[point](char event){if(event==point)throw std::runtime_error("injected native callback");};
        editor_creation_native::ModelAdmission unchanged;unchanged.key={1,2,3};bool caught=false;
        try{(void)editor_creation_native::inspect_model(alias,fence,unchanged,error);}catch(const std::runtime_error&){caught=true;}
        CHECK(caught&&unchanged.key==editor::ResourceKey({1,2,3}));
        CHECK(admission_releases[0]==admission_loads[0]&&admission_releases[1]==admission_loads[1]);
        admission_callback={};
    }
    reset_admission();
    const auto calls=admission_loads[0];auto stale=fence;++stale.scope.author.generation;
    editor_creation_native::ModelAdmission value;CHECK(!editor_creation_native::inspect_model(alias,stale,value,error)&&admission_loads[0]==calls);
    CHECK(!editor_creation_native::validate_model(alias,stale,error)&&admission_loads[0]==calls);
    CHECK(admission_logs.back().find("observed=0 accepted=0 key=40646200!7D3EB486.24682294")!=std::string::npos);
    // These module-state diagnostics perform no native model lookup.
    reset_admission();start();result();
    const auto serial=editor_creation::scope()->serial;
    CHECK(editor_creation_native::validate_model(alias,fence,error));
    CHECK(admission_logs.back().find("caller_mode=creation serial="+std::to_string(serial))!=std::string::npos);
    const auto counters=[] {return std::array<unsigned,4>{admission_loads[0],admission_loads[1],admission_releases[0],admission_releases[1]};};
    const auto calls_before_diagnostic=counters();
    const auto diagnostic=editor_creation::diagnostic();
    CHECK(diagnostic.find("phase=2")!=std::string::npos&&diagnostic.find("last_saved=40626200!29004321.2B978C46")!=std::string::npos);
    CHECK(counters()==calls_before_diagnostic);
    pump_native_messages();return_ready=true;step();CHECK(!editor_creation::active());
    CHECK(editor_creation::diagnostic().find("last_saved=40626200!29004321.2B978C46")!=std::string::npos);
    CHECK(counters()==calls_before_diagnostic);
    admission_manager=nullptr;admission_callback={};
}
void test_ga_new_routes(){
    std::string error;
    // Independent stock GA category table: a typo in the production menu must
    // not pass merely because the request and UI share that same table.
    const std::array<uint32_t,4> building_ids{0x99E92F05,0x4E3F7777,0x8707BE7D,0x72C49181};
    const std::array<uint32_t,9> vehicle_ids{0x7D433FAD,0x8F963DCB,0x441CD3E6,
        0xF670AA43,0x2A5147A9,0x1A4E0708,0x9AD7D4AA,0x1F2A25B6,0x449C040F};
    const auto buildings=editor_creation::new_variants(0x2399BE55);
    const auto vehicles=editor_creation::new_variants(0x24682294);
    CHECK(buildings.size()==building_ids.size()&&vehicles.size()==vehicle_ids.size());
    for(size_t i=0;i<building_ids.size();++i)CHECK(buildings[i].editor_id==building_ids[i]);
    for(size_t i=0;i<vehicle_ids.size();++i)CHECK(vehicles[i].editor_id==vehicle_ids[i]);
    CHECK(editor_creation::new_variants(0x12345678).empty());
    for(const auto [type,expected]:std::array<std::pair<uint32_t,uint32_t>,4>{{
        {0x2B978C46,0x465C50BA},{0x2399BE55,0x99E92F05},{0x24682294,0x7D433FAD},{0x476A98C7,0x96B24187}}}){
        initialize();CHECK(editor_creation::open(type,{},std::nullopt,error));grant=true;step();
        CHECK(submitted_request_editor==expected&&submitted_editor==expected&&submissions==1);
        result_key({0x29004321,type,0x40646200},true);return_ready=true;step();CHECK(!editor_creation::active()&&allocations.empty());
        for(const auto& variant:editor_creation::new_variants(type)){
            initialize();CHECK(editor_creation::open(type,{},std::nullopt,error,variant.editor_id));
            CHECK(editor_creation::active()&&!submissions&&allocations.size()==1);
            CHECK(skg::field<uint32_t>(*allocations.begin(),0xC)==variant.editor_id);
            editor_creation::cancel_pending();CHECK(!editor_creation::active()&&allocations.empty()&&!submissions);
            // The genuine GA browser request carries an entirely empty key
            // and a category-specific editor ID, not a generic family ID.
            initialize();skg::field<uint32_t>(request.data(),0xC)=variant.editor_id;
            skg::field<editor::ResourceKey>(request.data(),0x10)={};
            CHECK(!editor_creation::intercept(request.data(),submit)&&editor_creation::active());
            CHECK(skg::field<int>(request.data(),8)==2);unref(request.data());grant=true;step();
            CHECK(submissions==1&&submitted_editor==variant.editor_id&&submitted_source==editor::ResourceKey{});
            result_key({0x29004321,type,0x40646200},true);return_ready=true;step();
            CHECK(!editor_creation::active()&&returned_cancelled&&native_imports==0&&skg::field<int>(request.data(),8)==0);
        }
    }
    for(const auto [type,id]:std::array<std::pair<uint32_t,uint32_t>,5>{{
        {0x24682294,0x99F87089},{0x2399BE55,0xD817CD63},{0x24682294,0x465C50BA},
        {0x2B978C46,0x7D433FAD},{0x24682294,0x12345678}}}){
        initialize();CHECK(!editor_creation::open(type,{},std::nullopt,error,id));
        CHECK(!editor_creation::active()&&allocations.empty()&&!submissions&&!error.empty());
        skg::field<uint32_t>(request.data(),0xC)=id;skg::field<editor::ResourceKey>(request.data(),0x10)={0,type,0};
        CHECK(!editor_creation::intercept(request.data(),submit)&&!editor_creation::active());
        CHECK(skg::field<int>(request.data(),8)==1&&!submissions);
    }
    initialize();CHECK(!editor_creation::open(source.type,source,std::nullopt,error,0x465C50BA));
    CHECK(allocations.empty()&&!editor_creation::active()&&!submissions);
    // The variant choice belongs to the original full participant context.
    // Native allocation/constructor/Submit callbacks cannot repin it to a
    // replacement project, epoch or participant generation.
    for(char boundary:{'A','C','S'})for(unsigned change=0;change<3;++change){
        initialize();request_callback=[=](char at){if(at!=boundary)return;
            if(change==0)++owner.project[0];if(change==1)++owner.epoch;if(change==2)++owner.author.generation;};
        CHECK(!editor_creation::open(0x24682294,{},std::nullopt,error,0x441CD3E6));
        CHECK(!editor_creation::active()&&allocations.empty()&&!submissions&&!error.empty());
    }
    // Retain input values before Native callbacks can release/change a caller's
    // source holder. Existing Edit still reaches the native metadata resolver.
    initialize();auto input=source;const auto original=input;native_existing_editor=0x465C50BA;
    request_callback=[&](char at){if(at=='C')input={};};
    CHECK(editor_creation::open(original.type,input,std::nullopt,error));grant=true;step();
    CHECK(submitted_request_editor==UINT32_MAX&&submitted_source==original&&input==editor::ResourceKey{});
    result_key(original,true);return_ready=true;step();CHECK(!editor_creation::active()&&allocations.empty());
}

// Exact C32 admitted alias pollen: class1342 later became an unresolved
// server-only reference during ordinary native Adventure Save. Kept separately
// from synthetic model fixtures so the inherited online identity is real.
static std::vector<uint8_t> c32_alias_pollen(){
    static constexpr std::array<uint8_t,193> bytes{{
        0x00,0x00,0x00,0x0D,0x00,0x00,0x00,0x74,0xAD,0x64,0x45,0xF8,0x24,0x68,0x22,0x94,
        0x40,0x64,0x62,0x00,0x7D,0xAA,0x8C,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,0x0E,0xE2,0x7C,0x97,0x06,0x00,0x00,0x00,0x0E,
        0xE2,0x7C,0x97,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x74,0x77,0x8A,0x76,0x25,
        0x00,0x00,0x00,0x0B,0x70,0x00,0x65,0x00,0x6C,0x00,0x69,0x00,0x63,0x00,0x61,0x00,
        0x6E,0x00,0x74,0x00,0x68,0x00,0x6F,0x00,0x72,0x00,0x00,0x00,0x00,0x16,0x50,0x00,
        0x72,0x00,0x65,0x00,0x76,0x00,0x69,0x00,0x65,0x00,0x77,0x00,0x39,0x00,0x56,0x00,
        0x65,0x00,0x68,0x00,0x69,0x00,0x63,0x00,0x6C,0x00,0x65,0x00,0x43,0x00,0x33,0x00,
        0x32,0x00,0x48,0x00,0x6F,0x00,0x73,0x00,0x74,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
        0x00,0x01,0x00,0x00,0x00,0x0B,0x70,0x65,0x6C,0x69,0x63,0x61,0x6E,0x74,0x68,0x6F,
        0x72,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0xFF,0xFF,0xFF,0x00,0x00,0x00,
        0x00,
    }};
    return {bytes.begin(),bytes.end()};
}
static void test_immutable_alias_has_its_own_local_metadata_identity(){
    editor::ProjectId project{};project[0]=0x71;std::string error;
    const editor::ResourceKey actual{0x7DAA8C24,0x24682294,0x40646200};
    const auto retained=c32_alias_pollen();
    CHECK(editor_creation_native::be32(retained.data())==13);
    CHECK((std::array<uint8_t,8>{0,0,0,0x74,0xAD,0x64,0x45,0xF8}==
        std::array<uint8_t,8>{retained[4],retained[5],retained[6],retained[7],retained[8],retained[9],retained[10],retained[11]}));
    editor::ResourceKey version;std::vector<dbpf::Record> remapped;
    std::vector<dbpf::Record> records{{{actual.instance,0x1A99B06B,actual.group},model()},
        {{actual.instance,0x030BDEE3,actual.group},retained}};
    const auto original=records;
    CHECK(editor_creation_native::immutable_version(project,actual,records,{},version,remapped,error));
    const auto pollen=std::find_if(remapped.begin(),remapped.end(),[](const auto& row){return row.key.type==0x030BDEE3;});
    CHECK(pollen!=remapped.end()&&pollen->data.size()==retained.size());
    CHECK(std::all_of(pollen->data.begin()+4,pollen->data.begin()+12,[](uint8_t value){return value==0xFF;}));
    auto expected=retained;std::fill(expected.begin()+4,expected.begin()+12,0xFF);
    editor_creation_native::putbe(expected.data()+0x14,version.instance);
    CHECK(pollen->data==expected&&same_records(records,original)&&version!=actual);
    // A previously admitted alias is immutable. Reserving it forces a fresh
    // key; the retained old bytes and the source are never overwritten.
    const auto old_version=version;const auto old_records=remapped;
    CHECK(editor_creation_native::immutable_version(project,actual,records,[&](const auto& key){return key.instance==old_version.instance;},version,remapped,error));
    CHECK(version!=old_version&&same_records(records,original));
    const auto retained_old=std::find_if(old_records.begin(),old_records.end(),[](const auto& row){return row.key.type==0x030BDEE3;});
    CHECK(retained_old!=old_records.end()&&retained_old->key.instance==old_version.instance&&retained_old->data==expected);
    // The metadata format is common to every reviewed New variant. Only the
    // owner online ID and owner self instance change; all parent/foreign bytes
    // and both sentinels retain their exact representation.
    for(const auto family:std::array<editor::ResourceKey,4>{{
        {actual.instance,0x2B978C46,0x40626200},{actual.instance,0x2399BE55,0x40636200},
        actual,{actual.instance,0x476A98C7,0x40656200}}}){
        for(const auto& variant:editor_creation::new_variants(family.type))for(bool sentinel:{false,true}){
            CHECK(variant.editor_id!=0);auto metadata=retained;
            editor_creation_native::putbe(metadata.data()+0xC,family.type);
            editor_creation_native::putbe(metadata.data()+0x10,family.group);
            if(sentinel)std::fill(metadata.begin()+4,metadata.begin()+12,0xFF);
            // Parent key and a later foreign online identity are preserved.
            for(size_t i=0;i<12;++i)metadata[0x18+i]=uint8_t(0x80+i);
            for(size_t i=0;i<8;++i)metadata[0x24+i]=uint8_t(0x40+i);
            auto foreign=metadata;foreign[4]^=0x5A;
            records={{{family.instance,0x1A99B06B,family.group},model()},
                {{family.instance,0x030BDEE3,family.group},metadata},
                {{family.instance+1,0x030BDEE3,family.group},foreign},
                {{family.instance,0x030BDEE3,family.group+0x10000},foreign}};
            const auto before=records;
            CHECK(editor_creation_native::immutable_version(project,family,records,{},version,remapped,error));
            CHECK(same_records(records,before)&&remapped.size()==records.size());
            auto exact=metadata;std::fill(exact.begin()+4,exact.begin()+12,0xFF);editor_creation_native::putbe(exact.data()+0x14,version.instance);
            unsigned owners=0,foreign_records=0;
            for(const auto& row:remapped){
                if(row.key.type==0x030BDEE3&&row.key.instance==version.instance){CHECK(row.data==exact);++owners;}
                else if(row.key.type==0x030BDEE3){CHECK(row.data==foreign);++foreign_records;}
                else CHECK(row.data==model());
            }
            CHECK(owners==1&&foreign_records==2);
        }
    }
    // Existing strict format/identity/duplicate gates still reject atomically.
    records=original;const auto unchanged_version=version;const auto unchanged_records=remapped;
    auto reject=[&](const std::vector<dbpf::Record>& bad){const auto input=bad;error.clear();
        CHECK(!editor_creation_native::immutable_version(project,actual,bad,{},version,remapped,error));
        CHECK(!error.empty()&&version==unchanged_version&&same_records(remapped,unchanged_records)&&same_records(bad,input));};
    for(size_t size=0;size<0x18;++size){auto bad=records;bad[1].data.resize(size);reject(bad);}
    for(uint32_t format:{0u,12u,14u,UINT32_MAX}){auto bad=records;editor_creation_native::putbe(bad[1].data.data(),format);reject(bad);}
    for(size_t field:{size_t(0xC),size_t(0x10),size_t(0x14)}){auto bad=records;bad[1].data[field]^=1;reject(bad);}
    auto duplicate=records;duplicate.push_back(records[1]);duplicate.back().data[4]^=1;reject(duplicate);
    duplicate=records;duplicate.push_back(records[1]);reject(duplicate);
    CHECK(!editor_creation_native::immutable_version(project,actual,records,[](const auto&){return true;},version,remapped,error));
    CHECK(version==unchanged_version&&same_records(remapped,unchanged_records));
}
int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--alias-identity-only"){test_immutable_alias_has_its_own_local_metadata_identity();std::printf("immutable alias metadata: %u checks passed\n",checks);return 0;}
    if(argc!=1)return 2;
    test_immutable_alias_has_its_own_local_metadata_identity();
    test_ga_new_routes();
    test_native_model_admission();
    initialize();
    CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());step();step();
    CHECK(submissions==0 && editor_creation::active() && !editor_creation::designing());
    CHECK(skg::field<int>(request.data(),8)==1);
    grant=true;step();CHECK(submissions==1 && editor_creation::designing());step();CHECK(submissions==1);
    CHECK(!editor_creation::intercept(request.data(),submit));CHECK(submissions==1);
    result();CHECK(native_returns==1 && native_imports==0);
    CHECK(skg::field<bool>(native_result.data(),0x44) && skg::field<int>(native_result.data(),8)==1);
    CHECK(native_messages.size()==2 && native_result_deliveries==0 && mode==modes::kEditor && returned==0);
    CHECK(editor_creation::last_saved()->saved.instance==0x29004321);
    CHECK(!editor_creation::last_saved()->cancelled);
    pump_native_message();CHECK(native_imports==0 && native_result_deliveries==1 && skg::field<int>(native_result.data(),8)==0);
    editor_creation::tick();pump();CHECK(returned==0 && editor_creation::active());
    CHECK(mode==modes::kEditor && native_mode_returns==0);
    step();CHECK(editor_creation::active() && returned==1);
    CHECK(native_mode_returns==1 && !returned_cancelled && returned_saved.instance==0x29004321);
    return_ready=true;step();CHECK(!editor_creation::active() && finished==1 && skg::field<int>(request.data(),8)==0);
    initialize();start();result(true);return_ready=true;step();CHECK(!editor_creation::active() && native_imports==0 && returned_cancelled);
    // A family may save changed bytes under its existing native key. Identity
    // equality cannot prove an unchanged Done; the coordinator compares content.
    initialize();start();result(false,true);return_ready=true;step();CHECK(!editor_creation::active() && !returned_cancelled && native_imports==0);
    initialize();CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());live_scope=false;step();CHECK(!editor_creation::active() && submissions==0);
    initialize();CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());reject=true;step();CHECK(!editor_creation::active() && submissions==0);
    initialize();CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());editor_creation::cancel_pending();CHECK(!editor_creation::active() && submissions==0);
    initialize();start();lease=false;step();result();return_ready=true;step();CHECK(!editor_creation::active() && returned==0 && editor_creation::last_saved().has_value());
    initialize();start();live_scope=false;result();step();CHECK(!editor_creation::active() && returned==0);
    initialize();CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());now=32;step();CHECK(!editor_creation::active());
    initialize();submit_ok=false;grant=true;CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());step();CHECK(!editor_creation::active() && skg::field<int>(request.data(),8)==0);
    // A posted result can outlive the module's session or scene. Its own native
    // reference remains valid and cancelled; the real saved result is owned.
    initialize();start();result();editor_creation::shutdown();
    CHECK(skg::field<int>(request.data(),8)==0 && skg::field<int>(native_result.data(),8)==1);
    CHECK(native_result_deliveries==0 && native_messages.size()==2);
    CHECK(editor_creation::last_saved()->saved.instance==0x29004321 && !editor_creation::last_saved()->cancelled);
    pump_native_messages();CHECK(native_imports==0 && native_result_deliveries==1 && native_mode_returns==1);
    CHECK(skg::field<int>(native_result.data(),8)==0);
    initialize();
    // Unrelated native results are delivered with their real cancellation bit.
    skg::field<void*>(native_result.data(),0)=refs_vt.data();skg::field<int>(native_result.data(),8)=1;
    notify_hook(native_editor.data(),native_result.data());CHECK(native_imports==0 && !skg::field<bool>(native_result.data(),0x44));
    pump_native_messages();CHECK(native_imports==1 && skg::field<int>(native_result.data(),8)==0);
    // An active owned session must not capture or cancel another request's
    // result. Its queued listeners retain the native import behavior.
    initialize();start();
    std::array<uint8_t,0x1D0> other_editor{};std::array<uint8_t,0x9C> other_request{};
    std::array<uint8_t,0x48> other_result{};
    skg::field<void*>(other_request.data(),0)=refs_vt.data();skg::field<int>(other_request.data(),8)=1;
    skg::field<void*>(other_editor.data(),0x1CC)=other_request.data();
    skg::field<void*>(other_result.data(),0)=refs_vt.data();skg::field<int>(other_result.data(),8)=1;
    notify_hook(other_editor.data(),other_result.data());
    CHECK(!skg::field<bool>(other_result.data(),0x44) && editor_creation::active());
    CHECK(skg::field<void*>(native_editor.data(),0x1CC)==request.data());
    CHECK(skg::field<int>(other_request.data(),8)==0 && skg::field<int>(other_result.data(),8)==1);
    pump_native_messages();CHECK(native_imports==1 && skg::field<int>(other_result.data(),8)==0);
    result();return_ready=true;step();CHECK(!editor_creation::active() && native_imports==1 && !returned_cancelled);
    editor_native::Snapshot base,out;editor::ObjectId cls{(2ull<<32)|7,1},settings{(5ull<<32)|7,1};
    base.document.classes.insert(cls);base.document.next_class=8;
    base.document.class_settings[settings]={settings,cls,{},{{editor::kClassAssetResourceProperty,source}}};
    editor::ResourceKey saved{0x7D123456,source.type,source.group};std::string error;
    CHECK(editor_creation_native::prepare_cast_save(base,saved,cls,out,error));
    CHECK(owned_classes[0x180]==0xA5 && live_class_writes==0);
    CHECK(std::get<editor::ResourceKey>(out.document.class_settings.at(settings).properties.at(editor::kClassAssetResourceProperty))==saved);
    CHECK(editor_creation_native::prepare_cast_save(base,saved,std::nullopt,out,error));
    CHECK(out.document.classes.size()==2 && live_class_writes==0 && owned_classes.size()==2*0x27E8);
    CHECK(skg::field<uint32_t>(owned_classes.data()+0x27E8+8,0x18)==7);
    editor::ProjectId project{};project[0]=7;editor::ResourceKey version,second;
    std::vector<dbpf::Record> records{{{source.instance,0x1A99B06B,source.group},model()},{{source.instance,0x030BDEE3,source.group},metadata()},{{99,0x1A99B06B,source.group},model()}};
    std::vector<dbpf::Record> remapped,again;
    CHECK(editor_creation_native::immutable_version(project,source,records,{},version,remapped,error));
    CHECK(version.instance!=source.instance && remapped.size()==3);
    for(const auto& row:remapped){
        if(row.key.instance==99)CHECK(row.data==model());
        else {CHECK(row.key.instance==version.instance);if(row.key.type==0x030BDEE3){CHECK(editor_creation_native::be32(row.data.data()+0x14)==version.instance);CHECK(row.data[0x20]==0xAA);}else CHECK(row.data==model());}
    }
    CHECK(editor_creation_native::immutable_version(project,source,records,{},second,again,error) && version==second);
    // Preserve a usable image under the exact key native Crew requests,
    // without retaining its old importable metadata. Both owner variants are
    // sanitized; a +1-only source gains the model-group presentation record.
    std::vector<uint8_t> pixels(8*8*4,255),plain_png;
    for(size_t i=0;i<pixels.size();i+=4){pixels[i]=uint8_t(i);pixels[i+1]=105;pixels[i+2]=63;}
    CHECK(editor_creation_png::from_rgba(8,8,pixels,plain_png,error));
    auto carrier_png=plain_png;carrier_png.resize(carrier_png.size()-12);
    const std::array<uint8_t,24> old_name{'S','o','u','r','c','e',0,'o','l','d','-','o','w','n','e','r','-','m','e','t','a','d','a','t'};
    editor_creation_png::chunk(carrier_png,"tEXt",old_name);editor_creation_png::chunk(carrier_png,"IEND",{});
    carrier_png.insert(carrier_png.end(),{0xD3,0x7E,0x65,0x48}); // old trailing carrier is discarded too
    auto imaged=records;imaged.push_back({{source.instance,0x2F7D0004,source.group},carrier_png});
    imaged.push_back({{source.instance,0x2F7D0004,source.group+1},carrier_png});
    imaged.push_back({{99,0x2F7D0004,source.group},{9,8,7}});
    const auto original_imaged=imaged;editor::ResourceKey image_version;std::vector<dbpf::Record> image_records;
    CHECK(editor_creation_native::immutable_version(project,source,imaged,{},image_version,image_records,error));
    unsigned images=0;
    for(const auto& row:image_records){
        if(row.key.type==0x2F7D0004 && row.key.instance==image_version.instance){++images;CHECK(row.data==plain_png);}
        if(row.key.type==0x2F7D0004 && row.key.instance==99)CHECK(row.data==std::vector<uint8_t>({9,8,7}));
        if(row.key.type==0x1A99B06B && row.key.instance==image_version.instance)CHECK(row.data==model());
    }
    CHECK(images==2 && same_records(imaged,original_imaged));
    auto secondary_only=records;secondary_only.push_back({{source.instance,0x2F7D0004,source.group+1},carrier_png});
    CHECK(editor_creation_native::immutable_version(project,source,secondary_only,{},image_version,image_records,error));
    CHECK(std::count_if(image_records.begin(),image_records.end(),[&](const auto& row){return row.key.instance==image_version.instance && row.key.type==0x2F7D0004 && row.data==plain_png;})==2);
    const auto previous_image_version=image_version;const auto previous_image_records=image_records;
    secondary_only.back().data[0]=0;
    CHECK(!editor_creation_native::immutable_version(project,source,secondary_only,{},image_version,image_records,error));
    CHECK(image_version==previous_image_version && same_records(image_records,previous_image_records));
    // WIC itself accepts a bad Adler checksum. Repair PNG CRC after corrupting
    // Adler so only strict compressed-stream validation can reject this input.
    auto broken_png=plain_png;const auto idat_size=editor_creation_native::be32(broken_png.data()+33);
    broken_png[40+idat_size]^=1;
    editor_creation_native::putbe(broken_png.data()+41+idat_size,editor_creation_png::crc(std::span<const uint8_t>(broken_png).subspan(37,idat_size+4)));
    secondary_only.back().data=std::move(broken_png);
    CHECK(!editor_creation_native::immutable_version(project,source,secondary_only,{},image_version,image_records,error));
    CHECK(image_version==previous_image_version && same_records(image_records,previous_image_records));
    // Private profiles may reuse the same local source key for different
    // authoritative BEM bodies (actual Crew failure29058782). Equal metadata
    // and cache bytes must not collapse those creations to the same version.
    const auto first_version=version;const auto first_records=remapped;
    auto other_body=records;other_body[0].data.back()^=0x5A;other_body[0].data.push_back(0x42);
    const auto original_body=other_body[0].data;
    CHECK(editor_creation_native::immutable_version(project,source,other_body,{},second,again,error));
    CHECK(second!=first_version && second.instance!=source.instance && same_records(remapped,first_records));
    auto other_model=std::find_if(again.begin(),again.end(),[](const auto& row){return row.key.type==0x1A99B06B&&row.key.instance!=99;});
    CHECK(other_model!=again.end()&&other_model->key.instance==second.instance&&other_model->data==original_body);
    CHECK(records[0].data==model()&&other_body[0].data==original_body);
    // Missing, invalid and unavailable authoritative models reject atomically;
    // a summary or valid dependency BEM cannot stand in for the selected body.
    const auto previous_records=again;const auto previous_version=second;
    auto no_body=records;no_body.erase(no_body.begin());
    CHECK(!editor_creation_native::immutable_version(project,source,no_body,{},second,again,error));
    CHECK(second==previous_version&&same_records(again,previous_records));
    auto bad_body=records;bad_body[0].data[0]=0;
    CHECK(!editor_creation_native::immutable_version(project,source,bad_body,{},second,again,error));
    CHECK(second==previous_version&&same_records(again,previous_records));
    collected.clear();
    CHECK(!editor_creation_native::collect_version(project,source,{},second,again,error));
    CHECK(second==previous_version&&same_records(again,previous_records));
    CHECK(editor_creation_native::immutable_version(project,source,records,[&](const auto& key){return key.instance==version.instance;},second,again,error) && second!=version);
    CHECK(editor_creation_native::immutable_version(project,source,records,[&](const auto& key){return key.instance==version.instance && key.type==source.type;},second,again,error) && second!=version);
    CHECK(!editor_creation_native::immutable_version(project,source,records,[](const auto&){return true;},second,again,error));
    records[1].data[0]=99;CHECK(!editor_creation_native::immutable_version(project,source,records,{},second,again,error));
    // Only typed owner ResourceKeys change; ordinary numeric words and external
    // dependency references which happen to share an instance stay untouched.
    records[1].data=metadata();
    std::vector<uint8_t> properties(4+8+16+8+4);auto* p=properties.data();
    editor_creation_native::putbe(p,2);editor_creation_native::putbe(p+4,0x1234);p[8]=0;p[9]=0x20;
    editor_creation_native::putle(p+12,source.instance);editor_creation_native::putle(p+16,source.type);editor_creation_native::putle(p+20,source.group);
    editor_creation_native::putbe(p+28,0x1235);p[32]=0;p[33]=9;editor_creation_native::putle(p+36,source.instance);
    records.push_back({{source.instance,0x00B1B104,(source.group&0xFFFF0000u)|0xE500u},properties});
    CHECK(editor_creation_native::immutable_version(project,source,records,{},version,remapped,error));
    auto prop=std::find_if(remapped.begin(),remapped.end(),[](const auto& row){return row.key.type==0x00B1B104;});CHECK(prop!=remapped.end());
    CHECK(editor_creation_native::le32(prop->data.data()+12)==version.instance);
    CHECK(editor_creation_native::le32(prop->data.data()+36)==source.instance);
    records.back().data.pop_back();CHECK(!editor_creation_native::immutable_version(project,source,records,{},version,remapped,error));
    // Every Adventure-compatible family uses the verified request constructor
    // and native default for New, with failed allocation leaving no request.
    for(auto type:{0x2B978C46u,0x2399BE55u,0x24682294u,0x476A98C7u}){
        initialize();allocation_failure=true;CHECK(!editor_creation::open(type,{},std::nullopt,error));CHECK(!editor_creation::active() && allocations.empty());
        allocation_failure=false;CHECK(editor_creation::open(type,{},std::nullopt,error));CHECK(editor_creation::active() && allocations.size()==1);
        grant=true;step();CHECK(submissions==1 && mode==modes::kEditor);
        CHECK(submitted_request_editor==editor_creation::default_editor(type));
        native_result.fill(0);skg::field<void*>(native_result.data(),0)=refs_vt.data();skg::field<int>(native_result.data(),8)=1;
        skg::field<editor::ResourceKey>(native_result.data(),0x18)={0x29004321,type,source.group};
        notify_hook(native_editor.data(),native_result.data());return_ready=true;step();CHECK(!editor_creation::active() && allocations.empty() && native_imports==0);
        auto family_source=source;family_source.type=type;
        std::vector<dbpf::Record> family_records{{{source.instance,0x1A99B06B,source.group},model()}};
        CHECK(editor_creation_native::immutable_version(project,family_source,family_records,{},version,remapped,error));
        CHECK(version.type==type && remapped[0].data==model());
    }
    // Existing air/water and specialized content retains the ctor sentinel
    // until ownership arrives. The native resolver then chooses its metadata
    // variant, and a real logical Save is still admitted under the same family.
    for(auto variant:{0x8F963DCBu,0x441CD3E6u,0x2A5147A9u,0x1A4E0708u,
                     0x1F2A25B6u,0x449C040Fu,0xC15695DAu,0x2090A11Bu}){
        initialize();native_existing_editor=variant;
        const editor::ResourceKey vehicle{source.instance,0x24682294,0x40646200};
        CHECK(editor_creation::open(vehicle.type,vehicle,cls,error));
        CHECK(editor_creation::active() && !editor_creation::designing() && submissions==0);
        CHECK(skg::field<uint32_t>(*allocations.begin(),0xC)==UINT32_MAX);
        CHECK(skg::field<editor::ResourceKey>(*allocations.begin(),0x10)==vehicle);
        step();CHECK(submissions==0);grant=true;step();
        CHECK(submitted_request_editor==UINT32_MAX && submitted_editor==variant && submitted_source==vehicle);
        auto changed=vehicle;changed.instance=0x29004321;
        result_key(changed);return_ready=true;step();
        CHECK(!editor_creation::active() && !returned_cancelled && returned_saved==changed && native_imports==0);
    }
    // Native picker requests already contain the correct specialized editor.
    // Keep that exact choice even if the metadata resolver would choose another.
    initialize();native_existing_editor=0x441CD3E6;
    skg::field<uint32_t>(request.data(),0xC)=0x8F963DCB;
    const editor::ResourceKey picker_vehicle{source.instance,0x24682294,0x40646200};
    skg::field<editor::ResourceKey>(request.data(),0x10)=picker_vehicle;
    start();CHECK(submitted_request_editor==0x8F963DCB && submitted_editor==0x8F963DCB && submitted_source==picker_vehicle);
    result_key(picker_vehicle);return_ready=true;step();CHECK(!returned_cancelled && returned_saved==picker_vehicle);
    // Plant/Cell have native editors but EEDD40 uses a fixed placeholder for
    // those direct source types. Refuse them before entry, detached import,
    // dependency collection or immutable remapping, leaving existing drafts and
    // output values untouched. Legacy document reading is outside this gate.
    for(auto type:{0x438F6347u,0x3D97A8E4u}){
        initialize();
        CHECK(!editor_creation::open(type,{},std::nullopt,error));
        CHECK(!editor_creation::active() && allocations.empty() && submissions==0 && returned==0 && finished==0);
        const editor::ResourceKey unsupported{source.instance,type,source.group};
        skg::field<editor::ResourceKey>(request.data(),0x10)=unsupported;
        CHECK(!editor_creation::intercept(request.data(),submit));
        CHECK(!editor_creation::active() && skg::field<int>(request.data(),8)==1 && submissions==0);
        const auto previous_out=out.document.class_settings;
        CHECK(!editor_creation_native::prepare_cast_save(base,unsupported,std::nullopt,out,error));
        CHECK(owned_edits==0 && native_class_imports==0 && out.document.class_settings==previous_out);
        const auto previous_version=version;const auto previous_records=remapped;
        CHECK(!editor_creation_native::immutable_version(project,unsupported,records,{},version,remapped,error));
        CHECK(version==previous_version && same_records(remapped,previous_records));
        CHECK(!editor_creation_native::collect_version(project,unsupported,{},version,remapped,error));
        CHECK(creation_collections==0 && version==previous_version && same_records(remapped,previous_records));
    }
    // A mismatched unsupported result cannot become a shared saved creation.
    initialize();start();result_key({0x29004321,0x438F6347,source.group});return_ready=true;step();
    CHECK(returned_cancelled && !editor_creation::active() && native_imports==0 && owned_edits==0);
    editor_creation::shutdown();CHECK(!editor_creation::available());
    std::printf("editor creation Windows checks: %u passed\n",checks);
}
