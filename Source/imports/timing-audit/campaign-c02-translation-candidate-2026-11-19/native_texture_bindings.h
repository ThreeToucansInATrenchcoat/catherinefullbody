#pragma once
#include <vector>
#include "native_image_contract.h"
#include "native_submit_resources.h"
namespace nx::nvn::native_bindings {
inline int mode() {
    static const int value=[] {const auto* s=std::getenv("CATHERINE_VULKAN_TEXTURE_BINDINGS");return s?std::atoi(s):1;}();
    return value;
}
struct Context;
inline thread_local Context* active=nullptr;
class TextureRef {
    Texture* object=nullptr;
    uint64_t serial=0,mark=0,token=0,packet=0;
public:
    uint64_t key=0;
    TextureRef()=default;
    TextureRef(Texture* p,uint64_t guest,uint64_t s,uint64_t m,uint64_t t,uint64_t batch):object(p),serial(s),mark(m),token(t),packet(batch),key(guest) {}
    Texture* get()const;
};
class AdmittedVersion {
    Texture* object=nullptr;
    uint64_t token=0;
public:
    AdmittedVersion()=default;
    AdmittedVersion(Texture* p,uint64_t t):object(p),token(t) {}
    Texture* get()const;
};
struct PreparedView {
    native_resources::ScopedImage allocation;
    VkImageViewCreateInfo values{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    uint64_t cache_range=0;
    uint32_t mapping=0;
    bool prepared=false,own=false,compatible=false;
};
struct Binding {
    TextureRef texture;
    ViewInfo view; // owned resolved pool values; compatibility comparator / preparation frontier
    VkSampler sampler=VK_NULL_HANDLE;
    PreparedView prepared;
};
struct Context {
    uint64_t serial=0,mark=0,token=0,resource_token=0,packet=0;
    std::vector<Binding> bindings;
    struct Admission {uint64_t key=0;AdmittedVersion version;};
    std::array<Admission,256> admitted;
};
inline bool valid_context(const Context* c) {
    return c&&native_resources::active&&c->serial==t_replay_serial&&c->mark==t_replay_mark&&
        native_resources::active->token==c->resource_token;
}
inline Texture* TextureRef::get()const {
    return valid_context(active)&&active->token==token&&active->serial==serial&&active->mark==mark&&active->packet==packet?object:nullptr;
}
inline Texture* AdmittedVersion::get()const {return valid_context(active)&&active->token==token?object:nullptr;}
class BindingRef {
    uint64_t token=0,packet=0;
    size_t index=SIZE_MAX;
public:
    BindingRef()=default;
    BindingRef(uint64_t t,uint64_t p,size_t i):token(t),packet(p),index(i) {}
    Binding* get()const {
        return valid_context(active)&&active->token==token&&active->packet==packet&&index<active->bindings.size()?&active->bindings[index]:nullptr;
    }
};
inline uint64_t next_token=0;
inline thread_local std::vector<Binding> spare;
struct Census {uint64_t scopes=0,captured=0,uses=0,compared=0,unstable=0,synthetic=0,no_scope=0,pool_any=0,prepared=0,view_fallback=0,admission_hits=0,admission_checks=0;};
inline Census census;
class Scope {
    Context context;
    Context* previous=nullptr;
    bool installed=false;
public:
    Scope() {
        if(!mode()||!native_resources::active||!t_replay_serial||!t_replay_mark)return;
        context.serial=t_replay_serial;context.mark=t_replay_mark;context.token=++next_token;
        context.resource_token=native_resources::active->token;context.bindings=std::move(spare);
        previous=active;active=&context;installed=true;++census.scopes;
    }
    ~Scope() {
        if(!installed)return;
        // No positive GPU owner is stored here. The checked Texture references
        // die before the owning ReplayScope can drain retired unique_ptrs.
        context.bindings.clear();for(auto& entry:context.admitted)entry={};active=previous;
        if(context.bindings.capacity()>spare.capacity())spare=std::move(context.bindings);
        if(census.scopes==1||census.scopes%600==0)
            log("Native Texture bindings: %llu scopes, %llu captured, %llu uses, %llu comparisons, %llu unstable, %llu synthetic, %llu no scope, %llu live-pool fallback, %llu prepared views, %llu view fallbacks, %llu admission hits, %llu admission checks",
                (unsigned long long)census.scopes,(unsigned long long)census.captured,(unsigned long long)census.uses,(unsigned long long)census.compared,
                (unsigned long long)census.unstable,(unsigned long long)census.synthetic,(unsigned long long)census.no_scope,
                (unsigned long long)census.pool_any,(unsigned long long)census.prepared,(unsigned long long)census.view_fallback,
                (unsigned long long)census.admission_hits,(unsigned long long)census.admission_checks);
    }
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
};
inline void begin_packet() {
    if(valid_context(active)) {active->bindings.clear();++active->packet;}
}
inline BindingRef capture(uint64_t key,Texture* selected,VkSampler sampler,const ViewInfo& view) {
    if(!mode())return {};
    if(!valid_context(active)) {++census.no_scope;return {};}
    if(!key||!selected)return {};
    if((key>>60)>=14) {++census.synthetic;return {};}
    // This debug mode reads mutable pool_swizzle at the later sample point.
    // Do not freeze that observation earlier in a native view snapshot.
    static const bool live_pool=std::getenv("NX_POOL_SWIZZLE_ANY")!=nullptr;
    if(live_pool) {++census.pool_any;return {};}
    auto& admission=active->admitted[((key>>4)^(key>>14))&255];
    bool stable=admission.key==key&&admission.version.get()==selected;
    if(stable)++census.admission_hits;
    else {
        ++census.admission_checks;
        {std::shared_lock lock(objects_lock());stable=native_texture_stable_at_mark(key,active->mark,selected);}
        if(stable)admission={key,AdmittedVersion(selected,active->token)};
    }
    if(!stable) {++census.unstable;return {};}
    const auto index=active->bindings.size();Binding b;
    b.texture=TextureRef(selected,key,active->serial,active->mark,active->token,active->packet);b.sampler=sampler;b.view=view;
    active->bindings.push_back(std::move(b));++census.captured;return {active->token,active->packet,index};
}
inline Texture* checked(const Binding& b) {
    auto* t=b.texture.get();
    if(mode()==2&&t) {
        if(get<Texture>(b.texture.key)!=t)fatal("Native Texture version differs from marked compatibility identity");
        ++census.compared;
    }
    if(t)++census.uses;return t;
}
}
namespace nx::nvn {
GpuImage* image_from_texture_native(uint64_t,Texture&);
GpuImage* image_of_bound(const native_bindings::Binding&);
bool sampled_ready_bound(const native_bindings::Binding&);
bool prepare_bound_view(native_bindings::Binding&,const native_images::Contract&,GpuImage*,bool);
VkImageView sampled_view_bound(VkCommandBuffer,const native_bindings::Binding&,const native_images::Contract&,VkImageLayout*);
VkImageView storage_view_bound(VkCommandBuffer,const native_bindings::Binding&,const native_images::Contract&);
}
