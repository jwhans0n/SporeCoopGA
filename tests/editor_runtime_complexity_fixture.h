// Real UDP Runtime policy coverage. Native code/tuning capture is isolated by
// the adapter double; the real Session authenticates and acknowledges proofs.
static bool complexity_both_ready(){
    return runtime_host::controller->view().complexity_ready&&runtime_guest::controller->view().complexity_ready;
}
static void complexity_toggle(unsigned author,bool enabled,editor::Revision revision){
    side=int(author);if(author)runtime_guest::set_extended_complexity(enabled);else runtime_host::set_extended_complexity(enabled);
    CHECK(spin([&]{return runtime_host::controller->state().document.revision==revision&&runtime_guest::controller->state().document.revision==revision&&
        !runtime_host::awaiting_operation&&!runtime_guest::awaiting_operation&&!runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_host::controller->settled();}));
    CHECK(editor::extended_complexity(runtime_host::controller->state().document)==enabled);
    CHECK(runtime_host::controller->state()==runtime_guest::controller->state());
    CHECK(runtime_host::store.durable_revision()==revision&&runtime_guest::store.durable_revision()==revision);
}
static void test_runtime_complexity_default_mismatch_and_exact_disable(){
    seed_complexity_settings=seed_guest_complexity_mismatch=true;Party party;
    seed_complexity_settings=seed_guest_complexity_mismatch=false;
    CHECK(spin([]{return !runtime_host::controller->view().complexity_ready&&!runtime_guest::controller->view().complexity_ready;}));
    const auto original=runtime_host::controller->state();const auto writes=std::array{native[0].restores,native[1].restores};
    side=0;runtime_host::set_extended_complexity(true);
    CHECK(spin([]{return !runtime_host::complexity_request;}));
    CHECK(runtime_host::controller->state()==original&&runtime_guest::controller->state()==original);
    CHECK(native[0].restores==writes[0]&&native[1].restores==writes[1]);
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    // Native default still accepts an ordinary owned gesture under mismatch.
    grant_tentative_placement(1);
    side=1;const auto default_samples=native[1].complexity_samples;CHECK(native[1].callbacks.gate({placement}));
    CHECK(native[1].complexity_samples==default_samples); // Native default adds no fresh-proof gate.
    capture_tentative_move(1);
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1&&runtime_guest::controller->state().document.revision==1&&
        !runtime_guest::awaiting_operation&&!runtime_host::durable_commit&&!runtime_guest::durable_commit;}));
    native[1].complexity=native[0].complexity;CHECK(spin(complexity_both_ready));
    complexity_toggle(0,true,2);
    CHECK(spin([]{return runtime_host::view().can_save&&runtime_guest::view().can_save;}));
    // Queue controls using their old enabled cache, then expire the local proof
    // before the next outside-frame drain executes those controls.
    side=1;runtime_guest::save();runtime_guest::playtest();runtime_guest::undo();runtime_guest::edit_acts(0);
    const auto published=native[1].complexity;
    native[1].complexity.words[3]=std::bit_cast<uint32_t>(2.f);
    CHECK(runtime_guest::controller->view().complexity_ready);
    const auto before_ratio=native[1].complexity_samples;
    CHECK(!native[1].callbacks.complexity_current(native[1].complexity));
    CHECK(native[1].callbacks.complexity_current(published));
    CHECK(native[1].complexity_samples==before_ratio); // Already-captured ratio proof never recurses into its getter.
    CHECK(!native[1].callbacks.gate({placement})&&!native[1].callbacks.terrain_gate({},{}));
    CHECK(!native[1].callbacks.navigation_allowed()&&!runtime_guest::current_gesture_scope().epoch);
    CHECK(runtime_guest::current_projection_scope().epoch&&runtime_guest::controller->view().complexity_ready);
    auto new_asset=creation_request(1,0x2399BE55);std::string request_error;
    CHECK(editor_creation::fixture_contexts[1].scope().has_value());
    CHECK(editor_creation::fixture_contexts[1].prepare(new_asset.scope,new_asset.request,request_error)==editor_creation::PrepareResult::Reject&&!request_error.empty());
    const auto before=runtime_host::controller->state();const auto saves=native[1].save_as_calls+native[1].save_calls;
    CHECK(spin([]{return runtime_host::complexity_held()&&runtime_guest::complexity_held();}));
    side=1;CHECK(!native[1].callbacks.gate({placement}));CHECK(!native[1].callbacks.structure_gate(UINT32_MAX));
    CHECK(!native[1].callbacks.terrain_gate({},{}));CHECK(!native[1].callbacks.navigation_allowed());
    CHECK(!native[1].callbacks.history_available(false)&&!native[1].callbacks.history_available(true));
    CHECK(native[1].callbacks.transaction_pending());
    CHECK(!runtime_guest::current_gesture_scope().epoch&&runtime_guest::current_projection_scope().epoch);
    CHECK(!runtime_guest::view().can_save&&!runtime_guest::view().can_playtest&&runtime_guest::view().can_complexity);
    CHECK(!runtime_guest::view().status.empty()&&runtime_guest::view().waiting);
    CHECK(runtime_host::controller->state()==before&&runtime_guest::controller->state()==before);
    CHECK(native[1].save_as_calls+native[1].save_calls==saves&&!native[1].active);
    CHECK(!runtime_guest::waiting_acts&&!runtime_guest::pending&&!runtime_guest::awaiting_operation);
    CHECK(spin([]{return !runtime_host::controller->view().members.at(0).presence.scene_ready&&
        !runtime_host::controller->view().members.at(1).presence.scene_ready;}));
    CHECK(native[0].complexity_samples&&native[1].complexity_samples);
    // Only the reserved singleton false Set can leave this hold.
    complexity_toggle(1,false,3);
    CHECK(!complexity_both_ready());
    CHECK(spin([]{return runtime_host::view().can_save&&runtime_guest::view().can_save;}));
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}
static void test_runtime_complexity_samples_while_host_creates(){
    seed_creation_settings=seed_complexity_settings=true;Party party;
    seed_creation_settings=seed_complexity_settings=false;CHECK(spin(complexity_both_ready));
    complexity_toggle(0,true,1);
    auto away=creation_request(0,0x2399BE55);creation_enter(away);
    const auto scene=native[0].snapshot.bytes;const auto restores=native[0].restores,samples=native[0].complexity_samples;
    native[0].complexity.words[5]=std::bit_cast<uint32_t>(3.f);
    CHECK(spin([]{return runtime_host::complexity_held()&&runtime_guest::complexity_held();}));
    CHECK(creation_active[0]&&!native[0].state.ready&&native[0].complexity_samples>samples);
    CHECK(native[0].snapshot.bytes==scene&&native[0].restores==restores);
    native[0].complexity=native[1].complexity;CHECK(spin(complexity_both_ready));
    grant_tentative_placement(1);capture_tentative_move(1);
    CHECK(spin([]{return runtime_host::controller->state().document.revision==2&&runtime_guest::controller->state().document.revision==2&&
        !runtime_guest::awaiting_operation&&!runtime_host::durable_commit&&!runtime_guest::durable_commit;}));
    CHECK(native[0].snapshot.bytes==scene&&native[0].restores==restores&&creation_active[0]);
    creation_return(away);
    CHECK(native[0].snapshot.document.placements.at(placement).position==editor::Vec3{3,2,1});
    CHECK(runtime_host::controller->state()==runtime_guest::controller->state());
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}
static void test_runtime_extended_baseline_waits_for_current_cohort(){
    seed_complexity_settings=seed_extended_complexity=seed_guest_complexity_mismatch=true;
    Party party(0,0xA5,false,false,false,1,0,false,0,true);
    seed_complexity_settings=seed_extended_complexity=seed_guest_complexity_mismatch=false;
    CHECK(!runtime_guest::native_attached&&native[1].restores==0);
    CHECK(!runtime_host::controller->view().members.at(1).ready);
    CHECK(runtime_host::complexity_held()&&!runtime_host::current_gesture_scope().epoch);
    native[1].complexity=native[0].complexity;
    CHECK(spin([]{return complexity_both_ready()&&runtime_guest::controller->view().phase==es::Phase::Ready&&
        runtime_host::controller->view().members.at(1).phase==es::Phase::Ready&&runtime_guest::native_attached;}));
    CHECK(native[1].restores==1&&runtime_guest::store.healthy());
    CHECK(runtime_host::controller->state()==runtime_guest::controller->state());
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}
static void test_runtime_extended_recovery_waits_for_local_proof(){
    seed_complexity_settings=seed_extended_complexity=true;Party party(0,0xA5,false,true);seed_complexity_settings=seed_extended_complexity=false;
    CHECK(spin(complexity_both_ready));const auto checkpoint=runtime_host::store.snapshot();
    const auto restores=native[0].restores;
    native[0].complexity={};side=0;runtime_host::frozen=true;runtime_host::recover();
    CHECK(spin([]{return runtime_host::recovery.has_value();}));
    for(unsigned i=0;i<5;++i)step();
    CHECK(!runtime_host::recovery->restored&&!runtime_host::recovery->controller_started);
    CHECK(native[0].restores==restores&&runtime_host::store.snapshot()==checkpoint);
    CHECK(!runtime_host::view().can_save&&!native[0].callbacks.navigation_allowed());
    native[0].complexity=fixture_complexity_capability();
    CHECK(spin([]{return !runtime_host::recovery&&runtime_host::controller->view().phase==es::Phase::Ready&&
        (runtime_guest::controller->view().phase==es::Phase::Ready||runtime_guest::view().join_prompt);},true,15000));
    // Follow the normal retained-scene policy: the owned shared scene can
    // rejoin directly, while a local-draft prompt must be resolved explicitly.
    if(runtime_guest::view().join_prompt){side=1;runtime_guest::resolve_join(1);}
    CHECK(spin([]{return runtime_guest::controller->view().phase==es::Phase::Ready&&complexity_both_ready();}));
    CHECK(runtime_host::controller->state().document.epoch!=checkpoint.epoch);
    CHECK(runtime_host::store.snapshot().epoch==runtime_host::controller->view().context.epoch);
    CHECK(runtime_host::controller->state()==runtime_guest::controller->state());
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
}
static void test_runtime_complexity_diagnostic_uses_owned_values(){
    seed_complexity_settings=true;Party party;seed_complexity_settings=false;CHECK(spin(complexity_both_ready));
    for(unsigned author=0;author<2;++author){
        side=int(author);auto& n=native[author];auto& controller=author?runtime_guest::controller:runtime_host::controller;
        auto& observed=author?runtime_guest::observed_transport:runtime_host::observed_transport;
        auto& failed=author?runtime_guest::complexity_publication_failed:runtime_host::complexity_publication_failed;
        auto& local_error=author?runtime_guest::complexity_local_error:runtime_host::complexity_local_error;
        const auto state=controller->state();const auto checkpoint=(author?runtime_guest::store:runtime_host::store).snapshot();
        const auto samples=n.complexity_samples,captures=n.capture_calls,restores=n.restores,validations=n.validations;
        const auto task_count=tasks[author].size();const auto current_words=n.complexity.words;
        n.state_callback=[]{throw std::runtime_error("The passive complexity diagnostic called Native state");};
        n.complexity.words[6]=std::bit_cast<uint32_t>(9.f);
        const auto diagnostic=[&]{return author?runtime_guest::debug({"complexity"}):runtime_host::debug({"complexity"});};
        const auto text=diagnostic();CHECK(text.find("context_current=1")!=std::string::npos&&text.find("ready=1")!=std::string::npos);
        CHECK(n.callbacks.canonical_revision()==state.document.revision);
        CHECK(text.find("policy=00000002 formula=DC04DB80 available=1 reason=0")!=std::string::npos);
        CHECK(text.find("41100000")==std::string::npos&&std::count(text.begin(),text.end(),',')==12&&text.size()<1536);
        CHECK(text.find("claim_serial=0")==std::string::npos);
        ++observed;CHECK(diagnostic().find("context_current=0")!=std::string::npos);--observed;
        auto& view=const_cast<es::View&>(controller->view());++view.context.epoch;
        CHECK(diagnostic().find("context_current=0")!=std::string::npos&&!n.callbacks.canonical_revision());--view.context.epoch;
        auto& document=const_cast<editor::Document&>(controller->state().document);++document.revision;
        CHECK(n.callbacks.canonical_revision()==document.revision);--document.revision;
        ++view.local.generation;CHECK(diagnostic().find("context_current=0")!=std::string::npos);--view.local.generation;
        failed=true;local_error="first\nsecond\rthird\tfourth";
        const auto held=diagnostic();CHECK(held.find("ready=0")!=std::string::npos&&held.find("error=first second third fourth")!=std::string::npos);
        CHECK(held.find('\n')==std::string::npos&&held.find('\r')==std::string::npos);failed=false;local_error.clear();
        n.complexity.words=current_words;n.state_callback={};
        CHECK(n.complexity_samples==samples&&n.capture_calls==captures&&n.restores==restores&&n.validations==validations&&tasks[author].size()==task_count);
        CHECK(controller->state()==state&&(author?runtime_guest::store:runtime_host::store).snapshot()==checkpoint);
    }
}
static void test_all_runtime_complexity_cases(){
    run_case("complexity_default_mismatch_and_exact_disable",test_runtime_complexity_default_mismatch_and_exact_disable);
    run_case("complexity_samples_while_host_creates",test_runtime_complexity_samples_while_host_creates);
    run_case("extended_baseline_waits_for_current_cohort",test_runtime_extended_baseline_waits_for_current_cohort);
    run_case("extended_recovery_waits_for_local_proof",test_runtime_extended_recovery_waits_for_local_proof);
    run_case("complexity_diagnostic_uses_owned_values",test_runtime_complexity_diagnostic_uses_owned_values);
}
