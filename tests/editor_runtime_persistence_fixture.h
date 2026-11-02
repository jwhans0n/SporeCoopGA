// Actual UDP and durable Store coverage for replica-only serialization work.
// Native work remains isolated by the main fixture's native adapter double.
static void test_guest_persistence_skips_unused_envelope_while_host_is_away(){
    seed_creation_settings=true;Party party(1u<<20);seed_creation_settings=false;
    auto away=creation_request(0,0x2B978C46,class_id);creation_enter(away);
    const auto host_scene=native[0].snapshot.bytes;const auto host_restores=native[0].restores;
    const auto previous=runtime_guest::store.snapshot();
    const auto closure=runtime_guest::resources.bundle.records;const auto old_hash=runtime_guest::resource_hash;
    std::string error;side=1;
    const editor::Scope owned{editor::ScopeKind::Placement,placement,{}};
    CHECK(runtime_guest::controller->request_leases({owned},15000,error));
    CHECK(spin([&]{return std::any_of(runtime_guest::controller->view().local_leases.begin(),runtime_guest::controller->view().local_leases.end(),[&](const auto& held){return held.scope==owned;});}));
    auto request=runtime_guest::request(editor::Intent::Forward);request.summary="Replica persistence pose";
    editor::Mutation mutation;mutation.target=placement;mutation.field={editor::Field::Position,0};mutation.value=editor::Vec3{31,2,9};
    mutation.expected=editor::field_revision(runtime_guest::controller->state().document,placement,mutation.field);request.mutations={mutation};
    CHECK(runtime_guest::controller->submit(request,error));
    CHECK(spin([]{return bool(runtime_guest::durable_commit);}));
    auto& job=*runtime_guest::durable_commit;
    CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready);CHECK(job.task.get());
    CHECK(!job.hosting&&!job.flushing&&!job.publication&&job.envelope.empty()&&job.delta.empty());
    CHECK(job.hash==old_hash&&job.resources.bundle.records.size()==closure.size());
    for(const auto& row:closure)if(row.key!=runtime_guest::resources.root){
        const auto found=std::find_if(job.resources.bundle.records.begin(),job.resources.bundle.records.end(),[&](const auto& candidate){return candidate.key==row.key;});
        CHECK(found!=job.resources.bundle.records.end()&&found->data==row.data);
    }
    editor::State prepared;editor_native::Snapshot prepared_native;
    CHECK(editor_project::load_snapshot(job.saved,prepared,prepared_native,error));
    CHECK(prepared==job.state&&prepared_native.bytes==job.native.bytes&&prepared_native.bindings==job.native.bindings);
    std::promise<bool> complete;job.task=complete.get_future();
    for(unsigned i=0;i<4;++i){step();CHECK(runtime_guest::store.snapshot()==previous&&runtime_guest::controller->state().document.revision==0);}
    complete.set_value(true);
    CHECK(spin([]{return runtime_host::controller->settled()&&!runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_guest::controller->state().document.revision==1;}));
    CHECK(runtime_host::controller->state()==runtime_guest::controller->state());
    CHECK(runtime_host::store.durable_revision()==1&&runtime_guest::store.durable_revision()==1);
    CHECK(runtime_guest::resource_hash==old_hash&&native[0].snapshot.bytes==host_scene&&native[0].restores==host_restores);
    CHECK(creation_active[0]&&!native[0].state.ready);
    for(bool redo:{false,true}){
        side=1;runtime_guest::undo_redo(redo);const auto revision=redo?3u:2u;
        CHECK(spin([&]{return runtime_host::controller->settled()&&!runtime_guest::awaiting_operation&&!runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_guest::controller->state().document.revision==revision;}));
        CHECK(runtime_host::controller->state()==runtime_guest::controller->state());
        CHECK(runtime_guest::controller->state().document.placements.at(placement).position==(redo?editor::Vec3{31,2,9}:editor::Vec3{}));
    }
    creation_return(away);
    const auto committed=runtime_guest::controller->state();const auto live=runtime_guest::current_native;
    runtime_guest::store.close();CHECK(runtime_guest::store.open(runtime_guest::project_root,committed.document.project,error));
    editor::State recovered;editor_native::Snapshot native_recovered;
    CHECK(editor_project::load_snapshot(runtime_guest::store.snapshot(),recovered,native_recovered,error));
    CHECK(recovered==committed&&native_recovered.bytes==live.bytes&&native_recovered.bindings==live.bindings);
    CHECK(runtime_guest::store.durable_revision()==3&&!runtime_host::frozen&&!runtime_guest::frozen);
}

static void test_guest_new_dependencies_still_keep_full_recovery_artifact(){
    seed_creation_settings=true;creation_content_enabled=true;creation_version=1;
    Party party(1u<<20);seed_creation_settings=false;
    const auto old_hash=runtime_guest::resource_hash;const auto old_count=runtime_guest::resources.bundle.records.size();
    auto value=creation_request(1,0x24682294);creation_enter(value);
    const editor_creation::Result result{value.request,{0x29001001,0x24682294,0x408A0000},false};
    auto& context=editor_creation::fixture_contexts[1];std::string error;side=1;
    CHECK(!context.returned(value.scope,result,error)&&error.empty());
    CHECK(spin([&]{
        if(runtime_guest::durable_commit)return true;
        side=1;error.clear();CHECK(!context.returned(value.scope,result,error)&&error.empty());
        return bool(runtime_guest::durable_commit);
    }));
    auto& job=*runtime_guest::durable_commit;
    CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready);CHECK(job.task.get());
    CHECK(!job.hosting&&!job.flushing&&!job.publication&&!job.envelope.empty()&&job.delta.empty());
    CHECK(job.resources.bundle.records.size()==old_count+1&&job.hash!=old_hash);
    editor_project::Resources decoded;CHECK(editor_project::decode_resources(job.envelope,decoded,error));
    CHECK(decoded.root==job.resources.root&&decoded.bundle.records.size()==job.resources.bundle.records.size());
    for(const auto& row:job.resources.bundle.records){
        const auto found=std::find_if(decoded.bundle.records.begin(),decoded.bundle.records.end(),[&](const auto& candidate){return candidate.key==row.key;});
        CHECK(found!=decoded.bundle.records.end()&&found->data==row.data);
    }
    std::vector<uint8_t> artifact;side=1;
    CHECK(runtime_guest::read_blob(job.hash,artifact,editor::wire::kResourceBytes,error)&&artifact==job.envelope);
    std::promise<bool> complete;job.task=complete.get_future();complete.set_value(true);
    creation_return(value,false);
    CHECK(runtime_host::controller->state()==runtime_guest::controller->state());
    CHECK(runtime_guest::resources.bundle.records.size()==old_count+1&&runtime_guest::resource_hash!=old_hash);
    CHECK(runtime_host::store.durable_revision()==1&&runtime_guest::store.durable_revision()==1);
    std::string assets;std::map<editor::OperationId,runtime_guest::Artifact> history;
    CHECK(runtime_guest::parse_manifest(runtime_guest::store.snapshot().manifest,assets,history,error)&&assets==runtime_guest::resource_hash);
    creation_content_enabled=false;
}

static void test_all_replica_persistence_cases(){
    test_guest_persistence_skips_unused_envelope_while_host_is_away();
    test_guest_new_dependencies_still_keep_full_recovery_artifact();
    test_foreign_durable_commit_fences_owned_native_input();
}
