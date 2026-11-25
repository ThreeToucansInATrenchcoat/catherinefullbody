#pragma once
// CPU-only immutable descriptor publication. No GPU API calls, Texture owners,
// or persistent Texture pointers. All writer hooks preserve the old object lock.
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "nvn.h"

namespace nx::nvn::native_pools {
inline int mode() {
    static const int value=[] {const char* e=std::getenv("CATHERINE_VULKAN_POOL_PUBLISHER");return e?std::clamp(std::atoi(e),0,2):1;}();
    return value;
}
struct Census {
    std::atomic<uint64_t> initialized{},finalized{},texture_published{},sampler_published{},lookups{},admitted{},fallback{},compared{},sampler_uses{},sampler_compared{},pruned{},blocked{};
};
inline Census census;
inline void report() {
    const auto n=census.lookups.load();if(n!=1&&n%1000000)return;
    log("Native pool publisher: mode %d lookups %llu admitted %llu fallback %llu compared %llu sampler uses %llu sampler comparisons %llu pools %llu textures %llu samplers %llu blocked %llu pruned %llu",mode(),
        (unsigned long long)n,(unsigned long long)census.admitted.load(),(unsigned long long)census.fallback.load(),(unsigned long long)census.compared.load(),
        (unsigned long long)census.sampler_uses.load(),(unsigned long long)census.sampler_compared.load(),(unsigned long long)census.initialized.load(),
        (unsigned long long)census.texture_published.load(),(unsigned long long)census.sampler_published.load(),(unsigned long long)census.blocked.load(),(unsigned long long)census.pruned.load());
}

inline VkSamplerAddressMode wrap(uint32_t w) {
    switch(w) {case 1:return VK_SAMPLER_ADDRESS_MODE_REPEAT;case 2:case 3:case 4:return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    case 5:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;case 6:return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;default:return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;}
}
using SamplerKey=std::array<unsigned char,sizeof(SamplerInfo)+1>;
struct SamplerPlan {
    SamplerInfo source; // comparison/readback projection, never decoded by native GPU execution
    std::array<VkSamplerCreateInfo,2> values;
    std::array<SamplerKey,2> keys;
};
inline std::shared_ptr<const SamplerPlan> compile_sampler(const SamplerInfo& source) {
    auto p=std::make_shared<SamplerPlan>();p->source=source;
    for(unsigned shadow=0;shadow!=2;++shadow) {
        auto& ci=p->values[shadow];std::memset(&ci,0,sizeof ci);ci.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        const auto m=source.min_filter;
        ci.minFilter=(m==1||m==3||m==5)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
        ci.magFilter=source.mag_filter==1?VK_FILTER_LINEAR:VK_FILTER_NEAREST;
        ci.mipmapMode=(m==4||m==5)?VK_SAMPLER_MIPMAP_MODE_LINEAR:VK_SAMPLER_MIPMAP_MODE_NEAREST;
        ci.addressModeU=wrap(source.wrap[0]);ci.addressModeV=wrap(source.wrap[1]);ci.addressModeW=wrap(source.wrap[2]);
        ci.mipLodBias=source.lod_bias;ci.minLod=m<=1?0.f:std::max(0.f,source.lod_min);ci.maxLod=m<=1?.25f:std::max(ci.minLod,source.lod_max);
        ci.anisotropyEnable=source.max_anisotropy>1.f;ci.maxAnisotropy=std::min(source.max_anisotropy,16.f);
        ci.compareEnable=shadow;ci.compareOp=shadow?vk_compare(source.compare_func):VK_COMPARE_OP_ALWAYS;
        ci.borderColor=source.border[3]>.5f?(source.border[0]>.5f?VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK):VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        std::memcpy(p->keys[shadow].data(),&source,sizeof source);p->keys[shadow][sizeof source]=shadow?'1':'0';
    }
    return p;
}
inline bool same_ci(const VkSamplerCreateInfo& a,const VkSamplerCreateInfo& b) {
#define SAME(F) if(std::memcmp(&a.F,&b.F,sizeof a.F))return false
    SAME(sType);SAME(pNext);SAME(flags);SAME(magFilter);SAME(minFilter);SAME(mipmapMode);SAME(addressModeU);SAME(addressModeV);SAME(addressModeW);
    SAME(mipLodBias);SAME(anisotropyEnable);SAME(maxAnisotropy);SAME(compareEnable);SAME(compareOp);SAME(minLod);SAME(maxLod);SAME(borderColor);SAME(unnormalizedCoordinates);
#undef SAME
    return true;
}
inline bool same_entry(const PoolEntry& a,const PoolEntry& b) {
    return a.has==b.has&&a.has_view==b.has_view&&a.texture==b.texture&&a.snapshot==b.snapshot&&std::memcmp(&a.view,&b.view,sizeof a.view)==0;
}
struct TextureSlot {PoolEntry value;};
using TextureDescription=std::shared_ptr<const TextureSlot>;
using SamplerDescription=std::shared_ptr<const SamplerPlan>; // null means absent, not a default sampler
template<class Value> struct Slot {
    Value current;
    std::vector<std::pair<uint64_t,Value>> before; // actual change epoch, not snapshot's later replacement epoch
};
struct Instance {
    uint64_t id=0,key=0,born_drain=0,blocked_until=0;
    const DescriptorPool* identity=nullptr; // opaque identity; never dereferenced here
    bool ready=false,blocked=false;
    uint64_t memory_pool=0,offset=0;uint32_t count=0;
    std::unordered_map<uint32_t,Slot<TextureDescription>> textures;
    std::unordered_map<uint32_t,Slot<SamplerDescription>> samplers;
};
struct Publisher {
    std::mutex mutex;
    uint64_t next_id=0,drains=0;
    std::unordered_map<uint64_t,Instance> current;
};
inline Publisher publisher;

// All hooks except complete() run under the unchanged exclusive objects lock.
// No publisher mutex holder ever acquires objects_lock: lock order is one-way.
inline uint64_t initialize(uint64_t key,const DescriptorPool* identity,bool replaced_object) {
    if(!mode())return 0;
    std::lock_guard lock(publisher.mutex);auto& map=publisher.current;
    const bool recreation=replaced_object||map.contains(key);
    Instance fresh;fresh.id=++publisher.next_id;fresh.key=key;fresh.identity=identity;fresh.born_drain=publisher.drains;
    fresh.blocked=recreation;fresh.blocked_until=current_epoch();const auto id=fresh.id;
    map.insert_or_assign(key,std::move(fresh));++census.initialized;if(recreation)++census.blocked;return id;
}
inline void complete(uint64_t key,uint64_t id,const DescriptorPool* identity,uint64_t memory_pool,uint64_t offset,uint32_t count) {
    if(!id)return;std::lock_guard lock(publisher.mutex);auto it=publisher.current.find(key);
    if(it==publisher.current.end()||it->second.id!=id||it->second.identity!=identity)return;
    auto& p=it->second;p.memory_pool=memory_pool;p.offset=offset;p.count=count;p.ready=true;
}
inline void finalize(uint64_t key) {
    if(!mode())return;std::lock_guard lock(publisher.mutex);auto it=publisher.current.find(key);if(it==publisher.current.end())return;
    auto& p=it->second;p.identity=nullptr;p.ready=false;p.blocked=true;p.born_drain=publisher.drains;p.blocked_until=current_epoch();++census.finalized;
}
inline Instance* writer(uint64_t key,const DescriptorPool* identity,uint64_t epoch) {
    auto it=publisher.current.find(key);if(it==publisher.current.end())return nullptr;
    auto& p=it->second;
    const auto live=objects().find(key);
    if(p.identity!=identity||live==objects().end()||live->second.get()!=identity) {
        p.blocked=true;p.blocked_until=std::max(p.blocked_until,epoch);p.born_drain=publisher.drains;++census.blocked;return nullptr;
    }
    return &p;
}
inline void texture(uint64_t key,const DescriptorPool* identity,uint32_t id,uint64_t epoch,const PoolEntry& before,const PoolEntry& after) {
    if(!mode()||texture_race_old())return;
    std::lock_guard lock(publisher.mutex);auto* p=writer(key,identity,epoch);if(!p)return;
    auto& s=p->textures[id];
    auto old=s.current;
    if(!old||!same_entry(old->value,before)) {
        // A direct map mutation outside our hooks must not be guessed stable.
        if(old) {p->blocked=true;p->blocked_until=std::max(p->blocked_until,epoch);p->born_drain=publisher.drains;++census.blocked;}
        old=std::make_shared<TextureSlot>(TextureSlot{before});
    }
    s.before.emplace_back(epoch,std::move(old));s.current=std::make_shared<TextureSlot>(TextureSlot{after});++census.texture_published;
}
inline void sampler(uint64_t key,const DescriptorPool* identity,uint32_t id,uint64_t epoch,bool changed,const SamplerSlot& before,const SamplerInfo& after) {
    if(!mode()||texture_race_old())return;
    std::lock_guard lock(publisher.mutex);auto* p=writer(key,identity,epoch);if(!p)return;
    auto& s=p->samplers[id];
    if(!changed&&s.current&&std::memcmp(&s.current->source,&after,sizeof after)==0)return;
    SamplerDescription old;
    if(before.has)old=s.current&&std::memcmp(&s.current->source,&before.info,sizeof before.info)==0?s.current:compile_sampler(before.info);
    if(changed)s.before.emplace_back(epoch,std::move(old));
    s.current=compile_sampler(after);++census.sampler_published;
}

// Called immediately AFTER the actual legacy purge in drain_objects, under its
// same lock. A later real drain, not equality with a pre-existing replay floor,
// proves that cross-instance legacy history through blocked_until is gone.
inline void drained(uint64_t upto) {
    if(!mode())return;std::lock_guard lock(publisher.mutex);++publisher.drains;
    for(auto it=publisher.current.begin();it!=publisher.current.end();) {
        auto& p=it->second;const auto live=objects().find(p.key);
        if(p.identity&&(live==objects().end()||live->second.get()!=p.identity)) {
            p.identity=nullptr;p.ready=false;p.blocked=true;p.blocked_until=std::max(p.blocked_until,current_epoch());p.born_drain=publisher.drains;
        }
        auto trim=[&](auto& slots) {for(auto& [id,s]:slots) {
            auto old=s.before.size();s.before.erase(std::remove_if(s.before.begin(),s.before.end(),[&](const auto& x){return x.first<=upto;}),s.before.end());census.pruned+=old-s.before.size();
        }};
        trim(p.textures);trim(p.samplers);
        if(p.blocked&&p.born_drain<publisher.drains&&p.blocked_until<=upto) {
            if(!p.identity) {it=publisher.current.erase(it);continue;}
            p.blocked=false;
        }
        ++it;
    }
}

// objects_lock is held shared by the caller, and the actual get<DescriptorPool>
// observations are supplied rather than reconstructed from guest key + mark.
inline bool select(uint64_t tk,const DescriptorPool* tp,uint32_t tid,uint64_t sk,const DescriptorPool* sp,uint32_t sid,uint64_t mark,PoolEntry& entry,SamplerDescription& sampler) {
    if(!mode())return false;++census.lookups;
    static const bool kept=std::getenv("NX_KEEP_TEXTURES")!=nullptr;
    if(!mark||!t_replay_serial||texture_race_old()||kept) {++census.fallback;report();return false;}
    std::lock_guard lock(publisher.mutex);
    auto instance=[&](uint64_t key,const DescriptorPool* identity)->Instance* {
        if(!identity)return nullptr;auto it=publisher.current.find(key);if(it==publisher.current.end())return nullptr;
        auto& p=it->second;auto live=objects().find(key);
        return p.ready&&!p.blocked&&p.identity==identity&&live!=objects().end()&&live->second.get()==identity?&p:nullptr;
    };
    auto* t=instance(tk,tp);auto* s=instance(sk,sp);
    if((tp&&!t)||(sp&&!s)) {++census.fallback;report();return false;}
    entry={};sampler.reset();
    if(t)if(auto it=t->textures.find(tid);it!=t->textures.end()) {
        auto value=it->second.current;
        for(const auto& [epoch,before]:it->second.before)if(epoch>mark) {if(before&&(before->value.has||mark_empty_old()))value=before;break;}
        if(value)entry=value->value;
    }
    if(s)if(auto it=s->samplers.find(sid);it!=s->samplers.end()) {
        sampler=it->second.current;
        for(const auto& [epoch,before]:it->second.before)if(epoch>mark) {if(before)sampler=before;break;}
    }
    ++census.admitted;report();return true;
}
inline void verify_selection(const PoolEntry& actual,const PoolEntry& legacy,const SamplerDescription& sampler,bool old_has,const SamplerInfo& old_sampler) {
    if(mode()!=2)return;
    if(!same_entry(actual,legacy)||bool(sampler)!=old_has||(sampler&&std::memcmp(&sampler->source,&old_sampler,sizeof old_sampler)))fatal("Native pool publication differs from original marked descriptor lookup");
    ++census.compared;
}
} // namespace nx::nvn::native_pools

namespace nx::nvn {
void verify_published_sampler(const native_pools::SamplerPlan& plan,bool shadow);
VkSampler sampler_for_published(const native_pools::SamplerPlan& plan,bool shadow);
}
