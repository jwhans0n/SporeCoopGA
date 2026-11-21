// Real Session/UDP/Store sequencing; only the Native private owner is modeled.
static editor::Request pose_commit_request(unsigned author,editor::ObjectId object=placement,float x=41){
    side=int(author);auto* c=author?runtime_guest::controller.get():runtime_host::controller.get();
    auto r=author?runtime_guest::request(editor::Intent::Forward):runtime_host::request(editor::Intent::Forward);
    r.summary="Experimental staged pose";editor::Mutation m;m.target=object;m.field={editor::Field::Position,0};m.value=editor::Vec3{x,2,3};
    m.expected=editor::field_revision(c->state().document,object,m.field);r.mutations={m};return r;
}
static void enable_pose_commit(){
    runtime_host::pose_prepare_experiment=runtime_guest::pose_prepare_experiment=true;
    // The generic test codec serializes logical project/epoch fields that the
    // genuine Native writer omits. Seed the explicit fixed-point baseline.
    for(unsigned p=0;p<2;++p)if(!creation_active[p]){
        const auto source=native[p].snapshot.source;
        native[p].snapshot=p?runtime_guest::current_native:runtime_host::current_native;native[p].snapshot.source=source;
    }
}
static void test_pose_commit_later_frame_and_durable_order(){
    for(unsigned author=0;author<2;++author){
        Party party;grant_tentative_placement(author);enable_pose_commit();
        const auto baseline=runtime_host::store.snapshot();const auto applies=native[0].apply_calls;
        std::string error;side=int(author);auto* c=author?runtime_guest::controller.get():runtime_host::controller.get();
        CHECK(c->submit(pose_commit_request(author),error));
        CHECK(spin([]{return bool(runtime_host::native_pose_prepare);}));
        CHECK(!runtime_host::durable_commit && runtime_host::controller->state().document.revision==0 && runtime_host::store.snapshot()==baseline);
        CHECK(native[0].apply_calls==applies && native[0].pose_begins==1 && native[0].pose_base);
        const auto frame=game_frame;const auto finishes=native[0].pose_finishes;
        for(unsigned i=0;i<4;++i){side=0;runtime_host::drain(nullptr);CHECK(game_frame==frame && native[0].pose_finishes==finishes && !runtime_host::durable_commit);}
        side=0;CHECK(!runtime_host::idle_native() && native[0].callbacks.transaction_pending());
        CHECK(native[0].callbacks.gesture_scope()==editor_native::GestureScope{} && native[0].callbacks.projection_scope().epoch);
        CHECK(runtime_host::controller->poll_persistence(clock_ms)); // Transport pump only; no Native/worker phase is advanced.
        CHECK(native[0].pose_finishes==finishes && !runtime_host::durable_commit);
        CHECK(spin([]{return bool(runtime_guest::native_pose_prepare);}));
        CHECK(runtime_host::controller->state().document.revision==1 && runtime_host::store.durable_revision()==1);
        CHECK(runtime_guest::controller->state().document.revision==0 && runtime_guest::store.durable_revision()==0 && !runtime_guest::durable_commit);
        CHECK(spin([]{return runtime_host::controller->state().document.revision==1 && runtime_guest::controller->state().document.revision==1 && !runtime_guest::durable_commit;}));
        CHECK(runtime_host::controller->state()==runtime_guest::controller->state());
        for(unsigned p=0;p<2;++p){
            CHECK(native[p].pose_begins==1 && native[p].pose_checked_applies==1 && !native[p].pose_base);
            CHECK(native[p].snapshot.document.placements.at(placement).position==editor::Vec3{41,2,3});
        }
        CHECK(!runtime_host::native_pose_prepare && !runtime_guest::native_pose_prepare && !runtime_host::frozen && !runtime_guest::frozen);
    }
}
static void test_pose_commit_cancel_stale_and_partial_failure(){
    for(unsigned failure=0;failure<6;++failure){
        Party party;grant_tentative_placement(0);enable_pose_commit();
        const auto snapshot=native[0].snapshot;const auto state=runtime_host::controller->state();const auto stored=runtime_host::store.snapshot();
        const auto directory=runtime_host::project_directory;
        std::vector<uint8_t> restart_envelope;std::string encoding_error;CHECK(editor_project::encode_resources(runtime_host::resources,restart_envelope,encoding_error));
        std::string error;side=0;CHECK(runtime_host::controller->submit(pose_commit_request(0),error));
        CHECK(spin([]{return bool(runtime_host::native_pose_prepare);}));
        const auto applies=native[0].apply_calls,restores=native[0].restores;
        if(failure==0){side=0;runtime_host::controller->end();}
        if(failure==1){native[0].scene_owner.resource+=4;++game_frame;side=0;runtime_host::drain(nullptr);}
        if(failure==2){native[0].snapshot.bytes[0]^=1;++game_frame;side=0;runtime_host::drain(nullptr);}
        if(failure==3){native[0].partially_apply=true;++game_frame;side=0;runtime_host::drain(nullptr);native[0].partially_apply=false;}
        if(failure==4){clock_ms+=6000;side=0;runtime_host::drain(nullptr);}
        if(failure==5){
            native[0].apply_callback=[] {native[0].apply_callback={};runtime_host::controller->end();};
            ++game_frame;side=0;runtime_host::drain(nullptr);
        }
        CHECK(!runtime_host::native_pose_prepare && !native[0].pose_base && !runtime_host::durable_commit);
        if(failure==5){
            editor_store::Store reopened;CHECK(reopened.open(directory.parent_path(),stored.project,error));CHECK(reopened.snapshot()==stored);reopened.close();
        }else if(runtime_host::store.snapshot()!=stored){
            const auto& value=runtime_host::store.snapshot();
            std::printf("pose negative=%u store revision=%llu/%llu native_equal=%u document_equal=%u mapping_equal=%u manifest_equal=%u metadata_equal=%u phase=%u status=%s\n",failure,
                static_cast<unsigned long long>(stored.revision),static_cast<unsigned long long>(value.revision),unsigned(value.native_snapshot==stored.native_snapshot),
                unsigned(value.document==stored.document),unsigned(value.id_mapping==stored.id_mapping),unsigned(value.manifest==stored.manifest),unsigned(value.metadata==stored.metadata),
                unsigned(runtime_host::controller->view().phase),runtime_host::status.c_str());std::fflush(stdout);
        }
        if(failure!=5)CHECK(runtime_host::store.snapshot()==stored);
        if(failure==0 || failure==5){
            CHECK(runtime_host::controller->view().phase==es::Phase::Idle && native[0].snapshot.bytes==snapshot.bytes);
            CHECK(runtime_host::controller->start(state,std::move(restart_envelope),"Restart after cancelled pose",error));
        }else CHECK(runtime_host::controller->state().document==state.document);
        if(failure==3 || failure==5){CHECK(native[0].pose_checked_applies==1 && native[0].restores>restores);CHECK(native[0].snapshot.bytes==snapshot.bytes);}
        else if(failure!=2){CHECK(native[0].apply_calls==applies && native[0].restores==restores);}
    }
}
static void test_pose_commit_queued_foreign_edit_and_default_fallback(){
    seed_unrelated_placement=true;
    { Party party;seed_unrelated_placement=false;
      grant_tentative_placement(0);grant_tentative_placement(1,unrelated_placement);enable_pose_commit();
      std::string error;side=0;CHECK(runtime_host::controller->submit(pose_commit_request(0),error));
      CHECK(spin([]{return bool(runtime_host::native_pose_prepare);}));
      side=1;CHECK(runtime_guest::controller->submit(pose_commit_request(1,unrelated_placement,53),error));
      CHECK(spin([]{return runtime_host::controller->state().document.revision==2 && runtime_guest::controller->state().document.revision==2 && !runtime_guest::durable_commit;}));
      CHECK(runtime_host::controller->state()==runtime_guest::controller->state());
      CHECK(runtime_host::controller->state().history.size()==2);
      for(const auto& n:native){CHECK(n.snapshot.document.placements.at(placement).position==editor::Vec3{41,2,3});CHECK(n.snapshot.document.placements.at(unrelated_placement).position==editor::Vec3{53,2,3});}
    }
    for(unsigned fallback=0;fallback<3;++fallback){
        Party party;grant_tentative_placement(0);
        runtime_host::pose_prepare_experiment=runtime_guest::pose_prepare_experiment=bool(fallback);
        native[0].pose_fallback=native[1].pose_fallback=fallback==1;
        const auto before=native[0].prepare_calls;std::string error;side=0;
        CHECK(runtime_host::controller->submit(pose_commit_request(0),error));
        CHECK(spin([]{return runtime_host::controller->state().document.revision==1 && runtime_guest::controller->state().document.revision==1 && !runtime_guest::durable_commit;}));
        CHECK(native[0].prepare_calls==before+1 && native[0].pose_checked_applies==0 && native[1].pose_checked_applies==0);
        CHECK(native[0].pose_begins==unsigned(fallback!=0) && native[1].pose_begins==unsigned(fallback!=0));
        CHECK(!runtime_host::native_pose_prepare && !runtime_guest::native_pose_prepare);
    }
}
static void test_pose_commit_host_away_falls_back(){
    seed_creation_settings=true;Party party;seed_creation_settings=false;
    auto away=creation_request(0,0x2B978C46,class_id);creation_enter(away);
    grant_tentative_placement(1);enable_pose_commit();
    const auto live=native[0].snapshot.bytes;const auto writes=native[0].apply_calls;
    std::string error;side=1;CHECK(runtime_guest::controller->submit(pose_commit_request(1),error));
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1 && runtime_guest::controller->state().document.revision==1 && !runtime_guest::durable_commit;}));
    CHECK(native[0].pose_begins==0 && native[0].apply_calls==writes && native[0].snapshot.bytes==live);
    CHECK(native[1].pose_begins==1 && native[1].pose_checked_applies==1 && !runtime_host::frozen && !runtime_guest::frozen);
    creation_return(away);
}
static void test_pose_commit_preserves_fresh_local_save_sidecars(){
    Party party;grant_tentative_placement(0);enable_pose_commit();
    std::string error;side=0;CHECK(runtime_host::controller->submit(pose_commit_request(0),error));
    CHECK(spin([]{return bool(runtime_host::native_pose_prepare);}));
    native[0].snapshot.local_dirty=editor_native::Snapshot::Dirty{77,false,true};
    native[1].snapshot.local_dirty=editor_native::Snapshot::Dirty{88,false,false};
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1 && runtime_guest::controller->state().document.revision==1 && !runtime_guest::durable_commit;}));
    CHECK(native[0].pose_checked_applies==1 && native[1].pose_checked_applies==1);
    CHECK(runtime_host::current_native.local_dirty && runtime_host::current_native.local_dirty->saved_revision==77 && runtime_host::current_native.local_dirty->imported && runtime_host::current_native.local_dirty->dirty);
    CHECK(runtime_guest::current_native.local_dirty && runtime_guest::current_native.local_dirty->saved_revision==88 && !runtime_guest::current_native.local_dirty->imported && runtime_guest::current_native.local_dirty->dirty);
}
static void test_pose_commit_unsupported_terrain_keeps_original_route(){
    Party party;enable_pose_commit();
    const auto before=native[0].prepare_calls;
    side=0;auto request=runtime_host::request(editor::Intent::Forward);request.summary="Terrain stays on original apply route";
    editor::Mutation m;m.kind=editor::MutationKind::AppendTerrain;m.terrain=inverse_stamp(513);
    m.terrain->object={};m.terrain->order=0;m.terrain->revision=0;request.mutations={m};
    std::string error;CHECK(runtime_host::controller->submit(request,error));
    CHECK(spin([]{return runtime_host::controller->state().document.revision==1 && runtime_guest::controller->state().document.revision==1 && !runtime_guest::durable_commit;}));
    CHECK(native[0].pose_begins==0 && native[1].pose_begins==0 && native[0].prepare_calls==before+1);
    CHECK(runtime_host::controller->state()==runtime_guest::controller->state() && runtime_host::controller->state().document.terrain.size()==1);
}
static void test_prepared_native_callback_self_reset_lifetime(){
    Party party;std::string error;auto request=pose_commit_request(0);
    editor::Coordinator core;CHECK(core.reset(runtime_host::controller->state(),request.epoch,error));CHECK(core.connect(request.operation.author));
    auto lease=core.acquire(request.operation.author,{{editor::ScopeKind::Placement,placement,{}}},1,15000);CHECK(lease.accepted());
    request.leases={{lease.leases[0].scope,lease.leases[0].token}};
    editor::Decision decision;CHECK(core.prepare(request,1,{},decision,error));
    bool alive=false;struct Token {bool& flag;Token(bool& value):flag(value){flag=true;}~Token(){flag=false;}};
    auto token=std::make_shared<Token>(alive);std::weak_ptr<Token> weak=token;editor::Hooks::Native hook;
    hook=[token,&hook,&alive](const auto&,const auto&,const auto&,std::string&){auto* slot=&hook;auto* flag=&alive;*slot={};return *flag;};token.reset();
    CHECK(core.apply_prepared_native(hook,error));CHECK(weak.expired()&&!alive&&!hook);
    CHECK(core.finish_prepared(true).accepted());
}
static void test_native_pose_commit_experiment(){
    test_pose_commit_later_frame_and_durable_order();
    test_pose_commit_cancel_stale_and_partial_failure();
    test_pose_commit_queued_foreign_edit_and_default_fallback();
    test_pose_commit_host_away_falls_back();
    test_pose_commit_preserves_fresh_local_save_sidecars();
    test_pose_commit_unsupported_terrain_keeps_original_route();
    test_prepared_native_callback_self_reset_lifetime();
}
