// Values from one already-occurring native ratio query, never editing authority.
bool environment_interaction_idle();
struct ComplexityObservationPin {
    ComplexityObservationState state=ComplexityObservationState::Disarmed;
    uint64_t serial=0,generation=0,gesture=0;
    GestureScope scope;
    editor::Revision revision=0;
    SceneOwner owner;
    const SkHost* host=nullptr;
    decltype(SkHost::frame_index) frame_source=nullptr;
    ComplexityFn original=nullptr;
    ComplexityObservation observation;
    ComplexityPool pool;
    std::vector<std::array<uint32_t,2>> identities;
};
ComplexityObservationPin complexity_observed;
uint64_t complexity_observation_serial=0;
extern uint64_t gesture_counter;
bool complexity_observation_owner(const ComplexityObservationPin& pin){
    skg::ReadOnlyMemoryScope memory;
    auto* m=mode();auto* d=data(m);auto* r=resource(d);
    return supported && H && H==pin.host && pin.frame_source && H->frame_index==pin.frame_source &&
        generation==pin.generation && original_complexity==pin.original &&
        native_owner_thread==GetCurrentThreadId() && uintptr_t(m)==pin.owner.mode && uintptr_t(d)==pin.owner.data &&
        uintptr_t(r)==pin.owner.resource && skg::readable(m,0xDC) && skg::field<int>(m,0xCC)==1 && !skg::field<int>(m,0xD0) &&
        skg::readable(r,kResourceSize) && skg::field<editor::ResourceKey>(r,8)==pin.owner.source &&
        act_identity.project==pin.scope.project && act_identity.epoch==pin.scope.epoch &&
        gesture_counter==pin.gesture;
}
bool complexity_observation_rows(void* r,ComplexityPool& pool,uint32_t& count,std::vector<std::array<uint32_t,2>>& identities){
    if(!complexity_pool(r,pool,count))return false;
    std::vector<std::array<uint32_t,2>> rows;rows.reserve(pool.headers.size());
    for(size_t i=0;i<pool.headers.size();++i){
        auto* marker=reinterpret_cast<void*>(pool.begin+i*0x238+4);
        rows.push_back({skg::field<uint32_t>(marker,0),skg::field<uint32_t>(marker,0x228)});
    }
    identities=std::move(rows);return true;
}
struct ComplexityDiagnosticCapture {
    uint64_t serial=0;
    const uint32_t* pointer=nullptr; // Stack-local only, never stored in the observation.
    ComplexityObservationPin pin;
    std::array<uint32_t,12> counts{};
    editor::ComplexityCapability tuning;
    uint32_t markers=0;
    ComplexityDiagnosticCapture(void* self,const uint32_t* input)noexcept{
        try{
            if(!native_owner_thread || GetCurrentThreadId()!=native_owner_thread ||
               complexity_observed.state!=ComplexityObservationState::Pending ||
               uintptr_t(self)!=complexity_observed.owner.data || !environment_interaction_idle())return;
            pin=complexity_observed;
            complexity_observed.state=ComplexityObservationState::Expired;
            if(!complexity_observation_owner(pin) || !skg::readable(input,sizeof counts))return;
            std::memcpy(counts.data(),input,sizeof counts);std::string error;
            if(!read_complexity_capability(tuning,error) || !complexity_observation_owner(pin) ||
               !complexity_observation_rows(reinterpret_cast<void*>(pin.owner.resource),pin.pool,markers,pin.identities))return;
            serial=pin.serial;pointer=input;
        }catch(...){ }
    }
    void finish(float original,float effective,bool extended)noexcept{
        if(!serial)return;
        try{
            const auto current=[&]{return complexity_observed.serial==serial &&
                complexity_observed.state==ComplexityObservationState::Expired && complexity_observation_owner(pin);};
            if(!current() || !environment_interaction_idle())return;
            const auto frame_callback=pin.frame_source;
            const auto frame=frame_callback?frame_callback():0;
            if(!current() || H->frame_index!=frame_callback)return;
            editor::ComplexityCapability after;std::string error;
            ComplexityPool pool;uint32_t count=0;std::vector<std::array<uint32_t,2>> identities;
            if(!read_complexity_capability(after,error) || after!=tuning || !current() ||
               !skg::readable(pointer,sizeof counts) || std::memcmp(counts.data(),pointer,sizeof counts) ||
               !complexity_observation_rows(reinterpret_cast<void*>(pin.owner.resource),pool,count,identities) ||
               pool!=pin.pool || count!=markers || identities!=pin.identities || !current())return;
            editor_native_complexity::Tuning values;
            for(size_t i=0;i<12;++i)values.weights[i]=std::bit_cast<float>(tuning.words[i]);
            values.maximum=std::bit_cast<float>(tuning.words[12]);float weighted=0,corrected=0;
            if(!editor_native_complexity::weighted_ratio(counts,values,weighted) ||
               !editor_native_complexity::extended_ratio(original,counts,markers,values,corrected) ||
               !editor_native_complexity::same_ratio(effective,extended?corrected:original))return;
            const auto clock=work_clock();if(!clock)return;
            pin.observation={serial,frame,clock,pin.generation,markers,counts,tuning.words,
                weighted,float(markers+1)*(1.f/1024.f),float(markers)*(1.f/4096.f),original,effective,extended};
            pin.state=ComplexityObservationState::Ready;serial=0;
            complexity_observed=std::move(pin);
        }catch(...){ }
    }
};
