#pragma once
#include "c03_exports.h"
#include "npc_exact_root_profiles.h"
using C03Read = bool (*)(void*, uint64_t, void*, size_t);

namespace c03_private {
using RootProfile=catherine::private_root_profiles::Profile;
// Only the loader's authenticated exact-path boolean may bind this tuple descriptor.
inline const RootProfile* key_profile(uint32_t kind,uint32_t variant,uint32_t resource,catherine::private_root_profiles::OwnerFamily family,catherine::private_root_profiles::ResourceFormat format) noexcept {
 const RootProfile* found=nullptr;
 for(const auto& p:catherine::private_root_profiles::profiles)
  if(p.kind==kind&&p.variant==variant&&p.resource==resource&&p.family==family&&p.format==format){if(found)return nullptr;found=&p;}
 return found;
}
inline const RootProfile* tuple_profile(uint32_t kind,uint32_t variant,uint32_t resource) noexcept {
 const RootProfile* found=nullptr;
 for(const auto& p:catherine::private_root_profiles::profiles)
  if(p.kind==kind&&p.variant==variant&&p.resource==resource){if(found)return nullptr;found=&p;}
 return found;
}
inline catherine::private_root_profiles::OwnerFamily context_family(const C03Context& c) noexcept {
 using F=catherine::private_root_profiles::OwnerFamily;
 if(c.manager==c.root+0x970&&c.actor_vptr==0x70009f6d98ull&&c.manager_vptr==0x70009f6ea0ull)return F::Lounge;
 if(c.manager==c.root+0xa00&&c.actor_vptr==0x70009f7b88ull&&c.manager_vptr==0x70009f7c18ull)return F::Middlefloor;
 return F(0);
}
inline const RootProfile* context_profile(const C03Context& c) noexcept {
 using F=catherine::private_root_profiles::OwnerFamily;using R=catherine::private_root_profiles::ResourceFormat;
 const auto family=context_family(c);if(family!=F::Lounge&&family!=F::Middlefloor)return nullptr;
 return key_profile(c.kind,c.variant,c.resource,family,family==F::Lounge?R::Gfs:R::Gmd);
}
inline uint64_t context_caller(const C03Context& c) noexcept {
 using F=catherine::private_root_profiles::OwnerFamily;
 return context_family(c)==F::Lounge?0x70000b0fb0ull:context_family(c)==F::Middlefloor?0x70000a1be8ull:0;
}
class Registry {
public:
    static constexpr size_t Capacity = 256;
    void reset();
    void actor_birth(uint64_t);
    void actor_retire(uint64_t);
    void actor_discontinuity(uint64_t);
    void component_discontinuity(uint64_t);
    void model_birth(uint64_t);
    void model_retire(uint64_t);
    void resource_loaded(uint64_t model, uint64_t sdk, uint32_t kind,
                         uint32_t variant, uint32_t resource, bool exact_path);
    void gmd_prepare(uint64_t,const RootProfile*);
    bool gmd_request(uint64_t,uint64_t,uint64_t);
    bool gmd_commit(uint64_t,uint64_t,uint64_t);
    bool gmd_known(uint64_t)const;
    uint64_t gmd_ticket(uint64_t)const;
    bool gmd_request_matches(uint64_t,uint64_t)const;
    const RootProfile* gmd_descriptor(uint64_t)const;
    struct Preflight {uint64_t actor_birth,model_birth,resource_cookie;};
    struct Ticket {C03Admission admission;uint64_t visit;};
    bool preflight(uint64_t actor,uint64_t model,Preflight&) const;
    bool same_preflight(uint64_t actor,uint64_t model,const Preflight&) const;
    size_t dependent_actors(uint64_t model,uint64_t out[Capacity]) const;
    bool pending_ticket(uint64_t actor,Ticket&) const;
    bool consume_ticket(const Ticket&,const C03Context&,C03Admission&);
    bool observe(const C03Context&);
    bool hint(uint64_t actor, C03Context&) const;
    bool resolve(const C03Context&, bool high_refresh, bool native60,
                 C03Admission&) const;
private:
    struct Actor { uint64_t ptr{},birth{},continuity{},association{},visit{}; C03Context seen{}; };
    struct Model { uint64_t ptr{},birth{},cookie{},sdk{}; const RootProfile* profile{}; bool exact{}; uint64_t request{},candidate_sdk{}; bool request_ready{}; };
    Actor actors_[Capacity]{};
    Model models_[Capacity]{};
    uint64_t next_{1};
    uint64_t mint();
    Actor* actor(uint64_t);
    const Actor* actor(uint64_t) const;
    Model* model(uint64_t);
    const Model* model(uint64_t) const;
    void revoke_model(uint64_t);
};
bool capture(C03Read, void*, uint64_t model, uint64_t actor,
             uint64_t node, uint64_t sentinel, C03Context&);
}

