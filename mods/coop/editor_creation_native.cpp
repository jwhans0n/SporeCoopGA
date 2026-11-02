#include "editor_creation_native.h"
#include "editor_creation.h"
#include "editor_native_cast.h"
#include "editor_creation_png.h"
#include "adventure_collect.h"
#include "net/adventure_assets.h"
#include "sporekit_game.hpp"
#include "../../updater/sha256.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <tuple>
#include <set>
#include <cstdio>
#include <utility>

namespace editor_creation_native {
namespace {
const SkHost* H;
SkMod* M;
bool supported=false;
void* detached_owner=nullptr;
bool detached_assign_failed=false;
uint64_t model_observation=0;
using Assign=void(SK_THISCALL*)(void*,uint32_t,const editor::ResourceKey*);
Assign original_assign=nullptr;
void SK_THISCALL assign(void* owner,uint32_t index,const editor::ResourceKey* key){
    if(!detached_owner || owner!=detached_owner){original_assign(owner,index,key);return;}
    // F462F0 owns insertion and default-act construction. Its ordinary asset
    // setter also refreshes live actors and species. This exact detached owner
    // has no actors, so perform only the setter's owned primary-ref operations.
    auto get=H->sym("SporeCoop::CreationEditor::GetClass");
    auto type=H->sym("SporeCoop::CreationEditor::AssetModelType");
    auto* row=skg::thiscall<uint8_t*>(get,owner,index);
    if(!row || !key){detached_assign_failed=true;return;}
    const auto model=skg::cdecl_call<uint32_t>(type,key);
    if(model==UINT32_MAX){detached_assign_failed=true;return;}
    skg::field<editor::ResourceKey>(row,0)=*key;
    skg::field<uint32_t>(row,0xC)=0;
    skg::field<uint64_t>(row,0x10)=UINT64_MAX;
    skg::field<uint32_t>(row,0x18)=model;
}
bool rows(void* resource,uint8_t*& begin,uint8_t*& end,std::string& error){
    const auto a=skg::field<uintptr_t>(resource,0x2BF4),b=skg::field<uintptr_t>(resource,0x2BF8);
    if(b<a || (b-a)%0x27E8 || (b-a)/0x27E8>1024 || (b!=a && !skg::readable(reinterpret_cast<void*>(a),b-a))){
        error="The owned cast roster is invalid";return false;
    }
    begin=reinterpret_cast<uint8_t*>(a);end=reinterpret_cast<uint8_t*>(b);return true;
}
bool family(uint32_t type){
    switch(type){case 0x2B978C46:case 0x2399BE55:case 0x24682294:case 0x476A98C7:return true;default:return false;}
}
uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
uint16_t be16(const uint8_t* p){return uint16_t(p[0])<<8|p[1];}
uint32_t le32(const uint8_t* p){return uint32_t(p[3])<<24|uint32_t(p[2])<<16|uint32_t(p[1])<<8|p[0];}
void putbe(uint8_t* p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(24-8*i));}
void putle(uint8_t* p,uint32_t n){for(unsigned i=0;i<4;++i)p[i]=uint8_t(n>>(8*i));}
bool owner_record(const dbpf::Key& key,const editor::ResourceKey& source){
    return key.instance==source.instance && (key.group==source.group ||
        (key.group==source.group+1 && key.type==0x2F7D0004) ||
        (key.group==((source.group&0xFFFF0000u)|0xE500u) && key.type==0x00B1B104));
}
bool remap_properties(std::vector<uint8_t>& bytes,const std::function<bool(const dbpf::Key&)>& owns,uint32_t instance,std::string& error){
    auto bad=[&](){error="Unsupported or malformed creation property stream";return false;};
    if(bytes.size()<4 || bytes.size()>(16u<<20))return bad();
    const auto count=be32(bytes.data());if(count>2048)return bad();
    size_t at=4;std::set<uint32_t> seen;
    for(uint32_t i=0;i<count;++i){
        if(bytes.size()-at<8)return bad();
        auto* header=bytes.data()+at;
        const auto id=be32(header);const auto type=be16(header+4),flags=be16(header+6);at+=8;
        if(!seen.insert(id).second)return bad();
        size_t items=1,stride=0,size=0;const bool array=(flags&0x80)!=0;
        if(array){
            if(bytes.size()-at<8)return bad();
            items=be32(bytes.data()+at);stride=be32(bytes.data()+at+4);at+=8;
            if(items>4096 || stride>(16u<<20))return bad();
            if(type==0x12 || type==0x13){
                size_t end=at;const size_t width=type==0x13?2:1;
                for(size_t j=0;j<items;++j){if(bytes.size()-end<4)return bad();auto n=be32(bytes.data()+end);end+=4;if(n>(bytes.size()-end)/width)return bad();end+=n*width;}
                size=end-at;
            }else{if(items && stride>(bytes.size()-at)/items)return bad();size=items*stride;}
        }else{
            switch(type){
            case 1:case 2:case 5:case 6:size=1;break;
            case 3:case 7:case 8:size=2;break;
            case 4:case 9:case 0xA:case 0xD:size=4;break;
            case 0xB:case 0xC:case 0xE:case 0x30:size=8;break;
            case 0x20:case 0x33:case 0x34:size=16;break;
            case 0x31:case 0x32:size=12;break;
            case 0x12:case 0x13:{if(bytes.size()-at<4)return bad();const auto n=be32(bytes.data()+at);const size_t width=type==0x13?2:1;if(n>(bytes.size()-at-4)/width)return bad();size=4+n*width;break;}
            default:return bad();
            }
        }
        if(size>bytes.size()-at)return bad();
        if(type==0x20){
            if((array && stride!=12) || (!array && size!=16))return bad();
            for(size_t j=0;j<items;++j){auto* p=bytes.data()+at+j*(array?stride:16);dbpf::Key key{le32(p),le32(p+4),le32(p+8)};if(owns(key))putle(p,instance);}
        }
        at+=size;
    }
    return at==bytes.size() || bad();
}
bool normalize_images(std::vector<dbpf::Record>& records,const editor::ResourceKey& source,std::string& error){
    bool primary=false;std::optional<std::vector<uint8_t>> fallback;
    for(auto& row:records){
        if(row.key.instance!=source.instance || row.key.type!=0x2F7D0004 ||
           (row.key.group!=source.group && row.key.group!=source.group+1))continue;
        std::vector<uint8_t> clean;if(!editor_creation_png::sanitize(row.data,clean,error))return false;
        row.data=std::move(clean);
        if(row.key.group==source.group)primary=true;else fallback=row.data;
    }
    // Native Crew explicitly requests the model-group PNG. A +1-only image
    // also has to be sanitized; it is not assumed free of embedded content.
    if(!primary && fallback)records.push_back({{source.instance,0x2F7D0004,source.group},std::move(*fallback)});
    return true;
}
bool remap_owner(std::vector<dbpf::Record>& records,const editor::ResourceKey& source,uint32_t instance,std::string& error){
    const auto original=records;
    auto owns=[&](const dbpf::Key& key){return (key==dbpf::Key{source.instance,source.type,source.group}) ||
        std::any_of(original.begin(),original.end(),[&](const auto& row){return row.key==key && owner_record(row.key,source);});};
    for(auto it=records.begin();it!=records.end();){
        if(!owner_record(it->key,source)){++it;continue;}
        // These caches are regenerated by native loading/baking. Owner PNGs
        // were normalized before identity allocation: pixels retain no native
        // creation carrier and the fixed encoder emits no ancillary payload.
        if(it->key.type==0x02D5C9AF || it->key.type==0x0F43029A ||
           it->key.type==source.type){
            it=records.erase(it);continue;
        }
        auto& bytes=it->data;
        if(it->key.type==0x1A99B06B){
            if(bytes.size()<4 || bytes[0]!=0xD3 || bytes[1]!=0x7E || bytes[2]!=0x65 || bytes[3]!=0x48){error="Unknown native editable model format";return false;}
            // The verified BEM factory assigns identity from the resource
            // manager's requested key. Reject a format with an unexpected
            // embedded complete self key instead of blind byte replacement.
            std::array<uint8_t,12> little{},big{};
            putle(little.data(),source.instance);putle(little.data()+4,source.type);putle(little.data()+8,source.group);
            putbe(big.data(),source.type);putbe(big.data()+4,source.group);putbe(big.data()+8,source.instance);
            if(std::search(bytes.begin(),bytes.end(),little.begin(),little.end())!=bytes.end() ||
               std::search(bytes.begin(),bytes.end(),big.begin(),big.end())!=bytes.end()){
                error="The saved model contains an unaudited embedded creation identity";return false;
            }
        }else if(it->key.type==0x030BDEE3){
            // Verified pollen v13 self key at0C is BE(type,group,instance).
            // Its later parent key describes ancestry and must stay original.
            if(bytes.size()<0x18 || be32(bytes.data())!=13 || be32(bytes.data()+0xC)!=source.type ||
               be32(bytes.data()+0x10)!=source.group || be32(bytes.data()+0x14)!=source.instance){
                error="Unknown native creation metadata identity format";return false;
            }
            putbe(bytes.data()+0x14,instance);
        }else if(it->key.type==0x00B1B104){
            if(!remap_properties(bytes,owns,instance,error))return false;
        }else if(it->key.type!=0x2F7D0004){
            error="The saved creation contains an unaudited owned companion";return false;
        }
        it->key.instance=instance;++it;
    }
    return true;
}
}
bool inspect_model(editor::ResourceKey key,const ModelFence& fence,ModelAdmission& out,std::string& error){
    error.clear();
    if(!supported || !key.instance || !key.group || !family(key.type) || !fence.generation || !fence.current ||
       !fence.scope.epoch || !fence.scope.author.generation || fence.scope.author.slot>=4 || fence.scope.project==editor::ProjectId{}){
        error="The creation model inspection has no current owned context";return false;
    }
    const auto manager_address=H->sym("Resource::ResourceManager_ptr");
    const auto validation=H->sym("SporeCoop::CreationEditor::ValidateModel");
    const auto mask_address=H->sym("SporeCoop::CreationEditor::AdventureValidationMask");
    auto* manager=manager_address && skg::readable(reinterpret_cast<void*>(manager_address),sizeof(void*))?
        *reinterpret_cast<void**>(manager_address):nullptr;
    if(!validation || !mask_address || !skg::readable(reinterpret_cast<void*>(mask_address),16) || !skg::readable(manager,sizeof(void*)) ||
       !skg::readable(skg::vtable(manager),0x20) || !skg::vtable(manager)[0x0C/4] || !skg::vtable(manager)[0x1C/4]){
        error="The verified native creation model inspection is unavailable";return false;
    }
    const auto requested=key;
    ModelAdmission result;result.key=key;std::memcpy(result.mask.data(),reinterpret_cast<void*>(mask_address),16);
    const auto current=[&]{
        return fence.current(fence.scope,fence.generation) && editor_native::state().generation==fence.generation && key==requested &&
            skg::readable(reinterpret_cast<void*>(manager_address),sizeof(void*)) && *reinterpret_cast<void**>(manager_address)==manager &&
            skg::readable(reinterpret_cast<void*>(mask_address),16) && !std::memcmp(result.mask.data(),reinterpret_cast<void*>(mask_address),16);
    };
    if(!current()){error="The creation model inspection context expired";return false;}
    struct Mask {uint32_t words[4];};Mask mask{};std::copy(result.mask.begin(),result.mask.end(),mask.words);
    struct ResourceRef {
        void* object=nullptr;
        ~ResourceRef(){close();}
        void close(){if(object){auto* value=std::exchange(object,nullptr);
            if(skg::readable(value,sizeof(void*)) && skg::readable(skg::vtable(value),8) && skg::vtable(value)[0x04/4])skg::vcall<int>(value,0x04/4);}}
    };
    for(auto slot:{0x0Cu,0x1Cu}){
        auto& load=slot==0x0C?result.cached:result.private_copy;
        ResourceRef reference;
        load.loaded=skg::vcall<bool>(manager,slot/4,&key,&reference.object,static_cast<void*>(nullptr),static_cast<void*>(nullptr),static_cast<void*>(nullptr),static_cast<void*>(nullptr));
        void* object=reference.object;
        load.object=object!=nullptr;
        if(object && (!skg::readable(object,sizeof(void*)) || !skg::readable(skg::vtable(object),8) || !skg::vtable(object)[0x04/4])){
            error="The native creation model returned an invalid resource reference";return false;
        }
        bool okay=current();
        if(object){
            if(!skg::readable(object,sizeof(void*)) || !skg::readable(skg::vtable(object),0x10) || !skg::vtable(object)[0x0C/4]){
                // Native owns the ResourceObject reference. An unreadable
                // vtable cannot be called or passed on as a valid result.
                error="The native creation model returned an invalid resource object";return false;
            }
            if(okay && load.loaded){
                void* model=skg::vcall<void*>(object,0x0C/4,uint32_t(0x03C609F8));okay=current();
                load.model=model!=nullptr;
                if(okay && model){
                    if(!skg::readable(model,0x1C)){okay=false;error="The native creation model is truncated";}
                    else{
                        load.subtype=skg::field<uint32_t>(model,0x18);
                        const auto returned=skg::thiscall<uint32_t*>(validation,model,load.errors.data(),mask,true);
                        okay=current();
                        if(returned!=load.errors.data()){okay=false;error="The native creation validation returned a different output";}
                    }
                }
            }
            reference.close();
            okay=current()&&okay;
        }
        if(!okay){if(error.empty())error="The creation model inspection context changed during a native callback";return false;}
    }
    out=std::move(result);error.clear();return true;
}
bool model_admitted(const ModelAdmission& result,std::string& error){
    if(!result.cached.loaded || !result.cached.object){error="The shared creation could not be loaded through the native Adventure resource path";return false;}
    if(!result.cached.model){error="The shared creation did not produce its editable native model";return false;}
    if(std::any_of(result.cached.errors.begin(),result.cached.errors.end(),[](uint32_t word){return word!=0;})){
        error="The shared creation failed native Adventure model validation";return false;
    }
    error.clear();return true;
}
std::string model_admission_status(const ModelAdmission& result){
    char text[384];
    std::snprintf(text,sizeof(text),"key=%08X!%08X.%08X mask=%08X,%08X,%08X,%08X cached=%u/%u/%u subtype=%08X errors=%08X,%08X,%08X,%08X private=%u/%u/%u subtype=%08X errors=%08X,%08X,%08X,%08X",
        result.key.group,result.key.instance,result.key.type,result.mask[0],result.mask[1],result.mask[2],result.mask[3],
        unsigned(result.cached.loaded),unsigned(result.cached.object),unsigned(result.cached.model),result.cached.subtype,
        result.cached.errors[0],result.cached.errors[1],result.cached.errors[2],result.cached.errors[3],
        unsigned(result.private_copy.loaded),unsigned(result.private_copy.object),unsigned(result.private_copy.model),result.private_copy.subtype,
        result.private_copy.errors[0],result.private_copy.errors[1],result.private_copy.errors[2],result.private_copy.errors[3]);
    return text;
}
bool validate_model(editor::ResourceKey key,const ModelFence& fence,std::string& error){
    ModelAdmission result;
    const bool observed=inspect_model(key,fence,result,error);
    const bool accepted=observed && model_admitted(result,error);
    // Report this already-completed observation. Re-probing later can load a
    // warmed cache and cannot reconstruct the first cached/private outcomes.
    // These creation accessors copy module state; they make no native calls.
    const auto creation=editor_creation::scope();
    const bool local=creation && creation->project==fence.scope.project && creation->epoch==fence.scope.epoch && creation->author==fence.scope.author;
    if(H && H->log){
        if(model_observation!=UINT64_MAX)++model_observation;
        result.key=key;
        H->log(M,accepted?SK_INFO:SK_ERROR,
            "Creation model admission observed: pid=%lu observation=%llu caller=validate_model caller_mode=%s serial=%llu epoch=%llu author=%u:%u native_generation=%llu observed=%u accepted=%u %s error=%s",
            static_cast<unsigned long>(GetCurrentProcessId()),static_cast<unsigned long long>(model_observation),
            local?"creation":"canonical",static_cast<unsigned long long>(local?creation->serial:0),
            static_cast<unsigned long long>(fence.scope.epoch),unsigned(fence.scope.author.slot),unsigned(fence.scope.author.generation),
            static_cast<unsigned long long>(fence.generation),unsigned(observed),unsigned(accepted),model_admission_status(result).c_str(),error.substr(0,192).c_str());
    }
    if(!observed)return false;
    if(accepted)return true;
    char detail[192];std::snprintf(detail,sizeof(detail),"; key=%08X!%08X.%08X cached=%u/%u/%u errors=%08X,%08X,%08X,%08X private=%u/%u/%u",
        key.group,key.instance,key.type,unsigned(result.cached.loaded),unsigned(result.cached.object),unsigned(result.cached.model),
        result.cached.errors[0],result.cached.errors[1],result.cached.errors[2],result.cached.errors[3],
        unsigned(result.private_copy.loaded),unsigned(result.private_copy.object),unsigned(result.private_copy.model));
    error+=detail;return false;
}
bool init(const SkHost* host,SkMod* mod){
    H=host;M=mod;auto setter=H?H->sym("SporeCoop::CreationEditor::SetClassAsset"):0;
    supported=H && skg::supported_gog2024(H->exe_sha256()) && setter &&
        H->sym("SporeCoop::CreationEditor::GetClass") && H->sym("SporeCoop::CreationEditor::AssetModelType") &&
        H->hook_create(mod,reinterpret_cast<void*>(setter),reinterpret_cast<void*>(assign),reinterpret_cast<void**>(&original_assign))==0;
    return supported;
}
void shutdown(){supported=false;detached_owner=nullptr;detached_assign_failed=false;H=nullptr;M=nullptr;}
bool resource_reserved(const editor::ResourceKey& key){
    if(!H || !key.instance || !key.type || !key.group)return true;
    const auto address=H->sym("Resource::ResourceManager_ptr");
    auto* manager=address && skg::readable(reinterpret_cast<void*>(address),sizeof(void*))?
        *reinterpret_cast<void**>(address):nullptr;
    if(!skg::readable(manager,sizeof(void*)))return true;
    dbpf::Key requested{key.instance,key.type,key.group},resolved{};
    return skg::vcall<void*>(manager,0x30/4,&requested,&resolved,static_cast<void*>(nullptr))!=nullptr;
}
std::optional<editor::ObjectId> selected_cast(){
    if(!H || !supported)return std::nullopt;
    const auto pointer=H->sym("App::cScenarioMode::ptr");
    auto* mode=pointer && skg::readable(reinterpret_cast<void*>(pointer),sizeof(void*))?*reinterpret_cast<void**>(pointer):nullptr;
    if(!skg::readable(mode,0xD4) || skg::field<int>(mode,0xCC)!=1 || skg::field<uint32_t>(mode,0xD0)!=0)return std::nullopt;
    auto* data=skg::field<void*>(mode,0x74);auto* display=skg::field<void*>(mode,0x14);
    auto* script=skg::readable(display,0x1C)?skg::field<void*>(display,0x18):nullptr;
    auto* settings=skg::readable(script,0x1C)?skg::field<void*>(script,0x18):nullptr;
    auto* layout=skg::readable(settings,0x24)?skg::field<void*>(settings,0x10):nullptr;
    const auto shown=H->sym("SporeCoop::Editor::LayoutShown");
    if(shown && skg::readable(layout,0x18) && skg::thiscall<bool>(shown,layout))
        return editor::ObjectId{(2ull<<32)|skg::field<uint32_t>(settings,0x20),1};
    const auto context_pointer=H->sym("SporeCoop::Editor::Context"),marker_for=H->sym("Simulator::cScenarioData::GetMarkerForObject");
    auto* context=context_pointer && skg::readable(reinterpret_cast<void*>(context_pointer),sizeof(void*))?*reinterpret_cast<void**>(context_pointer):nullptr;
    auto* actor=skg::readable(context,8)?skg::field<void*>(context,4):nullptr;
    if(!actor || !marker_for || !skg::readable(data,0x90))return std::nullopt;
    auto* marker=skg::thiscall<void*>(marker_for,data,actor);
    if(!skg::readable(marker,4) || marker==skg::field<void*>(data,0x8C))return std::nullopt;
    return editor::ObjectId{(2ull<<32)|skg::field<uint32_t>(marker,0),1};
}
std::optional<editor::ObjectId> resolve_cast_asset(const editor::Document& document,const editor::ResourceKey& key){
    std::optional<editor::ObjectId> found;
    for(const auto& [id,row]:document.class_settings){
        (void)id;auto field=row.properties.find(editor::kClassAssetResourceProperty);
        const auto* resource=field==row.properties.end()?nullptr:std::get_if<editor::ResourceKey>(&field->second);
        if(resource && *resource==key){if(found && *found!=row.class_id)return std::nullopt;found=row.class_id;}
    }
    return found;
}
bool prepare_cast_save(const editor_native::Snapshot& base,const editor::ResourceKey& saved,
                       std::optional<editor::ObjectId> target,editor_native::Snapshot& result,std::string& error){
    if(!supported || !saved.instance || !saved.group || !family(saved.type)){
        error="The saved creation cannot be used by the shared adventure";return false;
    }
    if(target && !base.document.classes.contains(*target)){error="The creation's cast entry no longer exists";return false;}
    return editor_native::edit_owned_snapshot(base,[&](void* resource,std::string& reason){
        uint8_t* begin=nullptr;uint8_t* end=nullptr;if(!rows(resource,begin,end,reason))return false;
        if(target){
            auto* row=begin;for(;row!=end;row+=0x27E8)if(skg::field<uint32_t>(row,0)==uint32_t(target->id))break;
            if(row==end){reason="The cast content target is absent from the owned snapshot";return false;}
            auto* primary=row+8;
            if(skg::field<editor::ResourceKey>(primary,0).type!=saved.type){reason="Editing cannot change the creation's native resource family";return false;}
            skg::field<editor::ResourceKey>(primary,0)=saved;
            skg::field<uint32_t>(primary,0xC)=0;skg::field<uint64_t>(primary,0x10)=UINT64_MAX;
            return true;
        }
        if(begin!=end && size_t(end-begin)/0x27E8>=1024){reason="The adventure cast is full";return false;}
        const auto importer=H->sym("SporeCoop::Editor::ImportClass");
        if(!importer || detached_owner){reason="The detached cast importer is unavailable";return false;}
        // F462F0/F3E740 read only owner+10 outside the intercepted F43990.
        // Native owning constructors/copy routines retain every embedded vector.
        std::array<uint8_t,0x14> owner{};skg::field<void*>(owner.data(),0x10)=resource;
        struct ScopedOwner {void* previous;bool failed;ScopedOwner(void* p):previous(detached_owner),failed(detached_assign_failed){detached_owner=p;detached_assign_failed=false;}~ScopedOwner(){detached_owner=previous;detached_assign_failed=failed;}} guard(owner.data());
        const auto index=skg::thiscall<uint32_t>(importer,owner.data(),&saved);
        if(index==UINT32_MAX || detached_assign_failed){reason="The native cast importer rejected the saved creation or its model type";return false;}
        return true;
    },result,error);
}
bool immutable_version(const editor::ProjectId& project,const editor::ResourceKey& source,const std::vector<dbpf::Record>& source_records,
                       const std::function<bool(const editor::ResourceKey&)>& reserved,
                       editor::ResourceKey& version,std::vector<dbpf::Record>& records,std::string& error){
    if(project==editor::ProjectId{} || !source.instance || !source.group || !family(source.type) || source_records.empty()){
        error="A verified local creation is required for shared content versioning";return false;
    }
    std::vector<dbpf::Record> canonical=source_records;
    std::sort(canonical.begin(),canonical.end(),[](const auto& a,const auto& b){return std::tie(a.key.group,a.key.type,a.key.instance)<std::tie(b.key.group,b.key.type,b.key.instance);});
    net::adventure_content::Bundle validated;
    if(!net::adventure_content::make_bundle(canonical.front().key,canonical,validated,error))return false;
    // Every supported creation is regenerated from its editable model. Moving
    // only BEM without the matching metadata/PNG/property companions is unsafe.
    if(std::none_of(canonical.begin(),canonical.end(),[&](const auto& row){return row.key.instance==source.instance && row.key.group==source.group && row.key.type==0x1A99B06B;})){
        error="The saved creation has no editable native model";return false;
    }
    if(!normalize_images(canonical,source,error))return false;
    std::sort(canonical.begin(),canonical.end(),[](const auto& a,const auto& b){return std::tie(a.key.group,a.key.type,a.key.instance)<std::tie(b.key.group,b.key.type,b.key.instance);});
    if(!net::adventure_content::make_bundle(canonical.front().key,canonical,validated,error))return false;
    std::vector<uint8_t> identity(project.begin(),project.end());
    auto append=[&](uint32_t value){for(unsigned i=0;i<4;++i)identity.push_back(uint8_t(value>>(i*8)));};
    append(source.type);append(source.group);
    for(const auto& row:canonical){
        append(row.key.instance);append(row.key.type);append(row.key.group);append(uint32_t(row.data.size()));
        auto hash=upd::sha256_hex(row.data);identity.insert(identity.end(),hash.begin(),hash.end());
    }
    auto hash=upd::sha256_hex(identity);uint32_t seed=0;
    for(unsigned i=0;i<8;++i){const char c=hash[i];seed=(seed<<4)|uint32_t(c<='9'?c-'0':c-'a'+10);}
    for(uint32_t attempt=0;attempt<4096;++attempt){
        const auto instance=0x7D000000u|((seed+attempt*0x9E3779B9u)&0x00FFFFFFu);
        bool collision=instance==source.instance;
        if(!collision && reserved)collision=reserved({instance,source.type,source.group});
        if(!collision && reserved)for(const auto& row:canonical)
            if(owner_record(row.key,source) && reserved({instance,row.key.type,row.key.group})){collision=true;break;}
        if(collision)continue;
        auto candidate=canonical;if(!remap_owner(candidate,source,instance,error))return false;
        records=std::move(candidate);
        version={instance,source.type,source.group};error.clear();return true;
    }
    error="Shared creation resource identity space is occupied";return false;
}
bool collect_version(const editor::ProjectId& project,const editor::ResourceKey& source,const std::function<bool(const editor::ResourceKey&)>& reserved,
                     editor::ResourceKey& version,std::vector<dbpf::Record>& records,std::string& error){
    if(!family(source.type)){
        error="The saved creation cannot be used by the shared adventure";return false;
    }
    std::vector<dbpf::Record> original;
    return adventure_collect::collect_creation({source.instance,source.type,source.group},original,error) &&
        immutable_version(project,source,original,reserved,version,records,error);
}
} // namespace editor_creation_native
