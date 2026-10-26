#include "editor_native.h"
#include "editor_native_bindings.h"

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
bool supported, applying, queued;
bool fence_queued;
double last_fence;
std::map<sui::Win,uint32_t> fenced_windows;
uint64_t generation = 1, captures = 0, restores = 0, rejected = 0;
void* observed_mode;
void* observed_data;
bool observed_scenario;
std::map<uint32_t, editor::ObjectId> bindings;
std::map<uint32_t,uint32_t> incarnations;
std::vector<editor::ObjectId> selection;
uint32_t selected_class;
std::optional<editor::ObjectId> input_clone, gesture_clone;
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
editor::ObjectId marker_id(uint32_t key,const std::map<uint32_t,editor::ObjectId>* supplied=nullptr) {
    const auto& source=supplied?*supplied:bindings;
    auto it = source.find(key);
    auto incarnation=incarnations.find(key);
    return it != source.end() ? it->second : editor::ObjectId{(1ull << 32) | key,
        incarnation==incarnations.end()?1:incarnation->second};
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
                const std::map<uint32_t,editor::ObjectId>* supplied=nullptr) {
    Rows classes, markers, acts;
    if (!rows(r, 0x2BF4, 0x27E8, 1024, classes) || !rows(r, 0x2C10, 0x238, kPlacementLimit, markers) ||
        !rows(r, 0x23C, 0x534, 8, acts)) {
        error = "Native editor resource vectors are invalid"; return false;
    }
    out.document = {};
    out.document.next_id = 4ull << 32;
    for (auto p = classes.begin; p != classes.end; p += 0x27E8)
        out.document.classes.insert({(2ull << 32) | skg::field<uint32_t>(p, 0), 1});
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
        if (!out.document.classes.count(placement.class_id)) {
            error = "Native editor marker has no scenario class"; return false;
        }
        out.document.placements.emplace(placement.object, placement);
        out.document.next_id=std::max(out.document.next_id,placement.object.id+1);
        out.bindings.push_back({placement.object, key});
    }
    // Goals name classes, so every matching placement is conservatively retained.
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
HistoryFn original_history_undo,original_history_redo;
using HistoryQueryFn=bool(SK_THISCALL*)(void*);
HistoryQueryFn original_has_undo,original_has_redo;
struct HeldActor { editor::ObjectId id; void* object; };
std::vector<HeldActor> gesture_actors;
void* gesture_before_resource;
std::map<uint32_t,editor::ObjectId> gesture_before_bindings;
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
CloneSelectionFn original_clone_selection;
using ScriptInputFn=bool(SK_THISCALL*)(void*,void*,void*);
ScriptInputFn original_script_input;
struct Pending {
    void* entry;
    void* before;
    void* owner;
    uint64_t generation;
    uint32_t act;
    std::optional<editor::ObjectId> clone_source;
    std::map<uint32_t,editor::ObjectId> before_bindings,after_bindings;
};
std::deque<Pending> pending;

void SK_CDECL drain(void*) {
    editor_native::state(); // Retire a replaced native owner before old callbacks.
    queued = false;
    auto work = std::move(pending);
    pending.clear();
    for (const auto& item : work) {
        Owned entry(item.entry),before(item.before);
        if (!active() || applying || item.owner != data(mode()) || item.generation != generation) continue;
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
    bool observe=!applying && active() && self==data(mode()) && *reinterpret_cast<uint8_t*>(a_history+0x20);
    if(observe){
        gesture_clone=input_clone;
        release(gesture_before_resource);
        gesture_before_resource=construct();
        if(gesture_before_resource)skg::thiscall<void*>(a_copy,gesture_before_resource,resource(self));
        // Native Start reuses its previous entry.after, which is stale after
        // canonical remote application. This independent owned before is the
        // current authored resource; serialize it only in the deferred drain.
    }
    original_start(self);
    if(observe && !*reinterpret_cast<uint8_t*>(a_history+0x20)){
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
void SK_THISCALL select_native(void* self,void* object){
    original_select(self,object);
    if(!applying && editing())refresh_selection();
}
uint32_t SK_THISCALL tool_native(void* self,uint32_t action,uint32_t a,uint32_t b){
    editor_native::state();
    if(!applying && active() && (action==1 || action==6 || action==11)){
        refresh_selection();
        void* context=*reinterpret_cast<void**>(a_context);
        // Patrol-node editing changes opaque per-act paths and has no registered
        // semantic transaction in this preview.
        if(skg::readable(context,0x14) && skg::field<void*>(context,0x10)){++rejected;return 1;}
        if(selection.empty() || !C.gate || !C.gate(selection) ||
            (action==11 && (!C.structure_gate || !C.structure_gate(selected_class)))){
            ++rejected;return 1;
        }
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
        refresh_selection();
        if(selection.empty() || !C.gate || !C.gate(selection)) {++rejected;return;}
    }
    original_delete(self);
}
bool SK_THISCALL create_native(void* self,uint32_t class_id){
    editor_native::state();
    if(!applying && active()){
        // Native EFEDC0 routes class -2 to authored avatar placement F432C0,
        // which is outside the shared placement roster and protected scope.
        if(class_id==UINT32_MAX-1 || !C.structure_gate || !C.structure_gate(class_id)){++rejected;return false;}
    }
    return original_create(self,class_id);
}
bool SK_THISCALL clone_selection_native(void* self,void* object){
    editor_native::state();
    std::optional<editor::ObjectId> source;
    if(!applying && active()){
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
        ++rejected;
        H->log(M,SK_INFO,"Shared editor: class import is read-only; load templates before sharing");
        return UINT32_MAX;
    }
    return original_import(self,key);
}
void* script_ui(){
    void* display=skg::readable(mode(),0x18)?skg::field<void*>(mode(),0x14):nullptr;
    return skg::readable(display,0x1C)?skg::field<void*>(display,0x18):nullptr;
}
bool SK_THISCALL script_input(void* self,void* window,void* message){
    auto* script=static_cast<uint8_t*>(script_ui());
    if(!applying && active() && script && self==script+8){
        // This IWinProc owns authored act, goal, class and text controls. None
        // has a shared semantic transaction yet. Do not invoke their setters;
        // leave the event unhandled so independent camera/tool procs still run.
        return false;
    }
    return original_script_input(self,window,message);
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
    if (!before || pending.size() >= 8) {
        release(entry);release(before); ++rejected;
        if (C.failure) C.failure(before?"Native editor gestures exceeded the pending transaction limit":"Native editor before snapshot allocation failed");
        return;
    }
    uint32_t act = skg::cdecl_call<uint32_t>(a_act);
    pending.push_back({entry,before,self,generation,act,gesture_clone,gesture_before_bindings,bindings});
    if (!queued) { queued = true; H->run_outside_frame(drain, nullptr); }
}
void SK_THISCALL undo_native(void* self) {
    if (!applying && active()) { if (C.history) C.history(false); return; }
    original_undo(self);
}
void SK_THISCALL redo_native(void* self) {
    if (!applying && active()) { if (C.history) C.history(true); return; }
    original_redo(self);
}
void SK_THISCALL history_undo(void* self){
    if(!applying && active() && self==reinterpret_cast<void*>(a_history)){
        if(C.history)C.history(false);return;
    }
    original_history_undo(self);
}
void SK_THISCALL history_redo(void* self){
    if(!applying && active() && self==reinterpret_cast<void*>(a_history)){
        if(C.history)C.history(true);return;
    }
    original_history_redo(self);
}
bool SK_THISCALL has_undo(void* self){
    if(!applying && active() && self==reinterpret_cast<void*>(a_history))
        return C.history_available && C.history_available(false);
    return original_has_undo(self);
}
bool SK_THISCALL has_redo(void* self){
    if(!applying && active() && self==reinterpret_cast<void*>(a_history))
        return C.history_available && C.history_available(true);
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
void SK_CDECL fence_ui(void*){
    fence_queued=false;
    if(!supported || !active()){restore_fences();return;}
    // Ordinary native Save/Done/Cancel are brokered by the collaboration panel.
    // Its verified save adapter can still show and complete the normal save UI.
    constexpr uint32_t ids[]={0x07466E50,0x07104132,0x07104131,0x04766FF0,
        0x07108388,0xBBBB1001,0x90439D7C,0x06FCB638,0x272EB68E};
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
        disable_owned(skg::thiscall<sui::Win>(a_layout_find,layout,uint32_t(0x073E6BA0),true));
    }
}
} // namespace

bool init(const SkHost* host, SkMod* mod, Ctx context) {
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
    a_restore=H->sym("SporeCoop::Editor::RestoreHistorySnapshot");
    a_alloc=H->sym("SporeCoop::Editor::Allocate");
    a_context=H->sym("SporeCoop::Editor::Context");
    a_tools=H->sym("SporeCoop::Editor::ToolManager");
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
    a_insert_marker=H->sym("SporeCoop::Editor::InsertMarker");
    supported = a_mode && a_history && a_act && a_ctor && a_read && a_write && a_copy && a_entry && a_restore && a_alloc &&
        a_context && a_tools && a_marker && a_object && a_clone_marker && a_create_marker && a_delete_marker && a_apply_marker && a_loading && a_layout_find && a_erase_history && a_discard_history && a_copy_marker_rows && a_insert_marker;
    if (!supported) return false;
    supported = install("SporeCoop::Editor::StartHistory", (void*)start, (void**)&original_start) &&
        install("SporeCoop::Editor::CommitHistory", (void*)commit, (void**)&original_commit) &&
        install("SporeCoop::Editor::UndoHistoryEntry", (void*)undo_native, (void**)&original_undo) &&
        install("SporeCoop::Editor::RedoHistoryEntry", (void*)redo_native, (void**)&original_redo) &&
        install("SporeCoop::Editor::SetSelection", (void*)select_native, (void**)&original_select) &&
        install("SporeCoop::Editor::ToolAction", (void*)tool_native, (void**)&original_tool) &&
        install("SporeCoop::Editor::DeleteSelection", (void*)delete_native, (void**)&original_delete) &&
        install("SporeCoop::Editor::PlaceClass", (void*)create_native, (void**)&original_create) &&
        install("SporeCoop::Editor::CloneSelection", (void*)clone_selection_native, (void**)&original_clone_selection) &&
        install("SporeCoop::Editor::ImportClass", (void*)import_native, (void**)&original_import) &&
        install("SporeCoop::Editor::UndoHistory",(void*)history_undo,(void**)&original_history_undo) &&
        install("SporeCoop::Editor::RedoHistory",(void*)history_redo,(void**)&original_history_redo) &&
        install("SporeCoop::Editor::HasUndo",(void*)has_undo,(void**)&original_has_undo) &&
        install("SporeCoop::Editor::HasRedo",(void*)has_redo,(void**)&original_has_redo) &&
        install("SporeCoop::Editor::ScriptInput",(void*)script_input,(void**)&original_script_input);
    return supported;
}

void shutdown() {
    ++generation;
    for (auto item : pending){release(item.entry);release(item.before);}
    release(gesture_before_resource);gesture_before_resource=nullptr;
    pending.clear();
    for(auto actor:gesture_actors)release(actor.object);
    gesture_actors.clear();gesture_before_bindings.clear();
    restore_fences();bindings.clear(); C = {}; supported = false;
}

void tick(){
    if(!H || fence_queued)return;
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
    s.ready=s.editing && display_valid && skg::field<int>(display,0x1C)==0 && skg::readable(container,0x8C) &&
        skg::readable(loading,0x371) && !skg::field<bool>(loading,0x370);
    s.playtesting=s.entry==0 && skg::field<int>(m,0xCC)==2;
    void* context=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    if(s.editing && skg::readable(context,4))s.act=skg::field<uint32_t>(context,0);
    s.selected=selection;
    s.gesture=!*reinterpret_cast<uint8_t*>(a_history+0x20);
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
    return true;
}

bool supported_changes(const Gesture& gesture,std::string& error){
    if(!supported || !a_copy_marker_rows || !a_insert_marker){error="Native authored-change validation is unavailable";return false;}
    Owned before(parse(gesture.before.bytes,error));if(!before.p)return false;
    Owned after(parse(gesture.after.bytes,error));if(!after.p)return false;
    Owned compared_before(construct()),compared_after(construct()),empty(construct());
    if(!compared_before.p || !compared_after.p || !empty.p){error="Native authored-change allocation failed";return false;}
    skg::thiscall<void*>(a_copy,compared_before.p,before.p);
    skg::thiscall<void*>(a_copy,compared_after.p,after.p);
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
        // F412C0 placement validation derives only bits10/2/4 from native
        // collision/placement checks. Other marker flags remain authored.
        const uint32_t old_flags=skg::field<uint32_t>(normalized_old,0x224);
        const uint32_t new_flags=skg::field<uint32_t>(normalized_new,0x224);
        skg::field<uint32_t>(normalized_new,0x224)=(new_flags&~0x16u)|(old_flags&0x16u);
        if(prior->second.scale!=to.scale){
            skg::field<float>(normalized_new,0x20)=skg::field<float>(normalized_old,0x20);
            // Native scaling owns the related normalized scale as well. Without
            // a scale gesture, a change to this field remains unsupported.
            skg::field<float>(normalized_new,0x24)=skg::field<float>(normalized_old,0x24);
        }
    }
    // Camera ownership is local. Every remaining serialized difference is an
    // unsupported authored field, including marker elevation/flags and globals.
    memcpy(skg::ptr_add<void>(compared_after.p,0x2C30),skg::ptr_add<void>(compared_before.p,0x2C30),0x20);
    std::vector<uint8_t> left,right;
    if(!serialize(compared_before.p,left,error) || !serialize(compared_after.p,right,error))return false;
    if(left!=right){error="Shared editing rejected an unsupported avatar, template, act, global or placement property change";return false;}
    error.clear();return true;
}

bool validate(const Snapshot& snapshot,const editor::Document& document,std::string& error){
    if(!supported){error="Native editor validation is unavailable";return false;}
    if(document.placements.size()>kPlacementLimit || snapshot.bindings.size()>kPlacementLimit){
        error="Native editor placement limit exceeded";return false;
    }
    Owned copy(parse(snapshot.bytes,error));if(!copy.p)return false;
    std::map<uint32_t,editor::ObjectId> supplied;
    std::set<editor::ObjectId> ids;
    for(const auto& b:snapshot.bindings){
        if(!b.object.id || !b.object.generation || !supplied.emplace(b.marker,b.object).second || !ids.insert(b.object).second){
            error="Native snapshot has duplicate or invalid placement bindings";return false;
        }
    }
    Snapshot projected;projected.act=snapshot.act;
    if(!projection(copy.p,projected,error,&supplied))return false;
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
           p.rotation!=wanted.rotation || p.scale!=wanted.scale || !wanted.properties.empty() ||
           !std::isfinite(p.scale) || p.scale<=0 || !std::isfinite(p.position.x) ||
           !std::isfinite(p.position.y) || !std::isfinite(p.position.z) || !std::isfinite(p.rotation.x) ||
           !std::isfinite(p.rotation.y) || !std::isfinite(p.rotation.z) || !std::isfinite(p.rotation.w)){
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
    ++restores;
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
    Snapshot snapshot;
    if (!capture(snapshot,error)) return false;
    if (snapshot.document.classes != document.classes || snapshot.document.acts != document.acts) {
        error="Shared editing cannot change native classes or acts yet"; return false;
    }
    for(const auto& [id,to]:document.placements){
        auto from=snapshot.document.placements.find(id);
        if((from!=snapshot.document.placements.end() &&
            (from->second.class_id!=to.class_id || from->second.act_id!=to.act_id || from->second.properties!=to.properties)) ||
           !to.properties.empty() || !document.classes.count(to.class_id) ||
           !std::isfinite(to.scale) || to.scale<=0 || !std::isfinite(to.position.x) ||
           !std::isfinite(to.position.y) || !std::isfinite(to.position.z) ||
           !std::isfinite(to.rotation.x) || !std::isfinite(to.rotation.y) ||
           !std::isfinite(to.rotation.z) || !std::isfinite(to.rotation.w)) {
            error="Unsupported native placement field";return false;
        }
    }
    Owned source(artifact?parse(artifact->bytes,error):nullptr);
    if(artifact && !source.p)return false;
    Owned base(parse(snapshot.bytes,error));if(!base.p)return false;
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
    for(const auto& b:snapshot.bindings){
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
        void* marker=marker_for_id(resource(owner),id);
        if(!marker)return fail("Native transform target is no longer bound");
        float previous=skg::field<float>(marker,0x20);
        float relative=skg::field<float>(marker,0x24);
        if(skg::field<editor::Vec3>(marker,4)==to.position &&
           skg::field<editor::Rotation>(marker,0x10)==to.rotation && previous==to.scale)continue;
        skg::field<editor::Vec3>(marker,4)=to.position;
        skg::field<editor::Rotation>(marker,0x10)=to.rotation;
        skg::field<float>(marker,0x20)=to.scale;
        if(previous>0 && std::isfinite(relative))skg::field<float>(marker,0x24)=relative*(to.scale/previous);
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

bool enter(const editor::ResourceKey& key,std::string& error){
    if(!supported || !key.instance || key.type!=0x366A930D || game::mode_id()!=modes::kMenu){
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

std::string status() {
    auto s=state(); char b[300];
    snprintf(b,sizeof b,"editor_native available=%u editing=%u ready=%u playtesting=%u entry=%u act=%u generation=%llu gesture=%u history=%u/%u captures=%llu restores=%llu rejected=%llu",
        s.available,s.editing,s.ready,s.playtesting,s.entry,s.act,(unsigned long long)s.generation,s.gesture,
        s.history_cursor,s.history_size,(unsigned long long)captures,(unsigned long long)restores,(unsigned long long)rejected);
    return b;
}
} // namespace editor_native
