// Recognize exact canonical placement-only changes before native preparation.
#pragma once
#include "net/editor_document.h"
#include <set>

namespace editor_native {
inline bool exact_pose_change(const editor::Document& before,const editor::Document& after,
                              const editor::Change& change,std::set<editor::ObjectId>& targets){
    const auto& patch=change.patch;targets.clear();
    if(patch.fields.empty() || !patch.objects.empty() || !patch.classes.empty() || patch.acts || !patch.terrain.empty() ||
       before.revision>=UINT64_MAX-1 || after.revision!=before.revision+1 || change.revision!=after.revision)return false;
    auto expected=before;expected.revision=after.revision;std::set<std::pair<editor::ObjectId,editor::FieldKey>> unique;
    for(const auto& field:patch.fields){
        auto placement=expected.placements.find(field.object);auto original=before.placements.find(field.object);
        if(placement==expected.placements.end() || original==before.placements.end() ||
           !unique.emplace(field.object,field.field).second || field.expected!=editor::field_revision(original->second,field.field) ||
           field.before!=editor::value(original->second,field.field) || field.before==field.after)return false;
        auto& p=placement->second;
        if(field.field.field==editor::Field::Position){const auto* v=field.after?std::get_if<editor::Vec3>(&*field.after):nullptr;if(!v)return false;p.position=*v;}
        else if(field.field.field==editor::Field::Rotation){const auto* v=field.after?std::get_if<editor::Rotation>(&*field.after):nullptr;if(!v)return false;p.rotation=*v;}
        else if(field.field.field==editor::Field::Scale){const auto* v=field.after?std::get_if<float>(&*field.after):nullptr;if(!v)return false;p.scale=*v;}
        else if(field.field.field==editor::Field::Property && field.field.property){
            if(field.after)p.properties[field.field.property]=*field.after;else p.properties.erase(field.field.property);
        }else return false;
        p.revision=after.revision;p.field_revisions[field.field]=after.revision;targets.insert(field.object);
    }
    return expected==after;
}
} // namespace editor_native
