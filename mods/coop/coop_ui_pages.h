// Shared lobby page routing; preparation owns the dialog until it finishes.
#pragma once

namespace coop_ui {

enum class Page { Connect, Lobby, Space, AdventureSettings, Preparation, Editor };

constexpr bool prefer_editor_on_open(bool connected, bool visible, bool active, bool offered, bool can_begin)
{
    return connected && visible && (active || offered || can_begin);
}

constexpr Page select_page(bool lobby, bool preparing, bool settings, bool space, bool editor = false)
{
    if (!lobby) return Page::Connect;
    if (settings) return Page::AdventureSettings;
    if (preparing) return Page::Preparation;
    if (editor) return Page::Editor;
    return space ? Page::Space : Page::Lobby;
}

} // namespace coop_ui
