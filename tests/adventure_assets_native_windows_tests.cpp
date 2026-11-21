// Real content cache, sealed transfer and i686 registration ABI. No game needed.
#include "../mods/coop/adventure_assets.cpp"
#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <thread>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
namespace aa=adventure_assets;
unsigned checks;
#define CHECK(...) do {++checks;if(!(__VA_ARGS__)){std::printf("FAIL %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(1);}} while(0)
const dbpf::Key root{7,0x366A930D,0x408A0000};
const dbpf::Key dependency{8,0x00B1B104,0x40626200};
std::array<net::Session,2> sessions;
std::deque<std::pair<SkTaskFn,void*>> tasks;
std::array<void*,24> rm_vt{},db_vt{};
std::array<void*,2> ref_vt{};
struct Object{void** vt;};Object manager{rm_vt.data()};void* manager_pointer=&manager;
std::vector<std::unique_ptr<std::array<uint8_t,0x388>>> databases;
std::map<void*,std::wstring> locations;
std::map<void*,int> references;
unsigned allocations,constructors,mounts,unmounts,finds,prepared,failed;
bool fail_allocate,fail_initialize,fail_open,fail_add,fail_remove,throw_add;
void* unknown_resident;
struct Sub {SkEventFn fn=nullptr;void* user=nullptr;};
std::array<Sub,SK_EV_COUNT> subscriptions{};
bool booted;
int subscribe(SkMod*,int event,SkEventFn fn,void* user){CHECK(event>=0&&event<SK_EV_COUNT&&!subscriptions[event].fn);subscriptions[event]={fn,user};return 0;}
void unsubscribe(SkMod*,int event,SkEventFn fn,void* user){CHECK(subscriptions[event].fn==fn&&subscriptions[event].user==user);subscriptions[event]={};}
const char* exe_hash(){return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";}
void boot(){for(int event:{SK_EV_APP_PREINIT,SK_EV_APP_INIT,SK_EV_POST_INIT}){CHECK(subscriptions[event].fn);subscriptions[event].fn(subscriptions[event].user,event,nullptr);}}
bool owner=true,cache_only=true;
std::string last_error;
uint64_t acknowledged;
net::adventure_content::Bundle source;
void log(SkMod*,int,const char*,...){}
void outside(SkTaskFn fn,void* user){tasks.emplace_back(fn,user);}
void drain(){auto work=std::move(tasks);tasks.clear();for(auto [fn,user]:work)fn(user);}
void network(){sessions[0].update();sessions[1].update();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
template<class F>bool spin(F predicate){auto begun=net::now_ms();while(uint32_t(net::now_ms()-begun)<4000){network();if(predicate())return true;}return false;}
void* SK_CDECL get_db(uint32_t){return nullptr;}
void* SK_CDECL allocate(size_t size,const char*,int a,int b,int c,int d){CHECK(size==0x388&&!a&&!b&&!c&&!d);++allocations;
    const auto accounting=aa::g_admission.accounting();CHECK(accounting.count&&accounting.bytes>=source.bytes);
    if(fail_allocate)return nullptr;
    databases.push_back(std::make_unique<std::array<uint8_t,0x388>>());return databases.back()->data();}
void* SK_THISCALL construct(void* self,const wchar_t* path,void* allocator){CHECK(!allocator);++constructors;
    skg::field<void**>(self,0)=db_vt.data();skg::field<void**>(self,4)=ref_vt.data();locations[self]=path;return self;}
int SK_THISCALL addref(void* self){return ++references[static_cast<uint8_t*>(self)-4];}
int SK_THISCALL release(void* self){auto* db=static_cast<uint8_t*>(self)-4;CHECK(references[db]>0);return --references[db];}
bool SK_THISCALL initialize(void*){return !fail_initialize;}
bool SK_THISCALL open_database(void*,int access,int creation,bool lazy){CHECK(access==1&&creation==6&&!lazy);return !fail_open;}
const wchar_t* SK_THISCALL location(void* self){return locations.at(self).c_str();}
void* SK_THISCALL find(void* self,const dbpf::Key*,dbpf::Key*,void* zero){CHECK(self==&manager&&!zero);++finds;return unknown_resident;}
bool SK_THISCALL registration(void* self,bool add,void* db,int priority){CHECK(self==&manager);if(add){CHECK(priority>0);++mounts;if(throw_add)throw std::runtime_error("injected native AddDB exception");return !fail_add;}
    CHECK(!priority&&references[db]==1);++unmounts;return !fail_remove;}
uintptr_t sym(const char* name){
    const std::map<std::string,uintptr_t> names{{"Resource::GetDatabaseByID",uintptr_t(get_db)},
        {"Memory::operator_new_named",uintptr_t(allocate)},{"Resource::DatabasePackedFile::ctor",uintptr_t(construct)},
        {"Resource::ResourceManager_ptr",uintptr_t(&manager_pointer)}};
    auto it=names.find(name);return it==names.end()?0:it->second;
}
aa::Ctx context(unsigned side){
    aa::Ctx c;c.sess=&sessions[side];
    c.collect=[](const dbpf::Key& key,std::vector<dbpf::Record>& records,std::string&){CHECK(key==source.root);records=source.records;return true;};
    c.registration_policy=[](uint64_t run,const dbpf::Key& key,bool& cache,std::string& error){CHECK(run&&key==root);
        if(!owner){error="The pinned dependency owner was retired";return false;}cache=cache_only;return true;};
    c.prepared=[](uint64_t run,const dbpf::Key& key,const std::string& hash){CHECK(run&&key==source.root&&hash==source.hash);++prepared;};
    c.failed=[](uint64_t,const std::string& error){++failed;last_error=error;};return c;
}
SkHost host{};
void configure(unsigned side,bool policy=true){auto c=context(side);if(!policy)c.registration_policy={};CHECK(aa::init(&host,nullptr,std::move(c)));if(!booted){boot();booted=true;}}
void send(const net::Writer& w){CHECK(sessions[0].send_reliable_to(1,net::MSG_ADVENTURE,w));}
net::Writer packet(uint8_t kind,uint64_t run){net::Writer w;w.u8(kind);w.u32(uint32_t(run));w.u32(uint32_t(run>>32));return w;}
void key(net::Writer& w,const dbpf::Key& k){w.u32(k.instance);w.u32(k.type);w.u32(k.group);}
void send_bundle(uint64_t run,bool corrupt=false){
    auto w=packet(64,run);key(w,source.root);w.u16(uint16_t(source.manifest.size()));w.u32(source.bytes);w.str8(source.hash);send(w);
    for(size_t i=0;i<source.manifest.size();++i){const auto& e=source.manifest[i];w=packet(65,run);w.u16(uint16_t(i));key(w,e.key);w.u32(e.size);w.str8(e.hash);send(w);}
    send(packet(71,run));
    if(corrupt)for(size_t i=0;i<source.records.size();++i){auto bytes=source.records[i].data;bytes[0]^=0xFF;
        for(size_t offset=0;offset<bytes.size();offset+=net::adventure_content::kChunkBytes){auto count=std::min<size_t>(net::adventure_content::kChunkBytes,bytes.size()-offset);
            w=packet(67,run);w.u16(uint16_t(i));w.u32(uint32_t(offset));w.u16(uint16_t(count));w.raw(bytes.data()+offset,count);send(w);}}
    send(packet(68,run));
}
}
int main(int argc,char** argv){
    CHECK(argc==1||argc==2);const std::string test_case=argc==2?argv[1]:"ordinary";
    std::string error;auto directory=fs::temp_directory_path()/("sporecoop-native-assets-tests-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(net::now_ms()));
    CHECK(!fs::exists(directory));CHECK(fs::create_directories(directory));
    host.sym=sym;host.log=log;host.run_outside_frame=outside;host.subscribe=subscribe;host.unsubscribe=unsubscribe;host.exe_sha256=exe_hash;
    rm_vt[0x30/4]=reinterpret_cast<void*>(find);rm_vt[0x50/4]=reinterpret_cast<void*>(registration);
    db_vt[0x04/4]=reinterpret_cast<void*>(initialize);db_vt[0x18/4]=reinterpret_cast<void*>(open_database);db_vt[0x28/4]=reinterpret_cast<void*>(location);
    ref_vt[0]=reinterpret_cast<void*>(addref);ref_vt[1]=reinterpret_cast<void*>(release);
    CHECK(sessions[0].host(0,"Host",error));CHECK(sessions[1].join("127.0.0.1:"+std::to_string(sessions[0].port()),"Guest",error));
    CHECK(spin([]{return sessions[0].players().size()==2&&sessions[1].connected();}));
    sessions[0].cb.custom=[](uint8_t type,uint8_t,net::Reader& r){CHECK(type==net::MSG_ADVENTURE);auto kind=r.u8();uint64_t run=r.u32();run|=uint64_t(r.u32())<<32;
        if(kind==69){dbpf::Key k{r.u32(),r.u32(),r.u32()};auto hash=r.str8();CHECK(r.ok()&&!r.left()&&k==source.root&&hash==source.hash);acknowledged=run;}};
    sessions[1].cb.custom=[](uint8_t type,uint8_t from,net::Reader& r){CHECK(type==net::MSG_ADVENTURE);aa::on_message(from,r);};
    CHECK(net::adventure_content::make_bundle(root,{{root,std::vector<uint8_t>(2000,0x42)},{dependency,{1,2,3}}},source,error));
    if(test_case!="ordinary") {
        configure(0,false);aa::g_dir=directory.wstring();
        if(test_case=="count") {
            for(unsigned n=0;n<32;++n){CHECK(net::adventure_content::make_bundle(root,{{root,{uint8_t(n),1,2,3}}},source,error));CHECK(aa::apply_bundle(source,error));}
            const auto calls=allocations;CHECK(net::adventure_content::make_bundle(root,{{root,{33,1,2,3}}},source,error));
            CHECK(!aa::apply_bundle(source,error));CHECK(allocations==calls&&mounts==32&&unmounts==0);CHECK(aa::g_admission.accounting().count==32);
        }else if(test_case=="unknown-resident") {
            auto memory=std::make_unique<std::array<uint8_t,0x388>>();unknown_resident=memory->data();
            skg::field<void**>(unknown_resident,0)=db_vt.data();skg::field<void**>(unknown_resident,4)=ref_vt.data();
            locations[unknown_resident]=aa::cached_path(source.hash,L".package");databases.push_back(std::move(memory));
            CHECK(!aa::apply_bundle(source,error));CHECK(!allocations&&!constructors&&!mounts&&!unmounts&&references[unknown_resident]==0);
            CHECK(aa::g_admission.accounting().uncertain==1);
        }else if(test_case=="priority-exhausted") {
            CHECK(aa::apply_bundle(source,error));const auto native_calls=allocations+mounts+finds;
            aa::g_priority=std::numeric_limits<int>::max();CHECK(!aa::apply_bundle(source,error));
            CHECK(allocations+mounts+finds==native_calls&&!unmounts&&aa::g_admission.accounting().count==1);
        }else if(test_case=="reload") {
            CHECK(aa::apply_bundle(source,error));const auto native_calls=allocations+mounts+finds;
            aa::shutdown();aa::g_databases.clear();configure(0,false);
            CHECK(aa::g_admission.accounting().reloaded);CHECK(!aa::apply_bundle(source,error));
            CHECK(allocations+mounts+finds==native_calls&&!unmounts);CHECK(aa::g_admission.accounting().count==1);
        }else if(test_case.starts_with("promotion")) {
            CHECK(aa::apply_bundle(source,error));auto* database=aa::g_databases.at(source.hash);CHECK(references[database]==1);
            fail_remove=test_case=="promotion-remove-failure";fail_add=test_case=="promotion-add-failure";throw_add=test_case=="promotion-exception";
            const bool expected=test_case=="promotion";CHECK(aa::apply_bundle(source,error)==expected);
            CHECK(allocations==1&&constructors==1&&references[database]==1&&unmounts==1);
            CHECK(aa::g_admission.accounting().count==1&&aa::g_admission.accounting().bytes==source.bytes);
            CHECK(aa::g_admission.accounting().uncertain==unsigned(!expected));
        }else {
            fail_allocate=test_case=="allocation-failure";fail_initialize=test_case=="initialization-failure";
            fail_open=test_case=="open-failure";fail_add=test_case=="registration-failure";throw_add=test_case=="exception";
            CHECK(fail_allocate||fail_initialize||fail_open||fail_add||throw_add);
            CHECK(!aa::apply_bundle(source,error));CHECK(allocations==1&&aa::g_databases.empty());
            CHECK(aa::g_admission.accounting().count==1&&aa::g_admission.accounting().uncertain==1);
            const auto native_calls=allocations+mounts+finds;CHECK(!aa::apply_bundle(source,error));CHECK(allocations+mounts+finds==native_calls);
        }
        aa::shutdown();sessions[0].leave();sessions[1].leave();
        auto resolved=fs::weakly_canonical(directory);CHECK(resolved.parent_path()==fs::weakly_canonical(fs::temp_directory_path())&&resolved.filename().string().starts_with("sporecoop-native-assets-tests-"));fs::remove_all(resolved);
        std::printf("native admission %s: %u checks passed\n",test_case.c_str(),checks);return 0;
    }
    configure(0);aa::g_dir=directory.wstring();CHECK(aa::prepare(1,root));CHECK(!prepared&&!allocations);drain();
    CHECK(prepared==1&&aa::g_prepared&&aa::g_cache_only);CHECK(!allocations&&!constructors&&!mounts&&!finds&&aa::g_databases.empty());
    for(const auto& e:source.manifest)CHECK(fs::exists(directory/(e.hash+".bin")));CHECK(!fs::exists(directory/(source.hash+".package")));
    CHECK(aa::prepare(2,root));aa::reset();drain();CHECK(prepared==1&&!failed&&!mounts);
    CHECK(aa::prepare(3,root));owner=false;drain();CHECK(failed==1&&last_error.find("retired")!=std::string::npos&&!aa::g_prepared&&!mounts);
    owner=true;CHECK(aa::prepare(4,root));cache_only=false;drain();CHECK(failed==2&&last_error.find("changed")!=std::string::npos&&!mounts&&!allocations);
    // Empty policy preserves ordinary GA's registration and immutable package.
    configure(0,false);CHECK(aa::prepare(5,root));drain();CHECK(prepared==2&&allocations==1&&constructors==1&&mounts==1&&aa::g_databases.size()==1);
    CHECK(fs::exists(directory/(source.hash+".package")));CHECK(aa::prepare(5,root));drain();CHECK(prepared==3&&allocations==1&&constructors==1&&mounts==2&&unmounts==1);
    CHECK(aa::g_admission.accounting().count==1);aa::release();CHECK(unmounts==2&&aa::g_databases.empty());
    owner=cache_only=true;configure(1);CHECK(aa::prepare(6,root));send_bundle(6);
    CHECK(spin([]{return !tasks.empty();}));CHECK(prepared==3&&acknowledged==0);drain();CHECK(prepared==4&&aa::g_prepared);
    CHECK(spin([]{return acknowledged==6;}));CHECK(mounts==2&&allocations==1&&aa::g_databases.empty());
    // A changed native owner after a sealed receive cannot acknowledge or mount.
    CHECK(aa::prepare(7,root));send_bundle(7);CHECK(spin([]{return !tasks.empty();}));owner=false;drain();
    for(unsigned i=0;i<5;++i)network();CHECK(acknowledged==6&&failed==3&&!aa::g_prepared&&mounts==2);owner=true;
    // Cache-only does not bypass hash validation for newly transferred records.
    CHECK(net::adventure_content::make_bundle(root,{{root,std::vector<uint8_t>(2000,0x17)}},source,error));
    CHECK(aa::prepare(8,root));send_bundle(8,true);CHECK(spin([]{return failed==4;}));drain();CHECK(acknowledged==6&&!aa::g_prepared&&mounts==2);
    owner=false;CHECK(!aa::prepare(9,root));CHECK(failed==5&&last_error.find("retired")!=std::string::npos&&tasks.empty()&&mounts==2);owner=true;
    configure(0);
    for(uint64_t run=10;run<30;++run){CHECK(net::adventure_content::make_bundle(root,{{root,{uint8_t(run),1,2,3}},{dependency,{1,2,3}}},source,error));
        CHECK(aa::prepare(run,root));drain();CHECK(aa::g_prepared&&aa::g_cache_only&&aa::g_databases.empty());}
    CHECK(mounts==2&&allocations==1&&constructors==1);aa::release();CHECK(unmounts==2);aa::shutdown();
    sessions[0].leave();sessions[1].leave();drain();
    auto resolved=fs::weakly_canonical(directory);CHECK(resolved.parent_path()==fs::weakly_canonical(fs::temp_directory_path())&&resolved.filename().string().starts_with("sporecoop-native-assets-tests-"));fs::remove_all(resolved);
    std::printf("native adventure assets: %u checks passed\n",checks);
}
