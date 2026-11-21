#include "editor_native_refpack.h"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace editor_native_refpack {
namespace {
// 92C780: hash reset, link reuse, strict gain/tie order and literal grouping.
// Indices avoid borrowed/unaligned word loads and signed pointer arithmetic.
void tokens(std::span<const uint8_t> input,uint32_t mask,std::span<int32_t> hash,
            std::span<int32_t> links,std::span<uint8_t> output,size_t& written){
    size_t used=0;
    std::fill(hash.begin(),hash.end(),-1);
    const auto byte=[&](uint32_t value){
        if(used==output.size())throw std::length_error("RefPack output bound");
        output[used++]=uint8_t(value);
    };
    const auto copy=[&](size_t at,size_t count){
        if(at>input.size() || count>input.size()-at || count>output.size()-used)
            throw std::length_error("RefPack copy bound");
        std::copy_n(input.data()+at,count,output.data()+used);used+=count;
    };
    uint32_t pos=0,literal_start=0,pending=0;
    int32_t remaining=int32_t(input.size())-4;
    const auto key=[&](uint32_t at){return ((uint32_t(input[at])<<8)|input[at+2])^(uint32_t(input[at+1])<<4);};
    const auto insert=[&](uint32_t at){const auto h=key(at);links[at&mask]=hash[h];hash[h]=int32_t(at);};
    const auto literals=[&]{
        while(pending>3){
            const auto count=std::min(pending&~3u,112u);
            byte(count/4-0x21);copy(literal_start,count);literal_start+=count;pending-=count;
        }
    };
    while(remaining>=0){
        int32_t candidate=hash[key(pos)];
        const int32_t cutoff=pos>mask?int32_t(pos-mask):0;
        const uint32_t max_length=std::min(uint32_t(remaining),1028u);
        uint32_t best=2,command=2,distance=0;
        while(candidate>=cutoff){
            const auto at=uint32_t(candidate);
            if(input[pos+best]==input[at+best]){
                uint32_t length=0;
                while(length<max_length && input[pos+length]==input[at+length])++length;
                if(length>best){
                    const auto d=pos-at-1;
                    const uint32_t cost=d<0x400 && length<=10?2:d<0x4000 && length<=67?3:4;
                    if(length+4-cost>best+4-command){
                        best=length;command=cost;distance=d;
                        if(length>=1028)break;
                    }
                }
            }
            const auto next=links[at&mask];
            if(next>=candidate)throw std::runtime_error("RefPack nondecreasing chain");
            candidate=next;
        }
        if(command<best && remaining>=4){
            literals();
            if(command==2){byte(((distance>>3)&0xE0)+4*(best-3)+pending);byte(distance);}
            else if(command==3){byte(best+0x7C);byte((pending<<6)+(distance>>8));byte(distance);}
            else {byte((((best-5)>>6)&0xFC)+((distance>>12)&0xF0)+pending-0x40);byte(distance>>8);byte(distance);byte(best-5);}
            copy(literal_start,pending);pending=0;
            for(uint32_t consumed=0;consumed<best;++consumed)insert(pos++);
            remaining-=int32_t(best);literal_start=pos;
        }else{insert(pos++);++pending;--remaining;}
    }
    pending+=uint32_t(remaining+4);literals();byte(pending-4);copy(literal_start,pending);written=used;
}
}
bool encode(std::span<const uint8_t> input,Window window,std::vector<uint8_t>& output,std::string& error){
    const auto mask=uint32_t(window);
    if(input.empty() || input.size()>kInputLimit || (mask!=0x3FFF && mask!=0x1FFFF)){
        error="Unsupported bounded RefPack input";return false;
    }
    try{
        const size_t bound=input.size()*20/16+32;
        std::vector<uint8_t> candidate(bound);
        std::vector<int32_t> hash(65536),links(size_t(mask)+1);
        size_t used=0;tokens(input,mask,hash,links,std::span(candidate).subspan(5),used);
        candidate[0]=mask==0x3FFF?0x50:0x10;candidate[1]=0xFB;
        candidate[2]=uint8_t(input.size()>>16);candidate[3]=uint8_t(input.size()>>8);candidate[4]=uint8_t(input.size());
        candidate.resize(used+5);output=std::move(candidate);error.clear();return true;
    }catch(const std::bad_alloc&){error="No memory";return false;}
    catch(const std::exception& e){error=e.what();return false;}
}
}
