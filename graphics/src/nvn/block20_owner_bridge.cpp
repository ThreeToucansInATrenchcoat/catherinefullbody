#include "block20_owner_bridge.h"
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cmath>
extern "C" uint64_t catherine_camera_render_clock(void);
extern "C" int catherine_high_refresh_active(void);
namespace {
template<class T>T read(uint64_t address){T value;std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof value);return value;}
struct Owner{uint64_t block,generation,lifetime,model,parent,table,node;};
std::mutex owner_mutex;std::unordered_map<uint64_t,Owner> owners;std::unordered_map<uint64_t,std::unordered_set<uint64_t>> entries;std::unordered_set<uint64_t> retired;uint64_t serial=0;
thread_local CatherineBlock20Packet job{};
void erase_owner(uint64_t block){auto i=owners.find(block);if(i==owners.end())return;for(unsigned slot=0;slot<3;++slot){auto e=entries.find(i->second.table+8+8*slot);if(e!=entries.end()){e->second.erase(block);if(e->second.empty())entries.erase(e);}}owners.erase(i);}

bool exact_name(uint64_t model){constexpr char name[]="puzzle/object/c001_004.GMD";char actual[sizeof name];std::memcpy(actual,reinterpret_cast<const void*>(model+0x1c),sizeof actual);return !std::memcmp(actual,name,sizeof name-1)&&(actual[sizeof name-1]==0||actual[sizeof name-1]==' '||actual[sizeof name-1]=='(');}
}
extern "C" void catherine_block20_owner_forget(uint64_t block){if(!block)return;std::lock_guard lock(owner_mutex);erase_owner(block);retired.insert(block);}
static void sync_locked(uint64_t block){
 if(!block||!catherine_high_refresh_active()||retired.contains(block))return;
 uint32_t type=read<uint32_t>(block+0xe0);uint64_t model=read<uint64_t>(block+0x20);
 if((type!=2&&type!=3)||!model||!exact_name(model)){erase_owner(block);return;}
 uint64_t parent=read<uint64_t>(model+0x468),table=parent?read<uint64_t>(parent+0x138):0,node=read<uint64_t>(model+0x4a8),generation=read<uint32_t>(block+0x174);
 if(!parent||!table||read<uint32_t>(table)!=20||!node){erase_owner(block);return;}
 auto found=owners.find(block);if(found!=owners.end()){const Owner&o=found->second;if(o.generation==generation&&o.model==model&&o.parent==parent&&o.table==table&&o.node==node)return;erase_owner(block);}
 // Shared descriptor aliases are retained as a set and never resolve to an arbitrary owner.
 if(owners.size()>=2048)return;
 uint64_t lifetime=++serial;if(!lifetime)lifetime=++serial;owners.emplace(block,Owner{block,generation,lifetime,model,parent,table,node});for(unsigned s=0;s<3;++s)entries[table+8+8*s].insert(block);
}
extern "C" void catherine_block20_owner_sync(uint64_t block){std::lock_guard lock(owner_mutex);sync_locked(block);}
extern "C" void catherine_block20_owner_constructed(uint64_t block){if(!block)return;std::lock_guard lock(owner_mutex);erase_owner(block);retired.erase(block);sync_locked(block);}
extern "C" void catherine_block20_job_leave(void){job={};}
extern "C" int catherine_block20_job_snapshot(CatherineBlock20Packet*out){*out=job;return job.valid!=0;}
extern "C" void catherine_block20_job_enter(uint64_t row,uint64_t palette_address){
 job={};if(!row||!catherine_high_refresh_active())return;std::lock_guard lock(owner_mutex);
 if(read<uint64_t>(row)!=0x70002fb6b0ull||read<uint64_t>(row+0x28)!=1280)return;uint32_t count=read<uint32_t>(row+0x10);if(count!=1457&&count!=393)return;uint64_t entry=read<uint64_t>(row+0x20);auto e=entries.find(entry);if(e==entries.end()||e->second.size()!=1)return;auto i=owners.find(*e->second.begin());if(i==owners.end())return;const Owner&o=i->second;
 if(read<uint32_t>(o.block+0x174)!=o.generation||read<uint64_t>(o.block+0x20)!=o.model||read<uint64_t>(o.model+0x468)!=o.parent||read<uint64_t>(o.parent+0x138)!=o.table||read<uint64_t>(o.model+0x4a8)!=o.node||read<uint32_t>(o.table)!=20)return;
 CatherineBlock20Packet p{};p.block=o.block;p.generation=o.generation;p.lifetime=o.lifetime;p.model=o.model;p.parent=o.parent;p.table=o.table;p.entry=entry;p.entry_value=read<uint64_t>(entry);p.palette_address=palette_address;p.node=o.node;p.clock=catherine_camera_render_clock();p.type=read<uint32_t>(o.block+0xe0);p.subtype=read<uint32_t>(o.block+0xe4);p.variant=read<uint32_t>(o.block+0xe8);p.flags=read<uint32_t>(o.block+0xec);if(p.type!=2&&p.type!=3)return;
 std::memcpy(p.world,reinterpret_cast<const void*>(o.node+0x20),64);for(float f:p.world)if(!std::isfinite(f))return;p.valid=1;job=p;
}
