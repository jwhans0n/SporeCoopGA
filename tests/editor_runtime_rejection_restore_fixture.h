// Real UDP/controller/durability orchestration with a delayed native scene.
// Native ABI/camera callback fencing is covered by the separate native fixture.
static void begin_rejected_scene(unsigned author,bool deferred=true){
    side=int(author);auto& n=native[author];n.state.selected={placement};
    n.defer_readiness_on_restore=deferred;
    editor_native::Gesture gesture;gesture.generation=n.state.generation;gesture.before=n.snapshot;gesture.after=gesture.before;
    gesture.after.document.placements.at(placement).position={3,4,5};gesture.after.bytes=body(gesture.after.document,gesture.before.bytes.front());
    n.snapshot=gesture.after;n.failed_supported_callback=[]{};
    if(author)runtime_guest::captured(std::move(gesture));else runtime_host::captured(std::move(gesture));
    n.failed_supported_callback={};
}
static bool rejection_held(unsigned author){return author?runtime_guest::rejection_restore.has_value():runtime_host::rejection_restore.has_value();}
static void service_rejection(unsigned author){side=int(author);if(author)runtime_guest::service_rejection_restore();else runtime_host::service_rejection_restore();}
static void test_rejection_scene_waits_for_later_frame_and_local_controls(){
    for(unsigned author=0;author<2;++author){
        Party party;auto& n=native[author];auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        const auto canonical=controller->state();const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto view=n.view;const auto restores=n.restores,views=n.view_restores;
        begin_rejected_scene(author);
        CHECK(rejection_held(author)&&!n.state.ready&&n.restores==restores+1);
        CHECK(author?!runtime_guest::frozen:!runtime_host::frozen);
        const editor_native::GestureScope scope{controller->view().context.project,controller->view().context.epoch,controller->view().local};
        CHECK(n.callbacks.gesture_scope()==editor_native::GestureScope{}&&n.callbacks.projection_scope()==scope);
        CHECK(n.callbacks.transaction_pending()&&!n.callbacks.gate({placement})&&!n.callbacks.terrain_gate({},{}));
        CHECK(!n.callbacks.navigation_allowed()&&!n.callbacks.history_available(true));
        const auto saves=n.save_calls+n.save_as_calls,enters=n.enters;
        if(author){runtime_guest::save();runtime_guest::playtest();runtime_guest::create_asset(0x2B978C46);}
        else{runtime_host::save();runtime_host::playtest();runtime_host::create_asset(0x2B978C46);}
        for(unsigned i=0;i<4;++i)step();
        CHECK(rejection_held(author)&&controller->state()==canonical);
        CHECK(n.save_calls+n.save_as_calls==saves&&n.enters==enters&&n.view_restores==views);
        CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
        const auto held=author?runtime_guest::view():runtime_host::view();CHECK(held.waiting&&!held.can_save&&!held.can_playtest);
        n.defer_readiness_on_restore=false;n.state.ready=true;const auto captures=n.draft_capture_attempts;
        service_rejection(author);CHECK(rejection_held(author)&&n.view_restores==views+1);
        service_rejection(author);CHECK(rejection_held(author)&&n.draft_capture_attempts==captures);
        CHECK(spin([&]{return !rejection_held(author);}));
        CHECK(n.snapshot.document==canonical.document&&n.view==view&&n.state.selected==std::vector<editor::ObjectId>{placement});
        CHECK(controller->state()==canonical&&(author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
        CHECK(author?!runtime_guest::frozen:!runtime_host::frozen);
    }
}
static void test_rejection_scene_preserves_independent_act_and_view(){
    for(unsigned author=0;author<2;++author){
        Party party(0,0xA5,false,false,false,3);auto& n=native[author];n.state.act=2;n.snapshot.act=2;
        const auto view=n.view;begin_rejected_scene(author);CHECK(n.state.act==2);
        n.defer_readiness_on_restore=false;n.state.ready=true;CHECK(spin([&]{return !rejection_held(author);}));
        CHECK(n.state.act==2&&n.snapshot.act==2&&n.view==view);
    }
}
static void test_rejection_scene_accepts_foreign_commits_and_coalesces_latest(){
    for(unsigned observer=0;observer<2;++observer){
        Party party;auto& n=native[observer];const auto restores=n.restores,applies=n.apply_calls;
        begin_rejected_scene(observer);const auto initial_restores=n.restores;
        const unsigned author=1-observer;
        creation_field(author,placement,{editor::Field::Position,0},editor::Vec3{21,8,9},{editor::ScopeKind::Placement,placement,{}});
        auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();std::string error;side=int(author);
        CHECK(controller->request_leases({{editor::ScopeKind::Structure,{},{}},{editor::ScopeKind::Class,class_id,{}}},15000,error));
        CHECK(spin([&]{return controller->view().local_leases.size()>=2;}));
        auto request=author?runtime_guest::request(editor::Intent::Forward):runtime_host::request(editor::Intent::Forward);
        editor::Mutation create;create.kind=editor::MutationKind::Create;create.initial.class_id=class_id;create.initial.act_id=act_id;
        create.initial.properties[editor_native::kRelativeScaleProperty]=.5f;create.initial.position={44,8,9};request.mutations={create};
        side=int(author);CHECK(controller->submit(request,error));
        CHECK(spin([]{return runtime_host::controller->state().document.revision==2&&runtime_guest::controller->state().document.revision==2&&
            !runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
        CHECK(runtime_host::store.durable_revision()==2&&runtime_guest::store.durable_revision()==2);
        CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
        CHECK(n.restores==initial_restores&&initial_restores==restores+1&&n.apply_calls==applies);
        CHECK(n.snapshot.document.revision==0&&rejection_held(observer));
        CHECK((observer?runtime_guest::current_native:runtime_host::current_native).document.revision==2);
        n.state.ready=true;service_rejection(observer);
        CHECK(n.restores==initial_restores+1&&!n.state.ready); // One latest restore, no revision-one scene.
        n.defer_readiness_on_restore=false;n.state.ready=true;
        CHECK(spin([&]{return !rejection_held(observer);}));
        const auto& latest=runtime_host::controller->state().document;
        CHECK(n.snapshot.document==latest&&n.restores==initial_restores+1&&n.snapshot.document.placements.size()==2);
        CHECK((observer?runtime_guest::rendered_revision:runtime_host::rendered_revision)==2);
        CHECK(runtime_host::controller->state().history.size()==2&&!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_rejection_scene_detects_post_view_or_selection_mutation(){
    for(unsigned author=0;author<2;++author)for(unsigned changed=0;changed<4;++changed){
        Party party;auto& n=native[author];auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        const auto canonical=controller->state();const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        begin_rejected_scene(author);n.defer_readiness_on_restore=false;n.state.ready=true;
        auto mutate=[&]{n.snapshot.document.placements.at(placement).rotation={.6f,0,0,.8f};n.snapshot.bytes=body(n.snapshot.document,n.snapshot.bytes.front());};
        if(changed==0)n.selection_callback=mutate;
        if(changed==1)n.view_callback=mutate;
        if(changed==2)n.view_callback=[&]{n.snapshot.bytes.front()^=0x20;};
        if(changed==3)n.refuse_draft_capture=true;
        CHECK(spin([&]{return author?runtime_guest::frozen:runtime_host::frozen;}));
        CHECK(rejection_held(author)&&controller->state()==canonical);
        CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
        const auto restores=n.restores,views=n.view_restores;service_rejection(author);
        CHECK(n.restores==restores&&n.view_restores==views);
        const auto& status=author?runtime_guest::status:runtime_host::status;
        CHECK(status.find("Injected unsupported callback failure")!=std::string::npos&&status.find("canonical restoration failed")!=std::string::npos);
    }
}
static void test_rejection_scene_expiry_never_writes_again(){
    for(unsigned author=0;author<2;++author)for(unsigned expired=0;expired<12;++expired){
        Party party;auto& n=native[author];auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        begin_rejected_scene(author);const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto canonical=controller->state();const auto restores=n.restores,views=n.view_restores,applies=n.apply_calls,registrations=n.registrations;
        auto& view=const_cast<es::View&>(controller->view());
        if(expired==0)++view.context.project[0];if(expired==1)++view.context.epoch;if(expired==2)++view.local.generation;
        if(expired==3)++view.local.slot;if(expired==4)++n.state.generation;
        if(expired==5)++n.scene_owner.mode;if(expired==6)++n.scene_owner.data;if(expired==7)++n.scene_owner.resource;
        if(expired==8)++n.snapshot.source.instance;if(expired==9)n.scene_owner_available=false;
        if(expired==10){if(author)++runtime_guest::observed_transport;else ++runtime_host::observed_transport;}
        if(expired==11)sessions[author].leave("rejected scene lost transport");
        service_rejection(author);
        CHECK(author?runtime_guest::frozen&&runtime_guest::native_identity_lost:runtime_host::frozen&&runtime_host::native_identity_lost);
        CHECK(n.restores==restores&&n.view_restores==views&&n.apply_calls==applies&&n.registrations==registrations);
        CHECK(controller->state()==canonical&&(author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
    }
}
static void test_rejection_scene_fenced_view_expiry_and_failed_restore(){
    for(unsigned author=0;author<2;++author){
        {Party party;auto& n=native[author];begin_rejected_scene(author);n.defer_readiness_on_restore=false;n.state.ready=true;
            const auto views=n.view_restores,restores=n.restores;const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
            n.view_callback=[&]{++n.snapshot.source.instance;};++game_frame;service_rejection(author);
            CHECK(author?runtime_guest::frozen:runtime_host::frozen);CHECK(n.view_restores==views&&n.restores==restores);
            CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);}
        {Party party;auto& n=native[author];n.refuse_restore=true;const auto restores=n.restores;
            const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();begin_rejected_scene(author,false);
            CHECK(author?runtime_guest::frozen:runtime_host::frozen);CHECK(rejection_held(author)&&n.restores==restores);
            service_rejection(author);CHECK(n.restores==restores&&(author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);}
    }
}
static void test_rejection_scene_last_readiness_getter_never_writes_expired_owner(){
    for(unsigned author=0;author<2;++author)for(unsigned coalesce=0;coalesce<2;++coalesce){
        Party party;auto& n=native[author];begin_rejected_scene(author);
        if(coalesce)creation_field(1-author,placement,{editor::Field::Position,0},editor::Vec3{21,8,9},{editor::ScopeKind::Placement,placement,{}});
        const auto restores=n.restores,views=n.view_restores,registrations=n.registrations;
        const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        n.defer_readiness_on_restore=false;n.state.ready=true;++game_frame;
        unsigned reads=0;n.scene_status_callback=[&]{if(++reads==2)++n.snapshot.source.instance;};
        service_rejection(author);n.scene_status_callback={};
        CHECK(reads>=2);
        CHECK(author?runtime_guest::frozen&&runtime_guest::native_identity_lost:runtime_host::frozen&&runtime_host::native_identity_lost);
        CHECK(n.restores==restores&&n.view_restores==views&&n.registrations==registrations);
        CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
    }
}
static void test_rejection_scene_failed_hold_recovery_rejects_replacement(){
    for(unsigned replaced=0;replaced<2;++replaced){
        Party party;auto& n=native[0];begin_rejected_scene(0);n.defer_readiness_on_restore=false;n.state.ready=true;
        n.refuse_draft_capture=true;CHECK(spin([]{return runtime_host::frozen;}));n.refuse_draft_capture=false;
        const auto checkpoint=runtime_host::store.snapshot();const auto canonical=runtime_host::controller->state();
        const auto restores=n.restores,registrations=n.registrations;
        if(replaced)++n.scene_owner.resource;else ++n.snapshot.source.instance;
        side=0;runtime_host::execute(runtime_host::Action::Recover);
        CHECK(runtime_host::frozen&&runtime_host::native_identity_lost&&runtime_host::rejection_restore);
        CHECK(n.restores==restores&&n.registrations==registrations);
        CHECK(runtime_host::store.snapshot()==checkpoint&&runtime_host::controller->state()==canonical);
    }
}
static void test_rejection_scene_begin_never_adopts_state_callback_replacement(){
    for(unsigned author=0;author<2;++author)for(unsigned replaced=0;replaced<4;++replaced){
        Party party;auto& n=native[author];side=int(author);
        auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        const auto canonical=controller->state();const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto restores=n.restores,registrations=n.registrations;
        editor_native::Gesture gesture;gesture.generation=n.state.generation;gesture.before=n.snapshot;gesture.after=gesture.before;
        gesture.after.document.placements.at(placement).position={3,4,5};gesture.after.bytes=body(gesture.after.document);n.snapshot=gesture.after;
        bool changed=false;n.failed_supported_callback=[&]{n.state_callback=[&]{
            if(changed||!n.callbacks.transaction_pending())return;
            changed=true;if(replaced==0)++n.scene_owner.mode;if(replaced==1)++n.scene_owner.data;
            if(replaced==2)++n.scene_owner.resource;if(replaced==3)++n.snapshot.source.instance;
        };};
        if(author)runtime_guest::captured(std::move(gesture));else runtime_host::captured(std::move(gesture));
        n.failed_supported_callback={};n.state_callback={};
        CHECK(changed&&n.restores==restores&&n.registrations==registrations);
        CHECK(author?runtime_guest::frozen&&runtime_guest::native_identity_lost:runtime_host::frozen&&runtime_host::native_identity_lost);
        CHECK(controller->state()==canonical&&(author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
    }
}
static void test_rejection_scene_generic_callers_keep_preoperation_owner(){
    for(unsigned author=0;author<2;++author)for(unsigned operation=0;operation<2;++operation)for(unsigned replaced=0;replaced<6;++replaced){
        Party party;auto& n=native[author];side=int(author);
        auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        const auto canonical=controller->state();const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto restores=n.restores,registrations=n.registrations,applies=n.apply_calls;
        n.snapshot.bytes.front()^=0x20; // A real opaque delta requires the unsupported guard.
        n.failed_supported_callback=[&]{
            if(replaced==0)++n.scene_owner.mode;if(replaced==1)++n.scene_owner.data;
            if(replaced==2)++n.scene_owner.resource;if(replaced==3)++n.snapshot.source.instance;
            auto& view=const_cast<es::View&>(controller->view());if(replaced==4)++view.context.epoch;
            if(replaced==5){if(author)++runtime_guest::observed_transport;else ++runtime_host::observed_transport;}
        };
        std::string error;bool accepted=false;
        if(!operation)accepted=author?runtime_guest::check_native_integrity(error):runtime_host::check_native_integrity(error);
        else{editor::Change change;change.operation={controller->view().local,42,canonical.document.epoch};
            accepted=author?runtime_guest::apply(canonical.document,canonical.document,change,error):runtime_host::apply(canonical.document,canonical.document,change,error);}
        n.failed_supported_callback={};CHECK(!accepted);
        CHECK(author?runtime_guest::frozen&&runtime_guest::native_identity_lost:runtime_host::frozen&&runtime_host::native_identity_lost);
        CHECK(n.restores==restores&&n.registrations==registrations&&n.apply_calls==applies);
        CHECK(controller->state()==canonical&&(author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
    }
}
static void test_rejection_scene_fenced_restore_never_writes_a_replacement(){
    for(unsigned author=0;author<2;++author)for(unsigned coalesce=0;coalesce<2;++coalesce)for(unsigned replaced=0;replaced<4;++replaced){
        Party party;auto& n=native[author];
        if(coalesce){
            begin_rejected_scene(author);
            creation_field(1-author,placement,{editor::Field::Position,0},editor::Vec3{21,8,9},{editor::ScopeKind::Placement,placement,{}});
            n.defer_readiness_on_restore=false;n.state.ready=true;++game_frame;
        }
        const auto restores=n.restores,registrations=n.registrations;
        const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        n.restore_callback=[&]{if(replaced==0)++n.scene_owner.resource;if(replaced==1)++n.snapshot.source.instance;
            if(replaced==2)++n.state.generation;if(replaced==3){if(author)++runtime_guest::observed_transport;else ++runtime_host::observed_transport;}};
        if(coalesce)service_rejection(author);else begin_rejected_scene(author,false);
        n.restore_callback={};
        CHECK(author?runtime_guest::frozen&&runtime_guest::native_identity_lost:runtime_host::frozen&&runtime_host::native_identity_lost);
        CHECK(n.restores==restores&&n.registrations==registrations+(coalesce?1:0));
        CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
    }
}
static void test_all_rejection_restore_cases(){
    test_rejection_scene_waits_for_later_frame_and_local_controls();
    test_rejection_scene_preserves_independent_act_and_view();
    test_rejection_scene_accepts_foreign_commits_and_coalesces_latest();
    test_rejection_scene_detects_post_view_or_selection_mutation();
    test_rejection_scene_expiry_never_writes_again();
    test_rejection_scene_fenced_view_expiry_and_failed_restore();
    test_rejection_scene_last_readiness_getter_never_writes_expired_owner();
    test_rejection_scene_failed_hold_recovery_rejects_replacement();
    test_rejection_scene_begin_never_adopts_state_callback_replacement();
    test_rejection_scene_generic_callers_keep_preoperation_owner();
    test_rejection_scene_fenced_restore_never_writes_a_replacement();
}
