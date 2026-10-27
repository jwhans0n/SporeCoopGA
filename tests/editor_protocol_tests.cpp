#include "net/editor_protocol.h"
#include "../updater/sha256.h"
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>

using namespace editor;
namespace w=editor::wire;
static int checks=0;
#define CHECK(x) do { ++checks;if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);} } while(0)
static const Peer host{0,71},guest{1,92},other{2,13};
static State sample(){
    State s;auto& d=s.document;d.project[0]=31;d.epoch=15;d.revision=3;d.next_id=5;d.classes.insert({1,1});d.acts.insert({2,1});d.tombstones[{4,1}]=2;
    Placement p;p.object={3,1};p.class_id={1,1};p.act_id={2,1};p.position={1,2,3};p.rotation={0,0,0,1};p.scale=2;p.revision=3;p.field_revisions[FieldKey{Field::Position,0}]=3;
    for(unsigned i=2;i<=7;++i){PropertySpec spec;spec.kind=ValueKind(i);d.schema[i]=spec;}
    p.properties[2]=1.25f;p.properties[3]=true;p.properties[4]=int64_t(-42);p.properties[5]=std::string("a saved captain");p.properties[6]=ObjectId{3,1};p.properties[7]=ResourceKey{5,6,7};d.placements[p.object]=p;
    const OperationId op{guest,4,15};Change c;c.operation=c.gesture=op;c.revision=3;c.summary="Move saved placement";
    c.patch.fields.push_back({p.object,{Field::Position,0},Value(Vec3{}),Value(p.position),2,0,0});c.inverse.fields.push_back({p.object,{Field::Position,0},Value(p.position),Value(Vec3{}),3,2,2});
    Decision decision{Code::Accepted,"",3,c};s.decisions[op]=decision;s.decision_order.push_back(op);
    Gesture gesture;gesture.operation=op;gesture.original_revision=2;gesture.transition_revision=3;gesture.summary=c.summary;gesture.undo=c.inverse;gesture.redo=c.patch;gesture.applied=false;gesture.redo_available=true;s.history.push_back(gesture);s.retired_sequences[other]=18;s.history_truncated.insert(other);return s;
}
static w::Context context(){auto d=sample().document;return {d.project,d.epoch,d.revision};}
static Request request(){
    const auto d=sample().document;Request q;q.project=d.project;q.epoch=d.epoch;q.base_revision=d.revision;q.operation={guest,8,d.epoch};q.summary="Set supported values";
    std::vector<Value> values{Vec3{4,5,6},Rotation{0,0,0,1},float(3),true,int64_t(-9),std::string("name"),ObjectId{3,1},ResourceKey{2,4,6}};
    for(size_t i=0;i<values.size();++i){Mutation m;m.target={3,1};m.field={i<3?Field(i):Field::Property,i<3?0:uint32_t(i)};m.expected=3;m.value=values[i];q.mutations.push_back(m);}
    Scope scope{ScopeKind::Placement,{3,1},{}};q.leases.push_back({scope,17});q.dependencies.push_back({{3,1},CheckKind::Object,{},3});return q;
}
static std::vector<uint8_t> packet(const w::Message& m){net::Writer out;std::string error;const bool okay=w::write(m,out,error);if(!okay)std::cerr<<"packet "<<m.payload.index()<<": "<<error<<"\n";CHECK(okay);CHECK(out.size()<=w::kPacketBytes);return out.data;}
static bool read(const std::vector<uint8_t>& bytes,w::Message& m,Peer sender,bool from_host){net::Reader r(bytes.data(),bytes.size());std::string error;const bool okay=w::read(r,m,sender,from_host,error);if(okay)CHECK(r.left()==0);return okay;}
static void codec(){
    std::string error;auto state=sample();std::vector<uint8_t> bytes;CHECK(w::encode_state(state,bytes,error));State result;CHECK(w::decode_state(bytes,result,error));CHECK(result==state);
    for(size_t cut=0;cut<bytes.size();++cut){auto unchanged=state;CHECK(!w::decode_state({bytes.begin(),bytes.begin()+cut},unchanged,error));CHECK(unchanged==state);}
    auto malformed=bytes;malformed.push_back(0);CHECK(!w::decode_state(malformed,result,error));
    // Huge first container count must fail before allocating it.
    malformed=bytes;for(size_t i=40;i<44;++i)malformed[i]=255;CHECK(!w::decode_state(malformed,result,error));
    auto invalid=state;invalid.document.epoch=0;CHECK(!w::encode_state(invalid,bytes,error));
    invalid=state;invalid.history.front().operation.epoch=0;CHECK(!w::encode_state(invalid,bytes,error));
    invalid=state;invalid.history.front().transition_revision=4;CHECK(!w::encode_state(invalid,bytes,error));
    invalid=state;invalid.history.front().applied=true;CHECK(!w::encode_state(invalid,bytes,error));
    invalid=state;invalid.decision_order.push_back(invalid.decision_order.front());CHECK(!w::encode_state(invalid,bytes,error));
    invalid=state;invalid.document.placements.begin()->second.position.x=std::numeric_limits<float>::infinity();CHECK(!w::encode_state(invalid,bytes,error));
    invalid=state;invalid.document.placements.begin()->second.properties[5]=std::string("\xc0\xaf");CHECK(!w::encode_state(invalid,bytes,error));
    const auto q=request();Request decoded;CHECK(w::encode_request(q,bytes,error));CHECK(w::decode_request(bytes,decoded,error));CHECK(decoded==q);
    for(size_t cut=0;cut<bytes.size();++cut){decoded=q;CHECK(!w::decode_request({bytes.begin(),bytes.begin()+cut},decoded,error));CHECK(decoded==q);}
    auto invalid_q=q;invalid_q.operation.epoch++;CHECK(!w::encode_request(invalid_q,bytes,error));invalid_q=q;invalid_q.intent=Intent(255);CHECK(!w::encode_request(invalid_q,bytes,error));
    invalid_q=q;invalid_q.summary=std::string("\xed\xa0\x80");CHECK(!w::encode_request(invalid_q,bytes,error));
    auto clone_q=q;clone_q.mutations.clear();Mutation clone;clone.kind=MutationKind::Clone;clone.target={3,1};clone.expected=3;
    clone.clone_pose=Pose{{30,40,50},{0,0,1,0},2};clone_q.mutations.push_back(clone);
    CHECK(w::encode_request(clone_q,bytes,error));CHECK(w::decode_request(bytes,decoded,error));CHECK(decoded==clone_q);
    for(size_t cut=0;cut<bytes.size();++cut){decoded=clone_q;CHECK(!w::decode_request({bytes.begin(),bytes.begin()+cut},decoded,error));CHECK(decoded==clone_q);}
    clone_q.mutations.front().clone_pose->position.x=std::numeric_limits<float>::quiet_NaN();CHECK(!w::encode_request(clone_q,bytes,error));
    auto decision=state.decisions.begin()->second;Decision decoded_d;CHECK(w::encode_decision(decision,bytes,error));CHECK(w::decode_decision(bytes,decoded_d,error));CHECK(decoded_d==decision);
    decision.change->allocated.push_back({5,1});decision.change->clone_sources[ObjectId{5,1}]={3,1};
    CHECK(w::encode_decision(decision,bytes,error));CHECK(w::decode_decision(bytes,decoded_d,error));CHECK(decoded_d==decision);
    auto invalid_clone=decision;invalid_clone.change->clone_sources[ObjectId{5,1}]={5,1};CHECK(!w::encode_decision(invalid_clone,bytes,error));
    invalid_clone=decision;invalid_clone.change->allocated.clear();CHECK(!w::encode_decision(invalid_clone,bytes,error));
    invalid_clone=decision;invalid_clone.change->clone_sources[ObjectId{5,1}]={3,0};CHECK(!w::encode_decision(invalid_clone,bytes,error));
    decision.change.reset();CHECK(!w::encode_decision(decision,bytes,error));
}
static void authority(){
    auto c=context();std::string error;w::Message result;
    std::vector<w::Payload> host_payloads{w::Offer{w::kSchema,"Shared adventure",guest},w::LeaseReply{5,{Code::Accepted,"",{}}},sample().decisions.begin()->second,w::Saved{3,{4,5,6},91}};
    for(const auto& p:host_payloads){const auto bytes=packet({c,p});CHECK(read(bytes,result,host,true));CHECK(!read(bytes,result,guest,false));CHECK(!read(bytes,result,other,true));}
    auto short_request=request();short_request.mutations.resize(1);
    const std::string hash(64,'0');std::vector<w::Payload> guest_payloads{w::Join{0},w::LeaseRequest{w::LeaseAction::Acquire,4,1000,{{ScopeKind::Placement,{3,1},{}}},{}},short_request,w::Coverage{3,hash},w::SavedApplied{{3,{4,5,6},91}}};
    for(const auto& p:guest_payloads){const auto bytes=packet({c,p});CHECK(read(bytes,result,guest,false));CHECK(!read(bytes,result,host,true));}
    auto q=short_request;const auto bytes=packet({c,q});CHECK(!read(bytes,result,other,false));CHECK(!read(bytes,result,Peer{1,93},false));
    const auto presence=packet({c,w::Presence{{3,1},{2,1},true,guest}});CHECK(read(presence,result,guest,false));CHECK(read(presence,result,host,true));CHECK(!read(presence,result,other,false));
    for(auto kind:{w::TransferKind::Baseline,w::TransferKind::Decision,w::TransferKind::Resources}){const auto b=packet({c,w::TransferBegin{kind,1,42,hash}});CHECK(read(b,result,host,true));CHECK(!read(b,result,guest,false));}
    const auto req=packet({c,w::TransferBegin{w::TransferKind::Request,1,42,hash}});CHECK(read(req,result,guest,false));CHECK(!read(req,result,host,true));
    for(unsigned i=0;i<=6;++i){const bool client=i==1||i==5;w::Control control{w::ControlKind(i),81,c.revision,i==1,0,uint8_t(i==0||i==2?3:0)};const auto b=packet({c,control});CHECK(read(b,result,client?guest:host,!client));CHECK(!read(b,result,client?host:guest,client));}
    net::Writer writer;CHECK(!w::write({c,w::Control{w::ControlKind::HostBeginPlaytest,0,3,false}},writer,error));CHECK(!w::write({c,w::Control{w::ControlKind::HostBeginPlaytest,1,2,false}},writer,error));CHECK(!w::write({c,w::Control{w::ControlKind::HostBeginPlaytest,1,3,true}},writer,error));
    CHECK(!w::write({c,w::Saved{3,{4,5,6},0}},writer,error));CHECK(!w::write({c,w::SavedApplied{{3,{4,5,6},0}}},writer,error));
    CHECK(!w::write({c,w::SavedApplied{{4,{4,5,6},91}}},writer,error));
    auto saved_ack=packet({c,w::SavedApplied{{3,{4,5,6},91}}});
    for(size_t cut=0;cut<saved_ack.size();++cut)CHECK(!read({saved_ack.begin(),saved_ack.begin()+cut},result,guest,false));
    CHECK(!w::write({c,w::Control{w::ControlKind::GuestReadyPlaytest,1,3,true,1}},writer,error));
    CHECK(!w::write({c,w::Control{w::ControlKind::HostReturnEditor,1,3,false,1}},writer,error));
    CHECK(!w::write({c,w::Control{w::ControlKind::GuestEditorReturned,1,3,true,0}},writer,error));
    CHECK(!w::write({c,w::Control{w::ControlKind::HostEditorResumed,1,3,false,1}},writer,error));
    CHECK(!w::write({c,w::Control{w::ControlKind::HostPreparePlaytest,1,3,false,16}},writer,error));
    CHECK(read(packet({c,w::Control{w::ControlKind::HostPreparePlaytest,1,3,false,7,7}}),result,host,true));
    CHECK(!w::write({c,w::Control{w::ControlKind::HostBeginPlaytest,1,3,false,7,3}},writer,error));
    CHECK(!w::write({c,w::Control{w::ControlKind::HostPreparePlaytest,1,3,false,0,2}},writer,error));
    CHECK(!w::write({c,w::Offer{w::kSchema,"bad",host}},writer,error));CHECK(!w::write({c,w::Offer{w::kSchema,"bad",Peer{1,0}}},writer,error));
    CHECK(!w::write({c,w::Offer{w::kSchema,"bad",guest,other}},writer,error));
    CHECK(read(packet({c,w::Offer{w::kSchema,"rehost",guest,host}}),result,host,true));CHECK(std::get<w::Offer>(result.payload).host==host);
    auto bad=bytes;bad[0]=99;CHECK(!read(bad,result,guest,false));bad=bytes;bad.push_back(0);CHECK(!read(bad,result,guest,false));bad.assign(w::kPacketBytes+1,0);CHECK(!read(bad,result,guest,false));
    for(size_t cut=0;cut<bytes.size();++cut)CHECK(!read({bytes.begin(),bytes.begin()+cut},result,guest,false));
}
static std::vector<w::Message> messages(w::Context c,w::TransferKind kind,std::vector<uint8_t> bytes,uint64_t serial=17){w::Sender sender;std::string error;CHECK(sender.begin(c,kind,serial,std::move(bytes),error));CHECK(sender.active());CHECK(!sender.begin(c,kind,serial,{1},error));std::vector<w::Message> all;w::Message m;while(sender.next(m))all.push_back(m);CHECK(!sender.active());return all;}
static void transfer(std::vector<uint8_t> bytes,w::TransferKind kind,Peer sender,bool from_host){
    const auto c=context();const auto all=messages(c,kind,bytes);std::string error;w::Receiver receiver;std::vector<uint8_t> output{9};
    CHECK(receiver.begin(c,std::get<w::TransferBegin>(all.front().payload),sender,from_host,error));CHECK(receiver.begin(c,std::get<w::TransferBegin>(all.front().payload),sender,from_host,error));
    auto conflict=std::get<w::TransferBegin>(all.front().payload);conflict.serial++;CHECK(!receiver.begin(c,conflict,sender,from_host,error));
    const auto end=std::get<w::TransferEnd>(all.back().payload);CHECK(!receiver.finish(c,end,output,error));CHECK(output==std::vector<uint8_t>{9});
    auto stale=c;stale.epoch++;
    for(size_t i=all.size()-1;i-->1;){w::Message parsed;CHECK(read(packet(all[i]),parsed,sender,from_host));const auto chunk=std::get<w::TransferChunk>(parsed.payload);
        CHECK(!receiver.chunk(stale,chunk,error));CHECK(receiver.chunk(c,chunk,error));CHECK(receiver.chunk(c,chunk,error));
        auto changed=chunk;changed.bytes[0]^=1;CHECK(!receiver.chunk(c,changed,error));}
    CHECK(!receiver.finish(stale,end,output,error));CHECK(receiver.finish(c,end,output,error));CHECK(output==bytes);CHECK(!receiver.active());CHECK(!receiver.finish(c,end,output,error));
    // Corruption reaching a previously unseen chunk is detected only at SHA seal.
    CHECK(receiver.begin(c,std::get<w::TransferBegin>(all.front().payload),sender,from_host,error));
    for(size_t i=1;i+1<all.size();++i){auto chunk=std::get<w::TransferChunk>(all[i].payload);if(i==1)chunk.bytes[0]^=1;CHECK(receiver.chunk(c,chunk,error));}
    output={9};CHECK(!receiver.finish(c,end,output,error));CHECK(output==std::vector<uint8_t>{9});receiver.reset();CHECK(!receiver.active());
}
static void transfers(){
    std::string error;std::vector<uint8_t> bytes;CHECK(w::encode_state(sample(),bytes,error));transfer(bytes,w::TransferKind::Baseline,host,true);
    CHECK(w::encode_request(request(),bytes,error));transfer(bytes,w::TransferKind::Request,guest,false);
    w::Join join;join.base_size=100;join.base_hash=std::string(64,'a');join.records.push_back({{1,2,3},42,std::string(64,'b')});
    CHECK(w::encode_join(join,bytes,error));transfer(bytes,w::TransferKind::Join,guest,false);
    CHECK(w::encode_decision(sample().decisions.begin()->second,bytes,error));transfer(bytes,w::TransferKind::Decision,host,true);
    bytes.resize(4517);for(size_t i=0;i<bytes.size();++i)bytes[i]=uint8_t(i*71);transfer(bytes,w::TransferKind::Resources,host,true);
    w::Receiver receiver;auto c=context();const std::string hash(64,'0');
    for(auto [kind,limit]:std::vector<std::pair<w::TransferKind,size_t>>{{w::TransferKind::Baseline,w::kStateBytes},{w::TransferKind::Request,w::kOperationBytes},{w::TransferKind::Resources,w::kResourceBytes},{w::TransferKind::Join,w::kJoinBytes}}){CHECK(!receiver.begin(c,{kind,1,uint32_t(limit+1),hash},(kind==w::TransferKind::Request||kind==w::TransferKind::Join)?guest:host,kind!=w::TransferKind::Request&&kind!=w::TransferKind::Join,error));CHECK(!receiver.active());}
    CHECK(!receiver.begin(c,{w::TransferKind(255),1,1,hash},host,true,error));CHECK(!receiver.active());
    // A valid hash cannot turn arbitrary bytes into a baseline or a forged request.
    const auto all=messages(c,w::TransferKind::Baseline,{1,2,3});CHECK(receiver.begin(c,std::get<w::TransferBegin>(all[0].payload),host,true,error));CHECK(receiver.chunk(c,std::get<w::TransferChunk>(all[1].payload),error));CHECK(!receiver.finish(c,std::get<w::TransferEnd>(all[2].payload),bytes,error));receiver.reset();
    auto q=request();q.operation.author=other;CHECK(w::encode_request(q,bytes,error));const auto forged=messages(c,w::TransferKind::Request,bytes);CHECK(receiver.begin(c,std::get<w::TransferBegin>(forged.front().payload),guest,false,error));for(size_t i=1;i+1<forged.size();++i)CHECK(receiver.chunk(c,std::get<w::TransferChunk>(forged[i].payload),error));CHECK(!receiver.finish(c,std::get<w::TransferEnd>(forged.back().payload),bytes,error));
}
static void preview_authority_and_malformed()
{
    const auto c=context();std::string error;w::Message result;net::Writer output;
    w::TransformPreview preview{guest,1,true,{3,1},{2,1},Pose{{1,2,3},{0,0,0,1},2},{{ScopeKind::Placement,{3,1},{}},8}};
    const auto bytes=packet({c,preview});CHECK(read(bytes,result,guest,false));CHECK(read(bytes,result,host,true));CHECK(!read(bytes,result,other,false));
    CHECK(std::get<w::TransformPreview>(result.payload)==preview);
    for(size_t cut=0;cut<bytes.size();++cut)CHECK(!read({bytes.begin(),bytes.begin()+cut},result,guest,false));
    auto bad=preview;bad.pose.position.x=std::numeric_limits<float>::infinity();CHECK(!w::write({c,bad},output,error));
    bad=preview;bad.pose.rotation={0,0,0,0};CHECK(!w::write({c,bad},output,error));
    bad=preview;bad.pose.scale=0;CHECK(!w::write({c,bad},output,error));
    bad=preview;bad.lease.scope.object={4,1};CHECK(!w::write({c,bad},output,error));
    bad=preview;bad.serial=0;CHECK(!w::write({c,bad},output,error));
    bad=preview;bad.active=false;CHECK(!w::write({c,bad},output,error));
    bad.object={};bad.act={};bad.pose={};bad.lease={};CHECK(read(packet({c,bad}),result,guest,false));
    w::Presence presence{{3,1},{2,1},true,guest};presence.pending=true;presence.ownership=w::kOwnSelection;presence.preview_lease=preview.lease;
    CHECK(read(packet({c,presence}),result,host,true));CHECK(!read(packet({c,presence}),result,guest,false));
    presence.ownership=0;presence.preview_lease={};presence.phase=w::PresencePhase::Frozen;CHECK(!read(packet({c,presence}),result,guest,false));
    presence.phase=w::PresencePhase::Ready;CHECK(read(packet({c,presence}),result,guest,false));
}
static void join_inventory_codec_bounds_and_authority()
{
    w::Join join;join.base_hash=std::string(64,'a');join.base_size=1024;
    for(unsigned i=0;i<2048;++i)join.records.push_back({{i+1,9,1},1,std::string(64,'b')});
    std::vector<uint8_t> bytes;std::string error;CHECK(w::encode_join(join,bytes,error));CHECK(bytes.size()>w::kPacketBytes&&bytes.size()<=w::kJoinBytes);
    w::Join decoded;CHECK(w::decode_join(bytes,decoded,error));CHECK(decoded==join);
    for(size_t cut:{size_t(0),size_t(3),size_t(8),bytes.size()/2,bytes.size()-1}){decoded=join;CHECK(!w::decode_join({bytes.begin(),bytes.begin()+cut},decoded,error));CHECK(decoded==join);}
    auto bad=join;bad.records.push_back({{9999,9,1},1,std::string(64,'b')});CHECK(!w::encode_join(bad,bytes,error));
    bad=join;bad.records[1]=bad.records[0];CHECK(!w::encode_join(bad,bytes,error));
    bad=join;bad.records[0].size=(16u<<20)+1;CHECK(!w::encode_join(bad,bytes,error));
    bad=join;bad.base_size=0;CHECK(!w::encode_join(bad,bytes,error));
    bad=join;bad.records[0].hash[0]='z';CHECK(!w::encode_join(bad,bytes,error));
    auto c=context();w::Message parsed;const auto header=packet({c,w::TransferBegin{w::TransferKind::Join,1,100,std::string(64,'0')}});
    CHECK(read(header,parsed,guest,false));CHECK(!read(header,parsed,host,true));
    net::Writer packet_output;CHECK(!w::write({c,join},packet_output,error));
}
static void malformed(){
    std::mt19937 random(0x41876);w::Message message;std::string error;State state;Request request;Decision decision;
    for(unsigned i=0;i<3000;++i){std::vector<uint8_t> bytes(random()%512);for(auto& x:bytes)x=uint8_t(random());read(bytes,message,guest,false);w::decode_state(bytes,state,error);w::decode_request(bytes,request,error);w::decode_decision(bytes,decision,error);}
    CHECK(true);
}
static void class_settings_codec(){
    auto s=sample();auto& d=s.document;d.next_id=6;
    d.class_schema[200]={ValueKind::Number,1,10000,0,false,false};
    ClassActSettings c;c.object={5,1};c.class_id={1,1};c.act_id={2,1};c.revision=3;
    c.properties[200]=100.f;c.field_revisions[{Field::Property,200}]=3;d.class_settings[c.object]=c;
    std::string error;std::vector<uint8_t> bytes;CHECK(w::encode_state(s,bytes,error));
    State result;CHECK(w::decode_state(bytes,result,error) && result==s);
    for(size_t cut=0;cut<bytes.size();++cut){auto unchanged=s;
        CHECK(!w::decode_state({bytes.begin(),bytes.begin()+cut},unchanged,error));CHECK(unchanged==s);}
    auto bad=bytes;bad[24]=2;result=s;CHECK(!w::decode_state(bad,result,error) && result==s);
    bad=bytes;bad.push_back(0);CHECK(!w::decode_state(bad,result,error) && result==s);
    auto legacy=s;legacy.document.class_schema.clear();legacy.document.class_settings.clear();
    std::vector<uint8_t> old;CHECK(w::encode_state(legacy,old,error));
    CHECK(old.size()+28<bytes.size());CHECK(std::equal(old.begin(),old.end(),bytes.begin()+28));
    // Malicious extension counts fail before unbounded allocation.
    bad=bytes;for(size_t i=28+old.size();i<32+old.size();++i)bad[i]=255;
    CHECK(!w::decode_state(bad,result,error) && result==s);
    CHECK(w::decode_state(old,result,error) && result==legacy);
    std::vector<uint8_t> again;CHECK(w::encode_state(result,again,error) && again==old);
    // Legacy UUIDs matching both magic words remain unambiguous: their epoch
    // at byte16 is nonzero, unlike the new versioned wrapper.
    uint64_t magic=UINT64_C(0x3153504344455053);std::memcpy(legacy.document.project.data(),&magic,8);
    std::memcpy(legacy.document.project.data()+8,&magic,8);
    CHECK(w::encode_state(legacy,old,error));CHECK(w::decode_state(old,result,error) && result==legacy);

    Coordinator core;CHECK(core.reset(s,s.document.epoch,error));CHECK(core.connect(host));
    auto lease=core.acquire(host,{{ScopeKind::Class,{1,1},{}}},0,1000);CHECK(lease.accepted());
    State replica=s;
    for(unsigned i=0;i<3;++i){
        Request q;q.project=d.project;q.epoch=d.epoch;q.base_revision=core.state().document.revision;
        q.operation={host,100+i,d.epoch};q.summary="Class health";
        q.leases.push_back({lease.leases.front().scope,lease.leases.front().token});
        if(!i){Mutation m;m.target=c.object;m.field={Field::Property,200};m.value=200.f;m.expected=3;q.mutations.push_back(m);}
        else q.intent=i==1?Intent::Undo:Intent::Redo;
        auto decision=core.submit(q,i);CHECK(decision.accepted());
        std::vector<uint8_t> encoded;CHECK(w::encode_decision(decision,encoded,error));Decision decoded;
        CHECK(w::decode_decision(encoded,decoded,error));CHECK(apply_committed(replica,decoded,error));
        CHECK(w::encode_state(core.state(),encoded,error));CHECK(w::decode_state(encoded,result,error));
        CHECK(result==core.state() && replica==core.state());
        Coordinator recovered;CHECK(recovered.reset(result,1000+i,error));
        CHECK(recovered.state().history==core.state().history);
    }
}
int main(){codec();authority();transfers();preview_authority_and_malformed();join_inventory_codec_bounds_and_authority();malformed();class_settings_codec();std::cout<<"editor protocol: "<<checks<<" checks passed\n";}
