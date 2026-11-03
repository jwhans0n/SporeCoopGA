#include "editor_native.h"
#include "editor_native_snapshot_values.h"
#include "editor_native_bindings.h"
#include "editor_native_tools.h"
#include "editor_native_complexity.h"
#include "editor_native_complexity_ratio.h"
#include "editor_native_pose.h"
#include "editor_native_projection_cache.h"
#include "editor_native_act_cache.h"
#include "editor_native_terrain_materialization.h"
#include "editor_native_terrain_rotations.h"
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
namespace {struct ProjectedSnapshot;}
struct PoseSupportProof {
    struct Asset {uint64_t server=0;editor::ResourceKey key;bool operator==(const Asset&)const=default;};
    CaptureTicket ticket;
    Snapshot before,after;
    std::vector<Asset> before_assets,after_assets;
    std::shared_ptr<const ProjectedSnapshot> before_projection,after_projection;
    uintptr_t context=0;
    std::function<CaptureState(const CaptureTicket&)> current;
    mutable bool busy=false;
};
struct CreationTerrainProof {
    struct Placement {
        Binding live;
        uint32_t class_index=0,native_key=0;
        editor::Revision revision=0;
        editor::Vec3 rendered,authored;
        bool grounded=false;
    };
    GestureScope scope;uint64_t serial=0,generation=0;
    SceneOwner owner;uintptr_t tool=0;
    Snapshot entry;
    editor_native_terrain_materialization::Recipe rendered;
    editor_native_terrain_materialization::Materialization materialization;
    std::vector<Placement> placements;
    size_t owned_bytes=0;
    mutable bool retired=false;
};
namespace {
const SkHost* H;
SkMod* M;
Ctx C;
uintptr_t a_mode, a_history, a_act, a_ctor, a_read, a_write, a_copy, a_entry, a_restore, a_alloc;
uintptr_t a_context, a_tools, a_marker, a_object, a_clone_marker, a_create_marker, a_delete_marker, a_apply_marker;
uintptr_t a_loading, a_layout_find, a_display_vtable, a_erase_history, a_discard_history, a_copy_marker_rows, a_insert_marker;
uintptr_t a_history_undo_return,a_history_redo_return;
uintptr_t a_environment_ui_refresh,a_environment_ui_vtable;
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
uintptr_t a_property_manager_get,a_resolve_property_parents;
bool supported, applying, queued;
bool fence_queued;
double last_fence;
double last_tool_rejection_log=-1;
std::array<double,8> last_tool_rejection{{-1,-1,-1,-1,-1,-1,-1,-1}};
double last_legacy_scale_notice=-10;
double last_clone_scale_notice=-10;
std::array<uint64_t,8> tool_rejection_counts{};
std::map<sui::Win,uint32_t> fenced_windows;
uint64_t history_ui_updates=0,history_ui_undo=0,history_ui_redo=0,history_ui_changes=0,history_ui_expired=0;
uint64_t fenced_generation=0;
uint64_t generation = 1, captures = 0, restores = 0, rejected = 0;
unsigned validation_depth=0;
unsigned service_depth=0;
struct ServiceScope {
    ServiceScope(){++service_depth;}
    ~ServiceScope(){--service_depth;}
};
WorkStats native_work;
GestureScope value_scope(){const auto callback=C.projection_scope?C.projection_scope:C.gesture_scope;return callback?callback():GestureScope{};}
bool valid_scope(const GestureScope&);
uint64_t work_clock(){LARGE_INTEGER value{};return QueryPerformanceCounter(&value)&&value.QuadPart>0?uint64_t(value.QuadPart):0;}
struct WorkTimer {
    WorkSpan& span;uint64_t begin=work_clock();
    explicit WorkTimer(WorkSpan& value):span(value){++span.calls;}
    ~WorkTimer(){const auto end=work_clock();if(begin&&end>=begin)span.ticks+=end-begin;}
};
using ResolvedAssetWitness=PoseSupportProof::Asset;
struct ProjectedSnapshot {
    Snapshot image;
    GestureScope scope;
    uint64_t generation=0;
    std::vector<ResolvedAssetWitness> assets;
    size_t cost=0;
};
std::deque<std::shared_ptr<const ProjectedSnapshot>> projected_snapshots;
const std::array<std::shared_ptr<const ProjectedSnapshot>,2>* borrowed_projections=nullptr;
struct ProjectionBorrow {
    std::array<std::shared_ptr<const ProjectedSnapshot>,2> values;
    const std::array<std::shared_ptr<const ProjectedSnapshot>,2>* previous=borrowed_projections;
    ProjectionBorrow(std::shared_ptr<const ProjectedSnapshot> before,std::shared_ptr<const ProjectedSnapshot> after):
        values{std::move(before),std::move(after)}{borrowed_projections=&values;}
    ProjectionBorrow(const ProjectionBorrow&)=delete;
    ProjectionBorrow& operator=(const ProjectionBorrow&)=delete;
    ~ProjectionBorrow(){borrowed_projections=previous;}
};
size_t projected_snapshot_bytes=0;
uint64_t projected_snapshot_hits=0,projected_snapshot_misses=0;
uint64_t recent_match_hits=0,recent_match_different=0,recent_match_errors=0,pose_roundtrip_hits=0,pose_roundtrip_fallbacks=0;
uint64_t opaque_pose_hits=0,opaque_pose_fallbacks=0;
struct ResolutionAudit {std::map<uint64_t,editor::ResourceKey> keys;bool failed=false;};
ResolutionAudit* resolution_audit=nullptr;
struct ScopedResolutionAudit {
    ResolutionAudit audit;ResolutionAudit* previous=resolution_audit;
    ScopedResolutionAudit(){resolution_audit=&audit;}
    ~ScopedResolutionAudit(){resolution_audit=previous;}
};
bool resolve_asset(uint64_t server,editor::ResourceKey& key){
    WorkTimer timer(native_work.resolutions);
    const auto resolve=H&&H->sym?H->sym("Simulator::Scenario::ResolveLocalServerAsset"):0;
    if(!resolve || !skg::cdecl_call<bool>(resolve,server,&key))return false;
    if(resolution_audit){
        try{
            auto [at,added]=resolution_audit->keys.emplace(server,key);
            if(!added && at->second!=key){resolution_audit->failed=true;return false;}
        }catch(const std::bad_alloc&){resolution_audit->failed=true;return false;}
    }
    return true;
}
void remember_projection(const Snapshot&,const std::vector<ResolvedAssetWitness>&,const GestureScope&,uint64_t);
std::shared_ptr<const ProjectedSnapshot> projected_snapshot(const Snapshot&,std::string&);
editor_native_cache::ClassesCache class_projection_cache;
editor_native_act_cache::Cache act_projection_cache;
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
bool input_cast=false,gesture_cast=false,input_pose=false,gesture_pose=false;
struct ScopedPoseInput {
    bool previous=input_pose;
    explicit ScopedPoseInput(bool pose=true){input_pose=previous||pose;}
    ~ScopedPoseInput(){input_pose=previous;}
};
bool input_speech=false;
uint32_t script_events=0;
constexpr uint32_t kResourceSize = 0x2CB0, kLimit = 32u << 20;
uintptr_t a_copy_crew;
uintptr_t a_terrain_load,a_terrain_rebuild,a_terrain_selection,a_terrain_loaded,a_terrain_vtable,a_copy_terrain_properties;
uintptr_t a_terrain_materialize;
uintptr_t a_terraform_ui_vtable,a_flora_ui_vtable;
bool terrain_available=false,terrain_pending=false,terrain_capturing=false,terrain_queued=false;
bool terrain_recipe_replacement=false;
uint64_t terrain_rebuild_frame=0;
bool terrain_rebuilding=false;
std::optional<Snapshot> terrain_before;
GestureScope terrain_scope;uint64_t terrain_generation=0;void* terrain_owner=nullptr;
struct Grounding {editor::Vec3 native,authored;};
std::map<editor::ObjectId,Grounding> terrain_grounding;
struct TerrainCaptureRotations;
std::shared_ptr<const TerrainCaptureRotations> terrain_capture_rotations;
uint64_t terrain_rotation_serial=0;
std::string terrain_capture_rotation_error;
void clear_terrain_capture_rotations(){
    terrain_capture_rotations.reset();
    if(terrain_rotation_serial!=UINT64_MAX)++terrain_rotation_serial;
    terrain_capture_rotation_error.clear();
}
std::optional<editor::Document> materialized_terrain;
editor_native_terrain_materialization::Materialization terrain_materialization;
editor_native_terrain_materialization::AuthoredRows terrain_authored_rows;
struct TerrainMaterializationOwner {
    GestureScope scope;uint64_t generation=0;
    uintptr_t mode=0,data=0,resource=0,tool=0;editor::ResourceKey source;
    bool operator==(const TerrainMaterializationOwner&)const=default;
};
std::optional<TerrainMaterializationOwner> terrain_materialization_owner;
struct TerrainReadIdentity {
    const TerrainTransitionKey* key=nullptr;uintptr_t tool=0;
    const TerrainTransitionCurrent* current=nullptr;
    const editor_native_terrain_materialization::Materialization* values=nullptr;
    bool invalid=false;
};
TerrainReadIdentity* terrain_read_identity=nullptr; // Borrowed only inside one synchronous owned transition.
std::string terrain_materialization_error;
bool terrain_materialization_error_queued=false;
bool terrain_materialization_error_reported=false;
bool normalize_materialized_terrain(std::vector<uint8_t>&,std::string&);
void clear_terrain_materialization(){terrain_materialization.clear();terrain_materialization_owner.reset();terrain_materialization_error.clear();terrain_materialization_error_reported=false;for(auto& rows:terrain_authored_rows)rows.clear();}
struct DeferredLocalView {
    LocalView view;GestureScope scope;uint64_t generation=0;void* mode=nullptr;void* owner=nullptr;
    void* resource=nullptr;editor::ResourceKey source;
};
std::optional<DeferredLocalView> deferred_local_view;
bool local_view_queued=false,local_view_settling=false;
editor_native_complexity::Bounds complexity_bounds;
bool complexity_available=false;
uintptr_t a_complexity_ratio,a_complexity_count,a_complexity_weights,a_complexity_max,a_complexity_create,a_complexity_clone;
std::array<uint8_t,editor_native_complexity::kFormulaCode.size()> complexity_installed_code{};
constexpr auto complexity_count_code=editor_native_complexity::detail::unhex(
    "8b4c2404568b74240c33c03bf17426908bd181c138020000894c24088b12c1ea1ef6c201750a8b11c1ea1ff6c20175e0403bf175db5ec3");
bool complexity_code_pinned=false;
uint64_t complexity_conflicts=0;
std::string complexity_conflict;
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
bool read_complexity_capability(editor::ComplexityCapability& out,std::string& error){
    out={};
    const auto reject=[&](editor::ComplexityReason reason,const char* message){out={};out.reason=reason;error=message;return false;};
    if(!complexity_available || !complexity_code_pinned)
        return reject(editor::ComplexityReason::Unavailable,"The exact native Extended Complexity adapter is unavailable");
    // No native calls or callbacks occur inside this bounded global read. The
    // same proof remains available while the host owns a creation editor.
    skg::ReadOnlyMemoryScope memory;
    const auto code_current=[&]{
        return a_complexity_ratio && a_complexity_count && a_complexity_create && a_complexity_clone &&
            skg::readable(reinterpret_cast<void*>(a_complexity_ratio),complexity_installed_code.size()) &&
            skg::readable(reinterpret_cast<void*>(a_complexity_count),complexity_count_code.size()) &&
            skg::readable(reinterpret_cast<void*>(a_complexity_create),11) && skg::readable(reinterpret_cast<void*>(a_complexity_clone),11) &&
            !std::memcmp(reinterpret_cast<void*>(a_complexity_ratio),complexity_installed_code.data(),complexity_installed_code.size()) &&
            !std::memcmp(reinterpret_cast<void*>(a_complexity_count),complexity_count_code.data(),complexity_count_code.size()) && complexity_bounds.valid();
    };
    if(!code_current())return reject(editor::ComplexityReason::CodeConflict,"Native complexity code or placement bounds conflict with another patch");
    if(!a_complexity_weights || !a_complexity_max || !skg::readable(reinterpret_cast<void*>(a_complexity_weights),11*16+4) ||
       !skg::readable(reinterpret_cast<void*>(a_complexity_max),4))
        return reject(editor::ComplexityReason::Unavailable,"Native complexity tuning is unavailable");
    editor::ComplexityCapability value;value.available=true;value.reason=editor::ComplexityReason::None;
    const auto read_words=[&]{
        std::array<uint32_t,13> words{};
        for(size_t i=0;i<12;++i)std::memcpy(&words[i],reinterpret_cast<void*>(a_complexity_weights+16*i),4);
        std::memcpy(&words[12],reinterpret_cast<void*>(a_complexity_max),4);return words;
    };
    value.words=read_words();
    if(!editor::valid_complexity(value))return reject(editor::ComplexityReason::InvalidTuning,"Native complexity tuning is invalid");
    if(!code_current() || read_words()!=value.words)
        return reject(editor::ComplexityReason::Changed,"Native complexity evidence changed during capture");
    out=value;error.clear();return true;
}
float SK_THISCALL complexity_ratio(void* self,const uint32_t* counts);
bool update_complexity(std::string& error){
    const bool requested=extended_complexity();
    if(requested){
        editor::ComplexityCapability evidence;
        if(!read_complexity_capability(evidence,error)){
            if(evidence.reason==editor::ComplexityReason::CodeConflict){++complexity_conflicts;complexity_conflict=error;return false;}
            // Runtime publishes/holds invalid or mismatching tuning. Keep the
            // disable operation possible; never enable new bounds on no proof.
            error.clear();return true;
        }
    }
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
    WorkTimer timer(native_work.reads);
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
    WorkTimer timer(native_work.writes);
    Stream stream{stream_vtable, {}, 0, 1, false, false};
    if (!skg::readable(p, kResourceSize) || !skg::thiscall<bool>(a_write, p, &stream) ||
        stream.failed || stream.bytes.empty()) {
        error = "Native editor snapshot serialization failed"; return false;
    }
    bytes = std::move(stream.bytes); return true;
}
bool prepare_terrain_load_input(void* owned,const std::vector<uint8_t>& serialized,
                                std::function<bool()> required_current,std::string& error){
    // Native F261B0 resolves the parsed PropertyList's parent before Build.
    // Our private Read has the saved parent key but no runtime parent pointer.
    // Resolve only that asserted key; never invent an absent exported field.
    constexpr uint32_t parent_property=0x00B2CCCB,prop_type=0x00B1B104;
    const auto pinned_generation=generation;
    auto* pinned_mode=mode();auto* pinned_data=data(pinned_mode);auto* pinned_resource=resource(pinned_data);
    if(!skg::readable(pinned_resource,kResourceSize) || !skg::readable(owned,kResourceSize)){
        error="The owned native terrain load has no current scene";return false;
    }
    const auto source=skg::field<editor::ResourceKey>(pinned_resource,8);const auto scope=value_scope();
    const auto current=[=]{
        try{return required_current && required_current() && value_scope()==scope && generation==pinned_generation &&
            mode()==pinned_mode && data(pinned_mode)==pinned_data && resource(pinned_data)==pinned_resource &&
            skg::readable(pinned_resource,kResourceSize) && skg::field<editor::ResourceKey>(pinned_resource,8)==source &&
            generation==pinned_generation;}catch(...){return false;}
    };
    const auto expired=[&]{error="The owned terrain parent resolution lost its scene or read identity";return false;};
    if(!current())return expired();
    editor_native_terrain::Properties expected;
    if(!editor_native_terrain::decode(serialized,expected,error))return false;
    auto* properties=skg::ptr_add<void>(owned,0x18);
    auto* parent=skg::field<void*>(properties,0x30);
    const auto asserted=expected.fields.find(parent_property);
    if(asserted==expected.fields.end()){
        if(parent){error="A parent-less native terrain input acquired a runtime parent";return false;}
        return current()?true:expired();
    }
    const auto& value=asserted->second;
    if(value.type!=0x20 || value.flags || value.value.size()!=16){error="The native terrain parent key has an unsupported shape";return false;}
    const editor::ResourceKey key{editor::terrain_word(value.value,0),editor::terrain_word(value.value,4),editor::terrain_word(value.value,8)};
    if(!key.instance || key.type!=prop_type){error="The native terrain parent is not a complete property resource key";return false;}
    auto resolved_key=key;if(!resolved_key.group)resolved_key.group=skg::field<uint32_t>(properties,0x10);
    const auto matches=[&](void* candidate){return skg::readable(candidate,0x34) && skg::field<editor::ResourceKey>(candidate,8)==resolved_key;};
    if(parent && !matches(parent)){error="The owned terrain input retained a different runtime parent";return false;}
    if(!parent && (!a_property_manager_get || !a_resolve_property_parents)){error="The verified native terrain parent resolver is unavailable";return false;}
    std::vector<uint8_t> before_bytes;
    if(!serialize(owned,before_bytes,error))return false;
    if(!current())return expired();
    editor_native_terrain::Properties before;
    if(!editor_native_terrain::decode(before_bytes,before,error))return false;
    const auto before_parent=before.fields.find(parent_property);
    if(before_parent==before.fields.end() || editor_native_terrain::property_value(before_parent->second)!=editor_native_terrain::property_value(value)){
        error="The parsed native terrain input changed its asserted parent";return false;
    }
    if(parent){
        if(!current())return expired();
        if(skg::field<void*>(properties,0x30)!=parent || !matches(parent)){
            error="The owned terrain input changed its resolved parent during serialization";return false;
        }
        error.clear();return true;
    }
    auto* manager=skg::cdecl_call<void*>(a_property_manager_get);
    if(!current())return expired();
    if(!manager || !skg::readable(manager,sizeof(void*))){error="The native property manager is unavailable";return false;}
    if(skg::field<void*>(properties,0x30)){error="The owned terrain parent changed before its resolver call";return false;}
    skg::thiscall<void>(a_resolve_property_parents,manager,properties);
    if(!current())return expired();
    parent=skg::field<void*>(properties,0x30);
    if(!matches(parent)){error="The native terrain parent could not be resolved to its asserted resource";return false;}
    std::vector<uint8_t> after_bytes;
    if(!serialize(owned,after_bytes,error))return false;
    if(!current())return expired();
    editor_native_terrain::Properties after;
    if(!editor_native_terrain::decode(after_bytes,after,error))return false;
    auto after_parent=after.fields.find(parent_property);
    if(after_parent==after.fields.end() || editor_native_terrain::property_value(after_parent->second)!=editor_native_terrain::property_value(value)){
        error="Resolving the native terrain parent changed its saved key or presence";return false;
    }
    // The scalar key's fourth word is verified unused storage. All other
    // property metadata/payloads and every byte of the DBPF body stay exact.
    auto before_compared=before_parent->second,after_compared=after_parent->second;
    std::fill(before_compared.value.begin()+12,before_compared.value.end(),0);
    std::fill(after_compared.value.begin()+12,after_compared.value.end(),0);
    before.fields[parent_property]=std::move(before_compared);after.fields[parent_property]=std::move(after_compared);
    if(before.header!=after.header || before.fields!=after.fields || before.tail!=after.tail){
        error="Native parent resolution changed an unrelated owned scenario field";return false;
    }
    if(!current())return expired();
    if(skg::field<void*>(properties,0x30)!=parent || !matches(parent)){
        error="The resolved native terrain parent changed during verification";return false;
    }
    error.clear();return true;
}

// These owners exist only inside the outer synchronous ValidationScope. No
// native address is stored in a Snapshot or handed to a worker. Only the
// explicitly read-only consumers below may borrow one; mutations keep using
// their own constructed/Read resource.
size_t parsed_owner_count=0,parsed_owner_bytes=0;
struct ParsedImage {
    std::vector<uint8_t> bytes;
    editor::ResourceKey source;
    GestureScope scope;
    uint64_t generation=0;
    void* resource=nullptr;
    ~ParsedImage(){
        if(resource){release(resource);--parsed_owner_count;parsed_owner_bytes-=bytes.size();}
    }
};
std::deque<std::shared_ptr<const ParsedImage>> parsed_images;
size_t parsed_image_bytes=0;
struct ReadImage {
    std::shared_ptr<const ParsedImage> retained;
    Owned owned;
    void* p=nullptr;
    void* get()const{return p;}
};
bool read_image(const Snapshot& image,ReadImage& out,std::string& error){
    const auto scope=value_scope();
    if(validation_depth)for(auto at=parsed_images.rbegin();at!=parsed_images.rend();++at){
        const auto& found=**at;
        if(found.generation==generation && found.scope==scope && found.source==image.source && found.bytes==image.bytes){
            out.retained=*at;out.p=found.resource;++native_work.parsed_hits;return true;
        }
    }
    ++native_work.parsed_misses;
    const auto pinned_generation=generation;
    Owned parsed(parse(image.bytes,error));if(!parsed.p)return false;
    if(generation!=pinned_generation || value_scope()!=scope || generation!=pinned_generation){
        error="The immutable native image belongs to an expired editor";return false;
    }
    // Native Read omits the external ScenarioResource identity at +8.
    skg::field<editor::ResourceKey>(parsed.p,8)=image.source;
    constexpr size_t limit=16u<<20;
    if(validation_depth && image.bytes.size()<=limit){
        try{
            // A borrowed owner may outlive its deque entry during reentry.
            // Count its native lifetime, not just currently indexed entries.
            for(auto at=parsed_images.begin();at!=parsed_images.end() &&
                (parsed_owner_count>=2 || parsed_owner_bytes>limit-image.bytes.size());){
                if(at->use_count()==1){parsed_image_bytes-=(*at)->bytes.size();at=parsed_images.erase(at);}
                else ++at;
            }
            if(parsed_owner_count>=2 || parsed_owner_bytes>limit-image.bytes.size()){
                out.owned.p=std::exchange(parsed.p,nullptr);out.p=out.owned.p;return true;
            }
            auto entry=std::make_shared<ParsedImage>();entry->bytes=image.bytes;entry->source=image.source;
            entry->scope=scope;entry->generation=pinned_generation;
            while(!parsed_images.empty()&&(parsed_images.size()>=2 || parsed_image_bytes>limit-image.bytes.size())){
                parsed_image_bytes-=parsed_images.front()->bytes.size();parsed_images.pop_front();
            }
            parsed_images.push_back(entry);parsed_image_bytes+=image.bytes.size();
            entry->resource=std::exchange(parsed.p,nullptr);++parsed_owner_count;parsed_owner_bytes+=image.bytes.size();
            native_work.parsed_peak_owners=std::max(native_work.parsed_peak_owners,parsed_owner_count);
            native_work.parsed_peak_bytes=std::max(native_work.parsed_peak_bytes,parsed_owner_bytes);
            out.p=entry->resource;out.retained=std::move(entry);return true;
        }catch(const std::bad_alloc&){} // Reuse is optional; the owned Read still succeeds.
    }
    out.owned.p=std::exchange(parsed.p,nullptr);out.p=out.owned.p;return true;
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
bool act_roster_fingerprint(const Rows& acts,editor_native_act_cache::Key& key,std::string& error){
    Owned fragment(construct());if(!fragment.p){error="Cannot allocate the owned act fingerprint";return false;}
    normalize_fragment(fragment.p);
    const std::array<uintptr_t,3> slice{uintptr_t(acts.begin),uintptr_t(acts.end),uintptr_t(acts.end)};
    // CopyActs owns every nested text/goal allocation. Native Write witnesses
    // the entire ordered roster, including opaque and in-place changes. Goal
    // reference metadata is derived only from these bytes by act_block; it
    // does not resolve server assets or consult the current class roster.
    skg::thiscall<void*>(a_copy_acts,skg::ptr_add<void>(fragment.p,0x23C),slice.data());
    return serialize(fragment.p,key.bytes,error);
}
bool project_authoring(void* r,editor::Document& document,std::string& error,const editor::Document& identity){
    document.settings_schema=authoring_property_schema();
    document.next_id=std::max<uint64_t>(document.next_id,9ull<<32);
    Rows acts;if(!rows(r,0x23C,0x534,8,acts)){error="Invalid authored act collection";return false;}
    std::vector<editor_native_act_cache::Identity> identities;
    uint32_t index=0;
    for(auto act=acts.begin;act!=acts.end;act+=0x534,++index){
        editor_native_act_cache::Identity entry{{(3ull<<32)|index,1},act_settings_id(index)};
        if(!identity.act_order.empty()){
            if(index>=identity.act_order.size()){error="The native act has no stable identity";return false;}
            entry.act=identity.act_order[index];
            auto found=std::find_if(identity.settings.begin(),identity.settings.end(),[&](const auto& row){return row.second.kind==editor::SettingsKind::Act && row.second.act_id==entry.act;});
            if(found==identity.settings.end()){error="The act settings identity is missing";return false;}
            entry.settings=found->first;
        }
        identities.push_back(entry);
    }
    std::optional<editor_native_act_cache::Key> cache_key;
    std::shared_ptr<const editor_native_act_cache::Acts> cached;
    // This extra owning fingerprint is an optimization. Its allocation or
    // serialization failure must fall back to the original per-act checks.
    if(!identities.empty())try{
        editor_native_act_cache::Key key;key.context=act_projection_cache.context();
        key.project=identity.project;key.epoch=identity.epoch;key.source=skg::field<editor::ResourceKey>(r,8);
        key.identities=identities;key.schema=document.settings_schema;
        std::string fingerprint_error;
        if(act_roster_fingerprint(acts,key,fingerprint_error)){
            cached=act_projection_cache.get(key);cache_key=std::move(key);
        }
    }catch(const std::bad_alloc&){cache_key.reset();cached.reset();}
    editor_native_act_cache::Acts fresh;
    for(size_t i=0;i<identities.size();++i){
        editor::AuthoringSettings settings;settings.object=identities[i].settings;settings.act_id=identities[i].act;
        document.act_order.push_back(settings.act_id);document.acts.insert(settings.act_id);
        document.next_id=std::max(document.next_id,std::max(settings.object.id,settings.act_id.id)+1);
        editor::StructuredValue block;
        if(cached)block=(*cached)[i];
        else{
            if(!act_block(acts.begin+i*0x534,block,error))return false;
            if(cache_key)try{fresh.push_back(block);}catch(const std::bad_alloc&){cache_key.reset();fresh.clear();}
        }
        settings.properties[kActBlockProperty]=std::move(block);document.settings.emplace(settings.object,std::move(settings));
    }
    if(!cached && cache_key)act_projection_cache.put(std::move(*cache_key),fresh);
    editor::AuthoringSettings captain;captain.object=kCaptainObject;captain.kind=editor::SettingsKind::Captain;
    auto key=skg::field<editor::ResourceKey>(r,0x130);
    const auto server=skg::field<uint64_t>(r,0x140);
    if(server && server!=UINT64_MAX){
        if(!resolve_asset(server,key)){error="The authored captain dependency is unavailable";return false;}
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
                editor::ResourceKey key;
                if(!resolve_asset(server,key) || !key.instance){error="An adventure crew asset is unavailable";return false;}
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
bool crew_primary_asset(void* row,editor::ResourceKey& key,std::string& error){
    key=skg::field<editor::ResourceKey>(row,0);const auto server=skg::field<uint64_t>(row,0x10);
    if(server && server!=UINT64_MAX){
        if(!resolve_asset(server,key) || !key.instance){error="An adventure crew asset is unavailable";return false;}
    }
    return true;
}
bool canonical_crew(const editor::Document& document,Owned& fragment,Rows& crew,std::string& error){
    const auto found=document.settings.find(kCrewObject);
    if(!supported || !a_copy_crew || found==document.settings.end()){
        error="The canonical adventure crew roster is unavailable";return false;
    }
    const auto& settings=found->second;const auto roster=settings.properties.find(kCrewRosterProperty),capacity=settings.properties.find(kCrewCapacityProperty);
    const auto schema=authoring_property_schema();
    for(auto property:{kCrewRosterProperty,kCrewCapacityProperty}){
        auto advertised=document.settings_schema.find(property);
        if(advertised==document.settings_schema.end() || advertised->second!=schema.at(property)){
            error="The advertised adventure crew schema is invalid";return false;
        }
    }
    const auto* block=roster==settings.properties.end()?nullptr:std::get_if<editor::StructuredValue>(&roster->second);
    const auto* count=capacity==settings.properties.end()?nullptr:std::get_if<int64_t>(&capacity->second);
    if(settings.object!=kCrewObject || settings.kind!=editor::SettingsKind::Crew || settings.act_id!=editor::ObjectId{} || settings.properties.size()!=2 ||
       !block || block->format!=kCrewRosterFormat || !block->references.empty() || block->bytes.empty() || block->bytes.size()>editor::Limits{}.structured_bytes ||
       !count || *count<0 || *count>3){error="The canonical adventure crew settings are invalid";return false;}
    fragment.p=parse(block->bytes,error);
    if(!fragment.p || !rows(fragment.p,0x178,0x38,3,crew) || (crew.end-crew.begin)/0x38>*count){
        if(error.empty())error="The canonical adventure crew exceeds its capacity";return false;
    }
    editor::Document projected,identity;if(!project_authoring(fragment.p,projected,error,identity))return false;
    auto canonical=projected.settings.find(kCrewObject);
    if(canonical==projected.settings.end() || canonical->second.properties.at(kCrewRosterProperty)!=editor::Value{*block}){
        error="The adventure crew subtree is not canonical";return false;
    }
    return true;
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
    std::function<bool()> current;
    bool current_owner()const{try{return !current || current();}catch(...){return false;}}
    MarkerAudio(void* candidate_owner,void* marker,std::function<bool()> guard={}):current(std::move(guard)){
        if(!current_owner() || !active() || !tracks_extended_poses() || candidate_owner!=data(mode()) || !current_owner())return;
        auto* r=resource(candidate_owner);Rows map;
        if(!rows(r,0x2C10,0x238,kPlacementLimit,map))return;
        const auto begin=reinterpret_cast<uintptr_t>(map.begin),end=reinterpret_cast<uintptr_t>(map.end),p=reinterpret_cast<uintptr_t>(marker);
        if(p<begin+4 || p>=end || (p-begin-4)%0x238 || !live_slot(static_cast<uint8_t*>(marker)-4))return;
        owner=candidate_owner;resource_pin=r;generation_pin=generation;slot=uint32_t((p-begin-4)/0x238);
        class_index=skg::field<uint32_t>(marker,0);pitch=skg::field<float>(marker,0x1E0);gain=skg::field<float>(marker,0x1E4);
    }
    ~MarkerAudio(){
        if(!current_owner() || !owner || owner!=data(mode()) || resource(owner)!=resource_pin || generation!=generation_pin)return;
        Rows map;if(!rows(resource_pin,0x2C10,0x238,kPlacementLimit,map) || slot>=uint32_t((map.end-map.begin)/0x238))return;
        auto* marker=map.begin+slot*0x238+4;
        if(!live_slot(marker-4) || skg::field<uint32_t>(marker,0)!=class_index)return;
        if(!current_owner())return;
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
            editor::ResourceKey key;
            if(!resolve_asset(server,key) || !key.instance){error="A cast dependency is not installed locally";return false;}
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
        if(!resolve_asset(server,key) || !key.instance){error="The primary cast dependency is unavailable";return false;}
    }
    return true;
}
bool asset_witnesses(void* resource,const editor::Document& document,std::vector<ResolvedAssetWitness>& out,std::string& error){
    std::vector<ResolvedAssetWitness> found;
    auto witness=[&](uint64_t server){
        if(!server || server==UINT64_MAX)return true;
        editor::ResourceKey key;
        if(!resolve_asset(server,key)){
            error="A native asset resolution witness is unavailable";return false;
        }
        found.push_back({server,key});return true;
    };
    if(!document.class_schema.empty()){
        Rows classes;if(!rows(resource,0x2BF4,0x27E8,1024,classes)){error="The native cast witness is invalid";return false;}
        for(auto row=classes.begin;row!=classes.end;row+=0x27E8)
            for(auto offset:{0u,0x28u,0x50u})if(!witness(skg::field<uint64_t>(row+8+offset,0x10)))return false;
    }
    if(document.settings.contains(kCaptainObject) && !witness(skg::field<uint64_t>(resource,0x140)))return false;
    if(document.settings.contains(kCrewObject)){
        Rows crew;if(!rows(resource,0x178,0x38,3,crew)){error="The native crew witness is invalid";return false;}
        for(auto row=crew.begin;row!=crew.end;row+=0x38)if(!witness(skg::field<uint64_t>(row,0x10)))return false;
    }
    out=std::move(found);return true;
}
bool resolved_asset_values(void* resource,const editor::Document& document,std::vector<editor::ResourceKey>& out,std::string& error){
    std::vector<editor::ResourceKey> values;
    auto value=[&](void* native){editor::ResourceKey key;if(!class_primary_asset(native,key,error))return false;values.push_back(key);return true;};
    if(!document.class_schema.empty()){
        Rows classes;if(!rows(resource,0x2BF4,0x27E8,1024,classes))return false;
        for(auto row=classes.begin;row!=classes.end;row+=0x27E8)
            for(auto offset:{0u,0x28u,0x50u})if(!value(row+8+offset))return false;
    }
    if(document.settings.contains(kCaptainObject)&&!value(skg::ptr_add<void>(resource,0x130)))return false;
    if(document.settings.contains(kCrewObject)){
        Rows crew;if(!rows(resource,0x178,0x38,3,crew))return false;
        for(auto row=crew.begin;row!=crew.end;row+=0x38)if(!value(row))return false;
    }
    out=std::move(values);return true;
}
bool current_asset_witnesses(const std::vector<ResolvedAssetWitness>& assets){
    if(assets.empty())return true;
    for(const auto& asset:assets){
        editor::ResourceKey current;
        if(!resolve_asset(asset.server,current) || current!=asset.key)return false;
    }
    return true;
}
bool same_projection_image(const Snapshot& a,const Snapshot& b){
    return a.act==b.act && a.source==b.source && a.bytes==b.bytes && a.bindings==b.bindings &&
        a.captain==b.captain && a.captain_locked==b.captain_locked && a.metadata==b.metadata;
}
void remember_projection(const Snapshot& image,const std::vector<ResolvedAssetWitness>& assets,const GestureScope& scope,uint64_t native_generation){
    // Only this private producer can grant provenance. All memory is owned;
    // native owners, parsed resources and mutable vectors never escape here.
    try{
        constexpr size_t limit=32u<<20;
        auto charged=image.bytes.size()+editor::bytes(image.document)+image.bindings.size()*sizeof(Binding)+sizeof(ProjectedSnapshot);
        if(charged>limit)return;
        charged+=assets.size()*sizeof(ResolvedAssetWitness);
        if(image.metadata)for(const auto& text:image.metadata->text)charged+=text.size()*sizeof(char16_t);
        if(charged>limit)return;
        if(native_generation!=generation || value_scope()!=scope || native_generation!=generation)return;
        for(auto at=projected_snapshots.begin();at!=projected_snapshots.end();++at)
            if((*at)->generation==generation && (*at)->scope==scope && same_projection_image((*at)->image,image)){
                projected_snapshot_bytes-=(*at)->cost;projected_snapshots.erase(at);break;
            }
        while(!projected_snapshots.empty() && (projected_snapshots.size()>=4 || projected_snapshot_bytes>limit-charged)){
            projected_snapshot_bytes-=projected_snapshots.front()->cost;projected_snapshots.pop_front();
        }
        auto immutable=std::make_shared<const ProjectedSnapshot>(ProjectedSnapshot{image,scope,native_generation,assets,charged});
        projected_snapshots.push_back(std::move(immutable));projected_snapshot_bytes+=charged;
    }catch(const std::bad_alloc&){} // Optional reuse cannot reject a valid projection.
}
std::shared_ptr<const ProjectedSnapshot> projected_snapshot(const Snapshot& image,std::string& error){
    const auto scope=value_scope();
    std::shared_ptr<const ProjectedSnapshot> retained;
    if(borrowed_projections)for(const auto& candidate:*borrowed_projections)
        if(candidate && candidate->generation==generation && candidate->scope==scope && same_projection_image(candidate->image,image)){
            retained=candidate;break;
        }
    if(!retained)for(auto at=projected_snapshots.rbegin();at!=projected_snapshots.rend();++at){
        if((*at)->generation==generation && (*at)->scope==scope && same_projection_image((*at)->image,image)){
            retained=*at;break;
        }
    }
    // Resolver callbacks cannot invalidate the retained owning proof or leave
    // an iterator into a cache cleared by a new native context.
    if(retained && current_asset_witnesses(retained->assets) &&
       value_scope()==scope && retained->generation==generation){
        ++projected_snapshot_hits;error.clear();return retained;
    }
    ++projected_snapshot_misses;return {};
}
void retain_projection(const std::shared_ptr<const ProjectedSnapshot>& proof){
    if(!proof || proof->generation!=generation || proof->cost>(32u<<20))return;
    try{
        for(const auto& existing:projected_snapshots)if(existing==proof)return;
        while(!projected_snapshots.empty() && (projected_snapshots.size()>=4 || projected_snapshot_bytes>(32u<<20)-proof->cost)){
            projected_snapshot_bytes-=projected_snapshots.front()->cost;projected_snapshots.pop_front();
        }
        projected_snapshots.push_back(proof);projected_snapshot_bytes+=proof->cost;
    }catch(const std::bad_alloc&){} // Only value-cache reuse is optional.
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

struct ComplexityPool {
    uintptr_t begin=0,end=0,capacity=0,first=0;
    uint32_t initial=0;
    std::vector<uint32_t> headers;
    std::optional<uint32_t> sentinel;
    bool operator==(const ComplexityPool&)const=default;
};
bool complexity_pool(void* resource,ComplexityPool& out,uint32_t& live){
    Rows rows;if(!editor_native::rows(resource,0x2C10,0x238,kPlacementLimit,rows))return false;
    ComplexityPool value;value.begin=uintptr_t(rows.begin);value.end=uintptr_t(rows.end);
    value.capacity=skg::field<uintptr_t>(resource,0x2C18);value.initial=skg::field<uint32_t>(resource,0x2C24);
    const auto size=size_t((value.end-value.begin)/0x238);
    if(value.capacity<value.end || (value.capacity-value.begin)%0x238)return false;
    if(value.initial<0x3FFFFFFFu){if(value.initial>size)return false;value.first=value.begin+size_t(value.initial)*0x238;}
    else value.first=value.end;
    value.headers.reserve(size);
    for(size_t i=0;i<size;++i)value.headers.push_back(skg::field<uint32_t>(reinterpret_cast<void*>(value.begin+i*0x238),0));
    if(value.capacity-value.end>=4 && skg::readable(reinterpret_cast<void*>(value.end),4))
        value.sentinel=skg::field<uint32_t>(reinterpret_cast<void*>(value.end),0);
    // Mirror the verified iterator, including its bit30 shortcut and next-row
    // bit31 test. Occupied-marker count is not a naive pool-slot bit count.
    size_t index=size_t((value.first-value.begin)/0x238),steps=0;live=0;
    while(index<size){
        for(;;){
            if(index>=size || ++steps>size+1)return false;
            const auto old=value.headers[index++];if(old&0x40000000u)break;
            const auto next=index<size?std::optional<uint32_t>{value.headers[index]}:value.sentinel;
            if(!next)return false;if(!(*next&0x80000000u))break;
        }
        ++live;if(index>size)return false;
    }
    out=std::move(value);return true;
}
float SK_THISCALL complexity_ratio(void* self,const uint32_t* counts){
    // Every ordinary/native-default query keeps the original result exactly.
    auto* m=mode();auto* owner=data(m);
    if(self!=owner || !editor::extended_complexity(act_identity) || !skg::readable(m,0xDC) ||
       skg::field<int>(m,0xCC)!=1 || skg::field<int>(m,0xD0)!=0)return original_complexity(self,counts);
    const auto fail=[&](const char* message)noexcept{
        ++complexity_conflicts;
        try{complexity_conflict=message;}catch(...){complexity_conflict.clear();}
        return 1.0f;
    };
    try{
        const auto active_callback=C.active;
        const auto projection=C.projection_scope?C.projection_scope:C.gesture_scope;
        const auto cohort=C.complexity_current;
        // Pin the actual caller before game-mode/active/read-scope callbacks;
        // none may authorize old counts against a newly sampled resource.
        const auto native_generation=generation;auto* r=resource(owner);
        if(!skg::readable(r,kResourceSize) || !skg::readable(counts,12*sizeof(uint32_t)))
            return fail("Extended complexity query lost its original resource or counts");
        std::array<uint32_t,12> values{};std::memcpy(values.data(),counts,sizeof values);
        const auto source=skg::field<editor::ResourceKey>(r,8);
        const auto raw_current=[&]{
            skg::ReadOnlyMemoryScope memory;
            return generation==native_generation && mode()==m && data(m)==owner && resource(owner)==r &&
                skg::readable(m,0xDC) && skg::field<int>(m,0xCC)==1 && skg::field<int>(m,0xD0)==0 &&
                skg::readable(r,kResourceSize) && skg::field<editor::ResourceKey>(r,8)==source;
        };
        const bool editor_ready=editing();
        if(!raw_current())return fail("Extended complexity query changed its original editor");
        const bool shared=editor_ready && active_callback && active_callback();
        if(!raw_current())return fail("Extended complexity query changed its original editor");
        if(!shared)return original_complexity(self,counts);
        const auto scope=projection?projection():GestureScope{};
        if(!raw_current() || !valid_scope(scope) || !skg::readable(counts,12*sizeof(uint32_t)))
            return fail("Extended complexity query lost its original editor");
        const auto current=[&]{
            if(!projection || projection()!=scope || generation!=native_generation || !editor::extended_complexity(act_identity))return false;
            return raw_current();
        };
        editor::ComplexityCapability before,after;std::string error;
        ComplexityPool pool;uint32_t counted=0;
        if(!read_complexity_capability(before,error) || !current() || !complexity_pool(r,pool,counted))return fail("Extended complexity evidence is unavailable or conflicting");
        const auto unchanged=[&]{
            // Cohort and scope callbacks precede the final callback-free input
            // closure. Neither may mutate a count/tuning value already sampled
            // and have that old sample accepted after returning.
            if(!current() || !cohort || !cohort(before) || !current())return false;
            if(!skg::readable(counts,sizeof values) || std::memcmp(values.data(),counts,sizeof values))return false;
            ComplexityPool now;uint32_t number=0;
            return complexity_pool(r,now,number) && pool==now && counted==number &&
                read_complexity_capability(after,error) && before==after && raw_current();
        };
        if(!unchanged())return fail("Extended complexity has no matching current participant tuning proof");
        const auto native_count=skg::cdecl_call<uint32_t>(a_complexity_count,reinterpret_cast<void*>(pool.first),reinterpret_cast<void*>(pool.end));
        if(native_count!=counted || !unchanged())return fail("Native complexity pool changed during counting");
        const auto ratio=original_complexity(self,counts);
        if(!unchanged())return fail("Native complexity inputs changed during the original query");
        editor_native_complexity::Tuning tuning;
        for(size_t i=0;i<12;++i)tuning.weights[i]=std::bit_cast<float>(before.words[i]);
        tuning.maximum=std::bit_cast<float>(before.words[12]);float extended=1;
        if(!editor_native_complexity::extended_ratio(ratio,values,counted,tuning,extended))
            return fail("Native complexity formula no longer matches its verified inputs");
        complexity_conflict.clear();return extended;
    }catch(...){return fail("Extended complexity query failed its owned evidence checks");}
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
    WorkTimer timer(native_work.projections);
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
        // Native projections carry authored values and stable identities, not
        // session revisions. Terrain identity matching preserves its logical
        // IDs/order, but must not copy a canonical record revision into this
        // fresh revision-zero document. Runtime supplies its guarded canonical
        // record revision separately when constructing a terrain mutation.
        for(auto& [id,record]:out.document.terrain){(void)id;record.revision=0;}
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
        if(!resolve_asset(server,actual)){
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
RecentMatches reuse_resource_projection(void* resource,Snapshot& out,const std::map<uint32_t,editor::ObjectId>* supplied,
                               const editor::Document* identity,std::string& error){
    if(projected_snapshots.empty())return RecentMatches::Different;
    const auto scope=value_scope();
    const auto pinned_generation=generation;auto* pinned_mode=mode();auto* pinned_data=data(pinned_mode);
    const bool live_input=editor_native::resource(pinned_data)==resource;
    auto consistent=[&]{
        if(generation!=pinned_generation || value_scope()!=scope || generation!=pinned_generation)return false;
        return !live_input || (mode()==pinned_mode && data(pinned_mode)==pinned_data &&
            editor_native::resource(pinned_data)==resource && skg::readable(resource,kResourceSize) &&
            skg::field<editor::ResourceKey>(resource,8)==out.source);
    };
    const auto& logical=identity?*identity:act_identity;
    Rows markers;if(!rows(resource,0x2C10,0x238,kPlacementLimit,markers))return RecentMatches::Different;
    std::vector<Binding> live,compact;
    for(auto marker=markers.begin;marker!=markers.end;marker+=0x238)if(live_slot(marker)){
        const auto key=slot_index(markers,marker);if(supplied && !supplied->contains(key))return RecentMatches::Different;
        live.push_back({marker_id(key,supplied),key});
    }
    if(!serialized_bindings(live,compact,error))return RecentMatches::Different;
    const auto schema=tracks_extended_poses()?(tracks_relative_scale()?placement_property_schema():legacy_extended_schema()):
        tracks_relative_scale()?relative_scale_schema():std::map<uint32_t,editor::PropertySpec>{};
    std::shared_ptr<const ProjectedSnapshot> proof;
    for(auto at=projected_snapshots.rbegin();at!=projected_snapshots.rend();++at){
        const auto& candidate=**at;const auto& image=candidate.image;const auto& doc=image.document;
        if(candidate.generation!=generation || candidate.scope!=scope || image.act!=out.act || image.source!=out.source ||
           image.bytes!=out.bytes || image.bindings!=compact || doc.schema!=schema ||
           (!doc.class_schema.empty())!=tracks_cast() || (!doc.settings_schema.empty())!=tracks_authoring())continue;
        if(tracks_authoring()){
            if(!logical.act_order.empty() && doc.act_order!=logical.act_order)continue;
            bool same_act_ids=true;
            for(size_t i=0;i<doc.act_order.size();++i){
                const auto act=logical.act_order.empty()?editor::ObjectId{(3ull<<32)|i,1}:logical.act_order[i];
                auto settings=act_settings_id(uint32_t(i));
                if(!logical.act_order.empty()){
                    auto row=std::find_if(logical.settings.begin(),logical.settings.end(),[&](const auto& entry){
                        return entry.second.kind==editor::SettingsKind::Act && entry.second.act_id==act;});
                    if(row==logical.settings.end()){same_act_ids=false;break;}
                    settings=row->first;
                }
                auto projected=doc.settings.find(settings);
                if(doc.act_order[i]!=act || projected==doc.settings.end() || projected->second.kind!=editor::SettingsKind::Act ||
                   projected->second.act_id!=act){same_act_ids=false;break;}
            }
            if(!same_act_ids)continue;
        }
        if(tracks_cast() && doc.next_class!=skg::field<uint32_t>(resource,0x2C0C))continue;
        auto old_project=doc.settings.find(kProjectObject),new_project=logical.settings.find(kProjectObject);
        if((old_project==doc.settings.end())!=(new_project==logical.settings.end()) ||
           (old_project!=doc.settings.end() && old_project->second.properties!=new_project->second.properties))continue;
        if(doc.terrain.size()!=logical.terrain.size())continue;
        bool same_terrain_ids=true;
        for(const auto& [id,record]:doc.terrain){
            auto current=logical.terrain.find(id);
            if(current==logical.terrain.end() || record.order!=current->second.order || record.kind!=current->second.kind ||
               record.payload!=current->second.payload){same_terrain_ids=false;break;}
        }
        if(!same_terrain_ids)continue;
        proof=*at;break;
    }
    if(!proof)return RecentMatches::Different;
    const bool assets=current_asset_witnesses(proof->assets);
    if(!consistent()){error="The native projection owner changed during asset resolution";return RecentMatches::Error;}
    if(!assets)return RecentMatches::Different;
    const bool captain=captain_sidecar_matches(resource,proof->image,proof->image.document,error);
    if(!consistent()){error="The native projection owner changed during captain resolution";return RecentMatches::Error;}
    if(!captain)return RecentMatches::Different;
    try{out.document=proof->image.document;out.bindings=std::move(compact);}
    catch(const std::bad_alloc&){return RecentMatches::Different;}
    out.captain=proof->image.captain;out.captain_locked=proof->image.captain_locked;
    ++projected_snapshot_hits;error.clear();return RecentMatches::Match;
}
// This ticket exists only between a checked native terrain-before boundary and
// its synchronous gesture delivery. It owns values, never native resources.
// A later authored rotation cannot borrow an earlier grounding result.
struct TerrainCaptureRotations {
    using Row=editor_native_terrain_rotations::Row;
    uint64_t serial=0,generation=0;
    GestureScope scope;
    void* mode=nullptr;void* data=nullptr;void* resource=nullptr;void* tool=nullptr;
    void* context=nullptr;uint32_t act=0;
    editor::ResourceKey source;
    uintptr_t first=0,last=0;
    std::vector<Row> canonical,rendered;
    std::vector<uint32_t> native_keys;
    editor_native_terrain_rotations::Witness values;
    bool raw_current()const{
        try{
            if(serial!=terrain_rotation_serial || editor_native::generation!=generation)return false;
            // Cache only scalar probes, never an identity/native callback.
            skg::ReadOnlyMemoryScope memory;
            auto* m=editor_native::mode();auto* d=editor_native::data(m);auto* r=editor_native::resource(d);
            auto* c=a_context?*reinterpret_cast<void**>(a_context):nullptr;
            Rows markers;
            return m==mode && d==data && r==resource && terraform_mode()==tool && c==context &&
                skg::readable(c,4) && skg::field<uint32_t>(c,0)==act && skg::readable(m,0xDC) &&
                skg::field<int>(m,0xCC)==1 && skg::field<int>(m,0xD0)==0 && skg::readable(r,kResourceSize) &&
                skg::field<editor::ResourceKey>(r,8)==source && rows(r,0x2C10,0x238,kPlacementLimit,markers) &&
                uintptr_t(markers.begin)==first && uintptr_t(markers.end)==last && editor_native::generation==generation;
        }catch(...){return false;}
    }
    bool current()const{
        try{return valid_scope(scope) && value_scope()==scope && raw_current();}catch(...){return false;}
    }
    bool raw_rows(std::span<const Row> expected)const{
        skg::ReadOnlyMemoryScope memory;
        if(!raw_current())return false;
        Rows markers;if(!rows(resource,0x2C10,0x238,kPlacementLimit,markers))return false;
        size_t count=0;
        for(auto p=markers.begin;p!=markers.end;p+=0x238)if(live_slot(p)){
            if(count>=expected.size() || count>=native_keys.size())return false;
            const auto key=native_keys[count];const auto& row=expected[count++];const auto slot=slot_index(markers,p);
            if(row.object!=marker_id(slot) || row.slot!=slot || row.class_index!=skg::field<uint32_t>(p,4) ||
               key!=skg::field<uint32_t>(p,0x22C) ||
               !editor_native_terrain_rotations::exact(row.q,skg::field<editor::Rotation>(p,0x14)))return false;
        }
        return count==expected.size() && count==native_keys.size() && raw_current();
    }
    bool collect(std::vector<Row>& out,bool same_rendered,std::string& error)const{
        try{
        const auto expired=[&]{error="The native terrain rotation ticket lost its editor or placement identity";return false;};
        if(!current() || !C.object_revision)return expired();
        Rows markers;if(!rows(resource,0x2C10,0x238,kPlacementLimit,markers))return expired();
        out.clear();out.reserve(canonical.size());
        for(auto p=markers.begin;p!=markers.end;p+=0x238)if(live_slot(p)){
            const auto slot=slot_index(markers,p);const auto object=marker_id(slot);
            if(out.size()>=canonical.size() || out.size()>=native_keys.size())return expired();
            const auto& row=canonical[out.size()];if(row.object!=object)return expired();
            const auto revision=C.object_revision(object);
            if(!current() || !revision || *revision!=row.revision || row.slot!=slot ||
               skg::field<uint32_t>(p,4)!=row.class_index || skg::field<uint32_t>(p,0x22C)!=native_keys[out.size()])return expired();
            const auto q=skg::field<editor::Rotation>(p,0x14);
            if(!editor_native_terrain_rotations::finite(q))return expired();
            if(same_rendered){
                if(out.size()>=rendered.size() || !editor_native_terrain_rotations::identity(rendered[out.size()],row) ||
                   (!editor_native_terrain_rotations::exact(rendered[out.size()].q,q) &&
                    !editor_native_terrain_rotations::exact(row.q,q)))return expired();
            }
            out.push_back({object,slot,row.class_index,*revision,q});
        }
        // No callbacks follow this final primitive closure: even the last
        // identity query must not make an earlier q sample look current.
        if(out.size()!=canonical.size() || !current() || !raw_rows(out))return expired();
        return true;
        }catch(...){error="The native terrain rotation identity callback failed";return false;}
    }
};
bool begin_terrain_capture_rotations(const Snapshot& before,std::string& error){
    clear_terrain_capture_rotations();
    if(terrain_rotation_serial==UINT64_MAX){error="The native terrain rotation ticket sequence is exhausted";return false;}
    try{
        auto ticket=std::make_shared<TerrainCaptureRotations>();ticket->serial=terrain_rotation_serial;
        ticket->generation=generation;ticket->scope=value_scope();ticket->mode=mode();ticket->data=data(ticket->mode);
        ticket->resource=resource(ticket->data);ticket->tool=terraform_mode();
        ticket->context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
        if(!skg::readable(ticket->resource,kResourceSize) || !skg::readable(ticket->context,4) || !C.object_revision){
            error="The native terrain rotation before-image is unavailable";return false;
        }
        ticket->act=skg::field<uint32_t>(ticket->context,0);ticket->source=skg::field<editor::ResourceKey>(ticket->resource,8);
        Rows markers;if(!rows(ticket->resource,0x2C10,0x238,kPlacementLimit,markers))return false;
        ticket->first=uintptr_t(markers.begin);ticket->last=uintptr_t(markers.end);
        if(!ticket->current())return false;
        ticket->canonical.reserve(before.document.placements.size());
        ticket->native_keys.reserve(before.document.placements.size());
        for(auto p=markers.begin;p!=markers.end;p+=0x238)if(live_slot(p)){
            const auto slot=slot_index(markers,p);const auto object=marker_id(slot);
            auto wanted=before.document.placements.find(object);
            if(wanted==before.document.placements.end() || uint32_t(wanted->second.class_id.id)!=skg::field<uint32_t>(p,4)){
                error="The native terrain rotation before-image has different placement bindings";return false;
            }
            const auto revision=C.object_revision(object);
            if(!ticket->current() || !revision){error="The native terrain rotation before-image expired";return false;}
            ticket->canonical.push_back({object,slot,uint32_t(wanted->second.class_id.id),*revision,wanted->second.rotation});
            ticket->native_keys.push_back(skg::field<uint32_t>(p,0x22C));
        }
        if(ticket->canonical.size()!=before.document.placements.size() ||
           !ticket->collect(ticket->rendered,false,error) || !ticket->values.observe(ticket->rendered,ticket->rendered,ticket->canonical,error))return false;
        std::vector<TerrainCaptureRotations::Row> settled;
        if(!ticket->collect(settled,true,error))return false;
        terrain_capture_rotations=std::move(ticket);return true;
    }catch(const std::bad_alloc&){error="Cannot allocate bounded native terrain rotation witnesses";return false;}
    catch(...){error="The native terrain rotation before-image callback failed";return false;}
}
bool finish_terrain_capture_rebuild(const std::shared_ptr<const TerrainCaptureRotations>& before,
                                  const std::vector<TerrainCaptureRotations::Row>& original,std::string& error){
    if(!before || terrain_capture_rotations!=before || !before->current()){
        error="The native terrain rotation rebuild ticket expired";return false;
    }
    try{
        auto next=std::make_shared<TerrainCaptureRotations>(*before);
        if(!before->collect(next->rendered,false,error) ||
           !next->values.observe(original,next->rendered,next->canonical,error))return false;
        std::vector<TerrainCaptureRotations::Row> settled;
        if(!next->collect(settled,true,error) || terrain_capture_rotations!=before || !next->raw_current())return false;
        terrain_capture_rotations=std::move(next);return true;
    }catch(const std::bad_alloc&){error="Cannot retain bounded native terrain rotation witnesses";return false;}
}
bool terrain_capture_rotations_current(const std::shared_ptr<const TerrainCaptureRotations>& proof,std::string& error){
    if(!proof || terrain_capture_rotations!=proof || !proof->current()){
        error="The native terrain rotation capture ticket expired";return false;
    }
    std::vector<TerrainCaptureRotations::Row> rows;
    return proof->collect(rows,true,error);
}
bool normalize_terrain_capture_rotations(void* copied,const std::shared_ptr<const TerrainCaptureRotations>& proof,
        const std::map<uint32_t,editor::ObjectId>* supplied,std::string& error){
    if(!terrain_capture_rotations_current(proof,error))return false;
    if(!skg::readable(copied,kResourceSize) || skg::field<editor::ResourceKey>(copied,8)!=proof->source){
        error="The owned terrain rotation copy changed its native source";return false;
    }
    Rows markers;if(!rows(copied,0x2C10,0x238,kPlacementLimit,markers))return false;
    size_t count=0;
    for(auto p=markers.begin;p!=markers.end;p+=0x238)if(live_slot(p)){
        const auto slot=slot_index(markers,p);const auto object=marker_id(slot,supplied);
        if(count>=proof->canonical.size()){error="The owned terrain rotation copy added a placement";return false;}
        const auto& wanted=proof->canonical[count++];
        if(wanted.object!=object || wanted.slot!=slot || wanted.class_index!=skg::field<uint32_t>(p,4)){
            error="The owned terrain rotation copy changed its placement bindings";return false;
        }
        auto actual=wanted;actual.q=skg::field<editor::Rotation>(p,0x14);editor::Rotation authored;
        const auto result=proof->values.normalize(actual,wanted,authored);
        if(result==editor_native_terrain_rotations::Result::Stale){error="The owned terrain rotation copy has stale authored provenance";return false;}
        if(result==editor_native_terrain_rotations::Result::Match)skg::field<editor::Rotation>(p,0x14)=authored;
    }
    if(count!=proof->canonical.size() || !terrain_capture_rotations_current(proof,error))return false;
    return true;
}
bool capture_resource(void* r, uint32_t act, Snapshot& out, std::string& error,
                      const std::map<uint32_t,editor::ObjectId>* supplied=nullptr,const editor::Document* identity=nullptr,
                      bool boundary_owned=false) {
    const bool materialized_input=boundary_owned || r==resource(data(mode()));
    const auto rotations=materialized_input && terrain_capture_rotations && terrain_capture_rotations->values.size()?terrain_capture_rotations:nullptr;
    try{
    if(materialized_input && !terrain_capture_rotation_error.empty()){error=terrain_capture_rotation_error;return false;}
    void* original=r;std::vector<uint8_t> original_bytes;
    Owned authored;
    if(rotations){
        if(!skg::readable(r,kResourceSize) || skg::field<editor::ResourceKey>(r,8)!=rotations->source){
            error="The original terrain rotation image changed its native source";return false;
        }
        if(!terrain_capture_rotations_current(rotations,error) || !serialize(r,original_bytes,error) ||
           !terrain_capture_rotations_current(rotations,error))return false;
    }
    if(rotations || !terrain_grounding.empty()){
        authored.p=construct();if(!authored.p){error="Cannot allocate an owned grounding snapshot";return false;}
        skg::thiscall<void*>(a_copy,authored.p,r);r=authored.p;
        if(rotations){
            std::vector<uint8_t> copied;
            if(!terrain_capture_rotations_current(rotations,error) || !serialize(r,copied,error) ||
               !terrain_capture_rotations_current(rotations,error))return false;
            if(copied!=original_bytes){error="The owned terrain rotation copy changed unknown authored bytes";return false;}
            if(!normalize_terrain_capture_rotations(r,rotations,supplied,error))return false;
        }
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
    if (!serialize(r,out.bytes,error))return false;
    if(rotations && !terrain_capture_rotations_current(rotations,error))return false;
    if(materialized_input && !normalize_materialized_terrain(out.bytes,error))return false;
    const auto rotation_closure=[&]{
        if(!rotations)return true;
        std::vector<uint8_t> settled;
        if(!terrain_capture_rotations_current(rotations,error) || !serialize(original,settled,error) ||
           !terrain_capture_rotations_current(rotations,error))return false;
        if(settled!=original_bytes){error="The original terrain rotation image changed during owned capture";return false;}
        if(!serialize(r,settled,error) || !terrain_capture_rotations_current(rotations,error) ||
           !normalize_materialized_terrain(settled,error))return false;
        if(skg::field<editor::ResourceKey>(original,8)!=rotations->source || skg::field<editor::ResourceKey>(r,8)!=rotations->source){
            error="The native terrain rotation image source changed during owned capture";return false;
        }
        if(settled!=out.bytes){error="The owned terrain rotation projection changed its authored bytes";return false;}
        return terrain_capture_rotations_current(rotations,error);
    };
    const auto reused=reuse_resource_projection(r,out,supplied,identity,error);
    if(reused==RecentMatches::Match){if(!rotation_closure())return false;++captures;return true;}
    if(reused==RecentMatches::Error)return false;
    error.clear();
    if (!projection(r, out, error,supplied,std::nullopt,std::nullopt,std::nullopt,std::nullopt,identity,&out.bytes,terrain_recipe_replacement&&terrain_capturing)) return false;
    if(!hydrate_native_captain(r,out,error))return false;
    // Write omits free native pool slots. Read inserts its marker sequence into
    // a fresh pool, so artifact bindings describe serialized order, not live holes.
    std::vector<Binding> compact;
    if(!serialized_bindings(out.bindings,compact,error))return false;
    out.bindings=std::move(compact);
    if(!rotation_closure())return false;
    ++captures; return true;
    }catch(...){
        if(!rotations)throw;
        error="The owned native terrain rotation capture callback failed";return false;
    }
}

struct PoseCaptureProof {
    std::vector<ResolvedAssetWitness> assets;
    GestureScope scope;
    uint64_t generation=0;
};
// Borrow only the drain's private, immutable before-history resource, whose
// actual Native Write produced this image. A typed NativeRead projection alone
// cannot prove that Read preserved unknown authored bytes. No borrowed owner
// escapes this synchronous stack scope or becomes part of a snapshot/cache.
struct SerializedBeforeWitness {
    void* resource=nullptr;
    const std::map<uint32_t,editor::ObjectId>* bindings=nullptr;
    Snapshot image;
    GestureScope scope;
    uint64_t generation=0;
    mutable bool busy=false;
};
const SerializedBeforeWitness* serialized_before_witness=nullptr;
struct SerializedBeforeScope {
    SerializedBeforeWitness value;
    const SerializedBeforeWitness* previous=serialized_before_witness;
    SerializedBeforeScope(void* resource,const std::map<uint32_t,editor::ObjectId>& bindings,const Snapshot& image){
        try{
            if(image.bytes.size()>16u<<20)return;
            value.resource=resource;value.bindings=&bindings;value.scope=value_scope();value.generation=generation;
            value.image.bytes=image.bytes;value.image.source=image.source;value.image.bindings=image.bindings;value.image.act=image.act;
            value.image.captain=image.captain;value.image.captain_locked=image.captain_locked;value.image.metadata=image.metadata;
            serialized_before_witness=&value;
        }catch(const std::bad_alloc&){} // Optional proof; generic validation remains available.
    }
    SerializedBeforeScope(const SerializedBeforeScope&)=delete;
    SerializedBeforeScope& operator=(const SerializedBeforeScope&)=delete;
    ~SerializedBeforeScope(){serialized_before_witness=previous;}
};
bool only_existing_pose_fields(const editor::Document& before,const editor::Document& after){
    if(before.placements.size()!=after.placements.size())return false;
    auto expected=before;bool changed=false;
    for(const auto& [id,placement]:after.placements){
        auto found=expected.placements.find(id);if(found==expected.placements.end())return false;
        auto& prior=found->second;
        changed=changed || prior.position!=placement.position || prior.rotation!=placement.rotation ||
            prior.scale!=placement.scale || prior.properties!=placement.properties;
        prior.position=placement.position;prior.rotation=placement.rotation;
        prior.scale=placement.scale;
        // A new typed property is not automatically a pose companion. Keep
        // every unrelated or structured value exact unless its original live
        // primitive is explicitly checked by direct_pose_fields below.
        for(auto key:{kRelativeScaleProperty,kElevationProperty,kSoundPitchProperty,kSoundGainProperty,
                      kDestinationPositionProperty,kDestinationRotationProperty,kDestinationScaleProperty}){
            auto old=prior.properties.find(key);const auto next=placement.properties.find(key);
            if((old==prior.properties.end())!=(next==placement.properties.end()))return false;
            if(old!=prior.properties.end())old->second=next->second;
        }
    }
    return changed && expected==after;
}
bool capture_raw_owner(const CaptureTicket& ticket){
    skg::ReadOnlyMemoryScope memory;
    auto* m=mode();auto* d=data(m);auto* r=resource(d);
    auto* context=a_context&&skg::readable(reinterpret_cast<void*>(a_context),sizeof(void*))?*reinterpret_cast<void**>(a_context):nullptr;
    return supported && generation==ticket.generation && skg::readable(context,4) && skg::field<uint32_t>(context,0)==ticket.act &&
        uintptr_t(m)==ticket.owner.mode && uintptr_t(d)==ticket.owner.data &&
        uintptr_t(r)==ticket.owner.resource && skg::readable(r,kResourceSize) &&
        skg::field<editor::ResourceKey>(r,8)==ticket.owner.source;
}
uintptr_t capture_native_context(){
    return a_context&&skg::readable(reinterpret_cast<void*>(a_context),sizeof(uintptr_t))?
        *reinterpret_cast<uintptr_t*>(a_context):0;
}
RecentMatches sealed_pose_support(const Gesture& gesture,std::string& error){
    const auto proof=gesture.capture_proof;
    if(!proof)return RecentMatches::Different;
    if(proof->busy){error="The staged pose proof is already being checked";return RecentMatches::Error;}
    struct Checking {const PoseSupportProof& proof;Checking(const PoseSupportProof& value):proof(value){proof.busy=true;}~Checking(){proof.busy=false;}} checking(*proof);
    if(!gesture.capture || *gesture.capture!=proof->ticket || gesture.generation!=proof->ticket.generation || gesture.clone_source ||
       !same_projection_image(gesture.before,proof->before) || !same_projection_image(gesture.after,proof->after) ||
       gesture.before.document!=proof->before.document || gesture.after.document!=proof->after.document){
        error="The staged pose proof does not match its exact captured images";return RecentMatches::Error;
    }
    const auto current=[&]{
        return active() && proof->current && proof->current(proof->ticket)==CaptureState::Ready && value_scope()==proof->ticket.scope &&
            capture_native_context()==proof->context && capture_raw_owner(proof->ticket);
    };
    try{
        ScopedResolutionAudit resolution;
        if(!current() || !current_asset_witnesses(proof->before_assets) || !current() ||
           !current_asset_witnesses(proof->after_assets) || !current() || resolution.audit.failed){
            error="The staged pose proof belongs to an expired or waiting native context";return RecentMatches::Error;
        }
        retain_projection(proof->before_projection);retain_projection(proof->after_projection);
        if(!current()){error="The staged pose proof context expired while retaining its values";return RecentMatches::Error;}
        return RecentMatches::Match;
    }catch(...){error="The staged pose proof callback failed";return RecentMatches::Error;}
}
bool direct_pose_fields(void* resource,const std::map<uint32_t,editor::ObjectId>& supplied,const Snapshot& image){
    Rows markers;if(!rows(resource,0x2C10,0x238,kPlacementLimit,markers))return false;
    size_t count=0;
    for(auto raw=markers.begin;raw!=markers.end;raw+=0x238)if(live_slot(raw)){
        const auto key=slot_index(markers,raw);auto identity=supplied.find(key);
        if(identity==supplied.end())return false;
        auto found=image.document.placements.find(identity->second);if(found==image.document.placements.end())return false;
        const auto& p=found->second;auto* marker=raw+4;++count;
        auto same=[&]<class T>(uint32_t property,uint32_t offset){
            auto value=p.properties.find(property);if(value==p.properties.end())return true;
            const auto* wanted=std::get_if<T>(&value->second);return wanted&&*wanted==skg::field<T>(marker,offset);
        };
        if(skg::field<uint32_t>(marker,0)!=uint32_t(p.class_id.id) || skg::field<editor::Vec3>(marker,4)!=p.position ||
           skg::field<editor::Rotation>(marker,0x10)!=p.rotation || skg::field<float>(marker,0x20)!=p.scale ||
           !same.template operator()<float>(kRelativeScaleProperty,0x24) || !same.template operator()<float>(kElevationProperty,0x220) ||
           !same.template operator()<float>(kSoundPitchProperty,0x1E0) || !same.template operator()<float>(kSoundGainProperty,0x1E4) ||
           !same.template operator()<editor::Vec3>(kDestinationPositionProperty,0x1E8) ||
           !same.template operator()<editor::Rotation>(kDestinationRotationProperty,0x1F4) ||
           !same.template operator()<float>(kDestinationScaleProperty,0x204))return false;
    }
    return count==image.document.placements.size();
}
struct RestorePrivatePose {
    struct Marker {
        void* pointer=nullptr;
        std::array<uint8_t,0x24> transform{};
        std::array<uint8_t,0x28> companions{};
        uint32_t elevation=0,flags=0;
    };
    const SerializedBeforeWitness& owner;
    std::vector<Marker> markers;
    RestorePrivatePose(const SerializedBeforeWitness& value,size_t count):owner(value){markers.reserve(count);owner.busy=true;}
    RestorePrivatePose(const RestorePrivatePose&)=delete;
    RestorePrivatePose& operator=(const RestorePrivatePose&)=delete;
    void retain(void* marker){
        Marker old;old.pointer=marker;
        std::memcpy(old.transform.data(),skg::ptr_add<void>(marker,4),old.transform.size());
        std::memcpy(old.companions.data(),skg::ptr_add<void>(marker,0x1E0),old.companions.size());
        old.elevation=skg::field<uint32_t>(marker,0x220);old.flags=skg::field<uint32_t>(marker,0x224);
        markers.push_back(old);
    }
    ~RestorePrivatePose(){
        // The drain owns this independent buffer until C.gesture returns, even
        // if a callback replaces or disposes the actual native editor.
        for(const auto& old:markers){
            std::memcpy(skg::ptr_add<void>(old.pointer,4),old.transform.data(),old.transform.size());
            std::memcpy(skg::ptr_add<void>(old.pointer,0x1E0),old.companions.data(),old.companions.size());
            skg::field<uint32_t>(old.pointer,0x220)=old.elevation;skg::field<uint32_t>(old.pointer,0x224)=old.flags;
        }
        owner.busy=false;
    }
};
RecentMatches supported_pose_image(const Gesture& gesture,std::string& error){
    const auto* witness=serialized_before_witness;
    if(!witness || witness->busy || !witness->resource || !witness->bindings || !gesture.before.metadata || !gesture.after.metadata ||
       !same_projection_image(witness->image,gesture.before) || gesture.before.source!=gesture.after.source ||
       gesture.before.act!=gesture.after.act || gesture.before.bindings!=gesture.after.bindings ||
       gesture.before.captain!=gesture.after.captain || gesture.before.captain_locked!=gesture.after.captain_locked ||
       gesture.before.metadata!=gesture.after.metadata || gesture.clone_source ||
       !only_existing_pose_fields(gesture.before.document,gesture.after.document))return RecentMatches::Different;
    const auto pinned_generation=generation;const auto scope=value_scope();
    auto* pinned_mode=mode();auto* pinned_data=data(pinned_mode);auto* pinned_live=resource(pinned_data);
    const bool readable=skg::readable(pinned_live,kResourceSize);
    const auto source=readable?skg::field<editor::ResourceKey>(pinned_live,8):editor::ResourceKey{};
    auto current=[&]{
        return generation==pinned_generation && gesture.generation==pinned_generation && value_scope()==scope && generation==pinned_generation &&
            witness==serialized_before_witness && witness->generation==pinned_generation && witness->scope==scope &&
            skg::readable(witness->resource,kResourceSize) && skg::field<editor::ResourceKey>(witness->resource,8)==gesture.before.source &&
            mode()==pinned_mode && data(pinned_mode)==pinned_data && resource(pinned_data)==pinned_live &&
            readable && skg::readable(pinned_live,kResourceSize) && source==gesture.after.source &&
            skg::field<editor::ResourceKey>(pinned_live,8)==source;
    };
    auto different=[&]{
        if(current())return RecentMatches::Different;
        error="The owned pose proof belongs to an expired native editor";return RecentMatches::Error;
    };
    try{
    if(!current())return different();
    ScopedResolutionAudit resolution_proof;
    if(!validate(gesture.before,gesture.before.document,error) || !validate(gesture.after,gesture.after.document,error))return different();
    ReadImage after;if(!read_image(gesture.after,after,error))return different();
    if(!direct_pose_fields(witness->resource,*witness->bindings,gesture.before))return different();
    std::vector<editor::ResourceKey> before_assets,after_assets;
    if(!resolved_asset_values(witness->resource,gesture.before.document,before_assets,error) ||
       !resolved_asset_values(after.p,gesture.after.document,after_assets,error) || before_assets!=after_assets)return different();
    std::map<editor::ObjectId,uint32_t> originals,afters;
    for(const auto& [marker,id]:*witness->bindings)if(!originals.emplace(id,marker).second)return different();
    for(const auto& binding:gesture.after.bindings)if(!afters.emplace(binding.object,binding.marker).second)return different();
    bool tracked=false;if(!relative_scale_mode(gesture.before.document,tracked,error))return different();
    RestorePrivatePose restore(*witness,gesture.after.document.placements.size());
    void* expected=witness->resource;
    for(const auto& [id,to]:gesture.after.document.placements){
        const auto& prior=gesture.before.document.placements.at(id);
        if(prior.position==to.position && prior.rotation==to.rotation && prior.scale==to.scale && prior.properties==to.properties)continue;
        auto original=originals.find(id),newer=afters.find(id);
        auto* destination=original==originals.end()?nullptr:marker_for_key(expected,original->second);
        auto* actual=newer==afters.end()?nullptr:marker_for_key(after.p,newer->second);
        if(!destination || !actual || skg::field<uint32_t>(destination,0)!=uint32_t(to.class_id.id))return different();
        restore.retain(destination);
        if(prior.position!=to.position)skg::field<editor::Vec3>(destination,4)=to.position;
        if(prior.rotation!=to.rotation)skg::field<editor::Rotation>(destination,0x10)=to.rotation;
        if(prior.scale!=to.scale){
            if(!tracked){const auto old=skg::field<float>(destination,0x20);if(!(old>0))return different();skg::field<float>(destination,0x24)*=to.scale/old;}
            skg::field<float>(destination,0x20)=to.scale;
        }
        auto assign=[&]<class T>(uint32_t property,uint32_t offset){
            auto before=prior.properties.find(property),next=to.properties.find(property);
            if(before==prior.properties.end())return next==to.properties.end();
            if(next==to.properties.end())return false;
            if(before->second==next->second)return true;
            const auto* value=std::get_if<T>(&next->second);if(!value)return false;
            skg::field<T>(destination,offset)=*value;return true;
        };
        if(!assign.template operator()<float>(kRelativeScaleProperty,0x24) || !assign.template operator()<float>(kElevationProperty,0x220) ||
           !assign.template operator()<float>(kSoundPitchProperty,0x1E0) || !assign.template operator()<float>(kSoundGainProperty,0x1E4) ||
           !assign.template operator()<editor::Vec3>(kDestinationPositionProperty,0x1E8) ||
           !assign.template operator()<editor::Rotation>(kDestinationRotationProperty,0x1F4) || !assign.template operator()<float>(kDestinationScaleProperty,0x204))return different();
        auto& flags=skg::field<uint32_t>(destination,0x224);
        flags=(flags&~0x16u)|(skg::field<uint32_t>(actual,0x224)&0x16u);
    }
    std::vector<editor::ResourceKey> settled_assets;
    if(!resolved_asset_values(expected,gesture.after.document,settled_assets,error) || settled_assets!=before_assets || resolution_proof.audit.failed)return different();
    if(!current())return different();
    std::vector<uint8_t> bytes;if(!serialize(expected,bytes,error))return different();
    if(!current())return different();
    if(bytes!=gesture.after.bytes)return RecentMatches::Different;
    error.clear();return RecentMatches::Match;
    }catch(...){return different();} // Private primitive restoration runs first.
}
RecentMatches capture_pose_resource(void* r,uint32_t act,const Snapshot& before,
                                   const std::map<uint32_t,editor::ObjectId>& supplied,
                                   const Snapshot::Metadata& metadata,Snapshot& out,PoseCaptureProof& proof,std::string& error,
                                   const Snapshot* captured_image=nullptr,const std::vector<editor::ResourceKey>* captured_assets=nullptr){
    const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};const auto pinned_generation=generation;
    const auto projection_scope=value_scope();
    auto* pinned_mode=mode();auto* pinned_data=data(pinned_mode);auto* pinned_live=resource(pinned_data);
    const auto pinned_source=skg::readable(pinned_live,kResourceSize)?skg::field<editor::ResourceKey>(pinned_live,8):editor::ResourceKey{};
    auto current=[&]{
        return generation==pinned_generation&&(!C.gesture_scope||C.gesture_scope()==scope)&&value_scope()==projection_scope&&
            mode()==pinned_mode&&data(pinned_mode)==pinned_data&&resource(pinned_data)==pinned_live&&
            skg::readable(pinned_live,kResourceSize)&&skg::field<editor::ResourceKey>(pinned_live,8)==pinned_source&&
            generation==pinned_generation;
    };
    auto different=[&]{
        if(current())return RecentMatches::Different;
        error="The native pose capture owner or context expired";return RecentMatches::Error;
    };
    Snapshot image;image.act=act;image.source=skg::field<editor::ResourceKey>(r,8);image.metadata=metadata;
    if(act!=before.act || image.source!=before.source)return different();
    ScopedResolutionAudit resolution_proof;
    std::vector<editor::ResourceKey> original_assets,parsed_assets,settled_assets;
    if(!resolved_asset_values(r,before.document,original_assets,error))return different();
    if(captured_assets && original_assets!=*captured_assets){error="The captured AFTER asset identities changed between frames";return RecentMatches::Error;}
    Rows markers;std::vector<Binding> bindings;
    if(!rows(r,0x2C10,0x238,kPlacementLimit,markers))return different();
    for(auto marker=markers.begin;marker!=markers.end;marker+=0x238)if(live_slot(marker)){
        const auto index=slot_index(markers,marker);auto found=supplied.find(index);
        if(found==supplied.end())return different();
        bindings.push_back({found->second,index});
    }
    if(!serialized_bindings(bindings,image.bindings,error) || image.bindings!=before.bindings)return different();
    if(captured_image){
        if(captured_image->act!=act || captured_image->source!=image.source || captured_image->bindings!=image.bindings ||
           captured_image->metadata!=image.metadata){error="The staged AFTER byte image lost its original identity";return RecentMatches::Error;}
        image.bytes=captured_image->bytes;
    }else if(!serialize(r,image.bytes,error) || !normalize_materialized_terrain(image.bytes,error))return different();
    if(!current()){error="The pose capture context changed during serialization";return RecentMatches::Error;}
    ReadImage parsed;if(!read_image(image,parsed,error))return different();
    std::vector<ResolvedAssetWitness> before_assets,after_assets;
    if(!asset_witnesses(parsed.p,before.document,before_assets,error))return different();
    std::map<uint32_t,editor::ObjectId> dense;for(const auto& binding:image.bindings)dense.emplace(binding.marker,binding.object);
    if(!projection(parsed.p,image,error,&dense,std::nullopt,std::nullopt,std::nullopt,std::nullopt,nullptr,&image.bytes) ||
       !hydrate_native_captain(parsed.p,image,error))return different();
    image.bindings=before.bindings;
    if(!only_existing_pose_fields(before.document,image.document) ||
       !captain_sidecar_matches(r,image,image.document,error) ||
       !direct_pose_fields(r,supplied,image) ||
       std::memcmp(skg::ptr_add<void>(r,0x150),skg::ptr_add<void>(parsed.p,0x150),0x26))return different();
    if(!asset_witnesses(parsed.p,image.document,after_assets,error))return different();
    if(!resolved_asset_values(parsed.p,image.document,parsed_assets,error) ||
       !resolved_asset_values(r,image.document,settled_assets,error))return different();
    if(!current() || resolution_proof.audit.failed || before_assets!=after_assets){
        error="The native pose projection context or asset resolution changed";return RecentMatches::Error;
    }
    if(original_assets!=parsed_assets || original_assets!=settled_assets)return different();
    // Close the source image after every native projection/resolver callback.
    // A Write/Read normalization may not silently change the user's live pose.
    std::vector<uint8_t> settled;
    if(!serialize(r,settled,error) || !normalize_materialized_terrain(settled,error))return different();
    if(!current()){error="The pose capture context expired during verification";return RecentMatches::Error;}
    if(settled!=image.bytes || skg::field<editor::ResourceKey>(r,8)!=image.source ||
       !direct_pose_fields(r,supplied,image) ||
       std::memcmp(skg::ptr_add<void>(r,0x150),skg::ptr_add<void>(parsed.p,0x150),0x26))return different();
    proof={std::move(after_assets),projection_scope,pinned_generation};out=std::move(image);return RecentMatches::Match;
}

using HistoryFn = void(SK_THISCALL*)(void*);
HistoryFn original_start, original_commit, original_undo, original_redo;
HistoryFn original_terrain_start,original_terrain_commit,original_terrain_rebuild,original_terrain_discard,original_terrain_undo,original_terrain_materialize;
HistoryFn original_terrain_complete_water;
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
SceneOwner gesture_scene_owner;
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
    bool pose_candidate=false;
    uint64_t serial=0;
    SceneOwner owner_pin;
};
std::deque<Pending> pending;
struct StagedPose {
    enum class Phase {Before,AfterBytes,AfterProjection,Canonical,Opaque,Delivery};
    CaptureTicket ticket;
    Pending boundary;
    void* entry=nullptr;void* before=nullptr;void* after=nullptr;
    Gesture image;
    std::vector<editor::ResourceKey> after_assets;
    Phase phase=Phase::Before;
    uint64_t frame=0;
    uintptr_t context=0;
    bool invalid=false;
    std::shared_ptr<const ProjectedSnapshot> before_projection,after_projection;
    std::function<CaptureState(const CaptureTicket&)> current;
    std::function<bool(const CaptureTicket&,const Snapshot&,std::string&)> canonical;
    std::function<void(const CaptureTicket&,const std::string&)> failed;
    std::function<void(Gesture)> deliver;
    void release_resources(){release(std::exchange(entry,nullptr));release(std::exchange(before,nullptr));release(std::exchange(after,nullptr));}
    ~StagedPose(){release_resources();}
};
std::shared_ptr<StagedPose> staged_pose;
bool capture_servicing=false;
std::array<uint64_t,6> capture_last_begin{},capture_last_end{},capture_last_frame{};
struct CaptureTiming {
    size_t phase;WorkTimer timer;
    CaptureTiming(size_t index,uint64_t frame):phase(index),timer(native_work.staged[index]){
        capture_last_begin[phase]=timer.begin;capture_last_frame[phase]=frame;
    }
    ~CaptureTiming(){capture_last_end[phase]=work_clock();}
};
size_t capture_value_bytes(const Snapshot& image){
    size_t bytes=sizeof(image)+image.bytes.size()+editor::bytes(image.document)+image.bindings.size()*sizeof(Binding);
    if(image.metadata)for(const auto& text:image.metadata->text)bytes+=text.size()*sizeof(char16_t);
    return bytes;
}
bool bounded_capture(const StagedPose& stage){
    constexpr size_t limit=64u<<20;size_t charged=sizeof(stage);
    const auto charge=[&](size_t bytes){if(bytes>limit-charged)return false;charged+=bytes;return true;};
    for(const auto* metadata:{&stage.boundary.before_metadata,&stage.boundary.after_metadata})
        if(*metadata)for(const auto& text:(**metadata).text)if(!charge(text.size()*sizeof(char16_t)))return false;
    if(!charge((stage.boundary.before_bindings.size()+stage.boundary.after_bindings.size())*64u))return false;
    if(!charge(stage.after_assets.capacity()*sizeof(editor::ResourceKey)))return false;
    return charge(capture_value_bytes(stage.image.before)) && charge(capture_value_bytes(stage.image.after)) &&
        (!stage.before_projection || charge(stage.before_projection->cost)) &&
        (!stage.after_projection || charge(stage.after_projection->cost)) &&
        (!stage.image.capture_proof || (charge(capture_value_bytes(stage.image.capture_proof->before)) &&
            charge(capture_value_bytes(stage.image.capture_proof->after)) &&
            charge((stage.image.capture_proof->before_assets.capacity()+stage.image.capture_proof->after_assets.capacity())*sizeof(ResolvedAssetWitness))));
}
CaptureState staged_capture_current(const std::shared_ptr<StagedPose>& stage){
    try{
        if(staged_pose!=stage || stage->invalid || !capture_raw_owner(stage->ticket) || !stage->current || !active() ||
           (stage->entry && skg::field<void*>(stage->entry,0x14)!=stage->after))return CaptureState::Retired;
        const auto disposition=stage->current(stage->ticket);
        if(staged_pose!=stage || stage->invalid || value_scope()!=stage->ticket.scope ||
           capture_native_context()!=stage->context || !capture_raw_owner(stage->ticket) ||
           (stage->entry && skg::field<void*>(stage->entry,0x14)!=stage->after))return CaptureState::Retired;
        return disposition;
    }catch(...){return CaptureState::Retired;}
}
void fail_staged_capture(const std::shared_ptr<StagedPose>& stage,const std::string& reason){
    const auto ticket=stage->ticket;const auto failed=stage->failed;
    if(staged_pose==stage)staged_pose.reset();
    stage->release_resources();stage->before_projection.reset();stage->after_projection.reset();stage->image={};
    if(failed)failed(ticket,reason.substr(0,512));
}
bool enroll_staged_capture(const Pending& boundary,Owned& entry,Owned& before,std::string& error){
    if(!boundary.pose_candidate || boundary.clone_source || boundary.speech || !boundary.serial || !terrain_grounding.empty() ||
       terrain_capturing || terrain_capture_rotations || !boundary.metadata_error.empty() || !boundary.before_metadata ||
       boundary.before_metadata!=boundary.after_metadata || boundary.before_bindings!=boundary.after_bindings ||
       !C.capture_begin || !C.capture_current || !C.capture_before || !C.capture_failed || !H || !H->frame_index)return false;
    auto stage=std::make_shared<StagedPose>();stage->boundary=boundary;
    stage->ticket={boundary.scope,{},boundary.generation,boundary.serial,boundary.act};
    auto* m=mode();auto* d=data(m);auto* r=resource(d);
    if(d!=boundary.owner || !valid_scope(boundary.scope) || !skg::readable(r,kResourceSize))return false;
    stage->ticket.owner=boundary.owner_pin;
    const SceneOwner actual{uintptr_t(m),uintptr_t(d),uintptr_t(r),skg::field<editor::ResourceKey>(r,8)};
    if(!boundary.owner_pin.resource)return false;
    if(actual!=boundary.owner_pin){error="The native pose release no longer owns its original scene";return false;}
    stage->context=capture_native_context();stage->frame=H->frame_index();
    if(!stage->context)return false;
    stage->current=C.capture_current;stage->canonical=C.capture_before;stage->failed=C.capture_failed;stage->deliver=C.gesture;
    const auto begin=C.capture_begin;
    if(staged_pose){error="Another native pose capture is already held";return false;}
    auto* after=skg::field<void*>(entry.p,0x14);
    if(!skg::readable(after,kResourceSize)){error="The original pose AFTER resource is unavailable";return false;}
    addref(after);stage->after=after;
    bool accepted=false;
    try{accepted=begin(stage->ticket,error);}
    catch(...){error="The native pose capture begin callback failed";}
    if(!accepted && error.empty())return false; // No Runtime hold was published.
    stage->entry=std::exchange(entry.p,nullptr);stage->before=std::exchange(before.p,nullptr);
    stage->boundary.entry=stage->boundary.before=nullptr;
    stage->image.generation=boundary.generation;stage->image.capture=stage->ticket;
    staged_pose=stage;
    if(!accepted){fail_staged_capture(stage,error);return true;}
    if(staged_capture_current(stage)==CaptureState::Retired){
        error="The native pose capture expired while establishing its hold";fail_staged_capture(stage,error);
    }
    return true;
}
bool speech_pending(){return std::any_of(pending.begin(),pending.end(),[](const auto& item){return item.speech!=nullptr;});}
bool ordinary_capture_pending(){return staged_pose || std::any_of(pending.begin(),pending.end(),[](const auto& item){return !item.speech;});}

void SK_CDECL drain(void*) {
    ServiceScope service_scope;
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
        std::string staging_error;
        try{if(enroll_staged_capture(item,entry,before,staging_error))continue;}
        catch(...){staging_error="Cannot retain the native pose capture stage";}
        if(!staging_error.empty()){if(C.failure)C.failure(staging_error);continue;}
        ValidationScope image_scope;
        Gesture gesture;
        gesture.generation = generation;
        gesture.clone_source = item.clone_source;
        std::string error;
        if(C.gesture_capture)C.gesture_capture(true);
        bool ok=item.metadata_error.empty() && item.before_metadata && item.after_metadata;
        if(!ok)error=item.metadata_error.empty()?"The native gesture lost its authored metadata witnesses":item.metadata_error;
        ok=ok&&capture_resource(before.p,item.act,gesture.before,error,&item.before_bindings,nullptr,true);
        if(ok)gesture.before.metadata=item.before_metadata;
        bool projected_pose=false;PoseCaptureProof pose_proof;
        if(ok && item.pose_candidate && !item.clone_source && item.before_bindings==item.after_bindings &&
           item.before_metadata==item.after_metadata && terrain_grounding.empty() && !terrain_capturing){
            const auto captured=capture_pose_resource(skg::field<void*>(entry.p,0x14),item.act,gesture.before,
                item.after_bindings,*item.after_metadata,gesture.after,pose_proof,error);
            if(captured==RecentMatches::Error)ok=false;
            else if(captured==RecentMatches::Match)projected_pose=true;
            else {++native_work.pose_fallbacks;error.clear();}
        }
        if(ok&&!projected_pose)ok=capture_resource(skg::field<void*>(entry.p,0x14),item.act,gesture.after,error,&item.after_bindings,nullptr,true);
        if(ok)gesture.after.metadata=item.after_metadata;
        if(ok&&projected_pose){
            Snapshot::Metadata current;
            const bool authorized=!C.gesture_scope||item.scope==C.gesture_scope();
            const bool projection_current=pose_proof.scope==value_scope();
            if(!authorized || !projection_current || item.owner!=data(mode()) || item.generation!=generation ||
               !capture_metadata(item.owner,current,error) || current!=*item.after_metadata){
                ok=false;if(error.empty())error="The native pose capture lost its owner or metadata witness";
            }else{
                remember_projection(gesture.after,pose_proof.assets,pose_proof.scope,pose_proof.generation);
                ++native_work.pose_captures;++captures;
            }
        }
        if(C.gesture_capture)C.gesture_capture(false);
        if(ok){
            SerializedBeforeScope serialized_before(before.p,item.before_bindings,gesture.before);
            if (C.gesture) C.gesture(std::move(gesture));
        } else if (C.failure) C.failure(error);
    }
}
bool advance_staged_capture(const std::shared_ptr<StagedPose>& stage,std::string& error){
    ValidationScope image_scope;
    ProjectionBorrow retained(stage->before_projection,stage->after_projection);
    auto& image=stage->image;const auto& boundary=stage->boundary;
    switch(stage->phase){
    case StagedPose::Phase::Before:
        if(!capture_resource(stage->before,boundary.act,image.before,error,&boundary.before_bindings))return false;
        image.before.metadata=boundary.before_metadata;
        stage->before_projection=projected_snapshot(image.before,error);error.clear();return true;
    case StagedPose::Phase::AfterBytes:{
        auto* after=stage->after;
        Snapshot captured;captured.act=boundary.act;captured.source=skg::field<editor::ResourceKey>(after,8);captured.metadata=boundary.after_metadata;
        std::vector<Binding> live;for(const auto& [slot,id]:boundary.after_bindings)live.push_back({id,slot});
        if(!serialized_bindings(live,captured.bindings,error) || !resolved_asset_values(after,image.before.document,stage->after_assets,error) ||
           !serialize(after,captured.bytes,error))return false;
        image.after=std::move(captured);return true;
    }
    case StagedPose::Phase::AfterProjection:{
        auto* after=stage->after;PoseCaptureProof proof;
        const auto captured_bytes=image.after;
        const auto captured=capture_pose_resource(after,boundary.act,image.before,boundary.after_bindings,
            *boundary.after_metadata,image.after,proof,error,&captured_bytes,&stage->after_assets);
        if(captured==RecentMatches::Error)return false;
        if(captured==RecentMatches::Different){
            ++native_work.pose_fallbacks;error.clear();
            std::vector<uint8_t> settled;
            if(!serialize(after,settled,error) || settled!=captured_bytes.bytes){
                if(error.empty())error="The retained native AFTER changed between capture frames";return false;
            }
            if(!capture_resource(after,boundary.act,image.after,error,&boundary.after_bindings))return false;
            image.after.metadata=boundary.after_metadata;
            if(!validate(image.after,image.after.document,error))return false;
        }else{
            Snapshot::Metadata settled;
            if(!capture_metadata(boundary.owner,settled,error) || settled!=*boundary.after_metadata){
                if(error.empty())error="The staged pose lost its exact native metadata";return false;
            }
            if(staged_capture_current(stage)!=CaptureState::Ready){error.clear();return false;}
            remember_projection(image.after,proof.assets,proof.scope,proof.generation);++native_work.pose_captures;++captures;
        }
        stage->after_projection=projected_snapshot(image.after,error);error.clear();return true;
    }
    case StagedPose::Phase::Canonical:
        if(!stage->canonical(stage->ticket,image.before,error))return false;
        stage->before_projection=projected_snapshot(image.before,error);error.clear();return true;
    case StagedPose::Phase::Opaque:{
        if(!only_existing_pose_fields(image.before.document,image.after.document)){
            // Classification is optional. Ambiguous/no-op images retain the
            // complete ordinary guard and receive no reusable pose seal.
            return supported_changes(image,error);
        }
        // Strong ownership across frames is not an immutable-byte witness.
        // Re-serialize the independently owned BEFORE before temporarily
        // applying any pose primitive to it on this synchronous stack.
        std::vector<uint8_t> original;
        if(!serialize(stage->before,original,error))return false;
        if(staged_capture_current(stage)!=CaptureState::Ready){error.clear();return false;}
        if(original!=image.before.bytes || skg::field<editor::ResourceKey>(stage->before,8)!=image.before.source){
            error="The retained native BEFORE changed between capture frames";return false;
        }
        RecentMatches supported_pose;
        {
            SerializedBeforeScope serialized_before(stage->before,boundary.before_bindings,image.before);
            WorkTimer timer(native_work.supported);supported_pose=supported_pose_image(image,error);
        }
        if(supported_pose==RecentMatches::Error)return false;
        if(supported_pose==RecentMatches::Different){++opaque_pose_fallbacks;error.clear();return supported_changes(image,error);}
        ++opaque_pose_hits;
        const auto before=projected_snapshot(image.before,error),after=projected_snapshot(image.after,error);
        if(!before || !after){error.clear();return true;} // Optional cache allocation may opt out of reuse.
        auto proof=std::make_shared<PoseSupportProof>();proof->ticket=stage->ticket;
        proof->before=image.before;proof->after=image.after;
        proof->before_assets=before->assets;proof->after_assets=after->assets;
        proof->before_projection=before;proof->after_projection=after;
        proof->context=stage->context;proof->current=stage->current;image.capture_proof=std::move(proof);
        return true;
    }
    case StagedPose::Phase::Delivery:break;
    }
    error="The native pose capture reached an invalid phase";return false;
}
void service_staged_capture(){
    if(capture_servicing || !staged_pose || !H || !H->frame_index)return;
    struct Servicing {Servicing(){capture_servicing=true;}~Servicing(){capture_servicing=false;}} servicing;
    const auto stage=staged_pose;
    try{
        state(); // Expire a replaced owner before touching its retained values.
        const auto disposition=staged_capture_current(stage);
        if(disposition==CaptureState::Retired){fail_staged_capture(stage,"The native pose capture owner or context expired");return;}
        const auto frame=H->frame_index();
        if(disposition==CaptureState::Wait || frame<=stage->frame)return;
        stage->frame=frame;
        CaptureTiming phase_timer(size_t(stage->phase),frame);
        if(stage->phase==StagedPose::Phase::Delivery){
            auto image=std::move(stage->image);const auto deliver=stage->deliver;
            staged_pose.reset();stage->release_resources();
            ProjectionBorrow retained(stage->before_projection,stage->after_projection);
            stage->before_projection.reset();stage->after_projection.reset();
            if(deliver)deliver(std::move(image));return;
        }
        std::string error;const bool advanced=advance_staged_capture(stage,error);
        const auto settled=staged_capture_current(stage);
        if(settled==CaptureState::Retired){fail_staged_capture(stage,"The native pose capture expired during a Native callback");return;}
        if(settled==CaptureState::Wait){stage->image.capture_proof.reset();return;}
        if(!advanced){if(!error.empty())fail_staged_capture(stage,error);return;}
        if(!bounded_capture(*stage)){fail_staged_capture(stage,"The native pose capture values exceed 64 MiB");return;}
        switch(stage->phase){
        case StagedPose::Phase::Before:stage->phase=StagedPose::Phase::AfterBytes;break;
        case StagedPose::Phase::AfterBytes:stage->phase=StagedPose::Phase::AfterProjection;break;
        case StagedPose::Phase::AfterProjection:stage->phase=StagedPose::Phase::Canonical;break;
        case StagedPose::Phase::Canonical:stage->phase=StagedPose::Phase::Opaque;break;
        case StagedPose::Phase::Opaque:
            stage->phase=StagedPose::Phase::Delivery;
            // Parsed owners and the stack before witness have already died at
            // advance's return. Only sealed values survive to the later frame.
            stage->release_resources();
            if(staged_capture_current(stage)==CaptureState::Retired)
                fail_staged_capture(stage,"The native pose capture expired while retiring its owned history images");
            break;
        case StagedPose::Phase::Delivery:break;
        }
    }catch(...){
        try{fail_staged_capture(stage,"The staged native pose capture callback or allocation failed");}
        catch(...){stage->release_resources();if(staged_pose==stage)staged_pose.reset();}
    }
}
void SK_THISCALL start(void* self) {
    editor_native::state();
    if(!applying && active() && ordinary_capture_pending())return;
    bool observe=!applying && !cancellation && active() && self==data(mode()) && *reinterpret_cast<uint8_t*>(a_history+0x20);
    if(observe){
        auto* m=mode();auto* d=data(m);auto* r=resource(d);
        gesture_scene_owner={uintptr_t(m),uintptr_t(d),uintptr_t(r),skg::readable(r,kResourceSize)?skg::field<editor::ResourceKey>(r,8):editor::ResourceKey{}};
        // A real authored pose boundary must never inherit a previous native
        // grounding result, including a rotation equal to its old rendered q.
        if(input_pose)clear_terrain_capture_rotations();
        gesture_owner=self;gesture_entry=nullptr;gesture_generation=generation;
        gesture_scope_pin=C.gesture_scope?C.gesture_scope():GestureScope{};
        gesture_serial=gesture_counter==UINT64_MAX?0:++gesture_counter;
        gesture_clone=input_clone;
        gesture_cast=input_cast;
        gesture_pose=input_pose&&!input_cast;
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
std::optional<TerrainMaterializationOwner> materialization_owner(void* self){
    // This identifies an existing value-only proof; it grants no editing lease.
    const auto scope=terrain_read_identity?terrain_read_identity->key->scope:value_scope();
    if(terrain_read_identity){
        auto& proof=*terrain_read_identity;bool current=false;
        try{current=!proof.invalid && proof.current && *proof.current && (*proof.current)();}catch(...){current=false;}
        void* m=mode();void* d=data(m);void* r=resource(d);
        const bool exact=current && editing() && valid_scope(scope) && generation==proof.key->generation &&
            self==terraform_mode() && uintptr_t(self)==proof.tool && skg::readable(self,0xCC) && skg::readable(r,kResourceSize) &&
            SceneOwner{uintptr_t(m),uintptr_t(d),uintptr_t(r),skg::field<editor::ResourceKey>(r,8)}==proof.key->owner;
        if(!exact){proof.invalid=true;return std::nullopt;}
    }else if(!active() || !valid_scope(scope) || self!=terraform_mode() || !skg::readable(self,0xCC))return std::nullopt;
    void* m=mode();void* d=data(m);void* r=resource(d);
    if(!skg::readable(r,kResourceSize))return std::nullopt;
    return TerrainMaterializationOwner{scope,generation,reinterpret_cast<uintptr_t>(m),reinterpret_cast<uintptr_t>(d),
        reinterpret_cast<uintptr_t>(r),reinterpret_cast<uintptr_t>(self),skg::field<editor::ResourceKey>(r,8)};
}
bool materialization_recipe(void* self,editor_native_terrain_materialization::Recipe& result,std::string& error){
    namespace material=editor_native_terrain_materialization;
    if(!skg::readable(self,0xCC)){error="The native terrain materialization owner is unavailable";return false;}
    for(auto kind:{editor::TerrainKind::Effect,editor::TerrainKind::Model}){
        const std::array<uint32_t,6> offsets=kind==editor::TerrainKind::Effect?
            std::array<uint32_t,6>{0x14,0x28,0x50,0x64,0x3C,0x8C}:std::array<uint32_t,6>{0xA0,0xB4,0,0,0,0};
        material::Columns columns;const size_t fields=kind==editor::TerrainKind::Effect?6:2;
        for(size_t c=0;c<fields;++c){Rows column;
            if(!rows(self,offsets[c],material::widths[c],editor_native_terrain::kRecipeLimit,column)){
                error="The native terrain materialization column is invalid";return false;
            }
            if(column.begin!=column.end)columns[c].assign(column.begin,column.end);
        }
        if(!material::from_columns(kind,columns,result,error))return false;
    }
    return true;
}
bool normalize_materialized_terrain(std::vector<uint8_t>& bytes,std::string& error){
    const auto owner=materialization_owner(terraform_mode());
    if(terrain_read_identity){
        if(!owner || terrain_read_identity->invalid){error="The owned terrain transition read identity expired";return false;}
        if(terrain_read_identity->values){
            try{return terrain_read_identity->values->normalize(bytes,error);}
            catch(const std::bad_alloc&){error="Owned terrain transition normalization allocation failed";return false;}
        }
    }
    if(!owner || !terrain_materialization_owner || *owner!=*terrain_materialization_owner){clear_terrain_materialization();return true;}
    if(!terrain_materialization_error.empty()){error=terrain_materialization_error;return false;}
    try{return terrain_materialization.normalize(bytes,error);}
    catch(const std::bad_alloc&){error="Native terrain materialization normalization allocation failed";return false;}
}
void SK_THISCALL terrain_materialize(void* self){
    state();const auto owner=materialization_owner(self);
    if(!owner){clear_terrain_materialization();original_terrain_materialize(self);return;}
    if(!terrain_materialization_owner || *terrain_materialization_owner!=*owner){clear_terrain_materialization();terrain_materialization_owner=owner;}
    editor_native_terrain_materialization::Recipe before;std::string error;bool captured=false;
    try{captured=materialization_recipe(self,before,error);}catch(const std::bad_alloc&){error="Native terrain materialization capture allocation failed";}
    original_terrain_materialize(self);
    if(materialization_owner(self)!=owner){clear_terrain_materialization();return;}
    try{
        editor_native_terrain_materialization::Recipe after;
        if(captured && materialization_recipe(self,after,error) && terrain_materialization.observe(before,after,error,terrain_authored_rows)){
            for(auto& rows:terrain_authored_rows)rows.clear();return;
        }
    }catch(const std::bad_alloc&){error="Native terrain materialization witness allocation failed";}
    terrain_materialization_error=error.empty()?"Native terrain materialization provenance is unavailable":std::move(error);
}
std::optional<editor::ObjectId> terrain_record(void* descriptor);
void prepare_materialization_owner(void* self){
    const auto owner=materialization_owner(self);
    if(owner && (!terrain_materialization_owner || *terrain_materialization_owner!=*owner)){clear_terrain_materialization();terrain_materialization_owner=owner;}
}
bool authored_terrain_append(void* self,editor::TerrainKind kind){
    prepare_materialization_owner(self);Rows keys;
    if(!rows(self,kind==editor::TerrainKind::Effect?0x14:0xA0,12,editor_native_terrain::kRecipeLimit,keys))return false;
    try{terrain_authored_rows[size_t(kind)].insert(size_t(keys.end-keys.begin)/12);return true;}
    catch(const std::bad_alloc&){terrain_materialization_error="Native terrain authored-row allocation failed";return false;}
}
bool authored_terrain_record(void* descriptor){
    prepare_materialization_owner(terraform_mode());
    if(!skg::readable(descriptor,16))return false;
    void* definition=skg::field<void*>(descriptor,4);if(!skg::readable(definition,0x69))return false;
    const auto kind=skg::field<bool>(definition,0x68)?editor::TerrainKind::Model:editor::TerrainKind::Effect;
    const auto offset=skg::field<uint32_t>(descriptor,8);Rows keys;
    if(!rows(terraform_mode(),kind==editor::TerrainKind::Effect?0x14:0xA0,12,editor_native_terrain::kRecipeLimit,keys) || offset>=size_t(keys.end-keys.begin)/12)return false;
    try{terrain_authored_rows[size_t(kind)].insert(offset);}catch(const std::bad_alloc&){terrain_materialization_error="Native terrain authored-row allocation failed";return false;}
    if(auto object=terrain_record(descriptor)){auto row=act_identity.terrain.find(*object);if(row!=act_identity.terrain.end())terrain_materialization.forget(kind,row->second.payload);}
    return true;
}
void SK_CDECL materialization_failure(void*){
    terrain_materialization_error_queued=false;
    terrain_materialization_error_reported=true;
    if(!terrain_materialization_error.empty() && materialization_owner(terraform_mode())==terrain_materialization_owner && C.failure)
        C.failure(terrain_materialization_error);
}
void remember_grounding(const editor::Document& document){
    terrain_grounding.clear();
    for(const auto& [id,row]:document.placements){
        void* marker=marker_for_id(resource(data(mode())),id);if(!marker)continue;
        const auto actual=skg::field<editor::Vec3>(marker,4);
        if(actual!=row.position)terrain_grounding.emplace(id,Grounding{actual,row.position});
    }
}
bool creation_terrain_placements(const Snapshot&,std::vector<CreationTerrainProof::Placement>&,std::string&);
bool creation_terrain_placements_current(const CreationTerrainProof&,bool,std::string&);
bool exact_grounding_position(const editor::Vec3&,const editor::Vec3&);
void SK_THISCALL terrain_complete_water(void* self){
    if(applying){original_terrain_complete_water(self);return;}
    try{
        const auto rotations=terrain_capture_rotations;const auto authorizing=C.gesture_scope;
        if(!active()){
            if(rotations || terrain_before){terrain_capture_rotation_error="The native Water completion lost its original shared gesture";return;}
            original_terrain_complete_water(self);return;
        }
        std::string error;std::vector<TerrainCaptureRotations::Row> before;
        const auto authorized=[&]{return rotations && terrain_capture_rotations && terrain_before && !terrain_pending &&
            self==terraform_mode() && terrain_owner==rotations->data && terrain_generation==rotations->generation &&
            terrain_scope==rotations->scope && authorizing && authorizing()==terrain_scope;};
        if(!authorized() || !terrain_capture_rotations_current(rotations,error) || !rotations->collect(before,true,error)){
            terrain_capture_rotation_error=error.empty()?"The native Water completion has no current owned gesture":error;return;
        }
        // Water's native completion grounds through F335B0, before F34400's
        // later F37A00 rebuild. Retain exact input XYZ as well as q; an unrelated
        // preexisting position change cannot acquire a grounding map here.
        CreationTerrainProof grounding;grounding.entry=*terrain_before;
        if(!creation_terrain_placements(grounding.entry,grounding.placements,error) || !authorized() ||
           !rotations->raw_rows(before) || !creation_terrain_placements_current(grounding,true,error)){
            terrain_capture_rotation_error=error.empty()?"The native Water grounding before-image expired":error;return;
        }
        struct Applying {bool prior=applying;Applying(){applying=true;}~Applying(){applying=prior;}};
        {Applying guard;original_terrain_complete_water(self);}
        // Sample the native result before invoking ANY policy/identity
        // callback. A callback's subsequent pose write is not native grounding.
        if(!rotations->raw_current()){
            terrain_capture_rotation_error="The native Water completion replaced its original scene";return;
        }
        auto after=before;
        std::map<editor::ObjectId,Grounding> mapped;
        for(size_t i=0;i<grounding.placements.size();++i){auto& row=grounding.placements[i];
            auto* marker=marker_for_key(rotations->resource,row.live.marker);
            if(!marker || marker_id(row.live.marker)!=row.live.object || skg::field<uint32_t>(marker,0)!=row.class_index ||
               skg::field<uint32_t>(marker,0x228)!=row.native_key){
                terrain_capture_rotation_error="The native Water grounding changed a placement binding";return;
            }
            after[i].q=skg::field<editor::Rotation>(marker,0x10);
            row.rendered=skg::field<editor::Vec3>(marker,4);row.grounded=false;
            if(!editor_native_terrain_rotations::finite(after[i].q) ||
               !std::isfinite(row.rendered.x)||!std::isfinite(row.rendered.y)||!std::isfinite(row.rendered.z)){
                terrain_capture_rotation_error="The native Water grounding returned an invalid position";return;
            }
            if(!exact_grounding_position(row.rendered,row.authored))mapped.emplace(row.live.object,Grounding{row.rendered,row.authored});
        }
        if(!rotations->raw_rows(after) || !authorized() || !finish_terrain_capture_rebuild(rotations,before,error)){
            terrain_capture_rotation_error=error.empty()?"The native Water grounding owner expired":error;return;
        }
        const auto settled=terrain_capture_rotations;
        if(!terrain_capture_rotations_current(settled,error) || !authorized() ||
           !creation_terrain_placements_current(grounding,true,error) || !settled->raw_rows(after)){
            terrain_capture_rotation_error=error.empty()?"The native Water grounding changed before its value proof closed":error;return;
        }
        terrain_grounding.swap(mapped); // Values only; no live pose is written.
    }catch(...){terrain_capture_rotation_error="The native Water grounding callback or allocation failed";}
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
    if(!begin_terrain_capture_rotations(before,error)){
        clear_terrain_capture_rotations();
        if(C.notice)C.notice(error.empty()?"The native terrain rotation before-image expired":error);
        return false;
    }
    terrain_before=std::move(before);terrain_scope=scope;terrain_generation=generation;terrain_owner=data(mode());
    return true;
}
void SK_THISCALL terrain_append_model(void* self,const editor::ResourceKey* key,const void* transform){
    if(!applying && active() && (!C.terrain_gate || !C.terrain_gate({},{}) || !terrain_begin(self)))return;
    if(!applying && active() && !authored_terrain_append(self,editor::TerrainKind::Model))return;
    original_terrain_append_model(self,key,transform);
}
void SK_THISCALL terrain_append_effect(void* self,uint32_t a,uint32_t b,const void* transform,uint32_t c,uint32_t d,uint32_t e){
    if(!applying && active() && (!C.terrain_gate || !C.terrain_gate({},{}) || !terrain_begin(self)))return;
    if(!applying && active() && !authored_terrain_append(self,editor::TerrainKind::Effect))return;
    original_terrain_append_effect(self,a,b,transform,c,d,e);
}
void SK_THISCALL terrain_start(void* self){
    state();
    if(!applying && active() && !terrain_begin(self))return;
    original_terrain_start(self);
}
void SK_THISCALL terrain_rebuild(void* self){
    const auto rotations=terrain_capture_rotations;
    std::vector<TerrainCaptureRotations::Row> before;std::string rotation_error;
    if(rotations && self==rotations->tool &&
       (!terrain_before || terrain_owner!=rotations->data || terrain_generation!=rotations->generation ||
        (C.gesture_scope && C.gesture_scope()!=terrain_scope) ||
        !terrain_capture_rotations_current(rotations,rotation_error) || !rotations->collect(before,true,rotation_error))){
        terrain_capture_rotation_error=rotation_error.empty()?"The native terrain rotation rebuild lost its original gesture":rotation_error;
        return;
    }
    if(rotations && self==rotations->tool){
        bool authorized=false;
        try{authorized=!C.gesture_scope || C.gesture_scope()==terrain_scope;}catch(...){authorized=false;}
        if(!authorized || terrain_capture_rotations!=rotations || !rotations->raw_rows(before)){
            terrain_capture_rotation_error="The native terrain rebuild lost its original authorizing gesture";return;
        }
    }
    struct Applying {bool prior=applying;~Applying(){applying=prior;}} scope;
    applying=true;original_terrain_rebuild(self);applying=scope.prior;
    if(rotations && self==rotations->tool && !finish_terrain_capture_rebuild(rotations,before,rotation_error))
        terrain_capture_rotation_error=rotation_error.empty()?"The native terrain rotation rebuild lost its exact provenance":rotation_error;
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
    ServiceScope service_scope;
    terrain_queued=false;state();
    if(!terrain_pending || !terrain_before)return;
    if(!active() || terrain_generation!=generation || terrain_owner!=data(mode()) ||
       (C.gesture_scope && terrain_scope!=C.gesture_scope())){terrain_pending=false;terrain_before.reset();terrain_recipe_replacement=false;clear_terrain_capture_rotations();return;}
    if(!terrain_settled() || !*reinterpret_cast<uint8_t*>(a_history+0x20))return;
    terrain_capturing=true;
    struct RotationEnd {uint64_t serial=terrain_rotation_serial;~RotationEnd(){if(serial==terrain_rotation_serial)clear_terrain_capture_rotations();}} rotation_end;
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
    std::optional<std::pair<editor::TerrainKind,uint32_t>> removed;
    if(!applying && active()){
        void* descriptor=actor&&a_terrain_selection?skg::thiscall<void*>(a_terrain_selection,self,actor):nullptr;
        if(!authored_terrain_record(descriptor))return;
        void* definition=skg::field<void*>(descriptor,4);
        removed={{skg::field<bool>(definition,0x68)?editor::TerrainKind::Model:editor::TerrainKind::Effect,skg::field<uint32_t>(descriptor,8)}};
    }
    original_terrain_remove(self,actor);
    if(removed){
        auto& changed=terrain_authored_rows[size_t(removed->first)];changed.erase(removed->second);
        std::set<size_t> shifted;
        while(!changed.empty()){auto node=changed.extract(changed.begin());if(node.value()>removed->second)--node.value();shifted.insert(std::move(node));}
        changed=std::move(shifted);
    }
}
void SK_THISCALL terrain_update(void* self,bool final){
    if(!applying && active()){
        if(self!=terraform_mode() || !skg::readable(self,0x1B9))return;
        void* descriptor=skg::field<void*>(self,0x1B0);
        if(!skg::readable(descriptor,16))return;
        void* actor=skg::field<void*>(descriptor,0);
        void* definition=skg::field<void*>(descriptor,4);
        const auto index=skg::field<uint32_t>(descriptor,8);
        const bool preview=skg::field<bool>(self,0x1B8);
        if(!terrain_gate_record(actor) || !terrain_begin(self))return;
        if(!active() || self!=terraform_mode() || skg::field<void*>(self,0x1B0)!=descriptor ||
           !skg::readable(descriptor,16) || skg::field<void*>(descriptor,0)!=actor ||
           skg::field<void*>(descriptor,4)!=definition || skg::field<uint32_t>(descriptor,8)!=index ||
           skg::field<bool>(self,0x1B8)!=preview)return;
        if(preview){
            // F381D0's new-proxy branch appends through F37150/F37240 before
            // there is a recipe row at descriptor+8. The append hooks mark
            // the allocated index; never mark a coincident existing index.
            if(!actor || !skg::readable(actor,4) || !skg::readable(definition,0x69) ||
               !a_terrain_selection || skg::thiscall<void*>(a_terrain_selection,self,actor)!=descriptor)return;
        }else if(!authored_terrain_record(descriptor))return;
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
        clear_terrain_materialization();
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
    clear_terrain_materialization();
    original_terrain_resource_init(self);
}
void SK_THISCALL terrain_activate(void* self,const editor::ResourceKey* key){
    // EF1DEE creates the Terraform scene. Its empty-key fallback builds the
    // default planet directly, rather than passing through F261B0.
    NativeTerrainInitialization initialization;
    clear_terrain_materialization();
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
        clear_terrain_materialization();
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
    auto* pinned_mode=mode();auto* pinned_resource=resource(accepted.owner);auto* pinned_tool=terraform_mode();
    auto* pinned_context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    const auto pinned_read_scope=value_scope();
    if(!skg::readable(pinned_resource,kResourceSize)){if(C.notice)C.notice("The pending planet preset lost its native source");return;}
    const auto pinned_source=skg::field<editor::ResourceKey>(pinned_resource,8);
    struct PresetGuard {std::map<editor::ObjectId,editor::TerrainRecord> records;editor::AuthoringSettings planet;};
    std::shared_ptr<const PresetGuard> guard;
    try{guard=std::make_shared<const PresetGuard>(PresetGuard{std::move(accepted.records),std::move(accepted.planet)});}
    catch(const std::bad_alloc&){if(C.notice)C.notice("The pending planet preset could not retain its checked state");return;}
    const auto current=[guard,pinned_mode,pinned_resource,pinned_tool,pinned_context,pinned_read_scope,pinned_source,
                        owner=accepted.owner,expected_act=accepted.act,expected_generation=accepted.generation,expected_scope=accepted.scope]{
        try{
            const auto planet=act_identity.settings.find(editor_native_terrain::kPlanetObject);
            return generation==expected_generation && C.gesture_scope && C.gesture_scope()==expected_scope &&
                value_scope()==pinned_read_scope && mode()==pinned_mode && data(pinned_mode)==owner &&
                resource(owner)==pinned_resource && terraform_mode()==pinned_tool && skg::readable(pinned_resource,kResourceSize) &&
                a_context && *reinterpret_cast<void**>(a_context)==pinned_context && skg::readable(pinned_context,4) &&
                skg::field<uint32_t>(pinned_context,0)==expected_act &&
                skg::field<editor::ResourceKey>(pinned_resource,8)==pinned_source &&
                act_identity.terrain==guard->records && planet!=act_identity.settings.end() && planet->second==guard->planet &&
                generation==expected_generation;
        }catch(...){return false;}
    };
    const auto expired=[&]{if(C.notice)C.notice("The checked planet preset changed before native loading");};
    if(!current()){expired();return;}
    Owned copy(parse(accepted.bytes,error));
    if(!copy.p){if(C.notice)C.notice(error);return;}
    if(!prepare_terrain_load_input(copy.p,accepted.bytes,current,error)) {if(C.notice)C.notice(error);return;}
    if(!current()){expired();return;}
    auto* tool=terraform_mode();if(!terrain_begin(tool))return;
    if(!current()){expired();return;}
    if(*reinterpret_cast<uint8_t*>(a_history+0x20))original_terrain_start(tool);
    if(!current()){expired();return;}
    terrain_build(tool,skg::ptr_add<void>(copy.p,0x18));
    if(!current()){expired();return;}
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
        skg::vcall<void>(slider,0x1C/4,int32_t(std::lround(double(density)*maximum)),false);
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
    ServiceScope service_scope;
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
void SK_THISCALL ground_step(void* self){if(!held_tool){ScopedPoseInput pose;MarkerAudio audio(data(mode()),selected_marker());original_ground_step(self);}}
void SK_THISCALL vertical_step(void* self){if(!held_tool){ScopedPoseInput pose;MarkerAudio audio(data(mode()),selected_marker());original_vertical_step(self);}}
void SK_THISCALL radial_step(void* self){if(!held_tool){ScopedPoseInput pose;MarkerAudio audio(data(mode()),selected_marker());original_radial_step(self);}}
void SK_THISCALL auxiliary_step(void* self){if(!held_tool){ScopedPoseInput pose;MarkerAudio audio(data(mode()),selected_marker());original_auxiliary_step(self);}}
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
                ScopedPoseInput pose;
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
    ScopedPoseInput pose(action==1||action==6);
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
    std::function<bool()> current;
    explicit InspectorView(std::function<bool()> guard={}):current(std::move(guard)){
        if(current && !current())return;
        void* settings=inspector_settings();
        const bool shown=a_inspector_open && inspector_shown(settings);
        if(current && !current())return;
        if(shown)asset=skg::field<uint32_t>(settings,0x20);
    }
    ~InspectorView(){
        if(current && !current())return;
        if(!asset || !editing() || owner!=data(mode()))return;
        if(current && !current())return;
        Rows classes;if(!rows(resource(owner),0x2BF4,0x27E8,1024,classes))return;
        bool exists=false;for(auto row=classes.begin;row!=classes.end;row+=0x27E8)if(skg::field<uint32_t>(row,0)==*asset){exists=true;break;}
        if(!exists)return;
        void* settings=inspector_settings();
        if(!skg::readable(settings,0x24))return;
        const bool shown=inspector_shown(settings);
        if(current && !current())return;
        if(shown && skg::field<uint32_t>(settings,0x20)==*asset)return;
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
        std::move(before_metadata),std::move(after_metadata),std::move(metadata_error),gesture_pose&&!gesture_cast&&!speech&&!gesture_clone,gesture_serial,gesture_scene_owner});
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
using UpdateDisplay=void(SK_THISCALL*)(void*,uint32_t);
using SetHistoryButtonFlags=void(SK_THISCALL*)(void*,uint32_t,bool);
UpdateDisplay original_update_display;
SetHistoryButtonFlags original_history_button_flags;
struct HistoryDisplayPin {
    uint64_t generation=0;
    GestureScope scope;
    void* mode=nullptr;void* data=nullptr;void* resource=nullptr;void* display=nullptr;void* layout=nullptr;void* layout_owner=nullptr;
    editor::ResourceKey source;
};
thread_local const HistoryDisplayPin* history_display_scope=nullptr;
bool history_display_current(const HistoryDisplayPin& pin){
    // These Runtime callbacks read cached policy only. Never retain a memory
    // query cache across them or call any native UI getter from this hook.
    try{
        if(!supported || applying || generation!=pin.generation || !valid_scope(pin.scope) ||
           !C.active || !C.active() || generation!=pin.generation || value_scope()!=pin.scope ||
           generation!=pin.generation)return false;
        skg::ReadOnlyMemoryScope memory;
        return mode()==pin.mode && skg::readable(pin.mode,0xDC) &&
            skg::field<int>(pin.mode,0xCC)==1 && skg::field<int>(pin.mode,0xD0)==0 &&
            data(pin.mode)==pin.data && resource(pin.data)==pin.resource &&
            skg::readable(pin.resource,kResourceSize) && skg::field<editor::ResourceKey>(pin.resource,8)==pin.source &&
            skg::field<void*>(pin.mode,0x14)==pin.display && skg::readable(pin.display,0x28) &&
            skg::field<void*>(pin.display,0x24)==pin.layout && skg::readable(pin.layout,0x18) &&
            skg::field<void*>(pin.layout,0x14)==pin.layout_owner && skg::readable(pin.layout_owner,0x6C);
    }catch(...){return false;}
}
bool capture_history_display(void* self,HistoryDisplayPin& pin){
    try{
        pin.generation=generation;pin.scope=value_scope();pin.mode=mode();
        {
            skg::ReadOnlyMemoryScope memory;
            pin.data=data(pin.mode);pin.resource=resource(pin.data);pin.display=self;
            if(!skg::readable(self,0x28) || !skg::readable(pin.resource,kResourceSize))return false;
            pin.layout=skg::field<void*>(self,0x24);pin.source=skg::field<editor::ResourceKey>(pin.resource,8);
            if(!skg::readable(pin.layout,0x18))return false;
            pin.layout_owner=skg::field<void*>(pin.layout,0x14);
        }
        return history_display_current(pin);
    }catch(...){return false;}
}
void SK_THISCALL update_display(void* self,uint32_t elapsed){
    // Clear an outer pin even for a nested unrelated display. A nested native
    // updater can never borrow another display's history authority.
    struct Scope {
        const HistoryDisplayPin* previous=history_display_scope;
        Scope(){history_display_scope=nullptr;}
        ~Scope(){history_display_scope=previous;}
    } scope;
    HistoryDisplayPin pin;
    if(capture_history_display(self,pin)){history_display_scope=&pin;++history_ui_updates;}
    original_update_display(self,elapsed);
}
void set_history_button_flags_at(void* window,uint32_t flags,bool value,uintptr_t caller){
    const auto* pin=history_display_scope;
    const bool undo=a_history_undo_return && caller==a_history_undo_return;
    const bool redo=a_history_redo_return && caller==a_history_redo_return;
    if(pin && flags==sui::kFlagEnabled && (undo || redo)){
        const auto id=undo?0xB006EF6Eu:0xF006EFA5u;
        auto current_window=[&]{
            return history_display_scope==pin && history_display_current(*pin) && history_display_scope==pin &&
                skg::readable(window,0x80) && skg::field<uint32_t>(window,0x7C)==id;
        };
        try{
            if(current_window() && C.history_available){
                const bool shared=C.history_available(redo);
                if(current_window()){
                    const bool enabled=!cancellation && shared;
                    if(enabled!=value)++history_ui_changes;
                    value=enabled;if(undo)++history_ui_undo;else ++history_ui_redo;
                }else ++history_ui_expired;
            }else ++history_ui_expired;
        }catch(...){++history_ui_expired;}
    }
    // This is the game's existing call, with only its enabled argument changed.
    // No extra window lookup, setter, allocation or native cursor write occurs.
    original_history_button_flags(window,flags,value);
}
void SK_THISCALL history_button_flags(void* window,uint32_t flags,bool value){
    set_history_button_flags_at(window,flags,value,reinterpret_cast<uintptr_t>(__builtin_return_address(0)));
}
void SK_CDECL fence_ui(void*){
    struct Completion {
        Completion(){fence_queued=true;}
        ~Completion(){if(H && H->time_seconds)last_fence=H->time_seconds();fence_queued=false;}
    } completion;
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
void* view_camera(std::string& error,const std::function<bool()>* current=nullptr) {
    auto checked=[&]{
        if(!current || (*current)())return true;
        error="The local editor camera belongs to an expired scene";return false;
    };
    if (!a_view_manager || !a_view_target || !a_view_orbit || !a_view_set_orbit || !a_view_lookat) {
        error="The verified local editor camera adapter is unavailable";return nullptr;
    }
    void* manager=skg::cdecl_call<void*>(a_view_manager);
    if(!checked())return nullptr;
    if (!view_slot(manager,0x50/4)) {error="The native camera manager is unavailable";return nullptr;}
    void* cameras=skg::vcall<void*>(manager,0x50/4);
    if(!checked())return nullptr;
    if (!view_slot(cameras,0x38/4) || !view_slot(cameras,0x40/4)) {
        error="The native editor camera manager is unavailable";return nullptr;
    }
    void* active_camera=skg::vcall<void*>(cameras,0x38/4);
    if(!checked())return nullptr;
    void* editor_camera=skg::vcall<void*>(cameras,0x40/4,uint32_t(0x00EBB801));
    if(!checked())return nullptr;
    if (!active_camera || active_camera!=editor_camera || !view_slot(active_camera,0x0C/4)) {
        error="Waiting for the active native Adventure Editor camera";return nullptr;
    }
    void* orbit=skg::vcall<void*>(active_camera,0x0C/4,uint32_t(0x011966ED));
    if(!checked())return nullptr;
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
bool read_view(void* camera, LocalView& view,const std::function<bool()>* current=nullptr) {
    // B10160 returns a borrowed static vector, copied before another call.
    const float* target=skg::thiscall<const float*>(a_view_target,camera);
    if ((current && !(*current)()) || !skg::readable(target,sizeof(float)*3)) {view={};return false;}
    std::memcpy(view.target.data(),target,sizeof(float)*3);
    // B133E0 final transform consumes the current quaternion AC, not the
    // desired CC used by native Save and the 32-byte ScenarioResource view.
    std::memcpy(view.rotation.data(),skg::ptr_add<void>(camera,0xAC),sizeof(float)*4);
    skg::thiscall<void>(a_view_orbit,camera,&view.orbit[0],&view.orbit[1],&view.orbit[2]);
    if(current && !(*current)()){view={};return false;}
    view.settings={skg::field<float>(camera,0x26C),skg::field<float>(camera,0x270),
        skg::field<float>(camera,0x310),skg::field<float>(camera,0x314)};
    return true;
}
bool same_view(const LocalView& a,const LocalView& b) {
    auto close_enough=[](float x,float y){return std::fabs(double(x)-y)<=1e-5*std::max(1.0,std::max(std::fabs(double(x)),std::fabs(double(y))));};
    for(unsigned i=0;i<3;++i)if(!close_enough(a.target[i],b.target[i])||!close_enough(a.orbit[i],b.orbit[i]))return false;
    // A quaternion and its negation encode the same rotation.
    bool positive=true,negative=true;
    for(unsigned i=0;i<4;++i){positive&=close_enough(a.rotation[i],b.rotation[i]);negative&=close_enough(a.rotation[i],-b.rotation[i]);}
    return (positive||negative)&&a.settings==b.settings;
}
bool exact_grounding_position(const editor::Vec3& a,const editor::Vec3& b){
    return !std::memcmp(&a,&b,sizeof a);
}
bool creation_terrain_raw_owner(const SceneOwner& owner,uint64_t expected_generation,uintptr_t tool){
    skg::ReadOnlyMemoryScope memory;
    auto* m=mode();auto* d=data(m);auto* r=resource(d);
    return supported && generation==expected_generation && uintptr_t(m)==owner.mode && uintptr_t(d)==owner.data &&
        uintptr_t(r)==owner.resource && uintptr_t(terraform_mode())==tool && skg::readable(r,kResourceSize) &&
        skg::field<editor::ResourceKey>(r,8)==owner.source;
}
struct EnvironmentRefresh {
    SceneOwner owner;GestureScope scope;
    uint64_t generation=0,serial=0,frame=0,gesture=0,terrain=0;
    uintptr_t tool=0;double started=0;
};
struct EnvironmentUiOwner {
    void* display=nullptr;void* layout=nullptr;void* terraform=nullptr;void* environment=nullptr;void* root=nullptr;
    bool operator==(const EnvironmentUiOwner&)const=default;
};
std::optional<EnvironmentRefresh> environment_refresh;
uint64_t environment_refresh_serial=0,environment_ui_updates=0,environment_ui_expired=0;
bool environment_refresh_queued=false,environment_refresh_servicing=false;
void clear_environment_refresh(){environment_refresh.reset();}
bool environment_interaction_idle(){
    return !applying && !gesture_before_resource && pending.empty() && !staged_pose && !terrain_before && !terrain_pending &&
        held_terrain_controls.empty() && !held_terrain_preset && !held_tool && !cancellation &&
        skg::readable(reinterpret_cast<void*>(a_history),0x21) && *reinterpret_cast<uint8_t*>(a_history+0x20)!=0;
}
bool environment_refresh_current(const EnvironmentRefresh& pin,bool published=true){
    try{
        const auto retained=[&]{return !published || (pin.serial && environment_refresh && environment_refresh->serial==pin.serial);};
        // Never cache memory probes across these native/policy callbacks.
        if(!retained() || !supported || !active() || !retained() || value_scope()!=pin.scope || !retained())return false;
        return retained() && valid_scope(pin.scope) && generation==pin.generation && gesture_counter==pin.gesture &&
            terrain_rotation_serial==pin.terrain && environment_interaction_idle() &&
            creation_terrain_raw_owner(pin.owner,pin.generation,pin.tool);
    }catch(...){return false;}
}
bool current_environment_ui(EnvironmentUiOwner& out){
    skg::ReadOnlyMemoryScope memory;
    auto* m=mode();auto* display=skg::readable(m,0x18)?skg::field<void*>(m,0x14):nullptr;
    if(!a_environment_ui_refresh || !a_environment_ui_vtable || !a_terraform_ui_vtable ||
       !skg::readable(display,0x2C) || (a_display_vtable&&skg::field<uintptr_t>(display,0)!=a_display_vtable) ||
       skg::field<uint32_t>(display,0x1C)!=2)return false;
    auto* terraform=skg::field<void*>(display,0x28);auto* layout=skg::field<void*>(display,0x24);
    if(!skg::readable(terraform,0x2C) || skg::field<uintptr_t>(terraform,0)!=a_terraform_ui_vtable ||
       skg::field<uint32_t>(terraform,0x28)!=3 || !skg::readable(layout,0x18))return false;
    auto* environment=skg::field<void*>(terraform,0x14);
    if(!skg::readable(environment,0x40) || skg::field<uintptr_t>(environment,0)!=a_environment_ui_vtable)return false;
    auto* root=skg::field<void*>(environment,0x0C);if(!skg::readable(root,0x80))return false;
    out={display,layout,terraform,environment,root};return true;
}
void request_environment_refresh(){
    if(!a_environment_ui_refresh || !a_environment_ui_vtable || !H || !H->frame_index || !H->time_seconds ||
       environment_refresh_serial==UINT64_MAX)return;
    try{
        EnvironmentRefresh pin;pin.generation=generation;pin.frame=H->frame_index();pin.started=H->time_seconds();
        pin.gesture=gesture_counter;pin.terrain=terrain_rotation_serial;pin.tool=uintptr_t(terraform_mode());
        auto* m=mode();auto* d=data(m);auto* r=resource(d);if(!skg::readable(r,kResourceSize))return;
        pin.owner={uintptr_t(m),uintptr_t(d),uintptr_t(r),skg::field<editor::ResourceKey>(r,8)};pin.scope=value_scope();
        if(!environment_refresh_current(pin,false))return;
        pin.serial=++environment_refresh_serial;environment_refresh=pin;
    }catch(...){ } // Optional UI synchronization never changes authored state.
}
void SK_CDECL drain_environment_refresh(void*){
    ServiceScope service_scope;
    if(environment_refresh_servicing)return;
    struct Completion {
        Completion(){environment_refresh_queued=environment_refresh_servicing=true;}
        ~Completion(){environment_refresh_queued=environment_refresh_servicing=false;}
    } completion;
    if(!environment_refresh)return;
    const auto pin=*environment_refresh;
    const auto drop=[&]{if(environment_refresh&&environment_refresh->serial==pin.serial)environment_refresh.reset();};
    try{
        if(!H || !H->frame_index || !H->time_seconds || H->time_seconds()-pin.started>3 || !environment_refresh_current(pin)){
            drop();++environment_ui_expired;return;
        }
        if(H->frame_index()<=pin.frame)return;
        const auto native=state();
        if(!environment_refresh_current(pin)){drop();++environment_ui_expired;return;}
        if(!native.ready || native.gesture || !terrain_settled() || deferred_local_view || local_view_settling)return;
        EnvironmentUiOwner owner,checked;
        if(!current_environment_ui(owner)){drop();return;} // Preserve current browsing.
        if(!environment_refresh_current(pin) || !current_environment_ui(checked) || checked!=owner){
            drop();++environment_ui_expired;return;
        }
        // Native F3AF60 uses ISlider::SetValue(value,false), then updates its
        // existing captions/flags. It sends no authoring setter notification.
        skg::thiscall<void>(a_environment_ui_refresh,owner.environment);
        if(environment_refresh_current(pin) && current_environment_ui(checked) && checked==owner)++environment_ui_updates;
        else ++environment_ui_expired;
        drop();
    }catch(...){drop();++environment_ui_expired;}
}
editor::Document act_binding_identity(const editor::Document& document){
    editor::Document result;result.act_order=document.act_order;
    result.project=document.project;result.epoch=document.epoch;result.next_id=document.next_id;
    result.next_terrain_order=document.next_terrain_order;result.terrain=document.terrain;
    for(const auto& [id,row]:document.settings)if(row.kind==editor::SettingsKind::Act){
        auto& bound=result.settings[id];bound.object=id;bound.act_id=row.act_id;
    }else if(row.kind==editor::SettingsKind::Project || row.kind==editor::SettingsKind::Planet)result.settings[id]=row;
    return result;
}
bool creation_terrain_placements(const Snapshot& entry,std::vector<CreationTerrainProof::Placement>& out,std::string& error){
    Rows markers;if(!rows(resource(data(mode())),0x2C10,0x238,kPlacementLimit,markers)){
        error="The creation entry placement roster is unavailable";return false;
    }
    std::vector<CreationTerrainProof::Placement> captured;captured.reserve(entry.document.placements.size());
    std::vector<Binding> live,compact;
    for(auto* raw=markers.begin;raw!=markers.end;raw+=0x238)if(live_slot(raw)){
        const auto slot=slot_index(markers,raw);const auto id=marker_id(slot);auto found=entry.document.placements.find(id);
        auto* marker=raw+4;
        if(found==entry.document.placements.end() || uint32_t(found->second.class_id.id)!=skg::field<uint32_t>(marker,0)){
            error="The creation entry placement identity differs from its committed snapshot";return false;
        }
        CreationTerrainProof::Placement row;row.live={id,slot};row.class_index=skg::field<uint32_t>(marker,0);
        row.native_key=skg::field<uint32_t>(marker,0x228);row.revision=found->second.revision;
        row.rendered=skg::field<editor::Vec3>(marker,4);row.authored=found->second.position;
        if(!std::isfinite(row.rendered.x)||!std::isfinite(row.rendered.y)||!std::isfinite(row.rendered.z)){
            error="The creation entry placement position is invalid";return false;
        }
        if(!exact_grounding_position(row.rendered,row.authored)){
            const auto prior=terrain_grounding.find(id);
            if(prior==terrain_grounding.end() || !exact_grounding_position(prior->second.native,row.rendered) ||
               !exact_grounding_position(prior->second.authored,row.authored)){
                error="The creation entry placement has no exact retained grounding proof";return false;
            }
            row.grounded=true;
        }
        live.push_back(row.live);captured.push_back(row);
    }
    if(captured.size()!=entry.document.placements.size() || !serialized_bindings(live,compact,error) || compact!=entry.bindings){
        error="The creation entry placement bindings differ from its committed snapshot";return false;
    }
    out=std::move(captured);return true;
}
bool creation_terrain_placements_current(const CreationTerrainProof& proof,bool entry_boundary,std::string& error){
    Rows markers;if(!rows(resource(data(mode())),0x2C10,0x238,kPlacementLimit,markers)){
        error="The creation return placement roster is unavailable";return false;
    }
    size_t count=0;for(auto* raw=markers.begin;raw!=markers.end;raw+=0x238)if(live_slot(raw))++count;
    if(count!=proof.placements.size()){error="The creation return placement roster changed";return false;}
    for(const auto& row:proof.placements){
        auto found=proof.entry.document.placements.find(row.live.object);
        auto* marker=marker_for_key(resource(data(mode())),row.live.marker);
        if(found==proof.entry.document.placements.end() || found->second.revision!=row.revision ||
           uint32_t(found->second.class_id.id)!=row.class_index || !exact_grounding_position(found->second.position,row.authored) ||
           !marker || skg::field<uint32_t>(marker,0)!=row.class_index || skg::field<uint32_t>(marker,0x228)!=row.native_key ||
           !exact_grounding_position(skg::field<editor::Vec3>(marker,4),row.rendered) ||
           (entry_boundary && marker_id(row.live.marker)!=row.live.object)){
            error="The creation return lost an exact original placement grounding witness";return false;
        }
        if(entry_boundary && row.grounded){
            auto prior=terrain_grounding.find(row.live.object);
            if(prior==terrain_grounding.end() || !exact_grounding_position(prior->second.native,row.rendered) ||
               !exact_grounding_position(prior->second.authored,row.authored)){
                error="The creation entry grounding proof changed while being retained";return false;
            }
        }
    }
    return true;
}
}

void service_capture(){service_staged_capture();}
bool capture_view(LocalView& view, std::string& error) {
    ServiceScope service_scope;
    const auto native=state();
    if (!native.ready || native.gesture || applying) {error="The native editor is not ready to capture its local view";return false;}
    void* camera=view_camera(error);if(!camera)return false;
    LocalView captured;read_view(camera,captured);
    if (!finite_view(captured)) {error="The native editor returned an invalid local view";return false;}
    if(state().generation!=native.generation) {error="The native editor changed while capturing its local view";return false;}
    view=captured;return true;
}

bool capture_creation_terrain(const GestureScope& expected_scope,uint64_t serial,const SceneOwner& expected_owner,const Snapshot& entry_snapshot,
                              CreationTerrainTicket& out,std::string& error){
    ServiceScope service_scope;
    try{
        constexpr size_t limit=64u<<20;size_t charged=sizeof(CreationTerrainProof);
        const auto charge=[&](size_t bytes){if(bytes>limit-charged)return false;charged+=bytes;return true;};
        if(!charge(entry_snapshot.bytes.size()) || !charge(editor::bytes(entry_snapshot.document)) ||
           !charge(entry_snapshot.bindings.size()*sizeof(Binding))){
            error="The creation's retained terrain proof exceeds 64 MiB";return false;
        }
        if(entry_snapshot.metadata)for(const auto& text:entry_snapshot.metadata->text)if(!charge(text.size()*sizeof(char16_t))){
            error="The creation's retained terrain proof exceeds 64 MiB";return false;
        }
        // A Native callback may retire Runtime's LocalCreation. Retain every
        // referenced input before invoking one, and publish only after its
        // copied operation callback still confirms the original entry.
        const auto scope=expected_scope;const auto owner=expected_owner;const auto entry=entry_snapshot;
        const auto authorizing=C.gesture_scope,projection=C.projection_scope?C.projection_scope:C.gesture_scope;
        const auto creation_current=C.creation_return_current;
        const auto native=state();const auto current=materialization_owner(terraform_mode());
        const auto authorized=[&]{
            SceneOwner actual;
            return valid_scope(scope) && serial && current && current->scope==scope && native.ready && !native.gesture &&
                current->generation==native.generation && authorizing && authorizing()==scope &&
                creation_current && creation_current(scope,serial) &&
                capture_scene_owner(actual,error) && actual==owner && state().generation==native.generation &&
                materialization_owner(terraform_mode())==current && projection && projection()==scope &&
                authorizing()==scope && creation_current(scope,serial) && creation_terrain_raw_owner(owner,native.generation,current->tool);
        };
        if(!authorized()){error="The creation cannot retain its current native terrain owner";return false;}
        if(!validate(entry,entry.document,error))return false;
        if(!terrain_materialization_error.empty() ||
           (!terrain_materialization.empty() && terrain_materialization_owner!=current)){
            error="The creation's native terrain materialization proof is unavailable";return false;
        }
        std::vector<CreationTerrainProof::Placement> placements;
        if(!creation_terrain_placements(entry,placements,error))return false;
        editor_native_terrain_materialization::Recipe rendered,canonical;
        if(!materialization_recipe(terraform_mode(),rendered,error) ||
           !editor_native_terrain_materialization::from_bytes(entry.bytes,canonical,error))return false;
        auto normalized=rendered;terrain_materialization.normalize_recipe(normalized);
        if(normalized!=canonical){error="The creation's native terrain does not match its committed entry recipe";return false;}
        if(!charge(terrain_materialization.owned_bytes()) ||
           !charge(placements.capacity()*sizeof(CreationTerrainProof::Placement))){
            error="The creation's retained terrain proof exceeds 64 MiB";return false;
        }
        for(const auto& kind:rendered.rows)for(const auto& row:kind)
            if(!charge(sizeof(row)+row.bytes.capacity()+row.references.capacity()*sizeof(editor::ObjectId))){
                error="The creation's retained terrain proof exceeds 64 MiB";return false;
            }
        auto proof=std::make_shared<CreationTerrainProof>();
        proof->scope=scope;proof->serial=serial;proof->generation=native.generation;proof->owner=owner;proof->tool=current->tool;
        proof->entry=entry;proof->rendered=std::move(rendered);proof->materialization=terrain_materialization;proof->owned_bytes=charged;
        proof->placements=std::move(placements);
        if(!authorized()){error="The native terrain owner changed while retaining the creation entry";return false;}
        editor_native_terrain_materialization::Recipe settled;
        if(!materialization_recipe(terraform_mode(),settled,error))return false;
        if(settled!=proof->rendered){error="The native terrain recipe changed while retaining the creation entry";return false;}
        if(!creation_terrain_raw_owner(owner,native.generation,proof->tool) || !creation_terrain_placements_current(*proof,true,error)){
            if(error.empty())error="The native scene changed while retaining its creation grounding proof";return false;
        }
        out=std::move(proof);error.clear();return true;
    }catch(const std::bad_alloc&){error="Cannot allocate the owned creation terrain proof";return false;}
    catch(...){error="The owned creation terrain proof callback failed";return false;}
}
bool resume_creation_terrain(const CreationTerrainTicket& supplied_ticket,const GestureScope& expected_scope,uint64_t serial,const SceneOwner& expected_owner,
                             const Snapshot& entry_snapshot,uint64_t returned_generation,std::string& error){
    const auto ticket=supplied_ticket;
    if(!ticket || ticket->retired){error="The creation terrain return ticket is unavailable or retired";return false;}
    ticket->retired=true; // One attempt only. Runtime records success before any later return retry.
    try{
        if(!same_projection_image(ticket->entry,entry_snapshot) || ticket->entry.document!=entry_snapshot.document){
            error="The creation terrain return no longer owns its exact native entry";return false;
        }
        const auto scope=expected_scope;const auto owner=expected_owner;const auto entry=entry_snapshot;
        const auto authorizing=C.gesture_scope,projection=C.projection_scope?C.projection_scope:C.gesture_scope;
        const auto creation_current=C.creation_return_current;
        const auto authorized=[&]{
            const auto native=creation_scene_state(owner);const auto current=materialization_owner(terraform_mode());
            return valid_scope(scope) && serial && returned_generation && ticket->scope==scope && ticket->serial==serial &&
                ticket->owner==owner && native.settled() && native.generation==returned_generation &&
                returned_generation>=ticket->generation && current && current->scope==scope && current->generation==returned_generation &&
                current->tool==ticket->tool && authorizing && authorizing()==scope &&
                creation_current && creation_current(scope,serial) && projection && projection()==scope &&
                authorizing()==scope && creation_current(scope,serial) && creation_terrain_raw_owner(owner,returned_generation,ticket->tool);
        };
        if(!authorized() || !same_projection_image(ticket->entry,entry) || ticket->entry.document!=entry.document){
            error="The creation terrain return no longer owns its exact native entry";return false;
        }
        editor_native_terrain_materialization::Recipe actual;
        if(!materialization_recipe(terraform_mode(),actual,error))return false;
        if(actual!=ticket->rendered){error="The native terrain recipe changed while the creation editor was open";return false;}
        if(!creation_terrain_placements_current(*ticket,false,error))return false;
        auto restored=ticket->materialization;
        auto logical=act_binding_identity(entry.document);
        std::map<uint32_t,editor::ObjectId> rebound;std::map<editor::ObjectId,Grounding> grounded;
        for(const auto& row:ticket->placements){
            rebound.emplace(row.live.marker,row.live.object);
            if(row.grounded)grounded.emplace(row.live.object,Grounding{row.rendered,row.authored});
        }
        if(!authorized()){error="The native terrain owner changed while resuming the creation";return false;}
        editor_native_terrain_materialization::Recipe settled;
        if(!materialization_recipe(terraform_mode(),settled,error))return false;
        if(settled!=ticket->rendered){error="The native terrain recipe changed while resuming the creation";return false;}
        if(!creation_terrain_raw_owner(owner,returned_generation,ticket->tool) || !creation_terrain_placements_current(*ticket,false,error)){
            if(error.empty())error="The native scene changed while resuming its creation grounding proof";return false;
        }
        // Only previously witnessed values and logical identities cross this
        // one-use return. No live marker, pose or terrain property is written.
        terrain_materialization=std::move(restored);
        terrain_materialization_owner=TerrainMaterializationOwner{scope,returned_generation,owner.mode,owner.data,owner.resource,ticket->tool,owner.source};
        terrain_grounding.swap(grounded);bindings.swap(rebound);act_identity=std::move(logical);
        terrain_materialization_error.clear();terrain_materialization_error_reported=false;
        for(auto& rows:terrain_authored_rows)rows.clear();
        error.clear();return true;
    }catch(const std::bad_alloc&){error="Cannot restore the owned creation terrain proof";return false;}
    catch(...){error="The owned creation terrain return callback failed";return false;}
}
#include "editor_native_terrain_transition_impl.h"

bool capture_scene_owner(SceneOwner& owner,std::string& error){
    ServiceScope service_scope;
    const auto native=state();
    void* current_mode=mode();void* current_data=data(current_mode);void* current_resource=resource(current_data);
    if(!native.editing || !native.ready || native.playtesting || native.gesture || applying ||
       !skg::readable(current_resource,kResourceSize)){
        error="The native Adventure Editor cannot pin its current scene owner";return false;
    }
    const SceneOwner captured{uintptr_t(current_mode),uintptr_t(current_data),uintptr_t(current_resource),skg::field<editor::ResourceKey>(current_resource,8)};
    const auto settled=state();
    void* final_mode=mode();void* final_data=data(final_mode);void* final_resource=resource(final_data);
    if(settled.generation!=native.generation || generation!=native.generation || uintptr_t(final_mode)!=captured.mode ||
       uintptr_t(final_data)!=captured.data || uintptr_t(final_resource)!=captured.resource ||
       !skg::readable(final_resource,kResourceSize) || skg::field<editor::ResourceKey>(final_resource,8)!=captured.source){
        error="The native Adventure Editor changed while pinning its scene owner";return false;
    }
    owner=captured;error.clear();return true;
}
bool saved_scene_current(SceneOwner owner,uint64_t expected_generation){
    const auto native=state();
    if(native.generation!=expected_generation || generation!=expected_generation ||
       !native.editing || native.playtesting || native.gesture)return false;
    // Resolve after the only callback-bearing getter. These final raw owner
    // reads do not require readiness or dereference the retained addresses.
    void* current_mode=mode();void* current_data=data(current_mode);void* current_resource=resource(current_data);
    return owner.mode && owner.data && owner.resource && uintptr_t(current_mode)==owner.mode &&
        uintptr_t(current_data)==owner.data && uintptr_t(current_resource)==owner.resource &&
        skg::readable(current_resource,kResourceSize) && generation==expected_generation;
}
SceneResumeState creation_scene_state(const SceneOwner& owner){
    ServiceScope service_scope;
    const auto native=state();SceneResumeState result;
    result.generation=native.generation;result.world_visible=native.world_visible;
    // state() permits its own deferred camera drain to run while settling.
    // A reentrant caller must wait until that drain has returned as well.
    result.ready=native.editing&&native.ready&&!native.playtesting&&!native.gesture&&!applying&&!local_view_settling;
    void* current_mode=mode();void* current_data=data(current_mode);void* current_resource=resource(current_data);
    const auto current_source=skg::readable(current_resource,kResourceSize)?skg::field<editor::ResourceKey>(current_resource,8):editor::ResourceKey{};
    // Compare opaque identities before reading only the freshly resolved owner.
    result.owner=owner.mode && owner.data && owner.resource && uintptr_t(current_mode)==owner.mode &&
        uintptr_t(current_data)==owner.data && uintptr_t(current_resource)==owner.resource &&
        skg::readable(current_resource,kResourceSize) && skg::field<editor::ResourceKey>(current_resource,8)==owner.source;
    const auto settled_owner=[&]{
        // The getters below can run callbacks. Their initial owner comparison
        // cannot authorize a result after those callbacks replace this scene.
        void* final_mode=mode();void* final_data=data(final_mode);void* final_resource=resource(final_data);
        const bool same_current=generation==native.generation && final_mode==current_mode && final_data==current_data &&
            final_resource==current_resource && skg::readable(final_resource,kResourceSize) &&
            skg::field<editor::ResourceKey>(final_resource,8)==current_source;
        result.owner=result.owner && same_current;
        if(!same_current)result.ready=false;
        return same_current;
    };
    if(game::mode_id()!=modes::kScenario || !skg::readable(current_mode,0xDD) ||
       !a_scenario_initialization || !skg::readable(reinterpret_cast<void*>(a_scenario_initialization),4) || !a_get_simulator_mode_manager){
        settled_owner();return result;
    }
    void* simulator_modes=skg::cdecl_call<void*>(a_get_simulator_mode_manager);
    // No further native calls follow: use only fresh primitive probes before
    // reading the proven current mode's fields or publishing its owner flag.
    if(!settled_owner() || !skg::readable(simulator_modes,0x29))return result;
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
                             const Snapshot::Metadata& metadata,std::string& error,const std::function<bool()>* required_current=nullptr){
    const auto pinned_scope=scope;
    const auto current_owner=[&]()->void*{
        const auto native=state();void* current_mode=mode();void* current_data=data(current_mode);
        void* current_resource=resource(current_data);
        if(!expected_generation || native.generation!=expected_generation || !native.editing || native.playtesting || native.gesture || applying ||
           !owner.mode || !owner.data || !owner.resource || uintptr_t(current_mode)!=owner.mode || uintptr_t(current_data)!=owner.data ||
           uintptr_t(current_resource)!=owner.resource || !skg::readable(current_resource,kResourceSize) ||
           skg::field<editor::ResourceKey>(current_resource,8)!=owner.source || (C.gesture_scope && C.gesture_scope()!=pinned_scope))return nullptr;
        if(required_current && !(*required_current)())return nullptr;
        return current_data;
    };
    auto* current=current_owner();Snapshot::Metadata actual;
    if(!current || !capture_metadata(current,actual,error) || actual!=metadata){
        if(error.empty())error="The restored adventure metadata no longer owns its native caption";return false;
    }
    if(!a_metadata_message_manager || !a_metadata_message_send){error="The verified native name notification is unavailable";return false;}
    auto* manager=skg::cdecl_call<void*>(a_metadata_message_manager);
    if(required_current && !(*required_current)()){error="The native caption's restoration context expired";return false;}
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

bool deferred_view_current(const DeferredLocalView& item,const std::function<bool()>* required_current=nullptr){
    try{
    if(valid_scope(item.scope) && value_scope()!=item.scope)return false;
    if(!editing())return false;
    if(required_current && (!*required_current || !(*required_current)()))return false;
    if(valid_scope(item.scope) && value_scope()!=item.scope)return false;
    return generation==item.generation && mode()==item.mode && data(item.mode)==item.owner &&
        resource(item.owner)==item.resource && skg::readable(item.resource,kResourceSize) &&
        skg::field<editor::ResourceKey>(item.resource,8)==item.source;
    }catch(...){return false;}
}
bool restore_view_impl(const LocalView& view,uint64_t expected_generation,const DeferredLocalView* pin,
                       const std::function<bool()>* required_current,std::string& error){
    ServiceScope service_scope;
    auto current_identity=[&]{
        try{return generation==expected_generation && (!pin || deferred_view_current(*pin,required_current));}
        catch(...){return false;}
    };
    auto expired=[&]{error="The pinned local editor view belongs to an expired scene";return false;};
    std::function<bool()> checked_identity;if(pin)checked_identity=current_identity;
    const auto* identity=pin?&checked_identity:nullptr;
    const auto native=state();
    if (!native.ready || native.gesture || applying || native.generation!=expected_generation) {
        error="The native editor context is not ready to restore this local view";return false;
    }
    if(!current_identity())return expired();
    if(!finite_view(view)){error="The pinned local editor view is invalid";return false;}
    void* camera=view_camera(error,identity);if(!camera)return false;
    if(!current_identity())return expired();
    LocalView current;read_view(camera,current,identity);
    if(!current_identity())return expired();
    if(!finite_view(current)||current.settings!=view.settings) {
        error="The native editor camera settings changed during Test";return false;
    }
    const float low=skg::field<float>(camera,0x2AC),high=skg::field<float>(camera,0x2B0);
    if(!std::isfinite(low)||!std::isfinite(high)||low>high||view.orbit[1]<low||view.orbit[1]>high) {
        error="The pinned local editor view exceeds the native camera limits";return false;
    }
    if(state().generation!=expected_generation || !current_identity())return expired();
    // Existing native instant LookAt restores current orbit point/quaternion.
    // B106C0 then sets all current/desired scalar values, including the actual
    // angles omitted from ScenarioResource's persisted camera fields.
    skg::thiscall<void>(a_view_lookat,camera,view.target.data(),view.rotation.data(),true);
    if(!current_identity())return expired();
    skg::thiscall<void>(a_view_set_orbit,camera,view.orbit[0],view.orbit[1],view.orbit[2]);
    if(!current_identity())return expired();
    LocalView restored;read_view(camera,restored,identity);
    if(state().generation!=expected_generation || !current_identity())return expired();
    if(!finite_view(restored) || !same_view(view,restored)) {
        error="The native editor could not restore the pinned local view";return false;
    }
    return true;
}
bool restore_view(const LocalView& view,uint64_t expected_generation,std::string& error){
    return restore_view_impl(view,expected_generation,nullptr,nullptr,error);
}
bool restore_view_fenced(const LocalView& view,uint64_t expected_generation,SceneOwner owner,GestureScope scope,
                         std::function<bool()> current,std::string& error){
    // A Runtime callback may retire its optional hold while a native getter
    // runs. Retain only copied values and a value-owned policy callback.
    const auto copied_view=view;
    if(!valid_scope(scope) || !current){error="The local view restoration has no current scene authority";return false;}
    const DeferredLocalView pin{copied_view,scope,expected_generation,reinterpret_cast<void*>(owner.mode),
        reinterpret_cast<void*>(owner.data),reinterpret_cast<void*>(owner.resource),owner.source};
    return restore_view_impl(copied_view,expected_generation,&pin,&current,error);
}
void SK_CDECL drain_local_view(void*){
    ServiceScope service_scope;
    local_view_queued=false;if(!deferred_local_view)return;
    const auto item=*deferred_local_view;
    const auto native=state();
    if(item.generation!=native.generation || !deferred_view_current(item)){
        deferred_local_view.reset();return;
    }
    if(!terrain_settled() || native.gesture || applying)return;
    if(!deferred_view_current(item)){deferred_local_view.reset();return;}
    local_view_settling=true;std::string error;
    const bool okay=restore_view_impl(item.view,item.generation,&item,nullptr,error);
    local_view_settling=false;deferred_local_view.reset();
    if(!okay && C.failure)C.failure(error);
}

bool init(const SkHost* host, SkMod* mod, Ctx context) {
    clear_environment_refresh();
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
    a_terrain_materialize=H->sym("SporeCoop::Editor::TerraformMaterialize");
    a_copy_terrain_properties=H->sym("SporeCoop::Editor::CopyTerrainProperties");
    a_terrain_rebuild=H->sym("SporeCoop::Editor::TerraformRebuild");
    a_terrain_selection=H->sym("SporeCoop::Editor::TerraformSelection");
    a_terrain_loaded=H->sym("SporeCoop::Editor::TerrainLoaded");
    a_terrain_vtable=H->sym("SporeCoop::Editor::TerrainVTable");
    a_terraform_ui_vtable=H->sym("SporeCoop::Editor::TerraformUIVTable");
    a_environment_ui_refresh=H->sym("SporeCoop::Editor::RefreshEnvironmentUI");
    a_environment_ui_vtable=H->sym("SporeCoop::Editor::EnvironmentUIVTable");
    a_flora_ui_vtable=H->sym("SporeCoop::Editor::FloraUIVTable");
    a_copy_class_acts=H->sym("SporeCoop::Editor::CopyClassActs");a_copy_marker_acts=H->sym("SporeCoop::Editor::CopyMarkerActs");
    a_insert_act=H->sym("SporeCoop::Editor::InsertAct");a_insert_class_act=H->sym("SporeCoop::Editor::InsertClassAct");a_insert_marker_act=H->sym("SporeCoop::Editor::InsertMarkerAct");
    a_add_act=H->sym("SporeCoop::Editor::AddAct");a_delete_act=H->sym("SporeCoop::Editor::DeleteAct");
    a_select_act=H->sym("SporeCoop::Editor::SelectAct");
    a_loading=H->sym("SporeCoop::Editor::LoadingScreen");
    a_layout_find=H->sym("SporeCoop::Editor::FindLayoutWindow");
    a_history_undo_return=H->sym("SporeCoop::Editor::UndoButtonFlagReturn");
    a_history_redo_return=H->sym("SporeCoop::Editor::RedoButtonFlagReturn");
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
    a_property_manager_get=H->sym("SporeCoop::Editor::PropertyManagerGet");
    a_resolve_property_parents=H->sym("SporeCoop::Editor::ResolvePropertyParents");
    a_view_target=H->sym("SporeCoop::Editor::CurrentOrbitTarget");
    a_view_orbit=H->sym("SporeCoop::Editor::CurrentOrbitScalars");
    a_view_set_orbit=H->sym("SporeCoop::Editor::SetOrbitScalarsInstant");
    a_view_lookat=H->sym("SporeCoop::SimCamera_LookAt");
    supported = a_copy_acts && a_copy_act && a_select_act && a_inspector_open && a_layout_shown && a_preview_factory && a_mode && a_history && a_act && a_ctor && a_read && a_write && a_copy && a_entry && a_entry_vtable && a_restore && a_alloc &&
        a_context && a_tools && a_marker && a_object && a_clone_marker && a_create_marker && a_delete_marker && a_apply_marker && a_loading && a_layout_find && a_erase_history && a_discard_history && a_copy_marker_rows && a_insert_marker && a_copy_class_rows && a_copy_class && a_insert_class && a_speech_ui && a_behavior_data && a_history_undo_return && a_history_redo_return;
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
        install("SporeCoop::Editor::UpdateDisplay",(void*)update_display,(void**)&original_update_display) &&
        install("SporeCoop::Editor::SetHistoryButtonFlags",(void*)history_button_flags,(void**)&original_history_button_flags) &&
        install("SporeCoop::Editor::ScriptInput",(void*)script_input,(void**)&original_script_input) &&
        install("SporeCoop::Editor::ClassInput",(void*)class_input,(void**)&original_class_input) &&
        install("SporeCoop::Editor::CastCardAction",(void*)card_action,(void**)&original_card_action) &&
        install("SporeCoop::Editor::PaletteMessage",(void*)palette_message,(void**)&original_palette_message) &&
        install("SporeCoop::Editor::ApplySpeech",(void*)speech_apply,(void**)&original_speech_apply) &&
        install("SporeCoop::Editor::ApplyBehaviors",(void*)behavior_apply,(void**)&original_behavior_apply);
    a_complexity_ratio=H->sym("SporeCoop::EditorComplexity::WeightedRatio");
    a_complexity_count=H->sym("SporeCoop::EditorComplexity::CountLiveMarkers");
    a_complexity_weights=H->sym("SporeCoop::EditorComplexity::TuningWeights");
    a_complexity_max=H->sym("SporeCoop::EditorComplexity::TuningMaximum");
    a_complexity_create=H->sym("SporeCoop::EditorComplexity::CreateMarkerLimit");
    a_complexity_clone=H->sym("SporeCoop::EditorComplexity::CloneMarkerLimit");
    complexity_code_pinned=false;complexity_available=false;complexity_conflict.clear();complexity_conflicts=0;
    if(supported && skg::readable(reinterpret_cast<void*>(a_complexity_ratio),complexity_installed_code.size()) &&
       editor_native_complexity::formula_matches({reinterpret_cast<const uint8_t*>(a_complexity_ratio),complexity_installed_code.size()},a_complexity_ratio) &&
       skg::readable(reinterpret_cast<void*>(a_complexity_count),complexity_count_code.size()) &&
       !std::memcmp(reinterpret_cast<void*>(a_complexity_count),complexity_count_code.data(),complexity_count_code.size()) &&
       skg::readable(reinterpret_cast<void*>(a_complexity_create),11) && skg::readable(reinterpret_cast<void*>(a_complexity_clone),11) &&
       complexity_bounds.initialize(a_complexity_create,a_complexity_clone) &&
       install("SporeCoop::EditorComplexity::WeightedRatio",(void*)complexity_ratio,(void**)&original_complexity)){
        std::memcpy(complexity_installed_code.data(),reinterpret_cast<void*>(a_complexity_ratio),complexity_installed_code.size());
        complexity_code_pinned=complexity_available=true;
    }
    terrain_available=supported && a_terrain_load && a_copy_terrain_properties && a_terrain_rebuild && a_terrain_materialize && a_terrain_selection && a_terrain_loaded && a_terrain_vtable && a_terraform_ui_vtable && a_flora_ui_vtable && a_environment_ui_refresh && a_environment_ui_vtable &&
        install("SporeCoop::Editor::TerraformInitializeResource",(void*)terrain_resource_init,(void**)&original_terrain_resource_init) &&
        install("SporeCoop::Editor::TerraformActivate",(void*)terrain_activate,(void**)&original_terrain_activate) &&
        install("SporeCoop::Editor::TerraformStartHistory",(void*)terrain_start,(void**)&original_terrain_start) &&
        install("SporeCoop::Editor::TerraformCommitHistory",(void*)terrain_commit,(void**)&original_terrain_commit) &&
        install("SporeCoop::Editor::TerraformDiscardHistory",(void*)terrain_discard,(void**)&original_terrain_discard) &&
        install("SporeCoop::Editor::TerraformUndoHistoryEntry",(void*)terrain_undo,(void**)&original_terrain_undo) &&
        install("SporeCoop::Editor::TerraformLoadProperties",(void*)terrain_load,(void**)&original_terrain_load) &&
        install("SporeCoop::Editor::TerraformBuildProperties",(void*)terrain_build,(void**)&original_terrain_build) &&
        install("SporeCoop::Editor::TerraformRebuild",(void*)terrain_rebuild,(void**)&original_terrain_rebuild) &&
        install("SporeCoop::Editor::TerraformCompleteWater",(void*)terrain_complete_water,(void**)&original_terrain_complete_water) &&
        install("SporeCoop::Editor::TerraformMaterialize",(void*)terrain_materialize,(void**)&original_terrain_materialize) &&
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
    clear_environment_refresh();
    staged_pose.reset();
    clear_terrain_capture_rotations();
    if(terrain_read_identity)terrain_read_identity->invalid=true;
    projected_snapshots.clear();projected_snapshot_bytes=0;
    class_projection_cache.clear();
    act_projection_cache.clear();
    std::string bounds_error;
    if(!complexity_bounds.set(false,bounds_error) && H && H->log)H->log(M,SK_ERROR,"%s",bounds_error.c_str());
    complexity_available=complexity_code_pinned=false;
    update_previews({});
    for(auto& [peer,actor]:preview_actors){(void)peer;clear_preview_actor(actor);}preview_actors.clear();
    ++generation;
    input_speech=false;
    terrain_before.reset();terrain_pending=terrain_queued=terrain_capturing=terrain_rebuilding=terrain_recipe_replacement=false;terrain_grounding.clear();materialized_terrain.reset();terrain_available=false;
    clear_terrain_materialization();
    held_tool.reset();held_creation.reset();held_class_removal.reset();held_captain_asset.reset();held_crew_action.reset();held_terrain_controls.clear();held_terrain_preset.reset();input_queued=false;
    deferred_local_view.reset();local_view_queued=local_view_settling=false;
    held_import.reset();
    cancellation.reset();cancellation_queued=cancellation_settling=false;
    gesture_owner=gesture_entry=nullptr;gesture_generation=gesture_serial=0;gesture_scope_pin={};gesture_scene_owner={};
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
    if(environment_refresh && !environment_refresh_queued && !environment_refresh_servicing){
        environment_refresh_queued=true;H->run_outside_frame(drain_environment_refresh,nullptr);
    }
    if(terrain_pending && !terrain_queued){terrain_queued=true;H->run_outside_frame(drain_terrain,nullptr);}
    if(!terrain_materialization_error.empty() && !terrain_materialization_error_queued && !terrain_materialization_error_reported){terrain_materialization_error_queued=true;H->run_outside_frame(materialization_failure,nullptr);}
    if(!pending.empty() && !queued){queued=true;H->run_outside_frame(drain,nullptr);}
    if((held_tool || held_import || held_creation || held_class_removal || held_captain_asset || held_crew_action || held_terrain_preset || !held_terrain_controls.empty()) && !input_queued){input_queued=true;H->run_outside_frame(continue_input,nullptr);}
    if(cancellation && !cancellation_queued){cancellation_queued=true;H->run_outside_frame(drain_cancellation,nullptr);}
    if(fence_queued)return;
    double now=H->time_seconds();
    if(now-last_fence<0.2)return;
    last_fence=now;fence_queued=true;H->run_outside_frame(fence_ui,nullptr);
}

State state() {
    ServiceScope service_scope;
    State s; s.available = supported;
    const bool scenario=game::mode_id()==modes::kScenario;
    void* m = mode(); void* d = data(m);
    if (m != observed_mode || d != observed_data || scenario!=observed_scenario) {
        // Retire only the value identity here. Native owning refs are released
        // by the next outside-frame service, never by a render-time state read.
        if(staged_pose)staged_pose->invalid=true;
        clear_terrain_capture_rotations();
        if(terrain_read_identity)terrain_read_identity->invalid=true;
        projected_snapshots.clear();projected_snapshot_bytes=0;
        class_projection_cache.clear();
        act_projection_cache.clear();
        observed_mode = m; observed_data = d; observed_scenario=scenario; ++generation;
        bindings.clear();incarnations.clear();selection.clear();act_identity={};
        terrain_before.reset();terrain_pending=terrain_capturing=terrain_rebuilding=terrain_recipe_replacement=false;terrain_grounding.clear();materialized_terrain.reset();
        clear_terrain_materialization();
        deferred_local_view.reset();clear_environment_refresh();held_terrain_controls.clear();held_terrain_preset.reset();held_tool.reset();
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
    s.ready=terrain_materialization_error.empty() && !ordinary_capture_pending() && (!cancellation || cancellation_settling) && (!deferred_local_view || local_view_settling) && terrain_ready && s.editing && display_valid &&
        (skg::field<int>(display,0x1C)==0 || (s.terraforming && terrain_available)) && skg::readable(container,0x8C) &&
        skg::readable(loading,0x371) && !skg::field<bool>(loading,0x370);
    s.playtesting=s.entry==0 && skg::field<int>(m,0xCC)==2;
    void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    if(s.editing && skg::readable(context,4))s.act=skg::field<uint32_t>(context,0);
    s.selected=selection;
    s.gesture=!*reinterpret_cast<uint8_t*>(a_history+0x20) || !pending.empty() || bool(staged_pose) || (active() && speech_pending()) || (terrain_pending && !terrain_capturing);
    s.history_cursor=*reinterpret_cast<uint32_t*>(a_history+0x14);
    uintptr_t begin=*reinterpret_cast<uintptr_t*>(a_history),end=*reinterpret_cast<uintptr_t*>(a_history+4);
    if(end>=begin && (end-begin)%4==0) s.history_size=uint32_t((end-begin)/4);
    return s;
}
bool terraform_navigation_allowed(){
    const auto current=state();return terrain_available && active() && current.ready && !current.gesture &&
        C.navigation_allowed && C.navigation_allowed() && C.terrain_gate && C.terrain_gate({},{});
}
bool complexity_capability(editor::ComplexityCapability& out,std::string& error){return read_complexity_capability(out,error);}
bool extended_complexity_available(){editor::ComplexityCapability value;std::string error;return read_complexity_capability(value,error);}

void bind(const std::vector<Binding>& value) {
    bindings.clear();
    for (const auto& b : value){bindings[b.marker]=b.object;incarnations[b.marker]=std::max(incarnations[b.marker],1u);}
    selection.clear();
}
void bind_acts(const editor::Document& document){
    act_identity=act_binding_identity(document);
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

bool service_busy(){
    return service_depth || applying || capture_servicing || local_view_settling || environment_refresh_servicing;
}
bool capture(Snapshot& out, std::string& error) {
    ServiceScope service_scope;
    auto s=state();
    if (!s.ready || s.gesture || applying) { error="Native Adventure Editor is not ready for a snapshot"; return false; }
    refresh_selection();
    void* pinned_mode=mode();void* owner=data(pinned_mode);void* pinned_resource=resource(owner);
    const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    if(!capture_resource(pinned_resource,s.act,out,error))return false;
    const auto current=state();
    if(current.generation!=s.generation || !current.ready || current.gesture || current.act!=s.act ||
       mode()!=pinned_mode || data(pinned_mode)!=owner || resource(owner)!=pinned_resource ||
       !skg::readable(pinned_resource,kResourceSize) || skg::field<editor::ResourceKey>(pinned_resource,8)!=out.source ||
       (C.gesture_scope && C.gesture_scope()!=scope)){
        error="The native editor changed during snapshot capture";return false;
    }
    out.local_dirty=Snapshot::Dirty{skg::field<uint32_t>(owner,0x80),skg::field<bool>(owner,0x84),skg::field<bool>(owner,0x85)};
    Snapshot::Metadata metadata;if(!capture_metadata(owner,metadata,error))return false;
    out.metadata=std::move(metadata);
    return true;
}
RecentMatches recent_matches(const Snapshot& expected,const editor::Document& document,uint64_t expected_generation,
                             Snapshot& out,std::string& error){
    ServiceScope service_scope;
    auto matched=[](RecentMatches result){
        if(result==RecentMatches::Match)++recent_match_hits;
        else if(result==RecentMatches::Different)++recent_match_different;
        else ++recent_match_errors;
        return result;
    };
    const auto s=state();const auto scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    SceneOwner owner;
    if(!s.ready || s.gesture || applying || s.generation!=expected_generation || !capture_scene_owner(owner,error)){
        if(error.empty())error="The native snapshot match belongs to an unavailable editor";
        return matched(RecentMatches::Error);
    }
    // Native Save can replace the canonical root while a replica retains its
    // verified local editor alias. Such images need the original full capture
    // and integrity path; they never borrow a projection under another source.
    if(owner.source!=expected.source){error.clear();return matched(RecentMatches::Different);}
    if(!validate(expected,document,error))return matched(RecentMatches::Error);
    SceneOwner current;
    auto current_owner=[&]{
        const auto now=state();
        return now.generation==expected_generation && now.ready && !now.gesture &&
            (!C.gesture_scope || C.gesture_scope()==scope) && capture_scene_owner(current,error) && current==owner;
    };
    if(!current_owner()){error="The native snapshot match owner expired";return matched(RecentMatches::Error);}
    // Grounding has an additional authored/native radial normalization. Keep
    // its existing checked capture path until it has the same exact witness.
    if(s.act!=expected.act || !terrain_grounding.empty()){error.clear();return matched(RecentMatches::Different);}
    auto* live=reinterpret_cast<void*>(owner.resource);
    Rows markers;if(!rows(live,0x2C10,0x238,kPlacementLimit,markers)){
        error="The native snapshot match marker roster is invalid";return matched(RecentMatches::Error);
    }
    std::vector<Binding> actual,compact;
    for(auto marker=markers.begin;marker!=markers.end;marker+=0x238)if(live_slot(marker)){
        const auto index=slot_index(markers,marker);actual.push_back({marker_id(index),index});
    }
    if(!serialized_bindings(actual,compact,error))return matched(RecentMatches::Error);
    if(compact!=expected.bindings){error="The native snapshot match placement identities changed";return matched(RecentMatches::Error);}
    std::vector<uint8_t> bytes;
    if(!serialize(live,bytes,error) || !normalize_materialized_terrain(bytes,error))return matched(RecentMatches::Error);
    if(!current_owner()){error="The native snapshot match owner expired during serialization";return matched(RecentMatches::Error);}
    // Native callbacks during Write must not make the borrowed projection
    // describe an earlier live scene. Recheck the actual serialized state and
    // nonserialized sidecars after that boundary, with no retained native data.
    if(!captain_sidecar_matches(live,expected,document,error) || !validate(expected,document,error) ||
       !current_owner())return matched(RecentMatches::Error);
    // All resolver/projection callbacks precede the final live byte witness.
    // A same-owner mutation inside those callbacks must select the fallback.
    std::array<uint8_t,0x46> captain_before{};
    std::memcpy(captain_before.data(),skg::ptr_add<void>(live,0x130),captain_before.size());
    std::vector<uint8_t> settled_bytes;
    if(!serialize(live,settled_bytes,error) || !normalize_materialized_terrain(settled_bytes,error))return matched(RecentMatches::Error);
    if(!current_owner()){error="The native snapshot match owner changed during verification";return matched(RecentMatches::Error);}
    Snapshot::Metadata metadata;
    if(!capture_metadata(reinterpret_cast<void*>(owner.data),metadata,error))return matched(RecentMatches::Error);
    Rows settled_markers;std::vector<Binding> settled_bindings,settled_compact;
    if(!rows(live,0x2C10,0x238,kPlacementLimit,settled_markers)){
        error="The native snapshot match marker roster changed";return matched(RecentMatches::Error);
    }
    bool same_live_pose=true;
    for(auto marker=settled_markers.begin;marker!=settled_markers.end;marker+=0x238)if(live_slot(marker)){
        const auto index=slot_index(settled_markers,marker);const auto object=marker_id(index);
        settled_bindings.push_back({object,index});
        auto wanted=document.placements.find(object);
        if(wanted==document.placements.end()){same_live_pose=false;continue;}
        const auto& p=wanted->second;auto* value=marker+4;
        auto same=[&]<class T>(uint32_t key,uint32_t offset){
            auto found=p.properties.find(key);if(found==p.properties.end())return true;
            const auto* expected=std::get_if<T>(&found->second);
            return expected && *expected==skg::field<T>(value,offset);
        };
        same_live_pose=same_live_pose && skg::field<uint32_t>(value,0)==uint32_t(p.class_id.id) &&
            skg::field<editor::Vec3>(value,4)==p.position && skg::field<editor::Rotation>(value,0x10)==p.rotation && skg::field<float>(value,0x20)==p.scale &&
            same.template operator()<float>(kRelativeScaleProperty,0x24) && same.template operator()<float>(kElevationProperty,0x220) &&
            same.template operator()<float>(kSoundPitchProperty,0x1E0) && same.template operator()<float>(kSoundGainProperty,0x1E4) &&
            same.template operator()<editor::Vec3>(kDestinationPositionProperty,0x1E8) &&
            same.template operator()<editor::Rotation>(kDestinationRotationProperty,0x1F4) && same.template operator()<float>(kDestinationScaleProperty,0x204);
    }
    if(!serialized_bindings(settled_bindings,settled_compact,error) || settled_compact!=expected.bindings || !current_owner()){
        error="The native snapshot match identities changed during verification";return matched(RecentMatches::Error);
    }
    if(!same_live_pose || std::memcmp(captain_before.data(),skg::ptr_add<void>(live,0x130),captain_before.size()) ||
       bytes!=settled_bytes || bytes!=expected.bytes || expected.metadata!=std::optional<Snapshot::Metadata>{metadata}){
        error.clear();return matched(RecentMatches::Different);
    }
    Snapshot snapshot=expected;snapshot.document=document;
    snapshot.local_dirty=Snapshot::Dirty{skg::field<uint32_t>(reinterpret_cast<void*>(owner.data),0x80),
        skg::field<bool>(reinterpret_cast<void*>(owner.data),0x84),skg::field<bool>(reinterpret_cast<void*>(owner.data),0x85)};
    out=std::move(snapshot);++captures;error.clear();return matched(RecentMatches::Match);
}
bool capture_draft(Snapshot& out,std::string& error) {
    ServiceScope service_scope;
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
    if(!serialize(copy.p,draft.bytes,error) || !normalize_materialized_terrain(draft.bytes,error))return false;
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

bool diagnostic_payload(const std::vector<uint8_t>& input,std::vector<uint8_t>& output){
    static constexpr uint8_t magic[]={'D','B','P','F'};
    constexpr uint32_t diagnostic_limit=4u<<20;
    auto cursor=input.begin();
    for(unsigned attempt=0;attempt<4;++attempt){
        cursor=std::search(cursor,input.end(),std::begin(magic),std::end(magic));
        if(cursor==input.end())return false;
        dbpf::Package package;
        if(package.load(std::vector<uint8_t>(cursor,input.end()))){
            const auto* entry=package.find({0x01897C18,0x01897C18,0x01897C18});
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
}

bool supported_changes(const Gesture& gesture,std::string& error){
    ValidationScope scope;
    WorkTimer timer(native_work.supported);
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
    const auto sealed=sealed_pose_support(gesture,error);
    if(sealed==RecentMatches::Match){++opaque_pose_hits;return true;}
    if(sealed==RecentMatches::Error)return false;
    const auto pose=supported_pose_image(gesture,error);
    if(pose==RecentMatches::Match){++opaque_pose_hits;return true;}
    if(pose==RecentMatches::Error)return false;
    if(serialized_before_witness)++opaque_pose_fallbacks;
    error.clear();
    ReadImage before,after;
    if(!read_image(gesture.before,before,error) || !read_image(gesture.after,after,error))return false;
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
            const bool decoded=diagnostic_payload(left,decoded_before) && diagnostic_payload(right,decoded_after);
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
        if(H)H->log(M,SK_INFO,"%s",error.c_str());
        return false;
    }
    error.clear();return true;
}

std::array<uint32_t,4> diagnostic_value_words(const editor::Value* value){
    std::array<uint32_t,4> words{};if(!value)return words;
    if(const auto* v=std::get_if<editor::Vec3>(value))std::memcpy(words.data(),v,sizeof *v);
    else if(const auto* v=std::get_if<editor::Rotation>(value))std::memcpy(words.data(),v,sizeof *v);
    else if(const auto* v=std::get_if<float>(value))std::memcpy(words.data(),v,sizeof *v);
    else if(const auto* v=std::get_if<bool>(value))words[0]=*v;
    else if(const auto* v=std::get_if<int64_t>(value)){const auto bits=uint64_t(*v);words[0]=uint32_t(bits);words[1]=uint32_t(bits>>32);}
    else if(const auto* v=std::get_if<editor::ObjectId>(value)){words[0]=uint32_t(v->id);words[1]=uint32_t(v->id>>32);words[2]=v->generation;}
    else if(const auto* v=std::get_if<editor::ResourceKey>(value)){words[0]=v->instance;words[1]=v->type;words[2]=v->group;}
    else if(const auto* v=std::get_if<editor::StructuredValue>(value)){words[0]=v->format;words[1]=uint32_t(v->bytes.size());words[2]=uint32_t(v->references.size());}
    else if(const auto* v=std::get_if<std::string>(value))words[0]=uint32_t(v->size());
    return words;
}
void log_setting_value(const char* stage,editor::ObjectId object,uint32_t property,const editor::Value* actual,const editor::Value* wanted){
    if(!H || !H->log)return;
    const auto a=diagnostic_value_words(actual),b=diagnostic_value_words(wanted);
    H->log(M,SK_DEBUG,"Native settings mismatch: stage=%s object=%llu:%u property=%08X types=%d/%d actual=%08X,%08X,%08X,%08X canonical=%08X,%08X,%08X,%08X",
        stage,static_cast<unsigned long long>(object.id),object.generation,property,actual?int(actual->index()):-1,wanted?int(wanted->index()):-1,
        a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]);
    const auto* sa=actual?std::get_if<editor::StructuredValue>(actual):nullptr;
    const auto* sb=wanted?std::get_if<editor::StructuredValue>(wanted):nullptr;
    if(sa && sb){size_t first=0;while(first<std::min(sa->bytes.size(),sb->bytes.size()) && sa->bytes[first]==sb->bytes[first])++first;
        H->log(M,SK_DEBUG,"Native settings fragment mismatch: stage=%s object=%llu:%u property=%08X first=%zu sizes=%zu/%zu bytes=%02X/%02X",
            stage,static_cast<unsigned long long>(object.id),object.generation,property,first,sa->bytes.size(),sb->bytes.size(),
            first<sa->bytes.size()?sa->bytes[first]:0,first<sb->bytes.size()?sb->bytes[first]:0);}
}
bool validate_projection(const Snapshot& projected,const Snapshot& snapshot,const editor::Document& document,std::string& error){
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
            unsigned logged=0;
            for(const auto& [property,value]:settings.properties){
                const editor::Value* wanted=nullptr;
                if(found!=document.settings.end()){auto field=found->second.properties.find(property);if(field!=found->second.properties.end())wanted=&field->second;}
                if(!wanted || value!=*wanted){log_setting_value("validation",id,property,&value,wanted);if(++logged==8)break;}
            }
            if(found!=document.settings.end() && logged<8)for(const auto& [property,value]:found->second.properties)
                if(!settings.properties.contains(property)){log_setting_value("validation",id,property,nullptr,&value);if(++logged==8)break;}
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
       projected.document.placements.size()!=document.placements.size() || snapshot.bindings.size()!=document.placements.size() ||
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
    error.clear();return true;
}

ValidationScope::ValidationScope(){
    ++service_depth;
    if(!validation_depth){parsed_images.clear();parsed_image_bytes=0;}
    ++validation_depth;
}
ValidationScope::~ValidationScope(){
    if(validation_depth&&--validation_depth==0){parsed_images.clear();parsed_image_bytes=0;}
    --service_depth;
}
bool validate(const Snapshot& snapshot,const editor::Document& document,std::string& error){
    ValidationScope scope;
    if(!supported){error="Native editor validation is unavailable";return false;}
    if(document.placements.size()>kPlacementLimit || snapshot.bindings.size()>kPlacementLimit){
        error="Native editor placement limit exceeded";return false;
    }
    bool tracked=false;if(!relative_scale_mode(document,tracked,error))return false;
    if(!valid_metadata(snapshot.metadata,error))return false;
    const auto retained=projected_snapshot(snapshot,error);
    if(retained && retained->image.document.schema==document.schema && retained->image.document.class_schema==document.class_schema &&
       retained->image.document.settings_schema==document.settings_schema && retained->image.document.act_order==document.act_order){
        // The producer already projected these exact immutable bytes with
        // fresh resolver witnesses. Still compare every wanted typed field;
        // a forged Document or sidecar cannot borrow the producer's proof.
        if(validate_projection(retained->image,snapshot,document,error)){++validation_hits;return true;}
        // Stable logical IDs and project-only settings are supplied separately
        // from native bytes. A different wanted identity needs fresh projection;
        // its rejection must come from that full check, never from an old key.
        error.clear();
    }
    ReadImage copy;if(!read_image(snapshot,copy,error))return false;
    ScopedResolutionAudit resolution_proof;
    const auto proof_generation=generation;const auto proof_scope=value_scope();
    std::vector<ResolvedAssetWitness> assets_before;
    if(!asset_witnesses(copy.get(),document,assets_before,error))return false;
    if(!captain_sidecar_matches(copy.get(),snapshot,document,error))return false;
    std::map<uint32_t,editor::ObjectId> supplied;
    std::set<editor::ObjectId> ids;
    for(const auto& b:snapshot.bindings){
        if(!b.object.id || !b.object.generation || !supplied.emplace(b.marker,b.object).second || !ids.insert(b.object).second){
            error="Native snapshot has duplicate or invalid placement bindings";return false;
        }
    }
    Snapshot projected;projected.act=snapshot.act;
    if(!projection(copy.get(),projected,error,&supplied,tracked,document.schema.contains(kElevationProperty),!document.class_schema.empty(),!document.settings_schema.empty(),&document,&snapshot.bytes))return false;
    if(!validate_projection(projected,snapshot,document,error))return false;
    std::vector<ResolvedAssetWitness> assets_after;
    if(!asset_witnesses(copy.get(),document,assets_after,error))return false;
    if(resolution_proof.audit.failed || assets_before!=assets_after || generation!=proof_generation || value_scope()!=proof_scope || generation!=proof_generation){
        error="Native asset resolution or editor context changed during validation";return false;
    }
    // Retain the actual projection, not the caller's assertions or native
    // pointers. Resolution changes are rechecked before every future reuse.
    projected.bytes=snapshot.bytes;projected.source=snapshot.source;projected.bindings=snapshot.bindings;
    projected.captain=snapshot.captain;projected.captain_locked=snapshot.captain_locked;projected.metadata=snapshot.metadata;
    remember_projection(projected,assets_after,proof_scope,proof_generation);
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

void log_restore_quaternions(const Snapshot& snapshot,void* candidate,const char* stage){
    // Primitive-only diagnostics: never project, serialize, resolve an actor or
    // modify an authored value. Snapshot bindings name this restored pool.
    if(!H || !H->log || !skg::readable(candidate,kResourceSize))return;
    Rows markers;if(!rows(candidate,0x2C10,0x238,kPlacementLimit,markers))return;
    unsigned logged=0;const size_t count=size_t(markers.end-markers.begin)/0x238;
    for(const auto& binding:snapshot.bindings){
        if(binding.marker>=count)continue;
        const auto found=snapshot.document.placements.find(binding.object);
        if(found==snapshot.document.placements.end())continue;
        auto* raw=markers.begin+binding.marker*0x238;
        if(!live_slot(raw) || skg::field<uint32_t>(raw+4,0)!=uint32_t(found->second.class_id.id))continue;
        const auto actual=skg::field<editor::Rotation>(raw+4,0x10);
        if(actual==found->second.rotation)continue;
        std::array<uint32_t,4> a{},b{};std::memcpy(a.data(),&actual,sizeof actual);
        std::memcpy(b.data(),&found->second.rotation,sizeof found->second.rotation);
        H->log(M,SK_DEBUG,"Native restore quaternion diagnostic: stage=%s object=%llu:%u marker=%u actual=%08X,%08X,%08X,%08X canonical=%08X,%08X,%08X,%08X",
            stage,static_cast<unsigned long long>(binding.object.id),binding.object.generation,binding.marker,
            a[0],a[1],a[2],a[3],b[0],b[1],b[2],b[3]);
        if(++logged==8)break;
    }
    const auto captain=snapshot.document.settings.find(kCaptainObject);
    if(captain!=snapshot.document.settings.end()){
        const std::array<std::pair<uint32_t,editor::Value>,3> fields{{
            {kCaptainPositionProperty,skg::field<editor::Vec3>(candidate,0x150)},
            {kCaptainRotationProperty,skg::field<editor::Rotation>(candidate,0x15C)},
            {kCaptainScaleProperty,skg::field<float>(candidate,0x16C)}}};
        for(const auto& [property,actual]:fields){auto wanted=captain->second.properties.find(property);
            if(wanted!=captain->second.properties.end() && actual!=wanted->second)log_setting_value(stage,kCaptainObject,property,&actual,&wanted->second);}
    }
}

struct RestoreFence {
    uint64_t generation=0;SceneOwner owner;GestureScope scope;std::function<bool()> current;
    bool checked()const{
        try{
            if(!supported || !valid_scope(scope) || !current || !current() || value_scope()!=scope)return false;
            void* m=mode();void* d=data(m);void* r=resource(d);
            return editor_native::generation==generation && uintptr_t(m)==owner.mode && uintptr_t(d)==owner.data &&
                uintptr_t(r)==owner.resource && skg::readable(m,0xDC) && skg::field<int>(m,0xCC)==1 &&
                skg::field<int>(m,0xD0)==0 && skg::readable(r,kResourceSize) && skg::field<editor::ResourceKey>(r,8)==owner.source;
        }catch(...){return false;}
    }
};
struct TerrainRotationOwner {
    uint64_t generation=0;
    void* mode=nullptr;void* data=nullptr;void* resource=nullptr;void* tool=nullptr;
    editor::ResourceKey source;GestureScope scope;
    uintptr_t first=0,last=0;
    std::function<bool()> required;
    bool current()const{
        try{
            if(!supported || (required && !required()) || value_scope()!=scope || editor_native::generation!=generation)return false;
            auto* m=editor_native::mode();auto* d=editor_native::data(m);auto* r=editor_native::resource(d);
            Rows markers;
            return m==mode && d==data && r==resource && terraform_mode()==tool &&
                skg::readable(m,0xDC) && skg::field<int>(m,0xCC)==1 && skg::field<int>(m,0xD0)==0 &&
                skg::readable(r,kResourceSize) && skg::field<editor::ResourceKey>(r,8)==source &&
                rows(r,0x2C10,0x238,kPlacementLimit,markers) && uintptr_t(markers.begin)==first && uintptr_t(markers.end)==last &&
                editor_native::generation==generation;
        }catch(...){return false;}
    }
};
struct TerrainRotation {
    Binding binding;uint32_t class_index=0;editor::Rotation canonical,observed;
};
bool pin_terrain_rotations(const Snapshot& snapshot,const RestoreFence* fence,TerrainRotationOwner& owner,
                           std::vector<TerrainRotation>& rotations,std::string& error){
    owner.generation=generation;owner.mode=mode();owner.data=data(owner.mode);owner.resource=resource(owner.data);
    owner.tool=terraform_mode();owner.scope=value_scope();if(fence)owner.required=fence->current;
    Rows markers;
    if(!skg::readable(owner.resource,kResourceSize) || !rows(owner.resource,0x2C10,0x238,kPlacementLimit,markers)){
        error="Native terrain restoration lost its placement map";return false;
    }
    owner.source=skg::field<editor::ResourceKey>(owner.resource,8);owner.first=uintptr_t(markers.begin);owner.last=uintptr_t(markers.end);
    if(!owner.current()){error="Native terrain restoration lost its editor identity";return false;}
    try{
        rotations.reserve(snapshot.bindings.size());
        for(const auto& binding:snapshot.bindings){
            auto wanted=snapshot.document.placements.find(binding.object);
            auto* marker=marker_for_key(owner.resource,binding.marker);
            if(wanted==snapshot.document.placements.end() || !marker || skg::field<uint32_t>(marker,0)!=uint32_t(wanted->second.class_id.id)){
                error="Native terrain restoration lost a canonical placement binding";return false;
            }
            const auto q=skg::field<editor::Rotation>(marker,0x10);
            if(q==wanted->second.rotation)rotations.push_back({binding,uint32_t(wanted->second.class_id.id),q,q});
        }
    }catch(const std::bad_alloc&){error="Native terrain rotation witnesses could not be allocated";return false;}
    if(!owner.current()){error="Native terrain rotation capture lost its editor identity";return false;}
    return true;
}
bool restore_terrain_rotations(const TerrainRotationOwner& owner,std::vector<TerrainRotation>& rotations,std::string& error){
    const auto current=[owner]{return owner.current();};
    const auto expired=[&]{error="Native terrain rotation restoration lost its editor or placement identity";return false;};
    if(!applying){error="Native terrain rotations may only be refreshed during owned restoration";return false;}
    const auto marker_for=[&](const TerrainRotation& rotation)->void*{
        if(!current())return nullptr;
        auto* marker=marker_for_key(owner.resource,rotation.binding.marker);
        return marker && skg::field<uint32_t>(marker,0)==rotation.class_index?marker:nullptr;
    };
    // Record the exact post-rebuild values before invoking any actor callback.
    // An initially mismatched rotation is never authorized for this repair.
    for(auto& rotation:rotations){
        auto* marker=marker_for(rotation);if(!marker)return expired();
        rotation.observed=skg::field<editor::Rotation>(marker,0x10);
    }
    for(const auto& rotation:rotations){
        if(rotation.observed==rotation.canonical)continue;
        if(!original_apply_marker || !a_object)return expired();
        auto* marker=marker_for(rotation);if(!marker)return expired();
        auto* object=skg::thiscall<void*>(a_object,owner.data,marker);
        if(!current())return expired();
        if(!object){error="Native terrain rotation restoration has no placed object";return false;}
        auto* spatial=native_cast(object,0x01186577);
        if(!current())return expired();
        if(!spatial){error="Native terrain rotation restoration has no spatial component";return false;}
        marker=marker_for(rotation);
        if(!marker || skg::field<editor::Rotation>(marker,0x10)!=rotation.observed)return expired();
        {
            // The existing ApplyMarkerState wrapper only preserves audio.
            // Use the same verified trampoline with a stronger private guard,
            // so its cleanup cannot write after a same-wrapper source change.
            MarkerAudio audio(owner.data,marker,current);
            if(!current())return expired();
            marker=marker_for(rotation);
            if(!marker || skg::field<editor::Rotation>(marker,0x10)!=rotation.observed)return expired();
            skg::field<editor::Rotation>(marker,0x10)=rotation.canonical;
            original_apply_marker(owner.data,marker,false);
            if(!current())return expired();
        }
        marker=marker_for(rotation);
        if(!marker || skg::field<editor::Rotation>(marker,0x10)!=rotation.canonical){
            error="Native terrain rotation refresh changed its canonical quaternion";return false;
        }
    }
    for(const auto& rotation:rotations){auto* marker=marker_for(rotation);
        if(!marker || skg::field<editor::Rotation>(marker,0x10)!=rotation.canonical)return expired();}
    return current()?true:expired();
}
bool restore_impl(const Snapshot& snapshot,const RestoreFence* fence,std::string& error){
    ServiceScope service_scope;
    std::function<bool()> current;if(fence)current=[fence]{return fence->checked();};
    const auto* required=fence?&current:nullptr;
    auto checked=[&]{if(!fence || fence->checked())return true;error="The native restoration's pinned editor expired";return false;};
    if(!checked())return false;
    clear_terrain_capture_rotations();
    InspectorView inspector(current);
    if(!checked())return false;
    // Scene reconstruction also installs the snapshot camera. That camera
    // belongs to the saved draft, while each participant's live browsing
    // camera belongs to that participant, including during remote commits.
    std::optional<LocalView> local_view;
    std::string view_error;
    if(void* camera=view_camera(view_error,required)){
        LocalView captured;read_view(camera,captured,required);
        if(finite_view(captured))local_view=captured;
    }
    if(!checked())return false;
    auto s=state();
    if(!checked())return false;
    if (!s.ready || s.gesture || applying) { error="Native Adventure Editor cannot restore during this transition"; return false; }
    SceneOwner metadata_owner;
    const auto metadata_scope=C.gesture_scope?C.gesture_scope():GestureScope{};
    if(snapshot.metadata && !capture_scene_owner(metadata_owner,error))return false;
    if(!checked())return false;
    const bool rebuild_terrain=snapshot.document.settings.contains(editor_native_terrain::kPlanetObject) &&
        !same_terrain(materialized_terrain?*materialized_terrain:act_identity,snapshot.document);
    if(rebuild_terrain && (!terrain_available || !skg::readable(terraform_mode(),0x1CC))){error="Native terrain regeneration is unavailable";return false;}
    if(!validate(snapshot,snapshot.document,error))return false;
    if(!checked())return false;
    Owned copy(parse(snapshot.bytes,error));
    if (!copy.p) return false;
    if(!checked())return false;
    if(rebuild_terrain){
        std::function<bool()> parent_current=[]{return true;};
        if(fence)parent_current=[pin=*fence]{return pin.checked();};
        if(!prepare_terrain_load_input(copy.p,snapshot.bytes,std::move(parent_current),error) || !checked())return false;
    }
    // Camera state belongs to this editor. Native Undo's full copy includes it.
    // Preserve the native local resource camera before that copy is applied.
    void* live=resource(data(mode()));
    refresh_selection();
    if(!checked())return false;
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
    // Complete the private entry owner even if allocation ran callbacks. Its
    // verified constructor stores this pointer and AddRefs only our snapshots.
    void* entry_owner=fence?reinterpret_cast<void*>(fence->owner.data):data(mode());
    Owned entry(skg::thiscall<void*>(a_entry,memory,entry_owner,copy.p,copy.p));
    addref(entry.p); // Own cleanup before any later callback can expire the scene.
    if(!checked())return false;
    if(snapshot.metadata) {
        const auto assign=H->sym("SporeCoop::Editor::AssignMetadataText");
        if(!checked())return false;
        if(!assign){error="The native metadata text adapter is unavailable";return false;}
        for(size_t i=0;i<snapshot.metadata->text.size();++i) {
            const auto& text=snapshot.metadata->text[i];
            if(text.size()>4096 || text.find(u'\0')!=std::u16string::npos){error="Invalid native metadata text";return false;}
        }
        for(size_t i=0;i<snapshot.metadata->text.size();++i) {
            const auto& text=snapshot.metadata->text[i];
            if(!checked())return false;
            skg::thiscall<void>(assign,skg::ptr_add<void>(data(mode()),0x4C+16*i),text.data(),text.data()+text.size());
            if(!checked())return false;
        }
        skg::field<uint32_t>(data(mode()),0x7C)=snapshot.metadata->genre;
    }
    if(!checked())return false;
    struct Applying {bool prior=applying;~Applying(){applying=prior;}} restore_applying;
    applying=true;
    // RestoreHistorySnapshot rebuilds through the current native act. Clamp
    // before its copy when deletion removed the old last act.
    if(s.act>=snapshot.document.acts.size()){
        original_select_act(uint32_t(snapshot.document.acts.size()-1));if(!checked())return false;
    }
    log_restore_quaternions(snapshot,copy.p,"parsed-before-restore");
    if(!checked())return false;
    skg::thiscall<void>(a_restore,entry.p,copy.p);
    if(!checked())return false;
    log_restore_quaternions(snapshot,resource(data(mode())),"after-restore");
    if(!checked())return false;
    original_select_act(local_act);
    if(!checked())return false;
    log_restore_quaternions(snapshot,resource(data(mode())),"after-act-selection");
    if(!checked())return false;
    if(rebuild_terrain){
        TerrainRotationOwner rotation_owner;std::vector<TerrainRotation> rotations;
        if(!pin_terrain_rotations(snapshot,fence,rotation_owner,rotations,error) || !checked())return false;
        original_terrain_load(terraform_mode(),skg::ptr_add<void>(copy.p,0x18));
        if(!checked() || !rotation_owner.current()){error="Native terrain loading changed its restoration identity";return false;}
        terrain_rebuild(terraform_mode());
        if(!checked() || !rotation_owner.current()){error="Native terrain rebuilding changed its restoration identity";return false;}
        log_restore_quaternions(snapshot,resource(data(mode())),"after-terrain-rebuild");
        if(!checked())return false;
        if(!restore_terrain_rotations(rotation_owner,rotations,error) || !checked())return false;
        log_restore_quaternions(snapshot,resource(data(mode())),"after-terrain-rotation-refresh");
        if(!checked())return false;
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
    if(!checked())return false;
    if(!selected.empty()){
        void* selected_marker=marker_for_id(resource(data(mode())),selected.front());
        void* object=selected_marker?skg::thiscall<void*>(a_object,data(mode()),selected_marker):nullptr;
        if(!checked())return false;
        void* tools=*reinterpret_cast<void**>(a_tools);
        if(tools)original_select(tools,object);
        if(!checked())return false;
    }
    refresh_selection();
    if(!checked())return false;
    log_restore_quaternions(snapshot,resource(data(mode())),"after-placement-selection");
    if(!checked())return false;
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
    if(snapshot.metadata && !notify_metadata_caption(metadata_owner,s.generation,metadata_scope,*snapshot.metadata,error,required))return false;
    if(!checked())return false;
    ++restores;
    if(local_view){
        if(terrain_rebuilding){
            const auto read_scope=value_scope();
            void* current_mode=mode();void* current_data=data(current_mode);void* current_resource=resource(current_data);
            if(!skg::readable(current_resource,kResourceSize)){error="The deferred local view lost its native resource";return false;}
            deferred_local_view=DeferredLocalView{*local_view,read_scope,generation,current_mode,current_data,current_resource,
                skg::field<editor::ResourceKey>(current_resource,8)};
            error.clear();
        }else if(fence){
            if(!restore_view_fenced(*local_view,fence->generation,fence->owner,fence->scope,fence->current,error))return false;
        }else if(!restore_view(*local_view,generation,error))return false;
    }
    if(!checked())return false;
    if(rebuild_terrain)request_environment_refresh();
    return checked();
}
bool restore(const Snapshot& snapshot,std::string& error){return restore_impl(snapshot,nullptr,error);}
bool restore_fenced(const Snapshot& snapshot,uint64_t expected_generation,SceneOwner owner,GestureScope scope,
                    std::function<bool()> current,std::string& error){
    try{
        const auto copied_snapshot=snapshot;
        const RestoreFence fence{expected_generation,owner,scope,std::move(current)};
        return restore_impl(copied_snapshot,&fence,error);
    }catch(const std::bad_alloc&){
        error="The pinned native restoration could not allocate its owned state";return false;
    }catch(...){
        error="The pinned native restoration failed during a callback";return false;
    }
}

bool rebaseline(std::string& error){
    clear_terrain_capture_rotations();
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
    ServiceScope service_scope;
    return apply(document,editor::Change{},nullptr,error);
}

namespace {
#include "editor_native_pose_prepare_impl.h"
}
static bool prepare_impl(const Snapshot& base,const editor::Document& document,const editor::Change& change,
             const Snapshot* artifact,Snapshot& out,std::string& error,const PreparedPoseContext* pin=nullptr){
    if(!supported || document.placements.size()>kPlacementLimit || document.acts.empty()){
        error="The detached native editor resource is unavailable or exceeds its limits";return false;
    }
    bool tracked=false;if(!relative_scale_mode(document,tracked,error))return false;
    // No capture/state/restore calls belong here: the host may currently own a
    // creature/building editor and have no Adventure Editor scene at all.
    if(pin&&!pin->current()){error="The prepared pose input belongs to an expired native context";return false;}
    if(!validate(base,base.document,error))return false;
    if(pin&&!pin->current()){error="The prepared pose input changed during base validation";return false;}
    std::set<editor::ObjectId> pose_targets;
    if(exact_pose_change(base.document,document,change,pose_targets)){
        editor::Limits limits;limits.placements=kPlacementLimit;
        if(!editor::valid(document,limits,error))return false;
        std::optional<PreparedPoseContext> context;
        std::shared_ptr<const ProjectedSnapshot> base_projection;
        if(pin){
            try{
                context=*pin;base_projection=projected_snapshot(base,error);
                if(!context->current()){error="The prepared pose input belongs to an expired native context";return false;}
            }catch(...){error="The prepared pose input context callback failed";return false;}
        }
        Owned posed(parse(base.bytes,error));if(!posed.p)return false;
        if(context&&!context->current()){error="The prepared pose input changed during NativeRead";return false;}
        std::vector<PreparedPoseOriginal> originals;
        for(auto id:pose_targets){
            auto binding=std::find_if(base.bindings.begin(),base.bindings.end(),[&](const auto& b){return b.object==id;});
            auto* marker=binding==base.bindings.end()?nullptr:marker_for_key(posed.p,binding->marker);
            const auto& p=document.placements.at(id);float relative=0;
            if(!marker || skg::field<uint32_t>(marker,0)!=uint32_t(p.class_id.id) || !relative_scale_value(p,tracked,relative,error)){
                error="The owned pose target has no verified native marker";return false;
            }
            PreparedPoseOriginal original;original.marker=binding->marker;
            std::memcpy(original.transform.data(),skg::ptr_add<void>(marker,4),original.transform.size());
            std::memcpy(original.companions.data(),skg::ptr_add<void>(marker,0x1E0),original.companions.size());
            original.elevation=skg::field<uint32_t>(marker,0x220);original.flags=skg::field<uint32_t>(marker,0x224);
            original.relative=tracked || base.document.placements.at(id).scale!=p.scale;
            original.pitch=p.properties.contains(kSoundPitchProperty);original.gain=p.properties.contains(kSoundGainProperty);
            original.destination=p.properties.contains(kDestinationPositionProperty);original.height=p.properties.contains(kElevationProperty);
            originals.push_back(original);
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
        Snapshot prepared;
        {WorkTimer copy(native_work.snapshot_copies);prepared=snapshot_values::with_document(base,document);}
        if(!serialize(posed.p,prepared.bytes,error))return false;
        if(context){
            const auto derived=prepared_pose_projection(*context,base_projection,base,prepared,posed,originals,error);
            if(derived==RecentMatches::Error)return false;
            if(derived==RecentMatches::Match){++pose_roundtrip_hits;out=std::move(prepared);error.clear();return true;}
            // Failed private owners have now finished their Release callbacks.
            // A fallback must not repin a replacement value context.
            if(!context->current()){error="The prepared pose context expired during private cleanup";return false;}
            error.clear();
        }
        if(!validate(prepared,document,error))return false;
        // Inspect the actual serialized after image, not the mutable producer.
        // Reversing only the verified primitive writes must recover every byte
        // of the validated base. This retains the opaque-field proof without
        // cloning all class/act rows and rebuilding every unrelated marker.
        bool exact=false;std::string roundtrip_error;
        Owned roundtrip(parse(prepared.bytes,roundtrip_error));
        if(roundtrip.p){
            bool restored=true;
            for(const auto& original:originals){
                auto* marker=marker_for_key(roundtrip.p,original.marker);if(!marker){restored=false;break;}
                std::memcpy(skg::ptr_add<void>(marker,4),original.transform.data(),0x20);
                if(original.relative)std::memcpy(skg::ptr_add<void>(marker,0x24),original.transform.data()+0x20,4);
                if(original.pitch)std::memcpy(skg::ptr_add<void>(marker,0x1E0),original.companions.data(),4);
                if(original.gain)std::memcpy(skg::ptr_add<void>(marker,0x1E4),original.companions.data()+4,4);
                if(original.destination)std::memcpy(skg::ptr_add<void>(marker,0x1E8),original.companions.data()+8,0x20);
                if(original.height)skg::field<uint32_t>(marker,0x220)=original.elevation;
                auto& flags=skg::field<uint32_t>(marker,0x224);flags=(flags&~0x16u)|(original.flags&0x16u);
            }
            std::vector<uint8_t> restored_bytes;
            exact=restored && serialize(roundtrip.p,restored_bytes,roundtrip_error) && restored_bytes==base.bytes;
        }
        if(exact)++pose_roundtrip_hits;
        else{
            ++pose_roundtrip_fallbacks;
            // Noncanonical old artifacts and allocation failures retain the
            // existing complete normalization/opaque comparison path.
            Gesture checked;checked.before=base;checked.after=prepared;
            if(!supported_changes(checked,error))return false;
        }
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

bool prepare(const Snapshot& base,const editor::Document& document,const editor::Change& change,
             const Snapshot* artifact,Snapshot& out,std::string& error){
    ServiceScope service_scope;
    // A callback may retire the caller's pending canonical inputs. The narrow
    // pose path uses independent values before its first Native validation;
    // private preparation acquires no live-scene or authoring permission.
    struct Inputs {Snapshot base;editor::Document document;editor::Change change;};
    std::set<editor::ObjectId> targets;
    const auto& patch=change.patch;
    const bool candidate=!patch.fields.empty() && patch.objects.empty() && patch.classes.empty() && !patch.acts && patch.terrain.empty() &&
                         prepared_pose_fields(base.document,document,change);
    if(!candidate)return prepare_impl(base,document,change,artifact,out,error);
    try{
        editor::Limits limits;limits.placements=kPlacementLimit;
        if(base.bytes.empty() || base.bytes.size()>kLimit || base.bindings.size()>kPlacementLimit ||
           editor::bytes(base.document)>limits.document_bytes || editor::bytes(document)>limits.document_bytes ||
           editor::bytes(change.patch)>limits.document_bytes || editor::bytes(change.inverse)>limits.document_bytes ||
           change.summary.size()>limits.summary_bytes || change.allocated.size()>kPlacementLimit || change.clone_sources.size()>kPlacementLimit){
            error="The detached native preparation inputs exceed their owned value bounds";return false;
        }
        if(base.metadata)for(const auto& text:base.metadata->text)if(text.size()>4096){error="Invalid native metadata text";return false;}
        if(exact_pose_change(base.document,document,change,targets)){
            auto retained=std::make_unique<Inputs>(Inputs{base,document,change});
            ScopedResolutionAudit resolution;
            PreparedPoseContext pin;
            Snapshot candidate;
            if(!prepare_impl(retained->base,retained->document,retained->change,nullptr,candidate,error,&pin))return false;
            // All private producer/fallback owners have finished their Release
            // callbacks before the caller can observe the result.
            if(!pin.current()){error="The prepared pose context expired during private cleanup";return false;}
            auto closure=projected_snapshot(candidate,error);
            if(resolution.audit.failed){error="The prepared pose asset resolution changed during preparation";return false;}
            if(!closure && !validate(candidate,retained->document,error))return false;
            if(!pin.current() || resolution.audit.failed){error="The prepared pose context expired during final projection closure";return false;}
            out=std::move(candidate);error.clear();return true;
        }
    }catch(const std::bad_alloc&){error="The prepared pose inputs could not be retained independently";return false;}
    catch(...){error="The prepared pose input context callback failed";return false;}
    return prepare_impl(base,document,change,artifact,out,error);
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

bool crew_assets(const editor::Document& document,std::vector<editor::ResourceKey>& out,std::string& error){
    if(!document.settings.contains(kCrewObject)){
        if(document.settings_schema.contains(kCrewRosterProperty) || document.settings_schema.contains(kCrewCapacityProperty)){
            error="The advertised adventure crew roster is missing";return false;
        }
        out.clear();error.clear();return true;
    }
    Owned fragment;Rows crew;if(!canonical_crew(document,fragment,crew,error))return false;
    std::vector<editor::ResourceKey> keys;
    for(auto row=crew.begin;row!=crew.end;row+=0x38){
        editor::ResourceKey key;if(!crew_primary_asset(row,key,error))return false;keys.push_back(key);
    }
    out=std::move(keys);error.clear();return true;
}
bool remap_crew_assets(const Snapshot& base,const std::vector<CrewAssetRemap>& edits,Snapshot& out,std::string& error){
    if(!supported || edits.size()>3){error="The adventure crew remap exceeds its allowed slots";return false;}
    if(!validate(base,base.document,error))return false;
    if(edits.empty()){out=base;error.clear();return true;}
    Owned fragment;Rows crew;if(!canonical_crew(base.document,fragment,crew,error))return false;
    const auto count=size_t((crew.end-crew.begin)/0x38);std::set<uint32_t> slots;
    for(const auto& edit:edits){
        if(edit.slot>=count || !slots.insert(edit.slot).second || !edit.expected.instance || !edit.expected.type || !edit.expected.group ||
           !edit.replacement.instance || edit.replacement.type!=edit.expected.type || edit.replacement.group!=edit.expected.group ||
           skg::field<editor::ResourceKey>(crew.begin+edit.slot*0x38,0)!=edit.expected){
            error="The adventure crew asset slot changed before versioning";return false;
        }
        auto* row=crew.begin+edit.slot*0x38;skg::field<editor::ResourceKey>(row,0)=edit.replacement;
        skg::field<uint32_t>(row,0xC)=0;skg::field<uint64_t>(row,0x10)=UINT64_MAX;
    }
    auto wanted=base.document;auto& block=std::get<editor::StructuredValue>(wanted.settings.at(kCrewObject).properties.at(kCrewRosterProperty));
    if(!serialize(fragment.p,block.bytes,error))return false;
    Snapshot result;
    if(!edit_owned_snapshot(base,[&](void* owned,std::string& reason){
        Rows current;if(!rows(owned,0x178,0x38,3,current) || size_t((current.end-current.begin)/0x38)!=count){
            reason="The native adventure crew roster changed before versioning";return false;
        }
        // Preflight every slot before the first owned write. Duplicate assets
        // in other slots retain their original keys, locks and opaque bytes.
        for(const auto& edit:edits){
            editor::ResourceKey actual;
            if(!crew_primary_asset(current.begin+edit.slot*0x38,actual,reason))return false;
            if(actual!=edit.expected){reason="The native adventure crew asset slot changed before versioning";return false;}
        }
        for(const auto& edit:edits){auto* row=current.begin+edit.slot*0x38;
            skg::field<editor::ResourceKey>(row,0)=edit.replacement;skg::field<uint32_t>(row,0xC)=0;skg::field<uint64_t>(row,0x10)=UINT64_MAX;
        }
        // Native Read/Write omit the external ScenarioResource key at +8.
        // Rehydrate this owned copy from the validated envelope, as prepare
        // does, so the exact identity guard below still checks its provenance.
        skg::field<editor::ResourceKey>(owned,8)=base.source;
        return true;
    },result,error))return false;
    // Revisions, allocation counters, stable IDs and every unrelated typed
    // value come from the caller's draft, not a fresh native projection.
    result.document=std::move(wanted);result.captain=base.captain;result.captain_locked=base.captain_locked;
    if(result.source!=base.source || result.act!=base.act || result.bindings!=base.bindings || result.metadata!=base.metadata){
        error="The adventure crew version changed an unrelated native identity";return false;
    }
    if(!validate(result,result.document,error))return false;
    Gesture checked;checked.before=base;checked.after=result;
    if(!supported_changes(checked,error))return false;
    out=std::move(result);error.clear();return true;
}

bool apply_prepared(const Snapshot& prepared,const editor::Change& change,std::string& error){
    ServiceScope service_scope;
    const auto current=state();
    if(!current.ready || current.gesture || applying){error="The native editor is not ready to apply its canonical resource";return false;}
    clear_terrain_capture_rotations();
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
    ServiceScope service_scope;
    // Runtime supplies its independently canonical owned artifact for a terrain
    // inverse. Pin the original scene before InspectorView or snapshot getters;
    // a normalized typed pose must not let the generic field loop skip LIVE
    // grounding, nor can a terrain-only inverse leave the changed recipe live.
    const auto terrain_inverse=terrain_capture_rotations;
    if(terrain_inverse && artifact && artifact->document==document){
        try{
        const Snapshot canonical=*artifact;
        const auto dependencies=C.validate_dependencies;
        const auto canonical_head=C.canonical_revision;const auto expected_head=canonical.document.revision;
        const auto native_generation=generation;
        void* m=mode();void* d=data(m);void* r=resource(d);
        if(!skg::readable(r,kResourceSize)){error="The canonical terrain inverse lost its original native scene";return false;}
        const SceneOwner owner{uintptr_t(m),uintptr_t(d),uintptr_t(r),skg::field<editor::ResourceKey>(r,8)};
        const auto projection=C.projection_scope?C.projection_scope:C.gesture_scope;
        const auto authorizing=C.gesture_scope;const auto revisions=C.object_revision;
        const auto scope=projection?projection():GestureScope{};const auto author=authorizing?authorizing():GestureScope{};
        const auto rows=terrain_inverse->canonical;
        const auto context=[native_generation,scope,author,projection,authorizing,revisions,rows,canonical_head,expected_head]{
            try{
                if(generation!=native_generation || !valid_scope(scope) || !valid_scope(author) || !projection ||
                   projection()!=scope || (authorizing && authorizing()!=author) || !canonical_head ||
                   canonical_head()!=std::optional<editor::Revision>{expected_head})return false;
                for(const auto& row:rows){const auto revision=revisions?revisions(row.object):std::nullopt;
                    if(!revision || *revision!=row.revision)return false;}
                return generation==native_generation && projection()==scope && (!authorizing || authorizing()==author) &&
                    canonical_head()==std::optional<editor::Revision>{expected_head};
            }catch(...){return false;}
        };
        const auto target=[&]{
            if(canonical.document.project!=scope.project || canonical.document.epoch!=scope.epoch || canonical.document.placements.size()!=rows.size())return false;
            for(const auto& row:rows){auto wanted=canonical.document.placements.find(row.object);
                if(wanted==canonical.document.placements.end() || uint32_t(wanted->second.class_id.id)!=row.class_index ||
                   wanted->second.revision!=row.revision || !editor_native_terrain_rotations::exact(wanted->second.rotation,row.q))return false;}
            return true;
        };
        if(!context() || !target() || !terrain_capture_rotations_current(terrain_inverse,error) ||
           terrain_inverse->generation!=native_generation || uintptr_t(terrain_inverse->mode)!=owner.mode ||
           uintptr_t(terrain_inverse->data)!=owner.data || uintptr_t(terrain_inverse->resource)!=owner.resource || terrain_inverse->source!=owner.source){
            if(error.empty())error="The canonical terrain inverse has no current owned authority";return false;
        }
        const auto expired=[&]{if(error.empty())error="The canonical terrain inverse lost its original owned authority";return false;};
        if(!validate(canonical,canonical.document,error))return false;
        if(!context() || !terrain_capture_rotations_current(terrain_inverse,error))return expired();
        if(dependencies){
            if(!dependencies(canonical,error)){
                if(error.empty())error="The canonical terrain inverse dependencies were rejected";return false;
            }
            if(!context() || !terrain_capture_rotations_current(terrain_inverse,error))return expired();
        }
        // The restore deliberately retires the q ticket. Its copied current
        // predicate therefore fences context/revisions independently of that
        // retired serial, while restore_fenced pins fresh owner/source values.
        return restore_fenced(canonical,native_generation,owner,scope,context,error);
        }catch(...){error="The canonical terrain inverse could not retain its owned restoration proof";return false;}
    }
    InspectorView inspector;
    if(document.placements.size()>kPlacementLimit){error="Native editor placement limit exceeded";return false;}
    bool tracked=false;if(!relative_scale_mode(document,tracked,error))return false;
    Snapshot snapshot;
    if (!capture(snapshot,error)) return false;
    clear_terrain_capture_rotations();
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
WorkStats work_stats(){
    auto result=native_work;LARGE_INTEGER frequency{};
    if(QueryPerformanceFrequency(&frequency)&&frequency.QuadPart>0)result.frequency=uint64_t(frequency.QuadPart);
    result.parsed_entries=parsed_images.size();result.parsed_bytes=parsed_image_bytes;
    result.parsed_owners=parsed_owner_count;result.parsed_owner_bytes=parsed_owner_bytes;return result;
}
std::string status() {
    auto s=state();const auto resume=creation_scene_state({});const auto cache=projection_cache_stats();const auto acts=act_projection_cache.stats();char b[1536];
    snprintf(b,sizeof b,"editor_native available=%u editing=%u ready=%u playtesting=%u entry=%u act=%u generation=%llu gesture=%u history=%u/%u captures=%llu restores=%llu rejected=%llu cancellation=%u cancellation_serial=%llu ordinary_cancellations=%llu class_cache_bytes=%zu class_cache_entries=%zu class_cache_hits=%llu class_cache_misses=%llu act_cache_bytes=%zu act_cache_entries=%zu act_cache_hits=%llu act_cache_misses=%llu projection_proof_bytes=%zu projection_proof_entries=%zu projection_proof_hits=%llu projection_proof_misses=%llu recent_match_hits=%llu recent_match_different=%llu recent_match_errors=%llu pose_roundtrip_hits=%llu pose_roundtrip_fallbacks=%llu resume_available=%u resume_initialization=%u resume_enter_pending=%u resume_transitioning=%u world_visible=%u",
        s.available,s.editing,s.ready,s.playtesting,s.entry,s.act,(unsigned long long)s.generation,s.gesture,
        s.history_cursor,s.history_size,(unsigned long long)captures,(unsigned long long)restores,(unsigned long long)rejected,
        unsigned(cancellation.has_value()),(unsigned long long)cancellation_counter,(unsigned long long)ordinary_cancellations,
        cache.bytes,cache.entries,(unsigned long long)cache.hits,(unsigned long long)cache.misses,
        acts.bytes,acts.entries,(unsigned long long)acts.hits,(unsigned long long)acts.misses,
        projected_snapshot_bytes,projected_snapshots.size(),(unsigned long long)projected_snapshot_hits,(unsigned long long)projected_snapshot_misses,
        (unsigned long long)recent_match_hits,(unsigned long long)recent_match_different,(unsigned long long)recent_match_errors,
        (unsigned long long)pose_roundtrip_hits,(unsigned long long)pose_roundtrip_fallbacks,
        unsigned(resume.available),resume.initialization,unsigned(resume.enter_pending),unsigned(resume.transitioning),unsigned(resume.world_visible));
    std::string result=b;const auto work=work_stats();char detail[1024];
    editor::ComplexityCapability complexity;std::string complexity_error;
    read_complexity_capability(complexity,complexity_error);
    snprintf(detail,sizeof detail," native_work_frequency=%llu native_read_calls=%llu native_read_ticks=%llu native_write_calls=%llu native_write_ticks=%llu native_projection_calls=%llu native_projection_ticks=%llu native_resolution_calls=%llu native_resolution_ticks=%llu native_supported_calls=%llu native_supported_ticks=%llu parsed_image_hits=%llu parsed_image_misses=%llu parsed_image_entries=%zu parsed_image_bytes=%zu parsed_image_owners=%zu parsed_image_owner_bytes=%zu parsed_image_peak_owners=%zu parsed_image_peak_bytes=%zu pose_capture_hits=%llu pose_capture_fallbacks=%llu",
        (unsigned long long)work.frequency,(unsigned long long)work.reads.calls,(unsigned long long)work.reads.ticks,
        (unsigned long long)work.writes.calls,(unsigned long long)work.writes.ticks,
        (unsigned long long)work.projections.calls,(unsigned long long)work.projections.ticks,
        (unsigned long long)work.resolutions.calls,(unsigned long long)work.resolutions.ticks,
        (unsigned long long)work.supported.calls,(unsigned long long)work.supported.ticks,
        (unsigned long long)work.parsed_hits,(unsigned long long)work.parsed_misses,work.parsed_entries,work.parsed_bytes,
        work.parsed_owners,work.parsed_owner_bytes,work.parsed_peak_owners,work.parsed_peak_bytes,
        (unsigned long long)work.pose_captures,(unsigned long long)work.pose_fallbacks);
    result+=detail;
    constexpr std::array<const char*,6> phase_names{"before","after_bytes","after_projection","canonical","opaque","delivery"};
    snprintf(detail,sizeof detail," native_snapshot_copy_calls=%llu native_snapshot_copy_ticks=%llu",
        (unsigned long long)work.snapshot_copies.calls,(unsigned long long)work.snapshot_copies.ticks);
    result+=detail;
    for(size_t i=0;i<phase_names.size();++i){
        const auto prefix=" native_capture_"+std::string(phase_names[i]);
        result+=prefix+"_calls="+std::to_string(work.staged[i].calls)+prefix+"_ticks="+std::to_string(work.staged[i].ticks)+
            prefix+"_begin="+std::to_string(capture_last_begin[i])+prefix+"_end="+std::to_string(capture_last_end[i])+
            prefix+"_frame="+std::to_string(capture_last_frame[i]);
    }
    return result+" opaque_pose_hits="+std::to_string(opaque_pose_hits)+
        " opaque_pose_fallbacks="+std::to_string(opaque_pose_fallbacks)+
        " history_ui_updates="+std::to_string(history_ui_updates)+
        " history_ui_undo="+std::to_string(history_ui_undo)+
        " history_ui_redo="+std::to_string(history_ui_redo)+
        " history_ui_changes="+std::to_string(history_ui_changes)+
        " history_ui_expired="+std::to_string(history_ui_expired)+
        " environment_ui_updates="+std::to_string(environment_ui_updates)+
        " environment_ui_expired="+std::to_string(environment_ui_expired)+
        " complexity_available="+std::to_string(unsigned(complexity.available))+
        " complexity_reason="+std::to_string(unsigned(complexity.reason))+
        " complexity_conflicts="+std::to_string(complexity_conflicts)+
        " complexity_conflict="+complexity_conflict.substr(0,512);
}
} // namespace editor_native
