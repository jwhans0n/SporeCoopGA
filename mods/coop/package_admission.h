// Process-lifetime native import admission. No native pointers or allocator claims.
#pragma once
#include <array>
#include <cstdint>
#include <type_traits>

namespace package_admission {
constexpr uint32_t kVersion = 1;
constexpr uint32_t kMaxPackages = 32;
constexpr uint32_t kMaxBytes = 256u * 1024u * 1024u;
constexpr uint32_t kMaxBundleBytes = 128u * 1024u * 1024u;
constexpr uint64_t kMagic = 0x31414447504f4f43ull;
using Hash = std::array<uint8_t,32>;
struct Identity { uint32_t pid; uint32_t reserved; uint64_t creation; Hash executable; };
struct Key { uint32_t instance, type, group; bool operator==(const Key&) const = default; };
enum class State : uint32_t { Reserved=1, Success=2, Uncertain=3 };
struct Entry { Hash hash; Key root; uint32_t bytes; uint32_t module; State state; };
// This exact POD lives in a pagefile mapping retained by one process-owned handle.
// Fields are touched only under the wrapper's interlocked guard, outside Present.
struct Ledger {
    uint64_t magic;
    uint32_t version, size;
    Identity identity;
    uint64_t owner_handle;
    int32_t update_lock;
    uint32_t module_counter, attached_module, boot_stage, flags;
    Hash profile;
    uint32_t profile_bound, count, bytes;
    uint64_t ticket_counter, active_ticket;
    uint32_t active_index, active_module, active_reused;
    std::array<Entry,kMaxPackages> entries;
};
static_assert(std::is_trivial_v<Ledger> && std::is_standard_layout_v<Ledger>);
static_assert(sizeof(Ledger) <= 4096);
constexpr uint32_t kUncertain=1, kRetired=2;
struct Ticket {
    uint64_t serial=0;
    uint32_t module=0, index=0;
    bool reused=false;
};
struct Accounting { uint32_t count=0, bytes=0, successful=0, uncertain=0, boot_stage=0, flags=0; bool reloaded=false; };
enum class Event { PreInit, AppInit, PostInit, Shutdown };

class Core {
public:
    Core()=default;
    Core(const Core&)=delete;
    Core& operator=(const Core&)=delete;
    static void initialize(Ledger& ledger,const Identity& identity,uint64_t owner_handle);
    bool attach(Ledger& ledger,const Identity& identity,const char*& error);
    void detach(); // never refunds charges or resets trusted lifecycle state
    bool event(Event event,const char*& error);
    bool bind_profile(const Hash& hash,const char*& error);
    bool begin(const Hash& hash,const Key& root,uint32_t bytes,bool current_owned,Ticket& ticket,const char*& error);
    bool finish(const Ticket& ticket,bool success,const char*& error);
    Accounting accounting() const;
private:
    Ledger* ledger_=nullptr;
    uint32_t module_=0;
    bool reloaded_=false;
    bool valid(const char*& error) const;
};
} // namespace package_admission
