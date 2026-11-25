#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <mutex>
#include <unordered_set>
#include "root9_native_api.h"
#include "root9_translation.h"
#include "root9_working_set_reader.h"

namespace {
std::mutex bridge_mutex;
catherine::root9::RigidTransport transport;
thread_local catherine::root9::RigidTransport::Thread local;
thread_local bool local_dirty=false;
thread_local uint64_t replay_cookie=0,replay_serial=0;
std::unordered_set<uint64_t> owned_contexts;
uint64_t last_recorded_context=0;
bool ever_owned=false;
std::atomic<bool> scope_seen{false},recorded_seen{false};
std::unordered_set<uint64_t> owned_models;
void leave_local_scope(){if(local_dirty){local={};local_dirty=false;}}
bool read_bytes(uint64_t address,void* destination,size_t size){
    return catherine::root9::private_reader::read(address,destination,size);
}
bool live(const CatherineBlock20Packet& p){return catherine_root9_owner_live(&p)!=0;}
bool active_scope(){
    if(!scope_seen.load(std::memory_order_acquire))return false;
    if(catherine_root9_has_owners())return true;
    std::lock_guard lock(bridge_mutex);if(catherine_root9_has_owners())return true;
    transport.clear();owned_models.clear();scope_seen.store(false,std::memory_order_release);return false;
}
}
extern "C" uint64_t catherine_root9_replay_enter(void){
    const uint64_t previous=replay_cookie;
    replay_cookie=replay_serial==UINT64_MAX?0:++replay_serial;
    return previous;
}
extern "C" void catherine_root9_replay_leave(uint64_t previous){replay_cookie=previous;}
extern "C" uint64_t catherine_root9_replay_cookie(void){return replay_cookie;}
namespace catherine::visual {
// Replay is a single ordered consumer. Both C3 and skin adapters call this exact
// out-of-line function; no per-TU or per-mesh interpolation history exists.
Root9Presentation& root9_presentation(){static Root9Presentation value;return value;}
}
extern "C" void catherine_root9_model_retired(uint64_t model){
    std::lock_guard lock(bridge_mutex);transport.retire(model);
    if(owned_models.erase(model)&&owned_models.empty()){transport.clear();scope_seen.store(false,std::memory_order_release);}
}
extern "C" void catherine_root9_rigid_copied(uint64_t mesh,uint32_t slot,uint64_t destination){
    CatherineBlock20Packet p{};bool owned=catherine_root9_copy_owner(mesh,slot,destination,&p)!=0;
    if(!owned&&!scope_seen.load(std::memory_order_acquire))return;
    std::lock_guard lock(bridge_mutex);
    if(owned){owned_models.insert(p.model);scope_seen.store(true,std::memory_order_release);}
    transport.copied(owned?&p:nullptr,mesh,destination,read_bytes);
}
extern "C" void catherine_root9_c3_loaded(uint64_t source){
    if(!active_scope()){leave_local_scope();return;}
    std::lock_guard lock(bridge_mutex);local_dirty=true;transport.loaded(local,source,read_bytes,live);
}
extern "C" void catherine_root9_c3_uploaded(uint64_t destination,uint64_t backing,uint64_t offset){
    if(!active_scope()){leave_local_scope();return;}
    std::lock_guard lock(bridge_mutex);local_dirty=true;transport.uploaded(local,destination,backing,offset,read_bytes,live);
}
extern "C" void catherine_root9_c3_buffer_address(uint64_t backing,uint64_t base){
    if(!active_scope()){leave_local_scope();return;}
    std::lock_guard lock(bridge_mutex);local_dirty=true;transport.buffer_address(local,backing,base,read_bytes,live);
}
extern "C" int catherine_root9_c3_snapshot(uint64_t command,uint64_t gpu,uint64_t size,catherine::root9::RigidPacket* out){
    if(!scope_seen.load(std::memory_order_acquire)&&!recorded_seen.load(std::memory_order_acquire)){*out={};return 0;}
    const bool active=active_scope();std::lock_guard lock(bridge_mutex);*out={};bool owned=active&&transport.snapshot(local,gpu,size,*out,read_bytes,live);
    if(owned){owned_contexts.insert(command);ever_owned=true;}else owned_contexts.erase(command);
    last_recorded_context=command;
    if(!active&&owned_contexts.empty())ever_owned=false;
    recorded_seen.store(ever_owned,std::memory_order_release);return owned;
}
extern "C" int catherine_root9_c3_force_bind(void){
    if(!recorded_seen.load(std::memory_order_acquire))return scope_seen.load(std::memory_order_acquire)&&local.upload_owned;
    std::lock_guard lock(bridge_mutex);
    if(!ever_owned)return scope_seen.load(std::memory_order_acquire)&&local.upload_owned;
    uint64_t engine=0,command=0;
    const bool known=read_bytes(0x7000cc9498ull,&engine,8)&&engine&&engine<=UINT64_MAX-0x5ca0&&read_bytes(engine+0x5ca0,&command,8)&&command;
    if(!known)return local.upload_owned||ever_owned;
    return local.upload_owned||owned_contexts.contains(command)||(ever_owned&&command!=last_recorded_context);
}
