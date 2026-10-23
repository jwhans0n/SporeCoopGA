// Helpers for calling into SporeApp.exe (MSVC 2008, x86) from mingw/clang code.
//
// Game classes are NOT mirrored as C++ classes (vtable/ABI differences). Instead:
//   * fields are read by offset:           field<float>(obj, 0x1C)
//   * non-virtual methods by address:      thiscall<int>(addr, obj, args...)
//   * virtual methods by vtable index:     vcall<int>(obj, index, args...)
// Methods returning structs by value use MSVC's hidden return pointer, passed
// right after `this`: declare them explicitly as thiscall<T*>(addr, obj, &ret, ...).
#pragma once

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
    return reinterpret_cast<F>(addr)(self, a...);
}

template <typename R, typename... A>
inline R cdecl_call(uintptr_t addr, A... a)
{
    using F = R(SK_CDECL*)(A...);
    return reinterpret_cast<F>(addr)(a...);
}

template <typename R, typename... A>
inline R stdcall_call(uintptr_t addr, A... a)
{
    using F = R(SK_STDCALL*)(A...);
    return reinterpret_cast<F>(addr)(a...);
}

inline void** vtable(void* obj) { return *reinterpret_cast<void***>(obj); }

template <typename R, typename... A>
inline R vcall(void* self, int index, A... a)
{
    using F = R(SK_THISCALL*)(void*, A...);
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

// Basic memory sanity check before dereferencing game pointers from tools.
inline bool readable(const void* p, size_t n)
{
    uintptr_t a = reinterpret_cast<uintptr_t>(p);
    if (n > std::numeric_limits<uintptr_t>::max() - a) return false;
    uintptr_t end = a + n;
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
