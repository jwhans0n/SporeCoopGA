// Real Runtime rejection/UDP/storage orchestration. The native supported-check
// double can fail after expiring its caller's identity, just like a callback.
static void test_captured_rejection_never_restores_a_replacement(){
    for(unsigned author=0;author<2;++author)for(unsigned replaced=0;replaced<12;++replaced){
        Party party;side=int(author);auto& n=native[author];
        auto& controller=author?runtime_guest::controller:runtime_host::controller;
        auto& store=author?runtime_guest::store:runtime_host::store;
        const auto canonical=controller->state();const auto checkpoint=store.snapshot();
        const auto restores=n.restores,applies=n.apply_calls,rebaselines=n.rebaselines;
        editor_native::Gesture gesture;gesture.generation=n.state.generation;gesture.before=n.snapshot;gesture.after=gesture.before;
        gesture.after.document.placements.at(placement).position={3,4,5};gesture.after.bytes=body(gesture.after.document);
        n.snapshot=gesture.after;
        n.failed_supported_callback=[&,replaced]{
            if(replaced==0)++n.state.generation;
            if(replaced==1)++n.snapshot.source.instance;
            if(replaced==2)++n.scene_owner.mode;
            if(replaced==3)++n.scene_owner.data;
            if(replaced==4)++n.scene_owner.resource;
            auto& view=const_cast<es::View&>(controller->view());
            if(replaced==5)++view.context.project[0];
            if(replaced==6)++view.context.epoch;
            if(replaced==7)++view.local.generation;
            if(replaced==8){if(author)++runtime_guest::observed_transport;else ++runtime_host::observed_transport;}
            if(replaced==9)sessions[author].leave("injected callback transport replacement");
            if(replaced==11)n.scene_owner_available=false;
        };
        if(author)runtime_guest::captured(std::move(gesture));else runtime_host::captured(std::move(gesture));
        n.failed_supported_callback={};
        CHECK(controller->state()==canonical&&store.snapshot()==checkpoint);
        CHECK(n.apply_calls==applies&&n.rebaselines==rebaselines);
        CHECK(author?!runtime_guest::pending&&!runtime_guest::awaiting_operation:!runtime_host::pending&&!runtime_host::awaiting_operation);
        const auto& status=author?runtime_guest::status:runtime_host::status;
        CHECK(status.find("Injected unsupported callback failure")!=std::string::npos);
        if(replaced==10){
            // Ordinary recoverable rejection still restores the committed pose.
            CHECK(n.restores==restores+1&&n.snapshot.document==canonical.document);
            CHECK(spin([&]{return author?!runtime_guest::rejection_restore:!runtime_host::rejection_restore;}));
            CHECK(author?!runtime_guest::frozen&&!runtime_guest::native_identity_lost:!runtime_host::frozen&&!runtime_host::native_identity_lost);
            CHECK(status=="The edit was rejected and the committed draft restored: Injected unsupported callback failure");
        }else{
            CHECK(n.restores==restores&&n.snapshot.document.placements.at(placement).position==editor::Vec3{3,4,5});
            CHECK(author?runtime_guest::frozen&&runtime_guest::native_identity_lost:runtime_host::frozen&&runtime_host::native_identity_lost);
            CHECK(status.find("left intact")!=std::string::npos);
            if(replaced==11)CHECK(status.find("Injected native scene owner unavailable")!=std::string::npos);
        }
    }
}
