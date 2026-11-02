#include "editor_creation_ui.h"
#include "editor_creation.h"
#include "editor_creation_native.h"
#include "editor_runtime.h"
#include "coop_ui.h"
#include "sui.h"
#include <algorithm>
#include <array>
#include <optional>

namespace editor_creation_ui {
namespace {
const SkHost* H;
bool queued=false,want_menu=false;
uint32_t variant_type=0;
struct Panel {
    sui::Win root=nullptr,main=nullptr,new_button=nullptr,edit_button=nullptr;
    std::array<sui::Win,4> choices{};
    sui::Win back=nullptr;
    std::array<sui::Win,9> variants{};
    std::vector<std::pair<sui::Win,void*>> handlers;
    std::string identity;
    std::optional<editor::ObjectId> target;
} panel;
constexpr uint32_t kRoot=0xC00F0300,kNew=0xC00F0301,kEdit=0xC00F0302,kChoice=0xC00F0310;
constexpr uint32_t kBack=0xC00F0320,kVariant=0xC00F0340;
constexpr std::array<uint32_t,4> types{0x2B978C46,0x2399BE55,0x24682294,0x476A98C7};
constexpr std::array<const char*,4> labels{"New creature","New building","New vehicle","New spaceship"};
bool native_modal_visible(sui::Win main){
    // Sporepedia keeps its root and empty scaffold visible after closing.
    // Its header/card/sidebar frames are visible only while a picker is open.
    // Search their current owning visible path, including duplicate roots.
    // Native child lists are bounded by sui::children; this traversal also
    // bounds depth, total work and stack size, failing closed on overflow.
    struct Entry {sui::Win window;unsigned depth;bool picker,options;};
    constexpr size_t limit=4096;
    std::vector<Entry> stack{{main,0,false,false}};size_t visited=0;
    while(!stack.empty()){
        const auto [window,depth,in_picker,in_options]=stack.back();stack.pop_back();
        if(!window || window==panel.root || !sui::visible(window))continue;
        if(++visited>limit || depth>=64)return true;
        const auto id=sui::control_id(window);
        const bool picker=in_picker || id==0x53C6DC80;
        if(picker && (id==0x53C5B392 || id==0x53C5B391 || id==0x53C5B390))return true;
        const bool options=in_options || id==0x0615B50D;
        if(options && id==0x064C23F8)return true;
        for(auto child:sui::children(window)){
            if(sui::parent(child)!=window)continue;
            if(stack.size()>=limit)return true;
            stack.push_back({child,depth+1,picker,options});
        }
    }
    return false;
}
void clear(){
    want_menu=false;variant_type=0;
    for(auto [window,handler]:panel.handlers)sui::remove_handler(window,handler);
    if(panel.root){sui::set_visible(panel.root,false);if(auto parent=sui::parent(panel.root))sui::remove_child(parent,panel.root);sui::release(panel.root);}
    if(panel.main)sui::release(panel.main);
    panel={};
}
bool current(){
    const auto view=coop_editor::view();
    return panel.root && sui::visible(panel.root) && panel.main==sui::main_window() &&
        view.active && view.can_save && !view.frozen && view.native_editing && view.prompt_identity==panel.identity &&
        !editor_creation::active() && !native_modal_visible(panel.main);
}
void handler(sui::Win window,std::function<bool(sui::Win,const sui::Msg&)> callback){
    if(auto value=sui::add_handler(window,std::move(callback)))panel.handlers.push_back({window,value});
}
bool build(const coop_editor::View& view){
    // Reuse the reviewed native account-dialog frame/style, as the existing
    // playtest popup does. Every window belongs to this retained local panel.
    auto root=sui::load_layout(0x34A6339E);if(!root)return false;
    panel.root=root;sui::add_ref(root);panel.identity=view.prompt_identity;
    auto like=sui::find(root,0x0519E168),text=sui::find(root,0x0451D381);
    if(!like || !text){clear();return false;}
    const auto font=sui::font(text);
    // Keep the native style sources alive, but hide every original subtree,
    // including decorative logos and future template additions.
    for(auto child:sui::children(root))sui::set_visible(child,false);
    sui::set_ids(root,kRoot,0);sui::set_drawable(root,nullptr);sui::set_fill_color(root,0xE5202834);
    panel.new_button=sui::button_like(root,like,{4,4,120,34},"New asset");
    panel.edit_button=sui::button_like(root,like,{124,4,240,34},"Edit asset");
    if(!panel.new_button || !panel.edit_button){clear();return false;}
    sui::set_ids(panel.new_button,kNew,kNew);sui::set_ids(panel.edit_button,kEdit,kEdit);
    for(auto b:{panel.new_button,panel.edit_button}){sui::set_font(b,font);sui::set_caption_padding(b,2);}
    handler(panel.new_button,[](auto,const sui::Msg& message){if(!sui::is_click(message))return false;if(current()){want_menu=!want_menu;variant_type=0;}return true;});
    handler(panel.edit_button,[](auto,const sui::Msg& message){if(!sui::is_click(message))return false;if(current() && panel.target)coop_editor::edit_asset(*panel.target);want_menu=false;variant_type=0;return true;});
    for(size_t i=0;i<types.size();++i){
        auto b=sui::button_like(root,like,{4,40.f+34*i,240,70.f+34*i},labels[i]);if(!b){clear();return false;}
        panel.choices[i]=b;sui::set_ids(b,kChoice+uint32_t(i),kChoice+uint32_t(i));sui::set_font(b,font);sui::set_caption_padding(b,2);
        handler(b,[i](auto,const sui::Msg& message){
            if(!sui::is_click(message))return false;
            if(current() && want_menu && !variant_type){
                if(editor_creation::new_variants(types[i]).size()>1){variant_type=types[i];return true;}
                coop_editor::create_asset(types[i]);
            }
            want_menu=false;variant_type=0;return true;
        });
    }
    panel.back=sui::button_like(root,like,{4,40,240,70},"Back");if(!panel.back){clear();return false;}
    sui::set_ids(panel.back,kBack,kBack);sui::set_font(panel.back,font);sui::set_caption_padding(panel.back,2);
    handler(panel.back,[](auto,const sui::Msg& message){if(!sui::is_click(message))return false;if(current() && want_menu)variant_type=0;return true;});
    for(size_t i=0;i<panel.variants.size();++i){
        auto b=sui::button_like(root,like,{4,74.f+34*i,240,104.f+34*i},"");if(!b){clear();return false;}
        panel.variants[i]=b;sui::set_ids(b,kVariant+uint32_t(i),kVariant+uint32_t(i));sui::set_font(b,font);sui::set_caption_padding(b,2);
        handler(b,[i](auto,const sui::Msg& message){
            if(!sui::is_click(message))return false;
            const auto type=variant_type;const auto variants=editor_creation::new_variants(type);
            if(current() && want_menu && type && i<variants.size())coop_editor::create_asset(type,variants[i].editor_id);
            want_menu=false;variant_type=0;return true;
        });
    }
    panel.main=sui::main_window();if(!panel.main){clear();return false;}sui::add_ref(panel.main);
    if(auto parent=sui::parent(root))if(parent!=panel.main)sui::remove_child(parent,root);
    if(sui::parent(root)!=panel.main)sui::add_child(panel.main,root);
    sui::set_flag(root,sui::kFlagAlwaysInFront,true);
    handler(panel.main,[](auto,const sui::Msg& message){
        if(!want_menu)return false;
        if(message.type==sui::kMsgKeyDown && message.a==0x1B){want_menu=false;variant_type=0;return true;}
        if(message.type==sui::kMsgMouseDown && message.source){
            for(auto w=message.source;w;w=sui::parent(w))if(w==panel.root)return false;
            want_menu=false;variant_type=0;
        }
        return false;
    });
    return true;
}
void SK_CDECL update(void*){
    queued=false;if(!H)return;
    const auto view=coop_editor::view();const auto native=editor_native::state();
    const bool visible=view.active && view.native_editing && native.ready && native.world_visible &&
        !view.frozen && !view.playtesting && !view.exporting && !editor_creation::active() && !coop_ui::is_open() &&
        !native_modal_visible(sui::main_window());
    if(panel.root && (panel.main!=sui::main_window() || panel.identity!=view.prompt_identity))clear();
    if(!visible){want_menu=false;variant_type=0;if(panel.root)sui::set_visible(panel.root,false);return;}
    if(!panel.root && !build(view))return;
    panel.target=editor_creation_native::selected_cast();
    sui::set_enabled(panel.new_button,view.can_save);
    sui::set_enabled(panel.edit_button,view.can_save && panel.target.has_value());
    if(!view.can_save){want_menu=false;variant_type=0;}
    int width=0,height=0;H->screen_size(&width,&height);
    const auto variants=editor_creation::new_variants(variant_type);
    const float menu_height=40.f+34.f*(variant_type?variants.size()+1:types.size());
    const float x=12,y=std::clamp(98.f,4.f,std::max(4.f,height-menu_height-4.f));
    sui::set_area(panel.root,{x,y,x+244,y+(want_menu?menu_height:38.f)});
    for(auto b:panel.choices)sui::set_visible(b,want_menu&&!variant_type);
    sui::set_visible(panel.back,want_menu&&variant_type);
    for(size_t i=0;i<panel.variants.size();++i){
        const bool shown=want_menu&&variant_type&&i<variants.size();
        if(shown)sui::set_caption(panel.variants[i],variants[i].name);
        sui::set_visible(panel.variants[i],shown);
    }
    sui::set_visible(panel.root,true);sui::bring_to_front(panel.main,panel.root);
}
}
void init(const SkHost* host){H=host;}
void tick(){if(H && !queued){queued=true;H->run_outside_frame(update,nullptr);}}
void shutdown(){clear();H=nullptr;queued=false;}
} // namespace editor_creation_ui
