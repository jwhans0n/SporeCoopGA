// Production creation toolbar with controlled native window ownership.
// Verifies visible picker/modal behavior, not native rendering acceptance.
#include "../mods/coop/editor_creation_ui.cpp"
#include <cstdio>
#include <cstdlib>
#include <memory>

static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
namespace {
struct Handler {std::function<bool(sui::Win,const sui::Msg&)> callback;};
struct Node {
    uint32_t id=0,command=0,flags=sui::kFlagVisible|sui::kFlagEnabled,font=77;
    Node* parent=nullptr;
    std::vector<sui::Win> children;
    std::vector<std::unique_ptr<Handler>> handlers;
    sui::Rect area;
    int refs=0;
};
std::vector<std::unique_ptr<Node>> nodes;
std::vector<std::pair<SkTaskFn,void*>> tasks;
std::vector<Node*> template_children;
Node* main_node=nullptr;
coop_editor::View view;
editor_native::State native_state;
bool creation=false,lobby=false;
unsigned created=0,edited=0;
uint32_t created_type=0;
uint32_t created_editor=0;
const editor::ObjectId selected{(2ull<<32)|7,1};
Node* node(sui::Win value){return static_cast<Node*>(value);}
Node* make(Node* parent,uint32_t id){
    auto item=std::make_unique<Node>();item->parent=parent;item->id=id;
    auto* result=item.get();nodes.push_back(std::move(item));
    if(parent)parent->children.push_back(result);return result;
}
void pump(){auto work=std::move(tasks);tasks.clear();for(auto [fn,data]:work)fn(data);}
void step(){editor_creation_ui::tick();pump();}
void click(sui::Win window){
    sui::Msg message{};message.source=window;message.type=sui::kMsgComponentActivated;
    for(auto& handler:node(window)->handlers)handler->callback(window,message);
}
bool shown(sui::Win window){
    for(auto* p=node(window);p;p=p->parent)if(!(p->flags&sui::kFlagVisible))return false;
    return window;
}
}
namespace coop_editor {
View view(){return ::view;}
void create_asset(uint32_t type,uint32_t editor_id){++created;created_type=type;created_editor=editor_id;}
void edit_asset(editor::ObjectId id){CHECK(id==selected);++edited;}
}
namespace editor_native {State state(){return native_state;}}
namespace editor_creation {bool active(){return creation;}}
namespace editor_creation_native {std::optional<editor::ObjectId> selected_cast(){return selected;}}
namespace coop_ui {bool is_open(){return lobby;}}
namespace sui {
Win main_window(){return main_node;}
std::vector<Win> children(Win w){return w?node(w)->children:std::vector<Win>{};}
Win parent(Win w){return w?node(w)->parent:nullptr;}
uint32_t control_id(Win w){return w?node(w)->id:0;}
bool visible(Win w){return w&&(node(w)->flags&kFlagVisible);}
Win find(Win w,uint32_t id){
    if(!w)return nullptr;if(control_id(w)==id)return w;
    for(auto child:children(w))if(auto match=find(child,id))return match;
    return nullptr;
}
uint32_t font(Win w){return node(w)->font;}
void add_ref(Win w){++node(w)->refs;}
void release(Win w){CHECK(node(w)->refs>0);--node(w)->refs;}
void set_visible(Win w,bool value){set_flag(w,kFlagVisible,value);}
void set_enabled(Win w,bool value){set_flag(w,kFlagEnabled,value);}
void set_flag(Win w,uint32_t flag,bool value){if(value)node(w)->flags|=flag;else node(w)->flags&=~flag;}
void set_ids(Win w,uint32_t control,uint32_t command){node(w)->id=control;node(w)->command=command;}
void set_drawable(Win,void*){}
void set_fill_color(Win,uint32_t){}
void set_font(Win w,uint32_t value){node(w)->font=value;}
void set_caption(Win,const std::string&){}
void set_caption_padding(Win,float){}
void set_area(Win w,const Rect& value){node(w)->area=value;}
void add_child(Win owner,Win child){CHECK(!parent(child));node(child)->parent=node(owner);node(owner)->children.push_back(child);}
void remove_child(Win owner,Win child){
    auto& children=node(owner)->children;auto at=std::find(children.begin(),children.end(),child);
    CHECK(at!=children.end()&&parent(child)==owner);children.erase(at);node(child)->parent=nullptr;
}
void bring_to_front(Win,Win){}
Win load_layout(uint32_t instance,uint32_t){
    CHECK(instance==0x34A6339E);
    auto root=make(main_node,0x1234);auto body=make(root,0x2000);
    make(body,0x0519E168);make(body,0x0451D381);
    auto logo=make(root,0xDEADBEEF);make(logo,0xABCDEF01);
    make(root,0x98765432);template_children={body,logo,node(root->children.back())};
    return root;
}
Win button_like(Win owner,Win style,const Rect& rect,const std::string&){
    CHECK(style&&node(style)->font==77);auto button=make(node(owner),0);button->area=rect;return button;
}
void* add_handler(Win w,std::function<bool(Win,const Msg&)> callback){
    auto value=std::make_unique<Handler>();value->callback=std::move(callback);
    auto* result=value.get();node(w)->handlers.push_back(std::move(value));return result;
}
void remove_handler(Win w,void* handler){
    auto& handlers=node(w)->handlers;
    auto found=std::find_if(handlers.begin(),handlers.end(),[&](const auto& value){return value.get()==handler;});
    CHECK(found!=handlers.end());handlers.erase(found);
}
}
int main(){
    main_node=make(nullptr,1);view.active=view.native_editing=view.can_save=true;view.prompt_identity="project:epoch";
    native_state.ready=native_state.world_visible=true;
    SkHost host{};host.run_outside_frame=[](SkTaskFn fn,void* data){tasks.push_back({fn,data});};
    host.screen_size=[](int* width,int* height){*width=1024;*height=768;};
    editor_creation_ui::init(&host);editor_creation_ui::tick();
    CHECK(!editor_creation_ui::panel.root&&tasks.size()==1);pump();
    auto& panel=editor_creation_ui::panel;CHECK(panel.root&&shown(panel.root));
    CHECK(template_children.size()==3);
    for(auto* original:template_children)CHECK(!sui::visible(original));
    CHECK(!shown(sui::find(panel.root,0xABCDEF01)));
    CHECK(panel.choices.size()==4&&!sui::find(panel.root,0xC00F0314));
    click(panel.new_button);step();CHECK(editor_creation_ui::want_menu&&shown(panel.choices[0]));
    // A hidden cached picker must not block authoring, even when its child
    // remains individually visible. A later live duplicate must block it.
    auto hidden_owner=make(main_node,2);sui::set_visible(hidden_owner,false);
    make(make(hidden_owner,0x53C6DC80),0x53C5B392);step();CHECK(shown(panel.root));
    // Actual closed Sporepedia retains a visible root/scaffold and hidden
    // content frames, whose cached captions may themselves remain visible.
    auto scaffold=make(main_node,0x53C6DC80);make(scaffold,0x94174874);
    for(auto id:{0x53C5B392u,0x53C5B391u,0x53C5B390u}){
        auto frame=make(scaffold,id);sui::set_visible(frame,false);make(frame,0x9999);
    }
    step();CHECK(shown(panel.root)&&editor_creation_ui::want_menu);
    // A similarly named frame outside an owning picker is unrelated.
    auto unrelated_frame=make(main_node,0x53C5B392);step();CHECK(shown(panel.root));
    sui::set_visible(unrelated_frame,false);
    auto live_owner=make(main_node,3);auto live_picker=make(live_owner,0x53C6DC80);
    auto live_frame=make(live_picker,0x53C5B392);
    CHECK(sui::find(main_node,0x53C6DC80)!=live_picker);
    // Test a queued click before the next UI update: cached runtime editing
    // state is still true, but the actual current picker must reject it.
    click(panel.choices[0]);CHECK(created==0&&!editor_creation_ui::want_menu);
    step();CHECK(!shown(panel.root));click(panel.new_button);CHECK(!editor_creation_ui::want_menu);
    // Each observed native frame independently identifies an open browser.
    for(auto id:{0x53C5B391u,0x53C5B390u}){
        live_frame->id=id;step();CHECK(!shown(panel.root));
    }
    sui::set_visible(live_owner,false);step();CHECK(shown(panel.root));
    // A stale child-list reference is not an owning visible UI path.
    main_node->children.push_back(live_picker);step();CHECK(shown(panel.root));main_node->children.pop_back();
    click(panel.new_button);step();click(panel.choices[0]);CHECK(created==1&&created_type==0x2B978C46);
    click(panel.edit_button);CHECK(edited==1);
    // Native Options retains cached windows too. Only its visible owning
    // panel path blocks authoring, including clicks queued before update.
    auto options=make(main_node,0x0615B50D);auto options_body=make(options,0x064C23F8);
    sui::set_visible(options,false);step();CHECK(shown(panel.root));
    sui::set_visible(options,true);sui::set_visible(options_body,false);step();CHECK(shown(panel.root));
    click(panel.new_button);step();CHECK(editor_creation_ui::want_menu);
    sui::set_visible(options_body,true);click(panel.choices[0]);CHECK(created==1&&!editor_creation_ui::want_menu);
    step();CHECK(!shown(panel.root));sui::set_visible(options,false);step();CHECK(shown(panel.root));
    auto unrelated_options=make(main_node,0x064C23F8);step();CHECK(shown(panel.root));sui::set_visible(unrelated_options,false);
    // Family choices do not enter an unconstrained generic Vehicle/Building
    // editor. Each native GA variant is an explicit, locally guarded action.
    for(size_t family_index:{size_t(1),size_t(2)}){
        const auto type=editor_creation_ui::types[family_index];const auto variants=editor_creation::new_variants(type);
        CHECK(variants.size()==(family_index==1?4u:9u));
        for(size_t i=0;i<variants.size();++i){
            const auto count=created;click(panel.new_button);step();click(panel.choices[family_index]);step();
            CHECK(created==count&&editor_creation_ui::want_menu&&editor_creation_ui::variant_type==type);
            CHECK(shown(panel.back)&&!shown(panel.choices[family_index])&&shown(panel.variants[i]));
            CHECK(node(panel.root)->area.y2<=768&&node(panel.root)->area.y1>=0);
            for(size_t n=variants.size();n<panel.variants.size();++n)CHECK(!shown(panel.variants[n]));
            click(panel.variants[i]);CHECK(created==count+1&&created_type==type&&created_editor==variants[i].editor_id);
            CHECK(!editor_creation_ui::want_menu&&!editor_creation_ui::variant_type);
        }
    }
    click(panel.new_button);step();click(panel.choices[2]);step();click(panel.back);step();
    CHECK(editor_creation_ui::want_menu&&!editor_creation_ui::variant_type&&shown(panel.choices[2])&&!shown(panel.back));
    click(panel.new_button);step();
    // Hidden/stale variant handlers cannot reuse a choice against another
    // project or during a Native modal/readiness hold.
    for(unsigned reason=0;reason<5;++reason){
        click(panel.new_button);step();click(panel.choices[2]);step();const auto count=created;
        const auto identity=view.prompt_identity;
        if(reason==0)view.can_save=false;
        if(reason==1)view.prompt_identity="retired cohort";
        if(reason==2)creation=true;
        if(reason==3)sui::set_visible(live_owner,true);
        if(reason==4){click(panel.back);step();}
        click(panel.variants[0]);CHECK(created==count&&!editor_creation_ui::want_menu);
        view.can_save=true;view.prompt_identity=identity;creation=false;sui::set_visible(live_owner,false);step();
    }
    // Existing transition guards remain effective independently of picker IDs.
    creation=true;step();CHECK(!shown(panel.root));creation=false;
    lobby=true;step();CHECK(!shown(panel.root));lobby=false;step();CHECK(shown(panel.root));
    // Unknown/pathological UI depth fails closed within a fixed bound.
    auto deep=make(main_node,4);auto at=deep;
    for(unsigned i=0;i<65;++i)at=make(at,5+i);
    step();CHECK(!shown(panel.root));sui::set_visible(deep,false);step();CHECK(shown(panel.root));
    auto old_root=panel.root;view.prompt_identity="replacement";step();
    CHECK(panel.root!=old_root&&!sui::parent(old_root)&&node(old_root)->refs==0);
    editor_creation_ui::shutdown();CHECK(!panel.root&&main_node->handlers.empty());
    std::printf("creation UI Windows checks: %u passed\n",checks);
}
