// Included inside editor_native. Value-only first-use Terraform input; no
// native clone, scale write or history boundary is opened before ownership.
bool terrain_input_current(const std::shared_ptr<HeldTerrainInput>& pin){
    const auto live=[&]{return held_terrain_input==pin && terrain_delete_raw(pin->target);};
    if(!live() || applying || !active() || !live())return false;
    const auto scope_callback=C.gesture_scope;const auto revision_callback=C.object_revision;
    const auto scope=scope_callback?scope_callback():GestureScope{};
    if(!live() || scope!=pin->target.scope)return false;
    const auto revision=revision_callback?revision_callback(pin->target.object):std::nullopt;
    if(!live() || revision!=pin->target.revision)return false;
    const auto observed=state();
    return live() && observed.ready && !observed.gesture && observed.terraforming && observed.act==pin->target.act &&
        !(GetKeyState(VK_ESCAPE)&0x8000) && (!pin->target.focused || real_focus()) &&
        H->time_seconds()-pin->target.started<=2;
}
bool terrain_input_scene(const HeldTerrainDelete& pin){
    const auto raw=[&]{
        auto* m=mode();auto* d=data(m);auto* r=resource(d);
        auto* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
        return generation==pin.generation && uintptr_t(m)==pin.owner.mode && uintptr_t(d)==pin.owner.data &&
            uintptr_t(r)==pin.owner.resource && skg::readable(r,kResourceSize) &&
            skg::field<editor::ResourceKey>(r,8)==pin.owner.source && uintptr_t(terraform_mode())==pin.terrain &&
            a_tools && uintptr_t(*reinterpret_cast<void**>(a_tools))==pin.manager &&
            uintptr_t(context)==pin.context && skg::readable(context,0x14) && skg::field<uint32_t>(context,0)==pin.act;
    };
    if(!raw())return false;
    const auto callback=C.gesture_scope;
    return callback && callback()==pin.scope && raw();
}
bool append_terrain_input(const std::shared_ptr<HeldTerrainInput>& pin,uint32_t a,uint32_t b){
    float delta=0;std::memcpy(&delta,&b,4);
    if(held_terrain_input!=pin || pin->steps.size()>=64 ||
       (pin->action==6 && (!std::isfinite(delta) || std::abs(delta)>10000))){
        if(held_terrain_input==pin)held_terrain_input.reset();return false;
    }
    try{pin->steps.emplace_back(a,b);return true;}
    catch(const std::bad_alloc&){if(held_terrain_input==pin)held_terrain_input.reset();return false;}
}
std::optional<uint32_t> continue_terrain_input(){
    const auto pin=held_terrain_input;if(!pin)return std::nullopt;
    const auto retire=[&]{if(held_terrain_input==pin)held_terrain_input.reset();};
    if(!terrain_input_current(pin)){retire();return std::nullopt;}
    const auto pending_callback=C.transaction_pending;
    if(pending_callback && pending_callback())return std::nullopt;
    if(!terrain_input_current(pin)){retire();return std::nullopt;}
    const auto gate=C.terrain_gate;
    if(!gate){retire();return std::nullopt;}
    const bool granted=gate({pin->target.object},{});
    if(!terrain_input_current(pin)){retire();return std::nullopt;}
    if(!granted)return std::nullopt;
    retire();
    auto* manager=reinterpret_cast<void*>(pin->target.manager);
    auto* m=reinterpret_cast<void*>(pin->target.owner.mode);
    const auto old_latch=skg::field<uint8_t>(manager,4);
    const auto old_modifiers=skg::field<uint32_t>(m,0x40);
    skg::field<uint8_t>(manager,4)=1;skg::field<uint32_t>(m,0x40)=pin->modifiers;
    uint32_t result=1;
    for(const auto& [a,b]:pin->steps){
        if(!terrain_input_scene(pin->target))break;
        result=original_terrain_tool(manager,pin->action,a,b);
        if(!result)break;
    }
    if(result && terrain_input_scene(pin->target)){
        if(pin->action==11){
            original_terrain_tick_tools(manager);
            if(pin->released && terrain_input_scene(pin->target))original_terrain_tool(manager,0,0,0);
        }else if(pin->released)original_terrain_end_scale(manager);
    }
    // Native calls may replace the entire editor. Restore borrowed latches
    // only to the exact scene whose values we saved, never an old raw pointer.
    if(terrain_input_scene(pin->target)){
        skg::field<uint32_t>(m,0x40)=old_modifiers;skg::field<uint8_t>(manager,4)=old_latch;
    }
    return result;
}
uint32_t begin_terrain_input(void* self,const State& observed,uint32_t action,uint32_t a,uint32_t b){
    if(!observed.ready || observed.gesture || held_tool || held_terrain_delete || terrain_pending ||
       !original_terrain_tick_tools || !original_terrain_end_scale || !skg::readable(self,8))return 1;
    std::shared_ptr<HeldTerrainInput> pin;
    try{pin=std::make_shared<HeldTerrainInput>();}catch(const std::bad_alloc&){return 1;}
    auto& target=pin->target;std::string error;
    if(!capture_scene_owner(target.owner,error))return 1;
    auto* tool=terraform_mode();auto* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    if(!skg::readable(tool,0x1BC) || !skg::readable(context,0x14))return 1;
    auto* descriptor=skg::field<void*>(tool,0x1B0);const auto object=terrain_record(descriptor);
    if(!object)return 1;
    target.generation=observed.generation;target.object=*object;
    target.manager=uintptr_t(self);target.terrain=uintptr_t(tool);target.context=uintptr_t(context);
    target.actor=skg::field<uintptr_t>(descriptor,0);target.descriptor=uintptr_t(descriptor);
    target.definition=skg::field<uintptr_t>(descriptor,4);target.index=skg::field<uint32_t>(descriptor,8);
    target.act=observed.act;target.started=H->time_seconds();target.focused=real_focus();
    pin->action=action;pin->modifiers=skg::field<uint32_t>(mode(),0x40);
    const auto scope_callback=C.gesture_scope;const auto revision_callback=C.object_revision;
    target.scope=scope_callback?scope_callback():GestureScope{};
    const auto revision=revision_callback?revision_callback(*object):std::nullopt;
    if(!valid_scope(target.scope) || !revision)return 1;
    target.revision=*revision;held_terrain_input=pin;
    if(!append_terrain_input(pin,a,b))return 1;
    return continue_terrain_input().value_or(1);
}
bool SK_THISCALL terrain_end_scale_native(void* self){
    const auto observed=state();
    if(applying || !active() || !observed.terraforming)return original_terrain_end_scale(self);
    const auto pin=held_terrain_input;
    if(pin && pin->action==6){
        if(uintptr_t(self)==pin->target.manager && terrain_input_current(pin))pin->released=true;
        else if(held_terrain_input==pin)held_terrain_input.reset();
        return true;
    }
    return original_terrain_end_scale(self);
}
