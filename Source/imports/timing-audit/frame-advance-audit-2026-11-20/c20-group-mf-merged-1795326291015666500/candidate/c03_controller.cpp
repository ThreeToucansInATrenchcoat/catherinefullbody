#include "c03_registry.h"
#include "c03_rigid_transport.h"
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <unordered_map>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {
// This is the ONLY mutex for both lifecycle values and scalar history.
std::mutex controller_mutex;
c03_private::Registry registry;
c03_private::RigidTransport rigid;
thread_local c03_private::RigidTransport::Flow c3flow;
#include "scalar_history_locked.inc"

// perf 2026-11-22: same-process read without a kernel transition. Hot per-draw callers hold the
// controller mutex across these reads, and ReadProcessMemory made each one a syscall. A frame-based SEH
// handler runs before the host's unhandled-exception filter (which commits guest pages on demand), so an
// unreadable address still fails exactly as ReadProcessMemory did and never commits guest memory. The host
// registers no vectored exception handlers. CATHERINE_C03_SYSCALL_READ=1: ReadProcessMemory, as before.
__declspec(noinline) bool guarded_copy(const void* src,void* out,size_t n){
    __try{std::memcpy(out,src,n);return true;}
    __except(GetExceptionCode()==EXCEPTION_ACCESS_VIOLATION||GetExceptionCode()==EXCEPTION_IN_PAGE_ERROR?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return false;}
}
bool system_read(void*,uint64_t p,void*out,size_t n){
    static const bool syscall_read=[]{const char* e=std::getenv("CATHERINE_C03_SYSCALL_READ");return e&&e[0]=='1';}();
    if(syscall_read){SIZE_T done=0;return p && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(p),out,n,&done) && done==n;}
    if(!p||p>UINTPTR_MAX-n)return false;
    if(guarded_copy(reinterpret_cast<const void*>(p),out,n))return true;
    std::memset(out,0,n);return false;
}
C03Read reader=system_read;
void* reader_context=nullptr;
bool path_exact(const char*p,uint32_t kind,uint32_t variant,uint32_t resource){
    const auto* profile=c03_private::key_profile(kind,variant,resource,catherine::private_root_profiles::OwnerFamily::Lounge,catherine::private_root_profiles::ResourceFormat::Gfs);
    if(!profile||profile->format!=catherine::private_root_profiles::ResourceFormat::Gfs)return false;
    const size_t n=profile->actual_gfs_path.size();char path[64]{};
    return n<sizeof(path) && reader(reader_context,reinterpret_cast<uint64_t>(p),path,n+1) && path[n]==0 &&
        catherine::private_root_profiles::lookup(kind,variant,resource,std::string_view(path,n))==profile;
}
const c03_private::RootProfile* gmd_path_locked(uint64_t model,const char* path){
 uint64_t vptr{};uint32_t tuple[3]{};
 if(!model||!reader(reader_context,model,&vptr,8)||vptr!=0x70009f5a10ull||!reader(reader_context,model+0x5a0,tuple,sizeof(tuple)))return nullptr;
 char actual[64]{};size_t n=0;
 for(;n<sizeof(actual);++n){if(!reader(reader_context,reinterpret_cast<uint64_t>(path)+n,actual+n,1))return nullptr;if(!actual[n])break;}
 if(n==sizeof(actual))return nullptr;
 return catherine::private_root_profiles::lookup_format(tuple[0],tuple[1],tuple[2],std::string_view(actual,n),catherine::private_root_profiles::ResourceFormat::Gmd);
}
bool gmd_model_matches_locked(uint64_t model){
 const auto*p=registry.gmd_descriptor(model);uint64_t vptr{};uint32_t t[3]{};
 return p&&reader(reader_context,model,&vptr,8)&&vptr==0x70009f5a10ull&&reader(reader_context,model+0x5a0,t,sizeof(t))&&t[0]==p->kind&&t[1]==p->variant&&t[2]==p->resource;
}
void prune_model_locked(uint64_t p){
    rigid.revoke_model(p);
    uint64_t actors[c03_private::Registry::Capacity]{};size_t n=registry.dependent_actors(p,actors);
    for(size_t i=0;i<n;i++)forget_actor_locked(actors[i]);
}
void reject_actor_locked(uint64_t p){rigid.revoke_actor(p);forget_actor_locked(p);registry.actor_discontinuity(p);}
}

extern "C" {
void catherine_c03_actor_birth(uint64_t p){std::lock_guard lock(controller_mutex);rigid.revoke_actor(p);forget_actor_locked(p);registry.actor_birth(p);}
void catherine_c03_actor_retire(uint64_t p){std::lock_guard lock(controller_mutex);rigid.revoke_actor(p);forget_actor_locked(p);registry.actor_retire(p);}
void catherine_c03_actor_reset(uint64_t p){std::lock_guard lock(controller_mutex);reject_actor_locked(p);}
void catherine_c03_component_reset(uint64_t p){std::lock_guard lock(controller_mutex);if(p>=16)reject_actor_locked(p-16);}
void catherine_c03_model_birth(uint64_t p){std::lock_guard lock(controller_mutex);prune_model_locked(p);registry.model_birth(p);}
void catherine_c03_model_retire(uint64_t p){std::lock_guard lock(controller_mutex);prune_model_locked(p);registry.model_retire(p);}
void catherine_c03_resource_loaded(uint64_t p,uint64_t sdk,const char*path,uint32_t k,uint32_t v,uint32_t r){
    // Negative tuple rejects do not read foreign resource paths.
    bool exact=p && sdk && c03_private::key_profile(k,v,r,catherine::private_root_profiles::OwnerFamily::Lounge,catherine::private_root_profiles::ResourceFormat::Gfs) && path_exact(path,k,v,r);
    std::lock_guard lock(controller_mutex);prune_model_locked(p);registry.resource_loaded(p,sdk,k,v,r,exact);
}
uint64_t catherine_c03_gmd_prepare(uint64_t model,const char* path){
 std::lock_guard lock(controller_mutex);prune_model_locked(model);
 registry.gmd_prepare(model,gmd_path_locked(model,path));return registry.gmd_ticket(model);
}
uint64_t catherine_c03_gmd_ticket(uint64_t model){std::lock_guard lock(controller_mutex);return registry.gmd_ticket(model);}
void catherine_c03_gmd_requested(uint64_t model,uint64_t request,uint64_t ticket){
 std::lock_guard lock(controller_mutex);if(!registry.gmd_known(model))return;
 uint64_t actual{};
 if(!gmd_model_matches_locked(model)||!reader(reader_context,model+0x10,&actual,8)||actual!=request||!registry.gmd_request(model,request,ticket)){
  prune_model_locked(model);registry.model_retire(model);
 }
}
void catherine_c03_gmd_sdk_bound(uint64_t model,uint64_t sdk,uint64_t ticket){
 std::lock_guard lock(controller_mutex);if(!registry.gmd_known(model))return;
 uint64_t actual{};
 if(!gmd_model_matches_locked(model)||!reader(reader_context,model+0x468,&actual,8)||actual!=sdk||!registry.gmd_commit(model,sdk,ticket)){
  prune_model_locked(model);registry.model_retire(model);
 }
}
uint64_t catherine_c03_gmd_assignment_ticket(uint64_t model,uint64_t request,uint64_t source,uint64_t auxiliary){
 std::lock_guard lock(controller_mutex);uint64_t r{},s{},a{};
 if(!gmd_model_matches_locked(model)||!registry.gmd_request_matches(model,request)||!reader(reader_context,model+0x10,&r,8)||r!=request||!reader(reader_context,model+0x460,&s,8)||s!=source||!reader(reader_context,model+0x430,&a,8)||a!=auxiliary)return 0;
 return registry.gmd_ticket(model);
}
void catherine_c03_gmd_assignment_bound(uint64_t model,uint64_t sdk,uint64_t ticket,uint64_t request,uint64_t source,uint64_t auxiliary){
 std::lock_guard lock(controller_mutex);if(!registry.gmd_known(model))return;uint64_t r{},s{},a{},actual{};
 if(!ticket||registry.gmd_ticket(model)!=ticket||!gmd_model_matches_locked(model)||!registry.gmd_request_matches(model,request)||!reader(reader_context,model+0x10,&r,8)||r!=request||!reader(reader_context,model+0x460,&s,8)||s!=source||!reader(reader_context,model+0x430,&a,8)||a!=auxiliary||!reader(reader_context,model+0x468,&actual,8)||actual!=sdk||!registry.gmd_commit(model,sdk,ticket)){
  prune_model_locked(model);registry.model_retire(model);
 }
}
void catherine_c03_native_observe(uint64_t m,uint64_t a,uint64_t n,uint64_t s){
    c03_private::Registry::Preflight before{};
    {std::lock_guard lock(controller_mutex);
        // Only rejects: known flags alone NEVER admit a model or replace fresh source checks.
        if(!registry.preflight(a,m,before)){reject_actor_locked(a);return;}
    }
    C03Context fresh{};bool captured=c03_private::capture(reader,reader_context,m,a,n,s,fresh);
    std::lock_guard lock(controller_mutex);
    if(!captured||!registry.same_preflight(a,m,before)){reject_actor_locked(a);return;}
    C03Context old{};bool had=registry.hint(a,old);
    C03Admission prior{};bool resolved=had&&registry.resolve(old,true,false,prior);
    if(!registry.observe(fresh)){reject_actor_locked(a);return;}
    C03Admission now{};
    if(!registry.resolve(fresh,true,false,now)){reject_actor_locked(a);return;}
    if(!resolved||prior.association_serial!=now.association_serial||prior.continuity_serial!=now.continuity_serial)
        forget_actor_locked(a);
    const uint64_t prior_capability=rigid.generation(a);
    if(!rigid.reserve(reader,reader_context,now) || (prior_capability && prior_capability!=rigid.generation(a)))forget_actor_locked(a);
}
uint32_t catherine_c03_scalar_native(uint64_t a,uint64_t m,uint64_t lr,uint64_t clock,uint32_t raw,uint32_t nativebits,uint32_t factor,int native60,int config_high,uint32_t framework_bits){
    // No expensive native reads for native30/highoff/foreign update entry.
    if(lr!=0x70000b0fb0ull&&lr!=0x70000a1be8ull)return nativebits;
    if(lr==0x70000a1be8ull){std::lock_guard lock(controller_mutex);C03Context known{};
        if(!registry.hint(a,known)||known.model!=m||c03_private::context_caller(known)!=lr){reject_actor_locked(a);return nativebits;}}
    const bool narrow_pause=!(clock&0x80000000ull) && config_high && !native60 && (framework_bits&0x7fffffffu)==0;
    if(native60 || !config_high) {
        std::lock_guard lock(controller_mutex);reject_actor_locked(a);return nativebits;
    }
    if(!(clock&0x80000000ull) && !narrow_pause) {
        float framework;std::memcpy(&framework,&framework_bits,4);
        std::lock_guard lock(controller_mutex);
        c03_private::Registry::Preflight known{};C03Context existing{};C03Admission existing_admission{};
        // One slow presentation frame is not an actor/model/resource lifetime change.
        // Keep only existing authenticated ownership; never mint or refresh admission here.
        if(std::isfinite(framework)&&framework>0.f && registry.preflight(a,m,known) && registry.hint(a,existing) && existing.model==m && registry.resolve(existing,true,false,existing_admission) && c03_private::context_caller(existing)==lr) {
            // Expire this frame's unused visit; it cannot authorize a later active call.
            c03_private::Registry::Ticket unused{};C03Admission ignored{};
            if(registry.pending_ticket(a,unused) && !registry.consume_ticket(unused,unused.admission.context,ignored)) {
                reject_actor_locked(a);return nativebits;
            }
            forget_actor_locked(a);rigid.scalar_status(a,clock,0,nativebits,nativebits);
        } else reject_actor_locked(a);
        return nativebits;
    }
    c03_private::Registry::Ticket ticket{};
    {std::lock_guard lock(controller_mutex);
        if(!registry.pending_ticket(a,ticket))return nativebits;
        if(ticket.admission.context.model!=m||c03_private::context_caller(ticket.admission.context)!=lr){reject_actor_locked(a);return nativebits;}
    }
    C03Context fresh{};bool captured=c03_private::capture(reader,reader_context,m,a,ticket.admission.context.node,ticket.admission.context.sentinel,fresh);
    std::lock_guard lock(controller_mutex);C03Admission admitted{};
    if(!captured||c03_private::context_caller(fresh)!=lr||!registry.consume_ticket(ticket,fresh,admitted)){reject_actor_locked(a);return nativebits;}
    if(!rigid.arm(admitted)){forget_actor_locked(a);return nativebits;}
    const Key key{a,m,admitted.context.sdk,admitted.context.synthetic_root,admitted.association_serial};
    const bool existed=states.find(key)!=states.end();
    const uint32_t result=sample_locked(a,m,admitted.context.sdk,admitted.context.synthetic_root,admitted.association_serial,clock,raw,nativebits,factor,native60,config_high,framework_bits);
    const bool retained=states.find(key)!=states.end();
    rigid.scalar_status(a,clock,retained?(narrow_pause?3u:(existed?2u:1u)):0u,nativebits,result);
    return result;
}
void catherine_c03_registry_reset(){std::lock_guard lock(controller_mutex);forget_all_locked();rigid.reset();c3flow={};registry.reset();}
int catherine_c03_c3_status(uint64_t actor,CatherineC03RigidStatus*out){if(!out)return 0;*out={};std::lock_guard lock(controller_mutex);return rigid.status(actor,*out)?1:0;}
void catherine_c03_rigid_copied(uint64_t mesh,uint32_t slot,uint64_t destination){std::lock_guard lock(controller_mutex);uint64_t revoked=0;rigid.copied(reader,reader_context,mesh,slot,destination,&revoked);if(revoked)forget_actor_locked(revoked);}
void catherine_c03_c3_loaded(uint64_t source){std::lock_guard lock(controller_mutex);rigid.loaded(reader,reader_context,c3flow,source);}
void catherine_c03_c3_uploaded(uint64_t cpu,uint64_t backing,uint64_t offset,int gpu_eligible){std::lock_guard lock(controller_mutex);rigid.uploaded(reader,reader_context,c3flow,cpu,backing,offset,gpu_eligible!=0);}
int catherine_c03_c3_force_bind(uint64_t command){std::lock_guard lock(controller_mutex);return rigid.force_bind(c3flow,command)?1:0;}
void catherine_c03_c3_buffer_address(uint64_t command,uint64_t backing,uint64_t base,uint64_t offset){std::lock_guard lock(controller_mutex);rigid.buffer_address(reader,reader_context,c3flow,command,backing,base,offset);}
int catherine_c03_c3_snapshot(uint64_t command,uint64_t address,uint64_t size,CatherineC03RigidPacket*out){if(!out){CatherineC03RigidPacket discarded{};std::lock_guard lock(controller_mutex);rigid.snapshot(reader,reader_context,command,0,0,discarded);return 0;}*out={};std::lock_guard lock(controller_mutex);return rigid.snapshot(reader,reader_context,command,address,size,*out)?1:0;}
void catherine_c03_backing_retired(uint64_t backing){std::lock_guard lock(controller_mutex);rigid.backing_retired(backing);}
void catherine_c03_command_retired(uint64_t command){std::lock_guard lock(controller_mutex);rigid.command_retired(command);}
// Source-hook-only lifecycle notifications; no new actor admission or guest reads.
void catherine_c20_child_event(uint32_t event,uint64_t pointer){std::lock_guard lock(controller_mutex);auto actors=rigid.child_event(event,pointer);for(auto a:actors)if(a)forget_actor_locked(a);}
void catherine_c20_hierarchy_changed(uint64_t node,uint64_t old_parent,uint64_t new_parent){std::lock_guard lock(controller_mutex);auto actors=rigid.hierarchy_changed(node,old_parent,new_parent);for(auto a:actors)if(a)forget_actor_locked(a);}


}
