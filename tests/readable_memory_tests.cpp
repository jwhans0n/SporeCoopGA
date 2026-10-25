// Win32 regression for the repeated VirtualQuery cost seen with 438 GA actors.
#include <windows.h>
#include <cstdio>
#include <limits>

static unsigned queries;
static SIZE_T WINAPI tracked_query(LPCVOID address, PMEMORY_BASIC_INFORMATION info, SIZE_T size)
{
    ++queries;
    return VirtualQuery(address, info, size);
}
#define VirtualQuery tracked_query
#include "sporekit_game.hpp"
#undef VirtualQuery

static int failures;
static void check(bool condition, const char* message)
{
    if (!condition) { ++failures; std::printf("FAIL: %s\n", message); }
}

int main()
{
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    size_t page = system.dwPageSize;
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, page * 3, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(memory != nullptr, "allocate the native fixture");
    if (!memory) return 1;
    queries = 0;
    for (unsigned i = 0; i < 438; ++i) check(skg::readable(memory + i, 32), "actor memory is readable");
    check(queries == 438, "unscoped checks query Windows afresh");
    queries = 0;
    {
        skg::ReadOnlyMemoryScope pass;
        for (unsigned i = 0; i < 438; ++i) {
            check(skg::readable(memory + i, 32), "the traversal validates each actor");
            skg::ReadOnlyMemoryScope nested;
            check(skg::readable(memory + page + i, 32), "nested helpers share only the current read-only pass");
        }
        check(queries == 1, "438 actors and their nested getters require one query for one memory region");
    }
    DWORD previous;
    check(VirtualProtect(memory + page, page, PAGE_NOACCESS, &previous) != FALSE, "protect the middle page between passes");
    {
        skg::ReadOnlyMemoryScope next_pass;
        check(!skg::readable(memory + page, 1), "a new pass observes changed page protection");
        check(!skg::readable(memory + page - 16, 32), "a range spanning an inaccessible page is rejected");
        check(skg::readable(memory, page), "the first readable region still passes");
        check(skg::readable(memory + 2 * page, page), "a separate readable region still passes");
    }
    check(VirtualProtect(memory + page, page, PAGE_READWRITE | PAGE_GUARD, &previous) != FALSE, "mark a guard page");
    check(!skg::readable(memory + page, 1), "fresh checks reject a guard page without touching it");
    check(VirtualFree(memory, 0, MEM_RELEASE) != FALSE, "release the traversal fixture");
    {
        skg::ReadOnlyMemoryScope after_destroy;
        check(!skg::readable(memory, 1), "no mapping cache survives an actor allocation being released");
    }
    auto* fragmented = static_cast<unsigned char*>(VirtualAlloc(nullptr, page * 160, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(fragmented != nullptr, "allocate more regions than fit in a validation scope");
    if (fragmented) {
        for (unsigned i = 0; i < 80; ++i)
            check(VirtualProtect(fragmented + (2 * i + 1) * page, page, PAGE_NOACCESS, &previous) != FALSE,
                  "split the allocation into readable and inaccessible regions");
        {
            skg::ReadOnlyMemoryScope bounded;
            for (unsigned i = 0; i < 80; ++i) {
                check(skg::readable(fragmented + 2 * i * page, page), "cache overflow falls back to fresh readable checks");
                check(!skg::readable(fragmented + (2 * i + 1) * page, 1), "cache overflow never admits inaccessible memory");
            }
        }
        VirtualFree(fragmented, 0, MEM_RELEASE);
    }
    check(!skg::readable(reinterpret_cast<void*>(std::numeric_limits<uintptr_t>::max() - 4), 16),
          "an overflowing range cannot wrap into a readable address");
    check(skg::readable(nullptr, 0), "an empty range needs no dereference");
    std::printf("readable memory: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
    return failures ? 1 : 0;
}
