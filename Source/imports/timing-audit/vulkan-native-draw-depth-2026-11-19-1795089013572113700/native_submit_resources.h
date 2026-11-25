#pragma once
#include <array>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include "nvn_draw_state.h"

namespace nx::nvn {
GpuImage* image_of_compat(uint64_t);
// Called under objects_lock; uses the same incarnation history as find_object.
bool native_texture_stable_at_mark(uint64_t, uint64_t, const Object*);
}

namespace nx::nvn::native_resources {
inline int mode() {
    static const int value=[] {
        const auto* setting=std::getenv("CATHERINE_VULKAN_SUBMIT_RESOURCES");
        return setting?std::atoi(setting):1;
    }();
    return value;
}
struct Context;
inline thread_local Context* active=nullptr;
// The public reference is a checked borrow. Only Context owns positive leases.
class ScopedImage {
    GpuImage* image=nullptr;
    uint64_t serial=0,token=0;
public:
    uint64_t resource_id=0;
    ScopedImage()=default;
    ScopedImage(GpuImage* p,uint64_t id,uint64_t replay_serial,uint64_t scope_token):image(p),serial(replay_serial),token(scope_token),resource_id(id) {}
    GpuImage* get() const;
};
struct Context {
    uint64_t serial=0, mark=0, token=0, next_id=0;
    std::unordered_map<uint64_t,ScopedImage> textures;
    std::unordered_map<const GpuImage*,uint64_t> image_ids;
    std::vector<std::shared_ptr<GpuImage>> leases; // append-only until scope exit, even after mapping invalidation
};
inline GpuImage* ScopedImage::get() const {
    return active&&active->token==token&&active->serial==serial&&t_replay_serial==serial?image:nullptr;
}
inline uint64_t next_scope_token=0;
struct Census {
    uint64_t scopes=0,hits=0,misses=0,retained=0,unstable=0,unavailable=0;
    uint64_t invalidated=0,compared=0,mismatches=0,targets=0,target_compared=0,target_fallbacks=0;
};
inline Census census;
class Scope {
    Context context;
    Context* previous=nullptr;
    bool installed=false;
public:
    Scope() {
        if(!mode()||!t_replay_serial||!t_replay_mark||texture_race_old())return;
        previous=active;context.serial=t_replay_serial;context.mark=t_replay_mark;
        context.token=++next_scope_token;
        active=&context;installed=true;++census.scopes;
    }
    ~Scope() {
        if(!installed)return;
        // Destroy positive leases while this ReplayScope still keeps every
        // referenced Texture alive. No native lease escapes into a GPU slot.
        context.textures.clear();context.image_ids.clear();context.leases.clear();active=previous;
        if(census.scopes==1||census.scopes%600==0)
            log("Native submit resources: %llu scopes, %llu hits, %llu retained, %llu unstable, %llu unavailable, "
                "%llu invalidated, %llu compared, %llu mismatches, %llu native target bindings, %llu target comparisons, %llu target fallbacks",
                (unsigned long long)census.scopes,(unsigned long long)census.hits,
                (unsigned long long)census.retained,(unsigned long long)census.unstable,
                (unsigned long long)census.unavailable,(unsigned long long)census.invalidated,
                (unsigned long long)census.compared,(unsigned long long)census.mismatches,
                (unsigned long long)census.targets,(unsigned long long)census.target_compared,
                (unsigned long long)census.target_fallbacks);
    }
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
inline bool eligible_key(uint64_t guest) {
    // Synthetic snapshot/kept keys have different creation and mutation rules.
    return guest && (guest>>60)<14;
}
inline void invalidate(uint64_t guest) {
    if(active)census.invalidated+=active->textures.erase(guest);
}
inline ScopedImage find_owned(uint64_t guest) {
    if(!active||active->serial!=t_replay_serial||active->mark!=t_replay_mark)return {};
    const auto it=active->textures.find(guest);
    if(it==active->textures.end()||!it->second.get()||!it->second.get()->image)return {};
    return it->second;
}
inline GpuImage* cached(uint64_t guest) {
    const auto owned=find_owned(guest);
    auto* const pinned=owned.get();
    if(!pinned)return nullptr;
    ++census.hits;
    if(mode()==2) {
        const auto* old=image_of_compat(guest);
        ++census.compared;
        if(old!=pinned||!old||old->image!=pinned->image) {
            ++census.mismatches;
            fatal("Native submit image lease disagrees with incarnation resolution");
        }
    }
    return pinned;
}
inline void retain(uint64_t guest,GpuImage* resolved) {
    if(!active)return;
    if(!eligible_key(guest)) { ++census.unstable;return; }
    ++census.misses;
    if(!resolved||!resolved->image) { ++census.unavailable;return; }
    Texture* texture=get<Texture>(guest);
    if(!texture||texture->image.get()!=resolved) { ++census.unavailable;return; }
    bool stable=false;
    {
        std::shared_lock lock(objects_lock());
        stable=native_texture_stable_at_mark(guest,active->mark,texture);
    }
    if(!stable) { ++census.unstable;return; }
    // Image identities describe Vulkan allocations, so aliases share the ID.
    auto [id,inserted]=active->image_ids.try_emplace(resolved,0);
    if(inserted) { id->second=++active->next_id;active->leases.push_back(texture->image); }
    active->textures[guest]=ScopedImage(resolved,id->second,active->serial,active->token);
    ++census.retained;
}
struct Attachment {
    ScopedImage resource;
    uint32_t level=0,layer=0;
    VkFormat format=VK_FORMAT_UNDEFINED;
};
struct TargetBinding {
    std::array<Attachment,9> attachments;
    VkExtent2D extent{1,1};
    uint32_t color_count=0;
    uint64_t scope_serial=0,scope_token=0;
    bool complete=false;
};
// A typed native pass input, created only at the correctly marked submit
// frontier. It carries native allocation ownership and already resolved Vulkan
// formats/levels/extents, rather than reinterpreting texture keys in the pass.
inline TargetBinding prepare_targets(const Targets& targets) {
    TargetBinding binding;
    if(!active)return binding;
    binding.color_count=targets.color_count;binding.scope_serial=active->serial;binding.scope_token=active->token;
    VkExtent2D extent{UINT32_MAX,UINT32_MAX};
    auto one=[&](uint32_t slot,uint64_t guest) {
        if(!guest)return true;
        if(!eligible_key(guest))return false;
        GpuImage* raw=image_of(guest);
        auto owned=find_owned(guest);
        if(!owned.get()||owned.get()!=raw)return false;
        auto& attachment=binding.attachments[slot];
        attachment.resource=owned;
        attachment.level=std::min<uint32_t>(targets.views.level[slot],raw->levels-1);
        attachment.layer=targets.views.layer[slot];
        attachment.format=slot<8?view_format(*raw,targets.views.format[slot]):raw->format;
        if(slot<8||raw->aspect!=VK_IMAGE_ASPECT_COLOR_BIT)
            extent={std::min(extent.width,std::max(1u,raw->width>>attachment.level)),
                    std::min(extent.height,std::max(1u,raw->height>>attachment.level))};
        return true;
    };
    for(uint32_t slot=0;slot<targets.color_count;++slot)
        if(!one(slot,targets.colors[slot])) { ++census.target_fallbacks;return binding; }
    if(!one(8,targets.depth)) { ++census.target_fallbacks;return binding; }
    if(extent.width!=UINT32_MAX)binding.extent=extent;
    binding.complete=true;++census.targets;
    if(mode()==2) {
        VkExtent2D expected{UINT32_MAX,UINT32_MAX};
        auto check=[&](uint32_t slot,uint64_t guest) {
            if(!guest)return;
            const auto* old=image_of_compat(guest);
            const auto& prepared=binding.attachments[slot];
            const auto level=old?std::min<uint32_t>(targets.views.level[slot],old->levels-1):0;
            const auto format=old?(slot<8?view_format(*old,targets.views.format[slot]):old->format):VK_FORMAT_UNDEFINED;
            if(!old||prepared.resource.get()!=old||prepared.level!=level||prepared.layer!=targets.views.layer[slot]||prepared.format!=format)
                fatal("Native target binding disagrees with compatibility attachment");
            if(slot<8||old->aspect!=VK_IMAGE_ASPECT_COLOR_BIT)
                expected={std::min(expected.width,std::max(1u,old->width>>level)),
                          std::min(expected.height,std::max(1u,old->height>>level))};
        };
        for(uint32_t slot=0;slot<targets.color_count;++slot)check(slot,targets.colors[slot]);
        check(8,targets.depth);
        if(expected.width==UINT32_MAX)expected={1,1};
        if(binding.extent.width!=expected.width||binding.extent.height!=expected.height)
            fatal("Native target binding disagrees with compatibility extent");
        ++census.target_compared;
    }
    return binding;
}
}
