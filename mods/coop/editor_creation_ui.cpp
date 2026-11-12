#include "editor_creation_ui.h"
#include "editor_creation.h"
#include "editor_runtime.h"
#include "editor_native.h"
#include "coop_ui.h"
#include "sui.h"
#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <set>
#include <cstdio>
#include <chrono>

namespace editor_creation_ui {
namespace {
const SkHost* H;
bool queued=false;
std::string display_status="browser unavailable";
uint64_t update_us=0;
constexpr uint32_t kEdit=0xC00F0401,kDecoration=0xC00F0410;
struct Card {
    sui::Win window=nullptr,edit=nullptr;
    std::array<sui::Win,4> border{};
    std::array<sui::Win,5> padlock{};
    std::vector<std::pair<sui::Win,void*>> handlers;
    void *edit_tip=nullptr,*owner_tip=nullptr;
    coop_editor::AssetCardView value;
    bool hover=false,button_hover=false,focus=false;
    float edit_width=36,edit_height=37;
    std::string identity,owner_text;
};
sui::Win main_window=nullptr;
sui::Win edit_style=nullptr;
std::string identity;
std::map<sui::Win,std::shared_ptr<Card>> cards;
void destroy(Card& card){
    for(auto [window,handler]:card.handlers)sui::remove_handler(window,handler);
    sui::remove_tooltip(card.edit,card.edit_tip);sui::remove_tooltip(card.window,card.owner_tip);
    const auto remove=[](sui::Win window){
        if(!window)return;
        if(auto owner=sui::parent(window))sui::remove_child(owner,window);
        sui::release(window);
    };
    remove(card.edit);for(auto window:card.border)remove(window);for(auto window:card.padlock)remove(window);
    if(card.window)sui::release(card.window);
    card={};
}
void clear(){
    for(auto& [window,card]:cards){(void)window;destroy(*card);}cards.clear();
    if(edit_style)sui::release(edit_style);edit_style=nullptr;
    if(main_window)sui::release(main_window);main_window=nullptr;identity.clear();
}
// Find only owning visible paths. Cached fullscreen picker/scaffold windows
// remain in the tree after close; hidden ancestors must never block authoring.
bool modal_visible(sui::Win main){
    struct Item{sui::Win window;unsigned depth;bool picker,options;};
    std::vector<Item> stack{{main,0,false,false}};size_t visited=0;
    while(!stack.empty()){
        auto [window,depth,in_picker,in_options]=stack.back();stack.pop_back();
        if(!window || !sui::visible(window))continue;
        if(++visited>4096 || depth>=64)return true;
        const auto id=sui::control_id(window);
        const bool picker=in_picker||id==0x53C6DC80,options=in_options||id==0x0615B50D;
        if((picker&&(id==0x53C5B392||id==0x53C5B391||id==0x53C5B390)) || (options&&id==0x064C23F8))return true;
        for(auto child:sui::children(window))if(sui::parent(child)==window){
            if(stack.size()>=4096)return true;stack.push_back({child,depth+1,picker,options});
        }
    }
    return false;
}
std::vector<sui::Win> visible_cards(sui::Win main){
    struct Item{sui::Win window;unsigned depth;bool palette;};
    std::vector<Item> stack{{main,0,false}};std::vector<sui::Win> result;size_t visited=0;
    while(!stack.empty()){
        auto [window,depth,in_palette]=stack.back();stack.pop_back();
        if(!window||!sui::visible(window))continue;
        if(++visited>4096||depth>=64)return {};
        const auto id=sui::control_id(window);if(id>=kEdit && id<kDecoration+32)continue;
        const bool palette=in_palette||id==0x52DF67AF;
        if(palette)if(auto card=editor_native::cast_window(window);card && card->index>=-1){result.push_back(window);continue;}
        for(auto child:sui::children(window))if(sui::parent(child)==window){
            if(stack.size()>=4096)return {};stack.push_back({child,depth+1,palette});
        }
    }
    return result;
}
bool current(const std::shared_ptr<Card>& card,const coop_editor::View& view){
    const auto found=cards.find(card->window);
    return found!=cards.end() && found->second==card && view.active && view.native_editing && view.can_save &&
        !view.frozen && view.prompt_identity==card->identity && !editor_creation::active() &&
        main_window==sui::main_window() && !modal_visible(main_window);
}
bool build(const std::shared_ptr<Card>& card,sui::Win style){
    sui::add_ref(card->window);
    const auto native_size=sui::area(style);
    if(native_size.w()>0&&native_size.w()<=64&&native_size.h()>0&&native_size.h()<=64){card->edit_width=native_size.w();card->edit_height=native_size.h();}
    card->edit=sui::button_like(card->window,style,{},"");if(!card->edit)return false;
    // Cached Sporepedia buttons retain their closing fade (00FFFFFF). That
    // transient opacity is not part of the icon/style to reuse on a live card.
    sui::set_shade_color(card->edit,0xFFFFFFFF);
    sui::set_flag(card->edit,sui::kFlagAlwaysInFront,true);
    sui::set_ids(card->edit,kEdit,kEdit);sui::set_visible(card->edit,false);
    auto tip=sui::tooltip_caption(style);if(tip.empty())tip="Edit";
    card->edit_tip=sui::tooltip(card->edit,tip);
    card->owner_tip=sui::tooltip(card->window,"");
    if(!card->edit_tip||!card->owner_tip)return false;
    const auto decorate=[&](sui::Win& window,unsigned index){
        window=sui::label(card->window,{},"",sui::font(style),0xFFFFFFFF,sui::kAlignLeft);
        if(!window)return false;sui::set_flag(window,sui::kFlagAlwaysInFront,true);sui::set_ids(window,kDecoration+index,0);sui::set_visible(window,false);return true;
    };
    for(unsigned i=0;i<4;++i)if(!decorate(card->border[i],i))return false;
    for(unsigned i=0;i<5;++i)if(!decorate(card->padlock[i],4+i))return false;
    // AddWindow appends behind the native thumbnail children. The front flag
    // controls later ordering but does not itself move an existing list node.
    for(auto window:card->border)sui::bring_to_front(card->window,window);
    for(auto window:card->padlock)sui::bring_to_front(card->window,window);
    sui::bring_to_front(card->window,card->edit);
    std::weak_ptr<Card> weak=card;
    for(auto window:{card->window,card->edit}){
        const bool button=window==card->edit;
        auto handler=sui::add_handler(window,[weak,button](auto recipient,const sui::Msg& message){
            auto item=weak.lock();if(!item)return false;
            if(message.type==sui::kMsgMouseEnter)(button?item->button_hover:item->hover)=true;
            if(message.type==sui::kMsgMouseLeave)(button?item->button_hover:item->hover)=false;
            if(message.type==0x0A && message.a==0)item->focus=message.d==uintptr_t(recipient);
            const bool keyboard=message.type==sui::kMsgKeyDown && message.b==VK_F2;
            if(keyboard)item->focus=true;
            if(!keyboard&&(!button||!sui::is_click(message)))return false;
            const auto view=coop_editor::view();
            if(current(item,view))for(const auto& asset:view.assets)if(asset.class_id==item->value.class_id &&
                asset.asset==item->value.asset && asset.content_revision==item->value.content_revision && asset.editable){
                const bool blocked=std::any_of(asset.owners.begin(),asset.owners.end(),[](const auto& owner){return owner.content&&!owner.local;});
                if(!blocked)coop_editor::edit_asset(asset.class_id,asset.asset,asset.content_revision);
                break;
            }
            return true;
        },0xC1); // Basic/advanced input plus native hover/focus refresh events.
        if(!handler)return false;card->handlers.push_back({window,handler});
    }
    return true;
}
void SK_CDECL update(void*){
    struct Timer{std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();~Timer(){update_us=uint64_t(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-start).count());}} timer;
    queued=false;if(!H)return;
    const auto view=coop_editor::view();const auto native=editor_native::state();const auto main=sui::main_window();
    if(main_window && (main_window!=main || identity!=view.prompt_identity))clear();
    if(!view.active || !view.native_editing || !native.ready || !native.world_visible || view.frozen ||
       view.playtesting || view.exporting || editor_creation::active() || coop_ui::is_open() || modal_visible(main)){
        display_status="browser held active="+std::to_string(view.active)+" editing="+std::to_string(view.native_editing)+
            " ready="+std::to_string(native.ready)+" world="+std::to_string(native.world_visible)+" modal="+std::to_string(modal_visible(main));clear();return;
    }
    // The native Sporepedia Edit button supplies its icon, drawable states,
    // font and localized tooltip. No separate creation toolbar is installed.
    if(!edit_style){
        auto style=sui::find(main,0x54ACB9F1);if(!style || !sui::as_button(style)){display_status="browser missing native Edit prototype";clear();return;}
        edit_style=style;sui::add_ref(edit_style);
    }
    const auto style=edit_style;
    if(!main_window){main_window=main;sui::add_ref(main_window);identity=view.prompt_identity;}
    std::set<sui::Win> present;
    std::vector<sui::Rect> empty_slots;
    unsigned candidates=0;
    for(auto window:visible_cards(main)){
        ++candidates;
        const auto key=editor_native::cast_window(window);if(!key)continue;
        if(key->index==-1){empty_slots.push_back(sui::screen_rect(window));continue;}
        const editor::ObjectId id{(2ull<<32)|uint32_t(key->index),1};
        const auto value=std::find_if(view.assets.begin(),view.assets.end(),[&](const auto& item){return item.class_id==id && item.asset==key->asset;});
        if(value==view.assets.end())continue;
        auto found=cards.find(window);
        if(found!=cards.end() && (found->second->value.class_id!=id || found->second->value.asset!=key->asset)){
            destroy(*found->second);cards.erase(found);found=cards.end();
        }
        if(found==cards.end()){
            auto card=std::make_shared<Card>();card->window=window;card->identity=identity;card->value=*value;
            if(!build(card,style)){destroy(*card);continue;}found=cards.emplace(window,std::move(card)).first;
        }
        present.insert(window);auto& card=*found->second;card.value=*value;
        const auto area=sui::area(window);const float width=area.w(),height=area.h();
        sui::set_area(card.edit,{width-4-card.edit_width,height-4-card.edit_height,width-4,height-4});
        sui::set_visible(card.edit,card.value.editable&&(card.hover||card.button_hover||card.focus));
        const bool blocked=std::any_of(value->owners.begin(),value->owners.end(),[](const auto& owner){return owner.content&&!owner.local;});
        sui::set_enabled(card.edit,view.can_save&&!blocked);
        std::string owners;
        for(const auto& owner:value->owners){
            if(!owners.empty())owners+='\n';owners+=owner.name+": ";
            if(owner.content)owners+="editing content";
            if(owner.content&&owner.properties)owners+="; ";
            if(owner.properties)owners+="editing properties";
        }
        if(owners!=card.owner_text){card.owner_text=owners;sui::tooltip_text(card.owner_tip,owners);}
        const std::array<sui::Rect,4> border{{{1,1,width-1,3},{1,height-3,width-1,height-1},{1,3,3,height-3},{width-3,3,width-1,height-3}}};
        for(unsigned i=0;i<4;++i){
            sui::set_area(card.border[i],border[i]);sui::set_visible(card.border[i],!value->owners.empty());
            if(!value->owners.empty())sui::set_fill_color(card.border[i],value->owners[i%value->owners.size()].color);
        }
        // A small closed padlock drawn with noninteractive native windows.
        // It never changes native selection, drag hit-testing or caption text.
        const float x=6,y=height-17;
        const std::array<sui::Rect,5> lock{{{x+2,y,x+8,y+2},{x+1,y+2,x+3,y+6},{x+7,y+2,x+9,y+6},{x,y+5,x+10,y+13},{x+4,y+8,x+6,y+11}}};
        for(unsigned i=0;i<5;++i){
            sui::set_area(card.padlock[i],lock[i]);sui::set_visible(card.padlock[i],!owners.empty());
            sui::set_fill_color(card.padlock[i],i==4?0xFF20303C:0xFFFFFFFF);
        }
    }
    for(auto it=cards.begin();it!=cards.end();)if(!present.contains(it->first)){destroy(*it->second);it=cards.erase(it);}else ++it;
    display_status="browser cards="+std::to_string(cards.size())+" candidates="+std::to_string(candidates)+" assets="+std::to_string(view.assets.size());
    for(const auto& rect:empty_slots){
        char row[128];std::snprintf(row,sizeof(row),"\nempty rect=%.0f,%.0f,%.0f,%.0f",rect.x1,rect.y1,rect.x2,rect.y2);display_status+=row;
    }
    for(const auto& [window,card]:cards){
        if(display_status.size()>7000)break;const auto rect=sui::screen_rect(window);
        char row[256];std::snprintf(row,sizeof(row),"\ncard class=%llu:%u key=%08X!%08X.%08X revision=%llu rect=%.0f,%.0f,%.0f,%.0f edit=%u owners=%u",
            static_cast<unsigned long long>(card->value.class_id.id),card->value.class_id.generation,card->value.asset.group,card->value.asset.instance,card->value.asset.type,
            static_cast<unsigned long long>(card->value.content_revision),rect.x1,rect.y1,rect.x2,rect.y2,unsigned(sui::visible(card->edit)),unsigned(card->value.owners.size()));display_status+=row;
    }
}
}
void init(const SkHost* host){H=host;}
void tick(){if(H&&!queued){queued=true;H->run_outside_frame(update,nullptr);}}
void shutdown(){clear();H=nullptr;queued=false;}
std::string diagnostic(){return display_status+"\nupdate_us="+std::to_string(update_us);}
} // namespace editor_creation_ui
