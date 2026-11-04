// Actual i686 adapter ABI with controlled native producer/finalizer doubles.
namespace {
std::vector<uint32_t> t_first_steps;
bool t_first_replace=false;
uint32_t t_first_raw(float value){uint32_t raw;std::memcpy(&raw,&value,4);return raw;}
uint32_t SK_THISCALL t_first_tool(void* self,uint32_t action,uint32_t,uint32_t b){
    CHECK(self==t_tool_pointer&&t_granted&&skg::field<uint32_t>(t_mode.data(),0x40)==0x42);
    if(action==6){
        if(!skg::field<uint8_t>(self,5)){skg::field<uint8_t>(self,5)=1;t_terrain_start(t_terraform.data());}
        t_first_steps.push_back(b);value(t_live.data(),0)+=1;
    }else if(action==11){t_first_steps.push_back(action);t_terrain_start(t_terraform.data());value(t_live.data(),0)+=100;}
    else {CHECK(action==0);++t_finishes;t_terrain_commit(t_terraform.data());}
    if(t_first_replace){++generation;skg::field<uint32_t>(t_mode.data(),0x40)=0x99;}
    return 1;
}
bool SK_THISCALL t_first_end_scale(void* self){
    CHECK(self==t_tool_pointer);
    if(skg::field<uint8_t>(self,5)){
        t_terrain_commit(t_terraform.data());skg::field<uint8_t>(self,5)=0;
    }
    return true;
}
void t_first_setup(){
    setup_terrain_delete();t_first_steps.clear();t_first_replace=false;t_granted=false;
    skg::field<uint32_t>(t_mode.data(),0x40)=0x42;skg::field<uint8_t>(t_tool_pointer,5)=0;
    original_terrain_tool=t_first_tool;original_terrain_end_scale=t_first_end_scale;
}
void test_terrain_first_use(){
    for(auto action:{6u,11u})for(bool quick:{false,true}){
        t_first_setup();const auto native=t_live;
        terrain_tool_native(t_tool_pointer,action,17,t_first_raw(1));
        CHECK(held_terrain_input&&held_terrain_input->action==action&&!held_tool);
        CHECK(t_first_steps.empty()&&!terrain_starts&&!terrain_commits&&t_live==native);
        terrain_tick_tools(t_tool_pointer);CHECK(!t_ticks&&!t_steps);
        if(action==6)terrain_tool_native(t_tool_pointer,action,19,t_first_raw(2));
        else terrain_tool_native(t_tool_pointer,action,17,t_first_raw(1)); // repeat cannot allocate another clone
        CHECK(held_terrain_input->steps.size()==(action==6?2u:1u));
        if(quick){
            if(action==6)CHECK(terrain_end_scale_native(t_tool_pointer));
            else terrain_tool_native(t_tool_pointer,0,0,0);
            CHECK(held_terrain_input&&held_terrain_input->released);
        }
        continue_input(nullptr);CHECK(t_first_steps.empty()&&t_live==native);
        skg::field<uint8_t>(t_tool_pointer,4)=0;
        skg::field<uint32_t>(t_mode.data(),0x40)=0; // native release may already have cleared modifiers
        t_granted=true;continue_input(nullptr);
        CHECK(!held_terrain_input&&terrain_starts==1&&terrain_commits==unsigned(quick));
        CHECK(t_first_steps.size()==(action==6?2u:1u));
        if(action==6)CHECK(t_first_steps==std::vector<uint32_t>({t_first_raw(1),t_first_raw(2)}));
        CHECK(value(t_live.data(),0)==(action==6?3:99)); // Clone's ordinary movement tick consumes the retained gesture.
        CHECK(skg::field<uint8_t>(t_tool_pointer,4)==0&&skg::field<uint32_t>(t_mode.data(),0x40)==0);
        CHECK(t_ticks==unsigned(action==11));
        if(!quick){
            skg::field<uint32_t>(t_mode.data(),0x40)=0x42;
            if(action==6){
                CHECK(state().gesture);
                terrain_tool_native(t_tool_pointer,6,23,t_first_raw(3));
                CHECK(t_first_steps==std::vector<uint32_t>({t_first_raw(1),t_first_raw(2),t_first_raw(3)}));
                CHECK(terrain_starts==1&&value(t_live.data(),0)==4);
                CHECK(terrain_end_scale_native(t_tool_pointer));
            }
            else terrain_tool_native(t_tool_pointer,0,0,0);
        }
        CHECK(terrain_commits==1);
    }
    for(auto action:{6u,11u})for(unsigned reason=0;reason<20;++reason){
        t_first_setup();const auto native=t_live;
        terrain_tool_native(t_tool_pointer,action,0,t_first_raw(1));CHECK(held_terrain_input);
        if(reason==0)++t_revision;if(reason==1)++t_scope.epoch;if(reason==2)++generation;
        if(reason==3)++skg::field<uint32_t>(t_context.data(),0);
        if(reason==4)skg::field<void*>(t_context.data(),4)=t_actors[1].data();
        if(reason==5)skg::field<uintptr_t>(t_context.data(),0x10)=123;
        if(reason==6)t_time+=3;if(reason==7)held_terrain_input->target.focused=true;
        if(reason==8)deny_input({{editor::ScopeKind::Document,{},{}}});
        if(reason==9)deny_input({{editor::ScopeKind::Terrain,{55,1},{}}});
        if(reason==10)deny_input({{editor::ScopeKind::Authoring,{},{}}});
        if(reason==11)native_fixture_escape_state=SHORT(0x8000);
        if(reason==12)t_active=false;
        if(reason==13)skg::field<editor::ResourceKey>(t_live.data(),8).instance++;
        if(reason==14)skg::field<void*>(t_terraform.data(),0x1B0)=nullptr;
        if(reason==15)act_identity.terrain.clear();
        if(reason==16)skg::field<bool>(t_terrain_definition.data(),0x68)=true;
        if(reason==17)C.object_revision=[](editor::ObjectId)->std::optional<editor::Revision>{return std::nullopt;};
        if(reason==18)skg::field<void*>(t_terraform.data(),0x19C)=nullptr;
        if(reason==19)skg::field<void*>(t_context.data(),0xC)=t_actors[0].data();
        const auto changed=t_live;t_granted=true;continue_input(nullptr);
        CHECK(!held_terrain_input&&t_first_steps.empty()&&!terrain_starts&&!terrain_commits&&t_live==changed);
        (void)native;
    }
    for(auto action:{6u,11u})for(unsigned changed=0;changed<3;++changed){
        t_first_setup();terrain_tool_native(t_tool_pointer,action,0,t_first_raw(1));CHECK(held_terrain_input);
        C.terrain_gate=[changed](const auto&,const auto&){
            if(changed==0)++t_scope.epoch;
            if(changed==1)skg::field<editor::ResourceKey>(t_live.data(),8).instance++;
            if(changed==2)held_terrain_input.reset();return true;
        };
        continue_input(nullptr);CHECK(!held_terrain_input&&t_first_steps.empty()&&!terrain_starts);
    }
    t_first_setup();terrain_tool_native(t_tool_pointer,6,0,t_first_raw(1));CHECK(held_terrain_input);
    deny_input({{editor::ScopeKind::Terrain,{99,1},{}}});CHECK(held_terrain_input);
    C.transaction_pending=[]{return true;};t_granted=true;continue_input(nullptr);
    CHECK(held_terrain_input&&t_first_steps.empty());C.transaction_pending=[]{return false;};
    terrain_end_scale_native(t_tool_pointer);continue_input(nullptr);CHECK(!held_terrain_input&&terrain_commits==1);
    t_first_setup();
    for(unsigned i=0;i<64;++i)terrain_tool_native(t_tool_pointer,6,0,t_first_raw(1));
    CHECK(held_terrain_input&&held_terrain_input->steps.size()==64);
    terrain_tool_native(t_tool_pointer,6,0,t_first_raw(1));CHECK(!held_terrain_input&&t_first_steps.empty());
    for(auto invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),10001.f}){
        t_first_setup();terrain_tool_native(t_tool_pointer,6,0,t_first_raw(invalid));CHECK(!held_terrain_input&&t_first_steps.empty());
    }
    t_first_setup();t_granted=true;terrain_tool_native(t_tool_pointer,6,0,t_first_raw(1));
    CHECK(!held_terrain_input&&t_first_steps.size()==1&&terrain_starts==1);
    terrain_end_scale_native(t_tool_pointer);CHECK(terrain_commits==1);
    t_first_setup();terrain_tool_native(t_tool_pointer,6,0,t_first_raw(1));terrain_end_scale_native(t_tool_pointer);
    t_first_replace=true;t_granted=true;continue_input(nullptr);
    CHECK(!held_terrain_input&&skg::field<uint32_t>(t_mode.data(),0x40)==0x99&&!terrain_commits);
    reset();
}
} // namespace
