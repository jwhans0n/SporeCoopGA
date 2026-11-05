// Local, exact evidence from F3E250. Native pointers are identity tokens only:
// every read resolves the current scene/marker again before touching storage.
struct CaptainGrounding {
    SceneOwner owner;GestureScope scope;
    uint64_t generation=0,serial=0;
    editor::Revision revision=0;
    uintptr_t marker=0;
    uint32_t act=0;
    editor::ResourceKey asset;
    uint64_t server=0;
    editor::Pose authored,rendered;
    float health=0;
    bool invulnerable=false,locked=false;
};
std::optional<CaptainGrounding> captain_grounding;
uint64_t captain_grounding_serial=0;
GestureScope captain_read_scope(){
    if(!terrain_read_identity)return value_scope();
    auto* read=terrain_read_identity;
    if(read->invalid||!read->key||!read->current||!*read->current)return {};
    const auto key=*read->key;const auto current=*read->current;
    if(!current()||terrain_read_identity!=read||read->invalid||!read->key||*read->key!=key)return {};
    // This exact, synchronous transition grants read identity only. The normal
    // gesture scope remains empty during Save/recovery holds.
    return key.scope;
}
bool captain_exact(const auto& a,const auto& b){static_assert(sizeof(a)==sizeof(b));return std::memcmp(&a,&b,sizeof(a))==0;}
bool captain_finite(const editor::Pose& p){
    return std::isfinite(p.position.x)&&std::isfinite(p.position.y)&&std::isfinite(p.position.z)&&
        editor_native_terrain_rotations::finite(p.rotation)&&std::isfinite(p.scale)&&p.scale>0;
}
bool captain_resource_matches(void* r,const CaptainGrounding& p){
    if(!skg::readable(r,kResourceSize)||skg::field<editor::ResourceKey>(r,8)!=p.owner.source ||
       skg::field<editor::ResourceKey>(r,0x130)!=p.asset||skg::field<uint64_t>(r,0x140)!=p.server ||
       !captain_exact(skg::field<editor::Rotation>(r,0x15C),p.authored.rotation)||
       !captain_exact(skg::field<float>(r,0x16C),p.authored.scale))return false;
    const auto position=skg::field<editor::Vec3>(r,0x150);
    return captain_exact(position,p.authored.position)||captain_exact(position,p.rendered.position);
}
bool captain_grounding_raw(const CaptainGrounding& p){
    skg::ReadOnlyMemoryScope memory;
    if(generation!=p.generation||captain_grounding_serial!=p.serial)return false;
    auto* m=mode();auto* d=data(m);auto* r=resource(d);
    auto* c=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    if(uintptr_t(m)!=p.owner.mode||uintptr_t(d)!=p.owner.data||uintptr_t(r)!=p.owner.resource||
       !skg::readable(c,4)||skg::field<uint32_t>(c,0)!=p.act||!skg::readable(d,0x90)||!captain_resource_matches(r,p))return false;
    auto* marker=skg::field<void*>(d,0x8C);
    return uintptr_t(marker)==p.marker&&skg::readable(marker,0x24)&&skg::field<uint32_t>(marker,0)==UINT32_MAX-1&&
        captain_exact(skg::field<editor::Pose>(marker,4),p.rendered);
}
bool captain_grounding_current(const CaptainGrounding& p){
    try{
        if(!valid_scope(p.scope)||captain_read_scope()!=p.scope||!C.object_revision)return false;
        const auto revision=C.object_revision(kCaptainObject);
        return revision&&*revision==p.revision&&captain_grounding_raw(p);
    }catch(...){return false;}
}
bool captain_grounding_at(const CaptainGrounding& p,const TerrainTransitionKey& key){
    try{
        if(!valid_scope(key.scope)||!key.serial||p.scope!=key.scope||p.owner!=key.owner||p.generation!=key.generation||!C.object_revision)return false;
        const auto revision=C.object_revision(kCaptainObject);
        return revision&&*revision==p.revision&&captain_grounding_raw(p);
    }catch(...){return false;}
}
std::optional<CaptainGrounding> current_captain_grounding(const TerrainTransitionKey* key=nullptr){
    const auto pin=captain_grounding;
    if(pin&&(key?captain_grounding_at(*pin,*key):captain_grounding_current(*pin)))return pin;
    return std::nullopt;
}
bool captain_has_grounding(){
    const auto proof=current_captain_grounding();
    return proof&&!captain_exact(proof->authored.position,proof->rendered.position);
}
void normalize_captain_grounding(void* owned,const CaptainGrounding& proof){
    if(captain_resource_matches(owned,proof))skg::field<editor::Vec3>(owned,0x150)=proof.authored.position;
}
std::optional<CaptainGrounding> pin_captain_grounding(void* self){
    if(!active()||!tracks_authoring()||self!=data(mode())||!C.object_revision)return std::nullopt;
    const auto prior=current_captain_grounding();
    CaptainGrounding p;p.scope=captain_read_scope();p.generation=generation;p.serial=captain_grounding_serial;
    auto* m=mode();auto* r=resource(self);auto* c=a_context?*reinterpret_cast<void**>(a_context):nullptr;
    if(!valid_scope(p.scope)||!skg::readable(self,0x90)||!skg::readable(r,kResourceSize)||!skg::readable(c,4))return std::nullopt;
    p.owner={uintptr_t(m),uintptr_t(self),uintptr_t(r),skg::field<editor::ResourceKey>(r,8)};p.act=skg::field<uint32_t>(c,0);
    auto* marker=skg::field<void*>(self,0x8C);
    if(!skg::readable(marker,0x24)||skg::field<uint32_t>(marker,0)!=UINT32_MAX-1)return std::nullopt;
    p.marker=uintptr_t(marker);p.asset=skg::field<editor::ResourceKey>(r,0x130);p.server=skg::field<uint64_t>(r,0x140);
    p.authored=skg::field<editor::Pose>(r,0x150);p.rendered=skg::field<editor::Pose>(marker,4);
    p.health=skg::field<float>(r,0x170);p.invulnerable=skg::field<bool>(r,0x174);p.locked=skg::field<bool>(r,0x175);
    if(prior&&prior->owner==p.owner&&prior->marker==p.marker&&captain_resource_matches(r,*prior)&&captain_exact(p.rendered,prior->rendered))p.authored=prior->authored;
    else if(!captain_exact(p.authored,p.rendered))return std::nullopt;
    const auto revision=C.object_revision(kCaptainObject);if(!revision)return std::nullopt;p.revision=*revision;
    if(!captain_finite(p.authored)||!captain_finite(p.rendered)||!std::isfinite(p.health)||!captain_grounding_current(p))return std::nullopt;
    return p;
}
void SK_THISCALL ground_scene(void* self,bool flag){
    std::optional<CaptainGrounding> pin;
    try{pin=pin_captain_grounding(self);}catch(...){ }
    captain_grounding.reset();
    if(captain_grounding_serial==UINT64_MAX)pin.reset();else ++captain_grounding_serial;
    original_ground_scene(self,flag);
    // The first sample after the native call has no policy/native callbacks.
    // Only XYZ may differ. Appearance, q, scale, health and lock remain strict.
    if(!pin)return;
    try{
        pin->serial=captain_grounding_serial;
        auto* d=data(mode());auto* r=resource(d);
        if(uintptr_t(d)!=pin->owner.data||uintptr_t(r)!=pin->owner.resource||!skg::readable(d,0x90))return;
        auto* marker=skg::field<void*>(d,0x8C);
        if(uintptr_t(marker)!=pin->marker||!skg::readable(marker,0x24))return;
        const auto after=skg::field<editor::Pose>(marker,4);
        if(!captain_finite(after)||!captain_exact(after.rotation,pin->rendered.rotation)||!captain_exact(after.scale,pin->rendered.scale))return;
        pin->rendered=after;
        // These are not position provenance. A later property gesture keeps
        // XYZ evidence while its changed values remain fully serialized, but
        // the grounding call itself must not acquire unrelated field changes.
        if(!captain_exact(skg::field<float>(r,0x170),pin->health)||
           skg::field<bool>(r,0x174)!=pin->invulnerable||skg::field<bool>(r,0x175)!=pin->locked)return;
        if(!captain_grounding_raw(*pin)||!captain_grounding_current(*pin)||!captain_grounding_raw(*pin))return;
        captain_grounding=*pin;
    }catch(...){ } // Observation never suppresses native grounding or grants ownership.
}
