#include "adventure_hud.h"
#include "adventure_hud_model.h"
#include "adventure.h"
#include "adventure_flow.h"
#include "game.h"
#include "modes.h"
#include "sui.h"

#include <d3d9.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <sstream>
#include <utility>

namespace adventure_hud {
namespace {
const SkHost* H;
Ctx C;
bool enabled, queued;
uint64_t generation, ring_run;
uint64_t panel_run, panel_serial;
double last_refresh=-1;
IDirect3DStateBlock9* saved_state;
IDirect3DDevice9* saved_device;
bool device_ready=true;
sui::Win parent, check, header, intro_text;
sui::Rect intro_text_area;
std::array<sui::Win,4> labels{};
bool check_disabled, check_was_enabled;
std::string panel_text;
size_t ring_count;
struct Ring {
    ga::Point position;
    float progress=0;
    bool reviving=false;
    std::string text;
};
std::array<Ring,4> rings;
using Rect=adventure_hud_model::Rect;
Rect rect(sui::Rect r) { return {r.x1,r.y1,r.x2,r.y2}; }
sui::Rect local(Rect r, sui::Rect origin) { return {r.x1-origin.x1,r.y1-origin.y1,r.x2-origin.x1,r.y2-origin.y1}; }
sui::Win shown(sui::Win root,uint32_t id,unsigned depth=0)
{
    if (!root || !sui::visible(root) || depth>64) return nullptr;
    if (sui::control_id(root)==id) return root;
    for (auto child:sui::children(root)) if (auto match=shown(child,id,depth+1)) return match;
    return nullptr;
}

void clear_panel()
{
    if (check && check_disabled) sui::set_enabled(check,check_was_enabled);
    check_disabled=false;
    for (auto& label:labels) {
        if (label) { if (sui::parent(label)==parent) sui::remove_child(parent,label); sui::release(label); }
        label=nullptr;
    }
    if (header) { if (sui::parent(header)==parent) sui::remove_child(parent,header); sui::release(header); }
    header=nullptr;
    if (intro_text) { sui::set_area(intro_text,intro_text_area); sui::release(intro_text); intro_text=nullptr; }
    if (check) sui::release(check);
    if (parent) sui::release(parent);
    check=parent=nullptr; panel_text.clear(); panel_run=panel_serial=0;
}
void refresh_panel()
{
    adventure_flow::ConfirmationView view;
    if ((C.blocked && C.blocked()) || !adventure_flow::confirmation_view(view)) { clear_panel(); return; }
    auto root=sui::main_window();
    auto button=shown(root,0x0445C99D), bar=shown(root,0x045023D0);
    if (!root || !button || !bar || !sui::visible(button) || !sui::visible(bar)) { clear_panel(); return; }
    auto layout=adventure_hud_model::confirmations(rect(sui::screen_rect(bar)),rect(sui::screen_rect(button)),view.players.size());
    if (!layout.valid) { clear_panel(); return; }
    if (parent!=root || check!=button || panel_run!=view.scene.run || panel_serial!=view.scene.serial) {
        clear_panel(); parent=root; check=button; sui::add_ref(parent); sui::add_ref(check);
        panel_run=view.scene.run; panel_serial=view.scene.serial;
        if (view.scene.kind==adventure_flow::Kind::Intro) {
            intro_text=shown(root,0x0445C9AB);
            if (intro_text) { sui::add_ref(intro_text); intro_text_area=sui::area(intro_text); }
        }
        uint32_t font=sui::font(check);
        header=sui::label(parent,{},"",font,0xFFFFFFFF,sui::kAlignLeft);
        if (header) sui::set_ids(header,0xC00F3000,0);
        for (size_t i=0;i<labels.size();++i) {
            labels[i]=sui::label(parent,{},"",font,0xFFFFFFFF,sui::kAlignLeft);
            if (labels[i]) sui::set_ids(labels[i],0xC00F3001+uint32_t(i),0);
        }
    }
    auto origin=sui::screen_rect(parent);
    if (intro_text) {
        auto text_origin=sui::screen_rect(sui::parent(intro_text)), area=intro_text_area;
        // SetArea takes layout coordinates; the native label is bottom-anchored.
        float anchor=sui::real_area(intro_text).y2-sui::area(intro_text).y2;
        area.y2=std::max(area.y1,layout.header.y1-text_origin.y1-4.f-anchor);
        sui::set_area(intro_text,area);
    }
    size_t ready=std::count_if(view.players.begin(),view.players.end(),[](const auto& p){return p.confirmed;});
    panel_text=(view.scene.kind==adventure_flow::Kind::Talk?"Conversation ":"Beam-down ")+
        std::to_string(ready)+"/"+std::to_string(view.players.size());
    if (header) {
        sui::set_area(header,local(layout.header,origin)); sui::set_caption(header,panel_text);
        sui::set_flag(header,sui::kFlagAlwaysInFront,true); sui::bring_to_front(parent,header);
    }
    for (size_t i=0;i<labels.size();++i) {
        auto label=labels[i]; if (!label) continue;
        bool have=i<view.players.size(); sui::set_visible(label,have); if (!have) continue;
        const auto& p=view.players[i];
        auto r=layout.players[i];
        size_t name_length=size_t(std::clamp((r.x2-r.x1)/8.f-14.f,4.f,8.f));
        auto text="P"+std::to_string(int(p.player)+1)+" "+adventure_hud_model::name(p.name,name_length)+
            (p.confirmed?": Ready":p.pending?": Sent":": Waiting");
        sui::set_area(label,local(layout.players[i],origin)); sui::set_caption(label,text);
        sui::set_label_color(label,p.confirmed?0xFF8CF08C:0xFFFFD866);
        sui::set_flag(label,sui::kFlagAlwaysInFront,true); sui::bring_to_front(parent,label);
        if (p.local && (p.confirmed || p.pending) && !check_disabled) {
            check_was_enabled=(sui::flags(check)&sui::kFlagEnabled)!=0;
            sui::set_enabled(check,false); check_disabled=true;
        }
    }
}
void refresh_rings()
{
    ring_count=0; ring_run=0;
    const auto* run=adventure::current();
    if (!run || !adventure::running() || adventure_flow::held() || (C.blocked && C.blocked()) ||
        game::mode_id()!=modes::kScenario || !C.players) return;
    const auto players=C.players();
    auto local=std::find_if(players.begin(),players.end(),[](const auto& p){return p.local;});
    adventure::CaptainState viewer;
    if (local==players.end() || !adventure::captain_state(local->id,viewer)) return;
    ring_run=run->run_id;
    for (const auto& p:players) {
        adventure::CaptainState state;
        if (!adventure::captain_state(p.id,state) || state.captain.life!=ga::Life::Downed) continue;
        auto a=state.captain.position,b=viewer.captain.position;
        float dx=a.x-b.x,dy=a.y-b.y,dz=a.z-b.z;
        if (!p.local && dx*dx+dy*dy+dz*dz>144.f) continue;
        if (ring_count==rings.size()) break;
        auto& ring=rings[ring_count++]; ring.position=a;
        ring.progress=adventure_hud_model::progress(state.revive_progress);
        ring.reviving=state.captain.reviver!=ga::kNoPlayer;
        ring.text="P"+std::to_string(int(p.id)+1)+" "+adventure_hud_model::name(p.name)+"  "+
            adventure_hud_model::revive_timer(ring.progress,ring.reviving,state.automatic_remaining);
    }
}
struct Vertex { float x,y,z=0,rhw=1; DWORD color; };
void quad(IDirect3DDevice9* device,float x,float y,float width,float height,DWORD color)
{
    if (width<=0 || height<=0) return;
    Vertex vertices[]={{x,y,0,1,color},{x+width,y,0,1,color},{x,y+height,0,1,color},{x+width,y+height,0,1,color}};
    device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(Vertex));
}
ga::Point unit(ga::Point p)
{
    float length=std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);
    return length>0.0001f?ga::Point{p.x/length,p.y/length,p.z/length}:ga::Point{0,0,1};
}
ga::Point cross(ga::Point a,ga::Point b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
bool project(ga::Point p,float (&screen)[2])
{
    float point[]={p.x,p.y,p.z};
    return H->world_to_screen(point,screen) && std::isfinite(screen[0]) && std::isfinite(screen[1]);
}
void release_state()
{
    if (saved_state) saved_state->Release();
    saved_state=nullptr;
    saved_device=nullptr;
}
void device_event(void*,int event,void*)
{
    if (event==SK_EV_DEVICE_LOST) { device_ready=false; release_state(); }
    else { device_ready=true; last_refresh=-1; }
}
void refresh()
{
    refresh_panel(); refresh_rings();
    auto device=static_cast<IDirect3DDevice9*>(H->d3d_device());
    if (saved_device!=device) release_state();
    if (ring_count && device_ready && !saved_state) {
        if (device && SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL,&saved_state))) saved_device=device;
    }
}
} // namespace

void init(const SkHost* host,SkMod* mod,Ctx ctx)
{
    H=host; C=std::move(ctx); enabled=true; ++generation; queued=false; last_refresh=-1;
    H->command_add(mod,"coop.adventure.hud","Read native confirmation and revive HUD state",
        [](void*,int,const char*const*,SkOut* out) {
            refresh(); H->out_printf(out,"%s\n",status().c_str()); return 0;
        },nullptr,SK_CMD_OUTSIDE_FRAME);
    H->subscribe(mod,SK_EV_DEVICE_LOST,device_event,nullptr);
    H->subscribe(mod,SK_EV_DEVICE_RESET,device_event,nullptr);
}
void tick()
{
    if (!enabled || queued || H->time_seconds()-last_refresh<0.1) return;
    queued=true; last_refresh=H->time_seconds();
    H->run_outside_frame([](void* arg) {
        std::unique_ptr<uint64_t> expected(static_cast<uint64_t*>(arg));
        if (*expected!=generation || !enabled) return;
        queued=false; refresh();
    },new uint64_t(generation));
}
void draw()
{
    const auto* run=adventure::current();
    if (!enabled || !ring_count || !run || run->run_id!=ring_run || !adventure::running() ||
        adventure_flow::held() || (C.blocked && C.blocked())) return;
    auto device=static_cast<IDirect3DDevice9*>(H->d3d_device());
    if (!device || device!=saved_device || !device_ready || !saved_state || FAILED(saved_state->Capture())) return;
    device->SetVertexShader(nullptr); device->SetPixelShader(nullptr); device->SetTexture(0,nullptr);
    device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
    device->SetRenderState(D3DRS_LIGHTING,FALSE); device->SetRenderState(D3DRS_ZENABLE,FALSE);
    device->SetRenderState(D3DRS_FOGENABLE,FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE); device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE); device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_ALPHA);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA); device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    device->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1); device->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    device->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1); device->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    device->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    std::array<std::array<float,2>,4> captions{};
    std::array<bool,4> have_caption{};
    int screen_width=0,screen_height=0;
    H->screen_size(&screen_width,&screen_height);
    for (size_t n=0;n<ring_count;++n) {
        const auto& ring=rings[n]; auto up=unit(ring.position);
        auto a=unit(cross(up,std::abs(up.z)<0.9f?ga::Point{0,0,1}:ga::Point{0,1,0}));
        auto b=cross(up,a);
        constexpr size_t segments=64; std::array<Vertex,segments*2> vertices{}; size_t count=0;
        DWORD color=ring.reviving?0xEE8CF08C:0xEEFFB060;
        for (size_t i=0;i<segments;++i) {
            float screen[2][2]; bool visible=true;
            for (size_t j=0;j<2;++j) {
                float angle=float(i+j)*6.28318530718f/float(segments), ca=std::cos(angle),sa=std::sin(angle);
                ga::Point p{ring.position.x+2.f*(a.x*ca+b.x*sa)+up.x*.04f,
                            ring.position.y+2.f*(a.y*ca+b.y*sa)+up.y*.04f,
                            ring.position.z+2.f*(a.z*ca+b.z*sa)+up.z*.04f};
                visible=project(p,screen[j]) && visible;
            }
            if (visible) for (auto& xy:screen) vertices[count++]={xy[0],xy[1],0,1,color};
        }
        if (count) device->DrawPrimitiveUP(D3DPT_LINELIST,UINT(count/2),vertices.data(),sizeof(Vertex));
        ga::Point caption{ring.position.x+up.x*1.6f,ring.position.y+up.y*1.6f,ring.position.z+up.z*1.6f};
        float xy[2];
        if (project(caption,xy)) {
            float width=adventure_hud_model::caption_width(ring.text);
            auto text=adventure_hud_model::inside_viewport(xy[0]-width*.5f,xy[1]-31,width,20,
                float(screen_width),float(screen_height));
            auto bar=adventure_hud_model::inside_viewport(xy[0]-70,xy[1]-7,140,9,
                float(screen_width),float(screen_height));
            captions[n]={text.x1,text.y1}; have_caption[n]=screen_width>0 && screen_height>0;
            quad(device,bar.x1,bar.y1,bar.x2-bar.x1,bar.y2-bar.y1,0xCC101820);
            quad(device,bar.x1+2,bar.y1+2,std::max(0.f,bar.x2-bar.x1-4)*ring.progress,
                std::max(0.f,bar.y2-bar.y1-4),0xFF8CF08C);
        }
    }
    saved_state->Apply();
    for (size_t n=0;n<ring_count;++n) if (have_caption[n])
        H->overlay_text(int(captions[n][0]),int(captions[n][1]),0xFFFFFFFF,rings[n].text.c_str());
}
void shutdown()
{
    enabled=false; ++generation; ring_count=0;
    H->run_outside_frame([](void*) { clear_panel(); release_state(); },nullptr);
}
std::string status()
{
    std::ostringstream out;
    out<<"confirmation_hud="<<(header?1:0)<<" check_disabled="<<check_disabled<<" text='"<<panel_text<<"'\n";
    if (header) { auto r=sui::screen_rect(header); out<<"confirmation_header "<<r.x1<<" "<<r.y1<<" "<<r.x2<<" "<<r.y2<<"\n"; }
    for (size_t i=0;i<labels.size();++i) if (labels[i] && sui::visible(labels[i])) {
        auto r=sui::screen_rect(labels[i]); out<<"confirmation_player "<<i<<" "<<r.x1<<" "<<r.y1<<" "<<r.x2<<" "<<r.y2<<" '"<<sui::caption(labels[i])<<"'\n";
    }
    out<<"revive_rings="<<ring_count<<" run="<<ring_run<<"\n";
    for (size_t i=0;i<ring_count;++i) out<<"revive_ring "<<i<<" progress="<<rings[i].progress<<" text='"<<rings[i].text<<"'\n";
    return out.str();
}
} // namespace adventure_hud
