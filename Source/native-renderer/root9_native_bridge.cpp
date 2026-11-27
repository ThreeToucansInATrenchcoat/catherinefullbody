#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifdef _WIN32
#include <windows.h>
#endif
#include <atomic>
#include <mutex>
#include "C:/catherine/native-gpu-experiment/Source/root9_bridge_mutex.h"
#include "C:/catherine/native-gpu-experiment/Source/root9_source_presence.h"
#include "C:/catherine/native-gpu-experiment/Source/root9_force_snapshot.h"
#include <cstdio>
#include <unordered_set>
#include <unordered_map>
#include <cmath>
extern "C" uint64_t catherine_camera_render_clock(void);
#include "root9_native_api.h"
#include "root9_translation.h"
#include "root12_translation.h"
#include "root9_working_set_reader.h"
#include "C:/catherine/recomp/Source/imports/timing-audit/campaign-c02-translation-candidate-2026-11-19/c02_guarded_read.h"
#include <cstdlib>

namespace {
experimental_sync::BridgeMutex bridge_mutex;
experimental_root9::SourcePresence source_presence;
int presence_mode(){static const int value=[] {const auto*e=std::getenv("CATHERINE_ROOT9_SOURCE_PRESENCE");return e?std::atoi(e):1;}();return value;}
struct PresenceCensus {uint64_t queries=0,negative=0,stable=0,concurrent=0;} presence_census;
catherine::root9::RigidTransport transport;
thread_local catherine::root9::RigidTransport::Thread local;
thread_local bool local_dirty=false;
thread_local uint64_t replay_cookie=0,replay_serial=0;
std::unordered_set<uint64_t> owned_contexts;
uint64_t last_recorded_context=0;
bool last_recorded_owned=false;
experimental_root9::ForceSnapshot force_snapshot;
int force_snapshot_mode(){static const int value=[] {const auto*e=std::getenv("CATHERINE_NATIVE_FORCE_SNAPSHOT");return e?std::atoi(e):1;}();return value;}
int context_memo_mode(){static const int value=[] {const auto*e=std::getenv("CATHERINE_NATIVE_CONTEXT_MEMO");return e?std::atoi(e):1;}();return value;}
bool ever_owned=false;
std::atomic<bool> scope_seen{false},recorded_seen{false};
std::unordered_set<uint64_t> owned_models;
void leave_local_scope(){if(local_dirty){local={};local_dirty=false;}}
bool read_bytes(uint64_t address,void* destination,size_t size){
    // The producer owns these reads. Use the existing guarded copy leaf rather
    // than querying the Windows working set for each 8/64-byte read. It catches
    // inaccessible/decommitted memory and restores guard pages on failed reads;
    // packet bytes and live-owner checks below still authenticate every use.
    static const bool direct=[] {const char* p=std::getenv("CATHERINE_ROOT9_GUARDED_READ");return !(p&&p[0]=='0');}();
    return direct ? c02_bridge::direct::read(address,destination,size)
                  : catherine::root9::private_reader::read(address,destination,size);
}
// Unweighted scene meshes have no actor-owner table entry. Their native mesh
// objects still provide distinct instance identities, including coincident copies.
std::unordered_map<uint64_t,catherine::root9::BackgroundOwner> background_owners;
std::atomic<bool> background_seen{false};
uint64_t background_serial=0;
// Negative routing hints never authenticate an upload. Recheck a changed native
// transform immediately, and give newly initialized copy groups one warm-up retry.
struct BackgroundRejection {uint64_t parent=0,resource=0;uint32_t tick=0;bool stable=false;std::array<uint32_t,16> words{};};
std::unordered_map<uint64_t,BackgroundRejection> background_rejections;
bool live_background(const catherine::root9::BackgroundOwner& p){
    auto found=background_owners.find(p.mesh);
    if(found==background_owners.end()||found->second.identity!=p.identity)return false;
    uint64_t parent=0,resource=0,roster=0,rows=0,table=0,entries[3]{};uint32_t flags=0,count=0;
    return read_bytes(p.mesh+8,&resource,8)&&resource==p.resource&&
        read_bytes(p.mesh+0x20,&flags,4)&&!(flags&1u)&&
        read_bytes(p.mesh+0x1c0,&parent,8)&&parent==p.parent&&
        read_bytes(parent+0x138,&table,8)&&table&&read_bytes(table+8,entries,sizeof entries)&&
        !entries[0]&&!entries[1]&&!entries[2]&&
        read_bytes(parent+0x38,&roster,8)&&roster==p.roster&&
        read_bytes(roster+8,&count,4)&&count==p.count&&
        read_bytes(roster+0x10,&rows,8)&&rows==p.rows;
}
bool background_owner(uint64_t mesh,uint32_t slot,uint64_t destination,catherine::root9::BackgroundOwner& out){
    static const bool enabled=[] {const char* value=std::getenv("CATHERINE_NATIVE_BACKGROUND_INSTANCES");return !(value&&value[0]=='0');}();
    if(!enabled)return false;
    const uint64_t clock=catherine_camera_render_clock();
    if(!(clock&0x80000000ull)||!mesh||slot>=3)return false;
    bool stable_rejection=false;
    if(auto rejected=background_rejections.find(mesh);rejected!=background_rejections.end()){
        uint64_t parent=0,resource=0;std::array<uint32_t,16> words{};
        const bool unchanged=read_bytes(mesh+8,&resource,8)&&read_bytes(mesh+0x1c0,&parent,8)&&
           parent==rejected->second.parent&&resource==rejected->second.resource&&
           read_bytes(destination,words.data(),64)&&words==rejected->second.words;
        if(unchanged){
            if(rejected->second.stable||uint32_t(uint32_t(clock>>32)-rejected->second.tick)<2)return false;
            stable_rejection=true;
        }
        background_rejections.erase(rejected);
    }
    auto old=background_owners.find(mesh);
    if(old!=background_owners.end()&&live_background(old->second)&&
       uint32_t(uint32_t(clock>>32)-uint32_t(old->second.clock>>32))<=2){
        uint64_t storage=0;
        if(!read_bytes(mesh+0x1b8,&storage,8)||!storage||storage>UINT64_MAX-128||destination!=storage+slot*64)return false;
        // The native mesh owns the identity; rotating matrix upload storage does not.
        old->second.storage=storage;old->second.clock=clock;
        out=old->second;return true;
    }
    catherine::root9::BackgroundOwner p{};p.mesh=mesh;p.clock=clock;
    uint32_t flags=0;uint64_t table=0,entry=1;
    if(!read_bytes(mesh+8,&p.resource,8)||!p.resource||
       !read_bytes(mesh+0x20,&flags,4)||(flags&1u)||
       !read_bytes(mesh+0x1c0,&p.parent,8)||!p.parent||
       !read_bytes(mesh+0x1b8,&p.storage,8)||!p.storage||
       p.storage>UINT64_MAX-128||destination!=p.storage+slot*64||
       !read_bytes(p.parent+0x138,&table,8)||!table||
       !read_bytes(table+8+slot*8,&entry,8)||entry||
       !read_bytes(p.parent+0x38,&p.roster,8)||!p.roster||
       !read_bytes(p.roster+8,&p.count,4)||!p.count||p.count>64||
       !read_bytes(p.roster+0x10,&p.rows,8)||!p.rows)return false;
    uint64_t rows[64]{};if(!read_bytes(p.rows,rows,p.count*8))return false;
    unsigned matches=0;for(unsigned k=0;k<p.count;++k)matches+=rows[k]==mesh;
    if(matches!=1)return false;
    // The ambiguous case is repeated geometry at a common origin with
    // independently authored bases. Ordinary translated puzzle blocks keep
    // their existing path. Resource wrappers alone do not identify geometry.
    float own[16]{};uint64_t index=0;uint32_t count=0;
    bool coincident=false;
    if(read_bytes(destination,own,64)&&read_bytes(mesh+0x40,&index,8)&&index&&read_bytes(index+0x58,&count,4)&&count){
        for(unsigned k=0;k<p.count&&!coincident;++k){
            if(rows[k]==mesh)continue;
            uint64_t other_index=0,storage=0;uint32_t other_count=0;
            if(!rows[k]||!read_bytes(rows[k]+0x40,&other_index,8)||!other_index||
               !read_bytes(other_index+0x58,&other_count,4)||other_count!=count||
               !read_bytes(rows[k]+0x1b8,&storage,8)||!storage||storage>UINT64_MAX-128)continue;
            for(unsigned n=0;n<3&&!coincident;++n){float other[16]{};
                if(!read_bytes(storage+n*64,other,64)||std::fabs(other[15]-1)>1e-5f)continue;
                bool same=true,different=false;
                for(unsigned c=0;c<3;++c){
                    same &= std::isfinite(own[12+c])&&std::isfinite(other[12+c])&&std::fabs(own[12+c]-other[12+c])<.001f;
                    for(unsigned r=0;r<3;++r)different |= std::fabs(own[c*4+r]-other[c*4+r])>.01f;
                }
                coincident=same&&different;
            }
        }
    }
    if(!coincident){
        if(background_rejections.size()>=4096)background_rejections.clear();
        BackgroundRejection rejection{p.parent,p.resource,uint32_t(clock>>32),stable_rejection,{}};
        std::memcpy(rejection.words.data(),own,64);background_rejections[mesh]=rejection;return false;
    }
    if(old!=background_owners.end()&&old->second.parent==p.parent&&old->second.resource==p.resource&&
       uint32_t(uint32_t(clock>>32)-uint32_t(old->second.clock>>32))<=2)
        p.identity=old->second.identity;
    else {if(!++background_serial)++background_serial;p.identity=background_serial;}
    if(background_owners.size()>=4096)background_owners.clear();
    background_owners[mesh]=p;out=p;return true;
}
bool live(const catherine::root9::RigidPacket& p){
    return p.background.identity?live_background(p.background):catherine_root9_owner_live(&p.owner)!=0;
}
// Intermediate transfers authenticate exact bytes and copy serials. The final
// recording snapshot performs full live native-object validation before any
// provenance is allowed into a GPU command list.
bool live_transfer(const catherine::root9::RigidPacket& p){
    if(!p.background.identity)return live(p);
    auto i=background_owners.find(p.background.mesh);
    return i!=background_owners.end()&&i->second.identity==p.background.identity;
}
bool active_scope(){
    if(background_seen.load(std::memory_order_acquire)){
        if(catherine_camera_render_clock()&0x80000000ull)return true;
        std::lock_guard lock(bridge_mutex);background_owners.clear();background_rejections.clear();
        background_seen.store(false,std::memory_order_release);transport.clear();
    }
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
Root12Translation& root12_presentation(){static Root12Translation value;return value;}
}
extern "C" void catherine_root9_model_retired(uint64_t model){
    std::lock_guard lock(bridge_mutex);transport.retire(model);
    for(auto i=background_owners.begin();i!=background_owners.end();)
        if(i->second.parent==model||i->second.mesh==model)i=background_owners.erase(i);else ++i;
    if(owned_models.erase(model)&&owned_models.empty()){transport.clear();scope_seen.store(false,std::memory_order_release);}
}
extern "C" void catherine_root9_rigid_copied(uint64_t mesh,uint32_t slot,uint64_t destination){
    const int selected=presence_mode();
    auto published_read=[&](uint64_t address,void*to,size_t size){
        const bool ok=read_bytes(address,to,size);
        // Both copies-map insertion paths invoke this after owner admission and
        // before installing their exact destination key. No invalidation clears bits.
        if(ok&&selected&&address==destination&&size==64)source_presence.publish(destination);
        return ok;
    };
    CatherineBlock20Packet p{};bool owned=catherine_root9_copy_owner(mesh,slot,destination,&p)!=0;
    
    std::lock_guard lock(bridge_mutex);
    if(owned){owned_models.insert(p.model);scope_seen.store(true,std::memory_order_release);}
    catherine::root9::BackgroundOwner background;
    if(!owned&&background_owner(mesh,slot,destination,background)){
        background_seen.store(true,std::memory_order_release);
        transport.copied_background(background,destination,published_read);
    }else transport.copied(owned?&p:nullptr,mesh,destination,published_read);
}
// Read-only census at the completed 64-byte job copy. It never installs or
// invalidates transport evidence, and never touches the recording thread's TLS.
extern "C" void catherine_root9_job_copy_probe(uint64_t source,uint64_t destination){
    static const bool enabled=[] {const char* e=std::getenv("CATHERINE_ROOT9_JOB_CENSUS");return e&&e[0]=='1';}();
    if(!enabled)return;
    std::lock_guard lock(bridge_mutex);
    static uint64_t queries=0,keys=0,live_keys=0,source_exact=0,dest_exact=0,background=0,native=0;
    bool key=false,alive=false;
    catherine::root9::RigidTransport::Thread probe{};
    transport.loaded(probe,source,read_bytes,[&](const catherine::root9::RigidPacket&p){key=true;alive=live_transfer(p);return alive;});
    ++queries;keys+=key;live_keys+=alive;source_exact+=probe.cursor.valid;
    if(probe.cursor.valid){
        std::array<uint32_t,16> actual{};
        if(read_bytes(destination,actual.data(),64)&&actual==probe.cursor.copy.packet.words){
            ++dest_exact;background+=probe.cursor.copy.packet.background.identity!=0;
            native+=probe.cursor.copy.packet.background.identity==0;
        }
    }
    if((queries&(queries-1))==0||queries%10000==0)std::fprintf(stderr,"Root9 job copy census: queries %llu keys %llu live %llu source-exact %llu dest-exact %llu background %llu native %llu\n",(unsigned long long)queries,(unsigned long long)keys,(unsigned long long)live_keys,(unsigned long long)source_exact,(unsigned long long)dest_exact,(unsigned long long)background,(unsigned long long)native);
}
extern "C" void catherine_root9_c3_loaded(uint64_t source){
    if(!active_scope()){leave_local_scope();return;}
    const int selected=presence_mode();const bool possible=!selected||source_presence.may_have(source);
    if(selected==1&&!possible){local.cursor={};local.latest_upload={};local.upload_owned=false;local_dirty=true;return;}
    std::lock_guard lock(bridge_mutex);local_dirty=true;
    if(selected==2){
        bool existed=false;
        transport.loaded(local,source,read_bytes,[&](const catherine::root9::RigidPacket&p){existed=true;return live_transfer(p);});
        const bool now=source_presence.may_have(source);
        if(existed&&!now){std::fprintf(stderr,"Root9 ever-source marking missed an installed key\n");std::abort();}
        ++presence_census.queries;presence_census.negative+=!possible;
        if(!possible){presence_census.stable+=!now;presence_census.concurrent+=now;}
        if(presence_census.queries==1||presence_census.queries%100000==0)std::fprintf(stderr,"Root9 source presence: queries %llu negatives %llu stable %llu publication-overlaps %llu\n",(unsigned long long)presence_census.queries,(unsigned long long)presence_census.negative,(unsigned long long)presence_census.stable,(unsigned long long)presence_census.concurrent);
    }else transport.loaded(local,source,read_bytes,live_transfer);
}
extern "C" void catherine_root9_c3_uploaded(uint64_t destination,uint64_t backing,uint64_t offset){
    if(!active_scope()){leave_local_scope();return;}
    std::lock_guard lock(bridge_mutex);local_dirty=true;transport.uploaded(local,destination,backing,offset,read_bytes,live_transfer);
}
extern "C" void catherine_root9_c3_buffer_address(uint64_t backing,uint64_t base){
    if(!active_scope()){leave_local_scope();return;}
    std::lock_guard lock(bridge_mutex);local_dirty=true;transport.buffer_address(local,backing,base,read_bytes,live_transfer);
}
template<bool Initialized>static int root9_snapshot_impl(uint64_t command,uint64_t gpu,uint64_t size,catherine::root9::RigidPacket* out){
    if(!background_seen.load(std::memory_order_acquire)&&!scope_seen.load(std::memory_order_acquire)&&!recorded_seen.load(std::memory_order_acquire)){if constexpr(!Initialized)*out={};return 0;}
    const bool active=active_scope();std::lock_guard lock(bridge_mutex);
    if constexpr(!Initialized)*out={};
    bool owned=active&&transport.snapshot<!Initialized>(local,gpu,size,*out,read_bytes,live);
    bool next_ever=ever_owned||owned;
    if(!active&&!owned&&(owned_contexts.empty()||(owned_contexts.size()==1&&owned_contexts.contains(command))))next_ever=false;
    const experimental_root9::ForceTuple next{command,next_ever,owned};
    const uint64_t publication=force_snapshot.begin_change(next);
    // Every membership mutation publishes this exact context/value pair while
    // bridge_mutex is held. A repeat has no change to apply to the set.
    const int context_mode=context_memo_mode();
    const bool repeated=command==last_recorded_context&&owned==last_recorded_owned;
    if(context_mode==2&&repeated&&owned_contexts.contains(command)!=owned){std::fprintf(stderr,"Native context memo snapshot mismatch\n");std::abort();}
    if(context_mode!=1||!repeated){if(owned)owned_contexts.insert(command);else owned_contexts.erase(command);}
    if(owned)ever_owned=true;
    last_recorded_context=command;last_recorded_owned=owned;
    if(!active&&owned_contexts.empty())ever_owned=false;
    force_snapshot.commit(publication,next);
    recorded_seen.store(ever_owned,std::memory_order_release);return owned;
}
extern "C" int catherine_root9_c3_snapshot(uint64_t command,uint64_t gpu,uint64_t size,catherine::root9::RigidPacket* out){
    return root9_snapshot_impl<false>(command,gpu,size,out);
}
// Private contract: out is a fresh default-constructed RigidPacket. All context
// bookkeeping is identical to the public ABI, including failed captures.
extern "C" int catherine_root9_c3_snapshot_preinitialized(uint64_t command,uint64_t gpu,uint64_t size,catherine::root9::RigidPacket* out){
    return root9_snapshot_impl<true>(command,gpu,size,out);
}
extern "C" int catherine_root9_c3_force_bind(void){
    if(!recorded_seen.load(std::memory_order_acquire))return (background_seen.load(std::memory_order_acquire)||scope_seen.load(std::memory_order_acquire))&&local.upload_owned;
    auto read_command=[] {
        uint64_t engine=0,command=0;
        const bool known=read_bytes(0x7000cc9498ull,&engine,8)&&engine&&engine<=UINT64_MAX-0x5ca0&&read_bytes(engine+0x5ca0,&command,8)&&command;
        return experimental_root9::ForceCommand{command,known};
    };
    const int fast_mode=force_snapshot_mode(),context_mode=context_memo_mode();
    if(fast_mode==1&&context_mode==1){
        experimental_root9::ForceObservation observed;
        if(force_snapshot.observe(observed,read_command))return experimental_root9::force_result(observed,local.upload_owned);
    }
    std::lock_guard lock(bridge_mutex);
    if(!ever_owned)return (background_seen.load(std::memory_order_acquire)||scope_seen.load(std::memory_order_acquire))&&local.upload_owned;
    const auto input=read_command();const uint64_t command=input.command;const bool known=input.known;
    if(fast_mode==2){
        experimental_root9::ForceObservation observed;
        const bool accepted=force_snapshot.observe(observed,[&]{return input;});
        static thread_local uint64_t queries=0,accepted_queries=0;++queries;accepted_queries+=accepted;
        if(queries==1||queries%100000==0)std::fprintf(stderr,"Native force snapshot: checked %llu accepted %llu\n",(unsigned long long)queries,(unsigned long long)accepted_queries);
        if(accepted){
            const experimental_root9::ForceTuple actual{last_recorded_context,ever_owned,last_recorded_owned};
            const bool original=local.upload_owned||!known||owned_contexts.contains(command)||(ever_owned&&command!=last_recorded_context);
            if(!(observed.tuple==actual)||experimental_root9::force_result(observed,local.upload_owned)!=original){std::fprintf(stderr,"Native force snapshot mismatch\n");std::abort();}
        }
    }
    if(!known)return local.upload_owned||ever_owned;
    const bool memo_result=local.upload_owned||command!=last_recorded_context||last_recorded_owned;
    if(context_mode!=1){
        const bool original=local.upload_owned||owned_contexts.contains(command)||(ever_owned&&command!=last_recorded_context);
        if(context_mode==2&&original!=memo_result){std::fprintf(stderr,"Native context memo force mismatch\n");std::abort();}
        return original;
    }
    return memo_result;
}
