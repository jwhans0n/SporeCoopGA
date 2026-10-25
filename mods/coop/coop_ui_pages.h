// Shared lobby page routing; preparation owns the dialog until it finishes.
#pragma once

namespace coop_ui {

enum class Page { Connect, Lobby, Space, AdventureSettings, Preparation };

constexpr Page select_page(bool lobby, bool preparing, bool settings, bool space)
{
    if (!lobby) return Page::Connect;
    if (settings) return Page::AdventureSettings;
    if (preparing) return Page::Preparation;
    return space ? Page::Space : Page::Lobby;
}

} // namespace coop_ui
