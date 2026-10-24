#include "adventure_native.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "adventure_captains.h"
#include "adventure_catalog.h"
#include "adventure_collect.h"
#include "adventure_world.h"
#include "adventure_relationships.h"
#include "adventure_social.h"
#include "adventure_flow.h"
#include "game.h"
#include "modes.h"
#include "coop_ui.h"
#include "sporekit_game.hpp"

namespace adventure_native {
using namespace skg;
namespace {
const SkHost* H;
SkMod* M;
Ctx C;
bool g_symbols, g_frozen, g_replay, g_coop_attempt, g_recovering;
uint32_t g_suppressed_scores;
unsigned g_initializing;
uint64_t g_intro_run;
bool g_intro_pending;
dbpf::Key g_host_captain, g_score_adventure;
std::vector<ga::Captain> g_captains;
struct Immunity { void* object; bool authored; };
std::map<uint8_t, Immunity> g_invulnerability;
std::string g_error;
using Show = void(SK_THISCALL*)(void*, bool, uint32_t, bool);
using Update = void(SK_THISCALL*)(void*, int);
using Switch = void(SK_THISCALL*)(void*, int);
using Statistic = void(SK_THISCALL*)(void*, uint32_t, const dbpf::Key*, const int64_t*, uint32_t);
using CreatureAct = void(SK_THISCALL*)(void*);
using Initialize = void(SK_THISCALL*)(void*);
using Message = bool(SK_THISCALL*)(void*,uint32_t,void*);
Show original_show;
Update original_update;
Switch original_switch;
Statistic original_statistic;
CreatureAct original_creature_act;
Initialize original_initialize;
Initialize original_save_captain;
Message original_message;

void feed(const std::string& message) { if (C.feed) C.feed(message); }
bool connected() { return C.sess && C.sess->connected(); }
void* scenario()
{
    uintptr_t pointer = H->sym("App::cScenarioMode::ptr");
    void* object = pointer ? *reinterpret_cast<void**>(pointer) : nullptr;
    return object && readable(object, 0xE0) ? object : nullptr;
}
void* play_mode()
{
    void* object = scenario();
    return object && game::mode_id() == modes::kScenario ? field<void*>(object, 0x78) : nullptr;
}
bool available()
{
    return g_symbols && adventure_collect::available() && adventure_captains::available() &&
           adventure_world::available() && adventure_relationships::available() && adventure_social::available();
}
std::string unavailable()
{
    if (!g_symbols) return "Native adventure launch is unavailable for this executable build.";
    if (!adventure_collect::available()) return "Native adventure content collection is unavailable.";
    if (!adventure_captains::available()) return "Native captain selection is unavailable.";
    if (!adventure_world::available()) return "Native adventure actor and objective synchronization is unavailable.";
    if (!adventure_relationships::available()) return "Native captain relationship synchronization is unavailable.";
    if (!adventure_social::available()) return "Native adventure social interactions are unavailable.";
    return g_error;
}

bool sporepedia_request()
{
    uintptr_t address = H->sym("SporeCoop::Adventure::Request");
    return address && readable(reinterpret_cast<void*>(address), 0x40) &&
           game::mode_id() != modes::kSpace && game::mode_id() != modes::kEdit && game::mode_id() != modes::kEditor &&
           field<int>(reinterpret_cast<void*>(address), 0x28) != 2;
}

void capture_request()
{
    if (g_recovering || adventure_captains::native_pending()) return;
    uintptr_t request_address = H->sym("SporeCoop::Adventure::Request");
    if (request_address && readable(reinterpret_cast<void*>(request_address), 0x40)) {
        void* request = reinterpret_cast<void*>(request_address);
        if (adventure_captains::complete_native(field<dbpf::Key>(request, 0), field<dbpf::Key>(request, 0x18))) return;
    }
    // Guests may emit more than one native launch request while leaving the wizard.
    if (C.sess->role() != net::Role::Host && adventure::active()) return;
    if (C.sess->role() != net::Role::Host) {
        feed("The party host selects the adventure. Join the next attempt from the lobby.");
        return;
    }
    if (!available()) {
        feed(unavailable());
        return;
    }
    uintptr_t address = H->sym("SporeCoop::Adventure::Request");
    if (!address || !readable(reinterpret_cast<void*>(address), 0x40)) return;
    dbpf::Key adventure = field<dbpf::Key>(reinterpret_cast<void*>(address), 0);
    if (const auto* pending = adventure::current(); pending && pending->adventure == adventure) return;
    g_host_captain = field<dbpf::Key>(reinterpret_cast<void*>(address), 0x18);
    dbpf::Key locked;
    std::string error;
    if (!adventure_collect::inspect(adventure, locked, error)) {
        feed("Adventure preparation failed: " + error);
        return;
    }
    if (!adventure::request_play(adventure, locked)) feed(adventure::status());
    else coop_ui::open(true);
}

void SK_THISCALL save_captain(void* self)
{
    original_save_captain(self);
    // The earlier loading request starts an animation before conversion and saving.
    // This boundary is after the native save and before our shared launch barrier.
    if (connected() && !g_replay && sporepedia_request()) capture_request();
}

void SK_THISCALL show(void* self, bool on, uint32_t mode, bool transition)
{
    if (g_recovering && on && mode == modes::kScenario) return;
    // Results can outlive Scenario. Only a fresh solo/native entry retires the
    // previous co-op score guard; ordinary runtime cleanup must retain it.
    if (on && mode == modes::kScenario && !g_replay && (!connected() || !sporepedia_request())) {
        g_coop_attempt = false;
        g_score_adventure = {};
    }
    if (!on || mode != modes::kScenario || !connected() || g_replay || !sporepedia_request()) {
        original_show(self, on, mode, transition);
        return;
    }
    capture_request();
}

void SK_THISCALL switch_mode(void* manager, int index)
{
    // The loading-screen method is only visual. Gate the actual transition before
    // the previous mode's OnExit, since quick-play can queue this before showing it.
    if ((g_recovering || (connected() && !g_replay && sporepedia_request())) && index >= 0 && readable(manager, 0x2C)) {
        auto* begin = field<uint8_t*>(manager, 0x14);
        auto* end = field<uint8_t*>(manager, 0x18);
        uintptr_t size = reinterpret_cast<uintptr_t>(end) - reinterpret_cast<uintptr_t>(begin);
        if (reinterpret_cast<uintptr_t>(end) >= reinterpret_cast<uintptr_t>(begin) && size <= 0x18000 && static_cast<unsigned>(index) < size / 0x18 &&
            readable(begin + index * 0x18, 0x18) && field<uint32_t>(begin + index * 0x18, 4) == modes::kScenario) {
            capture_request();
            return;
        }
    }
    original_switch(manager, index);
}

void SK_THISCALL update(void* self, int milliseconds)
{
    if (connected() && g_frozen && game::mode_id() == modes::kScenario) return;
    if(adventure_flow::held() && field<int>(self,0x90)==3) return;
    original_update(self, milliseconds);
}

bool SK_THISCALL message(void* self, uint32_t type, void* data)
{
    if(connected() && g_coop_attempt) {
        if(type==0x044F1189 && adventure::active() && field<int>(self,0x90)==1) {
            if(const auto* d=adventure::current()) {
                g_intro_run=d->run_id; g_intro_pending=true;
                adventure_flow::confirm_intro();
            }
            return false;
        }
        if(type==0x070B18C7 && data && readable(data,12)) {
            int choice=field<int>(data,8);
            if(choice==1 || choice==2) {
                bool accepted=adventure::results_accepted(choice==2);
                if(choice==2 && accepted) coop_ui::open(true);
                // A delayed results callback must not restart/exit a later run.
                if(choice==2 || !accepted) return false;
            }
        }
    }
    return original_message(self,type,data);
}

void SK_THISCALL apply_creature_act(void* creature)
{
    // Captain replicas have no authored NPC class. The native global creature
    // loop otherwise dereferences a null class-act while changing/initializing acts.
    if (connected() && game::mode_id() == modes::kScenario && game::is_puppet(creature)) return;
    original_creature_act(creature);
}

void SK_THISCALL initialize(void* self)
{
    struct Initializing {
        Initializing() { ++g_initializing; }
        ~Initializing() { --g_initializing; }
    } initializing;
    g_intro_pending=false; g_intro_run=0;
    adventure_world::begin_native_attempt(self);
    original_initialize(self);
}

void SK_THISCALL record_statistic(void* self, uint32_t statistic, const dbpf::Key* asset,
                                const int64_t* values, uint32_t count)
{
    // The native results panel submits these two adventure result statistics to
    // Pollinator. Keep local captain progression and the rest of its result UI intact.
    if (g_coop_attempt && asset && *asset == g_score_adventure &&
        (statistic == 0x2D59837B || statistic == 0x706CCB27)) {
        ++g_suppressed_scores;
        return;
    }
    original_statistic(self, statistic, asset, values, count);
}

bool launch(const dbpf::Key& adventure, const dbpf::Key& captain, std::string& error)
{
    if (!available() || !adventure.instance || !captain.instance) {
        error = unavailable();
        return false;
    }
    // Match the native quick-play request (DFBDE9 and DFBF25), then enter through
    // the game's loading screen rather than entering Scenario directly from Present.
    uintptr_t reset_request = H->sym("SporeCoop::Adventure::ResetRequest");
    uintptr_t loading_get = H->sym("SporeCoop::Adventure::LoadingScreenGet");
    void* request = cdecl_call<void*>(reset_request);
    void* loading = cdecl_call<void*>(loading_get);
    if (!request || !loading) {
        error = "The native adventure loading context is not initialized.";
        return false;
    }
    field<dbpf::Key>(request, 0) = adventure;
    field<dbpf::Key>(request, 0x18) = captain;
    field<int>(request, 0x24) = 2;
    field<int>(request, 0x28) = 1;
    g_coop_attempt = true;
    g_score_adventure = adventure;
    g_replay = true;
    original_show(loading, true, modes::kScenario, true);
    // Quick-play owns an application state as well as a game mode. Results Done
    // switches back to the galaxy application state, so guests must enter it too.
    void* state_manager = cdecl_call<void*>(H->sym("App::IStateManager::Get"));
    if (!state_manager) {
        g_replay = false;
        error = "The native application state manager is unavailable.";
        return false;
    }
    thiscall<bool>(H->sym("App::cStateManager::SwitchToState"), state_manager, uint32_t(0x21851EBE));
    void* mode_manager = cdecl_call<void*>(H->sym("App::IGameModeManager::Get"));
    if (!mode_manager) {
        g_replay = false;
        error = "The native game mode manager is unavailable.";
        return false;
    }
    bool entered = game::mode_id() == modes::kScenario ||
        thiscall<bool>(H->sym("App::cGameModeManager::SetActiveMode"), mode_manager, modes::kScenario);
    g_replay = false;
    if (!entered) error = "The game declined the adventure mode transition.";
    return entered;
}

bool loaded(const dbpf::Key& key)
{
    void* object = scenario();
    void* play = play_mode();
    void* data = object ? field<void*>(object, 0x74) : nullptr;
    if (g_initializing || !play || !data || !readable(data, 0x128) || !field<bool>(data, 0x124) || !game::avatar()) return false;
    void* resource = field<void*>(data, 0x10);
    void* loading = cdecl_call<void*>(H->sym("SporeCoop::Adventure::LoadingScreenGet"));
    return resource && readable(resource, 0x14) && field<dbpf::Key>(resource, 8) == key &&
           !field<bool>(data, 0x14) && field<int>(object, 0xCC) == 2 &&
           loading && readable(loading, 0x371) && !field<bool>(loading, 0x370);
}

bool paused()
{
    auto* p=play_mode();
    if(adventure_flow::held() || (p && field<int>(p,0x90)!=3)) return true;
    uintptr_t get = H->sym("Simulator::cGameTimeManager::Get");
    void* manager = get ? cdecl_call<void*>(get) : nullptr;
    return manager && (field<uint32_t>(manager, 0x48) & 1);
}

bool sample(uint8_t player, ga::CaptainSeed& seed)
{
    if (!C.sess || !C.captain_of) return false;
    if (player != C.sess->my_id() && C.remote_state) {
        net::PlayerState state;
        if (!C.remote_state(player, state) || state.mode != modes::kScenario ||
            !std::isfinite(state.max_health) || state.max_health <= 0) return false;
        seed = {player, state.max_health, {state.pos.x, state.pos.y, state.pos.z}};
        return true;
    }
    void* captain = C.captain_of(player);
    if (!captain || !readable(captain, game::kHpOff + sizeof(float))) return false;
    net::Vec3 position = game::position(captain);
    seed = {player, game::max_health(captain), {position.x, position.y, position.z}};
    return std::isfinite(seed.max_health) && seed.max_health > 0;
}

void apply(const std::vector<ga::Captain>& captains)
{
    g_captains = captains;
    for (const auto& state : captains) {
        void* captain = C.captain_of ? C.captain_of(state.player) : nullptr;
        if (!captain || state.life == ga::Life::Disconnected || !readable(captain, game::kFlagsOff + 8)) continue;
        game::set_health(captain, std::max(1.f, state.health));
        bool& invincible = field<bool>(captain, game::kCombatOff + 0x80);
        auto previous = g_invulnerability.find(state.player);
        if (previous == g_invulnerability.end() || previous->second.object != captain)
            g_invulnerability[state.player] = {captain, invincible};
        invincible = g_invulnerability.at(state.player).authored || state.life == ga::Life::Downed;
        // Authority also rejects hits during grace, without altering authored immunity.
        if (state.life == ga::Life::Downed) {
            game::stop(captain);
            game::set_position(captain, {state.position.x, state.position.y, state.position.z});
        }
    }
}
} // namespace

bool init(const SkHost* host, SkMod* mod, Ctx context)
{
    H = host; M = mod; C = std::move(context);
    const char* required[] = {"SporeCoop::Adventure::ResetRequest", "SporeCoop::Adventure::Request",
        "SporeCoop::Adventure::LoadingScreenGet", "SporeCoop::Adventure::LoadingScreenShow",
        "Simulator::cScenarioPlayMode::Update", "Simulator::cGameTimeManager::Get",
        "App::cGameModeManager::SwitchToIndex", "App::cGameModeManager::SetActiveMode",
        "App::IGameModeManager::Get", "SporeCoop::Adventure::RecordAssetStatistic",
        "SporeCoop::Adventure::ApplyCreatureAct", "Simulator::cScenarioPlayMode::Initialize",
        "App::IStateManager::Get", "App::cStateManager::SwitchToState", "SporeCoop::Adventure::CaptainWizardSave",
        "SporeCoop::Adventure::HandlePlayMessage"};
    g_symbols = H->sym_require(M, required, 16) == 0;
    adventure_collect::init(H, M);
    adventure_catalog::init(H, M);
    adventure_captains::init(H, M, C.sess);
    adventure_world::Ctx world;
    world.sess = C.sess; world.feed = C.feed; world.captain_of = C.captain_of;
    world.captain_can_act = [](uint8_t player) {
        adventure::CaptainState state;
        return adventure::captain_state(player, state) && state.captain.life == ga::Life::Alive;
    };
    world.run = [] { const auto* run = adventure::current(); return run ? run->run_id : 0; };
    world.active = [] { return adventure::running(); };
    world.held=adventure_flow::held;
    world.difficulty = [] { const auto* run = adventure::current(); return run ? run->difficulty : adventure::settings(); };
    world.party_size = [] { const auto* run = adventure::current(); return run ? run->roster.size() : size_t(1); };
    world.captain_damage = adventure::captain_damage;
    world.captain_heal = adventure::captain_heal;
    world.result = [](bool success) { adventure::complete(success); };
    adventure_world::init(H, M, std::move(world));
    adventure_relationships::Ctx relationships;
    relationships.active = [] { return adventure::running() && game::mode_id() == modes::kScenario; };
    relationships.local_avatar = game::avatar;
    relationships.authoritative_relationship = adventure_world::party_relationship;
    relationships.remote_captain = [](void* creature) {
        if (!creature || !C.sess || !C.captain_of || !game::is_puppet(creature)) return false;
        for (const auto& player : C.sess->players())
            if (player.id != C.sess->my_id() && adventure::participating(player.id) && C.captain_of(player.id) == creature)
                return !game::is_destroyed(creature);
        return false;
    };
    adventure_relationships::init(H, M, std::move(relationships));
    adventure_social::Ctx social;
    social.active = [] {
        adventure::CaptainState state;
        return C.sess && C.sess->role() == net::Role::Client && adventure::running() &&
            !g_frozen && !adventure_flow::held() && game::mode_id() == modes::kScenario &&
            adventure::captain_state(C.sess->my_id(), state) && state.captain.life == ga::Life::Alive;
    };
    social.run = [] { const auto* run = adventure::current(); return run ? run->run_id : 0; };
    social.act = [] { void* p = play_mode(); return p && readable(p, 0xBC) ? field<int>(p, 0xB8) : -1; };
    social.local_avatar = game::avatar;
    social.mirrored = adventure_world::is_live_mirror;
    social.listed = [](void* object) {
        if (!object) return false;
        std::pair<void*, bool> query{object, false};
        game::for_each_object([](void* candidate, uint32_t, void* argument) {
            auto& q = *static_cast<std::pair<void*, bool>*>(argument);
            if (candidate != q.first) return true;
            q.second = true; return false;
        }, &query);
        return query.second;
    };
    adventure_social::init(H, M, std::move(social));
    adventure_flow::Ctx flow;
    flow.sess=C.sess;
    flow.run=[] { const auto* d=adventure::current(); return d?d->run_id:0; };
    flow.running=adventure::running;
    flow.members=[] {
        std::vector<uint8_t> members;
        if(const auto* d=adventure::current()) for(uint8_t id:d->roster)
            if(adventure::participating(id)) members.push_back(id);
        return members;
    };
    flow.validate_talk=adventure_world::validate_talk;
    flow.present=adventure_world::present_talk;
    flow.release=[](const adventure_flow::Scene& s) {
        if(s.kind==adventure_flow::Kind::Talk) adventure_world::release_talk(s);
        else if(s.kind==adventure_flow::Kind::Intro && g_intro_pending && s.run==g_intro_run) {
            g_intro_pending=false;
            std::array<uint8_t,16> data{};
            if(auto* p=play_mode()) original_message(p,0x044F1189,data.data());
        }
    };
    flow.waiting=[](bool waiting) { coop_ui::open(waiting); };
    g_symbols=adventure_flow::init(H,std::move(flow)) && g_symbols;
    if (g_symbols) {
        g_symbols = H->hook_create(M, reinterpret_cast<void*>(H->sym(required[3])), reinterpret_cast<void*>(show),
                                  reinterpret_cast<void**>(&original_show)) == 0;
        if (g_symbols)
            g_symbols = H->hook_create(M, reinterpret_cast<void*>(H->sym(required[4])), reinterpret_cast<void*>(update),
                                      reinterpret_cast<void**>(&original_update)) == 0;
        if (g_symbols)
            g_symbols = H->hook_create(M, reinterpret_cast<void*>(H->sym(required[6])), reinterpret_cast<void*>(switch_mode),
                                      reinterpret_cast<void**>(&original_switch)) == 0;
        if (g_symbols)
            g_symbols = H->hook_create(M, reinterpret_cast<void*>(H->sym(required[9])), reinterpret_cast<void*>(record_statistic),
                                      reinterpret_cast<void**>(&original_statistic)) == 0;
        if (g_symbols)
            g_symbols = H->hook_create(M, reinterpret_cast<void*>(H->sym(required[10])), reinterpret_cast<void*>(apply_creature_act),
                                      reinterpret_cast<void**>(&original_creature_act)) == 0;
        if (g_symbols)
            g_symbols = H->hook_create(M, reinterpret_cast<void*>(H->sym(required[11])), reinterpret_cast<void*>(initialize),
                                      reinterpret_cast<void**>(&original_initialize)) == 0;
    }
    if (g_symbols)
        g_symbols = H->hook_create(M, reinterpret_cast<void*>(H->sym(required[14])), reinterpret_cast<void*>(save_captain),
                                  reinterpret_cast<void**>(&original_save_captain)) == 0;
    if(g_symbols)
        g_symbols=H->hook_create(M,reinterpret_cast<void*>(H->sym(required[15])),reinterpret_cast<void*>(message),
                                 reinterpret_cast<void**>(&original_message))==0;
    return available();
}

adventure::Ctx coordinator_context()
{
    adventure::Ctx context;
    context.sess = C.sess; context.feed = C.feed; context.invalidate_ready = C.invalidate_ready;
    context.native_available = available; context.native_reason = unavailable;
    context.collect = adventure_collect::collect;
    context.captain_validated = adventure_captains::validated;
    context.choose_captain = [](const dbpf::Key& locked, auto selected) {
        coop_ui::open(false);
        auto confirmed = [selected = std::move(selected)](const dbpf::Key& key) {
            selected(key);
            if (connected()) coop_ui::open(true);
        };
        dbpf::Key initial_host = g_host_captain; g_host_captain = {};
        bool ok = C.sess && C.sess->role() == net::Role::Host && !locked.instance && initial_host.instance
            ? adventure_captains::publish(initial_host, std::move(confirmed))
            : adventure_captains::choose(locked, std::move(confirmed));
        if (!ok) {
            adventure::cancel(adventure_captains::status());
            coop_ui::open(true);
        }
    };
    context.launch = launch;
    context.reuse_captain=[](const dbpf::Key& previous, auto selected) {
        const auto* d=adventure::current();
        bool ok=d && (d->locked_captain.instance
            ? adventure_captains::choose(d->locked_captain,std::move(selected))
            : adventure_captains::publish(previous,std::move(selected)));
        if(!ok) adventure::cancel("The saved replay captain could not be prepared.");
    };
    context.freeze = [](bool frozen) {
        g_frozen = frozen;
        if (!frozen && adventure::running()) coop_ui::open(false);
    };
    auto restore_menu = [] {
        g_frozen = false;
        adventure_captains::close_picker();
        adventure_native::reset();
        g_host_captain = {};
        void* loading = cdecl_call<void*>(H->sym("SporeCoop::Adventure::LoadingScreenGet"));
        if (loading && original_show) original_show(loading, false, modes::kScenario, true);
        // Quickplay owns application state even when Scenario never became active.
        void* state_manager = cdecl_call<void*>(H->sym("App::IStateManager::Get"));
        g_recovering = true;
        if (state_manager) {
            thiscall<bool>(H->sym("App::cStateManager::SwitchToState"), state_manager, uint32_t(0x02CCDA9C));
        }
        void* manager = cdecl_call<void*>(H->sym("App::IGameModeManager::Get"));
        if (manager && game::mode_id() != modes::kMenu)
            thiscall<bool>(H->sym("App::cGameModeManager::SetActiveMode"), manager, modes::kMenu);
        adventure_captains::restore_galaxy();
        g_recovering = false;
        if (connected()) coop_ui::open(true);
    };
    context.abort_preparation = restore_menu;
    context.abort_native = restore_menu;
    context.loaded = loaded; context.paused = paused; context.captain_of = C.captain_of;
    context.sample = sample; context.apply = apply; context.source = adventure_world::source;
    context.native_result = adventure_world::apply_result;
    return context;
}

void tick(double now) { adventure_captains::tick(now); adventure_flow::tick(); adventure_world::tick(now); }
void blocked_update(void* creature, int milliseconds) { adventure_social::blocked_update(creature, milliseconds); }
void on_message(uint8_t from, net::Reader& message)
{
    if (!message.left()) return;
    if (*message.cur() >= 192) adventure_captains::on_message(from, message);
    else if(*message.cur()>=160 && *message.cur()<=163) adventure_flow::on_message(from,message);
    else adventure_world::on_message(from, message);
}
bool update_filter(void* creature)
{
    if (connected() && game::mode_id() == modes::kScenario) {
        if (g_frozen || adventure_flow::held()) return false;
        for (const auto& state : g_captains)
            if (state.life == ga::Life::Downed && C.captain_of && C.captain_of(state.player) == creature) return false;
    }
    return adventure_world::update_filter(creature);
}
bool damage_filter(void* victim, float& amount, int type, void* attacker)
{
    if (adventure::running() && type != 10 && victim &&
        readable(victim, game::kCombatOff + 0x81)) {
        bool authored_immunity = field<bool>(victim, game::kCombatOff + 0x80);
        for (const auto& [player, immunity] : g_invulnerability)
            if (immunity.object == victim) { authored_immunity = immunity.authored; break; }
        if (authored_immunity) return false;
    }
    if (!adventure_world::damage_filter(victim, amount, type, attacker)) return false;
    return adventure::damage_filter(victim, amount, type, attacker);
}
void reset()
{
    adventure_flow::reset(); g_intro_run=0; g_intro_pending=false;
    adventure_social::reset();
    g_frozen = false; g_host_captain = {}; g_captains.clear();
    for (const auto& [player, immunity] : g_invulnerability)
        if (game::mode_id() == modes::kScenario && C.captain_of && C.captain_of(player) == immunity.object &&
            readable(immunity.object, game::kFlagsOff + 8) && !game::is_destroyed(immunity.object))
            field<bool>(immunity.object, game::kCombatOff + 0x80) = immunity.authored;
    g_invulnerability.clear();
    adventure_captains::reset(); adventure_world::reset();
    adventure_relationships::reset();
}
void shutdown() { reset(); }
std::string status()
{
    auto social = adventure_social::counters();
    return available() ? adventure::status() + "\n" + adventure_flow::status()+"\n" + adventure_captains::status() + "\n" + adventure_world::status() +
        "native party relationship aliases=" + std::to_string(adventure_relationships::aliased_calls()) +
        " authoritative=" + std::to_string(adventure_relationships::authoritative_calls()) + "\n" +
        "native social presentation: starts=" + std::to_string(social.starts) + " ticks=" + std::to_string(social.ticks) +
        " ends=" + std::to_string(social.ends) + " rejected=" + std::to_string(social.rejected) +
        " input_allowed=" + std::to_string(social.input_queries_allowed) + "\n" +
        "native co-op results: suppressed score submissions=" + std::to_string(g_suppressed_scores) : unavailable();
}
} // namespace adventure_native
