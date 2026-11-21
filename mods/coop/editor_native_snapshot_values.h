// Private value-copy helpers. They mint no semantic/native provenance.
#pragma once
#include "editor_native.h"

namespace editor_native::snapshot_values {
inline Snapshot with_document(const Snapshot& original,const editor::Document& wanted){
    // Exactly every Snapshot member is decomposed. Adding a future member must
    // fail compilation here rather than silently omit a sidecar. Bytes are
    // intentionally empty: the next existing NativeWrite supplies their proof.
    const auto& [old_bytes,source,captain,locked,old_document,bindings,act,dirty,metadata]=original;
    (void)old_bytes;(void)old_document;
    return Snapshot{{},source,captain,locked,wanted,bindings,act,dirty,metadata};
}
inline Snapshot with_image(const Snapshot& projection,const Snapshot& image){
    // Keep the actual producer's projected document and local rollback state.
    // The existing caller still derives/validates known fields from genuine
    // AFTER Read. Every other image value comes from the independently owned
    // prepared image, exactly as the previous assignments required.
    const auto& [old_bytes,old_source,old_captain,old_locked,document,old_bindings,old_act,dirty,old_metadata]=projection;
    const auto& [bytes,source,captain,locked,unused_document,bindings,act,unused_dirty,metadata]=image;
    (void)old_bytes;(void)old_source;(void)old_captain;(void)old_locked;(void)old_bindings;(void)old_act;(void)old_metadata;
    (void)unused_document;(void)unused_dirty;
    return Snapshot{bytes,source,captain,locked,document,bindings,act,dirty,metadata};
}
}
