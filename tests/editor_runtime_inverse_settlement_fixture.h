// Actual Runtime/controller/UDP/store orchestration. Native async inverse and
// scene ownership are modeled explicitly; native ABI/view fences are separate.
static editor::TerrainRecord inverse_stamp(uint32_t instance){
    editor::TerrainRecord row;row.object={0xF0000000ull+instance,1};row.order=1;
    row.payload.format=editor::kTerrainRecordFormat;row.payload.bytes.resize(editor::kTerrainEffectBytes);
    std::memcpy(row.payload.bytes.data(),&instance,4);const float scale=1;
    std::memcpy(row.payload.bytes.data()+28,&scale,4);
    CHECK(editor::valid_terrain_payload(row.kind,row.payload));return row;
}
static void begin_inverse_append(unsigned author,uint32_t instance){
    side=int(author);auto& n=native[author];n.state.selected={placement};
    editor_native::Gesture gesture;gesture.generation=n.state.generation;gesture.before=n.snapshot;gesture.after=gesture.before;
    auto row=inverse_stamp(instance);row.order=gesture.after.document.next_terrain_order++;
    gesture.after.document.terrain.emplace(row.object,row);gesture.after.bytes=body(gesture.after.document,gesture.before.bytes.front());
    n.snapshot=gesture.after;n.defer_readiness_on_apply=true;
    if(author)runtime_guest::captured(std::move(gesture));else runtime_host::captured(std::move(gesture));
}
static void submit_inverse(unsigned author){side=int(author);if(author)runtime_guest::submit_pending_gesture();else runtime_host::submit_pending_gesture();}
static bool inverse_pending(unsigned author){return author?bool(runtime_guest::pending):bool(runtime_host::pending);}
static void test_inverse_waits_for_strict_later_frame_proof(){
    for(unsigned author=0;author<2;++author){
        Party party;auto& n=native[author];auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        const auto canonical=controller->state();const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        begin_inverse_append(author,101+author);
        CHECK(inverse_pending(author)&&rejection_held(author)&&!n.state.ready);
        CHECK(author?!runtime_guest::frozen:!runtime_host::frozen);
        const auto request=author?runtime_guest::pending->request:runtime_host::pending->request;
        auto submitted=[&]{return author?runtime_guest::pending->submitted:runtime_host::pending->submitted;};
        CHECK(!submitted()&&!(author?runtime_guest::pending->tentative:runtime_host::pending->tentative)&&request.base_revision==0&&request.mutations.size()==1);
        CHECK(request.mutations.front().kind==editor::MutationKind::AppendTerrain);
        CHECK((author?runtime_guest::rejection_restore->continuation:runtime_host::rejection_restore->continuation)==request.operation);
        editor_native::SceneOwner unavailable;std::string error;side=int(author);
        CHECK(!editor_native::capture_scene_owner(unavailable,error)); // Real API requires readiness.
        CHECK(n.callbacks.gesture_scope()==editor_native::GestureScope{}&&n.callbacks.projection_scope().epoch);
        CHECK(n.callbacks.transaction_pending()&&!n.callbacks.gate({placement})&&!n.callbacks.terrain_gate({},{}));
        CHECK(!n.callbacks.navigation_allowed()&&!n.callbacks.history_available(false));
        const auto saves=n.save_calls+n.save_as_calls,enters=n.enters,captures=n.draft_capture_attempts;
        if(author){runtime_guest::save();runtime_guest::playtest();runtime_guest::create_asset(0x2B978C46);}
        else{runtime_host::save();runtime_host::playtest();runtime_host::create_asset(0x2B978C46);}
        for(unsigned i=0;i<3;++i){submit_inverse(author);service_rejection(author);}
        CHECK(controller->state()==canonical&&(author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
        CHECK(n.save_calls+n.save_as_calls==saves&&n.enters==enters&&n.draft_capture_attempts==captures);
        n.state.ready=true;
        service_rejection(author);submit_inverse(author);CHECK(rejection_held(author)&&!submitted());
        n.state.ready=false;
        for(unsigned i=0;i<3;++i)step(); // Queued controls execute under the live hold.
        CHECK(n.save_calls+n.save_as_calls==saves&&n.enters==enters);
        n.defer_readiness_on_apply=false;n.state.ready=true;
        service_rejection(author);CHECK(rejection_held(author)&&!submitted());
        CHECK(n.draft_capture_attempts==captures); // View/selection precede final proof.
        service_rejection(author);submit_inverse(author);CHECK(rejection_held(author)&&n.draft_capture_attempts==captures);
        CHECK(spin([&]{return runtime_host::store.durable_revision()==1&&runtime_guest::store.durable_revision()==1&&
            !runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
        CHECK(n.draft_capture_attempts>captures&&!rejection_held(author));
        CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
        CHECK(controller->state().history.size()==1&&controller->state().document.terrain.size()==1);
        CHECK(controller->state().history.front().operation==request.operation);
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_simultaneous_inverse_appends_rebase_without_losing_foreign(){
    for(unsigned first=0;first<2;++first){
        Party party;begin_inverse_append(0,101);begin_inverse_append(1,202);
        CHECK(rejection_held(0)&&rejection_held(1));
        const unsigned second=1-first;auto& waiting=native[second];
        const auto original=(second?runtime_guest::pending->request:runtime_host::pending->request);
        const auto old_live=waiting.snapshot;const auto restores=waiting.restores,applies=waiting.apply_calls;
        native[first].defer_readiness_on_apply=false;native[first].state.ready=true;
        CHECK(spin([]{return runtime_host::store.durable_revision()==1&&runtime_guest::store.durable_revision()==1&&
            !runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
        CHECK(rejection_held(second)&&waiting.snapshot.bytes==old_live.bytes&&waiting.apply_calls==applies);
        CHECK((second?runtime_guest::current_native:runtime_host::current_native).document.terrain.size()==1);
        const auto& pending_request=(second?runtime_guest::pending->request:runtime_host::pending->request);
        std::vector<uint8_t> original_bytes,current_bytes;std::string error;
        CHECK(ew::encode_request(original,original_bytes,error)&&ew::encode_request(pending_request,current_bytes,error));
        CHECK(original_bytes==current_bytes&&pending_request.base_revision==0);
        waiting.defer_readiness_on_apply=false;waiting.defer_readiness_on_restore=true;waiting.state.ready=true;
        ++game_frame;service_rejection(second);
        CHECK(waiting.restores==restores+1&&!waiting.state.ready&&rejection_held(second));
        CHECK((second?runtime_guest::pending->request:runtime_host::pending->request).operation==original.operation);
        waiting.defer_readiness_on_restore=false;waiting.state.ready=true;
        CHECK(spin([]{return runtime_host::store.durable_revision()==2&&runtime_guest::store.durable_revision()==2&&
            !runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
        CHECK(!rejection_held(second)&&waiting.restores==restores+1);
        const auto accepted=runtime_host::controller->state().document;
        CHECK(accepted==runtime_guest::controller->state().document&&accepted.terrain.size()==2);
        std::set<uint32_t> instances;for(const auto& [id,row]:accepted.terrain){(void)id;instances.insert(editor::terrain_word(row.payload.bytes,0));}
        CHECK(instances==std::set<uint32_t>{101,202});
        CHECK(runtime_host::controller->state().history.size()==2&&!runtime_host::frozen&&!runtime_guest::frozen);
        side=int(second);if(second)runtime_guest::undo();else runtime_host::undo();
        CHECK(spin([]{return runtime_host::store.durable_revision()==3&&runtime_guest::store.durable_revision()==3&&
            !runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
        const auto& remaining=runtime_host::controller->state().document.terrain;CHECK(remaining.size()==1);
        CHECK(editor::terrain_word(remaining.begin()->second.payload.bytes,0)==(first?202u:101u));
        CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
    }
}
static void test_inverse_settlement_expiry_and_strict_failure_never_submit(){
    for(unsigned author=0;author<2;++author)for(unsigned expired=0;expired<13;++expired){
        Party party;begin_inverse_append(author,101+author);auto& n=native[author];
        auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        const auto canonical=controller->state();const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto restores=n.restores,applies=n.apply_calls,registrations=n.registrations,views=n.view_restores;
        auto& view=const_cast<es::View&>(controller->view());
        if(expired==0)++view.context.project[0];if(expired==1)++view.context.epoch;
        if(expired==2)++view.local.generation;if(expired==3)++view.local.slot;if(expired==4)++n.state.generation;
        if(expired==5)++n.scene_owner.mode;if(expired==6)++n.scene_owner.data;if(expired==7)++n.scene_owner.resource;
        if(expired==8)++n.snapshot.source.instance;if(expired==9)n.scene_owner_available=false;
        if(expired==10){if(author)++runtime_guest::observed_transport;else ++runtime_host::observed_transport;}
        if(expired==11)sessions[author].leave("inverse expired transport");
        if(expired==12)n.scene_status_callback=[&]{++n.scene_owner.resource;};
        n.defer_readiness_on_apply=false;n.state.ready=true;++game_frame;service_rejection(author);submit_inverse(author);
        CHECK(author?runtime_guest::frozen&&runtime_guest::native_identity_lost:runtime_host::frozen&&runtime_host::native_identity_lost);
        CHECK(!inverse_pending(author)&&n.restores==restores&&n.apply_calls==applies&&n.registrations==registrations&&n.view_restores==views);
        CHECK(controller->state()==canonical&&(author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
    }
    for(unsigned author=0;author<2;++author)for(unsigned failure=0;failure<4;++failure){
        Party party;begin_inverse_append(author,101+author);auto& n=native[author];
        const auto canonical=(author?runtime_guest::controller:runtime_host::controller)->state();
        const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        n.defer_readiness_on_apply=false;n.state.ready=true;
        if(failure==0)n.view_callback=[&]{n.snapshot.document.placements.at(placement).rotation={.6f,0,0,.8f};n.snapshot.bytes=body(n.snapshot.document);};
        if(failure==1)n.view_callback=[&]{n.snapshot.bytes.front()^=0x20;};
        if(failure==2)n.refuse_draft_capture=true;
        if(failure==3)clock_ms+=180001;
        CHECK(spin([&]{return author?runtime_guest::frozen:runtime_host::frozen;}));
        CHECK(!inverse_pending(author)&&rejection_held(author));
        CHECK((author?runtime_guest::controller:runtime_host::controller)->state()==canonical);
        CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
    }
}
static void test_inverse_owner_replaced_inside_apply_is_never_adopted(){
    for(unsigned author=0;author<2;++author)for(unsigned changed=0;changed<3;++changed){
        Party party;auto& n=native[author];const auto canonical=(author?runtime_guest::controller:runtime_host::controller)->state();
        const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        n.apply_callback=[&,changed]{if(changed==0)++n.scene_owner.resource;if(changed==1)++n.snapshot.source.instance;if(changed==2)++n.state.generation;};
        begin_inverse_append(author,101+author);
        CHECK(author?runtime_guest::frozen&&runtime_guest::native_identity_lost:runtime_host::frozen&&runtime_host::native_identity_lost);
        CHECK(!inverse_pending(author)&&!rejection_held(author));
        CHECK((author?runtime_guest::controller:runtime_host::controller)->state()==canonical);
        CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
    }
}
static void test_inverse_continuation_renews_original_lease_tokens(){
    for(unsigned author=0;author<2;++author){
        Party party;begin_inverse_append(author,303+author);auto& n=native[author];
        n.defer_readiness_on_apply=false;n.state.ready=true;
        CHECK(spin([]{return runtime_host::store.durable_revision()==1&&runtime_guest::store.durable_revision()==1&&
            !runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
        auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        const auto terrain=controller->state().document.terrain.begin()->first;
        std::vector<editor::Scope> scopes{{editor::ScopeKind::Terrain,terrain,{}},{editor::ScopeKind::Class,class_id,{}},
            {editor::ScopeKind::Structure,{},{}},{editor::ScopeKind::Field,placement,{editor::Field::Scale,0}}};
        std::string error;side=int(author);CHECK(controller->request_leases(scopes,15000,error));
        CHECK(spin([&]{return controller->view().local_leases.size()==scopes.size();}));
        const auto granted=controller->view().local_leases;const auto initial_time=clock_ms;
        editor_native::Gesture gesture;gesture.generation=n.state.generation;gesture.before=n.snapshot;gesture.after=gesture.before;
        const float x=9;auto& changed=gesture.after.document.terrain.at(terrain);
        std::memcpy(changed.payload.bytes.data()+16,&x,4);gesture.after.bytes=body(gesture.after.document,gesture.before.bytes.front());
        (author?runtime_guest::guarded_terrain:runtime_host::guarded_terrain)=controller->state().document.terrain;
        n.snapshot=gesture.after;n.defer_readiness_on_apply=true;side=int(author);
        if(author)runtime_guest::captured(std::move(gesture));else runtime_host::captured(std::move(gesture));
        CHECK(rejection_held(author)&&inverse_pending(author)&&!n.state.ready);
        const auto operation=author?runtime_guest::pending->request.operation:runtime_host::pending->request.operation;
        const auto tokens=author?runtime_guest::pending->request.leases:runtime_host::pending->request.leases;
        CHECK(tokens.size()==scopes.size());
        for(unsigned cycle=0;cycle<4;++cycle){clock_ms+=4000;for(unsigned i=0;i<12;++i)step();}
        CHECK(clock_ms>initial_time+15000&&rejection_held(author)&&controller->state().document.revision==1);
        CHECK((author?runtime_guest::pending->request.leases:runtime_host::pending->request.leases)==tokens);
        for(const auto& lease:granted){
            const auto current=std::find_if(controller->view().local_leases.begin(),controller->view().local_leases.end(),[&](const auto& row){
                return row.scope==lease.scope&&row.token==lease.token&&row.owner==lease.owner;});
            CHECK(current!=controller->view().local_leases.end()&&current->expires>clock_ms&&current->expires>lease.expires);
        }
        n.defer_readiness_on_apply=false;n.state.ready=true;
        CHECK(spin([]{return runtime_host::store.durable_revision()==2&&runtime_guest::store.durable_revision()==2&&
            !runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
        CHECK(controller->state().history.back().operation==operation);
        CHECK(editor::terrain_float(controller->state().document.terrain.at(terrain).payload.bytes,16)==9.f);
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_inverse_keeps_hold_when_final_getter_starts_foreign_durability(){
    for(unsigned observer=0;observer<2;++observer){
        Party party;begin_inverse_append(observer,606+observer);auto& n=native[observer];
        n.defer_readiness_on_apply=false;n.state.ready=true;++game_frame;service_rejection(observer);
        const auto captures=n.draft_capture_attempts;bool injected=false;
        const unsigned author=1-observer;
        auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        side=int(author);auto request=author?runtime_guest::request(editor::Intent::Forward):runtime_host::request(editor::Intent::Forward);
        auto row=inverse_stamp(909+author);row.object={};row.order=row.revision=0;
        editor::Mutation mutation;mutation.kind=editor::MutationKind::AppendTerrain;mutation.terrain=row;request.mutations={mutation};
        n.scene_status_callback=[&]{
            if(n.draft_capture_attempts<=captures)return;
            n.scene_status_callback={};injected=true;side=int(author);std::string error;
            CHECK(controller->submit(request,error));
            CHECK(spin([&]{return observer?bool(runtime_guest::durable_commit):bool(runtime_host::durable_commit);}));
            CHECK((observer?runtime_guest::controller:runtime_host::controller)->state().document.revision==0);
            side=int(observer);
        };
        ++game_frame;service_rejection(observer);
        CHECK(injected&&rejection_held(observer)&&inverse_pending(observer));
        CHECK(observer?!runtime_guest::pending->submitted:!runtime_host::pending->submitted);
        CHECK(observer?!runtime_guest::rejection_restore->verified:!runtime_host::rejection_restore->verified);
        CHECK((observer?runtime_guest::rendered_revision:runtime_host::rendered_revision)==0);
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
        CHECK(spin([]{return runtime_host::store.durable_revision()==2&&runtime_guest::store.durable_revision()==2&&
            !runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
        CHECK(!rejection_held(observer)&&runtime_host::controller->state().document.terrain.size()==2);
        CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_all_inverse_settlement_cases(){
    test_inverse_waits_for_strict_later_frame_proof();
    test_simultaneous_inverse_appends_rebase_without_losing_foreign();
    test_inverse_settlement_expiry_and_strict_failure_never_submit();
    test_inverse_owner_replaced_inside_apply_is_never_adopted();
    test_inverse_continuation_renews_original_lease_tokens();
    test_inverse_keeps_hold_when_final_getter_starts_foreign_durability();
}
