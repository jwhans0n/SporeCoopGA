// Exact serialized native class-roster cache. No native address is a cache key.
#pragma once
#include "net/editor_document.h"
#include <limits>
#include <list>
#include <memory>
#include <mutex>

namespace editor_native_cache {
using Classes=std::map<editor::ObjectId,editor::ClassActSettings>;
struct Key {
    uint64_t context=0;
    std::array<uint8_t,16> project{};
    uint64_t epoch=0;
    editor::ResourceKey source;
    editor::ObjectId anchor;
    uint32_t acts=0,next_class=0;
    bool authoring=false;
    std::vector<editor::ObjectId> act_order;
    std::vector<editor::ResourceKey> assets;
    std::map<uint32_t,editor::PropertySpec> schema;
    std::vector<uint8_t> bytes;
    bool operator==(const Key&)const=default;
};
struct Stats {size_t bytes=0,entries=0;uint64_t hits=0,misses=0;};
class ClassesCache {
    struct Entry {Key key;std::shared_ptr<const Classes> classes;size_t cost=0;};
    static constexpr size_t kLimit=32u<<20,kEntries=8;
    mutable std::mutex mutex;
    std::list<Entry> entries;
    size_t retained=0;
    uint64_t serial=1,hits=0,misses=0;
    static size_t cost(const Key& key,const Classes& classes){
        size_t n=sizeof(Entry)+64+key.bytes.capacity()+key.act_order.capacity()*sizeof(editor::ObjectId)+
            key.assets.capacity()*sizeof(editor::ResourceKey)+key.schema.size()*96;
        for(const auto& [id,row]:classes){
            (void)id;n+=sizeof(editor::ObjectId)+sizeof(editor::ClassActSettings)+96+row.field_revisions.size()*96;
            for(const auto& [property,value]:row.properties){
                (void)property;n+=sizeof(editor::Value)+96;
                if(const auto* block=std::get_if<editor::StructuredValue>(&value))
                    n+=block->bytes.size()*2+block->references.size()*sizeof(editor::ObjectId)*2;
                else if(const auto* text=std::get_if<std::string>(&value))n+=text->size()*2;
            }
        }
        return n;
    }
public:
    uint64_t context()const{std::lock_guard lock(mutex);return serial;}
    void clear(){
        std::lock_guard lock(mutex);entries.clear();retained=0;
        serial=serial==std::numeric_limits<uint64_t>::max()?0:serial?serial+1:0;
    }
    std::shared_ptr<const Classes> get(const Key& key){
        std::lock_guard lock(mutex);
        if(serial && key.context==serial)for(auto at=entries.begin();at!=entries.end();++at)if(at->key==key){
            ++hits;auto result=at->classes;entries.splice(entries.end(),entries,at);return result;
        }
        ++misses;return {};
    }
    void put(Key key,const Classes& classes){
        // An allocation failure only disables this optional optimization. The
        // normal verified native projection has already completed successfully.
        try{
            const auto charged=cost(key,classes);if(charged>kLimit)return;
            auto immutable=std::make_shared<const Classes>(classes);
            std::lock_guard lock(mutex);if(!serial || key.context!=serial)return;
            for(auto at=entries.begin();at!=entries.end();++at)if(at->key==key){
                entries.splice(entries.end(),entries,at);return;
            }
            while(!entries.empty() && (entries.size()>=kEntries || retained>kLimit-charged)){
                retained-=entries.front().cost;entries.pop_front();
            }
            entries.push_back({std::move(key),std::move(immutable),charged});retained+=charged;
        }catch(const std::bad_alloc&){}
    }
    Stats stats()const{std::lock_guard lock(mutex);return {retained,entries.size(),hits,misses};}
};
} // namespace editor_native_cache
