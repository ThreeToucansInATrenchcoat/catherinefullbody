// Included by the existing native movement bridge. All output is private
// render-copy storage; native skeleton nodes and gameplay state are read only.
#include "babel_node_interpolation.h"
#include <unordered_map>
#include <immintrin.h>
extern "C" uint64_t catherine_announcement_frame_clock(void);
extern "C" { uint64_t catherine_babel_pose_counters[4] = {}; }
namespace babel_bridge {
using namespace catherine::visual;
using namespace catherine::visual::babel;
struct Owner {
 uint64_t actor=0,model=0,parent=0,table=0,root=0;
 uint32_t ids[4]{},count=0;
 bool operator==(const Owner&)const=default;
};
struct Player {Owner owner;uint64_t lifetime=0;bool hanging=false;NodeHistory root;std::unordered_map<uint64_t,NodeHistory> nodes;};
std::mutex mutex;std::array<Player,2> players;
uint64_t sampled_frame=~0ull,next_lifetime=0;
std::atomic<uint64_t> published_frame{~0ull},parents[2]{};
bool enabled(){static bool value=[] {const char*s=std::getenv("CATHERINE_BABEL_NODE_INTERPOLATION");return !s||(s[0]=='1'&&!s[1]);}();return value;}
template<class T>bool read(uint64_t address,T& value){return c02_bridge::read_bytes(address,&value,sizeof value);}
bool sample(unsigned player,Owner& out){
 uint64_t registry=0,vt=0;
 if(!read(0x7000ccadf0ull,registry)||!registry||!read(registry+8*player,out.actor)||!out.actor||
    !read(out.actor,vt)||vt!=0x7000abff90ull||!read(out.actor+0x20,out.model)||!out.model||
    !read(out.model,vt)||vt!=0x70009f5a10ull||!read(out.model+0x5a0,out.ids)||
    (out.ids[1]!=30&&out.ids[1]!=31)||out.ids[2]!=1||out.ids[3]!=0||!read(out.model+0x468,out.parent)||!out.parent||
    !read(out.parent+0x138,out.table)||!out.table||!read(out.table,out.count)||
    !supported(out.ids[0],out.ids[1],out.count)||!read(out.model+0x4a8,out.root)||!out.root)return false;
 uint64_t again=0;return read(registry+8*player,again)&&again==out.actor&&read(out.actor+0x20,again)&&again==out.model;
}
void sync(uint64_t frame){
 if(sampled_frame==frame)return;sampled_frame=frame;
 for(unsigned i=0;i<2;++i){Owner owner;if(!sample(i,owner))owner={};auto&p=players[i];if(!(owner==p.owner)){p=Player{};p.owner=owner;p.lifetime=++next_lifetime;}
  // Native bit7 identifies the hanging/root-motion family. Cache the choice
  // once per render so skin and rigid parts cannot select different spaces.
  uint32_t state=0;const bool hanging=owner.actor&&read(owner.actor+0x18c,state)&&(state&0x80u);
  if(hanging!=p.hanging){p.hanging=hanging;p.root={};p.nodes.clear();}
 }
 for(unsigned i=0;i<2;++i)parents[i].store(players[i].owner.parent,std::memory_order_relaxed);
 if(players[0].owner.actor||players[1].owner.actor)++catherine_babel_pose_counters[0];
 published_frame.store(frame,std::memory_order_release);
}
bool active(){return parents[0].load(std::memory_order_relaxed)||parents[1].load(std::memory_order_relaxed);}
bool admitted(uint64_t parent){
 if(!enabled()||!parent)return false;
 if(!(catherine_camera_render_clock()&0x80000000u)){
  if(active()){std::lock_guard lock(mutex);players={};sampled_frame=~0ull;parents[0]=0;parents[1]=0;published_frame.store(~0ull,std::memory_order_release);}
  return false;
 }
 const uint64_t frame=catherine_announcement_frame_clock()>>32;
 if(published_frame.load(std::memory_order_acquire)!=frame){std::lock_guard lock(mutex);sync(frame);}
 return parent==parents[0].load(std::memory_order_relaxed)||parent==parents[1].load(std::memory_order_relaxed);
}
void trace(uint64_t frame,uint64_t clock,const Owner&o,uint64_t node,const Matrix&raw,const Matrix&out){
 static const char*path=std::getenv("CATHERINE_TRACE_BABEL_NODES");if(!path||!*path)return;
 static FILE*f=std::fopen(path,"wb");static unsigned rows=0;if(!f||rows>=100000)return;
 static uint64_t sampled=~0ull;static std::map<uint64_t,unsigned> per_player;
 if(sampled!=frame){sampled=frame;per_player.clear();}if(per_player[o.actor]++>=4)return;
 uint64_t header[]={frame,clock,o.actor,o.model,o.parent,node,uint64_t(o.ids[0])<<32|o.ids[1]};
 std::fwrite(header,sizeof header,1,f);std::fwrite(raw.data(),64,1,f);std::fwrite(out.data(),64,1,f);++rows;
 if(rows%256==0)std::fflush(f);
}
uint64_t world(uint64_t parent,uint64_t node){
 if(!node||!admitted(parent))return node+0x20;
 struct FpGuard{unsigned saved=_mm_getcsr();~FpGuard(){_mm_setcsr(saved);}}fp;
 const uint64_t clock=catherine_camera_render_clock(),frame=catherine_announcement_frame_clock()>>32;
 if(!(clock&0x80000000u)||!frame)return node+0x20;
 std::lock_guard lock(mutex);sync(frame);
 Player* player=nullptr;for(auto&p:players)if(p.owner.parent==parent){if(player)return node+0x20;player=&p;}
 if(!player)return node+0x20;
 Matrix native;if(!read(node+0x20,native))return node+0x20;
 Matrix raw=transpose(native);
 for(float value:raw)if(!std::isfinite(value))return node+0x20;
 Matrix root{},source=raw,rendered{};
 if(!player->hanging){
  Matrix native_root,inverse{};if(!read(player->owner.root+0x20,native_root))return node+0x20;
  root=transpose(native_root);if(!inverse_affine(root.data(),inverse.data()))return node+0x20;
  multiply_affine(inverse.data(),raw.data(),source.data());
  for(float value:source)if(!std::isfinite(value))return node+0x20;
 }
 if(player->nodes.size()>2048)player->nodes.clear();
 auto&history=player->nodes[node];const uint64_t last=history.frame;
 // Ground locomotion has a held placement channel alongside faster pose
 // animation. Keep their histories separate so pose updates cannot mask a
 // 30 Hz translation. Hanging hops instead cancel discrete grid-anchor steps
 // inside the animated skeleton, so their already composed worlds must be
 // sampled together. Interpolating that anchor separately caused block jumps.
 const Matrix&pose=history.sample(source,frame,clock);
 if(player->hanging)rendered=pose;
 else{const Matrix&placement=player->root.sample(root,frame,clock);if(pose==source&&placement==root)return node+0x20;multiply_affine(placement.data(),pose.data(),rendered.data());}
 for(float value:rendered)if(!std::isfinite(value))return node+0x20;
 // Stationary/initial samples retain all native bits, including padding.
 if(rendered==raw)return node+0x20;
 ++catherine_babel_pose_counters[1];
 if(last!=frame)trace(frame,clock,player->owner,node,raw,rendered);
 alignas(16) static thread_local Matrix output;output=transpose(rendered);return reinterpret_cast<uint64_t>(output.data());
}
// Use the existing C02 command provenance bit to keep these already smoothed
// rigid draws out of the generic replay interpolator. Proof follows the exact
// native copy, transpose, upload and buffer-address chain, including unowned
// writers. No geometry hash or palette size establishes ownership.
struct Rigid {uint64_t lifetime=0,frame=0;Matrix words{};};
struct Upload {Rigid rigid;uint64_t cpu=0,backing=0,offset=0,base=0;bool owned=false;};
std::map<uint64_t,Rigid> rigid_copies;
std::map<uint64_t,std::vector<Upload>> uploads;
std::map<uint64_t,Upload> gpu;
thread_local Rigid loaded{};
thread_local uint64_t address_backing=0,address_base=0;
thread_local bool uploaded_owned=false;
std::atomic<bool> recorded_owned{false};
bool live(const Rigid&r){for(const auto&p:players)if(p.owner.actor&&p.lifetime==r.lifetime)return true;return false;}
template<class Map>void invalidate(Map&map,uint64_t begin){auto i=map.lower_bound(begin>63?begin-63:0);while(i!=map.end()&&i->first<begin+64)i=map.erase(i);}
void c3_loaded(uint64_t source){
 if(!enabled()||!active()){loaded.lifetime=0;return;}loaded={};std::lock_guard lock(mutex);
 auto i=rigid_copies.find(source);Matrix actual{};
 if(i!=rigid_copies.end()&&live(i->second)&&read(source,actual)&&actual==i->second.words)loaded=i->second;
}
void c3_uploaded(uint64_t cpu,uint64_t backing,uint64_t offset){
 uploaded_owned=false;if(!enabled()||!active())return;std::lock_guard lock(mutex);
 Upload u;u.cpu=cpu;u.backing=backing;u.offset=offset;
 if(!cpu||!backing){gpu.clear();uploads.clear();return;}
 Matrix actual{};if(loaded.lifetime&&live(loaded)&&read(cpu,actual)&&actual==transpose(loaded.words)){u.owned=true;u.rigid=loaded;u.rigid.words=actual;}
 uploaded_owned=u.owned;if(!u.owned&&gpu.empty()&&uploads.empty())return;
 if(uploads.size()>4096){uploads.clear();gpu.clear();}
 auto& list=uploads[backing];if(list.size()>4096){list.clear();gpu.clear();}list.push_back(u);
}
void buffer_address(uint64_t backing,uint64_t base){
 address_backing=backing;address_base=base;if(!enabled()||!active())return;std::lock_guard lock(mutex);
 auto i=uploads.find(backing);if(i==uploads.end())return;
 for(auto&u:i->second){if(!base||base>UINT64_MAX-64||u.offset>UINT64_MAX-base-64){gpu.clear();continue;}uint64_t address=base+u.offset;invalidate(gpu,address);if(u.owned&&live(u.rigid)){if(gpu.size()>=65536)gpu.clear();u.base=base;gpu[address]=u;}}
 uploads.erase(i);
}
bool record_owned(uint64_t address,uint64_t bytes){
 if(!enabled())return false;
 if(!active()&&!recorded_owned.load(std::memory_order_relaxed))return false;
 recorded_owned.store(false,std::memory_order_relaxed);if(!active())return false;std::lock_guard lock(mutex);
 if(bytes!=64||!address_backing||!address_base)return false;
 const uint64_t frame=catherine_announcement_frame_clock()>>32;sync(frame);
 auto i=gpu.find(address);Matrix actual{};
 if(i==gpu.end()||!live(i->second.rigid)||i->second.backing!=address_backing||i->second.base!=address_base||
    !read(i->second.cpu,actual)||actual!=i->second.rigid.words)return false;
 recorded_owned.store(true,std::memory_order_relaxed);++catherine_babel_pose_counters[3];return true;
}
bool force_bind(){return enabled()&&uploaded_owned!=recorded_owned.load(std::memory_order_relaxed);}
}
extern "C" uint64_t catherine_babel_node_world(uint64_t parent,uint64_t node){return babel_bridge::world(parent,node);}
extern "C" int catherine_babel_parent_admitted(uint64_t parent){return babel_bridge::admitted(parent);}
extern "C" void catherine_babel_rigid_copied(uint64_t mesh,uint32_t slot,uint64_t destination){
 using namespace babel_bridge;if(!enabled()||slot>=3)return;
 uint64_t parent=0,node=0,base=0;uint32_t flags=0;
 if(!read(mesh+0x1c0,parent))return;const bool owned=admitted(parent);
 if(active()){std::lock_guard lock(mutex);rigid_copies.erase(destination);}
 if(!owned||!read(mesh+8,node)||!node||!read(mesh+0x20,flags)||
    (flags&0xf01u)||!read(mesh+0x1b8,base)||destination!=base+64*slot)return;
 const uint64_t result=world(parent,node);if(result!=node+0x20&&!c02_bridge::write_bytes(destination,reinterpret_cast<const void*>(result),64))return;
 std::lock_guard lock(mutex);sync(catherine_announcement_frame_clock()>>32);
 for(const auto&p:players)if(p.owner.parent==parent){Rigid r;r.lifetime=p.lifetime;r.frame=sampled_frame;if(read(destination,r.words)){if(rigid_copies.size()>65536)rigid_copies.clear();rigid_copies[destination]=r;++catherine_babel_pose_counters[2];}break;}
}
