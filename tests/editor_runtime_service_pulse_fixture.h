static void test_runtime_service_pulse_reentry(){
    Party party;
    for(unsigned author=0;author<2;++author){
        side=int(author);auto& n=native[author];
        const auto scope=author?runtime_guest::service_pulse_scope():runtime_host::service_pulse_scope();
        CHECK(scope);const auto queries=n.state_queries;
        CHECK(author?runtime_guest::service_pulse_idle():runtime_host::service_pulse_idle());
        CHECK(n.state_queries==queries); // Both scheduling queries are passive.
        // A local creation hold still permits detached canonical networking.
        if(author)runtime_guest::local_creation.emplace();else runtime_host::local_creation.emplace();
        CHECK((author?runtime_guest::service_pulse_scope():runtime_host::service_pulse_scope())==scope);
        if(author)runtime_guest::local_creation.reset();else runtime_host::local_creation.reset();
        auto& view=const_cast<es::View&>((author?runtime_guest::controller:runtime_host::controller)->view());
        const auto original=view;
        view.phase=es::Phase::Preparing;CHECK(!(author?runtime_guest::service_pulse_scope():runtime_host::service_pulse_scope()));
        view=original;++view.local.generation;CHECK(!(author?runtime_guest::service_pulse_scope():runtime_host::service_pulse_scope()));view=original;
        bool fired=false;
        n.state_callback=[&]{
            if(fired)return;fired=true;
            CHECK(author?runtime_guest::runtime_draining:runtime_host::runtime_draining);
            CHECK(!(author?runtime_guest::service_pulse_idle():runtime_host::service_pulse_idle()));
            const auto calls=n.state_queries;const auto frame=game_frame;
            if(author){runtime_guest::drain(nullptr);runtime_guest::tick(now_seconds());}
            else{runtime_host::drain(nullptr);runtime_host::tick(now_seconds());}
            CHECK(n.state_queries==calls&&game_frame==frame);
            CHECK(!(author?runtime_guest::queued:runtime_host::queued)); // no self-post spin
        };
        if(author)runtime_guest::drain(nullptr);else runtime_host::drain(nullptr);
        n.state_callback={};CHECK(fired);
        CHECK(author?runtime_guest::service_pulse_idle():runtime_host::service_pulse_idle());
        n.service_busy=true;const auto before=n.state_queries;
        if(author)runtime_guest::drain(nullptr);else runtime_host::drain(nullptr);
        CHECK(n.state_queries==before&&!(author?runtime_guest::service_pulse_idle():runtime_host::service_pulse_idle()));
        CHECK((author?runtime_guest::service_pulse_scope():runtime_host::service_pulse_scope())==scope);
        n.service_busy=false;
        // The ordinary later service retries the queued state; no synthetic
        // frame is needed for eligibility and no actual frame fence changes.
        const auto frame=game_frame;
        if(author)runtime_guest::drain(nullptr);else runtime_host::drain(nullptr);
        CHECK(n.state_queries>before&&game_frame==frame);
        CHECK(!(author?runtime_guest::frozen:runtime_host::frozen));
    }
}
