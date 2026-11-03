#include "editor_saved_dependencies_store.h"
#include "../../updater/sha256.h"
#include <algorithm>
#include <stdexcept>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#endif


namespace editor_saved_dependencies {
namespace {
namespace fs=std::filesystem;
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool full(Key key){return key.instance && key.type && key.group;}
bool hash(const std::string& text){return text.size()==64 && std::all_of(text.begin(),text.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}) && text!=std::string(64,'0');}
std::string digest(std::span<const uint8_t> bytes){return upd::sha256_hex(bytes.data(),bytes.size());}
enum class Fault {None,BeforeCreate,PartialWrite,BeforeFlush,BeforeInstall,AfterInstall,BeforeReuseFlush};
#ifdef EDITOR_SAVED_DEPENDENCIES_STORE_TEST
thread_local Fault injected_fault=Fault::None;
#else
constexpr Fault injected_fault=Fault::None;
#endif
std::string basename(Key saved,const std::string& image_hash){
    need(full(saved) && saved.type==kScenario && hash(image_hash),"Receipt lookup identity");char key[32];
    std::snprintf(key,sizeof key,"%08x-%08x-%08x-",saved.group,saved.instance,saved.type);return std::string(key)+image_hash+".sdr";
}
bool safe_directory(const fs::path& path){
    const auto absolute=fs::absolute(path).lexically_normal();fs::path current;
    for(const auto& part:absolute){current/=part;std::error_code ec;const auto state=fs::symlink_status(current,ec);
        if(ec==std::errc::no_such_file_or_directory)continue;if(ec)return false;
        if(fs::is_symlink(state) || (fs::exists(state) && !fs::is_directory(state)))return false;
#if defined(_WIN32)
        const auto attrs=GetFileAttributesW(current.c_str());if(attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_REPARSE_POINT))return false;
#endif
    }return true;
}
Bytes read_bounded(const fs::path& path){
    std::error_code ec;const auto state=fs::symlink_status(path,ec);need(!ec && fs::is_regular_file(state) && !fs::is_symlink(state),"Receipt file is absent/not regular");
#if defined(_WIN32)
    const auto attrs=GetFileAttributesW(path.c_str());need(attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_REPARSE_POINT),"Receipt file reparse point");
#endif
    const auto size=fs::file_size(path);need(size && size<=kMaxReceiptBytes,"Receipt file size");Bytes bytes(size_t(size),uint8_t{});
    std::ifstream input(path,std::ios::binary);input.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(bytes.size()));need(bool(input),"Cannot read complete receipt");need(input.peek()==std::char_traits<char>::eof(),"Receipt changed size while reading");return bytes;
}
void flush_new_file(const fs::path& path,const Bytes& bytes,Fault fault,bool& created){
    need(fault!=Fault::BeforeCreate,"Injected create failure");
#if defined(_WIN32)
    const auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);need(file!=INVALID_HANDLE_VALUE,"Cannot create receipt temporary");
    created=true;
    bool okay=false;try{const size_t count=fault==Fault::PartialWrite?bytes.size()/2:bytes.size();size_t at=0;
        while(at<count){DWORD written=0;const auto part=DWORD(std::min<size_t>(count-at,1u<<20));need(WriteFile(file,bytes.data()+at,part,&written,nullptr) && written==part,"Receipt write failed");at+=written;}
        need(fault!=Fault::PartialWrite && fault!=Fault::BeforeFlush,"Injected write/flush failure");need(FlushFileBuffers(file)!=0,"Receipt flush failed");okay=true;
    }catch(...){CloseHandle(file);throw;}need(CloseHandle(file)!=0 && okay,"Receipt close/flush failed");
#else
    const int file=::open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);need(file>=0,"Cannot create receipt temporary");
    created=true;
    try{const size_t count=fault==Fault::PartialWrite?bytes.size()/2:bytes.size();size_t at=0;
        while(at<count){const auto n=::write(file,bytes.data()+at,count-at);need(n>0,"Receipt write failed");at+=size_t(n);}
        need(fault!=Fault::PartialWrite && fault!=Fault::BeforeFlush,"Injected write/flush failure");need(::fsync(file)==0,"Receipt flush failed");
    }catch(...){::close(file);throw;}need(::close(file)==0,"Receipt close failed");
#endif
}
#if !defined(_WIN32)
void flush_directory_chain(const fs::path& directory){
    // A newly created cache directory also needs its entry in each parent
    // durable. The same closure covers a concurrent publisher's new directory.
    auto current=fs::absolute(directory).lexically_normal();
    for(;;){const int dir=::open(current.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW);need(dir>=0,"Cannot open receipt directory for durability");
        const bool flushed=::fsync(dir)==0,closed=::close(dir)==0;need(flushed && closed,"Receipt directory durability failed");
        const auto parent=current.parent_path();if(parent==current || parent.empty())break;current=parent;}
}
#endif
void flush_existing(const fs::path& path,const fs::path& directory,Fault fault){
    need(fault!=Fault::BeforeReuseFlush,"Injected reuse durability failure");
#if defined(_WIN32)
    const auto file=CreateFileW(path.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    need(file!=INVALID_HANDLE_VALUE,"Cannot open existing receipt for durability");const bool flushed=FlushFileBuffers(file)!=0,closed=CloseHandle(file)!=0;need(flushed && closed,"Existing receipt flush/close failed");
    // Initial no-replace MoveFileEx uses WRITE_THROUGH; an existing receipt is
    // never acknowledged solely because its bytes happen to be visible.
    (void)directory;
#else
    const int file=::open(path.c_str(),O_RDONLY|O_NOFOLLOW);need(file>=0,"Cannot open existing receipt for durability");const bool flushed=::fsync(file)==0,closed=::close(file)==0;need(flushed && closed,"Existing receipt flush/close failed");
    flush_directory_chain(directory);
#endif
}
}
Publication publish(const fs::path& requested,const Receipt& receipt,std::string& error){
    const auto fault=injected_fault;fs::path temporary;bool installed=false,temporary_owned=false;
    try{const auto directory=fs::absolute(requested).lexically_normal();Bytes encoded;if(!encode(receipt,encoded,error))throw std::runtime_error(error);need(safe_directory(directory),"Unsafe receipt directory");
        fs::create_directories(directory);need(safe_directory(directory),"Receipt directory changed");const auto target=directory/basename(receipt.inventory.saved,receipt.inventory.raw_hash);
        if(fs::exists(fs::symlink_status(target))){need(read_bounded(target)==encoded,"Existing immutable receipt conflicts with this image");installed=true;flush_existing(target,directory,fault);error.clear();return Publication::Unchanged;}
        static std::atomic<uint64_t> serial{0};
#if defined(_WIN32)
        const auto pid=GetCurrentProcessId();
#else
        const auto pid=::getpid();
#endif
        temporary=directory/(".pending-"+std::to_string(pid)+"-"+std::to_string(++serial));flush_new_file(temporary,encoded,fault,temporary_owned);
        need(fault!=Fault::BeforeInstall,"Injected pre-install failure");need(safe_directory(directory),"Receipt directory changed before publication");
#if defined(_WIN32)
        if(!MoveFileExW(temporary.c_str(),target.c_str(),MOVEFILE_WRITE_THROUGH)){
            need(fs::exists(target) && read_bounded(target)==encoded,"Atomic immutable receipt install failed/conflicted");installed=true;fs::remove(temporary);flush_existing(target,directory,fault);error.clear();return Publication::Unchanged;}
        installed=true;
#else
        // link is atomic and refuses an existing target. Temporary and target
        // are in the same directory/filesystem; there is no replace fallback.
        if(::link(temporary.c_str(),target.c_str())){need(fs::exists(target) && read_bounded(target)==encoded,"Atomic immutable receipt install failed/conflicted");installed=true;fs::remove(temporary);flush_existing(target,directory,fault);error.clear();return Publication::Unchanged;}
        installed=true;
        fs::remove(temporary);
#endif
        need(fault!=Fault::AfterInstall,"Injected post-install durability uncertainty");
#if !defined(_WIN32)
        flush_directory_chain(directory);
#endif
        error.clear();return Publication::Published;
    }catch(const std::exception& e){error=e.what();std::error_code ignored;if(temporary_owned)fs::remove(temporary,ignored);return installed?Publication::Uncertain:Publication::NotPublished;}
}
bool load(const fs::path& requested,const Domain& domain,Key selected,std::span<const uint8_t> genuine_raw,Receipt& out,std::string& error){
    try{const auto directory=fs::absolute(requested).lexically_normal();need(!genuine_raw.empty() && genuine_raw.size()<=kMaxRecordBytes && safe_directory(directory),"Saved receipt lookup context");
        const auto raw_hash=digest(genuine_raw);auto bytes=read_bounded(directory/basename(selected,raw_hash));Receipt candidate;
        if(!decode(bytes,domain,candidate,error))throw std::runtime_error(error);need(candidate.inventory.saved==selected && candidate.inventory.raw_size==genuine_raw.size() && candidate.inventory.raw_hash==raw_hash,"Selected Native Saved image is not this receipt");
        out=std::move(candidate);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
