#include "net/adventure_actor_motion.h"

#include <cstdio>
#include <limits>
#include <vector>

using namespace adventure_actor_motion;
static int failures;
#define CHECK(value) do { if (!(value)) { std::printf("FAIL %d: %s\n",__LINE__,#value); ++failures; } } while (0)
static Scope scope{7,0,101,201};
static Sample sample(uint32_t sequence,float x,double arrival)
{
    Sample s; s.sequence=sequence; s.position={x,0,0}; s.orientation.w=1; s.arrival_time=arrival; return s;
}

// A small native-style waypoint runner. Move changes a destination, not its
// rendered pose or current gait clock. Only Snap resets presentation. This
// distinguishes actual between-packet following from snapshot teleporting.
struct Walker {
    Local local;
    net::Vec3 target;
    float speed=0;
    bool walking=false;
    unsigned moves=0,snaps=0,stops=0;
    double gait_time=0;
    void apply(const Decision& d)
    {
        if(d.command==Command::Move) {target=d.target;speed=d.speed;walking=true;++moves;}
        else if(d.command==Command::Stop) {walking=false;++stops;}
        else if(d.command==Command::Snap) {local.position=d.target;walking=false;gait_time=0;++snaps;}
    }
    void tick(double dt)
    {
        if(!walking) return;
        auto delta=difference(target,local.position); float dist=length(delta);
        float step=std::min(dist,float(dt)*speed);
        if(dist>0) {
            local.position={local.position.x+delta.x*step/dist,local.position.y+delta.y*step/dist,local.position.z+delta.z*step/dist};
            gait_time+=dt;
        }
    }
};

static void walking_and_jitter()
{
    for(bool jitter:{false,true}) {
        Follower follower; Walker walker; std::vector<Sample> packets;
        for(unsigned i=0;i<=40;++i) {
            if(jitter&&(i==9||i==10||i==21)) continue;
            double arrival=i*.1+(jitter&&i?((i%2)?.025:-.025):0.);
            packets.push_back(sample(i+1,float(i)*.2f,arrival));
        }
        size_t next=0; double previous_gait=0; unsigned between_packet_steps=0;
        for(unsigned frame=0;frame<240;++frame) {
            double now=frame/60.; bool admitted=false;
            while(next<packets.size()&&packets[next].arrival_time<=now) {CHECK(follower.admit(scope,packets[next++]));admitted=true;}
            walker.apply(follower.step(walker.local,now)); walker.tick(1./60.);
            if(!admitted&&walker.gait_time>previous_gait) ++between_packet_steps;
            CHECK(walker.gait_time>=previous_gait || frame==0);
            previous_gait=walker.gait_time;
        }
        CHECK(walker.snaps==1 && walker.moves>5 && walker.moves<40);
        CHECK(walker.local.position.x>6.8f && walker.local.position.x<8.8f);
        if(between_packet_steps<=100 || walker.gait_time<=3)
            std::printf("walk trace jitter=%d gait=%f between=%u moves=%u snaps=%u x=%f\n",jitter,walker.gait_time,between_packet_steps,walker.moves,walker.snaps,walker.local.position.x);
        CHECK(between_packet_steps>100 && walker.gait_time>3);
    }
}

static void stationary_and_stale()
{
    Follower f; Local local; CHECK(f.admit(scope,sample(1,0,0))); CHECK(f.step(local,0).reason==Reason::Initial);
    CHECK(f.admit(scope,sample(2,1,.1))); CHECK(f.step(local,.1).command==Command::Move);
    auto predicted=f.step(local,.5); CHECK(predicted.target.x<=3.001f); // at most .2 s extrapolation
    auto stale=f.step(local,1); CHECK(stale.command==Command::Stop&&stale.reason==Reason::Stale);
    CHECK(f.step(local,1.1).command==Command::Keep); // no repeated stop/clip reset
    // Silence cannot fabricate confirmation that the host intentionally stopped.
    CHECK(f.step(local,2).command==Command::Keep);
    CHECK(f.admit(scope,sample(3,1,2.1))); f.step(local,2.1);
    CHECK(f.admit(scope,sample(4,1,2.2))); f.step(local,2.2);
    CHECK(f.admit(scope,sample(5,1,2.3)));
    auto stopped=f.step(local,2.3); CHECK(stopped.command==Command::Snap&&stopped.reason==Reason::Stationary);
    local.position=stopped.target; CHECK(f.step(local,2.31).command==Command::Keep);
    Follower never_commanded; CHECK(never_commanded.admit(scope,sample(1,0,0)));
    CHECK(never_commanded.step(local,1).command==Command::Stop);
}

static void corrections_and_scope()
{
    Follower f; Local local; CHECK(f.admit(scope,sample(1,0,0))); f.step(local,0);
    CHECK(f.admit(scope,sample(2,30,.1)));
    auto severe=f.step(local,.1); CHECK(severe.command==Command::Snap&&severe.reason==Reason::Severe);
    f.reset(); CHECK(f.admit(scope,sample(1,0,0))); f.step(local,0);
    bool corrected=false;
    for(unsigned i=1;i<35;++i) {
        CHECK(f.admit(scope,sample(i+1,float(i)*.2f,i*.1)));
        auto d=f.step(local,i*.1);
        if(d.command==Command::Snap) {CHECK(d.reason==Reason::Stuck); corrected=true;break;}
    }
    CHECK(corrected);
    for(unsigned field=0;field<4;++field) {
        auto other=scope;
        if(field==0) ++other.run; else if(field==1) ++other.act;
        else if(field==2) ++other.host_incarnation; else ++other.local_incarnation;
        CHECK(f.admit(other,sample(1,4,4))); CHECK(f.step(local,4).reason==Reason::Initial);
        f.reset(); CHECK(!f.has_sample());
    }
}

static void holds_and_invalid()
{
    Follower f; Local local; CHECK(f.admit(scope,sample(1,0,0))); f.step(local,0);
    CHECK(f.admit(scope,sample(2,1,.1))); CHECK(f.step(local,.1).command==Command::Move);
    local.held=true; CHECK(f.step(local,.12).command==Command::Stop);
    CHECK(f.step(local,9).command==Command::Keep);
    CHECK(f.admit(scope,sample(3,1,9))); local.held=false;
    CHECK(f.step(local,9).reason==Reason::Resuming && !f.moving());
    CHECK(f.step(local,9.1).command==Command::Keep);
    CHECK(f.admit(scope,sample(4,1.2f,9.2))); auto resumed=f.step(local,9.2);
    CHECK(resumed.command==Command::Move && resumed.target.x<2);
    local.attached=true; CHECK(f.step(local,9.3).command==Command::Stop);
    CHECK(f.step(local,9.4).command==Command::Keep); local.attached=false; local.alive=false;
    CHECK(f.step(local,9.5).command==Command::Keep); // one stop across a continuous blocked interval
    auto previous=f.latest(); auto invalid=sample(5,2,9.6);
    invalid.position.x=std::numeric_limits<float>::quiet_NaN(); CHECK(!f.admit(scope,invalid));
    invalid=sample(5,2,9.6); invalid.orientation={0,0,0,0}; CHECK(!f.admit(scope,invalid));
    invalid=sample(5,2,-1); CHECK(!f.admit(scope,invalid));
    CHECK(!f.admit(scope,sample(2,2,10)) && !f.admit(scope,sample(5,2,9.1)));
    CHECK(f.latest().sequence==previous.sequence && f.latest().position.x==previous.position.x);
    f.reset(); CHECK(f.admit(scope,sample(UINT32_MAX,0,0))); CHECK(f.admit(scope,sample(1,.2f,.1)));
}

static void full_3d_destination()
{
    Follower f; Local local; local.scale=2; local.standard_speed=3;
    CHECK(f.admit(scope,sample(1,0,0))); f.step(local,0);
    auto one=sample(2,0,.1); one.position={0,1,1}; CHECK(f.admit(scope,one)); f.step(local,.1);
    auto two=sample(3,0,.2); two.position={0,2,2}; CHECK(f.admit(scope,two));
    auto movement=f.step(local,.26);
    CHECK(movement.command==Command::Move && movement.target.y>2 && movement.target.z>2);
    CHECK(local.scale==2 && local.standard_speed==3); // policy does not alter authored tuning
}

static void slow_drift_and_stationary_anchor()
{
    for(float authored_speed:{.1f,.05f}) {
        Follower f; Walker walker; walker.local.standard_speed=4;
        unsigned next=0; bool false_stationary=false; float maximum_speed=0;
        for(unsigned frame=0;frame<900;++frame) {
            const double now=frame/60.;
            if(next<=149&&next*.1<=now) {
                CHECK(f.admit(scope,sample(next+1,next*.1f*authored_speed,next*.1))); ++next;
                if(f.confirmed_stationary()) false_stationary=true;
            }
            auto d=f.step(walker.local,now);
            if(d.command==Command::Move) maximum_speed=std::max(maximum_speed,d.speed);
            walker.apply(d); walker.tick(1./60.);
        }
        CHECK(!false_stationary && walker.snaps==1 && walker.moves>10);
        CHECK(walker.local.position.x>authored_speed*12 && walker.local.position.x<authored_speed*17);
        CHECK(maximum_speed<=authored_speed*1.51f); // slow authored walking must not use standard speed4
    }

    Follower cumulative; CHECK(cumulative.admit(scope,sample(1,0,0)));
    CHECK(cumulative.admit(scope,sample(2,.02f,.1)));
    CHECK(cumulative.admit(scope,sample(3,.04f,.2)));
    CHECK(cumulative.admit(scope,sample(4,.06f,.3)));
    CHECK(!cumulative.confirmed_stationary()); // each delta is quiet, but the anchor moved6 cm

    Follower idle; Local local;
    const float jitter[]={0,.005f,-.003f,.004f,-.002f,.003f,0};
    unsigned corrections=0,moves=0;
    for(unsigned i=0;i<7;++i) {
        CHECK(idle.admit(scope,sample(i+1,jitter[i],i*.1)));
        auto d=idle.step(local,i*.1);
        if(d.command==Command::Snap) {++corrections;local.position=d.target;}
        if(d.command==Command::Move) ++moves;
    }
    CHECK(idle.confirmed_stationary() && corrections==1 && !moves);
}

int main()
{
    walking_and_jitter(); stationary_and_stale(); corrections_and_scope(); holds_and_invalid(); full_3d_destination();
    slow_drift_and_stationary_anchor();
    std::printf("adventure actor motion: %d failures\n",failures); return failures?1:0;
}
