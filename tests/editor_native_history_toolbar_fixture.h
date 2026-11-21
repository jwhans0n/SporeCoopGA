// Actual adapter on the two native SetFlag call sites; no UI tree traversal.
namespace history_ui_fixture {
constexpr uint32_t undo_id=0xB006EF6E,redo_id=0xF006EFA5;
constexpr uintptr_t undo_return=0x11223344,redo_return=0x55667788;
struct Node {std::array<uint8_t,0x90> raw{};uint32_t flags=sui::kFlagVisible;unsigned writes=0;bool enabled=false;};
struct Write {void* window;uint32_t flags;bool value;};
bool active=false;std::map<void*,std::unique_ptr<Node>> nodes;
unsigned mutations=0,sui_calls=0,layout_calls=0,updates=0,availability_calls[2]{};
std::vector<Write> writes;
std::function<void(void*,uint32_t)> display_body;
std::function<void()> after_flags;
Node& get(void* window){auto found=nodes.find(window);CHECK(active&&found!=nodes.end());return *found->second;}
Node* make(uint32_t id){
    auto node=std::make_unique<Node>();skg::field<uint32_t>(node->raw.data(),0x7C)=id;
    auto* raw=node.get();CHECK(raw->raw.data()==reinterpret_cast<uint8_t*>(raw));nodes.emplace(raw,std::move(node));return raw;
}
void SK_THISCALL original_flags(void* window,uint32_t flags,bool value){
    auto& node=get(window);++mutations;++node.writes;writes.push_back({window,flags,value});
    if(flags&sui::kFlagEnabled)node.enabled=value;
    if(value)node.flags|=flags;else node.flags&=~flags;
    if(after_flags)after_flags();
}
void SK_THISCALL original_display(void* self,uint32_t elapsed){++updates;if(display_body)display_body(self,elapsed);}
void* SK_THISCALL absent_layout(void*,uint32_t,bool){++layout_calls;return nullptr;}
void setup(){
    active=false;nodes.clear();display_body={};after_flags={};reset();active=true;
    CHECK(fenced_windows.empty());mutations=sui_calls=layout_calls=updates=0;
    availability_calls[0]=availability_calls[1]=0;writes.clear();
    history_display_scope=nullptr;fence_queued=false;last_fence=0;fenced_generation=generation;
    a_history_undo_return=undo_return;a_history_redo_return=redo_return;
    original_update_display=original_display;original_history_button_flags=original_flags;
    a_layout_find=reinterpret_cast<uintptr_t>(absent_layout);
    C.projection_scope=[] {return t_scope;};
    C.history_available=[](bool redo){++availability_calls[redo];return redo;};
}
void flags_at(Node* node,bool native_value,uintptr_t caller,uint32_t flags=sui::kFlagEnabled){
    set_history_button_flags_at(node,flags,native_value,caller);
}
// Existing main-fixture SUI doubles remain available, but any invocation is
// counted: the new native hook must never use them.
void enabled(void* window,bool value){++sui_calls;get(window).enabled=value;}
uint32_t id(void* window){++sui_calls;return skg::field<uint32_t>(get(window).raw.data(),0x7C);}
uint32_t flags(void* window){++sui_calls;return get(window).flags;}
std::vector<sui::Win> children(void*){++sui_calls;return {};}
sui::Win parent(void*){++sui_calls;return nullptr;}
bool visible(void* window){++sui_calls;return (get(window).flags&sui::kFlagVisible)!=0;}
void finish(){
    CHECK(!history_display_scope);CHECK(sui_calls==0);display_body={};after_flags={};
    active=false;nodes.clear();reset();fence_queued=false;last_fence=0;
}
}

void test_history_toolbar_current_layout(){
    using namespace history_ui_fixture;
    // Private cursor has no entries; shared Redo exists. Change only the
    // argument of each original native setter, with no added UI calls.
    setup();auto* undo=make(undo_id);auto* redo=make(redo_id);auto* unrelated=make(0x12345678);
    CHECK(skg::field<uint32_t>(t_history.data(),0x14)==0);
    display_body=[&](void* self,uint32_t elapsed){CHECK(self==t_display.data()&&elapsed==37);
        flags_at(undo,false,undo_return);flags_at(redo,false,redo_return);};
    update_display(t_display.data(),37);
    CHECK(updates==1&&mutations==2&&undo->writes==1&&redo->writes==1&&!undo->enabled&&redo->enabled);
    CHECK(unrelated->writes==0&&availability_calls[0]==1&&availability_calls[1]==1);
    CHECK(sui_calls==0&&layout_calls==0&&!history_display_scope&&skg::field<uint32_t>(t_history.data(),0x14)==0);
    // Wrong native callsite/mask/ID/display and absent authority forward the
    // original argument exactly once, without querying shared availability.
    for(unsigned reason=0;reason<8;++reason){
        setup();redo=make(redo_id);std::array<uint8_t,0x2C> other_display{};
        auto invoke=[&]{flags_at(redo,false,reason==1?0x1234:redo_return,
            reason==2?sui::kFlagEnabled|sui::kFlagVisible:sui::kFlagEnabled);};
        if(reason==3)skg::field<uint32_t>(redo->raw.data(),0x7C)=undo_id;
        if(reason==4)C.projection_scope=[] {return GestureScope{};};
        if(reason==5)applying=true;if(reason==6)t_active=false;
        display_body=[&](void*,uint32_t){invoke();};
        if(reason==0)invoke();else update_display(reason==7?other_display.data():t_display.data(),0);
        CHECK(mutations==1&&redo->writes==1&&!redo->enabled&&availability_calls[1]==0);
        CHECK(sui_calls==0&&layout_calls==0&&!history_display_scope);
    }
    setup();redo=make(redo_id);C.history_available=[](bool){return false;};
    display_body=[&](void*,uint32_t){flags_at(redo,true,redo_return);};update_display(t_display.data(),0);
    CHECK(mutations==1&&!redo->enabled);
    // Pending and callback-established cancellation both dominate cached true.
    // Clearing it in the callback uses the freshly checked cancellation state.
    for(unsigned reason=0;reason<3;++reason){
        setup();redo=make(redo_id);if(reason!=1)cancellation.emplace();
        C.history_available=[reason](bool){if(reason==1)cancellation.emplace();if(reason==2)cancellation.reset();return true;};
        display_body=[&](void*,uint32_t){flags_at(redo,false,redo_return);};update_display(t_display.data(),0);
        CHECK(mutations==1&&redo->enabled==(reason==2));
    }
    // Identity changes inside the policy callback cannot override a stale
    // window. Native setter calls still forward exactly once, unchanged.
    for(unsigned reason=0;reason<16;++reason){
        setup();undo=make(undo_id);redo=make(redo_id);
        auto replacement_mode=t_mode;auto replacement_display=t_display;auto replacement_layout=t_layout;
        std::array<uint8_t,0x8C> replacement_container{};
        C.history_available=[&,reason](bool is_redo){++availability_calls[is_redo];
            if(reason==0)++generation;if(reason==1)++t_scope.epoch;if(reason==2)++t_scope.author.generation;
            if(reason==3)++t_scope.project[0];if(reason==4)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
            if(reason==5)skg::field<void*>(t_display.data(),0x24)=nullptr;if(reason==6)t_active=false;
            if(reason==7)C.projection_scope=[] {return GestureScope{};};
            if(reason==8)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
            if(reason==9)skg::field<void*>(t_data.data(),0x10)=t_other_data.data();
            if(reason==10)t_mode_pointer=replacement_mode.data();
            if(reason==11)skg::field<void*>(t_mode.data(),0x14)=replacement_display.data();
            if(reason==12)++t_scope.author.slot;
            if(reason==13)skg::field<void*>(t_display.data(),0x24)=replacement_layout.data();
            if(reason==14)skg::field<void*>(t_layout.data(),0x14)=replacement_container.data();
            if(reason==15)skg::field<uint32_t>(redo->raw.data(),0x7C)=undo_id;
            return true;};
        display_body=[&](void*,uint32_t){flags_at(redo,false,redo_return);flags_at(undo,false,undo_return);};
        update_display(t_display.data(),0);
        CHECK(mutations==2&&!redo->enabled&&undo->enabled==(reason==15)&&writes.size()==2);
        CHECK(!history_display_scope&&sui_calls==0&&layout_calls==0);
    }
    // An unrelated nested native updater cannot borrow outer authority. On
    // return it restores the outer scope, whose identity is checked afresh.
    for(bool expire:{false,true}){
        setup();redo=make(redo_id);std::array<uint8_t,0x2C> other_display{};
        display_body=[&](void* self,uint32_t){
            if(self==t_display.data()){
                const auto* outer=history_display_scope;CHECK(outer);
                update_display(other_display.data(),0);CHECK(history_display_scope==outer);
                flags_at(redo,false,redo_return);
            }else{CHECK(!history_display_scope);flags_at(redo,false,redo_return);if(expire)++generation;}
        };
        update_display(t_display.data(),0);
        CHECK(updates==2&&mutations==2&&!writes[0].value&&writes[1].value==!expire);
        CHECK(availability_calls[1]==unsigned(!expire)&&!history_display_scope);
    }
    setup();HistoryDisplayPin sentinel;history_display_scope=&sentinel;
    display_body=[](void*,uint32_t){throw std::runtime_error("native updater failed");};
    bool threw=false;try{update_display(t_display.data(),0);}catch(const std::runtime_error&){threw=true;}
    CHECK(threw&&history_display_scope==&sentinel&&mutations==0);history_display_scope=nullptr;
    setup();redo=make(redo_id);C.history_available=[](bool)->bool{throw std::runtime_error("cached policy failed");};
    display_body=[&](void*,uint32_t){flags_at(redo,false,redo_return);};update_display(t_display.data(),0);
    CHECK(mutations==1&&!redo->enabled&&!history_display_scope);
    setup();redo=make(redo_id);after_flags=[] {throw std::runtime_error("native setter failed");};
    display_body=[&](void*,uint32_t){flags_at(redo,false,redo_return);};threw=false;
    try{update_display(t_display.data(),0);}catch(const std::runtime_error&){threw=true;}
    CHECK(threw&&mutations==1&&redo->enabled&&!history_display_scope);
    // A slow callback cannot enqueue a recurring fence while one is running,
    // or immediately refence when the queued keepalive reaches completion.
    setup();bool reentered=false;C.active=[&]{if(!reentered){reentered=true;t_time+=1;
        CHECK(fence_queued);editor_native::tick();CHECK(t_tasks.empty());}return true;};
    editor_native::tick();CHECK(fence_queued&&t_tasks.size()==1);drain();
    CHECK(reentered&&!fence_queued&&t_tasks.empty()&&last_fence==t_time);
    editor_native::tick();CHECK(t_tasks.empty());t_time+=.201;editor_native::tick();
    CHECK(fence_queued&&t_tasks.size()==1);drain();CHECK(!fence_queued&&last_fence==t_time&&t_tasks.empty());
    CHECK(sui_calls==0);
    setup();C.active=[]()->bool{t_time+=1;throw std::runtime_error("fence callback failed");};
    threw=false;try{fence_ui(nullptr);}catch(const std::runtime_error&){threw=true;}
    CHECK(threw&&!fence_queued&&last_fence==t_time);finish();
}
