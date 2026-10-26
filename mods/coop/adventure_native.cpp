#include "adventure_native.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <optional>

#include "adventure_captains.h"
#include "adventure_catalog.h"
#include "adventure_collect.h"
#include "adventure_control.h"
#include "adventure_results.h"
#include "adventure_world.h"
#include "adventure_relationships.h"
#include "adventure_social.h"
#include "adventure_flow.h"
#include "adventure_intro_gate.h"
#include "adventure_pause.h"
#include "adventure_pause_native.h"
#include "game.h"
#include "modes.h"
#include "coop_ui.h"
#include "editor_native.h"
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
adventure_intro::Gate g_intro;
adventure_intro::SceneGate g_talk;
adventure_flow::Scene g_talk_scene;
double g_now, g_settling_deadline;
bool g_settling_timeout_queued;
uint64_t g_intro_settling;
uint64_t g_result_end_run;
void* g_result_end_play;
uint32_t g_result_end_id;
uint64_t g_result_messages_accepted, g_result_messages_rejected;
dbpf::Key g_host_captain, g_score_adventure, g_score_local_adventure;
struct Preview {
    EditorPreview config;
    editor_native::Snapshot snapshot;
    void* mode = nullptr;
    void* data = nullptr;
    void* resource = nullptr;
    uint64_t run = 0;
    bool launched = false, returning = false, ready_queued = false;
    bool intro_queued = false, intro_confirmed = false;
};
std::optional<Preview> g_preview;
uint64_t g_preview_generation = 1;
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
using EndCinematic = void(SK_THISCALL*)(void*);
Show original_show;
Update original_update;
Switch original_switch;
Statistic original_statistic;
CreatureAct original_creature_act;
Initialize original_initialize;
Initialize original_save_captain;
Message original_message;
Message original_cinematic_message;
EndCinematic original_end_cinematic;

void feed(const std::string& message) { if (C.feed) C.feed(message); }
bool connected() { return C.sess && C.sess->connected(); }
bool simulation_held() { return adventure::outcome_locked() || adventure_flow::held() || adventure_pause::held(); }
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
bool preview_current()
{
    return g_preview && connected() && g_preview->config.current && g_preview->config.current();
}
bool preview_bound(bool playing)
{
    if (!g_preview || scenario() != g_preview->mode || !readable(g_preview->mode, 0xE0)) return false;
    void* mode = g_preview->mode;
    return game::mode_id() == modes::kScenario && field<int>(mode, 0xD0) == 0 &&
        field<int>(mode, 0xCC) == (playing ? 2 : 1) && field<void*>(mode, 0x74) == g_preview->data &&
        readable(g_preview->data, 0x128) && field<void*>(g_preview->data, 0x10) == g_preview->resource &&
        readable(g_preview->resource, 0x14) && field<dbpf::Key>(g_preview->resource, 8) ==
            dbpf::Key{g_preview->snapshot.source.instance, g_preview->snapshot.source.type, g_preview->snapshot.source.group};
}
bool preview_attempt()
{
    const auto* d = adventure::current();
    return preview_current() && d && d->adventure == g_preview->config.adventure &&
        d->locked_captain == g_preview->config.authored_avatar && (!g_preview->run || g_preview->run == d->run_id);
}
bool content_registration(uint64_t run, const dbpf::Key& root, bool& cache_only, std::string& error)
{
    cache_only = false;
    if (!g_preview) return true;
    const auto* d = adventure::current();
    if (!preview_attempt() || !d || d->run_id != run || root != g_preview->config.adventure ||
        !preview_bound(false) || g_preview->returning) {
        error = "The shared editor dependency owner no longer matches this confirmed playtest.";
        return false;
    }
    // The restricted shared editor already loaded this immutable dependency
    // closure. Revised roots are owned serializations, not extra live DBs.
    g_preview->run = run;
    cache_only = true;
    error.clear();
    return true;
}
void preview_return()
{
    if (!g_preview || g_preview->returning) return;
    g_preview->returning = true;
    auto callback = g_preview->config.return_editor;
    if (callback) callback();
}
void schedule_preview_ready()
{
    if (!g_preview || g_preview->returning || g_preview->ready_queued || !preview_attempt() ||
        !adventure::can_ready() || adventure::ready()) return;
    struct Ready { uint64_t generation, run; };
    g_preview->ready_queued = true;
    H->run_outside_frame([](void* opaque) {
        std::unique_ptr<Ready> ready(static_cast<Ready*>(opaque));
        if (ready->generation != g_preview_generation || !g_preview) return;
        g_preview->ready_queued = false;
        const auto* d = adventure::current();
        if (d && d->run_id == ready->run && !g_preview->returning && preview_attempt() &&
            adventure::can_ready() && !adventure::ready()) adventure::set_ready(true);
    }, new Ready{g_preview_generation, adventure::current()->run_id});
}
adventure_intro::Cinematic current_cinematic()
{
    uintptr_t getter = H->sym("Simulator::cCinematicManager::Get");
    void* manager = getter ? cdecl_call<void*>(getter) : nullptr;
    if (!manager || !readable(manager, 0x160)) return {};
    return {reinterpret_cast<uintptr_t>(manager), field<uint32_t>(manager, 0x2C), field<uint32_t>(manager, 0x148)};
}
bool loaded(const dbpf::Key& key);
bool preview_intro_ready()
{
    if (!g_preview || g_preview->returning || !g_preview->launched || !preview_attempt() ||
        !adventure::running() || !loaded(g_preview->config.adventure) || g_intro.pending()) return false;
    const auto scene = adventure_flow::scene();
    void* play = play_mode();
    // Native editor Test enters Playing directly, without the quickplay beam
    // cinematic or its Continue control. Every participant already explicitly
    // confirmed this immutable editor round. Keep the normal flow barrier until
    // all participants have entered that native playing state, then acknowledge
    // this missing cinematic through the existing run-scoped confirmation path.
    return scene.kind == adventure_flow::Kind::Intro && scene.run == g_preview->run &&
        readable(play, 0x94) && field<int>(play, 0x90) == 3 && !current_cinematic().active;
}
void schedule_preview_intro()
{
    if (!g_preview || g_preview->intro_queued || g_preview->intro_confirmed || !preview_intro_ready()) return;
    struct Intro { uint64_t generation, run; };
    g_preview->intro_queued = true;
    H->run_outside_frame([](void* opaque) {
        std::unique_ptr<Intro> intro(static_cast<Intro*>(opaque));
        if (intro->generation != g_preview_generation || !g_preview) return;
        g_preview->intro_queued = false;
        if (g_preview->run != intro->run || !preview_intro_ready()) return;
        g_preview->intro_confirmed = true;
        adventure_flow::confirm_intro();
    }, new Intro{g_preview_generation, g_preview->run});
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
           field<int>(reinterpret_cast<void*>(address), 0x24) == 2 &&
           field<int>(reinterpret_cast<void*>(address), 0x28) == 1;
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
    if (on && mode == modes::kScenario && !g_replay && !g_preview && (!connected() || !sporepedia_request())) {
        g_coop_attempt = false;
        g_score_adventure = {};
        g_score_local_adventure = {};
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
    // Result animations still need native updates. No gameplay/intro update may
    // resume while the host's outcome is pending or the party reviews results.
    if (connected() && adventure::outcome_locked() && field<int>(self,0x90)!=5 && field<int>(self,0x90)!=6) return;
    if(simulation_held() && field<int>(self,0x90)==3) return;
    original_update(self, milliseconds);
}

bool SK_THISCALL message(void* self, uint32_t type, void* data)
{
    if(connected() && g_coop_attempt) {
        if(type==0x070B18C7 && data && readable(data,12)) {
            int choice=field<int>(data,8);
            if(choice==1 || choice==2) {
                if (g_preview) {
                    // Editor results return to the shared immutable draft. They
                    // never enter quickplay Replay or captain progression saves.
                    if (g_preview->launched && self == play_mode() && preview_bound(true)) preview_return();
                    return false;
                }
                adventure_results::accepted(self,choice);
                bool accepted=adventure::results_accepted(choice==2);
                if(choice==2 && accepted) coop_ui::open(true);
                // Party return/replay is coordinated, including delayed native
                // callbacks after a later attempt has replaced these results.
                return false;
            }
        }
        if (type == 0x044F1189 && adventure::active() && self == play_mode() &&
            readable(self, 0xDC) && data && readable(data, 12)) {
            if (const auto* d = adventure::current()) {
                uint32_t id = field<uint32_t>(data, 8);
                // The native handler also matches the published cinematic ID
                // against play+D8 before calling TalkFinish (F1C764–F1C779).
                if (id == field<uint32_t>(self, 0xD8)) {
                    if (g_talk.duplicate(d->run_id, id)) return false;
                    if (field<int>(self, 0x90) == 3 && g_talk.complete(d->run_id, id)) {
                        auto completed = g_talk_scene;
                        g_settling_timeout_queued = false;
                        adventure_world::release_talk(completed);
                        return false;
                    }
                }
            }
        }
        if (adventure::outcome_locked()) {
            // A final result can wait for a previously released native scene's
            // cleanup. Only that owned Intro tail may write its ordinary state
            // before terminal application; the owned Talk tail was handled above.
            const uint64_t run=adventure::outcome_run();
            if (!adventure_world::terminal_state(self) && type==0x044F1189 && self==play_mode() &&
                readable(self,0x98) && field<int>(self,0x90)==1 && g_intro_settling==run && g_intro.released(run)) {
                const bool result=original_message(self,type,data);
                if (field<int>(self,0x90)==3) g_intro_settling=0;
                return result;
            }
            if (!adventure_world::terminal_state(self) || !adventure_world::terminal_message_allowed(self, type) ||
                !data || !readable(data,12) || g_result_end_run != run ||
                g_result_end_play != self || field<uint32_t>(data,8) != g_result_end_id) {
                ++g_result_messages_rejected;
                return false;
            }
            // EndCinematic publishes synchronously after clearing its current ID.
            // Consume the token before native result callbacks can reenter.
            g_result_end_run=0; g_result_end_play=nullptr; g_result_end_id=0;
            ++g_result_messages_accepted;
            return original_message(self,type,data);
        }
        if(type==0x044F1189 && !g_intro.releasing() && adventure::active() && field<int>(self,0x90)==1) {
            if(const auto* d=adventure::current()) {
                if (g_intro.released(d->run_id)) {
                    bool result = original_message(self, type, data);
                    if (g_intro_settling == d->run_id && field<int>(self, 0x90) == 3) g_intro_settling = 0;
                    return result;
                }
                // Compatibility fallback for a native end publisher outside the
                // reviewed cinematic path. Normally EndCinematic is held earlier.
                g_intro.legacy_message(d->run_id);
                adventure_flow::confirm_intro();
            }
            return false;
        }
    }
    return original_message(self,type,data);
}

bool defer_intro_end(void* self, uint32_t native_message)
{
    if (!g_intro.releasing() && !g_recovering && connected() && g_coop_attempt && adventure::active()) {
        auto* play = play_mode();
        const auto* run = adventure::current();
        auto cinematic = current_cinematic();
        if (play && readable(play, 0x94) && field<int>(play, 0x90) == 1 && run &&
            cinematic.manager == reinterpret_cast<uintptr_t>(self) &&
            (!native_message || native_message != 0x0445F729 || field<bool>(self, 0x3A)) &&
            g_intro.capture(run->run_id, cinematic, native_message)) {
            adventure_flow::confirm_intro();
            return true;
        }
    }
    return false;
}

bool defer_talk_end(void* self, uint32_t native_message)
{
    if (g_recovering || !connected() || !g_coop_attempt || !adventure::active()) return false;
    const auto* run = adventure::current();
    auto scene = adventure_flow::scene();
    auto cinematic = current_cinematic();
    if (!run || scene.kind != adventure_flow::Kind::Talk || scene.run != run->run_id ||
        cinematic.manager != reinterpret_cast<uintptr_t>(self) || !g_talk.owns(scene.run, cinematic)) return false;
    // Both native controls publish Continue. The manager's wait type selects
    // Finished (+3B) versus Next (+3C); never confirm an intermediate Next page.
    if (native_message == 0x0445F729 && (!field<bool>(self, 0x3A) || !field<bool>(self, 0x3B))) return false;
    // Native Skip can jump to another script stage instead of leaving Talk.
    if (native_message == 0x044EE076 && field<int>(self, 0x154) != -1) return false;
    if (!g_talk.capture(scene.run, scene.serial, cinematic, native_message)) return false;
    adventure_flow::finish_talk();
    return true;
}

bool SK_THISCALL cinematic_message(void* self, uint32_t type, void* data)
{
    // Native caption Continue changes waiting flags and UI before the cinematic
    // eventually calls EndCinematic. These three branches read no payload.
    if ((type == 0x0445F729 || type == 0x044EE075 || type == 0x044EE076) &&
        (defer_intro_end(self, type) || defer_talk_end(self, type)))
        return true;
    return original_cinematic_message(self, type, data);
}

void SK_THISCALL end_cinematic(void* self)
{
    // Retain a fallback for direct native end/skip paths, also before cleanup.
    if (defer_intro_end(self, 0) || defer_talk_end(self, 0)) return;
    if (connected() && g_coop_attempt && adventure::outcome_locked()) {
        auto* play=play_mode(); auto cinematic=current_cinematic();
        const int state=adventure_world::terminal_state(play);
        if (play && state && field<int>(play,0x90)==state && field<int>(play,0x94)!=2 &&
            cinematic.manager==reinterpret_cast<uintptr_t>(self) && cinematic.active && cinematic.id) {
            g_result_end_run=adventure::outcome_run(); g_result_end_play=play; g_result_end_id=cinematic.id;
        }
    }
    original_end_cinematic(self);
}

void release_intro(uint64_t run)
{
    // Reacquire the owned singleton rather than dereferencing a captured UI
    // pointer after cancellation, scene replacement or a later attempt.
    auto cinematic = current_cinematic();
    auto release = g_intro.release(run, cinematic);
    if (release.action == adventure_intro::Action::EndCinematic ||
        release.action == adventure_intro::Action::CinematicMessage || release.action == adventure_intro::Action::PlayMessage) {
        g_intro_settling = run; g_settling_deadline = g_now + 10.0;
        g_settling_timeout_queued = false;
    }
    struct Releasing {
        adventure_intro::Release release;
        ~Releasing() { g_intro.finish(release); }
    } releasing{release};
    if (release.action == adventure_intro::Action::EndCinematic)
        original_end_cinematic(reinterpret_cast<void*>(cinematic.manager));
    else if (release.action == adventure_intro::Action::CinematicMessage)
        original_cinematic_message(reinterpret_cast<void*>(cinematic.manager), release.native_message, nullptr);
    else if (release.action == adventure_intro::Action::PlayMessage) {
        std::array<uint8_t,16> data{};
        if (auto* play = play_mode()) original_message(play, 0x044F1189, data.data());
    } else if (release.action == adventure_intro::Action::Changed) {
        adventure::cancel("The native beam-down cinematic changed while waiting for the party. Please retry the adventure.");
    }
    if (g_intro_settling == run) {
        if (auto* play = play_mode(); play && readable(play, 0x94) && field<int>(play, 0x90) == 3) g_intro_settling = 0;
    }
}

bool present_talk(const adventure_flow::Scene& scene)
{
    if (g_talk.settling() || !adventure_world::present_talk(scene)) return false;
    auto cinematic = current_cinematic();
    auto* play = play_mode();
    if (!play || !readable(play, 0xDC) || cinematic.id != field<uint32_t>(play, 0xD8) ||
        !g_talk.begin(scene.run, scene.serial, cinematic)) {
        adventure::cancel("The native conversation cinematic could not be retained. Please retry the adventure.");
        return false;
    }
    g_talk_scene = scene;
    return true;
}

void release_talk(const adventure_flow::Scene& scene)
{
    auto cinematic = current_cinematic();
    auto release = g_talk.release(scene.run, scene.serial, cinematic);
    if (release.action == adventure_intro::Action::Changed || release.action == adventure_intro::Action::Ignore) {
        adventure::cancel("The native conversation changed while waiting for the party. Please retry the adventure.");
        return;
    }
    g_settling_deadline = g_now + 10.0; g_settling_timeout_queued = false;
    // Replaying Continue can advance the script on a later update. The matching
    // native end message performs TalkFinish/host goal commit exactly once.
    if (release.action == adventure_intro::Action::CinematicMessage)
        original_cinematic_message(reinterpret_cast<void*>(cinematic.manager), release.native_message, nullptr);
    else if (release.action == adventure_intro::Action::EndCinematic)
        original_end_cinematic(reinterpret_cast<void*>(cinematic.manager));
    g_talk.replayed(release);
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
    g_intro.reset();
    adventure_results::reset();
    g_talk.reset(); g_talk_scene = {}; g_intro_settling = 0;
    g_result_end_run=0; g_result_end_play=nullptr; g_result_end_id=0;
    g_settling_deadline = 0; g_settling_timeout_queued = false;
    adventure_world::begin_native_attempt(self);
    original_initialize(self);
}

void SK_THISCALL record_statistic(void* self, uint32_t statistic, const dbpf::Key* asset,
                                const int64_t* values, uint32_t count)
{
    // The native results panel submits these two adventure result statistics to
    // Pollinator. Keep local captain progression and the rest of its result UI intact.
    if (g_coop_attempt && asset && (*asset == g_score_adventure || *asset == g_score_local_adventure) &&
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
    if (g_preview) {
        if (!preview_attempt() || adventure != g_preview->config.adventure ||
            captain != g_preview->config.authored_avatar || !preview_bound(false) || g_preview->returning) {
            error = "The shared editor playtest no longer matches its confirmed draft.";
            return false;
        }
        g_preview->run = adventure::current()->run_id;
        g_coop_attempt = true;
        g_score_adventure = adventure;
        const auto& local = g_preview->snapshot.source;
        g_score_local_adventure = {local.instance, local.type, local.group};
        coop_ui::close_for_adventure_launch();
        g_replay = true;
        const uint64_t generation = g_preview_generation;
        auto native_launch = g_preview->config.launch;
        const bool entered = native_launch(error);
        g_replay = false;
        if (!entered || generation != g_preview_generation || !preview_current() || !preview_bound(true)) {
            if (error.empty()) error = "The native shared editor playtest did not start.";
            return false;
        }
        g_preview->launched = true;
        error.clear();
        return true;
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
    g_score_local_adventure = {};
    g_replay = true;
    // A visible preparation dialog can hold native loading completion. This
    // callback already runs outside the frame, so hide it before mode entry.
    coop_ui::close_for_adventure_launch();
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
    if (g_initializing || !play || !data || !readable(data, 0x128) || !game::avatar()) return false;
    void* resource = field<void*>(data, 0x10);
    void* loading = cdecl_call<void*>(H->sym("SporeCoop::Adventure::LoadingScreenGet"));
    if (g_preview) {
        return preview_attempt() && g_preview->launched && !g_preview->returning && key == g_preview->config.adventure &&
            preview_bound(true) && !field<bool>(data, 0x14) &&
            loading && readable(loading, 0x371) && !field<bool>(loading, 0x370);
    }
    return field<bool>(data, 0x124) && resource && readable(resource, 0x14) && field<dbpf::Key>(resource, 8) == key &&
           !field<bool>(data, 0x14) && field<int>(object, 0xCC) == 2 &&
           loading && readable(loading, 0x371) && !field<bool>(loading, 0x370);
}

bool paused()
{
    auto* p=play_mode();
    if(simulation_held() || (p && field<int>(p,0x90)!=3)) return true;
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
        // Native control fences own downed movement at the actual simulation
        // boundary. Snapshot application must not jerk the pose every 100 ms.
    }
}
} // namespace

bool set_editor_preview(EditorPreview config, std::string& error)
{
    if (!H || !available() || g_preview || adventure::active() || adventure::outcome_locked() || adventure::cleanup_pending() ||
        !connected() || std::all_of(config.project.begin(), config.project.end(), [](uint8_t v) { return v == 0; }) ||
        !config.epoch || !config.round || !config.adventure.instance || config.adventure.type != 0x366A930D ||
        !config.authored_avatar.instance || !config.authored_avatar.type ||
        !config.current || !config.launch || !config.return_editor || !config.current()) {
        error = "The shared editor playtest has no current confirmed draft or native adventure adapter.";
        return false;
    }
    Preview preview;
    preview.config = std::move(config);
    if (!editor_native::capture(preview.snapshot, error)) return false;
    preview.mode = scenario();
    preview.data = preview.mode ? field<void*>(preview.mode, 0x74) : nullptr;
    preview.resource = readable(preview.data, 0x128) ? field<void*>(preview.data, 0x10) : nullptr;
    if (!preview.snapshot.source.instance || preview.snapshot.source.type != 0x366A930D ||
        preview.snapshot.bytes.empty() ||
        dbpf::Key{preview.snapshot.captain.instance, preview.snapshot.captain.type, preview.snapshot.captain.group} != preview.config.authored_avatar) {
        error = "The editor playtest requires its current saved draft and authored captain.";
        return false;
    }
    g_preview = std::move(preview);
    if (!preview_bound(false)) {
        g_preview.reset();
        error = "The native editor identity changed while preparing its shared playtest.";
        return false;
    }
    ++g_preview_generation;
    error.clear();
    return true;
}
bool editor_preview() { return g_preview.has_value(); }
void clear_editor_preview() { ++g_preview_generation; g_preview.reset(); }

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
        "SporeCoop::Adventure::HandlePlayMessage", "Simulator::cCinematicManager::Get",
        "SporeCoop::cCinematicManager_EndCinematic", "SporeCoop::Adventure::HandleCinematicMessage"};
    g_symbols = H->sym_require(M, required, 19) == 0;
    adventure_results::Ctx results;
    results.run=adventure::outcome_run; results.applied=adventure_world::result_applied; results.feed=C.feed;
    g_symbols=adventure_results::init(H,M,std::move(results)) && g_symbols;
    adventure_pause_native::Ctx native_pause;
    native_pause.active = [] { return adventure_pause::active() && game::mode_id() == modes::kScenario; };
    native_pause.held = adventure_pause::held;
    native_pause.local_intent = adventure_pause::local_intent;
    native_pause.reset_movement = C.reset_pause_movement;
    g_symbols = adventure_pause_native::init(H, M, std::move(native_pause)) && g_symbols;
    adventure_pause::Ctx pause;
    pause.sess = C.sess;
    pause.run = [] { const auto* run = adventure::current(); return run ? run->run_id : 0; };
    pause.running = adventure::running;
    pause.members = [] { const auto* run = adventure::current(); return run ? run->roster : std::vector<uint8_t>{}; };
    pause.eligible = adventure::participating;
    pause.local_intent = adventure_pause_native::local_intent;
    pause.changed = adventure_pause_native::changed;
    g_symbols = adventure_pause::init(std::move(pause)) && g_symbols;
    adventure_control::Ctx controls;
    controls.active = [] { return connected() && adventure::running() && game::mode_id() == modes::kScenario; };
    controls.local_avatar = game::avatar;
    controls.local_paused = adventure_pause::local_requested;
    controls.terminal_pending = [] { return connected() && game::mode_id() == modes::kScenario && adventure::outcome_locked(); };
    controls.captain_state = [](void* object, ga::Captain& result) {
        if (!C.sess || !C.captain_of) return false;
        for (const auto& player : C.sess->players()) {
            adventure::CaptainState state;
            if (C.captain_of(player.id) == object && adventure::captain_state(player.id, state)) {
                result = state.captain;
                return true;
            }
        }
        return false;
    };
    g_symbols = adventure_control::init(H, M, std::move(controls)) && g_symbols;
    adventure_collect::init(H, M);
    adventure_catalog::init(H, M);
    adventure_captains::init(H, M, C.sess);
    adventure_world::Ctx world;
    world.sess = C.sess; world.feed = C.feed; world.captain_of = C.captain_of;
    world.captain_can_act = [](uint8_t player) {
        adventure::CaptainState state;
        return !adventure::outcome_locked() && adventure::captain_state(player, state) && state.captain.life == ga::Life::Alive;
    };
    world.run = [] { const auto* run = adventure::current(); return run ? run->run_id : 0; };
    world.active = [] { return adventure::running(); };
    world.held=simulation_held;
    world.goals_held=adventure_flow::native_scene_held;
    world.explosive_environment=adventure_explosives::environmental_damage;
    world.explosive_visual=adventure_explosives::visual_only;
    world.simulation_time=[] {
        adventure::CaptainState state;
        return C.sess && adventure::captain_state(C.sess->my_id(),state) ? state.simulation_time : 0.0;
    };
    world.teleport=C.teleport;
    world.interaction_failed=[](const std::string& reason) { adventure::cancel(reason); };
    world.difficulty = [] { const auto* run = adventure::current(); return run ? run->difficulty : adventure::settings(); };
    world.party_size = [] { const auto* run = adventure::current(); return run ? run->roster.size() : size_t(1); };
    world.captain_damage = adventure::captain_damage;
    world.captain_heal = adventure::captain_heal;
    world.result = [](bool success) { adventure::complete(success); };
    adventure_world::init(H, M, std::move(world));
    auto pickups=adventure_world::pickups_context();
    pickups.active=[] { return adventure::native_attempt() && game::mode_id()==modes::kScenario; };
    pickups.held=[] { return !adventure::running() || simulation_held(); };
    g_symbols=adventure_pickups::init(H,M,std::move(pickups)) && g_symbols;
    auto explosives=adventure_world::explosives_context();
    explosives.active=[] { return adventure::native_attempt() && game::mode_id()==modes::kScenario; };
    explosives.held=[] { return !adventure::running() || simulation_held(); };
    g_symbols=adventure_explosives::init(H,M,std::move(explosives)) && g_symbols;
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
            !g_frozen && !simulation_held() && game::mode_id() == modes::kScenario &&
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
    flow.present=present_talk;
    flow.release=[](const adventure_flow::Scene& s) {
        if(s.kind==adventure_flow::Kind::Talk) release_talk(s);
        else if(s.kind==adventure_flow::Kind::Intro) release_intro(s.run);
    };
    flow.settling=[] {
        const auto* d = adventure::current();
        return d && (g_intro_settling == d->run_id || (g_talk_scene.run == d->run_id && g_talk.settling()));
    };
    // The native letterbox HUD shows confirmations; F9 remains a manual party/chat action.
    flow.waiting=[](bool) {};
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
    if(g_symbols)
        g_symbols=H->hook_create(M,reinterpret_cast<void*>(H->sym(required[17])),reinterpret_cast<void*>(end_cinematic),
                                 reinterpret_cast<void**>(&original_end_cinematic))==0;
    if(g_symbols)
        g_symbols=H->hook_create(M,reinterpret_cast<void*>(H->sym(required[18])),reinterpret_cast<void*>(cinematic_message),
                                 reinterpret_cast<void**>(&original_cinematic_message))==0;
    return available();
}

adventure::Ctx coordinator_context()
{
    adventure::Ctx context;
    context.sess = C.sess; context.feed = C.feed; context.invalidate_ready = C.invalidate_ready;
    context.native_available = available; context.native_reason = unavailable;
    context.collect = [](const dbpf::Key& root, std::vector<dbpf::Record>& records, std::string& error) {
        if (!g_preview) return adventure_collect::collect(root, records, error);
        if (!preview_attempt() || root != g_preview->config.adventure || !preview_bound(false) || g_preview->returning) {
            error = "The shared editor draft changed before playtest content collection.";
            return false;
        }
        g_preview->run = adventure::current()->run_id;
        return adventure_collect::collect_snapshot(root, g_preview->snapshot.bytes, records, error);
    };
    context.content_registration = content_registration;
    context.captain_validated = adventure_captains::validated;
    context.choose_captain = [](const dbpf::Key& locked, auto selected) {
        if (g_preview) {
            if (!preview_attempt() || locked != g_preview->config.authored_avatar || g_preview->returning ||
                !adventure_captains::choose(locked, std::move(selected))) {
                adventure::cancel("The shared editor playtest captain no longer matches its confirmed draft.");
            } else g_preview->run = adventure::current()->run_id;
            return;
        }
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
        if (g_preview) {
            // The editor controller owns the return transition and restoration.
            // Preparation cancellation must not switch the native app to Galaxy.
            adventure_captains::close_picker();
            adventure_native::reset();
            preview_return();
            return;
        }
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
    context.native_result_applied = [](bool success) { return adventure_world::result_applied(adventure::outcome_run(), success); };
    context.prepare_result_exit=[] { return g_preview ? true : adventure_results::prepare_exit(); };
    context.native_settling = [] {
        const auto* d = adventure::current();
        return d && ((adventure_flow::release_pending() && adventure_flow::scene().run == d->run_id) ||
            g_intro_settling == d->run_id || (g_talk_scene.run == d->run_id && g_talk.settling()) ||
            adventure_world::goal_application_pending());
    };
    context.incoming_damage=adventure_pickups::adjust_damage;
    context.native_message=adventure_native::on_message;
    return context;
}

void tick(double now) {
    g_now = now;
    schedule_preview_ready();
    schedule_preview_intro();
    if ((g_intro_settling || g_talk.settling()) && now >= g_settling_deadline && !g_settling_timeout_queued) {
        g_settling_timeout_queued = true;
        struct Timeout { uint64_t run, intro_generation, talk_generation; };
        const auto* d = adventure::current();
        H->run_outside_frame([](void* data) {
            std::unique_ptr<Timeout> timeout(static_cast<Timeout*>(data));
            const auto* current = adventure::current();
            if (!current || current->run_id != timeout->run || timeout->intro_generation != g_intro.generation() ||
                timeout->talk_generation != g_talk.generation() || (!g_intro_settling && !g_talk.settling())) return;
            adventure::cancel("The native cinematic did not finish after the party confirmed. Please retry the adventure.");
        }, new Timeout{d ? d->run_id : 0, g_intro.generation(), g_talk.generation()});
    }
    adventure_captains::tick(now); adventure_pause::tick(); adventure_flow::tick(); adventure_world::tick(now);
    adventure_explosives::tick(); adventure_pickups::tick();
}
void blocked_update(void* creature, int milliseconds) { adventure_social::blocked_update(creature, milliseconds); }
bool party_paused() { return adventure_pause::held(); }
void creature_update(void* creature, bool before) { adventure_control::creature_update(creature, before); }
void on_message(uint8_t from, net::Reader& message)
{
    if (!message.left()) return;
    if (*message.cur() >= 192) adventure_captains::on_message(from, message);
    else if(*message.cur()>=144 && *message.cur()<=151) adventure_explosives::on_message(from,message);
    else if(*message.cur()>=152 && *message.cur()<=159) adventure_pickups::on_message(from,message);
    else if(*message.cur()>=160 && *message.cur()<=163) adventure_flow::on_message(from,message);
    else if(*message.cur()>=164 && *message.cur()<=165) adventure_pause::on_message(from,message);
    else adventure_world::on_message(from, message);
}
bool update_filter(void* creature)
{
    if (connected() && game::mode_id() == modes::kScenario) {
        if (g_frozen || simulation_held()) return false;
    }
    (void)creature;
    return true; // Guest NPC authority is applied at NPCTickAI, preserving visual Update.
}
bool damage_filter(void* victim, float& amount, int type, void* attacker)
{
    if (connected() && game::mode_id() == modes::kScenario && adventure::outcome_locked()) return false;
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
    adventure_results::reset();
    adventure_pause::reset(); adventure_pause_native::reset();
    adventure_control::reset();
    adventure_pickups::reset(); adventure_explosives::reset();
    adventure_flow::reset(); g_intro.reset(); g_talk.reset(); g_talk_scene = {};
    g_result_end_run=0; g_result_end_play=nullptr; g_result_end_id=0;
    g_intro_settling = 0; g_settling_deadline = 0; g_settling_timeout_queued = false;
    if (C.reset_movement) C.reset_movement();
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
void shutdown() { reset(); clear_editor_preview(); }
std::string status()
{
    auto social = adventure_social::counters();
    return available() ? adventure::status() + "\n" + adventure_flow::status()+"\n" + adventure_captains::status() + "\n" + adventure_world::status() +
        "native party relationship aliases=" + std::to_string(adventure_relationships::aliased_calls()) +
        " authoritative=" + std::to_string(adventure_relationships::authoritative_calls()) + "\n" +
        "native social presentation: starts=" + std::to_string(social.starts) + " ticks=" + std::to_string(social.ticks) +
        " ends=" + std::to_string(social.ends) + " rejected=" + std::to_string(social.rejected) +
        " input_allowed=" + std::to_string(social.input_queries_allowed) + "\n" +
        adventure_pickups::status()+"\n"+adventure_explosives::status()+"\n"+adventure_control::status()+adventure_pause::status()+
        "native intro cinematic: pending="+std::to_string(g_intro.pending())+" run="+std::to_string(g_intro.run())+
        " id="+std::to_string(g_intro.cinematic().id)+"\n"+
        "native cinematic settling: intro="+std::to_string(g_intro_settling)+" talk="+std::to_string(g_talk.settling())+
        " talk_run="+std::to_string(g_talk_scene.run)+" talk_serial="+std::to_string(g_talk_scene.serial)+"\n"+
        "native terminal messages: accepted="+std::to_string(g_result_messages_accepted)+
        " rejected="+std::to_string(g_result_messages_rejected)+" token_run="+std::to_string(g_result_end_run)+
        " token_id="+std::to_string(g_result_end_id)+"\n"+
        adventure_results::status()+
        "native co-op results: suppressed score submissions=" + std::to_string(g_suppressed_scores) : unavailable();
}
} // namespace adventure_native
