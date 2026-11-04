namespace clone_input_fixture {
using namespace editor_native;
bool structure=false;unsigned clones=0,anchors=0,placement_queries=0,structure_queries=0,notices=0;
float cursor=2;bool anchor_writes=true;
std::function<void()> anchor_callback,tick_callback;
std::array<uint8_t,0x27E8> class_row;
void* SK_THISCALL marker(void* owner,void* actor){
    CHECK(owner==t_data.data());
    if(actor==t_actors[1].data() && clones)return t_rows[t_live.data()].data()+2*0x238+4;
    return t_marker(owner,actor);
}
uint8_t SK_CDECL anchor(void*){
    ++anchors;
    if(anchor_writes){
        skg::field<float>(t_context.data(),0x6C)=10-cursor;
        skg::field<float>(t_context.data(),0x70)=0;skg::field<float>(t_context.data(),0x74)=0;
    }
    if(anchor_callback){auto callback=std::move(anchor_callback);callback();}
    return 0; // Native fallback can produce all outputs with AL false.
}
void SK_THISCALL step(void*){
    ++t_steps;CHECK(clones==1);
    auto* copy=t_rows[t_live.data()].data()+2*0x238+4;
    skg::field<float>(copy,4)=cursor+skg::field<float>(t_context.data(),0x6C);
}
void SK_THISCALL no_step(void*){}
void SK_THISCALL tick(void*){
    ++t_ticks;
    if(skg::field<uint8_t>(t_context.data(),0x64)){
        skg::field<uint8_t>(t_context.data(),0x64)=0;anchor(t_actors[1].data());
    }
    ground_step(nullptr);vertical_step(nullptr);radial_step(nullptr);auxiliary_step(nullptr);
    if(tick_callback){auto callback=std::move(tick_callback);callback();}
}
bool SK_THISCALL clone(void* manager,void* actor){
    CHECK(manager==t_tool_pointer && actor==t_actors[0].data());
    CHECK(t_granted && structure && input_clone==marker_id(0));
    ++clones;start(t_data.data());
    auto source=std::vector<uint8_t>(t_rows[t_live.data()].begin(),t_rows[t_live.data()].begin()+0x238);
    auto& rows=t_rows[t_live.data()];rows.insert(rows.end(),source.begin(),source.end());rows_for(t_live.data());
    skg::field<uint32_t>(rows.data()+2*0x238+4,0x228)=901;
    select_native(manager,t_actors[1].data());
    skg::field<void*>(t_context.data(),0xC)=t_actors[1].data();
    skg::field<uint8_t>(t_context.data(),0x64)=1;skg::field<uint32_t>(t_context.data(),0x68)=1;
    return true;
}
uint32_t SK_THISCALL tool(void* manager,uint32_t action,uint32_t a,uint32_t b){
    if(action==11){++t_presses;return clone_selection_native(manager,t_actors[0].data());}
    const auto result=t_tool_action(manager,action,a,b);
    if(!action)skg::field<void*>(t_context.data(),0xC)=nullptr;
    return result;
}
void setup(){
    reset();structure=false;clones=anchors=placement_queries=structure_queries=notices=0;
    cursor=2;anchor_writes=true;anchor_callback=tick_callback={};
    a_marker=uintptr_t(marker);original_tool=tool;original_tick_tools=tick;original_clone_selection=clone;
    original_ground_step=step;original_vertical_step=original_radial_step=original_auxiliary_step=no_step;
#ifndef NATIVE_CLONE_INPUT_LEGACY
    a_capture_ground_anchor=uintptr_t(anchor);clone_replay.reset();clone_enrolling=clone_pose_blocked=false;
#endif
    auto* source=t_rows[t_live.data()].data()+4;
    skg::field<uint32_t>(source,0)=7;skg::field<float>(source,4)=10;
    skg::field<uint32_t>(source,0x228)=900;
    class_row.fill(0);skg::field<uint32_t>(class_row.data(),0)=7;
    skg::field<void*>(t_live.data(),0x2BF4)=class_row.data();skg::field<void*>(t_live.data(),0x2BF8)=class_row.data()+class_row.size();
    skg::field<float>(t_context.data(),0x6C)=73;skg::field<float>(t_context.data(),0x70)=74;skg::field<float>(t_context.data(),0x74)=75;
    C.gate=[](const auto& ids){
        ++placement_queries;
        // The already-owned legacy route normally selects its newly allocated
        // clone and asks the ordinary selection gate about that provisional ID.
        CHECK(ids==std::vector<editor::ObjectId>{marker_id(0)} ||
              (clones==1 && ids==std::vector<editor::ObjectId>{marker_id(2)}));
        return t_granted;
    };
    C.structure_gate=[](uint32_t kind){++structure_queries;CHECK(kind==7);return structure;};
    C.notice=[](const std::string&){++notices;};
    refresh_selection();CHECK(selection==std::vector<editor::ObjectId>{marker_id(0)});
}
void finish_reset(){
    anchor_callback=tick_callback={};reset();
}
void delayed(bool direct,bool placement){
    setup();t_granted=placement;
    if(direct)CHECK(!clone_selection_native(t_tool_pointer,t_actors[0].data()));
    else CHECK(tool_native(t_tool_pointer,11,17,23)==1);
    CHECK(held_tool && held_tool->action==11); // Genuine C57 RED: no held continuation.
    CHECK(!clones && !t_presses && !t_steps && !t_finishes && t_allocations.empty());
    CHECK(placement_queries && structure_queries && anchors==1);
    CHECK(skg::field<float>(t_context.data(),0x6C)==73 && skg::field<float>(t_context.data(),0x70)==74 && skg::field<float>(t_context.data(),0x74)==75);
    cursor=9;CHECK(tool_native(t_tool_pointer,0,0,0)==1 && held_tool->released);
    structure=t_granted=true;
    continue_input(nullptr);CHECK(held_tool && !clones); // Not the press's actual frame.
    ++t_frame;continue_input(nullptr);
    CHECK(!held_tool && clones==1 && t_presses==1 && t_steps==1 && t_finishes==1 && t_ticks==2);
    CHECK(anchors==2); // Late initializer ran, then original anchor replaced it.
    CHECK(skg::field<float>(t_rows[t_live.data()].data()+2*0x238+4,4)==17);
    CHECK(skg::field<float>(t_rows[t_live.data()].data()+4,4)==10);
    continue_input(nullptr);CHECK(clones==1 && t_finishes==1);
    CHECK(gesture_clone==marker_id(0));finish_reset();
}
void test(){
    delayed(false,true);delayed(false,false);delayed(true,true);
    for(unsigned reason=0;reason<16;++reason){
        setup();t_granted=true;tool_native(t_tool_pointer,11,0,0);CHECK(held_tool && !clones);
        tool_native(t_tool_pointer,0,0,0);++t_frame;
        if(reason==0)++t_revision;
        if(reason==1)++t_scope.epoch;
        if(reason==2)++generation;
        if(reason==3)t_time+=3;
        if(reason==4)native_fixture_escape_state=SHORT(0x8000);
        if(reason==5)held_tool->focused=true;
        if(reason==6)skg::field<uintptr_t>(t_context.data(),0x10)=1;
        if(reason==7)skg::field<void*>(t_context.data(),4)=t_actors[1].data();
        if(reason==8)++skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0x228);
        if(reason==9)++skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0);
        if(reason==10)skg::field<float>(t_rows[t_live.data()].data()+4,4)+=1;
        if(reason==11)deny_held_input({{editor::ScopeKind::Structure,{},{}}});
        if(reason==12)deny_held_input({{editor::ScopeKind::Class,{(2ull<<32)|7,1},{}}});
        if(reason==13)skg::field<editor::ResourceKey>(t_live.data(),8).instance=9;
        if(reason==14)C.object_revision=[](editor::ObjectId id)->std::optional<editor::Revision>{return id.id>>32==2?1:0;};
        if(reason==15)pending.emplace_back(Pending{});
        structure=true;continue_input(nullptr);
        if(held_tool || clones || t_presses || t_steps || t_finishes)std::printf("clone retirement reason=%u held=%u clones=%u press=%u steps=%u finish=%u\n",reason,unsigned(held_tool.has_value()),clones,t_presses,t_steps,t_finishes);
        CHECK(!held_tool && !clones && !t_presses && !t_steps && !t_finishes);finish_reset();
    }
    for(unsigned reason=0;reason<10;++reason){
        setup();t_granted=true;
        if(reason==0)anchor_writes=false;
        if(reason==1)anchor_callback=[] {++generation;};
        if(reason==2)anchor_callback=[] {++t_revision;};
        if(reason==3)anchor_callback=[] {held_tool.reset();};
        if(reason==4)C.structure_gate=[](uint32_t){++t_scope.epoch;return false;};
        if(reason==5)C.gate=[](const auto&){pending.emplace_back(Pending{});return true;};
        if(reason==6)anchor_callback=[] {skg::field<float>(t_rows[t_live.data()].data()+4,4)+=1;};
        if(reason==7)anchor_callback=[] {class_row[20]^=1;};
        if(reason==8)anchor_callback=[] {++skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0x228);};
        if(reason==9)anchor_callback=[] {held_tool.reset();ground_step(nullptr);vertical_step(nullptr);radial_step(nullptr);auxiliary_step(nullptr);};
        tool_native(t_tool_pointer,11,0,0);
        CHECK(!held_tool && !clones && !t_steps && t_allocations.empty());finish_reset();
    }
    for(uint32_t modifiers:{1u,2u,3u}){
        setup();t_granted=true;skg::field<uint32_t>(t_mode.data(),0x40)=modifiers;
        tool_native(t_tool_pointer,11,0,0);CHECK(!held_tool && !clones && !anchors && notices==1);finish_reset();
    }
    setup();t_granted=structure=true;skg::field<uint32_t>(t_mode.data(),0x40)=1;
    tool_native(t_tool_pointer,11,0,0);CHECK(clones==1 && !held_tool && !anchors);finish_reset();
    setup();t_granted=structure=true;skg::field<uintptr_t>(t_context.data(),0x10)=1;
    tool_native(t_tool_pointer,11,0,0);CHECK(!clones && !held_tool && !anchors);finish_reset();
    setup();t_granted=true;tool_native(t_tool_pointer,11,0,0);CHECK(held_tool);
    tool_native(t_tool_pointer,0,0,0);++t_frame;structure=true;
    // A callback retiring the optional during first-tick initialization must
    // not expose any of the four authored steps in the rest of that tick.
    anchor_callback=[] {held_tool.reset();};
    continue_input(nullptr);CHECK(clones==1 && !held_tool && !t_steps && !t_finishes);
    finish_reset();
}
}
