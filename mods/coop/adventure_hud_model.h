#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include "net/adventure.h"

namespace adventure_hud_model {
struct Rect { float x1=0,y1=0,x2=0,y2=0; };
struct Layout { Rect header; std::array<Rect,4> players{}; bool valid=false; };
inline Layout confirmations(Rect bar, Rect check, size_t count)
{
    Layout result;
    if (!count || count>4 || bar.x2<=bar.x1 || bar.y2<=bar.y1) return result;
    float right=std::min(bar.x2-12.f,check.x1-16.f), available=right-bar.x1-12.f;
    int columns=int(count), rows=1;
    if (available<220.f || bar.y2-bar.y1<14.f+rows*22.f+20.f) return result;
    float width=std::min(count>2?900.f:460.f,available), left=right-width;
    float top=bar.y2-14.f-rows*22.f-20.f;
    result.header={left,top,right,top+19.f};
    float cell=(width-12.f*(columns-1))/columns;
    for (size_t i=0;i<count;++i) {
        float x=left+float(i%columns)*(cell+12.f), y=top+20.f+float(i/columns)*22.f;
        result.players[i]={x,y,x+cell,y+21.f};
    }
    result.valid=true; return result;
}
inline std::string name(const std::string& value, size_t limit=12)
{
    std::string out; size_t n=0;
    for (size_t i=0;i<value.size();) {
        unsigned char c=value[i]; size_t width=c<0x80?1:(c&0xE0)==0xC0?2:(c&0xF0)==0xE0?3:4;
        if (i+width>value.size()) break;
        if (n++==limit) { out+="..."; break; }
        if (c<0x20 || c==0x7F) out+=' '; else out.append(value,i,width);
        i+=width;
    }
    return out;
}
inline float progress(float value) { return std::isfinite(value)?std::clamp(value,0.f,1.f):0.f; }
inline Rect inside_viewport(float x, float y, float width, float height, float screen_width, float screen_height)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(screen_width) || !std::isfinite(screen_height) || screen_width<=0 || screen_height<=0)
        return {};
    width=std::clamp(width,0.f,screen_width); height=std::clamp(height,0.f,screen_height);
    x=std::clamp(x,0.f,screen_width-width); y=std::clamp(y,0.f,screen_height-height);
    return {x,y,x+width,y+height};
}
inline float caption_width(const std::string& text)
{
    size_t count=0;
    for (unsigned char c:text) if ((c&0xC0)!=0x80) ++count;
    return float(count)*9.f;
}
inline std::string revive_timer(float value, bool reviving, double automatic)
{
    char buffer[64];
    if (reviving) snprintf(buffer,sizeof buffer,"Reviving: %.1f s",ga::kReviveSeconds*(1.f-progress(value)));
    else if (automatic>=0 && std::isfinite(automatic)) snprintf(buffer,sizeof buffer,"Recovery: %.0f s",std::ceil(std::max(0.0,automatic)));
    else { snprintf(buffer,sizeof buffer,"Revive: stay within %.0f m",double(ga::kReviveDistance)); }
    return buffer;
}
} // namespace adventure_hud_model
