#include "editor_native.h"
#include "editor_native_bindings.h"
#include "editor_native_tools.h"
#include "net/dbpf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <set>
#include <utility>

#include "sporekit_game.hpp"
#include "game.h"
#include <windows.h>
#include "modes.h"
#include "sui.h"

namespace editor_native {
namespace {
const SkHost* H;
SkMod* M;
Ctx C;
uintptr_t a_mode, a_history, a_act, a_ctor, a_read, a_write, a_copy, a_entry, a_restore, a_alloc;
uintptr_t a_context, a_tools, a_marker, a_object, a_clone_marker, a_create_marker, a_delete_marker, a_apply_marker;
uintptr_t a_loading, a_layout_find, a_display_vtable, a_erase_history, a_discard_history, a_copy_marker_rows, a_insert_marker;
uintptr_t a_entry_vtable;
uintptr_t a_tool_tick;
uintptr_t a_copy_class_rows,a_copy_class,a_insert_class;
uintptr_t a_speech_ui,a_behavior_data;
uintptr_t a_view_manager, a_view_target, a_view_orbit, a_view_set_orbit, a_view_lookat;
bool supported, applying, queued;
bool fence_queued;
double last_fence;
double last_tool_rejection_log=-1;
std::array<double,8> last_tool_rejection{{-1,-1,-1,-1,-1,-1,-1,-1}};
double last_legacy_scale_notice=-10;
double last_clone_scale_notice=-10;
std::array<uint64_t,8> tool_rejection_counts{};
std::map<sui::Win,uint32_t> fenced_windows;
uint64_t fenced_generation=0;
uint64_t generation = 1, captures = 0, restores = 0, rejected = 0;
void* observed_mode;
void* observed_data;
bool observed_scenario;
std::map<uint32_t, editor::ObjectId> bindings;
std::map<uint32_t,uint32_t> incarnations;
std::vector<editor::ObjectId> selection;
uint32_t selected_class;
std::optional<editor::ObjectId> input_clone, gesture_clone;
bool input_cast=false,gesture_cast=false;
bool input_speech=false;
uint32_t script_events=0;
constexpr uint32_t kResourceSize = 0x2CB0, kLimit = 32u << 20;

void* mode() { return a_mode ? *reinterpret_cast<void**>(a_mode) : nullptr; }
void* data(void* m) { return skg::readable(m, 0xDC) ? skg::field<void*>(m, 0x74) : nullptr; }
void* resource(void* d) { return skg::readable(d, 0x128) ? skg::field<void*>(d, 0x10) : nullptr; }
bool editing() {
    void* m = mode();
    return supported && game::mode_id()==modes::kScenario && skg::readable(m, 0xDC) && skg::field<int>(m, 0xCC) == 1 &&
           skg::field<int>(m, 0xD0) == 0 && skg::readable(resource(data(m)), kResourceSize);
}
bool active() { return editing() && C.active && C.active(); }
bool tracks_relative_scale(){return !C.relative_scale_tracked || C.relative_scale_tracked();}
bool tracks_extended_poses(){return !C.extended_poses_tracked || C.extended_poses_tracked();}
bool tracks_cast(){return !C.cast_tracked || C.cast_tracked();}
void addref(void* p) { if (p) skg::vcall<int>(p, 0); }
void release(void* p) { if (p) skg::vcall<int>(p, 1); }
struct Owned {
    void* p = nullptr;
    explicit Owned(void* value = nullptr) : p(value) {}
    ~Owned() { release(p); }
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
};

// Same independently verified IO::IStream slots as adventure_collect. Native Read
// sees a strictly bounded stream; corrupt input can neither grow it nor seek past it.
struct Stream {
    void** vtable;
    std::vector<uint8_t> bytes;
    uint32_t position = 0;
    int refs = 1;
    bool failed = false, readonly = false;
};
void* SK_THISCALL destroy(Stream* s, int) { return s; }
int SK_THISCALL ref(Stream* s) { return ++s->refs; }
int SK_THISCALL unref(Stream* s) { return --s->refs; }
uint32_t SK_THISCALL type(Stream*) { return 0; }
int SK_THISCALL access(Stream* s) { return s->readonly ? 1 : 3; }
int SK_THISCALL stream_state(Stream* s) { return s->failed ? -1 : 0; }
bool SK_THISCALL close(Stream* s) { return !s->failed; }
uint32_t SK_THISCALL size(Stream* s) { return uint32_t(s->bytes.size()); }
bool SK_THISCALL resize(Stream* s, uint32_t n) {
    if (s->readonly || n > kLimit) { s->failed = true; return false; }
    s->bytes.resize(n); s->position = std::min(n, s->position); return true;
}
int SK_THISCALL position(Stream* s, int origin) {
    return origin == 0 ? int(s->position) : origin == 1 ? 0 : int(s->position) - int(s->bytes.size());
}
bool SK_THISCALL seek(Stream* s, int offset, int origin) {
    int64_t base = origin == 0 ? 0 : origin == 1 ? s->position : origin == 2 ? s->bytes.size() : -1;
    int64_t n = base + offset;
    if (base < 0 || n < 0 || n > kLimit || (s->readonly && n > int64_t(s->bytes.size()))) {
        s->failed = true; return false;
    }
    if (n > int64_t(s->bytes.size())) s->bytes.resize(size_t(n));
    s->position = uint32_t(n); return true;
}
int SK_THISCALL available(Stream* s) { return int(s->bytes.size()) - int(s->position); }
int SK_THISCALL read(Stream* s, void* out, uint32_t n) {
    n = std::min(n, uint32_t(s->bytes.size()) - s->position);
    if (n && !out) { s->failed = true; return 0; }
    if (n) memcpy(out, s->bytes.data() + s->position, n);
    s->position += n; return int(n);
}
bool SK_THISCALL flush(Stream* s) { return !s->failed; }
bool SK_THISCALL write(Stream* s, const void* in, uint32_t n) {
    if (s->readonly || n > kLimit - s->position || (n && !in)) { s->failed = true; return false; }
    uint32_t end = s->position + n;
    if (end > s->bytes.size()) s->bytes.resize(end);
    if (n) memcpy(s->bytes.data() + s->position, in, n);
    s->position = end; return true;
}
void* stream_vtable[] = {(void*)destroy,(void*)ref,(void*)unref,(void*)type,(void*)access,
    (void*)stream_state,(void*)close,(void*)size,(void*)resize,(void*)position,(void*)seek,
    (void*)available,(void*)read,(void*)flush,(void*)write};

void* construct() {
    void* p = skg::cdecl_call<void*>(a_alloc, size_t(kResourceSize), "CoopEditorSnapshot", 0, 0, 0, 0);
    if (!p) return nullptr;
    p = skg::thiscall<void*>(a_ctor, p);
    addref(p);
    return p;
}
void* parse(const std::vector<uint8_t>& bytes, std::string& error) {
    if (bytes.empty() || bytes.size() > kLimit) { error = "Invalid native editor snapshot size"; return nullptr; }
    Owned result(construct());
    if (!result.p) { error = "Native editor snapshot allocation failed"; return nullptr; }
    Stream stream{stream_vtable, bytes, 0, 1, false, true};
    if (!skg::thiscall<bool>(a_read, result.p, &stream) || stream.failed) {
        error = "Native editor snapshot could not be read"; return nullptr;
    }
    // Decode privately, then reject a roster the verified native editor cannot
    // construct before any owned bytes can be copied into the live scenario.
    const uintptr_t begin=skg::field<uintptr_t>(result.p,0x2C10),end=skg::field<uintptr_t>(result.p,0x2C14);
    if(end<begin || (end-begin)%0x238 || (end-begin)/0x238>kPlacementLimit){
        error="Native editor placement limit exceeded";return nullptr;
    }
    return std::exchange(result.p, nullptr);
}
bool serialize(void* p, std::vector<uint8_t>& bytes, std::string& error) {
    Stream stream{stream_vtable, {}, 0, 1, false, false};
    if (!skg::readable(p, kResourceSize) || !skg::thiscall<bool>(a_write, p, &stream) ||
        stream.failed || stream.bytes.empty()) {
        error = "Native editor snapshot serialization failed"; return false;
    }
    bytes = std::move(stream.bytes); return true;
}

struct Rows { uint8_t* begin = nullptr; uint8_t* end = nullptr; };
bool live_slot(uint8_t* row){return !(skg::field<uint32_t>(row,0)&0x80000000u);}
uint32_t slot_index(const Rows& map,uint8_t* row){return uint32_t((row-map.begin)/0x238);}
bool rows(void* r, uint32_t offset, uint32_t stride, uint32_t limit, Rows& out) {
    uintptr_t begin = skg::field<uintptr_t>(r, offset), end = skg::field<uintptr_t>(r, offset + 4);
    if (end < begin || (end - begin) % stride || (end - begin) / stride > limit ||
        (end != begin && !skg::readable(reinterpret_cast<void*>(begin), end - begin))) return false;
    out = {reinterpret_cast<uint8_t*>(begin), reinterpret_cast<uint8_t*>(end)}; return true;
}
bool owned_class_gate(void* target){
    if(!tracks_cast() || !C.class_gate)return false;
    Rows classes;if(!rows(resource(data(mode())),0x2BF4,0x27E8,1024,classes))return false;
    for(auto row=classes.begin;row!=classes.end;row+=0x27E8)
        if(row+8==target)return C.class_gate(skg::field<uint32_t>(row,0));
    return false;
}
editor::ObjectId marker_id(uint32_t key,const std::map<uint32_t,editor::ObjectId>* supplied=nullptr) {
    const auto& source=supplied?*supplied:bindings;
    auto it = source.find(key);
    auto incarnation=incarnations.find(key);
    return it != source.end() ? it->second : editor::ObjectId{(1ull << 32) | key,
        incarnation==incarnations.end()?1:incarnation->second};
}
struct MarkerAudio {
    void* owner=nullptr;
    void* resource_pin=nullptr;
    uint64_t generation_pin=0;
    uint32_t slot=0,class_index=0;
    float pitch=0,gain=0;
    MarkerAudio(void* candidate_owner,void* marker){
        if(!active() || !tracks_extended_poses() || candidate_owner!=data(mode()))return;
        auto* r=resource(candidate_owner);Rows map;
        if(!rows(r,0x2C10,0x238,kPlacementLimit,map))return;
        const auto begin=reinterpret_cast<uintptr_t>(map.begin),end=reinterpret_cast<uintptr_t>(map.end),p=reinterpret_cast<uintptr_t>(marker);
        if(p<begin+4 || p>=end || (p-begin-4)%0x238 || !live_slot(static_cast<uint8_t*>(marker)-4))return;
        owner=candidate_owner;resource_pin=r;generation_pin=generation;slot=uint32_t((p-begin-4)/0x238);
        class_index=skg::field<uint32_t>(marker,0);pitch=skg::field<float>(marker,0x1E0);gain=skg::field<float>(marker,0x1E4);
    }
    ~MarkerAudio(){
        if(!owner || owner!=data(mode()) || resource(owner)!=resource_pin || generation!=generation_pin)return;
        Rows map;if(!rows(resource_pin,0x2C10,0x238,kPlacementLimit,map) || slot>=uint32_t((map.end-map.begin)/0x238))return;
        auto* marker=map.begin+slot*0x238+4;
        if(!live_slot(marker-4) || skg::field<uint32_t>(marker,0)!=class_index)return;
        // Pose and selection refreshes reconstruct sound controls. Keep the
        // authored source values, including a genuine setter's new values.
        skg::field<float>(marker,0x1E0)=pitch;skg::field<float>(marker,0x1E4)=gain;
    }
};
bool class_block(uint8_t* row,editor::StructuredValue& block,std::string& error){
    Owned fragment(construct());if(!fragment.p){error="Cannot allocate the owned cast subtree";return false;}
    // F2E670 leaves screenshot types 68..74 uninitialized. They and the local
    // camera belong to the wrapper, never the authored class subtree.
    std::memset(skg::ptr_add<void>(fragment.p,0x68),0,0x10);
    std::memset(skg::ptr_add<void>(fragment.p,0x2C30),0,0x20);
    skg::field<float>(fragment.p,0x2C48)=1.f;
    const std::array<uintptr_t,3> slice{reinterpret_cast<uintptr_t>(row),reinterpret_cast<uintptr_t>(row+0x27E8),reinterpret_cast<uintptr_t>(row+0x27E8)};
    skg::thiscall<void*>(a_copy_class_rows,skg::ptr_add<void>(fragment.p,0x2BF4),slice.data());
    skg::field<uint32_t>(fragment.p,0x2C0C)=skg::field<uint32_t>(row,0)+1;
    auto* copied=skg::field<uint8_t*>(fragment.p,0x2BF4)+8;
    // Match the collector's verified normalization on this owned class copy.
    // Server IDs are local lookup hints; the canonical subtree uses local keys.
    for(uint32_t offset:{0u,0x28u,0x50u}){
        auto& server=skg::field<uint64_t>(copied,offset+0x10);
        if(server && server!=UINT64_MAX){
            const auto resolve=H->sym("Simulator::Scenario::ResolveLocalServerAsset");
            editor::ResourceKey key;
            if(!resolve || !skg::cdecl_call<bool>(resolve,server,&key) || !key.instance){error="A cast dependency is not installed locally";return false;}
            skg::field<editor::ResourceKey>(copied,offset)=key;server=UINT64_MAX;
        }
    }
    canonical_cast_asset_hints(copied);
    block.format=kCastBlockFormat;
    if(!serialize(fragment.p,block.bytes,error))return false;
    // Native Read normalizes deprecated fields. Hash the owned read/write
    // representation so capture, transfer and application compare that form.
    Owned normalized(parse(block.bytes,error));if(!normalized.p)return false;
    std::memset(skg::ptr_add<void>(normalized.p,0x68),0,0x10);
    std::memset(skg::ptr_add<void>(normalized.p,0x2C30),0,0x20);
    skg::field<float>(normalized.p,0x2C48)=1.f;
    if(!serialize(normalized.p,block.bytes,error))return false;
    if(block.bytes.size()>editor::Limits{}.structured_bytes){error="The cast subtree exceeds its transaction limit";return false;}
    Rows acts;
    auto* normalized_class=skg::field<uint8_t*>(normalized.p,0x2BF4)+8;
    if(!rows(normalized_class,0x70,0x4E0,8,acts)){error="Invalid native cast act collection";return false;}
    std::set<editor::ObjectId> references;
    for(auto act=acts.begin;act!=acts.end;act+=0x4E0){
        for(auto offset:{0x4BCu,0x4C0u,0x4C4u})behavior_class_reference(references,2,skg::field<int32_t>(act,offset));
        Rows rules;
        if(!rows(act,0x4CC,0x20,1024,rules)){error="Invalid native behavior rule collection";return false;}
        for(auto rule=rules.begin;rule!=rules.end;rule+=0x20)
            for(auto offset:{4u,0x10u,0x18u})
                behavior_class_reference(references,skg::field<int32_t>(rule,offset),skg::field<int32_t>(rule,offset+4));
    }
    block.references.assign(references.begin(),references.end());
    return true;
}
void* marker_for_key(void* r, uint32_t key) {
    Rows map;
    if (!rows(r,0x2C10,0x238,kPlacementLimit,map)) return nullptr;
    if(key>=uint32_t((map.end-map.begin)/0x238))return nullptr;
    auto p=map.begin+key*0x238;
    return live_slot(p)?p+4:nullptr;
}
void* marker_for_id(void* r, editor::ObjectId id) {
    Rows map;
    if (!rows(r,0x2C10,0x238,kPlacementLimit,map))return nullptr;
    for(auto p=map.begin;p!=map.end;p+=0x238)
        if(live_slot(p) && marker_id(slot_index(map,p))==id)return p+4;
    return nullptr;
}
void refresh_selection() {
    selection.clear();selected_class=0;
    void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    if(!editing() || !skg::readable(context,0x14))return;
    void* object=skg::field<void*>(context,4);
    if(!object)return;
    void* marker=skg::thiscall<void*>(a_marker,data(mode()),object);
    Rows map;
    if(!marker || !rows(resource(data(mode())),0x2C10,0x238,kPlacementLimit,map))return;
    for(auto p=map.begin;p!=map.end;p+=0x238)if(live_slot(p) && p+4==marker){
        selection.push_back(marker_id(slot_index(map,p)));
        selected_class=skg::field<uint32_t>(marker,0);return;
    }
}
bool projection(void* r, Snapshot& out, std::string& error,
                const std::map<uint32_t,editor::ObjectId>* supplied=nullptr,
                std::optional<bool> requested_scale_mode=std::nullopt,
                std::optional<bool> requested_extended=std::nullopt,
                std::optional<bool> requested_cast=std::nullopt) {
    const bool tracked=requested_scale_mode?*requested_scale_mode:tracks_relative_scale();
    const bool extended=requested_extended.value_or(tracks_extended_poses());
    const bool cast=requested_cast.value_or(tracks_cast());
    Rows classes, markers, acts;
    if (!rows(r, 0x2BF4, 0x27E8, 1024, classes) || !rows(r, 0x2C10, 0x238, kPlacementLimit, markers) ||
        !rows(r, 0x23C, 0x534, 8, acts)) {
        error = "Native editor resource vectors are invalid"; return false;
    }
    out.document = {};
    out.document.next_id = 4ull << 32;
    if(extended)out.document.schema=tracked?placement_property_schema():legacy_extended_schema();
    else if(tracked)out.document.schema=relative_scale_schema();
    for (auto p = classes.begin; p != classes.end; p += 0x27E8)
        out.document.classes.insert({(2ull << 32) | skg::field<uint32_t>(p, 0), 1});
    if(cast){
        out.document.class_schema=cast_property_schema();
        out.document.next_class=skg::field<uint32_t>(r,0x2C0C);
        out.document.next_id=6ull<<32;
        for(auto row=classes.begin;row!=classes.end;row+=0x27E8){
            const auto index=skg::field<uint32_t>(row,0);
            editor::ClassActSettings settings;
            settings.object={(5ull<<32)|index,1};settings.class_id={(2ull<<32)|index,1};settings.act_id={(3ull<<32)|out.act,1};
            editor::StructuredValue value;if(!class_block(row,value,error))return false;
            settings.properties[kCastBlockProperty]=std::move(value);
            out.document.class_settings.emplace(settings.object,std::move(settings));
        }
    }
    uint32_t count = uint32_t((acts.end - acts.begin) / 0x534);
    for (uint32_t i = 0; i < count; ++i) out.document.acts.insert({(3ull << 32) | i, 1});
    out.bindings.clear();
    for (auto p = markers.begin; p != markers.end; p += 0x238) {
        if(!live_slot(p))continue;
        uint32_t key = slot_index(markers,p);
        auto marker = p + 4;
        editor::Placement placement;
        if(supplied && !supplied->count(key)){error="Native placement is missing its stable binding";return false;}
        placement.object = marker_id(key,supplied);
        placement.class_id = {(2ull << 32) | skg::field<uint32_t>(marker, 0), 1};
        placement.act_id = {(3ull << 32) | out.act, 1};
        placement.position = skg::field<editor::Vec3>(marker, 4);
        placement.rotation = skg::field<editor::Rotation>(marker, 0x10);
        placement.scale = skg::field<float>(marker, 0x20);
        if(!project_relative_scale(placement,tracked,skg::field<float>(marker,0x24),error))return false;
        if(extended){
            placement.properties[kElevationProperty]=skg::field<float>(marker,0x220);
            placement.properties[kSoundPitchProperty]=skg::field<float>(marker,0x1E0);
            placement.properties[kSoundGainProperty]=skg::field<float>(marker,0x1E4);
            for(auto row=classes.begin;row!=classes.end;row+=0x27E8){
                if(skg::field<uint32_t>(row,0)!=uint32_t(placement.class_id.id) ||
                   skg::field<uint32_t>(row,8)!=0x90C48D09u)continue;
                placement.properties[kDestinationPositionProperty]=skg::field<editor::Vec3>(marker,0x1E8);
                placement.properties[kDestinationRotationProperty]=skg::field<editor::Rotation>(marker,0x1F4);
                placement.properties[kDestinationScaleProperty]=skg::field<float>(marker,0x204);
            }
        }
        if (!out.document.classes.count(placement.class_id)) {
            error = "Native editor marker has no scenario class"; return false;
        }
        out.document.placements.emplace(placement.object, placement);
        out.document.next_id=std::max(out.document.next_id,placement.object.id+1);
        out.bindings.push_back({placement.object, key});
    }
    // Goals name classes, including classes with no instances. Matching
    // placements remain conservatively retained for older recovery semantics.
    // This uses the same reviewed authored layout as adventure_catalog, distinct
    // from the larger runtime goal records in cScenarioPlayMode.
    uint32_t act_index=0;
    for(auto act=acts.begin;act!=acts.end;act+=0x534,++act_index){
        Rows goals;
        if(!rows(act,0x84,0x188,3,goals)){error="Invalid native authored goal list";return false;}
        uint32_t goal_index=0;
        for(auto goal=goals.begin;goal!=goals.end;goal+=0x188,++goal_index){
            if(skg::field<uint32_t>(goal,0)==0)continue;
            auto owner=editor::ObjectId{(3ull<<32)|0x1000u|(act_index*3+goal_index),1};
            std::set<uint32_t> targets{skg::field<uint32_t>(goal,4),skg::field<uint32_t>(goal,8)};
            if(cast)for(auto id:out.document.classes)
                if(targets.count(uint32_t(id.id)))out.document.external_references.push_back({owner,id});
            for(const auto& [id,placement]:out.document.placements)
                if(targets.count(uint32_t(placement.class_id.id)))out.document.external_references.push_back({owner,id});
        }
    }
    return true;
}
bool capture_resource(void* r, uint32_t act, Snapshot& out, std::string& error,
                      const std::map<uint32_t,editor::ObjectId>* supplied=nullptr) {
    out = {}; out.act = act;
    out.source = skg::field<editor::ResourceKey>(r, 8);
    out.captain=skg::field<editor::ResourceKey>(r,0x130);
    out.captain_locked=skg::field<bool>(r,0x175);
    if (!projection(r, out, error,supplied) || !serialize(r, out.bytes, error)) return false;
    // Write omits free native pool slots. Read inserts its marker sequence into
    // a fresh pool, so artifact bindings describe serialized order, not live holes.
    std::vector<Binding> compact;
    if(!serialized_bindings(out.bindings,compact,error))return false;
    out.bindings=std::move(compact);
    ++captures; return true;
}

using HistoryFn = void(SK_THISCALL*)(void*);
HistoryFn original_start, original_commit, original_undo, original_redo;
HistoryFn original_tick_tools;
using ApplyMarkerFn=void(SK_THISCALL*)(void*,void*,uint32_t);
ApplyMarkerFn original_apply_marker;
HistoryFn original_ground_step,original_vertical_step,original_radial_step,original_auxiliary_step;
HistoryFn original_history_undo,original_history_redo;
using HistoryQueryFn=bool(SK_THISCALL*)(void*);
HistoryQueryFn original_has_undo,original_has_redo;
struct HeldActor { editor::ObjectId id; void* object; };
std::vector<HeldActor> gesture_actors;
void* gesture_before_resource;
std::map<uint32_t,editor::ObjectId> gesture_before_bindings;
void* gesture_owner;
void* gesture_entry;
uint64_t gesture_generation=0,gesture_serial=0,gesture_counter=0,cancellation_counter=0;
uint64_t ordinary_cancellations=0;
GestureScope gesture_scope_pin;
struct PendingCancellation {
    CancellationTicket ticket;
    void* mode;
    void* owner;
    uint64_t frame;
    std::vector<editor::ObjectId> selected;
    std::string failure;
};
std::optional<PendingCancellation> cancellation;
bool cancellation_queued=false,cancellation_settling=false;
using SelectFn=void(SK_THISCALL*)(void*,void*);
using ToolFn=uint32_t(SK_THISCALL*)(void*,uint32_t,uint32_t,uint32_t);
using CreateFn=bool(SK_THISCALL*)(void*,uint32_t);
using ImportFn=uint32_t(SK_THISCALL*)(void*,const editor::ResourceKey*);
using CloneSelectionFn=bool(SK_THISCALL*)(void*,void*);
SelectFn original_select;
ToolFn original_tool;
HistoryFn original_delete;
CreateFn original_create;
ImportFn original_import;
using RemoveClassFn=void(SK_THISCALL*)(void*,uint32_t);
RemoveClassFn original_remove_class;
CloneSelectionFn original_clone_selection;
using ScriptInputFn=bool(SK_THISCALL*)(void*,void*,void*);
ScriptInputFn original_script_input;
ScriptInputFn original_class_input;
using CardActionFn=bool(SK_THISCALL*)(void*,uint32_t,void*,uint32_t);
CardActionFn original_card_action;
using PaletteMessageFn=bool(SK_THISCALL*)(void*,uint32_t,uint32_t);
PaletteMessageFn original_palette_message;
using SpeechApplyFn=void(SK_CDECL*)();
SpeechApplyFn original_speech_apply;
SpeechApplyFn original_behavior_apply;
struct HeldTool {
    GestureScope scope;
    uint64_t generation;
    editor::ObjectId object;
    editor::Revision revision;
    void* manager;
    uint32_t action,a,b;
    uintptr_t auxiliary;
    bool released=false;
    double started=0;
    uint32_t modifiers=0;
    bool native_started=false,focused=false;
};
std::optional<HeldTool> held_tool;
struct HeldCreation {
    GestureScope scope;
    uint64_t generation;
    uint32_t class_index;
    editor::Revision revision;
    void* manager;
    double started;
    uint32_t modifiers;
    bool released=false,focused=false;
};
std::optional<HeldCreation> held_creation;
std::optional<std::deque<editor::ResourceKey>> held_import;
struct HeldClassRemoval {
    GestureScope scope;
    uint64_t generation;
    uint32_t index;
    editor::Revision revision;
    double started;
    void* listener=nullptr;
};
std::optional<HeldClassRemoval> held_class_removal;
GestureScope import_scope;
uint64_t import_generation=0;
double import_started=0;
bool input_queued=false;
void SK_THISCALL commit(void*);
uint32_t SK_THISCALL import_native(void*,const editor::ResourceKey*);
void SK_THISCALL remove_class_native(void*,uint32_t);
void* palette_listener(){
    void* display=skg::readable(mode(),0x18)?skg::field<void*>(mode(),0x14):nullptr;
    auto* palette=skg::readable(display,0x18)?skg::field<uint8_t*>(display,0x14):nullptr;
    return skg::readable(palette,0xC)?palette+8:nullptr;
}
struct Pending {
    void* entry;
    void* before;
    void* owner;
    uint64_t generation;
    uint32_t act;
    std::optional<editor::ObjectId> clone_source;
    std::map<uint32_t,editor::ObjectId> before_bindings,after_bindings;
    GestureScope scope;
    void* speech=nullptr;
};
std::deque<Pending> pending;
bool speech_pending(){return std::any_of(pending.begin(),pending.end(),[](const auto& item){return item.speech!=nullptr;});}

void SK_CDECL drain(void*) {
    editor_native::state(); // Retire a replaced native owner before old callbacks.
    queued = false;
    auto work = std::move(pending);
    pending.clear();
    for (const auto& item : work) {
        if(item.speech && a_speech_ui && *reinterpret_cast<void**>(a_speech_ui)==item.speech && active() && item.generation==generation &&
           (!C.gesture_scope || item.scope==C.gesture_scope())){
            pending.push_back(item);continue;
        }
        Owned entry(item.entry),before(item.before);
        if (!active() || applying || item.owner != data(mode()) || item.generation != generation ||
            (C.gesture_scope && item.scope!=C.gesture_scope())) continue;
        Gesture gesture;
        gesture.generation = generation;
        gesture.clone_source = item.clone_source;
        std::string error;
        bool ok=capture_resource(before.p,item.act,gesture.before,error,&item.before_bindings);
        ok=ok&&capture_resource(skg::field<void*>(entry.p,0x14),item.act,gesture.after,error,&item.after_bindings);
        if(ok){
            if (C.gesture) C.gesture(std::move(gesture));
        } else if (C.failure) C.failure(error);
    }
}
void SK_THISCALL start(void* self) {
    editor_native::state();
    bool observe=!applying && !cancellation && active() && self==data(mode()) && *reinterpret_cast<uint8_t*>(a_history+0x20);
    if(observe){
        gesture_owner=self;gesture_entry=nullptr;gesture_generation=generation;
        gesture_scope_pin=C.gesture_scope?C.gesture_scope():GestureScope{};
        gesture_serial=gesture_counter==UINT64_MAX?0:++gesture_counter;
        gesture_clone=input_clone;
        gesture_cast=input_cast;
        release(gesture_before_resource);
        gesture_before_resource=construct();
        if(gesture_before_resource)skg::thiscall<void*>(a_copy,gesture_before_resource,resource(self));
        // Native Start reuses its previous entry.after, which is stale after
        // canonical remote application. This independent owned before is the
        // current authored resource; serialize it only in the deferred drain.
    }
    original_start(self);
    if(observe && !*reinterpret_cast<uint8_t*>(a_history+0x20)){
        gesture_entry=*reinterpret_cast<void**>(a_history+0x24);
        for(auto actor:gesture_actors)release(actor.object);
        gesture_actors.clear();
        Rows map;
        if(rows(resource(self),0x2C10,0x238,kPlacementLimit,map))for(auto p=map.begin;p!=map.end;p+=0x238){
            if(!live_slot(p))continue;
            uint32_t key=slot_index(map,p);auto id=marker_id(key);
            bindings[key]=id;incarnations[key]=std::max(incarnations[key],1u);
            void* object=skg::thiscall<void*>(a_object,self,p+4);
            if(object){addref(object);gesture_actors.push_back({id,object});}
        }
        gesture_before_bindings=bindings;
    }
}

bool valid_scope(const GestureScope& scope){
    return scope.epoch && scope.author.slot<4 && scope.author.generation &&
        std::any_of(scope.project.begin(),scope.project.end(),[](uint8_t byte){return byte!=0;});
}
bool cancellation_current(const PendingCancellation& item){
    const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    return active() && item.owner==data(mode()) && item.mode==mode() && item.ticket.generation==generation &&
        item.ticket.gesture==gesture_serial && item.ticket.serial==cancellation_counter &&
        scope==item.ticket.scope;
}
void SK_CDECL drain_cancellation(void*){
    cancellation_queued=false;
    if(!H || !cancellation)return;
    const auto observed=editor_native::state();
    if(!cancellation_current(*cancellation)){
        cancellation.reset();return; // Only the replaced operation's hold is retired.
    }
    if(H->frame_index()<=cancellation->frame || observed.gesture || applying)return;
    cancellation_settling=true;
    const auto ready=editor_native::state();
    if(!ready.ready){cancellation_settling=false;return;}
    const auto item=*cancellation;
    std::string error=item.failure;
    bool okay=false;
    if(error.empty()){
        // Native finalization may have cleared selection. Preserve the live
        // independent choice, if that stable placement survived cancellation.
        void* target=item.selected.empty()?nullptr:marker_for_id(resource(item.owner),item.selected.front());
        void* object=target?skg::thiscall<void*>(a_object,item.owner,target):nullptr;
        void* tools=*reinterpret_cast<void**>(a_tools);
        if(tools)original_select(tools,object);
        refresh_selection();
        if(C.cancelled)okay=C.cancelled(item.ticket,error);
        else error="Native cancellation settlement is unavailable. Recover the committed draft.";
    }
    const bool current=cancellation_current(item);
    if(cancellation && cancellation->ticket==item.ticket)cancellation.reset();
    cancellation_settling=false;
    if(current && !okay && C.failure)
        C.failure(error.empty()?"Native cancellation could not restore the committed draft. Recover it.":error);
}

void cancel_current_entry(void* self){
    // F3BF00 invokes this exact native entry Undo from the invalid-placement
    // finalizer. It is not a request to move the user's chronological cursor.
    refresh_selection();
    const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    editor_native::state();
    // A reentrant finalizer owns the same existing settlement; never replace
    // its ticket, restore twice or turn it into a chronological Undo.
    if(cancellation)return;
    const bool entry_owned=skg::readable(self,0x18) && skg::field<uintptr_t>(self,0)==a_entry_vtable &&
        self==gesture_entry && self==*reinterpret_cast<void**>(a_history+0x24) &&
        skg::field<void*>(self,0x0C)==data(mode()) && gesture_owner==data(mode()) &&
        gesture_generation==generation && gesture_serial && gesture_before_resource;
    const bool owned=entry_owned && valid_scope(gesture_scope_pin) && scope==gesture_scope_pin;
    const bool exhausted=cancellation_counter==UINT64_MAX;
    if(!exhausted)++cancellation_counter;
    // A known entry retains Start's logical identity even if End/recovery has
    // replaced that scope before its finalizer. The stale ticket then retires
    // only its old hold without restoring or freezing the replacement.
    PendingCancellation item{{entry_owned?gesture_scope_pin:scope,generation,gesture_serial,cancellation_counter},mode(),data(mode()),
        H->frame_index(),selection,{}};
    if(exhausted){
        item.failure="Native cancellation serial is exhausted. Recover the committed draft.";
    }else if(!owned){
        item.failure="An in-progress native cancellation did not match its owned entry. Recover the committed draft.";
    }else{
        Owned before(construct());
        if(!before.p)item.failure="Native cancellation could not allocate its before snapshot. Recover the committed draft.";
        else{
            skg::thiscall<void*>(a_copy,before.p,gesture_before_resource);
            memcpy(skg::ptr_add<void>(before.p,0x2C30),skg::ptr_add<void>(resource(item.owner),0x2C30),0x20);
            applying=true;
            skg::thiscall<void>(a_restore,self,before.p);
            applying=false;
            bindings=gesture_before_bindings;
        }
    }
    cancellation=std::move(item);
    release(gesture_before_resource);gesture_before_resource=nullptr;
    for(auto actor:gesture_actors)release(actor.object);
    gesture_actors.clear();gesture_before_bindings.clear();
    // A synchronous host dispatcher still cannot settle inside the native
    // finalizer: the ticket requires a subsequent actual frame in the drain.
    if(!cancellation_queued){cancellation_queued=true;H->run_outside_frame(drain_cancellation,nullptr);}
}
void finish_held_input();
void SK_THISCALL select_native(void* self,void* object){
    MarkerAudio audio(data(mode()),active() && object && a_marker?skg::thiscall<void*>(a_marker,data(mode()),object):nullptr);
    void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    if(held_tool && skg::readable(context,8) && skg::field<void*>(context,4)!=object)finish_held_input();
    original_select(self,object);
    if(!applying && editing())refresh_selection();
    if(held_tool && (selection.empty() || selection.front()!=held_tool->object))finish_held_input();
    if(!applying && active() && !selection.empty() && C.gate)C.gate(selection);
}
bool real_focus(){
    GUITHREADINFO info{};info.cbSize=sizeof info;
    const auto window=H && H->game_window?static_cast<HWND>(H->game_window()):nullptr;
    return window && GetGUIThreadInfo(GetCurrentThreadId(),&info) && (info.hwndFocus==window || IsChild(window,info.hwndFocus));
}
void finish_held_input(){
    if(!held_tool)return;
    const auto item=*held_tool;held_tool.reset();
    if(item.native_started && active() && !cancellation && item.generation==generation &&
       item.manager==*reinterpret_cast<void**>(a_tools) && state().gesture)
        original_tool(item.manager,0,0,0);
}
void deny_held_input(const std::vector<editor::Scope>& scopes){
    for(const auto& scope:scopes){
        if(held_tool && (scope.kind==editor::ScopeKind::Document || scope.object==held_tool->object))finish_held_input();
        if(scope.kind==editor::ScopeKind::Document){
            if(held_import)H->log(M,SK_DEBUG,"Cast queue denied: keys=%u",unsigned(held_import->size()));
            held_import.reset();
        }
        if(held_class_removal && (scope.kind==editor::ScopeKind::Document || scope.object==editor::ObjectId{(2ull<<32)|held_class_removal->index,1}))held_class_removal.reset();
        if(held_creation && (scope.kind==editor::ScopeKind::Document || scope.object==editor::ObjectId{(2ull<<32)|held_creation->class_index,1}))held_creation.reset();
    }
}
// Native Start prepares local drag anchors/history. Authoring steps remain
// stopped until ownership arrives, including between the press and release.
void SK_THISCALL tick_tools(void* self){if(!held_tool)original_tick_tools(self);}
void* selected_marker(){
    if(!active())return nullptr;
    void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    void* object=skg::readable(context,8)?skg::field<void*>(context,4):nullptr;
    return object && a_marker?skg::thiscall<void*>(a_marker,data(mode()),object):nullptr;
}
void SK_THISCALL ground_step(void* self){if(!held_tool){MarkerAudio audio(data(mode()),selected_marker());original_ground_step(self);}}
void SK_THISCALL vertical_step(void* self){if(!held_tool){MarkerAudio audio(data(mode()),selected_marker());original_vertical_step(self);}}
void SK_THISCALL radial_step(void* self){if(!held_tool){MarkerAudio audio(data(mode()),selected_marker());original_radial_step(self);}}
void SK_THISCALL auxiliary_step(void* self){if(!held_tool){MarkerAudio audio(data(mode()),selected_marker());original_auxiliary_step(self);}}
void SK_THISCALL apply_marker_state(void* self,void* marker,uint32_t flags){
    MarkerAudio audio(self,marker);original_apply_marker(self,marker,flags);
}
void SK_CDECL continue_input(void*){
    input_queued=false;
    if(held_creation){
        const auto item=*held_creation;
        const auto observed=state();
        const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
        const auto revision=C.object_revision?C.object_revision({(2ull<<32)|item.class_index,1}):std::nullopt;
        if(!active() || applying || !observed.ready || observed.gesture || observed.generation!=item.generation || scope!=item.scope ||
           !revision || *revision!=item.revision || item.manager!=*reinterpret_cast<void**>(a_tools) ||
           H->time_seconds()-item.started>2 || (GetKeyState(VK_ESCAPE)&0x8000) || (item.focused && !real_focus()))held_creation.reset();
        else if(C.structure_gate && C.structure_gate(item.class_index)){
            held_creation.reset();
            auto& modifiers=skg::field<uint32_t>(mode(),0x40);const auto previous=modifiers;modifiers=item.modifiers;
            auto& latch=skg::field<uint8_t>(item.manager,4);const auto previous_latch=latch;latch=1;
            // F00670 owns StartHistory, marker creation and cursor attachment.
            // Invoke it only after the structure grant, then consume the same
            // cursor movement and release through its ordinary tool finalizer.
            if(original_create(item.manager,item.class_index)){
                original_tick_tools(item.manager);
                if(item.released)original_tool(item.manager,0,0,0);
            }
            latch=previous_latch;
            modifiers=previous;
        }
    }
    if(held_import){
        const auto scope=C.import_scope?C.import_scope():C.gesture_scope?C.gesture_scope():GestureScope{};
        // Upload and canonical application already have their own cancellation
        // and transfer bounds. Their latency is not an ownership-input timeout
        // for the next key selected in the same native picker.
        if(!pending.empty() || queued || gesture_before_resource || (C.transaction_pending && C.transaction_pending()))
            import_started=H->time_seconds();
        if(!active() || state().generation!=import_generation || scope!=import_scope || H->time_seconds()-import_started>2 || (GetKeyState(VK_ESCAPE)&0x8000)){
            H->log(M,SK_DEBUG,"Cast queue retired: keys=%u active=%u generation=%u scope=%u elapsed=%.3f",unsigned(held_import->size()),active(),state().generation==import_generation,scope==import_scope,H->time_seconds()-import_started);
            held_import.reset();
        }
        else if(pending.empty() && !queued && !gesture_before_resource &&
                !(C.transaction_pending && C.transaction_pending()) && C.structure_gate && C.structure_gate(0)){
            const auto key=held_import->front();held_import->pop_front();
            H->log(M,SK_DEBUG,"Cast queue drain: key=%08X!%08X.%08X remaining=%u",key.group,key.instance,key.type,unsigned(held_import->size()));
            if(held_import->empty())held_import.reset();
            import_started=H->time_seconds();import_native(data(mode()),&key);
        }
    }
    if(held_class_removal){
        const auto item=*held_class_removal;
        const auto revision=C.object_revision?C.object_revision({(2ull<<32)|item.index,1}):std::nullopt;
        const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
        if(!active() || generation!=item.generation || scope!=item.scope || !revision || *revision!=item.revision ||
           (item.listener && item.listener!=palette_listener()) || H->time_seconds()-item.started>2 || (GetKeyState(VK_ESCAPE)&0x8000))held_class_removal.reset();
        else if(C.remove_class_gate && C.remove_class_gate(item.index)){
            held_class_removal.reset();
            if(item.listener)original_palette_message(item.listener,0x07326686,item.index);
            else remove_class_native(data(mode()),item.index);
        }
    }
    if(!held_tool)return;
    const auto observed=state();
    const auto item=*held_tool;
    refresh_selection();
    const auto revision=C.object_revision?C.object_revision(item.object):std::nullopt;
    const auto current_scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    if(!active() || applying || !observed.ready || (!item.native_started && observed.gesture) || item.generation!=generation ||
       item.scope!=current_scope || !revision || *revision!=item.revision || selection!=std::vector<editor::ObjectId>{item.object} ||
       item.manager!=*reinterpret_cast<void**>(a_tools) || !skg::readable(context,0x2C) ||
       skg::field<uintptr_t>(context,0x10)!=item.auxiliary || (GetKeyState(VK_ESCAPE)&0x8000) ||
       (item.focused && !real_focus()) || H->time_seconds()-item.started>2){
        finish_held_input();return;
    }
    if(!C.gate || !C.gate(selection))return;
    held_tool.reset();
    // ToolAction tests this native input latch before starting a drag. A quick
    // release may have cleared it while the ownership grant was in flight.
    auto& latch=skg::field<uint8_t>(item.manager,4);
    const auto previous=latch;latch=1;
    auto& modifiers=skg::field<uint32_t>(mode(),0x40);
    const auto current_modifiers=modifiers;modifiers=item.modifiers;
    if(!item.native_started)original_tool(item.manager,item.action,item.a,item.b);
    original_tick_tools(item.manager);
    if(item.released)original_tool(item.manager,0,0,0);
    modifiers=current_modifiers;
    latch=previous;
}
bool tool_rejection(size_t reason,uint32_t action,void* context,const State& observed){
    ++rejected;
    ++tool_rejection_counts[reason];
    const double now=H->time_seconds();
    // Input may retry every frame. Keep this diagnostic bounded globally and
    // per cause; it only reads the already-owned context and observed state.
    if(now-last_tool_rejection_log<0.25 || now-last_tool_rejection[reason]<1.0)return false;
    last_tool_rejection_log=now;last_tool_rejection[reason]=now;
    static constexpr const char* reasons[]={"context-auxiliary","empty-selection","missing-placement-gate",
        "placement-lease","missing-structure-gate","structure-lease","legacy-scale-disabled","clone-scale-disabled"};
    const bool readable=skg::readable(context,0x14);
    const bool handles=skg::readable(context,0x24);
    H->log(M,SK_INFO,"Shared editor tool rejected: reason=%s action=%u context=%p selected=%u object=%p auxiliary=%p rotation18=%p rotation1C=%p rotation20=%p ready=%u generation=%llu count=%llu",
        reasons[reason],action,context,uint32_t(selection.size()),readable?skg::field<void*>(context,4):nullptr,
        readable?skg::field<void*>(context,0x10):nullptr,handles?skg::field<void*>(context,0x18):nullptr,
        handles?skg::field<void*>(context,0x1C):nullptr,handles?skg::field<void*>(context,0x20):nullptr,unsigned(observed.ready),
        static_cast<unsigned long long>(observed.generation),static_cast<unsigned long long>(tool_rejection_counts[reason]));
    return true;
}
uint32_t SK_THISCALL tool_native(void* self,uint32_t action,uint32_t a,uint32_t b){
    const State observed=editor_native::state();
    MarkerAudio audio(data(mode()),selected_marker());
    if(!applying && active() && speech_pending() && action!=0)return 1;
    if(!applying && active() && held_creation){
        if(action==0){held_creation->released=true;return 1;}
        if(action==1)return 1;
        held_creation.reset();
    }
    if(!applying && active() && held_tool){
        if(action==0){held_tool->released=true;return 1;}
        if(action==1)return 1;
        finish_held_input();
    }
    if(!applying && active() && (action==1 || action==6 || action==11)){
        if(cancellation){++rejected;return 1;}
        refresh_selection();
        void* context=*reinterpret_cast<void**>(a_context);
        if(legacy_scale_blocked(action,tracks_relative_scale())){
            tool_rejection(6,action,context,observed);
            const double now=H->time_seconds();
            if(C.notice && now-last_legacy_scale_notice>=5){
                last_legacy_scale_notice=now;
                C.notice("Scale is unavailable in this older shared draft. Save a native copy and start a new shared project to enable it.");
            }
            return 1;
        }
        if(clone_scale_blocked(action,observed.gesture,gesture_clone.has_value())){
            tool_rejection(7,action,context,observed);
            const double now=H->time_seconds();
            if(C.notice && now-last_clone_scale_notice>=5){
                last_clone_scale_notice=now;
                C.notice("Finish the clone before scaling it.");
            }
            return 1;
        }
        // Native +10 is also the active rotation manipulator. Only its three
        // verified pose handles may pass; path and unknown auxiliaries remain
        // blocked, including every auxiliary clone action.
        const uintptr_t auxiliary=skg::readable(context,0x14)?skg::field<uintptr_t>(context,0x10):0;
        std::array<uintptr_t,3> rotations{};
        if(skg::readable(context,0x24))rotations={skg::field<uintptr_t>(context,0x18),
            skg::field<uintptr_t>(context,0x1C),skg::field<uintptr_t>(context,0x20)};
        auto pose_handle=[&](uint32_t offset,uint32_t first,uint32_t second=0)->uintptr_t{
            if(!skg::readable(context,offset+4))return 0;
            const auto handle=skg::field<uintptr_t>(context,offset);
            if(!handle || handle!=auxiliary || !skg::readable(reinterpret_cast<void*>(handle),4))return 0;
            auto* vtable=skg::vtable(reinterpret_cast<void*>(handle));
            if(!skg::readable(vtable,0x3C) || !vtable[0x38/4])return 0;
            const auto type=skg::vcall<uint32_t>(reinterpret_cast<void*>(handle),0x38/4);
            return type==first || (second && type==second)?handle:0;
        };
        const auto ball=pose_handle(0x24,0x07ABDD8D,0x07A81824);
        const auto secondary=tracks_extended_poses()?pose_handle(0x14,0x07A81824):0;
        const auto destination=tracks_extended_poses()?pose_handle(0x28,0x076C67DF):0;
        if(!supported_tool_auxiliary(action,auxiliary,rotations,ball,destination,secondary)){tool_rejection(0,action,context,observed);return 1;}
        if(selection.empty()){tool_rejection(1,action,context,observed);return 1;}
        if(!C.gate){tool_rejection(2,action,context,observed);return 1;}
        if(!C.gate(selection)){
            tool_rejection(3,action,context,observed);
            const auto revision=selection.size()==1 && C.object_revision?C.object_revision(selection.front()):std::nullopt;
            if(action==1 && revision && C.gesture_scope && a_tool_tick && original_tick_tools && valid_scope(C.gesture_scope())){
                held_tool=HeldTool{C.gesture_scope(),generation,selection.front(),*revision,self,action,a,b,auxiliary,false,H->time_seconds(),skg::field<uint32_t>(mode(),0x40),true,real_focus()};
                original_tool(self,action,a,b);
                // Initialize the native grab at its original press point. The
                // four authored pose steps above are held during this tick.
                if(held_tool)original_tick_tools(self);
            }
            return 1;
        }
        if(action==11 && !C.structure_gate){tool_rejection(4,action,context,observed);return 1;}
        if(action==11 && !C.structure_gate(selected_class)){tool_rejection(5,action,context,observed);return 1;}
    }
    auto previous=input_clone;
    if(action==11 && !selection.empty())input_clone=selection.front();
    uint32_t result=original_tool(self,action,a,b);
    input_clone=previous;
    return result;
}
void SK_THISCALL delete_native(void* self){
    editor_native::state();
    if(!applying && active()){
        if(cancellation || speech_pending()){++rejected;return;}
        refresh_selection();
        if(selection.empty() || !C.gate || !C.gate(selection)) {++rejected;return;}
    }
    original_delete(self);
}
bool SK_THISCALL create_native(void* self,uint32_t class_id){
    editor_native::state();
    if(!applying && active()){
        if(cancellation || speech_pending()){++rejected;return false;}
        // Native EFEDC0 routes class -2 to authored avatar placement F432C0,
        // which is outside the shared placement roster and protected scope.
        if(class_id==UINT32_MAX-1 || !C.structure_gate){++rejected;return false;}
        if(!C.structure_gate(class_id)){
            const auto revision=C.object_revision?C.object_revision({(2ull<<32)|class_id,1}):std::nullopt;
            const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
            if(revision && valid_scope(scope) && !state().gesture && self==*reinterpret_cast<void**>(a_tools))
                held_creation=HeldCreation{scope,generation,class_id,*revision,self,H->time_seconds(),skg::field<uint32_t>(mode(),0x40),false,real_focus()};
            ++rejected;return false;
        }
    }
    return original_create(self,class_id);
}
bool SK_THISCALL clone_selection_native(void* self,void* object){
    editor_native::state();
    std::optional<editor::ObjectId> source;
    if(!applying && active()){
        if(cancellation || speech_pending()){++rejected;return false;}
        void* context=*reinterpret_cast<void**>(a_context);
        void* marker=object?skg::thiscall<void*>(a_marker,data(mode()),object):nullptr;
        Rows map;
        if(skg::readable(context,0x14) && !skg::field<void*>(context,0x10) &&
           marker && rows(resource(data(mode())),0x2C10,0x238,kPlacementLimit,map)){
            for(auto p=map.begin;p!=map.end;p+=0x238)if(live_slot(p) && p+4==marker){
                source=marker_id(slot_index(map,p));break;
            }
        }
        if(!source || !C.gate || !C.gate({*source}) || !C.structure_gate ||
           !C.structure_gate(skg::field<uint32_t>(marker,0))){++rejected;return false;}
    }
    // Alt-drag reaches this native entry directly as well as through action11.
    // Capture its source before native StartHistory and all cursor mutations.
    auto previous=input_clone;if(source)input_clone=source;
    const bool result=original_clone_selection(self,object);
    input_clone=previous;return result;
}
uint32_t SK_THISCALL import_native(void* self,const editor::ResourceKey* key){
    editor_native::state();
    if(!applying && active()){
        if(speech_pending() || !tracks_cast() || !key || self!=data(mode()) || !C.structure_gate)return UINT32_MAX;
        if(!pending.empty() || queued || gesture_before_resource ||
           (C.transaction_pending && C.transaction_pending()) || !C.structure_gate(0)){
            const auto scope=C.import_scope?C.import_scope():C.gesture_scope?C.gesture_scope():GestureScope{};
            if(!valid_scope(scope))return UINT32_MAX;
            if(!held_import){
                held_import.emplace();import_scope=scope;
                import_generation=generation;import_started=H->time_seconds();
            }
            if(import_generation!=generation || import_scope!=scope)return UINT32_MAX;
            if(std::find(held_import->begin(),held_import->end(),*key)==held_import->end()){
                if(held_import->size()>=32){if(C.notice)C.notice("Choose up to 32 cast assets at a time");return UINT32_MAX;}
                held_import->push_back(*key);
                H->log(M,SK_DEBUG,"Cast queued: key=%08X!%08X.%08X keys=%u",key->group,key->instance,key->type,unsigned(held_import->size()));
            }
            return UINT32_MAX;
        }
        const bool prior=input_cast;input_cast=true;
        start(self);
        const auto result=original_import(self,key);
        H->log(M,SK_DEBUG,"Cast native import: key=%08X!%08X.%08X class=%u",key->group,key->instance,key->type,result);
        if(result!=UINT32_MAX)commit(self);
        else {
            cancel_current_entry(*reinterpret_cast<void**>(a_history+0x24));
            original_commit(self);
        }
        input_cast=prior;return result;
    }
    return original_import(self,key);
}
bool SK_THISCALL palette_message(void* self,uint32_t message,uint32_t payload){
    editor_native::state();
    if(!applying && active() && message==0x07326686 && self==palette_listener()){
        if(speech_pending() || !tracks_cast() || !C.remove_class_gate)return false;
        if(!C.remove_class_gate(payload)){
            const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
            const auto revision=C.object_revision?C.object_revision({(2ull<<32)|payload,1}):std::nullopt;
            if(valid_scope(scope) && revision)held_class_removal=HeldClassRemoval{scope,generation,payload,*revision,H->time_seconds(),self};
            return false;
        }
    }
    return original_palette_message(self,message,payload);
}
void SK_THISCALL remove_class_native(void* self,uint32_t index){
    editor_native::state();
    if(!applying && active()){
        if(speech_pending() || !tracks_cast() || self!=data(mode()) || !C.remove_class_gate)return;
        if(!C.remove_class_gate(index)){
            const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
            const auto revision=C.object_revision?C.object_revision({(2ull<<32)|index,1}):std::nullopt;
            if(valid_scope(scope) && revision)held_class_removal=HeldClassRemoval{scope,generation,index,*revision,H->time_seconds()};
            return;
        }
        const auto previous=input_cast;input_cast=true;
        start(self);original_remove_class(self,index);commit(self);
        input_cast=previous;return;
    }
    original_remove_class(self,index);
}
void* script_ui(){
    void* display=skg::readable(mode(),0x18)?skg::field<void*>(mode(),0x14):nullptr;
    return skg::readable(display,0x1C)?skg::field<void*>(display,0x18):nullptr;
}
bool SK_THISCALL script_input(void* self,void* window,void* message){
    auto* script=static_cast<uint8_t*>(script_ui());
    if(!applying && active() && script && self==script+8){
        const auto type=skg::readable(message,0x10)?skg::field<uint32_t>(message,8):0;
        if(held_creation && type==sui::kMsgMouseUp)held_creation->released=true;
        if(script_events<64 && (type==0x287259F6 || (type>=6 && type<=8))){
            ++script_events;
            H->log(M,SK_DEBUG,"Shared editor script input: type=%08X control=%08X window=%08X cast=%u",
                skg::field<uint32_t>(message,8),skg::field<uint32_t>(message,0xC),window?sui::control_id(window):0,unsigned(tracks_cast()));
        }
        // Permit only the exact cast inspector subtree. Timeline, goals,
        // terrain and adventure-wide authoring retain their separate fences.
        auto* settings=skg::readable(script,0x1C)?skg::field<uint8_t*>(script,0x18):nullptr;
        void* layout=skg::readable(settings,0x24)?skg::field<void*>(settings,0x10):nullptr;
        auto root=layout?skg::thiscall<sui::Win>(a_layout_find,layout,uint32_t(0x073E6BA0),true):nullptr;
        for(auto parent=window;parent && root;parent=sui::parent(parent))if(parent==root){
            if(tracks_cast() && C.class_gate && C.class_gate(skg::field<uint32_t>(settings,0x20))){
                const auto previous=input_cast;input_cast=true;
                const auto result=original_script_input(self,window,message);input_cast=previous;return result;
            }
            break;
        }
        return false;
    }
    return original_script_input(self,window,message);
}
bool SK_THISCALL class_input(void* self,void* window,void* message){
    auto* script=static_cast<uint8_t*>(script_ui());
    auto* settings=script && skg::readable(script,0x1C)?skg::field<uint8_t*>(script,0x18):nullptr;
    if(!applying && active() && settings && self==settings+0xC){
        // Class settings register their own binder on each native descendant;
        // ScriptUI's parent IWinProc does not own these slider notifications.
        // Preserve the normal dismiss control, whose native handler has no
        // authored setter branch. Guard other input before its native setter.
        const bool dismiss=skg::readable(message,0x10) && skg::field<uint32_t>(message,8)==0x287259F6 &&
           skg::field<uint32_t>(message,0xC)==0x0595E0A8;
        if(speech_pending() && !dismiss && !(skg::readable(message,0x10) && skg::field<uint32_t>(message,0xC)==0x074656A0))return false;
        if(!dismiss && (!tracks_cast() || !skg::readable(self,0x18) || !C.class_gate || !C.class_gate(skg::field<uint32_t>(self,0x14))))return false;
        const bool prior=input_cast;input_cast=true;
        const auto result=original_class_input(self,window,message);input_cast=prior;return result;
    }
    return original_class_input(self,window,message);
}
bool SK_THISCALL card_action(void* self,uint32_t first,void* window,uint32_t third){
    if(!applying && active() && window){
        const auto control=sui::control_id(window);
        // Randomize replaces the asset and recreates actors. The supported
        // inspector disguise picker has its own class ownership boundary.
        if(control==0x073272B0){
            if(C.notice)C.notice("Asset randomization is unavailable while sharing.");
            return true;
        }
    }
    return original_card_action(self,first,window,third);
}
void SK_CDECL speech_apply(){
    if(!applying && active()){
        void* ui=a_speech_ui?*reinterpret_cast<void**>(a_speech_ui):nullptr;
        if(!skg::readable(ui,8) || !owned_class_gate(skg::field<void*>(ui,4)))return;
        const auto previous=input_cast,previous_speech=input_speech;input_cast=input_speech=true;
        original_speech_apply();input_cast=previous;input_speech=previous_speech;return;
    }
    original_speech_apply();
}
void SK_CDECL behavior_apply(){
    if(!applying && active()){
        void* owner=a_behavior_data?*reinterpret_cast<void**>(a_behavior_data):nullptr;
        if(!skg::readable(owner,4) || !owned_class_gate(skg::field<void*>(owner,0)))return;
        const auto previous=input_cast;input_cast=true;original_behavior_apply();input_cast=previous;return;
    }
    original_behavior_apply();
}
void SK_THISCALL commit(void* self) {
    bool observe = !applying && active() && self == data(mode()) &&
        !*reinterpret_cast<uint8_t*>(a_history + 0x20);
    void* entry = observe ? *reinterpret_cast<void**>(a_history + 0x24) : nullptr;
    addref(entry);
    if(entry){
        Rows map;std::map<uint32_t,editor::ObjectId> after;
        if(rows(resource(self),0x2C10,0x238,kPlacementLimit,map)){
            for(const auto& actor:gesture_actors){
                void* marker=skg::thiscall<void*>(a_marker,self,actor.object);
                if(!marker)continue;
                for(auto p=map.begin;p!=map.end;p+=0x238)if(live_slot(p) && p+4==marker){
                    after[slot_index(map,p)]=actor.id;break;
                }
            }
            for(auto p=map.begin;p!=map.end;p+=0x238){
                if(!live_slot(p))continue;
                uint32_t key=slot_index(map,p);
                if(!after.count(key)){
                    // Inactive placements may have no instantiated actor. Native
                    // pool deletion never compacts other indices, so a surviving
                    // uninstantiated slot retains its previous explicit binding.
                    auto previous=gesture_before_bindings.find(key);
                    // A disguise or other class refresh can replace its scene
                    // actor without replacing the authored marker. Native pool
                    // slots do not compact. A live slot with the same class in
                    // the independently owned before retains its identity even
                    // when the old actor is no longer mapped by ScenarioData.
                    void* old_marker=marker_for_key(gesture_before_resource,key);
                    if(previous!=gesture_before_bindings.end() && old_marker &&
                       skg::field<uint32_t>(old_marker,0)==skg::field<uint32_t>(p,4)){
                        after[key]=previous->second;continue;
                    }
                    if(gesture_cast && previous!=gesture_before_bindings.end()){after[key]=previous->second;continue;}
                    bool observed=previous!=gesture_before_bindings.end() && std::any_of(gesture_actors.begin(),gesture_actors.end(),
                        [&](const HeldActor& actor){return actor.id==previous->second;});
                    if(previous!=gesture_before_bindings.end() && !observed){after[key]=previous->second;continue;}
                    uint32_t& incarnation=incarnations[key];
                    if(incarnation==UINT32_MAX){++rejected;continue;}
                    ++incarnation;
                    after[key]={(1ull<<32)|key,incarnation};
                }
            }
            bindings=std::move(after);
        }
    }
    void* before=observe?std::exchange(gesture_before_resource,nullptr):nullptr;
    original_commit(self);
    for(auto actor:gesture_actors)release(actor.object);
    gesture_actors.clear();
    if (!entry) {release(before);return;}
    void* speech=input_speech && a_speech_ui?*reinterpret_cast<void**>(a_speech_ui):nullptr;
    if(before && speech && !pending.empty()){
        auto& previous=pending.back();
        if(previous.speech==speech && previous.owner==self && previous.generation==generation && previous.scope==gesture_scope_pin){
            // Native speech commits every character. Retain the first owned
            // before and the latest owned after until this dialog closes.
            release(previous.entry);release(before);previous.entry=entry;previous.after_bindings=bindings;return;
        }
    }
    if (!before || pending.size() >= 8) {
        release(entry);release(before); ++rejected;
        if (C.failure) C.failure(before?"Native editor gestures exceeded the pending transaction limit":"Native editor before snapshot allocation failed");
        return;
    }
    uint32_t act = skg::cdecl_call<uint32_t>(a_act);
    pending.push_back({entry,before,self,generation,act,gesture_clone,gesture_before_bindings,bindings,gesture_scope_pin,speech});
    if (!queued) { queued = true; H->run_outside_frame(drain, nullptr); }
}
void SK_THISCALL undo_native(void* self) {
    editor_native::state();
    if (!applying && active()) {
        if(cancellation)return;
        if(!*reinterpret_cast<uint8_t*>(a_history+0x20)){
            cancel_current_entry(self);return;
        }
        if (C.history) C.history(false); return;
    }
    // Observe the original ordinary cancellation branch without changing it.
    // User history Undo has no matching ownedEntry24 and is not counted.
    if(!applying && editing() && !*reinterpret_cast<uint8_t*>(a_history+0x20) &&
       self==*reinterpret_cast<void**>(a_history+0x24) && skg::readable(self,0x18) &&
       skg::field<uintptr_t>(self,0)==a_entry_vtable && skg::field<void*>(self,0x0C)==data(mode()) &&
       ordinary_cancellations!=UINT64_MAX)++ordinary_cancellations;
    original_undo(self);
}
void SK_THISCALL redo_native(void* self) {
    if (!applying && active()) { if (!cancellation && C.history) C.history(true); return; }
    original_redo(self);
}
void SK_THISCALL history_undo(void* self){
    if(!applying && active() && self==reinterpret_cast<void*>(a_history)){
        if(!cancellation && C.history)C.history(false);return;
    }
    original_history_undo(self);
}
void SK_THISCALL history_redo(void* self){
    if(!applying && active() && self==reinterpret_cast<void*>(a_history)){
        if(!cancellation && C.history)C.history(true);return;
    }
    original_history_redo(self);
}
bool SK_THISCALL has_undo(void* self){
    if(!applying && active() && self==reinterpret_cast<void*>(a_history))
        return !cancellation && C.history_available && C.history_available(false);
    return original_has_undo(self);
}
bool SK_THISCALL has_redo(void* self){
    if(!applying && active() && self==reinterpret_cast<void*>(a_history))
        return !cancellation && C.history_available && C.history_available(true);
    return original_has_redo(self);
}

bool install(const char* name, void* hook, void** original) {
    auto address = H->sym(name);
    return address && H->hook_create(M, reinterpret_cast<void*>(address), hook, original) == 0;
}
void restore_fences(){
    for(const auto& [window,flags]:fenced_windows){
        sui::set_enabled(window,(flags&sui::kFlagEnabled)!=0);
        sui::release(window);
    }
    fenced_windows.clear();
}
void disable_owned(sui::Win window){
    if(!window)return;
    if(!fenced_windows.count(window)){
        uint32_t flags=sui::flags(window);
        sui::add_ref(window);fenced_windows.emplace(window,flags);
    }
    sui::set_enabled(window,false);
}
void disable_properties(sui::Win root){
    std::vector<sui::Win> pending_windows;
    if(root)pending_windows.push_back(root);
    size_t visited=0;
    while(!pending_windows.empty() && visited++<2048){
        auto window=pending_windows.back();pending_windows.pop_back();
        // The owned close button remains usable. Native class input is also
        // guarded before its setter, including the first UI tick after open.
        if(sui::control_id(window)!=0x0595E0A8){
            if(fenced_windows.contains(window) || fenced_windows.size()<4096)disable_owned(window);
        }
        auto descendants=sui::children(window);
        if(descendants.size()>2048 || pending_windows.size()+descendants.size()>2048)break;
        pending_windows.insert(pending_windows.end(),descendants.begin(),descendants.end());
    }
}
void SK_CDECL fence_ui(void*){
    fence_queued=false;
    state();
    if(fenced_generation!=generation){restore_fences();fenced_generation=generation;}
    if(!supported || !active()){restore_fences();return;}
    // Unsupported authoring remains held. Save/Done/Cancel/Play are intercepted
    // by the native command adapter and retain their ordinary toolbar locations.
    constexpr uint32_t ids[]={0x07466E50,0x07104132,0x04766FF0,
        0x07108388,0x272EB68E};
    void* display=skg::readable(mode(),0x18)?skg::field<void*>(mode(),0x14):nullptr;
    void* main_layout=skg::readable(display,0x28)?skg::field<void*>(display,0x24):nullptr;
    if(skg::readable(main_layout,0x18))for(auto id:ids){
        // Global trees retain cached browser/editor duplicates of these IDs.
        disable_owned(skg::thiscall<sui::Win>(a_layout_find,main_layout,id,true));
    }
    // ScriptUI's class/act property binder owns a UILayout, not an IWindow.
    // Cast palette display+14 is separate and remains available for placement.
    void* script=script_ui();
    void* settings=skg::readable(script,0x1C)?skg::field<void*>(script,0x18):nullptr;
    void* layout=skg::readable(settings,0x14)?skg::field<void*>(settings,0x10):nullptr;
    if(skg::readable(layout,0x18)){
        // GetContainerWindow is the shared parent layer, not this layout's
        // owned subtree. Disable only its exact native property UI root.
        auto root=skg::thiscall<sui::Win>(a_layout_find,layout,uint32_t(0x073E6BA0),true);
        if(!tracks_cast())disable_properties(root);
        else if(root && sui::visible(root) && C.class_gate && skg::readable(settings,0x24))C.class_gate(skg::field<uint32_t>(settings,0x20));
    }
}
} // namespace

void deny_input(const std::vector<editor::Scope>& scopes){deny_held_input(scopes);}

namespace {
// Exact native camera access paths: F03880 uses the active camera (+38),
// EFDDC0 resolves the Adventure Editor camera ID (+40), both Cast(+0C) to
// 011966ED. Borrowed pointers are used only inside this synchronous call.
bool view_slot(void* object, unsigned index) {
    if (!skg::readable(object, sizeof(void*))) return false;
    void** vt=skg::vtable(object);
    return skg::readable(vt, (index+1)*sizeof(void*)) && vt[index];
}
void* view_camera(std::string& error) {
    if (!a_view_manager || !a_view_target || !a_view_orbit || !a_view_set_orbit || !a_view_lookat) {
        error="The verified local editor camera adapter is unavailable";return nullptr;
    }
    void* manager=skg::cdecl_call<void*>(a_view_manager);
    if (!view_slot(manager,0x50/4)) {error="The native camera manager is unavailable";return nullptr;}
    void* cameras=skg::vcall<void*>(manager,0x50/4);
    if (!view_slot(cameras,0x38/4) || !view_slot(cameras,0x40/4)) {
        error="The native editor camera manager is unavailable";return nullptr;
    }
    void* active_camera=skg::vcall<void*>(cameras,0x38/4);
    void* editor_camera=skg::vcall<void*>(cameras,0x40/4,uint32_t(0x00EBB801));
    if (!active_camera || active_camera!=editor_camera || !view_slot(active_camera,0x0C/4)) {
        error="Waiting for the active native Adventure Editor camera";return nullptr;
    }
    void* orbit=skg::vcall<void*>(active_camera,0x0C/4,uint32_t(0x011966ED));
    if (!skg::readable(orbit,0x318)) {error="The native editor orbit camera is unavailable";return nullptr;}
    return orbit;
}
bool finite_view(const LocalView& view) {
    for (float value:view.target) if (!std::isfinite(value)) return false;
    for (float value:view.rotation) if (!std::isfinite(value)) return false;
    for (float value:view.orbit) if (!std::isfinite(value)) return false;
    for (float value:view.settings) if (!std::isfinite(value)) return false;
    double radius=0,norm=0;
    for (float value:view.target) radius+=double(value)*value;
    for (float value:view.rotation) norm+=double(value)*value;
    return radius>1e-10 && norm>0.999 && norm<1.001 && view.orbit[2]>=0.1f;
}
void read_view(void* camera, LocalView& view) {
    // B10160 returns a borrowed static vector, copied before another call.
    const float* target=skg::thiscall<const float*>(a_view_target,camera);
    if (!skg::readable(target,sizeof(float)*3)) {view={};return;}
    std::memcpy(view.target.data(),target,sizeof(float)*3);
    // B133E0 final transform consumes the current quaternion AC, not the
    // desired CC used by native Save and the 32-byte ScenarioResource view.
    std::memcpy(view.rotation.data(),skg::ptr_add<void>(camera,0xAC),sizeof(float)*4);
    skg::thiscall<void>(a_view_orbit,camera,&view.orbit[0],&view.orbit[1],&view.orbit[2]);
    view.settings={skg::field<float>(camera,0x26C),skg::field<float>(camera,0x270),
        skg::field<float>(camera,0x310),skg::field<float>(camera,0x314)};
}
bool same_view(const LocalView& a,const LocalView& b) {
    auto close_enough=[](float x,float y){return std::fabs(double(x)-y)<=1e-5*std::max(1.0,std::max(std::fabs(double(x)),std::fabs(double(y))));};
    for(unsigned i=0;i<3;++i)if(!close_enough(a.target[i],b.target[i])||!close_enough(a.orbit[i],b.orbit[i]))return false;
    // A quaternion and its negation encode the same rotation.
    bool positive=true,negative=true;
    for(unsigned i=0;i<4;++i){positive&=close_enough(a.rotation[i],b.rotation[i]);negative&=close_enough(a.rotation[i],-b.rotation[i]);}
    return (positive||negative)&&a.settings==b.settings;
}
}

bool capture_view(LocalView& view, std::string& error) {
    const auto native=state();
    if (!native.ready || native.gesture || applying) {error="The native editor is not ready to capture its local view";return false;}
    void* camera=view_camera(error);if(!camera)return false;
    LocalView captured;read_view(camera,captured);
    if (!finite_view(captured)) {error="The native editor returned an invalid local view";return false;}
    if(state().generation!=native.generation) {error="The native editor changed while capturing its local view";return false;}
    view=captured;return true;
}

bool restore_view(const LocalView& view, uint64_t expected_generation, std::string& error) {
    const auto native=state();
    if (!native.ready || native.gesture || applying || native.generation!=expected_generation) {
        error="The native editor context is not ready to restore this local view";return false;
    }
    if(!finite_view(view)){error="The pinned local editor view is invalid";return false;}
    void* camera=view_camera(error);if(!camera)return false;
    LocalView current;read_view(camera,current);
    if(!finite_view(current)||current.settings!=view.settings) {
        error="The native editor camera settings changed during Test";return false;
    }
    const float low=skg::field<float>(camera,0x2AC),high=skg::field<float>(camera,0x2B0);
    if(!std::isfinite(low)||!std::isfinite(high)||low>high||view.orbit[1]<low||view.orbit[1]>high) {
        error="The pinned local editor view exceeds the native camera limits";return false;
    }
    if(state().generation!=expected_generation) {error="The native editor changed before restoring its local view";return false;}
    // Existing native instant LookAt restores current orbit point/quaternion.
    // B106C0 then sets all current/desired scalar values, including the actual
    // angles omitted from ScenarioResource's persisted camera fields.
    skg::thiscall<void>(a_view_lookat,camera,view.target.data(),view.rotation.data(),true);
    skg::thiscall<void>(a_view_set_orbit,camera,view.orbit[0],view.orbit[1],view.orbit[2]);
    LocalView restored;read_view(camera,restored);
    if(state().generation!=expected_generation || !finite_view(restored) || !same_view(view,restored)) {
        error="The native editor could not restore the pinned local view";return false;
    }
    return true;
}

bool init(const SkHost* host, SkMod* mod, Ctx context) {
    script_events=0;
    input_speech=false;
    H = host; M = mod; C = std::move(context);
    if (!H || std::string(H->exe_sha256()) != "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37") return false;
    a_mode=H->sym("App::cScenarioMode::ptr");
    a_history=H->sym("SporeCoop::Editor::History");
    a_act=H->sym("SporeCoop::Editor::GetAct");
    a_ctor=H->sym("SporeCoop::Editor::ScenarioResourceCtor");
    a_read=H->sym("Simulator::cScenarioResource::Read");
    a_write=H->sym("Simulator::cScenarioResource::Write");
    a_copy=H->sym("SporeCoop::Editor::CopyScenarioResource");
    a_entry=H->sym("SporeCoop::Editor::HistoryEntryCtor");
    a_entry_vtable=H->sym("SporeCoop::Editor::HistoryEntryVTable");
    a_restore=H->sym("SporeCoop::Editor::RestoreHistorySnapshot");
    a_alloc=H->sym("SporeCoop::Editor::Allocate");
    a_context=H->sym("SporeCoop::Editor::Context");
    a_tools=H->sym("SporeCoop::Editor::ToolManager");
    a_tool_tick=H->sym("SporeCoop::Editor::TickTools");
    a_marker=H->sym("Simulator::cScenarioData::GetMarkerForObject");
    a_object=H->sym("Simulator::cScenarioData::GetObjectForMarker");
    a_clone_marker=H->sym("SporeCoop::Editor::CloneMarker");
    a_create_marker=H->sym("SporeCoop::Editor::CreateMarker");
    a_delete_marker=H->sym("SporeCoop::Editor::DeleteMarker");
    a_apply_marker=H->sym("SporeCoop::Adventure::ApplyMarkerState");
    a_loading=H->sym("SporeCoop::Editor::LoadingScreen");
    a_layout_find=H->sym("SporeCoop::Editor::FindLayoutWindow");
    a_display_vtable=H->sym("SporeCoop::Editor::DisplayVtable");
    a_erase_history=H->sym("SporeCoop::Editor::EraseHistoryRange");
    a_discard_history=H->sym("SporeCoop::Editor::DiscardHistoryEntry");
    a_copy_marker_rows=H->sym("SporeCoop::Editor::CopyMarkerRows");
    a_copy_class_rows=H->sym("SporeCoop::Editor::CopyClassRows");
    a_copy_class=H->sym("SporeCoop::Editor::CopyClass");
    a_insert_class=H->sym("SporeCoop::Editor::ClassByIndex");
    a_speech_ui=H->sym("SporeCoop::Editor::SpeechUI");
    a_behavior_data=H->sym("SporeCoop::Editor::BehaviorData");
    a_insert_marker=H->sym("SporeCoop::Editor::InsertMarker");
    a_view_manager=H->sym("App::IGameModeManager::Get");
    a_view_target=H->sym("SporeCoop::Editor::CurrentOrbitTarget");
    a_view_orbit=H->sym("SporeCoop::Editor::CurrentOrbitScalars");
    a_view_set_orbit=H->sym("SporeCoop::Editor::SetOrbitScalarsInstant");
    a_view_lookat=H->sym("SporeCoop::SimCamera_LookAt");
    supported = a_mode && a_history && a_act && a_ctor && a_read && a_write && a_copy && a_entry && a_entry_vtable && a_restore && a_alloc &&
        a_context && a_tools && a_marker && a_object && a_clone_marker && a_create_marker && a_delete_marker && a_apply_marker && a_loading && a_layout_find && a_erase_history && a_discard_history && a_copy_marker_rows && a_insert_marker && a_copy_class_rows && a_copy_class && a_insert_class && a_speech_ui && a_behavior_data;
    if (!supported) return false;
    supported = install("SporeCoop::Editor::StartHistory", (void*)start, (void**)&original_start) &&
        install("SporeCoop::Editor::CommitHistory", (void*)commit, (void**)&original_commit) &&
        install("SporeCoop::Editor::UndoHistoryEntry", (void*)undo_native, (void**)&original_undo) &&
        install("SporeCoop::Editor::RedoHistoryEntry", (void*)redo_native, (void**)&original_redo) &&
        install("SporeCoop::Editor::SetSelection", (void*)select_native, (void**)&original_select) &&
        install("SporeCoop::Editor::ToolAction", (void*)tool_native, (void**)&original_tool) &&
        install("SporeCoop::Editor::TickTools",(void*)tick_tools,(void**)&original_tick_tools) &&
        install("SporeCoop::Editor::GroundDragStep",(void*)ground_step,(void**)&original_ground_step) &&
        install("SporeCoop::Editor::VerticalDragStep",(void*)vertical_step,(void**)&original_vertical_step) &&
        install("SporeCoop::Editor::RadialDragStep",(void*)radial_step,(void**)&original_radial_step) &&
        install("SporeCoop::Editor::AuxiliaryDragStep",(void*)auxiliary_step,(void**)&original_auxiliary_step) &&
        install("SporeCoop::Adventure::ApplyMarkerState",(void*)apply_marker_state,(void**)&original_apply_marker) &&
        install("SporeCoop::Editor::DeleteSelection", (void*)delete_native, (void**)&original_delete) &&
        install("SporeCoop::Editor::PlaceClass", (void*)create_native, (void**)&original_create) &&
        install("SporeCoop::Editor::CloneSelection", (void*)clone_selection_native, (void**)&original_clone_selection) &&
        install("SporeCoop::Editor::ImportClass", (void*)import_native, (void**)&original_import) &&
        install("SporeCoop::Editor::RemoveClass", (void*)remove_class_native, (void**)&original_remove_class) &&
        install("SporeCoop::Editor::UndoHistory",(void*)history_undo,(void**)&original_history_undo) &&
        install("SporeCoop::Editor::RedoHistory",(void*)history_redo,(void**)&original_history_redo) &&
        install("SporeCoop::Editor::HasUndo",(void*)has_undo,(void**)&original_has_undo) &&
        install("SporeCoop::Editor::HasRedo",(void*)has_redo,(void**)&original_has_redo) &&
        install("SporeCoop::Editor::ScriptInput",(void*)script_input,(void**)&original_script_input) &&
        install("SporeCoop::Editor::ClassInput",(void*)class_input,(void**)&original_class_input) &&
        install("SporeCoop::Editor::CastCardAction",(void*)card_action,(void**)&original_card_action) &&
        install("SporeCoop::Editor::PaletteMessage",(void*)palette_message,(void**)&original_palette_message) &&
        install("SporeCoop::Editor::ApplySpeech",(void*)speech_apply,(void**)&original_speech_apply) &&
        install("SporeCoop::Editor::ApplyBehaviors",(void*)behavior_apply,(void**)&original_behavior_apply);
    return supported;
}

void shutdown() {
    ++generation;
    input_speech=false;
    held_tool.reset();held_creation.reset();held_class_removal.reset();input_queued=false;
    held_import.reset();
    cancellation.reset();cancellation_queued=cancellation_settling=false;
    gesture_owner=gesture_entry=nullptr;gesture_generation=gesture_serial=0;gesture_scope_pin={};
    for (auto item : pending){release(item.entry);release(item.before);}
    release(gesture_before_resource);gesture_before_resource=nullptr;
    pending.clear();
    for(auto actor:gesture_actors)release(actor.object);
    gesture_actors.clear();gesture_before_bindings.clear();
    restore_fences();bindings.clear(); C = {}; supported = false;
}

void tick(){
    if(!H)return;
    if(!pending.empty() && !queued){queued=true;H->run_outside_frame(drain,nullptr);}
    if((held_tool || held_import || held_creation || held_class_removal) && !input_queued){input_queued=true;H->run_outside_frame(continue_input,nullptr);}
    if(cancellation && !cancellation_queued){cancellation_queued=true;H->run_outside_frame(drain_cancellation,nullptr);}
    if(fence_queued)return;
    double now=H->time_seconds();
    if(now-last_fence<0.2)return;
    last_fence=now;fence_queued=true;H->run_outside_frame(fence_ui,nullptr);
}

State state() {
    State s; s.available = supported;
    const bool scenario=game::mode_id()==modes::kScenario;
    void* m = mode(); void* d = data(m);
    if (m != observed_mode || d != observed_data || scenario!=observed_scenario) {
        observed_mode = m; observed_data = d; observed_scenario=scenario; ++generation;
        bindings.clear();incarnations.clear();selection.clear();
    }
    s.generation = generation;
    if (!supported || !scenario || !skg::readable(m, 0xDC)) return s;
    s.entry=skg::field<uint32_t>(m,0xD0);
    s.editing=editing();
    void* loading=a_loading?*reinterpret_cast<void**>(a_loading):nullptr;
    void* display=skg::field<void*>(m,0x14);
    const bool display_valid=skg::readable(display,0x28) &&
        (!a_display_vtable || skg::field<uintptr_t>(display,0)==a_display_vtable);
    void* layout=display_valid?skg::field<void*>(display,0x24):nullptr;
    void* container=skg::readable(layout,0x18)?skg::field<void*>(layout,0x14):nullptr;
    // data124 is invalidated by native thumbnail generation even in a settled
    // editor and preview. Require the successful native shell load instead.
    s.ready=(!cancellation || cancellation_settling) && s.editing && display_valid && skg::field<int>(display,0x1C)==0 && skg::readable(container,0x8C) &&
        skg::readable(loading,0x371) && !skg::field<bool>(loading,0x370);
    s.playtesting=s.entry==0 && skg::field<int>(m,0xCC)==2;
    void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    if(s.editing && skg::readable(context,4))s.act=skg::field<uint32_t>(context,0);
    s.selected=selection;
    s.gesture=!*reinterpret_cast<uint8_t*>(a_history+0x20) || (active() && speech_pending());
    s.history_cursor=*reinterpret_cast<uint32_t*>(a_history+0x14);
    uintptr_t begin=*reinterpret_cast<uintptr_t*>(a_history),end=*reinterpret_cast<uintptr_t*>(a_history+4);
    if(end>=begin && (end-begin)%4==0) s.history_size=uint32_t((end-begin)/4);
    return s;
}

void bind(const std::vector<Binding>& value) {
    bindings.clear();
    for (const auto& b : value){bindings[b.marker]=b.object;incarnations[b.marker]=std::max(incarnations[b.marker],1u);}
    selection.clear();
}

editor::ResourceKey avatar(){
    void* r=resource(data(mode()));
    return supported && skg::readable(r,kResourceSize)?skg::field<editor::ResourceKey>(r,0x130):editor::ResourceKey{};
}
bool avatar_locked(){
    void* r=resource(data(mode()));
    return supported && skg::readable(r,kResourceSize) && skg::field<bool>(r,0x175);
}

bool capture(Snapshot& out, std::string& error) {
    auto s=state();
    if (!s.ready || s.gesture || applying) { error="Native Adventure Editor is not ready for a snapshot"; return false; }
    refresh_selection();
    void* owner=data(mode());
    if(!capture_resource(resource(owner),s.act,out,error))return false;
    out.local_dirty=Snapshot::Dirty{skg::field<uint32_t>(owner,0x80),skg::field<bool>(owner,0x84),skg::field<bool>(owner,0x85)};
    Snapshot::Metadata metadata;metadata.genre=skg::field<uint32_t>(owner,0x7C);
    for(size_t i=0;i<metadata.text.size();++i) {
        const auto offset=0x4C+16*i;
        const auto begin=skg::field<uintptr_t>(owner,offset),end=skg::field<uintptr_t>(owner,offset+4);
        if(end<begin || (end-begin)%2 || end-begin>8192 || (end!=begin && !skg::readable(reinterpret_cast<void*>(begin),end-begin))) {
            error="The native adventure metadata is unavailable or oversized";return false;
        }
        if(end!=begin)metadata.text[i].assign(reinterpret_cast<const char16_t*>(begin),(end-begin)/2);
    }
    out.metadata=std::move(metadata);
    return true;
}
bool capture_draft(Snapshot& out,std::string& error) {
    Snapshot draft;if(!capture(draft,error))return false;
    const auto export_terrain=H->sym("SporeCoop::Editor::CaptureTerrain");
    void* m=mode();void* terrain=skg::readable(m,0x1C)?skg::field<void*>(m,0x18):nullptr;
    if(!export_terrain || !skg::readable(terrain,0x10) || !skg::readable(skg::field<void*>(terrain,0x0C),0x34)) {
        error="The native planet snapshot exporter is unavailable";return false;
    }
    Owned copy(parse(draft.bytes,error));if(!copy.p)return false;
    // F45201 exports into Save's owned scenario copy. Its destination is the
    // resource PropertyList at +18, not the active adventure or a database.
    skg::thiscall<void>(export_terrain,copy.p);
    if(!serialize(copy.p,draft.bytes,error) || !validate(draft,draft.document,error))return false;
    out=std::move(draft);return true;
}
uint32_t creation_group() {
    const auto address=H?H->sym("SporeCoop::Editor::CreationGroup"):0;
    return address && skg::readable(reinterpret_cast<void*>(address),4)?*reinterpret_cast<const uint32_t*>(address):0;
}
bool expand_snapshot(const Snapshot& input,bool relative,Snapshot& out,std::string& error){
    if(!supported){error="Native snapshot expansion is unavailable";return false;}
    Owned copy(parse(input.bytes,error));if(!copy.p)return false;
    std::map<uint32_t,editor::ObjectId> supplied;
    for(const auto& binding:input.bindings)if(!supplied.emplace(binding.marker,binding.object).second){error="Duplicate legacy placement binding";return false;}
    auto expanded=input;
    if(!projection(copy.p,expanded,error,&supplied,relative,true,true))return false;
    out=std::move(expanded);return true;
}
bool canonicalize_cast_state(editor::State& state,std::string& error){
    // Recovery may predate machine-hint normalization. Upgrade every retained
    // precondition together, without changing revisions or retiring history.
    std::map<std::vector<uint8_t>,editor::StructuredValue> cache;
    auto value=[&](editor::Value& v){
        auto* block=std::get_if<editor::StructuredValue>(&v);
        if(!block || block->format!=kCastBlockFormat){error="Invalid recovery cast block";return false;}
        auto found=cache.find(block->bytes);
        if(found==cache.end()){
            Owned owned(parse(block->bytes,error));Rows classes;
            if(!owned.p || !rows(owned.p,0x2BF4,0x27E8,1,classes) || classes.end-classes.begin!=0x27E8){error="Invalid recovery cast fragment";return false;}
            editor::StructuredValue normalized;
            if(!class_block(classes.begin,normalized,error))return false;
            found=cache.emplace(block->bytes,std::move(normalized)).first;
        }
        if(found->second.references!=block->references){error="Recovery cast normalization changed its references";return false;}
        *block=found->second;return true;
    };
    auto settings=[&](editor::ClassActSettings& settings){
        auto found=settings.properties.find(kCastBlockProperty);
        return found==settings.properties.end() || value(found->second);
    };
    auto patch=[&](editor::Patch& patch){
        for(auto& c:patch.classes)for(auto* side:{&c.before,&c.after})if(*side && !settings(**side))return false;
        for(auto& f:patch.fields)if(f.field==editor::FieldKey{editor::Field::Property,kCastBlockProperty})
            for(auto* side:{&f.before,&f.after})if(*side && !value(**side))return false;
        return true;
    };
    for(auto& [id,c]:state.document.class_settings){(void)id;if(!settings(c))return false;}
    for(auto& h:state.history)if(!patch(h.undo) || !patch(h.redo))return false;
    for(auto& [id,d]:state.decisions){(void)id;if(d.change && (!patch(d.change->patch) || !patch(d.change->inverse)))return false;}
    return true;
}
bool validate_entry(const std::vector<uint8_t>& bytes,uint32_t act,std::string& error) {
    if(!supported){error="Native editor validation is unavailable";return false;}
    Owned copy(parse(bytes,error));if(!copy.p)return false;
    Snapshot projected;projected.act=act;
    if(!projection(copy.p,projected,error))return false;
    projected.bytes=bytes;
    return validate(projected,projected.document,error);
}

bool supported_changes(const Gesture& gesture,std::string& error){
    if(!supported || !a_copy_marker_rows || !a_insert_marker){error="Native authored-change validation is unavailable";return false;}
    bool tracked=false;
    if(gesture.before.document.schema!=gesture.after.document.schema ||
       !relative_scale_mode(gesture.after.document,tracked,error)){
        error="Native placement-property schema changed during a gesture";return false;
    }
    Owned before(parse(gesture.before.bytes,error));if(!before.p)return false;
    Owned after(parse(gesture.after.bytes,error));if(!after.p)return false;
    Owned compared_before(construct()),compared_after(construct()),empty(construct());
    if(!compared_before.p || !compared_after.p || !empty.p){error="Native authored-change allocation failed";return false;}
    skg::thiscall<void*>(a_copy,compared_before.p,before.p);
    skg::thiscall<void*>(a_copy,compared_after.p,after.p);
    if(!gesture.after.document.class_schema.empty()){
        if(gesture.before.document.class_schema!=gesture.after.document.class_schema || !cast_schema(gesture.after.document,error))return false;
        // Class changes are represented by independently owned typed subtrees.
        // Compare the remaining scenario against the same class roster.
        skg::thiscall<void*>(a_copy_class_rows,skg::ptr_add<void>(compared_after.p,0x2BF4),skg::ptr_add<void>(compared_before.p,0x2BF4));
        skg::field<uint32_t>(compared_after.p,0x2C0C)=skg::field<uint32_t>(compared_before.p,0x2C0C);
    }
    // F42660 derives the validation bit and diagnostic from current cast data.
    // Preserve every authored goal field, and compare only these companions
    // against their prior values on our owned comparison copies.
    Rows old_acts,new_acts;
    if(!rows(compared_before.p,0x23C,0x534,8,old_acts) || !rows(compared_after.p,0x23C,0x534,8,new_acts)){
        error="Native authored act comparison is invalid";return false;
    }
    if(old_acts.end-old_acts.begin==new_acts.end-new_acts.begin){
        for(auto old_act=old_acts.begin,new_act=new_acts.begin;old_act!=old_acts.end;old_act+=0x534,new_act+=0x534){
            Rows old_goals,new_goals;
            if(!rows(old_act,0x84,0x188,3,old_goals) || !rows(new_act,0x84,0x188,3,new_goals)){
                error="Native authored goal comparison is invalid";return false;
            }
            if(old_goals.end-old_goals.begin!=new_goals.end-new_goals.begin)continue;
            for(auto a=old_goals.begin,b=new_goals.begin;a!=old_goals.end;a+=0x188,b+=0x188){
                skg::field<uint32_t>(b,0x14)=(skg::field<uint32_t>(b,0x14)&~1u)|(skg::field<uint32_t>(a,0x14)&1u);
                skg::field<uint32_t>(b,0x184)=skg::field<uint32_t>(a,0x184);
            }
        }
    }
    auto clear_markers=[&](void* resource){
        // Exactly the native DFFA00 composition: owning row copy followed by
        // its free-index/count metadata. Never edit vector ownership pointers.
        skg::thiscall<void*>(a_copy_marker_rows,skg::ptr_add<void>(resource,0x2C10),skg::ptr_add<void>(empty.p,0x2C10));
        skg::field<uint32_t>(resource,0x2C24)=skg::field<uint32_t>(empty.p,0x2C24);
        skg::field<uint32_t>(resource,0x2C28)=skg::field<uint32_t>(empty.p,0x2C28);
    };
    clear_markers(compared_before.p);clear_markers(compared_after.p);
    std::map<editor::ObjectId,uint32_t> before_indices,after_indices;
    for(const auto& binding:gesture.before.bindings)if(!before_indices.emplace(binding.object,binding.marker).second){
        error="Duplicate native before binding";return false;
    }
    for(const auto& binding:gesture.after.bindings)if(!after_indices.emplace(binding.object,binding.marker).second){
        error="Duplicate native after binding";return false;
    }
    auto marker=[&](void* resource,const auto& indices,editor::ObjectId id)->void*{
        auto found=indices.find(id);return found==indices.end()?nullptr:marker_for_key(resource,found->second);
    };
    for(const auto& [id,to]:gesture.after.document.placements){
        auto prior=gesture.before.document.placements.find(id);
        auto source=id;
        if(prior==gesture.before.document.placements.end()){
            // Verified native Create supplies defaults for an existing template.
            // A Clone must retain its source's opaque authored marker contents.
            if(!gesture.clone_source)continue;
            source=*gesture.clone_source;
            prior=gesture.before.document.placements.find(source);
            if(prior==gesture.before.document.placements.end()){error="Native clone source is absent";return false;}
        }
        if(prior->second.class_id!=to.class_id){error="Native template changes are read-only";return false;}
        void* old_marker=marker(before.p,before_indices,source);
        void* new_marker=marker(after.p,after_indices,id);
        if(!old_marker || !new_marker){error="Native authored-change placement binding is invalid";return false;}
        const auto old_index=skg::thiscall<uint32_t>(a_insert_marker,skg::ptr_add<void>(compared_before.p,0x2C10),old_marker);
        const auto new_index=skg::thiscall<uint32_t>(a_insert_marker,skg::ptr_add<void>(compared_after.p,0x2C10),new_marker);
        void* normalized_old=marker_for_key(compared_before.p,old_index);
        void* normalized_new=marker_for_key(compared_after.p,new_index);
        if(!normalized_old || !normalized_new){error="Native authored-change marker copy failed";return false;}
        skg::field<editor::Vec3>(normalized_new,4)=skg::field<editor::Vec3>(normalized_old,4);
        skg::field<editor::Rotation>(normalized_new,0x10)=skg::field<editor::Rotation>(normalized_old,0x10);
        if(gesture.after.document.schema.contains(kElevationProperty)){
            skg::field<float>(normalized_new,0x220)=skg::field<float>(normalized_old,0x220);
            if(gesture.after.document.schema.contains(kSoundPitchProperty)){
                skg::field<float>(normalized_new,0x1E0)=skg::field<float>(normalized_old,0x1E0);
                skg::field<float>(normalized_new,0x1E4)=skg::field<float>(normalized_old,0x1E4);
            }
            if(to.properties.contains(kDestinationPositionProperty)){
                skg::field<editor::Vec3>(normalized_new,0x1E8)=skg::field<editor::Vec3>(normalized_old,0x1E8);
                skg::field<editor::Rotation>(normalized_new,0x1F4)=skg::field<editor::Rotation>(normalized_old,0x1F4);
                skg::field<float>(normalized_new,0x204)=skg::field<float>(normalized_old,0x204);
            }
        }
        // F412C0 placement validation derives only bits10/2/4 from native
        // collision/placement checks. Other marker flags remain authored.
        const uint32_t old_flags=skg::field<uint32_t>(normalized_old,0x224);
        const uint32_t new_flags=skg::field<uint32_t>(normalized_new,0x224);
        skg::field<uint32_t>(normalized_new,0x224)=(new_flags&~0x16u)|(old_flags&0x16u);
        if(tracked){
            float old_relative=0,new_relative=0;
            if(!relative_scale_value(prior->second,true,old_relative,error) ||
               !relative_scale_value(to,true,new_relative,error))return false;
            if(source!=id && old_relative!=new_relative){
                error="A native clone must preserve its source scale-range companion";return false;
            }
            if(old_relative!=new_relative)
                skg::field<float>(normalized_new,0x24)=skg::field<float>(normalized_old,0x24);
        }
        if(prior->second.scale!=to.scale){
            skg::field<float>(normalized_new,0x20)=skg::field<float>(normalized_old,0x20);
            // Legacy drafts retain their historical scale20-only semantics.
            if(!tracked)skg::field<float>(normalized_new,0x24)=skg::field<float>(normalized_old,0x24);
        }
    }
    // Camera ownership is local. Every remaining serialized difference is an
    // unsupported authored field, including marker elevation/flags and globals.
    memcpy(skg::ptr_add<void>(compared_after.p,0x2C30),skg::ptr_add<void>(compared_before.p,0x2C30),0x20);
    std::vector<uint8_t> left,right;
    if(!serialize(compared_before.p,left,error) || !serialize(compared_after.p,right,error))return false;
    if(left!=right){
        // Diagnostic only: native serialization ends in a compressed DBPF.
        // Compare its decoded body rather than mistaking its byte length for
        // the authored field that actually changed. No native calls are added.
        auto payload=[](const std::vector<uint8_t>& input,std::vector<uint8_t>& output){
            static constexpr uint8_t magic[]={'D','B','P','F'};
            auto cursor=input.begin();
            for(unsigned attempt=0;attempt<4;++attempt){
                cursor=std::search(cursor,input.end(),std::begin(magic),std::end(magic));
                if(cursor==input.end())return false;
                dbpf::Package package;
                if(package.load(std::vector<uint8_t>(cursor,input.end()))){
                    const auto* entry=package.find({0x01897C18,0x01897C18,0x01897C18});
                    constexpr uint32_t diagnostic_limit=1u<<20;
                    if(entry && entry->size<=diagnostic_limit && entry->usize<=diagnostic_limit){
                        bool bounded=true;
                        if(entry->refpack){
                            const auto* packed=std::to_address(cursor)+entry->offset;
                            const size_t header=entry->size && (packed[0]&0x80)?6:5;
                            bounded=entry->size>=header;
                            uint32_t declared=0;
                            if(bounded)for(size_t i=2;i<header;++i)declared=(declared<<8)|packed[i];
                            bounded=bounded && declared<=diagnostic_limit;
                        }
                        if(bounded && package.read(*entry,output))return output.size()<=diagnostic_limit;
                    }
                }
                ++cursor;
            }
            return false;
        };
        if(H){
            unsigned logged=0;
            for(const auto& [id,to]:gesture.after.document.placements){
                auto prior=gesture.before.document.placements.find(id);
                if(prior==gesture.before.document.placements.end())continue;
                auto* old_marker=marker(before.p,before_indices,id);
                auto* new_marker=marker(after.p,after_indices,id);
                if(!old_marker || !new_marker)continue;
                const auto old_scale=skg::field<float>(old_marker,0x20),new_scale=skg::field<float>(new_marker,0x20);
                const auto old_relative=skg::field<float>(old_marker,0x24),new_relative=skg::field<float>(new_marker,0x24);
                if(old_scale==new_scale && old_relative==new_relative)continue;
                H->log(M,SK_INFO,"Shared editor Scale diagnostic: placement=%llu:%u scale20=%.9g/%.9g relative24=%.9g/%.9g elevation220=%08X/%08X flags224=%08X/%08X",
                    static_cast<unsigned long long>(id.id),id.generation,double(old_scale),double(new_scale),double(old_relative),double(new_relative),
                    skg::field<uint32_t>(old_marker,0x220),skg::field<uint32_t>(new_marker,0x220),
                    skg::field<uint32_t>(old_marker,0x224),skg::field<uint32_t>(new_marker,0x224));
                if(++logged==8)break;
            }
            std::vector<uint8_t> decoded_before,decoded_after;
            const bool decoded=payload(left,decoded_before) && payload(right,decoded_after);
            std::string differences;
            if(decoded){
                unsigned recorded=0;
                for(size_t i=0;i<std::min(decoded_before.size(),decoded_after.size()) && recorded<32;++i){
                    if(decoded_before[i]==decoded_after[i])continue;
                    char item[40];snprintf(item,sizeof item,"%zu:%02X>%02X ",i,unsigned(decoded_before[i]),unsigned(decoded_after[i]));
                    differences+=item;++recorded;
                }
            }
            H->log(M,SK_INFO,"Shared editor normalized payload diagnostic: decoded=%u size=%u/%u first32=%s",
                unsigned(decoded),unsigned(decoded_before.size()),unsigned(decoded_after.size()),differences.c_str());
        }
        const auto count=std::min(left.size(),right.size());
        size_t offset=0;while(offset<count && left[offset]==right[offset])++offset;
        char detail[128];
        if(offset<count)snprintf(detail,sizeof detail," (normalized byte %zu: %02X to %02X; sizes %zu/%zu)",
            offset,unsigned(left[offset]),unsigned(right[offset]),left.size(),right.size());
        else snprintf(detail,sizeof detail," (normalized length differs at %zu; sizes %zu/%zu)",offset,left.size(),right.size());
        error=std::string("Shared editing rejected an unsupported avatar, template, act, global or placement property change")+detail;
        return false;
    }
    error.clear();return true;
}

bool validate(const Snapshot& snapshot,const editor::Document& document,std::string& error){
    if(!supported){error="Native editor validation is unavailable";return false;}
    if(document.placements.size()>kPlacementLimit || snapshot.bindings.size()>kPlacementLimit){
        error="Native editor placement limit exceeded";return false;
    }
    bool tracked=false;if(!relative_scale_mode(document,tracked,error))return false;
    Owned copy(parse(snapshot.bytes,error));if(!copy.p)return false;
    std::map<uint32_t,editor::ObjectId> supplied;
    std::set<editor::ObjectId> ids;
    for(const auto& b:snapshot.bindings){
        if(!b.object.id || !b.object.generation || !supplied.emplace(b.marker,b.object).second || !ids.insert(b.object).second){
            error="Native snapshot has duplicate or invalid placement bindings";return false;
        }
    }
    Snapshot projected;projected.act=snapshot.act;
    if(!projection(copy.p,projected,error,&supplied,tracked,document.schema.contains(kElevationProperty),!document.class_schema.empty()))return false;
    if(!cast_schema(document,error))return false;
    if(projected.document.class_settings.size()!=document.class_settings.size() || (!document.class_schema.empty() && projected.document.next_class!=document.next_class)){error="Native cast roster differs from the committed draft";return false;}
    for(const auto& [id,settings]:projected.document.class_settings){
        auto found=document.class_settings.find(id);
        if(found==document.class_settings.end() || settings.class_id!=found->second.class_id || settings.act_id!=found->second.act_id || settings.properties!=found->second.properties){
            error="Native cast properties differ from the committed draft";
            if(found!=document.class_settings.end()){
                auto have=settings.properties.find(kCastBlockProperty),want=found->second.properties.find(kCastBlockProperty);
                if(have!=settings.properties.end() && want!=found->second.properties.end()){
                    const auto* a=std::get_if<editor::StructuredValue>(&have->second);const auto* b=std::get_if<editor::StructuredValue>(&want->second);
                    if(a && b){
                        size_t first=0;while(first<std::min(a->bytes.size(),b->bytes.size()) && a->bytes[first]==b->bytes[first])++first;
                        H->log(M,SK_DEBUG,"Native cast mismatch: class=%u native=%u committed=%u first=%u nativeByte=%02X committedByte=%02X",
                            uint32_t(settings.class_id.id),unsigned(a->bytes.size()),unsigned(b->bytes.size()),unsigned(first),
                            first<a->bytes.size()?a->bytes[first]:0,first<b->bytes.size()?b->bytes[first]:0);
                    }
                }
            }
            return false;
        }
    }
    if(projected.document.classes!=document.classes || projected.document.acts!=document.acts ||
       projected.document.placements.size()!=document.placements.size() || supplied.size()!=document.placements.size() ||
       !document.acts.count({(3ull<<32)|snapshot.act,1})){
        error="Native snapshot classes, acts or placement roster do not match the committed document";return false;
    }
    for(const auto& [id,p]:projected.document.placements){
        auto it=document.placements.find(id);
        if(it==document.placements.end()){error="Native snapshot contains an uncommitted placement";return false;}
        const auto& wanted=it->second;
        if(p.class_id!=wanted.class_id || p.act_id!=wanted.act_id || p.position!=wanted.position ||
           p.rotation!=wanted.rotation || p.scale!=wanted.scale || !same_native_properties(p.properties,wanted.properties) ||
           !std::isfinite(p.scale) || p.scale<=0 || !std::isfinite(p.position.x) ||
           !std::isfinite(p.position.y) || !std::isfinite(p.position.z) || !std::isfinite(p.rotation.x) ||
           !std::isfinite(p.rotation.y) || !std::isfinite(p.rotation.z) || !std::isfinite(p.rotation.w)){
            if(H){
                H->log(M,SK_DEBUG,"Native placement mismatch: object=%llu pos=%.9g,%.9g,%.9g/%.9g,%.9g,%.9g rot=%.9g,%.9g,%.9g,%.9g/%.9g,%.9g,%.9g,%.9g scale=%.9g/%.9g properties=%u/%u",
                    static_cast<unsigned long long>(id.id),double(p.position.x),double(p.position.y),double(p.position.z),double(wanted.position.x),double(wanted.position.y),double(wanted.position.z),
                    double(p.rotation.x),double(p.rotation.y),double(p.rotation.z),double(p.rotation.w),double(wanted.rotation.x),double(wanted.rotation.y),double(wanted.rotation.z),double(wanted.rotation.w),
                    double(p.scale),double(wanted.scale),unsigned(p.properties.size()),unsigned(wanted.properties.size()));
                unsigned shown=0;
                for(const auto& [key,value]:p.properties){
                    auto old=wanted.properties.find(key);if(old==wanted.properties.end() || old->second==value)continue;
                    if(auto* a=std::get_if<float>(&value))if(auto* b=std::get_if<float>(&old->second))
                        H->log(M,SK_DEBUG,"Native placement scalar mismatch: key=%08X actual=%.9g expected=%.9g",key,double(*a),double(*b));
                    if(++shown==8)break;
                }
            }
            error="Native snapshot placement fields do not match the committed document";return false;
        }
    }
    // Class-based goals may also match newly cloned placements. The immutable
    // canonical references retain the original protected targets; additional
    // matching instances do not permit removal of any original target.
    std::set<editor::Reference> native_references(projected.document.external_references.begin(),projected.document.external_references.end());
    for(const auto& ref:document.external_references)if(!native_references.count(ref)){
        error="Native snapshot would orphan an authored objective reference";return false;
    }
    error.clear();return true;
}

bool restore(const Snapshot& snapshot, std::string& error) {
    // Scene reconstruction also installs the snapshot camera. That camera
    // belongs to the saved draft, while each participant's live browsing
    // camera belongs to that participant, including during remote commits.
    std::optional<LocalView> local_view;
    std::string view_error;
    if(void* camera=view_camera(view_error)){
        LocalView current;read_view(camera,current);
        if(finite_view(current))local_view=current;
    }
    auto s=state();
    if (!s.ready || s.gesture || applying) { error="Native Adventure Editor cannot restore during this transition"; return false; }
    if(!validate(snapshot,snapshot.document,error))return false;
    Owned copy(parse(snapshot.bytes,error));
    if (!copy.p) return false;
    // Camera state belongs to this editor. Native Undo's full copy includes it.
    // Preserve the native local resource camera before that copy is applied.
    void* live=resource(data(mode()));
    refresh_selection();
    auto selected=selection;
    skg::field<editor::ResourceKey>(copy.p,8)=skg::field<editor::ResourceKey>(live,8);
    memcpy(skg::ptr_add<void>(copy.p,0x2C30),skg::ptr_add<void>(live,0x2C30),0x20);
    void* memory=skg::cdecl_call<void*>(a_alloc,size_t(0x18),"CoopEditorRestore",0,0,0,0);
    if(!memory){error="Native editor history allocation failed";return false;}
    Owned entry(skg::thiscall<void*>(a_entry,memory,data(mode()),copy.p,copy.p));
    if(snapshot.metadata) {
        const auto assign=H->sym("SporeCoop::Editor::AssignMetadataText");
        if(!assign){error="The native metadata text adapter is unavailable";return false;}
        for(size_t i=0;i<snapshot.metadata->text.size();++i) {
            const auto& text=snapshot.metadata->text[i];
            if(text.size()>4096 || text.find(u'\0')!=std::u16string::npos){error="Invalid native metadata text";return false;}
        }
        for(size_t i=0;i<snapshot.metadata->text.size();++i) {
            const auto& text=snapshot.metadata->text[i];
            skg::thiscall<void>(assign,skg::ptr_add<void>(data(mode()),0x4C+16*i),text.data(),text.data()+text.size());
        }
        skg::field<uint32_t>(data(mode()),0x7C)=snapshot.metadata->genre;
    }
    addref(entry.p);
    applying=true;
    skg::thiscall<void>(a_restore,entry.p,copy.p);
    if(snapshot.local_dirty){
        skg::field<uint32_t>(data(mode()),0x80)=snapshot.local_dirty->saved_revision;
        skg::field<bool>(data(mode()),0x84)=snapshot.local_dirty->dirty;
        skg::field<bool>(data(mode()),0x85)=snapshot.local_dirty->imported;
    }else{
        // Incoming/recovered authored bytes are unsaved in this editor context.
        // Preserve the distinct imported/new flag and native saved revision.
        skg::field<bool>(data(mode()),0x84)=true;
    }
    applying=false;
    bind(snapshot.bindings);
    if(!selected.empty()){
        void* selected_marker=marker_for_id(resource(data(mode())),selected.front());
        void* object=selected_marker?skg::thiscall<void*>(a_object,data(mode()),selected_marker):nullptr;
        void* tools=*reinterpret_cast<void**>(a_tools);
        if(tools)original_select(tools,object);
    }
    refresh_selection();
    // The native scene/selection rebuild reconstructs sound controls. Its
    // serialized source must remain the committed value rather than acquiring
    // another round-trip error on every restoration.
    for(const auto& [id,placement]:snapshot.document.placements){
        void* marker=marker_for_id(resource(data(mode())),id);if(!marker)continue;
        for(auto [key,offset]:{std::pair{kSoundPitchProperty,0x1E0u},std::pair{kSoundGainProperty,0x1E4u}}){
            auto found=placement.properties.find(key);
            if(found!=placement.properties.end())if(const auto* value=std::get_if<float>(&found->second))skg::field<float>(marker,offset)=*value;
        }
    }
    ++restores;
    if(local_view && !restore_view(*local_view,generation,error))return false;
    return true;
}

bool rebaseline(std::string& error){
    const auto current=state();
    if(!current.ready || current.gesture || applying || !pending.empty()){
        error="Wait for the native editor gesture and pending captures to settle";return false;
    }
    auto* history=reinterpret_cast<void*>(a_history);
    const uint32_t serial=skg::field<uint32_t>(history,0x18);
    void* owner=data(mode());
    const uint32_t saved=skg::field<uint32_t>(owner,0x80);
    const bool counter_dirty=serial>saved;
    if(serial==UINT32_MAX){error="Native editor history serial is exhausted";return false;}
    uintptr_t begin=skg::field<uintptr_t>(history,0),end=skg::field<uintptr_t>(history,4);
    if(end<begin || (end-begin)%4 || (end!=begin && !skg::readable(reinterpret_cast<void*>(begin),end-begin))){
        error="Native editor history range is invalid";return false;
    }
    skg::field<uint32_t>(history,0x14)=0;
    skg::thiscall<void>(a_erase_history,history,reinterpret_cast<void*>(begin),reinterpret_cast<void*>(end));
    skg::field<uint32_t>(history,0x18)=serial+1;
    skg::cdecl_call<void>(a_discard_history);
    // Native dirty compares unsigned serial against the last saved counter.
    // Preserve that predicate and leave authored/metadata dirty bytes intact.
    if(!counter_dirty)skg::field<uint32_t>(owner,0x80)=serial+1;
    ++generation;
    release(gesture_before_resource);gesture_before_resource=nullptr;
    for(auto actor:gesture_actors)release(actor.object);
    gesture_actors.clear();gesture_before_bindings.clear();
    error.clear();return true;
}

bool apply(const editor::Document& document, std::string& error) {
    return apply(document,editor::Change{},nullptr,error);
}

bool apply(const editor::Document& document,const editor::Change& change,const Snapshot* artifact,std::string& error) {
    if(document.placements.size()>kPlacementLimit){error="Native editor placement limit exceeded";return false;}
    bool tracked=false;if(!relative_scale_mode(document,tracked,error))return false;
    Snapshot snapshot;
    if (!capture(snapshot,error)) return false;
    if ((!tracks_cast() && snapshot.document.classes != document.classes) || snapshot.document.acts != document.acts ||
        snapshot.document.schema!=document.schema) {
        error="Shared editing cannot change native classes or acts yet"; return false;
    }
    bool cast_changed=snapshot.document.classes!=document.classes || snapshot.document.next_class!=document.next_class;
    for(const auto& [id,settings]:document.class_settings){
        auto found=snapshot.document.class_settings.find(id);
        if(found==snapshot.document.class_settings.end() || found->second.properties!=settings.properties)cast_changed=true;
    }
    if(cast_changed){
        if(!cast_schema(document,error))return false;
        Owned merged(parse(snapshot.bytes,error)),empty(construct());
        if(!merged.p || !empty.p)return false;
        skg::thiscall<void*>(a_copy_class_rows,skg::ptr_add<void>(merged.p,0x2BF4),skg::ptr_add<void>(empty.p,0x2BF4));
        for(const auto& [id,settings]:document.class_settings){
            (void)id;
            auto value=settings.properties.find(kCastBlockProperty);
            const auto* block=value==settings.properties.end()?nullptr:std::get_if<editor::StructuredValue>(&value->second);
            if(!block || block->format!=kCastBlockFormat || block->bytes.size()>editor::Limits{}.structured_bytes){error="Invalid native cast subtree";return false;}
            Owned fragment(parse(block->bytes,error));if(!fragment.p)return false;
            Rows classes;
            if(!rows(fragment.p,0x2BF4,0x27E8,1,classes) || classes.end-classes.begin!=0x27E8 ||
               settings.class_id!=editor::ObjectId{(2ull<<32)|skg::field<uint32_t>(classes.begin,0),1}){
                error="A cast subtree has a different native class identity";return false;
            }
            uint32_t index=uint32_t(settings.class_id.id);
            void* target=skg::thiscall<void*>(a_insert_class,skg::ptr_add<void>(merged.p,0x2BF4),&index);
            if(!target){error="Cannot allocate the canonical cast class";return false;}
            skg::thiscall<void*>(a_copy_class,target,classes.begin+8);
        }
        skg::field<uint32_t>(merged.p,0x2C0C)=document.next_class;
        auto prepared=snapshot;
        prepared.document.classes=document.classes;prepared.document.class_schema=document.class_schema;
        prepared.document.class_settings=document.class_settings;prepared.document.next_class=document.next_class;
        // Class removal and all its placements form one transaction. Build an
        // internally valid owned resource before rebuilding the live scene;
        // validating a new class roster against old placements would reject
        // every populated class removal before its placement phase ran.
        if(std::any_of(snapshot.document.placements.begin(),snapshot.document.placements.end(),
                [&](const auto& row){return !document.classes.contains(row.second.class_id);})){
            Owned retained(construct());if(!retained.p){error="Cannot allocate retained native placements";return false;}
            prepared.bindings.clear();
            for(const auto& binding:snapshot.bindings){
                auto placement=snapshot.document.placements.find(binding.object);
                if(placement==snapshot.document.placements.end() || !document.classes.contains(placement->second.class_id))continue;
                void* marker=marker_for_key(merged.p,binding.marker);
                if(!marker){error="A retained cast placement has lost its native binding";return false;}
                auto index=skg::thiscall<uint32_t>(a_insert_marker,skg::ptr_add<void>(retained.p,0x2C10),marker);
                prepared.bindings.push_back({binding.object,index});
            }
            skg::thiscall<void*>(a_copy_marker_rows,skg::ptr_add<void>(merged.p,0x2C10),skg::ptr_add<void>(retained.p,0x2C10));
            skg::field<uint32_t>(merged.p,0x2C24)=skg::field<uint32_t>(retained.p,0x2C24);
            skg::field<uint32_t>(merged.p,0x2C28)=skg::field<uint32_t>(retained.p,0x2C28);
            std::erase_if(prepared.document.placements,[&](const auto& row){return !document.classes.contains(row.second.class_id);});
        }
        if(!serialize(merged.p,prepared.bytes,error) || !validate(prepared,prepared.document,error) ||
           (C.validate_dependencies && !C.validate_dependencies(prepared,error)) || !restore(prepared,error) || !capture(snapshot,error))return false;
    }
    for(const auto& [id,to]:document.placements){
        auto from=snapshot.document.placements.find(id);
        float required_relative=0;if(!relative_scale_value(to,tracked,required_relative,error))return false;
        if((from!=snapshot.document.placements.end() &&
            (from->second.class_id!=to.class_id || from->second.act_id!=to.act_id)) ||
           !document.classes.count(to.class_id) ||
           !std::isfinite(to.scale) || to.scale<=0 || !std::isfinite(to.position.x) ||
           !std::isfinite(to.position.y) || !std::isfinite(to.position.z) ||
           !std::isfinite(to.rotation.x) || !std::isfinite(to.rotation.y) ||
           !std::isfinite(to.rotation.z) || !std::isfinite(to.rotation.w)) {
            error="Unsupported native placement field";return false;
        }
    }
    Owned source(artifact?parse(artifact->bytes,error):nullptr);
    if(artifact && !source.p)return false;
    const bool structural=document.placements.size()!=snapshot.document.placements.size() ||
        std::any_of(snapshot.document.placements.begin(),snapshot.document.placements.end(),
            [&](const auto& row){return !document.placements.contains(row.first);});
    // Only roster changes can invalidate live slots or need an owned marker
    // artifact. A transform preserves the roster captured immediately above.
    Owned base(structural?parse(snapshot.bytes,error):nullptr);
    if(structural && !base.p)return false;
    auto source_marker=[&](editor::ObjectId id)->void*{
        if(artifact)for(const auto& b:artifact->bindings)
            if(b.object==id)return marker_for_key(source.p,b.marker);
        for(const auto& b:snapshot.bindings)
            if(b.object==id)return marker_for_key(base.p,b.marker);
        return nullptr;
    };
    struct Actor { editor::ObjectId id; std::unique_ptr<Owned> reference; };
    std::vector<Actor> actors;
    void* owner=data(mode());
    if(structural)for(const auto& b:snapshot.bindings){
        // Snapshot indices name its dense serialized resource. Live pool slots
        // can have holes and must be resolved through stable identity instead.
        void* marker=marker_for_id(resource(owner),b.object);
        void* object=marker?skg::thiscall<void*>(a_object,owner,marker):nullptr;
        if(object){addref(object);actors.push_back({b.object,std::make_unique<Owned>(object)});}
    }
    auto rebind=[&](){
        std::map<uint32_t,editor::ObjectId> next;
        Rows map;
        if(!rows(resource(owner),0x2C10,0x238,kPlacementLimit,map))return false;
        for(const auto& [key,id]:bindings)if(marker_for_key(resource(owner),key))next[key]=id;
        for(const auto& actor:actors){
            void* marker=skg::thiscall<void*>(a_marker,owner,actor.reference->p);
            if(!marker)continue;
            for(auto p=map.begin;p!=map.end;p+=0x238)if(live_slot(p) && p+4==marker){
                next[slot_index(map,p)]=actor.id;break;
            }
        }
        bindings=std::move(next);return true;
    };
    auto fail=[&](const char* reason){
        error=reason;applying=false;std::string rollback_error;
        if(!restore(snapshot,rollback_error))error += "; rollback failed: "+rollback_error;
        return false;
    };
    refresh_selection();auto selected=selection;
    applying=true;
    bool changed=false;
    for(const auto& [id,unused]:snapshot.document.placements){
        (void)unused;
        if(document.placements.count(id))continue;
        void* marker=marker_for_id(resource(owner),id);
        if(!marker)return fail("Native delete target is no longer bound");
        skg::thiscall<void>(a_delete_marker,owner,marker);
        changed=true;
        if(!rebind())return fail("Native placement identity changed during deletion");
    }
    for(const auto& [id,to]:document.placements){
        if(snapshot.document.placements.count(id))continue;
        void* original=source_marker(id);
        if(!original){
            auto clone=change.clone_sources.find(id);
            if(clone!=change.clone_sources.end())original=source_marker(clone->second);
        }
        void* marker=nullptr;
        if(original){
            if(skg::field<uint32_t>(original,0)!=uint32_t(to.class_id.id))
                return fail("Native restore artifact refers to a different class");
            marker=skg::thiscall<void*>(a_clone_marker,owner,original);
        }else{
            // Forward Create uses the verified native defaults for an existing
            // class. Undo/Redo restoration must supply its original artifact.
            if(change.intent!=editor::Intent::Forward)
                return fail("Native history restoration requires its owned marker artifact");
            marker=skg::thiscall<void*>(a_create_marker,owner,uint32_t(to.class_id.id),&to.position,&to.rotation,-1.0f);
        }
        if(!marker)return fail("The native editor rejected placement creation");
        changed=true;
        Rows map;
        if(!rows(resource(owner),0x2C10,0x238,kPlacementLimit,map))return fail("Native placement map became invalid");
        bool found=false;
        for(auto p=map.begin;p!=map.end;p+=0x238)if(live_slot(p) && p+4==marker){
            bindings[slot_index(map,p)]=id;found=true;break;
        }
        if(!found)return fail("The created native placement has no resource identity");
        void* object=skg::thiscall<void*>(a_object,owner,marker);
        if(object){addref(object);actors.push_back({id,std::make_unique<Owned>(object)});}
    }
    for(const auto& [id,to]:document.placements){
        auto before=snapshot.document.placements.find(id);
        if(!structural && before!=snapshot.document.placements.end() && before->second.position==to.position &&
           before->second.rotation==to.rotation && before->second.scale==to.scale &&
           before->second.properties==to.properties)continue;
        void* marker=marker_for_id(resource(owner),id);
        if(!marker)return fail("Native transform target is no longer bound");
        float previous=skg::field<float>(marker,0x20);
        float relative=skg::field<float>(marker,0x24),required_relative=relative;
        if(!relative_scale_value(to,tracked,required_relative,error)){const auto reason=error;return fail(reason.c_str());}
        if(skg::field<editor::Vec3>(marker,4)==to.position &&
           skg::field<editor::Rotation>(marker,0x10)==to.rotation && previous==to.scale &&
           (!tracked || relative==required_relative) && before!=snapshot.document.placements.end() &&
           before->second.properties==to.properties)continue;
        skg::field<editor::Vec3>(marker,4)=to.position;
        skg::field<editor::Rotation>(marker,0x10)=to.rotation;
        skg::field<float>(marker,0x20)=to.scale;
        if(tracked)skg::field<float>(marker,0x24)=required_relative;
        else if(previous>0 && std::isfinite(relative))skg::field<float>(marker,0x24)=relative*(to.scale/previous);
        auto companion=[&]<class T>(uint32_t property,uint32_t offset){
            auto found=to.properties.find(property);
            if(found!=to.properties.end())skg::field<T>(marker,offset)=std::get<T>(found->second);
        };
        companion.template operator()<float>(kElevationProperty,0x220);
        companion.template operator()<float>(kSoundPitchProperty,0x1E0);
        companion.template operator()<float>(kSoundGainProperty,0x1E4);
        companion.template operator()<editor::Vec3>(kDestinationPositionProperty,0x1E8);
        companion.template operator()<editor::Rotation>(kDestinationRotationProperty,0x1F4);
        companion.template operator()<float>(kDestinationScaleProperty,0x204);
        skg::thiscall<void>(a_apply_marker,owner,marker,false);
        changed=true;
    }
    void* tools=*reinterpret_cast<void**>(a_tools);
    if(tools && !selected.empty()){
        void* marker=marker_for_id(resource(owner),selected.front());
        original_select(tools,marker?skg::thiscall<void*>(a_object,owner,marker):nullptr);
    }
    if(changed)skg::field<bool>(owner,0x84)=true;
    applying=false;refresh_selection();
    ++restores;
    return true;
}

bool select_placement(editor::ObjectId id,std::string& error){
    const auto current=state();
    if(!current.ready || current.gesture || applying){
        error="Wait for the native editor before selecting the saved placement";return false;
    }
    void* owner=data(mode());
    void* marker=marker_for_id(resource(owner),id);
    void* object=marker?skg::thiscall<void*>(a_object,owner,marker):nullptr;
    void* tools=a_tools?*reinterpret_cast<void**>(a_tools):nullptr;
    if(!object || !tools){
        error="The saved placement is not currently available for local selection";return false;
    }
    // Use the same verified selection path as ordinary native editing. The
    // native cast-icon grouper keeps this one marker out of overlapping stacks.
    applying=true;
    original_select(tools,object);
    applying=false;
    refresh_selection();
    error.clear();return true;
}

bool enter(const editor::ResourceKey& key,std::string& error){
    if(!supported || !key.instance || key.type!=0x366A930D || !creation_group() || key.group!=creation_group() || game::mode_id()!=modes::kMenu){
        error="A registered adventure can enter the native editor only from Galaxy";return false;
    }
    const char* required[]={"SporeCoop::Adventure::ResetRequest","SporeCoop::Adventure::LoadingScreenGet",
        "SporeCoop::Adventure::LoadingScreenShow","App::IStateManager::Get","App::cStateManager::SwitchToState",
        "App::IGameModeManager::Get","App::cGameModeManager::SetActiveMode"};
    std::array<uintptr_t,7> address{};
    for(size_t i=0;i<address.size();++i)if(!(address[i]=H->sym(required[i]))){
        error="Verified native editor entry is unavailable";return false;
    }
    void* request=skg::cdecl_call<void*>(address[0]);
    void* loading=skg::cdecl_call<void*>(address[1]);
    void* app=skg::cdecl_call<void*>(address[3]);
    void* manager=skg::cdecl_call<void*>(address[5]);
    if(!request || !loading || !app || !manager){error="Native editor entry context is not initialized";return false;}
    // Native existing-adventure editor route DFBD54..DFBD95. The reset helper
    // initializes kind1/context0 and clears captain; this is not quickplay2/1.
    skg::field<editor::ResourceKey>(request,0)=key;
    skg::field<uint32_t>(request,0x24)=1;
    skg::field<uint32_t>(request,0x28)=0;
    skg::thiscall<void>(address[2],loading,true,modes::kScenario,true);
    if(!skg::thiscall<bool>(address[4],app,uint32_t(0x21851EBE)) ||
       (game::mode_id()!=modes::kScenario && !skg::thiscall<bool>(address[6],manager,modes::kScenario))){
        error="The game declined native Adventure Editor entry";return false;
    }
    return true;
}

bool selection_pose(editor::ObjectId& object,editor::ObjectId& act,editor::Pose& pose){
    const auto current=state();
    if(!active() || applying || !current.ready || !current.gesture || current.selected.empty())return false;
    void* marker=marker_for_id(resource(data(mode())),current.selected.front());
    if(!skg::readable(marker,0x28))return false;
    editor::Pose candidate{skg::field<editor::Vec3>(marker,4),skg::field<editor::Rotation>(marker,0x10),skg::field<float>(marker,0x20)};
    if(!std::isfinite(candidate.position.x) || !std::isfinite(candidate.position.y) || !std::isfinite(candidate.position.z) ||
       !std::isfinite(candidate.rotation.x) || !std::isfinite(candidate.rotation.y) || !std::isfinite(candidate.rotation.z) ||
       !std::isfinite(candidate.rotation.w) || !std::isfinite(candidate.scale) || candidate.scale<=0)return false;
    object=current.selected.front();act={(3ull<<32)|current.act,1};pose=candidate;return true;
}

std::string status() {
    auto s=state(); char b[384];
    snprintf(b,sizeof b,"editor_native available=%u editing=%u ready=%u playtesting=%u entry=%u act=%u generation=%llu gesture=%u history=%u/%u captures=%llu restores=%llu rejected=%llu cancellation=%u cancellation_serial=%llu ordinary_cancellations=%llu",
        s.available,s.editing,s.ready,s.playtesting,s.entry,s.act,(unsigned long long)s.generation,s.gesture,
        s.history_cursor,s.history_size,(unsigned long long)captures,(unsigned long long)restores,(unsigned long long)rejected,
        unsigned(cancellation.has_value()),(unsigned long long)cancellation_counter,(unsigned long long)ordinary_cancellations);
    return b;
}
} // namespace editor_native
