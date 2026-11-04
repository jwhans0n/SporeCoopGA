// Verified F0CD40 call ordering on controlled Native doubles plus real Core.
// This is not a running-game preset or a native serializer format oracle.
namespace planet_preset_fixture {
namespace terrain=editor_native_terrain;
std::array<uint8_t,0x2C> ui;
std::array<uint8_t,0x28> palette;
std::array<uint8_t,0x38> card;
std::array<uint8_t,0x18> layout;
std::array<uint8_t,0x8C> backing;
std::array<uint8_t,0x90> root,main_window;
std::array<void*,11> window_vtable;
std::map<uint32_t,editor::Value> native_properties,before,after;
editor::Coordinator* core=nullptr;
std::map<editor::Scope,editor::LeaseToken> tokens;
std::function<void()> on_gate;
editor::Decision decision;
unsigned callbacks=0,writes=0,sequence=0,delivered=0;bool forward_only=false;
const editor::Peer author{0,1},foreign{1,1};
// Independent contract from the verified F0CD40 setter/export call sites.
constexpr std::array<uint32_t,17> expected_fields{0x03A23F98,0x03A23F99,0x07B18058,
    0x0536250C,0x0536250D,0x968C496A,0x968C496B,0xB6929C94,0xB6929C95,0xB6929C92,0xB6929C93,
    0x56B14F05,0x03ABB0AF,0x04E13CB7,0x073154E0,0x073137BB,0x072BEC91};
std::map<uint32_t,editor::Value> properties(bool changed){
    auto result=ground_effect_properties(changed?0x74C0E257:0xA9D6A81D);
    terrain::Property color{0x31,0,{}};for(auto value:{changed?0x3F000000u:0u,0u,0u,0u})terrain::u32(color.value,value);
    result[0x968C496A]=terrain::property_value(std::move(color));
    const uint32_t count=changed?2:1;
    for(auto [id,type,width]:{std::tuple{0x04E13CB7u,uint16_t(0x20),12u},std::tuple{0x073154E0u,uint16_t(9),4u},std::tuple{0x072BEC91u,uint16_t(0xD),4u}}){
        terrain::Property property{type,0x9C,{}};terrain::u32(property.value,count);terrain::u32(property.value,width);
        for(uint32_t row=0;row<count;++row)for(uint32_t n=0;n<width/4;++n){
            const uint32_t word=id==0x04E13CB7?(n==0?row+101:n==1?0x40626000:0x438F6347):id==0x072BEC91?0x3F800000:0;
            for(unsigned shift=0;shift<32;shift+=8)property.value.push_back(uint8_t(word>>shift));
        }
        result[id]=terrain::property_value(std::move(property));
    }
    terrain::Property density{0xD,0,{}};terrain::u32(density.value,0x3F000000);
    result[0x073137BB]=terrain::property_value(std::move(density));return result;
}
void SK_THISCALL export_properties(void* resource){
    tmc_export(resource);std::string error;
    for(const auto& [id,value]:native_properties){terrain::Property property;CHECK(terrain::native_property(id,value,property,error));tmc_properties[resource].fields[id]=std::move(property);}
}
uintptr_t symbol(const char* name){CHECK(std::string(name)=="SporeCoop::Editor::CaptureTerrain");return uintptr_t(export_properties);}
void SK_THISCALL color_set(void*,uint32_t raw){
    CHECK(skg::readable(reinterpret_cast<void*>(uintptr_t(raw)),12));
    CHECK(*reinterpret_cast<const uint32_t*>(uintptr_t(raw))==0x3F000000);++writes;native_properties[0x968C496A]=after.at(0x968C496A);
}
void SK_THISCALL ground_set(void*,uint32_t){++writes;for(auto id:{0x56B14F05u,0x03ABB0AFu})native_properties[id]=after.at(id);}
bool SK_THISCALL native_message(void* self,uint32_t message,void* source){
    ++callbacks;if(forward_only)return true;
    CHECK(self==card.data()+0x20&&message==0x0743B6FA&&source==card.data());
    std::array<float,3> selected_color{.5f,0,0};
    terrain_start(t_terraform.data());terrain_value<0>(t_terraform.data(),uint32_t(uintptr_t(selected_color.data())));
    // F0D220 -> F37340 is a DIRECT call, not FloraUI::Add. Its rebuild
    // changes the three parallel arrays and retains the density value.
    ++writes;for(auto id:{0x04E13CB7u,0x073154E0u,0x073137BBu,0x072BEC91u})native_properties[id]=after.at(id);
    terrain_value<13>(t_terraform.data(),0x74C0E257);terrain_commit(t_terraform.data());
    return true;
}
void submit(const Gesture& gesture){
    ++delivered;std::string error;
    CHECK(gesture.before.document.settings.at(terrain::kPlanetObject).properties==before);
    CHECK(gesture.after.document.settings.at(terrain::kPlanetObject).properties==after);
    const bool supported=supported_changes(gesture,error);if(!supported)std::printf("preset opaque proof=%s\n",error.c_str());CHECK(supported);
    editor::Request request;request.project=core->state().document.project;request.epoch=t_scope.epoch;
    request.operation={author,++sequence,t_scope.epoch};request.base_revision=core->state().document.revision;request.summary="Native planet look preset";
    for(const auto& [scope,token]:tokens){(void)scope;request.leases.push_back(token);}
    for(const auto& [field,value]:native_properties)if(value!=before.at(field)){
        editor::Mutation mutation;mutation.target=terrain::kPlanetObject;mutation.field={editor::Field::Property,field};mutation.value=value;
        mutation.expected=editor::field_revision(core->state().document,mutation.target,mutation.field);request.mutations.push_back(std::move(mutation));
    }
    decision=core->submit(request,100);
}
struct Routes {
    uintptr_t writer=a_write,reader=a_read,acts=a_copy_acts,crew=a_copy_crew,shown=a_layout_shown,
        init=a_scenario_initialization,modes=a_get_simulator_mode_manager,loaded=a_terrain_loaded,terrain_vt=a_terrain_vtable,
        marker_rows=a_copy_marker_rows,insert=a_insert_marker;
    uintptr_t card_vt=a_planet_preset_card_vtable,message_vt=a_planet_preset_message_vtable,palette_vt=a_planet_palette_vtable;
    PlanetPresetMessageFn original=original_planet_preset_message;const SkHost* host=H;
    uintptr_t terraform_vt=a_terraform_ui_vtable;
    decltype(original_terrain_values) values=original_terrain_values;HistoryFn start=original_terrain_start,commit=original_terrain_commit,rebuild=original_terrain_rebuild;
    HistoryFn materialize=original_terrain_materialize;
    ~Routes(){on_gate={};tmc_properties.clear();act_cache_rows.clear();reset();a_write=writer;a_read=reader;a_copy_acts=acts;a_copy_crew=crew;
        a_layout_shown=shown;a_scenario_initialization=init;a_get_simulator_mode_manager=modes;a_terrain_loaded=loaded;a_terrain_vtable=terrain_vt;
        a_copy_marker_rows=marker_rows;a_insert_marker=insert;H=host;
        a_planet_preset_card_vtable=card_vt;a_planet_preset_message_vtable=message_vt;a_planet_palette_vtable=palette_vt;original_planet_preset_message=original;
        a_terraform_ui_vtable=terraform_vt;original_terrain_values=values;original_terrain_start=start;original_terrain_commit=commit;original_terrain_rebuild=rebuild;original_terrain_materialize=materialize;}
};
void setup(editor::Coordinator& coordinator){
    held_planet_preset.reset();planet_preset_busy=false;t_flora_on_container={};v_callback={};
    tmc_setup();state();t_scope.author=author;before=properties(false);after=properties(true);native_properties=before;
    callbacks=writes=sequence=delivered=0;decision={};tokens.clear();on_gate={};forward_only=false;core=&coordinator;
    static SkHost host;host=*H;host.sym=symbol;H=&host;export_properties(t_live.data());
    Snapshot snapshot;std::string error;CHECK(capture(snapshot,error));editor::State seed;seed.document=snapshot.document;
    seed.document.project=t_scope.project;seed.document.epoch=t_scope.epoch;
    for(auto id:expected_fields)seed.document.settings_schema[id]={editor::ValueKind::Structured,0,0,0,true,false};
    CHECK(core->reset(seed,t_scope.epoch,error)&&core->connect(author)&&core->connect(foreign));act_identity=core->state().document;
    ui.fill(0);palette.fill(0);card.fill(0);layout.fill(0);backing.fill(0);root.fill(0);main_window.fill(0);
    v_terrain.fill(0);a_terrain_vtable=0xA105;skg::field<uintptr_t>(v_terrain.data(),0)=a_terrain_vtable;
    skg::field<void*>(t_terraform.data(),0x190)=v_terrain.data();a_terrain_loaded=uintptr_t(v_terrain_loaded);
    a_copy_marker_rows=uintptr_t(t_pose_copy_rows);a_insert_marker=uintptr_t(t_pose_insert);
    window_vtable.fill(reinterpret_cast<void*>(t_flora_layout_poison));
    skg::field<void*>(root.data(),0)=window_vtable.data();skg::field<void*>(main_window.data(),0)=window_vtable.data();
    a_terraform_ui_vtable=0xA101;a_planet_preset_card_vtable=0xA102;a_planet_preset_message_vtable=0xA103;a_planet_palette_vtable=0xA104;
    skg::field<void*>(t_display.data(),0x28)=ui.data();skg::field<uint32_t>(t_display.data(),0x1C)=2;
    skg::field<uintptr_t>(ui.data(),0)=a_terraform_ui_vtable;skg::field<void*>(ui.data(),0x10)=palette.data();
    skg::field<uintptr_t>(palette.data(),0)=a_planet_palette_vtable;
    skg::field<uintptr_t>(card.data(),0)=a_planet_preset_card_vtable;skg::field<uintptr_t>(card.data(),0x20)=a_planet_preset_message_vtable;
    skg::field<void*>(card.data(),0x24)=layout.data();skg::field<uint32_t>(card.data(),0x28)=0x12345678;skg::field<uint32_t>(card.data(),0x30)=0x406B6A00;
    skg::field<void*>(layout.data(),0x14)=backing.data();skg::field<void*>(backing.data(),0x88)=root.data();
    t_planet_main=main_window.data();t_planet_parent=[](void* window)->void*{CHECK(window==root.data());return main_window.data();};
    t_planet_visible=[](void* window){CHECK(window==root.data());return true;};
    original_planet_preset_message=native_message;original_terrain_values[0]=color_set;original_terrain_values[13]=ground_set;
    C.gesture=submit;
    C.terrain_gate=[](const auto& records,const auto& fields){
        if(fields.size()==17)CHECK(fields==std::vector<uint32_t>(expected_fields.begin(),expected_fields.end()));
        CHECK(records.empty());if(on_gate&&fields.size()==17){const auto callback=std::exchange(on_gate,{});callback();}
        if(!t_granted)return false;
        if(fields.empty())return true;
        std::vector<editor::Scope> scopes;for(auto field:fields)scopes.push_back({editor::ScopeKind::Field,terrain::kPlanetObject,{editor::Field::Property,field}});
        auto acquired=core->acquire(author,scopes,100,1000);if(!acquired.accepted())return false;
        for(const auto& lease:acquired.leases)tokens[lease.scope]={lease.scope,lease.token};return true;
    };
}
bool input(){return planet_preset_message(card.data()+0x20,0x0743B6FA,card.data());}
void test(){
    Routes routes;
    for(bool foreign_held:{false,true}){
        editor::Coordinator coordinator;setup(coordinator);const auto seed=core->state().document;
        const bool entered=input();
        if(entered||!held_planet_preset||callbacks||writes||terrain_starts){
            HeldPlanetPreset pin;pin.owner={uintptr_t(mode()),uintptr_t(data(mode())),uintptr_t(resource(data(mode()))),skg::field<editor::ResourceKey>(t_live.data(),8)};
            pin.scope=t_scope;pin.generation=generation;pin.tool=uintptr_t(t_terraform.data());pin.display=t_display.data();pin.terraform=ui.data();pin.palette=palette.data();
            pin.card=card.data();pin.layout=layout.data();pin.backing=backing.data();pin.root=root.data();pin.main=main_window.data();pin.instance=0x12345678;pin.group=0x406B6A00;
            pin.root_vtable=pin.main_vtable=uintptr_t(window_vtable.data());
            pin.act=skg::field<uint32_t>(t_context.data(),0);pin.started=t_time;pin.active=C.active;pin.scope_current=C.gesture_scope;pin.gate=C.terrain_gate;
            std::printf("preset initial entered=%u held=%u cb=%u writes=%u starts=%u raw=%u current=%u ui=%u busy=%u\n",entered,unsigned(bool(held_planet_preset)),callbacks,writes,terrain_starts,
                planet_preset_raw_current(pin),planet_preset_current(pin,false),planet_preset_ui_current(pin,false),planet_preset_busy);
        }
        CHECK(!entered&&held_planet_preset&&!callbacks&&!writes&&!terrain_starts&&native_properties==before);
        continue_planet_preset();CHECK(!callbacks&&!writes&&core->state().document.revision==0);
        std::vector<editor::LeaseToken> foreign_tokens;
        if(foreign_held){auto held=core->acquire(foreign,{{editor::ScopeKind::Field,terrain::kPlanetObject,{editor::Field::Property,0x073154E0}}},100,1000);
            CHECK(held.accepted());for(const auto& lease:held.leases)foreign_tokens.push_back({lease.scope,lease.token});}
        t_granted=true;continue_planet_preset();
        if(foreign_held){CHECK(held_planet_preset&&!callbacks&&!writes&&native_properties==before);CHECK(core->release(foreign,foreign_tokens));continue_planet_preset();}
        if(held_planet_preset||callbacks!=1||writes!=3||terrain_starts!=1||terrain_commits!=1||terrain_rebuilds!=1)
            std::printf("preset grant held=%u cb=%u writes=%u starts=%u commits=%u rebuilds=%u tokens=%zu gen=%llu failure=%s\n",
                unsigned(bool(held_planet_preset)),callbacks,writes,terrain_starts,terrain_commits,terrain_rebuilds,tokens.size(),
                static_cast<unsigned long long>(generation),t_failure.c_str());
        CHECK(!held_planet_preset&&callbacks==1&&writes==3&&terrain_starts==1&&terrain_commits==1&&terrain_rebuilds==1);
        CHECK(terrain_pending&&delivered==0&&core->state().document.revision==0);
        ++t_frame;drain_terrain(nullptr);
        if(delivered!=1||terrain_pending||t_failed)std::printf("preset drain delivered=%u pending=%u failures=%u settled=%u error=%s\n",delivered,terrain_pending,t_failed,terrain_settled(),t_failure.c_str());
        CHECK(delivered==1&&!terrain_pending&&!t_failed);
        if(!decision.accepted())std::printf("preset Core rejection code=%u reason=%s\n",unsigned(decision.code),decision.reason.c_str());
        CHECK(decision.accepted()&&core->state().history.size()==1&&core->state().document.revision==1&&tokens.size()==17);
        CHECK(core->state().document.settings.at(terrain::kPlanetObject).properties==after&&native_properties==after);
        Snapshot captured;std::string error;CHECK(capture_draft(captured,error));
        CHECK(captured.document.settings.at(terrain::kPlanetObject).properties==after&&captured.document.terrain==seed.terrain);
        editor::Request undo;undo.project=seed.project;undo.epoch=seed.epoch;undo.operation={author,++sequence,seed.epoch};undo.base_revision=1;undo.intent=editor::Intent::Undo;
        for(const auto& [scope,token]:tokens){(void)scope;undo.leases.push_back(token);}
        editor::Hooks hooks;hooks.apply=[](const auto&,const auto& target,const auto&,std::string&){native_properties=target.settings.at(terrain::kPlanetObject).properties;return true;};
        CHECK(core->submit(undo,101,hooks).accepted()&&native_properties==before&&core->state().document.terrain==seed.terrain);
    }
    for(auto field:expected_fields){
        editor::Coordinator coordinator;setup(coordinator);t_granted=true;
        CHECK(core->acquire(foreign,{{editor::ScopeKind::Field,terrain::kPlanetObject,{editor::Field::Property,field}}},100,1000).accepted());
        CHECK(!input()&&held_planet_preset&&!callbacks&&!writes&&!terrain_starts&&native_properties==before);
    }
    // The entire preset waits. Stale selected keys, UI, logical context,
    // revisions, denial and callback retirement all perform zero Native writes.
    for(unsigned reason=0;reason<22;++reason){
        editor::Coordinator coordinator;setup(coordinator);CHECK(!input()&&held_planet_preset);t_granted=true;
        auto expire=[&]{
            if(reason==0)++generation;if(reason==1)++t_scope.epoch;
            if(reason==2)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
            if(reason==3)skg::field<uint32_t>(ui.data(),0x28)=1;
            if(reason==4)skg::field<void*>(ui.data(),0x10)=nullptr;
            if(reason==5)skg::field<uintptr_t>(card.data(),0)=0;
            if(reason==6)++skg::field<uint32_t>(card.data(),0x28);
            if(reason==7)++skg::field<uint32_t>(card.data(),0x30);
            if(reason==8)skg::field<void*>(card.data(),0x24)=nullptr;
            if(reason==9)skg::field<void*>(layout.data(),0x14)=nullptr;
            if(reason==10)skg::field<void*>(backing.data(),0x88)=nullptr;
            if(reason==11)t_planet_main=nullptr;
            if(reason==12)t_planet_parent=[](void*)->void*{return nullptr;};
            if(reason==13)++skg::field<uint32_t>(t_context.data(),0);
            if(reason==14)skg::field<int>(t_mode.data(),0xCC)=0;
            if(reason==15)act_identity.settings.at(terrain::kPlanetObject).properties[0x04E13CB7]=after.at(0x04E13CB7);
            if(reason==16)++act_identity.settings.at(terrain::kPlanetObject).field_revisions[{editor::Field::Property,0x073154E0}];
            if(reason==17)deny_input({{editor::ScopeKind::Field,terrain::kPlanetObject,{editor::Field::Property,0x072BEC91}}});
            if(reason==18)native_fixture_escape_state=SHORT(0x8000);
            if(reason==19)t_time+=3;
            if(reason==20)held_planet_preset.reset();
            if(reason==21)throw 7;
        };
        if(reason<20)expire();else on_gate=expire;
        continue_planet_preset();CHECK(!held_planet_preset&&!callbacks&&!writes&&!terrain_starts&&!terrain_commits&&native_properties==before&&core->state().document.revision==0);
    }
    for(unsigned boundary=0;boundary<3;++boundary){
        editor::Coordinator coordinator;setup(coordinator);bool fired=false,armed=false;
        auto expire=[&]{fired=true;++skg::field<uint32_t>(card.data(),0x28);};
        if(boundary==0)C.active=[&]{if(armed&&!fired)expire();return true;};
        CHECK(!input());t_granted=true;armed=true;
        if(boundary==1)t_flora_on_container=expire;
        if(boundary==2)on_gate=expire;
        continue_planet_preset();CHECK(fired&&!held_planet_preset&&!callbacks&&!writes&&!terrain_starts&&native_properties==before);
    }
    for(unsigned boundary=0;boundary<2;++boundary){
        editor::Coordinator coordinator;setup(coordinator);t_granted=true;bool fired=false;
        auto mutate=[&]{if(!fired){fired=true;act_identity.settings.at(terrain::kPlanetObject).properties[0x04E13CB7]=after.at(0x04E13CB7);}};
        if(boundary==0)C.active=[&]{mutate();return true;};
        else C.gesture_scope=[&]{mutate();return t_scope;};
        CHECK(!input()&&fired&&!held_planet_preset&&!callbacks&&!writes&&!terrain_starts);
    }
    for(bool after_capture:{false,true}){
        editor::Coordinator coordinator;setup(coordinator);bool fired=false,armed=false;
        C.active=[&]{if(armed&&!fired&&(!after_capture||terrain_before)){fired=true;if(after_capture)throw 9;CHECK(!input());}return true;};
        CHECK(!input());t_granted=true;armed=true;
        continue_planet_preset();
        if(!fired||held_planet_preset||terrain_before||callbacks||writes||terrain_starts||planet_preset_busy)
            std::printf("preset reentry after_capture=%u fired=%u held=%u before=%u cb=%u writes=%u starts=%u busy=%u error=%s\n",
                after_capture,fired,unsigned(bool(held_planet_preset)),unsigned(bool(terrain_before)),callbacks,writes,terrain_starts,planet_preset_busy,t_failure.c_str());
        CHECK(fired&&!held_planet_preset&&!terrain_before&&!callbacks&&!writes&&!terrain_starts&&!planet_preset_busy);
    }
    {
        editor::Coordinator coordinator;setup(coordinator);bool fired=false;
        C.active=[&]{if(!fired&&terrain_before){fired=true;++skg::field<uint32_t>(card.data(),0x28);}return true;};
        CHECK(!input());t_granted=true;
        continue_planet_preset();CHECK(fired&&!held_planet_preset&&!terrain_before&&!callbacks&&!writes&&!terrain_starts&&t_allocations.empty());
    }
    {
        editor::Coordinator coordinator;setup(coordinator);CHECK(!input());t_granted=true;t_alloc_fail=true;
        continue_planet_preset();CHECK(held_planet_preset&&!terrain_before&&!callbacks&&!writes&&!terrain_starts&&t_allocations.empty());
        t_alloc_fail=false;continue_planet_preset();++t_frame;drain_terrain(nullptr);CHECK(callbacks==1&&delivered==1&&decision.accepted()&&!held_planet_preset);
    }
    editor::Coordinator coordinator;setup(coordinator);forward_only=true;
    CHECK(planet_preset_message(card.data()+0x20,0,card.data())&&callbacks==1&&!held_planet_preset);
    CHECK(planet_preset_message(card.data()+0x20,0x0743B6FA,nullptr)&&callbacks==2&&!held_planet_preset);
    C.active=[] {return false;};CHECK(input()&&callbacks==3&&!held_planet_preset&&!writes);
    editor::Coordinator noop;setup(noop);t_granted=true;forward_only=true;CHECK(input());
    CHECK(callbacks==1&&!held_planet_preset&&!terrain_before&&!terrain_pending&&!writes&&!terrain_starts&&noop.state().document.revision==0);
}
}
