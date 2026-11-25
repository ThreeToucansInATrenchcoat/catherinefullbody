#include "c03_registry.h"
#include <cstring>
#include <limits>
#include <mutex>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace c03_private {
static bool eligible(const C03Context& c) {
    return c.root && c.actor && c.wrapper && c.model && c.sdk && c.authored_root && c.synthetic_root &&
        c.manager==c.root+0x970 && c.sentinel==c.manager+8 && c.node && c.node!=c.sentinel &&
        c.component==c.actor+0x10 && c.actor_vptr==0x70009f6d98ull &&
        c.component_vptr==0x70009f6ae0ull && c.manager_vptr==0x70009f6ea0ull &&
        c.model_vptr==0x70009f5a10ull && tuple_profile(c.kind,c.variant,c.resource)!=nullptr &&
        c.member_proven==1 && c.ready==1;
}
static bool equal(const C03Context&a,const C03Context&b) {
    return a.root==b.root && a.manager==b.manager && a.node==b.node && a.sentinel==b.sentinel &&
        a.actor==b.actor && a.component==b.component && a.wrapper==b.wrapper && a.model==b.model &&
        a.sdk==b.sdk && a.authored_root==b.authored_root && a.synthetic_root==b.synthetic_root &&
        a.actor_vptr==b.actor_vptr && a.component_vptr==b.component_vptr &&
        a.manager_vptr==b.manager_vptr && a.model_vptr==b.model_vptr &&
        a.kind==b.kind && a.variant==b.variant && a.resource==b.resource &&
        a.member_proven==b.member_proven && a.ready==b.ready;
}
uint64_t Registry::mint() {
    // Exhaustion fails closed permanently; never recycle an association identifier.
    if (!next_ || next_==std::numeric_limits<uint64_t>::max()) { next_=0; return 0; }
    return next_++;
}
Registry::Actor* Registry::actor(uint64_t p){for(auto& a:actors_)if(p && a.ptr==p)return &a;return nullptr;}
const Registry::Actor* Registry::actor(uint64_t p)const{for(auto& a:actors_)if(p && a.ptr==p)return &a;return nullptr;}
Registry::Model* Registry::model(uint64_t p){for(auto& m:models_)if(p && m.ptr==p)return &m;return nullptr;}
const Registry::Model* Registry::model(uint64_t p)const{for(auto& m:models_)if(p && m.ptr==p)return &m;return nullptr;}
void Registry::reset(){for(auto&a:actors_)a={};for(auto&m:models_)m={};/* next_ deliberately retained */}
void Registry::actor_birth(uint64_t p){
    if(!p)return;Actor*a=actor(p);if(!a)for(auto&v:actors_)if(!v.ptr){a=&v;break;}
    if(!a)return;*a={};a->ptr=p;a->birth=mint();a->continuity=mint();
}
void Registry::actor_retire(uint64_t p){if(auto*a=actor(p))*a={};}
void Registry::actor_discontinuity(uint64_t p){if(auto*a=actor(p)){a->continuity=mint();a->association=0;a->visit=0;a->seen={};}}
void Registry::component_discontinuity(uint64_t p){if(p>=16)actor_discontinuity(p-16);}
void Registry::revoke_model(uint64_t p){for(auto&a:actors_)if(a.seen.model==p){a.association=0;a.visit=0;a.seen={};a.continuity=mint();}}
void Registry::model_birth(uint64_t p){
    // Unknown standard models do not occupy this exact-profile registry. A constructor
    // at a known address still revokes its previous binding immediately.
    if(!p)return;revoke_model(p);if(auto*m=model(p))*m={};
}
void Registry::model_retire(uint64_t p){revoke_model(p);if(auto*m=model(p))*m={};}
void Registry::resource_loaded(uint64_t p,uint64_t sdk,uint32_t k,uint32_t v,uint32_t r,bool path){
    revoke_model(p);auto*m=model(p);
    const auto* profile=tuple_profile(k,v,r);
    if(!p||!sdk||!path||!profile){if(m)*m={};return;}
    if(!m)for(auto&v:models_)if(!v.ptr){m=&v;break;}
    if(!m)return;
    // This birth is an authenticated standard-model RESOURCE BINDING epoch.
    // Exact 899E0 source calls84C70 before tuple/path/2E5620 publication.
    // It is deliberately not an unbounded ledger of all standard constructors.
    *m={};m->ptr=p;m->birth=mint();m->cookie=mint();m->sdk=sdk;m->profile=profile;m->exact=true;
}
bool Registry::preflight(uint64_t a,uint64_t m,Preflight&out)const{
    out={};auto*aa=actor(a);auto*mm=model(m);
    if(!aa||!aa->birth||!aa->continuity||!mm||!mm->birth||!mm->cookie||!mm->exact)return false;
    out={aa->birth,mm->birth,mm->cookie};return true;
}
bool Registry::same_preflight(uint64_t a,uint64_t m,const Preflight&p)const{
    Preflight now{};return preflight(a,m,now)&&now.actor_birth==p.actor_birth&&now.model_birth==p.model_birth&&now.resource_cookie==p.resource_cookie;
}
size_t Registry::dependent_actors(uint64_t p,uint64_t out[Capacity])const{size_t n=0;for(auto&a:actors_)if(a.ptr&&a.seen.model==p)out[n++]=a.ptr;return n;}
bool Registry::pending_ticket(uint64_t p,Ticket&t)const{
    t={};auto*a=actor(p);if(!a||!a->visit||!a->association)return false;
    if(!resolve(a->seen,true,false,t.admission))return false;t.visit=a->visit;return true;
}
bool Registry::consume_ticket(const Ticket&t,const C03Context&c,C03Admission&out){
    out={};auto*a=actor(c.actor);auto*m=model(c.model);
    if(!a||!m||!t.visit||a->visit!=t.visit||a->association!=t.admission.association_serial||a->continuity!=t.admission.continuity_serial||
       a->birth!=t.admission.actor_birth||m->birth!=t.admission.model_birth||m->cookie!=t.admission.resource_cookie||
       !equal(t.admission.context,c)||!resolve(c,true,false,out))return false;
    a->visit=0;return true;
}
bool Registry::observe(const C03Context&c){
    auto*a=actor(c.actor);auto*m=model(c.model);
    if(!a)return false;
    if(!eligible(c)||!a->birth||!a->continuity||!m||!m->birth||!m->cookie||!m->exact||m->profile!=tuple_profile(c.kind,c.variant,c.resource)||m->sdk!=c.sdk){
        a->association=0;a->visit=0;a->seen={};return false;
    }
    if(!a->association||!equal(a->seen,c)){a->association=mint();a->continuity=mint();}
    a->seen=c;a->visit=mint();return a->association && a->continuity && a->visit;
}
bool Registry::hint(uint64_t p,C03Context&c)const{c={};auto*a=actor(p);if(!a||!a->association)return false;c=a->seen;return true;}
bool Registry::resolve(const C03Context&c,bool high,bool native60,C03Admission&out)const{
    out={};if(!high||native60||!eligible(c))return false;
    auto*a=actor(c.actor);auto*m=model(c.model);
    if(!a||!m||!a->birth||!a->association||!a->continuity||!m->birth||!m->cookie||!m->exact||m->profile!=tuple_profile(c.kind,c.variant,c.resource)||m->sdk!=c.sdk||!equal(a->seen,c))return false;
    out={c,a->birth,m->birth,m->cookie,a->association,a->continuity};return true;
}

bool capture(C03Read read,void*ctx,uint64_t model,uint64_t actor,uint64_t node,uint64_t sentinel,C03Context&c){
    c={};if(!read||!model||!actor||!node||sentinel<8)return false;
    auto u64=[&](uint64_t p,uint64_t&v){v=0;return read(ctx,p,&v,8);};
    auto u32=[&](uint64_t p,uint32_t&v){v=0;return read(ctx,p,&v,4);};
    uint64_t holder{},root{},manager=sentinel-8,owned{},first{},next{},flat{},array{},parent{},ready{};
    if(!u64(0x7000cca2c8ull,holder)||!holder||!u64(holder,root)||!root||manager!=root+0x970)return false;
    c.root=root;c.manager=manager;c.node=node;c.sentinel=sentinel;c.actor=actor;c.component=actor+16;c.model=model;
    if(!u64(manager,c.manager_vptr)||!u64(actor,c.actor_vptr)||!u64(actor+16,c.component_vptr)||
       !u64(actor+32,c.wrapper)||!c.wrapper||!u64(node+16,owned)||owned!=actor)return false;
    if(c.manager_vptr!=0x70009f6ea0ull||c.actor_vptr!=0x70009f6d98ull||c.component_vptr!=0x70009f6ae0ull)return false;
    // Current actual native linked-list membership, not nearest-position inference.
    if(!u64(manager+16,first))return false;
    for(size_t n=0;n<Registry::Capacity && first && first!=sentinel;n++){
        if(first==node){c.member_proven=1;break;}
        if(!u64(first+8,next)||next==first)return false;first=next;
    }
    if(!c.member_proven)return false;
    if(!u64(model,c.model_vptr)||!u64(model+0x468,c.sdk)||!c.sdk||
       !u64(model+0x4a8,c.synthetic_root)||!c.synthetic_root||!u64(model+0x450,ready)||
       !u32(model+0x5a0,c.kind)||!u32(model+0x5a4,c.variant)||!u32(model+0x5a8,c.resource)||
       !u64(c.sdk+0x28,c.authored_root)||!c.authored_root||!u64(c.sdk+0x30,flat)||!flat||
       !u64(flat+0x10,array)||!array||!u64(array,owned)||owned!=c.authored_root||
       !u64(c.authored_root+8,parent)||parent!=c.synthetic_root)return false;
    c.ready=ready?1:0;return eligible(c);
}
}
