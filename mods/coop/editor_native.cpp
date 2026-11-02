#include "editor_native.h"
#include "editor_native_bindings.h"
#include "editor_native_tools.h"
#include "editor_native_complexity.h"
#include "editor_native_pose.h"
#include "editor_native_projection_cache.h"
#include "net/dbpf.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <new>
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
uintptr_t a_preview_factory;
uintptr_t a_marker_ctor,a_marker_dtor,a_resize_marker_acts,a_object_reference;
uintptr_t a_inspector_open,a_layout_shown;
uintptr_t a_copy_acts,a_copy_act,a_select_act;
uintptr_t a_copy_class_acts,a_copy_marker_acts,a_insert_act,a_insert_class_act,a_insert_marker_act,a_add_act,a_delete_act;
editor::Document act_identity;
std::optional<bool> authoring_override;
uintptr_t a_view_manager, a_view_target, a_view_orbit, a_view_set_orbit, a_view_lookat;
uintptr_t a_get_simulator_system,a_get_simulator_camera_state,a_activate_simulator_camera;
uintptr_t a_scenario_initialization,a_get_simulator_mode_manager;
uintptr_t a_metadata_message_manager,a_metadata_message_send;
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
struct ValidatedSnapshot {
    std::vector<uint8_t> bytes;
    std::vector<Binding> bindings;
    editor::Document document;
    editor::ResourceKey source,captain;
    bool captain_locked=false;
    std::optional<Snapshot::Metadata> metadata;
    uint64_t generation=0;
    uint32_t act=0;
};
std::optional<ValidatedSnapshot> validated_snapshot;
unsigned validation_depth=0;
editor_native_cache::ClassesCache class_projection_cache;
[[maybe_unused]] uint64_t validation_hits=0;
void* observed_mode;
void* observed_data;
bool observed_scenario;
std::map<uint32_t, editor::ObjectId> bindings;
std::map<uint32_t,uint32_t> incarnations;
std::vector<editor::ObjectId> selection;
struct PreviewActor {
    void* object=nullptr;
    void* secondary=nullptr;
    std::unique_ptr<uint8_t[]> marker;
    editor::ObjectId source,asset,act;
};
std::map<editor::Peer,PreviewActor> preview_actors;
uint32_t selected_class;
std::optional<editor::ObjectId> input_clone, gesture_clone;
bool input_cast=false,gesture_cast=false;
bool input_speech=false;
uint32_t script_events=0;
constexpr uint32_t kResourceSize = 0x2CB0, kLimit = 32u << 20;
uintptr_t a_copy_crew;
uintptr_t a_terrain_load,a_terrain_rebuild,a_terrain_selection,a_terrain_loaded,a_terrain_vtable,a_copy_terrain_properties;
uintptr_t a_terraform_ui_vtable,a_flora_ui_vtable;
bool terrain_available=false,terrain_pending=false,terrain_capturing=false,terrain_queued=false;
bool terrain_recipe_replacement=false;
uint64_t terrain_rebuild_frame=0;
bool terrain_rebuilding=false;
std::optional<Snapshot> terrain_before;
GestureScope terrain_scope;uint64_t terrain_generation=0;void* terrain_owner=nullptr;
struct Grounding {editor::Vec3 native,authored;};
std::map<editor::ObjectId,Grounding> terrain_grounding;
std::optional<editor::Document> materialized_terrain;
struct DeferredLocalView {LocalView view;GestureScope scope;uint64_t generation=0;void* mode=nullptr;void* owner=nullptr;};
std::optional<DeferredLocalView> deferred_local_view;
bool local_view_queued=false,local_view_settling=false;
editor_native_complexity::Bounds complexity_bounds;
bool complexity_available=false;
void* mode();
void* terraform_mode(){return skg::readable(mode(),0x1C)?skg::field<void*>(mode(),0x18):nullptr;}
bool terrain_settled(){
    if(!terrain_rebuilding)return true;
    void* tool=terraform_mode();if(!skg::readable(tool,0x1CC) || skg::field<bool>(tool,0x1B9) || skg::field<bool>(tool,0x1BA))return false;
    void* terrain=skg::field<void*>(tool,0x190);
    return a_terrain_loaded && skg::readable(terrain,0x100) && skg::field<uintptr_t>(terrain,0)==a_terrain_vtable &&
        H->frame_index()>terrain_rebuild_frame && skg::thiscall<bool>(a_terrain_loaded,terrain);
}

void* mode() { return a_mode ? *reinterpret_cast<void**>(a_mode) : nullptr; }
void* data(void* m) { return skg::readable(m, 0xDC) ? skg::field<void*>(m, 0x74) : nullptr; }
void* resource(void* d) { return skg::readable(d, 0x128) ? skg::field<void*>(d, 0x10) : nullptr; }
bool editing() {
    void* m = mode();
    return supported && game::mode_id()==modes::kScenario && skg::readable(m, 0xDC) && skg::field<int>(m, 0xCC) == 1 &&
           skg::field<int>(m, 0xD0) == 0 && skg::readable(resource(data(m)), kResourceSize);
}
bool active() { return editing() && C.active && C.active(); }
bool extended_complexity(){
    if(!active())return false;
    auto project=act_identity.settings.find(kProjectObject);if(project==act_identity.settings.end())return false;
    auto field=project->second.properties.find(kExtendedComplexityProperty);
    return field!=project->second.properties.end() && std::get_if<bool>(&field->second) && std::get<bool>(field->second);
}
using ComplexityFn=float(SK_THISCALL*)(void*,const uint32_t*);
ComplexityFn original_complexity;
float SK_THISCALL complexity_ratio(void* self,const uint32_t* counts){
    const auto ratio=original_complexity(self,counts);
    return complexity_available && self==data(mode()) && extended_complexity()?ratio*0.25f:ratio;
}
bool update_complexity(std::string& error){
    const bool requested=extended_complexity();
    if(requested && !complexity_available){error="The exact native Extended Complexity adapter is unavailable";return false;}
    return complexity_bounds.set(requested,error);
}
bool native_slot(void* object,unsigned slot){
    if(!skg::readable(object,sizeof(void*)))return false;
    auto vt=skg::vtable(object);
    return skg::readable(vt,(slot+1)*sizeof(void*)) && vt[slot];
}
void* native_cast(void* object,uint32_t type){
    return native_slot(object,3)?skg::vcall<void*>(object,3,type):nullptr;
}
bool render_pose(void* object,const editor::Pose& pose){
    // B18D40 is precisely Cast(01186577). F3D930 and EFCD20 use these
    // component setters, with no writes to the authored ScenarioResource.
    void* model=native_cast(object,0x01186577);
    if(!native_slot(model,0x40/4))return false;
    skg::vcall<void>(model,0x38/4,&pose.position);
    skg::vcall<void>(model,0x3C/4,&pose.rotation);
    skg::vcall<void>(model,0x40/4,pose.scale);
    return true;
}
bool render_destination(void* object,const editor::Pose& pose){
    // EFCD20: the teleporter interface owns a separate exit object at +54.
    void* teleporter=native_cast(object,0x0175CDC9);
    return skg::readable(teleporter,0x58) && render_pose(skg::field<void*>(teleporter,0x54),pose);
}
bool tracks_relative_scale(){return !C.relative_scale_tracked || C.relative_scale_tracked();}
bool tracks_extended_poses(){return !C.extended_poses_tracked || C.extended_poses_tracked();}
bool tracks_cast(){return !C.cast_tracked || C.cast_tracked();}
bool tracks_authoring(){return authoring_override.value_or(!C.authoring_tracked || C.authoring_tracked());}
void addref(void* p) { if (p) skg::vcall<int>(p, 0); }
void release(void* p) { if (p) skg::vcall<int>(p, 1); }
void clear_preview_actor(PreviewActor& actor){
    // Both ornaments borrow the detached marker. Pin both until native removal
    // has completed, then break the intrusive parent/child cycle. DestroyInstance
    // queues references too, so detach their marker before releasing its storage.
    for(void* object:{actor.object,actor.secondary})if(object && skg::readable(object,0x28)){
        if(!skg::field<bool>(object,0x20))game::destroy_creature(object);
        if(skg::field<void*>(object,0x18)==actor.marker.get())skg::field<void*>(object,0x18)=nullptr;
    }
    if(actor.secondary && a_object_reference){
        void* parent=native_cast(actor.object,0x0175CDC9);
        void* child=native_cast(actor.secondary,0x0175CDC9);
        if(skg::readable(parent,0x58) && skg::field<void*>(parent,0x54)==actor.secondary)
            skg::thiscall<void>(a_object_reference,skg::ptr_add<void>(parent,0x54),(void*)nullptr);
        if(skg::readable(child,0x54) && skg::field<void*>(child,0x50)==actor.object)
            skg::thiscall<void>(a_object_reference,skg::ptr_add<void>(child,0x50),(void*)nullptr);
    }
    release(actor.secondary);release(actor.object);actor.secondary=actor.object=nullptr;
    if(actor.marker){skg::thiscall<void>(a_marker_dtor,actor.marker.get());actor.marker.reset();}
}
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
    if(bytes.size()>=8 && editor::terrain_word(bytes,0)==0xB523D4F9 && editor::terrain_word(bytes,4)==17){
        editor_native_terrain::Properties checked;if(!editor_native_terrain::decode(bytes,checked,error))return nullptr;
    }
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
bool permute_acts(void* destination,void* source,const std::vector<uint32_t>& order,std::string& error){
    const std::array<uintptr_t,3> empty{};
    auto vector=[&](void* dst,void* src,uint32_t offset,uint32_t stride,uintptr_t copy,uintptr_t insert){
        Rows from;if(!rows(src,offset,stride,8,from))return false;
        skg::thiscall<void*>(copy,skg::ptr_add<void>(dst,offset),empty.data());
        for(auto index:order){
            if(index>=uint32_t((from.end-from.begin)/stride))return false;
            auto* vec=skg::ptr_add<void>(dst,offset);
            if(!skg::thiscall<void*>(insert,vec,skg::field<void*>(vec,4),from.begin+index*stride))return false;
        }
        return true;
    };
    if(!vector(destination,source,0x23C,0x534,a_copy_acts,a_insert_act)){error="Cannot reorder native acts";return false;}
    Rows to_classes,from_classes,to_markers,from_markers;
    if(!rows(destination,0x2BF4,0x27E8,1024,to_classes) || !rows(source,0x2BF4,0x27E8,1024,from_classes) ||
       !rows(destination,0x2C10,0x238,kPlacementLimit,to_markers) || !rows(source,0x2C10,0x238,kPlacementLimit,from_markers) ||
       to_classes.end-to_classes.begin!=from_classes.end-from_classes.begin || to_markers.end-to_markers.begin!=from_markers.end-from_markers.begin){error="The native roster rows changed during reordering";return false;}
    for(auto to=to_classes.begin,from=from_classes.begin;to!=to_classes.end;to+=0x27E8,from+=0x27E8)
        if(skg::field<uint32_t>(to,0)!=skg::field<uint32_t>(from,0) || !vector(to+8,from+8,0x70,0x4E0,a_copy_class_acts,a_insert_class_act)){error="Cannot reorder native cast behaviors";return false;}
    for(auto to=to_markers.begin,from=from_markers.begin;to!=to_markers.end;to+=0x238,from+=0x238)
        if(live_slot(to) && !vector(to+4,from+4,0x28,0x34,a_copy_marker_acts,a_insert_marker_act)){error="Cannot reorder native patrol paths";return false;}
    return true;
}
void normalize_fragment(void* r){
    std::memset(skg::ptr_add<void>(r,0x68),0,0x10);
    std::memset(skg::ptr_add<void>(r,0x2C30),0,0x20);skg::field<float>(r,0x2C48)=1.f;
}
bool act_block(uint8_t* act,editor::StructuredValue& block,std::string& error){
    Owned fragment(construct());if(!fragment.p){error="Cannot allocate the owned act subtree";return false;}
    normalize_fragment(fragment.p);
    const std::array<uintptr_t,3> slice{reinterpret_cast<uintptr_t>(act),reinterpret_cast<uintptr_t>(act+0x534),reinterpret_cast<uintptr_t>(act+0x534)};
    skg::thiscall<void*>(a_copy_acts,skg::ptr_add<void>(fragment.p,0x23C),slice.data());
    if(!serialize(fragment.p,block.bytes,error))return false;
    Owned canonical(parse(block.bytes,error));if(!canonical.p)return false;
    normalize_fragment(canonical.p);
    Rows acts,goals;
    if(!rows(canonical.p,0x23C,0x534,1,acts) || acts.end-acts.begin!=0x534 || !rows(acts.begin,0x84,0x188,3,goals)){
        error="Invalid owned act or goal collection";return false;
    }
    std::set<editor::ObjectId> refs;
    for(auto goal=goals.begin;goal!=goals.end;goal+=0x188){
        // F42660 owns validation bit0 and diagnostic184, not authored goals.
        skg::field<uint32_t>(goal,0x14)&=~1u;skg::field<uint32_t>(goal,0x184)=0;
        if(!skg::field<uint32_t>(goal,0))continue;
        for(uint32_t offset:{4u,8u}){
            auto index=skg::field<int32_t>(goal,offset);
            if(index>=0)refs.insert({(2ull<<32)|uint32_t(index),1});
        }
    }
    block.format=kActBlockFormat;block.references.assign(refs.begin(),refs.end());
    if(!serialize(canonical.p,block.bytes,error))return false;
    if(block.bytes.size()>editor::Limits{}.structured_bytes){error="The act subtree exceeds its transaction limit";return false;}
    return true;
}
bool project_authoring(void* r,editor::Document& document,std::string& error,const editor::Document& identity){
    document.settings_schema=authoring_property_schema();
    document.next_id=std::max<uint64_t>(document.next_id,9ull<<32);
    Rows acts;if(!rows(r,0x23C,0x534,8,acts)){error="Invalid authored act collection";return false;}
    uint32_t index=0;
    for(auto act=acts.begin;act!=acts.end;act+=0x534,++index){
        editor::AuthoringSettings settings;settings.object=act_settings_id(index);settings.act_id={(3ull<<32)|index,1};
        if(!identity.act_order.empty()){
            if(index>=identity.act_order.size()){error="The native act has no stable identity";return false;}
            settings.act_id=identity.act_order[index];
            auto found=std::find_if(identity.settings.begin(),identity.settings.end(),[&](const auto& row){return row.second.kind==editor::SettingsKind::Act && row.second.act_id==settings.act_id;});
            if(found==identity.settings.end()){error="The act settings identity is missing";return false;}
            settings.object=found->first;
        }
        document.act_order.push_back(settings.act_id);document.acts.insert(settings.act_id);
        document.next_id=std::max(document.next_id,std::max(settings.object.id,settings.act_id.id)+1);
        editor::StructuredValue block;if(!act_block(act,block,error))return false;
        settings.properties[kActBlockProperty]=std::move(block);document.settings.emplace(settings.object,std::move(settings));
    }
    editor::AuthoringSettings captain;captain.object=kCaptainObject;captain.kind=editor::SettingsKind::Captain;
    auto key=skg::field<editor::ResourceKey>(r,0x130);
    const auto server=skg::field<uint64_t>(r,0x140);
    if(server && server!=UINT64_MAX){
        const auto resolve=H->sym("Simulator::Scenario::ResolveLocalServerAsset");
        if(!resolve || !skg::cdecl_call<bool>(resolve,server,&key)){error="The authored captain dependency is unavailable";return false;}
    }
    captain.properties={{kCaptainAssetProperty,key},{kCaptainLockedProperty,skg::field<bool>(r,0x175)},
        {kCaptainPositionProperty,skg::field<editor::Vec3>(r,0x150)},{kCaptainRotationProperty,skg::field<editor::Rotation>(r,0x15C)},
        {kCaptainScaleProperty,skg::field<float>(r,0x16C)},{kCaptainHealthProperty,skg::field<float>(r,0x170)},
        {kCaptainInvulnerableProperty,skg::field<bool>(r,0x174)}};
    document.settings.emplace(captain.object,std::move(captain));
    if(a_copy_crew){
        Rows crew;if(!rows(r,0x178,0x38,3,crew)){error="The native adventure crew roster is invalid";return false;}
        const auto capacity=skg::field<int32_t>(r,0x238);
        if(capacity<0 || capacity>3 || (crew.end-crew.begin)/0x38>capacity){error="The native adventure crew exceeds its allowed capacity";return false;}
        Owned fragment(construct());if(!fragment.p){error="Cannot allocate the owned adventure crew subtree";return false;}
        normalize_fragment(fragment.p);
        skg::thiscall<void*>(a_copy_crew,skg::ptr_add<void>(fragment.p,0x178),skg::ptr_add<void>(r,0x178));
        skg::field<int32_t>(fragment.p,0x238)=3;
        Rows copied;if(!rows(fragment.p,0x178,0x38,3,copied))return false;
        for(auto row=copied.begin;row!=copied.end;row+=0x38){
            const auto server=skg::field<uint64_t>(row,0x10);
            if(server && server!=UINT64_MAX){
                const auto resolve=H->sym("Simulator::Scenario::ResolveLocalServerAsset");editor::ResourceKey key;
                if(!resolve || !skg::cdecl_call<bool>(resolve,server,&key) || !key.instance){error="An adventure crew asset is unavailable";return false;}
                skg::field<editor::ResourceKey>(row,0)=key;
            }
            skg::field<uint32_t>(row,0xC)=0;skg::field<uint64_t>(row,0x10)=UINT64_MAX;
        }
        editor::StructuredValue block;block.format=kCrewRosterFormat;
        if(!serialize(fragment.p,block.bytes,error))return false;
        editor::AuthoringSettings settings;settings.object=kCrewObject;settings.kind=editor::SettingsKind::Crew;
        settings.properties={{kCrewRosterProperty,std::move(block)},{kCrewCapacityProperty,int64_t(capacity)}};
        document.settings.emplace(settings.object,std::move(settings));document.next_id=std::max<uint64_t>(document.next_id,11ull<<32);
    }
    return true;
}
bool owned_class_gate(void* target){
    if(!tracks_cast() || !C.class_gate)return false;
    Rows classes;if(!rows(resource(data(mode())),0x2BF4,0x27E8,1024,classes))return false;
    for(auto row=classes.begin;row!=classes.end;row+=0x27E8)
        if(row+8==target)return C.class_gate(skg::field<uint32_t>(row,0));
    return false;
}
bool assign_crew(void* resource,const editor::AuthoringSettings& settings,std::string& error){
    const auto roster=settings.properties.find(kCrewRosterProperty),count=settings.properties.find(kCrewCapacityProperty);
    const auto* block=roster==settings.properties.end()?nullptr:std::get_if<editor::StructuredValue>(&roster->second);
    const auto* capacity=count==settings.properties.end()?nullptr:std::get_if<int64_t>(&count->second);
    if(!a_copy_crew || settings.object!=kCrewObject || settings.kind!=editor::SettingsKind::Crew || settings.properties.size()!=2 ||
       !block || block->format!=kCrewRosterFormat || !block->references.empty() || block->bytes.size()>editor::Limits{}.structured_bytes ||
       !capacity || *capacity<0 || *capacity>3){error="The canonical adventure crew settings are invalid";return false;}
    Owned fragment(parse(block->bytes,error));Rows rows;
    if(!fragment.p || !editor_native::rows(fragment.p,0x178,0x38,3,rows) || (rows.end-rows.begin)/0x38>*capacity){error="The adventure crew exceeds its canonical capacity";return false;}
    editor::Document projected,identity;if(!project_authoring(fragment.p,projected,error,identity))return false;
    const auto canonical=projected.settings.find(kCrewObject);
    if(canonical==projected.settings.end() || canonical->second.properties.at(kCrewRosterProperty)!=editor::Value{*block}){
        error="The adventure crew subtree is not canonical";return false;
    }
    skg::thiscall<void*>(a_copy_crew,skg::ptr_add<void>(resource,0x178),skg::ptr_add<void>(fragment.p,0x178));
    skg::field<int32_t>(resource,0x238)=int32_t(*capacity);return true;
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
bool class_block(uint8_t* row,editor::StructuredValue& block,std::string& error,std::vector<editor::ObjectId>* legacy_references=nullptr){
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
    canonical_cast_asset_hints(copied);canonical_cast_primary_asset(copied);
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
    std::set<editor::ObjectId> references,legacy;
    for(auto act=acts.begin;act!=acts.end;act+=0x4E0){
        const bool custom=skg::field<uint8_t>(act,0x4C8)==1;
        standard_behavior_class_references(references,custom,skg::field<int32_t>(act,0x4B0),skg::field<int32_t>(act,0x4B4),
            skg::field<int32_t>(act,0x4B8),skg::field<int32_t>(act,0x4BC),skg::field<int32_t>(act,0x4C0),skg::field<int32_t>(act,0x4C4));
        if(legacy_references)for(auto offset:{0x4BCu,0x4C0u,0x4C4u})behavior_class_reference(legacy,2,skg::field<int32_t>(act,offset));
        Rows rules;
        if(!rows(act,0x4CC,0x20,1024,rules)){error="Invalid native behavior rule collection";return false;}
        if(custom || legacy_references)for(auto rule=rules.begin;rule!=rules.end;rule+=0x20)
            for(auto offset:{4u,0x10u,0x18u}){
                const auto type=skg::field<int32_t>(rule,offset),value=skg::field<int32_t>(rule,offset+4);
                if(custom)behavior_class_reference(references,type,value);
                if(legacy_references)behavior_class_reference(legacy,type,value);
            }
    }
    block.references.assign(references.begin(),references.end());
    if(legacy_references)legacy_references->assign(legacy.begin(),legacy.end());
    return true;
}
bool class_primary_asset(void* native,editor::ResourceKey& key,std::string& error){
    key=skg::field<editor::ResourceKey>(native,0);const auto server=skg::field<uint64_t>(native,0x10);
    if(server && server!=UINT64_MAX){
        const auto resolve=H->sym("Simulator::Scenario::ResolveLocalServerAsset");
        if(!resolve || !skg::cdecl_call<bool>(resolve,server,&key) || !key.instance){error="The primary cast dependency is unavailable";return false;}
    }
    return true;
}
bool class_roster_fingerprint(void* resource,const Rows& classes,editor_native_cache::Key& key,std::string& error){
    // Server IDs are resolver hints rather than immutable resource identities.
    // Witness all three resolved keys even on a hit, so dependency admission or
    // another native registration cannot silently change the cached result.
    for(auto row=classes.begin;row!=classes.end;row+=0x27E8)for(auto offset:{0u,0x28u,0x50u}){
        editor::ResourceKey asset;if(!class_primary_asset(row+8+offset,asset,error))return false;
        key.assets.push_back(asset);
    }
    Owned fragment(construct());if(!fragment.p){error="Cannot allocate the owned cast fingerprint";return false;}
    std::memset(skg::ptr_add<void>(fragment.p,0x68),0,0x10);
    std::memset(skg::ptr_add<void>(fragment.p,0x2C30),0,0x20);
    skg::field<float>(fragment.p,0x2C48)=1.f;
    const std::array<uintptr_t,3> slice{uintptr_t(classes.begin),uintptr_t(classes.end),uintptr_t(classes.end)};
    // CopyClassRows owns every nested act/rule/dialog allocation. One exact
    // native Write witnesses the complete roster, including opaque fields and
    // in-place edits whose native pointers and collaboration revision did not
    // change. No native address or partial layout digest grants a cache hit.
    skg::thiscall<void*>(a_copy_class_rows,skg::ptr_add<void>(fragment.p,0x2BF4),slice.data());
    skg::field<uint32_t>(fragment.p,0x2C0C)=skg::field<uint32_t>(resource,0x2C0C);
    return serialize(fragment.p,key.bytes,error);
}
bool assign_class_primary(void* native,const editor::ClassActSettings& settings,std::string& error){
    const auto field=settings.properties.find(editor::kClassAssetResourceProperty);
    const auto* key=field==settings.properties.end()?nullptr:std::get_if<editor::ResourceKey>(&field->second);
    if(!key || !key->instance || !key->type){error="The primary cast resource key is missing";return false;}
    skg::field<editor::ResourceKey>(native,0)=*key;skg::field<uint32_t>(native,0xC)=0;skg::field<uint64_t>(native,0x10)=UINT64_MAX;return true;
}
void* marker_for_key(void* r, uint32_t key) {
    Rows map;
    if (!rows(r,0x2C10,0x238,kPlacementLimit,map)) return nullptr;
    if(key>=uint32_t((map.end-map.begin)/0x238))return nullptr;
    auto p=map.begin+key*0x238;
    return live_slot(p)?p+4:nullptr;
}
void* marker_for_id(void* r, editor::ObjectId id) {
    if(id==kCaptainObject && tracks_authoring() && r==resource(data(mode())) && skg::readable(data(mode()),0x90))
        return skg::field<void*>(data(mode()),0x8C);
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
    if(tracks_authoring() && marker && skg::readable(data(mode()),0x90) && marker==skg::field<void*>(data(mode()),0x8C)){
        selection.push_back(kCaptainObject);selected_class=UINT32_MAX;return;
    }
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
                std::optional<bool> requested_cast=std::nullopt,
                std::optional<bool> requested_authoring=std::nullopt,
                const editor::Document* identity=nullptr,
                const std::vector<uint8_t>* native_bytes=nullptr,bool allow_recipe_replacement=false) {
    struct ScopedAuthoring {
        std::optional<bool> prior=authoring_override;
        explicit ScopedAuthoring(std::optional<bool> value){if(value)authoring_override=value;}
        ~ScopedAuthoring(){authoring_override=prior;}
    } scoped_authoring(requested_authoring);
    const bool tracked=requested_scale_mode?*requested_scale_mode:tracks_relative_scale();
    const bool extended=requested_extended.value_or(tracks_extended_poses());
    const bool cast=requested_cast.value_or(tracks_cast());
    Rows classes, markers, acts;
    if (!rows(r, 0x2BF4, 0x27E8, 1024, classes) || !rows(r, 0x2C10, 0x238, kPlacementLimit, markers) ||
        !rows(r, 0x23C, 0x534, 8, acts)) {
        error = "Native editor resource vectors are invalid"; return false;
    }
    out.document = {};
    const auto& logical=identity?*identity:act_identity;
    const uint32_t count=uint32_t((acts.end-acts.begin)/0x534);
    if(tracks_authoring()){
        if(!count){error="An adventure requires at least one act";return false;}
        if(!logical.act_order.empty() && logical.act_order.size()!=count){error="The native act roster changed without stable identities";return false;}
        if(!project_authoring(r,out.document,error,logical))return false;
        editor::AuthoringSettings project;project.object=kProjectObject;project.kind=editor::SettingsKind::Project;
        project.properties[kExtendedComplexityProperty]=false;
        if(auto prior=logical.settings.find(kProjectObject);prior!=logical.settings.end()){
            auto setting=prior->second.properties.find(kExtendedComplexityProperty);
            if(setting!=prior->second.properties.end()){
                if(!std::holds_alternative<bool>(setting->second)){error="The project complexity setting is not Boolean";return false;}
                project.properties[kExtendedComplexityProperty]=setting->second;
            }
        }
        out.document.settings.emplace(kProjectObject,std::move(project));
        out.document.next_id=std::max<uint64_t>(out.document.next_id,kProjectObject.id+1);
    }else for(uint32_t i=0;i<count;++i)out.document.acts.insert({(3ull<<32)|i,1});
    const auto anchor=tracks_authoring()?*out.document.acts.begin():editor::ObjectId{(3ull<<32)|out.act,1};
    out.document.next_id = std::max<uint64_t>(out.document.next_id,4ull << 32);
    if(tracks_authoring())for(auto id:out.document.act_order)out.document.next_id=std::max(out.document.next_id,id.id+2);
    if(extended)out.document.schema=tracked?placement_property_schema():legacy_extended_schema();
    else if(tracked)out.document.schema=relative_scale_schema();
    for (auto p = classes.begin; p != classes.end; p += 0x27E8)
        out.document.classes.insert({(2ull << 32) | skg::field<uint32_t>(p, 0), 1});
    if(cast){
        out.document.class_schema=cast_property_schema();
        out.document.next_class=skg::field<uint32_t>(r,0x2C0C);
        out.document.next_id=std::max<uint64_t>(out.document.next_id,6ull<<32);
        for(auto row=classes.begin;row!=classes.end;row+=0x27E8){
            Rows companions;
            if(!rows(row+8,0x70,0x4E0,8,companions) || uint32_t((companions.end-companions.begin)/0x4E0)!=count){error="A native class has a different act roster";return false;}
        }
        editor_native_cache::Key key;key.context=class_projection_cache.context();key.source=skg::field<editor::ResourceKey>(r,8);
        key.project=logical.project;key.epoch=logical.epoch;
        key.anchor=anchor;key.acts=count;key.next_class=out.document.next_class;key.authoring=tracks_authoring();
        key.act_order=logical.act_order;key.schema=out.document.class_schema;
        std::string cache_error;
        const bool fingerprinted=class_roster_fingerprint(r,classes,key,cache_error);
        // Fingerprinting is optional. A failed extra allocation or Write must
        // still take the original checked per-class projection path.
        if(auto cached=fingerprinted?class_projection_cache.get(key):std::shared_ptr<const editor_native_cache::Classes>{})out.document.class_settings=*cached;
        else{
            for(auto row=classes.begin;row!=classes.end;row+=0x27E8){
                const auto index=skg::field<uint32_t>(row,0);
                Rows companions;
                if(!rows(row+8,0x70,0x4E0,8,companions) || uint32_t((companions.end-companions.begin)/0x4E0)!=count){error="A native class has a different act roster";return false;}
                editor::ClassActSettings settings;
                settings.object={(5ull<<32)|index,1};settings.class_id={(2ull<<32)|index,1};settings.act_id=anchor;
                editor::StructuredValue value;if(!class_block(row,value,error))return false;
                editor::ResourceKey primary;if(!class_primary_asset(row+8,primary,error))return false;
                settings.properties[kCastBlockProperty]=std::move(value);settings.properties[editor::kClassAssetResourceProperty]=primary;
                out.document.class_settings.emplace(settings.object,std::move(settings));
            }
            if(fingerprinted)class_projection_cache.put(std::move(key),out.document.class_settings);
        }
    }
    out.bindings.clear();
    for (auto p = markers.begin; p != markers.end; p += 0x238) {
        if(!live_slot(p))continue;
        uint32_t key = slot_index(markers,p);
        auto marker = p + 4;
        if(tracks_authoring()){
            Rows companions;
            if(!rows(marker,0x28,0x34,8,companions) || uint32_t((companions.end-companions.begin)/0x34)!=count){error="A native placement has a different act roster";return false;}
        }
        editor::Placement placement;
        if(supplied && !supplied->count(key)){error="Native placement is missing its stable binding";return false;}
        placement.object = marker_id(key,supplied);
        placement.class_id = {(2ull << 32) | skg::field<uint32_t>(marker, 0), 1};
        placement.act_id = anchor;
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
    if(tracks_authoring()){
        std::vector<uint8_t> serialized;
        if(!native_bytes){if(!serialize(r,serialized,error))return false;native_bytes=&serialized;}
        if(!editor_native_terrain::project(*native_bytes,logical,out.document,error,allow_recipe_replacement))return false;
        return true;
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
bool valid_metadata(const std::optional<Snapshot::Metadata>& metadata,std::string& error){
    if(metadata)for(const auto& text:metadata->text)if(text.size()>4096 || text.find(u'\0')!=std::u16string::npos){
        error="The native adventure metadata text is invalid or oversized";return false;
    }
    return true;
}
bool hydrate_native_captain(void* resource,Snapshot& snapshot,std::string& error){
    auto key=skg::field<editor::ResourceKey>(resource,0x130);auto locked=skg::field<bool>(resource,0x175);
    if(!captain_identity(snapshot.document,key,locked,error))return false;
    snapshot.captain=key;snapshot.captain_locked=locked;return true;
}
bool captain_sidecar_matches(void* resource,const Snapshot& snapshot,const editor::Document& document,std::string& error){
    // Old projections without Captain settings have no asserted sidecar when
    // both values are their original defaults. Expansion hydrates them from
    // the owned resource before adopting the current authoring schema.
    const bool typed=document.settings.contains(kCaptainObject);
    const bool known=typed || snapshot.captain.instance || snapshot.captain.type || snapshot.captain.group || snapshot.captain_locked;
    auto actual=skg::field<editor::ResourceKey>(resource,0x130);
    const auto server=skg::field<uint64_t>(resource,0x140);
    if(known && typed && server && server!=UINT64_MAX){
        const auto resolve=H&&H->sym?H->sym("Simulator::Scenario::ResolveLocalServerAsset"):0;
        if(!resolve || !skg::cdecl_call<bool>(resolve,server,&actual)){
            error="The authored captain dependency is unavailable";return false;
        }
    }
    if(known && (snapshot.captain!=actual || snapshot.captain_locked!=skg::field<bool>(resource,0x175))){
        error="The native captain identity differs from its authored sidecar";return false;
    }
    return true;
}
bool capture_metadata(void* owner,Snapshot::Metadata& metadata,std::string& error){
    if(!skg::readable(owner,0x80)){error="The native adventure metadata owner is unavailable";return false;}
    Snapshot::Metadata captured;captured.genre=skg::field<uint32_t>(owner,0x7C);
    for(size_t i=0;i<captured.text.size();++i){
        const auto offset=0x4C+16*i;
        const auto begin=skg::field<uintptr_t>(owner,offset),end=skg::field<uintptr_t>(owner,offset+4);
        if(end<begin || (end-begin)%2 || end-begin>8192 || (end!=begin && !skg::readable(reinterpret_cast<void*>(begin),end-begin))){
            error="The native adventure metadata is unavailable or oversized";return false;
        }
        if(end!=begin)captured.text[i].assign(reinterpret_cast<const char16_t*>(begin),(end-begin)/2);
    }
    if(!valid_metadata(captured,error))return false;
    metadata=std::move(captured);return true;
}
bool capture_resource(void* r, uint32_t act, Snapshot& out, std::string& error,
                      const std::map<uint32_t,editor::ObjectId>* supplied=nullptr,const editor::Document* identity=nullptr) {
    Owned authored;
    if(!terrain_grounding.empty()){
        authored.p=construct();if(!authored.p){error="Cannot allocate an owned grounding snapshot";return false;}
        skg::thiscall<void*>(a_copy,authored.p,r);r=authored.p;
        Rows markers;if(!rows(r,0x2C10,0x238,kPlacementLimit,markers)){error="Grounded native markers are unavailable";return false;}
        for(auto p=markers.begin;p!=markers.end;p+=0x238)if(live_slot(p)){
            const auto index=slot_index(markers,p);auto logical=marker_id(index);
            if(supplied){auto found=supplied->find(index);if(found!=supplied->end())logical=found->second;}
            auto found=terrain_grounding.find(logical);
            if(found!=terrain_grounding.end() && skg::field<editor::Vec3>(p,8)==found->second.native)
                skg::field<editor::Vec3>(p,8)=found->second.authored;
        }
    }
    out = {}; out.act = act;
    out.source = skg::field<editor::ResourceKey>(r, 8);
    if (!serialize(r,out.bytes,error) || !projection(r, out, error,supplied,std::nullopt,std::nullopt,std::nullopt,std::nullopt,identity,&out.bytes,terrain_recipe_replacement&&terrain_capturing)) return false;
    if(!hydrate_native_captain(r,out,error))return false;
    // Write omits free native pool slots. Read inserts its marker sequence into
    // a fresh pool, so artifact bindings describe serialized order, not live holes.
    std::vector<Binding> compact;
    if(!serialized_bindings(out.bindings,compact,error))return false;
    out.bindings=std::move(compact);
    ++captures; return true;
}

using HistoryFn = void(SK_THISCALL*)(void*);
HistoryFn original_start, original_commit, original_undo, original_redo;
HistoryFn original_terrain_start,original_terrain_commit,original_terrain_rebuild,original_terrain_discard,original_terrain_undo;
using TerrainLoadFn=void(SK_THISCALL*)(void*,void*);
using TerrainRemoveFn=void(SK_THISCALL*)(void*,void*);
using TerrainUpdateFn=void(SK_THISCALL*)(void*,bool);
TerrainLoadFn original_terrain_load,original_terrain_build;TerrainRemoveFn original_terrain_remove;
TerrainUpdateFn original_terrain_update;
using TerrainAppendModelFn=void(SK_THISCALL*)(void*,const editor::ResourceKey*,const void*);
using TerrainAppendEffectFn=void(SK_THISCALL*)(void*,uint32_t,uint32_t,const void*,uint32_t,uint32_t,uint32_t);
TerrainAppendModelFn original_terrain_append_model;TerrainAppendEffectFn original_terrain_append_effect;
using SelectActFn=void(SK_CDECL*)(uint32_t);
SelectActFn original_select_act;
void SK_CDECL select_act_native(uint32_t act){
    if(!applying && active() && (!tracks_authoring() || !C.navigation_allowed || !C.navigation_allowed()))return;
    original_select_act(act);
}
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
std::optional<Snapshot::Metadata> gesture_before_metadata;
std::string gesture_metadata_error;
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
    bool terrain=false;
    uint32_t act=0;
    uintptr_t actor=0;
};
std::optional<HeldTool> held_tool;
struct HeldTerrainPreset {
    GestureScope scope;uint64_t generation=0;uint32_t act=0;double started=0;bool focused=false;
    void* owner=nullptr;
    std::vector<uint8_t> bytes;
    std::map<editor::ObjectId,editor::TerrainRecord> records;
    editor::AuthoringSettings planet;
};
std::optional<HeldTerrainPreset> held_terrain_preset;
void hold_terrain_preset(void*);
void continue_terrain_preset();
bool real_focus();
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
struct HeldCaptainAsset {
    GestureScope scope;
    uint64_t generation;
    editor::Revision revision;
    editor::ResourceKey key;
    double started;
};
std::optional<HeldCaptainAsset> held_captain_asset;
enum class CrewAction : uint8_t { Add, Replace, Remove, Lock, Capacity };
struct HeldCrewAction { GestureScope scope;uint64_t generation;editor::Revision revision;CrewAction action;int32_t index;editor::ResourceKey key;bool value;double started; };
std::optional<HeldCrewAction> held_crew_action;
void run_crew_action(CrewAction,int32_t,const editor::ResourceKey&,bool);
GestureScope import_scope;
uint64_t import_generation=0;
double import_started=0;
bool input_queued=false;
void SK_THISCALL commit(void*);
uint32_t SK_THISCALL import_native(void*,const editor::ResourceKey*);
void SK_THISCALL remove_class_native(void*,uint32_t);
void SK_THISCALL captain_asset(void*,const editor::ResourceKey*);
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
    std::optional<Snapshot::Metadata> before_metadata,after_metadata;
    std::string metadata_error;
};
std::deque<Pending> pending;
bool speech_pending(){return std::any_of(pending.begin(),pending.end(),[](const auto& item){return item.speech!=nullptr;});}
bool ordinary_capture_pending(){return std::any_of(pending.begin(),pending.end(),[](const auto& item){return !item.speech;});}

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
        if(C.gesture_capture)C.gesture_capture(true);
        bool ok=item.metadata_error.empty() && item.before_metadata && item.after_metadata;
        if(!ok)error=item.metadata_error.empty()?"The native gesture lost its authored metadata witnesses":item.metadata_error;
        ok=ok&&capture_resource(before.p,item.act,gesture.before,error,&item.before_bindings);
        ok=ok&&capture_resource(skg::field<void*>(entry.p,0x14),item.act,gesture.after,error,&item.after_bindings);
        if(ok){gesture.before.metadata=item.before_metadata;gesture.after.metadata=item.after_metadata;}
        if(C.gesture_capture)C.gesture_capture(false);
        if(ok){
            if (C.gesture) C.gesture(std::move(gesture));
        } else if (C.failure) C.failure(error);
    }
}
void SK_THISCALL start(void* self) {
    editor_native::state();
    if(!applying && active() && ordinary_capture_pending())return;
    bool observe=!applying && !cancellation && active() && self==data(mode()) && *reinterpret_cast<uint8_t*>(a_history+0x20);
    if(observe){
        gesture_owner=self;gesture_entry=nullptr;gesture_generation=generation;
        gesture_scope_pin=C.gesture_scope?C.gesture_scope():GestureScope{};
        gesture_serial=gesture_counter==UINT64_MAX?0:++gesture_counter;
        gesture_clone=input_clone;
        gesture_cast=input_cast;
        gesture_before_metadata.reset();gesture_metadata_error.clear();
        Snapshot::Metadata metadata;
        if(capture_metadata(self,metadata,gesture_metadata_error))gesture_before_metadata=std::move(metadata);
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
void remember_grounding(const editor::Document& document){
    terrain_grounding.clear();
    for(const auto& [id,row]:document.placements){
        void* marker=marker_for_id(resource(data(mode())),id);if(!marker)continue;
        const auto actual=skg::field<editor::Vec3>(marker,4);
        if(actual!=row.position)terrain_grounding.emplace(id,Grounding{actual,row.position});
    }
}
bool same_terrain(const editor::Document& left,const editor::Document& right){
    const auto a=editor::terrain_recipe(left),b=editor::terrain_recipe(right);
    if(a.size()!=b.size())return false;
    for(size_t i=0;i<a.size();++i)if(a[i].kind!=b[i].kind || a[i].payload!=b[i].payload)return false;
    const auto x=left.settings.find(editor_native_terrain::kPlanetObject),y=right.settings.find(editor_native_terrain::kPlanetObject);
    return (x==left.settings.end() && y==right.settings.end()) ||
        (x!=left.settings.end() && y!=right.settings.end() && x->second.properties==y->second.properties);
}
void remember_terrain(const editor::Document& document){
    materialized_terrain.emplace();materialized_terrain->terrain=document.terrain;
    auto row=document.settings.find(editor_native_terrain::kPlanetObject);
    if(row!=document.settings.end())materialized_terrain->settings.emplace(*row);
}
std::optional<editor::ObjectId> terrain_record(void* descriptor){
    if(!skg::readable(descriptor,16))return std::nullopt;
    void* definition=skg::field<void*>(descriptor,4);
    if(!skg::readable(definition,0x69))return std::nullopt;
    const auto kind=skg::field<bool>(definition,0x68)?editor::TerrainKind::Model:editor::TerrainKind::Effect;
    const auto index=skg::field<uint32_t>(descriptor,8);
    const auto& identity=terrain_before?terrain_before->document:act_identity;
    uint32_t at=0;for(const auto& row:editor::terrain_recipe(identity))if(row.kind==kind){
        if(at++==index)return row.object;
    }
    return std::nullopt; // The current stroke is an uncommitted append.
}
bool terrain_gate_record(void* actor){
    if(!terrain_available || !C.terrain_gate || terrain_pending)return false;
    void* descriptor=actor && a_terrain_selection?skg::thiscall<void*>(a_terrain_selection,terraform_mode(),actor):nullptr;
    const auto object=terrain_record(descriptor);
    if(descriptor && !object && (!skg::readable(terraform_mode(),0x1B9) || !skg::field<bool>(terraform_mode(),0x1B8)))return false;
    return C.terrain_gate(object?std::vector<editor::ObjectId>{*object}:std::vector<editor::ObjectId>{},{});
}
bool terrain_begin(void* self){
    if(terrain_pending)return false;
    if(terrain_before)return terrain_owner==data(mode()) && terrain_generation==generation &&
        (!C.gesture_scope || terrain_scope==C.gesture_scope());
    if(!terrain_available || self!=terraform_mode() || terrain_pending || !C.terrain_gate || !C.terrain_gate({},{}))return false;
    const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    if(!valid_scope(scope))return false;
    Snapshot before;std::string error;
    if(!capture_draft(before,error)){if(C.notice)C.notice(error);return false;}
    terrain_before=std::move(before);terrain_scope=scope;terrain_generation=generation;terrain_owner=data(mode());
    return true;
}
void SK_THISCALL terrain_append_model(void* self,const editor::ResourceKey* key,const void* transform){
    if(!applying && active() && (!C.terrain_gate || !C.terrain_gate({},{}) || !terrain_begin(self)))return;
    original_terrain_append_model(self,key,transform);
}
void SK_THISCALL terrain_append_effect(void* self,uint32_t a,uint32_t b,const void* transform,uint32_t c,uint32_t d,uint32_t e){
    if(!applying && active() && (!C.terrain_gate || !C.terrain_gate({},{}) || !terrain_begin(self)))return;
    original_terrain_append_effect(self,a,b,transform,c,d,e);
}
void SK_THISCALL terrain_start(void* self){
    state();
    if(!applying && active() && !terrain_begin(self))return;
    original_terrain_start(self);
}
void SK_THISCALL terrain_rebuild(void* self){
    const auto prior=applying;applying=true;original_terrain_rebuild(self);applying=prior;
    if(self==terraform_mode() && editing()){
        terrain_rebuilding=true;terrain_rebuild_frame=H->frame_index();
        if(terrain_before)remember_grounding(terrain_before->document);
    }
}
void SK_THISCALL terrain_commit(void* self){
    const bool observe=!applying && !terrain_pending && active() && self==terraform_mode() && terrain_before &&
        terrain_generation==generation && terrain_owner==data(mode()) && (!C.gesture_scope || terrain_scope==C.gesture_scope());
    if(observe && C.gesture_released)C.gesture_released();
    original_terrain_commit(self);
    if(observe){
        // Rebuild through the verified native path before projecting the after
        // state. Native grounding is a view derived from recipe and elevation;
        // its untouched output is normalized back to authored coordinates.
        const auto prior=applying;applying=true;terrain_rebuild(self);applying=prior;
        terrain_pending=true;
    }
}
void SK_THISCALL terrain_discard(void* self){
    const bool observe=!applying && !terrain_pending && active() && terrain_before && terrain_generation==generation && terrain_owner==data(mode());
    if(observe && C.gesture_released)C.gesture_released();
    original_terrain_discard(self);
    if(observe)terrain_pending=true;
}
void SK_THISCALL terrain_undo(void* self){
    if(!applying && active()){
        if(terrain_pending)return;
        if(!terrain_pending && terrain_before && self==*reinterpret_cast<void**>(a_history+0x24) && skg::readable(self,0x40) &&
           skg::field<void*>(self,0x3C)==terraform_mode()){
            if(C.gesture_released)C.gesture_released();
            applying=true;original_terrain_undo(self);applying=false;terrain_pending=true;return;
        }
        if(C.history)C.history(false);return;
    }
    original_terrain_undo(self);
}
void SK_CDECL drain_terrain(void*){
    terrain_queued=false;state();
    if(!terrain_pending || !terrain_before)return;
    if(!active() || terrain_generation!=generation || terrain_owner!=data(mode()) ||
       (C.gesture_scope && terrain_scope!=C.gesture_scope())){terrain_pending=false;terrain_before.reset();terrain_recipe_replacement=false;return;}
    if(!terrain_settled() || !*reinterpret_cast<uint8_t*>(a_history+0x20))return;
    terrain_capturing=true;
    if(C.gesture_capture)C.gesture_capture(true);
    Gesture gesture;gesture.before=*terrain_before;gesture.generation=generation;std::string error;
    const bool okay=capture_draft(gesture.after,error);
    if(C.gesture_capture)C.gesture_capture(false);
    terrain_capturing=false;terrain_pending=false;terrain_before.reset();terrain_rebuilding=false;terrain_recipe_replacement=false;
    if(okay){remember_terrain(gesture.after.document);if(C.gesture)C.gesture(std::move(gesture));}
    else if(C.failure)C.failure(error);
}
void SK_THISCALL terrain_remove(void* self,void* actor){
    if(!applying && active() && (!terrain_gate_record(actor) || !terrain_begin(self)))return;
    original_terrain_remove(self,actor);
}
void SK_THISCALL terrain_update(void* self,bool final){
    if(!applying && active()){
        void* descriptor=skg::readable(self,0x1B4)?skg::field<void*>(self,0x1B0):nullptr;
        void* actor=skg::readable(descriptor,4)?skg::field<void*>(descriptor,0):nullptr;
        if(!terrain_gate_record(actor) || !terrain_begin(self))return;
    }
    original_terrain_update(self,final);
}
void SK_THISCALL terrain_load(void* self,void* properties){
    if(!applying && active()){
        if(terrain_pending)return;
        std::vector<editor::ObjectId> records;for(const auto& [id,row]:act_identity.terrain){(void)row;records.push_back(id);}
        std::vector<uint32_t> fields;for(const auto& [id,type]:editor_native_terrain::planet_properties()){(void)type;fields.push_back(id);}
        if(!C.terrain_gate || !C.terrain_gate(records,fields) || !terrain_begin(self)){
            hold_terrain_preset(properties);return;
        }
        held_terrain_preset.reset();
        terrain_recipe_replacement=true;
    }
    original_terrain_load(self,properties);
}
using TerrainResourceInitFn=void(SK_THISCALL*)(void*);
using TerrainActivateFn=void(SK_THISCALL*)(void*,const editor::ResourceKey*);
TerrainResourceInitFn original_terrain_resource_init;
TerrainActivateFn original_terrain_activate;
struct NativeTerrainInitialization {
    bool prior=applying;
    NativeTerrainInitialization(){applying=true;}
    ~NativeTerrainInitialization(){applying=prior;}
};
void SK_THISCALL terrain_resource_init(void* self){
    // F46502 runs after ScenarioMode+CC and ScenarioData+74 are installed,
    // while the joining editor still has no authoring leases. Its F261D1
    // Build call initializes the native planet before actor grounding. Keep
    // the whole verified native boundary, including nested setters, outside
    // authoring capture and ownership checks.
    NativeTerrainInitialization initialization;
    original_terrain_resource_init(self);
}
void SK_THISCALL terrain_activate(void* self,const editor::ResourceKey* key){
    // EF1DEE creates the Terraform scene. Its empty-key fallback builds the
    // default planet directly, rather than passing through F261B0.
    NativeTerrainInitialization initialization;
    original_terrain_activate(self,key);
}
void SK_THISCALL terrain_build(void* self,void* properties){
    bool own_boundary=false;
    if(!applying && active() && properties!=skg::field<void*>(self,0x0C)){
        std::vector<editor::ObjectId> records;for(const auto& [id,row]:act_identity.terrain){(void)row;records.push_back(id);}
        std::vector<uint32_t> fields;for(const auto& [id,type]:editor_native_terrain::planet_properties()){(void)type;fields.push_back(id);}
        if(!C.terrain_gate || !C.terrain_gate(records,fields) || !terrain_begin(self)){
            hold_terrain_preset(properties);return;
        }
        held_terrain_preset.reset();
        terrain_recipe_replacement=true; // F3753C loads this external recipe.
        own_boundary=*reinterpret_cast<uint8_t*>(a_history+0x20)!=0;
        if(own_boundary)original_terrain_start(self);
    }
    const auto prior=applying;applying=true;original_terrain_build(self,properties);applying=prior;
    if(self==terraform_mode() && editing()){
        terrain_rebuilding=true;terrain_rebuild_frame=H->frame_index();
        remember_grounding(terrain_before?terrain_before->document:act_identity);
    }
    if(own_boundary)terrain_commit(self);
}
void hold_terrain_preset(void* properties){
    const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    const auto current=state();
    const auto planet=act_identity.settings.find(editor_native_terrain::kPlanetObject);
    if(!valid_scope(scope) || !a_copy_terrain_properties ||
       !skg::readable(properties,0x38) || planet==act_identity.settings.end())return;
    try{
        // DFFA29 uses this exact owning PropertyList assignment. Freeze it as
        // bytes before the picker/native caller releases its borrowed payload.
        Owned copy(construct());std::string error;
        if(!copy.p){if(C.notice)C.notice("The pending planet preset could not allocate its owned copy.");return;}
        skg::thiscall<void*>(a_copy_terrain_properties,skg::ptr_add<void>(copy.p,0x18),properties);
        std::vector<uint8_t> bytes;editor::Document checked;
        if(!serialize(copy.p,bytes,error) || !editor_native_terrain::project(bytes,{},checked,error)){
            if(C.notice)C.notice(error.empty()?"The pending planet preset is invalid.":error);return;
        }
        HeldTerrainPreset item{scope,current.generation,current.act,H->time_seconds(),real_focus(),data(mode()),
            std::move(bytes),act_identity.terrain,planet->second};
        if(held_terrain_preset && held_terrain_preset->scope==item.scope &&
           held_terrain_preset->generation==item.generation && held_terrain_preset->act==item.act){
            item.records=held_terrain_preset->records;item.planet=held_terrain_preset->planet;
        }
        held_terrain_preset=std::move(item);
    }catch(const std::bad_alloc&){if(C.notice)C.notice("The pending planet preset exceeded available memory; retry after editing settles.");}
}
void continue_terrain_preset(){
    if(!held_terrain_preset)return;
    const auto observed=state();if(!held_terrain_preset)return;
    const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    const auto& item=*held_terrain_preset;
    const auto planet=act_identity.settings.find(editor_native_terrain::kPlanetObject);
    if(!active() || item.scope!=scope || item.generation!=generation || item.act!=observed.act ||
       item.owner!=data(mode()) || H->time_seconds()-item.started>2 || (GetKeyState(VK_ESCAPE)&0x8000) ||
       (item.focused && !real_focus())){held_terrain_preset.reset();return;}
    if(item.records!=act_identity.terrain || planet==act_identity.settings.end() || planet->second!=item.planet){
        held_terrain_preset.reset();if(C.notice)C.notice("The planet changed while preset ownership was pending; select the preset again.");return;
    }
    if(applying || !observed.ready || !pending.empty() || queued || gesture_before_resource ||
       (observed.gesture && !terrain_before) || (C.transaction_pending && C.transaction_pending()) || !C.terrain_gate)return;
    std::vector<editor::ObjectId> records;for(const auto& [id,row]:item.records){(void)row;records.push_back(id);}
    std::vector<uint32_t> fields;for(const auto& [id,type]:editor_native_terrain::planet_properties()){(void)type;fields.push_back(id);}
    if(!C.terrain_gate(records,fields))return;
    auto accepted=std::move(*held_terrain_preset);held_terrain_preset.reset();std::string error;
    Owned copy(parse(accepted.bytes,error));
    if(!copy.p){if(C.notice)C.notice(error);return;}
    auto* tool=terraform_mode();if(!terrain_begin(tool))return;
    if(*reinterpret_cast<uint8_t*>(a_history+0x20))original_terrain_start(tool);
    terrain_build(tool,skg::ptr_add<void>(copy.p,0x18));
    terrain_commit(tool);
}
using TerrainValueFn=void(SK_THISCALL*)(void*,uint32_t);
std::array<TerrainValueFn,18> original_terrain_values{};
using TerrainGradientFn=void(SK_THISCALL*)(void*,void*,void*);
TerrainGradientFn original_terrain_gradient;
const std::array<std::vector<uint32_t>,18>& terrain_value_fields(){
    static const std::array<std::vector<uint32_t>,18> fields{{
        {0x968C496A},{0x968C496B},{0xB6929C95},{0xB6929C94},
        {0x03A23F98},{0x03A23F98},{0x03A23F99},{0x03A23F99},{0x07B18058},{0x8E390196},
        {0x03A23F98},
        {0x0536250D,0x968C496A,0x968C496B,0xB6929C94,0xB6929C95,0xB6929C92,0xB6929C93},
        {0x0536250C,0x968C496A,0x968C496B,0xB6929C94,0xB6929C95,0xB6929C92,0xB6929C93},
        {0x56B14F05},
        {0x968C496A,0x968C496B,0xB6929C94,0xB6929C95,0xB6929C92,0xB6929C93},
        {0x0B208B41},{0x0B208B41},{0x48C74C5C,0x03ABB0AF}}};
    return fields;
}
bool real_focus();
struct HeldTerrainControl {
    GestureScope scope;uint64_t generation=0;uint32_t act=0,index=0,raw=0;double started=0;bool focused=false;
    std::array<uint32_t,3> pointed{};
    std::vector<float> elevations;
    std::vector<editor::Vec3> colors;
    uint32_t control=0;uintptr_t ui=0,layout=0;
    std::map<uint32_t,std::pair<editor::Revision,std::optional<editor::Value>>> guards;
};
std::deque<HeldTerrainControl> held_terrain_controls;
std::optional<editor::Value> planet_value(uint32_t property){
    const auto row=act_identity.settings.find(editor_native_terrain::kPlanetObject);
    if(row==act_identity.settings.end())return std::nullopt;
    const auto value=row->second.properties.find(property);
    return value==row->second.properties.end()?std::nullopt:std::optional<editor::Value>{value->second};
}
void hold_terrain_control(HeldTerrainControl item,const std::vector<uint32_t>& fields){
    item.scope=C.gesture_scope?C.gesture_scope():GestureScope{};if(!valid_scope(item.scope))return;
    const auto native=state();item.generation=native.generation;item.act=native.act;item.started=H->time_seconds();item.focused=real_focus();
    for(const auto property:fields)item.guards.emplace(property,std::pair{
        editor::field_revision(act_identity,editor_native_terrain::kPlanetObject,{editor::Field::Property,property}),planet_value(property)});
    // Coalesce the still-unapplied values of one slider/picker. Retain its first
    // guard, so an intervening edit cannot become implicit consent to overwrite.
    const auto prior=std::find_if(held_terrain_controls.begin(),held_terrain_controls.end(),[&](const auto& old){return old.index==item.index && old.control==item.control;});
    if(prior!=held_terrain_controls.end()){
        item.guards=prior->guards;item.scope=prior->scope;item.generation=prior->generation;item.act=prior->act;
        *prior=std::move(item);
    }else if(held_terrain_controls.size()<16)held_terrain_controls.push_back(std::move(item));
    else if(C.notice)C.notice("Pending terrain controls are full; retry after the current changes finish.");
}
void hold_terrain_value(size_t index,uint32_t raw){
    HeldTerrainControl item;item.index=uint32_t(index);item.raw=raw;
    if(index<4 || index==17){
        if(!skg::readable(reinterpret_cast<void*>(uintptr_t(raw)),12))return;
        std::memcpy(item.pointed.data(),reinterpret_cast<void*>(uintptr_t(raw)),12);
    }
    hold_terrain_control(std::move(item),terrain_value_fields()[index]);
}
template<size_t Index>void SK_THISCALL terrain_value(void* self,uint32_t raw){
    if(!applying && active() && (!C.terrain_gate || !C.terrain_gate({},terrain_value_fields()[Index]) || !terrain_begin(self))){
        hold_terrain_value(Index,raw);return;
    }
    if(!applying && active())std::erase_if(held_terrain_controls,[](const auto& item){return item.index==Index;});
    const bool own_boundary=Index>=15 && !applying && active() && *reinterpret_cast<uint8_t*>(a_history+0x20);
    if(own_boundary)original_terrain_start(self);
    original_terrain_values[Index](self,raw);
    if(own_boundary)terrain_commit(self);
}
template<class T>bool copy_terrain_vector(void* source,std::vector<T>& out){
    if(!skg::readable(source,8))return false;
    const auto begin=skg::field<uintptr_t>(source,0),end=skg::field<uintptr_t>(source,4);
    if(end<begin || (end-begin)%sizeof(T) || (end-begin)/sizeof(T)>8192 || (end!=begin && !skg::readable(reinterpret_cast<void*>(begin),end-begin)))return false;
    if(begin!=end)out.assign(reinterpret_cast<const T*>(begin),reinterpret_cast<const T*>(end));
    return true;
}
void SK_THISCALL terrain_gradient(void* self,void* elevations,void* colors){
    if(!applying && active() && (!C.terrain_gate || !C.terrain_gate({},{0xB6929C92,0xB6929C93}) || !terrain_begin(self))){
        HeldTerrainControl item;item.index=18;
        if(copy_terrain_vector(elevations,item.elevations) && copy_terrain_vector(colors,item.colors) && item.elevations.size()==item.colors.size() &&
           std::all_of(item.elevations.begin(),item.elevations.end(),[](float v){return std::isfinite(v);}) &&
           std::all_of(item.colors.begin(),item.colors.end(),[](const auto& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}))
            hold_terrain_control(std::move(item),{0xB6929C92,0xB6929C93});
        return;
    }
    if(!applying && active())std::erase_if(held_terrain_controls,[](const auto& item){return item.index==18;});
    original_terrain_gradient(self,elevations,colors);
}
bool replay_flora_control(const HeldTerrainControl&);
void continue_terrain_control(){
    if(held_terrain_controls.empty())return;
    const auto item=held_terrain_controls.front();const auto observed=state();
    if(held_terrain_controls.empty())return; // state() retired a replaced context.
    const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    if(!active() || item.scope!=scope || item.generation!=generation || item.act!=observed.act ||
       H->time_seconds()-item.started>2 || (GetKeyState(VK_ESCAPE)&0x8000) || (item.focused && !real_focus())){
        held_terrain_controls.pop_front();return;
    }
    std::vector<uint32_t> fields;
    for(const auto& [property,guard]:item.guards){
        if(guard.first!=editor::field_revision(act_identity,editor_native_terrain::kPlanetObject,{editor::Field::Property,property}) || guard.second!=planet_value(property)){
            char message[144];std::snprintf(message,sizeof message,"Pending terrain property %08X changed before ownership arrived; retry this control.",property);
            if(C.notice)C.notice(message);held_terrain_controls.pop_front();return;
        }
        fields.push_back(property);
    }
    if(applying || !observed.ready || observed.gesture || !pending.empty() || queued || gesture_before_resource ||
       (C.transaction_pending && C.transaction_pending()) || !C.terrain_gate || !C.terrain_gate({},fields))return;
    auto accepted=std::move(held_terrain_controls.front());held_terrain_controls.pop_front();
    if(accepted.index>=19){replay_flora_control(accepted);return;}
    auto* tool=terraform_mode();if(!terrain_begin(tool))return;
    original_terrain_start(tool);
    if(accepted.index<18){
        const auto raw=accepted.index<4 || accepted.index==17?uint32_t(reinterpret_cast<uintptr_t>(accepted.pointed.data())):accepted.raw;
        original_terrain_values[accepted.index](tool,raw);
    }else{
        // F34380's primitive assignments 50D5A0/473A00 consume only source
        // begin/end. Destination vectors retain their native owning allocators.
        struct Source {const void* begin;const void* end;const void* capacity;uint64_t reserved=0;};
        const auto* height_end=accepted.elevations.empty()?accepted.elevations.data():accepted.elevations.data()+accepted.elevations.size();
        const auto* color_end=accepted.colors.empty()?accepted.colors.data():accepted.colors.data()+accepted.colors.size();
        Source heights{accepted.elevations.data(),height_end,height_end};
        Source colors{accepted.colors.data(),color_end,color_end};
        original_terrain_gradient(tool,&heights,&colors);
    }
    terrain_commit(tool);
}
bool flora_gate(){return C.terrain_gate && C.terrain_gate({},{0x04E13CB7,0x073154E0,0x073137BB,0x072BEC91});}
using FloraRemoveFn=void(SK_THISCALL*)(void*,void*);
using FloraAddFn=void(SK_THISCALL*)(void*,void*,const editor::ResourceKey*);
using FloraInputFn=bool(SK_THISCALL*)(void*,void*,void*);
FloraRemoveFn original_flora_remove;FloraAddFn original_flora_add;FloraInputFn original_flora_input;
void* current_flora_ui(){
    void* display=skg::readable(mode(),0x18)?skg::field<void*>(mode(),0x14):nullptr;
    void* terraform_ui=skg::readable(display,0x2C)?skg::field<void*>(display,0x28):nullptr;
    if(!a_terraform_ui_vtable || !a_flora_ui_vtable || !skg::readable(terraform_ui,0x1C) || skg::field<uintptr_t>(terraform_ui,0)!=a_terraform_ui_vtable)return nullptr;
    void* flora=skg::field<void*>(terraform_ui,0x18);
    return skg::readable(flora,0x174) && skg::field<uintptr_t>(flora,0)==a_flora_ui_vtable?flora:nullptr;
}
bool hold_flora_control(uint32_t index,void* self,void* card,const editor::ResourceKey* key=nullptr){
    if(self!=current_flora_ui() || !card)return false;
    HeldTerrainControl item;item.index=index;item.control=sui::control_id(card);item.ui=reinterpret_cast<uintptr_t>(self);
    item.layout=skg::field<uintptr_t>(self,0x168);
    if(index!=21 && (item.control<0x072C2820 || item.control>0x072C2828))return false;
    if(key)std::memcpy(item.pointed.data(),key,12);
    if(index==21){
        if(!native_slot(card,3))return false;
        void* slider=skg::vcall<void*>(card,3,0x0F00A8A0);
        if(!native_slot(slider,0x30/4))return false;
        const auto maximum=skg::vcall<int32_t>(slider,0x30/4),value=skg::vcall<int32_t>(slider,0x20/4);
        if(maximum<=0 || value<0 || value>maximum)return false;
        const float density=float(value)/float(maximum);std::memcpy(&item.raw,&density,4);
    }
    hold_terrain_control(std::move(item),{0x04E13CB7,0x073154E0,0x073137BB,0x072BEC91});return true;
}
bool replay_flora_control(const HeldTerrainControl& item){
    void* ui=current_flora_ui();
    void* layout=ui?skg::field<void*>(ui,0x168):nullptr;
    void* card=layout?sui::find(layout,item.control):nullptr;
    if(!ui || reinterpret_cast<uintptr_t>(ui)!=item.ui || reinterpret_cast<uintptr_t>(layout)!=item.layout || !card){
        if(C.notice)C.notice("The pending flora control was replaced; retry it in the current Terraform panel.");return false;
    }
    if(item.index==19)original_flora_remove(ui,card);
    else if(item.index==20){editor::ResourceKey key;std::memcpy(&key,item.pointed.data(),12);original_flora_add(ui,card,&key);}
    else if(item.index==21){
        void* slider=native_slot(card,3)?skg::vcall<void*>(card,3,0x0F00A8A0):nullptr;
        if(!native_slot(slider,0x30/4))return false;
        const auto maximum=skg::vcall<int32_t>(slider,0x30/4);float density;std::memcpy(&density,&item.raw,4);
        if(maximum<=0 || !std::isfinite(density) || density<0 || density>1 || !terrain_begin(terraform_mode()))return false;
        original_terrain_start(terraform_mode());
        const auto prior=applying;applying=true;
        skg::vcall<void>(slider,0x1C/4,int32_t(std::lround(double(density)*maximum)));
        sui::Msg message{card,0,int32_t(0xEF00A884),0x072BFDFD,0,0,0};
        original_flora_input(static_cast<uint8_t*>(ui)+8,card,&message);applying=prior;
        terrain_commit(terraform_mode());
    }
    return true;
}
void SK_THISCALL flora_remove(void* self,void* card){
    if(!applying && active()){
        if(!flora_gate()){hold_flora_control(19,self,card);return;}
        const auto control=sui::control_id(card);std::erase_if(held_terrain_controls,[&](const auto& item){return item.index==19&&item.control==control;});
    }
    original_flora_remove(self,card);
}
void SK_THISCALL flora_add(void* self,void* card,const editor::ResourceKey* key){
    if(!applying && active()){
        if(!flora_gate()){if(key)hold_flora_control(20,self,card,key);return;}
        const auto control=sui::control_id(card);std::erase_if(held_terrain_controls,[&](const auto& item){return item.index==20&&item.control==control;});
    }
    original_flora_add(self,card,key);
}
bool SK_THISCALL flora_input(void* self,void* window,void* message){
    const auto type=skg::readable(message,0x10)?skg::field<uint32_t>(message,8):0;
    const auto control=skg::readable(message,0x10)?skg::field<uint32_t>(message,0xC):0;
    if(!applying && active() && control==0x072BFDFD &&
       (type==0x07A4489C || type==0xEF00A884 || type==0x07A44749)){
        if(!flora_gate()){
            if(type==0xEF00A884 && skg::readable(message,4))hold_flora_control(21,static_cast<uint8_t*>(self)-8,skg::field<void*>(message,0));
            return false;
        }
        if(type==0xEF00A884)std::erase_if(held_terrain_controls,[](const auto& item){return item.index==21;});
        // The slider can have started while its field lease was in flight.
        // Open the same native history boundary before its first accepted value.
        if(type==0xEF00A884 && !terrain_before){
            if(!terrain_begin(terraform_mode()))return false;
            original_terrain_start(terraform_mode());
        }
    }
    return original_flora_input(self,window,message);
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
    gesture_before_metadata.reset();gesture_metadata_error.clear();
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
    if(held_tool && !held_tool->terrain && (selection.empty() || selection.front()!=held_tool->object))finish_held_input();
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
    const auto observed=state();
    if(item.native_started && active() && !cancellation && item.generation==generation &&
       (!item.terrain || (observed.terraforming && observed.act==item.act)) &&
       item.manager==*reinterpret_cast<void**>(a_tools) && observed.gesture)
        original_tool(item.manager,0,0,0);
}
void deny_held_input(const std::vector<editor::Scope>& scopes){
    for(const auto& scope:scopes){
        if(held_tool && (scope.kind==editor::ScopeKind::Document || scope.object==held_tool->object ||
           (held_tool->terrain && scope.kind==editor::ScopeKind::Authoring)))finish_held_input();
        if(scope.kind==editor::ScopeKind::Document){
            if(held_import)H->log(M,SK_DEBUG,"Cast queue denied: keys=%u",unsigned(held_import->size()));
            held_import.reset();
        }
        if(held_class_removal && (scope.kind==editor::ScopeKind::Document || scope.object==editor::ObjectId{(2ull<<32)|held_class_removal->index,1}))held_class_removal.reset();
        if(held_captain_asset && (scope.kind==editor::ScopeKind::Document || scope.object==kCaptainObject))held_captain_asset.reset();
        if(held_crew_action && (scope.kind==editor::ScopeKind::Document || scope.object==kCrewObject))held_crew_action.reset();
        if(scope.kind==editor::ScopeKind::Document || scope.kind==editor::ScopeKind::Authoring || scope.object==editor_native_terrain::kPlanetObject){
            if(held_terrain_preset){held_terrain_preset.reset();if(C.notice)C.notice("The pending planet preset was cancelled because its terrain or settings are owned by another player.");}
            for(auto item=held_terrain_controls.begin();item!=held_terrain_controls.end();){
                if(scope.kind!=editor::ScopeKind::Field || item->guards.contains(scope.field.property)){
                    if(C.notice)C.notice("A pending terrain control was cancelled because its property is owned by another player.");
                    item=held_terrain_controls.erase(item);
                }else ++item;
            }
        }
        if(held_terrain_preset && scope.kind==editor::ScopeKind::Terrain && held_terrain_preset->records.contains(scope.object)){
            held_terrain_preset.reset();if(C.notice)C.notice("The pending planet preset was cancelled because a terrain stamp is owned by another player.");
        }
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
    continue_terrain_preset();
    continue_terrain_control();
    if(held_creation){
        const auto item=*held_creation;
        const auto observed=state();
        const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
        const auto revision=C.object_revision?C.object_revision(item.class_index==UINT32_MAX-1?kCaptainObject:editor::ObjectId{(2ull<<32)|item.class_index,1}):std::nullopt;
        if(!active() || applying || !observed.ready || observed.gesture || observed.generation!=item.generation || scope!=item.scope ||
           !revision || *revision!=item.revision || item.manager!=*reinterpret_cast<void**>(a_tools) ||
           H->time_seconds()-item.started>2 || (GetKeyState(VK_ESCAPE)&0x8000) || (item.focused && !real_focus()))held_creation.reset();
        else if(item.class_index==UINT32_MAX-1?(C.settings_gate && C.settings_gate(kCaptainObject)):(C.structure_gate && C.structure_gate(item.class_index))){
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
                !(C.transaction_pending && C.transaction_pending()) && C.structure_gate && C.structure_gate(UINT32_MAX)){
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
    if(held_captain_asset){
        const auto item=*held_captain_asset;
        const auto revision=C.object_revision?C.object_revision(kCaptainObject):std::nullopt;
        const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
        if(!active() || generation!=item.generation || scope!=item.scope || !revision || *revision!=item.revision ||
           H->time_seconds()-item.started>2 || (GetKeyState(VK_ESCAPE)&0x8000))held_captain_asset.reset();
        else if(pending.empty() && !queued && !gesture_before_resource && !state().gesture &&
                !(C.transaction_pending && C.transaction_pending()) && C.settings_gate && C.settings_gate(kCaptainObject)){
            held_captain_asset.reset();start(data(mode()));captain_asset(data(mode()),&item.key);commit(data(mode()));
        }
    }
    if(held_crew_action){
        const auto item=*held_crew_action;
        const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
        const auto revision=C.object_revision?C.object_revision(kCrewObject):std::nullopt;
        if(!valid_scope(scope) || scope!=item.scope || item.generation!=generation || !revision || *revision!=item.revision ||
           H->time_seconds()-item.started>2 || (GetKeyState(VK_ESCAPE)&0x8000))held_crew_action.reset();
        else if(C.settings_gate && C.settings_gate(kCrewObject)){
            held_crew_action.reset();start(data(mode()));run_crew_action(item.action,item.index,item.key,item.value);commit(data(mode()));
        }
    }
    if(!held_tool)return;
    const auto item=*held_tool;
    const auto observed=state();if(!held_tool)return;
    if(!item.terrain)refresh_selection();
    const auto revision=C.object_revision?C.object_revision(item.object):std::nullopt;
    const auto current_scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    const auto actor=skg::readable(context,8)?skg::field<void*>(context,4):nullptr;
    const auto descriptor=item.terrain && actor && a_terrain_selection?skg::thiscall<void*>(a_terrain_selection,terraform_mode(),actor):nullptr;
    const bool same_selection=item.terrain?
        observed.terraforming && observed.act==item.act && uintptr_t(actor)==item.actor && terrain_record(descriptor)==item.object:
        selection==std::vector<editor::ObjectId>{item.object};
    if(!active() || applying || !observed.ready || (!item.native_started && observed.gesture) || item.generation!=generation ||
       item.scope!=current_scope || !revision || *revision!=item.revision || !same_selection ||
       item.manager!=*reinterpret_cast<void**>(a_tools) || !skg::readable(context,0x2C) ||
       skg::field<uintptr_t>(context,0x10)!=item.auxiliary || (GetKeyState(VK_ESCAPE)&0x8000) ||
       (item.focused && !real_focus()) || H->time_seconds()-item.started>2){
        finish_held_input();return;
    }
    if(item.terrain?(!C.terrain_gate || !C.terrain_gate({item.object},{})):(!C.gate || !C.gate(selection)))return;
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
    if(!applying && active() && ordinary_capture_pending() && action!=0)return 1;
    if(!applying && active() && held_tool){
        if(action==0){held_tool->released=true;return 1;}
        if(action==1)return 1;
        finish_held_input();
    }
    if(!applying && active() && observed.terraforming){
        if(action==1 || action==6 || action==11){
            void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
            void* actor=skg::readable(context,8)?skg::field<void*>(context,4):nullptr;
            if(!terrain_gate_record(actor)){
                const auto descriptor=actor && a_terrain_selection?skg::thiscall<void*>(a_terrain_selection,terraform_mode(),actor):nullptr;
                const auto object=terrain_record(descriptor);
                const auto revision=object && C.object_revision?C.object_revision(*object):std::nullopt;
                const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
                if(action==1 && observed.ready && !observed.gesture && object && revision && valid_scope(scope) &&
                   a_tool_tick && original_tick_tools && self==*reinterpret_cast<void**>(a_tools) && skg::readable(context,0x2C)){
                    held_tool=HeldTool{scope,generation,*object,*revision,self,action,a,b,skg::field<uintptr_t>(context,0x10),false,
                        H->time_seconds(),skg::field<uint32_t>(mode(),0x40),true,real_focus(),true,observed.act,uintptr_t(actor)};
                    original_tool(self,action,a,b);
                    if(held_tool)original_tick_tools(self);
                }
                return 1;
            }
        }
        return original_tool(self,action,a,b);
    }
    MarkerAudio audio(data(mode()),selected_marker());
    if(!applying && active() && speech_pending() && action!=0)return 1;
    if(!applying && active() && held_creation){
        if(action==0){held_creation->released=true;return 1;}
        if(action==1)return 1;
        held_creation.reset();
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
        if(state().terraforming){
            void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
            void* actor=skg::readable(context,8)?skg::field<void*>(context,4):nullptr;
            if(terrain_gate_record(actor))original_delete(self);
            return;
        }
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
        if(class_id==UINT32_MAX-1){
            if(!tracks_authoring() || !C.settings_gate){++rejected;return false;}
            if(!C.settings_gate(kCaptainObject)){
                const auto revision=C.object_revision?C.object_revision(kCaptainObject):std::nullopt;
                const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
                if(revision && valid_scope(scope) && !state().gesture && self==*reinterpret_cast<void**>(a_tools))
                    held_creation=HeldCreation{scope,generation,class_id,*revision,self,H->time_seconds(),skg::field<uint32_t>(mode(),0x40),false,real_focus()};
                ++rejected;return false;
            }
            return original_create(self,class_id);
        }
        if(!C.structure_gate){++rejected;return false;}
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
           (C.transaction_pending && C.transaction_pending()) || !C.structure_gate(UINT32_MAX)){
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
void* inspector_settings(){
    void* script=script_ui();return skg::readable(script,0x1C)?skg::field<void*>(script,0x18):nullptr;
}
bool inspector_shown(void* settings){
    void* layout=skg::readable(settings,0x24)?skg::field<void*>(settings,0x10):nullptr;
    return a_layout_shown && skg::readable(layout,0x18) && skg::thiscall<bool>(a_layout_shown,layout);
}
struct InspectorView {
    void* owner=data(mode());
    std::optional<uint32_t> asset;
    InspectorView(){
        void* settings=inspector_settings();
        if(a_inspector_open && inspector_shown(settings))asset=skg::field<uint32_t>(settings,0x20);
    }
    ~InspectorView(){
        if(!asset || !editing() || owner!=data(mode()))return;
        Rows classes;if(!rows(resource(owner),0x2BF4,0x27E8,1024,classes))return;
        bool exists=false;for(auto row=classes.begin;row!=classes.end;row+=0x27E8)if(skg::field<uint32_t>(row,0)==*asset){exists=true;break;}
        if(!exists)return;
        void* settings=inspector_settings();
        if(!skg::readable(settings,0x24) || (inspector_shown(settings)&&skg::field<uint32_t>(settings,0x20)==*asset))return;
        const bool prior=applying;applying=true;
        skg::thiscall<void>(a_inspector_open,settings,*asset);
        applying=prior;
    }
};
bool class_authoring_message(uint32_t type){
    // EE5380 mutates on these native notifications. Paint, mouse movement,
    // focus gain and other browsing messages must not acquire or renew leases.
    return type==6 || type==7 || type==0x1C || type==0x0685A2B9 || type==0x04CAB02D ||
        type==0xEF00A884 || type==0x07A44749 || type==0x07A4489C || type==0x287259F6;
}
bool SK_THISCALL script_input(void* self,void* window,void* message){
    auto* script=static_cast<uint8_t*>(script_ui());
    if(!applying && active() && script && self==script+8){
        const auto type=skg::readable(message,0x10)?skg::field<uint32_t>(message,8):0;
        if(type==1 && skg::readable(message,0x14) && skg::field<uint32_t>(message,0x10)==0x1B && C.act_action)C.act_action(-1);
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
            if(!class_authoring_message(type))return original_script_input(self,window,message);
            if(tracks_cast() && C.class_gate && C.class_gate(skg::field<uint32_t>(settings,0x20))){
                const auto previous=input_cast;input_cast=true;
                const auto result=original_script_input(self,window,message);input_cast=previous;return result;
            }
            return false;
        }
        if(tracks_authoring()){
            auto local=state().act;editor::ObjectId owner=act_settings_id(local);
            if(local<act_identity.act_order.size())for(const auto& [id,row]:act_identity.settings)
                if(row.act_id==act_identity.act_order[local]){owner=id;break;}
            auto text_control=[](void* target){
                const auto id=native_slot(target,0x1C/4)?sui::control_id(target):0;
                return id==0x0743B978 || id==0x0710A140 || id==0x05CA82B7 || id==0x07CCBD48 || id==0x07DF4C98;
            };
            // Reserve settings while the native text buffer receives focus or
            // typing. Its later Enter/focus-loss setter must already own the
            // act; a first request at that setter would discard the typed value.
            if(C.settings_gate && skg::readable(message,0x1C) &&
               ((type==0x1B && !skg::field<uint32_t>(message,0xC) && text_control(skg::field<void*>(message,0x18))) ||
                ((type==1 || type==3) && text_control(skg::field<void*>(message,0)))))
                C.settings_gate(owner);
            // EE4290 has authored branches only for key/down-drag events,
            // focus enter/leave, component activation and the timer selector.
            // Hover, redraw and ordinary focus navigation do not take a lease.
            if(type!=1 && type!=6 && type!=7 && type!=8 && type!=0x1B && type!=0x1C &&
               type!=0x287259F6 && type!=0x4F5527E8)return original_script_input(self,window,message);
            if(type==8 || type==0x1B)return original_script_input(self,window,message);
            if(type==0x1C && skg::readable(message,0x10) && skg::field<uint32_t>(message,0xC))
                return original_script_input(self,window,message); // Mouse leave does not commit text.
            if(type==1 && skg::readable(message,0x14) && skg::field<uint32_t>(message,0x10)!=13 && skg::field<uint32_t>(message,0x10)!=14)
                return original_script_input(self,window,message);
            if(type==0x1C && skg::readable(message,0x1C)){
                if(!text_control(skg::field<void*>(message,0x18)))return original_script_input(self,window,message);
            }
            const auto control=skg::readable(message,0x10)?skg::field<uint32_t>(message,0xC):0;
            // Act selection is local browsing. Roster changes need a separate
            // structural operation; they cannot escape the current act owner.
            if(control==0x07BD2DD0 || control==0x0743BC38){
                if(type==0x287259F6 && C.act_action)C.act_action(control==0x07BD2DD0?0:1);
                return false;
            }
            if(control>=0x0743B8E1 && control<=0x0743B8E8){
                if(state().gesture || (C.transaction_pending && C.transaction_pending()))return false;
                return original_script_input(self,window,message);
            }
            if(C.settings_gate && C.settings_gate(owner))return original_script_input(self,window,message);
        }
        return false;
    }
    return original_script_input(self,window,message);
}
bool SK_THISCALL class_input(void* self,void* window,void* message){
    auto* script=static_cast<uint8_t*>(script_ui());
    auto* settings=script && skg::readable(script,0x1C)?skg::field<uint8_t*>(script,0x18):nullptr;
    if(!applying && active() && settings && self==settings+0xC){
        const auto type=skg::readable(message,0x10)?skg::field<uint32_t>(message,8):0;
        if(!class_authoring_message(type))return original_class_input(self,window,message);
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
ScriptInputFn original_captain_input;
using CaptainAssetFn=void(SK_THISCALL*)(void*,const editor::ResourceKey*);
CaptainAssetFn original_captain_asset;
using CrewAddFn=int32_t(SK_THISCALL*)(void*,const editor::ResourceKey*);
using CrewReplaceFn=void(SK_THISCALL*)(void*,int32_t,const editor::ResourceKey*);
using CrewIndexFn=void(SK_THISCALL*)(void*,int32_t);
using CrewLockFn=void(SK_THISCALL*)(void*,int32_t,bool);
CrewAddFn original_crew_add;CrewReplaceFn original_crew_replace;
CrewIndexFn original_crew_remove,original_crew_capacity;CrewLockFn original_crew_lock;
bool crew_gate(void* self,CrewAction action,int32_t index,const editor::ResourceKey& key={},bool value=false){
    if(applying || !active())return true;
    if(self!=data(mode()) || !tracks_authoring() || !C.settings_gate)return false;
    if(C.settings_gate(kCrewObject))return true;
    const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    const auto revision=C.object_revision?C.object_revision(kCrewObject):std::nullopt;
    if(valid_scope(scope) && revision)held_crew_action=HeldCrewAction{scope,generation,*revision,action,index,key,value,H->time_seconds()};
    return false;
}
int32_t SK_THISCALL crew_add(void* self,const editor::ResourceKey* key){
    return key && crew_gate(self,CrewAction::Add,-1,*key)?original_crew_add(self,key):-1;
}
void SK_THISCALL crew_replace(void* self,int32_t index,const editor::ResourceKey* key){
    if(key && crew_gate(self,CrewAction::Replace,index,*key))original_crew_replace(self,index,key);
}
void SK_THISCALL crew_remove(void* self,int32_t index){if(crew_gate(self,CrewAction::Remove,index))original_crew_remove(self,index);}
void SK_THISCALL crew_capacity(void* self,int32_t count){if(count>=0 && count<=3 && crew_gate(self,CrewAction::Capacity,count))original_crew_capacity(self,count);}
void SK_THISCALL crew_lock(void* self,int32_t index,bool value){if(crew_gate(self,CrewAction::Lock,index,{},value))original_crew_lock(self,index,value);}
void run_crew_action(CrewAction action,int32_t index,const editor::ResourceKey& key,bool value){
    void* owner=data(mode());
    switch(action){
    case CrewAction::Add:original_crew_add(owner,&key);break;
    case CrewAction::Replace:original_crew_replace(owner,index,&key);break;
    case CrewAction::Remove:original_crew_remove(owner,index);break;
    case CrewAction::Lock:original_crew_lock(owner,index,value);break;
    case CrewAction::Capacity:original_crew_capacity(owner,index);break;
    }
    const auto refresh=H->sym?H->sym("SporeCoop::Editor::RefreshDisplay"):0;
    void* display=skg::readable(mode(),0x18)?skg::field<void*>(mode(),0x14):nullptr;
    if(refresh && display)skg::thiscall<void>(refresh,display);
}
bool SK_THISCALL captain_input(void* self,void* window,void* message){
    const auto type=skg::readable(message,0x10)?skg::field<uint32_t>(message,8):0;
    const auto control=skg::readable(message,0x10)?skg::field<uint32_t>(message,0xC):0;
    const bool mutation=(type==0x287259F6 && control==0x0770B540) ||
        (control==0x0742CD10 && (type==0x07A44749 || type==0x07A4489C || type==0xEF00A884));
    if(mutation && !applying && active() && (!tracks_authoring() || !C.settings_gate || !C.settings_gate(kCaptainObject)))return false;
    return original_captain_input(self,window,message);
}
void SK_THISCALL captain_asset(void* self,const editor::ResourceKey* key){
    editor_native::state();
    if(!applying && active()){
        if(!tracks_authoring() || self!=data(mode()) || !key || !key->instance || !key->type || !C.settings_gate)return;
        if(!C.settings_gate(kCaptainObject)){
            // The asynchronous picker may outlive its original lease. Retain
            // only the selected value; never retain its UI callback payload.
            const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
            const auto revision=C.object_revision?C.object_revision(kCaptainObject):std::nullopt;
            if(valid_scope(scope) && revision)held_captain_asset=HeldCaptainAsset{scope,generation,*revision,*key,H->time_seconds()};
            return;
        }
    }
    original_captain_asset(self,key);
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
    if(observe && C.gesture_released)C.gesture_released();
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
    auto before_metadata=observe?std::exchange(gesture_before_metadata,std::nullopt):std::optional<Snapshot::Metadata>{};
    std::string metadata_error=observe?std::exchange(gesture_metadata_error,{}):std::string{};
    original_commit(self);
    for(auto actor:gesture_actors)release(actor.object);
    gesture_actors.clear();
    if (!entry) {release(before);return;}
    std::optional<Snapshot::Metadata> after_metadata;Snapshot::Metadata metadata;std::string after_error;
    if(capture_metadata(self,metadata,after_error))after_metadata=std::move(metadata);
    else if(metadata_error.empty())metadata_error=std::move(after_error);
    void* speech=input_speech && a_speech_ui?*reinterpret_cast<void**>(a_speech_ui):nullptr;
    if(before && speech && !pending.empty()){
        auto& previous=pending.back();
        if(previous.speech==speech && previous.owner==self && previous.generation==generation && previous.scope==gesture_scope_pin){
            // Native speech commits every character. Retain the first owned
            // before and the latest owned after until this dialog closes.
            release(previous.entry);release(before);previous.entry=entry;previous.after_bindings=bindings;
            previous.after_metadata=std::move(after_metadata);
            if(previous.metadata_error.empty())previous.metadata_error=std::move(metadata_error);
            return;
        }
    }
    if (!before || pending.size() >= 8) {
        release(entry);release(before); ++rejected;
        if (C.failure) C.failure(before?"Native editor gestures exceeded the pending transaction limit":"Native editor before snapshot allocation failed");
        return;
    }
    uint32_t act = skg::cdecl_call<uint32_t>(a_act);
    pending.push_back({entry,before,self,generation,act,gesture_clone,gesture_before_bindings,bindings,gesture_scope_pin,speech,
        std::move(before_metadata),std::move(after_metadata),std::move(metadata_error)});
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
    std::string complexity_error;
    if(!update_complexity(complexity_error)){if(C.failure)C.failure(complexity_error);return;}
    if(fenced_generation!=generation){restore_fences();fenced_generation=generation;}
    if(!supported || !active()){restore_fences();return;}
    // Unsupported authoring remains held. Save/Done/Cancel/Play are intercepted
    // by the native command adapter and retain their ordinary toolbar locations.
    constexpr uint32_t ids[]={0x07104132,0x04766FF0,0x272EB68E};
    void* display=skg::readable(mode(),0x18)?skg::field<void*>(mode(),0x14):nullptr;
    void* main_layout=skg::readable(display,0x28)?skg::field<void*>(display,0x24):nullptr;
    if(skg::readable(main_layout,0x18))for(auto id:ids){
        if(id==0x07104132 && terrain_available && tracks_authoring())continue;
        // Global trees retain cached browser/editor duplicates of these IDs.
        disable_owned(skg::thiscall<sui::Win>(a_layout_find,main_layout,id,true));
    }
    if(!applying && skg::readable(main_layout,0x18) && C.history_available){
        // The shared history also contains private roster gestures and recovered
        // operations without a native cursor entry. Refresh the owned toolbar
        // from that same availability used by the native command getters.
        for(auto [id,redo]:{std::pair{0xB006EF6Eu,false},std::pair{0xF006EFA5u,true}})
            if(auto button=skg::thiscall<sui::Win>(a_layout_find,main_layout,id,true))
                sui::set_enabled(button,!cancellation && C.history_available(redo));
    }
    if(!tracks_authoring() && skg::readable(main_layout,0x18))for(uint32_t id:{0x07466E50u,0x07108388u})
        disable_owned(skg::thiscall<sui::Win>(a_layout_find,main_layout,id,true));
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
        else if(root && inspector_shown(settings) && C.class_gate)C.class_gate(skg::field<uint32_t>(settings,0x20));
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

bool capture_scene_owner(SceneOwner& owner,std::string& error){
    const auto native=state();
    void* current_mode=mode();void* current_data=data(current_mode);void* current_resource=resource(current_data);
    if(!native.editing || !native.ready || native.playtesting || native.gesture || applying ||
       !skg::readable(current_resource,kResourceSize)){
        error="The native Adventure Editor cannot pin its current scene owner";return false;
    }
    const SceneOwner captured{uintptr_t(current_mode),uintptr_t(current_data),uintptr_t(current_resource),skg::field<editor::ResourceKey>(current_resource,8)};
    if(state().generation!=native.generation){error="The native Adventure Editor changed while pinning its scene owner";return false;}
    owner=captured;error.clear();return true;
}
SceneResumeState creation_scene_state(const SceneOwner& owner){
    const auto native=state();SceneResumeState result;
    result.generation=native.generation;result.world_visible=native.world_visible;
    result.ready=native.editing&&native.ready&&!native.playtesting&&!native.gesture&&!applying;
    void* current_mode=mode();void* current_data=data(current_mode);void* current_resource=resource(current_data);
    // Compare opaque identities before reading only the freshly resolved owner.
    result.owner=owner.mode && owner.data && owner.resource && uintptr_t(current_mode)==owner.mode &&
        uintptr_t(current_data)==owner.data && uintptr_t(current_resource)==owner.resource &&
        skg::readable(current_resource,kResourceSize) && skg::field<editor::ResourceKey>(current_resource,8)==owner.source;
    if(game::mode_id()!=modes::kScenario || !skg::readable(current_mode,0xDD) ||
       !a_scenario_initialization || !skg::readable(reinterpret_cast<void*>(a_scenario_initialization),4) || !a_get_simulator_mode_manager)return result;
    void* simulator_modes=skg::cdecl_call<void*>(a_get_simulator_mode_manager);
    if(!skg::readable(simulator_modes,0x29))return result;
    result.available=true;
    result.initialization=*reinterpret_cast<const uint32_t*>(a_scenario_initialization);
    result.enter_pending=skg::field<bool>(current_mode,0xDC);
    result.transitioning=skg::field<bool>(simulator_modes,0x28);
    return result;
}
bool resume_creation_scene_camera(const GestureScope& scope,uint64_t serial,uint64_t expected_generation,const SceneOwner& owner,std::string& error){
    // Ordinary Scenario resume reaches this paired activation through
    // EF0990 -> EFF670 -> EFDDC0. Require its consumed OnEnter state before
    // this idempotent fallback; camera activation cannot substitute for resume.
    // Native edit-world setup calls B5CEF0 with EBB801: this switches the App
    // camera AND updates Simulator's cached orbit interface. A bare App switch
    // leaves that second owner stale. The complete setup also focuses captain,
    // so use only this verified paired boundary and preserve the caller's view.
    const auto owned=[&]{
        const auto native=creation_scene_state(owner);
        return valid_scope(scope) && serial && expected_generation && active() &&
            native.settled() &&
            native.generation==expected_generation && C.gesture_scope && C.gesture_scope()==scope &&
            C.creation_return_current && C.creation_return_current(scope,serial);
    };
    if(!owned()){error="The creation return no longer owns its ready Adventure Editor camera";return false;}
    if(!a_get_simulator_system || !a_get_simulator_camera_state || !a_activate_simulator_camera || !a_view_manager){
        error="The verified creation-return camera adapter is unavailable";return false;
    }
    void* manager=skg::cdecl_call<void*>(a_view_manager);
    if(!view_slot(manager,0x50/4)){error="The native game mode camera manager is unavailable";return false;}
    void* cameras=skg::vcall<void*>(manager,0x50/4);
    if(!view_slot(cameras,0x34/4) || !view_slot(cameras,0x38/4) || !view_slot(cameras,0x40/4)){
        error="The native creation-return camera registry is unavailable";return false;
    }
    void* named=skg::vcall<void*>(cameras,0x40/4,uint32_t(0x00EBB801));
    if(!view_slot(named,0x0C/4)){error="The registered Adventure Editor camera is unavailable";return false;}
    void* orbit=skg::vcall<void*>(named,0x0C/4,uint32_t(0x011966ED));
    if(!skg::readable(orbit,0x318)){error="The registered Adventure Editor orbit camera is unavailable";return false;}
    void* simulator=skg::cdecl_call<void*>(a_get_simulator_system);
    void* camera_state=skg::cdecl_call<void*>(a_get_simulator_camera_state);
    if(!skg::readable(simulator,0x10) || !skg::readable(camera_state,0x10) ||
       skg::field<void*>(camera_state,0)!=simulator || !owned()){
        error="The creation return's native scene changed before camera activation";return false;
    }
    if(!skg::thiscall<bool>(a_activate_simulator_camera,simulator,uint32_t(0x00EBB801))){
        error="The native creation return could not activate its Adventure Editor camera";return false;
    }
    if(!owned() || skg::cdecl_call<void*>(a_get_simulator_system)!=simulator ||
       skg::cdecl_call<void*>(a_get_simulator_camera_state)!=camera_state ||
       skg::field<void*>(camera_state,0)!=simulator || skg::field<void*>(camera_state,0xC)!=orbit ||
       skg::cdecl_call<void*>(a_view_manager)!=manager ||
       skg::vcall<void*>(manager,0x50/4)!=cameras ||
       skg::vcall<void*>(cameras,0x40/4,uint32_t(0x00EBB801))!=named ||
       skg::vcall<void*>(cameras,0x38/4)!=named || view_camera(error)!=orbit){
        if(error.empty())error="The creation return's Adventure Editor camera did not become active";
        return false;
    }
    error.clear();return true;
}

bool notify_metadata_caption(const SceneOwner& owner,uint64_t expected_generation,const GestureScope& scope,
                             const Snapshot::Metadata& metadata,std::string& error){
    const auto pinned_scope=scope;
    const auto current_owner=[&]()->void*{
        const auto native=state();void* current_mode=mode();void* current_data=data(current_mode);
        void* current_resource=resource(current_data);
        if(!expected_generation || native.generation!=expected_generation || !native.editing || native.playtesting || native.gesture || applying ||
           !owner.mode || !owner.data || !owner.resource || uintptr_t(current_mode)!=owner.mode || uintptr_t(current_data)!=owner.data ||
           uintptr_t(current_resource)!=owner.resource || !skg::readable(current_resource,kResourceSize) ||
           skg::field<editor::ResourceKey>(current_resource,8)!=owner.source || (C.gesture_scope && C.gesture_scope()!=pinned_scope))return nullptr;
        return current_data;
    };
    auto* current=current_owner();Snapshot::Metadata actual;
    if(!current || !capture_metadata(current,actual,error) || actual!=metadata){
        if(error.empty())error="The restored adventure metadata no longer owns its native caption";return false;
    }
    if(!a_metadata_message_manager || !a_metadata_message_send){error="The verified native name notification is unavailable";return false;}
    auto* manager=skg::cdecl_call<void*>(a_metadata_message_manager);
    if(!view_slot(manager,0x14/4) || uintptr_t(skg::vtable(manager)[0x14/4])!=a_metadata_message_send){
        error="The verified synchronous native name notification is unavailable";return false;
    }
    const Snapshot::Dirty dirty{skg::field<uint32_t>(current,0x80),skg::field<bool>(current,0x84),skg::field<bool>(current,0x85)};
    // F3CC00 sends this stack packet through MessageSend (+14), not MessagePost
    // (+18). Both reviewed listeners consume nameable+0 and UTF16 text+8 inline;
    // no pointer survives the call. Avoid SetName, which skips equal names and
    // changes the separate native imported flag even for a caption refresh.
    std::array<uint8_t,20> packet{};
    skg::field<void*>(packet.data(),0)=skg::ptr_add<void>(current,4);
    skg::field<const char16_t*>(packet.data(),8)=metadata.text[0].c_str();
    skg::thiscall<void>(a_metadata_message_send,manager,uint32_t(0x7AA519DC),packet.data(),static_cast<void*>(nullptr));
    current=current_owner();if(!current){error="The native adventure scene changed during its name notification";return false;}
    skg::field<uint32_t>(current,0x80)=dirty.saved_revision;skg::field<bool>(current,0x84)=dirty.dirty;skg::field<bool>(current,0x85)=dirty.imported;
    if(!capture_metadata(current,actual,error) || actual!=metadata){
        if(error.empty())error="The restored adventure metadata changed during its name notification";return false;
    }
    error.clear();return true;
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
void SK_CDECL drain_local_view(void*){
    local_view_queued=false;if(!deferred_local_view)return;
    const auto item=*deferred_local_view;
    const auto native=state();
    const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    if(item.generation!=native.generation || item.mode!=mode() || item.owner!=data(mode()) || !editing() ||
       (valid_scope(item.scope) && item.scope!=scope)){
        deferred_local_view.reset();return;
    }
    if(!terrain_settled() || native.gesture || applying)return;
    local_view_settling=true;std::string error;
    const bool okay=restore_view(item.view,item.generation,error);
    local_view_settling=false;deferred_local_view.reset();
    if(!okay && C.failure)C.failure(error);
}

bool init(const SkHost* host, SkMod* mod, Ctx context) {
    script_events=0;
    input_speech=false;
    H = host; M = mod; C = std::move(context);
    if (!H || !skg::supported_gog2024(H->exe_sha256())) return false;
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
    a_preview_factory=H->sym("Simulator::cScenarioData::CreateObjectInstance");
    a_marker_ctor=H->sym("SporeCoop::Editor::MarkerCtor");a_marker_dtor=H->sym("SporeCoop::Editor::MarkerDtor");
    a_resize_marker_acts=H->sym("SporeCoop::Editor::ResizeMarkerActs");
    a_object_reference=H->sym("SporeCoop::Adventure::AssignObjectReference");
    a_inspector_open=H->sym("SporeCoop::Editor::OpenClassInspector");
    a_layout_shown=H->sym("SporeCoop::Editor::LayoutShown");
    a_copy_acts=H->sym("SporeCoop::Editor::CopyActs");a_copy_act=H->sym("SporeCoop::Editor::CopyAct");
    a_copy_crew=H->sym("SporeCoop::Editor::CopyCrewRows");
    a_terrain_load=H->sym("SporeCoop::Editor::TerraformLoadProperties");
    a_copy_terrain_properties=H->sym("SporeCoop::Editor::CopyTerrainProperties");
    a_terrain_rebuild=H->sym("SporeCoop::Editor::TerraformRebuild");
    a_terrain_selection=H->sym("SporeCoop::Editor::TerraformSelection");
    a_terrain_loaded=H->sym("SporeCoop::Editor::TerrainLoaded");
    a_terrain_vtable=H->sym("SporeCoop::Editor::TerrainVTable");
    a_terraform_ui_vtable=H->sym("SporeCoop::Editor::TerraformUIVTable");
    a_flora_ui_vtable=H->sym("SporeCoop::Editor::FloraUIVTable");
    a_copy_class_acts=H->sym("SporeCoop::Editor::CopyClassActs");a_copy_marker_acts=H->sym("SporeCoop::Editor::CopyMarkerActs");
    a_insert_act=H->sym("SporeCoop::Editor::InsertAct");a_insert_class_act=H->sym("SporeCoop::Editor::InsertClassAct");a_insert_marker_act=H->sym("SporeCoop::Editor::InsertMarkerAct");
    a_add_act=H->sym("SporeCoop::Editor::AddAct");a_delete_act=H->sym("SporeCoop::Editor::DeleteAct");
    a_select_act=H->sym("SporeCoop::Editor::SelectAct");
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
    a_get_simulator_system=H->sym("SporeCoop::Editor::GetSimulatorSystem");
    a_get_simulator_camera_state=H->sym("SporeCoop::Editor::GetSimulatorCameraState");
    a_activate_simulator_camera=H->sym("SporeCoop::Editor::ActivateSimulatorCamera");
    a_scenario_initialization=H->sym("SporeCoop::Editor::ScenarioInitializationState");
    a_get_simulator_mode_manager=H->sym("SporeCoop::Editor::GetSimulatorModeManager");
    a_metadata_message_manager=H->sym("App::IMessageManager::Get");
    a_metadata_message_send=H->sym("SporeCoop::Editor::SendMetadataNameChanged");
    a_view_target=H->sym("SporeCoop::Editor::CurrentOrbitTarget");
    a_view_orbit=H->sym("SporeCoop::Editor::CurrentOrbitScalars");
    a_view_set_orbit=H->sym("SporeCoop::Editor::SetOrbitScalarsInstant");
    a_view_lookat=H->sym("SporeCoop::SimCamera_LookAt");
    supported = a_copy_acts && a_copy_act && a_select_act && a_inspector_open && a_layout_shown && a_preview_factory && a_mode && a_history && a_act && a_ctor && a_read && a_write && a_copy && a_entry && a_entry_vtable && a_restore && a_alloc &&
        a_context && a_tools && a_marker && a_object && a_clone_marker && a_create_marker && a_delete_marker && a_apply_marker && a_loading && a_layout_find && a_erase_history && a_discard_history && a_copy_marker_rows && a_insert_marker && a_copy_class_rows && a_copy_class && a_insert_class && a_speech_ui && a_behavior_data;
    if (!supported) return false;
    supported = install("SporeCoop::Editor::StartHistory", (void*)start, (void**)&original_start) &&
        install("SporeCoop::Editor::SelectAct",(void*)select_act_native,(void**)&original_select_act) &&
        install("SporeCoop::Editor::CaptainInput",(void*)captain_input,(void**)&original_captain_input) &&
        install("SporeCoop::Editor::SetCaptainAsset",(void*)captain_asset,(void**)&original_captain_asset) &&
        install("SporeCoop::Editor::AddCrew",(void*)crew_add,(void**)&original_crew_add) &&
        install("SporeCoop::Editor::ReplaceCrew",(void*)crew_replace,(void**)&original_crew_replace) &&
        install("SporeCoop::Editor::RemoveCrew",(void*)crew_remove,(void**)&original_crew_remove) &&
        install("SporeCoop::Editor::LockCrew",(void*)crew_lock,(void**)&original_crew_lock) &&
        install("SporeCoop::Editor::SetCrewCapacity",(void*)crew_capacity,(void**)&original_crew_capacity) &&
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
    complexity_available=supported && complexity_bounds.initialize(H->sym("SporeCoop::EditorComplexity::CreateMarkerLimit"),H->sym("SporeCoop::EditorComplexity::CloneMarkerLimit")) &&
        install("SporeCoop::EditorComplexity::WeightedRatio",(void*)complexity_ratio,(void**)&original_complexity);
    terrain_available=supported && a_terrain_load && a_copy_terrain_properties && a_terrain_rebuild && a_terrain_selection && a_terrain_loaded && a_terrain_vtable && a_terraform_ui_vtable && a_flora_ui_vtable &&
        install("SporeCoop::Editor::TerraformInitializeResource",(void*)terrain_resource_init,(void**)&original_terrain_resource_init) &&
        install("SporeCoop::Editor::TerraformActivate",(void*)terrain_activate,(void**)&original_terrain_activate) &&
        install("SporeCoop::Editor::TerraformStartHistory",(void*)terrain_start,(void**)&original_terrain_start) &&
        install("SporeCoop::Editor::TerraformCommitHistory",(void*)terrain_commit,(void**)&original_terrain_commit) &&
        install("SporeCoop::Editor::TerraformDiscardHistory",(void*)terrain_discard,(void**)&original_terrain_discard) &&
        install("SporeCoop::Editor::TerraformUndoHistoryEntry",(void*)terrain_undo,(void**)&original_terrain_undo) &&
        install("SporeCoop::Editor::TerraformLoadProperties",(void*)terrain_load,(void**)&original_terrain_load) &&
        install("SporeCoop::Editor::TerraformBuildProperties",(void*)terrain_build,(void**)&original_terrain_build) &&
        install("SporeCoop::Editor::TerraformRebuild",(void*)terrain_rebuild,(void**)&original_terrain_rebuild) &&
        install("SporeCoop::Editor::TerraformRemoveObject",(void*)terrain_remove,(void**)&original_terrain_remove) &&
        install("SporeCoop::Editor::TerraformUpdateObject",(void*)terrain_update,(void**)&original_terrain_update) &&
        install("SporeCoop::Editor::TerraformAppendModel",(void*)terrain_append_model,(void**)&original_terrain_append_model) &&
        install("SporeCoop::Editor::TerraformAppendEffect",(void*)terrain_append_effect,(void**)&original_terrain_append_effect) &&
        install("SporeCoop::Editor::TerraformSetGradient",(void*)terrain_gradient,(void**)&original_terrain_gradient) &&
        install("SporeCoop::Editor::FloraRemove",(void*)flora_remove,(void**)&original_flora_remove) &&
        install("SporeCoop::Editor::FloraAdd",(void*)flora_add,(void**)&original_flora_add) &&
        install("SporeCoop::Editor::FloraInput",(void*)flora_input,(void**)&original_flora_input);
    const std::array<const char*,18> names{{"TerraformSetWaterColor","TerraformSetAtmosphereColor","TerraformSetBeachColor","TerraformSetCliffColor",
        "TerraformSetTemperature","TerraformSetAtmosphere","TerraformSetGroundType","TerraformSetDeadType","TerraformSetOverride","TerraformSetHazards",
        "TerraformSetWaterLevel","TerraformSetWaterType","TerraformSetAtmosphereType","TerraformSetGroundEffect","TerraformRefreshColors",
        "TerraformSetTime","TerraformSetDayNight","TerraformSetVisualStyle"}};
    const std::array<TerrainValueFn,18> values{{terrain_value<0>,terrain_value<1>,terrain_value<2>,terrain_value<3>,terrain_value<4>,terrain_value<5>,terrain_value<6>,terrain_value<7>,terrain_value<8>,terrain_value<9>,
        terrain_value<10>,terrain_value<11>,terrain_value<12>,terrain_value<13>,terrain_value<14>,terrain_value<15>,terrain_value<16>,terrain_value<17>}};
    for(size_t i=0;i<values.size() && terrain_available;++i){
        const auto name=std::string("SporeCoop::Editor::")+names[i];
        terrain_available=install(name.c_str(),(void*)values[i],(void**)&original_terrain_values[i]);
    }
    return supported;
}

void shutdown() {
    class_projection_cache.clear();
    std::string bounds_error;
    if(!complexity_bounds.set(false,bounds_error) && H && H->log)H->log(M,SK_ERROR,"%s",bounds_error.c_str());
    complexity_available=false;
    update_previews({});
    for(auto& [peer,actor]:preview_actors){(void)peer;clear_preview_actor(actor);}preview_actors.clear();
    ++generation;
    input_speech=false;
    terrain_before.reset();terrain_pending=terrain_queued=terrain_capturing=terrain_rebuilding=terrain_recipe_replacement=false;terrain_grounding.clear();materialized_terrain.reset();terrain_available=false;
    held_tool.reset();held_creation.reset();held_class_removal.reset();held_captain_asset.reset();held_crew_action.reset();held_terrain_controls.clear();held_terrain_preset.reset();input_queued=false;
    deferred_local_view.reset();local_view_queued=local_view_settling=false;
    held_import.reset();
    cancellation.reset();cancellation_queued=cancellation_settling=false;
    gesture_owner=gesture_entry=nullptr;gesture_generation=gesture_serial=0;gesture_scope_pin={};
    for (auto item : pending){release(item.entry);release(item.before);}
    release(gesture_before_resource);gesture_before_resource=nullptr;
    gesture_before_metadata.reset();gesture_metadata_error.clear();
    pending.clear();
    for(auto actor:gesture_actors)release(actor.object);
    gesture_actors.clear();gesture_before_bindings.clear();
    restore_fences();bindings.clear(); C = {}; supported = false;
}

void tick(){
    if(deferred_local_view && !local_view_queued){local_view_queued=true;H->run_outside_frame(drain_local_view,nullptr);}
    if(!H)return;
    if(terrain_pending && !terrain_queued){terrain_queued=true;H->run_outside_frame(drain_terrain,nullptr);}
    if(!pending.empty() && !queued){queued=true;H->run_outside_frame(drain,nullptr);}
    if((held_tool || held_import || held_creation || held_class_removal || held_captain_asset || held_crew_action || held_terrain_preset || !held_terrain_controls.empty()) && !input_queued){input_queued=true;H->run_outside_frame(continue_input,nullptr);}
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
        class_projection_cache.clear();
        observed_mode = m; observed_data = d; observed_scenario=scenario; ++generation;
        bindings.clear();incarnations.clear();selection.clear();act_identity={};
        terrain_before.reset();terrain_pending=terrain_capturing=terrain_rebuilding=terrain_recipe_replacement=false;terrain_grounding.clear();materialized_terrain.reset();
        deferred_local_view.reset();held_terrain_controls.clear();held_terrain_preset.reset();held_tool.reset();
        gesture_before_metadata.reset();gesture_metadata_error.clear();
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
    s.world_visible=a_layout_shown && skg::readable(layout,0x18) && skg::thiscall<bool>(a_layout_shown,layout);
    // data124 is invalidated by native thumbnail generation even in a settled
    // editor and preview. Require the successful native shell load instead.
    s.terraforming=display_valid && skg::field<int>(display,0x1C)==2;
    const bool terrain_ready=terrain_settled();if(terrain_ready)terrain_rebuilding=false;
    s.ready=!ordinary_capture_pending() && (!cancellation || cancellation_settling) && (!deferred_local_view || local_view_settling) && terrain_ready && s.editing && display_valid &&
        (skg::field<int>(display,0x1C)==0 || (s.terraforming && terrain_available)) && skg::readable(container,0x8C) &&
        skg::readable(loading,0x371) && !skg::field<bool>(loading,0x370);
    s.playtesting=s.entry==0 && skg::field<int>(m,0xCC)==2;
    void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    if(s.editing && skg::readable(context,4))s.act=skg::field<uint32_t>(context,0);
    s.selected=selection;
    s.gesture=!*reinterpret_cast<uint8_t*>(a_history+0x20) || !pending.empty() || (active() && speech_pending()) || (terrain_pending && !terrain_capturing);
    s.history_cursor=*reinterpret_cast<uint32_t*>(a_history+0x14);
    uintptr_t begin=*reinterpret_cast<uintptr_t*>(a_history),end=*reinterpret_cast<uintptr_t*>(a_history+4);
    if(end>=begin && (end-begin)%4==0) s.history_size=uint32_t((end-begin)/4);
    return s;
}
bool terraform_navigation_allowed(){
    const auto current=state();return terrain_available && active() && current.ready && !current.gesture &&
        C.navigation_allowed && C.navigation_allowed() && C.terrain_gate && C.terrain_gate({},{});
}
bool extended_complexity_available(){return supported && complexity_available;}

void bind(const std::vector<Binding>& value) {
    bindings.clear();
    for (const auto& b : value){bindings[b.marker]=b.object;incarnations[b.marker]=std::max(incarnations[b.marker],1u);}
    selection.clear();
}
void bind_acts(const editor::Document& document){
    act_identity={};act_identity.act_order=document.act_order;
    act_identity.project=document.project;act_identity.epoch=document.epoch;
    act_identity.next_id=document.next_id;
    act_identity.next_terrain_order=document.next_terrain_order;
    act_identity.terrain=document.terrain;
    for(const auto& [id,row]:document.settings)if(row.kind==editor::SettingsKind::Act){
        auto& bound=act_identity.settings[id];bound.object=id;bound.act_id=row.act_id;
    }else if(row.kind==editor::SettingsKind::Project || row.kind==editor::SettingsKind::Planet)act_identity.settings[id]=row;
}
bool edit_acts(int action,const editor::Document& document,std::string& error){
    auto native=state();
    if(!active() || !native.ready || native.gesture || action<0 || action>3 || document.act_order.empty() || native.act>=document.act_order.size() ||
       !a_copy_class_acts || !a_copy_marker_acts || !a_insert_act || !a_insert_class_act || !a_insert_marker_act || !a_add_act || !a_delete_act){error="The native act roster is unavailable";return false;}
    const auto count=document.act_order.size();const auto index=native.act;
    if((action==0 && count>=8) || (action==1 && count<=1) || (action==2 && !index) || (action==3 && index+1>=count)){error="This act cannot move beyond the adventure limits";return false;}
    Gesture gesture;gesture.generation=native.generation;
    if(!capture(gesture.before,error))return false;
    Owned copy(parse(gesture.before.bytes,error));if(!copy.p)return false;
    auto identity=document;auto ordered=editor::act_roster(document);
    if(action<=1){
        // F448E0/F440B0 access only ScenarioData+10. They mutate the owned
        // resource and deep-copy all class-act and marker-act companions.
        std::array<uint8_t,0x14> owner{};skg::field<void*>(owner.data(),0x10)=copy.p;
        if(action==0){
            if(document.next_id>=UINT64_MAX-2){error="The act identity allocator is exhausted";return false;}
            skg::thiscall<void>(a_add_act,owner.data(),index+1);
            editor::AuthoringSettings row;row.act_id={document.next_id,1};row.object={document.next_id+1,1};
            ordered.insert(ordered.begin()+index+1,std::move(row));
        }else {skg::thiscall<void>(a_delete_act,owner.data(),index);ordered.erase(ordered.begin()+index);}
    }else{
        std::vector<uint32_t> order;for(uint32_t i=0;i<count;++i)order.push_back(i);
        const auto other=action==2?index-1:index+1;std::swap(order[index],order[other]);std::swap(ordered[index],ordered[other]);
        Owned source(parse(gesture.before.bytes,error));if(!source.p || !permute_acts(copy.p,source.p,order,error))return false;
    }
    identity.act_order.clear();identity.settings.clear();
    for(const auto& row:ordered){identity.act_order.push_back(row.act_id);identity.settings.emplace(row.object,row);}
    std::map<uint32_t,editor::ObjectId> supplied;for(const auto& b:gesture.before.bindings)supplied.emplace(b.marker,b.object);
    if(!capture_resource(copy.p,std::min<uint32_t>(index,uint32_t(ordered.size()-1)),gesture.after,error,&supplied,&identity))return false;
    if(C.gesture)C.gesture(std::move(gesture));
    return true;
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
    Snapshot::Metadata metadata;if(!capture_metadata(owner,metadata,error))return false;
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
    if(!serialize(copy.p,draft.bytes,error))return false;
    const auto identity=draft.document;std::map<uint32_t,editor::ObjectId> supplied;for(const auto& b:draft.bindings)supplied.emplace(b.marker,b.object);
    if(!projection(copy.p,draft,error,&supplied,std::nullopt,std::nullopt,std::nullopt,std::nullopt,&identity,&draft.bytes,terrain_recipe_replacement&&terrain_capturing) ||
       !validate(draft,draft.document,error))return false;
    remember_terrain(draft.document);out=std::move(draft);return true;
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
    if(!projection(copy.p,expanded,error,&supplied,relative,true,true,true,&input.document,&input.bytes))return false;
    if(!hydrate_native_captain(copy.p,expanded,error))return false;
    out=std::move(expanded);return true;
}
bool canonicalize_cast_state(editor::State& state,std::string& error){
    // Recovery may predate machine-hint normalization. Upgrade every retained
    // precondition together, without changing revisions or retiring history.
    struct Normalized {editor::StructuredValue block;editor::ResourceKey primary;std::vector<editor::ObjectId> legacy_references;};
    std::map<std::vector<uint8_t>,Normalized> cache;
    auto value=[&](editor::Value& v,editor::ResourceKey* primary=nullptr){
        auto* block=std::get_if<editor::StructuredValue>(&v);
        if(!block || block->format!=kCastBlockFormat){error="Invalid recovery cast block";return false;}
        auto found=cache.find(block->bytes);
        if(found==cache.end()){
            Owned owned(parse(block->bytes,error));Rows classes;
            if(!owned.p || !rows(owned.p,0x2BF4,0x27E8,1,classes) || classes.end-classes.begin!=0x27E8){error="Invalid recovery cast fragment";return false;}
            Normalized normalized;
            if(!class_primary_asset(classes.begin+8,normalized.primary,error) || !class_block(classes.begin,normalized.block,error,&normalized.legacy_references))return false;
            found=cache.emplace(block->bytes,std::move(normalized)).first;
        }
        if(found->second.block.references!=block->references && found->second.legacy_references!=block->references){error="Recovery cast normalization changed its references";return false;}
        if(primary)*primary=found->second.primary;
        *block=found->second.block;return true;
    };
    auto settings=[&](editor::ClassActSettings& settings){
        auto found=settings.properties.find(kCastBlockProperty);
        if(found==settings.properties.end())return true;
        editor::ResourceKey primary;if(!value(found->second,&primary))return false;
        auto asset=settings.properties.find(editor::kClassAssetResourceProperty);
        if(asset==settings.properties.end()){
            if(!primary.instance || !primary.type){error="A legacy cast subtree has no primary asset";return false;}
            settings.properties[editor::kClassAssetResourceProperty]=primary;
            auto revision=settings.field_revisions.find({editor::Field::Property,kCastBlockProperty});
            if(revision!=settings.field_revisions.end())settings.field_revisions[{editor::Field::Property,editor::kClassAssetResourceProperty}]=revision->second;
        }else if(primary.instance && asset->second!=editor::Value{primary}){error="Recovery cast content disagrees with its primary resource property";return false;}
        return true;
    };
    auto patch=[&](editor::Patch& patch){
        for(auto& c:patch.classes)for(auto* side:{&c.before,&c.after})if(*side && !settings(**side))return false;
        std::vector<editor::FieldEdit> fields;
        for(auto f:patch.fields){
            if(f.field!=editor::FieldKey{editor::Field::Property,kCastBlockProperty}){fields.push_back(std::move(f));continue;}
            editor::ResourceKey before,after;
            if((f.before && !value(*f.before,&before)) || (f.after && !value(*f.after,&after)))return false;
            if(before.instance && after.instance && before!=after){
                auto content=f;content.field={editor::Field::Property,editor::kClassAssetResourceProperty};content.before=before;content.after=after;
                fields.push_back(std::move(content));
            }
            if(f.before!=f.after)fields.push_back(std::move(f));
        }
        patch.fields=std::move(fields);
        return true;
    };
    for(auto& [id,c]:state.document.class_settings){(void)id;if(!settings(c))return false;}
    for(auto& h:state.history)if(!patch(h.undo) || !patch(h.redo))return false;
    for(auto& [id,d]:state.decisions){(void)id;if(d.change && (!patch(d.change->patch) || !patch(d.change->inverse)))return false;}
    if(!state.document.class_schema.empty())state.document.class_schema=cast_property_schema();
    return true;
}
bool validate_entry(const std::vector<uint8_t>& bytes,uint32_t act,std::string& error) {
    if(!supported){error="Native editor validation is unavailable";return false;}
    Owned copy(parse(bytes,error));if(!copy.p)return false;
    Snapshot projected;projected.act=act;
    const editor::Document fresh;
    if(!projection(copy.p,projected,error,nullptr,std::nullopt,std::nullopt,std::nullopt,std::nullopt,&fresh))return false;
    if(!hydrate_native_captain(copy.p,projected,error))return false;
    projected.bytes=bytes;
    return validate(projected,projected.document,error);
}

bool supported_changes(const Gesture& gesture,std::string& error){
    if(!supported || !a_copy_marker_rows || !a_insert_marker){error="Native authored-change validation is unavailable";return false;}
    if(!valid_metadata(gesture.before.metadata,error) || !valid_metadata(gesture.after.metadata,error))return false;
    // Missing metadata is explicitly unknown for legacy recovery. New native
    // gestures always retain both exact boundary witnesses and cannot use it.
    if(gesture.before.metadata && gesture.after.metadata && gesture.before.metadata!=gesture.after.metadata){
        error="Shared editing rejected an unsupported adventure metadata change";return false;
    }
    bool tracked=false;
    if(gesture.before.document.schema!=gesture.after.document.schema ||
       !relative_scale_mode(gesture.after.document,tracked,error)){
        error="Native placement-property schema changed during a gesture";return false;
    }
    Owned before(parse(gesture.before.bytes,error));if(!before.p)return false;
    Owned after(parse(gesture.after.bytes,error));if(!after.p)return false;
    if(!captain_sidecar_matches(before.p,gesture.before,gesture.before.document,error) ||
       !captain_sidecar_matches(after.p,gesture.after,gesture.after.document,error))return false;
    const bool roster=gesture.before.document.act_order!=gesture.after.document.act_order;
    auto same_marker_act=[&](void* a,void* b){
        if(std::memcmp(a,b,0x1C) || skg::field<uint32_t>(a,0x30)!=skg::field<uint32_t>(b,0x30))return false;
        Rows left,right;
        return rows(a,0x1C,0xC,kPlacementLimit,left) && rows(b,0x1C,0xC,kPlacementLimit,right) &&
            left.end-left.begin==right.end-right.begin && (left.begin==left.end || !std::memcmp(left.begin,right.begin,size_t(left.end-left.begin)));
    };
    Owned compared_before(construct()),compared_after(construct()),empty(construct());
    if(!compared_before.p || !compared_after.p || !empty.p){error="Native authored-change allocation failed";return false;}
    skg::thiscall<void*>(a_copy,compared_before.p,before.p);
    skg::thiscall<void*>(a_copy,compared_after.p,after.p);
    if(!gesture.after.document.settings_schema.empty()){
        if(gesture.before.document.settings_schema!=gesture.after.document.settings_schema || gesture.after.document.settings_schema!=authoring_property_schema()){
            error="The native authoring settings schema changed during a gesture";return false;
        }
        // Act subtrees and captain primitives have independent typed ownership.
        // Normalize only these supported fields on private comparison copies.
        skg::thiscall<void*>(a_copy_acts,skg::ptr_add<void>(compared_after.p,0x23C),skg::ptr_add<void>(compared_before.p,0x23C));
        std::memcpy(skg::ptr_add<void>(compared_after.p,0x130),skg::ptr_add<void>(compared_before.p,0x130),0x46);
        if(gesture.after.document.settings.contains(kCrewObject)){
            if(!a_copy_crew){error="The verified crew assignment is unavailable";return false;}
            skg::thiscall<void*>(a_copy_crew,skg::ptr_add<void>(compared_after.p,0x178),skg::ptr_add<void>(compared_before.p,0x178));
            skg::field<int32_t>(compared_after.p,0x238)=skg::field<int32_t>(compared_before.p,0x238);
        }
    }
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
        if(roster){
            Rows old_rows,new_rows;
            if(!rows(normalized_old,0x28,0x34,8,old_rows) || !rows(normalized_new,0x28,0x34,8,new_rows) ||
               size_t((old_rows.end-old_rows.begin)/0x34)!=gesture.before.document.act_order.size() ||
               size_t((new_rows.end-new_rows.begin)/0x34)!=gesture.after.document.act_order.size()){error="Invalid native act path companions";return false;}
            for(size_t i=0;i<gesture.after.document.act_order.size();++i){
                auto found=std::find(gesture.before.document.act_order.begin(),gesture.before.document.act_order.end(),gesture.after.document.act_order[i]);
                if(found!=gesture.before.document.act_order.end() && !same_marker_act(old_rows.begin+(found-gesture.before.document.act_order.begin())*0x34,new_rows.begin+i*0x34)){
                    error="Reordering acts changed an unrelated patrol path";return false;
                }
            }
            skg::thiscall<void*>(a_copy_marker_acts,skg::ptr_add<void>(normalized_new,0x28),skg::ptr_add<void>(normalized_old,0x28));
        }
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
    if(gesture.after.document.settings.contains(editor_native_terrain::kPlanetObject) &&
       !editor_native_terrain::normalize_supported(right,left,error))return false;
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

ValidationScope::ValidationScope(){if(!validation_depth)validated_snapshot.reset();++validation_depth;}
ValidationScope::~ValidationScope(){if(validation_depth && !--validation_depth)validated_snapshot.reset();}
bool validate(const Snapshot& snapshot,const editor::Document& document,std::string& error){
    if(!supported){error="Native editor validation is unavailable";return false;}
    if(validation_depth && validated_snapshot && validated_snapshot->generation==generation &&
       validated_snapshot->act==snapshot.act && validated_snapshot->bindings==snapshot.bindings &&
       validated_snapshot->source==snapshot.source && validated_snapshot->captain==snapshot.captain &&
       validated_snapshot->captain_locked==snapshot.captain_locked && validated_snapshot->metadata==snapshot.metadata &&
       validated_snapshot->bytes==snapshot.bytes && validated_snapshot->document==document){
        ++validation_hits;error.clear();return true;
    }
    if(document.placements.size()>kPlacementLimit || snapshot.bindings.size()>kPlacementLimit){
        error="Native editor placement limit exceeded";return false;
    }
    bool tracked=false;if(!relative_scale_mode(document,tracked,error))return false;
    if(!valid_metadata(snapshot.metadata,error))return false;
    Owned copy(parse(snapshot.bytes,error));if(!copy.p)return false;
    if(!captain_sidecar_matches(copy.p,snapshot,document,error))return false;
    std::map<uint32_t,editor::ObjectId> supplied;
    std::set<editor::ObjectId> ids;
    for(const auto& b:snapshot.bindings){
        if(!b.object.id || !b.object.generation || !supplied.emplace(b.marker,b.object).second || !ids.insert(b.object).second){
            error="Native snapshot has duplicate or invalid placement bindings";return false;
        }
    }
    Snapshot projected;projected.act=snapshot.act;
    if(!projection(copy.p,projected,error,&supplied,tracked,document.schema.contains(kElevationProperty),!document.class_schema.empty(),!document.settings_schema.empty(),&document,&snapshot.bytes))return false;
    if(projected.document.terrain.size()!=document.terrain.size()){error="The native terrain recipe differs from its canonical roster";return false;}
    for(const auto& [id,row]:document.terrain){
        auto found=projected.document.terrain.find(id);
        if(found==projected.document.terrain.end() || found->second.kind!=row.kind || found->second.payload!=row.payload || found->second.order!=row.order){
            error="A native terrain recipe record differs from the committed document";return false;
        }
    }
    if(projected.document.settings_schema!=document.settings_schema || projected.document.settings.size()!=document.settings.size()){
        error="The native act/captain settings roster differs from the committed draft";return false;
    }
    for(const auto& [id,settings]:projected.document.settings){
        auto found=document.settings.find(id);
        if(found==document.settings.end() || settings.kind!=found->second.kind || settings.act_id!=found->second.act_id || settings.properties!=found->second.properties){
            error="Native act/captain settings differ from the committed draft";return false;
        }
    }
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
       (document.act_order.empty()?!document.acts.count({(3ull<<32)|snapshot.act,1}):snapshot.act>=document.act_order.size())){
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
    if(validation_depth && snapshot.bytes.size()+editor::bytes(document)<=(16u<<20)){
        // Caching is optional. A cache allocation failure cannot turn a fully
        // checked snapshot into a failure or retain a partially built key.
        try{validated_snapshot=ValidatedSnapshot{snapshot.bytes,snapshot.bindings,document,snapshot.source,snapshot.captain,snapshot.captain_locked,snapshot.metadata,generation,snapshot.act};}
        catch(const std::bad_alloc&){validated_snapshot.reset();}
    }
    error.clear();return true;
}

bool normalize_saved_grounding(Snapshot& snapshot,const editor::Document& document,std::string& error){
    Owned owned(parse(snapshot.bytes,error));if(!owned.p)return false;
    Snapshot result=snapshot;
    for(const auto& binding:snapshot.bindings){
        const auto expected=document.placements.find(binding.object);void* marker=marker_for_key(owned.p,binding.marker);
        if(expected==document.placements.end() || !marker){error="Saved terrain grounding lost its placement identity";return false;}
        const auto actual=skg::field<editor::Vec3>(marker,4),wanted=expected->second.position;
        if(actual==wanted)continue;
        const auto length=[](const editor::Vec3& p){return std::sqrt(double(p.x)*p.x+double(p.y)*p.y+double(p.z)*p.z);};
        const auto a=length(actual),b=length(wanted);
        if(!std::isfinite(a) || !std::isfinite(b) || a<0.001 || b<0.001 || a>1e7 || b>1e7 ||
           std::abs(actual.x/a-wanted.x/b)>2e-6 || std::abs(actual.y/a-wanted.y/b)>2e-6 || std::abs(actual.z/a-wanted.z/b)>2e-6){
            error="Native Save changed an authored placement direction";return false;
        }
        skg::field<editor::Vec3>(marker,4)=wanted;
    }
    if(!serialize(owned.p,result.bytes,error) || !validate(result,document,error))return false;
    snapshot=std::move(result);return true;
}

bool restore(const Snapshot& snapshot, std::string& error) {
    InspectorView inspector;
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
    SceneOwner metadata_owner;
    const auto metadata_scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    if(snapshot.metadata && !capture_scene_owner(metadata_owner,error))return false;
    const bool rebuild_terrain=snapshot.document.settings.contains(editor_native_terrain::kPlanetObject) &&
        !same_terrain(materialized_terrain?*materialized_terrain:act_identity,snapshot.document);
    if(rebuild_terrain && (!terrain_available || !skg::readable(terraform_mode(),0x1CC))){error="Native terrain regeneration is unavailable";return false;}
    if(!validate(snapshot,snapshot.document,error))return false;
    Owned copy(parse(snapshot.bytes,error));
    if (!copy.p) return false;
    // Camera state belongs to this editor. Native Undo's full copy includes it.
    // Preserve the native local resource camera before that copy is applied.
    void* live=resource(data(mode()));
    refresh_selection();
    auto selected=selection;
    uint32_t local_act=std::min<uint32_t>(s.act,uint32_t(snapshot.document.acts.size()-1));
    if(s.act<act_identity.act_order.size() && !snapshot.document.act_order.empty()){
        auto found=std::find(snapshot.document.act_order.begin(),snapshot.document.act_order.end(),act_identity.act_order[s.act]);
        if(found!=snapshot.document.act_order.end())local_act=uint32_t(found-snapshot.document.act_order.begin());
    }
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
    // RestoreHistorySnapshot rebuilds through the current native act. Clamp
    // before its copy when deletion removed the old last act.
    if(s.act>=snapshot.document.acts.size())original_select_act(uint32_t(snapshot.document.acts.size()-1));
    skg::thiscall<void>(a_restore,entry.p,copy.p);
    original_select_act(local_act);
    if(rebuild_terrain){
        original_terrain_load(terraform_mode(),skg::ptr_add<void>(copy.p,0x18));
        terrain_rebuild(terraform_mode());
    }
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
    bind_acts(snapshot.document);
    remember_terrain(snapshot.document);remember_grounding(snapshot.document);
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
    if(snapshot.metadata && !notify_metadata_caption(metadata_owner,s.generation,metadata_scope,*snapshot.metadata,error))return false;
    ++restores;
    if(local_view){
        if(terrain_rebuilding){
            deferred_local_view=DeferredLocalView{*local_view,C.gesture_scope?C.gesture_scope():GestureScope{},generation,mode(),data(mode())};
            error.clear();
        }else if(!restore_view(*local_view,generation,error))return false;
    }
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

bool prepare(const Snapshot& base,const editor::Document& document,const editor::Change& change,
             const Snapshot* artifact,Snapshot& out,std::string& error){
    if(!supported || document.placements.size()>kPlacementLimit || document.acts.empty()){
        error="The detached native editor resource is unavailable or exceeds its limits";return false;
    }
    bool tracked=false;if(!relative_scale_mode(document,tracked,error))return false;
    // No capture/state/restore calls belong here: the host may currently own a
    // creature/building editor and have no Adventure Editor scene at all.
    if(!validate(base,base.document,error))return false;
    std::set<editor::ObjectId> pose_targets;
    if(exact_pose_change(base.document,document,change,pose_targets)){
        editor::Limits limits;limits.placements=kPlacementLimit;
        if(!editor::valid(document,limits,error))return false;
        Owned posed(parse(base.bytes,error));if(!posed.p)return false;
        for(auto id:pose_targets){
            auto binding=std::find_if(base.bindings.begin(),base.bindings.end(),[&](const auto& b){return b.object==id;});
            auto* marker=binding==base.bindings.end()?nullptr:marker_for_key(posed.p,binding->marker);
            const auto& p=document.placements.at(id);float relative=0;
            if(!marker || skg::field<uint32_t>(marker,0)!=uint32_t(p.class_id.id) || !relative_scale_value(p,tracked,relative,error)){
                error="The owned pose target has no verified native marker";return false;
            }
            const auto old_scale=skg::field<float>(marker,0x20);
            if(tracked)skg::field<float>(marker,0x24)=relative;
            else if(old_scale>0)skg::field<float>(marker,0x24)*=p.scale/old_scale;
            skg::field<editor::Vec3>(marker,4)=p.position;skg::field<editor::Rotation>(marker,0x10)=p.rotation;
            skg::field<float>(marker,0x20)=p.scale;
            auto assign=[&]<class T>(uint32_t property,uint32_t offset){
                auto f=p.properties.find(property);if(f==p.properties.end())return true;
                const auto* value=std::get_if<T>(&f->second);if(!value)return false;
                skg::field<T>(marker,offset)=*value;return true;
            };
            if(!assign.template operator()<float>(kElevationProperty,0x220) ||
               !assign.template operator()<float>(kSoundPitchProperty,0x1E0) ||
               !assign.template operator()<float>(kSoundGainProperty,0x1E4) ||
               !assign.template operator()<editor::Vec3>(kDestinationPositionProperty,0x1E8) ||
               !assign.template operator()<editor::Rotation>(kDestinationRotationProperty,0x1F4) ||
               !assign.template operator()<float>(kDestinationScaleProperty,0x204)){
                error="The owned pose companion has the wrong type";return false;
            }
        }
        Snapshot prepared=base;prepared.document=document;
        if(!serialize(posed.p,prepared.bytes,error) || !validate(prepared,document,error))return false;
        Gesture checked;checked.before=base;checked.after=prepared;
        if(!supported_changes(checked,error))return false;
        out=std::move(prepared);error.clear();return true;
    }
    Owned merged(parse(base.bytes,error)),source(artifact?parse(artifact->bytes,error):nullptr),retained(construct());
    if(!merged.p || !retained.p || (artifact && !source.p))return false;
    const bool roster=base.document.act_order!=document.act_order;
    if(roster){
        if(!source.p || !artifact || artifact->document.act_order!=document.act_order){
            error="Changing the detached act roster requires its matching owned artifact";return false;
        }
        Rows acts;if(!rows(source.p,0x23C,0x534,8,acts) || size_t((acts.end-acts.begin)/0x534)!=document.act_order.size()){
            error="The detached act artifact has a different native roster";return false;
        }
        skg::thiscall<void*>(a_copy_acts,skg::ptr_add<void>(merged.p,0x23C),skg::ptr_add<void>(source.p,0x23C));
    }else if(base.document.acts!=document.acts){error="An act changed without an ordered roster transaction";return false;}
    Rows acts;if(!rows(merged.p,0x23C,0x534,8,acts)){error="The detached native acts are invalid";return false;}
    if(document.settings_schema!=base.document.settings_schema){error="The detached authoring schema changed";return false;}
    for(const auto& [id,settings]:document.settings){
        if(settings.kind==editor::SettingsKind::Act){
            auto i=std::find(document.act_order.begin(),document.act_order.end(),settings.act_id);
            if(i==document.act_order.end()){error="A detached act settings owner is missing";return false;}
            const auto field=settings.properties.find(kActBlockProperty);
            const auto* block=field==settings.properties.end()?nullptr:std::get_if<editor::StructuredValue>(&field->second);
            if(!block || settings.properties.size()!=1 || block->format!=kActBlockFormat || block->bytes.size()>editor::Limits{}.structured_bytes){
                error="The detached act subtree is invalid";return false;
            }
            Owned fragment(parse(block->bytes,error));Rows one;
            if(!fragment.p || !rows(fragment.p,0x23C,0x534,1,one) || one.end-one.begin!=0x534)return false;
            editor::StructuredValue checked;if(!act_block(one.begin,checked,error) || checked!=*block){error="The detached act subtree is not canonical";return false;}
            skg::thiscall<void*>(a_copy_act,acts.begin+(i-document.act_order.begin())*0x534,one.begin);
        }else if(settings.kind==editor::SettingsKind::Crew){
            if(!assign_crew(merged.p,settings,error))return false;
        }else if(settings.kind==editor::SettingsKind::Captain){
            if(id!=kCaptainObject || settings.properties.size()!=7){error="The detached captain settings are invalid";return false;}
            const auto& p=settings.properties;
            auto field=p.find(kCaptainAssetProperty);
            const auto* key=field==p.end()?nullptr:std::get_if<editor::ResourceKey>(&field->second);
            if(!key){error="The detached captain asset is missing";return false;}
            skg::field<editor::ResourceKey>(merged.p,0x130)=*key;
            skg::field<uint32_t>(merged.p,0x13C)=0;skg::field<uint64_t>(merged.p,0x140)=UINT64_MAX;
            auto assign=[&]<class T>(uint32_t property,uint32_t offset){
                auto i=p.find(property);const auto* value=i==p.end()?nullptr:std::get_if<T>(&i->second);
                if(!value)return false;skg::field<T>(merged.p,offset)=*value;return true;
            };
            if(!assign.template operator()<editor::Vec3>(kCaptainPositionProperty,0x150) ||
               !assign.template operator()<editor::Rotation>(kCaptainRotationProperty,0x15C) ||
               !assign.template operator()<float>(kCaptainScaleProperty,0x16C) ||
               !assign.template operator()<float>(kCaptainHealthProperty,0x170) ||
               !assign.template operator()<bool>(kCaptainInvulnerableProperty,0x174) ||
               !assign.template operator()<bool>(kCaptainLockedProperty,0x175)){
                error="A detached captain field is missing or has the wrong type";return false;
            }
        }
    }
    if(!document.class_schema.empty()){
        if(!cast_schema(document,error))return false;
        Owned empty(construct());if(!empty.p){error="Cannot allocate detached cast storage";return false;}
        skg::thiscall<void*>(a_copy_class_rows,skg::ptr_add<void>(merged.p,0x2BF4),skg::ptr_add<void>(empty.p,0x2BF4));
        for(const auto& [id,settings]:document.class_settings){
            (void)id;auto field=settings.properties.find(kCastBlockProperty);
            const auto* block=field==settings.properties.end()?nullptr:std::get_if<editor::StructuredValue>(&field->second);
            if(!block || block->format!=kCastBlockFormat || block->bytes.size()>editor::Limits{}.structured_bytes){error="Invalid detached cast subtree";return false;}
            Owned fragment(parse(block->bytes,error));Rows one;
            if(!fragment.p || !rows(fragment.p,0x2BF4,0x27E8,1,one) || one.end-one.begin!=0x27E8 ||
               settings.class_id!=editor::ObjectId{(2ull<<32)|skg::field<uint32_t>(one.begin,0),1}){
                error="The detached cast subtree changed its identity";return false;
            }
            editor::StructuredValue checked;if(!class_block(one.begin,checked,error) || checked!=*block){error="The detached cast subtree is not canonical";return false;}
            auto index=uint32_t(settings.class_id.id);
            void* target=skg::thiscall<void*>(a_insert_class,skg::ptr_add<void>(merged.p,0x2BF4),&index);
            if(!target){error="Cannot allocate the detached cast class";return false;}
            skg::thiscall<void*>(a_copy_class,target,one.begin+8);
            if(!assign_class_primary(target,settings,error))return false;
        }
        skg::field<uint32_t>(merged.p,0x2C0C)=document.next_class;
    }else if(base.document.classes!=document.classes){error="The detached cast roster requires typed settings";return false;}
    auto lookup=[&](void* resource,const Snapshot& snapshot,editor::ObjectId id)->void*{
        for(const auto& binding:snapshot.bindings)if(binding.object==id)return marker_for_key(resource,binding.marker);
        return nullptr;
    };
    std::vector<Binding> next;
    for(const auto& [id,to]:document.placements){
        const auto prior=base.document.placements.find(id);
        if(prior!=base.document.placements.end() && prior->second.class_id!=to.class_id){error="A detached placement changed its class";return false;}
        void* original=prior==base.document.placements.end()?nullptr:lookup(merged.p,base,id);
        if(!original && artifact)original=lookup(source.p,*artifact,id);
        if(!original){
            auto clone=change.clone_sources.find(id);
            if(clone!=change.clone_sources.end())original=lookup(merged.p,base,clone->second);
        }
        if(!original || skg::field<uint32_t>(original,0)!=uint32_t(to.class_id.id)){
            error="The detached placement has no matching owned native marker artifact";return false;
        }
        const auto index=skg::thiscall<uint32_t>(a_insert_marker,skg::ptr_add<void>(retained.p,0x2C10),original);
        void* marker=marker_for_key(retained.p,index);
        if(!marker){error="Cannot allocate the detached native placement";return false;}
        if(roster){
            void* companion=artifact?lookup(source.p,*artifact,id):nullptr;
            if(!companion){error="The detached act transaction lost a placement companion";return false;}
            skg::thiscall<void*>(a_copy_marker_acts,skg::ptr_add<void>(marker,0x28),skg::ptr_add<void>(companion,0x28));
        }else if(artifact && prior==base.document.placements.end() && artifact->document.act_order!=document.act_order){
            error="The restored placement has a stale act roster";return false;
        }
        float relative=0;if(!relative_scale_value(to,tracked,relative,error))return false;
        skg::field<editor::Vec3>(marker,4)=to.position;skg::field<editor::Rotation>(marker,0x10)=to.rotation;
        const auto old_scale=skg::field<float>(marker,0x20);
        if(tracked)skg::field<float>(marker,0x24)=relative;
        else if(old_scale>0)skg::field<float>(marker,0x24)*=to.scale/old_scale;
        skg::field<float>(marker,0x20)=to.scale;
        auto assign=[&]<class T>(uint32_t property,uint32_t offset){
            auto i=to.properties.find(property);if(i==to.properties.end())return true;
            const auto* value=std::get_if<T>(&i->second);if(!value)return false;
            skg::field<T>(marker,offset)=*value;return true;
        };
        if(!assign.template operator()<float>(kElevationProperty,0x220) ||
           !assign.template operator()<float>(kSoundPitchProperty,0x1E0) ||
           !assign.template operator()<float>(kSoundGainProperty,0x1E4) ||
           !assign.template operator()<editor::Vec3>(kDestinationPositionProperty,0x1E8) ||
           !assign.template operator()<editor::Rotation>(kDestinationRotationProperty,0x1F4) ||
           !assign.template operator()<float>(kDestinationScaleProperty,0x204)){
            error="A detached placement field has the wrong type";return false;
        }
        next.push_back({id,index});
    }
    skg::thiscall<void*>(a_copy_marker_rows,skg::ptr_add<void>(merged.p,0x2C10),skg::ptr_add<void>(retained.p,0x2C10));
    skg::field<uint32_t>(merged.p,0x2C24)=skg::field<uint32_t>(retained.p,0x2C24);
    skg::field<uint32_t>(merged.p,0x2C28)=skg::field<uint32_t>(retained.p,0x2C28);
    Snapshot prepared=base;prepared.document=document;prepared.bindings=std::move(next);
    prepared.act=std::min<uint32_t>(base.act,uint32_t(document.acts.size()-1));
    if(!hydrate_native_captain(merged.p,prepared,error))return false;
    if(!serialize(merged.p,prepared.bytes,error))return false;
    if(document.settings.contains(editor_native_terrain::kPlanetObject) && !editor_native_terrain::merge(prepared.bytes,document,prepared.bytes,error))return false;
    if(!validate(prepared,document,error))return false;
    // A detached transform must pass the same opaque-field guard as an active
    // native gesture. Revisions and local metadata never authorize extra bytes.
    Gesture checked;checked.before=base;checked.after=prepared;
    if(!supported_changes(checked,error))return false;
    out=std::move(prepared);error.clear();return true;
}

bool edit_owned_snapshot(const Snapshot& base,const std::function<bool(void*,std::string&)>& edit,
                         Snapshot& out,std::string& error){
    if(!supported || !edit || !validate(base,base.document,error))return false;
    Owned owned(parse(base.bytes,error));if(!owned.p)return false;
    {struct ScopedApply{bool prior=applying;ScopedApply(){applying=true;}~ScopedApply(){applying=prior;}} scope;
     if(!edit(owned.p,error))return false;}
    bool relative=false;if(!relative_scale_mode(base.document,relative,error))return false;
    std::map<uint32_t,editor::ObjectId> supplied;
    for(const auto& b:base.bindings)if(!supplied.emplace(b.marker,b.object).second){error="The owned snapshot has duplicate marker bindings";return false;}
    Snapshot result=base;
    if(!serialize(owned.p,result.bytes,error) || !projection(owned.p,result,error,&supplied,relative,base.document.schema.contains(kElevationProperty),
                   !base.document.class_schema.empty(),!base.document.settings_schema.empty(),&base.document,&result.bytes))return false;
    result.source=skg::field<editor::ResourceKey>(owned.p,8);
    if(!hydrate_native_captain(owned.p,result,error))return false;
    std::vector<Binding> compact;if(!serialized_bindings(result.bindings,compact,error))return false;
    result.bindings=std::move(compact);out=std::move(result);error.clear();return true;
}

bool apply_prepared(const Snapshot& prepared,const editor::Change& change,std::string& error){
    const auto current=state();
    if(!current.ready || current.gesture || applying){error="The native editor is not ready to apply its canonical resource";return false;}
    const auto& patch=change.patch;
    const bool poses=!patch.fields.empty() && patch.objects.empty() && patch.classes.empty() && !patch.acts &&
        patch.terrain.empty() && std::all_of(patch.fields.begin(),patch.fields.end(),[&](const auto& f){return prepared.document.placements.contains(f.object);});
    if(!poses)return restore(prepared,error);
    std::set<editor::ObjectId> targets;for(const auto& f:patch.fields)targets.insert(f.object);
    void* owner=data(mode());
    // Preflight every stable binding before the first scene write. No native
    // indices or pointers from prepare's owned clone escape into this path.
    for(auto id:targets)if(!marker_for_id(resource(owner),id)){error="A canonical pose target is no longer bound";return false;}
    applying=true;
    for(auto id:targets){
        const auto& p=prepared.document.placements.at(id);void* marker=marker_for_id(resource(owner),id);
        skg::field<editor::Vec3>(marker,4)=p.position;skg::field<editor::Rotation>(marker,0x10)=p.rotation;
        const auto previous=skg::field<float>(marker,0x20);
        if(auto f=p.properties.find(kRelativeScaleProperty);f!=p.properties.end())skg::field<float>(marker,0x24)=std::get<float>(f->second);
        else if(previous>0)skg::field<float>(marker,0x24)*=p.scale/previous;
        skg::field<float>(marker,0x20)=p.scale;
        auto assign=[&]<class T>(uint32_t property,uint32_t offset){auto f=p.properties.find(property);if(f!=p.properties.end())skg::field<T>(marker,offset)=std::get<T>(f->second);};
        assign.template operator()<float>(kElevationProperty,0x220);assign.template operator()<float>(kSoundPitchProperty,0x1E0);
        assign.template operator()<float>(kSoundGainProperty,0x1E4);assign.template operator()<editor::Vec3>(kDestinationPositionProperty,0x1E8);
        assign.template operator()<editor::Rotation>(kDestinationRotationProperty,0x1F4);assign.template operator()<float>(kDestinationScaleProperty,0x204);
        skg::thiscall<void>(a_apply_marker,owner,marker,false);
        if(p.properties.contains(kDestinationPositionProperty))render_destination(skg::thiscall<void*>(a_object,owner,marker),
            {std::get<editor::Vec3>(p.properties.at(kDestinationPositionProperty)),std::get<editor::Rotation>(p.properties.at(kDestinationRotationProperty)),std::get<float>(p.properties.at(kDestinationScaleProperty))});
    }
    skg::field<bool>(owner,0x84)=true;applying=false;refresh_selection();++restores;error.clear();return true;
}

bool apply(const editor::Document& document,const editor::Change& change,const Snapshot* artifact,std::string& error) {
    InspectorView inspector;
    if(document.placements.size()>kPlacementLimit){error="Native editor placement limit exceeded";return false;}
    bool tracked=false;if(!relative_scale_mode(document,tracked,error))return false;
    Snapshot snapshot;
    if (!capture(snapshot,error)) return false;
    if(snapshot.document.act_order!=document.act_order && !document.act_order.empty()){
        if(!artifact || artifact->bytes.empty()){error="Changing acts requires its verified native artifact";return false;}
        Owned merged(parse(snapshot.bytes,error)),source(parse(artifact->bytes,error));
        if(!merged.p || !source.p)return false;
        Rows wanted_acts;if(!rows(source.p,0x23C,0x534,8,wanted_acts) || size_t((wanted_acts.end-wanted_acts.begin)/0x534)!=document.act_order.size()){
            error="The act artifact has a different roster";return false;
        }
        skg::thiscall<void*>(a_copy_acts,skg::ptr_add<void>(merged.p,0x23C),skg::ptr_add<void>(source.p,0x23C));
        Rows merged_acts,classes;if(!rows(merged.p,0x23C,0x534,8,merged_acts) || !rows(merged.p,0x2BF4,0x27E8,1024,classes))return false;
        const auto ordered=editor::act_roster(document);
        if(ordered.size()!=document.act_order.size()){error="The act settings roster is incomplete";return false;}
        for(size_t i=0;i<ordered.size();++i){
            auto value=ordered[i].properties.find(kActBlockProperty);
            const auto* block=value==ordered[i].properties.end()?nullptr:std::get_if<editor::StructuredValue>(&value->second);
            if(!block || block->format!=kActBlockFormat){error="The act subtree is missing";return false;}
            Owned fragment(parse(block->bytes,error));Rows rows;
            if(!fragment.p || !editor_native::rows(fragment.p,0x23C,0x534,1,rows) || rows.end-rows.begin!=0x534)return false;
            skg::thiscall<void*>(a_copy_act,merged_acts.begin+i*0x534,rows.begin);
        }
        for(const auto& [id,settings]:document.class_settings){
            (void)id;auto value=settings.properties.find(kCastBlockProperty);
            const auto* block=value==settings.properties.end()?nullptr:std::get_if<editor::StructuredValue>(&value->second);
            if(!block || block->format!=kCastBlockFormat){error="The act behavior subtree is missing";return false;}
            Owned fragment(parse(block->bytes,error));Rows from;
            if(!fragment.p || !rows(fragment.p,0x2BF4,0x27E8,1,from) || from.end-from.begin!=0x27E8 ||
               skg::field<uint32_t>(from.begin,0)!=uint32_t(settings.class_id.id))return false;
            auto target=classes.begin;
            for(;target!=classes.end;target+=0x27E8)if(skg::field<uint32_t>(target,0)==uint32_t(settings.class_id.id))break;
            if(target==classes.end){error="The act roster changed its cast identities";return false;}
            skg::thiscall<void*>(a_copy_class,target+8,from.begin+8);
            if(!assign_class_primary(target+8,settings,error))return false;
        }
        for(const auto& binding:snapshot.bindings){
            auto match=std::find_if(artifact->bindings.begin(),artifact->bindings.end(),[&](const auto& b){return b.object==binding.object;});
            auto* to=marker_for_key(merged.p,binding.marker);
            auto* from=match==artifact->bindings.end()?nullptr:marker_for_key(source.p,match->marker);
            if(!to || !from){error="The act artifact is missing a placement companion";return false;}
            skg::thiscall<void*>(a_copy_marker_acts,skg::ptr_add<void>(to,0x28),skg::ptr_add<void>(from,0x28));
        }
        auto prepared=snapshot;prepared.document=document;prepared.act=std::min<uint32_t>(snapshot.act,uint32_t(document.act_order.size()-1));
        Gesture checked;checked.before=snapshot;checked.after=prepared;
        if(!serialize(merged.p,prepared.bytes,error) || !validate(prepared,document,error) ||
           (C.validate_dependencies && !C.validate_dependencies(prepared,error)))return false;
        checked.after=prepared;
        if(!supported_changes(checked,error))return false;
        return restore(prepared,error);
    }
    if ((!tracks_cast() && snapshot.document.classes != document.classes) || snapshot.document.acts != document.acts ||
        snapshot.document.schema!=document.schema) {
        error="Shared editing cannot change native classes or acts yet"; return false;
    }
    const bool settings_changed=snapshot.document.settings!=document.settings &&
        std::any_of(document.settings.begin(),document.settings.end(),[&](const auto& row){
            auto prior=snapshot.document.settings.find(row.first);return prior==snapshot.document.settings.end() || prior->second.properties!=row.second.properties;
        });
    if(settings_changed){
        if(document.settings_schema!=authoring_property_schema() || document.settings.size()!=snapshot.document.settings.size()){
            error="Unsupported native act/captain settings roster";return false;
        }
        Owned merged(parse(snapshot.bytes,error));if(!merged.p)return false;
        Rows acts;if(!rows(merged.p,0x23C,0x534,8,acts)){error="Invalid native act list";return false;}
        for(const auto& [id,settings]:document.settings){
            auto prior=snapshot.document.settings.find(id);
            if(prior==snapshot.document.settings.end() || prior->second.kind!=settings.kind || prior->second.act_id!=settings.act_id){error="A native settings owner changed";return false;}
            if(prior->second.properties==settings.properties)continue;
            if(settings.kind==editor::SettingsKind::Act){
                const auto found=std::find(document.act_order.begin(),document.act_order.end(),settings.act_id);
                const auto index=document.act_order.empty()?uint32_t(settings.act_id.id):uint32_t(found-document.act_order.begin());
                auto field=settings.properties.find(kActBlockProperty);
                const auto* block=field==settings.properties.end()?nullptr:std::get_if<editor::StructuredValue>(&field->second);
                if(settings.properties.size()!=1 || !block || block->format!=kActBlockFormat ||
                   block->bytes.size()>editor::Limits{}.structured_bytes || index>=uint32_t((acts.end-acts.begin)/0x534)){
                    error="Invalid native act subtree";return false;
                }
                Owned fragment(parse(block->bytes,error));Rows source;
                if(!fragment.p || !rows(fragment.p,0x23C,0x534,1,source) || source.end-source.begin!=0x534)return false;
                editor::StructuredValue checked;if(!act_block(source.begin,checked,error))return false;
                if(checked!=*block){error="The native act subtree or its references are not canonical";return false;}
                skg::thiscall<void*>(a_copy_act,acts.begin+index*0x534,source.begin);
            }else if(settings.kind==editor::SettingsKind::Crew){
                if(!assign_crew(merged.p,settings,error))return false;
            }else{
                if(id!=kCaptainObject || settings.properties.size()!=7){error="Invalid native captain settings";return false;}
                const auto& p=settings.properties;
                const auto asset=p.find(kCaptainAssetProperty);
                if(asset==p.end() || !std::get_if<editor::ResourceKey>(&asset->second)){error="The captain asset is missing";return false;}
                skg::field<editor::ResourceKey>(merged.p,0x130)=std::get<editor::ResourceKey>(asset->second);
                skg::field<uint32_t>(merged.p,0x13C)=0;skg::field<uint64_t>(merged.p,0x140)=UINT64_MAX;
                for(auto [key,offset]:{std::pair{kCaptainPositionProperty,0x150u},std::pair{kCaptainRotationProperty,0x15Cu},
                    std::pair{kCaptainScaleProperty,0x16Cu},std::pair{kCaptainHealthProperty,0x170u},
                    std::pair{kCaptainLockedProperty,0x175u},std::pair{kCaptainInvulnerableProperty,0x174u}}){
                    auto field=p.find(key);if(field==p.end()){error="A captain field is missing";return false;}
                    if(key==kCaptainPositionProperty){auto* v=std::get_if<editor::Vec3>(&field->second);if(!v)return false;skg::field<editor::Vec3>(merged.p,offset)=*v;}
                    else if(key==kCaptainRotationProperty){auto* v=std::get_if<editor::Rotation>(&field->second);if(!v)return false;skg::field<editor::Rotation>(merged.p,offset)=*v;}
                    else if(key==kCaptainLockedProperty || key==kCaptainInvulnerableProperty){auto* v=std::get_if<bool>(&field->second);if(!v)return false;skg::field<bool>(merged.p,offset)=*v;}
                    else {auto* v=std::get_if<float>(&field->second);if(!v)return false;skg::field<float>(merged.p,offset)=*v;}
                }
            }
        }
        auto prepared=snapshot;prepared.document.settings_schema=document.settings_schema;prepared.document.settings=document.settings;
        if(!hydrate_native_captain(merged.p,prepared,error))return false;
        if(!serialize(merged.p,prepared.bytes,error) || !validate(prepared,prepared.document,error) ||
           (C.validate_dependencies && !C.validate_dependencies(prepared,error)) || !restore(prepared,error) || !capture(snapshot,error))return false;
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
            if(!assign_class_primary(target,settings,error))return false;
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
    if(source.p && !document.act_order.empty()){
        Rows acts;if(!rows(source.p,0x23C,0x534,8,acts)){error="Invalid native placement artifact acts";return false;}
        auto order=artifact->document.act_order;
        const auto count=uint32_t((acts.end-acts.begin)/0x534);
        if(order.empty())for(uint32_t i=0;i<count;++i)order.push_back({(3ull<<32)|i,1});
        if(order.size()!=count){error="The placement artifact act identities differ from its native rows";return false;}
        if(order!=document.act_order){
            std::vector<uint32_t> indices;
            for(auto act:document.act_order){
                auto found=std::find(order.begin(),order.end(),act);
                if(found==order.end()){error="This placement history predates a new act; its native path companion cannot be restored safely";return false;}
                indices.push_back(uint32_t(found-order.begin()));
            }
            Owned original(parse(artifact->bytes,error));if(!original.p || !permute_acts(source.p,original.p,indices,error))return false;
        }
    }
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
        if(to.properties.contains(kDestinationPositionProperty)){
            const editor::Pose exit{std::get<editor::Vec3>(to.properties.at(kDestinationPositionProperty)),
                std::get<editor::Rotation>(to.properties.at(kDestinationRotationProperty)),
                std::get<float>(to.properties.at(kDestinationScaleProperty))};
            render_destination(skg::thiscall<void*>(a_object,owner,marker),exit);
        }
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

bool select_act(uint32_t act,std::string& error){
    const auto current=state();Rows acts;
    if(!current.ready || current.gesture || applying || !a_select_act || !rows(resource(data(mode())),0x23C,0x534,8,acts) ||
       act>=uint32_t((acts.end-acts.begin)/0x534)){error="The requested native act is unavailable during this transition";return false;}
    if(current.act==act)return true;
    const bool prior=applying;applying=true;skg::cdecl_call<void>(a_select_act,act);applying=prior;
    refresh_selection();
    if(state().act!=act){error="The native editor did not select the requested act";return false;}
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
    object=current.selected.front();act=current.act<act_identity.act_order.size()?act_identity.act_order[current.act]:editor::ObjectId{(3ull<<32)|current.act,1};pose=candidate;return true;
}

bool selection_preview(LivePreview& out){
    if(!selection_pose(out.object,out.act,out.pose))return false;
    if(out.object==kCaptainObject){out.class_id={};out.creating=false;out.destination.reset();return true;}
    void* marker=marker_for_id(resource(data(mode())),out.object);
    if(!skg::readable(marker,0x230))return false;
    out.class_id={(2ull<<32)|skg::field<uint32_t>(marker,0),1};
    out.creating=input_clone.has_value() || gesture_clone.has_value();
    // The stable binding is installed only after canonical allocation.
    bool bound=false;for(const auto& [slot,id]:bindings){(void)slot;if(id==out.object){bound=true;break;}}
    out.creating=out.creating || !bound;
    Rows classes;if(!rows(resource(data(mode())),0x2BF4,0x27E8,1024,classes))return false;
    out.destination.reset();
    for(auto row=classes.begin;row!=classes.end;row+=0x27E8)if(skg::field<uint32_t>(row,0)==uint32_t(out.class_id.id) &&
        skg::field<uint32_t>(row,8)==0x90C48D09u){
        out.destination=editor::Pose{skg::field<editor::Vec3>(marker,0x1E8),skg::field<editor::Rotation>(marker,0x1F4),skg::field<float>(marker,0x204)};break;
    }
    return true;
}

void update_previews(const std::vector<LivePreview>& values){
    static void* visual_owner=nullptr;
    static void* visual_resource=nullptr;
    static std::set<editor::ObjectId> shown;
    void* owner=data(mode());void* r=resource(owner);
    if(!editing() || owner!=visual_owner || r!=visual_resource){
        shown.clear();visual_owner=owner;visual_resource=r;
        for(auto& [peer,actor]:preview_actors){(void)peer;clear_preview_actor(actor);}preview_actors.clear();
    }
    if(!editing() || applying || !state().ready)return;
    // Resolve every object afresh. Scene reconstruction, deletions and pool
    // reuse cannot leave a borrowed render pointer in a delayed callback.
    for(auto id:shown){
        void* marker=marker_for_id(r,id);if(!marker)continue;
        void* object=skg::thiscall<void*>(a_object,owner,marker);
        render_pose(object,{skg::field<editor::Vec3>(marker,4),skg::field<editor::Rotation>(marker,0x10),skg::field<float>(marker,0x20)});
        if(native_cast(object,0x0175CDC9))render_destination(object,{skg::field<editor::Vec3>(marker,0x1E8),skg::field<editor::Rotation>(marker,0x1F4),skg::field<float>(marker,0x204)});
    }
    shown.clear();
    std::set<editor::Peer> creating;
    for(const auto& value:values){
        if(!active())break;
        const auto index=state().act;
        const auto local=index<act_identity.act_order.size()?act_identity.act_order[index]:editor::ObjectId{(3ull<<32)|index,1};
        if(value.act!=local && (value.creating || !tracks_authoring()))continue;
        if(value.creating){
            if(!a_preview_factory || !a_marker_ctor || !a_marker_dtor || !a_resize_marker_acts || !a_object_reference)continue;
            creating.insert(value.author);
            auto& actor=preview_actors[value.author];
            if(actor.source!=value.object || actor.asset!=value.class_id || actor.act!=value.act){clear_preview_actor(actor);actor.source=value.object;actor.asset=value.class_id;actor.act=value.act;}
            if(!actor.object){
                Rows classes;if(!rows(r,0x2BF4,0x27E8,1024,classes))continue;
                for(auto row=classes.begin;row!=classes.end;row+=0x27E8)if(skg::field<uint32_t>(row,0)==uint32_t(value.class_id.id)){
                    const auto key=skg::field<editor::ResourceKey>(row,8);
                    Rows acts;if(!rows(r,0x23C,0x534,8,acts) || acts.begin==acts.end)break;
                    actor.marker.reset(new(std::nothrow) uint8_t[0x234]{});
                    if(!actor.marker)break;
                    skg::thiscall<void*>(a_marker_ctor,actor.marker.get());
                    skg::thiscall<void>(a_resize_marker_acts,actor.marker.get()+0x28,uint32_t((acts.end-acts.begin)/0x534));
                    auto* marker=actor.marker.get();
                    skg::field<uint32_t>(marker,0)=uint32_t(value.class_id.id);
                    skg::field<editor::Vec3>(marker,4)=value.pose.position;
                    skg::field<editor::Rotation>(marker,0x10)=value.pose.rotation;
                    skg::field<float>(marker,0x20)=value.pose.scale;
                    const auto exit=value.destination.value_or(value.pose);
                    skg::field<editor::Vec3>(marker,0x1E8)=exit.position;
                    skg::field<editor::Rotation>(marker,0x1F4)=exit.rotation;
                    skg::field<float>(marker,0x204)=exit.scale;
                    // Marker storage is detached from ScenarioResource and both
                    // native lookup maps. Powerup factories require it even for
                    // rendering and the linked teleporter exit borrows it too.
                    actor.object=skg::cdecl_call<void*>(a_preview_factory,&key,&value.pose.position,&value.pose.rotation,
                        value.pose.scale,false,uint32_t(value.class_id.id),(void*)marker,skg::field<void*>(marker,0x28),index);
                    addref(actor.object);
                    void* teleporter=native_cast(actor.object,0x0175CDC9);
                    if(skg::readable(teleporter,0x58)){actor.secondary=skg::field<void*>(teleporter,0x54);addref(actor.secondary);}
                    if(!actor.object)clear_preview_actor(actor);
                    break;
                }
            }
            if(actor.object){render_pose(actor.object,value.pose);if(value.destination)render_destination(actor.object,*value.destination);}
            continue;
        }
        void* marker=marker_for_id(r,value.object);if(!marker)continue;
        void* object=skg::thiscall<void*>(a_object,owner,marker);
        if(render_pose(object,value.pose))shown.insert(value.object);
        if(value.destination)render_destination(object,*value.destination);
    }
    for(auto it=preview_actors.begin();it!=preview_actors.end();){
        if(creating.contains(it->first)){++it;continue;}
        clear_preview_actor(it->second);it=preview_actors.erase(it);
    }
}

std::string visual_status(){
    if(!state().ready || applying)return "Native editor visuals unavailable";
    std::string result;
    const auto position=[](void* object){
        void* spatial=native_cast(object,0x01186577);if(!skg::readable(spatial,16))return std::string("unavailable");
        const auto p=skg::field<editor::Vec3>(spatial,4);char text[96];
        snprintf(text,sizeof text,"%.9g,%.9g,%.9g",double(p.x),double(p.y),double(p.z));return std::string(text);
    };
    size_t count=0;
    for(const auto& [slot,id]:bindings){
        (void)slot;if(count++>=64)break;
        void* marker=marker_for_id(resource(data(mode())),id);if(!marker)continue;
        void* object=skg::thiscall<void*>(a_object,data(mode()),marker);
        result+="placement="+std::to_string(id.id)+":"+std::to_string(id.generation)+" render="+position(object);
        void* teleporter=native_cast(object,0x0175CDC9);
        if(skg::readable(teleporter,0x58))result+=" exit="+position(skg::field<void*>(teleporter,0x54));
        result+='\n';
    }
    for(const auto& [peer,actor]:preview_actors){
        result+="ghost="+std::to_string(peer.slot)+":"+std::to_string(peer.generation)+" class="+
            std::to_string(actor.asset.id)+" render="+position(actor.object);
        if(actor.secondary)result+=" exit="+position(actor.secondary);
        result+='\n';
    }
    return result.empty()?"No native placement visuals":result;
}

std::string authoring_status(){
    const auto current=state();auto* r=static_cast<uint8_t*>(resource(data(mode())));Rows acts;
    if((!current.editing && !current.playtesting) || !skg::readable(r,kResourceSize) || !rows(r,0x23C,0x534,8,acts))return "Native authoring settings unavailable";
    auto text=[](uint8_t* value){
        const wchar_t* str=nullptr;size_t length=0;
        const auto* begin=skg::field<const wchar_t*>(value,0x14);const auto* end=skg::field<const wchar_t*>(value,0x18);
        if(end>=begin && size_t(end-begin)<=4096){str=begin;length=std::min<size_t>(end-begin,160);}
        if(!length){str=skg::field<const wchar_t*>(value,8);if(str)while(length<160 && skg::readable(str+length,2) && str[length])++length;}
        if(!str || !length || !skg::readable(str,length*2))return std::string{};
        std::string result(length*4,'\0');int count=WideCharToMultiByte(CP_UTF8,0,str,int(length),result.data(),int(result.size()),nullptr,nullptr);
        result.resize(count>0?size_t(count):0);for(char& c:result)if(c=='\r' || c=='\n' || c=='"')c=' ';return result;
    };
    const auto p=skg::field<editor::Vec3>(r,0x150);const auto key=skg::field<editor::ResourceKey>(r,0x130);char line[400];
    snprintf(line,sizeof line,"editor_act=%u captain=%08X!%08X.%08X locked=%u pos=%.9g,%.9g,%.9g scale=%.9g health=%.9g invulnerable=%u\n",
        current.act,key.group,key.instance,key.type,unsigned(skg::field<bool>(r,0x175)),double(p.x),double(p.y),double(p.z),double(skg::field<float>(r,0x16C)),double(skg::field<float>(r,0x170)),unsigned(skg::field<bool>(r,0x174)));
    std::string result=line;uint32_t index=0;
    float xy[2];
    if(H->world_to_screen && H->world_to_screen(&p.x,xy) && std::isfinite(xy[0]) && std::isfinite(xy[1])){
        snprintf(line,sizeof line,"captain_screen=%.9g,%.9g\n",double(xy[0]),double(xy[1]));result+=line;
    }
    for(auto act=acts.begin;act!=acts.end;act+=0x534,++index){
        result+="act="+std::to_string(index)+" name=\""+text(act)+"\" description=\""+text(act+0x3C)+"\" timer="+
            std::to_string(skg::field<int32_t>(act,0x78))+" visible="+std::to_string(unsigned(skg::field<bool>(act,0x7C)))+"\n";
        Rows goals;if(!rows(act,0x84,0x188,3,goals))return "Invalid native authoring goal list";
        uint32_t goal_index=0;for(auto goal=goals.begin;goal!=goals.end;goal+=0x188,++goal_index){
            snprintf(line,sizeof line," goal=%u:%u type=%d class=%d second=%d count=%d visible=%u\n",index,goal_index,skg::field<int32_t>(goal,0),
                skg::field<int32_t>(goal,4),skg::field<int32_t>(goal,8),skg::field<int32_t>(goal,0xC),unsigned(skg::field<bool>(goal,0x10)));result+=line;
        }
    }
    return result;
}

ProjectionCacheStats projection_cache_stats(){
    const auto stats=class_projection_cache.stats();return {stats.bytes,stats.entries,stats.hits,stats.misses};
}
std::string status() {
    auto s=state();const auto resume=creation_scene_state({});const auto cache=projection_cache_stats();char b[768];
    snprintf(b,sizeof b,"editor_native available=%u editing=%u ready=%u playtesting=%u entry=%u act=%u generation=%llu gesture=%u history=%u/%u captures=%llu restores=%llu rejected=%llu cancellation=%u cancellation_serial=%llu ordinary_cancellations=%llu class_cache_bytes=%zu class_cache_entries=%zu class_cache_hits=%llu class_cache_misses=%llu resume_available=%u resume_initialization=%u resume_enter_pending=%u resume_transitioning=%u world_visible=%u",
        s.available,s.editing,s.ready,s.playtesting,s.entry,s.act,(unsigned long long)s.generation,s.gesture,
        s.history_cursor,s.history_size,(unsigned long long)captures,(unsigned long long)restores,(unsigned long long)rejected,
        unsigned(cancellation.has_value()),(unsigned long long)cancellation_counter,(unsigned long long)ordinary_cancellations,
        cache.bytes,cache.entries,(unsigned long long)cache.hits,(unsigned long long)cache.misses,
        unsigned(resume.available),resume.initialization,unsigned(resume.enter_pending),unsigned(resume.transitioning),unsigned(resume.world_visible));
    return b;
}
} // namespace editor_native
