// Included beside the tool hooks. This capsule owns values only, never Native
// references. Native scene pointers are identities and are freshly resolved.
struct HeldClone {
    const SkHost* host=nullptr;SkMod* module=nullptr;
    SceneOwner owner;GestureScope scope;uint64_t generation=0,frame=0;
    editor::ObjectId object;editor::Revision revision=0,class_revision=0;
    uint32_t slot=0,class_index=0,key=0,modifiers=0;
    void* context=nullptr;void* manager=nullptr;void* actor=nullptr;void* dragged=nullptr;
    struct Row {uint32_t slot,kind,key;bool operator==(const Row&)const=default;};
    std::vector<Row> roster;
    std::array<uint8_t,0x27E8> class_row{};
    std::array<uint8_t,0x234> source{};
    std::array<uint32_t,3> anchor{};
};
enum class CloneInput {Allowed,Held,Denied};

bool clone_context(const HeldClone& pin){
    if(H!=pin.host || M!=pin.module || generation!=pin.generation || applying || cancellation ||
       !a_context || !a_tools || *reinterpret_cast<void**>(a_context)!=pin.context ||
       *reinterpret_cast<void**>(a_tools)!=pin.manager)return false;
    auto* m=mode();auto* d=data(m);auto* r=resource(d);
    return uintptr_t(m)==pin.owner.mode && uintptr_t(d)==pin.owner.data && uintptr_t(r)==pin.owner.resource &&
        skg::readable(r,kResourceSize) && skg::field<editor::ResourceKey>(r,8)==pin.owner.source &&
        skg::readable(pin.context,0x8C) && !skg::field<uintptr_t>(pin.context,0x10);
}
bool clone_raw(const HeldClone& pin,bool before){
    if(!clone_context(pin))return false;
    if(before && (ordinary_capture_pending() || skg::field<void*>(pin.context,4)!=pin.actor ||
                  skg::field<void*>(pin.context,0xC)!=pin.dragged))return false;
    Rows map;if(!rows(reinterpret_cast<void*>(pin.owner.resource),0x2C10,0x238,kPlacementLimit,map))return false;
    size_t found=0;const uint8_t* source=nullptr;
    for(auto p=map.begin;p!=map.end;p+=0x238)if(live_slot(p)){
        const uint32_t slot=slot_index(map,p);
        if(slot==pin.slot)source=p+4;
        if(found<pin.roster.size() && slot==pin.roster[found].slot){
            if(skg::field<uint32_t>(p+4,0)!=pin.roster[found].kind ||
               skg::field<uint32_t>(p+4,0x228)!=pin.roster[found].key)return false;
            ++found;
        }else if(before)return false;
    }
    if(found!=pin.roster.size() || !source)return false;
    if(before){
        Rows classes;if(!rows(reinterpret_cast<void*>(pin.owner.resource),0x2BF4,0x27E8,1024,classes))return false;
        const uint8_t* selected=nullptr;
        for(auto p=classes.begin;p!=classes.end;p+=0x27E8)if(skg::field<uint32_t>(p,0)==pin.class_index){
            if(selected)return false;selected=p;
        }
        if(!selected || std::memcmp(selected,pin.class_row.data(),pin.class_row.size()))return false;
    }
    // Before allocation every byte must still be the selected source. Native
    // CloneSelection may subsequently update derived visibility/selection flags;
    // its source pose, class and editor key must remain exact throughout replay.
    return before?std::memcmp(source,pin.source.data(),pin.source.size())==0:
        skg::field<uint32_t>(const_cast<uint8_t*>(source),0)==pin.class_index &&
        skg::field<uint32_t>(const_cast<uint8_t*>(source),0x228)==pin.key &&
        std::memcmp(source+4,pin.source.data()+4,0x20)==0;
}
bool clone_current(const HeldClone& pin,bool before){
    if(!clone_raw(pin,before))return false;
    const auto live=[&]{return clone_raw(pin,before);};
    const auto active_callback=C.active;const auto scope_callback=C.gesture_scope;const auto revision_callback=C.object_revision;
    if(!active_callback || !scope_callback || !revision_callback || !active_callback() || !live())return false;
    const auto scope=scope_callback();if(!live() || scope!=pin.scope)return false;
    const auto revision=revision_callback(pin.object);if(!live() || revision!=pin.revision)return false;
    const auto class_revision=revision_callback({(2ull<<32)|pin.class_index,1});
    return live() && class_revision==pin.class_revision;
}
bool clone_held(const std::shared_ptr<HeldClone>& pin){return held_tool && held_tool->clone==pin;}
bool clone_held_current(const std::shared_ptr<HeldClone>& pin,bool before){
    return clone_held(pin) && clone_current(*pin,before) && clone_held(pin);
}
bool clone_tool_current(const HeldTool& item){return item.clone && clone_raw(*item.clone,false);}

CloneInput gate_clone_input(void* manager,void* actor,uint32_t a,uint32_t b,
                           std::optional<editor::ObjectId>* accepted=nullptr){
    if(clone_enrolling || held_tool || clone_replay || !H || !actor || !a_marker || !C.gate || !C.structure_gate)return CloneInput::Denied;
    struct Enrolling {Enrolling(){clone_enrolling=true;}~Enrolling(){clone_enrolling=false;}} enrolling;
    try{
        auto pin=std::make_shared<HeldClone>();
        pin->host=H;pin->module=M;pin->generation=generation;pin->frame=H->frame_index();
        auto* m=mode();auto* d=data(m);auto* r=resource(d);
        if(!skg::readable(r,kResourceSize) || !skg::readable(m,0x44) || !a_context || !a_tools)return CloneInput::Denied;
        pin->owner={uintptr_t(m),uintptr_t(d),uintptr_t(r),skg::field<editor::ResourceKey>(r,8)};
        pin->context=*reinterpret_cast<void**>(a_context);pin->manager=manager;pin->actor=actor;
        if(!clone_context(*pin) || skg::field<void*>(pin->context,4)!=actor ||
           ordinary_capture_pending())return CloneInput::Denied;
        pin->dragged=skg::field<void*>(pin->context,0xC);
        pin->modifiers=skg::field<uint32_t>(m,0x40);
        auto* marker=skg::thiscall<void*>(a_marker,d,actor);
        if(!clone_context(*pin) || skg::field<void*>(pin->context,4)!=actor)return CloneInput::Denied;
        Rows map;if(!rows(r,0x2C10,0x238,kPlacementLimit,map))return CloneInput::Denied;
        bool found=false;
        for(auto p=map.begin;p!=map.end;p+=0x238)if(live_slot(p)){
            pin->roster.push_back({slot_index(map,p),skg::field<uint32_t>(p+4,0),skg::field<uint32_t>(p+4,0x228)});
            if(p+4==marker){pin->slot=slot_index(map,p);found=true;std::memcpy(pin->source.data(),marker,pin->source.size());}
        }
        if(!found || !clone_context(*pin))return CloneInput::Denied;
        pin->class_index=skg::field<uint32_t>(marker,0);pin->key=skg::field<uint32_t>(marker,0x228);
        Rows classes;if(!rows(r,0x2BF4,0x27E8,1024,classes))return CloneInput::Denied;
        bool class_found=false;
        for(auto p=classes.begin;p!=classes.end;p+=0x27E8)if(skg::field<uint32_t>(p,0)==pin->class_index){
            if(class_found)return CloneInput::Denied;
            class_found=true;std::memcpy(pin->class_row.data(),p,pin->class_row.size());
        }
        if(!class_found)return CloneInput::Denied;
        pin->object=marker_id(pin->slot);
        const auto scope_callback=C.gesture_scope;const auto revision_callback=C.object_revision;
        if(!scope_callback || !revision_callback)return CloneInput::Denied;
        pin->scope=scope_callback();if(!valid_scope(pin->scope) || !clone_raw(*pin,true))return CloneInput::Denied;
        const auto revision=revision_callback(pin->object);if(!revision || !clone_raw(*pin,true))return CloneInput::Denied;pin->revision=*revision;
        const auto class_revision=revision_callback({(2ull<<32)|pin->class_index,1});
        if(!class_revision || !clone_raw(*pin,true))return CloneInput::Denied;pin->class_revision=*class_revision;
        const auto placement_gate=C.gate;const auto structure_gate=C.structure_gate;
        const bool placement=placement_gate({pin->object});if(!clone_current(*pin,true))return CloneInput::Denied;
        const bool structure=structure_gate(pin->class_index);if(!clone_current(*pin,true))return CloneInput::Denied;
        if(placement && structure){if(accepted)*accepted=pin->object;return CloneInput::Allowed;}
        if(pin->modifiers&3){
            const auto notice=C.notice;if(notice)notice("This modified clone was cancelled while waiting for ownership. Try again after ownership is available.");
            return CloneInput::Denied;
        }
        if(!a_capture_ground_anchor || !original_tick_tools || !original_tool)return CloneInput::Denied;
        const auto observed=state();if(!clone_current(*pin,true) || !observed.ready || observed.gesture || observed.terraforming || pin->dragged)return CloneInput::Denied;
        held_tool=HeldTool{pin->scope,pin->generation,pin->object,pin->revision,manager,11,a,b,0,false,
            H->time_seconds(),pin->modifiers,false,real_focus(),false,0,uintptr_t(actor),pin};
        if(!clone_held_current(pin,true)){if(clone_held(pin))held_tool.reset();return CloneInput::Denied;}
        std::array<uint32_t,3> previous;std::memcpy(previous.data(),static_cast<uint8_t*>(pin->context)+0x6C,12);
        const std::array<uint32_t,3> sentinel{0x7FC00000,0x7FC00000,0x7FC00000};
        // Verified EFD860 does not read these output words. Its AL can be false
        // even after the fallback writes all three, so finite outputs prove it
        // completed. Restore this derived scratch before awaiting any grant.
        struct RestoreAnchor {
            HeldClone& pin;std::array<uint32_t,3> previous;
            ~RestoreAnchor(){if(clone_context(pin) && skg::field<void*>(pin.context,4)==pin.actor &&
                !skg::field<void*>(pin.context,0xC))std::memcpy(static_cast<uint8_t*>(pin.context)+0x6C,previous.data(),12);}
        } restore{*pin,previous};
        std::memcpy(static_cast<uint8_t*>(pin->context)+0x6C,sentinel.data(),12);
        skg::cdecl_call<uint8_t>(a_capture_ground_anchor,actor);
        if(!clone_held_current(pin,true)){if(clone_held(pin))held_tool.reset();return CloneInput::Denied;}
        std::memcpy(pin->anchor.data(),static_cast<uint8_t*>(pin->context)+0x6C,12);
        for(auto word:pin->anchor)if(!std::isfinite(std::bit_cast<float>(word))){held_tool.reset();return CloneInput::Denied;}
        return CloneInput::Held;
    }catch(...){if(held_tool && held_tool->clone)held_tool.reset();return CloneInput::Denied;}
}

void continue_clone_input(){
    const auto pin=held_tool->clone;
    if(!pin || clone_enrolling || clone_replay)return;
    const auto retire=[&]{if(clone_held(pin))finish_held_input();};
    if(!clone_held_current(pin,true) || (GetKeyState(VK_ESCAPE)&0x8000) ||
       (held_tool->focused && !real_focus()) || H->time_seconds()-held_tool->started>2){retire();return;}
    if(H->frame_index()<=pin->frame)return;
    const auto placement_gate=C.gate;const auto structure_gate=C.structure_gate;
    if(!placement_gate || !structure_gate){retire();return;}
    const bool placement=placement_gate({pin->object});if(!clone_held_current(pin,true)){retire();return;}
    const bool structure=structure_gate(pin->class_index);if(!clone_held_current(pin,true)){retire();return;}
    if(!placement || !structure)return;
    const auto observed=state();if(!clone_held_current(pin,true) || !observed.ready || observed.gesture){retire();return;}
    const auto item=*held_tool;
    const auto previous_modifiers=skg::field<uint32_t>(mode(),0x40);
    const auto previous_latch=skg::field<uint8_t>(pin->manager,4);
    struct RestoreInput {
        HeldClone& pin;uint32_t modifiers;uint8_t latch;
        ~RestoreInput(){if(clone_context(pin)){
            skg::field<uint32_t>(mode(),0x40)=modifiers;skg::field<uint8_t>(pin.manager,4)=latch;}}
    } restore{*pin,previous_modifiers,previous_latch};
    skg::field<uint32_t>(mode(),0x40)=pin->modifiers;skg::field<uint8_t>(pin->manager,4)=1;
    struct Replay {
        Replay(std::shared_ptr<HeldClone> p){clone_replay=std::move(p);clone_pose_blocked=true;}
        ~Replay(){clone_pose_blocked=false;clone_replay.reset();}
    } replay(pin);
    struct Source {std::optional<editor::ObjectId> previous=input_clone;Source(editor::ObjectId id){input_clone=id;}~Source(){input_clone=previous;}} source(pin->object);
    const auto result=original_tool(pin->manager,11,item.a,item.b);
    if(!result || !clone_held_current(pin,false)){retire();return;}
    held_tool->native_started=true;
    // Consume Native's flag64 initializer with every authored step held. It
    // establishes radius/elevation and may replace the derived anchor. Install
    // the original press witness only after that one initializer has finished.
    original_tick_tools(pin->manager);
    if(!clone_held_current(pin,false) || skg::field<uint8_t>(pin->context,0x64) ||
       skg::field<uint32_t>(pin->context,0x68)!=1){retire();return;}
    std::memcpy(static_cast<uint8_t*>(pin->context)+0x6C,pin->anchor.data(),12);
    held_tool.reset();
    clone_pose_blocked=false;
    original_tick_tools(pin->manager);
    if(!clone_current(*pin,false))return;
    if(item.released)original_tool(pin->manager,0,0,0);
}
