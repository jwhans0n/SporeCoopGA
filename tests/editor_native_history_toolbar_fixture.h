// Actual native adapter with isolated current-layout windows; no global UI tree.
namespace history_ui_fixture {
struct Node {
    void** vtable=nullptr;
    uint32_t id=0,flags=sui::kFlagVisible;
    Node* up=nullptr;std::vector<sui::Win> children;
    bool castable=true;unsigned writes=0;bool enabled=false;
    std::array<uint8_t,0x40> abi_tail{};
};
bool active=false;std::map<void*,std::unique_ptr<Node>> nodes;
std::array<void*,32> vtable{};std::vector<void*> roots;
unsigned mutations=0,availability_calls[2]{};
std::function<void()> on_read,on_write;
Node& get(void* window){auto found=nodes.find(window);CHECK(active&&found!=nodes.end());return *found->second;}
void* SK_THISCALL cast(void* object,uint32_t id){CHECK(id==0xEEEE8218);auto& node=get(object);if(on_read)on_read();return node.castable?object:nullptr;}
Node* make(uint32_t id=0,Node* parent=nullptr){
    auto node=std::make_unique<Node>();node->vtable=vtable.data();node->id=id;node->up=parent;
    auto* raw=node.get();nodes.emplace(raw,std::move(node));if(parent)parent->children.push_back(raw);return raw;
}
void bind(){skg::field<void*>(t_container.data(),0x64)=roots.empty()?nullptr:roots.data();
    skg::field<void*>(t_container.data(),0x68)=roots.empty()?nullptr:roots.data()+roots.size();}
void setup(){
    active=false;nodes.clear();roots.clear();on_read=on_write={};reset();active=true;mutations=0;availability_calls[0]=availability_calls[1]=0;
    vtable.fill(nullptr);vtable[3]=reinterpret_cast<void*>(cast);vtable[31]=reinterpret_cast<void*>(cast);
    C.projection_scope=[] {return t_scope;};C.history_available=[](bool redo){++availability_calls[redo];return !redo;};bind();
}
void enabled(void* window,bool value){auto& node=get(window);++mutations;++node.writes;node.enabled=value;
    if(value)node.flags|=sui::kFlagEnabled;else node.flags&=~sui::kFlagEnabled;if(on_write)on_write();}
uint32_t id(void* window){return get(window).id;}
uint32_t flags(void* window){auto& node=get(window);if(on_read)on_read();return node.flags;}
std::vector<sui::Win> children(void* window){auto result=get(window).children;if(on_read)on_read();return result;}
sui::Win parent(void* window){return get(window).up;}
bool visible(void* window){return (get(window).flags&sui::kFlagVisible)!=0;}
void finish(){on_read=on_write={};active=false;nodes.clear();roots.clear();reset();}
}

void test_history_toolbar_current_layout(){
    using namespace history_ui_fixture;using editor_native::HistoryToolbar;
    constexpr uint32_t undo=0xB006EF6E,redo=0xF006EFA5;
    setup();auto* root=make();auto* branch=make(0,root);auto* u1=make(undo,branch);auto* u2=make(undo,root);
    auto* r1=make(redo,root);auto* r2=make(redo,branch);auto* outside=make(undo);outside->enabled=false;
    roots={root};bind();HistoryToolbar collected;const auto current=[] {return true;};
    CHECK(collect_history_toolbar(t_layout.data(),current,collected));
    CHECK(collected.undo.size()==2&&collected.redo.size()==2&&collected.visited==6&&mutations==0);
    refresh_history_toolbar(t_layout.data());
    CHECK(availability_calls[0]==1&&availability_calls[1]==1);
    CHECK(u1->writes==1&&u2->writes==1&&u1->enabled&&u2->enabled);
    CHECK(r1->writes==1&&r2->writes==1&&!r1->enabled&&!r2->enabled&&outside->writes==0);
    // Duplicate owning object entries do not write a window twice.
    setup();root=make();u1=make(undo,root);roots={root,root};bind();refresh_history_toolbar(t_layout.data());CHECK(u1->writes==1);
    // The current owning vector can contain non-window layout objects.
    setup();root=make();auto* other=make();other->castable=false;u1=make(undo,root);roots={other,root};bind();
    CHECK(collect_history_toolbar(t_layout.data(),current,collected)&&collected.undo.size()==1&&mutations==0);
    // A cached duplicate belonging to another parent is outside this subtree.
    setup();root=make();other=make();outside=make(undo,other);root->children.push_back(outside);u1=make(undo,root);roots={root};bind();
    CHECK(!collect_history_toolbar(t_layout.data(),current,collected));
    refresh_history_toolbar(t_layout.data());CHECK(u1->writes==0&&outside->writes==0&&mutations==0);
    // Cycles and excessive roots/nodes reject atomically, preserving outputs.
    setup();root=make();branch=make(0,root);root->up=branch;branch->children.push_back(root);roots={root};bind();
    HistoryToolbar sentinel;sentinel.roots=77;sentinel.visited=88;sentinel.undo={reinterpret_cast<void*>(0x1234)};
    CHECK(!collect_history_toolbar(t_layout.data(),current,sentinel)&&sentinel.roots==77&&sentinel.visited==88&&sentinel.undo.size()==1&&mutations==0);
    refresh_history_toolbar(t_layout.data());CHECK(mutations==0);
    setup();root=make();roots.assign(4097,root);bind();CHECK(!collect_history_toolbar(t_layout.data(),current,sentinel)&&sentinel.roots==77);
    setup();root=make();for(unsigned n=0;n<4096;++n)make(undo,root);roots={root};bind();
    CHECK(!collect_history_toolbar(t_layout.data(),current,sentinel)&&sentinel.roots==77&&mutations==0);
    // Expired proof callback never yields a partially collected output.
    setup();root=make();make(undo,root);roots={root};bind();unsigned reads=0;
    CHECK(!collect_history_toolbar(t_layout.data(),[&]{return ++reads<2;},sentinel)&&sentinel.roots==77&&mutations==0);
    setup();root=make();other=make();make(undo,root);roots={root};bind();
    on_read=[&]{roots[0]=other;};CHECK(!collect_history_toolbar(t_layout.data(),current,sentinel)&&sentinel.roots==77&&mutations==0);
    // History callbacks are evaluated once, then exact owner/scope is checked
    // again before the first write. They cannot update a replacement scene.
    for(unsigned reason=0;reason<13;++reason){
        setup();root=make();u1=make(undo,root);r1=make(redo,root);roots={root};bind();
        auto replacement_mode=t_mode;auto replacement_display=t_display;
        C.history_available=[&,reason](bool is_redo){++availability_calls[is_redo];if(!is_redo){
            if(reason==0)++generation;if(reason==1)++t_scope.epoch;if(reason==2)++t_scope.author.generation;
            if(reason==3)++t_scope.project[0];if(reason==4)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
            if(reason==5)skg::field<void*>(t_display.data(),0x24)=nullptr;if(reason==6)t_active=false;
            if(reason==7)C.projection_scope=[] {return GestureScope{};};
            if(reason==8)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
            if(reason==9)skg::field<void*>(t_data.data(),0x10)=t_other_data.data();
            if(reason==10)t_mode_pointer=replacement_mode.data();
            if(reason==11)skg::field<void*>(t_mode.data(),0x14)=replacement_display.data();
            if(reason==12)++t_scope.author.slot;
        }return true;};
        refresh_history_toolbar(t_layout.data());CHECK(mutations==0&&u1->writes==0&&r1->writes==0);
    }
    // A callback during the first update prevents any later stale write.
    setup();root=make();u1=make(undo,root);u2=make(undo,root);r1=make(redo,root);roots={root};bind();
    on_write=[] {++generation;};refresh_history_toolbar(t_layout.data());CHECK(mutations==1&&u1->writes+u2->writes+r1->writes==1);
    setup();root=make();make(undo,root);roots={root};bind();C.projection_scope=[] {return GestureScope{};};
    refresh_history_toolbar(t_layout.data());CHECK(mutations==0);
    finish();
}
