// Helpers for calling into SporeApp.exe (MSVC 2008, x86) from mingw/clang code.
//
// Game classes are NOT mirrored as C++ classes (vtable/ABI differences). Instead:
//   * fields are read by offset:           field<float>(obj, 0x1C)
//   * non-virtual methods by address:      thiscall<int>(addr, obj, args...)
//   * virtual methods by vtable index:     vcall<int>(obj, index, args...)
// Methods returning structs by value use MSVC's hidden return pointer, passed
// right after `this`: declare them explicitly as thiscall<T*>(addr, obj, &ret, ...).
#pragma once

#include "sporekit_build.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <array>
#include <limits>

#define SK_THISCALL __attribute__((thiscall))
#define SK_STDCALL __attribute__((stdcall))
#define SK_CDECL __attribute__((cdecl))

namespace skg {

// A14 (CORE-6): a mod may install a probe that sees every game call made through the helpers below with the game
// function's address (coop's game.cpp counts the calls made off the game's main thread, mods/coop/off_main.h). One
// copy per module (an inline variable of this header); null, the default, costs a load and a branch per call.
using CallProbe = void (*)(uintptr_t fn);
inline CallProbe call_probe = nullptr;

template <typename T>
inline T& field(void* obj, uintptr_t off)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(obj) + off);
}

template <typename T>
inline T* ptr_add(void* obj, intptr_t off)
{
    return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(obj) + off);
}

template <typename R, typename... A>
inline R thiscall(uintptr_t addr, void* self, A... a)
{
    using F = R(SK_THISCALL*)(void*, A...);
    if (call_probe)
        call_probe(addr);
    return reinterpret_cast<F>(addr)(self, a...);
}

template <typename R, typename... A>
inline R cdecl_call(uintptr_t addr, A... a)
{
    using F = R(SK_CDECL*)(A...);
    if (call_probe)
        call_probe(addr);
    return reinterpret_cast<F>(addr)(a...);
}

template <typename R, typename... A>
inline R stdcall_call(uintptr_t addr, A... a)
{
    using F = R(SK_STDCALL*)(A...);
    if (call_probe)
        call_probe(addr);
    return reinterpret_cast<F>(addr)(a...);
}

inline void** vtable(void* obj) { return *reinterpret_cast<void***>(obj); }

template <typename R, typename... A>
inline R vcall(void* self, int index, A... a)
{
    using F = R(SK_THISCALL*)(void*, A...);
    if (call_probe)
        call_probe(reinterpret_cast<uintptr_t>(vtable(self)[index]));
    return reinterpret_cast<F>(vtable(self)[index])(self, a...);
}

// ---- common math types (layout-compatible with Spore's Math::) ----
struct Vector3 {
    float x, y, z;
};
struct Quaternion {
    float x, y, z, w;
};
struct ResourceKey {
    uint32_t instance, type, group;
};

// Reuse VirtualQuery results only during a synchronous, read-only traversal.
// Never keep this scope alive across native calls that allocate/free memory,
// queued tasks, frames or act changes. Outside a scope every check stays fresh.
class ReadOnlyMemoryScope {
    struct Region { uintptr_t begin, end; DWORD state, protect; };
    std::array<Region, 64> regions_;
    size_t count_ = 0;
    ReadOnlyMemoryScope* previous_;
    inline static thread_local ReadOnlyMemoryScope* current_ = nullptr;

    bool query(uintptr_t address, Region& result)
    {
        for (size_t i = 0; i < count_; ++i) {
            if (address >= regions_[i].begin && address < regions_[i].end) {
                result = regions_[i]; return true;
            }
        }
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<const void*>(address), &mbi, sizeof mbi)) return false;
        uintptr_t begin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        if (mbi.RegionSize > std::numeric_limits<uintptr_t>::max() - begin) return false;
        result = {begin, begin + mbi.RegionSize, mbi.State, mbi.Protect};
        if (count_ < regions_.size()) regions_[count_++] = result;
        return true;
    }
    friend bool readable(const void*, size_t);

public:
    ReadOnlyMemoryScope() : previous_(current_) { if (!current_) current_ = this; }
    ~ReadOnlyMemoryScope() { current_ = previous_; }
    ReadOnlyMemoryScope(const ReadOnlyMemoryScope&) = delete;
    ReadOnlyMemoryScope& operator=(const ReadOnlyMemoryScope&) = delete;
};
// BUG-0791 (measurement): readable() calls of this module (one VirtualQuery each, a syscall), and for a cost check
// readable_extra more VirtualQuery per call (tests compare the frame time with 1 and with 1 + extra queries).
inline volatile LONG readable_calls = 0;
inline int readable_extra = 0;

// Basic memory sanity check before dereferencing game pointers from tools.
inline bool readable(const void* p, size_t n)
{
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (n > std::numeric_limits<uintptr_t>::max() - a) return false;
    uintptr_t end = a + n;
    InterlockedIncrement(&readable_calls);
    MEMORY_BASIC_INFORMATION extra_query{};
    for (int i = 0; i < readable_extra; ++i) VirtualQuery(p, &extra_query, sizeof extra_query);
    while (a < end) {
        ReadOnlyMemoryScope::Region region;
        if (ReadOnlyMemoryScope::current_) {
            if (!ReadOnlyMemoryScope::current_->query(a, region)) return false;
        } else {
            MEMORY_BASIC_INFORMATION mbi;
            if (!VirtualQuery(reinterpret_cast<const void*>(a), &mbi, sizeof mbi)) return false;
            uintptr_t begin = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
            if (mbi.RegionSize > std::numeric_limits<uintptr_t>::max() - begin) return false;
            region = {begin, begin + mbi.RegionSize, mbi.State, mbi.Protect};
        }
        DWORD pr = region.protect & 0xFF;
        if (region.state != MEM_COMMIT || pr == PAGE_NOACCESS || pr == PAGE_EXECUTE ||
            (region.protect & PAGE_GUARD) || region.end <= a)
            return false;
        a = region.end;
    }
    return true;
}

} // namespace skg
