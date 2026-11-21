#include "../mods/coop/editor_creation_png.h"
#include "../mods/coop/editor_creation_png_carrier.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace {
unsigned checks=0;
#define CHECK(value) do{++checks;if(!(value)){std::fprintf(stderr,"FAIL %u: %s\n",__LINE__,#value);std::exit(1);}}while(false)
constexpr std::array<uint8_t,8> signature{137,80,78,71,13,10,26,10};
using Bytes=std::vector<uint8_t>;
using Chunk=std::pair<std::string,Bytes>;
uint32_t u32(const uint8_t* p){return uint32_t(p[0])*0x1000000u+uint32_t(p[1])*0x10000u+uint32_t(p[2])*0x100u+p[3];}
void put32(Bytes& out,uint32_t value){for(const unsigned shift:std::array<unsigned,4>{24,16,8,0})out.push_back(uint8_t(value>>shift));}
uint32_t crc(std::span<const uint8_t> bytes){
    static const auto table=[] {std::array<uint32_t,256> result{};for(uint32_t i=0;i<256;++i){uint32_t c=i;for(unsigned k=0;k<8;++k)c=(c&1)?0xEDB88320u^(c>>1):c>>1;result[i]=c;}return result;}();
    uint32_t value=0xFFFFFFFFu;for(const auto byte:bytes)value=table[(value^byte)&255]^(value>>8);return value^0xFFFFFFFFu;
}
uint32_t adler(std::span<const uint8_t> bytes){
    uint32_t low=1,high=0;
    for(size_t at=0;at<bytes.size();){const auto end=std::min(bytes.size(),at+5552);for(;at<end;++at){low+=bytes[at];high+=low;}low%=65521;high%=65521;}
    return (high<<16)|low;
}
Bytes stored(std::span<const uint8_t> bytes){
    Bytes result{0x78,0x01};size_t at=0;
    do{
        const size_t count=std::min<size_t>(bytes.size()-at,65535);const uint16_t inverse=uint16_t(count)^0xFFFFu;
        result.insert(result.end(),{uint8_t(at+count==bytes.size()),uint8_t(count),uint8_t(count>>8),uint8_t(inverse),uint8_t(inverse>>8)});
        result.insert(result.end(),bytes.begin()+at,bytes.begin()+at+count);at+=count;
    }while(at<bytes.size());
    put32(result,adler(bytes));return result;
}
Bytes png(const std::vector<Chunk>& chunks){
    Bytes result(signature.begin(),signature.end());
    for(const auto& [type,payload]:chunks){
        CHECK(type.size()==4);put32(result,uint32_t(payload.size()));const auto start=result.size();
        result.insert(result.end(),type.begin(),type.end());result.insert(result.end(),payload.begin(),payload.end());
        put32(result,crc(std::span<const uint8_t>(result).subspan(start)));
    }
    return result;
}
std::vector<Chunk> source_chunks(uint32_t width,uint32_t height,uint8_t color,std::span<const uint8_t> pixels){
    Bytes header;put32(header,width);put32(header,height);header.insert(header.end(),{8,color,0,0,0});
    const auto stride=size_t(width)*(color==2?3:4);CHECK(pixels.size()==stride*height);
    Bytes rows;for(size_t y=0;y<height;++y){rows.push_back(0);rows.insert(rows.end(),pixels.begin()+y*stride,pixels.begin()+(y+1)*stride);}
    return {Chunk{"IHDR",std::move(header)},Chunk{"IDAT",stored(rows)},Chunk{"IEND",{}}};
}
struct Image {uint32_t width=0,height=0;Bytes rgba;};
Image parse_plain(std::span<const uint8_t> bytes){
    CHECK(bytes.size()>=57 && std::equal(signature.begin(),signature.end(),bytes.begin()));
    Image result;Bytes compressed;size_t at=8;std::vector<std::string> types;
    while(at<bytes.size()){
        CHECK(bytes.size()-at>=12);const auto count=u32(bytes.data()+at);CHECK(count<=bytes.size()-at-12);
        std::string type(reinterpret_cast<const char*>(bytes.data()+at+4),4);types.push_back(type);
        const auto payload=bytes.subspan(at+8,count);CHECK(crc(bytes.subspan(at+4,count+4))==u32(bytes.data()+at+8+count));
        if(type=="IHDR"){
            CHECK(count==13);result.width=u32(payload.data());result.height=u32(payload.data()+4);
            CHECK(result.width && result.height && result.width<=512 && result.height<=512);
            CHECK(payload[8]==8 && payload[9]==6 && !payload[10] && !payload[11] && !payload[12]);
        }else if(type=="IDAT")compressed.assign(payload.begin(),payload.end());
        else CHECK(type=="IEND" && count==0);
        at+=count+12;
    }
    CHECK((types==std::vector<std::string>{"IHDR","IDAT","IEND"}));CHECK(at==bytes.size());
    CHECK(compressed.size()>=11 && compressed[0]==0x78 && compressed[1]==0x01);
    Bytes raw;at=2;bool last=false;
    while(!last){
        CHECK(compressed.size()-at>=9);const auto control=compressed[at++];CHECK(control<=1);last=control!=0;
        const auto count=uint16_t(compressed[at])|uint16_t(compressed[at+1])<<8;
        const auto inverse=uint16_t(compressed[at+2])|uint16_t(compressed[at+3])<<8;at+=4;
        CHECK((count^inverse)==0xFFFF);CHECK(size_t(count)<=compressed.size()-at-4);
        raw.insert(raw.end(),compressed.begin()+at,compressed.begin()+at+count);at+=count;
    }
    CHECK(at+4==compressed.size());CHECK(adler(raw)==u32(compressed.data()+at));
    const auto stride=size_t(result.width)*4;CHECK(raw.size()==(stride+1)*result.height);
    for(size_t y=0;y<result.height;++y){CHECK(raw[y*(stride+1)]==0);result.rgba.insert(result.rgba.end(),raw.begin()+y*(stride+1)+1,raw.begin()+(y+1)*(stride+1));}
    return result;
}
Bytes pixels(uint32_t width,uint32_t height){
    Bytes result;result.reserve(size_t(width)*height*4);
    for(uint32_t y=0;y<height;++y)for(uint32_t x=0;x<width;++x)
        result.insert(result.end(),{uint8_t(x*31+y*13),uint8_t(x*7+y*37),uint8_t(x*19+y*11),uint8_t((x+y)%6==0?0:(x+y)%6==1?255:x*17+y*29)});
    return result;
}
void verify_pixels(const Image& image,std::span<const uint8_t> original){
    CHECK(image.rgba.size()==original.size());
    for(size_t i=0;i<original.size();++i){CHECK(image.rgba[i]==uint8_t(original[i]&0xF8));CHECK(unsigned(original[i])-image.rgba[i]<=7);}
    auto bgra=image.rgba;for(size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
    const auto normal=editor_creation_png_carrier::header(bgra,false),alternate=editor_creation_png_carrier::header(bgra,true);
    CHECK(normal && alternate);CHECK(!editor_creation_png_carrier::valid_magic(*normal));CHECK(!editor_creation_png_carrier::valid_magic(*alternate));
}
void reject(const Bytes& input){
    static unsigned rejected_fixture=0;++rejected_fixture;
    Bytes output{0x17,0x29,0x42};const auto sentinel=output;std::string error;
    const bool accepted=editor_creation_png::sanitize(input,output,error);
    if(accepted)std::fprintf(stderr,"Unexpected accepted malformed PNG fixture %u (%zu bytes)\n",rejected_fixture,input.size());
    CHECK(!accepted);CHECK(output==sentinel);CHECK(!error.empty());
}
void good_images(){
    const auto original=pixels(16,8);const auto chunks=source_chunks(16,8,6,original);const auto input=png(chunks);
    Bytes output,repeat,direct;std::string error;
    CHECK(editor_creation_png::sanitize(input,output,error));CHECK(error.empty());const auto decoded=parse_plain(output);
    CHECK(decoded.width==16 && decoded.height==8);verify_pixels(decoded,original);
    CHECK(editor_creation_png::sanitize(input,repeat,error) && repeat==output);
    CHECK(editor_creation_png::from_rgba(16,8,original,direct,error) && direct==output);
    CHECK(editor_creation_png::sanitize(output,repeat,error) && repeat==output);
    auto ancillary=chunks;ancillary.insert(ancillary.begin()+1,{"tEXt",{'O','w','n','e','r',0,'o','l','d'}});
    ancillary.insert(ancillary.begin()+2,{"iCCP",{'B','a','d',0,0,0xFF,0xFF}});
    auto with_extra=png(ancillary);with_extra.insert(with_extra.end(),{'o','l','d',' ','n','a','t','i','v','e',' ','p','a','y','l','o','a','d'});
    CHECK(editor_creation_png::sanitize(with_extra,repeat,error) && repeat==output);
    Bytes rgb,expected;for(size_t i=0;i<original.size();i+=4){rgb.insert(rgb.end(),original.begin()+i,original.begin()+i+3);expected.insert(expected.end(),original.begin()+i,original.begin()+i+3);expected.push_back(255);}
    CHECK(editor_creation_png::sanitize(png(source_chunks(16,8,2,rgb)),output,error));verify_pixels(parse_plain(output),expected);
    rgb[0]=1;rgb[1]=2;rgb[2]=3;auto transparent=source_chunks(16,8,2,rgb);
    transparent.insert(transparent.begin()+1,{"tRNS",{0,1,0,2,0,3}});
    expected.clear();for(size_t i=0;i<rgb.size();i+=3){expected.insert(expected.end(),rgb.begin()+i,rgb.begin()+i+3);expected.push_back(rgb[i]==1 && rgb[i+1]==2 && rgb[i+2]==3?0:255);}
    CHECK(editor_creation_png::sanitize(png(transparent),output,error));verify_pixels(parse_plain(output),expected);
    const auto interlaced_pixels=pixels(17,11);auto interlaced=source_chunks(17,11,6,interlaced_pixels);interlaced[0].second[12]=1;
    Bytes passes;
    for(const auto& pass:std::array<std::array<unsigned,4>,7>{{{0,0,8,8},{4,0,8,8},{0,4,4,8},{2,0,4,4},{0,2,2,4},{1,0,2,2},{0,1,1,2}}})
        for(unsigned y=pass[1];y<11;y+=pass[3]){
            passes.push_back(0);for(unsigned x=pass[0];x<17;x+=pass[2]){
                const auto at=(y*17+x)*4;passes.insert(passes.end(),interlaced_pixels.begin()+at,interlaced_pixels.begin()+at+4);
            }
        }
    interlaced[1].second=stored(passes);CHECK(editor_creation_png::sanitize(png(interlaced),output,error));verify_pixels(parse_plain(output),interlaced_pixels);
    // The upper bound crosses multiple stored-DEFLATE blocks and preserves
    // the fixed native carrier limit; it does not require WIC or game state.
    const auto large=pixels(512,512);CHECK(editor_creation_png::from_rgba(512,512,large,output,error));verify_pixels(parse_plain(output),large);
}
void malformed_images(){
    const auto original=pixels(16,8);const auto chunks=source_chunks(16,8,6,original);const auto valid=png(chunks);
    reject({});auto bad=valid;bad[0]^=1;reject(bad);bad=valid;bad.pop_back();reject(bad);
    bad=valid;bad[16]^=1;reject(bad);bad=valid;bad[8]=0xFF;bad[9]=0xFF;bad[10]=0xFF;bad[11]=0xFF;reject(bad);
    for(unsigned field:std::array<unsigned,6>{0,1,2,3,4,5}){
        auto source=chunks;auto& header=source[0].second;
        if(field==0)header[8]=16;if(field==1)header[9]=3;if(field==2)header[10]=1;
        if(field==3)header[11]=1;if(field==4)header[12]=2;if(field==5){header[0]=header[1]=header[2]=header[3]=0;}
        reject(png(source));
    }
    auto source=chunks;source[0].second[2]=2;source[0].second[3]=1;reject(png(source));
    source=chunks;source.insert(source.begin()+1,source.front());reject(png(source));
    source=chunks;source.insert(source.begin()+1,{"ABCD",{1}});reject(png(source));
    source=chunks;source.insert(source.begin()+1,{"tExt",{1}});reject(png(source));
    source=chunks;source.insert(source.begin()+1,{std::string("a\x80" "Cd",4),{1}});reject(png(source));
    source=chunks;source.insert(source.begin()+1,{"tRNS",Bytes(6)});reject(png(source));
    source=chunks;source[1].second={0x78,0x01,0xFF};reject(png(source));
    source=chunks;source[1].second.back()^=1;reject(png(source)); // WIC alone accepts a bad Adler32.
    source=chunks;source[1].second.insert(source[1].second.end(),{0x42,0x91,0x17});reject(png(source));
    source=chunks;source[1].second.resize(source[1].second.size()-4);reject(png(source));
    source=chunks;source[1].second[1]^=1;reject(png(source));
    source=chunks;source[1].second[1]=0x20;reject(png(source)); // Valid FCHECK, unsupported preset dictionary.
    source=chunks;source[1].second.insert(source[1].second.end(),chunks[1].second.begin(),chunks[1].second.end());reject(png(source));
    source=chunks;Bytes invalid_filter(8*(16*4+1));invalid_filter[0]=5;source[1].second=stored(invalid_filter);reject(png(source));
    source=chunks;source.erase(source.begin()+1);reject(png(source));
    source=chunks;source.pop_back();reject(png(source));
    source=chunks;source.insert(source.begin()+2,{"tEXt",{1}});source.insert(source.begin()+3,source[1]);reject(png(source));
    source=chunks;for(unsigned i=0;i<257;++i)source.insert(source.begin()+1,{"tEXt",{1}});reject(png(source));
    bad=valid;bad.resize((4u<<20)+1);reject(bad);
    Bytes rgb(16*8*3);source=source_chunks(16,8,2,rgb);source.insert(source.begin()+1,{"tRNS",Bytes(5)});reject(png(source));
    source=source_chunks(16,8,2,rgb);source.insert(source.begin()+1,{"tRNS",Bytes(6)});source.insert(source.begin()+1,{"tRNS",Bytes(6)});reject(png(source));
    for(const auto& dimensions:std::array<std::pair<uint32_t,uint32_t>,4>{{{0,8},{16,0},{513,8},{16,513}}}){
        Bytes output{1,2,3};std::string error;CHECK(!editor_creation_png::from_rgba(dimensions.first,dimensions.second,{},output,error));CHECK((output==Bytes{1,2,3}));
    }
    Bytes output{1,2,3};std::string error;CHECK(!editor_creation_png::from_rgba(16,8,Bytes(1),output,error));CHECK((output==Bytes{1,2,3}));
}
Bytes read(const char* path){
    std::ifstream input(path,std::ios::binary);CHECK(input.good());Bytes result{std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};CHECK(!input.bad());return result;
}
void actual_source(int argc,char** argv){
    if(argc<3)return;
    const auto input=read(argv[1]),original=read(argv[2]);CHECK(input.size()>24);
    const auto width=u32(input.data()+16),height=u32(input.data()+20);CHECK(width==128 && height==128 && original.size()==size_t(width)*height*4);
    auto bgra=original;for(size_t i=0;i<bgra.size();i+=4)std::swap(bgra[i],bgra[i+2]);
    const auto original_header=editor_creation_png_carrier::header(bgra,false);CHECK(original_header && editor_creation_png_carrier::valid_magic(*original_header));
    Bytes output,direct;std::string error;CHECK(editor_creation_png::sanitize(input,output,error));verify_pixels(parse_plain(output),original);
    CHECK(editor_creation_png::from_rgba(width,height,original,direct,error) && output==direct);
    if(argc>3){std::ofstream stream(argv[3],std::ios::binary);stream.write(reinterpret_cast<const char*>(output.data()),std::streamsize(output.size()));CHECK(stream.good());}
    std::printf("actual native128x128 thumbnail: sanitized %zu bytes to %zu bytes\n",input.size(),output.size());
}
} // namespace

int main(int argc,char** argv){
    good_images();actual_source(argc,argv);malformed_images();
    std::printf("editor_creation_png_windows_tests: %u checks passed\n",checks);
}
