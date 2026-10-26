#include "editor_metrics.h"
#include <cstdio>
#include <cstdlib>
#include <limits>

unsigned checks;
#define CHECK(...) do { ++checks; if(!(__VA_ARGS__)){std::printf("FAIL %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(1);} } while(0)

int main() {
    editor_metrics::Confirmations metrics;
    const editor::OperationId own{{1,2},3,4},other{{2,1},3,4},old_epoch{{1,2},3,3};
    editor::Decision accepted;accepted.code=editor::Code::Accepted;accepted.change=editor::Change{};accepted.change->operation=own;
    metrics.begin(own,1);metrics.complete(accepted,2);
    CHECK(metrics.report().starts_with("samples=0 recording=0"));
    metrics.reset();metrics.begin(own,1);
    auto foreign=accepted;foreign.change->operation=other;metrics.complete(foreign,1.1);
    foreign.change->operation=old_epoch;metrics.complete(foreign,1.2);
    CHECK(metrics.report().starts_with("samples=0 recording=1"));
    metrics.complete(accepted,1.25);metrics.complete(accepted,1.5);
    CHECK(metrics.report().find("confirmation_ms=250.000000")!=std::string::npos);
    CHECK(metrics.report().starts_with("samples=1 "));
    metrics.begin(own,2);metrics.cancel(other);metrics.cancel(own);metrics.complete(accepted,3);
    CHECK(metrics.report().starts_with("samples=1 "));
    auto rejected=accepted;rejected.code=editor::Code::Conflict;
    metrics.begin(own,4);metrics.complete(rejected,5);
    metrics.begin(own,6);metrics.complete(accepted,5);
    metrics.begin(own,std::numeric_limits<double>::quiet_NaN());metrics.complete(accepted,8);
    CHECK(metrics.report().starts_with("samples=1 "));
    metrics.begin(own,9);metrics.stop();metrics.complete(accepted,10);
    CHECK(metrics.report().starts_with("samples=1 recording=0"));
    metrics.reset();
    for(size_t i=0;i<editor_metrics::Confirmations::capacity+10;++i){metrics.begin(own,1);metrics.complete(accepted,1.01);}
    CHECK(metrics.report().starts_with("samples=256 recording=0 capacity=256"));
    std::printf("editor confirmation metrics: %u checks passed\n",checks);
}
