#pragma once
#include "c03_registry.h"
#include "c03_native_api.h"
#include <array>
#include <map>
#include <algorithm>
#include <cstring>
#include <vector>

namespace c03_private {
// Value state only. Its owner must hold the SAME controller mutex as Registry/history.
class RigidTransport {
public:
 static constexpr size_t Owners=32, MaxMeshes=47, MaxNodes=218, Rigid=29, Slots=3;
 struct Member {uint64_t mesh{},node{},base{};};
 struct Copy {CatherineC03RigidPacket packet{};uint64_t source_serial{},reservation{};bool valid{};};
 struct Cap {C03Admission admission{};const RootProfile* profile{};std::array<Member,Rigid> members{};std::array<Copy,Rigid*Slots> copies{};uint64_t generation{};bool reserved{},enabled{};uint64_t first_scalar_clock{},last_scalar_clock{},scalar_samples{};uint32_t scalar_kind{},scalar_native_bits{},scalar_result_bits{};};
 struct Flow {Copy loaded{};Copy uploaded{};uint64_t cpu{},backing{},offset{};bool owned{};};
private:
 struct Upload {Copy copy{};uint64_t cpu{},backing{},offset{};};
 struct Pending {Copy copy{};uint64_t backing{},offset{};bool owned{},from_load{};};
 std::array<Cap,Owners> caps_{};
 using RangeKey=std::pair<uint64_t,uint64_t>;
 // No arbitrary tag cap or time eviction. Retained until actual overlap or backing/lifecycle retirement.
 std::map<RangeKey,Upload> uploads_;
 std::map<uint64_t,RangeKey> cpu_ranges_;
 std::map<uint64_t,uint64_t> bases_;
 std::map<uint64_t,Pending> pending_;
 std::map<uint64_t,bool> recorded_owned_;
 uint64_t next_{1};
 void recorded(uint64_t command,bool owned){auto i=recorded_owned_.find(command);if(i!=recorded_owned_.end())i->second=owned;else if(owned)recorded_owned_.emplace(command,true);}
 uint64_t serial(){if(!next_||next_==UINT64_MAX){next_=0;return 0;}return next_++;}
 static bool same(const C03Admission&a,const C03Admission&b){return a.actor_birth==b.actor_birth&&a.model_birth==b.model_birth&&a.resource_cookie==b.resource_cookie&&a.association_serial==b.association_serial&&a.continuity_serial==b.continuity_serial&&a.context.actor==b.context.actor&&a.context.model==b.context.model&&a.context.sdk==b.context.sdk;}
 Cap* find(uint64_t a){for(auto&c:caps_)if(c.reserved&&c.admission.context.actor==a)return &c;return nullptr;}
 const Cap* find(uint64_t a)const{for(auto&c:caps_)if(c.reserved&&c.admission.context.actor==a)return &c;return nullptr;}
 bool valid(const Copy&c)const{auto*cap=find(c.packet.actor);return c.valid&&cap&&cap->enabled&&cap->generation==c.reservation&&cap->admission.association_serial==c.packet.association&&cap->admission.continuity_serial==c.packet.continuity&&cap->admission.context.model==c.packet.model&&cap->admission.context.sdk==c.packet.sdk;}
 bool valid_source(const Copy&copy)const{
  if(!valid(copy))return false;auto*c=find(copy.packet.actor);
  for(const auto&current:c->copies)if(current.valid&&current.packet.mesh==copy.packet.mesh&&current.packet.slot==copy.packet.slot&&current.source_serial==copy.source_serial)return true;
  return false;
 }
 template<class T>static bool rd(C03Read r,void*x,uint64_t p,T&out){out={};return p&&r(x,p,&out,sizeof(out));}
 static bool span(uint64_t p,uint64_t n){return p&&n&&p<=UINT64_MAX-n;}
 static bool intersects(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return span(a,n)&&span(b,m)&&a<b+m&&b<a+n;}
 void erase_upload(std::map<RangeKey,Upload>::iterator i){cpu_ranges_.erase(i->second.cpu);uploads_.erase(i);}
 void cpu_overlap(uint64_t cpu,uint64_t size){
  if(!span(cpu,size))return;auto i=cpu_ranges_.lower_bound(cpu);
  if(i!=cpu_ranges_.begin()){auto p=std::prev(i);if(intersects(cpu,size,p->first,64))i=p;}
  while(i!=cpu_ranges_.end()&&i->first<cpu+size){auto key=i->second;++i;auto u=uploads_.find(key);if(u!=uploads_.end()&&intersects(cpu,size,u->second.cpu,64))erase_upload(u);}
 }
 void overlap(uint64_t backing,uint64_t offset,uint64_t size){
  if(!size||offset>UINT64_MAX-size)return;
  auto i=uploads_.lower_bound({backing,offset});
  if(i!=uploads_.begin()){auto p=std::prev(i);if(p->first.first==backing&&p->first.second<=UINT64_MAX-64&&p->first.second+64>offset)i=p;}
  while(i!=uploads_.end()&&i->first.first==backing&&i->first.second<offset+size){
   if(i->first.second<=UINT64_MAX-64&&i->first.second+64>offset){auto old=i++;erase_upload(old);}else ++i;
  }
 }
 bool roster(C03Read r,void*x,const C03Admission&a,const RootProfile& profile,std::array<Member,Rigid>&out)const{
  uint64_t meshes{},list{},flat{},nodes{};uint32_t meshcount{},nodecount{};
  if(!rd(r,x,a.context.sdk+0x38,meshes)||!rd(r,x,meshes+8,meshcount)||meshcount!=profile.meshes||!rd(r,x,meshes+16,list)||!list||
     !rd(r,x,a.context.sdk+0x30,flat)||!rd(r,x,flat+8,nodecount)||nodecount!=profile.nodes||!rd(r,x,flat+16,nodes)||!nodes)return false;
  std::array<uint64_t,MaxMeshes> meshptrs{};std::array<uint64_t,MaxNodes> nodeptrs{},parents{};
  if(!r(x,list,meshptrs.data(),profile.meshes*sizeof(uint64_t))||!r(x,nodes,nodeptrs.data(),profile.nodes*sizeof(uint64_t))||nodeptrs[0]!=a.context.authored_root)return false;
  for(size_t i=0;i<profile.nodes;i++){
   if(!nodeptrs[i]||std::find(nodeptrs.begin(),nodeptrs.begin()+i,nodeptrs[i])!=nodeptrs.begin()+i||!rd(r,x,nodeptrs[i]+8,parents[i]))return false;
  }
  if(parents[0]!=a.context.synthetic_root)return false;
  for(size_t i=1;i<profile.nodes;i++){
   uint64_t p=nodeptrs[i];bool root=false;
   for(size_t n=0;n<profile.nodes;n++){
    if(p==a.context.authored_root){root=true;break;}
    auto it=std::find(nodeptrs.begin(),nodeptrs.begin()+profile.nodes,p);if(it==nodeptrs.begin()+profile.nodes)return false;
    p=parents[size_t(it-nodeptrs.begin())];
   }
   if(!root)return false;
  }
  if(profile.kind!=3){
   uint64_t palette_table{};uint32_t palette_count{};
   if(!rd(r,x,a.context.sdk+0x138,palette_table)||!rd(r,x,palette_table,palette_count)||palette_count!=profile.palette_node_dfs.size())return false;
  }
  size_t rigid=0,weighted=0;
  for(size_t i=0;i<profile.meshes;i++){
   uint64_t m=meshptrs[i],owner{},node{},base{};uint32_t flags{};
   if(!m||std::find(meshptrs.begin(),meshptrs.begin()+i,m)!=meshptrs.begin()+i||!rd(r,x,m+0x1c0,owner)||owner!=a.context.sdk||
      !rd(r,x,m+8,node)||std::find(nodeptrs.begin(),nodeptrs.begin()+profile.nodes,node)==nodeptrs.begin()+profile.nodes||!rd(r,x,m+0x20,flags))return false;
   if(flags&1){++weighted;continue;}
   if(rigid>=profile.rigid||!rd(r,x,m+0x1b8,base)||!span(base,Slots*64))return false;
   std::array<unsigned char,Slots*64> readable{};if(!r(x,base,readable.data(),readable.size()))return false;
   for(size_t j=0;j<rigid;j++)if(intersects(base,Slots*64,out[j].base,Slots*64))return false;
   out[rigid++]={m,node,base};
  }
  return rigid==profile.rigid&&weighted==profile.weighted;
 }
public:
 bool reserve(C03Read r,void*x,const C03Admission&a){
  const auto* profile=tuple_profile(a.context.kind,a.context.variant,a.context.resource);
  std::array<Member,Rigid> members{};
  if(!profile||profile->nodes>MaxNodes||profile->meshes>MaxMeshes||profile->rigid>Rigid||profile->warm_copies>Rigid*Slots||!roster(r,x,a,*profile,members)){revoke_actor(a.context.actor);return false;}
  auto*c=find(a.context.actor);
  if(c&&c->profile==profile&&same(c->admission,a)&&std::memcmp(c->members.data(),members.data(),sizeof(members))==0)return true;
  revoke_actor(a.context.actor);c=nullptr;for(auto&v:caps_)if(!v.reserved){c=&v;break;}if(!c)return false;
  *c={};c->generation=serial();if(!c->generation)return false;c->admission=a;c->profile=profile;c->members=members;c->reserved=true;
  // The complete declared profile prefix exists before SDK interpolation is permitted. No per-mesh allocation.
  return true;
 }
 bool ready(const C03Admission&a)const{auto*c=find(a.context.actor);return c&&c->profile&&c->profile==tuple_profile(a.context.kind,a.context.variant,a.context.resource)&&same(c->admission,a)&&std::all_of(c->copies.begin(),c->copies.begin()+c->profile->warm_copies,[](const Copy&v){return v.valid;});}
 bool arm(const C03Admission&a){auto*c=find(a.context.actor);if(!c||!ready(a))return false;c->enabled=true;return true;}
 void revoke_actor(uint64_t actor){
  for(auto&c:caps_)if(c.reserved&&c.admission.context.actor==actor)c={};
  for(auto i=uploads_.begin();i!=uploads_.end();)if(i->second.copy.packet.actor==actor){auto old=i++;erase_upload(old);}else ++i;
  for(auto&[cmd,p]:pending_)if(p.copy.packet.actor==actor)p={};
  // Keep recorded_owned=true until the next native bind clears its historical packet.
 }
 void revoke_model(uint64_t model){std::array<uint64_t,Owners> actors{};size_t n=0;for(auto&c:caps_)if(c.reserved&&c.admission.context.model==model)actors[n++]=c.admission.context.actor;for(size_t i=0;i<n;i++)revoke_actor(actors[i]);}
 void reset(){for(auto&c:caps_)c={};uploads_.clear();cpu_ranges_.clear();bases_.clear();pending_.clear();recorded_owned_.clear();}
 void copied(C03Read r,void*x,uint64_t mesh,uint32_t slot,uint64_t destination,uint64_t* revoked=nullptr){
  if(revoked)*revoked=0;
  if(slot>=Slots||!span(destination,64))return;
  // A foreign/partial alias cannot invalidate just one member after SDK admission.
  // Revoke the whole owner; its next SDK sample must reserve and warm again.
  for(auto&c:caps_)if(c.reserved)for(size_t i=0;i<c.profile->rigid;i++)for(size_t k=0;k<Slots;k++){
   if(!intersects(destination,64,c.members[i].base+k*64,64))continue;
   if(mesh!=c.members[i].mesh||slot!=k||destination!=c.members[i].base+k*64){const uint64_t actor=c.admission.context.actor;revoke_actor(actor);if(revoked)*revoked=actor;return;}
   c.copies[i*Slots+k]={};
  }
  for(auto&c:caps_)if(c.reserved)for(size_t i=0;i<c.profile->rigid;i++){
   auto&m=c.members[i];if(m.mesh!=mesh)continue;uint64_t sdk{},node{},base{};uint32_t flags{};
   if(destination!=m.base+slot*64||!rd(r,x,mesh+0x1c0,sdk)||sdk!=c.admission.context.sdk||!rd(r,x,mesh+8,node)||node!=m.node||
      !rd(r,x,mesh+0x1b8,base)||base!=m.base||!rd(r,x,mesh+0x20,flags)||(flags&1)){const uint64_t actor=c.admission.context.actor;revoke_actor(actor);if(revoked)*revoked=actor;return;}
   Copy copy{};auto&p=copy.packet;p.actor=c.admission.context.actor;p.component=p.actor+16;p.model=c.admission.context.model;p.sdk=sdk;p.mesh=mesh;p.node=node;
   p.association=c.admission.association_serial;p.continuity=c.admission.continuity_serial;p.serial=serial();p.address=destination;p.size=64;p.slot=slot;p.version=1;
   if(!p.serial||!r(x,destination,p.words,64)){const uint64_t actor=c.admission.context.actor;revoke_actor(actor);if(revoked)*revoked=actor;return;}copy.source_serial=p.serial;copy.reservation=c.generation;copy.valid=true;c.copies[i*Slots+slot]=copy;return;
  }
 }
 void loaded(C03Read r,void*x,Flow&flow,uint64_t source){
  flow={};if(!span(source,64))return;
  for(auto&c:caps_)if(c.reserved)for(auto&copy:c.copies)if(copy.valid&&copy.packet.address==source){
   std::array<uint32_t,16> words{};if(!r(x,source,words.data(),64)||std::memcmp(words.data(),copy.packet.words,64))return;flow.loaded=copy;return;
  }
 }
 void uploaded(C03Read r,void*x,Flow&flow,uint64_t cpu,uint64_t backing,uint64_t offset,bool gpu_eligible=true){
  const Copy loaded=flow.loaded;flow.loaded={};
  flow.uploaded={};flow.owned=false;flow.cpu=cpu;flow.backing=backing;flow.offset=offset;
  cpu_overlap(cpu,64);
  if(!backing||offset>UINT64_MAX-64)return;
  overlap(backing,offset,64);
  if(!gpu_eligible||!span(cpu,64)||!valid_source(loaded))return;
  Upload u{};u.copy=loaded;u.cpu=cpu;u.backing=backing;u.offset=offset;
  std::array<uint32_t,16> actual{};if(!r(x,cpu,actual.data(),64))return;
  for(size_t a=0;a<4;a++)for(size_t b=0;b<4;b++)if(actual[a*4+b]!=loaded.packet.words[b*4+a])return;
  std::memcpy(u.copy.packet.words,actual.data(),64);u.copy.packet.serial=serial();if(!u.copy.packet.serial)return;
  uploads_[{backing,offset}]=u;cpu_ranges_[cpu]={backing,offset};flow.uploaded=u.copy;flow.owned=true;
 }
 bool force_bind(Flow&flow,uint64_t command){
  if(!command)return false;
  auto prior=recorded_owned_.find(command);const bool was=prior!=recorded_owned_.end()&&prior->second;
  if(flow.owned&&valid(flow.uploaded)){pending_[command]={flow.uploaded,flow.backing,flow.offset,true,false};return true;}
  const Copy loaded=flow.loaded;flow.loaded={};
  if(valid_source(loaded)){pending_[command]={loaded,0,0,true,true};return true;}
  pending_.erase(command);return was;
 }
 void buffer_address(C03Read read,void*context,Flow&flow,uint64_t command,uint64_t backing,uint64_t base,uint64_t offset){
  if(!command||!backing)return;
  if(!base){backing_retired(backing);return;}
  auto range=uploads_.lower_bound({backing,0});auto pending=pending_.find(command);
  if((range==uploads_.end()||range->first.first!=backing)&&(pending==pending_.end()||!pending->second.owned))return;
  auto it=bases_.find(backing);if(it!=bases_.end()&&it->second!=base){backing_retired(backing);return;}
  bases_[backing]=base;
  auto p=pending_.find(command);if(p==pending_.end()||!p->second.owned)return;
  auto u=uploads_.find({backing,offset});
  if(u==uploads_.end()||!valid(u->second.copy)||!valid(p->second.copy)||
     u->second.copy.packet.actor!=p->second.copy.packet.actor||u->second.copy.reservation!=p->second.copy.reservation){pending_.erase(p);return;}
  std::array<uint32_t,16> expected{},resident{};
  if(p->second.from_load){
   if(!valid_source(p->second.copy)){pending_.erase(p);return;}
   for(size_t a=0;a<4;a++)for(size_t b=0;b<4;b++)expected[a*4+b]=p->second.copy.packet.words[b*4+a];
  }else std::memcpy(expected.data(),p->second.copy.packet.words,64);
  if(std::memcmp(expected.data(),u->second.copy.packet.words,64)||!read(context,u->second.cpu,resident.data(),64)||std::memcmp(expected.data(),resident.data(),64)){pending_.erase(p);return;}
  // Native assignment elision may reuse another same-owner mesh's identical resident matrix.
  // Only a fresh owned source/load or an owned actual upload can transfer this proof.
  Copy transferred=p->second.copy;std::memcpy(transferred.packet.words,expected.data(),64);
  transferred.packet.serial=serial();if(!transferred.packet.serial){pending_.erase(p);return;}
  u->second.copy=transferred;p->second={transferred,backing,offset,true,false};
  flow.uploaded=transferred;flow.backing=backing;flow.offset=offset;flow.cpu=u->second.cpu;flow.owned=true;
 }
 bool snapshot(C03Read r,void*x,uint64_t command,uint64_t address,uint64_t size,CatherineC03RigidPacket&out){
  out={};Pending consumed{};auto permission=pending_.find(command);
  if(permission!=pending_.end()){consumed=permission->second;pending_.erase(permission);}
  if(!command||size!=64||!span(address,64)){if(command)recorded(command,false);return false;}
  if(!consumed.owned||!valid(consumed.copy)){recorded(command,false);return false;}
  auto base=bases_.find(consumed.backing);if(base==bases_.end()||address<base->second||address-base->second!=consumed.offset){recorded(command,false);return false;}
  auto u=uploads_.find({consumed.backing,address-base->second});
  if(u==uploads_.end()||!valid(u->second.copy)||u->second.copy.packet.actor!=consumed.copy.packet.actor||u->second.copy.packet.mesh!=consumed.copy.packet.mesh||
     u->second.copy.packet.slot!=consumed.copy.packet.slot||std::memcmp(u->second.copy.packet.words,consumed.copy.packet.words,64)){recorded(command,false);return false;}
  std::array<uint32_t,16> words{};if(!r(x,u->second.cpu,words.data(),64)||std::memcmp(words.data(),u->second.copy.packet.words,64)){recorded(command,false);return false;}
  out=u->second.copy.packet;out.address=address;out.size=64;recorded(command,true);return true;
 }
 void backing_retired(uint64_t backing){
  auto i=uploads_.lower_bound({backing,0});while(i!=uploads_.end()&&i->first.first==backing){auto old=i++;erase_upload(old);}
  bases_.erase(backing);for(auto&[cmd,p]:pending_)if(p.backing==backing)p={};
 }
 void command_retired(uint64_t command){pending_.erase(command);recorded_owned_.erase(command);}
 uint64_t generation(uint64_t actor)const{auto*c=find(actor);return c?c->generation:0;}
 bool status(uint64_t actor,CatherineC03RigidStatus&out)const{out={};auto*c=find(actor);if(!c)return false;out.association=c->admission.association_serial;out.continuity=c->admission.continuity_serial;out.upload_ranges=uploads_.size();out.reserved=1;out.ready_copies=uint32_t(ready_copies(actor));out.armed=c->enabled?1:0;out.first_scalar_clock=c->first_scalar_clock;out.last_scalar_clock=c->last_scalar_clock;out.scalar_samples=c->scalar_samples;out.scalar_kind=c->scalar_kind;out.scalar_native_bits=c->scalar_native_bits;out.scalar_result_bits=c->scalar_result_bits;return true;}
 void scalar_status(uint64_t actor,uint64_t clock,uint32_t kind,uint32_t nativebits,uint32_t result){auto*c=find(actor);if(!c)return;c->last_scalar_clock=clock;c->scalar_kind=kind;c->scalar_native_bits=nativebits;c->scalar_result_bits=result;if(kind){if(!c->scalar_samples)c->first_scalar_clock=clock;++c->scalar_samples;}else{c->first_scalar_clock=0;c->scalar_samples=0;}}
 size_t uploads()const{return uploads_.size();}
 size_t owner_count()const{size_t n=0;for(auto&c:caps_)n+=c.reserved;return n;}
 size_t ready_copies(uint64_t actor)const{auto*c=find(actor);if(!c)return 0;return std::count_if(c->copies.begin(),c->copies.begin()+c->profile->warm_copies,[](const Copy&v){return v.valid;});}
};
}
