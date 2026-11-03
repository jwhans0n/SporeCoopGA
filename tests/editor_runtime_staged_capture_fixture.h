// Native phase execution is controlled here; Runtime, authenticated UDP,
// Coordinator CAS, asynchronous Store and latest-scene restoration are real.
static bool staged_held(unsigned author){return author?bool(runtime_guest::staged_capture):bool(runtime_host::staged_capture);}
static editor::Request staged_original_request(unsigned author){return author?runtime_guest::staged_capture->request:runtime_host::staged_capture->request;}
static bool staged_settled(editor::Revision revision){return runtime_host::store.durable_revision()==revision && runtime_guest::store.durable_revision()==revision &&
    !runtime_host::durable_commit&&!runtime_guest::durable_commit&&!runtime_host::pending&&!runtime_guest::pending &&
    !runtime_host::staged_capture&&!runtime_guest::staged_capture&&runtime_host::controller->settled();}
static editor_native::CaptureTicket begin_staged_pose(unsigned author,bool paused=false,bool sealed=true){
    grant_tentative_placement(author);side=int(author);auto& n=native[author];
    editor_native::Gesture gesture;gesture.generation=n.state.generation;gesture.before=n.snapshot;gesture.after=gesture.before;
    gesture.after.document.placements.at(placement).position={3,2,1};gesture.after.bytes=body(gesture.after.document,gesture.before.bytes.front());
    static uint64_t serial=1000;auto owner=n.scene_owner;owner.source=n.snapshot.source;
    const editor_native::CaptureTicket ticket{n.callbacks.gesture_scope(),owner,n.state.generation,++serial,n.snapshot.act};
    std::string error;CHECK(ticket.scope.epoch&&n.callbacks.capture_begin(ticket,error)&&error.empty());
    n.snapshot=gesture.after;n.staged_ticket=ticket;n.staged_gesture=std::move(gesture);n.staged_phase=0;n.staged_frame=game_frame;
    n.staged_owners=2;n.staged_pause=paused;n.staged_unsealed=!sealed;n.state.gesture=true;
    CHECK(staged_held(author)&&n.callbacks.gesture_scope()==editor_native::GestureScope{}&&n.callbacks.projection_scope()==ticket.scope);
    CHECK(n.callbacks.capture_current(ticket)==editor_native::CaptureState::Ready);
    CHECK(n.callbacks.transaction_pending()&&!n.callbacks.gate({placement})&&!n.callbacks.navigation_allowed()&&!n.callbacks.history_available(false));
    if(author)runtime_guest::refresh();else runtime_host::refresh();
    CHECK(author?!runtime_guest::cached.can_save:!runtime_host::cached.can_save);
    return ticket;
}
static void test_staged_pose_uses_distinct_frames_and_one_owned_request(){
    for(unsigned author=0;author<2;++author)for(bool sealed:{false,true}){
        Party party;const auto ticket=begin_staged_pose(author,false,sealed);auto& n=native[author];
        const auto original=staged_original_request(author);const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto saves=n.save_calls+n.save_as_calls,opens=n.creation_opens;
        side=int(author);if(author){runtime_guest::save();runtime_guest::playtest();runtime_guest::create_asset(0x2B978C46,0);}
        else{runtime_host::save();runtime_host::playtest();runtime_host::create_asset(0x2B978C46,0);}
        for(unsigned same=0;same<3;++same){if(author)runtime_guest::pump_staged_capture();else runtime_host::pump_staged_capture();}
        CHECK(n.staged_phase==0&&n.staged_owners==2);
        CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
        CHECK(spin([&]{return staged_settled(1);}));
        CHECK(n.staged_frames.size()==6&&std::adjacent_find(n.staged_frames.begin(),n.staged_frames.end(),std::greater_equal<uint64_t>{})==n.staged_frames.end());
        CHECK(!n.staged_owners&&!n.staged_ticket&&n.save_calls+n.save_as_calls==saves&&n.creation_opens==opens);
        CHECK(runtime_host::controller->state().history.size()==1&&runtime_host::controller->state().history.front().operation==original.operation);
        CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
        CHECK(original.base_revision==0&&original.leases.size()==1&&original.operation.author==ticket.scope.author);
        for(const auto& live:native)CHECK(live.snapshot.document.placements.at(placement).position==editor::Vec3{3,2,1});
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_staged_pose_keeps_foreign_canonical_and_renews_original_lease(){
    for(unsigned author=0;author<2;++author){
        seed_unrelated_placement=true;Party party;seed_unrelated_placement=false;
        begin_staged_pose(author,true);auto& n=native[author];const auto original=staged_original_request(author);
        const auto native_after=n.snapshot;const auto restores=n.restores,applies=n.apply_calls;
        const auto initial_time=clock_ms;auto* local=author?runtime_guest::controller.get():runtime_host::controller.get();
        for(unsigned cycle=0;cycle<4;++cycle){clock_ms+=4000;for(unsigned i=0;i<12;++i)step();}
        CHECK(clock_ms>initial_time+15000&&staged_held(author));
        CHECK(staged_original_request(author).leases==original.leases);
        for(const auto& token:original.leases)CHECK(std::any_of(local->view().local_leases.begin(),local->view().local_leases.end(),[&](const auto& held){
            return held.scope==token.scope&&held.token==token.token&&held.expires>clock_ms;}));
        creation_field(1-author,unrelated_placement,{editor::Field::Position,0},editor::Vec3{11,12,13},{editor::ScopeKind::Placement,unrelated_placement,{}});
        CHECK(staged_held(author)&&local->state().document.revision==1&&n.snapshot.bytes==native_after.bytes&&n.restores==restores&&n.apply_calls==applies);
        CHECK((author?runtime_guest::rendered_revision:runtime_host::rendered_revision)==0);
        CHECK(staged_original_request(author).base_revision==0&&staged_original_request(author).operation==original.operation);
        n.staged_pause=false;CHECK(spin([&]{return staged_settled(2);}));
        CHECK(n.restores>restores&&!rejection_held(author));
        for(const auto& live:native){CHECK(live.snapshot.document.placements.at(placement).position==editor::Vec3{3,2,1});
            CHECK(live.snapshot.document.placements.at(unrelated_placement).position==editor::Vec3{11,12,13});}
        CHECK(local->state().history.back().operation==original.operation);
        side=int(author);if(author)runtime_guest::undo();else runtime_host::undo();
        CHECK(spin([]{return staged_settled(3);}));
        for(const auto& live:native){CHECK(live.snapshot.document.placements.at(placement).position==editor::Vec3{});
            CHECK(live.snapshot.document.placements.at(unrelated_placement).position==editor::Vec3{11,12,13});}
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_staged_pose_waits_if_c_validation_starts_foreign_durability(){
    for(unsigned observer=0;observer<2;++observer){
        seed_unrelated_placement=true;Party party;seed_unrelated_placement=false;
        const unsigned author=1-observer;grant_tentative_placement(author,unrelated_placement);
        begin_staged_pose(observer);auto& n=native[observer];const auto operation=staged_original_request(observer).operation;
        auto* foreign=author?runtime_guest::controller.get():runtime_host::controller.get();side=int(author);
        auto request=author?runtime_guest::request(editor::Intent::Forward):runtime_host::request(editor::Intent::Forward);
        editor::Mutation move;move.target=unrelated_placement;move.field={editor::Field::Position,0};move.value=editor::Vec3{11,12,13};
        move.expected=editor::field_revision(foreign->state().document,move.target,move.field);request.mutations={move};
        bool injected=false;n.staged_supported_callback=[&]{
            n.staged_supported_callback={};injected=true;side=int(author);std::string error;CHECK(foreign->submit(request,error));
            CHECK(spin([&]{return observer?bool(runtime_guest::durable_commit):bool(runtime_host::durable_commit);}));
            CHECK((observer?runtime_guest::controller:runtime_host::controller)->state().document.revision==0);
            side=int(observer);
        };
        CHECK(spin([&]{return injected;}));
        CHECK(staged_held(observer)&&!(observer?runtime_guest::pending.has_value():runtime_host::pending.has_value()));
        CHECK((observer?runtime_guest::staged_capture->failure:runtime_host::staged_capture->failure).empty());
        CHECK(spin([]{return staged_settled(2);}));
        CHECK(runtime_host::controller->state().history.back().operation==operation);
        for(const auto& live:native)CHECK(live.snapshot.document.placements.at(unrelated_placement).position==editor::Vec3{11,12,13});
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_staged_pose_unknown_after_mutation_restores_latest_without_commit(){
    for(unsigned author=0;author<2;++author){
        Party party;begin_staged_pose(author);auto& n=native[author];const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto canonical=(author?runtime_guest::controller:runtime_host::controller)->state();const auto restores=n.restores;
        n.staged_callback=[&]{if(n.staged_phase==5){n.snapshot.bytes.front()^=0x20;n.staged_callback={};}};
        CHECK(spin([&]{return !staged_held(author)&&!rejection_held(author)&&n.restores>restores;}));
        CHECK((author?runtime_guest::controller:runtime_host::controller)->state()==canonical);
        CHECK((author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
        CHECK(n.snapshot.document.placements.at(placement).position==editor::Vec3{}&&!n.staged_owners);
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_staged_pose_identity_expiry_never_writes_replacement(){
    for(unsigned author=0;author<2;++author)for(unsigned expired=0;expired<7;++expired){
        Party party;const auto ticket=begin_staged_pose(author,true);auto& n=native[author];
        auto* local=author?runtime_guest::controller.get():runtime_host::controller.get();const auto canonical=local->state();
        const auto checkpoint=author?runtime_guest::store.snapshot():runtime_host::store.snapshot();
        const auto writes=std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores};
        auto& view=const_cast<es::View&>(local->view());
        if(expired==0)++n.scene_owner.resource;if(expired==1)++n.snapshot.source.instance;if(expired==2)++n.state.generation;
        if(expired==3)++view.context.epoch;if(expired==4)++view.local.generation;
        if(expired==5){if(author)++runtime_guest::observed_transport;else ++runtime_host::observed_transport;}
        if(expired==6)n.scene_status_callback=[&]{++n.scene_owner.data;};
        ++game_frame;side=int(author);if(author)runtime_guest::pump_staged_capture();else runtime_host::pump_staged_capture();
        CHECK(author?runtime_guest::frozen&&runtime_guest::native_identity_lost:runtime_host::frozen&&runtime_host::native_identity_lost);
        CHECK(n.callbacks.capture_current(ticket)==editor_native::CaptureState::Retired&&!staged_held(author)&&!n.staged_owners);
        CHECK(writes==std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores});
        CHECK(local->state()==canonical&&(author?runtime_guest::store.snapshot():runtime_host::store.snapshot())==checkpoint);
    }
}
static void test_staged_pose_dependency_and_ticket_identity_are_immutable(){
    for(unsigned author=0;author<2;++author){
        Party party;auto& bundle=author?runtime_guest::resources.bundle:runtime_host::resources.bundle;
        const dbpf::Record dependency{{501,0x12345678,0x408A0000},{1,2,3,4}};bundle.records.push_back(dependency);
        const auto ticket=begin_staged_pose(author,true);auto& n=native[author];const auto original=staged_original_request(author);
        std::string error="old";side=int(author);CHECK(!n.callbacks.capture_begin(ticket,error)&&error.empty());
        auto orphan=*n.staged_gesture;orphan.capture=ticket;++orphan.capture->gesture;n.callbacks.gesture(std::move(orphan));
        CHECK(staged_original_request(author).operation==original.operation);
        bundle.records.push_back({{502,0x12345678,0x408A0000},{5,6}});
        CHECK(n.callbacks.capture_current(ticket)==editor_native::CaptureState::Ready); // Appending cannot change an old immutable record.
        auto found=std::find_if(bundle.records.begin(),bundle.records.end(),[&](const auto& row){return row.key==dependency.key;});
        CHECK(found!=bundle.records.end());found->data.back()^=1;
        CHECK(n.callbacks.capture_current(ticket)==editor_native::CaptureState::Retired);
        const auto writes=std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores};
        ++game_frame;if(author)runtime_guest::pump_staged_capture();else runtime_host::pump_staged_capture();
        CHECK(!staged_held(author)&&!n.staged_owners);
        CHECK(writes==std::array{n.restores,n.apply_calls,n.registrations,n.selections,n.view_restores});
        CHECK((author?runtime_guest::controller:runtime_host::controller)->state().document.revision==0);
    }
}
static void test_staged_pose_end_retires_owners_and_late_delivery(){
    for(unsigned author=0;author<2;++author){
        Party party;const auto ticket=begin_staged_pose(author,true);auto delayed=*native[author].staged_gesture;delayed.capture=ticket;
        side=int(author);if(author)runtime_guest::end();else runtime_host::end();
        CHECK(spin([&]{return !staged_held(author)&&!native[author].staged_owners&&
            (author?runtime_guest::controller:runtime_host::controller)->view().phase==es::Phase::Idle;}));
        const auto writes=std::array{native[author].restores,native[author].apply_calls,native[author].registrations};
        side=int(author);native[author].callbacks.gesture(std::move(delayed));
        CHECK(writes==std::array{native[author].restores,native[author].apply_calls,native[author].registrations});
        CHECK(!staged_held(author)&&!(author?runtime_guest::pending.has_value():runtime_host::pending.has_value()));
    }
}
static void test_staged_pose_blocks_export_after_native_owners_retire(){
    Party party;begin_staged_pose(0,true);auto& n=native[0];
    // Delivery may have retired the producer's owners while Runtime retains
    // its immutable gesture during canonical persistence or C retry.
    n.state.gesture=false;const auto transitions=n.transition_captures;const auto serial=runtime_host::terrain_transition_serial;
    unsigned native_queries=0;n.state_callback=[&]{++native_queries;};
    CHECK(!runtime_host::idle_native());
    side=0;auto requested=runtime_host::command(runtime_host::CommandKind::RequestExport,runtime_guest::controller->view().local);
    runtime_host::request_export(requested);
    CHECK(!runtime_host::exporting&&!native_queries&&n.transition_captures==transitions&&runtime_host::terrain_transition_serial==serial);
    CHECK(staged_held(0)&&!runtime_host::frozen&&runtime_host::controller->state().document.revision==0);
    n.state_callback={}; // Retire the stack counter before Party's teardown callbacks.
}
static void test_staged_guest_declines_authenticated_export_then_commits_original(){
    Party party;const auto ticket=begin_staged_pose(1,true);auto& n=native[1];const auto original=staged_original_request(1);
    n.state.gesture=false;const auto terrain=n.transition_captures;const auto saves=native[0].save_calls+native[0].save_as_calls;
    side=0;auto request=runtime_host::command(runtime_host::CommandKind::RequestExport,runtime_guest::controller->view().local);
    runtime_host::request_export(request); // Emit the real authenticated host barrier over UDP.
    CHECK(spin([]{return runtime_guest::staged_capture&&runtime_guest::staged_capture->rejected_export.has_value();}));
    CHECK(!runtime_guest::exporting&&n.transition_captures==terrain&&n.callbacks.capture_current(ticket)==editor_native::CaptureState::Wait);
    CHECK(runtime_guest::staged_capture->failure.empty()&&staged_original_request(1).operation==original.operation);
    const auto barrier=*runtime_guest::staged_capture->rejected_export;
    auto stale=barrier;stale.kind=runtime_guest::CommandKind::EndExport;++stale.serial;
    side=1;runtime_guest::receive_command(0,stale);
    CHECK(runtime_guest::staged_capture->rejected_export&&n.callbacks.capture_current(ticket)==editor_native::CaptureState::Wait);
    CHECK(spin([]{return !runtime_host::exporting&&runtime_guest::staged_capture&&!runtime_guest::staged_capture->rejected_export;}));
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen&&n.transition_captures==terrain&&native[0].save_calls+native[0].save_as_calls==saves);
    CHECK(staged_original_request(1).leases==original.leases&&n.callbacks.capture_current(ticket)==editor_native::CaptureState::Ready);
    n.state.gesture=true;n.staged_pause=false;CHECK(spin([]{return staged_settled(1);}));
    CHECK(runtime_host::controller->state().history.front().operation==original.operation);
    CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
    for(const auto& live:native)CHECK(live.snapshot.document.placements.at(placement).position==editor::Vec3{3,2,1});
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}
static void test_staged_prepare_waits_for_checked_rollback_with_stale_presence(){
    for(unsigned author=0;author<2;++author){
        Party party;begin_staged_pose(author,true);auto& n=native[author];
        const auto canonical=runtime_host::controller->state();const auto restores=n.restores,captures=n.draft_capture_attempts;
        // Host has not received the next Presence yet. This is deliberately
        // stale authoritative scene-ready metadata, not a forged wire peer.
        CHECK(runtime_host::controller->view().members.at(uint8_t(author)).presence.scene_ready);
        retain_fixture_logs=true;fixture_logs.clear();side=0;std::string error;
        CHECK(runtime_host::controller->prepare_playtest(error));
        runtime_host::controller->tick(clock_ms); // Host processes the stale view before the next Presence arrives.
        CHECK(spin([&]{return !staged_held(author)&&!rejection_held(author)&&n.restores>restores;}));
        CHECK(n.draft_capture_attempts>captures&&n.snapshot.document.placements.at(placement).position==editor::Vec3{});
        CHECK(runtime_host::controller->state()==canonical&&runtime_guest::controller->state()==canonical);
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
        CHECK(std::any_of(fixture_logs.begin(),fixture_logs.end(),[](const auto& text){return text.find("cancelled before playtest preparation")!=std::string::npos;}));
        retain_fixture_logs=false;fixture_logs.clear();
        if(author){
            CHECK(spin([]{return runtime_host::controller->view().phase==es::Phase::Preparing&&runtime_guest::controller->view().phase==es::Phase::Preparing;}));
        }else{
            // The host's own stale queued request is declined before global
            // lease revocation. A fresh request uses the now-proven baseline.
            CHECK(runtime_host::controller->view().phase==es::Phase::Ready);
            CHECK(spin([]{return runtime_host::cached.can_playtest;}));
            side=0;CHECK(runtime_host::controller->prepare_playtest(error));
            CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Preparing;}));
        }
        CHECK(!n.staged_owners&&!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_staged_pending_prepare_cannot_erase_rollback_receipt(){
    Party party;begin_staged_pose(1);auto& n=native[1];
    CHECK(spin([]{return runtime_guest::pending&&runtime_guest::pending->staged&&!runtime_guest::pending->submitted;}));
    const auto operation=runtime_guest::pending->request.operation;const auto canonical=runtime_host::controller->state();
    const auto restores=n.restores,captures=n.draft_capture_attempts;
    // Exercise the bounded network window where the host's member Presence
    // still names the last rendered scene even though delivery just completed.
    auto& member=const_cast<es::View&>(runtime_host::controller->view()).members.at(1);
    member.presence.scene_ready=true;member.presence.scene_revision=0;member.presence.pending=false;
    side=0;std::string error;CHECK(runtime_host::controller->prepare_playtest(error));
    runtime_host::controller->tick(clock_ms);
    CHECK(runtime_host::controller->view().phase==es::Phase::Preparing);
    CHECK(spin([&]{return runtime_host::controller->view().phase==es::Phase::Preparing&&runtime_guest::controller->view().phase==es::Phase::Preparing;}));
    CHECK(n.restores>restores&&n.draft_capture_attempts>captures&&!runtime_guest::pending&&!staged_held(1)&&!rejection_held(1));
    CHECK(runtime_host::controller->state().document==canonical.document&&runtime_guest::controller->state().document==canonical.document);
    CHECK(runtime_host::controller->state().history.empty()&&runtime_guest::controller->state().history.empty());
    CHECK(runtime_host::controller->state().decisions.contains(operation)&&!runtime_host::controller->state().decisions.at(operation).accepted());
    CHECK(n.snapshot.document.placements.at(placement).position==editor::Vec3{}&&!runtime_host::frozen&&!runtime_guest::frozen);
}
static void test_staged_guest_waits_for_checked_rollback_before_host_saved(){
    for(unsigned boundary=0;boundary<4;++boundary){
        Party party;begin_staged_pose(1,boundary==0||boundary==3);auto& n=native[1];std::string error;
        auto operation=runtime_guest::staged_capture->request.operation;
        if(boundary==1||boundary==2){
            CHECK(spin([]{return runtime_guest::pending&&runtime_guest::pending->staged&&!runtime_guest::pending->submitted;}));
            operation=runtime_guest::pending->request.operation;
            if(boundary==1)n.state.ready=false;
            else{
                ++game_frame;side=1;runtime_guest::submit_pending_gesture();
                CHECK(runtime_guest::pending->submitted&&runtime_guest::awaiting_operation==operation);
                runtime_guest::controller->tick(clock_ms);
            }
        }
        const auto checkpoint=runtime_guest::store.snapshot();const auto captures=n.draft_capture_attempts;
        n.snapshot_steps.clear();if(boundary==3)n.refuse_restore=true;
        // Exercise the ordinary host Save action and completed native callback,
        // not HoldExport or a direct invocation of the guest Saved callback.
        side=0;runtime_host::refresh();CHECK(runtime_host::cached.can_save);
        runtime_host::execute(runtime_host::Action::Save);CHECK(native[0].save_busy);
        native[0].snapshot.bytes=body(native[0].snapshot.document,0x5A);native[0].persisted_bytes=native[0].snapshot.bytes;
        complete_native_save(0,{saved_key.instance,saved_key.type,saved_key.group});
        CHECK(!runtime_host::frozen&&runtime_host::resources.root==saved_key);
        if(boundary==2){
            // Hold guest outside-frame work while authenticated Resources,
            // Saved, then the accepted request Decision are sent in order.
            CHECK(spin([]{return runtime_host::store.durable_revision()==1;},false));
            CHECK(runtime_guest::pending&&runtime_guest::pending->submitted&&runtime_guest::awaiting_operation==operation);
            CHECK(runtime_guest::store.snapshot()==checkpoint&&runtime_guest::controller->state().document.revision==0);
        }
        if(boundary==1){
            CHECK(spin([]{return runtime_guest::controller->accounting().deferred_count>0;}));
            CHECK(runtime_guest::pending&&!runtime_guest::pending->submitted&&runtime_guest::store.snapshot()==checkpoint);
            // Deliver the already authenticated deferred notice before the
            // next Runtime submission prelude. The separate submitted case
            // below covers the opposite scheduling order.
            n.state.ready=true;side=1;runtime_guest::controller->tick(clock_ms);
            CHECK(runtime_guest::pending&&!runtime_guest::pending->submitted&&runtime_guest::pending->barrier_cancel);
        }
        if(boundary==3){
            CHECK(spin([]{return runtime_guest::frozen;}));
            CHECK(runtime_guest::store.snapshot()==checkpoint&&runtime_guest::resources.root!=saved_key);
            CHECK(runtime_guest::rejection_restore&&runtime_guest::rejection_restore->failed);
            CHECK(!runtime_host::frozen);continue;
        }
        CHECK(spin([]{return runtime_guest::resources.root==saved_key&&!runtime_guest::pending_saved&&!runtime_host::save_publication_pending&&
            !runtime_guest::pending&&!staged_held(1)&&!rejection_held(1)&&!runtime_guest::durable_commit;}));
        const auto checked=std::find(n.snapshot_steps.begin(),n.snapshot_steps.end(),'C');
        CHECK(checked!=n.snapshot_steps.end()&&std::find(std::next(checked),n.snapshot_steps.end(),'R')!=n.snapshot_steps.end());
        CHECK(n.draft_capture_attempts>captures&&!n.staged_owners);
        CHECK(runtime_guest::current_native.source==editor::ResourceKey{saved_key.instance,saved_key.type,saved_key.group});
        CHECK(runtime_guest::current_native.bytes.front()==0x5A&&runtime_host::current_native.bytes.front()==0x5A);
        const auto revision=boundary==2?1u:0u;
        if(runtime_host::store.durable_revision()!=revision||runtime_guest::store.durable_revision()!=revision)
            std::printf("Saved boundary %u durable %llu/%llu expected %u\n",boundary,
                static_cast<unsigned long long>(runtime_host::store.durable_revision()),static_cast<unsigned long long>(runtime_guest::store.durable_revision()),revision);
        CHECK(runtime_host::store.durable_revision()==revision&&runtime_guest::store.durable_revision()==revision);
        CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
        if(boundary==2){
            CHECK(runtime_host::controller->state().decisions.at(operation).accepted());
            CHECK(runtime_host::controller->state().history.size()==1&&runtime_host::controller->state().history.front().operation==operation);
            CHECK(n.snapshot.document.placements.at(placement).position==editor::Vec3{3,2,1});
        }else{
            CHECK(runtime_host::controller->state().history.empty()&&runtime_guest::controller->state().history.empty());
            CHECK(n.snapshot.document.placements.at(placement).position==editor::Vec3{});
        }
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_staged_pose_host_away_and_explicit_creation_variant(){
    {
        seed_creation_settings=true;Party party;seed_creation_settings=false;
        auto creation=creation_request(0,0x2B978C46);creation_enter(creation);const auto restores=native[0].restores;
        begin_staged_pose(1);CHECK(spin([]{return staged_settled(1);}));
        CHECK(runtime_host::local_creation&&native[0].restores==restores&&!native[0].state.ready);
        creation_return(creation);CHECK(native[0].snapshot.document.placements.at(placement).position==editor::Vec3{3,2,1});
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
    for(unsigned author=0;author<2;++author){
        Party party;const auto canonical=(author?runtime_guest::controller:runtime_host::controller)->state();const auto writes=native[author].apply_calls;
        side=int(author);if(author)runtime_guest::create_asset(0x24682294,0x7D433FAD);else runtime_host::create_asset(0x24682294,0x7D433FAD);
        CHECK(spin([&]{return native[author].creation_opens==1;}));
        CHECK(native[author].creation_open_type==0x24682294&&native[author].creation_open_editor==0x7D433FAD);
        CHECK((author?runtime_guest::controller:runtime_host::controller)->state()==canonical&&native[author].apply_calls==writes);
    }
}
static void test_all_staged_capture_cases(){
    if(const char* selected=std::getenv("SPORECOOP_STAGED_BARRIER")){
        if(std::string(selected)=="export"){test_staged_guest_declines_authenticated_export_then_commits_original();return;}
        if(std::string(selected)=="prepare"){test_staged_prepare_waits_for_checked_rollback_with_stale_presence();return;}
        if(std::string(selected)=="pending"){test_staged_pending_prepare_cannot_erase_rollback_receipt();return;}
        if(std::string(selected)=="saved"){test_staged_guest_waits_for_checked_rollback_before_host_saved();return;}
    }
    test_staged_pose_uses_distinct_frames_and_one_owned_request();
    test_staged_pose_keeps_foreign_canonical_and_renews_original_lease();
    test_staged_pose_waits_if_c_validation_starts_foreign_durability();
    test_staged_pose_unknown_after_mutation_restores_latest_without_commit();
    test_staged_pose_identity_expiry_never_writes_replacement();
    test_staged_pose_dependency_and_ticket_identity_are_immutable();
    test_staged_pose_end_retires_owners_and_late_delivery();
    test_staged_pose_blocks_export_after_native_owners_retire();
    test_staged_guest_declines_authenticated_export_then_commits_original();
    test_staged_prepare_waits_for_checked_rollback_with_stale_presence();
    test_staged_pending_prepare_cannot_erase_rollback_receipt();
    test_staged_guest_waits_for_checked_rollback_before_host_saved();
    test_staged_pose_host_away_and_explicit_creation_variant();
}
