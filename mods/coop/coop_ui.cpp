#include "coop_ui.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <tuple>

#include "modes.h"
#include "sui.h"

namespace coop_ui {

static uint32_t g_text_font;

using sui::Rect;
using sui::Win;

static const SkHost* H;
static SkMod* M;
static Api A;
static bool g_ok;      // Spore UI available
static bool g_ru;      // Russian captions
static bool g_want_open, g_pending, g_dirty = true;
static double g_last_refresh;
static std::string g_status;
static uint32_t g_status_color = 0xFFFFD866;
static bool g_f9_down;
static bool g_raise = true; // move the window above all UI layers when it opens
static bool g_adventure_page;
static uint64_t g_last_preparation_run;
bool g_log_msgs; // debug: log UI messages that reach our handlers

constexpr uint32_t kIdMenuButton = 0xC00F0001, kIdDialog = 0xC00F0100;
constexpr uint32_t kIdMainMenu = 0x0615A5FF, kIdShare = 0x0615B616;
constexpr uint32_t kDialogLayout = 0x34A6339E; // the game's account dialog (frame, logo, close)
constexpr uint32_t kGold = 0xFFFFCC4D, kWhite = 0xFFFFFFFF, kDim = 0xFF9DB8CC, kGreen = 0xFF8CF08C,
                   kRed = 0xFFFF7A6A, kYellow = 0xFFFFD866;
constexpr float kW = 760, kH = 560;

static const char* T(const char* en, const char* ru) { return g_ru ? ru : en; }

// ---------------- widgets with cached state ----------------
struct Cache {
    std::string text;
    uint32_t color = 0;
    int vis = -1;
    bool left = false; // left aligned label: the box is sized to the text (labels center their text)
    Rect box;
};
static std::map<Win, Cache> g_cache;

// width of a text in the dialog font (average glyph width, measured at 1080p)
static float text_width(const std::string& t)
{
    int n = 0;
    for (unsigned char ch : t)
        n += (ch & 0xC0) != 0x80;
    return n * 9.3f + 30;
}

static void fit(Win w, const Cache& c)
{
    Rect r = c.box;
    r.x2 = std::min(r.x2, r.x1 + text_width(c.text));
    sui::set_area(w, r);
}

static void text(Win w, const std::string& t)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.text != t) {
        c.text = t;
        sui::set_caption(w, t);
        if (c.left)
            fit(w, c);
    }
}
static void color(Win w, uint32_t argb)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.color != argb) {
        c.color = argb;
        sui::set_label_color(w, argb);
    }
}
static void show(Win w, bool on)
{
    if (!w)
        return;
    auto& c = g_cache[w];
    if (c.vis != (int)on) {
        c.vis = on;
        sui::set_visible(w, on);
    }
}

// ---------------- main menu item ----------------
static Win g_menu_btn, g_menu_parent;
static void* g_menu_drawable;
static Win g_menu_drawable_for; // the menu window the drawable was made for

static void on_menu_click() { open(true); }

static void ensure_menu_button()
{
    Win root = sui::main_window();
    Win menu = A.mode && A.mode() == modes::kMenu ? sui::find(root, kIdMainMenu) : nullptr;
    if (!menu) {
        g_menu_btn = g_menu_parent = nullptr;
        return;
    }
    if (g_menu_btn && g_menu_parent == menu)
        return;
    Win share = sui::find(menu, kIdShare);
    if (!share)
        return;
    // a stale item from before a hot reload: remove it, a fresh one gets our handler
    if (Win old = sui::find(menu, kIdMenuButton))
        sui::remove_child(menu, old);
    Rect a = sui::area(share);
    float dy = 95; // below "Galactic Adventures"
    Rect r{a.x1, a.y1 + dy, a.x2, a.y2 + dy};
    // built detached and added to the menu last: changing the drawable of a window that is already
    // in the galaxy menu made the game render its planet view synchronously from here (outside the
    // frame), and DXVK deadlocked the main thread right after a stage was left (frames stopped)
    Win b = sui::button_like(nullptr, share, r, T("Co-op", "Кооператив"));
    if (!b)
        return;
    sui::set_ids(b, kIdMenuButton, kIdMenuButton);
    // a fresh drawable for every galaxy menu: it is a copy of the "Share" button's drawable (its
    // images and current state), and the copy from an earlier menu, set on the new button after a
    // few stage switches, hung the main thread inside SetDrawable (a render from the planet view
    // waiting in DXVK, docs/known-issues.md history). The old one stays with the old button.
    if (g_menu_drawable_for != menu) {
        g_menu_drawable = nullptr;
        g_menu_drawable_for = menu;
    }
    if (!g_menu_drawable) {
        void* icon = sui::image(sui::fnv("coop_icon"), 0x2F7D0004, sui::fnv("sporecoop"));
        g_menu_drawable = sui::std_drawable_with_icon(sui::drawable(share), icon);
        if (!icon)
            H->log(M, SK_WARN, "ui: coop_icon not found (Data/SporeCoop.package missing?)");
    }
    if (g_menu_drawable)
        sui::set_drawable(b, g_menu_drawable);
    sui::add_child(menu, b);
    sui::add_handler(b, [](Win, const sui::Msg& m) {
        if (g_log_msgs && m.type != 8 && m.type != 0xC && m.type != 0xD)
            H->log(M, SK_DEBUG, "ui msg 0x%X src=%p %08X %08X %08X %08X", m.type, m.source, m.a, m.b, m.c, m.d);
        if (sui::is_click(m)) {
            on_menu_click();
            return true;
        }
        return false;
    });
    g_menu_btn = b;
    g_menu_parent = menu;
    H->log(M, SK_INFO, "ui: co-op item added to the main menu");
}

// ---------------- dialog ----------------
struct Dialog {
    Win root = nullptr, layer = nullptr;
    uint32_t font_body = 0, font_button = 0;
    Win like_button = nullptr; // gold "Login" button of the layout
    Win subtitle = nullptr;
    // connect page
    std::vector<Win> page_connect;
    Win name_edit = nullptr, addr_edit = nullptr, port_edit = nullptr, join_btn = nullptr, host_btn = nullptr;
    Win lan_hdr = nullptr, host_hint = nullptr, status = nullptr, update_btn = nullptr;
    Win lan_rows[3] = {};
    std::string lan_addr[3];
    // lobby page
    std::vector<Win> page_lobby;
    Win session = nullptr, world = nullptr, chat_edit = nullptr, send_btn = nullptr;
    Win ready_btn = nullptr, share_btn = nullptr, leave_btn = nullptr;
    Win row_name[4] = {}, row_stage[4] = {}, row_ping[4] = {}, row_ready[4] = {};
    Win chat_lines[5] = {};
    Win creation = nullptr, creation_prev = nullptr, creation_next = nullptr, give_btn = nullptr, jump_btn = nullptr;
    std::vector<Win> page_adventure;
    Win adventure_death = nullptr, adventure_health = nullptr, adventure_damage = nullptr;
    Win adventure_fire = nullptr, adventure_status = nullptr;
    std::vector<Win> adventure_controls;
    struct Picture { Win window = nullptr; dbpf::Key key; };
    std::vector<Win> page_preparation;
    Win prep_title = nullptr, prep_author = nullptr, prep_hint = nullptr, prep_no_preview = nullptr;
    Win prep_player[4] = {}, prep_captain[4] = {}, prep_stage[4] = {}, prep_ready[4] = {}, prep_placeholder[4] = {};
    Win prep_ready_btn = nullptr, prep_change = nullptr, prep_chat[2] = {}, prep_edit = nullptr;
    Picture adventure_picture, captain_picture[4];
};
static Dialog D;
static std::map<std::string, Win> g_named; // for tests (coop.uistate)
static std::map<std::tuple<uint32_t,uint32_t,uint32_t>, AssetCard> g_cards;

static AssetCard card(const dbpf::Key& key)
{
    if (!key.instance) return {};
    auto id = std::tuple{key.instance,key.type,key.group};
    auto found = g_cards.find(id);
    if (found != g_cards.end()) return found->second;
    if (g_cards.size() >= 256) g_cards.clear();
    return g_cards[id] = A.asset_card ? A.asset_card(key) : AssetCard{};
}
static std::string short_text(const std::string& text, size_t characters)
{
    size_t total = 0;
    for (uint8_t c : text) if ((c & 0xC0) != 0x80) ++total;
    if (total <= characters) return text;
    characters = characters > 3 ? characters - 3 : 0;
    size_t count = 0, offset = 0;
    for (; offset < text.size(); ++offset) if ((uint8_t(text[offset]) & 0xC0) != 0x80 && ++count > characters) break;
    return offset == text.size() ? text : text.substr(0,offset) + "...";
}
static void picture(Dialog::Picture& p, const dbpf::Key& key, const Rect& rect, bool visible)
{
    if (!(p.key == key)) {
        if (p.window) { g_cache.erase(p.window); sui::remove_child(D.root,p.window); p.window = nullptr; }
        p.key = key;
        if (key.instance) p.window = sui::image_window(D.root,key.instance,key.group,rect.x1,rect.y1);
        if (p.window) { sui::set_area(p.window,rect); sui::set_flag(p.window,sui::kFlagIgnoreMouse,true); }
    }
    show(p.window,visible);
}

static Win add_label(std::vector<Win>& page, const Rect& r, const std::string& t, uint32_t argb,
                     int align = sui::kAlignLeft)
{
    Win w = sui::label(D.root, r, t, D.font_body, argb, align);
    if (!w)
        return nullptr;
    page.push_back(w);
    Cache c{t, argb, 1, align == sui::kAlignLeft, r};
    if (c.left)
        fit(w, c);
    g_cache[w] = c;
    return w;
}

static Win add_button(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t,
                      std::function<void()> fn)
{
    Win w = sui::button_like(D.root, D.like_button, r, t);
    if (!w)
        return nullptr;
    sui::set_font(w, D.font_body); // the dialog text font reads better than the small button font
    page.push_back(w);
    g_named[name] = w;
    g_cache[w] = Cache{t, 0, 1};
    sui::add_handler(w, [fn, name = std::string(name)](Win, const sui::Msg& m) {
        if (g_log_msgs && m.type != 8 && m.type != 0xC && m.type != 0xD)
            H->log(M, SK_DEBUG, "ui msg %s 0x%X src=%p %08X %08X %08X", name.c_str(), m.type, m.source, m.a, m.b, m.c);
        if (sui::is_click(m)) {
            fn();
            return true;
        }
        return false;
    });
    return w;
}

static Win add_edit(std::vector<Win>& page, const char* name, const Rect& r, const std::string& t, int max_len)
{
    Win w = sui::text_edit(D.root, r, t, D.font_body, max_len);
    if (w) {
        page.push_back(w);
        g_named[name] = w;
    }
    return w;
}

static void set_status(const std::string& s, uint32_t c)
{
    g_status = s;
    g_status_color = c;
    g_dirty = true;
}

static void save_str(const char* section, const char* key, const std::string& v)
{
    if (H->config_set_str)
        H->config_set_str(M, section, key, v.c_str());
}
static std::string load_str(const char* section, const char* key, const char* def)
{
    char b[256];
    H->config_str(M, section, key, def, b, sizeof b);
    return b;
}

static void apply_name()
{
    std::string n = sui::edit_text(D.name_edit);
    if (!n.empty() && n != A.name()) {
        A.set_name(n);
        save_str("coop", "name", n);
    }
}

static void do_join(const std::string& address)
{
    apply_name();
    std::string addr = address;
    while (!addr.empty() && addr.back() == ' ')
        addr.pop_back();
    if (addr.empty()) {
        set_status(T("Enter the host address, e.g. 192.168.1.5:47810", "Введите адрес хоста, например 192.168.1.5:47810"),
                   kRed);
        return;
    }
    std::string err;
    if (!A.join(addr, err)) {
        set_status(std::string(T("Cannot join: ", "Не удалось подключиться: ")) + err, kRed);
        return;
    }
    save_str("ui", "last_address", addr);
    set_status(std::string(T("Connecting to ", "Подключение к ")) + addr + "...", kYellow);
}

static void do_host()
{
    apply_name();
    int port = atoi(sui::edit_text(D.port_edit).c_str());
    if (port <= 0 || port > 65535)
        port = A.default_port;
    std::string err;
    if (!A.host((uint16_t)port, err)) {
        set_status(std::string(T("Cannot host: ", "Не удалось создать игру: ")) + err, kRed);
        return;
    }
    save_str("ui", "host_port", std::to_string(port));
    set_status("", kYellow);
}

static void do_send_chat()
{
    std::string t = sui::edit_text(D.chat_edit);
    if (t.empty())
        return;
    A.say(t);
    sui::set_edit_text(D.chat_edit, "");
    g_dirty = true;
}

static bool adventure_view(ga::Difficulty& difficulty, int& party, bool& editable, std::string& status)
{
    return A.adventure_difficulty && A.adventure_difficulty(difficulty, party, editable, status);
}

static void change_adventure(int field, int direction)
{
    ga::Difficulty difficulty;
    int party = 1;
    bool editable = false;
    std::string status;
    if (!adventure_view(difficulty, party, editable, status) || !editable || !A.set_adventure_difficulty)
        return;
    if (field == 0)
        difficulty.death_mode = static_cast<ga::DeathMode>((static_cast<int>(difficulty.death_mode) + 1) % 3);
    else if (field == 1)
        difficulty.enemy_health_override = std::clamp(difficulty.health_multiplier(party) + direction * 0.25f, 0.25f, 10.f);
    else if (field == 2)
        difficulty.enemy_damage_override = std::clamp(difficulty.damage_multiplier() + direction * 0.25f, 0.25f, 10.f);
    else if (field == 3) {
        const uint8_t values[] = {0, 25, 50, 100};
        int next = 0;
        while (next < 4 && values[next] != difficulty.friendly_fire_percent)
            ++next;
        difficulty.friendly_fire_percent = values[(next + 1) % 4];
    } else {
        difficulty.enemy_health_override = 0;
        difficulty.enemy_damage_override = 0;
    }
    A.set_adventure_difficulty(difficulty);
    g_dirty = true;
}

static void build_dialog()
{
    Win root = sui::find(sui::main_window(), kIdDialog);
    if (root) { // left over from before a hot reload: its handlers are dead
        if (Win p = sui::parent(root))
            sui::remove_child(p, root);
    }
    root = sui::load_layout(kDialogLayout);
    if (!root) {
        H->log(M, SK_ERROR, "ui: cannot load the dialog layout %08X", kDialogLayout);
        return;
    }
    D = Dialog{};
    g_named.clear();
    D.root = root;
    D.layer = sui::parent(root);
    sui::add_ref(root); // ours: the window survives mode changes that clear the layer
    sui::set_ids(root, kIdDialog, 0);
    Win text_win = sui::find(root, 0x0451D381), login = sui::find(root, 0x0519E168);
    D.font_body = sui::font(text_win);
    g_text_font = D.font_body;
    H->log(M, SK_DEBUG, "ui: dialog font %08X", D.font_body);
    D.like_button = login;
    D.font_button = sui::font(login);
    // keep the frame, glow, logo and close button; hide the account dialog's own content
    for (uint32_t id : {0x0451D381u, 0x0519E168u, 0x06555818u, 0x065D20A8u, 0x0519D9B8u, 0x0451A4C0u})
        if (Win w = sui::find(root, id))
            sui::set_visible(w, false);
    if (Win x = sui::find(root, 0xFFFFFFF2u)) {
        g_named["close"] = x;
        sui::add_handler(x, [](Win, const sui::Msg& m) {
            if (sui::is_click(m)) {
                open(false);
                return true;
            }
            return false;
        });
    }
    int sw = 0, sh = 0;
    H->screen_size(&sw, &sh);
    Rect a{(sw - kW) / 2, (sh - kH) / 2, (sw + kW) / 2, (sh + kH) / 2};
    sui::set_area(root, a);

    std::vector<Win> common;
    D.subtitle = add_label(common, {0, 70, kW, 96}, T("CO-OP", "КООПЕРАТИВ"), kGold, sui::kAlignCenter);

    // ---- connect page ----
    auto& P = D.page_connect;
    add_label(P, {40, 112, 225, 140}, T("Your name", "Ваше имя"), kWhite);
    D.name_edit = add_edit(P, "name", {230, 108, 520, 140}, A.name(), 24);
    add_label(P, {40, 158, 720, 186}, T("Join a friend's game", "Подключиться к другу"), kGold);
    add_label(P, {40, 198, 225, 226}, T("Host address", "Адрес хоста"), kWhite);
    D.addr_edit = add_edit(P, "address", {230, 194, 520, 226}, load_str("ui", "last_address", ""), 64);
    D.join_btn = add_button(P, "join", {540, 190, 720, 230}, T("Join", "Подключиться"),
                            [] { do_join(sui::edit_text(D.addr_edit)); });
    D.lan_hdr = add_label(P, {40, 242, 720, 268}, "", kDim);
    for (int i = 0; i < 3; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "lan%d", i);
        D.lan_rows[i] = add_button(P, nm, {40, 272.f + i * 38, 720, 306.f + i * 38}, "", [i] {
            if (!D.lan_addr[i].empty()) {
                sui::set_edit_text(D.addr_edit, D.lan_addr[i]);
                do_join(D.lan_addr[i]);
            }
        });
    }
    add_label(P, {40, 390, 720, 418}, T("Host a game", "Создать игру"), kGold);
    add_label(P, {40, 430, 225, 458}, T("Port (UDP)", "Порт (UDP)"), kWhite);
    D.port_edit = add_edit(P, "port", {230, 426, 330, 458},
                           load_str("ui", "host_port", std::to_string(A.default_port).c_str()), 5);
    D.host_btn = add_button(P, "host", {540, 422, 720, 462}, T("Host", "Создать"), do_host);
    D.host_hint = add_label(P, {40, 466, 720, 492}, "", kDim);
    D.status = add_label(P, {40, 498, 560, 526}, "", kYellow);
    D.update_btn = add_button(P, "update", {570, 494, 720, 530}, "", [] {
        if (A.update_do)
            A.update_do();
        g_dirty = true;
    });

    // ---- lobby page ----
    auto& L = D.page_lobby;
    D.session = add_label(L, {40, 104, 720, 130}, "", kWhite);
    add_label(L, {40, 138, 300, 162}, T("Player", "Игрок"), kDim);
    add_label(L, {300, 138, 540, 162}, T("Stage", "Этап"), kDim);
    add_label(L, {540, 138, 620, 162}, T("Ping", "Пинг"), kDim);
    add_label(L, {620, 138, 720, 162}, T("Ready", "Готов"), kDim);
    for (int i = 0; i < 4; i++) {
        float y = 166.f + i * 28;
        D.row_name[i] = add_label(L, {40, y, 300, y + 26}, "", kWhite);
        D.row_stage[i] = add_label(L, {300, y, 540, y + 26}, "", kWhite);
        D.row_ping[i] = add_label(L, {540, y, 620, y + 26}, "", kWhite);
        D.row_ready[i] = add_label(L, {620, y, 720, y + 26}, "", kWhite);
    }
    D.world = add_label(L, {40, 284, 720, 310}, "", kYellow);
    for (int i = 0; i < 4; i++)
        D.chat_lines[i] = add_label(L, {40, 316.f + i * 23, 720, 338.f + i * 23}, "", kWhite);
    // the own creation of this stage: "My creature: Kirgle  < >"
    D.creation = add_label(L, {40, 408, 560, 434}, "", kGold);
    D.creation_prev = add_button(L, "cprev", {570, 404, 640, 434}, "<", [] {
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle)
            A.civ_role_cycle(-1);
        else if (A.creation_cycle)
            A.creation_cycle(-1);
        g_dirty = true;
    });
    D.jump_btn = add_button(L, "jump", {400, 404, 560, 434}, "", [] {
        if (A.jump_do)
            A.jump_do();
        g_dirty = true;
    });
    D.give_btn = add_button(L, "give", {570, 404, 720, 434}, "", [] {
        if (A.give_do)
            A.give_do();
        g_dirty = true;
    });
    D.creation_next = add_button(L, "cnext", {650, 404, 720, 434}, ">", [] {
        if (A.mode && A.mode() == 0x1654C04 && A.civ_role_cycle)
            A.civ_role_cycle(1);
        else if (A.creation_cycle)
            A.creation_cycle(1);
        g_dirty = true;
    });
    D.chat_edit = add_edit(L, "chatedit", {40, 438, 560, 470}, "", 120);
    D.send_btn = add_button(L, "send", {570, 434, 720, 474}, T("Send", "Отправить"), do_send_chat);
    D.ready_btn = add_button(L, "ready", {40, 488, 240, 528}, "", [] {
        A.set_ready(!A.ready());
        g_dirty = true;
    });
    D.share_btn = add_button(L, "share", {260, 488, 480, 528}, T("Send my world", "Отправить мир"), [] {
        ga::Difficulty difficulty;
        int party = 1;
        bool editable = false;
        std::string status;
        if (adventure_view(difficulty, party, editable, status)) {
            g_adventure_page = true;
            g_dirty = true;
            return;
        }
        A.share_world();
        set_status(T("Sending the world to everyone...", "Мир отправляется всем..."), kYellow);
    });
    D.leave_btn = add_button(L, "leave", {500, 488, 720, 528}, "", [] {
        A.leave();
        set_status(T("Left the session", "Вы вышли из игры"), kYellow);
    });
    auto& G = D.page_adventure;
    add_label(G, {40, 108, 720, 136}, "Death rules", kGold);
    D.adventure_death = add_button(G, "adventure_death", {40, 142, 720, 180}, "", [] { change_adventure(0, 1); });
    D.adventure_controls.push_back(D.adventure_death);
    add_label(G, {40, 186, 720, 212}, "Revive: within 2 m for 3 s. Automatic recovery: 30 s in place.", kDim);
    add_label(G, {40, 214, 720, 240}, "The attempt always fails when the entire party is downed.", kDim);
    add_label(G, {40, 248, 720, 274}, "Enemy stats", kGold);
    D.adventure_health = add_label(G, {40, 284, 540, 312}, "", kWhite);
    D.adventure_damage = add_label(G, {40, 326, 540, 354}, "", kWhite);
    for (int field = 1; field <= 2; ++field) {
        float y = field == 1 ? 280.f : 322.f;
        for (int direction : {-1, 1}) {
            std::string name = std::string("adventure_") + (field == 1 ? "health" : "damage") + (direction < 0 ? "_less" : "_more");
            float x = direction < 0 ? 570.f : 650.f;
            D.adventure_controls.push_back(add_button(G, name.c_str(), {x, y, x + 70, y + 34}, direction < 0 ? "-" : "+",
                                                      [field, direction] { change_adventure(field, direction); }));
        }
    }
    D.adventure_fire = add_button(G, "adventure_fire", {40, 366, 440, 404}, "", [] { change_adventure(3, 1); });
    D.adventure_controls.push_back(D.adventure_fire);
    D.adventure_controls.push_back(add_button(G, "adventure_reset", {460, 366, 720, 404}, "Use party scaling", [] { change_adventure(4, 1); }));
    add_label(G, {40, 410, 720, 436}, "Settings are fixed during play. Friendly fire affects party captains.", kDim);
    D.adventure_status = add_label(G, {40, 444, 720, 478}, "", kYellow);
    add_button(G, "adventure_back", {40, 488, 240, 528}, "Back to lobby", [] { g_adventure_page = false; g_dirty = true; });
    auto& R = D.page_preparation;
    D.prep_no_preview = add_label(R,{40,106,112,174},"No preview",kDim,sui::kAlignCenter);
    D.prep_title = add_label(R,{126,108,720,136},"",kGold);
    D.prep_author = add_label(R,{126,138,720,162},"",kDim);
    D.prep_hint = add_label(R,{40,178,720,204},"",kYellow);
    for (int i=0;i<4;++i) {
        float x=40.f+i*172.f;
        D.prep_player[i]=add_label(R,{x,210,x+164,236},"",kGold,sui::kAlignCenter);
        D.prep_placeholder[i]=add_label(R,{x,252,x+164,310},"No portrait",kDim,sui::kAlignCenter);
        D.prep_captain[i]=add_label(R,{x,338,x+164,364},"",kWhite,sui::kAlignCenter);
        D.prep_stage[i]=add_label(R,{x,366,x+164,392},"",kDim,sui::kAlignCenter);
        D.prep_ready[i]=add_label(R,{x,394,x+164,420},"",kYellow,sui::kAlignCenter);
    }
    for (int i=0;i<2;++i) D.prep_chat[i]=add_label(R,{40,422.f+i*21,720,442.f+i*21},"",kWhite,sui::kAlignCenter);
    D.prep_edit=add_edit(R,"prep_chat",{40,466,580,496},"",120);
    auto send_prep=[] {
        std::string message=sui::edit_text(D.prep_edit);
        if (!message.empty()) { A.say(message); sui::set_edit_text(D.prep_edit,""); }
    };
    add_button(R,"prep_send",{590,462,720,496},"Send",send_prep);
    if (D.prep_edit) sui::add_handler(D.prep_edit,[send_prep](Win,const sui::Msg& m) {
        if (m.type==sui::kMsgKeyDown && (m.b&0xFFFF)==VK_RETURN) { send_prep(); return true; }
        return false;
    });
    D.prep_ready_btn=add_button(R,"prep_ready",{30,506,156,542},"Ready",[] { A.set_ready(!A.ready()); });
    D.prep_change=add_button(R,"prep_change",{166,506,316,542},"Change captain",[] { if(A.change_captain) A.change_captain(); });
    add_button(R,"prep_settings",{326,506,426,542},"Settings",[] { g_adventure_page=true; g_dirty=true; });
    add_button(R,"prep_cancel",{436,506,606,542},"Cancel adventure",[] { if(A.cancel_adventure) A.cancel_adventure(); });
    add_button(R,"prep_leave",{616,506,740,542},"Leave party",[] { A.leave(); });
    for (const char* name : {"prep_ready","prep_change","prep_settings","prep_cancel","prep_leave","prep_send"})
        sui::set_caption_padding(g_named[name],3.f);
    // Enter in the chat field sends the message
    if (D.chat_edit)
        sui::add_handler(D.chat_edit, [](Win, const sui::Msg& m) {
            if (m.type == sui::kMsgKeyDown && (m.b & 0xFFFF) == VK_RETURN) {
                do_send_chat();
                return true;
            }
            return false;
        });
    if (D.addr_edit)
        sui::add_handler(D.addr_edit, [](Win, const sui::Msg& m) {
            if (m.type == sui::kMsgKeyDown && (m.b & 0xFFFF) == VK_RETURN) {
                do_join(sui::edit_text(D.addr_edit));
                return true;
            }
            return false;
        });
    for (Win w : common)
        (void)w;
    sui::set_visible(root, false);
    g_cache[root] = Cache{"", 0, 0};
    H->log(M, SK_INFO, "ui: co-op dialog ready");
}

static void refresh_connect()
{
    bool scanning = A.role() == 0;
    auto games = A.lan_games ? A.lan_games() : std::vector<LanGame>{};
    text(D.lan_hdr, games.empty() ? T("Games on your network: searching...", "Игры в вашей сети: поиск...")
                                  : T("Games on your network (click to join):", "Игры в вашей сети (нажмите, чтобы войти):"));
    for (int i = 0; i < 3; i++) {
        bool have = i < (int)games.size();
        if (have) {
            const auto& g = games[i];
            char b[256];
            snprintf(b, sizeof b, "%s  -  %s  -  %d/%d  -  %s", g.name.c_str(), g.info.c_str(), g.players,
                     g.max_players, g.address.c_str());
            text(D.lan_rows[i], b);
            D.lan_addr[i] = g.address;
        } else
            D.lan_addr[i].clear();
        show(D.lan_rows[i], have && scanning);
    }
    std::string hint;
    if (A.local_addresses) {
        auto ips = A.local_addresses();
        std::string port = sui::edit_text(D.port_edit);
        for (size_t i = 0; i < ips.size() && i < 3; i++)
            hint += (i ? ",  " : "") + ips[i] + ":" + port;
    }
    text(D.host_hint, hint.empty() ? "" : std::string(T("Friends connect to: ", "Друзья подключаются к: ")) + hint);
    std::string up_line, up_btn;
    bool up = A.update && A.update(up_line, up_btn);
    // the session status wins; otherwise the update line (a new SporeCoop release)
    text(D.status, g_status.empty() && up ? up_line : g_status);
    color(D.status, g_status.empty() && up ? kGreen : g_status_color);
    show(D.update_btn, up && !up_btn.empty());
    text(D.update_btn, up_btn);
}

static void refresh_lobby()
{
    text(D.session, A.session_line());
    auto rows = A.players();
    for (int i = 0; i < 4; i++) {
        bool have = i < (int)rows.size();
        show(D.row_name[i], have);
        show(D.row_stage[i], have);
        show(D.row_ping[i], have);
        show(D.row_ready[i], have);
        if (!have)
            continue;
        const auto& p = rows[i];
        std::string n = p.name;
        if (p.host)
            n += T("  (host)", "  (хост)");
        if (p.me)
            n += T("  - you", "  - вы");
        text(D.row_name[i], n);
        color(D.row_name[i], p.me ? kGold : kWhite);
        text(D.row_stage[i], p.stage);
        text(D.row_ping[i], p.me ? "" : (p.ping_ms >= 0 ? std::to_string(p.ping_ms) + T(" ms", " мс") : "?"));
        text(D.row_ready[i], p.ready ? T("ready", "готов") : "-");
        color(D.row_ready[i], p.ready ? kGreen : kDim);
    }
    std::string wl = A.world_line ? A.world_line() : "";
    text(D.world, wl.empty() ? g_status : wl);
    auto chat = A.chat ? A.chat() : std::vector<std::string>{};
    size_t first = chat.size() > 4 ? chat.size() - 4 : 0;
    for (int i = 0; i < 4; i++)
        text(D.chat_lines[i], first + i < chat.size() ? chat[first + i] : "");
    int ck = 0, cown = 0;
    std::string cname, cl;
    if (A.creation && A.creation(ck, cname, cown)) {
        cl = ck == 1 ? T("My cell: ", "Моя клетка: ") : ck == 2 ? T("My ship: ", "Мой корабль: ") : T("My creature: ", "Моё существо: ");
        cl += cname.empty() ? T("the world's own", "из мира хоста") : cname;
        if (ck != 2 && cown > 0)
            cl += std::string(T("  (in My Creations: ", "  (своих: ")) + std::to_string(cown) + ")";
    }
    std::string gwhat, gto;
    int gn = A.give ? A.give(gwhat, gto) : 0;
    bool space = gwhat == "spice";
    bool giving = A.give && !gto.empty() && (cl.empty() || space);
    if (space) // the ship can't be changed in flight: the row is for the cargo
        cl = gn ? std::string(T("Spice in cargo: ", "Специи в трюме: ")) + std::to_string(gn)
                : T("No spice in cargo to give", "В трюме нет специи");
    else if (giving) {
        const char* w = gwhat == "city" ? T("the city whose menu you opened", "город, меню которого открыто")
                        : gwhat == "vehicle" ? T("selected vehicles: ", "выбрано машин: ")
                                             : T("selected tribe members: ", "выбрано жителей: ");
        cl = gn ? std::string(w) + (gwhat == "city" ? "" : std::to_string(gn))
                : T("Select your units to give them to a partner", "Выделите свои юниты, чтобы отдать напарнику");
    }
    bool roles = giving && gn == 0 && A.civ_role && A.mode && A.mode() == 0x1654C04; // civ, nothing selected
    if (roles) {
        int r = A.civ_role();
        const char* rn[4] = {T("none - pick one", "нет - выберите"), T("military (stronger vehicles)", "военная (сильнее машины)"),
                             T("economic (trade earns more)", "торговая (выгоднее торговля)"),
                             T("religious (faster conversion)", "религиозная (быстрее обращение)")};
        cl = std::string(T("My role: ", "Моя роль: ")) + rn[r & 3];
    }
    text(D.creation, cl);
    show(D.give_btn, giving && gn > 0);
    text(D.give_btn, space ? std::string(T("Give 10 to ", "Дать 10: ")) + gto : std::string(T("Give to ", "Отдать ")) + gto);
    std::string jto;
    bool jump = A.jump && A.jump(jto);
    show(D.jump_btn, jump);
    text(D.jump_btn, std::string(T("Jump to ", "К звезде: ")) + jto);
    bool can = (!cl.empty() && !giving && ck != 2 && cown > 0) || roles;
    show(D.creation_prev, can);
    show(D.creation_next, can);
    bool host = A.role() == 1;
    text(D.ready_btn, A.ready() ? T("Not ready", "Не готов") : T("I'm ready", "Я готов"));
    ga::Difficulty difficulty;
    int party = 1;
    bool editable = false;
    std::string adventure_status;
    bool adventure = adventure_view(difficulty, party, editable, adventure_status);
    show(D.share_btn, host || adventure);
    text(D.share_btn, adventure ? "Adventure settings" : A.vote_active && A.vote_active() ? T("Start now", "Начать сейчас") : T("Send my world", "Отправить мир"));
    text(D.leave_btn, host ? T("Stop hosting", "Закрыть игру") : T("Leave", "Выйти"));
}

static void refresh_adventure()
{
    ga::Difficulty difficulty;
    int party = 1;
    bool editable = false;
    std::string status;
    if (!adventure_view(difficulty, party, editable, status)) {
        g_adventure_page = false;
        g_dirty = true;
        return;
    }
    const char* deaths[] = {"Teammate revival", "Any death fails", "Teammate revival + automatic recovery"};
    unsigned mode = static_cast<unsigned>(difficulty.death_mode);
    text(D.adventure_death, std::string("Death rule: ") + (mode < 3 ? deaths[mode] : "Invalid"));
    char line[192];
    snprintf(line, sizeof line, "Enemy health: %.2fx%s", difficulty.health_multiplier(party),
             difficulty.enemy_health_override == 0 ? " (party scaling)" : " (host override)");
    text(D.adventure_health, line);
    snprintf(line, sizeof line, "Enemy damage: %.2fx", difficulty.damage_multiplier());
    text(D.adventure_damage, line);
    text(D.adventure_fire, "Friendly fire: " + std::to_string(difficulty.friendly_fire_percent) + "%");
    text(D.adventure_status, status.empty() ? editable ? "Host: choose settings, then select an adventure in the Sporepedia."
                                                                        : "Only the host can change settings before an attempt."
                                                  : status);
    for (Win w : D.adventure_controls)
        sui::set_enabled(w, editable);
}

static void refresh_preparation(const ga::PreparationView& view)
{
    auto adventure=card(view.adventure);
    text(D.prep_title,short_text(adventure.name.empty()?"Preparing adventure":adventure.name,55));
    text(D.prep_author,adventure.author.empty()?"":"By "+short_text(adventure.author,48));
    picture(D.adventure_picture,adventure.image,{40,106,112,174},true);
    show(D.prep_no_preview,!D.adventure_picture.window);
    text(D.prep_hint,view.loading?"Loading adventure together...":view.locked_captain.instance?
         "Authored captain required. Everyone must press Ready.":"Choose your captain, then press Ready to join the adventure.");
    auto players=A.players();
    for (int i=0;i<4;++i) {
        bool occupied=size_t(i)<view.members.size();
        const ga::PreparationMember* m=occupied?&view.members[i]:nullptr;
        PlayerRow player;
        if (m) for (const auto& p:players) if(p.id==m->player) player=p;
        auto key=m && A.captain_key?A.captain_key(m->player):dbpf::Key{};
        auto captain=card(key);
        text(D.prep_player[i],occupied?short_text(player.name,11)+(player.host?" (H)":player.me?" (you)":""):"Open slot");
        float x=40.f+i*172.f;
        picture(D.captain_picture[i],captain.image,{x+34,238,x+130,334},occupied);
        show(D.prep_placeholder[i],occupied&&!D.captain_picture[i].window);
        text(D.prep_captain[i],occupied?short_text(captain.name.empty()?key.instance?"Unnamed captain":"Choosing captain":captain.name,17):"");
        std::string stage;
        if(m) {
            const char* stages[]={"Content","Selecting","Name / review","Saving","Sharing","Captain saved"};
            stage=stages[unsigned(m->stage)];
            if(m->stage==ga::PreparationStage::Content||m->stage==ga::PreparationStage::Sharing) stage+=" "+std::to_string(m->progress)+"%";
        }
        text(D.prep_stage[i],stage);
        text(D.prep_ready[i],m?(m->ready?"Ready":m->validated?"Not ready":"Preparing..."):"");
        color(D.prep_ready[i],m&&m->ready?kGreen:kYellow);
    }
    auto chat=A.chat();
    for(int i=0;i<2;++i) { int j=int(chat.size())-2+i; text(D.prep_chat[i],j>=0?short_text(chat[j],52):""); }
    text(D.prep_ready_btn,A.ready()?"Not ready":"Ready");
    sui::set_enabled(D.prep_ready_btn,!view.loading && A.can_ready && A.can_ready());
    sui::set_enabled(D.prep_change,!view.loading && !view.locked_captain.instance && A.can_ready && A.can_ready());
}

static void refresh()
{
    g_pending = false;
    if (!g_ok)
        return;
    ensure_menu_button();
    if (g_want_open && !D.root)
        build_dialog();
    if (!D.root)
        return;
    if (g_want_open && g_raise) {
        // on top of every UI layer (in game the HUD layers cover the menu layer and take
        // the mouse): a direct child of the main window, brought to the front
        g_raise = false;
        Win main = sui::main_window();
        if (Win p = sui::parent(D.root))
            if (p != main)
                sui::remove_child(p, D.root);
        if (sui::parent(D.root) != main)
            sui::add_child(main, D.root);
        sui::bring_to_front(main, D.root);
        sui::set_flag(D.root, sui::kFlagAlwaysInFront, true);
    }
    show(D.root, g_want_open);
    if (!g_want_open) {
        if (A.lan_scan)
            A.lan_scan(false);
        return;
    }
    bool lobby = A.role() != 0;
    ga::PreparationView preparation;
    bool preparing=lobby && A.preparation && A.preparation(preparation);
    if (preparing && preparation.run != g_last_preparation_run) g_adventure_page=false;
    g_last_preparation_run=preparing?preparation.run:0;
    if (!lobby)
        g_adventure_page = false;
    for (Win w : D.page_connect)
        show(w, !lobby);
    for (Win w : D.page_lobby)
        show(w, lobby && !g_adventure_page && !preparing);
    for (Win w : D.page_adventure)
        show(w, lobby && g_adventure_page);
    for (Win w:D.page_preparation) show(w,preparing&&!g_adventure_page);
    if(!preparing||g_adventure_page) {
        show(D.adventure_picture.window,false);
        for(auto& p:D.captain_picture) show(p.window,false);
    }
    text(D.subtitle, lobby && g_adventure_page ? "ADVENTURE DIFFICULTY" : lobby ? T("CO-OP LOBBY", "ЛОББИ КООПЕРАТИВА") : T("CO-OP", "КООПЕРАТИВ"));
    if (A.lan_scan)
        A.lan_scan(!lobby);
    if (lobby && g_adventure_page)
        refresh_adventure();
    else if (preparing)
        refresh_preparation(preparation);
    else if (lobby)
        refresh_lobby();
    else
        refresh_connect();
}

static void refresh_task(void*) { refresh(); }

void init(const SkHost* host, SkMod* mod, Api api)
{
    H = host;
    M = mod;
    A = std::move(api);
    g_ok = sui::init(H, M);
    std::string lang = load_str("ui", "lang", "auto");
    g_ru = lang == "ru" || (lang == "auto" && PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_RUSSIAN);
    if (g_ok)
        sui::register_commands();
}

void open(bool on)
{
    if (g_want_open == on)
        return;
    g_want_open = on;
    if (on) {
        g_raise = true;
        if (D.root)
            sui::set_edit_text(D.name_edit, A.name()); // fresh values when the dialog appears
    }
    g_dirty = true;
}

bool is_open() { return g_want_open; }

void tick()
{
    if (!g_ok)
        return;
    // F9: show/hide the co-op window anywhere in the game
    bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    if (f9 && !g_f9_down)
        open(!g_want_open);
    g_f9_down = f9;
    double now = H->time_seconds();
    double period = g_want_open ? 0.25 : 1.0;
    if (!g_pending && (g_dirty || now - g_last_refresh >= period)) {
        g_pending = true;
        g_dirty = false;
        g_last_refresh = now;
        H->run_outside_frame(refresh_task, nullptr);
    }
}

void shutdown()
{
    if (D.root)
        sui::set_visible(D.root, false);
    if (g_menu_btn)
        sui::set_visible(g_menu_btn, false);
}

std::string debug_state()
{
    std::string s = "open=" + std::to_string(g_want_open) + " page=" + (A.role() ? "lobby" : "connect") +
                    " menu_button=" + (g_menu_btn ? "yes" : "no") + "\n";
    char b[256];
    ga::PreparationView preparation;
    if(A.preparation && A.preparation(preparation)) {
        s += "prep_controls ready_enabled="+std::to_string(bool(sui::flags(D.prep_ready_btn)&sui::kFlagEnabled))+" change_enabled="+std::to_string(bool(sui::flags(D.prep_change)&sui::kFlagEnabled))+" adventure_portrait="+std::to_string(D.adventure_picture.window!=nullptr)+"\n";
        s += "preparation run="+std::to_string(preparation.run)+" revision="+std::to_string(preparation.revision)+" epoch="+std::to_string(preparation.epoch)+" title='"+sui::caption(D.prep_title)+"' author='"+sui::caption(D.prep_author)+"'\n";
        for(size_t i=0;i<preparation.members.size();++i) {
            const auto& m=preparation.members[i];
            s += "captain player="+std::to_string(m.player)+" serial="+std::to_string(m.serial)+" name='"+sui::caption(D.prep_captain[i])+"' portrait="+std::to_string(D.captain_picture[i].window!=nullptr)+" ready="+std::to_string(m.ready)+" stage="+std::to_string(unsigned(m.stage))+"\n";
        }
    }
    if (g_menu_btn) {
        Rect r = sui::screen_rect(g_menu_btn);
        snprintf(b, sizeof b, "menu %.0f %.0f %.0f %.0f %d '%s'\n", r.x1, r.y1, r.x2, r.y2,
                 (int)sui::visible(g_menu_btn), sui::caption(g_menu_btn).c_str());
        s += b;
    }
    for (auto& [name, w] : g_named) {
        Rect r = sui::screen_rect(w);
        bool vis = sui::visible(w) && D.root && sui::visible(D.root);
        std::string cap = name == "name" || name == "address" || name == "port" || name == "chatedit" || name == "prep_chat"
                              ? sui::edit_text(w) : sui::caption(w);
        snprintf(b, sizeof b, "%s %.0f %.0f %.0f %.0f %d '%s'\n", name.c_str(), r.x1, r.y1, r.x2, r.y2, (int)vis,
                 cap.c_str());
        s += b;
    }
    for (int i = 0; i < 4; i++)
        if (D.row_name[i] && sui::visible(D.row_name[i]))
            s += "row " + std::to_string(i) + " '" + sui::caption(D.row_name[i]) + "' '" + sui::caption(D.row_stage[i]) +
                 "' '" + sui::caption(D.row_ping[i]) + "' '" + sui::caption(D.row_ready[i]) + "'\n";
    for (int i = 0; i < 5; i++)
        if (D.chat_lines[i] && !sui::caption(D.chat_lines[i]).empty())
            s += "chat '" + sui::caption(D.chat_lines[i]) + "'\n";
    if (D.session)
        s += "session '" + sui::caption(D.session) + "'\n";
    if (D.world)
        s += "world '" + sui::caption(D.world) + "'\n";
    if (D.creation)
        s += "creation '" + sui::caption(D.creation) + "'\n";
    if (D.lan_hdr)
        s += "lan '" + sui::caption(D.lan_hdr) + "'\n";
    s += "status '" + g_status + "'\n";
    return s;
}

uint32_t text_font() { return g_text_font; }

} // namespace coop_ui
