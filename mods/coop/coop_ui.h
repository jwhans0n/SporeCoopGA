// Co-op user interface built from Spore's own UI: a "Co-op" item in the main menu and a
// game dialog with "connect" (name, address, games on the network, hosting),
// "lobby" (players, stage, ping, ready, chat, world transfer) and, in the Space stage, "space"
// (the rows of the space features, space_ext.h), plus adventure preparation and settings.
// docs/08-ui.md.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sporekit.h"
#include "net/adventure.h"

namespace coop_ui {

struct PlayerRow {
    uint8_t id = 0;
    std::string name;
    std::string stage; // "Creature", "Space, other world", ...
    bool host = false, me = false, ready = false;
    int ping_ms = -1;
};

struct LanGame {
    std::string name, address, info;
    int players = 0, max_players = 0;
};
struct AssetCard {
    std::string name, author;
    dbpf::Key image;
};

// what the UI needs from the mod (filled in coop.cpp)
struct Api {
    std::function<std::string()> name;
    std::function<void(const std::string&)> set_name;
    std::function<bool(const std::string& address, std::string& err)> join;
    std::function<bool(uint16_t port, std::string& err)> host;
    std::function<void()> leave;
    std::function<int()> role;        // 0 offline, 1 host, 2 guest
    std::function<bool()> connected;
    std::function<std::string()> session_line;
    std::function<std::vector<PlayerRow>()> players;
    std::function<std::string()> world_line; // transfer progress / hints, "" if nothing
    std::function<bool()> ready;
    std::function<bool()> party_waiting;
    std::function<void(bool)> set_ready;
    std::function<void()> share_world; // host (during a vote: start the evolution now)
    std::function<bool()> vote_active;
    // Adventure difficulty is visible in the menu and during adventures. Only the host
    // may edit it, before an attempt. Guests see the same immutable launch settings.
    std::function<bool(ga::Difficulty&, int& party_size, bool& editable, std::string& status)> adventure_difficulty;
    std::function<void(const ga::Difficulty&)> set_adventure_difficulty;
    std::function<bool(ga::PreparationView&)> preparation;
    std::function<AssetCard(const dbpf::Key&)> asset_card;
    std::function<dbpf::Key(uint8_t)> captain_key;
    std::function<bool()> can_ready;
    std::function<void()> change_captain, cancel_adventure;
    // own creation of the current stage: kind 0 creature / 1 cell / 2 ship, its name ("" = the
    // world's own), how many own creations of that kind exist; false = not shown. cycle: -1 / +1
    std::function<bool(int& kind, std::string& name, int& own)> creation;
    std::function<void(int)> creation_cycle;
    // tribe / civilization: how many own units are selected (what: "member", "vehicle", "city"),
    // the partner they would go to; give_do gives them
    std::function<int(std::string& what, std::string& to)> give;
    std::function<void()> give_do;
    // space: travel to a partner's star system (false = not possible now, else whose)
    std::function<bool(std::string& to)> jump;
    std::function<void()> jump_do;
    // a SporeCoop update (autoupdate.h): the line and the button text ("" = no button)
    std::function<bool(std::string& line, std::string& button)> update;
    std::function<void()> update_do;
    // civilization: the own role (0 none, 1 military, 2 economic, 3 religious); role_cycle(+1/-1)
    std::function<int()> civ_role;
    std::function<void(int)> civ_role_cycle;
    std::function<void(const std::string&)> say;
    std::function<std::vector<std::string>()> chat;
    std::function<std::vector<LanGame>()> lan_games;
    std::function<void(bool)> lan_scan; // start/stop looking for games on the network
    std::function<std::vector<std::string>()> local_addresses;
    std::function<uint32_t()> mode; // game mode id
    std::function<std::string()> version_line; // "SporeCoop 0.7.2.1  -  GOG 2024" (window footer)
    std::function<std::string()> report;       // writes a problem report, returns its path ("" = cannot)
    uint16_t default_port = 0;
};

void init(const SkHost* host, SkMod* mod, Api api);
void tick();     // every frame (inside Present): only schedules UI work outside the frame
void open(bool on);
// Outside-frame launch transition: hide before entering the native loading UI.
void close_for_adventure_launch();
bool is_open();
void shutdown(); // mod unload: hide our windows
std::string debug_state();
extern bool g_log_msgs; // debug: log UI messages reaching our handlers

// the dialog text font (0 until the co-op window was built once)
uint32_t text_font();
// the window speaks Russian (ui lang = ru, or auto on a Russian system)
bool russian();

} // namespace coop_ui
