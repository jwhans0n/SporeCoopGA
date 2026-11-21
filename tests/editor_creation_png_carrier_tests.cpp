#include "../mods/coop/editor_creation_png_carrier.h"

#include <cstdio>
#include <cstdlib>
#include <utility>
#include <vector>

namespace {
unsigned checks=0;
#define CHECK(value) do{++checks;if(!(value)){std::fprintf(stderr,"FAIL %u: %s\n",__LINE__,#value);std::exit(1);}}while(false)

void actual_native_header(){
    // Independently observed decoded carrier bytes from the selected native
    // 128x128 Spice Crawler PNG, SHA e846add23bd3d4b3ce9f1e3bae6ca4a57e6213532a976ad32bea0acafdcf7632.
    // Only the first64 visited BGRA byte values are needed for an8-byte header.
    // Receipt: out/preview9-c12-crew-thumbnail-offline/source-header-analysis.json.
    constexpr std::array<std::pair<uint32_t,uint8_t>,64> samples{{
        {0xB400,0x00},{0x5A00,0x00},{0x2D00,0x00},{0x1680,0x01},
        {0x0B40,0x00},{0x05A0,0x01},{0x02D0,0x00},{0x0168,0x00},
        {0x00B4,0x00},{0x005A,0x01},{0x002D,0x00},{0xB416,0x28},
        {0x5A0B,0x00},{0x9905,0x48},{0xF882,0x00},{0x7C41,0x58},
        {0x8A20,0x28},{0x4510,0x2D},{0x2288,0x01},{0x1144,0x00},
        {0x08A2,0x00},{0x0451,0x01},{0xB628,0xAC},{0x5B14,0x20},
        {0x2D8A,0x00},{0x16C5,0x00},{0xBF62,0x29},{0x5FB1,0x00},
        {0x9BD8,0x01},{0x4DEC,0x01},{0x26F6,0x00},{0x137B,0x00},
        {0xBDBD,0x53},{0xEADE,0x00},{0x756F,0x01},{0x8EB7,0x29},
        {0xF35B,0x00},{0xCDAD,0x01},{0xD2D6,0x01},{0x696B,0x00},
        {0x80B5,0x28},{0xF45A,0x00},{0x7A2D,0x29},{0x8916,0x01},
        {0x448B,0xFE},{0x9645,0x00},{0xFF22,0x01},{0x7F91,0x00},
        {0x8BC8,0x00},{0x45E4,0x01},{0x22F2,0x01},{0x1179,0x01},
        {0xBCBC,0x28},{0x5E5E,0x9B},{0x2F2F,0x01},{0xA397,0x01},
        {0xE5CB,0x01},{0xC6E5,0x4A},{0xD772,0x00},{0x6BB9,0x00},
        {0x81DC,0x01},{0x40EE,0xCD},{0x2077,0x01},{0xA43B,0x01}
    }};
    std::vector<uint8_t> pixels(128*128*4);
    for(const auto& [at,value]:samples)pixels[at]=value;
    const auto original=pixels;
    const auto normal=editor_creation_png_carrier::header(pixels,false);
    const auto alternate=editor_creation_png_carrier::header(pixels,true);
    constexpr std::array<uint8_t,8> expected{0x16,0x0D,0x45,0x02,0x34,0x09,0x00,0x00};
    constexpr std::array<uint8_t,8> fallback{0x16,0xB5,0x0E,0xE3,0xBB,0xB0,0x94,0x2F};
    CHECK(normal && *normal==expected);
    CHECK(alternate && *alternate==fallback);
    CHECK(editor_creation_png_carrier::valid_magic(*normal));
    CHECK(!editor_creation_png_carrier::valid_magic(*alternate));
    CHECK(!editor_creation_png_carrier::rejects_headers(pixels));
    CHECK(pixels==original);
    for(auto& value:pixels)value&=0xF8;
    const auto clean_normal=editor_creation_png_carrier::header(pixels,false);
    const auto clean_alternate=editor_creation_png_carrier::header(pixels,true);
    CHECK(clean_normal && clean_alternate);
    CHECK(!editor_creation_png_carrier::valid_magic(*clean_normal));
    CHECK(!editor_creation_png_carrier::valid_magic(*clean_alternate));
    CHECK(editor_creation_png_carrier::rejects_headers(pixels));
    CHECK(editor_creation_png_carrier::header(original,false)==normal);
}

void invalid_witnesses(){
    for(const size_t count:std::array<size_t,5>{0,28,31,33,(1u<<20)+4}){
        std::vector<uint8_t> pixels(count);
        CHECK(!editor_creation_png_carrier::header(pixels,false));
        CHECK(!editor_creation_png_carrier::header(pixels,true));
        CHECK(!editor_creation_png_carrier::rejects_headers(pixels));
    }
    for(const size_t count:std::array<size_t,3>{32,128*128*4,1u<<20}){
        const std::vector<uint8_t> pixels(count);
        CHECK(editor_creation_png_carrier::header(pixels,false));
        CHECK(editor_creation_png_carrier::header(pixels,true));
    }
}
} // namespace

int main(){
    actual_native_header();invalid_witnesses();
    std::printf("editor_creation_png_carrier_tests: %u checks passed\n",checks);
}
