// Windows side of the SporeCoop updater, shared by coop.dll (in-game updates) and
// SporeCoopSetup.exe: HTTPS downloads from the project's GitHub releases, checks, and installing a
// pack's files with a rollback. Files in use (the loaded DLLs) are renamed to *.old and replaced;
// the *.old copies are deleted on the next start (cleanup_old).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "pack.h"

namespace upd {

// the GitHub API answer for the newest release (override for tests: [update] url in coop.ini)
constexpr const char* kLatestUrl = "https://api.github.com/repos/jwhans0n/SporeCoopGA/releases/latest";

using Progress = std::function<void(uint64_t got, uint64_t total)>;
bool http_get(const std::string& url, std::vector<uint8_t>& out, std::string& err, const Progress& progress = {},
              uint32_t max_bytes = 96u << 20);

bool latest_release(const std::string& api_url, Release& out, std::string& err);
// download the release's pack, check it against SHA256SUMS.txt of the same release, parse it
bool download_pack(const Release& r, Pack& out, std::string& err, const Progress& progress = {});
bool read_pack_file(const std::wstring& path, Pack& out, std::string& err);

// game_root = the folder with SporebinEP1 and Data. Installs every file of the pack (files with
// kOnlyIfMissing only when absent); on any failure everything replaced so far is put back.
// A file the running game holds open (it cannot be renamed) stays as <file>.new and is listed in
// SporebinEP1\mods\sporecoop.pending: the loader (or apply_pending) puts it in place before the
// next start. *pending = how many files wait for that.
bool apply_pack(const std::wstring& game_root, const Pack& p, std::string& err, int* pending = nullptr);
void apply_pending(const std::wstring& game_root); // the same as the loader, when the game is closed
void cleanup_old(const std::wstring& game_root);

// the version installed in a game folder (SporebinEP1/mods/sporecoop.version), "" if none
std::string installed_version(const std::wstring& game_root);
bool is_loader(const std::wstring& dll); // SporeCoop's steam_api.dll, not the game's own

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);

} // namespace upd
