namespace service_pulse_fixture {
unsigned releases=0,services=0;
int SK_THISCALL observed_release(void* owner){
    ++releases;
    // Model the main-thread pulse's passive gate from a final Native Release.
    // It cannot enter Session/Controller service while this owner is retiring.
    if(!editor_native::service_busy())++services;
    CHECK(editor_native::service_busy());
    return t_release(owner);
}
void test(){
    reset();CHECK(!editor_native::service_busy());releases=services=0;
    t_resource_vt[1]=reinterpret_cast<void*>(observed_release);
    t_entry_vt[1]=reinterpret_cast<void*>(observed_release);
    skg::field<void*>(t_entry.data(),0)=t_entry_vt.data();
    t_addref(t_entry.data());t_addref(t_live.data());
    Pending item{};item.entry=t_entry.data();item.before=t_live.data();item.owner=nullptr;item.generation=generation;
    pending.push_back(item);const auto frame=t_frame;
    drain(nullptr); // Reject stale owner, but retain the busy scope through both Releases.
    CHECK(releases==2&&!services&&pending.empty()&&t_frame==frame);
    CHECK(!editor_native::service_busy()&&!validation_depth);
    {
        ValidationScope outer;CHECK(editor_native::service_busy());
        {ValidationScope inner;CHECK(editor_native::service_busy());}
        CHECK(editor_native::service_busy());
    }
    CHECK(!editor_native::service_busy());
    creation_camera_fixture();bool fired=false;
    v_callback=[&](unsigned){fired=true;CHECK(editor_native::service_busy());if(!editor_native::service_busy())++services;};
    (void)state();CHECK(fired&&!services&&!editor_native::service_busy());
    v_callback={};reset();CHECK(!editor_native::service_busy());
}
}
