// Production creation lifecycle and detached cast helper, actual i686 ABI.
// Controlled native objects prove ownership/routing, not running-game support.
#include "../mods/coop/editor_creation.cpp"
#include "../mods/coop/editor_creation_native.cpp"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <set>

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
editor_creation::Notify notify_hook=nullptr;
editor_creation_native::Assign assign_hook=nullptr;
uint32_t mode=modes::kScenario;
double now=1;
unsigned submissions=0,native_returns=0,native_imports=0,finished=0,returned=0,live_class_writes=0;
bool grant=false,live_scope=true,lease=true,return_ready=false,submit_ok=true,reject=false;
bool allocation_failure=false;
bool returned_cancelled=false;
std::set<void*> allocations;
editor_native::GestureScope owner;
std::vector<dbpf::Record> collected;
editor::ResourceKey source{0x29001234,0x2B978C46,0x40626200};
int SK_THISCALL ref(void* p){return ++skg::field<int>(p,8);}
int SK_THISCALL unref(void* p){auto n=--skg::field<int>(p,8);CHECK(n>=0);if(!n && allocations.erase(p))std::free(p);return n;}
void SK_THISCALL native_notify(void* e,void* r){
    ++native_returns;if(!skg::field<bool>(r,0x44))++native_imports;
    auto* req=skg::field<void*>(e,0x1CC);skg::field<void*>(e,0x1CC)=nullptr;
    if(req)unref(req);unref(r);mode=modes::kScenario;
}
void SK_THISCALL native_assign(void*,uint32_t,const editor::ResourceKey*){++live_class_writes;}
void row_pointers(){skg::field<void*>(owned_resource.data(),0x2BF4)=owned_classes.data();skg::field<void*>(owned_resource.data(),0x2BF8)=owned_classes.data()+owned_classes.size();}
uint8_t* SK_THISCALL get_class(void* owner,uint32_t index){
    CHECK(skg::field<void*>(owner,0x10)==owned_resource.data());
    for(size_t at=0;at<owned_classes.size();at+=0x27E8)if(skg::field<uint32_t>(owned_classes.data()+at,0)==index)return owned_classes.data()+at+8;
    return nullptr;
}
uint32_t SK_CDECL model_type(const editor::ResourceKey* key){CHECK(key && key->instance);return 7;}
uint32_t SK_THISCALL native_import(void* owner,const editor::ResourceKey* key){
    CHECK(skg::field<void*>(owner,0x10)==owned_resource.data());
    auto index=skg::field<uint32_t>(owned_resource.data(),0x2C0C)++;
    const auto at=owned_classes.size();owned_classes.resize(at+0x27E8);row_pointers();
    skg::field<uint32_t>(owned_classes.data()+at,0)=index;
    assign_hook(owner,index,key);return index;
}
bool SK_CDECL submit(void* req){++submissions;if(!submit_ok)return false;ref(req);skg::field<void*>(native_editor.data(),0x1CC)=req;mode=modes::kEditor;return true;}
bool SK_CDECL submit_entry(void* req){return editor_creation::intercept(req,submit);}
void* SK_CDECL allocate(size_t size,const char*,int,int,int,int){if(allocation_failure)return nullptr;auto* p=std::calloc(1,size);if(p)allocations.insert(p);return p;}
void* SK_THISCALL ctor(void* p){skg::field<void*>(p,0)=refs_vt.data();return p;}
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
void step(){editor_creation::tick();pump();}
void initialize(){
    editor_creation::shutdown();tasks.clear();
    CHECK(allocations.empty());allocation_failure=false;
    request.fill(0);native_editor.fill(0);native_result.fill(0);
    refs_vt={nullptr,reinterpret_cast<void*>(ref),reinterpret_cast<void*>(unref)};
    skg::field<void*>(request.data(),0)=refs_vt.data();skg::field<int>(request.data(),8)=1;
    skg::field<uint32_t>(request.data(),0xC)=0x465C50BA;skg::field<editor::ResourceKey>(request.data(),0x10)=source;
    grant=false;live_scope=lease=true;return_ready=reject=returned_cancelled=false;submit_ok=true;mode=modes::kScenario;now=1;
    submissions=native_returns=native_imports=finished=returned=live_class_writes=0;
    owner={};owner.project[0]=9;owner.epoch=12;owner.author={1,3};
    static SkHost host{};host.sym=sym;host.hook_create=hook;host.time_seconds=[](){return now;};host.run_outside_frame=queue;
    host.exe_sha256=[](){return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";};
    editor_creation::Ctx context;
    context.scope=[](){return std::optional<editor_native::GestureScope>{owner};};
    context.current=[](const auto& scope){return live_scope && scope.project==owner.project && scope.epoch==owner.epoch && scope.author==owner.author;};
    context.prepare=[](const auto&,auto& input,std::string& error){input.target=editor::ObjectId{(2ull<<32)|7,1};if(reject){error="denied";return editor_creation::PrepareResult::Reject;}return grant?editor_creation::PrepareResult::Ready:editor_creation::PrepareResult::Wait;};
    context.lease_valid=[](const auto&,auto){return lease;};
    context.returned=[](const auto&,const auto& result,std::string&){++returned;returned_cancelled=result.cancelled;return return_ready;};
    context.finished=[](const auto&,bool,const std::string&){++finished;};
    CHECK(editor_creation::init(&host,nullptr,std::move(context)));
}
void start(){CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());grant=true;step();CHECK(submissions==1 && editor_creation::designing());}
void result(bool cancel=false,bool unchanged=false){
    native_result.fill(0);skg::field<void*>(native_result.data(),0)=refs_vt.data();skg::field<int>(native_result.data(),8)=1;
    skg::field<editor::ResourceKey>(native_result.data(),0x18)=unchanged?source:editor::ResourceKey{0x29004321,source.type,source.group};
    skg::field<bool>(native_result.data(),0x44)=cancel;
    notify_hook(native_editor.data(),native_result.data());
}
std::vector<uint8_t> model(){return {0xD3,0x7E,0x65,0x48,1,2,3,4,5,6,7,8};}
std::vector<uint8_t> metadata(){std::vector<uint8_t> data(0x24);editor_creation_native::putbe(data.data(),13);editor_creation_native::putbe(data.data()+0xC,source.type);editor_creation_native::putbe(data.data()+0x10,source.group);editor_creation_native::putbe(data.data()+0x14,source.instance);data[0x20]=0xAA;return data;}
}
namespace game {uint32_t mode_id(){return mode;}}
namespace editor_creation_ui {void init(const SkHost*){}void tick(){}void shutdown(){}}
namespace adventure_collect {bool collect_creation(const dbpf::Key&,std::vector<dbpf::Record>& records,std::string&){records=collected;return true;}}
namespace editor_native {
bool edit_owned_snapshot(const Snapshot& base,const std::function<bool(void*,std::string&)>& edit,Snapshot& out,std::string& error){
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
int main(){
    initialize();
    CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());step();step();
    CHECK(submissions==0 && editor_creation::active() && !editor_creation::designing());
    CHECK(skg::field<int>(request.data(),8)==1);
    grant=true;step();CHECK(submissions==1 && editor_creation::designing());step();CHECK(submissions==1);
    CHECK(!editor_creation::intercept(request.data(),submit));CHECK(submissions==1);
    result();CHECK(native_returns==1 && native_imports==0);
    CHECK(!skg::field<bool>(native_result.data(),0x44) && skg::field<int>(native_result.data(),8)==0);
    CHECK(editor_creation::last_saved()->saved.instance==0x29004321);
    step();CHECK(editor_creation::active() && returned==1);
    return_ready=true;step();CHECK(!editor_creation::active() && finished==1 && skg::field<int>(request.data(),8)==0);
    initialize();start();result(true);return_ready=true;step();CHECK(!editor_creation::active() && native_imports==0);
    initialize();start();result(false,true);return_ready=true;step();CHECK(!editor_creation::active() && returned_cancelled && native_imports==0);
    initialize();CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());live_scope=false;step();CHECK(!editor_creation::active() && submissions==0);
    initialize();CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());reject=true;step();CHECK(!editor_creation::active() && submissions==0);
    initialize();CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());editor_creation::cancel_pending();CHECK(!editor_creation::active() && submissions==0);
    initialize();start();lease=false;step();result();return_ready=true;step();CHECK(!editor_creation::active() && returned==0 && editor_creation::last_saved().has_value());
    initialize();start();live_scope=false;result();step();CHECK(!editor_creation::active() && returned==0);
    initialize();CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());now=32;step();CHECK(!editor_creation::active());
    initialize();submit_ok=false;grant=true;CHECK(!editor_creation::intercept(request.data(),submit));unref(request.data());step();CHECK(!editor_creation::active() && skg::field<int>(request.data(),8)==0);
    initialize();
    // Unrelated native results are delivered with their real cancellation bit.
    skg::field<void*>(native_result.data(),0)=refs_vt.data();skg::field<int>(native_result.data(),8)=1;
    notify_hook(native_editor.data(),native_result.data());CHECK(native_imports==1);
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
    // Every editor family uses the verified request constructor and its native
    // default editor, with failed allocation leaving no pending request.
    for(auto type:{0x2B978C46u,0x2399BE55u,0x24682294u,0x476A98C7u,0x438F6347u,0x3D97A8E4u}){
        initialize();allocation_failure=true;CHECK(!editor_creation::open(type,{},std::nullopt,error));CHECK(!editor_creation::active() && allocations.empty());
        allocation_failure=false;CHECK(editor_creation::open(type,{},std::nullopt,error));CHECK(editor_creation::active() && allocations.size()==1);
        grant=true;step();CHECK(submissions==1 && mode==modes::kEditor);
        native_result.fill(0);skg::field<void*>(native_result.data(),0)=refs_vt.data();skg::field<int>(native_result.data(),8)=1;
        skg::field<editor::ResourceKey>(native_result.data(),0x18)={0x29004321,type,source.group};
        notify_hook(native_editor.data(),native_result.data());return_ready=true;step();CHECK(!editor_creation::active() && allocations.empty() && native_imports==0);
        auto family_source=source;family_source.type=type;
        std::vector<dbpf::Record> family_records{{{source.instance,0x1A99B06B,source.group},model()}};
        CHECK(editor_creation_native::immutable_version(project,family_source,family_records,{},version,remapped,error));
        CHECK(version.type==type && remapped[0].data==model());
    }
    editor_creation::shutdown();CHECK(!editor_creation::available());
    std::printf("editor creation Windows checks: %u passed\n",checks);
}
