#include "../mods/coop/adventure_hud_model.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace adventure_hud_model;
static int failures;
#define CHECK(value) do { if (!(value)) { std::cerr<<__LINE__<<": " #value "\n"; ++failures; } } while(0)
int main()
{
    for (float width:{800.f,1024.f,1920.f}) for (size_t players:{1u,2u,3u,4u}) {
        Rect bar{0,657,width,767},check{width-60,706,width-18,748};
        auto layout=confirmations(bar,check,players);
        CHECK(layout.valid);
        CHECK(layout.header.x1>=bar.x1 && layout.header.x2<check.x1);
        CHECK(layout.header.y1>=bar.y1);
        for (size_t i=0;i<players;++i) {
            auto r=layout.players[i]; CHECK(r.x1>=bar.x1 && r.x2<check.x1);
            CHECK(r.y1>=layout.header.y2 && r.y2<=bar.y2);
            for (size_t j=0;j<i;++j) {
                auto s=layout.players[j];
                CHECK(r.x2<=s.x1 || r.x1>=s.x2 || r.y2<=s.y1 || r.y1>=s.y2);
            }
        }
    }
    CHECK(!confirmations({0,0,200,100},{150,50,190,90},4).valid);
    CHECK(!confirmations({0,0,1024,100},{964,40,1006,82},0).valid);
    CHECK(!confirmations({0,0,1024,100},{964,40,1006,82},5).valid);
    CHECK(!confirmations({0,0,1024,55},{964,13,1006,55},4).valid);
    CHECK(confirmations({0,0,1024,56},{964,14,1006,56},4).valid);
    CHECK(name("Captain\nName")=="Captain Name");
    CHECK(name("A really long captain")=="A really lon...");
    CHECK(name("\xC3\x89\xC3\x89\xC3\x89",2)=="\xC3\x89\xC3\x89...");
    CHECK(progress(std::numeric_limits<float>::quiet_NaN())==0);
    for (float x:{-100.f,0.f,1024.f,1200.f}) for (float y:{-100.f,0.f,768.f,1000.f}) {
        auto r=inside_viewport(x,y,300,20,1024,768);
        CHECK(r.x1>=0 && r.y1>=0 && r.x2<=1024 && r.y2<=768);
        CHECK(r.x2-r.x1==300 && r.y2-r.y1==20);
    }
    auto oversized=inside_viewport(-50,20,200,100,80,60);
    CHECK(oversized.x1==0 && oversized.y1==0 && oversized.x2==80 && oversized.y2==60);
    CHECK(inside_viewport(0,0,100,20,0,768).x2==0);
    CHECK(inside_viewport(std::numeric_limits<float>::quiet_NaN(),0,100,20,1024,768).x2==0);
    CHECK(caption_width("\xC3\x89")==9);
    CHECK(revive_timer(.5f,true,-1)=="Reviving: 1.5 s");
    CHECK(revive_timer(2,true,-1)=="Reviving: 0.0 s");
    CHECK(revive_timer(0,false,4.1)=="Recovery: 5 s");
    CHECK(revive_timer(0,false,-1)=="Revive: stay within 6 m");
    std::cout<<failures<<" failures\n"; return failures?EXIT_FAILURE:EXIT_SUCCESS;
}
