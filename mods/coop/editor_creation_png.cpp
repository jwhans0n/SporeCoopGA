#include "editor_creation_png.h"
#include "editor_creation_png_carrier.h"
#include "editor_creation_zlib.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <new>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <wincodec.h>
#endif

namespace editor_creation_png {
namespace {
constexpr uint32_t kDimension=512,kInput=4u<<20;
constexpr std::array<uint8_t,8> signature{137,80,78,71,13,10,26,10};
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
void append32(std::vector<uint8_t>& bytes,uint32_t v){for(int shift=24;shift>=0;shift-=8)bytes.push_back(uint8_t(v>>shift));}
uint32_t crc(std::span<const uint8_t> bytes){
    uint32_t value=UINT32_MAX;
    for(auto byte:bytes){value^=byte;for(unsigned bit=0;bit<8;++bit)value=(value>>1)^((value&1)?0xEDB88320u:0);}
    return value^UINT32_MAX;
}
void chunk(std::vector<uint8_t>& png,const char* type,std::span<const uint8_t> bytes){
    append32(png,uint32_t(bytes.size()));const auto begin=png.size();
    png.insert(png.end(),type,type+4);png.insert(png.end(),bytes.begin(),bytes.end());
    append32(png,crc(std::span<const uint8_t>(png).subspan(begin)));
}
bool dimensions(uint32_t width,uint32_t height){return width && height && width<=kDimension && height<=kDimension;}
bool preflight(std::span<const uint8_t> input,std::vector<uint8_t>& stripped,uint32_t& width,uint32_t& height,std::string& error){
    auto bad=[&](){error="The creation thumbnail is not a bounded native RGB/RGBA PNG";return false;};
    if(input.size()<57 || input.size()>kInput || !std::equal(signature.begin(),signature.end(),input.begin()))return bad();
    stripped.assign(signature.begin(),signature.end());
    size_t at=8;bool header=false,data=false,after_data=false,transparent=false,palette=false;unsigned chunks=0;uint8_t color=0,interlace=0;
    std::vector<uint8_t> compressed;
    while(at<input.size()){
        if(++chunks>256 || input.size()-at<12)return bad();
        const auto size=be32(input.data()+at);if(size>input.size()-at-12)return bad();
        const auto type=be32(input.data()+at+4);auto payload=input.subspan(at+8,size);
        for(size_t i=4;i<8;++i)if(!((input[at+i]>='A' && input[at+i]<='Z') || (input[at+i]>='a' && input[at+i]<='z')))return bad();
        if(input[at+6]&0x20)return bad();
        if(crc(input.subspan(at+4,size+4))!=be32(input.data()+at+8+size))return bad();
        bool keep=false;
        if(type==0x49484452){
            if(header || at!=8 || size!=13)return bad();
            width=be32(payload.data());height=be32(payload.data()+4);color=payload[9];
            if(!dimensions(width,height) || payload[8]!=8 || (color!=2 && color!=6) || payload[10] || payload[11] || payload[12]>1)return bad();
            interlace=payload[12];
            header=true;keep=true;
        }else if(!header)return bad();
        else if(type==0x49444154){if(after_data)return bad();data=true;keep=true;compressed.insert(compressed.end(),payload.begin(),payload.end());}
        else if(type==0x49454E44){
            if(size || !data)return bad();
            std::vector<std::pair<size_t,size_t>> passes;
            const auto channels=color==6?4u:3u;
            if(!interlace)passes.push_back({size_t(width)*channels,height});
            else{
                constexpr std::array<unsigned,7> start_x{0,4,0,2,0,1,0},start_y{0,0,4,0,2,0,1};
                constexpr std::array<unsigned,7> step_x{8,8,4,4,2,2,1},step_y{8,8,8,4,4,2,2};
                for(size_t p=0;p<7;++p){
                    const auto w=width>start_x[p]?(width-start_x[p]+step_x[p]-1)/step_x[p]:0;
                    const auto h=height>start_y[p]?(height-start_y[p]+step_y[p]-1)/step_y[p]:0;
                    if(w && h)passes.push_back({size_t(w)*channels,h});
                }
            }
            size_t expected=0;for(auto [stride,rows]:passes)expected+=(stride+1)*rows;
            std::vector<uint8_t> filtered;
            if(!editor_creation_zlib::decode(compressed,expected,filtered)){
                error="The creation thumbnail has a malformed compressed image";return false;
            }
            size_t cursor=0;
            for(auto [stride,rows]:passes)for(size_t row=0;row<rows;++row){if(filtered[cursor]>4)return bad();cursor+=stride+1;}
            stripped.insert(stripped.end(),input.begin()+at,input.begin()+at+12);
            return true; // Deliberately discard all trailing native payload.
        }else{
            if(data)after_data=true;
            if(type==0x74524E53){
                if(data || transparent || color!=2 || size!=6 || payload[0] || payload[2] || payload[4])return bad();transparent=true;keep=true;
            }else if(type==0x504C5445){if(data || palette || !size || size>768 || size%3)return bad();palette=true;}
            else if(!(input[at+4]&0x20))return bad();
            // Do not feed profiles, compressed ancillary data or native carrier
            // chunks to WIC. Truecolor tRNS alone is needed to retain alpha.
        }
        if(keep)stripped.insert(stripped.end(),input.begin()+at,input.begin()+at+12+size);
        at+=size_t(size)+12;
    }
    return bad();
}
#ifdef _WIN32
template<class T> struct Com {
    T* p=nullptr;~Com(){if(p)p->Release();}T** address(){return &p;}
    Com()=default;Com(const Com&)=delete;Com& operator=(const Com&)=delete;
};
struct Apartment {
    HRESULT result=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    ~Apartment(){if(SUCCEEDED(result))CoUninitialize();}
    bool valid()const{return SUCCEEDED(result) || result==RPC_E_CHANGED_MODE;}
};
#endif
}

bool from_rgba(uint32_t width,uint32_t height,std::span<const uint8_t> rgba,std::vector<uint8_t>& out,std::string& error){
    if(!dimensions(width,height) || rgba.size()!=size_t(width)*height*4){error="The decoded creation thumbnail exceeds its native bounds";return false;}
    try{
        std::vector<uint8_t> clean(rgba.begin(),rgba.end());for(auto& byte:clean)byte&=0xF8;
        auto bgra=clean;for(size_t at=0;at<bgra.size();at+=4)std::swap(bgra[at],bgra[at+2]);
        // Native 68E660 first probes highMask~7, then~1. Its FNV whitening
        // means zero carriers alone do not imply a zero decoded header.
        if(!editor_creation_png_carrier::rejects_headers(bgra)){
            error="The sanitized thumbnail still matches a native embedded creation header";return false;
        }
        std::vector<uint8_t> raw;raw.reserve(clean.size()+height);
        const auto stride=size_t(width)*4;
        for(size_t y=0;y<height;++y){raw.push_back(0);raw.insert(raw.end(),clean.begin()+y*stride,clean.begin()+(y+1)*stride);}
        uint32_t a=1,b=0;for(auto byte:raw){a=(a+byte)%65521;b=(b+a)%65521;}
        std::vector<uint8_t> zlib{0x78,0x01};size_t at=0;
        while(at<raw.size()){
            const auto count=uint16_t(std::min<size_t>(65535,raw.size()-at));
            zlib.push_back(at+count==raw.size()?1:0);
            zlib.push_back(uint8_t(count));zlib.push_back(uint8_t(count>>8));
            const auto inverse=uint16_t(~count);zlib.push_back(uint8_t(inverse));zlib.push_back(uint8_t(inverse>>8));
            zlib.insert(zlib.end(),raw.begin()+at,raw.begin()+at+count);at+=count;
        }
        append32(zlib,(b<<16)|a);
        std::vector<uint8_t> png(signature.begin(),signature.end()),ihdr;
        append32(ihdr,width);append32(ihdr,height);ihdr.insert(ihdr.end(),{8,6,0,0,0});
        chunk(png,"IHDR",ihdr);chunk(png,"IDAT",zlib);chunk(png,"IEND",{});
        out=std::move(png);error.clear();return true;
    }catch(const std::bad_alloc&){error="The creation thumbnail could not allocate its bounded buffers";return false;}
}
bool sanitize(std::span<const uint8_t> input,std::vector<uint8_t>& out,std::string& error){
    try{
        std::vector<uint8_t> stripped;uint32_t width=0,height=0;
        if(!preflight(input,stripped,width,height,error))return false;
#ifdef _WIN32
        Apartment apartment;Com<IWICImagingFactory> factory;Com<IWICStream> stream;
        Com<IWICBitmapDecoder> decoder;Com<IWICBitmapFrameDecode> frame;Com<IWICFormatConverter> converter;
        auto bad=[&](){error="The native creation thumbnail could not be decoded safely";return false;};
        if(!apartment.valid() || FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_IWICImagingFactory,reinterpret_cast<void**>(factory.address()))) ||
           FAILED(factory.p->CreateStream(stream.address())) || FAILED(stream.p->InitializeFromMemory(stripped.data(),DWORD(stripped.size()))) ||
           FAILED(factory.p->CreateDecoder(GUID_ContainerFormatPng,&GUID_VendorMicrosoft,decoder.address())) ||
           FAILED(decoder.p->Initialize(stream.p,WICDecodeMetadataCacheOnLoad)))return bad();
        UINT frames=0,decoded_width=0,decoded_height=0;
        if(FAILED(decoder.p->GetFrameCount(&frames)) || frames!=1 || FAILED(decoder.p->GetFrame(0,frame.address())) ||
           FAILED(frame.p->GetSize(&decoded_width,&decoded_height)) || decoded_width!=width || decoded_height!=height ||
           FAILED(factory.p->CreateFormatConverter(converter.address())) ||
           FAILED(converter.p->Initialize(frame.p,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))return bad();
        std::vector<uint8_t> pixels(size_t(width)*height*4);
        if(FAILED(converter.p->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data())))return bad();
        for(size_t at=0;at<pixels.size();at+=4)std::swap(pixels[at],pixels[at+2]);
        return from_rgba(width,height,pixels,out,error);
#else
        (void)out;error="The bounded Windows thumbnail decoder is unavailable";return false;
#endif
    }catch(const std::bad_alloc&){error="The creation thumbnail could not allocate its bounded buffers";return false;}
}
}
