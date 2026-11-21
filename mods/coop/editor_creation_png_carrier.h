#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>

// Private GOG 2024 PNG carrier witness. Native PngEncoder reads top-down
// BGRA bytes, using the LFSR/FNV path at 68DDB0/68DFC0 and header at 68E660.
// This recognizes the header only; it never imports or decodes a creation.
namespace editor_creation_png_carrier {
inline constexpr std::array<uint32_t,32> polynomials{
    0x00000000,0x00000000,0x00000003,0x00000006,
    0x0000000C,0x00000014,0x00000030,0x00000060,
    0x000000B8,0x00000110,0x00000240,0x00000500,
    0x00000CA0,0x00001B00,0x00003500,0x00006000,
    0x0000B400,0x00012000,0x00020400,0x00072000,
    0x00090000,0x00140000,0x00300000,0x00400000,
    0x00D80000,0x01200000,0x03880000,0x07200000,
    0x09000000,0x14000000,0x32800000,0x48000000
};

inline std::optional<std::array<uint8_t,8>> header(std::span<const uint8_t> bgra,bool alternate){
    // The caller supplies a bounded decoded RGBA8 thumbnail converted to
    // BGRA8. Too-small carriers cannot establish an eight-byte witness.
    if(bgra.size()<32 || bgra.size()>(1u<<20) || bgra.size()%4)return std::nullopt;
    unsigned highest=0;for(auto count=bgra.size();count>1;count>>=1)++highest;
    const uint32_t polynomial=polynomials[highest];
    uint32_t state=polynomial,mask=1,high_mask=alternate?~uint32_t(1):~uint32_t(7),hash=0x811C9DC5;
    auto step=[&]{
        state=(state>>1)^((state&1)?polynomial:0);
        if(state==polynomial){mask<<=1;high_mask<<=1;}
    };
    std::array<uint8_t,8> result{};
    for(auto& byte:result)for(unsigned bit=0;bit<8;++bit){
        while(state>=bgra.size() && mask<8)step();
        if(mask>=8 || !state)return std::nullopt;
        hash=(hash*0x01000193)^uint8_t((state&~high_mask)|(uint32_t(bgra[state])&high_mask));
        const bool value=bool(uint32_t(bgra[state])&mask)^bool(hash&0x8000);
        byte|=uint8_t(unsigned(value)<<bit);
        step();
    }
    return result;
}

inline bool valid_magic(const std::array<uint8_t,8>& value){
    return value[0]==0x16 && value[1]==0x0D && value[2]==0x45 && value[3]==0x02;
}

inline bool rejects_headers(std::span<const uint8_t> bgra){
    const auto normal=header(bgra,false),alternate=header(bgra,true);
    // A failed witness is not proof that an image is free of native data.
    return normal && alternate && !valid_magic(*normal) && !valid_magic(*alternate);
}
} // namespace editor_creation_png_carrier
