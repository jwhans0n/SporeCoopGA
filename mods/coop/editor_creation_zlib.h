#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>
#include <utility>

// Strict bounded RFC1950/1951 validation for a PNG's concatenated IDAT stream.
// WIC may fill missing pixels and return success for corrupt compressed data.
// This verifies compressed structure and scanline bytes, not image pixels.
// https://www.rfc-editor.org/rfc/rfc1950
// https://www.rfc-editor.org/rfc/rfc1951
namespace editor_creation_zlib {
namespace detail {
struct Bits {
    std::span<const uint8_t> bytes;size_t position=0;bool valid=true;
    uint32_t take(unsigned count){
        if(count>16 || position+count>bytes.size()*8){valid=false;return 0;}
        uint32_t value=0;for(unsigned i=0;i<count;++i,++position)value|=uint32_t((bytes[position/8]>>(position%8))&1)<<i;
        return value;
    }
    void align(){position=(position+7)&~size_t(7);if(position>bytes.size()*8)valid=false;}
};
struct Huffman {
    std::array<uint32_t,16> count{},first{},offset{};std::array<uint16_t,288> symbols{};
    unsigned maximum=0;
    bool build(std::span<const uint8_t> lengths,bool codes=false,bool empty=false){
        *this={};if(lengths.size()>symbols.size())return false;
        for(auto n:lengths){if(n>15)return false;if(n){++count[n];maximum=std::max(maximum,unsigned(n));}}
        if(!maximum)return empty;
        int left=1;for(unsigned n=1;n<=15;++n){left=left*2-int(count[n]);if(left<0)return false;}
        if(left && (codes || maximum!=1))return false;
        uint32_t code=0,index=0;
        for(unsigned n=1;n<=15;++n){code=(code+count[n-1])<<1;first[n]=code;offset[n]=index;index+=count[n];}
        auto next=offset;
        for(size_t symbol=0;symbol<lengths.size();++symbol)if(auto n=lengths[symbol])symbols[next[n]++]=uint16_t(symbol);
        return true;
    }
    int decode(Bits& bits)const{
        uint32_t code=0;for(unsigned n=1;n<=maximum;++n){
            code=(code<<1)|bits.take(1);if(!bits.valid)return -1;
            if(code>=first[n] && code-first[n]<count[n])return symbols[offset[n]+code-first[n]];
        }
        return -1;
    }
};
inline bool dynamic(Bits& bits,Huffman& literal,Huffman& distance){
    const auto literals=bits.take(5)+257,distances=bits.take(5)+1,code_lengths=bits.take(4)+4;
    if(!bits.valid || literals>286)return false;
    constexpr std::array<uint8_t,19> order{16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    std::array<uint8_t,19> code{};for(unsigned i=0;i<code_lengths;++i)code[order[i]]=uint8_t(bits.take(3));
    Huffman alphabet;if(!bits.valid || !alphabet.build(code,true))return false;
    std::vector<uint8_t> lengths;lengths.reserve(literals+distances);
    while(lengths.size()<literals+distances){
        const auto symbol=alphabet.decode(bits);if(symbol<0)return false;
        if(symbol<16){lengths.push_back(uint8_t(symbol));continue;}
        unsigned repeat=0;uint8_t value=0;
        if(symbol==16){if(lengths.empty())return false;repeat=3+bits.take(2);value=lengths.back();}
        else if(symbol==17)repeat=3+bits.take(3);
        else if(symbol==18)repeat=11+bits.take(7);
        else return false;
        if(!bits.valid || repeat>literals+distances-lengths.size())return false;
        lengths.insert(lengths.end(),repeat,value);
    }
    return lengths[256] && literal.build(std::span(lengths).first(literals)) &&
        distance.build(std::span(lengths).subspan(literals),false,true);
}
}
inline bool decode(std::span<const uint8_t> stream,size_t expected,std::vector<uint8_t>& out){
    constexpr size_t bound=(512*4+7)*512;
    if(stream.size()<6 || stream.size()>(4u<<20) || !expected || expected>bound ||
       (stream[0]&15)!=8 || (stream[0]>>4)>7 || ((uint32_t(stream[0])<<8)|stream[1])%31 || (stream[1]&0x20))return false;
    const auto window=uint32_t(1)<<(8+(stream[0]>>4));
    detail::Bits bits{stream.subspan(2,stream.size()-6)};
    std::vector<uint8_t> bytes;bytes.reserve(expected);
    bool final=false;unsigned blocks=0;
    constexpr std::array<uint16_t,29> lengths{3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
    constexpr std::array<uint8_t,29> length_bits{0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
    constexpr std::array<uint16_t,30> distances{1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
    constexpr std::array<uint8_t,30> distance_bits{0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};
    while(!final){
        if(++blocks>4096)return false;
        final=bits.take(1)!=0;const auto type=bits.take(2);if(!bits.valid || type==3)return false;
        if(type==0){
            bits.align();const auto length=bits.take(16),inverse=bits.take(16);
            if(!bits.valid || uint16_t(length^inverse)!=0xFFFF || length>expected-bytes.size())return false;
            for(unsigned i=0;i<length;++i){auto value=bits.take(8);if(!bits.valid)return false;bytes.push_back(uint8_t(value));}
            continue;
        }
        detail::Huffman literal,distance;
        if(type==1){
            std::array<uint8_t,288> literal_lengths{};std::array<uint8_t,32> distance_lengths{};distance_lengths.fill(5);
            for(unsigned i=0;i<288;++i)literal_lengths[i]=i<=143?8:i<=255?9:i<=279?7:8;
            if(!literal.build(literal_lengths) || !distance.build(distance_lengths))return false;
        }else if(!detail::dynamic(bits,literal,distance))return false;
        for(;;){
            const auto symbol=literal.decode(bits);if(symbol<0 || symbol>285)return false;
            if(symbol==256)break;
            if(symbol<256){if(bytes.size()==expected)return false;bytes.push_back(uint8_t(symbol));continue;}
            const auto index=unsigned(symbol-257),length=uint32_t(lengths[index])+bits.take(length_bits[index]);
            const auto dist=distance.decode(bits);if(dist<0 || dist>=30)return false;
            const auto offset=uint32_t(distances[size_t(dist)])+bits.take(distance_bits[size_t(dist)]);
            if(!bits.valid || !offset || offset>bytes.size() || offset>window || length>expected-bytes.size())return false;
            for(uint32_t i=0;i<length;++i)bytes.push_back(bytes[bytes.size()-offset]);
        }
    }
    if(!bits.valid || (bits.position+7)/8!=bits.bytes.size() || bytes.size()!=expected)return false;
    uint32_t a=1,b=0;for(auto byte:bytes){a=(a+byte)%65521;b=(b+a)%65521;}
    const auto end=stream.data()+stream.size()-4;
    const auto adler=uint32_t(end[0])<<24|uint32_t(end[1])<<16|uint32_t(end[2])<<8|end[3];
    if(adler!=((b<<16)|a))return false;
    out=std::move(bytes);return true;
}
}
