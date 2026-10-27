#include "editor_protocol.h"
#include "../../../updater/sha256.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <tuple>

namespace editor::wire {
namespace {
struct Invalid : std::runtime_error { using runtime_error::runtime_error; };
void need(bool yes, const char* why) { if (!yes) throw Invalid(why); }
bool project(const ProjectId& p) { return std::any_of(p.begin(),p.end(),[](auto x){return x!=0;}); }
bool peer(Peer p) { return p.slot<4 && p.generation; }
bool operation(OperationId p) { return peer(p.author) && p.sequence && p.epoch; }
bool context(const Context& c) { return project(c.project) && c.epoch && c.revision!=UINT64_MAX; }
bool hash(const std::string& h) {
    return h.size()==64 && std::all_of(h.begin(),h.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
bool text(const std::string& s) {
    for(size_t i=0;i<s.size();) {
        const auto c=uint8_t(s[i++]);
        if(c==0)return false;
        if(c<0x80)continue;
        unsigned count=0;uint32_t value=0,minimum=0;
        if(c>=0xc2&&c<=0xdf){count=1;value=c&31;minimum=0x80;}
        else if(c>=0xe0&&c<=0xef){count=2;value=c&15;minimum=0x800;}
        else if(c>=0xf0&&c<=0xf4){count=3;value=c&7;minimum=0x10000;}
        else return false;
        if(count>s.size()-i)return false;
        while(count--){const auto next=uint8_t(s[i++]);if((next&0xc0)!=0x80)return false;value=(value<<6)|(next&63);}
        if(value<minimum||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return false;
    }
    return true;
}
template<class T> struct Seq : std::false_type {};
template<class T,class A> struct Seq<std::vector<T,A>> : std::true_type {};
template<class T,class A> struct Seq<std::deque<T,A>> : std::true_type {};
template<class T> struct Set : std::false_type {};
template<class T,class C,class A> struct Set<std::set<T,C,A>> : std::true_type {};
template<class T> struct Map : std::false_type {};
template<class K,class V,class C,class A> struct Map<std::map<K,V,C,A>> : std::true_type {};
template<class T> struct Optional : std::false_type {};
template<class T> struct Optional<std::optional<T>> : std::true_type {};
template<class T> struct Variant : std::false_type {};
template<class... T> struct Variant<std::variant<T...>> : std::true_type {};
template<class T> constexpr unsigned maximum() {
    if constexpr(std::is_same_v<T,ValueKind>) return 7;
    if constexpr(std::is_same_v<T,Field>) return 3;
    if constexpr(std::is_same_v<T,CheckKind>) return 2;
    if constexpr(std::is_same_v<T,ScopeKind>) return 4;
    if constexpr(std::is_same_v<T,MutationKind>) return 3;
    if constexpr(std::is_same_v<T,Intent>) return 2;
    if constexpr(std::is_same_v<T,Code>) return unsigned(Code::Busy);
    if constexpr(std::is_same_v<T,TransferKind>) return 4;
    if constexpr(std::is_same_v<T,LeaseAction>) return 2;
    if constexpr(std::is_same_v<T,ControlKind>) return 6;
    if constexpr(std::is_same_v<T,PresencePhase>) return 7;
    if constexpr(std::is_same_v<T,CommandKind>) return unsigned(CommandKind::ExitEditor);
    return 0;
}
#define FIELDS(T,...) template<class A> void fields(A& a,T& v) { a(__VA_ARGS__); }
FIELDS(ObjectId,v.id,v.generation)
FIELDS(Peer,v.slot,v.generation)
FIELDS(OperationId,v.author,v.sequence,v.epoch)
FIELDS(Vec3,v.x,v.y,v.z)
FIELDS(Rotation,v.x,v.y,v.z,v.w)
FIELDS(Pose,v.position,v.rotation,v.scale)
FIELDS(ResourceKey,v.instance,v.type,v.group)
FIELDS(FieldKey,v.field,v.property)
FIELDS(PropertySpec,v.kind,v.minimum,v.maximum,v.max_text,v.optional,v.nullable)
FIELDS(Placement,v.object,v.class_id,v.act_id,v.protected_object,v.position,v.rotation,v.scale,v.properties,v.revision,v.field_revisions)
FIELDS(Reference,v.owner,v.target)
FIELDS(ClassActSettings,v.object,v.class_id,v.act_id,v.properties,v.revision,v.field_revisions)
FIELDS(Document,v.project,v.epoch,v.revision,v.next_id,v.classes,v.acts,v.schema,v.placements,v.tombstones,v.external_references)
FIELDS(Check,v.object,v.kind,v.field,v.expected)
FIELDS(Scope,v.kind,v.object,v.field)
FIELDS(Lease,v.scope,v.owner,v.token,v.expires)
FIELDS(LeaseToken,v.scope,v.token)
FIELDS(Mutation,v.kind,v.target,v.initial,v.field,v.value,v.expected,v.clone_pose)
FIELDS(Request,v.project,v.epoch,v.operation,v.base_revision,v.intent,v.summary,v.mutations,v.dependencies,v.leases)
FIELDS(ObjectEdit,v.object,v.before,v.after,v.expected)
FIELDS(FieldEdit,v.object,v.field,v.before,v.after,v.expected,v.restored_revision,v.restored_object_revision)
FIELDS(Patch,v.objects,v.fields,v.dependencies)
FIELDS(Change,v.operation,v.gesture,v.intent,v.revision,v.summary,v.patch,v.inverse,v.allocated,v.clone_sources)
FIELDS(Decision,v.code,v.reason,v.revision,v.change)
FIELDS(Gesture,v.operation,v.original_revision,v.transition_revision,v.summary,v.applied,v.redo_available,v.undo,v.redo)
FIELDS(State,v.document,v.history,v.decisions,v.decision_order,v.retired_sequences,v.history_truncated)
FIELDS(Context,v.project,v.epoch,v.revision)
FIELDS(Offer,v.schema,v.name,v.assigned,v.host)
FIELDS(ContentClaim,v.key,v.size,v.hash)
FIELDS(Join,v.durable_revision,v.base_hash,v.base_size,v.records)
FIELDS(TransferBegin,v.kind,v.serial,v.total,v.hash)
FIELDS(TransferChunk,v.serial,v.offset,v.bytes)
FIELDS(TransferEnd,v.serial)
FIELDS(LeaseRequest,v.action,v.serial,v.ttl,v.scopes,v.tokens)
FIELDS(LeaseResult,v.code,v.reason,v.leases)
FIELDS(LeaseReply,v.serial,v.result)
FIELDS(Presence,v.selected,v.act,v.editing,v.author,v.pending,v.ownership,v.phase,v.preview_lease)
FIELDS(TransformPreview,v.author,v.serial,v.active,v.object,v.act,v.pose,v.lease)
FIELDS(Coverage,v.durable_revision,v.state_hash)
FIELDS(Saved,v.saved_revision,v.resource,v.serial)
FIELDS(SavedApplied,v.saved)
FIELDS(Leave,v.reason)
FIELDS(Control,v.kind,v.serial,v.revision,v.ready,v.ready_mask,v.participant_mask)
FIELDS(Command,v.kind,v.serial,v.revision,v.actor,v.accepted)
#undef FIELDS
struct Encode {
    net::Writer w; size_t limit;
    template<class... T> void operator()(const T&... v) { (one(v),...); }
    template<class T> void one(const T& v) {
        if constexpr(std::is_same_v<T,bool>) w.u8(v?1:0);
        else if constexpr(std::is_enum_v<T>) { need(unsigned(v)<=maximum<T>(),"invalid enum"); w.u8(uint8_t(v)); }
        else if constexpr(std::is_arithmetic_v<T>) {
            if constexpr(std::is_floating_point_v<T>) need(std::isfinite(v),"nonfinite number");
            w.raw(&v,sizeof v);
        } else if constexpr(std::is_same_v<T,std::string>) {
            need(v.size()<=1024 && text(v),"invalid text"); w.u32(uint32_t(v.size()));
            if(!v.empty()) w.raw(v.data(),v.size());
        } else if constexpr(std::is_same_v<T,ProjectId>) w.raw(v.data(),v.size());
        else if constexpr(Optional<T>::value) { one(bool(v)); if(v) one(*v); }
        else if constexpr(Variant<T>::value) { w.u8(uint8_t(v.index())); std::visit([&](const auto& x){one(x);},v); }
        else if constexpr(Seq<T>::value || Set<T>::value || Map<T>::value) {
            need(v.size()<=16384,"container exceeds bound"); w.u32(uint32_t(v.size()));
            for(const auto& x:v) {
                if constexpr(Map<T>::value) { one(x.first); one(x.second); }
                else one(x);
            }
        } else fields(*this,const_cast<T&>(v));
        need(w.size()<=limit,"encoded message exceeds bound");
    }
};
struct Decode {
    net::Reader r; size_t budget;
    explicit Decode(const std::vector<uint8_t>& bytes):r(bytes.data(),bytes.size()),budget(std::min(kStateBytes*2,bytes.size()*8+(1u<<20))) {}
    void claim(size_t n) { need(n<=budget,"allocation budget exceeded"); budget-=n; }
    template<class... T> void operator()(T&... v) { (one(v),...); }
    template<size_t I=0,class T> void variant(T& v,uint8_t tag) {
        if constexpr(I<std::variant_size_v<T>) {
            if(tag==I) { v.template emplace<I>(); one(std::get<I>(v)); }
            else variant<I+1>(v,tag);
        } else throw Invalid("invalid variant tag");
    }
    template<class T> void one(T& v) {
        if constexpr(std::is_same_v<T,bool>) { const auto b=r.u8(); need(b<=1,"invalid boolean"); v=b!=0; }
        else if constexpr(std::is_enum_v<T>) { const auto b=r.u8(); need(b<=maximum<T>(),"invalid enum"); v=T(b); }
        else if constexpr(std::is_arithmetic_v<T>) {
            need(r.raw(&v,sizeof v),"truncated scalar");
            if constexpr(std::is_floating_point_v<T>) need(std::isfinite(v),"nonfinite number");
        } else if constexpr(std::is_same_v<T,std::string>) {
            const size_t n=r.u32(); need(n<=1024 && n<=r.left(),"invalid text length"); claim(n);
            v.assign(reinterpret_cast<const char*>(r.cur()),n); r.skip(n); need(text(v),"invalid UTF-8 text");
        } else if constexpr(std::is_same_v<T,ProjectId>) need(r.raw(v.data(),v.size()),"truncated project");
        else if constexpr(Optional<T>::value) { bool present=false; one(present); if(present) {v.emplace();one(*v);}else v.reset(); }
        else if constexpr(Variant<T>::value) variant(v,r.u8());
        else if constexpr(Seq<T>::value || Set<T>::value || Map<T>::value) {
            const size_t n=r.u32(); need(r.ok() && n<=16384,"container exceeds bound");
            claim(n*(sizeof(typename T::value_type)+16)); v.clear();
            for(size_t i=0;i<n;++i) {
                if constexpr(Map<T>::value) {
                    typename T::key_type key{}; typename T::mapped_type value{}; one(key); one(value);
                    need(v.emplace(std::move(key),std::move(value)).second,"duplicate map key");
                } else {
                    typename T::value_type value{}; one(value);
                    if constexpr(Set<T>::value) need(v.insert(std::move(value)).second,"duplicate set value");
                    else v.push_back(std::move(value));
                }
            }
        } else fields(*this,v);
        need(r.ok(),"truncated message");
    }
};
void validate(const State& s) {
    Limits limits; std::string error;
    // Use the coordinator's recovery gate so persisted/baseline history receives
    // exactly the same semantic and memory checks as a resumed host document.
    Coordinator verifier(limits);
    need(verifier.reset(s,s.document.epoch,error),error.empty()?"invalid durable state":error.c_str());
}
void validate(const Request& q) {
    Limits limits;
    need(project(q.project) && q.epoch && operation(q.operation) && q.operation.epoch==q.epoch,"invalid request scope");
    need(q.summary.size()<=limits.summary_bytes && q.mutations.size()<=limits.mutations &&
         q.dependencies.size()<=limits.dependencies && q.leases.size()<=limits.leases,"request exceeds limit");
    need(q.intent==Intent::Forward ? !q.mutations.empty() : q.mutations.empty(),"invalid history request shape");
}
void validate(const Decision& d) {
    need(d.reason.size()<=1024 && d.accepted()==bool(d.change),"invalid decision shape");
    if(d.change) {
        const auto& c=*d.change;
        need(operation(c.operation) && operation(c.gesture) && d.revision==c.revision &&
            c.summary.size()<=256 && c.allocated.size()<=64 && c.clone_sources.size()<=c.allocated.size(),"invalid committed change");
        need(c.intent==Intent::Forward || c.clone_sources.empty(),"history operation contains new clone identities");
        for(const auto& [allocated,source]:c.clone_sources)
            need(allocated.id && allocated.generation && source.id && source.generation && allocated!=source &&
                std::find(c.allocated.begin(),c.allocated.end(),allocated)!=c.allocated.end(),"invalid native clone identity");
    }
}
void validate(const Join& value) {
    need(value.durable_revision!=UINT64_MAX && value.records.size()<=2048 &&
        ((value.base_size==0)==value.base_hash.empty()) && value.base_size<=(16u<<20) &&
        (!value.base_size || hash(value.base_hash)),"invalid join cache inventory");
    uint64_t total=0;
    const auto order=[](const ContentClaim& c){return std::tuple{c.key.group,c.key.type,c.key.instance};};
    for(size_t n=0;n<value.records.size();++n){const auto& record=value.records[n];
        need(record.key.type && record.size<=(16u<<20) && hash(record.hash) &&
            (!n || order(value.records[n-1])<order(record)),"invalid or unordered join cache claim");
        total+=record.size;need(total<=kResourceBytes,"join cache inventory exceeds bundle bounds");
    }
}
size_t transfer_limit(TransferKind kind) {
    return kind==TransferKind::Resources?kResourceBytes:kind==TransferKind::Baseline?kStateBytes:kind==TransferKind::Join?kJoinBytes:kOperationBytes;
}
template<class T> bool encode(const T& object,std::vector<uint8_t>& bytes,size_t limit,std::string& error) {
    try { validate(object); Encode e{{},limit}; e.one(object); bytes=std::move(e.w.data); error.clear(); return true; }
    catch(const std::exception& e) { error=e.what(); return false; }
}
template<class T> bool decode(const std::vector<uint8_t>& bytes,T& object,size_t limit,std::string& error) {
    try { need(!bytes.empty() && bytes.size()<=limit,"invalid encoded length"); Decode d(bytes); T result{};
        d.one(result); need(d.r.ok() && !d.r.left(),"trailing bytes"); validate(result); object=std::move(result); error.clear(); return true;
    } catch(const std::exception& e) { error=e.what(); return false; }
}
void validate_payload(const Message& m) {
    need(context(m.context),"invalid message context");
    std::visit([&](const auto& p){
        using T=std::decay_t<decltype(p)>;
        if constexpr(std::is_same_v<T,Command>) need(p.serial && peer(p.actor) && p.revision==m.context.revision &&
            unsigned(p.kind)<=unsigned(CommandKind::ExitEditor) &&
            (!p.accepted || p.kind==CommandKind::ReplyPlaytest || p.kind==CommandKind::FinishExport || p.kind==CommandKind::EndExport),"invalid native editor command");
        if constexpr(std::is_same_v<T,Offer>) need(p.schema==kSchema && !p.name.empty() && p.name.size()<=256 && peer(p.assigned) && p.assigned.slot!=0 && peer(p.host) && p.host.slot==0,"unsupported offer");
        if constexpr(std::is_same_v<T,TransferBegin>) need(unsigned(p.kind)<=unsigned(TransferKind::Join) && p.serial && p.total && p.total<=transfer_limit(p.kind) && hash(p.hash),"invalid transfer header");
        if constexpr(std::is_same_v<T,Join>) {validate(p);need(p.durable_revision<=m.context.revision,"join durable revision exceeds its offered scope");}
        if constexpr(std::is_same_v<T,TransferChunk>) need(p.serial && !p.bytes.empty() && p.bytes.size()<=kChunkBytes && p.offset%kChunkBytes==0,"invalid transfer chunk");
        if constexpr(std::is_same_v<T,TransferEnd>) need(p.serial,"invalid transfer end");
        if constexpr(std::is_same_v<T,LeaseRequest>) need(p.serial && p.ttl<=30000 && p.scopes.size()<=64 && p.tokens.size()<=64 &&
            (p.action==LeaseAction::Acquire ? (!p.scopes.empty()&&p.tokens.empty()) : (p.scopes.empty()&&!p.tokens.empty())),"invalid lease request");
        if constexpr(std::is_same_v<T,LeaseReply>) need(p.serial && p.result.leases.size()<=64 && p.result.reason.size()<=1024,"invalid lease reply");
        if constexpr(std::is_same_v<T,Request>) {validate(p);need(p.project==m.context.project&&p.epoch==m.context.epoch&&p.base_revision==m.context.revision,"request context mismatch");}
        if constexpr(std::is_same_v<T,Decision>) {validate(p);need(p.revision==m.context.revision,"decision context mismatch");}
        if constexpr(std::is_same_v<T,Coverage>) need(p.durable_revision<=m.context.revision && hash(p.state_hash),"invalid durable coverage");
        if constexpr(std::is_same_v<T,Saved>) need(p.serial && p.saved_revision<=m.context.revision && p.resource.instance && p.resource.type,"invalid native save");
        if constexpr(std::is_same_v<T,SavedApplied>) need(p.saved.serial && p.saved.saved_revision<=m.context.revision && p.saved.resource.instance && p.saved.resource.type,"invalid applied native save");
        if constexpr(std::is_same_v<T,Leave>) need(p.reason<=3,"invalid leave reason");
        if constexpr(std::is_same_v<T,Presence>) need(peer(p.author) && p.ownership<=3 && unsigned(p.phase)<=unsigned(PresencePhase::Offered) &&
            ((p.ownership&kOwnSelection) ? (p.selected.id&&p.selected.generation&&p.preview_lease.token&&
                (p.preview_lease.scope.kind==ScopeKind::Document || (p.preview_lease.scope.kind==ScopeKind::Placement&&p.preview_lease.scope.object==p.selected))) :
                p.preview_lease==LeaseToken{}),"invalid presence authority");
        if constexpr(std::is_same_v<T,TransformPreview>) {
            const auto id=[](ObjectId v){return v.id && v.generation;};
            const float norm=p.pose.rotation.x*p.pose.rotation.x+p.pose.rotation.y*p.pose.rotation.y+p.pose.rotation.z*p.pose.rotation.z+p.pose.rotation.w*p.pose.rotation.w;
            need(peer(p.author) && p.serial && std::isfinite(norm) && norm>=0.99f && norm<=1.01f &&
                std::isfinite(p.pose.scale) && p.pose.scale>0 &&
                (p.active ? (id(p.object)&&id(p.act)&&p.lease.token &&
                    (p.lease.scope.kind==ScopeKind::Document ||
                     (p.lease.scope.kind==ScopeKind::Placement&&p.lease.scope.object==p.object))) :
                    (p.object==ObjectId{}&&p.act==ObjectId{}&&p.lease==LeaseToken{})),"invalid transform preview");
        }
        if constexpr(std::is_same_v<T,Control>) need(unsigned(p.kind)<=unsigned(ControlKind::HostEditorResumed) && p.serial && p.revision==m.context.revision &&
            (p.kind==ControlKind::GuestReadyPlaytest || !p.ready) && p.ready_mask<=15 && p.participant_mask<=15 &&
            ((p.kind!=ControlKind::HostPreparePlaytest && p.kind!=ControlKind::HostBeginPlaytest) || ((p.participant_mask&1) && !(p.ready_mask&~p.participant_mask))) &&
            ((p.kind==ControlKind::HostPreparePlaytest || p.kind==ControlKind::HostBeginPlaytest) || !p.ready_mask),"invalid playtest control");
    },m.payload);
}
void direction(const Message& m,Peer sender,bool host) {
    need(peer(sender) && (host ? sender.slot==0 : sender.slot!=0),"invalid session sender");
    const auto index=m.payload.index();
    const bool only_host=index==0||index==6||index==8||index==11;
    const bool only_guest=index==1||index==5||index==7||index==10||index==14;
    need(!only_host||host,"client forged host authority"); need(!only_guest||!host,"host sent client-only request");
    if(const auto* q=std::get_if<Request>(&m.payload)) need(q->operation.author==sender,"request author differs from session peer");
    if(const auto* p=std::get_if<Presence>(&m.payload)) need(host || (p->author==sender && !p->ownership && p->phase==PresencePhase::Ready && p->preview_lease==LeaseToken{}),"presence author or authority differs from session peer");
    if(const auto* p=std::get_if<TransformPreview>(&m.payload)) need(host||p->author==sender,"preview author differs from session peer");
    if(const auto* t=std::get_if<TransferBegin>(&m.payload))
        need((t->kind==TransferKind::Request || t->kind==TransferKind::Join) ? !host : host,"invalid transfer authority");
    if(const auto* c=std::get_if<Control>(&m.payload))
        need((c->kind==ControlKind::GuestReadyPlaytest || c->kind==ControlKind::GuestEditorReturned) ? !host : host,"invalid playtest authority");
    if(const auto* c=std::get_if<Command>(&m.payload)) {
        const bool guest=c->kind==CommandKind::RequestPlaytest || c->kind==CommandKind::RequestExport ||
            c->kind==CommandKind::ExportHeld || c->kind==CommandKind::FinishExport;
        need(guest ? (!host && c->actor==sender) : host,"invalid native editor command authority");
    }
}
}

bool encode_join(const Join& j,std::vector<uint8_t>& b,std::string& e) { return encode(j,b,kJoinBytes,e); }
bool decode_join(const std::vector<uint8_t>& b,Join& j,std::string& e) { return decode(b,j,kJoinBytes,e); }
// Empty placement-only documents keep their exact original encoding. The
// optional, versioned wrapper adds class-owned settings to durable checkpoints.
// Session protocol 17 fences older peers before this new state can be offered.
constexpr uint64_t kClassSettingsState=UINT64_C(0x3153504344455053);
bool encode_state(const State& s,std::vector<uint8_t>& b,std::string& error) {
    try {
        validate(s);Encode e{{},kStateBytes};
        if(!s.document.class_schema.empty() || !s.document.class_settings.empty()){
            // A legacy state starts with a 16-byte project and nonzero epoch.
            // Zero at byte16 makes this wrapper unambiguous for every valid
            // legacy project identity, including one matching our magic bytes.
            e(kClassSettingsState,kClassSettingsState,uint64_t(0),uint32_t(1),s,
                s.document.class_schema,s.document.class_settings);
        }else e.one(s);
        b=std::move(e.w.data);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
bool decode_state(const std::vector<uint8_t>& b,State& s,std::string& error) {
    try {
        need(!b.empty() && b.size()<=kStateBytes,"invalid encoded length");Decode d(b);State result{};
        uint64_t prefix[3]{};if(b.size()>=sizeof prefix)std::memcpy(prefix,b.data(),sizeof prefix);
        if(prefix[0]==kClassSettingsState && prefix[1]==kClassSettingsState && !prefix[2]){
            uint64_t first=0,second=0,reserved=0;uint32_t version=0;d(first,second,reserved,version);
            need(version==1,"unsupported class-settings checkpoint extension");d.one(result);
            d(result.document.class_schema,result.document.class_settings);
            need(!result.document.class_schema.empty() || !result.document.class_settings.empty(),"noncanonical empty class-settings extension");
        }else d.one(result);
        need(d.r.ok() && !d.r.left(),"trailing bytes");validate(result);s=std::move(result);error.clear();return true;
    } catch(const std::exception& e){error=e.what();return false;}
}
bool encode_request(const Request& q,std::vector<uint8_t>& b,std::string& e) { return encode(q,b,kOperationBytes,e); }
bool decode_request(const std::vector<uint8_t>& b,Request& q,std::string& e) { return decode(b,q,kOperationBytes,e); }
bool encode_decision(const Decision& d,std::vector<uint8_t>& b,std::string& e) { return encode(d,b,kOperationBytes,e); }
bool decode_decision(const std::vector<uint8_t>& b,Decision& d,std::string& e) { return decode(b,d,kOperationBytes,e); }
bool write(const Message& m,net::Writer& out,std::string& error) {
    try {validate_payload(m);Encode e{{},kPacketBytes};e(uint8_t(kSchema),m.context,m.payload);out.data=std::move(e.w.data);error.clear();return true;}
    catch(const std::exception& e){error=e.what();return false;}
}
bool read(net::Reader& reader,Message& out,Peer sender,bool host,std::string& error) {
    try {need(reader.left()<=kPacketBytes,"oversized packet");std::vector<uint8_t> bytes(reader.cur(),reader.cur()+reader.left());
        Decode d(bytes);uint8_t schema=0;Message m;d(schema,m.context,m.payload);need(schema==kSchema&&!d.r.left(),"schema or trailing bytes");
        validate_payload(m);direction(m,sender,host);reader.skip(bytes.size());out=std::move(m);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Receiver::begin(const Context& c,const TransferBegin& p,Peer sender,bool host,std::string& error) {
    try {Message m{c,p};validate_payload(m);direction(m,sender,host);
        if(active()) {need(context_==c&&sender_==sender&&serial_==p.serial&&kind_==p.kind&&hash_==p.hash&&data_.size()==p.total,"another transfer is active");error.clear();return true;}
        std::vector<uint8_t> data(p.total,0),chunks((p.total+kChunkBytes-1)/kChunkBytes,0);
        context_=c;sender_=sender;kind_=p.kind;serial_=p.serial;hash_=p.hash;
        data_=std::move(data);chunks_=std::move(chunks);received_=0;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Receiver::chunk(const Context& c,const TransferChunk& p,std::string& error) {
    try {need(active()&&c==context_&&p.serial==serial_,"stale transfer chunk");need(p.offset%kChunkBytes==0&&p.offset<data_.size(),"invalid chunk offset");
        const size_t n=std::min(kChunkBytes,data_.size()-p.offset);need(p.bytes.size()==n,"invalid chunk length");
        auto& present=chunks_[p.offset/kChunkBytes];
        if(present) need(std::equal(p.bytes.begin(),p.bytes.end(),data_.begin()+p.offset),"conflicting duplicate chunk");
        else {std::copy(p.bytes.begin(),p.bytes.end(),data_.begin()+p.offset);present=1;received_+=n;}
        error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Receiver::finish(const Context& c,const TransferEnd& p,std::vector<uint8_t>& out,std::string& error) {
    try {need(active()&&c==context_&&p.serial==serial_&&received_==data_.size(),"incomplete or stale transfer");
        need(upd::sha256_hex(data_)==hash_,"transfer checksum mismatch");
        if(kind_==TransferKind::Join) {Join j;need(decode_join(data_,j,error),"invalid join inventory");validate_payload({c,j});}
        if(kind_==TransferKind::Baseline) {State s;need(decode_state(data_,s,error),"invalid baseline");need(s.document.project==c.project&&s.document.epoch==c.epoch&&s.document.revision==c.revision,"baseline scope mismatch");}
        if(kind_==TransferKind::Request) {Request q;need(decode_request(data_,q,error),"invalid request");need(q.project==c.project&&q.epoch==c.epoch&&q.base_revision==c.revision&&q.operation.author==sender_,"request scope or session author mismatch");}
        if(kind_==TransferKind::Decision) {Decision d;need(decode_decision(data_,d,error),"invalid decision");need(d.revision==c.revision,"decision scope mismatch");}
        out=std::move(data_);reset();error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
void Receiver::reset(){context_={};sender_={};serial_=0;data_.clear();chunks_.clear();hash_.clear();received_=0;}
bool Sender::begin(Context c,TransferKind kind,uint64_t serial,std::vector<uint8_t> data,std::string& error) {
    try {need(!active(),"outgoing transfer active");TransferBegin header{kind,serial,uint32_t(data.size()),upd::sha256_hex(data)};validate_payload({c,header});
        need(data.size()==header.total,"transfer length overflow");context_=c;kind_=kind;serial_=serial;data_=std::move(data);hash_=header.hash;offset_=0;phase_=0;error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool Sender::next(Message& out) {
    if(!active()) return false;
    out.context=context_;
    if(phase_==0){out.payload=TransferBegin{kind_,serial_,uint32_t(data_.size()),hash_};phase_=1;}
    else if(offset_<data_.size()) {const size_t n=std::min(kChunkBytes,data_.size()-offset_);out.payload=TransferChunk{serial_,uint32_t(offset_),{data_.begin()+offset_,data_.begin()+offset_+n}};offset_+=n;}
    else {out.payload=TransferEnd{serial_};reset();}
    return true;
}
void Sender::reset(){context_={};serial_=0;data_.clear();hash_.clear();offset_=0;phase_=0;}
} // namespace editor::wire
