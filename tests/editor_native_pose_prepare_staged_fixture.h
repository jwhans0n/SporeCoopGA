// Actual i686 Native ABI doubles; never calls a running game.
#include <thread>
namespace {
struct StagedPrepareRoutes {
    PosePrepareOwningRoutes owning;
    DWORD thread=native_owner_thread;
    decltype(SkHost::frame_index) frame=H?H->frame_index:nullptr;
    ~StagedPrepareRoutes(){
        pose_prepare_owning_read_callback={};pose_prepare_owning_write_callback={};
        pose_prepare_owning_before_release={};pose_prepare_owning_release_callback={};recent_resolve_callback={};
        clear_staged_pose_prepare();native_owner_thread=thread;if(H)const_cast<SkHost*>(H)->frame_index=frame;
    }
};
Snapshot staged_prepare_setup(bool resolver=false){
    auto base=pose_prepare_owning_setup(resolver);native_owner_thread=GetCurrentThreadId();return base;
}
void test_two_frame_native_pose_prepare(){
    using Result=PosePrepareResult;
    for(unsigned field=0;field<2;++field){
        StagedPrepareRoutes routes;const auto base=staged_prepare_setup();const auto [target,change]=pose_prepare_target(base,field);
        const auto live=t_rows.at(t_live.data());const auto stats=work_stats();const auto frame=t_frame;
        const auto starts=staged_pose_starts,ready=staged_pose_ready;
        PosePrepareTicket ticket;std::string error;
        const auto begun=begin_pose_prepare(base,target,change,[]{return true;},ticket,error);
        if(begun!=Result::Pending)std::printf("staged prepare field=%u begin=%u error=%s\n",field,unsigned(begun),error.c_str());
        CHECK(begun==Result::Pending&&ticket.serial&&staged_pose_prepare&&t_allocations.size()==1);
        CHECK(staged_pose_starts==starts+1&&staged_pose_last_fallback==StagedPoseFallback::None);
        auto middle=work_stats();CHECK(middle.reads.calls==stats.reads.calls+2&&middle.writes.calls==stats.writes.calls+2&&middle.projections.calls==stats.projections.calls);
        Snapshot output;output.bytes={0xCD};
        CHECK(finish_pose_prepare(ticket,output,error)==Result::Pending&&output.bytes==std::vector<uint8_t>{0xCD});
        CHECK(work_stats().reads.calls==middle.reads.calls&&work_stats().writes.calls==middle.writes.calls&&t_allocations.size()==1);
        ++t_frame;CHECK(finish_pose_prepare(ticket,output,error)==Result::Ready);
        CHECK(staged_pose_ready==ready+1);
        const auto ended=work_stats();CHECK(ended.reads.calls==stats.reads.calls+2&&ended.writes.calls==stats.writes.calls+3&&ended.projections.calls==stats.projections.calls);
        CHECK(ended.steps[size_t(WorkStep::BaseRead)].frame==frame&&ended.steps[size_t(WorkStep::FixedpointWrite)].frame==frame&&
              ended.steps[size_t(WorkStep::InverseWrite)].frame==t_frame);
        CHECK(output.document==target&&output.source==base.source&&output.bindings==base.bindings&&output.metadata==base.metadata&&
              output.captain==base.captain&&output.captain_locked==base.captain_locked&&output.act==base.act&&output.local_dirty.has_value()==base.local_dirty.has_value());
        if(base.local_dirty)CHECK(output.local_dirty->saved_revision==base.local_dirty->saved_revision&&
            output.local_dirty->dirty==base.local_dirty->dirty&&output.local_dirty->imported==base.local_dirty->imported);
        CHECK(!staged_pose_prepare&&t_allocations.empty()&&t_rows.at(t_live.data())==live&&!t_restores&&!t_transforms);
        CHECK(validate(output,target,error));CHECK(finish_pose_prepare(ticket,output,error)==Result::Error);
    }
    for(unsigned reason=0;reason<17;++reason){
        StagedPrepareRoutes routes;const auto base=staged_prepare_setup(true);const auto [target,change]=pose_prepare_target(base);
        const auto live=t_rows.at(t_live.data());bool allowed=true;
        PosePrepareTicket ticket;std::string error;CHECK(begin_pose_prepare(base,target,change,[&]{return allowed;},ticket,error)==Result::Pending);
        auto work=staged_pose_prepare;const auto stats=work_stats();auto* after=work->after.p;
        if(reason==0)allowed=false;
        if(reason==1)++generation;
        if(reason==2)++t_scope.epoch;
        if(reason==3)++t_scope.author.generation;
        if(reason==4)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
        if(reason==5)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
        if(reason==6)++recent_resolved.instance;
        if(reason==7)skg::field<uint32_t>(t_rows.at(after).data()+4,0x10)^=1;
        if(reason==8)++skg::field<uint32_t>(t_rows.at(after).data()+4,0x228);
        if(reason==9)++skg::field<uint32_t>(t_rows.at(after).data()+4,0);
        if(reason==10)tcg_rows.at(after).classes.back()^=1; // Unadvertised class bytes must reach inverse closure.
        if(reason==11)act_cache_rows.at(after).acts.back()^=1;
        if(reason==12)++skg::field<uint32_t>(after,0x140);
        std::vector<uint8_t> retired;
        if(reason==13){auto replacement=t_rows.at(after);retired=std::move(t_rows.at(after));t_rows.at(after)=std::move(replacement);rows_for(after);std::fill(retired.begin(),retired.end(),0xA5);}
        if(reason==14)clear_staged_pose_prepare();
        if(reason==15)a_write=0;
        if(reason==16)const_cast<SkHost*>(H)->frame_index=nullptr;
        ++t_frame;Snapshot output;output.bytes={0xCD};
        CHECK(finish_pose_prepare(ticket,output,error)==Result::Error&&!error.empty()&&output.bytes==std::vector<uint8_t>{0xCD});
        work.reset();CHECK(!staged_pose_prepare&&t_allocations.empty()&&t_rows.at(t_live.data())==live&&!t_restores&&!t_transforms);
        if(reason==13)CHECK(std::all_of(retired.begin(),retired.end(),[](uint8_t c){return c==0xA5;}));
        CHECK(work_stats().writes.calls<=stats.writes.calls+1);
    }
    for(unsigned stage=0;stage<3;++stage){
        StagedPrepareRoutes routes;const auto base=staged_prepare_setup();const auto [target,change]=pose_prepare_target(base);
        bool fired=false;PosePrepareTicket ticket;std::string error;
        if(stage==0)pose_prepare_owning_release_callback=[&](void*){if(!fired){fired=true;++generation;}};
        const auto begun=begin_pose_prepare(base,target,change,[]{return true;},ticket,error);
        if(stage==0){CHECK(begun==Result::Error&&fired&&!staged_pose_prepare&&t_allocations.empty());continue;}
        CHECK(begun==Result::Pending);
        if(stage==1)pose_prepare_owning_write_callback=[&](void*){if(!fired){fired=true;++generation;}};
        if(stage==2)pose_prepare_owning_release_callback=[&](void*){if(!fired){fired=true;++generation;}};
        ++t_frame;Snapshot output;output.bytes={0xCD};
        CHECK(finish_pose_prepare(ticket,output,error)==Result::Error&&fired&&!staged_pose_prepare&&t_allocations.empty()&&output.bytes==std::vector<uint8_t>{0xCD});
    }
    {
        StagedPrepareRoutes routes;auto base=staged_prepare_setup();auto [target,change]=pose_prepare_target(base);
        const auto wanted=target;bool fired=false;
        pose_prepare_owning_read_callback=[&](void*){if(!fired){fired=true;base={};target={};change={};}};
        PosePrepareTicket ticket;std::string error;
        CHECK(begin_pose_prepare(base,target,change,[]{return true;},ticket,error)==Result::Pending&&fired);
        ++t_frame;Snapshot output;CHECK(finish_pose_prepare(ticket,output,error)==Result::Ready&&output.document==wanted&&t_allocations.empty());
    }
    {
        StagedPrepareRoutes routes;const auto base=staged_prepare_setup();const auto [target,change]=pose_prepare_target(base);
        PosePrepareTicket ticket;std::string error;CHECK(begin_pose_prepare(base,target,change,[]{return true;},ticket,error)==Result::Pending);
        const auto retained=staged_pose_prepare;const auto count=t_allocations.size();Snapshot output;
        CHECK(finish_pose_prepare({ticket.serial+1},output,error)==Result::Error&&staged_pose_prepare==retained);
        cancel_pose_prepare({ticket.serial+1});CHECK(staged_pose_prepare==retained&&t_allocations.size()==count);
        cancel_pose_prepare(ticket);CHECK(!staged_pose_prepare); // Local retained reference still owns cleanup until below.
    }
    CHECK(t_allocations.empty());
    // A genuinely projected foreign scope cannot become current authority
    // merely because a scope getter switches back after the cache lookup.
    {
        StagedPrepareRoutes routes;const auto base=staged_prepare_setup();const auto [target,change]=pose_prepare_target(base);
        auto foreign=t_scope;++foreign.epoch;C.projection_scope=[foreign]{return foreign;};
        std::string error;CHECK(validate(base,base.document,error));
        unsigned calls=0;C.projection_scope=[&]{++calls;return calls==3||calls==4?foreign:t_scope;};
        const auto stats=work_stats();PosePrepareTicket ticket;
        CHECK(begin_pose_prepare(base,target,change,[]{return true;},ticket,error)==Result::Error&&calls>=5&&!ticket.serial);
        CHECK(!staged_pose_prepare&&t_allocations.empty()&&work_stats().reads.calls==stats.reads.calls&&work_stats().writes.calls==stats.writes.calls);
    }
    for(unsigned at=0;at<3;++at){
        StagedPrepareRoutes routes;const auto base=staged_prepare_setup();const auto [target,change]=pose_prepare_target(base);
        bool allowed=true,fired=false;PosePrepareTicket ticket;std::string error;
        CHECK(begin_pose_prepare(base,target,change,[&]{return allowed;},ticket,error)==Result::Pending);
        PosePrepareResult nested=Result::Ready;PosePrepareTicket second;
        pose_prepare_owning_release_callback=[&](void*){if(fired)return;fired=true;std::string nested_error;
            nested=begin_pose_prepare(base,target,change,[]{return true;},second,nested_error);};
        if(at==0)cancel_pose_prepare(ticket);
        else{if(at==1)allowed=false;++t_frame;Snapshot output;
            CHECK(finish_pose_prepare(ticket,output,error)==(at==1?Result::Error:Result::Ready));}
        CHECK(fired&&nested==Result::Error&&!second.serial&&!staged_pose_prepare&&t_allocations.empty());
    }
    {
        StagedPrepareRoutes routes;const auto base=staged_prepare_setup();const auto [target,change]=pose_prepare_target(base);
        PosePrepareTicket ticket;std::string error;CHECK(begin_pose_prepare(base,target,change,[]{return true;},ticket,error)==Result::Pending);
        bool offthread_error=false;std::thread worker([&]{Snapshot output;std::string message;cancel_pose_prepare(ticket);
            offthread_error=finish_pose_prepare(ticket,output,message)==Result::Error;});worker.join();
        CHECK(offthread_error&&staged_pose_prepare&&t_allocations.size()==1);cancel_pose_prepare(ticket);CHECK(t_allocations.empty());
    }
    {
        StagedPrepareRoutes routes;const auto base=staged_prepare_setup();const auto [target,change]=pose_prepare_target(base,2);
        PosePrepareTicket ticket;std::string error;const auto stats=work_stats();
        CHECK(begin_pose_prepare(base,target,change,[]{return true;},ticket,error)==Result::Fallback&&!ticket.serial&&!staged_pose_prepare);
        CHECK(staged_pose_last_fallback==StagedPoseFallback::Shape);
        CHECK(work_stats().reads.calls==stats.reads.calls&&work_stats().writes.calls==stats.writes.calls&&t_allocations.empty());
    }
    {
        StagedPrepareRoutes routes;const auto base=staged_prepare_setup();const auto [target,change]=pose_prepare_target(base);
        const auto id=base.document.placements.begin()->first;const auto position=base.document.placements.at(id).position;
        terrain_grounding[id]={position,position};PosePrepareTicket ticket;std::string error;const auto stats=work_stats();
        CHECK(begin_pose_prepare(base,target,change,[]{return true;},ticket,error)==Result::Fallback&&staged_pose_last_fallback==StagedPoseFallback::Grounding);
        CHECK(!ticket.serial&&!staged_pose_prepare&&t_allocations.empty()&&work_stats().reads.calls==stats.reads.calls&&work_stats().writes.calls==stats.writes.calls);
    }
}
unsigned staged_apply_calls=0;
std::function<void(void*)> staged_apply_callback;
void SK_THISCALL staged_checked_apply(void* owner,void* marker,uint32_t flags){
    CHECK(owner==t_data.data()&&!flags&&applying);++staged_apply_calls;
    if(staged_apply_callback)staged_apply_callback(marker);
}
void test_staged_pose_checked_application(){
    for(unsigned reason=0;reason<5;++reason){
        StagedPrepareRoutes routes;auto base=staged_prepare_setup();auto [target,change]=pose_prepare_target(base);
        const auto route=original_apply_marker;original_apply_marker=staged_checked_apply;staged_apply_calls=0;staged_apply_callback={};
        Snapshot prepared;std::string error;CHECK(prepare(base,target,change,nullptr,prepared,error));
        const auto live=t_rows.at(t_live.data());const auto expected=target;const auto original_generation=generation;
        SceneOwner owner{uintptr_t(t_mode.data()),uintptr_t(t_data.data()),uintptr_t(t_live.data()),skg::field<editor::ResourceKey>(t_live.data(),8)};
        bool fired=false;
        const auto current=[&]{
            if(reason==1&&!fired){fired=true;++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
                owner.source=skg::field<editor::ResourceKey>(t_live.data(),8);}
            if(reason==4&&!fired){fired=true;prepared={};change={};}
            return true;
        };
        if(reason==2||reason==3)staged_apply_callback=[&](void* marker){fired=true;
            if(reason==2){++skg::field<editor::ResourceKey>(t_live.data(),8).instance;owner.source=skg::field<editor::ResourceKey>(t_live.data(),8);}
            else skg::field<uint32_t>(marker,0x10)^=1;};
        const bool applied=apply_prepared_checked(prepared,change,original_generation,owner,current,error);
        if(reason==0||reason==4){CHECK(applied&&staged_apply_calls==1);
            const auto id=expected.placements.begin()->first;auto* marker=marker_for_id(t_live.data(),id);
            CHECK(marker&&skg::field<editor::Vec3>(marker,4)==expected.placements.at(id).position);
        }else{CHECK(!applied&&fired&&!error.empty());CHECK(staged_apply_calls==(reason==1?0u:1u));}
        if(reason==1)CHECK(t_rows.at(t_live.data())==live);
        CHECK(!applying&&t_allocations.empty());staged_apply_callback={};original_apply_marker=route;
    }
}
}
