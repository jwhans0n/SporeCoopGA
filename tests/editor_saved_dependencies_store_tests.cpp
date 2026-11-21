#define EDITOR_SAVED_DEPENDENCIES_STORE_TEST
#include "../mods/coop/editor_saved_dependencies_store.cpp"
#include "editor_saved_dependencies_fixture.h"
#include <chrono>
#include <thread>
namespace saved_dependency_test {
using editor_saved_dependencies::Fault;
static Publication publish(const fs::path& directory,const Receipt& receipt,Fault fault,std::string& error){
    struct Reset {Fault old=editor_saved_dependencies::injected_fault;~Reset(){editor_saved_dependencies::injected_fault=old;}} reset;
    editor_saved_dependencies::injected_fault=fault;return editor_saved_dependencies::publish(directory,receipt,error);
}
static void store_cases(const Fixture& f,const fs::path& directory){
    check(!fs::exists(directory),"NEW OUT test-store only");std::string error;
    check(publish(directory,f.receipt,Fault::None,error)==Publication::Published,"durable immutable publication: "+error);++cases;
    const auto old_file=directory/basename(f.inventory.saved,f.inventory.raw_hash);const auto old_bytes=file(old_file);
    check(publish(directory,f.receipt,Fault::BeforeCreate,error)==Publication::Unchanged && file(old_file)==old_bytes,"exact idempotent immutable reuse");++cases;
    check(publish(directory,f.receipt,Fault::BeforeReuseFlush,error)==Publication::Uncertain && !error.empty() && file(old_file)==old_bytes,"visible identical bytes still require reuse durability");++cases;
    check(publish(directory,f.receipt,Fault::None,error)==Publication::Unchanged,"reuse retry reestablishes durability");++cases;
    Receipt read=f.receipt;check(load(directory,f.domain,f.inventory.saved,f.raw,read,error) && encoded(read)==encoded(f.receipt),"exact selected image lookup");++cases;
    auto raw=f.raw;raw.back()^=1;check(!load(directory,f.domain,f.inventory.saved,raw,read,error) && encoded(read)==encoded(f.receipt),"normalized/different image cannot borrow receipt");++cases;
    auto key=f.inventory.saved;++key.instance;check(!load(directory,f.domain,key,f.raw,read,error),"wrong selected key lookup");++cases;
    auto conflicting=f.receipt;conflicting.records.front().data.back()^=1;
    check(publish(directory,conflicting,Fault::None,error)==Publication::NotPublished && file(old_file)==old_bytes,"same Saved image different dependency bytes never overwrite");++cases;
    unsigned number=0;
    for(auto fault:{Fault::BeforeCreate,Fault::PartialWrite,Fault::BeforeFlush,Fault::BeforeInstall,Fault::AfterInstall}){
        auto next=f.receipt;auto raw_next=f.raw;raw_next[0]^=uint8_t(++number);next.inventory.raw_hash=digest(raw_next);
        const auto final=directory/basename(next.inventory.saved,next.inventory.raw_hash);const auto outcome=publish(directory,next,fault,error);
        check(outcome==(fault==Fault::AfterInstall?Publication::Uncertain:Publication::NotPublished),"injected publication outcome");
        check(!error.empty() && file(old_file)==old_bytes,"previous receipt survives every publication failure");
        check(fs::exists(final)==(fault==Fault::AfterInstall),"pre-install failures publish no target; post-install uncertainty is explicit");
        if(fault==Fault::AfterInstall)check(publish(directory,next,Fault::None,error)==Publication::Unchanged,"uncertain visibility is re-flushed before reuse acknowledgement");
        for(const auto& entry:fs::directory_iterator(directory))check(entry.path().filename().string().find(".pending-")!=0,"temporary cleanup");++cases;
    }
    auto next=f.receipt;auto raw_next=f.raw;raw_next[1]^=1;next.inventory.raw_hash=digest(raw_next);const auto new_file=directory/basename(next.inventory.saved,next.inventory.raw_hash);
    check(publish(directory,next,Fault::None,error)==Publication::Published && file(old_file)==old_bytes && fs::exists(new_file),"new Save image retains previous immutable receipt");++cases;
    // The target writer is no-replace. Concurrent equal publication wins once;
    // both observers may succeed only with exactly identical final bytes.
    auto concurrent=f.receipt;auto raw_concurrent=f.raw;raw_concurrent[2]^=1;concurrent.inventory.raw_hash=digest(raw_concurrent);
    std::array<Publication,2> outcomes;std::array<std::string,2> reasons;
    std::thread a([&]{outcomes[0]=publish(directory,concurrent,Fault::None,reasons[0]);});std::thread b([&]{outcomes[1]=publish(directory,concurrent,Fault::None,reasons[1]);});a.join();b.join();
    check(std::all_of(outcomes.begin(),outcomes.end(),[](auto p){return p==Publication::Published || p==Publication::Unchanged;}),"concurrent identical publication");++cases;
    auto damaged=file(new_file);damaged.back()^=1;{std::ofstream write(new_file,std::ios::binary|std::ios::trunc);write.write(reinterpret_cast<const char*>(damaged.data()),std::streamsize(damaged.size()));}
    const auto before=encoded(read);check(!load(directory,f.domain,next.inventory.saved,raw_next,read,error) && encoded(read)==before && file(old_file)==old_bytes,"corrupt selected receipt cannot replace output/prior receipt");++cases;
    check(publish(directory,next,Fault::None,error)==Publication::NotPublished && file(new_file)==damaged,"corrupt existing receipt never repaired/overwritten silently");++cases;
    const auto obstruction=directory/"not-a-directory";{std::ofstream o(obstruction);o<<"x";}
    check(publish(obstruction/"child",f.receipt,Fault::None,error)==Publication::NotPublished && file(old_file)==old_bytes,"non-directory path rejected before publication");++cases;
}

static void unequal_race(const Fixture& f,const fs::path& directory){
    auto first=f.receipt;auto raw=f.raw;raw[3]^=1;first.inventory.raw_hash=digest(raw);auto second=first;second.records[0].data.back()^=1;
    std::array<Publication,2> outcomes;std::array<std::string,2> errors;
    std::thread a([&]{outcomes[0]=publish(directory,first,Fault::None,errors[0]);});
    std::thread b([&]{outcomes[1]=publish(directory,second,Fault::None,errors[1]);});a.join();b.join();
    check(std::count(outcomes.begin(),outcomes.end(),Publication::Published)==1 && std::count(outcomes.begin(),outcomes.end(),Publication::NotPublished)==1,"unequal concurrent publication has one immutable winner");
    const auto bytes=file(directory/editor_saved_dependencies::basename(first.inventory.saved,first.inventory.raw_hash));
    check(bytes==encoded(first) || bytes==encoded(second),"unequal race never publishes partial/mixed bytes");++cases;
}
static void path_cases(const Fixture& f,const fs::path& directory){
    const auto target=directory/editor_saved_dependencies::basename(f.inventory.saved,f.inventory.raw_hash);const auto old=file(target);
    bool created=false,failed=false;try{editor_saved_dependencies::flush_new_file(target,Bytes{1,2,3},Fault::None,created);}catch(const std::exception&){failed=true;}
    check(failed && !created && file(target)==old,"exclusive create cannot own or change a prior file");++cases;
    const auto linked=directory.parent_path()/"linked-cache";std::error_code error;fs::create_directory_symlink(fs::absolute(directory),linked,error);
    if(!error){std::string reason;check(publish(linked,f.receipt,Fault::None,reason)==Publication::NotPublished && file(target)==old,"symlink/reparse directory rejected");
        Receipt output=f.receipt;check(!load(linked,f.domain,f.inventory.saved,f.raw,output,reason) && encoded(output)==f.golden,"symlink lookup atomic");++cases;}
    else{
#ifndef _WIN32
        check(false,"Linux symlink fixture must be available");
#else
        std::cout<<"NOTE directory-symlink fixture unavailable: "<<error.message()<<'\n';
#endif
    }
}
static void zero_group_cases(const Fixture& f,const fs::path& directory){auto receipt=f.receipt;receipt.inventory.saved.group=0;std::string error;
    check(publish(directory,receipt,Fault::None,error)==Publication::Published,"Saved group-zero namespace publication");Receipt loaded;
    check(load(directory,f.domain,receipt.inventory.saved,f.raw,loaded,error) && encoded(loaded)==encoded(receipt),"Saved group-zero exact lookup");++cases;
    for(bool missing_type:{false,true}){auto invalid=receipt;if(missing_type)invalid.inventory.saved.type=0;else invalid.inventory.saved.instance=0;
        check(publish(directory,invalid,Fault::None,error)==Publication::NotPublished && !error.empty(),"Saved group-zero still requires instance/Scenario type");++cases;}
}
}
int main(int argc,char** argv){using namespace saved_dependency_test;try{
    check(argc==2,"explicit isolated test parent required");const fs::path parent=argv[1];fs::create_directories(parent);
    const auto stem=std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());fs::path directory;
    for(unsigned n=0;n<100;++n){auto candidate=parent/(stem+"-"+std::to_string(n));if(fs::create_directory(candidate)){directory=candidate/"cache";break;}}
    check(!directory.empty(),"reserve unique isolated test directory");const auto f=fixture();store_cases(f,directory);unequal_race(f,directory);path_cases(f,directory);zero_group_cases(f,directory);
    std::cout<<"PASS Saved dependency store checks="<<checks<<" cases="<<cases<<" real_fs=1 native_calls=0 directory="<<directory.string()<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL checks="<<checks<<" cases="<<cases<<": "<<e.what()<<'\n';return 1;}}
