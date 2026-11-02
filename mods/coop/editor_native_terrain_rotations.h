// Exact, value-owned provenance for quaternion changes made by native terrain
// grounding. The caller supplies the independently fenced scene and canonical
// roster; this table never grants placement ownership or retains native objects.
#pragma once
#include "net/editor_document.h"
#include <bit>
#include <cmath>
#include <new>
#include <span>

namespace editor_native_terrain_rotations {
inline constexpr size_t kLimit=4096;
struct Row {
    editor::ObjectId object;
    uint32_t slot=0,class_index=0;
    editor::Revision revision=0;
    editor::Rotation q;
};
enum class Result { Match, None, Stale };
inline bool exact(editor::Rotation a,editor::Rotation b){
    return std::bit_cast<std::array<uint32_t,4>>(a)==std::bit_cast<std::array<uint32_t,4>>(b);
}
inline bool finite(editor::Rotation q){
    return std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w);
}
inline bool identity(const Row& a,const Row& b){
    return a.object==b.object&&a.slot==b.slot&&a.class_index==b.class_index&&a.revision==b.revision;
}
class Witness {
    struct Value {Row authored;editor::Rotation rendered;};
    std::map<editor::ObjectId,Value> values;
    using Rows=std::map<editor::ObjectId,const Row*>;
    static bool index(std::span<const Row> rows,Rows& out){
        if(rows.size()>kLimit)return false;
        std::set<uint32_t> slots;
        for(const auto& row:rows){
            if(!row.object.id||!row.object.generation||row.slot>=kLimit||!finite(row.q)||
               !out.emplace(row.object,&row).second||!slots.insert(row.slot).second)return false;
        }
        return true;
    }
public:
    void clear(){values.clear();}
    void erase(editor::ObjectId object){values.erase(object);}
    size_t size()const{return values.size();}
    Result normalize(const Row& rendered,const Row& canonical,editor::Rotation& authored)const{
        auto at=values.find(rendered.object);if(at==values.end())return Result::None;
        const auto& value=at->second;
        if(!identity(rendered,canonical)||!identity(value.authored,canonical)||
           !finite(rendered.q)||!finite(canonical.q)||!exact(value.authored.q,canonical.q))return Result::Stale;
        if(!exact(rendered.q,value.rendered))return Result::None;
        authored=value.authored.q;return Result::Match;
    }
    // Full before/after/canonical rosters are required. A real authored edit,
    // changed binding, changed revision or ambiguous roster retires this proof.
    bool observe(std::span<const Row> before,std::span<const Row> after,
                 std::span<const Row> canonical,std::string& error){
        auto reject=[&](const char* why){clear();error=why;return false;};
        try {
            Rows old_rows,new_rows,authored_rows;
            if(before.size()!=after.size()||before.size()!=canonical.size()||
               !index(before,old_rows)||!index(after,new_rows)||!index(canonical,authored_rows))
                return reject("Native terrain rotation roster is invalid or ambiguous");
            std::map<editor::ObjectId,Value> next;
            for(const auto& [object,old]:old_rows){
                auto n=new_rows.find(object),a=authored_rows.find(object);
                if(n==new_rows.end()||a==authored_rows.end()||!identity(*old,*n->second)||!identity(*old,*a->second))
                    return reject("Native terrain rotation binding changed during grounding");
                const auto& target=*a->second;
                if(!exact(old->q,target.q)){
                    editor::Rotation prior;
                    if(normalize(*old,target,prior)!=Result::Match||!exact(prior,target.q))
                        return reject("Native terrain rotation input lacks exact authored provenance");
                }
                if(!exact(n->second->q,target.q))next.emplace(object,Value{target,n->second->q});
            }
            values.swap(next);error.clear();return true;
        } catch(const std::bad_alloc&){return reject("Cannot retain bounded native terrain rotation provenance");}
    }
};
} // namespace editor_native_terrain_rotations
