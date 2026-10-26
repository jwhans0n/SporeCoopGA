#include "editor_store.h"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs=std::filesystem;
using namespace editor_store;
static int checks=0;
#define CHECK(x) do { ++checks; if(!(x)) {std::cerr<<__LINE__<<": " #x "\n";std::exit(1);} } while(0)
struct Temp {
    fs::path root=fs::temp_directory_path()/("sporecoop-editor-store-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp(){fs::create_directories(root);}
    ~Temp(){std::error_code ec;fs::remove_all(root,ec);}
};
static Snapshot sample(){Snapshot s;s.project[0]=37;s.epoch=4;s.native_snapshot={0,255,0,4,8};s.document={2,0,9};s.id_mapping={8,0,3};s.manifest={11,0,7};s.metadata="opaque project metadata";return s;}
static std::vector<uint8_t> load(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void put(const fs::path& p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary|std::ios::trunc);if(!b.empty())f.write(reinterpret_cast<const char*>(b.data()),std::streamsize(b.size()));CHECK(bool(f));}
static Snapshot next(Snapshot s){++s.revision;s.native_snapshot.clear();s.document.push_back(uint8_t(s.revision));s.metadata="revision "+std::to_string(s.revision);return s;}

static void codec(){
    const auto s=sample();std::string error;std::vector<uint8_t> bytes;
    CHECK(encode_snapshot(s,bytes,error));Snapshot decoded;CHECK(decode_snapshot(bytes,decoded,error));CHECK(decoded==s);
    for(size_t cut=0;cut<bytes.size();++cut){Snapshot result=s;const auto old=result;CHECK(!decode_snapshot({bytes.begin(),bytes.begin()+cut},result,error));CHECK(result==old);}
    auto bad=bytes;bad.push_back(0);CHECK(!decode_snapshot(bad,decoded,error));
    // The native length field is directly after the fixed 36-byte identity.
    bad=bytes;for(size_t i=36;i<40;++i)bad[i]=255;CHECK(!decode_snapshot(bad,decoded,error));
    auto invalid=s;invalid.project={};CHECK(!encode_snapshot(invalid,bytes,error));
    invalid=s;invalid.schema=2;CHECK(!encode_snapshot(invalid,bytes,error));
    invalid=s;invalid.metadata.resize(kMetadataBytes+1);CHECK(!encode_snapshot(invalid,bytes,error));
    invalid=s;invalid.native_snapshot.clear();CHECK(!encode_snapshot(invalid,bytes,error));
}
static void lifecycle(){
    Temp t;Store store,second;std::string error;auto s=sample();CHECK(store.create(t.root,s,error));CHECK(store.healthy());CHECK(store.snapshot()==s);
    CHECK(!second.open(t.root,s.project,error));CHECK(!second.create(t.root,s,error));
    auto candidate=next(s),wrong=candidate;wrong.epoch++;CHECK(!store.commit(wrong,error));wrong=candidate;wrong.revision++;CHECK(!store.commit(wrong,error));wrong=candidate;wrong.project[1]=1;CHECK(!store.commit(wrong,error));CHECK(store.durable_revision()==0);
    CHECK(store.commit(candidate,error));s=candidate;s.native_snapshot=sample().native_snapshot;CHECK(store.snapshot()==s);CHECK(store.durable_revision()==1);
    const auto dir=store.directory();store.close();CHECK(store.open(t.root,s.project,error));CHECK(store.snapshot()==s);
    CHECK(!second.create(t.root,s,error));
    auto changed=next(s);changed.native_snapshot={13,0,77,255};CHECK(store.commit(changed,error));s=changed;
    store.close();CHECK(store.open(t.root,s.project,error));CHECK(store.snapshot()==s);
    auto compact=s;compact.epoch++;CHECK(store.checkpoint(compact,error));CHECK(fs::file_size(dir/"journal.bin")==0);store.close();CHECK(store.open(t.root,s.project,error));CHECK(store.snapshot()==compact);
    CHECK(store.commit(next(compact),error));CHECK(store.durable_revision()==3);
    store.close();CHECK(!second.create(t.root,s,error));
}
static void every_torn_tail(){
    Temp t;Store store;std::string error;const auto initial=sample();CHECK(store.create(t.root,initial,error));const auto dir=store.directory();
    const auto one=next(initial);CHECK(store.commit(one,error));const auto boundary=fs::file_size(dir/"journal.bin");
    const auto two=next(one);CHECK(store.commit(two,error));store.close();const auto all=load(dir/"journal.bin");
    for(size_t cut=0;cut<=all.size();++cut){
        put(dir/"journal.bin",{all.begin(),all.begin()+cut});CHECK(store.open(t.root,initial.project,error));
        const uint64_t expected=cut==all.size()?2:cut>=boundary?1:0;CHECK(store.durable_revision()==expected);
        CHECK(store.snapshot().native_snapshot==initial.native_snapshot);
        CHECK(fs::file_size(dir/"journal.bin")== (expected==2?all.size():expected==1?boundary:0));
        // A recovered partial tail must not prevent the next durable commit.
        auto candidate=next(store.snapshot());CHECK(store.commit(candidate,error));store.close();
    }
}
static void corruption_and_compaction(){
    Temp t;Store store;std::string error;const auto initial=sample();CHECK(store.create(t.root,initial,error));const auto dir=store.directory();
    CHECK(store.commit(next(initial),error));const auto first=load(dir/"journal.bin");CHECK(store.commit(next(store.snapshot()),error));const auto final=store.snapshot();store.close();const auto both=load(dir/"journal.bin");
    auto broken=both;broken.back()^=128;put(dir/"journal.bin",broken);CHECK(!store.open(t.root,initial.project,error));
    auto duplicate=first;duplicate.insert(duplicate.end(),first.begin(),first.end());put(dir/"journal.bin",duplicate);CHECK(!store.open(t.root,initial.project,error));
    std::vector<uint8_t> reorder(both.begin()+first.size(),both.end());reorder.insert(reorder.end(),first.begin(),first.end());put(dir/"journal.bin",reorder);CHECK(!store.open(t.root,initial.project,error));
    put(dir/"journal.bin",both);CHECK(store.open(t.root,initial.project,error));CHECK(store.snapshot()==final);
    auto rehost=final;rehost.epoch++;CHECK(store.checkpoint(rehost,error));store.close();
    // Emulate power loss after checkpoint rename but before old journal truncation.
    put(dir/"journal.bin",both);put(dir/"checkpoint.tmp",{1,2,3});CHECK(store.open(t.root,initial.project,error));CHECK(store.snapshot()==rehost);CHECK(fs::file_size(dir/"journal.bin")==0);
    CHECK(store.commit(next(rehost),error));store.close();CHECK(store.open(t.root,initial.project,error));CHECK(store.durable_revision()==3);CHECK(store.snapshot().epoch==rehost.epoch);
}
static void output_failures(){
    Temp t;Store store;std::string error;const auto initial=sample();CHECK(store.create(t.root,initial,error));const auto dir=store.directory();
    fs::remove(dir/"journal.bin");fs::create_directory(dir/"journal.bin");CHECK(!store.commit(next(initial),error));CHECK(!store.healthy());CHECK(store.durable_revision()==0);CHECK(!store.commit(next(initial),error));
    store.close();fs::remove(dir/"journal.bin");CHECK(store.open(t.root,initial.project,error));CHECK(store.snapshot()==initial);
    fs::create_directory(dir/"checkpoint.tmp");CHECK(!store.checkpoint(initial,error));CHECK(!store.healthy());store.close();fs::remove(dir/"checkpoint.tmp");CHECK(store.open(t.root,initial.project,error));CHECK(store.snapshot()==initial);store.close();
#ifndef _WIN32
    // Force a real short append followed by ENOSPC-like EFBIG, then recover it.
    const pid_t child=fork();CHECK(child>=0);
    if(child==0){std::signal(SIGXFSZ,SIG_IGN);Store writer;if(!writer.open(t.root,initial.project,error))_exit(20);struct rlimit size{30,30};if(setrlimit(RLIMIT_FSIZE,&size))_exit(21);
        const bool accepted=writer.commit(next(initial),error);_exit(!accepted&&!writer.healthy()&&writer.durable_revision()==0?0:22);}
    int result=0;CHECK(waitpid(child,&result,0)==child);CHECK(WIFEXITED(result)&&WEXITSTATUS(result)==0);CHECK(fs::file_size(dir/"journal.bin")==30);
    CHECK(store.open(t.root,initial.project,error));CHECK(store.durable_revision()==0);CHECK(fs::file_size(dir/"journal.bin")==0);CHECK(store.commit(next(initial),error));store.close();
    fs::create_directory(t.root/"real");fs::create_directory_symlink(t.root/"real",t.root/"linked");CHECK(!store.create(t.root/"linked",initial,error));
    fs::remove(dir/"journal.bin");fs::create_symlink(t.root/"unrelated",dir/"journal.bin");CHECK(!store.open(t.root,initial.project,error));
#endif
}
static void replica_adoption(){
    Temp t;Store store;std::string error;auto s=sample();CHECK(store.create(t.root,s,error));const auto dir=store.directory();
    CHECK(store.commit(next(s),error));s=store.snapshot();auto baseline=s;baseline.revision=19;baseline.document={6,0,5};
    CHECK(!store.checkpoint(baseline,error));CHECK(store.healthy());CHECK(store.adopt_checkpoint(baseline,error));CHECK(store.snapshot()==baseline);CHECK(fs::file_size(dir/"journal.bin")==0);
    store.close();CHECK(store.open(t.root,s.project,error));CHECK(store.snapshot()==baseline);CHECK(store.adopt_checkpoint(baseline,error));
    auto invalid=baseline;--invalid.revision;CHECK(!store.adopt_checkpoint(invalid,error));invalid=baseline;invalid.epoch=0;CHECK(!store.adopt_checkpoint(invalid,error));invalid=baseline;invalid.project[1]=1;CHECK(!store.adopt_checkpoint(invalid,error));CHECK(store.snapshot()==baseline);
    // A new authoritative epoch is allowed to adopt its own pinned revision.
    auto new_session=baseline;--new_session.epoch;new_session.revision=7;CHECK(store.adopt_checkpoint(new_session,error));CHECK(store.snapshot()==new_session);
    fs::create_directory(dir/"checkpoint.tmp");auto later=new_session;later.revision=40;CHECK(!store.adopt_checkpoint(later,error));CHECK(!store.healthy());store.close();fs::remove(dir/"checkpoint.tmp");
    CHECK(store.open(t.root,s.project,error));CHECK(store.snapshot()==new_session);CHECK(store.commit(next(new_session),error));
}
int main(){codec();lifecycle();every_torn_tail();corruption_and_compaction();output_failures();replica_adoption();std::cout<<"editor store: "<<checks<<" checks passed\n";}
