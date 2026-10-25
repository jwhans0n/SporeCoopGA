#include "adventure_results.h"

#include <cstring>
#include <utility>
#include "game.h"
#include "sui.h"
#include "sporekit_game.hpp"

namespace adventure_results {
namespace {
using namespace skg;
const SkHost* H;
Ctx C;
bool ready;
uintptr_t mode_pointer, handler, end_cinematic, cinematic_get, layout_container;
uint64_t saved_run, issued_run, notified_run;
void* saved_play;
uint64_t clicks, saved, ended, rewards;

uint8_t* play()
{
    if (!ready || game::mode_id()!=0x01654C10) return nullptr;
    auto* mode=field<uint8_t*>(reinterpret_cast<void*>(mode_pointer),0);
    if (!mode || !readable(mode,0xD0) || field<uint32_t>(mode,0xCC)!=2) return nullptr;
    auto* p=field<uint8_t*>(mode,0x78);
    return p && readable(p,0x108) ? p : nullptr;
}
}

bool init(const SkHost* host, SkMod*, Ctx ctx)
{
    if (!host || !host->exe_sha256 || !host->sym || !ctx.run || !ctx.applied) return false;
    const char* hash=host->exe_sha256();
    if (!hash || std::strcmp(hash,"dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37")) return false;
    H=host; C=std::move(ctx);
    mode_pointer=H->sym("App::cScenarioMode::ptr");
    handler=H->sym("SporeCoop::Adventure::ResultsHandleUIMessage");
    end_cinematic=H->sym("SporeCoop::cCinematicManager_EndCinematic");
    cinematic_get=H->sym("Simulator::cCinematicManager::Get");
    layout_container=H->sym("UTFWin::UILayout::GetContainerWindow");
    ready=mode_pointer && handler && end_cinematic && cinematic_get && layout_container;
    return ready;
}

bool prepare_exit()
{
    auto* p=play(); const uint64_t run=C.run ? C.run() : 0;
    if (!p || !run) return false;
    if (saved_run==run && saved_play==p) return true;
    const int state=field<int>(p,0x90);
    if ((state!=5 && state!=6) || !C.applied(run,state==5)) return false;
    // Finish only this terminal cinematic. Reward dialogs belong to the result
    // panel after +94=2 and are never skipped by this path.
    if (field<int>(p,0x94)!=2) {
        void* cinematic=cdecl_call<void*>(cinematic_get);
        if (cinematic && readable(cinematic,0x160) && field<uint32_t>(cinematic,0x2C) &&
            field<uint32_t>(cinematic,0x148)) {
            ++ended; thiscall<void>(end_cinematic,cinematic);
        }
        return false;
    }
    if (issued_run==run) return false;
    auto* mode=field<uint8_t*>(reinterpret_cast<void*>(mode_pointer),0);
    auto* display=field<uint8_t*>(mode,0x6C);
    auto* results=display && readable(display,0x130) ? field<uint8_t*>(display,0x12C) : nullptr;
    if (!results || !readable(results,0xF8) || field<void*>(results,0x10)!=p) return false;
    // Results+18 is a UILayout, not an IWindow. Its virtual slots differ;
    // obtain the real native window before any window vtable/tree operation.
    auto* layout=field<uint8_t*>(results,0x18);
    if (!layout || !readable(layout,0x18)) return false;
    auto* container=field<uint8_t*>(layout,0x14);
    if (!container || !readable(container,0x8C)) return false;
    auto window=thiscall<sui::Win>(layout_container,layout);
    if (!window || !sui::visible(window)) return false;
    const int phase=field<int>(results,0xD4);
    auto button=sui::find(window,0x07C79948);
    if ((phase!=0 && phase!=9) || !button || !sui::visible(button) || !(sui::flags(button)&sui::kFlagEnabled)) {
        if (phase==4 && notified_run!=run) {
            notified_run=run; ++rewards;
            if (C.feed) C.feed("Finish selecting your captain rewards to return to the party menu.");
        }
        return false;
    }
    // Native ComponentActivated follows the same Done path as a real button:
    // save the captain, then publish its Saved choice. Claim before reentry.
    issued_run=run; ++clicks;
    const sui::Msg message{button,0,sui::kMsgComponentActivated,0x07C79948,0,0,0};
    thiscall<bool>(handler,results,button,&message);
    return saved_run==run && saved_play==p;
}

void accepted(void* p, int choice)
{
    const uint64_t run=C.run ? C.run() : 0;
    if (!run || (choice!=1 && choice!=2) || !p || p!=play()) return;
    const int state=field<int>(p,0x90);
    if ((state!=5 && state!=6) || !C.applied(run,state==5)) return;
    if (saved_run!=run || saved_play!=p) ++saved;
    saved_run=run; saved_play=p;
}

void reset() { saved_run=issued_run=notified_run=0; saved_play=nullptr; }
std::string status()
{
    return "native result saves: saved_run="+std::to_string(saved_run)+" issued_run="+std::to_string(issued_run)+
        " clicks="+std::to_string(clicks)+" saved="+std::to_string(saved)+" cinematics="+std::to_string(ended)+
        " reward_waits="+std::to_string(rewards)+"\n";
}
}
