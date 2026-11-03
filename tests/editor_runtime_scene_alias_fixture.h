// Preserve the real unsaved collaboration bootstrap: its canonical transport
// root differs from the host's external working-resource key. Party's default
// historical Saved callback deliberately makes those keys equal instead.
static void test_private_scene_alias_stages_without_changing_the_canonical_root(){
    for(unsigned author=0;author<2;++author){
        Party party(64,0xA5,false,false,author==1,1,0,false,0,false,true);
        auto& n=native[author];const auto actual=n.snapshot.source;
        const auto root=author?runtime_guest::resources.root:runtime_host::resources.root;
        const editor::ResourceKey canonical{root.instance,root.type,root.group};
        CHECK(actual!=canonical);
        begin_staged_pose(author);const auto original=staged_original_request(author);
        CHECK(spin([]{return staged_settled(1);}));
        CHECK(n.staged_frames.size()==6&&!n.staged_owners&&n.snapshot.source==actual);
        CHECK(runtime_host::current_native.source==canonical&&runtime_guest::current_native.source==canonical);
        CHECK(runtime_host::resources.root==root&&runtime_guest::resources.root==root);
        CHECK(runtime_host::controller->state().history.size()==1&&runtime_host::controller->state().history.front().operation==original.operation);
        CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
#ifndef SPORECOOP_RUNTIME_ALIAS_BASELINE
static void test_private_scene_alias_rejects_unattached_or_expired_identity(){
    for(unsigned author=0;author<2;++author)for(unsigned expired=0;expired<8;++expired){
        Party party(64,0xA5,false,false,author==1,1,0,false,0,false,true);
        grant_tentative_placement(author);side=int(author);auto& n=native[author];
        auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        auto& content=author?runtime_guest::resources:runtime_host::resources;
        const auto saved_content=content;const auto old_source=n.snapshot.source;const auto old_owner=n.scene_owner;
        const auto old_generation=n.state.generation;const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto writes=std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores};
        const auto old_view=controller->view();
        if(expired==0)n.snapshot.source.instance^=0x100;
        if(expired==1)++n.scene_owner.resource;
        if(expired==2)++n.state.generation;
        if(expired==3)++content.root.instance;
        if(expired==4){auto entry=std::find_if(content.bundle.records.begin(),content.bundle.records.end(),[&](const auto& r){return r.key!=content.root;});CHECK(entry!=content.bundle.records.end());entry->data.front()^=1;}
        if(expired==5)content.base_root=std::make_shared<const std::vector<uint8_t>>(*content.base_root);
        if(expired==6)++const_cast<es::View&>(controller->view()).context.epoch;
        if(expired==7){if(author)runtime_guest::retire_scene_alias();else runtime_host::retire_scene_alias();}
        auto owner=n.scene_owner;owner.source=n.snapshot.source;
        const editor_native::CaptureTicket ticket{n.callbacks.gesture_scope(),owner,n.state.generation,7000+expired,n.snapshot.act};
        std::string error;CHECK(!n.callbacks.capture_begin(ticket,error)&&!staged_held(author));
        CHECK(std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores}==writes);
        CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
        content=saved_content;n.snapshot.source=old_source;n.scene_owner=old_owner;n.state.generation=old_generation;
        const_cast<es::View&>(controller->view())=old_view;
    }
}
static void test_private_scene_alias_attachment_callback_cannot_authorize_a_replacement(){
    for(unsigned author=0;author<2;++author)for(unsigned expired=0;expired<4;++expired){
        Party party(64,0xA5,false,false,author==1,1,0,false,0,false,true);side=int(author);auto& n=native[author];
        const auto original=n.scene_owner;const auto source=n.snapshot.source;const auto generation=n.state.generation;
        const auto writes=std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores};
        if(author)runtime_guest::queue_scene_alias_attachment(runtime_guest::pin_scene_alias_owner());
        else runtime_host::queue_scene_alias_attachment(runtime_host::pin_scene_alias_owner());
        bool changed=false;n.draft_capture_callback=[&]{n.draft_capture_callback={};changed=true;
            if(expired==0)++n.snapshot.source.instance;
            if(expired==1)++n.scene_owner.resource;
            if(expired==2)++n.state.generation;
            if(expired==3){auto& root=author?runtime_guest::resources:runtime_host::resources;++root.root.instance;}
        };
        ++game_frame;if(author)runtime_guest::service_scene_alias_attachment();else runtime_host::service_scene_alias_attachment();
        CHECK(changed&&!(author?bool(runtime_guest::scene_alias):bool(runtime_host::scene_alias)));
        CHECK(std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores}==writes);
        n.scene_owner=original;n.snapshot.source=source;n.state.generation=generation;
        auto& root=author?runtime_guest::resources:runtime_host::resources;
        const auto canonical=author?runtime_guest::current_native.source:runtime_host::current_native.source;
        root.root={canonical.instance,canonical.type,canonical.group};
    }
}
static void test_scene_attachment_getters_cannot_authorize_changed_resources(){
    for(unsigned author=0;author<2;++author)for(unsigned getter=1;getter<=2;++getter)for(unsigned expired=0;expired<17;++expired){
        Party party(64,0xA5,false,false,author==1,1,0,false,0,false,true);side=int(author);auto& n=native[author];
        auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();
        auto& content=author?runtime_guest::resources:runtime_host::resources;
        auto& incoming=author?runtime_guest::incoming_resources:runtime_host::incoming_resources;
        const auto saved=content;const auto view=controller->view();const auto transport=author?runtime_guest::observed_transport:runtime_host::observed_transport;
        const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto writes=std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores};
        const auto owner=n.scene_owner;const auto source=n.snapshot.source;unsigned calls=0;bool changed=false;
        const auto native_state=n.state;const auto initialization=n.scene_initialization;
        n.scene_status_callback=[&]{if(++calls!=getter)return;changed=true;
            if(expired==0)++const_cast<es::View&>(controller->view()).context.epoch;
            if(expired==1){if(author)++runtime_guest::observed_transport;else ++runtime_host::observed_transport;}
            if(expired==2)++content.root.instance;
            if(expired==3)incoming=content;
            if(expired==4)content.base_root=std::make_shared<const std::vector<uint8_t>>(*content.base_root);
            if(expired==5)content.bundle.hash.push_back('0');
            if(expired==6)++content.bindings.front().marker;
            if(expired==7){if(author)++runtime_guest::pending_export_request;else ++runtime_host::pending_export_request;}
            if(expired==8){if(author)runtime_guest::save_publication_pending=true;else runtime_host::save_publication_pending=true;}
            if(expired==9){if(author){runtime_guest::rejection_restore.emplace();runtime_guest::rejection_restore->serial=23;}
                else{runtime_host::rejection_restore.emplace();runtime_host::rejection_restore->serial=23;}}
            if(expired==10){if(author)runtime_guest::staged_capture=std::make_shared<runtime_guest::StagedCapture>();
                else runtime_host::staged_capture=std::make_shared<runtime_host::StagedCapture>();}
            if(expired==11)n.state.ready=false;
            if(expired==12)n.scene_initialization=3;
            if(expired==13)n.scene_enter_pending=true;
            if(expired==14)n.scene_transitioning=true;
            if(expired==15)n.state.world_visible=false;
            if(expired==16)n.scene_owner_available=false;
        };
        std::string error;const auto native=author?runtime_guest::current_native:runtime_host::current_native;
        const bool okay=author?runtime_guest::checkpoint_resources(saved,native,error):runtime_host::checkpoint_resources(saved,native,error);
        n.scene_status_callback={};CHECK(changed&&!okay&&!error.empty());
        CHECK(n.scene_owner==owner&&n.snapshot.source==source);
        CHECK(std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores}==writes);
        CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
        content=saved;incoming.reset();const_cast<es::View&>(controller->view())=view;
        if(author)runtime_guest::observed_transport=transport;else runtime_host::observed_transport=transport;
        if(author){runtime_guest::pending_export_request=0;runtime_guest::save_publication_pending=false;runtime_guest::rejection_restore.reset();runtime_guest::staged_capture.reset();}
        else{runtime_host::pending_export_request=0;runtime_host::save_publication_pending=false;runtime_host::rejection_restore.reset();runtime_host::staged_capture.reset();}
        n.state=native_state;n.scene_initialization=initialization;n.scene_enter_pending=n.scene_transitioning=false;n.scene_owner_available=true;
    }
}
static void test_authorized_attachment_can_advance_native_generation(){
    fixture_native_initialized=[](unsigned player){if(!player)native[player].bump_generation_on_restore=true;};
    {Party party(64,0xA5,false,false,false,1,0,false,0,false,true);fixture_native_initialized={};
        CHECK(native[0].state.generation>1&&runtime_host::native_generation==native[0].state.generation);
        CHECK(runtime_host::controller->view().phase==es::Phase::Ready&&runtime_host::store.healthy());
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
        // A pre-write owner does not silently gain authority over a new native
        // generation; that optional alias proof retires, without rejecting the
        // successful ordinary attachment.
        CHECK(!runtime_host::scene_alias);
    }
}
static void test_start_attachment_closes_the_registration_callback(){
    for(unsigned after_registration=0;after_registration<2;++after_registration){
        Party party(64,0xA5,false,false,false,1,0,false,0,false,true,true);side=0;auto& n=native[0];
        const auto owner=n.scene_owner;const auto source=n.snapshot.source;const auto writes=n.restores;unsigned calls=0;bool changed=false;
        auto expire=[&]{changed=true;++runtime_host::observed_transport;};
        if(after_registration)n.registration_callback=expire;
        else n.scene_status_callback=[&]{if(++calls==2)expire();};
        runtime_host::start_saved();n.scene_status_callback={};n.registration_callback={};
        CHECK(changed&&n.restores==writes&&n.registrations==after_registration);
        CHECK(n.scene_owner==owner&&n.snapshot.source==source&&!runtime_host::store.healthy());
        CHECK(runtime_host::controller->view().phase==es::Phase::Idle);
    }
}
static void test_join_attachment_closes_incoming_resource_getters(){
    for(unsigned after_registration=0;after_registration<2;++after_registration){
        unsigned gets=0;bool changed=false;
        fixture_native_initialized=[&](unsigned player){if(player!=1)return;
            if(after_registration)native[1].registration_callback=[&]{
                if(native[1].registrations!=2)return;changed=true;++runtime_guest::incoming_resources->root.instance;
            };
            else native[1].scene_status_callback=[&]{if(++gets!=2)return;changed=true;++runtime_guest::incoming_resources->root.instance;};
        };
        {Party party(64,0xA5,false,false,true,1,0,false,1,false,true);fixture_native_initialized={};
            CHECK(changed&&native[1].restores==0&&native[1].registrations==1+after_registration);
            CHECK(!runtime_guest::store.healthy()&&runtime_guest::controller->view().phase==es::Phase::Frozen);
            native[1].scene_status_callback={};native[1].registration_callback={};
        }
    }
}
static void test_alias_final_readiness_getter_cannot_publish_expired_context(){
    for(unsigned author=0;author<2;++author){
        Party party(64,0xA5,false,false,author==1,1,0,false,0,false,true);side=int(author);auto& n=native[author];
        auto* controller=author?runtime_guest::controller.get():runtime_host::controller.get();const auto view=controller->view();
        if(author)runtime_guest::queue_scene_alias_attachment(runtime_guest::pin_scene_alias_owner());
        else runtime_host::queue_scene_alias_attachment(runtime_host::pin_scene_alias_owner());
        const auto writes=std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores};unsigned gets=0;
        n.scene_status_callback=[&]{if(++gets==2)++const_cast<es::View&>(controller->view()).context.epoch;};
        ++game_frame;if(author)runtime_guest::service_scene_alias_attachment();else runtime_host::service_scene_alias_attachment();
        n.scene_status_callback={};CHECK(gets==2&&!(author?bool(runtime_guest::scene_alias):bool(runtime_host::scene_alias)));
        CHECK(std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores}==writes);
        const_cast<es::View&>(controller->view())=view;
    }
}
static void test_private_scene_alias_keeps_original_request_across_foreign_head(){
    seed_unrelated_placement=true;Party party(64,0xA5,false,false,false,1,0,false,0,false,true);seed_unrelated_placement=false;
    const auto root=runtime_host::resources.root;const auto actual=native[0].snapshot.source;
    begin_staged_pose(0,true);const auto operation=staged_original_request(0).operation;
    creation_field(1,unrelated_placement,{editor::Field::Position,0},editor::Vec3{11,12,13},{editor::ScopeKind::Placement,unrelated_placement,{}});
    CHECK(staged_held(0)&&runtime_host::controller->state().document.revision==1&&staged_original_request(0).base_revision==0);
    native[0].staged_pause=false;CHECK(spin([]{return staged_settled(2);}));
    CHECK(runtime_host::resources.root==root&&native[0].snapshot.source==actual);
    CHECK(runtime_host::controller->state().history.back().operation==operation);
    side=0;runtime_host::undo();CHECK(spin([]{return staged_settled(3);}));
    for(const auto& n:native){CHECK(n.snapshot.document.placements.at(placement).position==editor::Vec3{});
        CHECK(n.snapshot.document.placements.at(unrelated_placement).position==editor::Vec3{11,12,13});}
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}
static void test_private_scene_alias_saved_rotation_requires_a_new_checked_receipt(){
    Party party(64,0xA5,false,false,true,1,0,false,0,false,true);begin_staged_pose(1,true);
    const auto previous=runtime_guest::scene_alias;CHECK(previous);
    side=0;runtime_host::refresh();runtime_host::execute(runtime_host::Action::Save);CHECK(native[0].save_busy);
    native[0].persisted_bytes=body(native[0].snapshot.document,0x5A);
    complete_native_save(0,{saved_key.instance,saved_key.type,saved_key.group});
    CHECK(spin([]{return runtime_guest::resources.root==saved_key&&!runtime_host::save_publication_pending&&!staged_held(1)&&!rejection_held(1)&&runtime_guest::scene_alias;}));
    CHECK(runtime_guest::scene_alias->serial!=previous->serial&&!runtime_guest::scene_alias_current(previous));
    CHECK(runtime_guest::scene_alias->canonical_source==editor::ResourceKey{saved_key.instance,saved_key.type,saved_key.group});
    const auto actual=native[1].snapshot.source;begin_staged_pose(1);CHECK(spin([]{return staged_settled(1);}));
    CHECK(native[1].snapshot.source==actual&&runtime_guest::current_native.bytes.front()==0x5A);
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}
#endif
static void test_all_scene_alias_cases(){
    test_private_scene_alias_stages_without_changing_the_canonical_root();
#ifndef SPORECOOP_RUNTIME_ALIAS_BASELINE
    test_private_scene_alias_rejects_unattached_or_expired_identity();
    test_private_scene_alias_attachment_callback_cannot_authorize_a_replacement();
    test_scene_attachment_getters_cannot_authorize_changed_resources();
    test_start_attachment_closes_the_registration_callback();
    test_join_attachment_closes_incoming_resource_getters();
    test_alias_final_readiness_getter_cannot_publish_expired_context();
    test_authorized_attachment_can_advance_native_generation();
    test_private_scene_alias_keeps_original_request_across_foreign_head();
    test_private_scene_alias_saved_rotation_requires_a_new_checked_receipt();
#endif
}
