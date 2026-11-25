#include <cstdint>
namespace babel_bridge {void c3_loaded(uint64_t);void c3_uploaded(uint64_t,uint64_t,uint64_t);void buffer_address(uint64_t,uint64_t);bool record_owned(uint64_t,uint64_t);bool force_bind();}
#include "c02_movement_bridge.h"
#include "movement_translation.h"
#include "facing_interpolation.h"
#include <algorithm>
#include <atomic>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#include <immintrin.h>
#ifndef C02_BRIDGE_FIXTURE
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#include "../../../../platform/host_clock.h"
#include "c02_guarded_io_v5.h"
#endif
extern "C" uint64_t catherine_camera_render_clock(void);
extern "C" uint64_t catherine_animation_render_clock(void);
extern "C" int catherine_high_refresh_active(void);

namespace c02_bridge {
using namespace catherine::visual::movement;
struct MxcsrGuard {unsigned original=_mm_getcsr();~MxcsrGuard(){_mm_setcsr(original);}MxcsrGuard()=default;MxcsrGuard(const MxcsrGuard&)=delete;};
constexpr uint64_t holder_global=0x7000ccadf0ull, p1_vtable=0x7000abff90ull, model_vtable=0x70009f5a10ull;
constexpr size_t record_limit=65536, unresolved_limit=4096;
bool strict(const char* s){return s&&s[0]=='1'&&s[1]==0;}
bool enabled(){
#ifdef C02_BRIDGE_FIXTURE
 extern bool fixture_enabled;return fixture_enabled;
#else
 static const bool value=[] {const char* override_value=std::getenv("CATHERINE_C02_TRANSLATION");return override_value?strict(override_value):true;}();return value;
#endif
}
bool turns_enabled(){
#ifdef C02_BRIDGE_FIXTURE
 return true;
#else
 static const bool value=[] {const char* s=std::getenv("CATHERINE_P1_FACING");return !s||strict(s);}();return value;
#endif
}
bool qa_enabled(){
#ifdef C02_BRIDGE_FIXTURE
 return false;
#else
 static const bool value=strict(std::getenv("CATHERINE_C02_TRANSLATION_QA"));
 if(!value)return false;
 static bool latched=false;static uint64_t next_poll=0;
 if(latched)return true;
 const uint64_t now=catherine_host_milliseconds();if(now<next_poll)return false;next_poll=now+250;
#ifdef _WIN32
 const DWORD attributes=GetFileAttributesA("c02-capture.marker");
 latched=attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&FILE_ATTRIBUTE_DIRECTORY);
#else
 struct stat result{};
 latched=stat("c02-capture.marker",&result)==0&&!S_ISDIR(result.st_mode);
#endif
 return latched;
#endif
}
bool read_bytes(uint64_t p,void* out,size_t n){
#ifdef C02_BRIDGE_FIXTURE
 extern bool fixture_read(uint64_t,void*,size_t);return fixture_read(p,out,n);
#else
 return direct::read(p,out,n);
#endif
}
bool write_bytes(uint64_t p,const void* input,size_t n){
#ifdef C02_BRIDGE_FIXTURE
 extern bool fixture_write(uint64_t,const void*,size_t);return fixture_write(p,input,n);
#else
 return direct::write(p,input,n);
#endif
}
template<class T>bool read(uint64_t p,T& out){return read_bytes(p,&out,sizeof out);}
struct Owner {
 uint64_t holder=0,actor=0,model=0,model_vtable=0,parent=0,table=0,node=0;
 uint32_t ids[4]{};uint32_t matrix_count=0;
 bool operator==(const Owner&)const=default;
};
bool family_matches(const uint32_t* ids,uint32_t count){return ids[0]==2&&ids[2]==1&&ids[3]==0&&((ids[1]==10&&count==89)||(ids[1]==11&&count==74));}
bool sample_owner(Owner& o){
 uint64_t v=0;
 if(!read(holder_global,o.holder)||!o.holder||!read(o.holder,o.actor)||!o.actor||
    !read(o.actor,v)||v!=p1_vtable||!read(o.actor+0x20,o.model)||!o.model||
    !read(o.model,o.model_vtable)||o.model_vtable!=model_vtable||!read_bytes(o.model+0x5a0,o.ids,sizeof o.ids)||
    o.ids[0]!=2||(o.ids[1]!=10&&o.ids[1]!=11)||o.ids[2]!=1||o.ids[3]!=0||
    !read(o.model+0x468,o.parent)||!o.parent||!read(o.parent+0x138,o.table)||!o.table||
    !read(o.table,o.matrix_count)||!family_matches(o.ids,o.matrix_count)||!read(o.model+0x4a8,o.node)||!o.node)return false;
 Owner again;uint64_t h=0;
 if(!read(holder_global,h)||h!=o.holder||!read(h,again.actor)||again.actor!=o.actor||
    !read(again.actor,v)||v!=p1_vtable||!read(again.actor+0x20,again.model)||again.model!=o.model||
    !read(again.model,again.model_vtable)||again.model_vtable!=o.model_vtable||
    !read_bytes(again.model+0x5a0,again.ids,sizeof again.ids)||std::memcmp(again.ids,o.ids,sizeof o.ids)||
    !read(again.model+0x468,again.parent)||again.parent!=o.parent||
    !read(again.parent+0x138,again.table)||again.table!=o.table||
    !read(again.table,again.matrix_count)||again.matrix_count!=o.matrix_count||!read(again.model+0x4a8,again.node)||again.node!=o.node)return false;
 return true;
}
bool high_clock(uint64_t clock){Clock c,puzzle;uint32_t legacy=0;return catherine_high_refresh_active()&&decode_clock(clock,c)&&c.enabled&&
 decode_clock(catherine_animation_render_clock(),puzzle)&&puzzle.enabled&&read(0x7000d06360ull,legacy)&&!(legacy&1);}
bool equal_position(const Position&a,const Position&b){return a==b;}
struct Version {History history{};Position endpoint{};uint64_t clock=0,serial=0;bool valid=false;};
struct Decision {Translation translation{};Position represented{};uint64_t clock=0,entry=0,version=0;bool valid=false;catherine::visual::facing::Correction facing{};};
struct Active {
 Owner owner{};uint64_t lifetime=0,changed_clock=0;bool valid=false,suppressed=false;
 History history{};catherine::visual::facing::History facing{};Position current{};std::array<Version,4> versions{};std::array<Decision,3> decisions{};
};
struct RigidRecord {uint64_t lifetime=0,serial=0,clock=0;std::array<uint32_t,16> words{};};
struct Upload {uint64_t cpu=0,backing=0,offset=0,serial=0,lifetime=0;std::array<uint32_t,16> words{};bool owned=false;};
struct GpuTag:Upload {uint64_t gpu=0,base=0;};
struct Counters {
 uint64_t published=0,coalesced=0,resets=0,owner_reject=0,represented_reject=0,suppressed=0;
 uint64_t palette=0,palette_owned=0,rigid=0,rigid_owned=0,basis_unchanged=0,cache_hit=0,cache_miss=0,delta_mismatch=0;
 uint64_t loaded=0,loaded_owned=0,uploaded=0,uploaded_owned=0,resolved=0,recorded=0,recorded_owned=0,overlap_erased=0;
} census;
std::mutex mutex;Active active;uint64_t serial=0,retired_model=0;std::atomic<bool> last_recorded_owned{false};std::atomic<bool> scope_seen{false};
std::map<uint64_t,RigidRecord> rigid_records;
std::map<uint64_t,std::vector<Upload>> unresolved;
std::map<uint64_t,GpuTag> gpu_tags;
thread_local struct Pending {Owner owner{};Position old{},input{};uint64_t lifetime=0,clock=0;bool ready=false;} pending;
thread_local struct C3 {RigidRecord record{};bool valid=false;} c3;
thread_local bool current_upload_owned=false;
thread_local struct Address {uint64_t backing=0,base=0;bool valid=false;} address;
uint64_t next_serial(){if(!++serial)++serial;return serial;}
FILE* qa_file=nullptr;uint64_t qa_rows=0;bool qa_attempted=false;
void qa(const char* event,uint64_t clock,const Owner& owner,uint64_t life,uint32_t slot,uint64_t entry,uint64_t destination,
        const Position& position={},const Position& delta={},uint64_t flags=0){
 if(!qa_enabled()||qa_rows>=100000)return;
 if(!qa_attempted){qa_attempted=true;qa_file=std::fopen("c02-translation-qa.csv","wb");if(qa_file)std::fprintf(qa_file,"sequence,event,clock,lifetime,actor,model,parent,slot,table_entry_value,destination,position_x,position_y,position_z,delta_x,delta_y,delta_z,flags\n");}
 if(!qa_file)return;
 std::fprintf(qa_file,"%llu,%s,%llu,%llu,%llu,%llu,%llu,%u,%llu,%llu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%llu\n",
   (unsigned long long)++qa_rows,event,(unsigned long long)clock,(unsigned long long)life,(unsigned long long)owner.actor,
   (unsigned long long)owner.model,(unsigned long long)owner.parent,slot,(unsigned long long)entry,(unsigned long long)destination,
   position[0],position[1],position[2],delta[0],delta[1],delta[2],(unsigned long long)flags);
 std::fflush(qa_file);
}
void log_counts(){
 const auto n=census.palette+census.rigid+census.recorded;
 if(!qa_enabled()||!(n==1||n%4096==0))return;
 std::fprintf(stderr,"[c02-translation] published=%llu coalesced=%llu resets=%llu owner_reject=%llu represented_reject=%llu suppressed=%llu palette=%llu palette_owned=%llu rigid=%llu rigid_owned=%llu basis_unchanged=%llu cache_hit=%llu cache_miss=%llu delta_mismatch=%llu loaded=%llu loaded_owned=%llu uploaded=%llu uploaded_owned=%llu resolved=%llu recorded=%llu recorded_owned=%llu overlap_erased=%llu\n",
 (unsigned long long)census.published,(unsigned long long)census.coalesced,(unsigned long long)census.resets,(unsigned long long)census.owner_reject,
 (unsigned long long)census.represented_reject,(unsigned long long)census.suppressed,(unsigned long long)census.palette,(unsigned long long)census.palette_owned,
 (unsigned long long)census.rigid,(unsigned long long)census.rigid_owned,(unsigned long long)census.basis_unchanged,(unsigned long long)census.cache_hit,
 (unsigned long long)census.cache_miss,(unsigned long long)census.delta_mismatch,(unsigned long long)census.loaded,(unsigned long long)census.loaded_owned,
 (unsigned long long)census.uploaded,(unsigned long long)census.uploaded_owned,(unsigned long long)census.resolved,(unsigned long long)census.recorded,
 (unsigned long long)census.recorded_owned,(unsigned long long)census.overlap_erased);std::fflush(stderr);
}
void clear_current(){active=Active{};pending={};rigid_records.clear();unresolved.clear();gpu_tags.clear();c3={};address={};current_upload_owned=false;++census.resets;}
// Cheap global scope rejection before locks/owner joins. Scope exit clears
// admission once; it never suppresses the one owned->unowned bind transition.
bool enter_scope(uint64_t clock){
 if(catherine_high_refresh_active()&&(uint32_t(clock)&0x80000000u)&&
    (uint32_t(catherine_animation_render_clock())&0x80000000u)){
   scope_seen.store(true,std::memory_order_relaxed);return true;
 }
 if(scope_seen.exchange(false,std::memory_order_relaxed)){std::lock_guard lock(mutex);clear_current();}
 current_upload_owned=false;return false;
}
bool sync(const Owner& owner,uint64_t clock,bool setter){
 if(retired_model==owner.model&&!setter)return false;
 if(setter&&retired_model==owner.model)retired_model=0;
 if(active.valid&&active.owner==owner)return true;
 clear_current();active.owner=owner;active.lifetime=next_serial();active.valid=true;
 Position native;if(!read_bytes(owner.model+0x5d0,native.data(),12)){clear_current();return false;}
 if(active.history.publish(active.lifetime,native,native,clock)==Publication::invalid){clear_current();return false;}
 active.current=native;active.versions[0]={active.history,native,clock,next_serial(),true};
 float initial_quaternion[4];double initial_yaw;
 if(read_bytes(owner.model+0x5f0,initial_quaternion,16)&&catherine::visual::facing::quaternion_yaw(initial_quaternion,initial_yaw))
   active.facing.publish(active.lifetime,initial_yaw,initial_yaw,clock);
 ++census.resets;return true;
}
void version(uint64_t clock){
 for(size_t i=active.versions.size()-1;i;--i)active.versions[i]=active.versions[i-1];
 active.versions[0]={active.history,active.current,clock,next_serial(),true};
}
template<class Map>void erase_span(Map& map,uint64_t start,uint64_t bytes){
 if(!start||bytes>UINT64_MAX-start){map.clear();return;}
 const uint64_t end=start+bytes;auto it=map.lower_bound(start);
 if(it!=map.begin()){auto prev=std::prev(it);if(prev->first>UINT64_MAX-64||prev->first+64>start)it=prev;}
 while(it!=map.end()&&it->first<end){it=map.erase(it);++census.overlap_erased;}
}
bool decision(const Owner& owner,uint32_t slot,uint64_t clock,Decision& out){
 if(slot>=3||!sync(owner,clock,false))return false;
 uint64_t entry=0;std::array<float,16> world{};Position native{};
 if(!read(owner.table+8+slot*8,entry)||!read_bytes(owner.node+0x20,world.data(),64)||
    !read_bytes(owner.model+0x5d0,native.data(),12)||native!=active.current)return false;
 Position represented{world[12],world[13],world[14]};
 const Version* selected=nullptr;
 bool settled=false;
 for(const auto& v:active.versions)if(v.valid&&equal_position(v.endpoint,represented)){
   double age;if(elapsed_ticks(v.clock,clock,2.0,age)){selected=&v;break;}
   // An exactly current, unchanged native endpoint needs no historical motion
   // sample. Preserve its per-render animation and mark rigid provenance only.
   if(represented==native&&v.endpoint==active.current){selected=&v;settled=true;break;}
 }
 if(!selected){++census.represented_reject;active.suppressed=true;++census.suppressed;
   qa("represented_reject",clock,owner,active.lifetime,slot,entry,0,represented,{},active.changed_clock);return false;}
 if(active.suppressed)return false;
 auto& cached=active.decisions[slot];
 if(cached.valid&&cached.clock==clock&&cached.entry==entry){
   if(cached.version==selected->serial&&equal_position(cached.represented,represented)){++census.cache_hit;out=cached;return true;}
   ++census.delta_mismatch;active.suppressed=true;++census.suppressed;return false;
 }
 ++census.cache_miss;out={settled?Translation{active.lifetime,{},represented,false}:selected->history.sample(active.lifetime,represented,clock,true),represented,clock,entry,selected->serial,true};
 if(turns_enabled()){
   if(std::abs(world[1])<1e-4&&std::abs(world[4])<1e-4&&std::abs(world[6])<1e-4&&std::abs(world[9])<1e-4)
     out.facing=active.facing.sample(active.lifetime,std::atan2(world[2],world[0]),clock,represented,true);
 }
 cached=out;return true;
}
bool current_owner(Owner& owner,uint64_t clock){
 if(!enabled()||!high_clock(clock)){clear_current();++census.owner_reject;return false;}
 if(!sample_owner(owner)){++census.owner_reject;return false;}return true;
}
bool current_lifetime(uint64_t lifetime){
 Owner owner;return active.valid&&active.lifetime==lifetime&&high_clock(catherine_camera_render_clock())&&sample_owner(owner)&&owner==active.owner;
}
} // namespace c02_bridge

extern "C" void catherine_c02_movement_begin(uint64_t model,uint64_t input,uint64_t caller,uint64_t apply){
 using namespace c02_bridge;pending={};if(!enabled())return;MxcsrGuard fp;
 const uint64_t clock=catherine_camera_render_clock();if(!enter_scope(clock))return;Owner owner;
 Position old{},incoming{};
 if(!high_clock(clock)){std::lock_guard lock(mutex);clear_current();return;}
 if(caller!=0x70001bf350ull||apply>1||!sample_owner(owner)||owner.model!=model||
    !read_bytes(model+0x5d0,old.data(),12)||!read_bytes(input+0x10,incoming.data(),12))return;
 std::lock_guard lock(mutex);if(!sync(owner,clock,true))return;
 pending={owner,old,incoming,active.lifetime,clock,true};
}
extern "C" void catherine_c02_movement_published(uint64_t model){
 using namespace c02_bridge;if(!enabled())return;MxcsrGuard fp;auto p=pending;pending={};if(!p.ready||p.owner.model!=model)return;
 Owner owner;Position current{};const uint64_t clock=catherine_camera_render_clock();
 if(!sample_owner(owner)||owner!=p.owner||clock!=p.clock||!read_bytes(model+0x5d0,current.data(),12)||current!=p.input)return;
 std::lock_guard lock(mutex);if(!active.valid||active.lifetime!=p.lifetime||active.owner!=owner)return;
 const auto result=active.history.publish(active.lifetime,p.old,current,clock);active.current=current;
 ++census.published;
 if(result==Publication::invalid){clear_current();return;}
 if(result==Publication::reset)active.facing.reset();
 if(result==Publication::changed||result==Publication::coalesced||result==Publication::reset){
   if(clock!=active.changed_clock||result==Publication::reset)active.suppressed=false;
   active.changed_clock=clock;active.decisions={};version(clock);
   if(result==Publication::coalesced)++census.coalesced;
 }
 Position delta{};for(size_t i=0;i<3;++i)delta[i]=current[i]-p.old[i];
 qa("publication_old",clock,owner,active.lifetime,~0u,0,0,p.old,delta,unsigned(result));
 qa("publication_new",clock,owner,active.lifetime,~0u,0,0,current,delta,unsigned(result));
}
extern "C" void catherine_c02_model_retired(uint64_t model){
 using namespace c02_bridge;if(!enabled()||!model)return;MxcsrGuard fp;std::lock_guard lock(mutex);Owner owner;
 if(!(active.valid&&active.owner.model==model)&&!(sample_owner(owner)&&owner.model==model))return;
 retired_model=model;
 if(active.valid&&active.owner.model==model)clear_current();
}
extern "C" void catherine_c02_palette_copied(uint64_t parent,uint32_t slot,uint64_t destination,uint32_t count){
 using namespace c02_bridge;if(!enabled())return;MxcsrGuard fp;const uint64_t clock=catherine_camera_render_clock();if(!enter_scope(clock))return;Owner owner;
 std::lock_guard lock(mutex);++census.palette;erase_span(rigid_records,destination,uint64_t(count)*64);
 if(count!=89&&count!=74){log_counts();return;}
 if(active.valid&&active.owner.parent!=parent){++census.owner_reject;log_counts();return;}
 if(!current_owner(owner,clock)||(!active.valid&&!sync(owner,clock,false))||owner.parent!=parent||count!=owner.matrix_count){log_counts();return;}
 Decision d;if(!decision(owner,slot,clock,d)){log_counts();return;}
 const size_t bytes=size_t(owner.matrix_count)*64;
 std::array<uint32_t,89*16> raw{},corrected{};
 if(!read_bytes(destination,raw.data(),bytes))return;
 if(d.translation.has_delta||d.facing.active){if(!catherine::visual::facing::apply(d.facing,d.translation,active.lifetime,raw.data(),corrected.data(),bytes,owner.matrix_count,true)||
      !write_bytes(destination,corrected.data(),bytes))return;}
 else corrected=raw;
 if(!d.facing.active)for(size_t m=0;m<owner.matrix_count;++m)for(size_t w=0;w<16;++w)if(w!=3&&w!=7&&w!=11&&raw[m*16+w]!=corrected[m*16+w])return;
 if(d.facing.active)qa("facing_palette",clock,owner,active.lifetime,slot,d.entry,destination,d.represented,{float(d.facing.angle),0,0},1);
 if(!d.facing.active)++census.basis_unchanged;++census.palette_owned;
 qa("palette",clock,owner,active.lifetime,slot,d.entry,destination,d.represented,d.translation.delta,d.translation.has_delta);log_counts();
}
extern "C" void catherine_c02_rigid_copied(uint64_t mesh,uint32_t slot,uint64_t destination){
 using namespace c02_bridge;if(!enabled())return;MxcsrGuard fp;const uint64_t clock=catherine_camera_render_clock();if(!enter_scope(clock))return;Owner owner;
 std::lock_guard lock(mutex);++census.rigid;erase_span(rigid_records,destination,64);
 uint64_t parent=0,base=0;uint32_t mesh_flags=0;
 if(slot>=3||!read(mesh+0x20,mesh_flags)||(mesh_flags&1)||!read(mesh+0x1c0,parent)){log_counts();return;}
 if(active.valid&&active.owner.parent!=parent){++census.owner_reject;log_counts();return;}
 if(!current_owner(owner,clock)||(!active.valid&&!sync(owner,clock,false))||parent!=owner.parent||
    !read(mesh+0x1b8,base)||base>UINT64_MAX-slot*64||destination!=base+slot*64||rigid_records.size()>=record_limit){log_counts();return;}
 Decision d;if(!decision(owner,slot,clock,d)){log_counts();return;}
 std::array<uint32_t,16> raw{},corrected{};if(!read_bytes(destination,raw.data(),64))return;
 if(d.translation.has_delta||d.facing.active){if(!catherine::visual::facing::apply(d.facing,d.translation,active.lifetime,raw.data(),corrected.data(),64,1,false)||!write_bytes(destination,corrected.data(),64))return;}
 else corrected=raw;
 if(!d.facing.active)for(size_t w=0;w<16;++w)if((w<12||w>14)&&raw[w]!=corrected[w])return;
 if(d.facing.active)qa("facing_rigid",clock,owner,active.lifetime,slot,d.entry,destination,d.represented,{float(d.facing.angle),0,0},1);
 rigid_records[destination]={active.lifetime,next_serial(),clock,corrected};
 ++census.rigid_owned;qa("rigid",clock,owner,active.lifetime,slot,d.entry,destination,d.represented,d.translation.delta,d.translation.has_delta);log_counts();
}
extern "C" void catherine_c02_c3_loaded(uint64_t source){
#ifndef C02_BRIDGE_FIXTURE
 babel_bridge::c3_loaded(source);
#endif

 using namespace c02_bridge;c3={};current_upload_owned=false;if(!enabled())return;MxcsrGuard fp;
 if(!enter_scope(catherine_camera_render_clock()))return;std::lock_guard lock(mutex);++census.loaded;
 if(!high_clock(catherine_camera_render_clock()))return;
 auto found=rigid_records.find(source);std::array<uint32_t,16> actual{};
 if(found==rigid_records.end()||!current_lifetime(found->second.lifetime)||!read_bytes(source,actual.data(),64)||actual!=found->second.words)return;
 c3={found->second,true};++census.loaded_owned;
}
extern "C" void catherine_c02_c3_uploaded(uint64_t destination,uint64_t backing,uint64_t offset){
#ifndef C02_BRIDGE_FIXTURE
 babel_bridge::c3_uploaded(destination,backing,offset);
#endif

 using namespace c02_bridge;if(!enabled())return;MxcsrGuard fp;current_upload_owned=false;
 if(!enter_scope(catherine_camera_render_clock()))return;std::lock_guard lock(mutex);++census.uploaded;
 Upload upload;upload.cpu=destination;upload.backing=backing;upload.offset=offset;upload.serial=next_serial();
 if(!destination||!backing){gpu_tags.clear();unresolved.clear();c3={};address={};return;}
 if(c3.valid&&current_lifetime(c3.record.lifetime)){
   std::array<uint32_t,16> expected{};for(size_t r=0;r<4;++r)for(size_t c=0;c<4;++c)expected[r*4+c]=c3.record.words[c*4+r];
   upload.owned=read_bytes(destination,upload.words.data(),64)&&upload.words==expected;
   if(upload.owned){upload.lifetime=c3.record.lifetime;++census.uploaded_owned;}
 }
 current_upload_owned=upload.owned;
 // An unowned upload only invalidates overlaps: it never needs a CPU64 read.
 if(!upload.owned&&gpu_tags.empty()&&unresolved.empty())return;
 if(unresolved.size()>=unresolved_limit&&!unresolved.contains(backing)){
   // Empty buckets retain capacity only; they carry no pending ownership proof.
   // Never discard unresolved writer records to make room for a new backing.
   const auto empty=std::find_if(unresolved.begin(),unresolved.end(),[](const auto& entry){return entry.second.empty();});
   if(empty==unresolved.end()){gpu_tags.clear();return;}
   unresolved.erase(empty);
 }
 auto& list=unresolved[backing];if(list.size()>=unresolved_limit){list.clear();gpu_tags.clear();return;}
 list.push_back(upload);
 if(upload.owned||census.uploaded==1||census.uploaded%4096==0)
  qa(upload.owned?"c3_upload_owned":"c3_upload_unowned",catherine_camera_render_clock(),active.owner,upload.lifetime,~0u,backing,destination,{}, {},upload.serial);
}
extern "C" void catherine_c02_c3_buffer_address(uint64_t backing,uint64_t base){
#ifndef C02_BRIDGE_FIXTURE
 babel_bridge::buffer_address(backing,base);
#endif

 using namespace c02_bridge;address={backing,base,backing&&base};if(!enabled())return;MxcsrGuard fp;
 if(!enter_scope(catherine_camera_render_clock()))return;std::lock_guard lock(mutex);auto found=unresolved.find(backing);if(found==unresolved.end())return;
 for(const auto& upload:found->second){
   if(!base||upload.offset>UINT64_MAX-base||base+upload.offset>UINT64_MAX-64){gpu_tags.clear();continue;}
   const uint64_t gpu=base+upload.offset;erase_span(gpu_tags,gpu,64);++census.resolved;
   std::array<uint32_t,16> actual{};
   if(!upload.owned||gpu_tags.size()>=record_limit||!read_bytes(upload.cpu,actual.data(),64)||actual!=upload.words)continue;
   GpuTag tag;static_cast<Upload&>(tag)=upload;tag.gpu=gpu;tag.base=base;gpu_tags[gpu]=tag;
 }
 found->second.clear(); // Every record was resolved above; retain this drained vector capacity.
}
static int catherine_c02_c3_record_owned_original(uint64_t gpu,uint64_t size){
 using namespace c02_bridge;if(!enabled())return 0;MxcsrGuard fp;
 if(!enter_scope(catherine_camera_render_clock())){last_recorded_owned.store(false,std::memory_order_relaxed);return 0;}
 std::lock_guard lock(mutex);++census.recorded;last_recorded_owned=false;
 if(!high_clock(catherine_camera_render_clock())){clear_current();log_counts();return 0;}
 if(size!=64||!address.valid){log_counts();return 0;}
 auto found=gpu_tags.find(gpu);std::array<uint32_t,16> actual{};
 if(found==gpu_tags.end()||!current_lifetime(found->second.lifetime)||found->second.backing!=address.backing||found->second.base!=address.base||
    !read_bytes(found->second.cpu,actual.data(),64)||actual!=found->second.words){log_counts();return 0;}
 last_recorded_owned=true;++census.recorded_owned;log_counts();return 1;
}
static int catherine_c02_c3_force_bind_original(void){
 using namespace c02_bridge;if(!enabled())return 0;MxcsrGuard fp;
 if(!enter_scope(catherine_camera_render_clock()))return last_recorded_owned.load(std::memory_order_relaxed);
 std::lock_guard lock(mutex);
 if(!high_clock(catherine_camera_render_clock())){clear_current();return last_recorded_owned.load(std::memory_order_relaxed);}
 // Preserve native address/backing dedup whenever ownership is unchanged.
 return current_upload_owned!=last_recorded_owned.load(std::memory_order_relaxed);
}

namespace c02_bridge {
thread_local struct FacingPending {Owner owner{};uint64_t life=0,clock=0;double old=0,incoming=0;bool valid=false;} facing_pending;
}
extern "C" void catherine_p1_facing_begin(uint64_t model,uint64_t input){
 using namespace c02_bridge;facing_pending={};if(!enabled()||!turns_enabled())return;MxcsrGuard fp;
 const uint64_t clock=catherine_camera_render_clock();if(!enter_scope(clock)||!high_clock(clock))return;
 Owner owner;float a[4],b[4];double old_yaw,new_yaw;
 uint64_t holder=0,actor=0,primary=0;
 if(!read(holder_global,holder)||!holder||!read(holder,actor)||!actor||!read(actor+0x20,primary)||primary!=model)return;
 if(!sample_owner(owner)||owner.model!=model||!read_bytes(model+0x5f0,a,16)||!read_bytes(input+0x10,b,16)||
    !catherine::visual::facing::quaternion_yaw(a,old_yaw)||!catherine::visual::facing::quaternion_yaw(b,new_yaw))return;
 std::lock_guard lock(mutex);if(!sync(owner,clock,true))return;
 facing_pending={owner,active.lifetime,clock,old_yaw,new_yaw,true};
}
extern "C" void catherine_p1_facing_published(uint64_t model){
 using namespace c02_bridge;if(!enabled()||!turns_enabled())return;MxcsrGuard fp;
 auto p=facing_pending;facing_pending={};if(!p.valid||p.owner.model!=model)return;
 Owner owner;float q[4];double yaw;const uint64_t clock=catherine_camera_render_clock();
 if(clock!=p.clock||!sample_owner(owner)||owner!=p.owner||!read_bytes(model+0x5f0,q,16)||
    !catherine::visual::facing::quaternion_yaw(q,yaw)||std::abs(catherine::visual::facing::difference(yaw,p.incoming))>1e-5)return;
 std::lock_guard lock(mutex);if(!active.valid||active.lifetime!=p.life||active.owner!=owner)return;
 active.facing.publish(p.life,p.old,yaw,clock);
 if(std::abs(catherine::visual::facing::difference(yaw,p.old))>1e-7){
   active.decisions={};qa("facing_publication",clock,owner,p.life,~0u,0,0,{}, {float(p.old),float(yaw),0},1);
 }
}

#ifndef C02_BRIDGE_FIXTURE
#include "../../../graphics/src/nvn/babel_node_bridge.inc"
#endif

extern "C" int catherine_c02_c3_record_owned(uint64_t gpu,uint64_t size){
 const int original=catherine_c02_c3_record_owned_original(gpu,size);
#ifndef C02_BRIDGE_FIXTURE
 const bool babel=babel_bridge::record_owned(gpu,size);return original||babel;
#else
 return original;
#endif
}
extern "C" int catherine_c02_c3_force_bind(void){
 const int original=catherine_c02_c3_force_bind_original();
#ifndef C02_BRIDGE_FIXTURE
 return original||babel_bridge::force_bind();
#else
 return original;
#endif
}
