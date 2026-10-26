#include "coop_ui_pages.h"

#include <cstdio>
#include <initializer_list>

int main()
{
    using coop_ui::Page;
    using coop_ui::select_page;
    int failures = 0;
    auto check = [&](bool passed, const char* message) {
        if (!passed) { std::printf("FAIL: %s\n", message); ++failures; }
    };

    // Losing the party takes precedence over any page remembered from that party.
    for (bool preparing : {false, true})
        for (bool settings : {false, true})
            for (bool space : {false, true})
                check(select_page(false, preparing, settings, space) == Page::Connect,
                      "disconnected party shows connection controls only");
    check(select_page(true, false, false, false) == Page::Lobby, "ordinary party lobby");
    check(select_page(true, false, false, true) == Page::Space, "Space feature page");
    check(select_page(true, true, false, true) == Page::Preparation,
          "new preparation supersedes a remembered Space page");
    check(select_page(true, true, true, true) == Page::AdventureSettings,
          "settings opens from preparation without showing either underlying page");
    check(select_page(true, true, false, false) == Page::Preparation,
          "closing settings restores pending captain preparation");
    check(select_page(true, false, false, false) == Page::Lobby,
          "cancelled preparation restores the party lobby");
    check(select_page(true, false, true, false) == Page::AdventureSettings,
          "adventure difficulty is available before choosing an adventure");
    check(select_page(true, false, false, false, true) == Page::Editor,
          "shared authoring has its own controls");
    check(select_page(false, false, false, false, true) == Page::Connect,
          "editor controls cannot survive losing the party");
    check(select_page(true, true, false, false, true) == Page::Preparation,
          "an editor playtest uses the existing captain preparation view");
    std::printf("UI page routing: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
