#include "block20_owner_bridge.h"
#include "root15_palette_transport.h"
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <unordered_set>
#include <cstring>
#include <cmath>
extern "C" uint64_t catherine_camera_render_clock(void);
extern "C" int catherine_high_refresh_active(void);
namespace {
template<class T>T read(uint64_t address){T value;std::memcpy(&value,reinterpret_cast<const void*>(address),sizeof value);return value;}
struct Owner{uint64_t block,generation,lifetime,model,parent,table,node;uint32_t type;};
std::mutex owner_mutex;std::unordered_map<uint64_t,Owner> owners;std::unordered_map<uint64_t,std::unordered_set<uint64_t>> entries;std::unordered_set<uint64_t> retired;uint64_t serial=0;
std::atomic<uint32_t> root9_owners{0};
thread_local CatherineBlock20Packet job{};
thread_local CatherineBlock15Packet job15{};catherine::root15::PaletteTransport palette15;
void erase_owner(uint64_t block){auto i=owners.find(block);if(i==owners.end())return;if(i->second.type==9)root9_owners.fetch_sub(1,std::memory_order_relaxed);for(unsigned slot=0;slot<3;++slot){palette15.invalidate_entry(i->second.table+8+8*slot);auto e=entries.find(i->second.table+8+8*slot);if(e!=entries.end()){e->second.erase(block);if(e->second.empty())entries.erase(e);}}owners.erase(i);}

bool exact_name(uint64_t model,uint32_t type){const char* name=type==9?"puzzle/object/c001_010.GMD":type==12?"puzzle/object/c001_013.GMD":"puzzle/object/c001_004.GMD";char actual[sizeof "puzzle/object/c001_004.GMD"];std::memcpy(actual,reinterpret_cast<const void*>(model+0x1c),sizeof actual);return !std::memcmp(actual,name,sizeof actual-1)&&(actual[sizeof actual-1]==0||actual[sizeof actual-1]==' '||actual[sizeof actual-1]=='(');}
}
extern "C" void catherine_block20_owner_forget(uint64_t block){if(!block)return;std::lock_guard lock(owner_mutex);erase_owner(block);retired.insert(block);}
static void sync_locked(uint64_t block){
 if(!block||!catherine_high_refresh_active()||retired.contains(block))return;
 uint32_t type=read<uint32_t>(block+0xe0);uint64_t model=read<uint64_t>(block+0x20);if((type==9||type==12)&&(read<uint8_t>(0x7000d06360ull)&1u)){erase_owner(block);return;}
 if((type!=2&&type!=3&&type!=9&&type!=12)||!model||!exact_name(model,type)){erase_owner(block);return;}
 uint64_t parent=read<uint64_t>(model+0x468),table=parent?read<uint64_t>(parent+0x138):0,node=read<uint64_t>(model+0x4a8),generation=read<uint32_t>(block+0x174);
 if(!parent||!table||read<uint32_t>(table)!=((type==9||type==12)?15u:20u)||!node){erase_owner(block);return;}
 auto found=owners.find(block);if(found!=owners.end()){const Owner&o=found->second;if(o.generation==generation&&o.model==model&&o.parent==parent&&o.table==table&&o.node==node&&(o.type==type||((o.type==2||o.type==3)&&(type==2||type==3))))return;erase_owner(block);}
 // Shared descriptor aliases are retained as a set and never resolve to an arbitrary owner.
 if(owners.size()>=2048)return;
 uint64_t lifetime=++serial;if(!lifetime)lifetime=++serial;owners.emplace(block,Owner{block,generation,lifetime,model,parent,table,node,type});if(type==9)root9_owners.fetch_add(1,std::memory_order_relaxed);for(unsigned s=0;s<3;++s)entries[table+8+8*s].insert(block);
}
extern "C" void catherine_block20_owner_sync(uint64_t block){std::lock_guard lock(owner_mutex);sync_locked(block);}
extern "C" void catherine_block20_owner_constructed(uint64_t block){if(!block)return;std::lock_guard lock(owner_mutex);erase_owner(block);retired.erase(block);sync_locked(block);}
extern "C" void catherine_block20_job_leave(void){job={};job15={};}
extern "C" int catherine_block20_job_snapshot(CatherineBlock20Packet*out){*out=job;return job.valid!=0;}

extern "C" void catherine_block20_job_enter(uint64_t row,uint64_t palette_address){
 job={};job15={};if(!row||!catherine_high_refresh_active())return;std::lock_guard lock(owner_mutex);
 if(read<uint64_t>(row)!=0x70002fb6b0ull)return;uint64_t bytes=read<uint64_t>(row+0x28);uint32_t count=read<uint32_t>(row+0x10);if(read<uint64_t>(row)!=0x70002fb6b0ull||!((bytes==1280&&(count==1457||count==393))||(bytes==960&&(count==25||count==298||count==273||count==79||count==64))))return;uint64_t entry=read<uint64_t>(row+0x20);auto e=entries.find(entry);if(e==entries.end()||e->second.size()!=1)return;auto i=owners.find(*e->second.begin());if(i==owners.end())return;const Owner&o=i->second;
 uint32_t type=read<uint32_t>(o.block+0xe0);if((type==9||type==12)&&(read<uint8_t>(0x7000d06360ull)&1u)){erase_owner(o.block);return;}if(type==9?(bytes!=960||count!=25):type==12?(bytes!=960||count==25):bytes!=1280)return;
 if(read<uint32_t>(o.block+0x174)!=o.generation||read<uint64_t>(o.block+0x20)!=o.model||read<uint64_t>(o.model+0x468)!=o.parent||read<uint64_t>(o.parent+0x138)!=o.table||read<uint64_t>(o.model+0x4a8)!=o.node||read<uint32_t>(o.table)!=((type==9||type==12)?15u:20u))return;
 if(type==9||type==12){
  const uint64_t engine=read<uint64_t>(0x7000cc9498ull);if(!engine)return;
  const uint64_t mapped=read<uint64_t>(engine+0x6210);if(!mapped)return;
  const uint64_t cpu_base=read<uint64_t>(mapped),offset=read<uint64_t>(entry);
  if(!cpu_base||offset>UINT64_MAX-cpu_base)return;
  auto live=[&](const CatherineBlock20Packet& p){return p.type==type&&o.type==type&&p.block==o.block&&p.generation==o.generation&&p.lifetime==o.lifetime&&p.model==o.model&&p.parent==o.parent&&p.table==o.table&&p.node==o.node;};
  if(!palette15.job(entry,offset,palette_address,cpu_base+offset,catherine_camera_render_clock(),job15,live))return;
  job=job15.owner;return;
 }
 CatherineBlock20Packet p{};p.block=o.block;p.generation=o.generation;p.lifetime=o.lifetime;p.model=o.model;p.parent=o.parent;p.table=o.table;p.entry=entry;p.entry_value=read<uint64_t>(entry);p.palette_address=palette_address;p.node=o.node;p.clock=catherine_camera_render_clock();p.type=type;p.subtype=read<uint32_t>(o.block+0xe4);p.variant=read<uint32_t>(o.block+0xe8);p.flags=read<uint32_t>(o.block+0xec);if(p.type!=2&&p.type!=3&&p.type!=9&&p.type!=12)return;
 std::memcpy(p.world,reinterpret_cast<const void*>(o.node+0x20),64);for(float f:p.world)if(!std::isfinite(f))return;p.valid=1;job=p;
}

extern "C" int catherine_block15_job_snapshot(CatherineBlock15Packet* out){*out=job15;return job15.owner.valid!=0;}
extern "C" void catherine_block15_palette_copied(uint64_t parent,uint32_t slot,uint64_t destination,uint32_t count){
 std::lock_guard lock(owner_mutex);palette15.invalidate_cpu(destination,uint64_t(count)*64);
 if(!parent||slot>=3)return;
 uint64_t table=read<uint64_t>(parent+0x138);if(!table)return;uint64_t entry=table+8+8*slot;
 palette15.invalidate_entry(entry);
 if(count!=15||!destination||!catherine_high_refresh_active()||(read<uint8_t>(0x7000d06360ull)&1u))return;
 auto e=entries.find(entry);if(e==entries.end()||e->second.size()!=1)return;
 auto found=owners.find(*e->second.begin());if(found==owners.end())return;const Owner& o=found->second;
 if(o.parent!=parent||o.table!=table||(read<uint32_t>(o.block+0xe0)!=9&&read<uint32_t>(o.block+0xe0)!=12)||
    read<uint32_t>(o.block+0x174)!=o.generation||read<uint64_t>(o.block+0x20)!=o.model||
    read<uint64_t>(o.model+0x468)!=parent||read<uint64_t>(o.model+0x4a8)!=o.node||read<uint32_t>(table)!=15||!exact_name(o.model,read<uint32_t>(o.block+0xe0)))return;
 CatherineBlock20Packet p{};p.valid=1;p.block=o.block;p.generation=o.generation;p.lifetime=o.lifetime;p.model=o.model;
 p.parent=o.parent;p.table=o.table;p.entry=entry;p.entry_value=read<uint64_t>(entry);p.node=o.node;p.clock=catherine_camera_render_clock();p.type=read<uint32_t>(o.block+0xe0);
 p.subtype=read<uint32_t>(o.block+0xe4);p.variant=read<uint32_t>(o.block+0xe8);p.flags=read<uint32_t>(o.block+0xec);
 if(!(p.clock&0x80000000u))return;std::memcpy(p.world,reinterpret_cast<const void*>(o.node+0x20),64);for(float f:p.world)if(!std::isfinite(f))return;
 // The selected native copy has stored this exact CPU offset in this slot.
 const uint64_t engine=read<uint64_t>(0x7000cc9498ull);if(!engine)return;
 const uint64_t mapped=read<uint64_t>(engine+0x6210);if(!mapped)return;const uint64_t cpu_base=read<uint64_t>(mapped);
 if(!cpu_base||destination<cpu_base||destination-cpu_base!=p.entry_value)return;
 auto read_bytes=[](uint64_t address,void* output,size_t bytes){if(!address)return false;std::memcpy(output,reinterpret_cast<const void*>(address),bytes);return true;};
 palette15.copied(&p,entry,destination,read_bytes);
}

extern "C" int catherine_root9_has_owners(void){return root9_owners.load(std::memory_order_relaxed)!=0;}
extern "C" int catherine_root9_owner_live(const CatherineBlock20Packet* p){
 if(!p||!p->valid||p->type!=9||!p->lifetime||!catherine_high_refresh_active())return 0;
 std::lock_guard lock(owner_mutex);auto i=owners.find(p->block);if(i==owners.end())return 0;const Owner& o=i->second;
 if(p->generation!=o.generation||p->lifetime!=o.lifetime||p->model!=o.model||p->parent!=o.parent||p->table!=o.table||p->node!=o.node||o.type!=9)return 0;
 if(read<uint32_t>(o.block+0xe0)!=9||read<uint32_t>(o.block+0x174)!=o.generation||read<uint64_t>(o.block+0x20)!=o.model)return 0;
 const uint64_t clock=catherine_camera_render_clock();const uint32_t produced=uint32_t(p->clock>>32),now=uint32_t(clock>>32);
 if(read<uint8_t>(0x7000d06360ull)&1u){erase_owner(p->block);return 0;}
 if(!(p->clock&0x80000000u)||!(clock&0x80000000u)||now<produced||now-produced>2)return 0;
 if(p->entry<o.table+8||p->entry>o.table+24||(p->entry-o.table-8)%8)return 0;
 auto e=entries.find(p->entry);if(e==entries.end()||e->second.size()!=1||!e->second.contains(p->block))return 0;
 return exact_name(o.model,9)&&
    read<uint64_t>(o.model+0x468)==o.parent&&read<uint64_t>(o.model+0x4a8)==o.node&&read<uint64_t>(o.parent+0x138)==o.table&&read<uint32_t>(o.table)==15;
}
extern "C" int catherine_root9_copy_owner(uint64_t mesh,uint32_t slot,uint64_t destination,CatherineBlock20Packet* out){
 *out={};if(!root9_owners.load(std::memory_order_relaxed)||!mesh||slot>=3||!destination||!catherine_high_refresh_active()||(read<uint8_t>(0x7000d06360ull)&1u))return 0;
 std::lock_guard lock(owner_mutex);
 if((read<uint32_t>(mesh+0x20)&1u)||!read<uint64_t>(mesh+8))return 0;
 const uint64_t parent=read<uint64_t>(mesh+0x1c0),base=read<uint64_t>(mesh+0x1b8);
 if(!parent||!base||base>UINT64_MAX-slot*64||destination!=base+slot*64)return 0;
 const uint64_t table=read<uint64_t>(parent+0x138);if(!table)return 0;
 auto e=entries.find(table+8+slot*8);if(e==entries.end()||e->second.size()!=1)return 0;
 auto found=owners.find(*e->second.begin());if(found==owners.end())return 0;const Owner& o=found->second;
 if(o.parent!=parent||o.table!=table||read<uint32_t>(o.block+0xe0)!=9||
    read<uint32_t>(o.block+0x174)!=o.generation||read<uint64_t>(o.block+0x20)!=o.model||read<uint64_t>(o.model+0x468)!=parent||
    read<uint64_t>(o.model+0x4a8)!=o.node||read<uint32_t>(table)!=15||!exact_name(o.model,9))return 0;
 // Native parent roster proves real mesh membership, not just a forged backpointer.
 const uint64_t list=read<uint64_t>(parent+0x38);if(!list)return 0;
 const uint32_t count=read<uint32_t>(list+8);const uint64_t rows=read<uint64_t>(list+0x10);if(!rows||!count||count>64)return 0;
 unsigned matches=0;for(unsigned k=0;k<count;++k)matches+=read<uint64_t>(rows+8*k)==mesh;if(matches!=1)return 0;
 CatherineBlock20Packet p{};p.valid=1;p.block=o.block;p.generation=o.generation;p.lifetime=o.lifetime;p.model=o.model;p.parent=o.parent;p.table=o.table;
 p.entry=table+8+slot*8;p.entry_value=read<uint64_t>(p.entry);p.node=o.node;p.clock=catherine_camera_render_clock();p.type=9;
 p.subtype=read<uint32_t>(o.block+0xe4);p.variant=read<uint32_t>(o.block+0xe8);p.flags=read<uint32_t>(o.block+0xec);
 if(!(p.clock&0x80000000u))return 0;std::memcpy(p.world,reinterpret_cast<const void*>(o.node+0x20),64);for(float f:p.world)if(!std::isfinite(f))return 0;
 *out=p;return 1;
}
