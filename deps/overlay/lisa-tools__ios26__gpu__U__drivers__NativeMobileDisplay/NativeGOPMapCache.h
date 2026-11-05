// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stddef.h>

// Caller serializes access. Each cached map owns a reference; find() returns
// another reference for the in-flight frame. Retired maps are released by the
// caller outside its lock. A map retains the descriptor used as its key.
template<class Map, size_t Count = 6> class NativeGOPMapCache {
    struct Entry {const void *memory=nullptr,*task=nullptr;Map *map=nullptr;uint32_t id=0;uint64_t used=0;};
    Entry entries_[Count]{};
    uint64_t uses_=0,next_=1;
public:
    Map *find(const void *memory,const void *task,uint32_t *id) {
        for(auto &entry:entries_)if(entry.map && entry.memory==memory && entry.task==task) {
            entry.used=++uses_;*id=entry.id;entry.map->retain();return entry.map;
        }
        *id=0;return nullptr;
    }
    // Keeps a cache reference; the caller's original map reference is unchanged.
    uint32_t insert(const void *memory,const void *task,Map *map,Map **retired) {
        *retired=nullptr;
        if(!map || next_>UINT32_MAX)return 0; // Never recycle a mapping identity.
        size_t victim=0;
        for(size_t i=0;i<Count;++i) {
            if(!entries_[i].map){victim=i;break;}
            if(entries_[i].used<entries_[victim].used)victim=i;
        }
        *retired=entries_[victim].map;
        map->retain();const uint32_t id=(uint32_t)next_++;
        entries_[victim]={memory,task,map,id,++uses_};return id;
    }
    Map *take() {
        for(auto &entry:entries_)if(entry.map){Map *map=entry.map;entry={};return map;}
        return nullptr;
    }
};
