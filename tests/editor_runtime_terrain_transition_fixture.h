// Runtime lifecycle regressions. Native calls are controlled doubles here;
// native constructor/recipe/export ABI behavior has its own Win32 fixture.
static void test_transition_capture_rejects_before_native_save(){
    Party party(0,0xA5,true,true);
    const auto state=runtime_host::controller->state();const auto checkpoint=runtime_host::store.snapshot();
    const auto scene=native[0].snapshot;side=0;native[0].refuse_transition_capture=true;
    runtime_host::save();same_frame_drain(0);
    CHECK(!native[0].save_busy&&!native[0].save_calls&&!native[0].save_as_calls);
    CHECK(runtime_host::controller->state()==state&&runtime_host::store.snapshot()==checkpoint);
    CHECK(native[0].snapshot.bytes==scene.bytes&&native[0].snapshot.source==scene.source&&!runtime_host::save_terrain);
}
static void test_transition_host_save_and_next_foreign_edit(){
    Party party(0,0xA5,true,true);const auto document=runtime_host::controller->state().document;
    const auto history=runtime_host::controller->state().history;const auto actual=native[0].snapshot.source;
    side=0;runtime_host::save();CHECK(spin([]{return native[0].save_busy;}));
    CHECK(runtime_host::save_terrain&&runtime_host::save_terrain->key.owner.source==actual);
    const auto op=*native[0].save_operation;const auto resumed=native[0].transition_resumes;
    complete_native_save(0,{808,0x366A930D,0x408A0000});
    CHECK(spin([]{return runtime_host::controller->saved_coverage().applied==2;}));
    CHECK(native[0].transition_normalizes==1&&native[0].transition_resumes==resumed+1);
    CHECK(!runtime_host::save_terrain&&!runtime_host::save_publication_pending&&!runtime_host::frozen);
    CHECK(runtime_host::controller->state().document==document&&runtime_host::controller->state().history==history);
    CHECK(runtime_host::controller->view().saved->resource.instance==808&&op.before==actual);
    creation_field(1,placement,{editor::Field::Position,0},editor::Vec3{14,8,7},{editor::ScopeKind::Placement,placement,{}});
    CHECK(runtime_host::controller->state().document.revision==1&&runtime_host::controller->state().history.size()==1);
}
static void test_transition_saved_record_rejection_retains_checkpoint(){
    Party party(0,0xA5,true,true);const auto checkpoint=runtime_host::store.snapshot();
    const auto document=runtime_host::controller->state().document;const auto root=runtime_host::resources.root;
    side=0;runtime_host::save();CHECK(spin([]{return native[0].save_busy;}));
    native[0].refuse_transition_normalize=true;const auto restores=native[0].restores;
    complete_native_save(0,{809,0x366A930D,0x408A0000});
    CHECK(runtime_host::frozen&&runtime_host::store.snapshot()==checkpoint&&runtime_host::resources.root==root);
    CHECK(runtime_host::controller->state().document==document&&runtime_host::controller->state().history.empty());
    CHECK(native[0].snapshot.source.instance==809&&native[0].restores==restores&&runtime_host::save_terrain);
}
static void test_transition_stale_save_operation_cannot_publish(){
    for(unsigned changed=0;changed<4;++changed){
        Party party(0,0xA5,true,true);side=0;runtime_host::save();CHECK(spin([]{return native[0].save_busy;}));
        const auto checkpoint=runtime_host::store.snapshot();const auto document=runtime_host::controller->state().document;
        const auto restores=native[0].restores,normalizes=native[0].transition_normalizes;
        auto result=*native[0].save_operation;result.after={810,0x366A930D,0x408A0000};
        if(changed==0)++result.serial;if(changed==1)++result.module_generation;
        if(changed==2)++result.native_generation;if(changed==3)++result.owner.source.instance;
        native[0].save_busy=false;native[0].save_callbacks.saved_operation(result);
        CHECK(runtime_host::frozen&&runtime_host::store.snapshot()==checkpoint);
        CHECK(runtime_host::controller->state().document==document&&native[0].restores==restores&&native[0].transition_normalizes==normalizes);
    }
}
static void test_transition_cancelled_save_waits_for_scene_and_rebuild(){
    Party party(0,0xA5,true,true);const auto checkpoint=runtime_host::store.snapshot();
    const auto document=runtime_host::controller->state().document;const auto source=native[0].snapshot.source;
    side=0;runtime_host::save();CHECK(spin([]{return native[0].save_busy;}));
    ++native[0].raw_terrain;native[0].defer_readiness_on_restore=true;native[0].save_busy=false;
    native[0].save_callbacks.cancelled();step();
    CHECK(runtime_host::save_cancelling&&runtime_host::save_publication_pending&&!native[0].state.ready);
    CHECK(runtime_host::store.snapshot()==checkpoint&&runtime_host::controller->state().document==document);
    native[0].defer_readiness_on_restore=false;native[0].state.ready=true;
    CHECK(spin([]{return !runtime_host::save_cancelling;}));
    CHECK(!runtime_host::frozen&&!runtime_host::save_publication_pending&&!runtime_host::save_terrain);
    CHECK(native[0].snapshot.source==source&&native[0].transition_rebuilds==1&&runtime_host::store.snapshot()==checkpoint);
}
static void test_transition_guest_export_cancel_preserves_all_authored_state(){
    Party party(0,0xA5,true,true);const auto document=runtime_host::controller->state().document;
    const auto host_checkpoint=runtime_host::store.snapshot(),guest_checkpoint=runtime_guest::store.snapshot();
    const auto source=native[1].snapshot.source;const auto view=native[1].view;
    side=1;runtime_guest::save();CHECK(spin([]{return native[1].save_busy;}));
    CHECK(runtime_host::export_terrain&&runtime_guest::export_terrain);
    native[1].snapshot.source={811,0x366A930D,0x408A0000};native[1].save_busy=false;
    native[1].defer_readiness_on_restore=true;native[1].save_callbacks.cancelled();step();
    CHECK(runtime_guest::exporting&&runtime_guest::export_return_result&&!native[1].state.ready);
    CHECK(runtime_host::store.snapshot()==host_checkpoint&&runtime_guest::store.snapshot()==guest_checkpoint);
    native[1].defer_readiness_on_restore=false;native[1].state.ready=true;
    CHECK(spin([]{return !runtime_host::exporting&&!runtime_guest::exporting;}));
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen&&native[1].snapshot.source==source&&native[1].view==view);
    CHECK(runtime_host::controller->state().document==document&&runtime_guest::controller->state().document==document);
    CHECK(runtime_host::controller->state().history.empty()&&runtime_guest::controller->state().history.empty());
}
static void test_transition_cancel_never_restores_replacement_scene(){
    for(unsigned player=0;player<2;++player)for(unsigned changed=0;changed<2;++changed){
        Party party(0,0xA5,true,true);side=int(player);
        if(player)runtime_guest::save();else runtime_host::save();
        CHECK(spin([&]{return native[player].save_busy;}));
        const auto restores=native[player].restores;const auto checkpoint=player?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto bytes=native[player].snapshot.bytes;const editor::ResourceKey source{812,0x366A930D,0x408A0000};
        native[player].snapshot.source=source;
        if(changed)++native[player].state.generation;else ++native[player].scene_owner.resource;
        side=int(player);native[player].save_busy=false;native[player].save_callbacks.cancelled();
        CHECK(spin([&]{return player?runtime_guest::frozen:runtime_host::frozen;}));
        CHECK(native[player].restores==restores&&native[player].snapshot.bytes==bytes&&native[player].snapshot.source==source);
        CHECK((player?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
    }
}
static void test_transition_recovery_bootstrap_keeps_prior_checkpoint_until_ready(){
    Party party;const auto checkpoint=runtime_host::store.snapshot();const auto prior_epoch=runtime_host::controller->state().document.epoch;
    const auto guest_checkpoint=runtime_guest::store.snapshot();const auto guest_document=runtime_guest::controller->state().document;
    side=0;native[0].transition_defer_ready=true;runtime_host::frozen=true;runtime_host::recover();
    CHECK(spin([]{return runtime_host::recovery&&runtime_host::recovery->controller_started&&runtime_host::recovery->terrain&&runtime_host::recovery->terrain->materializing;}));
    CHECK(runtime_host::store.snapshot()==checkpoint&&runtime_host::recovery->state.document.epoch!=prior_epoch);
    CHECK(!runtime_host::view().can_save&&native[0].transition_rebuilds==1);
    for(unsigned i=0;i<8;++i)step();
    CHECK(runtime_host::store.snapshot()==checkpoint&&runtime_guest::controller->view().context.epoch==prior_epoch);
    CHECK(runtime_guest::controller->view().phase==es::Phase::Frozen&&runtime_guest::controller->state().document==guest_document);
    CHECK(runtime_guest::store.snapshot()==guest_checkpoint&&!runtime_guest::view().can_save);
    side=1;CHECK(!runtime_guest::lease({{editor::ScopeKind::Placement,placement,{}}}));
    CHECK(runtime_host::controller->view().members.size()==1&&!native[0].state.ready);
    native[0].transition_defer_ready=false;native[0].state.ready=true;
    CHECK(spin([]{return !runtime_host::recovery;}));
    CHECK(!runtime_host::frozen&&runtime_host::store.snapshot().epoch==runtime_host::controller->state().document.epoch);
    CHECK(native[0].transition_resumes==1&&native[0].state.ready);
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Offered&&!runtime_guest::closing;}));
    CHECK(runtime_guest::controller->view().context.epoch!=prior_epoch);
    side=1;std::string error;CHECK(runtime_guest::controller->accept_offer(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Ready;}));
    CHECK(runtime_guest::controller->state().document==runtime_host::controller->state().document);
}
static void test_transition_recovery_failure_retains_previous_checkpoint(){
    Party party;const auto checkpoint=runtime_host::store.snapshot();const auto guest_checkpoint=runtime_guest::store.snapshot();
    const auto guest_document=runtime_guest::controller->state().document;side=0;
    native[0].refuse_transition_restore=true;runtime_host::frozen=true;runtime_host::recover();
    CHECK(spin([]{return runtime_host::recovery&&runtime_host::recovery->controller_started&&runtime_host::frozen;}));
    CHECK(runtime_host::store.snapshot()==checkpoint&&!runtime_host::view().can_save);
    CHECK(runtime_host::recovery&&native[0].transition_rebuilds>=1&&native[0].transition_resumes==0);
    const auto attempts=native[0].transition_rebuilds;const auto first=runtime_host::view().status;
    for(unsigned i=0;i<8;++i)step();
    CHECK(native[0].transition_rebuilds==attempts&&runtime_host::view().status==first);
    runtime_host::status="background diagnostic";runtime_host::refresh();CHECK(runtime_host::view().status==first);
    CHECK(runtime_guest::controller->view().phase==es::Phase::Frozen&&runtime_guest::controller->state().document==guest_document);
    CHECK(runtime_guest::store.snapshot()==guest_checkpoint&&!runtime_guest::view().can_save);
    side=0;runtime_host::end();CHECK(spin([]{return !runtime_host::recovery&&!runtime_host::closing;}));
}
static void test_transition_recovery_retires_failed_save_hold_case(bool extended){
    seed_complexity_settings=extended;
    Party party(0,0xA5,true,true);
    seed_complexity_settings=false;
    if(extended){
        CHECK(spin([]{return runtime_host::controller->view().complexity_ready&&runtime_guest::controller->view().complexity_ready;}));
        side=0;runtime_host::set_extended_complexity(true);
        CHECK(spin([]{return runtime_host::controller->state().document.revision==1&&runtime_guest::controller->state().document.revision==1&&
            !runtime_host::awaiting_operation&&!runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
        CHECK(editor::extended_complexity(runtime_host::controller->state().document));
    }
    const auto checkpoint=runtime_host::store.snapshot();
    const auto committed=runtime_host::controller->state();
    side=0;runtime_host::save();CHECK(spin([]{return native[0].save_busy;}));
    native[0].refuse_transition_normalize=true;
    complete_native_save(0,{812,0x366A930D,0x408A0000});
    CHECK(runtime_host::frozen&&runtime_host::save_publication_pending&&runtime_host::save_terrain);
    CHECK(runtime_host::store.snapshot()==checkpoint);
    native[0].refuse_transition_normalize=false;native[0].transition_defer_ready=true;
    runtime_host::recover();
    CHECK(spin([]{return runtime_host::recovery&&runtime_host::recovery->terrain&&runtime_host::recovery->terrain->materializing;}));
    CHECK(runtime_host::save_publication_pending&&runtime_host::save_terrain);
    CHECK(runtime_host::store.snapshot()==checkpoint&&!runtime_host::current_gesture_scope().epoch);
    native[0].transition_defer_ready=false;native[0].state.ready=true;
    CHECK(spin([]{return !runtime_host::recovery&&!runtime_host::frozen;}));
    CHECK(!runtime_host::save_publication_pending&&!runtime_host::save_terrain&&!runtime_host::save_cancelling);
    CHECK(runtime_host::controller->state().document.revision==committed.document.revision);
    CHECK(runtime_host::controller->state().history==committed.history);
    CHECK(runtime_host::store.snapshot().epoch!=checkpoint.epoch);
    CHECK(runtime_host::current_gesture_scope().epoch&&runtime_host::current_projection_scope().epoch);
    CHECK(spin([]{return !runtime_guest::closing&&(runtime_guest::controller->view().phase==es::Phase::Offered||runtime_guest::controller->view().phase==es::Phase::Ready);}));
    side=1;std::string error;
    if(runtime_guest::controller->view().phase==es::Phase::Offered)CHECK(runtime_guest::controller->accept_offer(error));
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Ready;}));
    for(unsigned author=0;author<2;++author){
        creation_field(author,placement,{editor::Field::Position,0},editor::Vec3{float(21+author),8,7},
            {editor::ScopeKind::Placement,placement,{}});
        CHECK(runtime_host::controller->state().document.revision==committed.document.revision+author+1);
        CHECK(runtime_guest::controller->state().document==runtime_host::controller->state().document);
    }
}
static void test_transition_recovery_retires_failed_save_hold(){
    test_transition_recovery_retires_failed_save_hold_case(false);
    test_transition_recovery_retires_failed_save_hold_case(true);
}
static void test_transition_failed_recovery_resume_is_terminal_until_explicit_retry(){
    Party party;const auto checkpoint=runtime_host::store.snapshot();side=0;
    runtime_host::save_publication_pending=true;
    native[0].transition_defer_ready=true;runtime_host::frozen=true;runtime_host::recover();
    CHECK(spin([]{return runtime_host::recovery&&runtime_host::recovery->terrain&&runtime_host::recovery->terrain->materializing;}));
    const auto resumes=native[0].transition_resumes;
    native[0].refuse_transition_resume=true;native[0].transition_defer_ready=false;native[0].state.ready=true;
    CHECK(spin([&]{return native[0].transition_resumes>resumes;}));
    CHECK(runtime_host::recovery&&runtime_host::frozen&&runtime_host::store.snapshot()==checkpoint);
    CHECK(runtime_host::save_publication_pending&&!runtime_host::current_gesture_scope().epoch);
    const auto held=runtime_host::recovery->terrain->ticket;const auto image=runtime_host::recovery->native.bytes;
    const auto first=runtime_host::view().status;const auto attempts=native[0].transition_resumes;
    for(unsigned i=0;i<12;++i)step();
    CHECK(runtime_host::recovery&&runtime_host::recovery->terrain->ticket==held&&runtime_host::recovery->native.bytes==image);
    CHECK(native[0].transition_resumes==attempts&&runtime_host::view().status==first&&runtime_host::store.snapshot()==checkpoint);
    runtime_host::status="later invalid receipt";runtime_host::refresh();CHECK(runtime_host::view().status==first);
    native[0].refuse_transition_resume=false;runtime_host::recover();
    CHECK(spin([]{return !runtime_host::recovery&&!runtime_host::frozen;}));
    CHECK(!runtime_host::save_publication_pending&&runtime_host::current_gesture_scope().epoch);
    CHECK(native[0].transition_resumes>attempts&&runtime_host::store.snapshot().epoch!=checkpoint.epoch);
}
static void test_transition_scene_probe_closes_logical_owner(){
    for(unsigned author=0;author<2;++author){
        Party party;side=int(author);runtime_host::TerrainHold host_hold;runtime_guest::TerrainHold guest_hold;
        std::string error;
        CHECK(author?runtime_guest::capture_terrain_hold(guest_hold,error):runtime_host::capture_terrain_hold(host_hold,error));
        const auto before=native[author].restores;unsigned calls=0;
        native[author].state_callback=[&,author]{++calls;
            if(author)runtime_guest::controller->end_for_recovery();else runtime_host::controller->end_for_recovery();
        };
        CHECK(!(author?runtime_guest::terrain_scene_current(guest_hold,error):runtime_host::terrain_scene_current(host_hold,error)));
        native[author].state_callback={};CHECK(calls==1 && native[author].restores==before && !error.empty());
    }
}
static void test_all_terrain_transition_runtime_cases(){
    test_transition_capture_rejects_before_native_save();
    test_transition_host_save_and_next_foreign_edit();
    test_transition_saved_record_rejection_retains_checkpoint();
    test_transition_stale_save_operation_cannot_publish();
    test_transition_cancelled_save_waits_for_scene_and_rebuild();
    test_transition_guest_export_cancel_preserves_all_authored_state();
    test_transition_scene_probe_closes_logical_owner();
    test_transition_cancel_never_restores_replacement_scene();
    test_transition_recovery_bootstrap_keeps_prior_checkpoint_until_ready();
    test_transition_recovery_failure_retains_previous_checkpoint();
    test_transition_recovery_retires_failed_save_hold();
    test_transition_failed_recovery_resume_is_terminal_until_explicit_retry();
}
