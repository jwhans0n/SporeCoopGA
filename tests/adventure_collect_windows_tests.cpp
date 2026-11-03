// Verified collector read path with controlled i686 ResourceManager/DB streams.
// No game process, package registration, native profile or save file is touched.
#include "../mods/coop/adventure_collect.cpp"
#include <array>
#include <cstdlib>
#include <functional>

namespace {
unsigned checks,finds,opens,closes,releases;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::printf("FAIL %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(1);}}while(0)
std::array<void*,16> rm_vt{},db_vt{},record_vt{},stream_vt{};
struct Object{void** vtable;};Object rm{rm_vt.data()},database{db_vt.data()},record{record_vt.data()},stream{stream_vt.data()};
void* rm_pointer=&rm;
const dbpf::Key root{7,0x366A930D,0x408A0000},asset{8,0x1A99B06B,0x40626200};
std::vector<uint8_t> installed{1,2,3,4};
dbpf::Key lookup_key=asset,resolved=asset;
bool present=true,fail_open=false,fail_read=false,fail_seek=false,oversized=false;
size_t offset=0;
std::wstring database_location=L"D:\\private-fixture\\Spore_Content.package";
void* SK_THISCALL find(void* self,const dbpf::Key* key,dbpf::Key* real,void* zero){
    CHECK(self==&rm && !zero);++finds;if(!present || *key!=lookup_key)return nullptr;*real=resolved;return &database;
}
const wchar_t* SK_THISCALL location(void* self){CHECK(self==&database);return database_location.c_str();}
bool SK_THISCALL open_record(void* self,const dbpf::Key* key,void** out,int access,int creation,bool lazy,void* zero){
    CHECK(self==&database && *key==resolved && access==1 && creation==6 && lazy && !zero);++opens;
    if(fail_open)return false;
    *out=&record;offset=0;return true;
}
void* SK_THISCALL get_stream(void* self){CHECK(self==&record);return &stream;}
bool SK_THISCALL close_record(void* self){CHECK(self==&record);++closes;return true;}
int SK_THISCALL release_record(void* self){CHECK(self==&record);++releases;return 0;}
uint32_t SK_THISCALL size(void* self){CHECK(self==&stream);return oversized?(17u<<20):uint32_t(installed.size());}
bool SK_THISCALL seek(void* self,int at,int origin){CHECK(self==&stream && !at && !origin);offset=0;return !fail_seek;}
int SK_THISCALL read_record(void* self,void* out,uint32_t length){
    CHECK(self==&stream && length<=installed.size()-offset);if(fail_read)return 0;
    const auto count=std::min<size_t>(2,length);std::memcpy(out,installed.data()+offset,count);offset+=count;return int(count);
}
uintptr_t sym(const char* name){return std::string(name)=="Resource::ResourceManager_ptr"?uintptr_t(&rm_pointer):0;}
std::array<uint8_t,0x2CB0> saved_image{};
std::array<uint8_t,0x38*2> saved_crew{};
std::vector<uint8_t> saved_classes(0x27E8*2);
void* saved_vtable[2]{};
unsigned saved_allocations=0,saved_releases=0;
bool saved_fail_allocate=false,saved_fail_read=false,saved_short_read=false;
std::function<void()> allocation_hook;
int SK_THISCALL saved_addref(void* self){return ++skg::field<int>(self,4);}
int SK_THISCALL saved_release(void* self){CHECK(skg::field<int>(self,4)==1);++saved_releases;std::free(self);return 0;}
void* SK_CDECL saved_allocate(uint32_t size,const char*,int,int,int,int){
    CHECK(size==saved_image.size());if(allocation_hook)allocation_hook();
    if(saved_fail_allocate)return nullptr;++saved_allocations;return std::calloc(1,size);
}
void* SK_THISCALL saved_construct(void* self){
    std::memcpy(self,saved_image.data(),saved_image.size());skg::field<void**>(self,0)=saved_vtable;return self;
}
bool SK_THISCALL saved_read(void*,void* input){
    auto& value=*static_cast<adventure_collect::WriteStream*>(input);
    value.position=uint32_t(value.bytes.size())-(saved_short_read?1:0);return !saved_fail_read;
}
void saved_reference_tests(){
    namespace saved=editor_saved_dependencies;
    using adventure_collect::saved_references;using adventure_collect::saved_dependencies;
    rm_pointer=&rm;present=true;lookup_key=resolved=asset;
    saved_vtable[0]=reinterpret_cast<void*>(saved_addref);saved_vtable[1]=reinterpret_cast<void*>(saved_release);
    adventure_collect::a_New=reinterpret_cast<uintptr_t>(saved_allocate);
    adventure_collect::a_ScenarioCtor=reinterpret_cast<uintptr_t>(saved_construct);
    adventure_collect::a_Read=reinterpret_cast<uintptr_t>(saved_read);
    const auto reference=[](void* target,uint32_t machine){
        skg::field<dbpf::Key>(target,0)=asset;skg::field<uint32_t>(target,0xC)=machine;
        skg::field<uint64_t>(target,0x10)=UINT64_MAX;skg::field<uint32_t>(target,0x18)=0x12345678;
    };
    reference(saved_image.data()+0x130,3);
    skg::field<dbpf::Key>(saved_image.data(),8)=root;
    reference(saved_crew.data(),4);reference(saved_crew.data()+0x38,5);
    skg::field<uint32_t>(saved_image.data(),0x178)=uint32_t(uintptr_t(saved_crew.data()));
    skg::field<uint32_t>(saved_image.data(),0x17C)=uint32_t(uintptr_t(saved_crew.data()+saved_crew.size()));
    for(unsigned n=0;n<2;++n){auto* row=saved_classes.data()+n*0x27E8;
        skg::field<uint32_t>(row,0)=n?2:9;
        for(unsigned slot=0;slot<3;++slot)reference(row+8+slot*0x28,10+n*3+slot);
    }
    skg::field<uint32_t>(saved_image.data(),0x2BF4)=uint32_t(uintptr_t(saved_classes.data()));
    skg::field<uint32_t>(saved_image.data(),0x2BF8)=uint32_t(uintptr_t(saved_classes.data()+saved_classes.size()));
    const std::vector<uint8_t> raw{8,6,7,5,3,0,9};saved::Inventory observed;std::string error;
    auto before=finds;
    CHECK(saved_references(root,raw,observed,error));CHECK(finds==before);
    CHECK(observed.saved==root && observed.raw_size==raw.size() && observed.raw_hash==upd::sha256_hex(raw.data(),raw.size()));
    CHECK(observed.classes==std::vector<uint32_t>({2,9}) && observed.crew_count==2 && observed.references.size()==9);
    CHECK(observed.references[0].kind==saved::Kind::Captain && observed.references[0].machine==3);
    CHECK(observed.references[1].kind==saved::Kind::Crew && observed.references[2].index==1 && observed.references[2].machine==5);
    for(unsigned n=0;n<6;++n){const auto& row=observed.references[3+n];
        CHECK(row.kind==saved::Kind::Class && row.index==(n<3?2u:9u) && row.slot==n%3);
    }
    for(const auto& row:observed.references){CHECK(row.raw==asset && row.server==UINT64_MAX && row.model==0x12345678);}
    CHECK(saved_allocations==saved_releases);
    saved::Inventory selected_root;
    CHECK(adventure_collect::selected_saved_references(saved_image.data(),selected_root,error));
    CHECK(selected_root.saved==root && selected_root.classes==observed.classes && selected_root.references.size()==observed.references.size());
    for(size_t n=0;n<observed.references.size();++n){
        CHECK(selected_root.references[n].raw==observed.references[n].raw && selected_root.references[n].model==observed.references[n].model &&
              selected_root.references[n].machine==observed.references[n].machine);
    }
    std::vector<dbpf::Record> selected{{root,{99}}};
    CHECK(saved_dependencies(root,raw,observed,selected,error));CHECK(selected.empty());
    for(const auto& row:observed.references)CHECK(row.origin==saved::Origin::Stock && row.stock_database==1 && row.effective==asset && row.records.empty());
    const auto location_before=database_location;
    const std::wstring suffix=L"\\Spore_Content.package";
    database_location=std::wstring(1000-suffix.size(),L'x')+suffix;
    CHECK(!adventure_collect::vanilla(&database)); // NUL is beyond the bounded Native location.
    database_location=location_before;CHECK(adventure_collect::vanilla(&database));
    // A missing resource is never inferred to be stock, and both outputs stay atomic.
    const auto previous=observed;selected={{root,{99}}};present=false;
    CHECK(!saved_dependencies(root,raw,observed,selected,error));
    CHECK(observed.raw_hash==previous.raw_hash && observed.references.size()==previous.references.size() && selected.size()==1 && selected[0].data==std::vector<uint8_t>({99}));
    present=true;
    for(auto* flag:{&saved_fail_allocate,&saved_fail_read,&saved_short_read}){
        *flag=true;CHECK(!saved_references(root,raw,observed,error));*flag=false;
        CHECK(observed.raw_hash==previous.raw_hash && observed.classes==previous.classes && saved_allocations==saved_releases);
    }
    skg::field<uint32_t>(saved_classes.data()+0x27E8,0)=9;
    CHECK(!saved_references(root,raw,observed,error));CHECK(observed.classes==previous.classes && saved_allocations==saved_releases);
    skg::field<uint32_t>(saved_classes.data()+0x27E8,0)=2;
    const auto end=skg::field<uint32_t>(saved_image.data(),0x17C);
    skg::field<uint32_t>(saved_image.data(),0x17C)=end-1;
    CHECK(!saved_references(root,raw,observed,error));CHECK(saved_allocations==saved_releases);
    skg::field<uint32_t>(saved_image.data(),0x17C)=end;
    auto mutable_raw=raw;allocation_hook=[&]{mutable_raw.clear();};
    CHECK(saved_references(root,mutable_raw,observed,error));allocation_hook={};
    CHECK(mutable_raw.empty() && observed.raw_hash==previous.raw_hash && observed.raw_size==raw.size());
    before=saved_allocations;
    CHECK(!saved_references(root,{},observed,error));CHECK(!saved_references(asset,raw,observed,error));CHECK(saved_allocations==before);
}
}
int main(){
    rm_vt[0x30/4]=reinterpret_cast<void*>(find);db_vt[0x28/4]=reinterpret_cast<void*>(location);db_vt[0x34/4]=reinterpret_cast<void*>(open_record);
    record_vt[0x18/4]=reinterpret_cast<void*>(get_stream);record_vt[0x24/4]=reinterpret_cast<void*>(close_record);record_vt[0x08/4]=reinterpret_cast<void*>(release_record);
    stream_vt[0x1C/4]=reinterpret_cast<void*>(size);stream_vt[0x28/4]=reinterpret_cast<void*>(seek);stream_vt[0x30/4]=reinterpret_cast<void*>(read_record);
    SkHost host{};host.sym=sym;host.exe_sha256=[](){return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";};CHECK(adventure_collect::init(&host,nullptr));
    std::string error;std::vector<dbpf::Record> records{{root,{99}},{asset,installed}};
    CHECK(adventure_collect::validate_uploaded_records(root,records,error));CHECK(opens==1 && closes==1 && releases==1); // Includes vanilla bytes.
    installed[0]=9;CHECK(!adventure_collect::validate_uploaded_records(root,records,error));CHECK(error.find("conflicts")!=std::string::npos && opens==2 && closes==2 && releases==2);
    present=false;CHECK(adventure_collect::validate_uploaded_records(root,records,error));CHECK(opens==2);present=true;installed=records[1].data;
    resolved.type=0x2399BE55;CHECK(!adventure_collect::validate_uploaded_records(root,records,error));CHECK(error.find("aliases")!=std::string::npos && opens==2);resolved=asset;
    fail_open=true;CHECK(!adventure_collect::validate_uploaded_records(root,records,error));CHECK(!error.empty() && opens==3 && closes==2 && releases==2);fail_open=false;
    for(auto* failure:{&fail_read,&fail_seek,&oversized}){
        *failure=true;const auto before=closes;CHECK(!adventure_collect::validate_uploaded_records(root,records,error));CHECK(closes==before+1 && releases==closes);*failure=false;
    }
    const auto before=finds;CHECK(adventure_collect::validate_uploaded_records(root,{{root,{1,2,3}}},error));CHECK(finds==before);
    lookup_key=resolved={root.instance+1,root.type,root.group};
    CHECK(!adventure_collect::validate_uploaded_records(root,{{lookup_key,{9,8}}},error)); // Other adventures are immutable dependencies.
    CHECK(error.find("conflicts")!=std::string::npos);
    lookup_key=resolved=asset;
    CHECK(!adventure_collect::validate_uploaded_records(asset,records,error));
    records.push_back(records.back());CHECK(!adventure_collect::validate_uploaded_records(root,records,error));
    rm_pointer=nullptr;CHECK(!adventure_collect::validate_uploaded_records(root,{},error));
    saved_reference_tests();
    std::printf("adventure collector Windows checks: %u passed\n",checks);
}
