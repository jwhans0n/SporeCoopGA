// Production native complexity hooks with controlled i686 code/data receipts.
// No running game, profile, resource registration or authored file is used.
namespace {
std::array<uint8_t,editor_native_complexity::kFormulaCode.size()> cx_formula;
std::array<uint8_t,11> cx_create,cx_clone;
std::array<uint8_t,12*16> cx_weights;
float cx_maximum=10;
uint32_t cx_live=0;
unsigned cx_original_calls=0;
std::optional<float> cx_override;
std::function<void()> cx_callback;
bool cx_cohort_ready=true;
void* cx_count_memory=nullptr;
std::array<uint8_t,kResourceSize> cx_other_resource;
std::array<uint32_t,12> cx_counts;

float SK_THISCALL cx_original(void* self,const uint32_t* counts){
    CHECK(self);++cx_original_calls;
    editor_native_complexity::Tuning tuning;
    for(size_t i=0;i<12;++i)tuning.weights[i]=skg::field<float>(cx_weights.data(),uint32_t(i*16));
    tuning.maximum=cx_maximum;
    std::array<uint32_t,12> input;std::memcpy(input.data(),counts,sizeof input);
    float weighted=0;CHECK(editor_native_complexity::weighted_ratio(input,tuning,weighted));
    float result=cx_override.value_or(std::max(weighted,float(cx_live+1)*(1.f/1024.f)));
    if(cx_callback){auto callback=std::move(cx_callback);cx_callback={};callback();}
    return result;
}
void cx_setup(uint32_t count=1){
    reset();cx_live=count;cx_original_calls=0;cx_override.reset();cx_callback={};cx_counts={};cx_maximum=10;
    complexity_conflicts=0;complexity_conflict.clear();
    cx_formula=editor_native_complexity::kFormulaCode;complexity_installed_code=cx_formula;
    cx_create={0x3D,0,4,0,0,0x0F,0x83,0xD7,1,0,0};
    cx_clone={0x3D,0,4,0,0,0x0F,0x83,0x9D,0,0,0};
    cx_weights.fill(0);for(size_t i=0;i<12;++i)skg::field<float>(cx_weights.data(),uint32_t(i*16))=1;
    a_complexity_ratio=uintptr_t(cx_formula.data());a_complexity_count=uintptr_t(cx_count_memory);
    a_complexity_weights=uintptr_t(cx_weights.data());a_complexity_max=uintptr_t(&cx_maximum);
    a_complexity_create=uintptr_t(cx_create.data());a_complexity_clone=uintptr_t(cx_clone.data());
    complexity_bounds={};CHECK(complexity_bounds.initialize(a_complexity_create,a_complexity_clone));
    complexity_code_pinned=complexity_available=true;original_complexity=cx_original;
    C.projection_scope=[] {return t_scope;};
    editor::ComplexityCapability expected;std::string error;CHECK(read_complexity_capability(expected,error));cx_cohort_ready=true;
    C.complexity_current=[expected](const editor::ComplexityCapability& value){return cx_cohort_ready&&value==expected;};
    auto& project=act_identity.settings[kProjectObject];project.object=kProjectObject;project.kind=editor::SettingsKind::Project;
    project.properties[kExtendedComplexityProperty]=true;
    skg::field<editor::ResourceKey>(t_live.data(),8)={1,2,3};
    auto& markers=t_rows[t_live.data()];markers.assign(size_t(count)*0x238,0);rows_for(t_live.data());
    skg::field<uintptr_t>(t_live.data(),0x2C18)=skg::field<uintptr_t>(t_live.data(),0x2C14);
    skg::field<uint32_t>(t_live.data(),0x2C24)=0;
    for(size_t n=0;n<count;++n)skg::field<uint32_t>(markers.data()+n*0x238,0)=0x40000000;
}
void cx_expect_unavailable(editor::ComplexityReason reason){
    editor::ComplexityCapability out;out.available=true;out.words.fill(0xFFFF);std::string error;
    CHECK(!complexity_capability(out,error));CHECK(editor::valid_complexity(out));
    CHECK((!out.available&&out.reason==reason&&out.words==std::array<uint32_t,13>{}&&!error.empty()));
}
void test_native_complexity_capability(){
    cx_count_memory=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);CHECK(cx_count_memory);
    std::memcpy(cx_count_memory,complexity_count_code.data(),complexity_count_code.size());DWORD previous=0;
    CHECK(VirtualProtect(cx_count_memory,4096,PAGE_EXECUTE_READ,&previous));CHECK(FlushInstructionCache(GetCurrentProcess(),cx_count_memory,4096));
    cx_setup();editor::ComplexityCapability evidence;std::string error;
    CHECK(complexity_capability(evidence,error)&&error.empty()&&evidence.available&&editor::valid_complexity(evidence));
    CHECK(evidence.policy==editor::kComplexityPolicy&&evidence.formula==editor::kComplexityFormula);
    for(size_t i=0;i<13;++i)CHECK(evidence.words[i]==std::bit_cast<uint32_t>(i==12?10.f:1.f));
    // Getter is independent of mode, scene, active authoring and callbacks.
    t_game_mode_id=modes::kEditor;t_active=false;t_mode_pointer=nullptr;
    C.active=[]()->bool{CHECK(false);return false;};C.projection_scope=[]()->GestureScope{CHECK(false);return {};};
    CHECK(complexity_capability(evidence,error)&&evidence.available&&cx_original_calls==0);
    for(unsigned kind=0;kind<6;++kind){
        cx_setup();if(kind==0)complexity_available=false;if(kind==1)complexity_code_pinned=false;
        if(kind==2)cx_formula[55]^=1;if(kind==3)cx_create[1]^=1;if(kind==4)cx_clone[8]^=1;
        if(kind==5)a_complexity_count=0;
        cx_expect_unavailable(kind<2?editor::ComplexityReason::Unavailable:editor::ComplexityReason::CodeConflict);
    }
    cx_setup();DWORD old=0;CHECK(VirtualProtect(cx_count_memory,4096,PAGE_EXECUTE_READWRITE,&old));
    static_cast<uint8_t*>(cx_count_memory)[0]^=1;cx_expect_unavailable(editor::ComplexityReason::CodeConflict);
    static_cast<uint8_t*>(cx_count_memory)[0]^=1;CHECK(VirtualProtect(cx_count_memory,4096,old,&old));
    for(size_t i=0;i<13;++i){
        cx_setup();if(i==12)cx_maximum=std::numeric_limits<float>::infinity();
        else skg::field<float>(cx_weights.data(),uint32_t(i*16))=std::numeric_limits<float>::quiet_NaN();
        cx_expect_unavailable(editor::ComplexityReason::InvalidTuning);
    }
    for(const auto maximum:{0.f,-1.f}){cx_setup();cx_maximum=maximum;cx_expect_unavailable(editor::ComplexityReason::InvalidTuning);}
    cx_setup();skg::field<float>(cx_weights.data(),16)=-1;cx_expect_unavailable(editor::ComplexityReason::InvalidTuning);
    cx_setup();a_complexity_weights=0;cx_expect_unavailable(editor::ComplexityReason::Unavailable);
    cx_setup();a_complexity_max=0;cx_expect_unavailable(editor::ComplexityReason::Unavailable);
    // Exact thirteen-word sampling: each changed word is retained independently.
    for(size_t i=0;i<13;++i){cx_setup();if(i==12)cx_maximum=11;else skg::field<float>(cx_weights.data(),uint32_t(i*16))=2;
        CHECK(complexity_capability(evidence,error));CHECK(evidence.words[i]==std::bit_cast<uint32_t>(i==12?11.f:2.f));}
    // Default queries forward the original result without any Extended proof.
    cx_setup();act_identity.settings[kProjectObject].properties[kExtendedComplexityProperty]=false;
    C.active=[]()->bool{CHECK(false);return false;};C.projection_scope=[]()->GestureScope{CHECK(false);return {};};
    complexity_available=false;cx_override=.734375f;CHECK(complexity_ratio(t_data.data(),cx_counts.data())==.734375f&&cx_original_calls==1);
    cx_setup();cx_override=.6875f;CHECK(complexity_ratio(t_other_data.data(),cx_counts.data())==.6875f&&cx_original_calls==1);
    for(unsigned flag=0;flag<2;++flag){
        cx_setup();cx_override=.625f;if(flag)skg::field<int>(t_mode.data(),0xD0)=1;else skg::field<int>(t_mode.data(),0xCC)=0;
        C.active=[]()->bool{CHECK(false);return false;};C.projection_scope=[]()->GestureScope{CHECK(false);return {};};
        C.complexity_current=[](const auto&)->bool{CHECK(false);return false;};
        CHECK(complexity_ratio(t_data.data(),cx_counts.data())==.625f&&cx_original_calls==1);
    }
    // Even the first shared callback cannot rotate the same owner's resource
    // and then let the hook adopt its replacement as the original caller.
    for(unsigned stage=0;stage<2;++stage)for(unsigned kind=0;kind<4;++kind){
        cx_setup();auto expire=[kind]{
            if(kind==0)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
            if(kind==1)skg::field<void*>(t_data.data(),0x10)=cx_other_resource.data();
            if(kind==2)++generation;if(kind==3)++cx_counts[0];
        };
        if(!stage)C.active=[expire]{expire();return true;};
        else C.projection_scope=[expire]{expire();return t_scope;};
        CHECK(complexity_ratio(t_data.data(),cx_counts.data())>=1.f&&!complexity_conflict.empty());
        if(kind!=3)CHECK(cx_original_calls==0);
    }
    cx_setup();C.active=[] {++generation;return false;};
    CHECK(complexity_ratio(t_data.data(),cx_counts.data())>=1.f&&!cx_original_calls&&!complexity_conflict.empty());
    cx_setup();C.active=[]()->bool{throw 7;};
    CHECK(complexity_ratio(t_data.data(),cx_counts.data())>=1.f&&!cx_original_calls&&!complexity_conflict.empty());
    for(unsigned missing=0;missing<3;++missing){
        cx_setup();if(!missing)C.complexity_current={};
        if(missing==1)C.complexity_current=[](const auto&){return false;};
        if(missing==2)C.complexity_current=[](const auto&)->bool{throw 7;};
        CHECK(complexity_ratio(t_data.data(),cx_counts.data())>=1.f&&!cx_original_calls&&!complexity_conflict.empty());
    }
    cx_setup();cx_callback=[]{cx_cohort_ready=false;};
    CHECK(complexity_ratio(t_data.data(),cx_counts.data())>=1.f&&cx_original_calls==1&&!complexity_conflict.empty());
    for(unsigned kind=0;kind<7;++kind){
        cx_setup();bool changed=false;C.complexity_current=[&](const auto&){
            if(!changed){changed=true;
                if(kind==0)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
                if(kind==1)++generation;if(kind==2)++cx_counts[0];if(kind==3)cx_maximum=11;
                if(kind==4)skg::field<uint32_t>(t_rows[t_live.data()].data(),0)=0;
                if(kind==5)cx_formula[0]^=1;if(kind==6)++t_scope.epoch;
            }return true;
        };
        CHECK(complexity_ratio(t_data.data(),cx_counts.data())>=1.f&&changed&&!cx_original_calls&&!complexity_conflict.empty());
    }
    // Mutation in the LAST read-scope callback must precede the final raw
    // closure, rather than following already checked counts/code/tuning.
    cx_setup();unsigned queries=0;C.projection_scope=[&]{++queries;return t_scope;};
    CHECK(complexity_ratio(t_data.data(),cx_counts.data())<1.f&&cx_original_calls==1);const auto final_query=queries;CHECK(final_query>0);
    for(unsigned kind=0;kind<4;++kind){
        cx_setup();queries=0;bool changed=false;C.projection_scope=[&]{
            if(++queries==final_query){changed=true;if(kind==0)++cx_counts[0];if(kind==1)cx_maximum=11;
                if(kind==2)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;if(kind==3)cx_formula[0]^=1;}
            return t_scope;
        };
        CHECK(complexity_ratio(t_data.data(),cx_counts.data())>=1.f&&changed&&cx_original_calls==1&&!complexity_conflict.empty());
    }
    // Existing count4095 permits ordinary creation4096; count4096 denies4097.
    for(const uint32_t count:{0u,1u,1023u,1024u,4095u,4096u}){
        cx_setup(count);ComplexityPool pool;uint32_t actual=UINT32_MAX;
        CHECK(complexity_pool(t_live.data(),pool,actual)&&actual==count);
        CHECK(skg::cdecl_call<uint32_t>(a_complexity_count,reinterpret_cast<void*>(pool.first),reinterpret_cast<void*>(pool.end))==count);
        const auto ratio=complexity_ratio(t_data.data(),cx_counts.data());
        CHECK(ratio==float(count)/4096.f&&cx_original_calls==1&&complexity_conflict.empty());
        CHECK((ratio<1.f)==(count<4096));
    }
    cx_setup(0);cx_counts[0]=40;CHECK(complexity_ratio(t_data.data(),cx_counts.data())==1.f);
    cx_counts[0]=41;CHECK(complexity_ratio(t_data.data(),cx_counts.data())>1.f);
    cx_counts[0]=39;CHECK(complexity_ratio(t_data.data(),cx_counts.data())<1.f);
    cx_setup();cx_override=0;CHECK(complexity_ratio(t_data.data(),cx_counts.data())==1.f&&!complexity_conflict.empty());
    // The original native query is a callback boundary: every input must close.
    for(unsigned kind=0;kind<13;++kind){
        cx_setup();cx_callback=[kind]{
            if(kind==0)++generation;if(kind==1)++t_scope.epoch;if(kind==2)++t_scope.author.generation;
            if(kind==3)++t_scope.project[0];if(kind==4)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
            if(kind==5)skg::field<void*>(t_data.data(),0x10)=cx_other_resource.data();
            if(kind==6)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
            if(kind==7)++cx_counts[1];if(kind==8)cx_maximum=11;if(kind==9)cx_formula[3]^=1;
            if(kind==10)skg::field<uint32_t>(t_rows[t_live.data()].data(),0)=0;
            if(kind==11)skg::field<uint32_t>(t_live.data(),0x2C24)=1;
            if(kind==12)act_identity.settings[kProjectObject].properties[kExtendedComplexityProperty]=false;
        };
        CHECK(complexity_ratio(t_data.data(),cx_counts.data())==1.f&&cx_original_calls==1&&!complexity_conflict.empty());
    }
    // Faithful iterator handles skip flags and rejects an unowned sentinel.
    cx_setup(3);auto& row=t_rows[t_live.data()];
    skg::field<uint32_t>(row.data(),0)=0;skg::field<uint32_t>(row.data()+0x238,0)=0xC0000000;
    ComplexityPool pool;uint32_t counted=0;CHECK(complexity_pool(t_live.data(),pool,counted)&&counted==2);
    CHECK(skg::cdecl_call<uint32_t>(a_complexity_count,reinterpret_cast<void*>(pool.first),reinterpret_cast<void*>(pool.end))==counted);
    cx_setup();skg::field<uint32_t>(t_rows[t_live.data()].data(),0)=0;
    CHECK(!complexity_pool(t_live.data(),pool,counted));
    cx_setup(2);skg::field<uint32_t>(t_live.data(),0x2C24)=3;CHECK(!complexity_pool(t_live.data(),pool,counted));
    cx_setup(2);skg::field<uintptr_t>(t_live.data(),0x2C18)--;CHECK(!complexity_pool(t_live.data(),pool,counted));
    cx_setup(2);skg::field<uint32_t>(t_live.data(),0x2C24)=0x3FFFFFFF;
    CHECK(complexity_pool(t_live.data(),pool,counted)&&counted==0);
    // Native hard bounds remain1024 by default and only valid Extended proof enables4096.
    cx_setup();CHECK(update_complexity(error)&&complexity_bounds.active());
    CHECK(skg::field<uint32_t>(cx_create.data(),1)==4096&&skg::field<uint32_t>(cx_clone.data(),1)==4096);
    act_identity.settings[kProjectObject].properties[kExtendedComplexityProperty]=false;
    CHECK(update_complexity(error)&&!complexity_bounds.active());
    CHECK(skg::field<uint32_t>(cx_create.data(),1)==1024&&skg::field<uint32_t>(cx_clone.data(),1)==1024);
    cx_setup();cx_maximum=0;CHECK(update_complexity(error)&&!complexity_bounds.active());
    cx_setup();cx_formula[0]^=1;CHECK(!update_complexity(error)&&!complexity_bounds.active()&&!error.empty());
    complexity_bounds={};complexity_available=complexity_code_pinned=false;
    a_complexity_ratio=a_complexity_count=a_complexity_weights=a_complexity_max=a_complexity_create=a_complexity_clone=0;
    original_complexity=nullptr;CHECK(VirtualFree(cx_count_memory,0,MEM_RELEASE));cx_count_memory=nullptr;reset();
}
} // namespace
