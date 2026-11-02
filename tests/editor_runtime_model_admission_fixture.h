// Production runtime admission and UDP/durability boundaries, with the native
// resource manager isolated by the main fixture's model adapter double.
static void test_model_diagnostic_bounds_and_context(){
    Party party;side=0;
    const auto before=runtime_host::controller->state();const auto snapshot=native[0].snapshot;
    const auto calls=native[0].model_admission_calls;
    const auto report=runtime_host::debug({"model","40646200!2912711D.24682294"});
    CHECK(report.find("key=40646200!2912711D.24682294")!=std::string::npos);
    CHECK(report.find("mask=00000001,00000002,00000004,00000008")!=std::string::npos);
    CHECK(native[0].model_admission_calls==calls+1);
    for(const auto& args:std::vector<std::vector<std::string>>{{"model"},{"model","../unsafe"},
        {"model","40646200!2912711D.2468229G"},{"model","40646200!2912711D.24682294","extra"}}){
        CHECK(runtime_host::debug(args).find("Use coop.editor model")!=std::string::npos);
    }
    CHECK(native[0].model_admission_calls==calls+1);
    side=1;CHECK(runtime_guest::debug({"model","40646200!2912711D.24682294"}).find("key=40646200")!=std::string::npos);
    CHECK(native[1].model_admission_calls==1);
    side=0;runtime_host::frozen=true;
    CHECK(runtime_host::debug({"model","40646200!2912711D.24682294"}).find("not settled")!=std::string::npos);
    CHECK(native[0].model_admission_calls==calls+1);runtime_host::frozen=false;
    native[0].model_admission_callback=[] {++native[0].state.generation;};
    CHECK(runtime_host::debug({"model","40646200!2912711D.24682294"}).find("error=Injected stale")!=std::string::npos);
    CHECK(runtime_host::controller->state()==before);
    CHECK(native[0].snapshot.bytes==snapshot.bytes&&native[0].snapshot.bindings==snapshot.bindings);
}
static void test_model_admission_preserves_class_wide_act_scope(){
    seed_creation_settings=true;Party party;seed_creation_settings=false;side=0;
    const auto before=runtime_host::controller->state().document;auto after=before;
    editor::Change change;change.operation={runtime_host::controller->view().local,100,before.epoch};
    auto extra=after.class_settings.begin()->second;extra.object.id+=1000;extra.act_id.id+=1;
    after.class_settings.emplace(extra.object,extra);
    std::string error;const auto calls=native[0].model_admission_calls;
    CHECK(runtime_host::admit_class_models(before,after,change,error));
    CHECK(native[0].model_admission_calls==calls);
    const editor::ResourceKey replacement{0x7D123456,0x24682294,0x40646200};
    for(auto& [id,row]:after.class_settings){(void)id;row.properties[editor::kClassAssetResourceProperty]=replacement;}
    CHECK(runtime_host::admit_class_models(before,after,change,error));
    CHECK(native[0].model_admission_calls==calls+1);
    CHECK(runtime_host::controller->state().document==before);
    for(const auto type:{0x438F6347u,0x3D97A8E4u}){
        auto invalid=before;invalid.class_settings.begin()->second.properties[editor::kClassAssetResourceProperty]=editor::ResourceKey{7,type,0x40646200};
        const auto snapshot=runtime_host::store.snapshot();const auto applies=native[0].apply_calls;
        CHECK(!runtime_host::admit_class_models(before,invalid,change,error)&&!error.empty());
        CHECK(native[0].model_admission_calls==calls+1&&native[0].apply_calls==applies);
        CHECK(runtime_host::controller->state().document==before&&runtime_host::store.snapshot()==snapshot);
    }
}
static void test_host_model_rejection_keeps_canonical_allocation_and_history(){
    for(unsigned author=0;author<2;++author){
        seed_creation_settings=true;creation_content_enabled=true;creation_version=1;Party party;seed_creation_settings=false;
        const auto before=runtime_host::controller->state();
        const auto host_snapshot=runtime_host::current_native,guest_snapshot=runtime_guest::current_native;
        const auto host_applies=native[0].apply_calls;
        auto value=creation_request(author,0x24682294);creation_enter(value);
        native[0].model_admission_reject=true;
        std::string failure;creation_return(value,false,&failure);
        CHECK(failure.find("model admission rejection")!=std::string::npos);
        CHECK(spin([&]{return runtime_host::controller->settled()&&runtime_host::controller->state().document==before.document&&
            runtime_guest::controller->state().document==before.document;}));
        CHECK(runtime_host::controller->state().history==before.history);
        CHECK(runtime_guest::controller->state().history==before.history);
        CHECK(runtime_host::store.durable_revision()==before.document.revision&&runtime_guest::store.durable_revision()==before.document.revision);
        CHECK(runtime_host::current_native.bytes==host_snapshot.bytes&&runtime_guest::current_native.bytes==guest_snapshot.bytes);
        CHECK(native[0].apply_calls==host_applies);
        CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
        if(author)CHECK(native[1].model_admission_calls&&native[0].model_admission_calls);
        native[0].model_admission_reject=false;
        auto retry=creation_request(author,0x24682294);creation_enter(retry);creation_return(retry,false);
        CHECK(spin([&]{return runtime_host::controller->settled()&&
            runtime_host::controller->state().document==runtime_guest::controller->state().document&&
            runtime_host::controller->state().history==runtime_guest::controller->state().history;}));
        const auto& committed=runtime_host::controller->state();
        CHECK(committed.document.revision==before.document.revision+1&&committed.document.classes.size()==before.document.classes.size()+1);
        CHECK(committed.document.next_class==before.document.next_class+1&&committed.history.size()==before.history.size()+1);
        CHECK(committed.document.classes.contains({(2ull<<32)|before.document.next_class,1}));
        CHECK(runtime_host::store.durable_revision()==committed.document.revision&&runtime_guest::store.durable_revision()==committed.document.revision);
        creation_content_enabled=false;
    }
}
static void test_model_admission_fences_callbacks_before_native_apply(){
    seed_creation_settings=true;Party party;seed_creation_settings=false;side=0;
    const auto before=runtime_host::controller->state().document;auto after=before;
    after.class_settings.begin()->second.properties[editor::kClassAssetResourceProperty]=editor::ResourceKey{0x7D123456,0x24682294,0x40646200};
    editor::Change change;change.operation={runtime_host::controller->view().members.at(1).peer,101,before.epoch};
    std::string error;const auto applies=native[0].apply_calls,restores=native[0].restores;
    native[0].model_admission_callback=[] {++native[0].state.generation;};
    CHECK(!runtime_host::admit_class_models(before,after,change,error));
    CHECK(!error.empty()&&native[0].model_admission_calls==1);
    CHECK(native[0].apply_calls==applies&&native[0].restores==restores);
    CHECK(runtime_host::controller->state().document==before&&runtime_host::store.durable_revision()==before.revision);
}
static void test_all_model_admission_runtime_cases(){
    test_model_diagnostic_bounds_and_context();
    test_model_admission_preserves_class_wide_act_scope();
    test_host_model_rejection_keeps_canonical_allocation_and_history();
    test_model_admission_fences_callbacks_before_native_apply();
}
