// Verified collector read path with controlled i686 ResourceManager/DB streams.
// No game process, package registration, native profile or save file is touched.
#include "../mods/coop/adventure_collect.cpp"
#include <array>
#include <cstdlib>

namespace {
unsigned checks,finds,opens,closes,releases;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::printf("FAIL %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(1);}}while(0)
std::array<void*,16> rm_vt{},db_vt{},record_vt{},stream_vt{};
struct Object{void** vtable;};Object rm{rm_vt.data()},database{db_vt.data()},record{record_vt.data()},stream{stream_vt.data()};
void* rm_pointer=&rm;
const dbpf::Key root{7,0x366A930D,0x408A0000},asset{8,0x1A99B06B,0x40626200};
std::vector<uint8_t> installed{1,2,3,4};
dbpf::Key resolved=asset;
bool present=true,fail_open=false,fail_read=false,fail_seek=false,oversized=false;
size_t offset=0;
void* SK_THISCALL find(void* self,const dbpf::Key* key,dbpf::Key* real,void* zero){
    CHECK(self==&rm && !zero);++finds;if(!present || *key!=asset)return nullptr;*real=resolved;return &database;
}
const wchar_t* SK_THISCALL location(void* self){CHECK(self==&database);return L"D:\\private-fixture\\Spore_Content.package";}
bool SK_THISCALL open_record(void* self,const dbpf::Key* key,void** out,int access,int creation,bool lazy,void* zero){
    CHECK(self==&database && *key==asset && access==1 && creation==6 && lazy && !zero);++opens;
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
    CHECK(!adventure_collect::validate_uploaded_records(asset,records,error));
    records.push_back(records.back());CHECK(!adventure_collect::validate_uploaded_records(root,records,error));
    rm_pointer=nullptr;CHECK(!adventure_collect::validate_uploaded_records(root,{},error));
    std::printf("adventure collector Windows checks: %u passed\n",checks);
}
