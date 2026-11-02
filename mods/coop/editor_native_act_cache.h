// Exact owning native act-roster projection cache. Retains value data only.
#pragma once
#include "net/editor_document.h"
#include <limits>
#include <list>
#include <memory>

namespace editor_native_act_cache {
using Acts=std::vector<editor::StructuredValue>;
struct Identity {
    editor::ObjectId act,settings;
    bool operator==(const Identity&)const=default;
};
struct Key {
    uint64_t context=0;
    std::array<uint8_t,16> project{};
    uint64_t epoch=0;
    editor::ResourceKey source;
    std::vector<Identity> identities;
    std::map<uint32_t,editor::PropertySpec> schema;
    std::vector<uint8_t> bytes;
    bool operator==(const Key&)const=default;
};
struct Stats {size_t bytes=0,entries=0;uint64_t hits=0,misses=0;};
class Cache {
    struct Entry {Key key;std::shared_ptr<const Acts> acts;size_t cost=0;};
    static constexpr size_t kLimit=32u<<20,kEntries=8;
    // Accessed only on the native adapter's game thread, outside rendering.
    std::list<Entry> entries;
    size_t retained=0;
    uint64_t serial=1,hits=0,misses=0;
    static size_t cost(const Key& key,const Acts& acts){
        size_t n=sizeof(Entry)+64+key.bytes.capacity()+key.identities.capacity()*sizeof(Identity)+key.schema.size()*96;
        for(const auto& block:acts)n+=sizeof(block)+block.bytes.capacity()+block.references.capacity()*sizeof(editor::ObjectId);
        return n;
    }
public:
    uint64_t context()const{return serial;}
    void clear(){
        entries.clear();retained=0;
        serial=serial==std::numeric_limits<uint64_t>::max()?0:serial?serial+1:0;
    }
    std::shared_ptr<const Acts> get(const Key& key){
        if(serial && key.context==serial)for(auto at=entries.begin();at!=entries.end();++at)if(at->key==key){
            ++hits;auto result=at->acts;entries.splice(entries.end(),entries,at);return result;
        }
        ++misses;return {};
    }
    void put(Key key,const Acts& acts){
        try{
            const auto charged=cost(key,acts);if(charged>kLimit)return;
            auto immutable=std::make_shared<const Acts>(acts);
            if(!serial || key.context!=serial)return;
            for(auto at=entries.begin();at!=entries.end();++at)if(at->key==key){
                entries.splice(entries.end(),entries,at);return;
            }
            while(!entries.empty() && (entries.size()>=kEntries || retained>kLimit-charged)){
                retained-=entries.front().cost;entries.pop_front();
            }
            entries.push_back({std::move(key),std::move(immutable),charged});retained+=charged;
        }catch(const std::bad_alloc&){}
    }
    Stats stats()const{return {retained,entries.size(),hits,misses};}
};
} // namespace editor_native_act_cache
