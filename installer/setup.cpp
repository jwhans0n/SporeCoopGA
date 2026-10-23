// SporeCoopSetup.exe: install, update and uninstall SporeCoop with one button.
// Finds the game (GOG and EA registry keys, the folder the setup runs from, common folders, or
// Browse), tells whether the game build is the tested one, installs the newest release from the
// project's GitHub releases (or the update pack next to the setup, offline) and removes it again.
//
// Silent mode for scripts and tests:
//   SporeCoopSetup.exe /S [/dir:<game folder>] [/pack:<file.pack>] [/api:<releases/latest url>]
//                      [/uninstall] [/log:<file>]       exit code 0 = done
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <string>
#include <vector>

#include "../updater/sha256.h"
#include "../updater/winupdate.h"

#ifndef SETUP_VERSION
#define SETUP_VERSION "dev"
#endif

namespace {

constexpr const char* kTestedSha = "922af7c7908af2614b9caca2ffb1ec2353442022fd124866105724acaa24c26e";
constexpr const char* kGaPreviewSha = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
constexpr UINT WM_DONE = WM_APP + 1, WM_INFO = WM_APP + 2;

struct Options {
    bool silent = false, uninstall = false;
    std::wstring dir, pack, log;
    std::string api = upd::kLatestUrl;
} g_opt;

HWND g_wnd, g_dir, g_game, g_inst, g_latest, g_status, g_install, g_remove;
HFONT g_font, g_bold;
upd::Release g_rel;
bool g_rel_ok;
std::string g_rel_err;
volatile bool g_busy;

bool exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
bool is_game(const std::wstring& root) { return !root.empty() && exists(root + L"\\SporebinEP1\\SporeApp.exe"); }

std::wstring trim_slash(std::wstring p)
{
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/'))
        p.pop_back();
    return p;
}

std::wstring reg_str(HKEY root, const wchar_t* key, const wchar_t* value)
{
    wchar_t b[MAX_PATH * 2] = {};
    DWORD sz = sizeof b;
    for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY})
        if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | view, nullptr, b, &sz) == ERROR_SUCCESS)
            return b;
    return {};
}

std::wstring parent(std::wstring p)
{
    p = trim_slash(p);
    size_t k = p.find_last_of(L"\\/");
    return k == std::wstring::npos ? L"" : p.substr(0, k);
}

// the game folder (the one with SporebinEP1 and Data)
std::wstring find_game()
{
    std::vector<std::wstring> c;
    c.push_back(reg_str(HKEY_LOCAL_MACHINE, L"Software\\GOG.com\\Games\\1948823323", L"PATH"));
    c.push_back(parent(parent(reg_str(HKEY_LOCAL_MACHINE, L"Software\\GOG.com\\Games\\1948823323", L"EXE"))));
    c.push_back(reg_str(HKEY_LOCAL_MACHINE, L"Software\\electronic arts\\spore", L"installloc"));
    c.push_back(reg_str(HKEY_LOCAL_MACHINE, L"Software\\electronic arts\\SPORE(TM)", L"InstallLoc"));
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring here = parent(self);
    c.push_back(here);          // SPORE\SporeCoopSetup.exe
    c.push_back(parent(here));  // SPORE\SporebinEP1\SporeCoopSetup.exe
    for (const wchar_t* d : {L"C:\\GOG Games\\SPORE", L"C:\\Program Files (x86)\\GOG Galaxy\\Games\\SPORE",
                             L"C:\\Program Files (x86)\\GOG.com\\SPORE", L"C:\\Program Files (x86)\\Electronic Arts\\SPORE",
                             L"C:\\Program Files (x86)\\Steam\\steamapps\\common\\Spore"})
        c.push_back(d);
    for (auto& p : c) {
        std::wstring r = trim_slash(p);
        if (is_game(r))
            return r;
    }
    return {};
}

std::string file_sha(const std::wstring& path)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                           nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return {};
    LARGE_INTEGER sz{};
    GetFileSizeEx(f, &sz);
    std::vector<uint8_t> b((size_t)sz.QuadPart);
    DWORD got = 0;
    ReadFile(f, b.data(), (DWORD)b.size(), &got, nullptr);
    CloseHandle(f);
    return upd::sha256_hex(b.data(), got);
}

bool game_running()
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32W e{sizeof e};
    bool found = false;
    for (BOOL ok = Process32FirstW(s, &e); ok; ok = Process32NextW(s, &e))
        if (!lstrcmpiW(e.szExeFile, L"SporeApp.exe"))
            found = true;
    CloseHandle(s);
    return found;
}

void log(const std::string& s)
{
    if (g_opt.log.empty())
        return;
    HANDLE f = CreateFileW(g_opt.log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return;
    std::string l = s + "\r\n";
    DWORD w;
    WriteFile(f, l.data(), (DWORD)l.size(), &w, nullptr);
    CloseHandle(f);
}

// the pack next to the setup (the zip ships one): used when it is as new as GitHub's
std::wstring local_pack()
{
    if (!g_opt.pack.empty())
        return g_opt.pack;
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring dir = parent(self);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\SporeCoop-*.pack").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return {};
    std::wstring p = dir + L"\\" + fd.cFileName;
    FindClose(h);
    return p;
}

bool do_install(const std::wstring& root, std::string& msg)
{
    if (!is_game(root)) {
        msg = "SporeApp.exe is not in " + upd::narrow(root) + "\\SporebinEP1";
        return false;
    }
    upd::apply_pending(root); // an in-game update left files for the next start
    upd::Pack pack;
    std::string err;
    std::wstring lp = local_pack();
    bool have_local = !lp.empty() && upd::read_pack_file(lp, pack, err);
    if (!have_local || (g_rel_ok && upd::compare_versions(g_rel.version, pack.version) > 0)) {
        if (!g_rel_ok && !upd::latest_release(g_opt.api, g_rel, err)) {
            msg = "cannot get SporeCoop from GitHub: " + err;
            return false;
        }
        g_rel_ok = true;
        if (!upd::download_pack(g_rel, pack, err, [](uint64_t got, uint64_t total) {
                if (g_status && total) {
                    wchar_t b[96];
                    swprintf(b, 96, L"Downloading SporeCoop... %u %%", (unsigned)(got * 100 / total));
                    SetWindowTextW(g_status, b);
                }
            })) {
            msg = "download failed: " + err;
            return false;
        }
    }
    if (!upd::apply_pack(root, pack, err)) {
        msg = err;
        return false;
    }
    upd::cleanup_old(root);
    msg = "SporeCoop " + pack.version + " is installed. Start Spore: the galaxy menu has a Co-op item.";
    return true;
}

bool do_uninstall(const std::wstring& root, std::string& msg)
{
    upd::apply_pending(root);
    std::wstring bin = root + L"\\SporebinEP1";
    std::wstring sa = bin + L"\\steam_api.dll", orig = bin + L"\\steam_api_orig.dll";
    if (exists(orig) && upd::is_loader(sa)) {
        std::wstring old = sa + L".old";
        DeleteFileW(old.c_str());
        if (!MoveFileExW(sa.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING) ||
            !MoveFileExW(orig.c_str(), sa.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            msg = "cannot restore steam_api.dll (run as administrator? is Spore closed?)";
            return false;
        }
    }
    for (const wchar_t* f : {L"\\sporekit.dll", L"\\mods\\coop.dll", L"\\mods\\sporecoop.version"})
        if (exists(bin + f) && !DeleteFileW((bin + f).c_str()))
            MoveFileExW((bin + f).c_str(), (bin + f + L".old").c_str(), MOVEFILE_REPLACE_EXISTING); // in use
    WIN32_FIND_DATAW fd;
    std::wstring sym = bin + L"\\mods\\sporekit\\symbols";
    HANDLE h = FindFirstFileW((sym + L"\\*.sym").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            DeleteFileW((sym + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    DeleteFileW((root + L"\\Data\\SporeCoop.package").c_str());
    upd::cleanup_old(root);
    msg = "SporeCoop is removed (your settings and logs in SporebinEP1\\mods are kept).";
    return true;
}

// ---------------- window ----------------
std::wstring dir_text()
{
    wchar_t b[MAX_PATH * 2];
    GetWindowTextW(g_dir, b, MAX_PATH * 2);
    return trim_slash(b);
}

void refresh()
{
    std::wstring root = dir_text();
    std::wstring game;
    if (!is_game(root))
        game = L"Game: SporeApp.exe is not in this folder's SporebinEP1. Choose the SPORE folder.";
    else {
        std::string sha = file_sha(root + L"\\SporebinEP1\\SporeApp.exe");
        if (sha == kGaPreviewSha)
            game = L"Game: GOG March 2017 - GA preview; other stages unvalidated";
        else if (sha == kTestedSha)
            game = L"Game: previously tested GOG build; GA preview needs March 2017";
        else
            game = L"Game: unvalidated executable; native GA preview adapter unavailable";
    }
    SetWindowTextW(g_game, game.c_str());
    std::string v = is_game(root) ? upd::installed_version(root) : "";
    SetWindowTextW(g_inst, upd::widen("Installed: " + (v.empty() ? std::string("not installed") : "SporeCoop " + v)).c_str());
    std::string latest = g_rel_ok ? "Newest release: SporeCoop " + g_rel.version
                         : g_rel_err.empty() ? "Newest release: checking GitHub..."
                                             : "Newest release: GitHub not reachable (" + g_rel_err + ")";
    SetWindowTextW(g_latest, upd::widen(latest).c_str());
    std::string btn = v.empty() ? "Install" : g_rel_ok && upd::compare_versions(g_rel.version, v) > 0 ? "Update to " + g_rel.version : "Reinstall";
    SetWindowTextW(g_install, upd::widen(btn).c_str());
    EnableWindow(g_install, is_game(root) && !g_busy);
    EnableWindow(g_remove, !v.empty() && !g_busy);
}

DWORD WINAPI check_thread(void*)
{
    upd::Release r;
    std::string err;
    bool ok = upd::latest_release(g_opt.api, r, err);
    if (ok)
        g_rel = r;
    g_rel_ok = ok;
    g_rel_err = ok ? "" : err;
    PostMessageW(g_wnd, WM_INFO, 0, 0);
    return 0;
}

struct Job {
    bool uninstall;
    std::wstring root;
    std::string msg;
    bool ok;
};

DWORD WINAPI job_thread(void* p)
{
    auto* j = (Job*)p;
    j->ok = j->uninstall ? do_uninstall(j->root, j->msg) : do_install(j->root, j->msg);
    PostMessageW(g_wnd, WM_DONE, 0, (LPARAM)j);
    return 0;
}

void start_job(bool uninstall)
{
    if (game_running() &&
        MessageBoxW(g_wnd, L"Spore is running. Close it first for a clean install.\n\nContinue anyway?",
                    L"SporeCoop Setup", MB_YESNO | MB_ICONWARNING) != IDYES)
        return;
    g_busy = true;
    SetWindowTextW(g_status, uninstall ? L"Removing SporeCoop..." : L"Installing SporeCoop...");
    refresh();
    auto* j = new Job{uninstall, dir_text(), {}, false};
    CloseHandle(CreateThread(nullptr, 0, job_thread, j, 0, nullptr));
}

void elevate()
{
    wchar_t self[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring args = L"\"/dir:" + dir_text() + L"\"";
    if ((INT_PTR)ShellExecuteW(g_wnd, L"runas", self, args.c_str(), nullptr, SW_SHOWNORMAL) > 32)
        PostQuitMessage(0);
}

HWND add(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id = 0, bool bold = false)
{
    HWND c = CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             x, y, w, h, g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)(bold ? g_bold : g_font), TRUE);
    return c;
}

enum { ID_BROWSE = 10, ID_INSTALL, ID_REMOVE, ID_CLOSE, ID_DIR };

LRESULT CALLBACK wndproc(HWND w, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_CREATE: {
        g_wnd = w;
        NONCLIENTMETRICSW nm{sizeof nm};
        SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof nm, &nm, 0);
        g_font = CreateFontIndirectW(&nm.lfMessageFont);
        LOGFONTW lf = nm.lfMessageFont;
        lf.lfWeight = FW_BOLD;
        lf.lfHeight = lf.lfHeight * 3 / 2;
        g_bold = CreateFontIndirectW(&lf);
        add(L"STATIC", L"SporeCoop " SETUP_VERSION, 0, 20, 14, 520, 28, 0, true);
        add(L"STATIC", L"Galactic Adventures preview. Read GALACTIC-ADVENTURES.md before playing.", 0, 20,
            44, 520, 20);
        add(L"STATIC", L"Game folder (the one with SporebinEP1):", 0, 20, 78, 520, 18);
        g_dir = add(L"EDIT", upd::widen(upd::narrow(find_game())).c_str(), ES_AUTOHSCROLL, 20, 98, 420, 24, ID_DIR);
        add(L"BUTTON", L"Browse...", BS_PUSHBUTTON, 448, 97, 92, 26, ID_BROWSE);
        g_game = add(L"STATIC", L"", 0, 20, 134, 520, 18);
        g_inst = add(L"STATIC", L"", 0, 20, 156, 520, 18);
        g_latest = add(L"STATIC", L"", 0, 20, 178, 520, 18);
        g_install = add(L"BUTTON", L"Install", BS_DEFPUSHBUTTON, 20, 214, 170, 32, ID_INSTALL);
        g_remove = add(L"BUTTON", L"Uninstall", BS_PUSHBUTTON, 200, 214, 120, 32, ID_REMOVE);
        add(L"BUTTON", L"Close", BS_PUSHBUTTON, 440, 214, 100, 32, ID_CLOSE);
        g_status = add(L"STATIC", L"", 0, 20, 258, 520, 36);
        add(L"STATIC", L"github.com/WoodyWizard/SporeCoop  -  every player needs the same SporeCoop version", 0, 20,
            300, 520, 18);
        refresh();
        CloseHandle(CreateThread(nullptr, 0, check_thread, nullptr, 0, nullptr));
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE: {
            BROWSEINFOW bi{};
            bi.hwndOwner = w;
            bi.lpszTitle = L"Choose the SPORE folder (the one with SporebinEP1)";
            bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
            if (LPITEMIDLIST id = SHBrowseForFolderW(&bi)) {
                wchar_t p[MAX_PATH];
                if (SHGetPathFromIDListW(id, p)) {
                    std::wstring r = trim_slash(p);
                    if (!is_game(r) && is_game(parent(r)))
                        r = parent(r); // picked SporebinEP1 itself
                    SetWindowTextW(g_dir, r.c_str());
                }
                CoTaskMemFree(id);
            }
            refresh();
            return 0;
        }
        case ID_DIR:
            if (HIWORD(wp) == EN_CHANGE)
                refresh();
            return 0;
        case ID_INSTALL:
            start_job(false);
            return 0;
        case ID_REMOVE:
            start_job(true);
            return 0;
        case ID_CLOSE:
            DestroyWindow(w);
            return 0;
        }
        break;
    case WM_INFO:
        refresh();
        return 0;
    case WM_DONE: {
        auto* j = (Job*)lp;
        g_busy = false;
        SetWindowTextW(g_status, upd::widen(j->msg).c_str());
        bool denied = !j->ok && j->msg.find("administrator") != std::string::npos;
        refresh();
        if (denied && MessageBoxW(w, L"Windows does not let normal programs change this folder.\n\nRestart the setup as "
                                     L"administrator?", L"SporeCoop Setup", MB_YESNO | MB_ICONQUESTION) == IDYES)
            elevate();
        delete j;
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(w, m, wp, lp);
}

void parse_args()
{
    int n = 0;
    LPWSTR* a = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 1; i < n; i++) {
        std::wstring s = a[i];
        auto val = [&](const wchar_t* k) {
            size_t l = wcslen(k);
            return s.compare(0, l, k) == 0 ? s.substr(l) : std::wstring();
        };
        if (s == L"/S" || s == L"/s")
            g_opt.silent = true;
        else if (s == L"/uninstall")
            g_opt.uninstall = true;
        else if (!val(L"/dir:").empty())
            g_opt.dir = trim_slash(val(L"/dir:"));
        else if (!val(L"/pack:").empty())
            g_opt.pack = val(L"/pack:");
        else if (!val(L"/api:").empty())
            g_opt.api = upd::narrow(val(L"/api:"));
        else if (!val(L"/log:").empty())
            g_opt.log = val(L"/log:");
    }
    LocalFree(a);
}

} // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show)
{
    parse_args();
    if (g_opt.silent) {
        std::wstring root = g_opt.dir.empty() ? find_game() : g_opt.dir;
        std::string msg;
        bool ok = g_opt.uninstall ? do_uninstall(root, msg) : do_install(root, msg);
        log(std::string(ok ? "OK: " : "FAILED: ") + msg);
        return ok ? 0 : 1;
    }
    INITCOMMONCONTROLSEX ic{sizeof ic, ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&ic);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    WNDCLASSW wc{};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.lpszClassName = L"SporeCoopSetup";
    RegisterClassW(&wc);
    RECT r{0, 0, 560, 330};
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    HWND w = CreateWindowW(L"SporeCoopSetup", L"SporeCoop Setup", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                           CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!g_opt.dir.empty())
        SetWindowTextW(g_dir, g_opt.dir.c_str());
    ShowWindow(w, show);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (!IsDialogMessageW(w, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return 0;
}
