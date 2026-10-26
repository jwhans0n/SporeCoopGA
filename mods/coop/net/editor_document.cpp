#include "editor_document.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace editor {
namespace {

bool identity(ObjectId id) { return id.id && id.generation; }
bool identity(Peer p) { return p.slot < 4 && p.generation; }
bool identity(OperationId op) { return identity(op.author) && op.sequence && op.epoch; }
bool project(const ProjectId& id) { return std::any_of(id.begin(), id.end(), [](auto v) { return v != 0; }); }
bool finite(float v) { return std::isfinite(v); }
bool position(Vec3 v) { return finite(v.x) && finite(v.y) && finite(v.z) && std::abs(v.x) <= 1e7f && std::abs(v.y) <= 1e7f && std::abs(v.z) <= 1e7f; }
bool rotation(Rotation v)
{
    double n = double(v.x) * v.x + double(v.y) * v.y + double(v.z) * v.z + double(v.w) * v.w;
    return finite(v.x) && finite(v.y) && finite(v.z) && finite(v.w) && std::abs(n - 1) <= .01;
}
bool field_key(FieldKey k) { return k.field <= Field::Property && ((k.field == Field::Property) == (k.property != 0)); }
bool utf8(const std::string& s)
{
    for (size_t i = 0; i < s.size();) {
        uint32_t c = uint8_t(s[i++]);
        if (!c) return false;
        if (c < 0x80) continue;
        unsigned n; uint32_t low;
        if (c >= 0xC2 && c <= 0xDF) { n = 1; c &= 31; low = 0x80; }
        else if (c >= 0xE0 && c <= 0xEF) { n = 2; c &= 15; low = 0x800; }
        else if (c >= 0xF0 && c <= 0xF4) { n = 3; c &= 7; low = 0x10000; }
        else return false;
        if (n > s.size() - i) return false;
        for (unsigned j = 0; j < n; ++j) { uint32_t b = uint8_t(s[i++]); if ((b & 0xC0) != 0x80) return false; c = (c << 6) | (b & 63); }
        if (c < low || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) return false;
    }
    return true;
}
size_t value_bytes(const Value& v)
{
    if (auto* s = std::get_if<std::string>(&v)) return 24 + s->size();
    return 40;
}
size_t placement_bytes(const Placement& p)
{
    size_t n = 192 + p.field_revisions.size() * 32;
    for (const auto& [id, v] : p.properties) { (void)id; n += 16 + value_bytes(v); }
    return n;
}
size_t change_bytes(const Change& c) { return 160 + c.summary.size() + bytes(c.patch) + bytes(c.inverse) + c.allocated.size() * 16 + c.clone_sources.size() * 32; }
size_t decision_bytes(const Decision& d) { return 96 + d.reason.size() + (d.change ? change_bytes(*d.change) : 0); }
size_t gesture_bytes(const Gesture& g) { return 96 + g.summary.size() + bytes(g.undo) + bytes(g.redo); }
size_t history_bytes(const State& s) { size_t n = 0; for (const auto& g : s.history) n += gesture_bytes(g); return n; }
size_t decisions_bytes(const State& s) { size_t n = 0; for (const auto& [id, d] : s.decisions) { (void)id; n += 48 + decision_bytes(d); } return n; }

bool typed(const Value& v, const PropertySpec& spec, const Limits& limits)
{
    if (v.index() != size_t(spec.kind)) return false;
    switch (spec.kind) {
    case ValueKind::Number: { float x = std::get<float>(v); return finite(x) && x >= spec.minimum && x <= spec.maximum; }
    case ValueKind::Boolean: return true;
    case ValueKind::Integer: { auto x = std::get<int64_t>(v); return static_cast<long double>(x) >= spec.minimum && static_cast<long double>(x) <= spec.maximum; }
    case ValueKind::Text: { const auto& s = std::get<std::string>(v); return s.size() <= std::min<size_t>(spec.max_text, limits.text_bytes) && utf8(s); }
    case ValueKind::Reference: { auto id = std::get<ObjectId>(v); return identity(id) || (spec.nullable && id == ObjectId{}); }
    case ValueKind::Resource: { auto k = std::get<ResourceKey>(v); return (k.instance && k.type) || (spec.nullable && k == ResourceKey{}); }
    default: return false;
    }
}
bool placement_shape(const Placement& p, const Document& d, const Limits& limits, bool versions)
{
    if (!identity(p.object) || !d.classes.contains(p.class_id) || !d.acts.contains(p.act_id) ||
        !position(p.position) || !rotation(p.rotation) || !finite(p.scale) || p.scale <= 0 || p.scale > 1000 ||
        p.properties.size() > limits.properties || p.field_revisions.size() > d.schema.size() + 3) return false;
    if (versions && p.revision > d.revision) return false;
    for (const auto& [id, v] : p.properties) {
        auto it = d.schema.find(id);
        if (it == d.schema.end() || !typed(v, it->second, limits)) return false;
    }
    for (const auto& [id, spec] : d.schema)
        if (!spec.optional && !p.properties.contains(id)) return false;
    for (const auto& [field, rev] : p.field_revisions)
        if (!field_key(field) || (field.field == Field::Property && !d.schema.contains(field.property)) || (versions && rev > p.revision)) return false;
    return true;
}
bool field_value(FieldKey k, const std::optional<Value>& v, const Document& d, const Limits& limits)
{
    if (!field_key(k)) return false;
    if (k.field == Field::Property) {
        auto it = d.schema.find(k.property);
        return it != d.schema.end() && (v ? typed(*v, it->second, limits) : it->second.optional);
    }
    if (!v) return false;
    if (k.field == Field::Position) { auto* x = std::get_if<Vec3>(&*v); return x && position(*x); }
    if (k.field == Field::Rotation) { auto* x = std::get_if<Rotation>(&*v); return x && rotation(*x); }
    auto* x = std::get_if<float>(&*v); return x && finite(*x) && *x > 0 && *x <= 1000;
}
void set(Placement& p, FieldKey k, const std::optional<Value>& v, Revision revision)
{
    if (k.field == Field::Position) p.position = std::get<Vec3>(*v);
    else if (k.field == Field::Rotation) p.rotation = std::get<Rotation>(*v);
    else if (k.field == Field::Scale) p.scale = std::get<float>(*v);
    else if (v) p.properties[k.property] = *v;
    else p.properties.erase(k.property);
    p.field_revisions[k] = revision;
    p.revision = revision;
}
void stamp(Placement& p, Revision revision)
{
    p.revision = revision;
    for (auto& [field, rev] : p.field_revisions) { (void)field; rev = revision; }
    for (Field field : {Field::Position, Field::Rotation, Field::Scale}) p.field_revisions[{field, 0}] = revision;
    for (const auto& [id, v] : p.properties) { (void)v; p.field_revisions[{Field::Property, id}] = revision; }
}
Revision absent_revision(const Document& d, ObjectId id)
{
    auto it = d.tombstones.find(id); return it == d.tombstones.end() ? 0 : it->second;
}
bool check(const Document& d, const Check& c)
{
    auto it = d.placements.find(c.object);
    if (!identity(c.object)) return false;
    if (c.kind == CheckKind::Absent) return it == d.placements.end() && absent_revision(d, c.object) == c.expected;
    if (it == d.placements.end()) return false;
    if (c.kind == CheckKind::Object) return it->second.revision == c.expected;
    return c.kind == CheckKind::Field && field_key(c.field) && field_revision(it->second, c.field) == c.expected;
}
bool patch_shape(const Patch& p, const Document& d, const Limits& l)
{
    if (p.objects.size() + p.fields.size() > l.mutations || p.dependencies.size() > l.dependencies) return false;
    std::set<ObjectId> objects;
    std::set<std::pair<ObjectId, FieldKey>> fields;
    for (const auto& e : p.objects) {
        if (!identity(e.object) || bool(e.before) == bool(e.after) || !objects.insert(e.object).second || e.expected > d.revision) return false;
        for (const auto* v : {&e.before, &e.after})
            if (*v && ((*v)->object != e.object || !placement_shape(**v, d, l, true))) return false;
    }
    for (const auto& e : p.fields)
        if (!identity(e.object) || objects.contains(e.object) || !fields.insert({e.object, e.field}).second ||
            e.expected > d.revision || e.restored_revision > d.revision || e.restored_object_revision > d.revision ||
            !field_value(e.field, e.before, d, l) || !field_value(e.field, e.after, d, l)) return false;
    for (const auto& c : p.dependencies)
        if (!identity(c.object) || c.kind > CheckKind::Absent || c.expected > d.revision ||
            (c.kind == CheckKind::Field ? !field_key(c.field) : c.field != FieldKey{})) return false;
    return true;
}
Decision failure(Code code, const std::string& reason, Revision revision)
{
    std::string bounded = reason.substr(0, 256);
    while (!bounded.empty() && !utf8(bounded)) bounded.pop_back();
    return {code, std::move(bounded), revision, std::nullopt};
}

} // namespace

std::optional<Value> value(const Placement& p, FieldKey field)
{
    switch (field.field) {
    case Field::Position: return p.position;
    case Field::Rotation: return p.rotation;
    case Field::Scale: return p.scale;
    case Field::Property: { auto it = p.properties.find(field.property); if (it != p.properties.end()) return it->second; return {}; }
    }
    return {};
}
Revision field_revision(const Placement& p, FieldKey field)
{
    auto it = p.field_revisions.find(field); return it == p.field_revisions.end() ? 0 : it->second;
}
size_t bytes(const Document& d)
{
    size_t n = 128 + (d.classes.size() + d.acts.size() + d.tombstones.size()) * 48 + d.schema.size() * 64 + d.external_references.size() * 32;
    for (const auto& [id, p] : d.placements) { (void)id; n += placement_bytes(p); }
    return n;
}
size_t bytes(const Patch& p)
{
    size_t n = 32 + p.dependencies.size() * 48;
    for (const auto& e : p.objects) n += 48 + (e.before ? placement_bytes(*e.before) : 0) + (e.after ? placement_bytes(*e.after) : 0);
    for (const auto& e : p.fields) n += 64 + (e.before ? value_bytes(*e.before) : 0) + (e.after ? value_bytes(*e.after) : 0);
    return n;
}
size_t bytes(const State& s) { return bytes(s.document) + history_bytes(s) + decisions_bytes(s) + (s.retired_sequences.size() + s.history_truncated.size()) * 48; }

bool valid(const Document& d, const Limits& l, std::string& error)
{
    auto bad = [&](const char* why) { error = why; return false; };
    if (!project(d.project) || !d.epoch || !d.next_id || d.next_id == UINT64_MAX || d.revision == UINT64_MAX) return bad("Invalid project identity, epoch, revision or ID allocator.");
    if (d.placements.size() > l.placements || d.classes.size() > l.classes || d.acts.size() > l.acts ||
        d.tombstones.size() > l.tombstones || d.schema.size() > l.properties * 8 || d.external_references.size() > l.dependencies * 16 || bytes(d) > l.document_bytes) return bad("Document exceeds a configured bound.");
    if (d.classes.empty() || d.acts.empty()) return bad("A document needs known classes and acts.");
    std::map<uint64_t, ObjectId> ids;
    auto add = [&](ObjectId id) { return identity(id) && id.id < d.next_id && ids.emplace(id.id, id).second; };
    for (auto id : d.classes) if (!add(id)) return bad("Invalid or reused class identity.");
    for (auto id : d.acts) if (!add(id)) return bad("Invalid or reused act identity.");
    for (const auto& [id, spec] : d.schema)
        if (!id || spec.kind < ValueKind::Number || spec.kind > ValueKind::Resource || !std::isfinite(spec.minimum) ||
            !std::isfinite(spec.maximum) || spec.minimum > spec.maximum || spec.max_text > l.text_bytes ||
            (spec.kind == ValueKind::Text && !spec.max_text)) return bad("Invalid editable-property schema.");
    for (const auto& [id, p] : d.placements) {
        if (!add(id) || id != p.object || !placement_shape(p, d, l, true)) return bad("Invalid placement, field value or revision.");
        for (const auto& [property, v] : p.properties) {
            (void)property;
            if (auto* ref = std::get_if<ObjectId>(&v); ref && identity(*ref) && !d.placements.contains(*ref)) return bad("A placement reference is missing.");
        }
    }
    for (const auto& [id, revision] : d.tombstones)
        if (!add(id) || revision > d.revision) return bad("Invalid or reused deleted identity.");
    std::set<Reference> refs;
    for (const auto& ref : d.external_references) {
        if (!identity(ref.owner) || !d.placements.contains(ref.target) || !refs.insert(ref).second) return bad("Invalid external objective/avatar reference.");
        auto known = ids.find(ref.owner.id);
        if (ref.owner.id >= d.next_id || (known != ids.end() && known->second != ref.owner)) return bad("An external reference owner has an invalid or reused identity.");
        ids.emplace(ref.owner.id, ref.owner);
    }
    error.clear(); return true;
}

Coordinator::Coordinator(Limits limits) : limits_(limits) {}

bool Coordinator::reset(State state, uint64_t epoch, std::string& error)
{
    if (busy_ || !epoch || (ready_ && state.document.project == state_.document.project && epoch == state_.document.epoch)) { error = "Recovery requires a fresh epoch nonce."; return false; }
    if (epoch != state.document.epoch) state.retired_sequences.clear();
    state.document.epoch = epoch;
    if (!valid(state.document, limits_, error)) return false;
    if (state.history.size() > limits_.history || history_bytes(state) > limits_.history_bytes ||
        state.decisions.size() > limits_.decisions || decisions_bytes(state) > limits_.decision_bytes || state.decision_order.size() != state.decisions.size() ||
        state.retired_sequences.size() > 4096 || state.history_truncated.size() > 4096) { error = "Recovery state exceeds a configured bound."; return false; }
    std::set<OperationId> seen;
    for (const auto& g : state.history) {
        if (!identity(g.operation) || !g.original_revision || g.original_revision > state.document.revision ||
            g.transition_revision < g.original_revision || g.transition_revision > state.document.revision ||
            !seen.insert(g.operation).second || g.summary.size() > limits_.summary_bytes || !utf8(g.summary) || (g.applied && g.redo_available) ||
            (g.applied && g.undo.objects.empty() && g.undo.fields.empty()) ||
            (g.redo_available && g.redo.objects.empty() && g.redo.fields.empty()) ||
            !patch_shape(g.undo, state.document, limits_) || !patch_shape(g.redo, state.document, limits_)) { error = "Invalid original-gesture history."; return false; }
    }
    seen.clear();
    for (auto id : state.decision_order) if (!seen.insert(id).second || !state.decisions.contains(id)) { error = "Invalid decision ordering."; return false; }
    for (const auto& [id, decision] : state.decisions) {
        if (!identity(id) || decision.code > Code::Busy || decision.reason.size() > 256 || decision.revision > state.document.revision ||
            (decision.accepted() != bool(decision.change))) { error = "Invalid remembered decision."; return false; }
        if (decision.change) {
            const auto& c = *decision.change;
            if (c.operation != id || !identity(c.gesture) || c.intent > Intent::Redo || c.revision != decision.revision || c.summary.size() > limits_.summary_bytes ||
                !patch_shape(c.patch, state.document, limits_) || !patch_shape(c.inverse, state.document, limits_) || c.allocated.size() > limits_.mutations) { error = "Invalid remembered transaction."; return false; }
            if ((!c.clone_sources.empty() && c.intent != Intent::Forward) || c.clone_sources.size() > c.allocated.size()) { error = "Invalid clone provenance."; return false; }
            for (const auto& [target, source] : c.clone_sources)
                if (!identity(source) || target == source || std::find(c.allocated.begin(), c.allocated.end(), target) == c.allocated.end() ||
                    std::none_of(c.patch.objects.begin(), c.patch.objects.end(), [&](const ObjectEdit& e) { return e.object == target && !e.before && e.after; })) { error = "Clone provenance does not name an allocated placement."; return false; }
        }
    }
    for (const auto& [p, sequence] : state.retired_sequences) if (!identity(p) || !sequence) { error = "Invalid operation retirement fence."; return false; }
    for (auto p : state.history_truncated) if (!identity(p)) { error = "Invalid history-retention author."; return false; }
    state_ = std::move(state); generations_ = {}; connected_ = {}; leases_.clear(); next_token_ = 1; last_time_ = 0;
    frozen_ = false; ready_ = true; error.clear(); return true;
}

bool Coordinator::peer(Peer p) const { return identity(p) && connected_[p.slot] && generations_[p.slot] == p.generation; }
bool Coordinator::connect(Peer p)
{
    if (!ready_ || busy_ || !identity(p) || p.generation < generations_[p.slot] || (p.generation == generations_[p.slot] && !connected_[p.slot])) return false;
    if (p.generation > generations_[p.slot]) {
        std::erase_if(leases_, [&](const Lease& l) { return l.owner.slot == p.slot; }); generations_[p.slot] = p.generation;
    }
    connected_[p.slot] = true; return true;
}
void Coordinator::disconnect(Peer p)
{
    if (!busy_ && peer(p)) { connected_[p.slot] = false; std::erase_if(leases_, [&](const Lease& l) { return l.owner == p; }); }
}
bool Coordinator::clock(Time now)
{
    if (now < last_time_) return false;
    last_time_ = now; std::erase_if(leases_, [&](const Lease& l) { return l.expires <= now; }); return true;
}
bool Coordinator::scope_valid(const Scope& s) const
{
    if (s.kind > ScopeKind::Field) return false;
    const auto& d = state_.document;
    if (s.kind == ScopeKind::Document) return s.object == ObjectId{} && s.field == FieldKey{};
    if (!identity(s.object)) return false;
    if (s.kind == ScopeKind::Class) return s.field == FieldKey{} && d.classes.contains(s.object);
    if (s.kind == ScopeKind::Act) return s.field == FieldKey{} && d.acts.contains(s.object);
    if (!d.placements.contains(s.object) && !d.tombstones.contains(s.object)) return false;
    return s.kind == ScopeKind::Placement ? s.field == FieldKey{} : field_key(s.field) && (s.field.field != Field::Property || d.schema.contains(s.field.property));
}
bool Coordinator::covers(const Scope& a, const Scope& b) const
{
    if (a.kind == ScopeKind::Document || a == b) return true;
    if (a.kind == ScopeKind::Placement && b.kind == ScopeKind::Field && a.object == b.object) return true;
    if ((a.kind == ScopeKind::Class || a.kind == ScopeKind::Act) && (b.kind == ScopeKind::Placement || b.kind == ScopeKind::Field)) {
        auto it = state_.document.placements.find(b.object);
        return it != state_.document.placements.end() && (a.kind == ScopeKind::Class ? it->second.class_id : it->second.act_id) == a.object;
    }
    return false;
}
bool Coordinator::overlaps(const Scope& a, const Scope& b) const
{
    if (covers(a, b) || covers(b, a)) return true;
    if ((a.kind == ScopeKind::Class && b.kind == ScopeKind::Act) || (a.kind == ScopeKind::Act && b.kind == ScopeKind::Class)) {
        for (const auto& [id, p] : state_.document.placements) { (void)id; if (p.class_id == (a.kind == ScopeKind::Class ? a.object : b.object) && p.act_id == (a.kind == ScopeKind::Act ? a.object : b.object)) return true; }
    }
    // Deleted placements can be restored by Undo. Until their native class/act
    // context is restored, conservatively conflict with class and act writers.
    auto unknown = [&](const Scope& x, const Scope& y) {
        return (x.kind == ScopeKind::Class || x.kind == ScopeKind::Act) && (y.kind == ScopeKind::Placement || y.kind == ScopeKind::Field) && !state_.document.placements.contains(y.object);
    };
    return unknown(a, b) || unknown(b, a);
}
LeaseResult Coordinator::acquire(Peer p, const std::vector<Scope>& scopes, Time now, Time ttl)
{
    auto bad = [](Code code, const char* reason) { return LeaseResult{code, reason, {}}; };
    if (busy_) return bad(Code::Busy, "A transaction is being applied.");
    if (frozen_) return bad(Code::Frozen, "Native state requires checkpoint recovery.");
    if (!peer(p)) return bad(Code::StalePeer, "The peer capability is no longer connected.");
    if (!clock(now) || !ttl || ttl > limits_.max_lease_ms || now > UINT64_MAX - ttl || scopes.empty() || scopes.size() > limits_.leases) return bad(Code::Invalid, "Invalid lease request or monotonic time.");
    std::set<Scope> unique;
    for (const auto& scope : scopes) {
        if (!scope_valid(scope) || !unique.insert(scope).second) return bad(Code::Invalid, "Invalid or duplicate lease scope.");
        for (const auto& held : leases_) if (held.owner != p && overlaps(scope, held.scope)) return bad(Code::Conflict, "An overlapping editing scope belongs to another player.");
    }
    auto next = leases_; auto token = next_token_; std::vector<Lease> granted;
    for (const auto& scope : scopes) {
        auto it = std::find_if(next.begin(), next.end(), [&](const Lease& l) { return l.owner == p && l.scope == scope; });
        if (it != next.end()) { it->expires = now + ttl; granted.push_back(*it); }
        else {
            if (next.size() >= limits_.leases || !token || token == UINT64_MAX) return bad(Code::Limit, "The lease table or token space is full.");
            Lease l{scope, p, token++, now + ttl}; next.push_back(l); granted.push_back(l);
        }
    }
    leases_ = std::move(next); next_token_ = token; return {Code::Accepted, {}, std::move(granted)};
}
LeaseResult Coordinator::renew(Peer p, const std::vector<LeaseToken>& tokens, Time now, Time ttl)
{
    if (busy_) return {Code::Busy, "A transaction is being applied.", {}};
    if (!peer(p)) return {Code::StalePeer, "The peer capability is no longer connected.", {}};
    if (frozen_) return {Code::Frozen, "Native state requires checkpoint recovery.", {}};
    if (!clock(now) || !ttl || ttl > limits_.max_lease_ms || now > UINT64_MAX - ttl || tokens.empty() || tokens.size() > limits_.leases) return {Code::Invalid, "Invalid renewal request.", {}};
    std::set<Scope> unique; std::vector<size_t> indices;
    for (const auto& token : tokens) {
        if (!unique.insert(token.scope).second) return {Code::Invalid, "Duplicate renewal scope.", {}};
        auto it = std::find_if(leases_.begin(), leases_.end(), [&](const Lease& l) { return l.owner == p && l.scope == token.scope && l.token == token.token; });
        if (it == leases_.end()) return {Code::LeaseExpired, "A lease expired or was replaced; nothing was renewed.", {}};
        indices.push_back(size_t(it - leases_.begin()));
    }
    std::vector<Lease> renewed;
    for (size_t i : indices) { leases_[i].expires = now + ttl; renewed.push_back(leases_[i]); }
    return {Code::Accepted, {}, std::move(renewed)};
}
bool Coordinator::release(Peer p, const std::vector<LeaseToken>& tokens)
{
    if (busy_ || !peer(p) || tokens.empty() || tokens.size() > limits_.leases) return false;
    std::set<Scope> unique;
    for (const auto& token : tokens) {
        if (!unique.insert(token.scope).second || std::none_of(leases_.begin(), leases_.end(), [&](const Lease& l) { return l.owner == p && l.scope == token.scope && l.token == token.token; })) return false;
    }
    std::erase_if(leases_, [&](const Lease& l) { return l.owner == p && std::any_of(tokens.begin(), tokens.end(), [&](const LeaseToken& t) { return t.scope == l.scope && t.token == l.token; }); });
    return true;
}
Code Coordinator::leased(const Request& r, const Scope& needed, Time now) const
{
    for (const auto& token : r.leases)
        for (const auto& l : leases_)
            if (l.owner == r.operation.author && l.scope == token.scope && l.token == token.token && l.expires > now && covers(l.scope, needed)) return Code::Accepted;
    return r.leases.empty() ? Code::LeaseRequired : Code::LeaseExpired;
}

void Coordinator::trim(State& s) const
{
    while (!s.history.empty() && (s.history.size() > limits_.history || history_bytes(s) > limits_.history_bytes)) {
        s.history_truncated.insert(s.history.front().operation.author); s.history.pop_front();
    }
    while (!s.decision_order.empty() && (s.decisions.size() > limits_.decisions || decisions_bytes(s) > limits_.decision_bytes)) {
        auto id = s.decision_order.front(); s.decision_order.pop_front(); s.decisions.erase(id);
        if (id.epoch == s.document.epoch) s.retired_sequences[id.author] = std::max(s.retired_sequences[id.author], id.sequence);
    }
    // Peers are capabilities; old peer generations cannot submit again. Keep
    // retirement/truncation metadata bounded without making a current peer replay.
    while (s.retired_sequences.size() > 4096) {
        auto old = std::find_if(s.retired_sequences.begin(), s.retired_sequences.end(), [&](const auto& entry) { return !peer(entry.first); });
        if (old == s.retired_sequences.end()) break;
        s.retired_sequences.erase(old);
    }
    while (s.history_truncated.size() > 4096) {
        auto old = std::find_if(s.history_truncated.begin(), s.history_truncated.end(), [&](Peer p) { return !peer(p); });
        if (old == s.history_truncated.end()) break;
        s.history_truncated.erase(old);
    }
}
void Coordinator::remember(State& s, OperationId id, const Decision& d) const
{
    if (s.decisions.contains(id)) return;
    s.decisions.emplace(id, d); s.decision_order.push_back(id); trim(s);
}

namespace {

bool references(const Document& d)
{
    for (const auto& [id, p] : d.placements) {
        (void)id;
        for (const auto& [property, v] : p.properties) {
            (void)property;
            if (auto* ref = std::get_if<ObjectId>(&v); ref && identity(*ref) && !d.placements.contains(*ref)) return false;
        }
    }
    for (const auto& ref : d.external_references) if (!d.placements.contains(ref.target)) return false;
    return true;
}

// Produces actual canonical before/after data and an inverse whose preconditions
// refer to the newly committed field/tombstone revisions. No live state is edited.
Code apply_patch(const Document& before, const Patch& input, const Limits& limits,
                 Document& after, Patch& canonical, Patch& inverse, std::string& error)
{
    auto bad = [&](Code code, const char* why) { error = why; return code; };
    if (!patch_shape(input, before, limits)) return bad(Code::Invalid, "Malformed or oversized semantic patch.");
    if (input.objects.empty() && input.fields.empty()) return bad(Code::Invalid, "An empty gesture is not an edit.");
    for (const auto& c : input.dependencies) if (!check(before, c)) return bad(Code::Conflict, "An explicit dependency changed or disappeared.");
    for (const auto& e : input.objects) {
        auto it = before.placements.find(e.object);
        if (e.before) {
            if (it == before.placements.end()) return bad(Code::MissingObject, "The placement no longer exists.");
            if (it->second.revision != e.expected || it->second != *e.before) return bad(Code::Conflict, "The placement changed; deletion/restoration cannot overwrite it.");
            if (it->second.protected_object) return bad(Code::Protected, "The required captain/avatar placement cannot be removed.");
        } else {
            if (it != before.placements.end() || absent_revision(before, e.object) != e.expected) return bad(Code::Conflict, "The placement identity was restored or its absence changed.");
            if (e.after->protected_object) return bad(Code::Protected, "A protected captain/avatar cannot be created or cloned.");
            if (!before.tombstones.contains(e.object) && e.object.id < before.next_id) return bad(Code::Conflict, "An allocated identity cannot be reused.");
        }
    }
    for (const auto& e : input.fields) {
        auto it = before.placements.find(e.object);
        if (it == before.placements.end()) return bad(Code::MissingObject, "The edited placement no longer exists.");
        if (field_revision(it->second, e.field) != e.expected || value(it->second, e.field) != e.before) return bad(Code::Conflict, "The edited field changed; no older action was selected.");
        if (e.before == e.after) return bad(Code::Invalid, "A no-op field change is not a gesture.");
    }
    after = before; after.revision++;
    canonical = input; inverse = {};
    for (auto& e : canonical.objects) {
        if (e.after) {
            Placement p = *e.after; stamp(p, after.revision); e.after = p;
            after.placements[e.object] = p; after.tombstones.erase(e.object);
            if (e.object.id >= after.next_id) {
                if (e.object.id >= UINT64_MAX - 1) return bad(Code::Limit, "The stable ID allocator is exhausted.");
                after.next_id = e.object.id + 1;
            }
        } else { after.placements.erase(e.object); after.tombstones[e.object] = after.revision; }
        inverse.objects.push_back({e.object, e.after, e.before, after.revision});
    }
    for (auto& e : canonical.fields) {
        Revision old_object_revision = before.placements.at(e.object).revision;
        set(after.placements.at(e.object), e.field, e.after, after.revision);
        inverse.fields.push_back({e.object, e.field, e.after, e.before, after.revision, e.expected, old_object_revision});
    }
    if (!references(after)) return bad(Code::Referenced, "Deleting this placement would orphan a placement or objective reference.");
    if (after.placements.size() > limits.placements || after.tombstones.size() > limits.tombstones || bytes(after) > limits.document_bytes)
        return bad(Code::Limit, "The candidate document exceeds a configured bound.");
    if (!valid(after, limits, error)) return Code::Invalid;
    inverse.dependencies = input.dependencies;
    for (auto& c : inverse.dependencies) {
        auto it = after.placements.find(c.object);
        if (it == after.placements.end()) { c.kind = CheckKind::Absent; c.expected = absent_revision(after, c.object); }
        else if (c.kind == CheckKind::Field) c.expected = field_revision(it->second, c.field);
        else { c.kind = CheckKind::Object; c.expected = it->second.revision; }
    }
    return Code::Accepted;
}

bool same_placement_values(Placement a, Placement b)
{
    a.revision = b.revision = 0; a.field_revisions.clear(); b.field_revisions.clear(); return a == b;
}

// Undo restores a historical logical state at a NEW revision. Refresh only the
// same author's causal preconditions that name that exact old state. This makes
// ordinary multi-step Undo/Redo work without rebasing over other players' edits.
void refresh_history(State& state, const Change& change, const Patch& requested)
{
    struct Restored { ObjectId object; FieldKey field; Revision field_revision, object_revision; bool whole; };
    std::vector<Restored> restored;
    for (const auto& e : change.patch.fields) restored.push_back({e.object, e.field, e.restored_revision, e.restored_object_revision, false});
    for (const auto& e : requested.objects) {
        if (!e.after) continue;
        std::set<FieldKey> fields{{Field::Position, 0}, {Field::Rotation, 0}, {Field::Scale, 0}};
        for (const auto& [field, revision] : e.after->field_revisions) { (void)revision; fields.insert(field); }
        for (const auto& [property, v] : e.after->properties) { (void)v; fields.insert({Field::Property, property}); }
        for (auto field : fields) restored.push_back({e.object, field, field_revision(*e.after, field), e.after->revision, true});
    }
    for (auto& g : state.history) {
        if (g.operation == change.gesture || g.operation.author != change.operation.author) continue;
        for (Patch* patch : {&g.undo, &g.redo}) {
            for (auto& e : patch->fields) {
                auto live = state.document.placements.find(e.object);
                if (live == state.document.placements.end() || value(live->second, e.field) != e.before) continue;
                for (const auto& r : restored)
                    if (r.object == e.object && r.field == e.field && e.expected == r.field_revision) e.expected = state.document.revision;
            }
            for (auto& e : patch->objects) {
                auto live = state.document.placements.find(e.object);
                if (!e.before || live == state.document.placements.end() || !same_placement_values(*e.before, live->second)) continue;
                for (const auto& r : restored)
                    if (r.object == e.object && e.expected == r.object_revision) { e.expected = state.document.revision; e.before = live->second; }
            }
            for (auto& dependency : patch->dependencies) {
                auto live = state.document.placements.find(dependency.object);
                if (live == state.document.placements.end()) continue;
                for (const auto& r : restored) {
                    if (r.object != dependency.object) continue;
                    if (dependency.kind == CheckKind::Field && dependency.field == r.field && dependency.expected == r.field_revision) dependency.expected = field_revision(live->second, r.field);
                    if (r.whole && dependency.kind == CheckKind::Object && dependency.expected == r.object_revision) dependency.expected = live->second.revision;
                }
            }
        }
    }
}

} // namespace

Decision Coordinator::submit(const Request& r, Time now, const Hooks& hooks)
{
    const Revision revision = state_.document.revision;
    auto reject = [&](Code code, const std::string& why, bool retain = true) {
        Decision d = failure(code, why, revision);
        if (retain && identity(r.operation) && r.operation.epoch == state_.document.epoch && peer(r.operation.author)) remember(state_, r.operation, d);
        return d;
    };
    if (busy_) return reject(Code::Busy, "A native/durable transaction is already being applied.", false);
    if (!ready_ || r.project != state_.document.project) return reject(Code::StaleProject, "This operation belongs to a different project.", false);
    if (r.epoch != state_.document.epoch || r.operation.epoch != r.epoch) return reject(Code::StaleEpoch, "This operation belongs to a different session epoch.", false);
    if (!peer(r.operation.author)) return reject(Code::StalePeer, "The peer capability expired or was replaced.", false);
    if (!identity(r.operation)) return reject(Code::Invalid, "An operation needs a nonzero sequence and author capability.", false);
    if (auto found = state_.decisions.find(r.operation); found != state_.decisions.end()) return found->second;
    auto retired = state_.retired_sequences.find(r.operation.author);
    if (retired != state_.retired_sequences.end() && r.operation.sequence <= retired->second) return reject(Code::ExpiredOperation, "The original decision expired; this operation will not execute again.", false);
    if (frozen_) return reject(Code::Frozen, "Native state requires checkpoint recovery.");
    if (!clock(now)) return reject(Code::Invalid, "Host monotonic time moved backwards.");
    if (r.base_revision > revision) return reject(Code::StaleRevision, "The operation expects a future document revision.");
    if (revision >= UINT64_MAX - 1) return reject(Code::Limit, "The document revision space is exhausted.");
    if (r.intent > Intent::Redo || r.summary.size() > limits_.summary_bytes || !utf8(r.summary) || r.mutations.size() > limits_.mutations ||
        r.dependencies.size() > limits_.dependencies || r.leases.size() > limits_.leases) return reject(Code::Invalid, "Invalid gesture envelope or configured bound.");
    std::set<Scope> supplied;
    for (const auto& l : r.leases) if (!l.token || !scope_valid(l.scope) || !supplied.insert(l.scope).second) return reject(Code::Invalid, "Invalid or duplicate lease token.");
    Patch patch; std::vector<ObjectId> allocated; std::map<ObjectId, ObjectId> clone_sources; std::optional<size_t> gesture_index;
    size_t transient_dependencies = 0;
    if (r.intent == Intent::Forward) {
        if (r.mutations.empty()) return reject(Code::Invalid, "A forward gesture requires mutations.");
        uint64_t next_id = state_.document.next_id;
        for (const auto& m : r.mutations) {
            if (m.kind > MutationKind::Set) return reject(Code::Invalid, "Unknown mutation kind.");
            if (m.kind != MutationKind::Clone && m.clone_pose) return reject(Code::Invalid, "Only cloning may provide a final clone pose.");
            if (m.kind != MutationKind::Create && m.initial != Placement{}) return reject(Code::Invalid, "Only creation may provide initial placement data.");
            if (m.kind != MutationKind::Set && (m.value || m.field != FieldKey{})) return reject(Code::Invalid, "Only field changes may provide a value or field selector.");
            if (m.kind == MutationKind::Create || m.kind == MutationKind::Clone) {
                auto code = leased(r, {}, now);
                if (code != Code::Accepted) return reject(code, "Creating or cloning requires the document structure lease.");
                Placement p;
                if (m.kind == MutationKind::Create) {
                    if (m.target != ObjectId{} || m.expected || m.initial.object != ObjectId{} || m.initial.revision || !m.initial.field_revisions.empty() || m.initial.protected_object)
                        return reject(Code::Invalid, "A new placement must not supply identity, versions or protected flags.");
                    p = m.initial;
                } else {
                    auto it = state_.document.placements.find(m.target);
                    if (it == state_.document.placements.end()) return reject(Code::MissingObject, "The clone source is missing.");
                    if (it->second.revision != m.expected) return reject(Code::Conflict, "The clone source changed.");
                    if (it->second.protected_object) return reject(Code::Protected, "The required captain/avatar cannot be cloned.");
                    p = it->second;
                    if (m.clone_pose) { p.position = m.clone_pose->position; p.rotation = m.clone_pose->rotation; p.scale = m.clone_pose->scale; }
                    patch.dependencies.push_back({m.target, CheckKind::Object, {}, m.expected});
                    transient_dependencies++;
                }
                if (next_id >= UINT64_MAX - 1) return reject(Code::Limit, "The stable ID allocator is exhausted.");
                p.object = {next_id++, 1}; p.revision = 0; p.field_revisions.clear();
                if (m.kind == MutationKind::Clone) clone_sources[p.object] = m.target;
                allocated.push_back(p.object); patch.objects.push_back({p.object, std::nullopt, p, 0});
            } else {
                auto it = state_.document.placements.find(m.target);
                if (it == state_.document.placements.end()) return reject(Code::MissingObject, "The edited placement is missing or belongs to an old generation.");
                if (m.kind == MutationKind::Delete) {
                    auto code = leased(r, {ScopeKind::Placement, m.target, {}}, now);
                    if (code != Code::Accepted) return reject(code, "Deleting requires its placement lease.");
                    if (it->second.revision != m.expected) return reject(Code::Conflict, "The placement changed before deletion.");
                    patch.objects.push_back({m.target, it->second, std::nullopt, m.expected});
                } else {
                    if (!field_value(m.field, m.value, state_.document, limits_)) return reject(Code::Unsupported, "The property or transform is not registered, typed and valid.");
                    auto code = leased(r, {ScopeKind::Field, m.target, m.field}, now);
                    if (code != Code::Accepted) return reject(code, "Editing requires a lease covering this field.");
                    patch.fields.push_back({m.target, m.field, value(it->second, m.field), m.value, m.expected});
                }
            }
        }
        patch.dependencies.insert(patch.dependencies.end(), r.dependencies.begin(), r.dependencies.end());
    } else {
        if (!r.mutations.empty()) return reject(Code::Invalid, "Undo/Redo cannot select or supply forward mutations.");
        for (size_t i = 0; i < state_.history.size(); ++i) {
            const auto& g = state_.history[i];
            if (g.operation.author != r.operation.author) continue;
            if (r.intent == Intent::Undo && g.applied && (!gesture_index || g.original_revision > state_.history[*gesture_index].original_revision)) gesture_index = i;
            if (r.intent == Intent::Redo && !g.applied && g.redo_available && (!gesture_index || g.transition_revision > state_.history[*gesture_index].transition_revision)) gesture_index = i;
        }
        if (!gesture_index) {
            if (state_.history_truncated.contains(r.operation.author)) return reject(Code::HistoryUnavailable, "Older author history exceeded retention limits.");
            return reject(r.intent == Intent::Undo ? Code::NothingToUndo : Code::NothingToRedo, "There is no eligible original gesture at this author history cursor.");
        }
        const auto& g = state_.history[*gesture_index];
        patch = r.intent == Intent::Undo ? g.undo : g.redo;
        patch.dependencies.insert(patch.dependencies.end(), r.dependencies.begin(), r.dependencies.end());
        for (const auto& e : patch.objects) {
            auto code = leased(r, {ScopeKind::Placement, e.object, {}}, now);
            if (code != Code::Accepted) return reject(code, "Undo/Redo requires its placement lease.");
        }
        for (const auto& e : patch.fields) {
            auto code = leased(r, {ScopeKind::Field, e.object, e.field}, now);
            if (code != Code::Accepted) return reject(code, "Undo/Redo requires its edited-field lease.");
        }
    }
    State candidate = state_;
    Change change; change.operation = r.operation; change.intent = r.intent;
    change.gesture = gesture_index ? state_.history[*gesture_index].operation : r.operation;
    change.summary = gesture_index ? state_.history[*gesture_index].summary : r.summary;
    change.allocated = std::move(allocated);
    change.clone_sources = std::move(clone_sources);
    std::string error;
    Code code = apply_patch(state_.document, patch, limits_, candidate.document, change.patch, change.inverse, error);
    if (code != Code::Accepted) return reject(code, error);
    // The clone source is needed to create the clone, not to remove it on Undo.
    // Explicit caller dependency guards remain reversible and checked.
    if (r.intent == Intent::Forward && transient_dependencies)
        change.inverse.dependencies.erase(change.inverse.dependencies.begin(), change.inverse.dependencies.begin() + transient_dependencies);
    change.revision = candidate.document.revision;
    if (!gesture_index) {
        for (auto& g : candidate.history) if (g.operation.author == r.operation.author && !g.applied) g.redo_available = false;
        Gesture g; g.operation = r.operation; g.original_revision = g.transition_revision = change.revision; g.summary = r.summary; g.undo = change.inverse;
        candidate.history.push_back(std::move(g));
    } else {
        auto& g = candidate.history[*gesture_index]; g.transition_revision = change.revision;
        if (r.intent == Intent::Undo) { g.applied = false; g.redo_available = true; g.redo = change.inverse; }
        else { g.applied = true; g.redo_available = false; g.undo = change.inverse; }
    }
    if (r.intent != Intent::Forward) refresh_history(candidate, change, patch);
    Decision accepted{Code::Accepted, {}, change.revision, change};
    remember(candidate, r.operation, accepted);
    // Never acknowledge an edit whose own dedup decision cannot fit. Otherwise an
    // immediate retry could be mistaken for a new operation after memory eviction.
    if (!candidate.decisions.contains(r.operation)) return reject(Code::Limit, "This transaction exceeds the retained decision budget.");
    struct Guard { bool& busy; explicit Guard(bool& b) : busy(b) { busy = true; } ~Guard() { busy = false; } } guard(busy_);
    bool applied = false;
    auto restore = [&]() {
        if (!applied) return true;
        std::string why;
        bool ok = false;
        try { ok = hooks.rollback && hooks.rollback(state_.document, candidate.document, change, why); } catch (...) { ok = false; }
        if (!ok) frozen_ = true;
        return ok;
    };
    auto native_failure = [&](Code failed, const std::string& why) {
        bool restored = restore();
        return reject(restored ? failed : Code::Frozen, restored ? why : "Native rollback failed; shared editing is frozen for checkpoint recovery.");
    };
    bool persisting = false;
    try {
        if (hooks.validate && !hooks.validate(state_.document, candidate.document, change, error)) return reject(Code::NativeRejected, error.empty() ? "Native validation rejected the gesture." : error);
        if (hooks.apply) {
            applied = true;
            if (!hooks.apply(state_.document, candidate.document, change, error)) return native_failure(Code::NativeFailed, error.empty() ? "Native application failed." : error);
        }
        persisting = true;
        if (hooks.persist && !hooks.persist(candidate, change, error)) return native_failure(Code::PersistenceFailed, error.empty() ? "The durable commit could not be flushed." : error);
    } catch (...) {
        auto decision = native_failure(persisting ? Code::PersistenceFailed : applied ? Code::NativeFailed : Code::NativeRejected, "A transaction hook threw; the candidate was not committed.");
        if (persisting) {
            frozen_ = true;
            decision = failure(Code::Frozen, "Durable commit status is ambiguous after a persistence exception; recover a checkpoint.", revision);
            if (auto remembered = state_.decisions.find(r.operation); remembered != state_.decisions.end()) remembered->second = decision;
        }
        return decision;
    }
    state_ = std::move(candidate);
    // Deleting a placement invalidates outstanding field/placement capabilities;
    // Undo restoration must explicitly reacquire one, even for the original author.
    std::erase_if(leases_, [&](const Lease& lease) {
        return (lease.scope.kind == ScopeKind::Placement || lease.scope.kind == ScopeKind::Field) && !state_.document.placements.contains(lease.scope.object);
    });
    return accepted;
}

bool apply_committed(State& state, const Decision& decision, std::string& error, const Hooks& hooks, Limits limits)
{
    auto bad = [&](const char* why) { error = why; return false; };
    if (!decision.accepted() || !decision.change || decision.revision != decision.change->revision) return bad("A replica applies only canonical host acceptances.");
    const auto& change = *decision.change;
    if (change.operation.epoch != state.document.epoch) return bad("The host commit belongs to a stale epoch.");
    if (auto remembered = state.decisions.find(change.operation); remembered != state.decisions.end()) {
        if (remembered->second != decision) return bad("The host reused an operation ID with a different decision.");
        error.clear(); return true;
    }
    if (decision.revision != state.document.revision + 1 || state.document.revision >= UINT64_MAX - 1) return bad("A host commit is missing, stale or out of order; a baseline is required.");
    Coordinator replay(limits);
    if (!replay.reset(state, state.document.epoch, error) || !replay.connect(change.operation.author)) return false;
    auto lease = replay.acquire(change.operation.author, {{}}, 0, 1);
    if (!lease.accepted()) return bad("Cannot stage the replica transaction lease.");
    Request request; request.project = state.document.project; request.epoch = state.document.epoch;
    request.operation = change.operation; request.base_revision = state.document.revision;
    request.intent = change.intent; request.summary = change.summary;
    request.leases.push_back({lease.leases[0].scope, lease.leases[0].token});
    if (change.intent == Intent::Forward) {
        for (const auto& e : change.patch.objects) {
            Mutation m;
            if (e.after && !e.before) {
                if (auto source = change.clone_sources.find(e.object); source != change.clone_sources.end()) {
                    auto original = state.document.placements.find(source->second);
                    if (original == state.document.placements.end()) return bad("The canonical clone source is unavailable.");
                    m.kind = MutationKind::Clone; m.target = source->second; m.expected = original->second.revision;
                    m.clone_pose = Pose{e.after->position, e.after->rotation, e.after->scale};
                } else {
                    m.kind = MutationKind::Create; m.initial = *e.after; m.initial.object = {};
                    m.initial.revision = 0; m.initial.field_revisions.clear();
                }
            } else if (e.before && !e.after) { m.kind = MutationKind::Delete; m.target = e.object; m.expected = e.expected; }
            else return bad("A canonical structural patch is malformed.");
            request.mutations.push_back(std::move(m));
        }
        for (const auto& e : change.patch.fields) {
            Mutation m; m.target = e.object; m.field = e.field; m.value = e.after; m.expected = e.expected;
            request.mutations.push_back(std::move(m));
        }
        if (change.patch.dependencies.size() < change.clone_sources.size()) return bad("The clone source dependency prefix is missing.");
        request.dependencies.assign(change.patch.dependencies.begin() + change.clone_sources.size(), change.patch.dependencies.end());
    } else {
        auto original = std::find_if(state.history.begin(), state.history.end(), [&](const Gesture& g) { return g.operation == change.gesture; });
        if (original == state.history.end()) return bad("The host commit references original-gesture history outside retention.");
        const Patch& stored = change.intent == Intent::Undo ? original->undo : original->redo;
        if (change.patch.dependencies.size() < stored.dependencies.size()) return bad("The host omitted retained dependency guards.");
        request.dependencies.assign(change.patch.dependencies.begin() + stored.dependencies.size(), change.patch.dependencies.end());
    }
    Hooks checked = hooks;
    checked.validate = [&](const Document& before, const Document& after, const Change& generated, std::string& why) {
        if (generated != change) { why = "Host patch, inverse, IDs or history transition is not canonical."; return false; }
        return !hooks.validate || hooks.validate(before, after, generated, why);
    };
    auto result = replay.submit(request, 0, checked);
    if (!result.accepted() || result != decision) { error = result.reason.empty() ? "The host commit failed replica validation." : result.reason; return false; }
    state = replay.state(); error.clear(); return true;
}

} // namespace editor
