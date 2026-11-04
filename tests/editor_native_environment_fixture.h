// Deferred outside-frame invocation of the separately byte-verified native
// Environment refresh. This fixture checks scheduling/identity, not real SUI.
namespace environment_refresh_fixture {
std::array<uint8_t,0x40> terraform,environment;
std::array<uint8_t,0x80> root,other_root;
unsigned calls=0;bool outside=false;
std::array<int32_t,3> thumbs{};
std::function<void()> callback;
void SK_THISCALL refresh(void* self){
    CHECK(outside&&self==environment.data());++calls;
    // The native ABI receipt separately proves all three calls use false.
    thumbs={int32_t(std::lround(skg::field<float>(t_terraform.data(),0xC8)*100)),
        int32_t(std::lround(skg::field<float>(t_terraform.data(),0xCC)*100)),
        int32_t(std::lround(skg::field<float>(t_terraform.data(),0xD0)*100))};
    const auto notify=callback;if(notify)notify();
}
struct Routes {
    terrain_rotation_capture_fixture::Routes rotation;
    uintptr_t ui=a_terraform_ui_vtable,environment_vt=a_environment_ui_vtable,function=a_environment_ui_refresh;
    bool fence=fence_queued;
    ~Routes(){callback={};staged_pose.reset();clear_environment_refresh();
        environment_refresh_queued=environment_refresh_servicing=false;
        a_terraform_ui_vtable=ui;a_environment_ui_vtable=environment_vt;a_environment_ui_refresh=function;fence_queued=fence;}
};
void setup(){
    callback={};outside=false;calls=0;thumbs={99,99,99};
    terrain_rotation_capture_fixture::setup(false,false);
    terraform.fill(0);environment.fill(0);root.fill(0);other_root.fill(0);
    a_environment_ui_refresh=uintptr_t(refresh);a_environment_ui_vtable=0x12345679;a_terraform_ui_vtable=0x1234567A;
    skg::field<uint32_t>(t_display.data(),0x1C)=2;skg::field<void*>(t_display.data(),0x28)=terraform.data();
    skg::field<uintptr_t>(terraform.data(),0)=a_terraform_ui_vtable;skg::field<uint32_t>(terraform.data(),0x28)=3;
    skg::field<void*>(terraform.data(),0x14)=environment.data();skg::field<uintptr_t>(environment.data(),0)=a_environment_ui_vtable;
    skg::field<void*>(environment.data(),0x0C)=root.data();
    skg::field<float>(t_terraform.data(),0xC8)=.2f;skg::field<float>(t_terraform.data(),0xCC)=.3f;skg::field<float>(t_terraform.data(),0xD0)=.4f;
    fence_queued=true;clear_environment_refresh();environment_refresh_queued=environment_refresh_servicing=false;
}
void run(){const auto prior=outside;outside=true;drain_environment_refresh(nullptr);outside=prior;}
void test(){
    Routes routes;setup();const auto rows=t_rows.at(t_live.data());const auto native=t_live;const auto data_before=t_data;
    const auto updates=environment_ui_updates;
    request_environment_refresh();CHECK(environment_refresh);run();CHECK(!calls&&environment_refresh);
    ++t_frame;run();CHECK(calls==1&&!environment_refresh&&environment_ui_updates==updates+1);
    CHECK((thumbs==std::array<int32_t,3>{20,30,40}));
    CHECK(t_rows.at(t_live.data())==rows&&t_live==native&&t_data==data_before&&!t_restores&&!applying);
    CHECK(!environment_refresh_queued&&!environment_refresh_servicing);
    // Scheduling remains outside-frame and does not repeatedly enqueue during
    // a reentrant native refresh. Completion clears its queue flag on all exits.
    setup();request_environment_refresh();++t_frame;
    callback=[] {editor_native::tick();drain_environment_refresh(nullptr);};
    editor_native::tick();CHECK(t_tasks.size()==1);outside=true;drain();outside=false;
    CHECK(calls==1&&t_tasks.empty()&&!environment_refresh&&!environment_refresh_queued&&!environment_refresh_servicing);
    for(unsigned reason=0;reason<24;++reason){
        setup();request_environment_refresh();CHECK(environment_refresh);++t_frame;bool allowed=true;
        if(reason<12)restore_fence_fixture::expire(reason,allowed);
        if(reason==10)C.active=[] {return false;};
        if(reason==12)++gesture_counter;
        if(reason==13)++terrain_rotation_serial;
        if(reason==14)terrain_pending=true;
        if(reason==15)held_terrain_controls.emplace_back();
        if(reason==16)staged_pose=std::make_shared<StagedPose>();
        if(reason==17)skg::field<uint8_t>(t_history.data(),0x20)=0;
        if(reason==18)skg::field<uintptr_t>(terraform.data(),0)=0;
        if(reason==19)skg::field<uintptr_t>(environment.data(),0)=0;
        if(reason==20)skg::field<uint32_t>(terraform.data(),0x28)=2;
        if(reason==21)skg::field<void*>(environment.data(),0x0C)=nullptr;
        if(reason==22)t_time+=4;
        if(reason==23)C.active=[]()->bool {throw 7;};
        run();CHECK(!calls&&!environment_refresh&&!environment_refresh_queued&&!environment_refresh_servicing);
        CHECK((thumbs==std::array<int32_t,3>{99,99,99}));
        staged_pose.reset();
    }
    // A Native readiness getter can expire the owner before the first UI call.
    for(unsigned reason=0;reason<12;++reason){
        setup();request_environment_refresh();++t_frame;bool fired=false,allowed=true;
        restore_fence_fixture::callback=[&](unsigned stage){if(stage==11&&!fired){fired=true;
            restore_fence_fixture::expire(reason,allowed);if(reason==10)C.active=[] {return false;};}};
        run();CHECK(fired&&!calls&&!environment_refresh);
    }
    // Capture the current UI owner, then replace its root or retire/replace the
    // queued serial from the LAST policy callback before the native call.
    for(unsigned reason=0;reason<3;++reason){
        setup();request_environment_refresh();++t_frame;unsigned checks=0;bool fired=false;uint64_t replacement=0;
        C.projection_scope=[&]{if(++checks==3){fired=true;
            if(reason==0)skg::field<void*>(environment.data(),0x0C)=other_root.data();
            if(reason==1)clear_environment_refresh();
            if(reason==2){auto newer=*environment_refresh;replacement=++newer.serial;environment_refresh=newer;}
        }return t_scope;};
        run();CHECK(fired&&!calls);
        if(reason==2)CHECK(environment_refresh&&environment_refresh->serial==replacement);
        else CHECK(!environment_refresh);
    }
    // Read identity remains valid while authorizing input is empty. No gesture
    // permission is consulted or granted by this passive UI refresh.
    setup();C.gesture_scope=[] {return GestureScope{};};request_environment_refresh();++t_frame;run();CHECK(calls==1);
    // Wait for genuine native terrain/view settlement; it is neither a fresh
    // input gesture nor permission to replay a stale UI owner.
    setup();request_environment_refresh();terrain_rebuilding=true;terrain_rebuild_frame=t_frame;
    ++t_frame;skg::field<void*>(v_terrain.data(),0xFC)=other_root.data();
    run();CHECK(!calls&&environment_refresh);skg::field<void*>(v_terrain.data(),0xFC)=nullptr;run();CHECK(calls==1&&!environment_refresh);
    // Original function exceptions or owner replacement cause no second UI
    // call and cannot leave a permanently held queue/reentry flag.
    for(bool throwing:{false,true}){
        setup();request_environment_refresh();++t_frame;
        callback=[&]{if(throwing)throw 9;++skg::field<editor::ResourceKey>(t_live.data(),8).instance;};
        run();CHECK(calls==1&&!environment_refresh&&!environment_refresh_queued&&!environment_refresh_servicing);
    }
    // A reentrant newer canonical refresh survives retirement of the old one,
    // then reads latest native tool values on a genuinely later frame.
    setup();request_environment_refresh();++t_frame;
    callback=[] {callback={};skg::field<float>(t_terraform.data(),0xCC)=.7f;request_environment_refresh();};
    run();CHECK(calls==1&&environment_refresh);++t_frame;run();CHECK(calls==2&&thumbs[1]==70&&!environment_refresh);
    setup();C.projection_scope=[] {return GestureScope{};};request_environment_refresh();CHECK(!environment_refresh&&!calls);
}
}
static void test_native_environment_refresh(){environment_refresh_fixture::test();}

// The same passive queue must repair Flora after checked rollback, Undo and
// remote canonical restoration. Distinct UILayout/backing/IWindow objects keep
// this separate from the native function's byte-verified UI-only behavior.
namespace flora_refresh_fixture {
unsigned calls=0;bool checked=false;uint32_t ground=0;
std::array<editor::ResourceKey,9> authored_cards{},shown_cards{};
std::function<void()> callback;
void SK_THISCALL refresh(void* self){
    CHECK(environment_refresh_fixture::outside&&self==t_flora_ui.data());++calls;
    const auto effect=skg::field<uint32_t>(t_terraform.data(),0x160);
    checked=effect==0x74C0E257;ground=effect==0x6A36F1CA?1:7;shown_cards=authored_cards;
    const auto notify=callback;if(notify)notify();
}
struct Routes {
    environment_refresh_fixture::Routes environment;
    uintptr_t function=a_flora_ui_refresh,vtable=a_flora_ui_vtable;
    ~Routes(){a_flora_ui_refresh=function;a_flora_ui_vtable=vtable;t_flora_on_container={};callback={};}
};
void setup(){
    environment_refresh_fixture::setup();calls=0;checked=true;ground=99;callback={};
    t_flora_ui.fill(0);t_flora_layout.fill(0);t_flora_container.fill(0);t_flora_window.fill(0);
    t_flora_on_container={};t_flora_layout_gets=0;t_flora_wrong_interface=0;
    a_flora_ui_refresh=uintptr_t(refresh);a_flora_ui_vtable=0x1234567B;
    skg::field<uint32_t>(environment_refresh_fixture::terraform.data(),0x28)=2;
    skg::field<void*>(environment_refresh_fixture::terraform.data(),0x18)=t_flora_ui.data();
    skg::field<uintptr_t>(t_flora_ui.data(),0)=a_flora_ui_vtable;
    skg::field<void*>(t_flora_ui.data(),0x168)=t_flora_layout.data();
    skg::field<void*>(t_flora_layout.data(),0x14)=t_flora_container.data();
    skg::field<void*>(t_flora_container.data(),0x88)=t_flora_window.data();
    skg::field<uint32_t>(t_terraform.data(),0x160)=0xA9D6A81D;
    authored_cards={};authored_cards[0]={11,13,17};authored_cards[1]={19,23,29};shown_cards={};
}
void run(){environment_refresh_fixture::run();}
void test(){
    Routes routes;setup();const auto data_before=t_data;const auto resource_before=t_live;
    const auto source=authored_cards;const auto rows=t_rows.at(t_live.data());
    const auto updates=environment_ui_updates;
    request_environment_refresh();CHECK(environment_refresh);run();CHECK(!calls&&checked&&environment_refresh);
    ++t_frame;run();CHECK(calls==1&&!checked&&ground==7&&shown_cards==source&&!environment_refresh);
    CHECK(t_flora_layout_gets>=2&&!t_flora_wrong_interface&&environment_ui_updates==updates+1);
    CHECK(t_data==data_before&&t_live==resource_before&&t_rows.at(t_live.data())==rows&&authored_cards==source);
    // A later restored canonical state updates the checkbox, selected option
    // and all card images without reopening the tab or applying another edit.
    skg::field<uint32_t>(t_terraform.data(),0x160)=0x74C0E257;authored_cards[0]={31,37,41};
    request_environment_refresh();++t_frame;run();CHECK(calls==2&&checked&&shown_cards==authored_cards);
    skg::field<uint32_t>(t_terraform.data(),0x160)=0xA9D6A81D;authored_cards=source;
    request_environment_refresh();++t_frame;run();CHECK(calls==3&&!checked&&shown_cards==source);
    for(unsigned reason=0;reason<12;++reason){
        setup();request_environment_refresh();CHECK(environment_refresh);++t_frame;bool allowed=true;
        restore_fence_fixture::expire(reason,allowed);if(reason==10)C.active=[] {return false;};
        run();CHECK(!calls&&!environment_refresh&&checked&&ground==99);
    }
    for(unsigned reason=0;reason<9;++reason){
        setup();request_environment_refresh();++t_frame;
        if(reason==0)skg::field<uint32_t>(environment_refresh_fixture::terraform.data(),0x28)=1;
        if(reason==1)skg::field<void*>(environment_refresh_fixture::terraform.data(),0x18)=nullptr;
        if(reason==2)skg::field<uintptr_t>(t_flora_ui.data(),0)=0;
        if(reason==3)skg::field<void*>(t_flora_ui.data(),0x168)=nullptr;
        if(reason==4)skg::field<void*>(t_flora_layout.data(),0x14)=nullptr;
        if(reason==5)skg::field<void*>(t_flora_container.data(),0x88)=nullptr;
        if(reason==6)a_flora_ui_refresh=0;
        if(reason==7)++gesture_counter;
        if(reason==8)t_time+=4;
        run();CHECK(!calls&&!environment_refresh&&checked&&shown_cards!=authored_cards);
    }
    // The leaf root getter is an explicit boundary: owner, layout, backing,
    // source, generation, serial and category expiry must precede UI dispatch.
    for(unsigned reason=0;reason<8;++reason){
        setup();request_environment_refresh();++t_frame;bool fired=false;
        t_flora_on_container=[&]{fired=true;
            if(reason==0)++generation;
            if(reason==1)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
            if(reason==2)skg::field<void*>(t_flora_ui.data(),0x168)=nullptr;
            if(reason==3)skg::field<void*>(t_flora_layout.data(),0x14)=nullptr;
            if(reason==4)skg::field<void*>(t_flora_container.data(),0x88)=nullptr;
            if(reason==5)clear_environment_refresh();
            if(reason==6)skg::field<uint32_t>(environment_refresh_fixture::terraform.data(),0x28)=1;
            if(reason==7)throw 9;
        };
        run();CHECK(fired&&!calls&&!environment_refresh&&checked&&!t_flora_wrong_interface);
    }
    // The LAST scope callback can replace a widget after both getter calls.
    // A fresh callback-free owner comparison must still prevent the UI call.
    for(unsigned reason=0;reason<4;++reason){
        setup();request_environment_refresh();++t_frame;bool fired=false;
        C.projection_scope=[&]{if(!fired&&t_flora_layout_gets>=2){fired=true;
            if(reason==0)skg::field<void*>(t_flora_container.data(),0x88)=environment_refresh_fixture::other_root.data();
            if(reason==1)skg::field<void*>(t_flora_ui.data(),0x168)=nullptr;
            if(reason==2)skg::field<uint32_t>(environment_refresh_fixture::terraform.data(),0x28)=1;
            if(reason==3)clear_environment_refresh();
        }return t_scope;};
        run();CHECK(fired&&!calls&&!environment_refresh&&checked);
    }
    setup();request_environment_refresh();held_terrain_controls.emplace_back();++t_frame;run();CHECK(!calls&&!environment_refresh);
    setup();request_environment_refresh();++t_frame;callback=[] {editor_native::tick();drain_environment_refresh(nullptr);};
    run();CHECK(calls==1&&t_tasks.empty()&&!environment_refresh&&!environment_refresh_queued&&!environment_refresh_servicing);
    setup();C.gesture_scope=[] {return GestureScope{};};request_environment_refresh();++t_frame;run();CHECK(calls==1&&!checked);
    // Newer canonical work issued by a native UI callback remains queued for
    // another frame, rather than being consumed under the old refresh serial.
    setup();request_environment_refresh();++t_frame;
    callback=[] {callback={};skg::field<uint32_t>(t_terraform.data(),0x160)=0x74C0E257;request_environment_refresh();};
    run();CHECK(calls==1&&environment_refresh);++t_frame;run();CHECK(calls==2&&checked&&!environment_refresh);
}
}
static void test_native_flora_refresh(){flora_refresh_fixture::test();}

// Category1 has two distinct owners before its UILayout. In particular the
// TerraformUI+C wrapper is not the ColorUI passed to the native refresh.
namespace color_refresh_fixture {
std::array<uint8_t,0x14> wrapper;
std::array<uint8_t,0x3C> color;
std::array<uint8_t,0x5C> gradient;
std::array<uint8_t,0x40> parent,wrapper_parent;
std::array<uint8_t,0x40> gradient_parent,gradient_image;
std::array<float,8> authored{},shown{};
unsigned calls=0;
std::function<void()> callback;
void SK_THISCALL refresh(void* self){
    CHECK(environment_refresh_fixture::outside&&self==color.data());++calls;
    CHECK(skg::field<void*>(self,0x18)==t_flora_layout.data());
    CHECK(skg::field<void*>(self,0x1C)==gradient.data());
    shown=authored;const auto notify=callback;if(notify)notify();
}
struct Routes {
    environment_refresh_fixture::Routes environment;
    uintptr_t function=a_color_ui_refresh,vtable=a_color_ui_vtable,wrapper_vtable=a_color_ui_wrapper_vtable,
        gradient_vtable=a_color_gradient_vtable;
    ~Routes(){a_color_ui_refresh=function;a_color_ui_vtable=vtable;a_color_ui_wrapper_vtable=wrapper_vtable;
        a_color_gradient_vtable=gradient_vtable;t_flora_on_container={};callback={};}
};
void setup(){
    environment_refresh_fixture::setup();calls=0;callback={};
    wrapper.fill(0);color.fill(0);gradient.fill(0);parent.fill(0);wrapper_parent.fill(0);
    gradient_parent.fill(0);gradient_image.fill(0);
    t_flora_layout.fill(0);t_flora_container.fill(0);t_flora_window.fill(0);
    t_flora_on_container={};t_flora_layout_gets=0;
    a_color_ui_refresh=uintptr_t(refresh);a_color_ui_vtable=0x12345680;
    a_color_ui_wrapper_vtable=0x12345681;a_color_gradient_vtable=0x12345682;
    skg::field<uint32_t>(environment_refresh_fixture::terraform.data(),0x28)=1;
    skg::field<void*>(environment_refresh_fixture::terraform.data(),0x0C)=wrapper.data();
    skg::field<uintptr_t>(wrapper.data(),0)=a_color_ui_wrapper_vtable;
    skg::field<void*>(wrapper.data(),0x0C)=wrapper_parent.data();skg::field<void*>(wrapper.data(),0x10)=color.data();
    skg::field<uintptr_t>(color.data(),0)=a_color_ui_vtable;skg::field<void*>(color.data(),0x14)=parent.data();
    skg::field<void*>(color.data(),0x18)=t_flora_layout.data();skg::field<void*>(color.data(),0x1C)=gradient.data();
    skg::field<uintptr_t>(gradient.data(),0)=a_color_gradient_vtable;
    skg::field<void*>(gradient.data(),0x14)=gradient_parent.data();skg::field<void*>(gradient.data(),0x18)=gradient_image.data();
    skg::field<void*>(t_flora_layout.data(),0x14)=t_flora_container.data();
    skg::field<void*>(t_flora_container.data(),0x88)=t_flora_window.data();
    authored={0,.1f,.3f,.5f,.7f,.8f,.9f,1};shown=authored;shown[2]=.4f;
}
void expire_ui(unsigned reason){
    if(reason==0)skg::field<uint32_t>(environment_refresh_fixture::terraform.data(),0x28)=2;
    if(reason==1)skg::field<void*>(environment_refresh_fixture::terraform.data(),0x0C)=nullptr;
    if(reason==2)skg::field<uintptr_t>(wrapper.data(),0)=0;
    if(reason==3)skg::field<void*>(wrapper.data(),0x0C)=nullptr;
    if(reason==4)skg::field<void*>(wrapper.data(),0x10)=nullptr;
    if(reason==5)skg::field<uintptr_t>(color.data(),0)=0;
    if(reason==6)skg::field<void*>(color.data(),0x14)=nullptr;
    if(reason==7)skg::field<void*>(color.data(),0x18)=nullptr;
    if(reason==8)skg::field<void*>(color.data(),0x1C)=nullptr;
    if(reason==9)skg::field<uintptr_t>(gradient.data(),0)=0;
    if(reason==10)skg::field<void*>(t_flora_layout.data(),0x14)=nullptr;
    if(reason==11)skg::field<void*>(t_flora_container.data(),0x88)=nullptr;
    if(reason==12)a_color_ui_refresh=0;
    if(reason==13)skg::field<void*>(gradient.data(),0x14)=nullptr;
    if(reason==14)skg::field<void*>(gradient.data(),0x18)=nullptr;
}
void run(){environment_refresh_fixture::run();}
void test(){
    Routes routes;setup();const auto original=authored;const auto resource_before=t_live;const auto data_before=t_data;
    const auto rows=t_rows.at(t_live.data());const auto current_category=skg::field<uint32_t>(environment_refresh_fixture::terraform.data(),0x28);
    const auto updates=environment_ui_updates;
    request_environment_refresh();CHECK(environment_refresh);run();CHECK(!calls&&shown!=authored&&environment_refresh);
    ++t_frame;run();CHECK(calls==1&&shown==authored&&!environment_refresh&&environment_ui_updates==updates+1);
    CHECK(t_flora_layout_gets>=2&&authored==original&&t_live==resource_before&&t_data==data_before&&t_rows.at(t_live.data())==rows);
    CHECK(skg::field<uint32_t>(environment_refresh_fixture::terraform.data(),0x28)==current_category);
    // A second canonical value, then Undo back to the first, each refreshes the
    // existing UI without reopening the tab or changing any serialized bytes.
    authored[2]=.4f;request_environment_refresh();++t_frame;run();CHECK(calls==2&&shown==authored);
    authored=original;request_environment_refresh();++t_frame;run();CHECK(calls==3&&shown==original);
    for(unsigned reason=0;reason<15;++reason){
        setup();request_environment_refresh();++t_frame;expire_ui(reason);run();
        CHECK(!calls&&!environment_refresh&&shown!=authored);
    }
    for(unsigned reason=0;reason<12;++reason){
        setup();request_environment_refresh();++t_frame;bool allowed=true;
        restore_fence_fixture::expire(reason,allowed);if(reason==10)C.active=[] {return false;};
        run();CHECK(!calls&&!environment_refresh&&shown!=authored);
    }
    // The root getter can retire either owner or replace the layout. No call
    // to the native refresh may follow even when the old objects stay readable.
    for(unsigned reason=0;reason<20;++reason){
        setup();request_environment_refresh();++t_frame;bool fired=false;
        t_flora_on_container=[&]{fired=true;
            if(reason<15)expire_ui(reason);
            if(reason==15)++generation;
            if(reason==16)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
            if(reason==17)clear_environment_refresh();
            if(reason==18)held_terrain_controls.emplace_back();
            if(reason==19)throw 5;
        };
        run();CHECK(fired&&!calls&&!environment_refresh&&shown!=authored);
    }
    // Fresh raw closure follows the final scope callback, including every
    // category1 pointer rather than only the shared outer TerraformUI owner.
    for(unsigned reason=0;reason<16;++reason){
        setup();request_environment_refresh();++t_frame;bool fired=false;
        C.projection_scope=[&]{if(!fired&&t_flora_layout_gets>=2){fired=true;
            if(reason<15)expire_ui(reason);else clear_environment_refresh();
        }return t_scope;};
        run();CHECK(fired&&!calls&&!environment_refresh&&shown!=authored);
    }
    setup();request_environment_refresh();++t_frame;
    callback=[] {editor_native::tick();drain_environment_refresh(nullptr);};run();
    CHECK(calls==1&&t_tasks.empty()&&!environment_refresh&&!environment_refresh_servicing&&!environment_refresh_queued);
    setup();C.gesture_scope=[] {return GestureScope{};};request_environment_refresh();++t_frame;run();CHECK(calls==1&&shown==authored);
    setup();request_environment_refresh();++t_frame;callback=[] {throw 7;};run();
    CHECK(calls==1&&!environment_refresh&&!environment_refresh_servicing&&!environment_refresh_queued);
}
}
static void test_native_color_refresh(){color_refresh_fixture::test();}
