// Profiles and multi-instance support.
//
//  -sporekit-profile:<name>   user data (saves, preferences, caches) goes to
//                             AppData\Roaming\SporeKit-<name>\Spore instead of the real
//                             profile, and Documents (My Spore Creations) to
//                             AppData\Roaming\SporeKit-<name>\Documents, so automated tests never
//                             touch the player's data. -sporekit-profile-docs:0: AppData only.
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

static bool g_redirect_docs = true; // -sporekit-profile-docs:0 = only AppData (the old behaviour, tests)

// The game's per-user folders that a profile redirects: AppData (saves, preferences, caches) and,
// since BUG-0156, Documents ("My Spore Creations": creation PNGs, screenshots, videos) with the
// picture / video / music folders. Without it a test on lane 1 (the owner's own bottle) wrote creations and
// screenshots into the owner's Documents.
static const wchar_t* docs_subfolder(int id)
{
    switch (id) {
    case CSIDL_PERSONAL: return L"Documents";
    case CSIDL_MYPICTURES: return L"Documents\\Pictures";
    case CSIDL_MYVIDEO: return L"Documents\\Videos";
    case CSIDL_MYMUSIC: return L"Documents\\Music";
    default: return nullptr;
    }
}

static HRESULT WINAPI h_SHGetFolderPathW(HWND h, int csidl, HANDLE tok, DWORD flags, LPWSTR out)
{
    int id = csidl & 0xFF;
    const wchar_t* docs = g_redirect_docs && !g_profile.empty() ? docs_subfolder(id) : nullptr;
    if (docs && out) {
        // <AppData>\SporeKit-<name>\Documents...: from the real AppData, whatever Documents is
        HRESULT r = o_SHGetFolderPathW(h, CSIDL_APPDATA | (csidl & ~0xFF), tok, flags, out);
        if (SUCCEEDED(r)) {
            std::wstring p = std::wstring(out) + L"\\SporeKit-" + g_profile;
            CreateDirectoryW(p.c_str(), nullptr);
            p += L"\\Documents";
            CreateDirectoryW(p.c_str(), nullptr);
            if (wcscmp(docs, L"Documents") != 0) {
                p = std::wstring(out) + L"\\SporeKit-" + g_profile + L"\\" + docs;
                CreateDirectoryW(p.c_str(), nullptr);
            }
            wcsncpy(out, p.c_str(), MAX_PATH - 1);
            out[MAX_PATH - 1] = 0;
            static bool logged;
            if (!logged) {
                logged = true;
                SKLOG(SK_INFO, "profile '%s': Documents in %s", narrow(g_profile).c_str(), narrow(p).c_str());
            }
        }
        return r;
    }
    HRESULT r = o_SHGetFolderPathW(h, csidl, tok, flags, out);
    if (SUCCEEDED(r) && out && id == CSIDL_APPDATA && !g_profile.empty()) {
        std::wstring p = std::wstring(out) + L"\\SporeKit-" + g_profile;
        CreateDirectoryW(p.c_str(), nullptr);
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
    if (wcsstr(GetCommandLineW(), L"-sporekit-profile-docs:0"))
        g_redirect_docs = false;
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
