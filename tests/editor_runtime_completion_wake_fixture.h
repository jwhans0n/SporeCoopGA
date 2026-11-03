// Real workers, real UDP and durable Store; Native APIs remain controlled
// doubles. Completion pumps deliberately run without advancing a game frame.
static auto completion_native_counts(unsigned process){
    const auto& n=native[process];
    return std::array<unsigned,12>{n.state_queries,n.prepare_calls,n.capture_calls,n.draft_capture_attempts,
        n.apply_calls,n.restores,n.validations,n.opaque_checks,n.view_captures,n.view_restores,n.registrations,n.selections};
}
static unsigned run_completion_tasks(unsigned process){
    side=int(process);unsigned count=0;
    for(auto task:tasks[process].take()){
        if(task.fn!=runtime_host::durable_wake&&task.fn!=runtime_guest::durable_wake){tasks[process].push_back(task);continue;}
        // Success cases schedule the main observation after the real future is
        // ready. The separate early-wake case exercises the nonblocking miss.
        const bool ready=process?(!runtime_guest::durable_commit || runtime_guest::durable_commit->task.wait_for(std::chrono::seconds(0))==std::future_status::ready):
            (!runtime_host::durable_commit || runtime_host::durable_commit->task.wait_for(std::chrono::seconds(0))==std::future_status::ready);
        if(!ready){tasks[process].push_back(task);continue;}
        const auto before=completion_native_counts(process);task.fn(task.user);++count;
        CHECK(completion_native_counts(process)==before);
    }
    return count;
}
static void test_completion_dispatch_has_fixed_thread_safe_destinations(){
    CHECK(tasks[0].size()==0&&tasks[1].size()==0);
    struct Observation {std::thread::id main=std::this_thread::get_id(),observed;int process=-1;};
    std::array<Observation,2> observed;
    const auto callback=+[](void* opaque){auto& value=*static_cast<Observation*>(opaque);value.observed=std::this_thread::get_id();value.process=side;};
    side=1;std::thread host_worker([&]{outside_host(callback,&observed[0]);});host_worker.join();
    side=0;std::thread guest_worker([&]{outside_guest(callback,&observed[1]);});guest_worker.join();
    CHECK(tasks[0].size()==1&&tasks[1].size()==1);
    CHECK(observed[0].process==-1&&observed[1].process==-1);
    run_tasks(0);run_tasks(1);
    for(unsigned p=0;p<2;++p)CHECK(observed[p].process==int(p)&&observed[p].observed==observed[p].main);
}
struct CompletionRequest {editor::OperationId operation;uint64_t frame;};
static CompletionRequest start_completion_request(unsigned author,bool enqueue_failure=false){
    std::string error;side=int(author);auto& controller=author?*runtime_guest::controller:*runtime_host::controller;
    const editor::Scope scope{editor::ScopeKind::Placement,placement,{}};
    CHECK(controller.request_leases({scope},15000,error));
    CHECK(spin([&]{return std::any_of(controller.view().local_leases.begin(),controller.view().local_leases.end(),
        [&](const auto& lease){return lease.scope==scope;});}));
    side=int(author);auto request=author?runtime_guest::request(editor::Intent::Forward):runtime_host::request(editor::Intent::Forward);
    request.summary="Worker completion pose";
    editor::Mutation mutation;mutation.target=placement;mutation.field={editor::Field::Position,0};mutation.value=editor::Vec3{11,22,33};
    mutation.expected=editor::field_revision(controller.state().document,placement,mutation.field);request.mutations={mutation};
    if(author)runtime_guest::awaiting_operation=request.operation;else runtime_host::awaiting_operation=request.operation;
    const auto frame=game_frame;if(enqueue_failure)outside_enqueue_failure[0]=true;
    CHECK(controller.submit(request,error));
    const auto until=net::now_ms()+5000;
    while(!runtime_host::durable_commit&&net::now_ms()<until){
        network();if(author){side=1;controller.tick(clock_ms);network();}
        side=0;runtime_host::controller->tick(clock_ms);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(runtime_host::durable_commit&&!runtime_host::durable_commit->flushing&&game_frame==frame);
    CHECK(runtime_host::controller->state().document.revision==0&&runtime_host::store.durable_revision()==0);
    return {request.operation,frame};
}
static void finish_completion_request(const CompletionRequest& request){
    const auto until=net::now_ms()+10000;unsigned wakes=0;
    while(net::now_ms()<until){
        wakes+=run_completion_tasks(0);network();
        // Deliver the already authenticated accepted Decision through its usual
        // controller path; only subsequent durability work uses the wake pump.
        if(!runtime_guest::durable_commit&&runtime_guest::controller->state().document.revision==0){side=1;runtime_guest::controller->tick(clock_ms);}
        wakes+=run_completion_tasks(1);network();
        if(!runtime_host::durable_commit&&!runtime_guest::durable_commit&&
           runtime_host::controller->state().document.revision==1&&runtime_guest::controller->state().document.revision==1)break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(game_frame==request.frame&&wakes>0);
    CHECK(runtime_host::controller->state().document==runtime_guest::controller->state().document);
    CHECK(runtime_host::controller->state().document.revision==1&&runtime_host::store.durable_revision()==1&&runtime_guest::store.durable_revision()==1);
    CHECK(runtime_host::controller->state().history.size()==1&&runtime_guest::controller->state().history.size()==1);
    CHECK(runtime_host::controller->state().history.front().operation==request.operation);
    CHECK(runtime_host::controller->state().document.placements.at(placement).position==(editor::Vec3{11,22,33}));
    CHECK(!runtime_host::frozen&&!runtime_guest::frozen&&!runtime_host::awaiting_operation&&!runtime_guest::awaiting_operation);
    std::string error;editor::State recovered;editor_native::Snapshot image;
    CHECK(editor_project::load_snapshot(runtime_host::store.snapshot(),recovered,image,error));
    CHECK(recovered.document==runtime_host::controller->state().document&&image.bytes==runtime_host::current_native.bytes);
    CHECK(editor_project::load_snapshot(runtime_guest::store.snapshot(),recovered,image,error));
    CHECK(recovered.document==runtime_guest::controller->state().document&&image.bytes==runtime_guest::current_native.bytes);
}
static void test_completion_wakes_finish_both_udp_roles_without_new_frames(){
    for(unsigned author=0;author<2;++author){Party party;const auto request=start_completion_request(author);finish_completion_request(request);}
}
static void test_early_completion_is_nonblocking_and_does_not_repost(){
    Party party;const auto request=start_completion_request(0);auto& job=*runtime_host::durable_commit;
    CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&job.task.get());
    const auto ready=job.task;std::promise<bool> blocked;job.task=blocked.get_future().share();
    const auto wake=job.wake;const auto native_before=completion_native_counts(0);const auto queued=tasks[0].size();
    side=0;runtime_host::durable_wake(new runtime_host::DurableWake(wake));
    CHECK(runtime_host::durable_commit&&runtime_host::durable_commit->wake==wake&&!job.flushing);
    CHECK(tasks[0].size()==queued&&completion_native_counts(0)==native_before&&runtime_host::store.durable_revision()==0);
    blocked.set_value(true);job.task=ready;finish_completion_request(request);
}
static void test_stale_completion_tokens_cannot_consume_current_job(){
    Party party;const auto request=start_completion_request(0);auto& job=*runtime_host::durable_commit;
    CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&job.task.get());
    const auto pin=job.wake;const auto native_before=completion_native_counts(0);const auto original_source=runtime_host::current_native.source;
    for(unsigned reason=0;reason<12;++reason){
        auto stale=pin;const auto domain=runtime_host::durable_runtime_domain,generation=runtime_host::native_generation,retired=runtime_host::retired_transport;
        if(reason==0)++stale.serial;if(reason==1)stale.phase=runtime_host::DurablePhase::Flush;
        if(reason==2)++stale.context.epoch;if(reason==3)++stale.operation.sequence;
        if(reason==4)++runtime_host::durable_runtime_domain;if(reason==5)++runtime_host::native_generation;
        if(reason==6)++runtime_host::current_native.source.instance;if(reason==7)runtime_host::closing=true;
        if(reason==8)runtime_host::retired_transport=runtime_host::observed_transport;
        if(reason==9)++stale.local.generation;
        if(reason==10)runtime_host::native_attached=false;
        if(reason==11)runtime_host::staged_capture=std::make_shared<runtime_host::StagedCapture>();
        side=0;runtime_host::durable_wake(new runtime_host::DurableWake(stale));
        CHECK(runtime_host::durable_commit&&runtime_host::durable_commit->wake==pin&&!job.flushing);
        CHECK(runtime_host::store.durable_revision()==0&&completion_native_counts(0)==native_before);
        runtime_host::durable_runtime_domain=domain;runtime_host::native_generation=generation;runtime_host::retired_transport=retired;
        runtime_host::current_native.source=original_source;runtime_host::closing=false;
        runtime_host::native_attached=pin.attached;runtime_host::staged_capture.reset();
    }
    // A new job can have identical project/head but a different operation/serial.
    auto original=std::move(runtime_host::durable_commit);runtime_host::durable_commit=std::make_unique<runtime_host::DurableCommit>();
    runtime_host::durable_commit->wake=pin;++runtime_host::durable_commit->wake.serial;
    std::promise<bool> blocked;runtime_host::durable_commit->task=blocked.get_future().share();
    side=0;runtime_host::durable_wake(new runtime_host::DurableWake(pin));
    CHECK(runtime_host::durable_commit->wake.serial==pin.serial+1&&runtime_host::store.durable_revision()==0);
    blocked.set_value(true);runtime_host::durable_commit=std::move(original);finish_completion_request(request);
}
static void test_old_phase_and_duplicate_completion_are_inert(){
    Party party;const auto request=start_completion_request(0);auto& job=*runtime_host::durable_commit;
    CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&job.task.get());const auto encode=job.wake;
    side=0;runtime_host::durable_wake(new runtime_host::DurableWake(encode));
    CHECK(runtime_host::durable_commit&&runtime_host::durable_commit->flushing);
    auto& flushing=*runtime_host::durable_commit;CHECK(flushing.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&flushing.task.get());
    const auto flush=flushing.wake;const auto before=completion_native_counts(0);
    side=0;runtime_host::durable_wake(new runtime_host::DurableWake(encode));
    CHECK(runtime_host::durable_commit&&runtime_host::durable_commit->wake==flush&&completion_native_counts(0)==before);
    finish_completion_request(request);const auto state=runtime_host::controller->state();
    side=0;runtime_host::durable_wake(new runtime_host::DurableWake(flush));
    CHECK(runtime_host::controller->state()==state&&runtime_host::store.durable_revision()==1&&completion_native_counts(0)==before);
}
static void test_enqueue_failure_keeps_ordinary_tick_fallback(){
    Party party;const auto request=start_completion_request(0,true);auto& job=*runtime_host::durable_commit;
    CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&job.task.get());
    CHECK(!outside_enqueue_failure[0]);
    CHECK(spin([]{return !runtime_host::durable_commit&&!runtime_guest::durable_commit&&runtime_guest::controller->state().document.revision==1;}));
    CHECK(runtime_host::store.durable_revision()==1&&runtime_guest::store.durable_revision()==1);
    CHECK(runtime_host::controller->state().history.front().operation==request.operation&&!runtime_host::frozen&&!runtime_guest::frozen);
}
static void test_completion_defers_worker_and_publication_failures_to_main_drain(){
    for(unsigned reason=0;reason<3;++reason){
        Party party;start_completion_request(0);auto& job=*runtime_host::durable_commit;
        CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&job.task.get());
        if(reason<2){std::promise<bool> failed;job.task=failed.get_future().share();
            if(reason==0)failed.set_value(false);else failed.set_exception(std::make_exception_ptr(std::runtime_error("injected worker exception")));}
        else job.publication.reset();
        const auto wake=job.wake;const auto before=completion_native_counts(0);side=0;
        runtime_host::durable_wake(new runtime_host::DurableWake(wake));
        CHECK(runtime_host::durable_commit&&!job.flushing&&runtime_host::store.durable_revision()==0);
        CHECK(completion_native_counts(0)==before&&runtime_host::controller->state().document.revision==0);
        // Only the normal controller drain may invoke the checked rollback.
        runtime_host::controller->tick(clock_ms);
        CHECK(!runtime_host::durable_commit&&runtime_host::controller->state().document.revision==0&&runtime_host::store.durable_revision()==0);
        CHECK(native[0].restores>before[5]&&runtime_host::controller->state().history.empty());
    }
}
static void test_completion_after_shutdown_cannot_reenter_new_runtime(){
    std::optional<runtime_host::DurableWake> retired;
    {Party party;start_completion_request(0);auto& job=*runtime_host::durable_commit;
        CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&job.task.get());retired=job.wake;}
    CHECK(!runtime_host::controller&&!runtime_host::durable_commit);
    const auto before=completion_native_counts(0);side=0;runtime_host::durable_wake(new runtime_host::DurableWake(*retired));
    CHECK(completion_native_counts(0)==before);
    {Party next;const auto request=start_completion_request(0);const auto fresh=runtime_host::durable_commit->wake;
        CHECK(fresh.domain!=retired->domain);const auto counts=completion_native_counts(0);side=0;
        runtime_host::durable_wake(new runtime_host::DurableWake(*retired));
        CHECK(runtime_host::durable_commit->wake==fresh&&completion_native_counts(0)==counts);finish_completion_request(request);}
}
static void test_completion_host_transport_loss_does_not_publish_from_wake(){
    Party party;start_completion_request(1);auto& job=*runtime_host::durable_commit;
    CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&job.task.get());
    const auto wake=job.wake;const auto before=completion_native_counts(0);
    sessions[0].leave();side=0;runtime_host::durable_wake(new runtime_host::DurableWake(wake));
    CHECK(runtime_host::durable_commit&&runtime_host::durable_commit->wake==wake&&!job.flushing);
    CHECK(runtime_host::store.durable_revision()==0&&runtime_host::controller->state().document.revision==0);
    CHECK(completion_native_counts(0)==before);
}
static void test_completion_uses_owned_context_without_a_fresh_native_probe(){
    Party party;const auto request=start_completion_request(0);auto& job=*runtime_host::durable_commit;
    CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&job.task.get());
    const auto wake=job.wake;const auto generation=native[0].state.generation;const auto before=completion_native_counts(0);
    // This notification has no scene-mutation authority. A raw Native change
    // not yet observed by Runtime is discovered by the ordinary scene drain,
    // while already-applied owned bytes may continue their durable acceptance.
    ++native[0].state.generation;side=0;runtime_host::durable_wake(new runtime_host::DurableWake(wake));
    CHECK(runtime_host::durable_commit&&runtime_host::durable_commit->flushing&&runtime_host::store.durable_revision()<=1);
    CHECK(runtime_host::native_generation==generation&&completion_native_counts(0)==before);
    native[0].state.generation=generation;finish_completion_request(request);
}
static void test_worker_timing_is_main_observed_and_capture_fenced(){
    using Stage=editor_stage_metrics::Stage;
    {
        Party party;LARGE_INTEGER frequency{};CHECK(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0);
        CHECK(runtime_host::stage_metrics.reset(GetCurrentProcessId(),uint64_t(frequency.QuadPart),runtime_host::stage_qpc()));
        CHECK(runtime_guest::stage_metrics.reset(GetCurrentProcessId(),uint64_t(frequency.QuadPart),runtime_guest::stage_qpc()));
        const auto request=start_completion_request(1);
        auto& job=*runtime_host::durable_commit;
        CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&job.task.get());
        CHECK(job.encoding.begin&&job.encoding.end>=job.encoding.begin&&!job.encoding.observed);
        finish_completion_request(request);
        for(unsigned p=0;p<2;++p){
            const auto& metrics=p?runtime_guest::stage_metrics:runtime_host::stage_metrics;
            CHECK(metrics.recording()&&!metrics.clock_error());
            for(const auto stages:std::array{
                std::array{Stage::WorkerEncodeBegin,Stage::WorkerEncodeEnd,Stage::WorkerEncodeObserved},
                std::array{Stage::WorkerFlushBegin,Stage::WorkerFlushEnd,Stage::WorkerFlushObserved}}){
                std::array<uint64_t,3> qpc{};std::array<unsigned,3> counts{};
                for(size_t i=0;i<metrics.size();++i){const auto* event=metrics.at(i);
                    if(event->operation!=request.operation)continue;
                    for(size_t j=0;j<stages.size();++j)if(event->stage==stages[j]){++counts[j];qpc[j]=event->qpc;CHECK(event->revision==1);}}
                CHECK(counts==(std::array<unsigned,3>{1,1,1})&&qpc[0]&&qpc[0]<=qpc[1]&&qpc[1]<=qpc[2]);
            }
            unsigned prepares=0;for(size_t i=0;i<metrics.size();++i)if(metrics.at(i)->operation==request.operation&&metrics.at(i)->stage==Stage::NativePrepareEnd)++prepares;
            CHECK(prepares==1);
        }
        runtime_host::stage_metrics.stop();runtime_guest::stage_metrics.stop();
    }
    {
        Party party;LARGE_INTEGER frequency{};CHECK(QueryPerformanceFrequency(&frequency));
        CHECK(runtime_host::stage_metrics.reset(GetCurrentProcessId(),uint64_t(frequency.QuadPart),runtime_host::stage_qpc()));
        const auto request=start_completion_request(0);auto& job=*runtime_host::durable_commit;
        CHECK(job.task.wait_for(std::chrono::seconds(5))==std::future_status::ready&&job.task.get());
        const auto previous_capture=job.encoding.capture;
        CHECK(runtime_host::stage_metrics.reset(GetCurrentProcessId(),uint64_t(frequency.QuadPart),runtime_host::stage_qpc()));
        CHECK(runtime_host::stage_metrics.capture_id()!=previous_capture);
        finish_completion_request(request);unsigned flush=0;
        for(size_t i=0;i<runtime_host::stage_metrics.size();++i){const auto stage=runtime_host::stage_metrics.at(i)->stage;
            CHECK(stage!=Stage::WorkerEncodeBegin&&stage!=Stage::WorkerEncodeEnd&&stage!=Stage::WorkerEncodeObserved);
            if(stage==Stage::WorkerFlushObserved)++flush;}
        CHECK(flush==1&&runtime_host::stage_metrics.recording()&&!runtime_host::stage_metrics.clock_error());runtime_host::stage_metrics.stop();
    }
}
static void test_all_completion_wake_cases(){
    test_completion_dispatch_has_fixed_thread_safe_destinations();
    test_completion_wakes_finish_both_udp_roles_without_new_frames();
    test_early_completion_is_nonblocking_and_does_not_repost();
    test_stale_completion_tokens_cannot_consume_current_job();
    test_old_phase_and_duplicate_completion_are_inert();
    test_enqueue_failure_keeps_ordinary_tick_fallback();
    test_completion_defers_worker_and_publication_failures_to_main_drain();
    test_completion_after_shutdown_cannot_reenter_new_runtime();
    test_completion_host_transport_loss_does_not_publish_from_wake();
    test_completion_uses_owned_context_without_a_fresh_native_probe();
    test_worker_timing_is_main_observed_and_capture_fenced();
}
