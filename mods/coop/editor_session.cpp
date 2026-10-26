#include "editor_session.h"
#include "../../updater/sha256.h"

#include <algorithm>
#include <deque>
#include <cmath>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace editor_session {
using namespace editor;
namespace w = editor::wire;
namespace {
constexpr Peer unknown_host{0, 1}; // Transport role only, before an authenticated Offer.
constexpr size_t envelope_bytes = 9; // Source capability and recipient generation.
using Buffer = std::shared_ptr<const std::vector<uint8_t>>;
bool same_context(const w::Context& a, const w::Context& b) { return a.project == b.project && a.epoch == b.epoch; }
bool same_transfer(const w::TransferBegin& a, const w::TransferBegin& b) { return a.kind == b.kind && a.serial == b.serial && a.total == b.total && a.hash == b.hash; }
}

bool document_hash(const Document& document, std::string& hash, std::string& error)
{
    State projection; projection.document = document;
    std::vector<uint8_t> bytes;
    if (!w::encode_state(projection, bytes, error)) return false;
    hash = upd::sha256_hex(bytes); return true;
}

struct Controller::Impl {
    struct Transfer {
        w::Context context;
        w::TransferKind kind{};
        uint64_t serial = 0;
        Buffer bytes;
        std::string hash;
        size_t offset = 0;
        uint8_t phase = 0;
    };
    using Out = std::variant<net::Writer, Transfer>;
    struct Channel {
        std::deque<net::Writer> priority;
        std::deque<Out> ordered;
        w::Receiver receiver;
        std::map<uint64_t, std::pair<w::Context, w::TransferBegin>> completed;
        std::map<uint64_t, std::pair<w::LeaseReply, Revision>> lease_decisions;
        uint64_t retired_lease_serial = 0;
        std::map<Scope,uint64_t> released_lease_scopes;
        Revision baseline = 0;
        uint64_t baseline_save_serial = 0;
        bool resources = false;
    };
    struct Input { uint8_t kind = 0, from = 0; std::vector<uint8_t> bytes; };
    struct HeldRequest { Peer author; Request request; size_t cost = 0; };
    using Local = std::variant<Request, w::LeaseRequest, w::Presence, w::Saved, w::Control>;
    struct PendingLease { w::LeaseRequest request; Time sent = 0; bool emitted = false; };
    struct ReleaseFence { LeaseToken token; uint64_t serial = 0; };
    struct LeaseStamp { uint64_t token = 0, serial = 0; Revision revision = 0; };
    struct PreviewInput { uint8_t from; Peer sender; uint32_t recipient; w::Message message; };
    struct ResourceStage {
        Revision revision = 0;
        Buffer bytes, broadcast_bytes;
        std::string full_hash, broadcast_hash;
        bool broadcast = false;
    };
    struct DeferredResource { Buffer bytes; };
    struct Deferred {
        w::Context context;
        std::variant<DeferredResource, Decision, w::Saved, w::Control> action;
        size_t cost = 0;
    };
    net::Session& session;
    Callbacks callbacks;
    Config config;
    Coordinator coordinator;
    State replica;
    View view;
    std::map<uint8_t, Channel> channels;
    std::array<uint32_t, 4> generations{};
    std::deque<Input> input;
    std::deque<std::pair<Local, size_t>> local;
    std::map<uint64_t, PendingLease> pending_leases;
    std::vector<ReleaseFence> release_fences;
    // Revisions are host-issued metadata already present in the message envelope.
    // Retain deletion fences while the exact ID is live or tombstoned, so a late
    // pre-delete Grant/Renew cannot regain authority after Undo restoration.
    std::map<ObjectId, Revision> lease_retirements;
    std::map<Scope, LeaseStamp> lease_grant_revisions;
    std::array<std::optional<PreviewInput>,4> preview_input;
    std::array<std::optional<w::Message>,4> preview_relay;
    std::array<std::optional<w::Presence>,4> presence_relay;
    std::array<Peer,4> preview_authors{};
    std::array<uint64_t,4> preview_seen{};
    std::optional<w::Message> preview_local;
    std::optional<w::Presence> presence_local;
    uint64_t preview_sequence = 0;
    Time preview_sent = 0;
    std::map<uint8_t, uint64_t> saved_acks;
    uint64_t local_saved_ack = 0;
    std::map<Revision, std::string> hashes;
    std::set<uint8_t> overflow;
    std::set<uint8_t> pending_offers;
    Buffer resources, baseline_cache;
    std::string resources_hash;
    Revision baseline_cache_revision = UINT64_MAX;
    std::optional<ResourceStage> staged;
    std::optional<State> pending_baseline;
    std::optional<w::Control> pending_control;
    std::optional<w::Control> returning_control;
    std::set<Peer> returned;
    bool local_returned = false;
    w::Context pending_baseline_context;
    std::deque<Deferred> deferred;
    std::deque<HeldRequest> held_requests;
    size_t held_request_bytes = 0;
    bool draining_deferred = false, draining_requests = false;
    size_t deferred_bytes = 0;
    size_t input_bytes = 0, output_bytes = 0, local_bytes = 0;
    uint64_t serial = 0, lease_serial = 0, operation_sequence = 0, round = 0;
    Time now = 0;
    bool hosting = false, ticking = false, coverage_pending = false;

    Impl(net::Session& s, Callbacks cb, Config cfg) : session(s), callbacks(std::move(cb)), config(cfg) {}
    const State& state() const { return hosting ? coordinator.state() : replica; }
    Peer host_author() const { auto it = view.members.find(0); return it == view.members.end() ? unknown_host : it->second.peer; }
    w::Context context() const { const auto& d = state().document; return {d.project, d.epoch, d.revision}; }
    bool active() const { return view.phase != Phase::Idle && view.phase != Phase::Disconnected; }
    bool authoring_ready() const { return !callbacks.authoring_ready || callbacks.authoring_ready(); }
    static w::PresencePhase presence_phase(Phase phase) {
        switch(phase) {
        case Phase::Ready:return w::PresencePhase::Ready;
        case Phase::Joining:case Phase::Syncing:return w::PresencePhase::Syncing;
        case Phase::Preparing:return w::PresencePhase::Preparing;
        case Phase::Playing:return w::PresencePhase::Playing;
        case Phase::Returning:return w::PresencePhase::Returning;
        case Phase::Frozen:return w::PresencePhase::Frozen;
        case Phase::Disconnected:case Phase::Idle:return w::PresencePhase::Disconnected;
        default:return w::PresencePhase::Offered;
        }
    }
    static Phase member_phase(w::PresencePhase phase) {
        switch(phase) {
        case w::PresencePhase::Ready:return Phase::Ready;
        case w::PresencePhase::Syncing:return Phase::Syncing;
        case w::PresencePhase::Preparing:return Phase::Preparing;
        case w::PresencePhase::Playing:return Phase::Playing;
        case w::PresencePhase::Returning:return Phase::Returning;
        case w::PresencePhase::Frozen:return Phase::Frozen;
        case w::PresencePhase::Disconnected:return Phase::Disconnected;
        default:return Phase::Offered;
        }
    }
    bool owns(Peer author,ObjectId object,const LeaseToken* token=nullptr) const {
        const auto& held=hosting?coordinator.leases():view.local_leases;
        return std::any_of(held.begin(),held.end(),[&](const Lease& l){
            return l.owner==author && l.expires>now && (author!=view.local || !quarantined(l)) && (!token || (l.scope==token->scope&&l.token==token->token)) &&
                (l.scope.kind==ScopeKind::Document || (l.scope.kind==ScopeKind::Placement&&l.scope.object==object));
        });
    }
    LeaseToken selection_lease(Peer author,ObjectId object) const {
        if(object==ObjectId{})return {};
        const auto& held=hosting?coordinator.leases():view.local_leases;
        LeaseToken placement;
        for(const auto& l:held)if(l.owner==author&&l.expires>now&&(author!=view.local || !quarantined(l))){
            if(l.scope.kind==ScopeKind::Document)return {l.scope,l.token};
            if(l.scope.kind==ScopeKind::Placement&&l.scope.object==object)placement={l.scope,l.token};
        }
        return placement;
    }
    void clear_preview_for(uint8_t slot,bool relay=true) {
        auto found=view.members.find(slot);
        if(found==view.members.end() || !found->second.preview)return;
        auto value=found->second.preview->value;
        found->second.preview.reset();
        if(hosting&&relay){value.active=false;value.object={};value.act={};value.pose={};value.lease={};preview_relay[slot]=w::Message{context(),value};}
    }
    void clear_previews() {
        for(auto& [slot,member]:view.members){(void)slot;member.preview.reset();}
        preview_local.reset();preview_input={};preview_relay={};
    }
    void presence(uint8_t from,w::Presence value) {
        auto found=view.members.find(value.author.slot);
        if(hosting) {
            if(found==view.members.end() || value.author!=found->second.peer || value.author.slot!=from)return;
            if((value.selected!=ObjectId{}&&!state().document.placements.contains(value.selected)) ||
               (value.act!=ObjectId{}&&!state().document.acts.contains(value.act)))return;
            value.ownership=0;value.preview_lease={};value.phase=presence_phase(found->second.phase);
            if(found->second.phase==Phase::Ready){
                value.preview_lease=selection_lease(value.author,value.selected);
                if(value.preview_lease.token)value.ownership|=w::kOwnSelection;
                for(const auto& lease:coordinator.leases())if(lease.owner==value.author&&lease.expires>now&&lease.scope.kind==ScopeKind::Document)value.ownership|=w::kOwnDocument;
                if(!value.ownership)value.editing=false;
            }else{value.editing=false;value.pending=false;}
        } else {
            if(from || (found!=view.members.end()&&found->second.peer.generation>value.author.generation))return;
            const auto roster=session.players();if(std::none_of(roster.begin(),roster.end(),[&](const net::PlayerInfo& p){return p.id==value.author.slot;}))return;
            auto& member=view.members[value.author.slot];member.peer=value.author;member.name=session.name_of(value.author.slot);member.phase=member_phase(value.phase);
            found=view.members.find(value.author.slot);
        }
        auto& member=found->second;
        if(member.presence.selected!=value.selected || member.presence.preview_lease!=value.preview_lease || !value.editing || value.pending)clear_preview_for(value.author.slot);
        member.presence=value;
        if(callbacks.presence)callbacks.presence(value.author,value);
        if(hosting)presence_relay[value.author.slot]=value;
    }
    void refresh_presence() {
        if(!hosting)return;
        for(auto& [slot,member]:view.members){
            if(!member.peer.generation)continue;
            auto value=member.presence;
            auto expected=value;expected.author=member.peer;expected.phase=presence_phase(member.phase);expected.ownership=0;expected.preview_lease={};
            if(member.phase==Phase::Ready){
                expected.preview_lease=selection_lease(member.peer,value.selected);
                if(expected.preview_lease.token)expected.ownership|=w::kOwnSelection;
                for(const auto& l:coordinator.leases())if(l.owner==member.peer&&l.expires>now&&l.scope.kind==ScopeKind::Document)expected.ownership|=w::kOwnDocument;
                if(!expected.ownership)expected.editing=false;
            }else{expected.editing=false;expected.pending=false;}
            if(expected!=value)presence(slot,expected);
        }
    }
    void preview(uint8_t from,const w::Message& message) {
        const auto& value=std::get<w::TransformPreview>(message.payload);
        if(view.phase!=Phase::Ready || !same_context(message.context,view.context) || message.context.revision!=context().revision)return;
        auto member=view.members.find(value.author.slot);
        if(member==view.members.end() || member->second.peer!=value.author || member->second.phase!=Phase::Ready || (hosting&&value.author.slot!=from))return;
        const auto slot=value.author.slot;
        if(preview_authors[slot]!=value.author){preview_authors[slot]=value.author;preview_seen[slot]=0;}
        if(value.serial<preview_seen[slot] || (value.active&&value.serial==preview_seen[slot]))return;
        if(value.active){
            const auto object=state().document.placements.find(value.object);
            if(object==state().document.placements.end() || object->second.act_id!=value.act ||
                !state().document.acts.contains(value.act) || member->second.presence.selected!=value.object ||
                !member->second.presence.editing || member->second.presence.pending ||
                !(member->second.presence.ownership&w::kOwnSelection) || member->second.presence.preview_lease!=value.lease ||
                (hosting&&!owns(value.author,value.object,&value.lease)))return;
            if(now>UINT64_MAX-kPreviewLifetime)return;
            member->second.preview=PreviewView{value,now+kPreviewLifetime};
        }else member->second.preview.reset();
        preview_seen[slot]=value.serial;
        if(hosting)preview_relay[slot]=message;
    }
    void service_previews() {
        if(presence_local){auto value=*presence_local;presence_local.reset();if(hosting)presence(0,value);else{std::string error;if(!queue(0,{context(),value},true,error))report(error);}}
        refresh_presence();
        for(auto& input:preview_input)if(input){auto value=std::move(*input);input.reset();
            if(value.sender.slot!=value.from || !value.recipient)continue;
            if(hosting){auto m=view.members.find(value.from);if(m==view.members.end()||m->second.peer!=value.sender||value.recipient!=view.local.generation)continue;}
            else if(value.from || value.sender!=host_author() || value.recipient!=view.local.generation)continue;
            preview(value.from,value.message);
        }
        if(hosting&&preview_local){auto value=std::move(*preview_local);preview_local.reset();preview(0,value);}
        for(auto& [slot,member]:view.members)if(member.preview &&
            (member.preview->expires<=now || member.phase!=Phase::Ready ||
             (hosting&&!owns(member.peer,member.preview->value.object,&member.preview->value.lease))))clear_preview_for(slot);
        if(hosting)for(auto& value:presence_relay)if(value){auto copy=*value;value.reset();broadcast({context(),copy},true);}
        if(now-preview_sent<kPreviewInterval)return;
        preview_sent=now;
        if(hosting){
            for(auto& message:preview_relay)if(message){
                net::Writer packet;std::string error;
                if(w::write(*message,packet,error))for(const auto& [id,member]:view.members)if(id&&member.phase==Phase::Ready)session.send_unreliable_to(id,config.message_type,address(id,packet));
                message.reset();
            }
        }else if(preview_local){net::Writer packet;std::string error;if(w::write(*preview_local,packet,error))session.send_unreliable_to(0,config.message_type,address(0,packet));preview_local.reset();}
    }
    bool quarantined(const Lease& lease) const {
        return std::any_of(release_fences.begin(), release_fences.end(), [&](const ReleaseFence& f) {
            return f.token.scope == lease.scope && f.token.token == lease.token;
        });
    }
    bool overlapping(const Scope& a, const Scope& b) const {
        if (a.kind == ScopeKind::Document || b.kind == ScopeKind::Document || a == b) return true;
        auto covers = [&](const Scope& x, const Scope& y) {
            if (x.kind == ScopeKind::Placement && y.kind == ScopeKind::Field) return x.object == y.object;
            if ((x.kind == ScopeKind::Class || x.kind == ScopeKind::Act) && (y.kind == ScopeKind::Placement || y.kind == ScopeKind::Field)) {
                auto found = state().document.placements.find(y.object);
                return found == state().document.placements.end() || (x.kind == ScopeKind::Class ? found->second.class_id : found->second.act_id) == x.object;
            }
            return false;
        };
        if (covers(a,b) || covers(b,a)) return true;
        if ((a.kind == ScopeKind::Class && b.kind == ScopeKind::Act) || (a.kind == ScopeKind::Act && b.kind == ScopeKind::Class))
            for (const auto& [id,p] : state().document.placements) { (void)id; if (p.class_id == (a.kind == ScopeKind::Class ? a.object : b.object) && p.act_id == (a.kind == ScopeKind::Act ? a.object : b.object)) return true; }
        return false;
    }
    bool release_pending(const std::vector<Scope>& scopes) const {
        for (const auto& [serial,pending] : pending_leases) { (void)serial;
            if (pending.request.action != w::LeaseAction::Release) continue;
            for (const auto& token : pending.request.tokens) for (const auto& scope : scopes) if (overlapping(token.scope,scope)) return true;
        }
        return false;
    }
    bool stale_grant(uint64_t serial, const Scope& scope) const {
        return std::any_of(release_fences.begin(),release_fences.end(),[&](const ReleaseFence& f) { return serial < f.serial && overlapping(f.token.scope,scope); });
    }
    void clear_lease_requests() {
        pending_leases.clear(); release_fences.clear(); lease_retirements.clear(); lease_grant_revisions.clear();
    }
    void retire_nonlive_leases() {
        const auto& document = state().document;
        // Core invalidates all non-live Placement/Field tokens after each new
        // commit. A fresh grant at this same revision was issued afterward and
        // remains valid even when its priority reply overtakes this commit.
        for (const auto& [object, revision] : document.tombstones) {
            (void)revision; lease_retirements[object] = document.revision;
        }
        // At most current placement+tombstone identities, within document limits.
        std::erase_if(lease_retirements, [&](const auto& item) {
            return !document.placements.contains(item.first) && !document.tombstones.contains(item.first);
        });
    }
    void service_leases() {
        // A host may still hold an atomically rejected Release's valid token.
        // Never republish that token implicitly; only a later explicit grant can
        // make it usable again. Other fences retire once all older replies drain.
        std::erase_if(release_fences,[&](const ReleaseFence& f) {
            if (pending_leases.contains(f.serial)) return false;
            for (const auto& [serial,pending] : pending_leases) if (serial < f.serial && pending.emitted && pending.request.action != w::LeaseAction::Release) {
                const auto& request = pending.request;
                if (std::any_of(request.scopes.begin(),request.scopes.end(),[&](const Scope& scope) { return overlapping(scope,f.token.scope); }) ||
                    std::any_of(request.tokens.begin(),request.tokens.end(),[&](const LeaseToken& token) { return overlapping(token.scope,f.token.scope); })) return false;
            }
            return !hosting || std::none_of(coordinator.leases().begin(),coordinator.leases().end(),[&](const Lease& l) { return l.owner == view.local && l.expires > now && l.scope == f.token.scope && l.token == f.token.token; });
        });
        if (view.phase != Phase::Ready) return;
        for (auto& [serial,pending] : pending_leases) {
            (void)serial;
            if (pending.emitted || release_pending(pending.request.scopes)) continue;
            net::Writer encoded; std::string error;
            if (!w::write({context(),pending.request},encoded,error)) { freeze(0,error); return; }
            bool okay = hosting ? queue_local(pending.request,encoded.size(),error) : queue(0,{context(),pending.request},true,error);
            if (!okay) continue; // Remains bounded by the pending-request limit.
            pending.sent = now; pending.emitted = true;
        }
    }
    void prune_local_leases() {
        std::erase_if(view.local_leases, [&](const Lease& lease) {
            if (quarantined(lease) || lease.expires <= now) return true;
            if (lease.scope.kind != ScopeKind::Placement && lease.scope.kind != ScopeKind::Field) return false;
            const auto& document = state().document;
            if (!document.placements.contains(lease.scope.object) && !document.tombstones.contains(lease.scope.object)) return true;
            auto retired = lease_retirements.find(lease.scope.object);
            if (retired == lease_retirements.end()) return false;
            auto issued = lease_grant_revisions.find(lease.scope);
            return issued == lease_grant_revisions.end() || issued->second.token != lease.token || issued->second.revision < retired->second;
        });
        // One metadata record per retained local scope, never per packet/retry.
        std::erase_if(lease_grant_revisions, [&](const auto& item) {
            return std::none_of(view.local_leases.begin(), view.local_leases.end(), [&](const Lease& lease) {
                return lease.scope == item.first && lease.token == item.second.token;
            });
        });
    }
    net::Writer address(uint8_t to, const net::Writer& packet) const {
        net::Writer result; result.u8(view.local.slot); result.u32(view.local.generation);
        auto target = view.members.find(to);
        result.u32(target == view.members.end() ? 0 : target->second.peer.generation);
        result.raw(packet.data.data(), packet.size()); return result;
    }
    void report(const std::string& why) { view.error = why; if (callbacks.error) try { callbacks.error(why); } catch (...) {} }
    bool queue_local(Local action, size_t cost, std::string& error) {
        if (local.size() >= config.outbound_packets || local_bytes + cost > config.outbound_bytes) { error = "The local editor transaction queue is full."; return false; }
        local_bytes += cost; local.emplace_back(std::move(action), cost); return true;
    }
    uint64_t next_serial() { if (serial == UINT64_MAX) throw std::runtime_error("Editor transfer nonce exhausted."); return ++serial; }
    size_t transfer_bytes() const {
        std::set<const std::vector<uint8_t>*> seen; size_t n = 0;
        auto add = [&](const Buffer& b) { if (b && seen.insert(b.get()).second) n += b->size(); };
        for (const auto& [id, ch] : channels) { (void)id; for (const auto& item : ch.ordered) if (auto* t = std::get_if<Transfer>(&item)) add(t->bytes); }
        if (staged) { add(staged->bytes); add(staged->broadcast_bytes); }
        return n;
    }
    void clear_channel(uint8_t id) {
        auto it = channels.find(id); if (it == channels.end()) return;
        for (const auto& packet : it->second.priority) output_bytes -= packet.size();
        for (const auto& item : it->second.ordered) if (auto* p = std::get_if<net::Writer>(&item)) output_bytes -= p->size();
        channels.erase(it);
    }
    bool queue_packet(uint8_t to, const w::Message& message, bool priority, std::string& error) {
        net::Writer packet; if (!w::write(message, packet, error)) return false;
        packet = address(to, packet);
        auto& ch = channels[to];
        if (ch.priority.size() + ch.ordered.size() >= config.outbound_packets || output_bytes + packet.size() > config.outbound_bytes) { error = "Editor control queue is full."; return false; }
        output_bytes += packet.size();
        if (priority) ch.priority.push_back(std::move(packet)); else ch.ordered.emplace_back(std::move(packet));
        return true;
    }
    bool queue_transfer(uint8_t to, const w::Context& ctx, w::TransferKind kind, Buffer bytes, std::string& error, const std::string& known_hash = {}) {
        if (!bytes || bytes->empty() || bytes->size() > (kind == w::TransferKind::Resources ? w::kResourceBytes : kind == w::TransferKind::Baseline ? w::kStateBytes : kind == w::TransferKind::Join ? w::kJoinBytes : w::kOperationBytes)) { error = "Invalid editor transfer size."; return false; }
        auto& ch = channels[to];
        bool retained = staged && (staged->bytes == bytes || staged->broadcast_bytes == bytes);
        for (const auto& [id, other] : channels) { (void)id; for (const auto& item : other.ordered) if (auto* t = std::get_if<Transfer>(&item); t && t->bytes == bytes) retained = true; }
        if (ch.priority.size() + ch.ordered.size() >= config.outbound_packets || transfer_bytes() + (retained ? 0 : bytes->size()) > config.transfer_bytes) { error = "Editor transfer queue is full."; return false; }
        ch.ordered.emplace_back(Transfer{ctx, kind, next_serial(), bytes, known_hash.empty() ? upd::sha256_hex(*bytes) : known_hash, 0, 0}); return true;
    }
    bool queue(uint8_t to, const w::Message& message, bool priority, std::string& error) {
        net::Writer packet;
        if (w::write(message, packet, error)) return queue_packet(to, message, priority, error);
        std::vector<uint8_t> bytes; w::TransferKind kind;
        if (auto* request = std::get_if<Request>(&message.payload)) { if (!w::encode_request(*request, bytes, error)) return false; kind = w::TransferKind::Request; }
        else if (auto* decision = std::get_if<Decision>(&message.payload)) { if (!w::encode_decision(*decision, bytes, error)) return false; kind = w::TransferKind::Decision; }
        else if (auto* join = std::get_if<w::Join>(&message.payload)) { if (!w::encode_join(*join, bytes, error)) return false; kind = w::TransferKind::Join; }
        else return false;
        return queue_transfer(to, message.context, kind, std::make_shared<const std::vector<uint8_t>>(std::move(bytes)), error);
    }
    void broadcast(const w::Message& message, bool priority = false) {
        std::vector<uint8_t> failed;
        for (const auto& [id, member] : view.members) if (id && (member.phase == Phase::Syncing || member.phase == Phase::Ready || member.phase == Phase::Preparing || member.phase == Phase::Playing || member.phase == Phase::Returning)) {
            std::string error; if (!queue(id, message, priority, error)) failed.push_back(id);
        }
        for (auto id : failed) freeze(id, "This editor participant exceeded its bounded catch-up queue.");
    }
    void freeze(uint8_t from, const std::string& why) {
        if (hosting && from != 0) {
            auto member = view.members.find(from);
            if (member != view.members.end()) { coordinator.disconnect(member->second.peer); member->second.phase = Phase::Frozen; member->second.ready = false;clear_preview_for(from); }
            std::erase_if(held_requests, [&](const HeldRequest& item) { if (item.author.slot != from) return false; held_request_bytes -= item.cost; return true; });
            clear_channel(from); std::string ignored; queue(from, {context(), w::Leave{3}}, true, ignored); report(why); progress_return();
        } else {
            const auto ctx = view.context;clear_previews();
            view.phase = Phase::Frozen; view.local_leases.clear(); channels.clear(); output_bytes = 0;
            pending_baseline.reset(); pending_control.reset(); returning_control.reset(); returned.clear(); local_returned = false; deferred.clear(); deferred_bytes = 0; held_requests.clear(); held_request_bytes = 0; staged.reset(); clear_lease_requests();
            if (hosting) {
                for (auto& [id, member] : view.members) { member.phase = Phase::Frozen; member.ready = false; if (id) { std::string ignored; queue(id, {ctx, w::Leave{3}}, true, ignored); } }
            } else if (view.local.generation && ctx.epoch) { std::string ignored; queue(0, {ctx, w::Leave{3}}, true, ignored); }
            report(why);
        }
    }
    void cache_hash() {
        std::string hash, error;
        if (!document_hash(state().document, hash, error)) { freeze(0, error); return; }
        hashes[state().document.revision] = std::move(hash);
        while (hashes.size() > config.catchup_decisions + 16) hashes.erase(hashes.begin());
        view.context = context();
    }
    void coverage() {
        cache_hash(); if (view.phase == Phase::Frozen) return;
        const auto ctx = context(); w::Coverage message{view.locally_durable, hashes.at(ctx.revision)};
        view.members[view.local.slot].coverage = message; view.members[view.local.slot].applied_revision = ctx.revision;
        if (!hosting) coverage_pending = true;
    }
    void offer(uint8_t id) {
        if (!hosting || !id || id >= 4 || !active() || view.phase == Phase::Frozen) return;
        auto old = view.members.find(id); if (old != view.members.end()) coordinator.disconnect(old->second.peer);
        clear_channel(id);
        saved_acks.erase(id);
        if (generations[id] == UINT32_MAX) { report("Editor peer capability exhausted."); return; }
        Peer capability{id, ++generations[id]}; if (!coordinator.connect(capability)) { report("Cannot assign an editor peer capability."); return; }
        Member member; member.peer = capability; member.name = session.name_of(id); view.members[id] = member;
        std::string error; if (!queue(id, {context(), w::Offer{w::kSchema, view.name, capability, view.local}}, true, error)) freeze(id, error);
    }
    void synchronize(uint8_t from,const w::Join& join) {
        auto it = view.members.find(from); if (!hosting || it == view.members.end()) return;
        auto& member = it->second;
        if (view.phase != Phase::Ready || !resources) { freeze(from, "The host is not available for an editor baseline."); return; }
        auto completed=std::move(channels[from].completed);
        clear_channel(from);channels[from].completed=std::move(completed);
        const auto ctx = context(); std::string error;
        if (!baseline_cache || baseline_cache_revision != ctx.revision) {
            std::vector<uint8_t> bytes; if (!w::encode_state(state(), bytes, error)) { freeze(from, error); return; }
            baseline_cache = std::make_shared<const std::vector<uint8_t>>(std::move(bytes)); baseline_cache_revision = ctx.revision;
        }
        Buffer for_peer=resources;std::string peer_hash=resources_hash;
        if(callbacks.resources_for_join && (!join.base_hash.empty() || !join.records.empty())){
            std::vector<uint8_t> encoded;
            bool okay=false;try{okay=callbacks.resources_for_join(*resources,join,encoded,error);}catch(...){error="Verified join cache negotiation threw.";}
            if(!okay || encoded.empty() || encoded.size()>w::kResourceBytes){freeze(from,error.empty()?"Verified join cache negotiation failed.":error);return;}
            for_peer=std::make_shared<const std::vector<uint8_t>>(std::move(encoded));peer_hash=upd::sha256_hex(*for_peer);
        }
        if (!queue_transfer(from, ctx, w::TransferKind::Resources, for_peer, error, peer_hash) || !queue_transfer(from, ctx, w::TransferKind::Baseline, baseline_cache, error)) { freeze(from, error); return; }
        channels[from].baseline = ctx.revision; channels[from].baseline_save_serial = view.saved ? view.saved->serial : 0;
        member.phase = Phase::Syncing; member.ready = false;
    }
    bool invoke_control(const w::Control& control, std::string& error) {
        try { if (!callbacks.control || !callbacks.control(control, error)) { if (error.empty()) error = "Native editor playtest/return is unavailable."; return false; } return true; }
        catch (...) { error = "Native editor control callback threw."; return false; }
    }
    void revoke_leases() {
        if (hosting) {
            auto held = coordinator.leases();
            for (const auto& [id, member] : view.members) {
                (void)id; std::vector<LeaseToken> tokens;
                for (const auto& lease : held) if (lease.owner == member.peer) tokens.push_back({lease.scope, lease.token});
                if (!tokens.empty()) coordinator.release(member.peer, tokens);
            }
        }
        view.local_leases.clear(); clear_lease_requests();clear_previews();
    }
    uint8_t ready_mask() const { uint8_t mask = 0; for (const auto& [id, member] : view.members) if (id < 4 && member.ready) mask |= uint8_t(1u << id); return mask; }
    void set_ready_mask(uint8_t mask) { for (auto& [id, member] : view.members) member.ready = id < 4 && ((mask >> id) & 1); }
    void progress_ready() {
        if (pending_control) return;
        w::Control progress{w::ControlKind::HostPreparePlaytest, view.playtest_round, view.playtest_revision, false, ready_mask(), view.playtest_participants};
        broadcast({context(), progress});
        if (std::any_of(view.members.begin(), view.members.end(), [](const auto& pair) { return pair.second.phase != Phase::Preparing || !pair.second.ready; })) return;
        auto begin = progress; begin.kind = w::ControlKind::HostBeginPlaytest; std::string error;
        if (!invoke_control(begin, error)) { freeze(0, error); return; }
        view.phase = Phase::Playing; for (auto& [id, member] : view.members) { (void)id; member.phase = Phase::Playing; }
        broadcast({context(), begin});
    }

    void host_request(uint8_t from, const Request& request);
    void lease_request(uint8_t from, const w::LeaseRequest& request);
    void lease_reply(const w::LeaseReply& reply, Revision issued_revision);
    void control(uint8_t from, const w::Control& control);
    void progress_return() {
        if (!hosting || view.phase != Phase::Returning || !local_returned) return;
        for (const auto& [id, member] : view.members) {
            (void)id;
            if (member.phase == Phase::Returning && !returned.contains(member.peer)) return;
        }
        w::Control resumed{w::ControlKind::HostEditorResumed, view.playtest_round, view.playtest_revision, false, 0};
        view.phase = Phase::Ready;
        for (auto& [id, member] : view.members) { (void)id; if (member.phase == Phase::Returning) member.phase = Phase::Ready; member.ready = false; }
        returning_control.reset(); returned.clear(); local_returned = false;
        broadcast({context(), resumed});
    }
    void service_control() {
        if (!pending_control || view.phase != Phase::Returning || local_returned) return;
        if (callbacks.control_ready && !callbacks.control_ready(*pending_control)) return;
        auto command = *pending_control; std::string error;
        if (!invoke_control(command, error)) { freeze(0, error); return; }
        pending_control.reset(); local_returned = true;
        if (hosting) { returned.insert(view.local); progress_return(); }
        else if (!queue(0, {context(), w::Control{w::ControlKind::GuestEditorReturned, view.playtest_round, view.playtest_revision, false, 0}}, false, error)) freeze(0, error);
    }
    void begin_return(const w::Control& command) {
        if (view.phase == Phase::Returning) return;
        revoke_leases(); view.phase = Phase::Returning; pending_control = command; returning_control = command;
        returned.clear(); local_returned = false;
        for (auto& [id, member] : view.members) { (void)id; if (member.phase == Phase::Preparing || member.phase == Phase::Playing) member.phase = Phase::Returning; member.ready = false; }
        if (hosting) broadcast({context(), command});
        service_control();
    }
    void message(uint8_t from, w::Message);
    void transfer(uint8_t from, const w::Message&);
    void service_baseline();
    void service_deferred();
    void service_requests() {
        if (!hosting || held_requests.empty() || view.phase != Phase::Ready || !authoring_ready()) return;
        draining_requests = true;
        struct Reset { bool& flag; ~Reset() { flag = false; } } reset{draining_requests};
        size_t count = config.catchup_decisions;
        while (count-- && !held_requests.empty() && view.phase == Phase::Ready && authoring_ready()) {
            auto item = std::move(held_requests.front()); held_requests.pop_front(); held_request_bytes -= item.cost;
            auto member = view.members.find(item.author.slot);
            if (member != view.members.end() && member->second.peer == item.author) host_request(item.author.slot, item.request);
        }
    }
    void pump();
};

void Controller::Impl::host_request(uint8_t from, const Request& request)
{
    if (!hosting || !view.members.contains(from) || request.operation.author != view.members.at(from).peer) return;
    if (!draining_requests && view.phase == Phase::Ready && (!held_requests.empty() || !authoring_ready())) {
        std::vector<uint8_t> bytes; std::string error;
        if (!w::encode_request(request, bytes, error)) { freeze(from, error); return; }
        if (held_requests.size() >= config.catchup_decisions || held_request_bytes + bytes.size() > config.outbound_bytes) { freeze(from, "Native gesture request waiting exceeded a bounded queue."); return; }
        held_requests.push_back({view.members.at(from).peer, request, bytes.size()}); held_request_bytes += bytes.size(); return;
    }
    auto hooks = callbacks.host_hooks;
    const auto validate = hooks.validate;
    hooks.validate = [&](const Document& before, const Document& after, const Change& change, std::string& error) {
        if (view.phase != Phase::Ready || view.members.at(from).phase != Phase::Ready) { error = "Authoring is held or this participant needs a baseline."; return false; }
        return !validate || validate(before, after, change, error);
    };
    const auto persist = hooks.persist;
    hooks.persist = [&](const State& candidate, const Change& change, std::string& error) {
        if (persist && !persist(candidate, change, error)) return false;
        if (!change.patch.objects.empty() && (!staged || staged->revision != change.revision || !staged->broadcast))
            throw std::runtime_error("A structural commit has no opaque-preserving target resource payload.");
        return true;
    };
    const auto previous_revision = context().revision;
    auto decision = coordinator.submit(request, now, hooks);
    if (decision.accepted() && decision.revision <= previous_revision) {
        if (from) {
            auto ctx = context(); ctx.revision = decision.revision; std::string error;
            if (!queue(from, {ctx, decision}, false, error)) freeze(from, error);
        }
        return;
    }
    if (decision.accepted()) {
        if (staged && staged->revision == decision.revision) {
            resources = staged->bytes;
            resources_hash = std::move(staged->full_hash);
            if (staged->broadcast) {
                std::vector<uint8_t> failed;
                for (const auto& [id, member] : view.members) if (id && (member.phase == Phase::Ready || member.phase == Phase::Syncing)) {
                    std::string error; if (!queue_transfer(id, context(), w::TransferKind::Resources, staged->broadcast_bytes, error, staged->broadcast_hash)) failed.push_back(id);
                }
                for (auto id : failed) freeze(id, "Structural resource catch-up exceeded the queue bound.");
            }
        }
        staged.reset(); baseline_cache.reset();
        if (callbacks.host_hooks.persist) view.locally_durable = decision.revision;
        clear_previews();coverage(); retire_nonlive_leases(); broadcast({context(), decision});
    } else {
        staged.reset();
        if (coordinator.frozen()) freeze(0, decision.reason);
        if (from) { std::string error; if (!queue(from, {context(), decision}, false, error)) freeze(from, error); }
    }
    prune_local_leases();
    if (!from && callbacks.decision) callbacks.decision(decision);
    view.context = context();
}

void Controller::Impl::lease_request(uint8_t from, const w::LeaseRequest& request)
{
    if (!hosting || !view.members.contains(from)) return;
    auto& ch = channels[from];
    auto remembered = ch.lease_decisions.find(request.serial);
    const bool released = request.action != w::LeaseAction::Release && std::any_of(ch.released_lease_scopes.begin(),ch.released_lease_scopes.end(),[&](const auto& fence) {
        return request.serial < fence.second && (std::any_of(request.scopes.begin(),request.scopes.end(),[&](const Scope& scope) { return overlapping(scope,fence.first); }) ||
            std::any_of(request.tokens.begin(),request.tokens.end(),[&](const LeaseToken& token) { return overlapping(token.scope,fence.first); }));
    });
    w::LeaseReply reply;
    Revision issued_revision = context().revision;
    if (remembered != ch.lease_decisions.end()) {
        reply = remembered->second.first; issued_revision = remembered->second.second;
    }
    else if (request.serial <= ch.retired_lease_serial || released) reply = {request.serial, {Code::ExpiredOperation, "This lease request predates a release or its original decision expired; it will not execute again.", {}}};
    else {
        reply.serial = request.serial;
        auto owner = view.members.at(from).peer;
        if (view.phase != Phase::Ready || view.members.at(from).phase != Phase::Ready) reply.result = {Code::Busy, "Authoring is held or this participant needs a baseline.", {}};
        else if (request.action == w::LeaseAction::Acquire) reply.result = coordinator.acquire(owner, request.scopes, now, request.ttl);
        else if (request.action == w::LeaseAction::Renew) reply.result = coordinator.renew(owner, request.tokens, now, request.ttl);
        else if (request.action == w::LeaseAction::Release) {
            // A delayed, previously unseen Acquire/Renew must not recreate a
            // scope after this release, even if UDP reordered its first delivery.
            for (const auto& token : request.tokens) ch.released_lease_scopes[token.scope] = std::max(ch.released_lease_scopes[token.scope],request.serial);
            reply.result = {coordinator.release(owner, request.tokens) ? Code::Accepted : Code::LeaseExpired, {}, {}};
        }
        else reply.result = {Code::Invalid, "Unknown lease action.", {}};
        ch.lease_decisions[request.serial] = {reply, issued_revision};
        while (ch.lease_decisions.size() > 64) { ch.retired_lease_serial = std::max(ch.retired_lease_serial, ch.lease_decisions.begin()->first); ch.lease_decisions.erase(ch.lease_decisions.begin()); }
        std::erase_if(ch.released_lease_scopes,[&](const auto& fence) { return fence.second <= ch.retired_lease_serial; });
    }
    if (!from) lease_reply(reply, issued_revision);
    else {
        // A cached grant retains its actual issue revision. Restamping a revoked
        // old token with the current revision would defeat deletion fences.
        auto scope = context(); scope.revision = issued_revision;
        std::string error; if (!queue(from, {scope, reply}, true, error)) freeze(from, error);
    }
}

void Controller::Impl::lease_reply(const w::LeaseReply& reply, Revision issued_revision)
{
    auto found = pending_leases.find(reply.serial); if (found == pending_leases.end() || !found->second.emitted) return;
    const auto pending = found->second; pending_leases.erase(found);
    // An authenticated reply, including LeaseExpired, proves the host finished
    // this Release. Deferred overlapping acquisition may now be emitted.
    if (pending.request.action == w::LeaseAction::Release) {
        if (!reply.result.accepted()) report(reply.result.reason.empty() ? "The editing lease was already released or expired." : reply.result.reason);
        return;
    }
    if (!reply.result.accepted()) { report(reply.result.reason.empty() ? "The editing lease was rejected." : reply.result.reason); return; }
    for (auto lease : reply.result.leases) {
        if (lease.owner != view.local) { freeze(0, "A lease reply assigned another player's capability."); return; }
        if (stale_grant(reply.serial,lease.scope)) continue;
        if (lease.scope.kind == ScopeKind::Placement || lease.scope.kind == ScopeKind::Field) {
            auto retired = lease_retirements.find(lease.scope.object);
            if (retired != lease_retirements.end() && issued_revision < retired->second) continue;
        }
        auto previous = lease_grant_revisions.find(lease.scope);
        if (previous != lease_grant_revisions.end() && reply.serial < previous->second.serial) continue;
        if (!hosting) {
            if (pending.sent > UINT64_MAX - pending.request.ttl) { freeze(0, "Local lease deadline overflow."); return; }
            lease.expires = pending.sent + pending.request.ttl;
        }
        std::erase_if(release_fences,[&](const ReleaseFence& f) { return f.serial < reply.serial && f.token.scope == lease.scope && f.token.token == lease.token; });
        std::erase_if(view.local_leases, [&](const Lease& old) { return old.scope == lease.scope; });
        if (lease.expires > now) {
            auto& issued = lease_grant_revisions[lease.scope];
            issued.revision = issued.token == lease.token ? std::max(issued.revision, issued_revision) : issued_revision;
            issued.token = lease.token; issued.serial = reply.serial;
            view.local_leases.push_back(lease);
        }
    }
}

void Controller::Impl::control(uint8_t from, const w::Control& command)
{
    std::string error;
    if (hosting) {
        if (from) {
            if (command.kind == w::ControlKind::GuestEditorReturned) {
                if (view.phase == Phase::Returning && command.serial == view.playtest_round && command.revision == view.playtest_revision && view.members.contains(from) && view.members.at(from).phase == Phase::Returning) {
                    returned.insert(view.members.at(from).peer); progress_return();
                }
                return;
            }
            if (pending_control || command.kind != w::ControlKind::GuestReadyPlaytest || view.phase != Phase::Preparing || command.serial != view.playtest_round || command.revision != view.playtest_revision || !view.members.contains(from)) return;
            auto& member = view.members.at(from); if (member.phase != Phase::Preparing) return;
            member.ready = command.ready; progress_ready(); return;
        }
        if (command.kind == w::ControlKind::HostPreparePlaytest) {
            if (view.phase != Phase::Ready || !held_requests.empty() || !authoring_ready() || command.serial != round + 1 || command.revision != context().revision ||
                std::any_of(view.members.begin(), view.members.end(), [&](const auto& pair) { return pair.second.phase != Phase::Ready || pair.second.applied_revision != context().revision; })) { report("Every editor participant must catch up before group playtest."); return; }
            revoke_leases(); view.phase = Phase::Preparing; round = command.serial;
            view.playtest_round = round; view.playtest_revision = command.revision;
            view.playtest_participants = 0;
            for (const auto& [id, member] : view.members) { (void)member; if (id < 4) view.playtest_participants |= uint8_t(1u << id); }
            for (auto& [id, member] : view.members) { (void)id; member.ready = false; member.phase = Phase::Preparing; }
            auto announced = command; announced.participant_mask = view.playtest_participants;
            if (!invoke_control(announced, error)) { freeze(0, error); return; }
            broadcast({context(), announced});
        } else if (command.kind == w::ControlKind::GuestReadyPlaytest) {
            if (!pending_control && view.phase == Phase::Preparing && command.serial == view.playtest_round && command.revision == view.playtest_revision) { view.members[0].ready = command.ready; progress_ready(); }
        } else if (command.kind == w::ControlKind::HostCancelPlaytest || command.kind == w::ControlKind::HostReturnEditor) {
            if ((view.phase != Phase::Preparing && view.phase != Phase::Playing) || command.serial != view.playtest_round || command.revision != view.playtest_revision) return;
            begin_return(command);
        }
        return;
    }
    if (from || command.serial < view.playtest_round) return;
    if (command.kind == w::ControlKind::HostPreparePlaytest) {
        if (command.serial == view.playtest_round && view.phase == Phase::Preparing && command.revision == view.playtest_revision) {
            if (command.participant_mask != view.playtest_participants) { freeze(0, "The host changed the immutable playtest roster."); return; }
            set_ready_mask(command.ready_mask); return;
        }
        if (view.phase != Phase::Ready || command.serial <= view.playtest_round || command.revision != context().revision) return;
        if (!(command.participant_mask & uint8_t(1u << view.local.slot))) { freeze(0, "This playtest round omitted its participating recipient."); return; }
        revoke_leases(); view.phase = Phase::Preparing; view.playtest_round = command.serial; view.playtest_revision = command.revision;
        view.playtest_participants = command.participant_mask;
        for (uint8_t id = 0; id < 4; ++id) if ((command.participant_mask >> id) & 1) {
            if (!view.members.contains(id)) { Member m; m.peer = {id, 0}; m.name = session.name_of(id); view.members[id] = m; }
            view.members[id].phase = Phase::Preparing;
        }
        set_ready_mask(command.ready_mask);
        if (!invoke_control(command, error)) freeze(0, error);
    } else if (command.kind == w::ControlKind::HostBeginPlaytest) {
        if (view.phase != Phase::Preparing || command.serial != view.playtest_round || command.revision != view.playtest_revision) return;
        if (command.participant_mask != view.playtest_participants || command.ready_mask != view.playtest_participants) { freeze(0, "The host began a group playtest without its complete immutable roster confirmations."); return; }
        if (!invoke_control(command, error)) { freeze(0, error); return; }
        set_ready_mask(command.ready_mask); view.phase = Phase::Playing;
        for (auto& [id, member] : view.members) { (void)id; member.phase = Phase::Playing; }
    } else if (command.kind == w::ControlKind::HostCancelPlaytest || command.kind == w::ControlKind::HostReturnEditor) {
        if ((view.phase != Phase::Preparing && view.phase != Phase::Playing) || command.serial != view.playtest_round || command.revision != view.playtest_revision) return;
        begin_return(command);
    } else if (command.kind == w::ControlKind::HostEditorResumed) {
        if (view.phase != Phase::Returning || command.serial != view.playtest_round || command.revision != view.playtest_revision) return;
        if (!local_returned) { freeze(0, "The host resumed authoring before this participant completed native return."); return; }
        view.phase = Phase::Ready; returning_control.reset(); returned.clear(); local_returned = false;
        for (auto& [id, member] : view.members) { (void)id; member.phase = Phase::Ready; member.ready = false; }
    }
}

void Controller::Impl::transfer(uint8_t from, const w::Message& message)
{
    auto& ch = channels[from]; std::string error;
    if (auto* begin = std::get_if<w::TransferBegin>(&message.payload)) {
        if (auto done = ch.completed.find(begin->serial); done != ch.completed.end()) {
            if (done->second.first != message.context || !same_transfer(done->second.second, *begin)) freeze(from, "A completed editor transfer nonce was reused.");
            return;
        }
        if(hosting && begin->kind==w::TransferKind::Join && (view.members.at(from).phase!=Phase::Offered || message.context.revision>context().revision)){freeze(from,"Join inventory is outside its offered scope.");return;}
        if (!hosting && begin->kind == w::TransferKind::Resources && view.phase != Phase::Joining && view.phase != Phase::Syncing && view.phase != Phase::Ready && view.phase != Phase::Preparing && view.phase != Phase::Playing && view.phase != Phase::Returning) return;
        if (!hosting && begin->kind == w::TransferKind::Baseline && (view.phase != Phase::Syncing || !ch.resources)) { freeze(0, "A baseline arrived before verified resources."); return; }
        if (!hosting && begin->kind == w::TransferKind::Baseline && pending_baseline) { freeze(0, "A pending native baseline cannot be replaced without a new offer."); return; }
        if (!ch.receiver.begin(message.context, *begin, hosting ? view.members.at(from).peer : host_author(), !hosting, error)) freeze(from, error);
        return;
    }
    uint64_t id = std::holds_alternative<w::TransferChunk>(message.payload) ? std::get<w::TransferChunk>(message.payload).serial : std::get<w::TransferEnd>(message.payload).serial;
    if (auto done = ch.completed.find(id); done != ch.completed.end()) { if (done->second.first != message.context) freeze(from, "A stale editor transfer changed context."); return; }
    if (auto* chunk = std::get_if<w::TransferChunk>(&message.payload)) { if (!ch.receiver.chunk(message.context, *chunk, error)) freeze(from, error); return; }
    const auto kind = ch.receiver.kind();
    std::vector<uint8_t> bytes;
    if (!ch.receiver.finish(message.context, std::get<w::TransferEnd>(message.payload), bytes, error)) { freeze(from, error); return; }
    // Remember the seal before callbacks: retries must not repeat native work.
    w::TransferBegin seal{kind, id, uint32_t(bytes.size()), upd::sha256_hex(bytes)};
    ch.completed[id] = {message.context, seal}; while (ch.completed.size() > 64) ch.completed.erase(ch.completed.begin());
    if (kind == w::TransferKind::Resources) {
        if (hosting) { freeze(from, "A guest attempted to install host resources."); return; }
        if (pending_baseline || !deferred.empty()) {
            if (deferred.size() >= config.catchup_decisions || deferred_bytes + bytes.size() > config.transfer_bytes) { freeze(0, "Native baseline catch-up exceeded its bounded resource queue."); return; }
            const size_t cost = bytes.size();
            deferred.push_back({message.context, DeferredResource{std::make_shared<const std::vector<uint8_t>>(std::move(bytes))}, cost}); deferred_bytes += cost; return;
        }
        if (message.context.revision < replica.document.revision ||
            (view.phase == Phase::Ready && message.context.revision > replica.document.revision + 1) ||
            ((view.phase == Phase::Preparing || view.phase == Phase::Playing || view.phase == Phase::Returning) && message.context.revision != replica.document.revision)) { freeze(0, "Resource staging skipped a committed revision."); return; }
        try { if (!callbacks.apply_resources || !callbacks.apply_resources(bytes, message.context, error)) { freeze(0, error.empty() ? "Native resource import is unavailable or failed." : error); return; } }
        catch (...) { freeze(0, "Native resource import threw."); return; }
        ch.resources = true; if (view.phase == Phase::Joining) view.phase = Phase::Syncing;
    } else if (kind == w::TransferKind::Baseline) {
        State baseline; if (!w::decode_state(bytes, baseline, error)) { freeze(0, error); return; }
        const auto& d = baseline.document;
        if (d.project != message.context.project || d.epoch != message.context.epoch || d.revision != message.context.revision) { freeze(0, "The sealed native baseline disagreed with its transfer scope."); return; }
        pending_baseline = std::move(baseline); pending_baseline_context = message.context;
    } else if(kind==w::TransferKind::Join){
        w::Join join;if(!w::decode_join(bytes,join,error)){freeze(from,error);return;}
        this->message(from,{message.context,std::move(join)});
    } else if (kind == w::TransferKind::Request) {
        Request request; if (!w::decode_request(bytes, request, error)) { freeze(from, error); return; }
        if (request.project != message.context.project || request.epoch != message.context.epoch || request.base_revision != message.context.revision) { freeze(from, "A transferred request disagreed with its scope."); return; }
        host_request(from, request);
    } else {
        Decision decision; if (!w::decode_decision(bytes, decision, error)) { freeze(0, error); return; }
        if (decision.revision != message.context.revision) { freeze(0, "A transferred commit disagreed with its revision."); return; }
        this->message(from, {message.context, decision});
    }
}

void Controller::Impl::service_baseline()
{
    if (!pending_baseline || view.phase != Phase::Syncing) return;
    if (!same_context(pending_baseline_context, view.context)) { pending_baseline.reset(); deferred.clear(); deferred_bytes = 0; return; }
    std::string error;
    try {
        if (callbacks.baseline_ready && !callbacks.baseline_ready(*pending_baseline)) return;
        if (!callbacks.apply_baseline || !callbacks.apply_baseline(*pending_baseline, error)) { freeze(0, error.empty() ? "Native baseline application failed." : error); return; }
    } catch (...) { freeze(0, "Native baseline readiness/application threw."); return; }
    replica = std::move(*pending_baseline); pending_baseline.reset(); view.phase = Phase::Ready;
    for (auto& [id, member] : view.members) { (void)id; member.phase = Phase::Ready; }
    if (callbacks.persist_guest) view.locally_durable = replica.document.revision;
    coverage();
}

void Controller::Impl::service_deferred()
{
    if (hosting || pending_baseline || deferred.empty()) return;
    std::string error; draining_deferred = true;
    struct Reset { bool& flag; ~Reset() { flag = false; } } reset{draining_deferred};
    while (!deferred.empty() && view.phase != Phase::Frozen) {
        const auto& next = deferred.front();
        if (const auto* decision = std::get_if<Decision>(&next.action); decision && decision->accepted() &&
            !replica.decisions.contains(decision->change->operation) && !authoring_ready()) return;
        if (std::holds_alternative<DeferredResource>(next.action) && view.phase == Phase::Ready && !authoring_ready()) return;
        if (std::holds_alternative<w::Saved>(next.action) && !authoring_ready()) return;
        if (const auto* command = std::get_if<w::Control>(&next.action); command && command->kind == w::ControlKind::HostPreparePlaytest && view.phase == Phase::Ready && !authoring_ready()) return;
        auto item = std::move(deferred.front()); deferred.pop_front(); deferred_bytes -= item.cost;
        if (auto* resource = std::get_if<DeferredResource>(&item.action)) {
            try {
                if (item.context.revision < replica.document.revision || item.context.revision > replica.document.revision + 1 ||
                    !callbacks.apply_resources || !callbacks.apply_resources(*resource->bytes, item.context, error)) { freeze(0, error.empty() ? "Deferred resource staging failed." : error); return; }
            } catch (...) { freeze(0, "Deferred native resource staging threw."); return; }
        } else std::visit([&](const auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (!std::is_same_v<T, DeferredResource>) this->message(0, {item.context, payload});
        }, item.action);
    }
}

void Controller::Impl::message(uint8_t from, w::Message message)
{
    std::string error;
    if (auto* offer_message = std::get_if<w::Offer>(&message.payload)) {
        if (hosting || from || offer_message->assigned.slot != session.my_id()) return;
        if (same_context(message.context, view.context) && offer_message->assigned.generation <= view.local.generation) return;
        channels.clear(); output_bytes = 0; clear_lease_requests(); staged.reset(); pending_baseline.reset(); pending_control.reset(); returning_control.reset(); returned.clear(); local_returned = false; deferred.clear(); deferred_bytes = 0; held_requests.clear(); held_request_bytes = 0;
        clear_previews();presence_local.reset();presence_relay={};preview_authors={};preview_seen={};preview_sequence=0;
        view = {}; view.phase = Phase::Offered; view.local = offer_message->assigned;
        view.name = offer_message->name; view.context = message.context;
        Member own; own.peer = view.local; own.name = session.name_of(view.local.slot); view.members[view.local.slot] = own;
        Member host; host.peer = offer_message->host; host.name = session.name_of(0); view.members[0] = host;
        return;
    }
    if (!active() || !same_context(message.context, view.context)) return;
    if (view.phase == Phase::Frozen && !std::holds_alternative<w::Leave>(message.payload)) return;
    if (hosting && (!view.members.contains(from) || view.members.at(from).phase == Phase::Frozen)) return;
    if (auto* join = std::get_if<w::Join>(&message.payload)) {
        if (hosting && from && view.members.at(from).phase == Phase::Offered && message.context.revision<=context().revision) synchronize(from,*join); return;
    }
    bool wait_for_authoring = false;
    if (!hosting && !draining_deferred && !pending_baseline && deferred.empty()) {
        if (auto* decision = std::get_if<Decision>(&message.payload); decision && decision->accepted() && !replica.decisions.contains(decision->change->operation)) wait_for_authoring = !authoring_ready();
        if (auto* command = std::get_if<w::Control>(&message.payload); command && command->kind == w::ControlKind::HostPreparePlaytest && view.phase == Phase::Ready) wait_for_authoring = !authoring_ready();
        if (std::holds_alternative<w::Saved>(message.payload)) wait_for_authoring = !authoring_ready();
    }
    if (!hosting && !draining_deferred && (pending_baseline || !deferred.empty() || wait_for_authoring) && (std::holds_alternative<Decision>(message.payload) || std::holds_alternative<w::Saved>(message.payload) || std::holds_alternative<w::Control>(message.payload))) {
        size_t cost = 128; if (auto* d = std::get_if<Decision>(&message.payload)) { std::vector<uint8_t> encoded; if (!w::encode_decision(*d, encoded, error)) { freeze(0, error); return; } cost = encoded.size(); }
        if (deferred.size() >= config.catchup_decisions || deferred_bytes + cost > config.transfer_bytes) { freeze(0, "Native baseline catch-up exceeded its bounded command queue."); return; }
        std::visit([&](const auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, Decision> || std::is_same_v<T, w::Saved> || std::is_same_v<T, w::Control>) deferred.push_back({message.context, payload, cost});
        }, message.payload);
        deferred_bytes += cost; return;
    }
    if (std::holds_alternative<w::TransferBegin>(message.payload) || std::holds_alternative<w::TransferChunk>(message.payload) || std::holds_alternative<w::TransferEnd>(message.payload)) {
        if (!hosting && (view.phase == Phase::Offered || view.phase == Phase::Idle)) return;
        transfer(from, message); return;
    }
    if (auto* request = std::get_if<Request>(&message.payload)) { host_request(from, *request); return; }
    if (auto* decision = std::get_if<Decision>(&message.payload)) {
        if (hosting || (view.phase != Phase::Ready && view.phase != Phase::Preparing && view.phase != Phase::Playing)) return;
        const bool duplicate = decision->accepted() && replica.decisions.contains(decision->change->operation);
        if (decision->accepted()) {
            Hooks hooks; hooks.apply = callbacks.apply_committed; hooks.rollback = callbacks.rollback_committed;
            hooks.persist = callbacks.persist_guest;
            if (!hooks.apply) { freeze(0, "Native committed-edit application is unavailable."); return; }
            if (!editor::apply_committed(replica, *decision, error, hooks)) { freeze(0, error); return; }
            if (callbacks.persist_guest) view.locally_durable = replica.document.revision;
            clear_previews();coverage();
            if (!duplicate) retire_nonlive_leases();
        }
        prune_local_leases();
        if (!duplicate && callbacks.decision) callbacks.decision(*decision);
        return;
    }
    if (auto* lease = std::get_if<w::LeaseRequest>(&message.payload)) { lease_request(from, *lease); return; }
    if (auto* reply = std::get_if<w::LeaseReply>(&message.payload)) { if (!hosting) lease_reply(*reply, message.context.revision); return; }
    if (auto* value = std::get_if<w::Presence>(&message.payload)) { presence(from,*value); return; }
    if (auto* cover = std::get_if<w::Coverage>(&message.payload)) {
        if (!hosting || !from || message.context.revision > context().revision) return;
        auto hash = hashes.find(message.context.revision);
        if (hash == hashes.end() || hash->second != cover->state_hash) { freeze(from, "The participant's canonical document hash diverged; a new baseline is required."); return; }
        auto& member = view.members.at(from);
        if (message.context.revision >= member.applied_revision) {
            member.applied_revision = message.context.revision; member.coverage = *cover;
            if (member.phase == Phase::Syncing && message.context.revision >= channels[from].baseline) {
                if (view.saved && channels[from].baseline_save_serial == view.saved->serial) saved_acks[from] = view.saved->serial;
                member.phase = Phase::Ready;
            }
        }
        if (callbacks.coverage) callbacks.coverage(member.peer, *cover);
        return;
    }
    if (auto* saved = std::get_if<w::Saved>(&message.payload)) {
        if (hosting || saved->saved_revision > replica.document.revision) return;
        if (view.saved && saved->serial <= view.saved->serial) {
            if (saved->serial == view.saved->serial && *saved != *view.saved) freeze(0, "A native save nonce changed its identity.");
            return;
        }
        local_saved_ack = 0;
        view.saved = *saved; if (callbacks.saved) callbacks.saved(*saved); return;
    }
    if (auto* applied = std::get_if<w::SavedApplied>(&message.payload)) {
        if (hosting && from && view.saved && applied->saved == *view.saved) saved_acks[from] = applied->saved.serial;
        return;
    }
    if (auto* leave = std::get_if<w::Leave>(&message.payload)) {
        pending_baseline.reset(); if (!hosting) { pending_control.reset(); returning_control.reset(); returned.clear(); local_returned = false; } deferred.clear(); deferred_bytes = 0;
        if (hosting) {
            if (view.phase == Phase::Preparing || view.phase == Phase::Playing) control(0, {w::ControlKind::HostCancelPlaytest, view.playtest_round, view.playtest_revision, false, 0});
            if(leave->reason==0){
                auto& member=view.members.at(from);coordinator.disconnect(member.peer);member.phase=Phase::Idle;member.ready=false;
                clear_preview_for(from);clear_channel(from);saved_acks.erase(from);
                std::erase_if(held_requests,[&](const HeldRequest& item){if(item.author.slot!=from)return false;held_request_bytes-=item.cost;return true;});
                pending_offers.insert(from);
            }else freeze(from, leave->reason == 3 ? "A participant needs explicit native/baseline recovery." : "A participant left the shared editor.");
            progress_return();
        } else if (leave->reason == 3) freeze(0, "The host requires an explicit editor baseline recovery.");
        else { view.phase = Phase::Idle; view.local_leases.clear(); clear_lease_requests(); channels.clear(); output_bytes = 0; }
        return;
    }
    if (auto* command = std::get_if<w::Control>(&message.payload)) control(from, *command);
}

void Controller::Impl::pump()
{
    for (auto& [to, ch] : channels) {
        size_t budget = config.packets_per_tick;
        while (budget-- && session.pending_to(to) < config.reliable_window) {
            if (!ch.priority.empty()) {
                if (!session.send_reliable_to(to, config.message_type, ch.priority.front())) break;
                output_bytes -= ch.priority.front().size(); ch.priority.pop_front(); continue;
            }
            if (ch.ordered.empty()) break;
            auto& item = ch.ordered.front();
            if (auto* packet = std::get_if<net::Writer>(&item)) {
                if (!session.send_reliable_to(to, config.message_type, *packet)) break;
                output_bytes -= packet->size(); ch.ordered.pop_front(); continue;
            }
            auto& t = std::get<Transfer>(item); w::Message packet; packet.context = t.context;
            if (t.phase == 0) packet.payload = w::TransferBegin{t.kind, t.serial, uint32_t(t.bytes->size()), t.hash};
            else if (t.offset < t.bytes->size()) {
                size_t count = std::min(w::kChunkBytes, t.bytes->size() - t.offset);
                packet.payload = w::TransferChunk{t.serial, uint32_t(t.offset), std::vector<uint8_t>(t.bytes->begin() + t.offset, t.bytes->begin() + t.offset + count)};
            } else packet.payload = w::TransferEnd{t.serial};
            net::Writer encoded; std::string error;
            if (!w::write(packet, encoded, error)) { report(error); break; }
            encoded = address(to, encoded);
            if (!session.send_reliable_to(to, config.message_type, encoded)) break;
            if (t.phase == 0) t.phase = 1;
            else if (t.offset < t.bytes->size()) t.offset += std::min(w::kChunkBytes, t.bytes->size() - t.offset);
            else ch.ordered.pop_front();
        }
    }
}

Controller::Controller(net::Session& s, Callbacks cb, Config config) : impl_(std::make_unique<Impl>(s, std::move(cb), config)) {}
Controller::~Controller() = default;
const State& Controller::state() const { return impl_->state(); }
const View& Controller::view() const { return impl_->view; }

bool Controller::start(State state, std::vector<uint8_t> resources, const std::string& name, std::string& error, std::optional<Peer> resume_host)
{
    auto& i = *impl_;
    if (i.ticking || i.session.role() != net::Role::Host || !i.session.connected() || resources.empty() || resources.size() > w::kResourceBytes || !i.callbacks.host_hooks.apply) { error = "A connected host, native adapter and bounded nonempty resources are required."; return false; }
    net::Writer offer;
    if (!w::write({{state.document.project, state.document.epoch, state.document.revision}, w::Offer{w::kSchema, name, {1, 1}}}, offer, error)) return false;
    std::array<uint32_t, 4> historical{};
    auto author = [&](Peer p) { if (p.slot < 4) historical[p.slot] = std::max(historical[p.slot], p.generation); };
    for (const auto& gesture : state.history) author(gesture.operation.author);
    for (const auto& [operation, decision] : state.decisions) { author(operation.author); if (decision.change) author(decision.change->gesture.author); }
    for (const auto& [peer, sequence] : state.retired_sequences) { (void)sequence; author(peer); }
    for (auto peer : state.history_truncated) author(peer);
    for (size_t slot = 0; slot < historical.size(); ++slot) historical[slot] = std::max(historical[slot], i.generations[slot]);
    Peer host;
    if (resume_host) {
        if (resume_host->slot || !resume_host->generation) { error = "Only an explicitly owned host author capability may resume."; return false; }
        host = *resume_host;
    } else {
        if (historical[0] == UINT32_MAX) { error = "Fresh host author capability space is exhausted."; return false; }
        host = {0, historical[0] + 1};
    }
    const auto epoch = state.document.epoch;
    Coordinator candidate; if (!candidate.reset(std::move(state), epoch, error)) return false;
    if (!candidate.connect(host)) { error = "Cannot initialize host author capability."; return false; }
    end(); i.hosting = true; i.coordinator = std::move(candidate); i.replica = {};
    i.resources = std::make_shared<const std::vector<uint8_t>>(std::move(resources));
    i.resources_hash = upd::sha256_hex(*i.resources);
    i.view = {}; i.view.phase = Phase::Ready; i.view.local = host; i.view.name = name; i.view.context = i.context();
    i.operation_sequence = 0; i.round = 0;
    for (const auto& [operation, decision] : i.coordinator.state().decisions) { (void)decision; if (operation.author == host && operation.epoch == epoch) i.operation_sequence = std::max(i.operation_sequence, operation.sequence); }
    if (auto retired = i.coordinator.state().retired_sequences.find(host); retired != i.coordinator.state().retired_sequences.end()) i.operation_sequence = std::max(i.operation_sequence, retired->second);
    for (const auto& gesture : i.coordinator.state().history) if (gesture.operation.author == host && gesture.operation.epoch == epoch) i.operation_sequence = std::max(i.operation_sequence, gesture.operation.sequence);
    Member member; member.peer = host; member.name = i.session.name_of(0); member.phase = Phase::Ready; member.applied_revision = i.context().revision; i.view.members[0] = member;
    if (i.callbacks.host_hooks.persist) i.view.locally_durable = i.context().revision;
    i.generations = historical; i.generations[0] = std::max(historical[0], host.generation); i.coverage();
    for (const auto& player : i.session.players()) if (player.id) on_join(player.id);
    error.clear(); return true;
}

bool Controller::update_resources(std::vector<uint8_t> resources, Revision target, std::string& error, bool broadcast, std::vector<uint8_t> broadcast_bytes)
{
    auto& i = *impl_;
    if (!i.hosting || i.view.phase != Phase::Ready || resources.empty() || resources.size() > w::kResourceBytes || broadcast_bytes.size() > w::kResourceBytes || target < i.context().revision || target > i.context().revision + 1) { error = "Invalid host resource revision or size."; return false; }
    if (!broadcast) broadcast_bytes.clear();
    if (i.transfer_bytes() + resources.size() + broadcast_bytes.size() > i.config.transfer_bytes) { error = "Resource staging exceeds bounded transfer memory."; return false; }
    for (const auto& [id, member] : i.view.members) if (id && broadcast && (member.phase == Phase::Ready || member.phase == Phase::Syncing)) {
        auto ch = i.channels.find(id);
        if (ch != i.channels.end() && ch->second.ordered.size() + ch->second.priority.size() + 2 > i.config.outbound_packets) { error = "Resource publication needs room for its commit decision."; return false; }
    }
    Buffer buffer = std::make_shared<const std::vector<uint8_t>>(std::move(resources));
    auto full_hash = upd::sha256_hex(*buffer);
    Buffer outgoing = broadcast_bytes.empty() ? buffer : std::make_shared<const std::vector<uint8_t>>(std::move(broadcast_bytes));
    auto broadcast_hash = outgoing == buffer ? full_hash : upd::sha256_hex(*outgoing);
    if (target == i.context().revision && !i.coordinator.applying()) {
        std::vector<uint8_t> queued; queued.reserve(i.view.members.size());
        auto rollback = [&] { for (auto previous : queued) i.channels.at(previous).ordered.pop_back(); };
        try {
            if (broadcast) for (const auto& [id, member] : i.view.members) if (id && (member.phase == Phase::Ready || member.phase == Phase::Syncing)) {
                if (!i.queue_transfer(id, i.context(), w::TransferKind::Resources, outgoing, error, broadcast_hash)) { rollback(); return false; }
                queued.push_back(id);
            }
        } catch (...) { rollback(); throw; }
        i.resources = std::move(buffer); i.resources_hash = std::move(full_hash);
    } else i.staged = Impl::ResourceStage{target, std::move(buffer), std::move(outgoing), std::move(full_hash), std::move(broadcast_hash), broadcast};
    error.clear(); return true;
}

bool Controller::accept_offer(std::string& error)
{
    auto& i = *impl_;
    if (i.hosting || i.view.phase != Phase::Offered || !i.session.connected()) { error = "There is no newly offered editor baseline to accept."; return false; }
    w::Join join;
    try{if(i.callbacks.prepare_join&&!i.callbacks.prepare_join(join,error))return false;}
    catch(...){error="Preparing the verified join inventory failed.";return false;}
    std::vector<uint8_t> checked;if(!w::encode_join(join,checked,error))return false;
    if(join.durable_revision>i.view.context.revision){error="The join durability claim exceeds this offered project revision.";return false;}
    i.clear_previews();i.presence_local.reset();i.presence_relay={};i.preview_authors={};i.preview_seen={};i.preview_sequence=0;
    i.replica = {}; i.channels.clear(); i.output_bytes = 0; i.view.local_leases.clear(); i.clear_lease_requests(); i.view.locally_durable = 0;
    if (!i.queue(0, {i.view.context, std::move(join)}, true, error)) return false;
    i.view.phase = Phase::Joining; error.clear(); return true;
}

void Controller::end()
{
    auto& i = *impl_;
    if (i.active() && i.view.context.epoch) {
        net::Writer packet; std::string ignored;
        if (w::write({i.view.context, w::Leave{0}}, packet, ignored)) {
            if (i.hosting) for (const auto& [id, member] : i.view.members) { (void)member; if (id) i.session.send_reliable_to(id, i.config.message_type, i.address(id, packet)); }
            else i.session.send_reliable_to(0, i.config.message_type, i.address(0, packet));
        }
    }
    i.clear_previews();i.presence_local.reset();i.presence_relay={};i.preview_authors={};i.preview_seen={};i.preview_sequence=0;
    i.channels.clear(); i.input.clear(); i.local.clear(); i.clear_lease_requests(); i.saved_acks.clear(); i.local_saved_ack = 0; i.hashes.clear(); i.overflow.clear(); i.pending_offers.clear();
    i.resources.reset(); i.resources_hash.clear(); i.baseline_cache.reset(); i.staged.reset(); i.pending_baseline.reset(); i.pending_control.reset(); i.returning_control.reset(); i.returned.clear(); i.local_returned = false; i.deferred.clear(); i.deferred_bytes = 0; i.held_requests.clear(); i.held_request_bytes = 0;
    i.input_bytes = i.output_bytes = i.local_bytes = 0; i.coverage_pending = false; i.view = {};
    i.hosting = false;
}

void Controller::on_message(uint8_t from, net::Reader& reader)
{
    auto& i = *impl_; const size_t count = reader.left();
    constexpr size_t tag_offset=envelope_bytes+1+16+8+8;
    if(count>tag_offset && reader.cur()[tag_offset]==std::variant_size_v<w::Payload>-1){
        net::Reader copy(reader.cur(),count);Peer sender{copy.u8(),copy.u32()};const auto recipient=copy.u32();
        w::Message message;std::string error;
        if(count<=w::kPacketBytes+envelope_bytes && w::read(copy,message,sender,from==0,error)){
            const auto& value=std::get<w::TransformPreview>(message.payload);auto& pending=i.preview_input[value.author.slot];
            if(!pending || std::get<w::TransformPreview>(pending->message.payload).author!=value.author ||
                std::get<w::TransformPreview>(pending->message.payload).serial<value.serial ||
                (std::get<w::TransformPreview>(pending->message.payload).serial==value.serial&&!value.active))
                pending=Impl::PreviewInput{from,sender,recipient,std::move(message)};
        }
        reader.skip(count);return;
    }
    if (count <= envelope_bytes || count > w::kPacketBytes + envelope_bytes || i.input.size() >= i.config.inbound_packets || i.input_bytes + count > i.config.inbound_bytes) { i.overflow.insert(from); reader.skip(count); return; }
    Impl::Input entry; entry.from = from; entry.bytes.assign(reader.cur(), reader.cur() + count); reader.skip(count);
    i.input_bytes += count; i.input.push_back(std::move(entry));
}
void Controller::freeze_local(const std::string& reason)
{
    auto& i = *impl_;
    if (i.active() && i.view.phase != Phase::Frozen) i.freeze(0, reason.empty() ? "The local editor replica requires recovery." : reason);
}
void Controller::on_join(uint8_t id) { auto& i = *impl_; if (i.input.size() < i.config.inbound_packets) i.input.push_back({1, id, {}}); else i.overflow.insert(id); }
void Controller::on_leave(uint8_t id) { auto& i = *impl_; if (i.input.size() < i.config.inbound_packets) i.input.push_back({2, id, {}}); else i.overflow.insert(id); }

void Controller::tick(Time now)
{
    auto& i = *impl_; if (i.ticking) return;
    if (now < i.now) { i.freeze(0, "Editor controller monotonic clock moved backwards."); return; }
    i.now = now; i.ticking = true;
    struct Guard { bool& ticking; ~Guard() { ticking = false; } } guard{i.ticking};
    for (auto from : i.overflow) i.freeze(from, "Editor inbound queue exceeded a configured bound."); i.overflow.clear();
    try {
        size_t count = i.config.inbound_packets;
        while (count-- && !i.input.empty()) {
            auto entry = std::move(i.input.front()); i.input.pop_front(); i.input_bytes -= entry.bytes.size();
            if (entry.kind == 1) {
                if (i.hosting) {
                    if (i.view.phase == Phase::Preparing || i.view.phase == Phase::Playing) i.control(0, {w::ControlKind::HostCancelPlaytest, i.view.playtest_round, i.view.playtest_revision, false, 0});
                    if (i.view.phase == Phase::Ready) i.offer(entry.from);
                    else if (entry.from && entry.from < 4) i.pending_offers.insert(entry.from);
                }
                continue;
            }
            if (entry.kind == 2) {
                if (i.hosting) {
                    i.pending_offers.erase(entry.from);
                    if (i.view.phase == Phase::Preparing || i.view.phase == Phase::Playing) i.control(0, {w::ControlKind::HostCancelPlaytest, i.view.playtest_round, i.view.playtest_revision, false, 0});
                    if (auto member = i.view.members.find(entry.from); member != i.view.members.end()) { i.coordinator.disconnect(member->second.peer); i.view.members.erase(member); }
                    i.saved_acks.erase(entry.from);
                    std::erase_if(i.held_requests, [&](const Impl::HeldRequest& item) { if (item.author.slot != entry.from) return false; i.held_request_bytes -= item.cost; return true; });
                    i.clear_preview_for(entry.from);if(entry.from<4){i.preview_input[entry.from].reset();i.preview_relay[entry.from].reset();}
                    i.clear_channel(entry.from);
                    i.progress_return();
                } else if (!entry.from) { i.clear_previews();i.view.phase = Phase::Disconnected; i.pending_control.reset(); i.pending_baseline.reset(); i.deferred.clear(); i.deferred_bytes = 0; i.view.local_leases.clear(); i.clear_lease_requests(); i.channels.clear(); i.output_bytes = 0; i.report("The editor host disconnected; locally durable recovery remains available."); }
                else {i.clear_preview_for(entry.from,false);i.view.members.erase(entry.from);}
                continue;
            }
            if (i.hosting && (!entry.from || !i.view.members.contains(entry.from))) continue;
            if (!i.hosting && entry.from) continue;
            net::Reader reader(entry.bytes.data(), entry.bytes.size());
            Peer sender{reader.u8(), reader.u32()}; const auto recipient = reader.u32();
            if (sender.slot != entry.from || !sender.generation || !recipient) { i.freeze(entry.from, "Invalid editor capability envelope."); continue; }
            if (i.hosting && (sender != i.view.members.at(entry.from).peer || recipient != i.view.local.generation)) continue;
            w::Message message; std::string error;
            if (!w::read(reader, message, sender, !i.hosting, error)) {
                if (!i.hosting && (sender != i.host_author() || recipient != i.view.local.generation)) continue;
                i.freeze(entry.from, error); continue;
            }
            if (!i.hosting) {
                if (auto* offer = std::get_if<w::Offer>(&message.payload)) {
                    if (offer->host != sender || offer->assigned.generation != recipient) { i.freeze(0, "An editor offer disagreed with its capability envelope."); continue; }
                } else if (sender != i.host_author() || recipient != i.view.local.generation) continue;
            }
            i.message(entry.from, std::move(message));
        }
        count = i.config.inbound_packets;
        while (count-- && !i.local.empty()) {
            auto queued = std::move(i.local.front()); i.local.pop_front(); i.local_bytes -= queued.second;
            auto action = std::move(queued.first);
            std::visit([&](const auto& value) {
                using T = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<T, Request>) i.host_request(0, value);
                else if constexpr (std::is_same_v<T, w::LeaseRequest>) i.lease_request(0, value);
                else if constexpr (std::is_same_v<T, w::Control>) i.control(0, value);
                else if constexpr (std::is_same_v<T, w::Presence>) { if (i.callbacks.presence) i.callbacks.presence(value.author, value); i.broadcast({i.context(), value}, true); }
                else { i.view.saved = value; i.saved_acks.clear(); i.saved_acks[0] = value.serial; if (i.callbacks.saved) i.callbacks.saved(value); i.broadcast({i.context(), value}); }
            }, action);
        }
        i.service_leases();
        i.prune_local_leases();
        if (i.hosting) {
            i.view.local_leases.clear();
            if (i.view.phase == Phase::Ready) for (const auto& lease : i.coordinator.leases()) if (lease.owner == i.view.local && lease.expires > now && !i.quarantined(lease)) i.view.local_leases.push_back(lease);
        }
        i.service_baseline();
        i.service_requests();
        i.service_deferred();
        i.service_control();
        if (i.hosting && i.view.phase == Phase::Ready && !i.pending_offers.empty()) {
            auto pending = std::move(i.pending_offers); i.pending_offers.clear(); for (auto id : pending) i.offer(id);
        }
        if (i.coverage_pending && i.view.phase == Phase::Ready) {
            i.coverage_pending = false; std::string error; auto ctx = i.context();
            if (!i.queue(0, {ctx, w::Coverage{i.view.locally_durable, i.hashes.at(ctx.revision)}}, true, error)) i.freeze(0, error);
        }
        i.service_previews();i.pump();
    } catch (const std::exception& error) { i.freeze(0, error.what()); }
    catch (...) { i.freeze(0, "Editor controller callback failed."); }
}

bool Controller::submit(Request request, std::string& error)
{
    auto& i = *impl_;
    if (i.view.phase != Phase::Ready) { error = "Authoring is held or needs a native baseline."; return false; }
    if (std::any_of(request.leases.begin(),request.leases.end(),[&](const LeaseToken& t) {
        return std::any_of(i.release_fences.begin(),i.release_fences.end(),[&](const auto& f) { return f.token == t; });
    })) { error = "An editing lease is being released; wait for a new grant."; return false; }
    const auto ctx = i.context();
    if ((request.project != ProjectId{} && request.project != ctx.project) || (request.epoch && request.epoch != ctx.epoch) ||
        (request.operation.author != Peer{} && request.operation.author != i.view.local) || (request.operation.epoch && request.operation.epoch != ctx.epoch)) { error = "A local request supplied stale or foreign identity."; return false; }
    request.project = ctx.project; request.epoch = ctx.epoch; request.operation.author = i.view.local; request.operation.epoch = ctx.epoch;
    if (!request.operation.sequence) {
        if (i.operation_sequence == UINT64_MAX) { error = "Author operation sequence exhausted."; return false; }
        request.operation.sequence = ++i.operation_sequence;
    } else i.operation_sequence = std::max(i.operation_sequence, request.operation.sequence);
    std::vector<uint8_t> bytes; if (!w::encode_request(request, bytes, error)) return false;
    if (i.hosting) return i.queue_local(std::move(request), bytes.size(), error);
    return i.queue(0, {{ctx.project, ctx.epoch, request.base_revision}, request}, false, error);
}

namespace {
template<class Impl>
uint64_t lease(Impl& i, w::LeaseAction action, const std::vector<Scope>& scopes, const std::vector<LeaseToken>& tokens, Time ttl, std::string& error)
{
    if (i.view.phase != Phase::Ready || i.pending_leases.size() >= 64 || i.lease_serial == UINT64_MAX) { error = "Editing leases are held or their queue is full."; return 0; }
    w::LeaseRequest request{action, ++i.lease_serial, ttl, scopes, tokens};
    net::Writer encoded; if (!w::write({i.context(), request}, encoded, error)) return 0;
    if (action == w::LeaseAction::Renew && std::any_of(tokens.begin(),tokens.end(),[&](const LeaseToken& t) {
        return std::any_of(i.release_fences.begin(),i.release_fences.end(),[&](const auto& f) { return f.token == t; });
    })) { error = "A releasing editing lease cannot be renewed."; return 0; }
    if (action == w::LeaseAction::Release) {
        const auto extra = std::count_if(tokens.begin(),tokens.end(),[&](const LeaseToken& token) {
            return std::none_of(i.release_fences.begin(),i.release_fences.end(),[&](const auto& fence) { return fence.token == token; });
        });
        if (i.release_fences.size()+size_t(extra)>64*64+64) { error="The local lease retirement queue is full."; return 0; }
    }
    bool deferred = action == w::LeaseAction::Acquire && i.release_pending(scopes);
    i.pending_leases[request.serial] = {request, deferred ? 0 : i.now, !deferred};
    if (!deferred) {
        bool okay = i.hosting ? i.queue_local(request, encoded.size(), error) : i.queue(0, {i.context(), request}, action != w::LeaseAction::Release, error);
        if (!okay) { i.pending_leases.erase(request.serial); return 0; }
    }
    if (action == w::LeaseAction::Release) {
        // Retire superseded grants now. Their reliable packets may still arrive,
        // but neither their old reply nor an unseen host request may revive them.
        std::erase_if(i.pending_leases,[&](const auto& entry) {
            const auto& old = entry.second.request;
            if (old.serial >= request.serial || old.action == w::LeaseAction::Release) return false;
            return std::any_of(tokens.begin(),tokens.end(),[&](const LeaseToken& token) {
                return std::any_of(old.scopes.begin(),old.scopes.end(),[&](const Scope& scope) { return i.overlapping(scope,token.scope); }) ||
                    std::any_of(old.tokens.begin(),old.tokens.end(),[&](const LeaseToken& t) { return i.overlapping(t.scope,token.scope); });
            });
        });
        for (const auto& token : tokens) {
            auto found = std::find_if(i.release_fences.begin(),i.release_fences.end(),[&](const auto& f) { return f.token == token; });
            if (found == i.release_fences.end()) i.release_fences.push_back({token,request.serial});
            else *found = {token,request.serial};
        }
        i.prune_local_leases(); i.clear_preview_for(i.view.local.slot); i.preview_local.reset();
    }
    error.clear(); return request.serial;
}
}

uint64_t Controller::request_leases(const std::vector<Scope>& scopes, Time ttl, std::string& error) { return lease(*impl_, w::LeaseAction::Acquire, scopes, {}, ttl, error); }
uint64_t Controller::renew_leases(const std::vector<LeaseToken>& tokens, Time ttl, std::string& error) { return lease(*impl_, w::LeaseAction::Renew, {}, tokens, ttl, error); }
uint64_t Controller::release_leases(const std::vector<LeaseToken>& tokens, std::string& error) { return lease(*impl_, w::LeaseAction::Release, {}, tokens, 0, error); }

bool Controller::set_presence(w::Presence presence, std::string& error)
{
    auto& i = *impl_; if (i.view.phase != Phase::Ready && i.view.phase != Phase::Preparing) { error = "No shared editor presence is active."; return false; }
    presence.author = i.view.local;
    if ((presence.selected != ObjectId{} && !i.state().document.placements.contains(presence.selected)) || (presence.act != ObjectId{} && !i.state().document.acts.contains(presence.act))) { error = "Presence selected an unknown placement or act."; return false; }
    presence.ownership=0;presence.preview_lease={};presence.phase=w::PresencePhase::Ready;
    i.presence_local=presence;error.clear();return true;
}

bool Controller::set_preview(ObjectId object,ObjectId act,Pose pose,std::string& error)
{
    auto& i=*impl_;
    if(i.view.phase!=Phase::Ready || !i.view.local.generation || i.preview_sequence==UINT64_MAX){error="No editable preview scope is active.";return false;}
    auto found=i.state().document.placements.find(object);
    if(found==i.state().document.placements.end() || found->second.act_id!=act){error="A preview requires an existing placement in its committed act.";return false;}
    const auto lease=i.selection_lease(i.view.local,object);
    if(!lease.token){error="A preview requires current placement ownership.";return false;}
    w::TransformPreview value{i.view.local,i.preview_sequence+1,true,object,act,pose,lease};
    w::Message message{i.context(),value};net::Writer check;
    if(!w::write(message,check,error))return false;
    ++i.preview_sequence;i.preview_local=std::move(message);return true;
}
void Controller::clear_preview()
{
    auto& i=*impl_;i.clear_preview_for(i.view.local.slot,false);i.preview_local.reset();
    if(!i.view.context.epoch || !i.view.local.generation || i.preview_sequence==UINT64_MAX)return;
    w::TransformPreview value;value.author=i.view.local;value.serial=++i.preview_sequence;value.active=false;
    i.preview_local=w::Message{i.context(),value};
}

bool Controller::publish_saved(w::Saved saved, std::string& error)
{
    auto& i = *impl_;
    if (!i.hosting || i.view.phase != Phase::Ready || saved.saved_revision > i.context().revision || !saved.resource.instance || !saved.resource.type) { error = "Only a completed host native save can be published."; return false; }
    if (i.serial == UINT64_MAX) { error = "Native save nonce exhausted."; return false; }
    saved.serial = i.next_serial();
    return i.queue_local(saved, 128, error);
}
bool Controller::acknowledge_saved(const w::Saved& saved, std::string& error)
{
    auto& i = *impl_;
    if (i.hosting || i.view.phase != Phase::Ready || !i.view.saved || saved != *i.view.saved || !saved.serial) { error = "There is no matching native save to acknowledge."; return false; }
    if (i.local_saved_ack == saved.serial) { error.clear(); return true; }
    if (!i.queue(0, {i.context(), w::SavedApplied{saved}}, true, error)) return false;
    i.local_saved_ack = saved.serial; error.clear(); return true;
}
bool Controller::prepare_playtest(std::string& error)
{
    auto& i = *impl_;
    if (!i.hosting || i.view.phase != Phase::Ready || i.round == UINT64_MAX) { error = "Only a ready host can prepare the next group playtest."; return false; }
    return i.queue_local(w::Control{w::ControlKind::HostPreparePlaytest, i.round + 1, i.context().revision, false, 0}, 64, error);
}
bool Controller::ready_playtest(bool ready, std::string& error)
{
    auto& i = *impl_;
    if (i.view.phase != Phase::Preparing) { error = "There is no current group playtest confirmation."; return false; }
    w::Control command{w::ControlKind::GuestReadyPlaytest, i.view.playtest_round, i.view.playtest_revision, ready, 0};
    if (i.hosting) return i.queue_local(command, 64, error);
    return i.queue(0, {i.context(), command}, false, error);
}
bool Controller::cancel_playtest(std::string& error)
{
    auto& i = *impl_;
    if (!i.hosting || (i.view.phase != Phase::Preparing && i.view.phase != Phase::Playing)) { error = "Only the host can cancel the active group playtest."; return false; }
    return i.queue_local(w::Control{w::ControlKind::HostCancelPlaytest, i.view.playtest_round, i.view.playtest_revision, false, 0}, 64, error);
}
bool Controller::return_editor(std::string& error)
{
    auto& i = *impl_;
    if (!i.hosting || i.view.phase != Phase::Playing) { error = "Only the host can return the active playtest to editing."; return false; }
    return i.queue_local(w::Control{w::ControlKind::HostReturnEditor, i.view.playtest_round, i.view.playtest_revision, false, 0}, 64, error);
}

SavedCoverage Controller::saved_coverage() const
{
    const auto& i = *impl_;
    SavedCoverage result;
    if (!i.active() || !i.view.saved) return result;
    const auto& saved = *i.view.saved;
    result.revision = saved.saved_revision; result.serial = saved.serial;
    result.current = saved.saved_revision == i.context().revision && saved.serial != 0;
    if (!i.hosting) {
        result.expected = 1; result.applied = i.local_saved_ack == saved.serial ? 1 : 0;
        return result;
    }
    result.expected = 1;
    auto own = i.saved_acks.find(0);
    result.applied = own != i.saved_acks.end() && own->second == saved.serial ? 1 : 0;
    for (const auto& [id, member] : i.view.members) {
        if (!id || member.phase == Phase::Offered || member.phase == Phase::Frozen ||
            member.phase == Phase::Disconnected || member.phase == Phase::Idle) continue;
        ++result.expected;
        auto ack = i.saved_acks.find(id);
        if (ack != i.saved_acks.end() && ack->second == saved.serial) ++result.applied;
    }
    return result;
}

bool Controller::settled() const
{
    const auto& i = *impl_;
    if (!i.active() || i.view.phase == Phase::Frozen) return true;
    if (i.ticking || i.view.phase != Phase::Ready || i.pending_control || i.returning_control || i.staged) return false;
    for (const auto& queued : i.local) if (!std::holds_alternative<Request>(queued.first)) return false;
    const auto queued_to = [&](uint8_t id) {
        auto channel = i.channels.find(id);
        return (channel != i.channels.end() && (!channel->second.priority.empty() || !channel->second.ordered.empty())) || i.session.pending_to(id) != 0;
    };
    if (!i.hosting) {
        auto channel = i.channels.find(0);
        return !queued_to(0) && !i.coverage_pending && (!i.view.saved || i.local_saved_ack == i.view.saved->serial) && !i.pending_baseline && i.deferred.empty() && i.input.empty() &&
            (channel == i.channels.end() || !channel->second.receiver.active());
    }
    const auto revision = i.context().revision;
    auto hash = i.hashes.find(revision);
    if (hash == i.hashes.end()) return false;
    for (const auto& [id, member] : i.view.members) {
        if (!id || member.phase == Phase::Offered || member.phase == Phase::Frozen || member.phase == Phase::Disconnected || member.phase == Phase::Idle) continue;
        if (member.phase != Phase::Ready || queued_to(id) || member.applied_revision != revision ||
            member.coverage.durable_revision != revision || member.coverage.state_hash != hash->second) return false;
        if (i.view.saved) { auto ack = i.saved_acks.find(id); if (ack == i.saved_acks.end() || ack->second != i.view.saved->serial) return false; }
    }
    return true;
}

} // namespace editor_session



