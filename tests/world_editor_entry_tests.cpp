#include "../mods/coop/world_editor_entry.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <functional>
#include <memory>

static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    using namespace world_editor_entry;
    for(unsigned kind=0;kind<13;++kind){
        Cinematic value{1,1,kMenuIntro};unsigned ends=0,observes=0,currents=0;
        bool live=true;std::string error;
        if(kind==0)value.active=0;
        if(kind==2)value.id=0x12345678;
        if(kind==3)value.manager=0;
        if(kind==4)live=false;
        auto current=[&]{
            ++currents;
            if(kind==12 && currents==2)value.id=0x12345678;
            return live;
        };
        auto observe=[&]{
            ++observes;
            if(kind==11 && observes==2)value.manager=2;
            return value;
        };
        auto end=[&](uintptr_t manager){
            CHECK(manager==1);CHECK(value.active&&value.id==kMenuIntro);++ends;
            if(kind==5)return;
            value.active=0;value.id=0;
            if(kind==6)live=false;
            if(kind==7)value.manager=2;
            if(kind==8)value.id=0x12345678;
            if(kind==9)value.active=1;
            if(kind==10)throw std::runtime_error("native end");
        };
        bool ready=false,threw=false;
        try{ready=prepare(current,observe,end,error);}catch(const std::runtime_error&){threw=true;}
        if(kind==0||kind==1){CHECK(ready);CHECK(error.empty());CHECK(ends==(kind==0?0u:1u));}
        else if(kind==10){CHECK(threw);CHECK(!ready);CHECK(ends==1);}
        else{CHECK(!ready);CHECK(!error.empty());}
        if(kind==2||kind==3||kind==4||kind==11||kind==12)CHECK(!ends);
    }
    // A copied current callback survives its original holder retiring itself.
    Cinematic value{1,0,0};std::string error;std::function<bool()> current;
    auto token=std::make_shared<int>(1);std::weak_ptr<int> weak=token;
    current=[keep=token,&current,&weak]{current={};CHECK(!weak.expired());return true;};token.reset();
    CHECK(prepare(current,[&]{return value;},[](uintptr_t){CHECK(false);},error));CHECK(weak.expired());
    std::printf("world editor entry: %u checks passed\n",checks);
}
