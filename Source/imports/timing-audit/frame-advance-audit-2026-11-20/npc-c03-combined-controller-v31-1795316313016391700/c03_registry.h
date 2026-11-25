#pragma once
#include "c03_exports.h"
using C03Read = bool (*)(void*, uint64_t, void*, size_t);

namespace c03_private {
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
    struct Model { uint64_t ptr{},birth{},cookie{},sdk{}; bool exact{}; };
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

