// Owned-value copy boundaries only. No Native implementation/game calls.
#include "../mods/coop/editor_native_snapshot_values.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <stdexcept>

namespace {
constexpr size_t base_blob=200003,target_blob=300007,base_image=50021,target_image=60013;
bool observe=false;unsigned base_copies=0,target_copies=0,base_image_copies=0,target_image_copies=0,checks=0;
void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
void reset_counts(){base_copies=target_copies=base_image_copies=target_image_copies=0;}
editor::Document document(size_t bytes,uint8_t fill){
    editor::Document value;value.project[0]=fill;value.epoch=7;value.revision=9;
    editor::AuthoringSettings row;row.object={37,2};row.kind=editor::SettingsKind::Project;
    row.properties[0xA1]=editor::StructuredValue{0x12345678,std::vector<uint8_t>(bytes,fill),{{9,3}}};
    value.settings.emplace(row.object,std::move(row));return value;
}
editor_native::Snapshot image(bool next){
    editor_native::Snapshot value;value.bytes.assign(next?target_image:base_image,next?0x41:0x32);
    value.source={next?101u:1u,2,3};value.captain={4,next?105u:5u,6};value.captain_locked=!next;
    value.document=document(next?target_blob:base_blob,next?0x83:0x27);
    value.bindings={{{uint64_t(next?109:9),2},3},{{11,4},7}};value.act=next?3:1;
    value.local_dirty=editor_native::Snapshot::Dirty{next?77u:55u,!next,next};
    value.metadata=editor_native::Snapshot::Metadata{{next?u"next name":u"name",next?u"next description":u"description",next?u"next tags":u"tags"},next?13u:17u};return value;
}
bool same(const editor_native::Snapshot& a,const editor_native::Snapshot& b){
    return a.bytes==b.bytes && a.source==b.source && a.captain==b.captain && a.captain_locked==b.captain_locked &&
        a.document==b.document && a.bindings==b.bindings && a.act==b.act && a.metadata==b.metadata &&
        a.local_dirty.has_value()==b.local_dirty.has_value() && (!a.local_dirty ||
        (a.local_dirty->saved_revision==b.local_dirty->saved_revision && a.local_dirty->dirty==b.local_dirty->dirty && a.local_dirty->imported==b.local_dirty->imported));
}
}
void* operator new(size_t n){
    if(observe){if(n==base_blob)++base_copies;if(n==target_blob)++target_copies;if(n==base_image)++base_image_copies;if(n==target_image)++target_image_copies;}
    if(auto* value=std::malloc(n?n:1))return value;throw std::bad_alloc();
}
void operator delete(void* value)noexcept{std::free(value);}
int main(){try{
    using namespace editor_native;
    const auto base=image(false),after=image(true);auto old=base;old.document=after.document;old.bytes.clear();
    reset_counts();observe=true;auto prepared=snapshot_values::with_document(base,after.document);observe=false;
    check(same(prepared,old),"every preparation sidecar/binding/metadata value matches previous behavior");
    check(base_copies==0 && base_image_copies==0 && target_copies==1,"no discarded BASE document/image copy");
    // Retain an executable reference for the exact removed operations. The
    // allocation delta is a real CPU-work assertion, not a Native trust flag.
    reset_counts();observe=true;auto legacy=base;legacy.document=after.document;observe=false;
    check(base_copies==1 && base_image_copies==1 && target_copies==1,"old boundary performs the eliminated deep copies");
    legacy.bytes.clear();check(same(prepared,legacy),"optimized and old construction equal");
    auto expected=base;expected.bytes=after.bytes;expected.source=after.source;expected.act=after.act;
    expected.bindings=after.bindings;expected.metadata=after.metadata;expected.captain=after.captain;expected.captain_locked=after.captain_locked;
    reset_counts();observe=true;auto projection=snapshot_values::with_image(base,after);observe=false;
    check(same(projection,expected),"every projection sidecar matches old assignments");
    check(base_copies==1 && target_copies==0 && base_image_copies==0 && target_image_copies==1,"actual projected document retained with only final byte image copied");
    reset_counts();observe=true;auto legacy_projection=base;legacy_projection.bytes=after.bytes;observe=false;
    check(base_copies==1 && base_image_copies==1 && target_image_copies==1,"old projection copied the replaced BASE image too");
    check(legacy_projection.document==base.document && legacy_projection.bytes==after.bytes,"old projection retains producer document while replacing bytes");
    check(projection.document!=after.document && projection.local_dirty->saved_revision==base.local_dirty->saved_revision,"caller target document/dirty state never replace actual producer projection");
    auto empty=base;empty.metadata.reset();empty.local_dirty.reset();empty.bytes.clear();empty.bindings.clear();
    auto values=snapshot_values::with_document(empty,after.document);
    check(!values.metadata && !values.local_dirty && values.bindings.empty() && values.bytes.empty(),"missing legacy sidecars remain missing");
    values=snapshot_values::with_image(empty,after);
    check(!values.local_dirty && values.metadata==after.metadata && values.document==empty.document,"projection dirty and image metadata come from their exact original domains");
    auto owned=snapshot_values::with_document(base,after.document);auto independent=owned;
    prepared.metadata->text[0]=u"changed";prepared.bindings.clear();prepared.document.settings.clear();
    check(same(owned,independent) && base.metadata->text[0]==u"name" && after.document.settings.size()==1,"all returned values own their storage");
    std::printf("Native snapshot values: %u checks passed\n",checks);return 0;
}catch(const std::exception& error){observe=false;std::printf("FAIL: %s\n",error.what());return 1;}}
