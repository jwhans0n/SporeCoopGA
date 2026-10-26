// Scenario Write serializes live pool rows in order; Read rebuilds dense rows.
#pragma once
#include "editor_native.h"
#include <algorithm>
#include <set>

namespace editor_native {
inline bool serialized_bindings(const std::vector<Binding>& live,
                                std::vector<Binding>& out, std::string& error) {
    if(live.size()>kPlacementLimit){error="Native editor placement limit exceeded";return false;}
    auto candidate=live;
    std::sort(candidate.begin(),candidate.end(),[](const Binding& a,const Binding& b){return a.marker<b.marker;});
    std::set<editor::ObjectId> identities;
    for(size_t i=0;i<candidate.size();++i){
        const auto& item=candidate[i];
        if(item.marker>=kPlacementLimit || !item.object.id || !item.object.generation ||
           (i && candidate[i-1].marker==item.marker) || !identities.insert(item.object).second){
            error="Invalid native live placement bindings";return false;
        }
    }
    for(size_t i=0;i<candidate.size();++i)candidate[i].marker=uint32_t(i);
    out=std::move(candidate);error.clear();return true;
}
} // namespace editor_native
