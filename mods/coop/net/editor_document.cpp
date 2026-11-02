#include "editor_document.h"
#include "editor_terrain.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>

namespace editor {
namespace {

bool identity(ObjectId id) { return id.id && id.generation; }
bool identity(Peer p) { return p.slot < 4 && p.generation; }
bool identity(OperationId op) { return identity(op.author) && op.sequence && op.epoch; }
bool terrain_shape(const TerrainRecord& row,const Limits& limits){
    return identity(row.object) && row.kind<=TerrainKind::Model && row.order &&
        row.payload.bytes.size()<=limits.structured_bytes && valid_terrain_payload(row.kind,row.payload);
}
const Placement* retained_placement(const State& state,ObjectId id){
    auto found=state.document.placements.find(id);
    if(found!=state.document.placements.end())return &found->second;
    for(auto g=state.history.rbegin();g!=state.history.rend();++g)for(const auto* patch:{&g->undo,&g->redo})
        for(const auto& e:patch->objects)if(e.object==id){if(e.after)return &*e.after;if(e.before)return &*e.before;}
    return nullptr;
}
const TerrainRecord* retained_terrain(const State& state,ObjectId id){
    auto found=state.document.terrain.find(id);if(found!=state.document.terrain.end())return &found->second;
    for(auto g=state.history.rbegin();g!=state.history.rend();++g)for(const auto* patch:{&g->undo,&g->redo})
        for(const auto& e:patch->terrain)if(e.object==id){if(e.after)return &*e.after;if(e.before)return &*e.before;}
    return nullptr;
}
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
    if (auto* s = std::get_if<StructuredValue>(&v)) return 64 + s->bytes.size() + s->references.size()*16;
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
    case ValueKind::Position: return position(std::get<Vec3>(v));
    case ValueKind::Rotation: return rotation(std::get<Rotation>(v));
    case ValueKind::Number: { float x = std::get<float>(v); return finite(x) && x >= spec.minimum && x <= spec.maximum; }
    case ValueKind::Boolean: return true;
    case ValueKind::Integer: { auto x = std::get<int64_t>(v); return static_cast<long double>(x) >= spec.minimum && static_cast<long double>(x) <= spec.maximum; }
    case ValueKind::Text: { const auto& s = std::get<std::string>(v); return s.size() <= std::min<size_t>(spec.max_text, limits.text_bytes) && utf8(s); }
    case ValueKind::Reference: { auto id = std::get<ObjectId>(v); return identity(id) || (spec.nullable && id == ObjectId{}); }
    case ValueKind::Resource: { auto k = std::get<ResourceKey>(v); return (k.instance && k.type) || (spec.nullable && k == ResourceKey{}); }
    case ValueKind::Structured: {
        const auto& data=std::get<StructuredValue>(v);
        if(!data.format || data.bytes.empty() || data.bytes.size()>limits.structured_bytes || data.references.size()>limits.dependencies)return false;
        std::set<ObjectId> refs;
        for(auto id:data.references)if(!identity(id) || !refs.insert(id).second)return false;
        return true;
    }
    default: return false;
    }
}
bool placement_shape(const Placement& p, const Document& d, const Limits& limits, bool versions)
{
    if (!identity(p.object) || (!d.classes.contains(p.class_id) && !(versions && d.tombstones.contains(p.class_id))) || (!d.acts.contains(p.act_id) && !(versions && d.tombstones.contains(p.act_id))) ||
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
bool field_value(FieldKey k, const std::optional<Value>& v, const Document& d, const Limits& limits, ObjectId object = {})
{
    if (!field_key(k) || d.terrain.contains(object)) return false;
    if(d.settings.contains(object) || (d.tombstones.contains(object) && k.field==Field::Property && d.settings_schema.contains(k.property))){
        if(k.field!=Field::Property)return false;
        auto spec=d.settings_schema.find(k.property);
        return spec!=d.settings_schema.end() && (v?typed(*v,spec->second,limits):spec->second.optional);
    }
    const bool settings = d.class_settings.contains(object) || (!d.placements.contains(object) && k.field==Field::Property && d.class_schema.contains(k.property));
    if (settings && k.field != Field::Property) return false;
    if (k.field == Field::Property) {
        const auto& schema = settings ? d.class_schema : d.schema;
        auto it = schema.find(k.property);
        return it != schema.end() && (v ? typed(*v, it->second, limits) : it->second.optional);
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
bool editable(const Document& d, ObjectId id) {
    return d.placements.contains(id) || d.class_settings.contains(id) || d.settings.contains(id) || d.terrain.contains(id);
}
Scope field_scope(const Document& d,ObjectId id,FieldKey field){
    if(auto s=d.settings.find(id);s!=d.settings.end())return s->second.kind==SettingsKind::Project || s->second.kind==SettingsKind::Planet?
        Scope{ScopeKind::Field,id,field}:Scope{ScopeKind::Settings,id,{}};
    auto c=d.class_settings.find(id);
    if(c!=d.class_settings.end() && field==FieldKey{Field::Property,kClassAssetResourceProperty})return {ScopeKind::Content,c->second.class_id,{}};
    return c==d.class_settings.end()?Scope{ScopeKind::Field,id,field}:
        Scope{ScopeKind::Class,c->second.class_id,{}};
}
Revision object_revision(const Document& d, ObjectId id) {
    auto t=d.terrain.find(id);if(t!=d.terrain.end())return t->second.revision;
    auto s=d.settings.find(id);if(s!=d.settings.end())return s->second.revision;
    auto p=d.placements.find(id);
    if(p!=d.placements.end())return p->second.revision;
    auto c=d.class_settings.find(id);
    return c==d.class_settings.end()?0:c->second.revision;
}
void set(Document& d, ObjectId id, FieldKey field, const std::optional<Value>& v, Revision revision) {
    if(auto s=d.settings.find(id);s!=d.settings.end()){
        if(v)s->second.properties[field.property]=*v;else s->second.properties.erase(field.property);
        s->second.field_revisions[field]=revision;s->second.revision=revision;return;
    }
    auto p=d.placements.find(id);
    if(p!=d.placements.end()){set(p->second,field,v,revision);return;}
    auto& c=d.class_settings.at(id);
    if(v)c.properties[field.property]=*v;else c.properties.erase(field.property);
    c.field_revisions[field]=revision;c.revision=revision;
}
bool check(const Document& d, const Check& c)
{
    if (!identity(c.object)) return false;
    if (c.kind == CheckKind::Absent) return !editable(d,c.object) && !d.classes.contains(c.object) && !d.acts.contains(c.object) && absent_revision(d, c.object) == c.expected;
    if (!editable(d,c.object)) return false;
    if (c.kind == CheckKind::Object) return object_revision(d,c.object) == c.expected;
    return !d.terrain.contains(c.object) && c.kind == CheckKind::Field && field_key(c.field) &&
        ((!d.class_settings.contains(c.object) && !d.settings.contains(c.object)) || c.field.field==Field::Property) && field_revision(d,c.object,c.field) == c.expected;
}
bool patch_shape(const Patch& p, const Document& d, const Limits& l)
{
    if(p.acts){
        if(p.acts->expected>d.revision || p.acts->restored_revision>d.revision)return false;
        for(const auto* list:{&p.acts->before,&p.acts->after}){
            if(list->empty() || list->size()>l.acts)return false;
            std::set<ObjectId> ids;
            for(const auto& row:*list){
                if(!identity(row.object) || !identity(row.act_id) || row.kind!=SettingsKind::Act ||
                   !ids.insert(row.object).second || !ids.insert(row.act_id).second || row.revision>d.revision ||
                   row.properties.size()>l.properties || row.field_revisions.size()>d.settings_schema.size())return false;
                for(const auto& [key,v]:row.properties){auto spec=d.settings_schema.find(key);if(spec==d.settings_schema.end() || !typed(v,spec->second,l))return false;}
                for(const auto& [field,rev]:row.field_revisions)if(field.field!=Field::Property || !d.settings_schema.contains(field.property) || rev>row.revision)return false;
            }
        }
    }
    if(p.acts && (!p.objects.empty() || !p.classes.empty() || !p.terrain.empty() || std::any_of(p.fields.begin(),p.fields.end(),[&](const auto& field){
        return !d.class_settings.contains(field.object) && !(d.tombstones.contains(field.object) && field.field.field==Field::Property && d.class_schema.contains(field.field.property));
    })))return false;
    if (!patch_count_valid(p,l) || p.dependencies.size() > l.dependencies) return false;
    std::set<ObjectId> objects;
    std::set<std::pair<ObjectId, FieldKey>> fields;
    for(const auto& e:p.terrain){
        if(!identity(e.object) || (!e.before && !e.after) || !objects.insert(e.object).second || e.expected>d.revision || e.restored_revision>d.revision)return false;
        for(const auto* item:{&e.before,&e.after})if(*item && ((**item).object!=e.object || !terrain_shape(**item,l) || (**item).revision>d.revision))return false;
        if(e.before && e.after && (e.before->order!=e.after->order || e.before->kind!=e.after->kind))return false;
    }
    for(const auto& e:p.classes){
        if(!identity(e.object) || bool(e.before)==bool(e.after) || !objects.insert(e.object).second || e.expected>d.revision)return false;
        for(const auto* item:{&e.before,&e.after})if(*item){
            const auto& c=**item;
            if(!identity(c.object) || c.class_id!=e.object || (!d.acts.contains(c.act_id) && !d.tombstones.contains(c.act_id)) || c.revision>d.revision || c.properties.size()>l.properties)return false;
            for(const auto& [key,v]:c.properties){auto spec=d.class_schema.find(key);if(spec==d.class_schema.end() || !typed(v,spec->second,l))return false;}
            for(const auto& [key,spec]:d.class_schema)if(!spec.optional && !c.properties.contains(key))return false;
        }
    }
    for (const auto& e : p.objects) {
        if (!identity(e.object) || bool(e.before) == bool(e.after) || !objects.insert(e.object).second || e.expected > d.revision) return false;
        for (const auto* v : {&e.before, &e.after})
            if (*v && ((*v)->object != e.object || !placement_shape(**v, d, l, true))) return false;
    }
    for (const auto& e : p.fields)
        if (!identity(e.object) || objects.contains(e.object) || !fields.insert({e.object, e.field}).second ||
            e.expected > d.revision || e.restored_revision > d.revision || e.restored_object_revision > d.revision ||
            !field_value(e.field, e.before, d, l,e.object) || !field_value(e.field, e.after, d, l,e.object)) return false;
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

bool mutation_count_valid(const std::vector<Mutation>& mutations,const Limits& limits){
    if(mutations.size()<=limits.mutations)return true;
    size_t terrain=0,companions=0,rosters=0;
    for(const auto& mutation:mutations){
        switch(mutation.kind){
        case MutationKind::AppendTerrain:case MutationKind::SetTerrain:case MutationKind::DeleteTerrain:++terrain;break;
        case MutationKind::Set:++companions;break;
        case MutationKind::SetActs:++rosters;break;
        default:return false;
        }
    }
    if(rosters)return rosters==1 && !terrain && companions<=limits.classes;
    return terrain && companions<=limits.mutations &&
        (terrain<=limits.terrain_records || terrain-limits.terrain_records<=limits.terrain_records);
}
bool patch_count_valid(const Patch& patch,const Limits& limits){
    if(patch.acts)return patch.objects.empty() && patch.classes.empty() && patch.terrain.empty() && patch.fields.size()<=limits.classes;
    const auto ordinary=patch.objects.size()+patch.classes.size()+patch.fields.size();
    if(ordinary<=limits.mutations && patch.terrain.size()<=limits.mutations-ordinary)return true;
    return patch.objects.empty() && patch.classes.empty() && !patch.terrain.empty() && patch.fields.size()<=limits.mutations &&
        (patch.terrain.size()<=limits.terrain_records || patch.terrain.size()-limits.terrain_records<=limits.terrain_records);
}
bool allocation_count_valid(const Change& change,const Limits& limits){
    if(!patch_count_valid(change.patch,limits))return false;
    if(change.allocated.size()<=limits.mutations)return true;
    if(change.patch.terrain.empty() || change.patch.acts)return false;
    if(change.patch.objects.empty() && change.patch.classes.empty())return change.allocated.size()<=limits.terrain_records;
    const auto terrain=size_t(std::count_if(change.patch.terrain.begin(),change.patch.terrain.end(),[](const auto& row){return !row.before && bool(row.after);}));
    return terrain<=limits.terrain_records && change.allocated.size()-limits.mutations<=terrain;
}

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
std::vector<AuthoringSettings> act_roster(const Document& d){
    std::vector<AuthoringSettings> result;
    for(auto act:d.act_order){
        auto row=std::find_if(d.settings.begin(),d.settings.end(),[&](const auto& value){return value.second.kind==SettingsKind::Act && value.second.act_id==act;});
        if(row==d.settings.end())return {};result.push_back(row->second);
    }
    return result;
}
std::vector<TerrainRecord> terrain_recipe(const Document& d){
    std::vector<TerrainRecord> result;result.reserve(d.terrain.size());
    for(const auto& [id,row]:d.terrain){(void)id;result.push_back(row);}
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.order<b.order;});return result;
}
Revision field_revision(const Placement& p, FieldKey field)
{
    auto it = p.field_revisions.find(field); return it == p.field_revisions.end() ? 0 : it->second;
}
std::optional<Value> value(const Document& d,ObjectId id,FieldKey field) {
    if(auto s=d.settings.find(id);s!=d.settings.end()){
        auto v=s->second.properties.find(field.property);
        return field.field==Field::Property && v!=s->second.properties.end()?std::optional<Value>{v->second}:std::nullopt;
    }
    auto p=d.placements.find(id);
    if(p!=d.placements.end())return value(p->second,field);
    auto c=d.class_settings.find(id);
    if(c==d.class_settings.end() || field.field!=Field::Property)return {};
    auto v=c->second.properties.find(field.property);
    return v==c->second.properties.end()?std::optional<Value>{}:v->second;
}
Revision field_revision(const Document& d,ObjectId id,FieldKey field) {
    if(auto s=d.settings.find(id);s!=d.settings.end()){
        auto r=s->second.field_revisions.find(field);return r==s->second.field_revisions.end()?0:r->second;
    }
    auto p=d.placements.find(id);
    if(p!=d.placements.end())return field_revision(p->second,field);
    auto c=d.class_settings.find(id);
    if(c==d.class_settings.end())return 0;
    auto r=c->second.field_revisions.find(field);
    return r==c->second.field_revisions.end()?0:r->second;
}
size_t bytes(const Document& d)
{
    size_t n = 128 + (d.classes.size() + d.acts.size() + d.tombstones.size()) * 48 + d.schema.size() * 64 + d.external_references.size() * 32 + d.act_order.size()*16;
    for(const auto& [id,row]:d.terrain){(void)id;n+=160+row.payload.bytes.size()+row.payload.references.size()*16;}
    for (const auto& [id, p] : d.placements) { (void)id; n += placement_bytes(p); }
    n += (d.class_schema.size()+d.settings_schema.size())*64;
    for(const auto& [id,s]:d.settings){
        (void)id;n+=128+s.field_revisions.size()*32;
        for(const auto& [key,v]:s.properties){(void)key;n+=16+value_bytes(v);}
    }
    for(const auto& [id,c]:d.class_settings){
        (void)id;n+=128+c.field_revisions.size()*32;
        for(const auto& [key,v]:c.properties){(void)key;n+=16+value_bytes(v);}
    }
    return n;
}
size_t bytes(const Patch& p)
{
    size_t n = 32 + p.dependencies.size() * 48;
    for(const auto& e:p.terrain){n+=64;for(const auto* row:{&e.before,&e.after})if(*row)n+=160+(**row).payload.bytes.size()+(**row).payload.references.size()*16;}
    if(p.acts)for(const auto* list:{&p.acts->before,&p.acts->after})for(const auto& row:*list){
        n+=160+row.field_revisions.size()*32;for(const auto& [key,v]:row.properties){(void)key;n+=16+value_bytes(v);}
    }
    for (const auto& e : p.objects) n += 48 + (e.before ? placement_bytes(*e.before) : 0) + (e.after ? placement_bytes(*e.after) : 0);
    for (const auto& e : p.fields) n += 64 + (e.before ? value_bytes(*e.before) : 0) + (e.after ? value_bytes(*e.after) : 0);
    for(const auto& e:p.classes)for(const auto* item:{&e.before,&e.after})if(*item){
        n+=160+(**item).field_revisions.size()*32;
        for(const auto& [key,v]:(**item).properties){(void)key;n+=16+value_bytes(v);}
    }
    return n;
}
size_t bytes(const State& s) { return bytes(s.document) + history_bytes(s) + decisions_bytes(s) + (s.retired_sequences.size() + s.history_truncated.size()) * 48; }

bool valid(const Document& d, const Limits& l, std::string& error)
{
    auto bad = [&](const char* why) { error = why; return false; };
    if (!project(d.project) || !d.epoch || !d.next_id || d.next_id == UINT64_MAX || d.revision == UINT64_MAX) return bad("Invalid project identity, epoch, revision or ID allocator.");
    if (d.placements.size() > l.placements || d.classes.size() > l.classes || d.acts.size() > l.acts ||
        d.tombstones.size() > l.tombstones || d.schema.size() > l.properties * 8 || d.class_schema.size()>l.properties*8 ||
        d.class_settings.size()>l.classes*l.acts || d.settings.size()>l.acts+4 || d.settings_schema.size()>l.properties*8 || d.terrain.size()>l.terrain_records || !d.next_terrain_order || d.next_terrain_order==UINT64_MAX ||
        d.external_references.size() > l.dependencies * 16 || bytes(d) > l.document_bytes) return bad("Document exceeds a configured bound.");
    // A new native adventure has acts before its first cast member is added.
    // Placements/settings still require their parent class below.
    if (d.acts.empty()) return bad("A document needs at least one known act.");
    if(d.act_revision>d.revision || (d.act_order.empty() && d.act_revision) || (!d.act_order.empty() &&
        (d.act_order.size()!=d.acts.size() || std::set<ObjectId>(d.act_order.begin(),d.act_order.end())!=d.acts || act_roster(d).size()!=d.acts.size())))
        return bad("Invalid ordered act roster or revision.");
    std::map<uint64_t, ObjectId> ids;
    auto add = [&](ObjectId id) { return identity(id) && id.id < d.next_id && ids.emplace(id.id, id).second; };
    for (auto id : d.classes) if (!add(id)) return bad("Invalid or reused class identity.");
    for (auto id : d.acts) if (!add(id)) return bad("Invalid or reused act identity.");
    for (const auto& [id, spec] : d.schema)
        if (!id || spec.kind > ValueKind::Structured || !std::isfinite(spec.minimum) ||
            !std::isfinite(spec.maximum) || spec.minimum > spec.maximum || spec.max_text > l.text_bytes ||
            (spec.kind == ValueKind::Text && !spec.max_text)) return bad("Invalid editable-property schema.");
    for(const auto& [id,spec]:d.class_schema)
        if(!id || spec.kind<ValueKind::Number || spec.kind>ValueKind::Structured || !std::isfinite(spec.minimum) ||
           !std::isfinite(spec.maximum) || spec.minimum>spec.maximum || spec.max_text>l.text_bytes ||
           (spec.kind==ValueKind::Text && !spec.max_text))return bad("Invalid class-property schema.");
    std::set<std::pair<ObjectId,ObjectId>> class_acts;
    for(const auto& [id,c]:d.class_settings){
        if(!add(id) || id!=c.object || !d.classes.contains(c.class_id) || !d.acts.contains(c.act_id) ||
           !class_acts.emplace(c.class_id,c.act_id).second || c.revision>d.revision || c.properties.size()>l.properties ||
           c.field_revisions.size()>d.class_schema.size())return bad("Invalid class/act settings identity or revision.");
        for(const auto& [property,v]:c.properties)
            if(!field_value({Field::Property,property},v,d,l,id))return bad("Invalid class-property value.");
        for(const auto& [property,spec]:d.class_schema)
            if(!spec.optional && !c.properties.contains(property))return bad("A required class property is missing.");
        for(const auto& [field,revision]:c.field_revisions)
            if(field.field!=Field::Property || !d.class_schema.contains(field.property) || revision>c.revision)
                return bad("Invalid class-property revision.");
    }
    for(const auto& [key,spec]:d.settings_schema)
        if(!key || spec.kind>ValueKind::Structured || !std::isfinite(spec.minimum) || !std::isfinite(spec.maximum) ||
           spec.minimum>spec.maximum || spec.max_text>l.text_bytes || (spec.kind==ValueKind::Text && !spec.max_text))
            return bad("Invalid authoring settings schema.");
    std::set<std::pair<SettingsKind,ObjectId>> setting_owners;
    for(const auto& [id,s]:d.settings){
        if(!add(id) || id!=s.object || s.kind>SettingsKind::Planet || s.revision>d.revision ||
           s.properties.size()>l.properties || s.field_revisions.size()>d.settings_schema.size() ||
           (s.kind==SettingsKind::Act?!d.acts.contains(s.act_id):s.act_id!=ObjectId{}) ||
           !setting_owners.emplace(s.kind,s.act_id).second)return bad("Invalid authoring settings owner or revision.");
        for(const auto& [key,v]:s.properties)
            if(!field_value({Field::Property,key},v,d,l,id))return bad("Invalid authoring settings value.");
        for(const auto& [key,spec]:d.settings_schema)
            if(!spec.optional && !s.properties.contains(key))return bad("A required authoring property is missing.");
        for(const auto& [key,revision]:s.field_revisions)
            if(key.field!=Field::Property || !d.settings_schema.contains(key.property) || revision>s.revision)
                return bad("Invalid authoring property revision.");
    }
    std::set<uint64_t> terrain_order;
    for(const auto& [id,row]:d.terrain)
        if(!add(id) || row.object!=id || !terrain_shape(row,l) || row.order>=d.next_terrain_order || !terrain_order.insert(row.order).second || row.revision>d.revision)
            return bad("Invalid terrain record, order or revision.");
    for (const auto& [id, p] : d.placements) {
        if (!add(id) || id != p.object || !d.classes.contains(p.class_id) || !placement_shape(p, d, l, true)) return bad("Invalid placement, field value or revision.");
        for (const auto& [property, v] : p.properties) {
            (void)property;
            if (auto* ref = std::get_if<ObjectId>(&v); ref && identity(*ref) && !d.placements.contains(*ref)) return bad("A placement reference is missing.");
        }
    }
    for (const auto& [id, revision] : d.tombstones)
        if (!add(id) || revision > d.revision) return bad("Invalid or reused deleted identity.");
    const auto known=[&](ObjectId id){return d.placements.contains(id) || d.class_settings.contains(id) || d.settings.contains(id) || d.classes.contains(id) || d.acts.contains(id);};
    for(const auto& [id,s]:d.settings)for(const auto& [key,v]:s.properties){
        (void)id;(void)key;
        if(auto* block=std::get_if<StructuredValue>(&v))for(auto ref:block->references)
            if(!known(ref))return bad("An authoring settings reference is missing.");
        if(auto* ref=std::get_if<ObjectId>(&v);ref && identity(*ref) && !known(*ref))return bad("An authoring settings reference is missing.");
    }
    for(const auto& [id,c]:d.class_settings)for(const auto& [property,v]:c.properties){
        (void)id;(void)property;
        if(auto* data=std::get_if<StructuredValue>(&v))for(auto ref:data->references)
            if(!known(ref)){
                const auto detail="A structured class-property reference is missing: owner="+std::to_string(id.id)+":"+std::to_string(id.generation)+
                    " property="+std::to_string(property)+" reference="+std::to_string(ref.id)+":"+std::to_string(ref.generation);
                return bad(detail.c_str());
            }
        if(auto* ref=std::get_if<ObjectId>(&v);ref && identity(*ref) &&
           !known(*ref))
            return bad("A class-property reference is missing.");
        if(auto* ref=std::get_if<ObjectId>(&v);ref && identity(*ref) && ids.at(ref->id)!=*ref)
            return bad("A class-property reference has an old generation.");
    }
    for(const auto& [id,p]:d.placements)for(const auto& [property,v]:p.properties){
        (void)id;(void)property;
        if(auto* data=std::get_if<StructuredValue>(&v))for(auto ref:data->references)
            if(!d.placements.contains(ref) && !d.class_settings.contains(ref) && !d.classes.contains(ref) && !d.acts.contains(ref))
                return bad("A structured placement-property reference is missing.");
    }
    std::set<Reference> refs;
    for (const auto& ref : d.external_references) {
        if (!identity(ref.owner) || (!d.placements.contains(ref.target) && !d.classes.contains(ref.target)) || !refs.insert(ref).second) return bad("Invalid external objective/avatar reference.");
        auto known = ids.find(ref.owner.id);
        if (ref.owner.id >= d.next_id || (known != ids.end() && known->second != ref.owner)) return bad("An external reference owner has an invalid or reused identity.");
        ids.emplace(ref.owner.id, ref.owner);
    }
    error.clear(); return true;
}

Coordinator::Coordinator(Limits limits) : limits_(limits) {}

bool Coordinator::reset(State state, uint64_t epoch, std::string& error)
{
    if (applying() || !epoch || (ready_ && state.document.project == state_.document.project && epoch == state_.document.epoch)) { error = "Recovery requires a fresh epoch nonce and no prepared transaction."; return false; }
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
            (g.applied && g.undo.objects.empty() && g.undo.fields.empty() && g.undo.classes.empty() && g.undo.terrain.empty() && !g.undo.acts) ||
            (g.redo_available && g.redo.objects.empty() && g.redo.fields.empty() && g.redo.classes.empty() && g.redo.terrain.empty() && !g.redo.acts) ||
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
                !patch_shape(c.patch, state.document, limits_) || !patch_shape(c.inverse, state.document, limits_) || !allocation_count_valid(c,limits_)) { error = "Invalid remembered transaction."; return false; }
            if ((!c.clone_sources.empty() && c.intent != Intent::Forward) || c.clone_sources.size() > c.allocated.size()) { error = "Invalid clone provenance."; return false; }
            for (const auto& [target, source] : c.clone_sources)
                if (!identity(source) || target == source || std::find(c.allocated.begin(), c.allocated.end(), target) == c.allocated.end() ||
                    std::none_of(c.patch.objects.begin(), c.patch.objects.end(), [clone_target = target](const ObjectEdit& e) { return e.object == clone_target && !e.before && e.after; })) { error = "Clone provenance does not name an allocated placement."; return false; }
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
    if (!ready_ || applying() || !identity(p) || p.generation < generations_[p.slot] || (p.generation == generations_[p.slot] && !connected_[p.slot])) return false;
    if (p.generation > generations_[p.slot]) {
        std::erase_if(leases_, [&](const Lease& l) { return l.owner.slot == p.slot; }); generations_[p.slot] = p.generation;
    }
    connected_[p.slot] = true; return true;
}
void Coordinator::disconnect(Peer p)
{
    if (!applying() && peer(p)) { connected_[p.slot] = false; std::erase_if(leases_, [&](const Lease& l) { return l.owner == p; }); }
}
bool Coordinator::clock(Time now)
{
    if (now < last_time_) return false;
    last_time_ = now; std::erase_if(leases_, [&](const Lease& l) { return l.expires <= now; }); return true;
}
bool Coordinator::scope_valid(const Scope& s) const
{
    if (s.kind > ScopeKind::Authoring) return false;
    const auto& d = state_.document;
    if (s.kind == ScopeKind::Document || s.kind == ScopeKind::Structure || s.kind==ScopeKind::Authoring) return s.object == ObjectId{} && s.field == FieldKey{};
    if (!identity(s.object)) return false;
    if(s.kind==ScopeKind::Terrain)return s.field==FieldKey{} && retained_terrain(state_,s.object);
    if(s.kind==ScopeKind::Content)return s.field==FieldKey{} && d.class_schema.contains(kClassAssetResourceProperty) &&
        (d.classes.contains(s.object) || ((s.object.id>>32)==2 && d.tombstones.contains(s.object)));
    if (s.kind == ScopeKind::Class) return s.field == FieldKey{} && (d.classes.contains(s.object) ||
        ((s.object.id>>32)==2 && d.tombstones.contains(s.object)));
    if (s.kind == ScopeKind::Act) return s.field == FieldKey{} && d.acts.contains(s.object);
    if(d.settings.contains(s.object))return s.kind==ScopeKind::Settings?s.field==FieldKey{}:
        s.kind==ScopeKind::Field && s.field.field==Field::Property && d.settings_schema.contains(s.field.property);
    if(d.class_settings.contains(s.object))return s.kind==ScopeKind::Field &&
        s.field.field==Field::Property && d.class_schema.contains(s.field.property);
    if ((!d.placements.contains(s.object) && !d.tombstones.contains(s.object)) || retained_terrain(state_,s.object)) return false;
    return s.kind == ScopeKind::Placement ? s.field == FieldKey{} : field_key(s.field) && (s.field.field != Field::Property || d.schema.contains(s.field.property));
}
bool Coordinator::covers(const Scope& a, const Scope& b) const
{
    if (a.kind == ScopeKind::Document || a == b) return true;
    if(a.kind==ScopeKind::Authoring)return is_authoring_scope(b);
    if(a.kind==ScopeKind::Content && b.kind==ScopeKind::Field && b.field==FieldKey{Field::Property,kClassAssetResourceProperty}){
        auto settings=state_.document.class_settings.find(b.object);return settings!=state_.document.class_settings.end() && settings->second.class_id==a.object;
    }
    if(a.kind==ScopeKind::Settings && b.kind==ScopeKind::Field && a.object==b.object)return true;
    if(a.kind==ScopeKind::Act && (b.kind==ScopeKind::Settings || b.kind==ScopeKind::Field)){
        auto s=state_.document.settings.find(b.object);
        if(s!=state_.document.settings.end())return s->second.kind==SettingsKind::Act && s->second.act_id==a.object;
    }
    if (a.kind == ScopeKind::Placement && b.kind == ScopeKind::Field && a.object == b.object) return true;
    if ((a.kind == ScopeKind::Class || a.kind == ScopeKind::Act) && (b.kind == ScopeKind::Placement || b.kind == ScopeKind::Field)) {
        auto settings=state_.document.class_settings.find(b.object);
        if(settings!=state_.document.class_settings.end())return b.kind==ScopeKind::Field && b.field!=FieldKey{Field::Property,kClassAssetResourceProperty} &&
            (a.kind==ScopeKind::Class?settings->second.class_id:settings->second.act_id)==a.object;
        auto* placement=retained_placement(state_,b.object);
        return placement && (a.kind == ScopeKind::Class ? placement->class_id : placement->act_id) == a.object;
    }
    return false;
}
bool Coordinator::overlaps(const Scope& a, const Scope& b) const
{
    if (covers(a, b) || covers(b, a)) return true;
    if ((a.kind == ScopeKind::Class && b.kind == ScopeKind::Act) || (a.kind == ScopeKind::Act && b.kind == ScopeKind::Class)) {
        for (const auto& [id, p] : state_.document.placements) { (void)id; if (p.class_id == (a.kind == ScopeKind::Class ? a.object : b.object) && p.act_id == (a.kind == ScopeKind::Act ? a.object : b.object)) return true; }
        for(const auto& [id,c]:state_.document.class_settings){(void)id;if(c.class_id==(a.kind==ScopeKind::Class?a.object:b.object) &&
            c.act_id==(a.kind==ScopeKind::Act?a.object:b.object))return true;}
    }
    // Deleted placements can be restored by Undo. Until their native class/act
    // context is restored, conservatively conflict with class and act writers.
    auto unknown = [&](const Scope& x, const Scope& y) {
        return (x.kind == ScopeKind::Class || x.kind == ScopeKind::Act) && (y.kind == ScopeKind::Placement || y.kind == ScopeKind::Field) &&
            !retained_placement(state_,y.object) && !state_.document.class_settings.contains(y.object) && !state_.document.settings.contains(y.object);
    };
    return unknown(a, b) || unknown(b, a);
}
LeaseResult Coordinator::acquire(Peer p, const std::vector<Scope>& scopes, Time now, Time ttl)
{
    auto bad = [](Code code, const char* reason) { return LeaseResult{code, reason, {}}; };
    if (applying()) return bad(Code::Busy, "A transaction is being applied or awaiting durability.");
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
    if (applying()) return {Code::Busy, "A transaction is being applied or awaiting durability.", {}};
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
    if (applying() || !peer(p) || tokens.empty() || tokens.size() > limits_.leases) return false;
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
    auto known=[&](ObjectId id){return d.classes.contains(id) || d.acts.contains(id) || d.placements.contains(id) || d.class_settings.contains(id) || d.settings.contains(id);};
    for(const auto& [id,s]:d.settings)for(const auto& [key,v]:s.properties){
        (void)id;(void)key;
        if(auto* block=std::get_if<StructuredValue>(&v))for(auto ref:block->references)if(!known(ref))return false;
        if(auto* ref=std::get_if<ObjectId>(&v);ref && identity(*ref) && !known(*ref))return false;
    }
    for(const auto& [id,c]:d.class_settings)for(const auto& [property,v]:c.properties){
        (void)id;(void)property;
        if(auto* data=std::get_if<StructuredValue>(&v))for(auto ref:data->references)if(!known(ref))return false;
    }
    for (const auto& [id, p] : d.placements) {
        (void)id;
        if(!d.classes.contains(p.class_id))return false;
        for (const auto& [property, v] : p.properties) {
            (void)property;
            if(auto* data=std::get_if<StructuredValue>(&v))for(auto ref:data->references)if(!known(ref))return false;
            if (auto* ref = std::get_if<ObjectId>(&v); ref && identity(*ref) && !d.placements.contains(*ref)) return false;
        }
    }
    for (const auto& ref : d.external_references) if (!d.placements.contains(ref.target) && !d.classes.contains(ref.target)) return false;
    for(const auto& [id,c]:d.class_settings)for(const auto& [property,v]:c.properties){
        (void)id;(void)property;
        if(auto* ref=std::get_if<ObjectId>(&v);ref && identity(*ref) &&
           !known(*ref))return false;
    }
    return true;
}

// Produces actual canonical before/after data and an inverse whose preconditions
// refer to the newly committed field/tombstone revisions. No live state is edited.
Code apply_patch(const Document& before, const Patch& input, const Limits& limits,
                 Document& after, Patch& canonical, Patch& inverse, std::string& error)
{
    auto bad = [&](Code code, const char* why) { error = why; return code; };
    if (!patch_shape(input, before, limits)) return bad(Code::Invalid, "Malformed or oversized semantic patch.");
    if (input.objects.empty() && input.fields.empty() && input.classes.empty() && input.terrain.empty() && !input.acts) return bad(Code::Invalid, "An empty gesture is not an edit.");
    for(const auto& e:input.terrain){
        auto found=before.terrain.find(e.object);
        if(e.before){
            if(found==before.terrain.end() || found->second!=*e.before || found->second.revision!=e.expected)
                return bad(Code::Conflict,"The terrain record changed or disappeared.");
            if(e.after && e.after->payload==e.before->payload)return bad(Code::Invalid,"An unchanged terrain record is not a gesture.");
        }else{
            if(found!=before.terrain.end() || absent_revision(before,e.object)!=e.expected ||
               (!before.tombstones.contains(e.object) && e.object.id<before.next_id))
                return bad(Code::Conflict,"The terrain identity is no longer absent.");
            for(const auto& [id,row]:before.terrain)if(id!=e.object && row.order==e.after->order)
                return bad(Code::Conflict,"The terrain recipe order is already occupied.");
        }
    }
    if(input.acts){
        const auto current=act_roster(before);const auto& expected=input.acts->before;
        if(before.act_order.empty() || input.acts->expected!=before.act_revision || expected.size()!=current.size())
            return bad(Code::Conflict,"The act order changed; no older action was selected.");
        for(size_t i=0;i<current.size();++i){
            const bool retained=std::any_of(input.acts->after.begin(),input.acts->after.end(),[&](const auto& row){return row.act_id==current[i].act_id && row.object==current[i].object;});
            if(expected[i].act_id!=current[i].act_id || expected[i].object!=current[i].object || (!retained && expected[i]!=current[i]))
                return bad(Code::Conflict,"An act being removed changed; no older action was selected.");
        }
        for(const auto& field:input.fields)if(before.settings.contains(field.object) && before.settings.at(field.object).kind==SettingsKind::Act)
            return bad(Code::Invalid,"Act metadata and roster changes require separate gestures.");
    }
    if(input.acts && input.acts->before==input.acts->after)return bad(Code::Invalid,"An unchanged act roster is not a gesture.");
    for(const auto& e:input.classes){
        const auto& item=e.before?*e.before:*e.after;
        auto found=before.class_settings.find(item.object);
        if(e.before){
            if(!before.classes.contains(e.object) || found==before.class_settings.end() || found->second!=*e.before || found->second.revision!=e.expected)
                return bad(Code::Conflict,"The shared cast class changed before removal.");
        }else if(before.classes.contains(e.object) || found!=before.class_settings.end() || absent_revision(before,e.object)!=e.expected ||
                 before.placements.contains(item.object) || before.acts.contains(item.object))
            return bad(Code::Conflict,"The shared cast identity is no longer absent.");
    }
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
        if (!editable(before,e.object)) return bad(Code::MissingObject, "The edited object no longer exists.");
        if (field_revision(before,e.object,e.field) != e.expected || value(before,e.object,e.field) != e.before) return bad(Code::Conflict, "The edited field changed; no older action was selected.");
        if (e.before == e.after) return bad(Code::Invalid, "A no-op field change is not a gesture.");
    }
    after = before; after.revision++;
    canonical = input; inverse = {};
    for(auto& e:canonical.terrain){
        if(e.after){
            e.after->revision=after.revision;after.terrain[e.object]=*e.after;after.tombstones.erase(e.object);
            if(e.object.id>=UINT64_MAX-1 || e.after->order>=UINT64_MAX-1)return bad(Code::Limit,"Terrain identities or order are exhausted.");
            after.next_id=std::max(after.next_id,e.object.id+1);
            after.next_terrain_order=std::max(after.next_terrain_order,e.after->order+1);
        }else{after.terrain.erase(e.object);after.tombstones[e.object]=after.revision;}
        inverse.terrain.push_back({e.object,e.after,e.before,after.revision,e.expected});
    }
    if(canonical.acts){
        auto& roster=*canonical.acts;std::set<ObjectId> surviving;
        roster.before=act_roster(before);
        for(auto& row:roster.after){
            auto current=before.settings.find(row.object);
            if(current!=before.settings.end() && current->second.act_id==row.act_id)row=current->second;
            else {
                row.revision=after.revision;row.field_revisions.clear();
                for(const auto& [key,v]:row.properties){(void)v;row.field_revisions[{Field::Property,key}]=after.revision;}
            }
        }
        for(const auto& row:roster.after){surviving.insert(row.act_id);surviving.insert(row.object);}
        for(const auto& row:roster.before){
            after.acts.erase(row.act_id);after.settings.erase(row.object);
            if(!surviving.contains(row.act_id))after.tombstones[row.act_id]=after.revision;
            if(!surviving.contains(row.object))after.tombstones[row.object]=after.revision;
        }
        after.act_order.clear();after.act_revision=after.revision;
        for(auto& row:roster.after){
            after.acts.insert(row.act_id);after.act_order.push_back(row.act_id);after.settings[row.object]=row;
            for(auto id:{row.act_id,row.object}){
                if(id.id>=UINT64_MAX-1)return bad(Code::Limit,"Act identities are exhausted.");
                after.next_id=std::max(after.next_id,id.id+1);after.tombstones.erase(id);
            }
        }
        const auto anchor=*after.acts.begin();
        for(auto& [id,p]:after.placements)if(p.act_id!=anchor){(void)id;p.act_id=anchor;p.revision=after.revision;}
        for(auto& [id,c]:after.class_settings)if(c.act_id!=anchor){(void)id;c.act_id=anchor;c.revision=after.revision;}
        inverse.acts=ActRosterEdit{roster.after,roster.before,after.revision,before.act_revision};
    }
    for(auto& e:canonical.classes){
        if(e.after){
            auto settings=*e.after;if(!after.act_order.empty())settings.act_id=*after.acts.begin();settings.revision=after.revision;settings.field_revisions.clear();
            for(const auto& [key,v]:settings.properties){(void)v;settings.field_revisions[{Field::Property,key}]=after.revision;}
            e.after=settings;after.classes.insert(e.object);after.class_settings[settings.object]=settings;
            after.tombstones.erase(e.object);after.tombstones.erase(settings.object);
            after.next_id=std::max(after.next_id,std::max(e.object.id,settings.object.id)+1);
            const auto index=uint32_t(e.object.id);
            if(index==UINT32_MAX)return bad(Code::Limit,"Native cast identities are exhausted.");
            after.next_class=std::max(after.next_class,index+1);
        }else{
            after.classes.erase(e.object);after.class_settings.erase(e.before->object);
            after.tombstones[e.object]=after.revision;after.tombstones[e.before->object]=after.revision;
        }
        inverse.classes.push_back({e.object,e.after,e.before,after.revision});
    }
    for (auto& e : canonical.objects) {
        if (e.after) {
            Placement p = *e.after;
            if(!after.act_order.empty())p.act_id=*after.acts.begin();
            stamp(p, after.revision); e.after = p;
            after.placements[e.object] = p; after.tombstones.erase(e.object);
            if (e.object.id >= after.next_id) {
                if (e.object.id >= UINT64_MAX - 1) return bad(Code::Limit, "The stable ID allocator is exhausted.");
                after.next_id = e.object.id + 1;
            }
        } else { after.placements.erase(e.object); after.tombstones[e.object] = after.revision; }
        inverse.objects.push_back({e.object, e.after, e.before, after.revision});
    }
    for (auto& e : canonical.fields) {
        Revision old_object_revision = object_revision(before,e.object);
        set(after,e.object,e.field,e.after,after.revision);
        inverse.fields.push_back({e.object, e.field, e.after, e.before, after.revision, e.expected, old_object_revision});
    }
    if (!references(after)) return bad(Code::Referenced, "This edit would orphan a placement, class, behavior or objective reference.");
    if (after.placements.size() > limits.placements || after.tombstones.size() > limits.tombstones || bytes(after) > limits.document_bytes)
        return bad(Code::Limit, "The candidate document exceeds a configured bound.");
    if (!valid(after, limits, error)) return Code::Invalid;
    inverse.dependencies = input.dependencies;
    for (auto& c : inverse.dependencies) {
        if (!editable(after,c.object)) { c.kind = CheckKind::Absent; c.expected = absent_revision(after, c.object); }
        else if (c.kind == CheckKind::Field) c.expected = field_revision(after,c.object,c.field);
        else { c.kind = CheckKind::Object; c.expected = object_revision(after,c.object); }
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
    for(const auto& e:requested.classes)if(e.after){
        for(const auto& [key,v]:e.after->properties){
            (void)v;auto prior=e.after->field_revisions.find({Field::Property,key});
            restored.push_back({e.after->object,{Field::Property,key},prior==e.after->field_revisions.end()?0:prior->second,e.after->revision,true});
        }
    }
    for (auto& g : state.history) {
        if (g.operation == change.gesture || g.operation.author != change.operation.author) continue;
        for (Patch* patch : {&g.undo, &g.redo}) {
            for(auto& e:patch->terrain){
                auto live=state.document.terrain.find(e.object);
                if(!e.before || live==state.document.terrain.end())continue;
                auto expected=*e.before,current=live->second;expected.revision=current.revision=0;
                if(expected!=current)continue;
                for(const auto& restored:requested.terrain)
                    if(restored.object==e.object && restored.after && e.expected==restored.restored_revision){e.expected=live->second.revision;e.before=live->second;}
            }
            if(patch->acts && requested.acts && patch->acts->expected==requested.acts->restored_revision){
                auto expected=patch->acts->before,current=act_roster(state.document);
                for(auto* rows:{&expected,&current})for(auto& row:*rows){row.revision=0;row.field_revisions.clear();row.properties.clear();}
                if(expected==current){
                    const auto live=act_roster(state.document);
                    for(size_t i=0;i<live.size();++i)if(patch->acts->before[i].properties==live[i].properties)patch->acts->before[i]=live[i];
                    patch->acts->expected=state.document.act_revision;
                }
            }
            for(auto& e:patch->classes)if(e.before){
                auto live=state.document.class_settings.find(e.before->object);
                if(live==state.document.class_settings.end() || live->second.properties!=e.before->properties)continue;
                for(const auto& r:restored)if(r.object==e.before->object && e.expected==r.object_revision){e.expected=state.document.revision;e.before=live->second;}
            }
            for (auto& e : patch->fields) {
                if (!editable(state.document,e.object) || value(state.document,e.object,e.field) != e.before) continue;
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
                if (!editable(state.document,dependency.object)) continue;
                for (const auto& r : restored) {
                    if (r.object != dependency.object) continue;
                    if (dependency.kind == CheckKind::Field && dependency.field == r.field && dependency.expected == r.field_revision) dependency.expected = field_revision(state.document,dependency.object,r.field);
                    if (r.whole && dependency.kind == CheckKind::Object && dependency.expected == r.object_revision) dependency.expected = object_revision(state.document,dependency.object);
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
    if (applying()) return reject(Code::Busy, "A native/durable transaction is already being applied.", false);
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
    if (r.intent > Intent::Redo || r.summary.size() > limits_.summary_bytes || !utf8(r.summary) || !mutation_count_valid(r.mutations,limits_) ||
        r.dependencies.size() > limits_.dependencies || r.leases.size() > limits_.leases) return reject(Code::Invalid, "Invalid gesture envelope or configured bound.");
    std::set<Scope> supplied;
    for (const auto& l : r.leases) if (!l.token || !scope_valid(l.scope) || !supplied.insert(l.scope).second) return reject(Code::Invalid, "Invalid or duplicate lease token.");
    Patch patch; std::vector<ObjectId> allocated; std::map<ObjectId, ObjectId> clone_sources; std::optional<size_t> gesture_index;
    size_t transient_dependencies = 0;
    if (r.intent == Intent::Forward) {
        if (r.mutations.empty()) return reject(Code::Invalid, "A forward gesture requires mutations.");
        uint64_t next_id = state_.document.next_id;
        uint32_t next_class=state_.document.next_class;
        uint64_t next_terrain_order=state_.document.next_terrain_order;
        for (const auto& m : r.mutations) {
            if (m.kind > MutationKind::DeleteTerrain) return reject(Code::Invalid, "Unknown mutation kind.");
            if(m.kind==MutationKind::AppendTerrain || m.kind==MutationKind::SetTerrain || m.kind==MutationKind::DeleteTerrain){
                if(m.initial!=Placement{} || m.field!=FieldKey{} || m.value || m.clone_pose || m.class_initial || m.act_roster)
                    return reject(Code::Invalid,"Terrain operations cannot contain unrelated authoring data.");
                if(m.kind==MutationKind::AppendTerrain){
                    if(!m.terrain || m.target!=ObjectId{} || m.expected || m.terrain->object!=ObjectId{} || m.terrain->order || m.terrain->revision)
                        return reject(Code::Invalid,"A terrain append cannot choose its identity, order or revision.");
                    if(std::any_of(leases_.begin(),leases_.end(),[&](const auto& lease){return (lease.scope.kind==ScopeKind::Document || lease.scope.kind==ScopeKind::Authoring) && lease.owner!=r.operation.author;}))
                        return reject(Code::Conflict,"The shared document is held for a global operation.");
                    if(next_id>=UINT64_MAX-1 || next_terrain_order>=UINT64_MAX-1)return reject(Code::Limit,"The terrain identity or order allocator is exhausted.");
                    auto row=*m.terrain;row.object={next_id++,1};row.order=next_terrain_order++;
                    patch.terrain.push_back({row.object,std::nullopt,row,0});allocated.push_back(row.object);
                }else{
                    const auto owned=leased(r,{ScopeKind::Terrain,m.target,{}},now);
                    if(owned!=Code::Accepted)return reject(owned,"Changing an existing terrain record requires its ownership.");
                    auto found=state_.document.terrain.find(m.target);
                    if(found==state_.document.terrain.end())return reject(Code::MissingObject,"The terrain record is missing.");
                    if(m.kind==MutationKind::DeleteTerrain){
                        if(m.terrain)return reject(Code::Invalid,"Terrain deletion cannot contain replacement data.");
                        patch.terrain.push_back({m.target,found->second,std::nullopt,m.expected});
                    }else{
                        if(!m.terrain || m.terrain->object!=m.target || m.terrain->order!=found->second.order || m.terrain->revision!=m.expected)
                            return reject(Code::Invalid,"Terrain replacement must retain its identity, order and expected revision.");
                        patch.terrain.push_back({m.target,found->second,m.terrain,m.expected});
                    }
                }
                continue;
            }
            if(m.terrain)return reject(Code::Invalid,"Only terrain operations may supply a terrain record.");
            if(m.kind==MutationKind::SetActs){
                if(!m.act_roster || patch.acts || state_.document.act_order.empty() || m.target!=ObjectId{} || m.initial!=Placement{} || m.field!=FieldKey{} || m.value || m.clone_pose || m.class_initial)
                    return reject(Code::Invalid,"Invalid act roster operation.");
                const auto code=leased(r,{ScopeKind::Authoring,{},{}},now);
                if(code!=Code::Accepted)return reject(code,"Changing the act roster requires the shared authoring hold.");
                for(const auto& row:*m.act_roster)if(!state_.document.acts.contains(row.act_id)){
                    if(next_id>=UINT64_MAX-2 || row.act_id!=ObjectId{next_id,1} || row.object!=ObjectId{next_id+1,1} || row.revision || !row.field_revisions.empty())
                        return reject(Code::Conflict,"The act allocation changed; retry the roster operation.");
                    next_id+=2;
                }else{
                    auto existing=state_.document.settings.find(row.object);
                    if(existing==state_.document.settings.end() || existing->second!=row)
                        return reject(Code::Conflict,"An existing act changed while its roster operation was pending.");
                }
                patch.acts=ActRosterEdit{act_roster(state_.document),*m.act_roster,m.expected};continue;
            }
            if(m.act_roster)return reject(Code::Invalid,"Only act roster operations may provide act rows.");
            if(m.kind==MutationKind::CreateClass || m.kind==MutationKind::DeleteClass){
                if(m.initial!=Placement{} || m.clone_pose || m.field!=FieldKey{} || m.value)
                    return reject(Code::Invalid,"A cast operation cannot contain placement data.");
                const auto code=leased(r,{ScopeKind::Structure,{},{}},now);
                if(code!=Code::Accepted)return reject(code,"Importing or removing cast requires structure ownership.");
                if(m.kind==MutationKind::CreateClass){
                    if(!m.class_initial || m.target!=ObjectId{} || m.expected || next_class==UINT32_MAX)return reject(Code::Invalid,"Invalid cast creation.");
                    auto settings=*m.class_initial;
                    const ObjectId class_id{(2ull<<32)|next_class,1},settings_id{(5ull<<32)|next_class,1};
                    if(settings.class_id!=class_id || settings.object!=settings_id || settings.revision || !settings.field_revisions.empty() ||
                       state_.document.classes.contains(class_id) || state_.document.tombstones.contains(class_id))
                        return reject(Code::Conflict,"The native cast allocation changed; reopen the asset picker.");
                    ++next_class;
                    patch.classes.push_back({class_id,std::nullopt,settings,0});allocated.push_back(class_id);allocated.push_back(settings_id);
                }else{
                    if(m.class_initial)return reject(Code::Invalid,"Cast removal cannot supply class data.");
                    const auto owned=leased(r,{ScopeKind::Class,m.target,{}},now);
                    if(owned!=Code::Accepted)return reject(owned,"Removing cast requires ownership of that class.");
                    if(state_.document.class_schema.contains(kClassAssetResourceProperty)){
                        const auto content=leased(r,{ScopeKind::Content,m.target,{}},now);
                        if(content!=Code::Accepted)return reject(content,"Removing cast requires ownership of its asset content.");
                    }
                    auto found=std::find_if(state_.document.class_settings.begin(),state_.document.class_settings.end(),[&](const auto& row){return row.second.class_id==m.target;});
                    if(found==state_.document.class_settings.end())return reject(Code::MissingObject,"The cast class is missing.");
                    patch.classes.push_back({m.target,found->second,std::nullopt,m.expected});
                }
                continue;
            }
            if(m.class_initial)return reject(Code::Invalid,"Only cast creation may supply class data.");
            if (m.kind != MutationKind::Clone && m.clone_pose) return reject(Code::Invalid, "Only cloning may provide a final clone pose.");
            if (m.kind != MutationKind::Create && m.initial != Placement{}) return reject(Code::Invalid, "Only creation may provide initial placement data.");
            if (m.kind != MutationKind::Set && (m.value || m.field != FieldKey{})) return reject(Code::Invalid, "Only field changes may provide a value or field selector.");
            if (m.kind == MutationKind::Create || m.kind == MutationKind::Clone) {
                auto code = leased(r, {ScopeKind::Structure, {}, {}}, now);
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
                auto class_owned=leased(r,{ScopeKind::Class,p.class_id,{}},now);
                if(class_owned!=Code::Accepted)return reject(class_owned,"Placing an asset requires ownership of its class.");
                if (next_id >= UINT64_MAX - 1) return reject(Code::Limit, "The stable ID allocator is exhausted.");
                p.object = {next_id++, 1}; p.revision = 0; p.field_revisions.clear();
                if (m.kind == MutationKind::Clone) clone_sources[p.object] = m.target;
                allocated.push_back(p.object); patch.objects.push_back({p.object, std::nullopt, p, 0});
            } else {
                if(m.kind==MutationKind::Set && (state_.document.class_settings.contains(m.target) || state_.document.settings.contains(m.target))){
                    if(!field_value(m.field,m.value,state_.document,limits_,m.target))
                        return reject(Code::Unsupported,"The class property is not registered, typed and valid.");
                    auto code=leased(r,field_scope(state_.document,m.target,m.field),now);
                    if(code!=Code::Accepted)return reject(code,"Editing shared properties requires ownership of their settings.");
                    patch.fields.push_back({m.target,m.field,value(state_.document,m.target,m.field),m.value,m.expected});
                    continue;
                }
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
        if(patch.acts){
            const auto code=leased(r,{ScopeKind::Authoring,{},{}},now);
            if(code!=Code::Accepted)return reject(code,"Act roster Undo/Redo requires the shared authoring hold.");
        }
        patch.dependencies.insert(patch.dependencies.end(), r.dependencies.begin(), r.dependencies.end());
        for (const auto& e : patch.objects) {
            auto code = leased(r, {ScopeKind::Placement, e.object, {}}, now);
            if (code != Code::Accepted) return reject(code, "Undo/Redo requires its placement lease.");
        }
        for (const auto& e : patch.fields) {
            auto code = leased(r, field_scope(state_.document,e.object,e.field), now);
            if (code != Code::Accepted) return reject(code, "Undo/Redo requires its edited-field lease.");
        }
        for(const auto& e:patch.terrain){
            auto code=leased(r,{ScopeKind::Terrain,e.object,{}},now);
            if(code!=Code::Accepted)return reject(code,"Terrain Undo/Redo requires ownership of the affected record.");
        }
        if(!patch.classes.empty()){
            const auto code=leased(r,{ScopeKind::Structure,{},{}},now);
            if(code!=Code::Accepted)return reject(code,"Cast Undo/Redo requires structure ownership.");
            for(const auto& entry:patch.classes)if(entry.before){
                const auto owned=leased(r,{ScopeKind::Class,entry.object,{}},now);
                if(owned!=Code::Accepted)return reject(owned,"Cast Undo/Redo requires ownership of the affected class.");
                if(state_.document.class_schema.contains(kClassAssetResourceProperty)){
                    const auto content=leased(r,{ScopeKind::Content,entry.object,{}},now);
                    if(content!=Code::Accepted)return reject(content,"Cast Undo/Redo requires ownership of its asset content.");
                }
            }
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
        if(preparing_){
            auto prepared=std::make_unique<Prepared>();prepared->state=std::move(candidate);prepared->decision=accepted;
            prepared->rollback=hooks.rollback;prepared->applied=applied;prepared_=std::move(prepared);return accepted;
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
        if(lease.scope.kind==ScopeKind::Terrain)return !state_.document.terrain.contains(lease.scope.object);
        if(lease.scope.kind==ScopeKind::Act)return !state_.document.acts.contains(lease.scope.object);
        if(lease.scope.kind==ScopeKind::Settings)return !state_.document.settings.contains(lease.scope.object);
        return (lease.scope.kind == ScopeKind::Placement || lease.scope.kind == ScopeKind::Field) &&
            !state_.document.placements.contains(lease.scope.object) &&
            !(lease.scope.kind==ScopeKind::Field && (state_.document.class_settings.contains(lease.scope.object) || state_.document.settings.contains(lease.scope.object)));
    });
    return accepted;
}

bool Coordinator::prepare(const Request& request,Time now,const Hooks& hooks,Decision& candidate,std::string& error){
    if(preparing_ || applying()){candidate=failure(Code::Busy,"A transaction is already prepared.",state_.document.revision);error=candidate.reason;return false;}
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(preparing_);
    candidate=submit(request,now,hooks);error=candidate.reason;return candidate.accepted();
}
const State* Coordinator::prepared_state()const{return prepared_?&prepared_->state:nullptr;}
const Change* Coordinator::prepared_change()const{return prepared_ && prepared_->decision.change?&*prepared_->decision.change:nullptr;}
Decision Coordinator::finish_prepared(bool durable,bool ambiguous,std::string error){
    if(busy_ || !prepared_)return failure(Code::Invalid,"No prepared transaction is awaiting durability.",state_.document.revision);
    struct Guard {bool& flag;explicit Guard(bool& value):flag(value){flag=true;}~Guard(){flag=false;}} guard(busy_);
    auto pending=std::move(prepared_);const auto& change=*pending->decision.change;
    if(durable && !ambiguous){
        state_=std::move(pending->state);
        std::erase_if(leases_,[&](const auto& lease){
            if(lease.scope.kind==ScopeKind::Act)return !state_.document.acts.contains(lease.scope.object);
            if(lease.scope.kind==ScopeKind::Settings)return !state_.document.settings.contains(lease.scope.object);
            if(lease.scope.kind==ScopeKind::Terrain)return !state_.document.terrain.contains(lease.scope.object);
            return (lease.scope.kind==ScopeKind::Placement || lease.scope.kind==ScopeKind::Field) && !editable(state_.document,lease.scope.object);
        });
        return pending->decision;
    }
    bool restored=!pending->applied;
    if(pending->applied){
        std::string why;try{restored=pending->rollback && pending->rollback(state_.document,pending->state.document,change,why);}catch(...){restored=false;}
        if(!restored && !why.empty())error+="; rollback: "+why;
    }
    if(ambiguous || !restored)frozen_=true;
    auto rejected=failure(frozen_?Code::Frozen:Code::PersistenceFailed,error.empty()?
        (frozen_?"Prepared commit durability or restoration is uncertain; recover a checkpoint.":"The prepared commit was not durably accepted."):error,state_.document.revision);
    remember(state_,change.operation,rejected);return rejected;
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
        std::map<size_t,size_t> allocation_rank;
        auto rank=[&](ObjectId id){
            auto found=std::find(change.allocated.begin(),change.allocated.end(),id);
            if(found!=change.allocated.end())allocation_rank.emplace(request.mutations.size(),size_t(found-change.allocated.begin()));
        };
        for(const auto& e:change.patch.terrain){
            Mutation m;m.target=e.object;m.expected=e.expected;
            if(!e.before && e.after){m.kind=MutationKind::AppendTerrain;m.target={};m.expected=0;m.terrain=*e.after;m.terrain->object={};m.terrain->order=0;m.terrain->revision=0;rank(e.object);}
            else if(e.before && e.after){m.kind=MutationKind::SetTerrain;m.terrain=*e.after;m.terrain->revision=e.expected;}
            else if(e.before){m.kind=MutationKind::DeleteTerrain;}
            else return bad("A canonical terrain patch is malformed.");
            request.mutations.push_back(std::move(m));
        }
        if(change.patch.acts){
            Mutation m;m.kind=MutationKind::SetActs;m.expected=change.patch.acts->expected;m.act_roster=change.patch.acts->after;
            for(auto& row:*m.act_roster){
                auto old=state.document.settings.find(row.object);
                row.revision=old==state.document.settings.end()?0:old->second.revision;
                row.field_revisions=old==state.document.settings.end()?std::map<FieldKey,Revision>{}:old->second.field_revisions;
            }
            request.mutations.push_back(std::move(m));
        }
        for(const auto& e:change.patch.classes){
            Mutation m;
            if(e.after && !e.before){m.kind=MutationKind::CreateClass;m.class_initial=*e.after;m.class_initial->revision=0;m.class_initial->field_revisions.clear();rank(e.object);}
            else if(e.before && !e.after){m.kind=MutationKind::DeleteClass;m.target=e.object;m.expected=e.expected;}
            else return bad("A canonical cast patch is malformed.");
            request.mutations.push_back(std::move(m));
        }
        for (const auto& e : change.patch.objects) {
            Mutation m;
            if (e.after && !e.before) {
                rank(e.object);
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
        // Canonical patch categories preserve their own order. Interleave their
        // allocations in the host's original order so a mixed placement/terrain
        // gesture allocates the exact same stable identities on every replica.
        std::array<std::deque<size_t>,5> groups;
        for(size_t i=0;i<request.mutations.size();++i){
            const auto kind=request.mutations[i].kind;
            const size_t group=kind>=MutationKind::AppendTerrain?0:kind==MutationKind::SetActs?1:
                (kind==MutationKind::CreateClass || kind==MutationKind::DeleteClass)?2:kind==MutationKind::Set?4:3;
            groups[group].push_back(i);
        }
        std::vector<Mutation> ordered;ordered.reserve(request.mutations.size());
        while(ordered.size()<request.mutations.size()){
            size_t selected=groups.size(),first_rank=SIZE_MAX;
            for(size_t group=0;group<groups.size();++group)for(auto index:groups[group]){
                auto found=allocation_rank.find(index);if(found==allocation_rank.end())continue;
                if(found->second<first_rank){first_rank=found->second;selected=group;}break;
            }
            if(selected==groups.size()){
                for(auto& group:groups)while(!group.empty()){ordered.push_back(std::move(request.mutations[group.front()]));group.pop_front();}
            }else while(!groups[selected].empty()){
                const auto index=groups[selected].front();groups[selected].pop_front();ordered.push_back(std::move(request.mutations[index]));
                if(allocation_rank.contains(index))break;
            }
        }
        request.mutations=std::move(ordered);
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
