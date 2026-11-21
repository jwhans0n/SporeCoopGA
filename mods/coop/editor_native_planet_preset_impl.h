// Verified Tab0 selected-item boundary. Value owners only, no Native AddRef.
using PlanetPresetMessageFn=bool(SK_THISCALL*)(void*,uint32_t,void*);
PlanetPresetMessageFn original_planet_preset_message;
uintptr_t a_planet_preset_card_vtable,a_planet_preset_message_vtable,a_planet_palette_vtable;
const std::vector<uint32_t>& planet_preset_fields(){
    static const std::vector<uint32_t> fields{0x03A23F98,0x03A23F99,0x07B18058,
        0x0536250C,0x0536250D,0x968C496A,0x968C496B,0xB6929C94,0xB6929C95,
        0xB6929C92,0xB6929C93,0x56B14F05,0x03ABB0AF,
        0x04E13CB7,0x073154E0,0x073137BB,0x072BEC91};
    return fields;
}
struct HeldPlanetPreset {
    SceneOwner owner;GestureScope scope;uint64_t generation=0,serial=0;uint32_t act=0;
    uintptr_t tool=0;void* display=nullptr;void* terraform=nullptr;void* palette=nullptr;
    void* card=nullptr;void* layout=nullptr;void* backing=nullptr;void* root=nullptr;void* main=nullptr;
    uintptr_t root_vtable=0,main_vtable=0;
    uint32_t instance=0,group=0;double started=0;bool focused=false;
    std::function<bool()> active;
    std::function<GestureScope()> scope_current;
    std::function<bool(const std::vector<editor::ObjectId>&,const std::vector<uint32_t>&)> gate;
    std::map<uint32_t,std::pair<editor::Revision,std::optional<editor::Value>>> guards;
};
std::optional<HeldPlanetPreset> held_planet_preset;
uint64_t planet_preset_serial=0;bool planet_preset_busy=false;
bool planet_preset_raw_current(const HeldPlanetPreset& pin){
    skg::ReadOnlyMemoryScope memory;
    auto* m=mode();auto* d=data(m);auto* r=resource(d);
    if(!supported || generation!=pin.generation || uintptr_t(m)!=pin.owner.mode || uintptr_t(d)!=pin.owner.data ||
       uintptr_t(r)!=pin.owner.resource || !skg::readable(r,kResourceSize) || skg::field<editor::ResourceKey>(r,8)!=pin.owner.source ||
       uintptr_t(terraform_mode())!=pin.tool || !skg::readable(m,0xD4) || skg::field<int>(m,0xCC)!=1 || skg::field<int>(m,0xD0)!=0 ||
       !skg::readable(reinterpret_cast<void*>(a_history),0x21) || skg::field<void*>(m,0x14)!=pin.display ||
       !skg::readable(pin.display,0x2C) || (a_display_vtable&&skg::field<uintptr_t>(pin.display,0)!=a_display_vtable) ||
       skg::field<uint32_t>(pin.display,0x1C)!=2 || skg::field<void*>(pin.display,0x28)!=pin.terraform ||
       !a_terraform_ui_vtable || !skg::readable(pin.terraform,0x2C) || skg::field<uintptr_t>(pin.terraform,0)!=a_terraform_ui_vtable ||
       skg::field<uint32_t>(pin.terraform,0x28)!=0 || skg::field<void*>(pin.terraform,0x10)!=pin.palette ||
       !a_planet_palette_vtable || !skg::readable(pin.palette,0x28) || skg::field<uintptr_t>(pin.palette,0)!=a_planet_palette_vtable ||
       !a_planet_preset_card_vtable || !a_planet_preset_message_vtable || !skg::readable(pin.card,0x34) ||
       skg::field<uintptr_t>(pin.card,0)!=a_planet_preset_card_vtable ||
       skg::field<uintptr_t>(pin.card,0x20)!=a_planet_preset_message_vtable ||
       skg::field<void*>(pin.card,0x24)!=pin.layout || skg::field<uint32_t>(pin.card,0x28)!=pin.instance ||
       skg::field<uint32_t>(pin.card,0x30)!=pin.group || !skg::readable(pin.layout,0x18) ||
       skg::field<void*>(pin.layout,0x14)!=pin.backing || !skg::readable(pin.backing,0x8C) ||
       skg::field<void*>(pin.backing,0x88)!=pin.root || !skg::readable(pin.root,0x40) ||
       skg::field<uintptr_t>(pin.root,0)!=pin.root_vtable || !native_slot(pin.root,0x28/4) ||
       (pin.main && (!skg::readable(pin.main,0x40) || skg::field<uintptr_t>(pin.main,0)!=pin.main_vtable)))return false;
    void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    return skg::readable(context,4) && skg::field<uint32_t>(context,0)==pin.act;
}
bool planet_preset_values_current(const HeldPlanetPreset& pin,bool retained){
    const auto present=[&]{return !retained || (held_planet_preset && held_planet_preset->serial==pin.serial);};
    if(!present() || !planet_preset_raw_current(pin) || pin.scope.project!=act_identity.project || pin.scope.epoch!=act_identity.epoch)return false;
    for(const auto& [field,guard]:pin.guards)
        if(editor::field_revision(act_identity,editor_native_terrain::kPlanetObject,{editor::Field::Property,field})!=guard.first ||
           planet_value(field)!=guard.second)return false;
    return true;
}
bool planet_preset_current(const HeldPlanetPreset& pin,bool retained){
    const auto present=[&]{return !retained || (held_planet_preset && held_planet_preset->serial==pin.serial);};
    if(!planet_preset_values_current(pin,retained) || !pin.active || !pin.scope_current || !pin.gate ||
       !H || !H->time_seconds || H->time_seconds()-pin.started>2 || (GetKeyState(VK_ESCAPE)&0x8000) ||
       (pin.focused&&!real_focus()) || !pin.active() || !present() || !planet_preset_raw_current(pin) ||
       pin.scope_current()!=pin.scope || !present() || !planet_preset_raw_current(pin))return false;
    return planet_preset_values_current(pin,retained);
}
bool planet_preset_ui_current(const HeldPlanetPreset& pin,bool retained){
    if(!planet_preset_current(pin,retained) || sui::layout_container(pin.layout)!=pin.root ||
       !planet_preset_current(pin,retained) || sui::main_window()!=pin.main || !planet_preset_current(pin,retained) ||
       !sui::visible(pin.root) || !planet_preset_current(pin,retained))return false;
    std::set<void*> seen;void* node=pin.root;
    for(unsigned depth=0;node && depth<64;++depth){
        if(!skg::readable(node,0x40) || !native_slot(node,0x10/4))return false;
        if(!seen.insert(node).second)return false;
        if(node==pin.main)return sui::main_window()==pin.main && planet_preset_values_current(pin,retained);
        node=sui::parent(node);if(!planet_preset_values_current(pin,retained))return false;
    }
    return false;
}
bool run_planet_preset(const HeldPlanetPreset& pin,bool& attempted){
    attempted=false;
    if(!planet_preset_ui_current(pin,true) || !pin.gate({},planet_preset_fields()) ||
       !planet_preset_ui_current(pin,true))return false;
    if(applying || terrain_before || terrain_pending || gesture_before_resource || !pending.empty() || staged_pose ||
       !*reinterpret_cast<uint8_t*>(a_history+0x20))return false;
    if(!terrain_begin(reinterpret_cast<void*>(pin.tool)))return false;
    const auto rotation=terrain_rotation_serial;
    struct BeforeCleanup {
        const HeldPlanetPreset& pin;uint64_t rotation;bool attempted=false;
        ~BeforeCleanup(){
            if(attempted)return;
            if(terrain_before && terrain_generation==pin.generation && uintptr_t(terrain_owner)==pin.owner.data &&
               terrain_rotation_serial==rotation && terrain_scope==pin.scope){
                terrain_before.reset();clear_terrain_capture_rotations();
            }
        }
    } cleanup{pin,rotation};
    if(!planet_preset_ui_current(pin,true)){
        // Only the before-image opened here may be retired. No native history
        // or authored setter has run, and a reentrant newer capture is retained.
        if(terrain_before && terrain_generation==pin.generation && uintptr_t(terrain_owner)==pin.owner.data &&
           terrain_rotation_serial==rotation && terrain_scope==pin.scope){
            terrain_before.reset();clear_terrain_capture_rotations();
        }
        return false;
    }
    attempted=cleanup.attempted=true;
    const bool result=original_planet_preset_message(static_cast<uint8_t*>(pin.card)+0x20,0x0743B6FA,pin.card);
    // Native property lookup can acknowledge selection without starting any
    // history (F0CE13 -> F0D262). Do not strand our unused before-image.
    if(!terrain_pending && *reinterpret_cast<uint8_t*>(a_history+0x20) && planet_preset_values_current(pin,false) &&
       terrain_before && terrain_generation==pin.generation && uintptr_t(terrain_owner)==pin.owner.data &&
       terrain_rotation_serial==rotation && terrain_scope==pin.scope){
        terrain_before.reset();clear_terrain_capture_rotations();
    }
    return result;
}
bool SK_THISCALL planet_preset_message(void* self,uint32_t message,void* source){
    if(applying || message!=0x0743B6FA || uintptr_t(self)<0x20 || source!=static_cast<uint8_t*>(self)-0x20 || !C.active)
        return original_planet_preset_message(self,message,source);
    if(planet_preset_busy){
        // A nested selection retires the old value intent, including when it
        // happens before that intent has been published by its first getter.
        held_planet_preset.reset();if(planet_preset_serial!=UINT64_MAX)++planet_preset_serial;return false;
    }
    struct Busy {Busy(){planet_preset_busy=true;}~Busy(){planet_preset_busy=false;}} busy;
    const auto entry_serial=planet_preset_serial;
    try{
        HeldPlanetPreset pin;pin.generation=generation;pin.active=C.active;pin.scope_current=C.gesture_scope;pin.gate=C.terrain_gate;
        auto* m=mode();auto* d=data(m);auto* r=resource(d);
        if(!supported || !editing() || !skg::readable(r,kResourceSize))return original_planet_preset_message(self,message,source);
        pin.owner={uintptr_t(m),uintptr_t(d),uintptr_t(r),skg::field<editor::ResourceKey>(r,8)};pin.tool=uintptr_t(terraform_mode());
        // Close the independently pinned scene before acting on a callback's
        // shared-active result, including false/reentrant retirement.
        if(!pin.scope_current || !pin.gate || !H || !H->time_seconds || planet_preset_serial==UINT64_MAX)return false;
        pin.display=skg::field<void*>(m,0x14);if(!skg::readable(pin.display,0x2C))return false;
        pin.terraform=skg::field<void*>(pin.display,0x28);if(!skg::readable(pin.terraform,0x2C))return false;
        pin.palette=skg::field<void*>(pin.terraform,0x10);pin.card=source;
        if(!skg::readable(pin.card,0x34))return false;
        pin.layout=skg::field<void*>(pin.card,0x24);pin.instance=skg::field<uint32_t>(pin.card,0x28);pin.group=skg::field<uint32_t>(pin.card,0x30);
        if(!skg::readable(pin.layout,0x18))return false;
        pin.backing=skg::field<void*>(pin.layout,0x14);if(!skg::readable(pin.backing,0x8C))return false;
        pin.root=skg::field<void*>(pin.backing,0x88);
        if(!skg::readable(pin.root,0x40))return false;pin.root_vtable=skg::field<uintptr_t>(pin.root,0);
        void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;if(!skg::readable(context,4))return false;
        pin.act=skg::field<uint32_t>(context,0);
        const auto project=act_identity.project;const auto epoch=act_identity.epoch;
        const bool planet_present=act_identity.settings.contains(editor_native_terrain::kPlanetObject);
        size_t charge=17*sizeof(editor::Value);constexpr size_t budget=64*1024*1024;
        for(auto field:planet_preset_fields()){
            const auto planet=act_identity.settings.find(editor_native_terrain::kPlanetObject);
            if(planet==act_identity.settings.end())break;
            auto value=planet->second.properties.find(field);
            if(value!=planet->second.properties.end()){
                if(auto text=std::get_if<std::string>(&value->second)){
                    if(text->size()>budget-charge)return false;charge+=text->size();
                }
                if(auto data=std::get_if<editor::StructuredValue>(&value->second)){
                    if(data->bytes.size()>budget-charge)return false;charge+=data->bytes.size();
                    if(data->references.size()>(budget-charge)/sizeof(editor::ObjectId))return false;
                    charge+=data->references.size()*sizeof(editor::ObjectId);
                }
            }
            pin.guards.emplace(field,std::pair{editor::field_revision(act_identity,editor_native_terrain::kPlanetObject,{editor::Field::Property,field}),planet_value(field)});
        }
        // Touching an existing field from the FIRST policy callback must not
        // become a newly adopted before-image for this selected preset.
        pin.started=H->time_seconds();pin.focused=real_focus();if(!planet_preset_raw_current(pin))return false;
        const bool shared=pin.active();if(!planet_preset_raw_current(pin))return false;
        if(!shared)return original_planet_preset_message(self,message,source);
        if(!planet_present)return false;
        pin.scope=pin.scope_current();
        if(!valid_scope(pin.scope) || pin.scope.project!=project || pin.scope.epoch!=epoch || !planet_preset_raw_current(pin))return false;
        if(!planet_preset_current(pin,false))return false;
        pin.main=sui::main_window();if(!skg::readable(pin.main,0x40))return false;pin.main_vtable=skg::field<uintptr_t>(pin.main,0);
        if(!planet_preset_ui_current(pin,false))return false;
        if(planet_preset_serial!=entry_serial)return false;
        pin.serial=++planet_preset_serial;held_planet_preset=std::move(pin);
        const auto owned=*held_planet_preset;
        bool attempted=false;const bool accepted=run_planet_preset(owned,attempted);
        if((attempted || !planet_preset_current(owned,true)) && held_planet_preset && held_planet_preset->serial==owned.serial)held_planet_preset.reset();
        return accepted;
    }catch(...){held_planet_preset.reset();return false;}
}
void continue_planet_preset(){
    if(!held_planet_preset || planet_preset_busy)return;
    struct Busy {Busy(){planet_preset_busy=true;}~Busy(){planet_preset_busy=false;}} busy;
    const auto serial=held_planet_preset->serial;
    const auto drop=[&]{if(held_planet_preset && held_planet_preset->serial==serial)held_planet_preset.reset();};
    try{
        const auto pin=*held_planet_preset;
        const auto observed=state();
        if(!planet_preset_ui_current(pin,true)){drop();return;}
        if(!observed.ready || observed.gesture || queued || applying || terrain_pending ||
           (C.transaction_pending && C.transaction_pending()))return;
        bool attempted=false;run_planet_preset(pin,attempted);if(attempted || !planet_preset_current(pin,true))drop();
    }catch(...){drop();}
}
