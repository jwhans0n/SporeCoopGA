#include "editor_store.h"
#include "net/buffer.h"
#include "../../updater/sha256.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace editor_store {
namespace {
namespace fs=std::filesystem;
constexpr uint32_t kMagic=0x31504345, kFormat=1;
constexpr size_t kFrameHeader=45, kFrameLimit=kNativeBytes+kDocumentBytes+kMappingBytes+kManifestBytes+kMetadataBytes+256;
struct Invalid:std::runtime_error{using runtime_error::runtime_error;};
void need(bool yes,const char* why){if(!yes)throw Invalid(why);}
std::string system_error(const char* what){
#ifdef _WIN32
    return std::string(what)+": Windows error "+std::to_string(GetLastError());
#else
    return std::string(what)+": "+std::strerror(errno);
#endif
}
bool present(const fs::path& p){std::error_code e;const bool yes=fs::exists(p,e);need(!e,"cannot inspect project path");return yes;}
void safe(const fs::path& p){
    for(auto path=fs::absolute(p);;path=path.parent_path()){
        std::error_code error;auto status=fs::symlink_status(path,error);
        if(!error)need(!fs::is_symlink(status),"project path cannot contain symbolic links");
#ifdef _WIN32
        const DWORD attrs=GetFileAttributesW(path.c_str());
        need(attrs==INVALID_FILE_ATTRIBUTES || !(attrs&FILE_ATTRIBUTE_REPARSE_POINT),"project path cannot contain reparse points");
#endif
        if(path==path.parent_path())break;
    }
}
void u64(net::Writer& w,uint64_t v){w.u32(uint32_t(v));w.u32(uint32_t(v>>32));}
uint64_t u64(net::Reader& r){const auto lo=r.u32();return lo|(uint64_t(r.u32())<<32);}
void blob(net::Writer& w,const std::vector<uint8_t>& v){w.u32(uint32_t(v.size()));if(!v.empty())w.raw(v.data(),v.size());}
void blob(net::Reader& r,std::vector<uint8_t>& v,size_t limit){const size_t n=r.u32();need(r.ok()&&n<=limit&&n<=r.left(),"invalid project field length");v.assign(r.cur(),r.cur()+n);r.skip(n);}
void digest(net::Writer& w,const std::string& h){need(h.size()==64,"invalid digest");for(size_t i=0;i<64;i+=2)w.u8(uint8_t(std::stoul(h.substr(i,2),nullptr,16)));}
std::string digest(net::Reader& r){static const char* hex="0123456789abcdef";std::string h;for(int i=0;i<32;++i){const auto c=r.u8();h+=hex[c>>4];h+=hex[c&15];}need(r.ok(),"truncated digest");return h;}
void valid(const Snapshot& s,bool native_required){
    need(std::any_of(s.project.begin(),s.project.end(),[](auto c){return c!=0;})&&s.epoch&&s.schema==kSchema,"invalid project identity/schema");
    need((!native_required||!s.native_snapshot.empty())&&!s.document.empty(),"missing native or semantic snapshot");
    need(s.native_snapshot.size()<=kNativeBytes&&s.document.size()<=kDocumentBytes&&s.id_mapping.size()<=kMappingBytes&&
         s.manifest.size()<=kManifestBytes&&s.metadata.size()<=kMetadataBytes,"project exceeds size limit");
}
void identity(net::Writer& w,const Snapshot& s){w.raw(s.project.data(),s.project.size());u64(w,s.epoch);u64(w,s.revision);w.u32(s.schema);}
void identity(net::Reader& r,Snapshot& s){need(r.raw(s.project.data(),s.project.size()),"truncated project identity");s.epoch=u64(r);s.revision=u64(r);s.schema=r.u32();}
void semantics(net::Writer& w,const Snapshot& s){blob(w,s.document);blob(w,s.id_mapping);blob(w,s.manifest);w.u32(uint32_t(s.metadata.size()));if(!s.metadata.empty())w.raw(s.metadata.data(),s.metadata.size());}
void semantics(net::Reader& r,Snapshot& s){blob(r,s.document,kDocumentBytes);blob(r,s.id_mapping,kMappingBytes);blob(r,s.manifest,kManifestBytes);
    const size_t n=r.u32();need(r.ok()&&n<=kMetadataBytes&&n<=r.left(),"invalid metadata length");s.metadata.assign(reinterpret_cast<const char*>(r.cur()),n);r.skip(n);}
std::string state_hash(const Snapshot& s,const std::string& native_hash){net::Writer w;identity(w,s);digest(w,native_hash);semantics(w,s);return upd::sha256_hex(w.data);}
std::vector<uint8_t> frame(uint8_t type,const std::vector<uint8_t>& payload){need(payload.size()<=kFrameLimit,"project frame exceeds limit");net::Writer w;w.u32(kMagic);w.u32(kFormat);w.u8(type);w.u32(uint32_t(payload.size()));digest(w,upd::sha256_hex(payload));if(!payload.empty())w.raw(payload.data(),payload.size());return std::move(w.data);}
struct Parsed {uint8_t type;std::vector<uint8_t> payload;size_t next;};
bool parse_frame(const std::vector<uint8_t>& bytes,size_t offset,Parsed& p){
    if(bytes.size()-offset<kFrameHeader)return false;
    net::Reader r(bytes.data()+offset,bytes.size()-offset);
    need(r.u32()==kMagic&&r.u32()==kFormat,"project frame magic/schema mismatch");p.type=r.u8();const size_t n=r.u32();
    need(n<=kFrameLimit,"project frame declared size exceeds limit");const auto hash=digest(r);
    if(n>r.left())return false;
    p.payload.assign(r.cur(),r.cur()+n);need(upd::sha256_hex(p.payload)==hash,"project checksum mismatch");p.next=offset+kFrameHeader+n;return true;
}
std::vector<uint8_t> read_file(const fs::path& path,size_t limit){safe(path);std::error_code e;const auto n=fs::file_size(path,e);need(!e&&n<=limit,"project file unavailable or oversized");
    std::vector<uint8_t> b(size_t(n),uint8_t{});std::ifstream file(path,std::ios::binary);need(bool(file),"cannot read project file");if(n)file.read(reinterpret_cast<char*>(b.data()),std::streamsize(n));need(bool(file),"short project read");return b;}
bool write_file(const fs::path& path,const std::vector<uint8_t>& bytes,bool append,std::string& error){
    try {safe(path);
#ifdef _WIN32
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,append?OPEN_ALWAYS:CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE){error=system_error("cannot open project output");return false;}
        bool okay=true;if(append){LARGE_INTEGER zero{};okay=SetFilePointerEx(file,zero,nullptr,FILE_END)!=0;}
        size_t offset=0;while(okay&&offset<bytes.size()){DWORD written=0;const DWORD size=DWORD(std::min<size_t>(1u<<20,bytes.size()-offset));okay=WriteFile(file,bytes.data()+offset,size,&written,nullptr)&&written;offset+=written;}
        okay=okay&&FlushFileBuffers(file);if(!okay)error=system_error("cannot flush project output");CloseHandle(file);return okay;
#else
        const int file=::open(path.c_str(),O_WRONLY|O_CREAT|(append?O_APPEND:O_TRUNC)|O_NOFOLLOW,0600);
        if(file<0){error=system_error("cannot open project output");return false;}
        size_t offset=0;while(offset<bytes.size()){const auto n=::write(file,bytes.data()+offset,bytes.size()-offset);if(n<0&&errno==EINTR)continue;if(n<=0){error=system_error("cannot write project output");::close(file);return false;}offset+=size_t(n);}
        const bool okay=::fsync(file)==0;if(!okay)error=system_error("cannot flush project output");::close(file);return okay;
#endif
    }catch(const std::exception& e){error=e.what();return false;}
}
bool truncate_file(const fs::path& path,uint64_t length,std::string& error){
#ifdef _WIN32
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){error=system_error("cannot repair journal");return false;}
    LARGE_INTEGER position{};position.QuadPart=length;const bool okay=SetFilePointerEx(file,position,nullptr,FILE_BEGIN)&&SetEndOfFile(file)&&FlushFileBuffers(file);
    if(!okay)error=system_error("cannot flush repaired journal");CloseHandle(file);return okay;
#else
    int file=::open(path.c_str(),O_WRONLY|O_CREAT|O_NOFOLLOW,0600);if(file<0){error=system_error("cannot repair journal");return false;}
    const bool okay=::ftruncate(file,off_t(length))==0&&::fsync(file)==0;if(!okay)error=system_error("cannot flush repaired journal");::close(file);return okay;
#endif
}
bool replace_file(const fs::path& from,const fs::path& to,std::string& error){
#ifdef _WIN32
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){error=system_error("cannot replace checkpoint");return false;}return true;
#else
    if(::rename(from.c_str(),to.c_str())){error=system_error("cannot replace checkpoint");return false;}
    const int dir=::open(to.parent_path().c_str(),O_RDONLY|O_DIRECTORY);if(dir<0){error=system_error("cannot flush project directory");return false;}
    const bool okay=::fsync(dir)==0;if(!okay)error=system_error("cannot flush project directory");::close(dir);return okay;
#endif
}
}

std::string project_name(const Project& p){static const char* hex="0123456789abcdef";std::string s;for(auto c:p){s+=hex[c>>4];s+=hex[c&15];}return s;}
bool encode_snapshot(const Snapshot& s,std::vector<uint8_t>& out,std::string& error){
    try{valid(s,true);net::Writer w;identity(w,s);blob(w,s.native_snapshot);semantics(w,s);out=std::move(w.data);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}
}
bool decode_snapshot(const std::vector<uint8_t>& data,Snapshot& out,std::string& error){
    try{need(data.size()<=kFrameLimit,"snapshot exceeds limit");net::Reader r(data.data(),data.size());Snapshot s;identity(r,s);blob(r,s.native_snapshot,kNativeBytes);semantics(r,s);need(r.ok()&&!r.left(),"snapshot truncated or trailing bytes");valid(s,true);out=std::move(s);error.clear();return true;}catch(const std::exception& e){error=e.what();return false;}
}
Store::~Store(){close();}
void Store::close(){
    if(lock_!=-1){
#ifdef _WIN32
        CloseHandle(reinterpret_cast<HANDLE>(lock_));
#else
        ::flock(int(lock_),LOCK_UN);::close(int(lock_));
#endif
    }
    lock_=-1;healthy_=false;directory_.clear();snapshot_={};digest_.clear();native_hash_.clear();journal_bytes_=0;
}
bool Store::lock(const fs::path& root,const Project& project,std::string& error){
    close();try{need(std::any_of(project.begin(),project.end(),[](auto b){return b!=0;}),"zero project identity");safe(root);directory_=fs::absolute(root)/project_name(project);safe(directory_);fs::create_directories(directory_);safe(directory_/"project.lock");
#ifdef _WIN32
        auto handle=CreateFileW((directory_/"project.lock").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle==INVALID_HANDLE_VALUE){error=system_error("project already open or unavailable");close();return false;}lock_=reinterpret_cast<intptr_t>(handle);
#else
        int file=::open((directory_/"project.lock").c_str(),O_RDWR|O_CREAT|O_NOFOLLOW,0600);
        if(file<0||::flock(file,LOCK_EX|LOCK_NB)){if(file>=0)::close(file);error=system_error("project already open or unavailable");close();return false;}lock_=file;
#endif
        for(const char* name:{"checkpoint.bin","checkpoint.tmp","journal.bin"})safe(directory_/name);
#ifndef _WIN32
        // Persist the new project directory entry before accepting its first save.
        for(auto path=directory_.parent_path();;path=path.parent_path()){
            const int parent=::open(path.c_str(),O_RDONLY|O_DIRECTORY);
            need(parent>=0,"cannot open project parent directory");
            const bool flushed=::fsync(parent)==0;::close(parent);
            need(flushed,"cannot flush project parent directory");
            if(path==path.parent_path())break;
        }
#endif
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();close();return false;}
}
bool Store::save_checkpoint(const Snapshot& s,std::string& error){
    // Allocate all replacement state before changing the durable checkpoint.
    Snapshot staged=s;auto native_hash=upd::sha256_hex(s.native_snapshot);
    auto state_digest=state_hash(s,native_hash);
    std::vector<uint8_t> payload;if(!encode_snapshot(s,payload,error))return false;
    auto bytes=frame(1,payload);
    if(!write_file(directory_/"checkpoint.tmp",bytes,false,error)||!replace_file(directory_/"checkpoint.tmp",directory_/"checkpoint.bin",error))return false;
    // A crash before journal truncation is safe: recovery skips revisions already
    // represented by the independently durable checkpoint.
    snapshot_=std::move(staged);native_hash_=std::move(native_hash);digest_=std::move(state_digest);
    if(!truncate_file(directory_/"journal.bin",0,error)){healthy_=false;return false;}
    journal_bytes_=0;return true;
}
bool Store::create(const fs::path& root,const Snapshot& s,std::string& error){
    try{valid(s,true);if(!lock(root,s.project,error))return false;need(!present(directory_/"checkpoint.bin")&&!present(directory_/"journal.bin"),"project already exists");healthy_=save_checkpoint(s,error);return healthy_;}
    catch(const std::exception& e){error=e.what();close();return false;}
}
bool Store::open(const fs::path& root,const Project& project,std::string& error){
    if(!lock(root,project,error))return false;
    try{auto checkpoint=read_file(directory_/"checkpoint.bin",kFrameLimit+kFrameHeader);Parsed p;need(parse_frame(checkpoint,0,p)&&p.type==1&&p.next==checkpoint.size(),"invalid checkpoint frame");
        Snapshot recovered;need(decode_snapshot(p.payload,recovered,error),"invalid checkpoint snapshot");need(recovered.project==project,"checkpoint belongs to another project");
        const auto checkpoint_revision=recovered.revision;native_hash_=upd::sha256_hex(recovered.native_snapshot);digest_=state_hash(recovered,native_hash_);
        auto bytes=present(directory_/"journal.bin")?read_file(directory_/"journal.bin",kJournalBytes+kFrameLimit):std::vector<uint8_t>{};size_t offset=0;
        while(offset<bytes.size()){
            Parsed entry;if(!parse_frame(bytes,offset,entry))break;need(entry.type==2,"invalid journal frame type");net::Reader r(entry.payload.data(),entry.payload.size());
            const auto previous=digest(r);Snapshot next;identity(r,next);const auto changed=r.u8();need(changed<=1,"invalid native snapshot flag");const auto native_hash=digest(r);
            if(changed)blob(r,next.native_snapshot,kNativeBytes);semantics(r,next);need(r.ok()&&!r.left(),"invalid journal payload");valid(next,false);
            need(next.project==project,"journal belongs to another project");
            if(next.revision>checkpoint_revision){need(next.epoch==recovered.epoch&&next.revision==recovered.revision+1&&previous==digest_,"journal revision/epoch chain mismatch");
                if(changed){need(!next.native_snapshot.empty()&&upd::sha256_hex(next.native_snapshot)==native_hash,"native snapshot hash mismatch");recovered.native_snapshot=std::move(next.native_snapshot);native_hash_=native_hash;}
                else need(native_hash==native_hash_,"journal native dependency mismatch");
                recovered.epoch=next.epoch;recovered.revision=next.revision;recovered.schema=next.schema;recovered.document=std::move(next.document);recovered.id_mapping=std::move(next.id_mapping);recovered.manifest=std::move(next.manifest);recovered.metadata=std::move(next.metadata);digest_=state_hash(recovered,native_hash_);
            }
            offset=entry.next;
        }
        snapshot_=std::move(recovered);journal_bytes_=snapshot_.revision==checkpoint_revision?0:offset;
        need(truncate_file(directory_/"journal.bin",journal_bytes_,error),"cannot repair torn journal tail");healthy_=true;error.clear();return true;
    }catch(const std::exception& e){error=e.what();close();return false;}
}
bool Store::commit(const Snapshot& candidate,std::string& error){
    try{need(healthy_,"project store is not writable; reopen it");valid(candidate,false);
        need(candidate.project==snapshot_.project&&candidate.epoch==snapshot_.epoch&&candidate.revision==snapshot_.revision+1&&snapshot_.revision!=UINT64_MAX,"candidate is not the next project revision");
        const bool changed=!candidate.native_snapshot.empty()&&candidate.native_snapshot!=snapshot_.native_snapshot;
        auto hash=changed?upd::sha256_hex(candidate.native_snapshot):native_hash_;
        Snapshot staged=candidate;
        auto state_digest=state_hash(staged,hash);
        net::Writer w;digest(w,digest_);identity(w,candidate);w.u8(changed?1:0);digest(w,hash);if(changed)blob(w,candidate.native_snapshot);semantics(w,candidate);
        auto bytes=frame(2,w.data);
        if(journal_bytes_>=kJournalBytes || bytes.size()>kJournalBytes-journal_bytes_){Snapshot full=candidate;if(full.native_snapshot.empty())full.native_snapshot=snapshot_.native_snapshot;
            healthy_=save_checkpoint(full,error);return healthy_;}
        if(!write_file(directory_/"journal.bin",bytes,true,error)){healthy_=false;return false;}
        journal_bytes_+=bytes.size();
        if(staged.native_snapshot.empty())staged.native_snapshot.swap(snapshot_.native_snapshot);
        snapshot_=std::move(staged);
        native_hash_=std::move(hash);digest_=std::move(state_digest);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Store::checkpoint(const Snapshot& s,std::string& error){
    try{need(healthy_,"project store is not writable");valid(s,true);need(s.project==snapshot_.project&&s.revision==snapshot_.revision,"checkpoint cannot invent a revision");
        healthy_=save_checkpoint(s,error);return healthy_;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Store::adopt_checkpoint(const Snapshot& s,std::string& error){
    try{need(healthy_,"project store is not writable");valid(s,true);
        need(s.project==snapshot_.project && (s.epoch!=snapshot_.epoch ||
             (s.epoch==snapshot_.epoch && s.revision>=snapshot_.revision)),"replica baseline regresses project or session scope");
        healthy_=save_checkpoint(s,error);return healthy_;
    }catch(const std::exception& e){error=e.what();return false;}
}
} // namespace editor_store
