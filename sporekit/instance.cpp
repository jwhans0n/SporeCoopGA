// Profiles and multi-instance support.
//
//  -sporekit-profile:<name>   user data (saves, preferences, caches) goes to
//                             AppData\Roaming\SporeKit-<name>\Spore instead of the real
//                             profile. Documents uses the same test root, so creation PNGs
//                             cannot be written into the player's My Spore Creations folder.
//  -sporekit-instance:N (N>=2) unique single-instance mutex + console/coop ports +N-1;
//                             implies profile "inst<N>" unless a profile is given.
#include "core.h"

#include <shlobj.h>

namespace sk {

typedef HRESULT(WINAPI* SHGetFolderPathW_t)(HWND, int, HANDLE, DWORD, LPWSTR);
typedef HANDLE(WINAPI* CreateMutexA_t)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
static SHGetFolderPathW_t o_SHGetFolderPathW;
static CreateMutexA_t o_CreateMutexA;
static std::wstring g_profile;

const std::wstring& profile_name() { return g_profile; }

static HRESULT WINAPI h_SHGetFolderPathW(HWND h, int csidl, HANDLE tok, DWORD flags, LPWSTR out)
{
    HRESULT r = o_SHGetFolderPathW(h, csidl, tok, flags, out);
    int folder = csidl & 0xFF;
    if (SUCCEEDED(r) && out && !g_profile.empty() && (folder == CSIDL_APPDATA || folder == CSIDL_PERSONAL)) {
        wchar_t roaming[MAX_PATH];
        if (folder == CSIDL_PERSONAL && FAILED(o_SHGetFolderPathW(h, CSIDL_APPDATA, tok, flags, roaming)))
            return E_FAIL; // a test profile must not fall back to the player's Documents
        std::wstring p = std::wstring(folder == CSIDL_PERSONAL ? roaming : out) + L"\\SporeKit-" + g_profile;
        CreateDirectoryW(p.c_str(), nullptr);
        if (folder == CSIDL_PERSONAL) {
            p += L"\\Documents";
            CreateDirectoryW(p.c_str(), nullptr);
        }
        wcsncpy(out, p.c_str(), MAX_PATH - 1);
        out[MAX_PATH - 1] = 0;
    }
    return r;
}

static HANDLE WINAPI h_CreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL own, LPCSTR name)
{
    if (name && *name) {
        char buf[300];
        snprintf(buf, sizeof buf, "%s_sk%d", name, instance_id());
        SKLOG(SK_INFO, "instance: mutex '%s' -> '%s'", name, buf);
        return o_CreateMutexA(sa, own, buf);
    }
    return o_CreateMutexA(sa, own, name);
}

void instance_install()
{
    if (const wchar_t* p = wcsstr(GetCommandLineW(), L"-sporekit-profile:")) {
        p += 18;
        while (*p && *p != L' ' && *p != L'"')
            g_profile += *p++;
    }
    if (g_profile.empty() && instance_id() >= 2)
        g_profile = L"inst" + std::to_wstring(instance_id());

    if (!g_profile.empty()) {
        HMODULE sh = LoadLibraryW(L"shell32.dll");
        hook_create(&g_kit_mod, (void*)GetProcAddress(sh, "SHGetFolderPathW"), (void*)h_SHGetFolderPathW,
                    (void**)&o_SHGetFolderPathW);
        SKLOG(SK_INFO, "profile '%s': user data in AppData\\Roaming\\SporeKit-%s\\Spore",
              narrow(g_profile).c_str(), narrow(g_profile).c_str());
    }
    if (instance_id() >= 2) {
        HMODULE k = GetModuleHandleW(L"kernel32.dll");
        hook_create(&g_kit_mod, (void*)GetProcAddress(k, "CreateMutexA"), (void*)h_CreateMutexA,
                    (void**)&o_CreateMutexA);
    }
}

} // namespace sk
