// Native projections carry geometry, not authoritative record revisions.
#pragma once
#include "net/editor_terrain.h"

namespace editor_terrain_edit {
inline bool replacement(const editor::TerrainRecord& owned,const editor::TerrainRecord& before,
                        const editor::TerrainRecord& after,editor::Mutation& out,std::string& error){
    if(before.object!=owned.object || after.object!=owned.object || before.kind!=owned.kind || after.kind!=owned.kind ||
       before.order!=owned.order || after.order!=owned.order || before.payload!=owned.payload ||
       !editor::valid_terrain_payload(owned.kind,after.payload)){
        error="The edited terrain stamp no longer matches its acquired identity and before-image";return false;
    }
    editor::Mutation mutation;mutation.kind=editor::MutationKind::SetTerrain;mutation.target=owned.object;
    mutation.expected=owned.revision;mutation.terrain=owned;mutation.terrain->payload=after.payload;
    out=std::move(mutation);error.clear();return true;
}
} // namespace editor_terrain_edit
