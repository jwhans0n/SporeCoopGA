#include "../sporekit/outside_tasks.h"
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>
using namespace sk::outside_tasks;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    {
        Queue queue;std::vector<int> order;unsigned left=100;
        std::function<void()> feed=[&]{order.push_back(1);if(--left)queue.push(feed);};
        queue.push(feed);queue.push([&]{order.push_back(2);});
        Result first;pump(queue,first,[]{return uint64_t(0);},8);
        CHECK(first.processed==2&&order==std::vector<int>({1,2})&&queue.size()==1);
        unsigned pumps=1;while(queue.size()){Result next;pump(queue,next,[]{return uint64_t(0);},8);CHECK(next.processed==1);++pumps;}
        CHECK(pumps==100&&order.size()==101);
    }
    {
        Queue queue;uint64_t clock=0;std::vector<int> order;
        queue.push([&]{order.push_back(1);clock+=3;});queue.push([&]{order.push_back(2);clock+=6;});queue.push([&]{order.push_back(3);++clock;});
        Result first;pump(queue,first,[&]{return clock;},8);
        CHECK(first.processed==2&&first.budget_reached&&!first.cap_reached&&queue.size()==1);
        Result second;pump(queue,second,[&]{return clock;},8);CHECK(second.processed==1&&order==std::vector<int>({1,2,3}));
        queue.push([&]{clock+=37;});queue.push([&]{++clock;});Result long_call;
        pump(queue,long_call,[&]{return clock;},8);CHECK(long_call.processed==1&&long_call.budget_reached&&queue.size()==1);
    }
    {
        Queue queue;std::vector<int> order;
        queue.push([&]{order.push_back(1);queue.push([&]{order.push_back(3);queue.push([&]{order.push_back(5);});});
            Result nested;pump(queue,nested,[]{return uint64_t(0);},8);CHECK(nested.processed==2);order.push_back(4);});
        queue.push([&]{order.push_back(2);});Result outer;pump(queue,outer,[]{return uint64_t(0);},8);
        CHECK(outer.processed==1&&order==std::vector<int>({1,2,3,4})&&queue.size()==1);
        Result next;pump(queue,next,[]{return uint64_t(0);},8);CHECK(order==std::vector<int>({1,2,3,4,5}));
    }
    {
        Queue queue;unsigned calls=0;for(unsigned i=0;i<300;++i)queue.push([&]{++calls;});
        Result first;pump(queue,first,[]{return uint64_t(0);},8);CHECK(first.processed==256&&first.cap_reached&&calls==256&&queue.size()==44);
        Result next;pump(queue,next,[]{return uint64_t(0);},8);CHECK(next.processed==44&&calls==300&&!queue.size());
    }
    {
        Queue queue;auto held=std::make_shared<int>(7);std::weak_ptr<int> weak=held;bool called=false;
        queue.push([&,held]{CHECK(!weak.expired());throw std::runtime_error("controlled task");});held.reset();
        queue.push([&]{called=true;});Result failed;
        try{pump(queue,failed,[]{return uint64_t(0);},8);CHECK(false);}catch(const std::runtime_error&){ }
        CHECK(failed.processed==1&&weak.expired()&&!called&&queue.size()==1);
        Result next;pump(queue,next,[]{return uint64_t(0);},8);CHECK(called&&!queue.size());
    }
    {
        Queue queue;unsigned calls=0;
        queue.push([&]{std::thread producer([&]{for(unsigned i=0;i<100;++i)queue.push([&]{++calls;});});producer.join();});
        Result first;pump(queue,first,[]{return uint64_t(0);},8);CHECK(first.processed==1&&!calls&&queue.size()==100);
        Result next;pump(queue,next,[]{return uint64_t(0);},8);CHECK(calls==100&&!queue.size());
    }
    // A queued pulse only schedules its drain. Neither can extend the current batch.
    {
        Queue queue;std::vector<int> calls;std::function<void()> pulse,drain;unsigned remaining=20;
        pulse=[&]{calls.push_back(1);queue.push(drain);};
        drain=[&]{calls.push_back(2);if(--remaining)queue.push(pulse);};queue.push(pulse);
        for(unsigned i=0;i<40;++i){Result result;pump(queue,result,[]{return uint64_t(0);},8);CHECK(result.processed==1);}
        CHECK(!queue.size()&&calls.size()==40);for(unsigned i=0;i<40;++i)CHECK(calls[i]==int(i%2+1));
    }
    std::printf("outside task batches: %u checks passed\n",checks);
}
