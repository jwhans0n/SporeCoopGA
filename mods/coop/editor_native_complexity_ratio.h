// F3DB80's reviewed GOG 2024 scalar SSE arithmetic. Extended editing changes
// only its marker term from (N+1)/4096 to N/4096, so strict native CanPlace
// accepts placement4096 and refuses4097. The weighted term remains strict.
#pragma once
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <xmmintrin.h>
#include <emmintrin.h>

namespace editor_native_complexity {
namespace detail {
template<size_t N> consteval auto unhex(const char (&text)[N]){
    std::array<uint8_t,(N-1)/2> result{};
    auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(size_t n=0;n<result.size();++n)result[n]=uint8_t((digit(text[n*2])<<4)|digit(text[n*2+1]));
    return result;
}
}
inline constexpr auto kFormulaCode=detail::unhex(
    "518b442408f30f2a402cf30f590534896c01f30f2a4828f30f590d24896c01"
    "f30f2a5010f30f5915c4886c01f30f58c1f30f2a4824f30f590d14896c01f30f58c1"
    "f30f2a4820f30f590d04896c01f30f58c1f30f2a481cf30f590df4886c01f30f58c1"
    "f30f2a4818f30f590de4886c01f30f58c1f30f2a4814f30f590dd4886c01f30f58ca"
    "f30f2a500cf30f5915b4886c01f30f58caf30f2a5008f30f5915a4886c01f30f58ca"
    "f30f2a5004f30f591594886c01f30f58caf30f2a108b4110f30f591584886c01"
    "8b88242c00008b90142c000005102c0000f30f58caf30f58c1f30f5e0544896c01"
    "f30f1144240881f9ffffff3f730a69c9380200000308eb028bca8b442408505251"
    "e8223cfbff4083c40c890424db042485c07d06d805c04f3f01d80df00141018d0424"
    "d91424d9442408d9c9dff1ddd877048d442408d90059c20400");
// All absolute data operands in the reviewed function. Relative code calls
// relocate with the image and retain their displacement unchanged.
inline constexpr std::array<size_t,15> kFormulaRelocations{
    0x0E,0x1B,0x28,0x39,0x4A,0x5B,0x6C,0x7D,0x8E,0x9F,0xB0,0xC3,0xE4,0x11E,0x124};
inline bool formula_matches(std::span<const uint8_t> code,uintptr_t entry){
    if(code.size()!=kFormulaCode.size()||entry>UINT32_MAX)return false;
    const auto relocation=uint32_t(entry)-0xF3DB80u;
    for(size_t n=0;n<code.size();){
        bool operand=false;
        for(const auto at:kFormulaRelocations)if(n==at){operand=true;break;}
        if(operand){
            uint32_t expected=0,actual=0;
            for(size_t b=0;b<4;++b){expected|=uint32_t(kFormulaCode[n+b])<<(b*8);actual|=uint32_t(code[n+b])<<(b*8);}
            if(actual!=uint32_t(expected+relocation))return false;
            n+=4;
        }else {if(code[n]!=kFormulaCode[n])return false;++n;}
    }
    return true;
}
struct Tuning {std::array<float,12> weights{};float maximum=0;};
inline bool same_ratio(float a,float b){return std::bit_cast<uint32_t>(a)==std::bit_cast<uint32_t>(b);}
inline bool weighted_ratio(const std::array<uint32_t,12>& counts,const Tuning& tuning,float& out){
    if(!std::isfinite(tuning.maximum)||tuning.maximum<=0)return false;
    for(size_t i=0;i<counts.size();++i)
        if(counts[i]>uint32_t(INT32_MAX)||!std::isfinite(tuning.weights[i])||tuning.weights[i]<0)return false;
    auto term=[&](size_t i){return _mm_mul_ss(_mm_cvtsi32_ss(_mm_setzero_ps(),int32_t(counts[i])),_mm_set_ss(tuning.weights[i]));};
    // Match both native accumulation groups and their addition order. Scalar
    // SSE intrinsics prevent an x87 accumulator or a reordered double sum.
    auto high=term(11);
    for(size_t i=10;i>=6;--i)high=_mm_add_ss(high,term(i));
    auto low=term(5);
    for(int i=4;i>=0;--i)low=_mm_add_ss(low,term(size_t(i)));
    const auto value=_mm_cvtss_f32(_mm_div_ss(_mm_add_ss(high,low),_mm_set_ss(tuning.maximum)));
    if(!std::isfinite(value)||value<0)return false;
    out=value;return true;
}
// Output is atomic. Callers must deny placement on false, never infer a
// weighted contribution from the already-maximized original result.
inline bool extended_ratio(float original,const std::array<uint32_t,12>& counts,
                           uint32_t live_markers,const Tuning& tuning,float& out){
    if(live_markers>4096||!std::isfinite(original))return false;
    float weighted=0;if(!weighted_ratio(counts,tuning,weighted))return false;
    const auto original_marker=float(live_markers+1)*(1.0f/1024.0f);
    const auto expected=weighted>original_marker?weighted:original_marker;
    if(!same_ratio(original,expected))return false;
    const auto marker=float(live_markers)*(1.0f/4096.0f);
    const auto budget=weighted*0.25f;
    out=budget>marker?budget:marker;return true;
}
} // namespace editor_native_complexity
