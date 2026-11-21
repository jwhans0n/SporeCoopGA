#include "editor_native_complexity_ratio.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace editor_native_complexity;
static float native(float weighted,uint32_t count){const float marker=float(count+1)/1024;return weighted>marker?weighted:marker;}
int main(){
    CHECK(kFormulaCode.size()==0x144);CHECK(formula_matches(kFormulaCode,0xF3DB80));
    for(size_t n=0;n<kFormulaCode.size();++n){auto bad=kFormulaCode;bad[n]^=1;CHECK(!formula_matches(bad,0xF3DB80));}
    for(const uint32_t relocation:{0x200000u,0xFFF00000u}){
        auto moved=kFormulaCode;
        for(const auto n:kFormulaRelocations){uint32_t value=0;for(size_t b=0;b<4;++b)value|=uint32_t(moved[n+b])<<(b*8);
            value+=relocation;for(size_t b=0;b<4;++b)moved[n+b]=uint8_t(value>>(b*8));}
        CHECK(formula_matches(moved,uint32_t(0xF3DB80u+relocation)));
        CHECK(!formula_matches(moved,0xF3DB80));
    }
    CHECK(!formula_matches(std::span(kFormulaCode).first(0x143),0xF3DB80));
    Tuning tuning;tuning.maximum=10;tuning.weights[0]=1;
    std::array<uint32_t,12> counts{};float out=37,weighted=0;
    CHECK(weighted_ratio(counts,tuning,weighted));CHECK(same_ratio(weighted,0));
    // Native strict comparison is intentionally unchanged by this helper:
    // its marker-only term already refuses the 1024th placement.
    CHECK(native(0,1022)<1);CHECK(native(0,1023)==1);
    for(uint32_t n:{0u,1022u,1023u,1024u,4094u,4095u,4096u}){
        CHECK(extended_ratio(native(0,n),counts,n,tuning,out));
        CHECK(same_ratio(out,float(n)/4096));CHECK((out<1)==(n<4096));
    }
    // Weighted limits remain strict at exactly4; moving only the marker term
    // cannot make a weighted-limit adventure eligible.
    counts[0]=1;tuning.weights[0]=40;
    for(uint32_t n:{0u,1023u,4095u,4096u}){
        CHECK(extended_ratio(native(4,n),counts,n,tuning,out));CHECK(out>=1);
    }
    tuning.weights[0]=std::bit_cast<float>(uint32_t{0x421FFFFF}); // immediately below40
    CHECK(weighted_ratio(counts,tuning,weighted));CHECK(weighted<4);
    CHECK(extended_ratio(native(weighted,4095),counts,4095,tuning,out));CHECK(out<1);
    CHECK(extended_ratio(native(weighted,4096),counts,4096,tuning,out));CHECK(out==1);
    // Independent reviewed native operation-order receipt: small terms in the
    // high group round away before the low group's exactly representable2.
    counts.fill(1);tuning.weights.fill(0);tuning.maximum=16777216;
    tuning.weights[11]=16777216;tuning.weights[10]=1;tuning.weights[9]=1;
    tuning.weights[5]=1;tuning.weights[4]=1;
    CHECK(weighted_ratio(counts,tuning,weighted));CHECK(std::bit_cast<uint32_t>(weighted)==0x3F800001);
    CHECK(extended_ratio(native(weighted,4095),counts,4095,tuning,out));CHECK(out<1);
    // All 12 independently tunable categories participate with their current
    // weights; no hard-coded default weighted denominator or class exemption.
    for(size_t i=0;i<12;++i){
        counts.fill(0);counts[i]=8;tuning.weights.fill(0);tuning.weights[i]=5;tuning.maximum=10;
        CHECK(weighted_ratio(counts,tuning,weighted));CHECK(weighted==4);
        CHECK(extended_ratio(native(weighted,4095),counts,4095,tuning,out));CHECK(out==1);
    }
    // Unsupported external patches, stale observations and invalid tuning fail
    // atomically; the adapter must deny rather than quietly widen the budget.
    counts.fill(0);tuning.weights.fill(0);tuning.maximum=10;
    for(unsigned which=0;which<10;++which){
        auto bad_counts=counts;auto bad=tuning;float original=4;uint32_t count=4095;out=37;
        if(which==0)original=std::bit_cast<float>(uint32_t{0x407FFFFF});
        if(which==1)original=std::numeric_limits<float>::quiet_NaN();
        if(which==2)count=4097;
        if(which==3)bad.maximum=0;
        if(which==4)bad.maximum=-10;
        if(which==5)bad.maximum=std::numeric_limits<float>::infinity();
        if(which==6)bad.weights[2]=-1;
        if(which==7)bad.weights[7]=std::numeric_limits<float>::quiet_NaN();
        if(which==8)bad_counts[11]=uint32_t(INT32_MAX)+1;
        if(which==9){bad_counts[1]=uint32_t(INT32_MAX);bad.weights[1]=std::numeric_limits<float>::max();}
        CHECK(!extended_ratio(original,bad_counts,count,bad,out));CHECK(same_ratio(out,37));
    }
    std::printf("editor native complexity ratio: %u checks passed\n",checks);
}
