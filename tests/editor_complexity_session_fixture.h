// Real UDP checks. No native function or process access.
static ComplexityCapability complexity_proof(){
    ComplexityCapability v;v.available=true;v.reason=ComplexityReason::None;
    v.words.fill(std::bit_cast<uint32_t>(1.0f));v.words[12]=std::bit_cast<uint32_t>(10.0f);return v;
}
static State complexity_seed(bool enabled=false){
    auto s=seed();auto& d=s.document;d.next_id=13ull<<32;
    d.settings_schema[kComplexityProperty]={ValueKind::Boolean,0,0,0,false,false};
    AuthoringSettings row;row.object=kComplexityProjectObject;row.kind=SettingsKind::Project;
    row.properties[kComplexityProperty]=enabled;d.settings[row.object]=row;return s;
}
static bool complexity_ready(const Party& p){
    for(unsigned n=0;n<p.count;++n)if(!p.controllers[n]->view().complexity_ready)return false;
    return true;
}
static void test_complexity_cohort(unsigned players){
    Party p(players);std::string e;const auto proof=complexity_proof();
    for(unsigned n=0;n<players;++n){auto v=proof;if(n==1)v.words[0]=std::bit_cast<uint32_t>(2.0f);
        CHECK(p.controllers[n]->set_complexity_capability(v,e));}
    p.start(complexity_seed());p.accept();CHECK(!complexity_ready(p));
    // Native-default edits remain accepted with different tuning.
    const auto placement=p.grant(1,{ScopeKind::Placement,object_a,{}});
    CHECK(p.controllers[1]->submit(p.edit(1,object_a,position,Vec3{61,2,3},placement),e));
    CHECK(p.revision(1));p.release(1,placement);
    const FieldKey enabled{Field::Property,kComplexityProperty};
    auto token=p.grant(0,{ScopeKind::Field,kComplexityProjectObject,enabled});
    const auto decisions=p.native[0].decisions;
    CHECK(p.controllers[0]->submit(p.edit(0,kComplexityProjectObject,enabled,true,token),e));
    CHECK(p.spin([&]{return p.native[0].decisions>decisions;}));
    CHECK(p.controllers[0]->state().document.revision==1);
    CHECK(!extended_complexity(p.controllers[0]->state().document));
    CHECK(p.controllers[1]->set_complexity_capability(proof,e));CHECK(p.spin([&]{return complexity_ready(p);}));
    CHECK(p.controllers[0]->submit(p.edit(0,kComplexityProjectObject,enabled,true,token),e));
    CHECK(p.revision(2));CHECK(extended_complexity(p.controllers[0]->state().document));p.release(0,token);
    for(size_t word=0;word<13;++word){
        auto v=proof;v.words[word]=std::bit_cast<uint32_t>(word==12?11.0f:2.0f);
        CHECK(p.controllers[1]->set_complexity_capability(v,e));CHECK(!p.controllers[1]->view().complexity_ready);
        CHECK(p.spin([&]{return !p.controllers[0]->view().complexity_ready;}));
        CHECK(p.controllers[0]->state().document.revision==2);
        for(unsigned n=0;n<players;++n)CHECK(p.controllers[n]->view().phase==es::Phase::Ready);
        CHECK(p.controllers[1]->set_complexity_capability(proof,e));CHECK(p.spin([&]{return complexity_ready(p);}));
    }
    CHECK(p.controllers[1]->set_complexity_capability({},e));
    CHECK(p.spin([&]{return !p.controllers[0]->view().complexity_ready;}));
    const auto replies=p.wire_lease_replies[1].size();
    CHECK(p.controllers[1]->request_leases({{ScopeKind::Placement,object_b,{}}},1000,e));
    CHECK(p.spin([&]{return p.wire_lease_replies[1].size()>replies;}));CHECK(!p.wire_lease_replies[1].back().result.accepted());
    token=p.grant(0,{ScopeKind::Field,kComplexityProjectObject,enabled});
    CHECK(p.controllers[0]->submit(p.edit(0,kComplexityProjectObject,enabled,false,token),e));
    CHECK(p.revision(3));CHECK(!extended_complexity(p.controllers[0]->state().document));
}
static void test_complexity_join_and_recovery(){
    Party p(2);std::string e;const auto proof=complexity_proof();
    CHECK(p.controllers[0]->set_complexity_capability(proof,e));p.start(complexity_seed(true));
    CHECK(p.controllers[1]->accept_offer(e));for(unsigned n=0;n<80;++n)p.step();
    CHECK(p.controllers[1]->view().phase==es::Phase::Syncing);CHECK(p.native[1].baselines==0);
    auto v=proof;v.words[12]=std::bit_cast<uint32_t>(20.0f);
    CHECK(p.controllers[1]->set_complexity_capability(v,e));for(unsigned n=0;n<40;++n)p.step();CHECK(p.native[1].baselines==0);
    CHECK(p.controllers[1]->set_complexity_capability(proof,e));
    CHECK(p.spin([&]{return complexity_ready(p)&&p.controllers[1]->view().phase==es::Phase::Ready;}));
    CHECK(p.native[1].baselines==1);
    auto state=p.controllers[0]->state();++state.document.epoch;p.controllers[0]->end_for_recovery();p.start(state);p.accept();
    CHECK(p.spin([&]{return complexity_ready(p);}));CHECK(p.controllers[1]->state().document.epoch==state.document.epoch);
}
static void test_complexity_old_ack(){
    Party p(2);std::string e;const auto proof=complexity_proof();
    for(unsigned n=0;n<2;++n)CHECK(p.controllers[n]->set_complexity_capability(proof,e));
    p.start(complexity_seed());p.accept();CHECK(p.spin([&]{return complexity_ready(p);}));
    CHECK(!p.wire_complexity_states[1].empty());auto old=p.wire_complexity_states[1].back();
    auto changed=proof;changed.words[3]=std::bit_cast<uint32_t>(3.0f);
    CHECK(p.controllers[1]->set_complexity_capability(changed,e));CHECK(!p.controllers[1]->view().complexity_ready);
    old.host=changed;++old.serial;old.ready=true;
    net::Writer body;CHECK(w::write({p.controllers[1]->view().context,old},body,e));
    const auto bytes=addressed(p.controllers[0]->view().local,p.controllers[1]->view().local,body);
    net::Reader reader(bytes.data.data(),bytes.size());p.controllers[1]->on_message(0,reader);p.step();
    CHECK(!p.controllers[1]->view().complexity_ready);CHECK(p.controllers[1]->view().phase!=es::Phase::Frozen);
}
static void test_complexity_queue_retry_and_stale_context(){
    es::Config config;config.outbound_packets=4;Party p(2,config);std::string e;const auto proof=complexity_proof();
    for(unsigned n=0;n<2;++n)CHECK(p.controllers[n]->set_complexity_capability(proof,e));
    p.start(complexity_seed());p.accept();CHECK(p.spin([&]{return complexity_ready(p);}));
    auto changed=proof;bool rejected=false;
    for(unsigned n=0;n<20&&!rejected;++n){changed.words[0]=std::bit_cast<uint32_t>(float(n+2));
        rejected=!p.controllers[1]->set_complexity_capability(changed,e);}
    CHECK(rejected);CHECK(!p.controllers[1]->view().complexity_ready);
    for(unsigned n=0;n<20;++n)p.step();
    CHECK(p.controllers[1]->set_complexity_capability(changed,e));
    CHECK(p.spin([&]{return p.controllers[0]->view().members.at(1).complexity==changed;}));
    CHECK(p.controllers[0]->set_complexity_capability(changed,e));CHECK(p.spin([&]{return complexity_ready(p);}));
    const auto host=p.controllers[0]->view().local,guest=p.controllers[1]->view().local;
    auto ctx=p.controllers[0]->view().context;--ctx.epoch;
    net::Writer body;CHECK(w::write({ctx,w::ComplexityClaim{999,{}}},body,e));
    auto bytes=addressed(guest,host,body);net::Reader old_epoch(bytes.data.data(),bytes.size());
    p.controllers[0]->on_message(1,old_epoch);p.step();CHECK(complexity_ready(p));
    ctx=p.controllers[0]->view().context;CHECK(w::write({ctx,w::ComplexityClaim{999,{}}},body,e));
    bytes=addressed({guest.slot,guest.generation+1},host,body);net::Reader wrong_generation(bytes.data.data(),bytes.size());
    p.controllers[0]->on_message(1,wrong_generation);p.step();CHECK(complexity_ready(p));
}
static void test_complexity_disable_is_not_a_general_bypass(){
    Party p(2);std::string e;const auto proof=complexity_proof();
    for(unsigned n=0;n<2;++n)CHECK(p.controllers[n]->set_complexity_capability(proof,e));
    p.start(complexity_seed(true));p.accept();CHECK(p.spin([&]{return complexity_ready(p);}));
    const FieldKey enabled{Field::Property,kComplexityProperty};
    auto tokens=p.grant(0,{ScopeKind::Placement,object_b,{}});
    const auto field=p.grant(0,{ScopeKind::Field,kComplexityProjectObject,enabled});tokens.insert(tokens.end(),field.begin(),field.end());
    CHECK(p.controllers[1]->set_complexity_capability({},e));CHECK(p.spin([&]{return !p.controllers[0]->view().complexity_ready;}));
    auto mixed=p.edit(0,kComplexityProjectObject,enabled,false,tokens);
    mixed.mutations.push_back(p.edit(0,object_b,position,Vec3{88,1,2},tokens).mutations.front());
    const auto decisions=p.native[0].decisions;CHECK(p.controllers[0]->submit(mixed,e));
    CHECK(p.spin([&]{return p.native[0].decisions>decisions;}));
    CHECK(p.controllers[0]->state().document.revision==0&&extended_complexity(p.controllers[0]->state().document));
    CHECK(!p.controllers[0]->prepare_playtest(e));
    CHECK(p.controllers[1]->set_complexity_capability(proof,e));CHECK(p.spin([&]{return complexity_ready(p);}));
    // A command queued under a valid cohort must recheck the cohort at dispatch.
    CHECK(p.controllers[0]->prepare_playtest(e));CHECK(p.controllers[0]->set_complexity_capability({},e));
    for(unsigned n=0;n<20;++n)p.step();CHECK(p.controllers[0]->view().phase==es::Phase::Ready);
    CHECK(p.controllers[0]->set_complexity_capability(proof,e));CHECK(p.spin([&]{return complexity_ready(p);}));
    CHECK(p.controllers[0]->prepare_playtest(e));CHECK(p.spin([&]{return p.controllers[1]->view().phase==es::Phase::Preparing;}));
    CHECK(p.controllers[1]->set_complexity_capability({},e));CHECK(p.spin([&]{return !p.controllers[0]->view().complexity_ready;}));
    CHECK(!p.controllers[1]->ready_playtest(true,e));CHECK(p.controllers[1]->ready_playtest(false,e));
    CHECK(p.controllers[0]->cancel_playtest(e));CHECK(p.spin([&]{return p.controllers[1]->view().phase==es::Phase::Ready;}));
}
