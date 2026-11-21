// Native +80/+84/+85 are local sidecars, not serialized change authority.
// Model a clean completed Save, then use real Runtime/Session/UDP/Store paths.
static void dirty_saved_baseline(){
    CHECK(spin([]{const auto saved=runtime_host::controller->saved_coverage();return saved.current&&saved.applied==2&&saved.expected==2&&runtime_host::controller->settled();}));
    CHECK(runtime_host::native_copy_owned);
    for(unsigned p=0;p<2;++p){
        const editor_native::Snapshot::Dirty clean{41,false,false};
        native[p].snapshot.local_dirty=clean;native[p].require_native_dirty_for_save=true;native[p].native_saved_history=41;
        (p?runtime_guest::current_native:runtime_host::current_native).local_dirty=clean;
    }
}
static void check_dirty_sidecar(const editor_native::Snapshot& value,bool dirty){
    CHECK(value.local_dirty&&value.local_dirty->saved_revision==41&&value.local_dirty->dirty==dirty&&!value.local_dirty->imported);
}
static void dirty_save_request(bool allowed){
    side=0;const auto saves=native[0].save_calls,copy=native[0].save_as_calls;
    runtime_host::status.clear();native[0].save_callbacks.request(editor_native_save::Request::Save);
    if(allowed){
        CHECK(spin([]{return native[0].save_busy;}));
        CHECK(native[0].save_calls==saves+1&&native[0].save_as_calls==copy);
        native[0].snapshot.local_dirty=editor_native::Snapshot::Dirty{41,false,false};
        native[0].persisted_bytes=native[0].snapshot.bytes;
        complete_native_save(0,{102,0x366A930D,0x408A0000});
        CHECK(spin([]{const auto saved=runtime_host::controller->saved_coverage();return saved.current&&saved.applied==2&&saved.expected==2&&!runtime_host::save_publication_pending;}));
        check_dirty_sidecar(runtime_host::current_native,false);check_dirty_sidecar(native[0].snapshot,false);
    }else{
        CHECK(spin([]{return runtime_host::status.find("reason 5")!=std::string::npos;}));
        CHECK(!native[0].save_busy&&native[0].save_calls==saves&&native[0].save_as_calls==copy);
    }
}
static void test_creation_admission_after_clean_save_is_dirty(){
    for(unsigned author=0;author<2;++author){
        seed_creation_settings=true;creation_content_enabled=true;creation_version=300;Party party;seed_creation_settings=false;
        dirty_saved_baseline();dirty_save_request(false);
        auto creation=creation_request(author,0x24682294);creation_enter(creation);creation_return(creation,false);
        CHECK(runtime_host::controller->state().document.revision==1&&runtime_host::controller->state()==runtime_guest::controller->state());
        CHECK(runtime_host::controller->state().document.classes.size()==2);
        check_dirty_sidecar(runtime_host::current_native,true);check_dirty_sidecar(runtime_guest::current_native,true);
        check_dirty_sidecar(native[author].snapshot,true);
        dirty_save_request(true);CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
        creation_content_enabled=false;
    }
}
static void test_clean_creation_cancel_keeps_native_unchanged_gate(){
    for(unsigned author=0;author<2;++author){
        seed_creation_settings=true;Party party;seed_creation_settings=false;dirty_saved_baseline();
        const auto before=runtime_host::controller->state();
        auto creation=creation_request(author,0x24682294);creation_enter(creation);creation_return(creation);
        CHECK(runtime_host::controller->state()==before&&runtime_guest::controller->state()==before);
        for(unsigned p=0;p<2;++p)check_dirty_sidecar(native[p].snapshot,false);
        dirty_save_request(false);CHECK(!runtime_host::frozen&&!runtime_guest::frozen);
    }
}
static void test_foreign_change_while_host_designs_marks_latest_dirty(){
    seed_creation_settings=true;Party party;seed_creation_settings=false;dirty_saved_baseline();
    auto creation=creation_request(0,0x24682294);creation_enter(creation);
    creation_field(1,placement,{editor::Field::Position,0},editor::Vec3{31,2,9},{editor::ScopeKind::Placement,placement,{}});
    CHECK(creation_active[0]&&!native[0].state.editing);
    check_dirty_sidecar(native[0].snapshot,false);check_dirty_sidecar(runtime_host::current_native,true);
    creation_return(creation);
    CHECK(native[0].snapshot.document.placements.at(placement).position==editor::Vec3{31,2,9});
    check_dirty_sidecar(native[0].snapshot,true);dirty_save_request(true);
}
static void test_adopted_snapshot_keeps_dirty_through_later_restore(){
    for(unsigned author=0;author<2;++author){
        seed_creation_settings=true;Party party;seed_creation_settings=false;dirty_saved_baseline();
        grant_tentative_placement(author);const auto applies=native[author].apply_calls;
        capture_tentative_move(author);
        CHECK(spin([]{return runtime_host::controller->state().document.revision==1&&runtime_guest::controller->state().document.revision==1&&!runtime_host::awaiting_operation&&!runtime_guest::awaiting_operation;}));
        CHECK(native[author].apply_calls==applies);
        check_dirty_sidecar(author?runtime_guest::current_native:runtime_host::current_native,true);
        auto creation=creation_request(author,0x24682294);creation_enter(creation);creation_return(creation);
        check_dirty_sidecar(native[author].snapshot,true);dirty_save_request(true);
    }
}
static void test_dirty_rollback_and_absent_sidecar(){
    {
        Party party;dirty_saved_baseline();grant_tentative_placement(0);capture_tentative_move(0);
        CHECK(spin([]{return bool(runtime_host::durable_commit);}));
        check_dirty_sidecar(runtime_host::before_apply,false);
        reject_prepared_commit(0,"Injected dirty-state durability rejection");
        CHECK(spin([]{return !runtime_host::durable_commit&&!runtime_host::pending&&!runtime_host::awaiting_operation&&native[0].restores;}));
        check_dirty_sidecar(native[0].snapshot,false);CHECK(runtime_host::controller->state().document.revision==0);
    }
    {
        seed_creation_settings=true;creation_content_enabled=true;creation_version=400;Party party;seed_creation_settings=false;
        CHECK(!runtime_host::current_native.local_dirty&&!runtime_guest::current_native.local_dirty);
        auto creation=creation_request(1,0x24682294);creation_enter(creation);creation_return(creation,false);
        CHECK(!runtime_host::current_native.local_dirty&&!runtime_guest::current_native.local_dirty);
        creation_content_enabled=false;
    }
}
static void test_all_native_dirty_runtime_cases(){
    test_creation_admission_after_clean_save_is_dirty();
    test_clean_creation_cancel_keeps_native_unchanged_gate();
    test_foreign_change_while_host_designs_marks_latest_dirty();
    test_adopted_snapshot_keeps_dirty_through_later_restore();
    test_dirty_rollback_and_absent_sidecar();
}
