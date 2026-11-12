// Production UI with controlled native cards. Rendering is qualified separately.
#include "../mods/coop/editor_creation_ui.cpp"
#include <cstdio>
#include <cstdlib>

static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
struct Handler{std::function<bool(sui::Win,const sui::Msg&)> callback;};
struct Node{
    uint32_t id=0,flags=3,color=0;int refs=0;bool button=false;
    Node* parent=nullptr;Node* style=nullptr;std::vector<sui::Win> children;
    std::vector<std::unique_ptr<Handler>> handlers;sui::Rect rect{0,0,156,132};
};
std::vector<std::unique_ptr<Node>> nodes;
std::map<void*,std::unique_ptr<std::string>> tips;
std::map<sui::Win,editor_native::CastCard> native_cards;
std::vector<std::pair<SkTaskFn,void*>> tasks;
Node *main_node=nullptr,*style=nullptr,*palette=nullptr,*first=nullptr,*second=nullptr;
coop_editor::View view;editor_native::State native_state;
bool creating=false,lobby=false;unsigned edits=0;
editor::ObjectId edited;editor::ResourceKey edited_key;std::optional<editor::Revision> edited_revision;
Node* node(sui::Win w){return static_cast<Node*>(w);}
Node* make(Node* owner,uint32_t id,bool button=false){
    auto n=std::make_unique<Node>();auto* p=n.get();p->parent=owner;p->id=id;p->button=button;nodes.push_back(std::move(n));
    if(owner)owner->children.push_back(p);return p;
}
void pump(){auto work=std::move(tasks);tasks.clear();for(auto [fn,data]:work)fn(data);}
void step(){editor_creation_ui::tick();pump();}
bool shown(sui::Win w){if(!w)return false;for(auto* n=node(w);n;n=n->parent)if(!(n->flags&sui::kFlagVisible))return false;return true;}
void message(sui::Win w,int type){
    CHECK(w);sui::Msg m{};m.source=w;m.type=type;
    // Retain callable values across a callback that could retire a binding.
    std::vector<std::function<bool(sui::Win,const sui::Msg&)>> calls;
    for(const auto& h:node(w)->handlers)calls.push_back(h->callback);
    for(auto& fn:calls)fn(w,m);
}
void hover(sui::Win w,bool yes=true){message(w,yes?sui::kMsgMouseEnter:sui::kMsgMouseLeave);step();}
sui::Win edit(sui::Win w){return sui::find(w,0xC00F0401);}
void click(sui::Win w){message(w,sui::kMsgComponentActivated);}
}
namespace coop_editor {
View view(){return ::view;}
void edit_asset(editor::ObjectId id,editor::ResourceKey key,std::optional<editor::Revision> revision){++edits;edited=id;edited_key=key;edited_revision=revision;}
}
namespace editor_native {
State state(){return native_state;}
std::optional<CastCard> cast_card(void* w){auto found=native_cards.find(w);return found==native_cards.end()?std::nullopt:std::optional(found->second);}
std::optional<CastCard> cast_window(void* w){return cast_card(w);}
}
namespace editor_creation {bool active(){return creating;}}
namespace coop_ui {bool is_open(){return lobby;}}
namespace sui {
Win main_window(){return main_node;}
std::vector<Win> children(Win w){return w?node(w)->children:std::vector<Win>{};}
Win parent(Win w){return w?node(w)->parent:nullptr;}
uint32_t control_id(Win w){return w?node(w)->id:0;}
bool visible(Win w){return w&&(node(w)->flags&kFlagVisible);}
Win find(Win w,uint32_t id){if(!w)return nullptr;if(control_id(w)==id)return w;for(auto child:children(w))if(auto hit=find(child,id))return hit;return nullptr;}
Rect area(Win w){return node(w)->rect;}
Rect screen_rect(Win w){return node(w)->rect;}
void* as_button(Win w){return w&&node(w)->button?w:nullptr;}
uint32_t font(Win){return 77;}
void add_ref(Win w){++node(w)->refs;}
void release(Win w){CHECK(node(w)->refs>0);--node(w)->refs;}
void set_visible(Win w,bool value){set_flag(w,kFlagVisible,value);}
void set_enabled(Win w,bool value){set_flag(w,kFlagEnabled,value);}
void set_flag(Win w,uint32_t flag,bool value){CHECK(w);if(value)node(w)->flags|=flag;else node(w)->flags&=~flag;}
void set_ids(Win w,uint32_t id,uint32_t){node(w)->id=id;}
void set_area(Win w,const Rect& r){node(w)->rect=r;}
void set_fill_color(Win w,uint32_t color){node(w)->color=color;}
void set_shade_color(Win,uint32_t color){CHECK(color==0xFFFFFFFF);}
void remove_child(Win owner,Win child){auto& list=node(owner)->children;auto it=std::find(list.begin(),list.end(),child);CHECK(it!=list.end());list.erase(it);node(child)->parent=nullptr;}
void bring_to_front(Win owner,Win child){auto& list=node(owner)->children;const auto it=std::find(list.begin(),list.end(),child);CHECK(it!=list.end());list.erase(it);list.insert(list.begin(),child);}
Win button_like(Win owner,Win source,const Rect& rect,const std::string& text){CHECK(source==style&&text.empty());auto* p=make(node(owner),0,true);p->rect=rect;p->style=node(source);p->refs=1;return p;}
Win label(Win owner,const Rect& rect,const std::string& text,uint32_t,uint32_t,int){CHECK(text.empty());auto* p=make(node(owner),0);p->rect=rect;p->refs=1;p->flags|=kFlagIgnoreMouse;return p;}
void* add_handler(Win w,std::function<bool(Win,const Msg&)> callback){auto h=std::make_unique<Handler>();h->callback=std::move(callback);auto* p=h.get();node(w)->handlers.push_back(std::move(h));return p;}
void* add_handler(Win w,std::function<bool(Win,const Msg&)> callback,int events){CHECK(events==0xC1);return add_handler(w,std::move(callback));}
void remove_handler(Win w,void* h){auto& list=node(w)->handlers;auto it=std::find_if(list.begin(),list.end(),[&](const auto& item){return item.get()==h;});CHECK(it!=list.end());list.erase(it);}
void* tooltip(Win,const std::string& text){auto value=std::make_unique<std::string>(text);auto* p=value.get();tips.emplace(p,std::move(value));return p;}
std::string tooltip_caption(Win w){CHECK(w==style);return "Native localized Edit";}
void tooltip_text(void* p,const std::string& text){CHECK(tips.contains(p));*tips.at(p)=text;}
void remove_tooltip(Win,void* p){if(p)CHECK(tips.erase(p)==1);}
}
int main(){
    main_node=make(nullptr,1);palette=make(main_node,0x52DF67AF);
    // The native editor icon can be in a cached, hidden Sporepedia tree.
    auto cached=make(main_node,0x53C6DC80);cached->flags&=~sui::kFlagVisible;style=make(cached,0x54ACB9F1,true);
    style->rect={0,0,36,37};
    auto native_new=make(cached,0x14580F81,true);
    first=make(palette,0x111);second=make(palette,0x222);
    const editor::ResourceKey building{17,0x2399BE55,0x40636200},creature{18,0x2B978C46,0x40626200};
    native_cards[first]={7,building};native_cards[second]={8,creature};
    auto empty=make(palette,0x333);native_cards[empty]={-1,{}};
    view.active=view.native_editing=view.can_save=true;view.prompt_identity="project:epoch";view.selection="An unrelated placement";
    const editor::ObjectId a{(2ull<<32)|7,1},b{(2ull<<32)|8,1};
    view.assets={{a,building,3,true,{}},{b,creature,4,true,{}}};native_state.ready=native_state.world_visible=true;
    SkHost host{};host.run_outside_frame=[](SkTaskFn fn,void* data){tasks.push_back({fn,data});};
    editor_creation_ui::init(&host);editor_creation_ui::tick();CHECK(!edit(first)&&tasks.size()==1);pump();
    CHECK(edit(first)&&edit(second)&&!shown(edit(first))&&!sui::find(main_node,0xC00F0301));
    CHECK(sui::find(main_node,0x14580F81)==native_new&&native_new->flags==3);
    CHECK(node(edit(first))->style==style);
    CHECK(editor_creation_ui::diagnostic().find("empty rect=")!=std::string::npos&&!edit(empty));
    hover(first);CHECK(shown(edit(first))&&!shown(edit(second)));
    const auto rect=node(edit(first))->rect;CHECK(rect.x2==152&&rect.y2==128&&rect.x1==116&&rect.y1==91);
    click(edit(first));CHECK(edits==1&&edited==a&&edited_key==building&&edited_revision==3);
    message(first,sui::kMsgMouseLeave);hover(edit(first));CHECK(shown(edit(first)));
    hover(edit(first),false);CHECK(!shown(edit(first)));
    // Focus notifications follow the same native enter/leave contract.
    hover(second);click(edit(second));CHECK(edits==2&&edited==b&&edited_revision==4);
    view.assets[0].owners={{{0,9},"Host",0xFF123456,false,false,true}};step();
    auto border=sui::find(first,0xC00F0410);CHECK(shown(border)&&node(border)->color==0xFF123456&&(node(border)->flags&sui::kFlagIgnoreMouse));
    CHECK(shown(sui::find(first,0xC00F0414)));hover(first);CHECK(node(edit(first))->flags&sui::kFlagEnabled);
    click(edit(first));CHECK(edits==3); // Property ownership does not own Content.
    view.assets[0].owners[0].content=true;view.assets[0].owners[0].properties=false;step();
    CHECK(!(node(edit(first))->flags&sui::kFlagEnabled));click(edit(first));CHECK(edits==3);
    click(edit(second));CHECK(edits==4&&edited==b); // Unrelated cards remain usable.
    bool native_tip=false,owner_tip=false;
    for(const auto& [p,text]:tips){(void)p;native_tip|=*text=="Native localized Edit";owner_tip|=*text=="Host: editing content";}
    CHECK(native_tip&&owner_tip);
    view.assets[0].owners.clear();step();CHECK(!shown(border)&&(node(edit(first))->flags&sui::kFlagEnabled));
    // Queued activations must not edit a replacement or another card.
    ++view.assets[0].content_revision;click(edit(first));CHECK(edits==4);step();click(edit(first));CHECK(edits==5&&edited_revision==4);
    auto stale=edit(first);view.prompt_identity="another epoch";click(stale);CHECK(edits==5);step();CHECK(edit(first)!=stale);
    view.assets[0].editable=false;hover(first);CHECK(!shown(edit(first)));view.assets[0].editable=true;step();
    // A live duplicate picker blocks input even behind an earlier hidden root.
    auto open=make(main_node,0x53C6DC80);make(open,0x53C5B391);click(edit(first));CHECK(edits==5);step();CHECK(!edit(first));
    open->flags&=~sui::kFlagVisible;step();CHECK(edit(first));
    auto settings=make(main_node,0x0615B50D);make(settings,0x064C23F8);step();CHECK(!edit(first));
    settings->flags&=~sui::kFlagVisible;step();CHECK(edit(first));
    const auto allocated=nodes.size();for(unsigned i=0;i<100;++i){hover(first,i%2);CHECK(nodes.size()==allocated);}
    // Lease expiry/leave is represented by removal from the host-derived view.
    view.assets[0].owners={{{1,2},"Me",0xFF456789,true,true,false}};step();click(edit(first));CHECK(edits==6);
    view.assets[0].owners.clear();step();CHECK(!shown(sui::find(first,0xC00F0410)));
    native_cards[first].asset.instance++;step();CHECK(!edit(first));native_cards[first].asset=building;step();CHECK(edit(first));
    view.assets.erase(view.assets.begin());step();CHECK(!edit(first));
    creating=true;step();CHECK(!edit(second));creating=false;step();CHECK(edit(second));
    lobby=true;step();CHECK(!edit(second));lobby=false;step();
    view.frozen=true;step();CHECK(!edit(second));view.frozen=false;step();
    {
        const auto count=edits;sui::Msg key{};key.type=sui::kMsgKeyDown;key.b=0x71;
        for(const auto& handler:node(second)->handlers)handler->callback(second,key);
        step();CHECK(edits==count+1&&edited==b&&shown(edit(second)));
        sui::Msg focus{};focus.type=0x0A;focus.a=0;focus.d=0;
        for(const auto& handler:node(second)->handlers)handler->callback(second,focus);
        hover(second,false);CHECK(!shown(edit(second)));
    }
    editor_creation_ui::shutdown();CHECK(tips.empty()&&main_node->refs==0&&first->refs==0&&second->refs==0);
    for(const auto& item:nodes)CHECK(item->refs==0);
    std::printf("editor asset browser UI: %u checks passed\n",checks);
}
