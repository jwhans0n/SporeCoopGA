// Symbol database: name -> VA (for preferred image base), selected by exe SHA-256.
#include "core.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace sk {

static std::unordered_map<std::string, uintptr_t> g_syms; // VA at preferred base
static std::map<uintptr_t, std::string> g_by_addr;
static std::string g_hash;
static std::string g_files;
static uintptr_t g_pref_base = 0x400000, g_base = 0x400000;

// On native Windows the loaded optional header can contain the relocated base.
// Symbol files use preferred VAs, so obtain their reference base from the file.
static bool disk_preferred_base(const wchar_t* path, uintptr_t& base)
{
    FILE* file = _wfopen(path, L"rb");
    if (!file)
        return false;
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    bool ok = fread(&dos, sizeof dos, 1, file) == 1 && dos.e_magic == IMAGE_DOS_SIGNATURE &&
              dos.e_lfanew >= (LONG)sizeof dos && fseek(file, dos.e_lfanew, SEEK_SET) == 0 &&
              fread(&nt, sizeof nt, 1, file) == 1 && nt.Signature == IMAGE_NT_SIGNATURE &&
              nt.FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
              nt.FileHeader.SizeOfOptionalHeader >= sizeof(IMAGE_OPTIONAL_HEADER32) &&
              nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC && nt.OptionalHeader.ImageBase != 0;
    fclose(file);
    if (ok)
        base = nt.OptionalHeader.ImageBase;
    return ok;
}

uintptr_t image_base() { return g_base; }
uintptr_t rebase(uintptr_t va) { return va - g_pref_base + g_base; }
std::string exe_sha256() { return g_hash; }
const char* symbols_file_name() { return g_files.c_str(); }
size_t symbols_count() { return g_syms.size(); }

uintptr_t sym(const char* name)
{
    auto it = g_syms.find(name);
    return it == g_syms.end() ? 0 : rebase(it->second);
}

static void load_file(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f)
        return;
    char line[1024];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r')
            continue;
        char* eq = strchr(line, '=');
        if (!eq)
            continue;
        *eq = 0;
        uintptr_t va = (uintptr_t)strtoul(eq + 1, nullptr, 16);
        if (!va)
            continue;
        g_syms[line] = va;
        n++;
    }
    fclose(f);
    SKLOG(SK_INFO, "symbols: %d entries from %s", n, narrow(path).c_str());
}

static std::string file_hash_header(const std::wstring& path)
{
    FILE* f = _wfopen(path.c_str(), L"r");
    if (!f)
        return {};
    char line[512];
    std::string h;
    for (int i = 0; i < 10 && fgets(line, sizeof line, f); i++) {
        const char* p = strstr(line, "exe_sha256=");
        if (p) {
            p += 11;
            while (*p && *p != '\n' && *p != '\r' && *p != ' ')
                h += *p++;
            break;
        }
    }
    fclose(f);
    return h;
}

bool symbols_load()
{
    g_syms.clear();
    g_by_addr.clear();
    g_files.clear();
    HMODULE exe = GetModuleHandleW(nullptr);
    g_base = (uintptr_t)exe;
    wchar_t exe_path[MAX_PATH];
    DWORD length = GetModuleFileNameW(exe, exe_path, MAX_PATH);
    if (!length || length >= MAX_PATH || !disk_preferred_base(exe_path, g_pref_base)) {
        SKLOG(SK_ERROR, "could not read the executable's preferred image base; game-specific symbols are disabled");
        return false;
    }
    double t0 = now_seconds();
    g_hash = sha256_file_hex(exe_path);
    SKLOG(SK_INFO, "exe %s sha256=%s base=0x%08X (preferred 0x%08X) hashed in %.2fs",
          narrow(exe_path).c_str(), g_hash.c_str(), (unsigned)g_base, (unsigned)g_pref_base,
          now_seconds() - t0);

    std::wstring dir = mods_dirw() + L"\\sporekit\\symbols\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.sym").c_str(), &fd);
    std::vector<std::wstring> normal, overrides;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring p = dir + fd.cFileName;
            if (file_hash_header(p) != g_hash)
                continue;
            if (wcsncmp(fd.cFileName, L"overrides", 9) == 0)
                overrides.push_back(p);
            else
                normal.push_back(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(normal.begin(), normal.end());
    std::sort(overrides.begin(), overrides.end());
    for (auto& p : normal) {
        load_file(p);
        g_files += narrow(p.substr(dir.size())) + " ";
    }
    for (auto& p : overrides) {
        load_file(p);
        g_files += narrow(p.substr(dir.size())) + " ";
    }
    for (auto& [n, a] : g_syms)
        g_by_addr[a] = n;
    if (g_syms.empty()) {
        SKLOG(SK_ERROR, "no symbol database for this SporeApp.exe build (%s); "
                        "game-specific features are disabled",
              g_hash.c_str());
        return false;
    }
    return true;
}

std::vector<std::pair<std::string, uintptr_t>> symbols_find(const std::string& needle, size_t limit)
{
    std::vector<std::pair<std::string, uintptr_t>> out;
    std::string ln = needle;
    std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
    for (auto& [a, n] : g_by_addr) {
        std::string l = n;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        if (l.find(ln) != std::string::npos) {
            out.emplace_back(n, rebase(a));
            if (out.size() >= limit)
                break;
        }
    }
    return out;
}

std::string symbols_reverse(uintptr_t addr)
{
    uintptr_t va = addr - g_base + g_pref_base;
    auto it = g_by_addr.upper_bound(va);
    if (it == g_by_addr.begin())
        return {};
    --it;
    uintptr_t off = va - it->first;
    if (off > 0x4000)
        return {};
    char b[32];
    snprintf(b, sizeof b, "+0x%X", (unsigned)off);
    return it->second + (off ? b : "");
}

} // namespace sk
