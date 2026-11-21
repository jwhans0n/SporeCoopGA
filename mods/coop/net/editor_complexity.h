// Read-only native tuning evidence, independent of a loaded Adventure scene.
#pragma once
#include "editor_document.h"
#include <bit>
#include <cmath>

namespace editor {
// Policy2 includes stable marker-arena capacity before Native actor binding.
// Legacy1 remains a decodable value, never current Extended authority.
inline constexpr uint32_t kLegacyComplexityPolicy=1,kComplexityPolicy=2,kComplexityFormula=0xDC04DB80;
inline constexpr ObjectId kComplexityProjectObject{12ull<<32,1};
inline constexpr uint32_t kComplexityProperty=0x434F4D50;
enum class ComplexityReason:uint8_t {None,Unavailable,CodeConflict,InvalidTuning,Changed};
struct ComplexityCapability {
    uint32_t policy=kComplexityPolicy,formula=kComplexityFormula;
    std::array<uint32_t,13> words{}; // twelve weights in category order, then MaxComplexity
    bool available=false;
    ComplexityReason reason=ComplexityReason::Unavailable;
    bool operator==(const ComplexityCapability&)const=default;
};
inline bool valid_complexity(const ComplexityCapability& value){
    if((value.policy!=kLegacyComplexityPolicy&&value.policy!=kComplexityPolicy)||
       value.formula!=kComplexityFormula||value.reason>ComplexityReason::Changed)return false;
    if(!value.available)return value.reason!=ComplexityReason::None&&value.words==std::array<uint32_t,13>{};
    if(value.reason!=ComplexityReason::None)return false;
    for(size_t n=0;n<value.words.size();++n){
        const auto f=std::bit_cast<float>(value.words[n]);
        if(!std::isfinite(f)||(n==12?f<=0:f<0))return false;
    }
    return true;
}
inline bool extended_complexity(const Document& document){
    auto row=document.settings.find(kComplexityProjectObject);if(row==document.settings.end())return false;
    auto field=row->second.properties.find(kComplexityProperty);if(field==row->second.properties.end())return false;
    const auto* enabled=std::get_if<bool>(&field->second);return enabled&&*enabled;
}
} // namespace editor
