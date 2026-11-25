#pragma once
#include "c03_registry.h"
#include "c03_native_api.h"
#include <array>
#include <map>
#include <algorithm>
#include <limits>

namespace c03_private::c20_group {
// This helper does not admit an actor. The caller must already possess an exact
// native owner/resource admission for the exact family/format and request epoch.
struct Family {
 uint32_t variant;std::string_view path;uint16_t nodes,meshes,weighted,rigid,accessory,type7_count,type7_index;
 uint64_t actor_vptr;
};
inline constexpr std::array<Family,2> families={{{0,"character/20/c20_00.GFS",216,33,11,22,108,1,0,0x70009f6d98ull},
 {2,"character/20/c20_02_0001.GMD",202,28,8,20,74,5,1,0x70009f7b88ull}}};
inline const Family* family(const RootProfile&p,const C03Admission&a){
 if(p.kind!=20||p.resource!=1||a.context.kind!=20||a.context.resource!=1)return nullptr;
 for(const auto&f:families)if(p.variant==f.variant&&a.context.variant==f.variant&&p.actual_gfs_path==f.path&&
  p.family==(f.variant==0?catherine::private_root_profiles::OwnerFamily::Lounge:catherine::private_root_profiles::OwnerFamily::Middlefloor)&&p.format==(f.variant==0?catherine::private_root_profiles::ResourceFormat::Gfs:catherine::private_root_profiles::ResourceFormat::Gmd)&&p.nodes==f.nodes&&p.meshes==f.meshes&&p.weighted==f.weighted&&p.rigid==f.rigid&&a.context.actor_vptr==f.actor_vptr)return &f;
 return nullptr;
}
struct Token {uint64_t birth{},binding{};friend bool operator==(const Token&,const Token&)=default;};
class Lifetimes {
 struct State {Token token{};uint64_t sdk{};bool live{},ready{},changing{};};
 std::map<uint64_t,State> states_;uint64_t next_{1};
 uint64_t mint(){if(!next_||next_==UINT64_MAX){next_=0;return 0;}return next_++;}
public:
 static constexpr size_t Capacity=4096;
 bool birth(uint64_t p){if(!p)return false;auto i=states_.find(p);if(i==states_.end()){if(states_.size()>=Capacity)return false;i=states_.emplace(p,State{}).first;}i->second={};
  auto& s=i->second;s.token={mint(),mint()};s.live=s.ready=s.token.birth&&s.token.binding;return s.live;}
 void begin(uint64_t p){auto i=states_.find(p);if(i==states_.end()||!i->second.live)return;auto&s=i->second;s.ready=false;s.changing=true;s.sdk=0;s.token.binding=mint();}
 bool end(uint64_t p){auto i=states_.find(p);if(i==states_.end()||!i->second.live||!i->second.changing||!i->second.token.binding)return false;i->second.changing=false;i->second.ready=true;return true;}
 void retire(uint64_t p){states_.erase(p);}
 void poison(uint64_t p){auto i=states_.find(p);if(i!=states_.end()){i->second.ready=false;i->second.changing=false;}}
 void sdk_retire(uint64_t sdk){for(auto&[p,s]:states_)if(sdk&&s.sdk==sdk){s.ready=false;s.changing=false;}}
 Token token(uint64_t p)const{auto i=states_.find(p);if(i==states_.end()||!i->second.live||!i->second.ready||i->second.changing)return {};return i->second.token;}
 bool associate(uint64_t p,uint64_t sdk){auto i=states_.find(p);if(!sdk||i==states_.end()||!token(p).birth)return false;
  if(i->second.sdk&&i->second.sdk!=sdk){i->second.ready=false;return false;}i->second.sdk=sdk;return true;}
 bool matches(uint64_t p,uint64_t sdk,Token t)const{auto i=states_.find(p);return t.birth&&t.binding&&token(p)==t&&i!=states_.end()&&i->second.sdk==sdk;}
};
struct Capture {
 uint64_t state{},reference{},sdk{},root{},parent{},type7{},embedded{},particle{},wrapper{};Token token{};
 std::array<uint64_t,227> graph{};uint16_t graph_count{};bool active{};
 friend bool operator==(const Capture&,const Capture&)=default;
 bool contains(uint64_t p)const{return p&&std::find(graph.begin(),graph.begin()+graph_count,p)!=graph.begin()+graph_count;}
};
template<class T>inline bool rd(C03Read r,void*x,uint64_t p,T&v){v={};return p>=0x10000&&p<=UINT64_MAX-sizeof(T)&&r(x,p,&v,sizeof(T));}
inline bool capture(C03Read r,void*x,const C03Admission&a,const Family&f,Lifetimes&life,Capture&out){
 out={};Capture c{};uint64_t flat{},array{},list{},items{},accessory{},embedded_child{},particle_list{},particle_array{},resource{};
 uint32_t count{},kind{};uint16_t mark{};
 if(f.nodes>c.graph.size()-11||f.accessory>=f.nodes||f.type7_index>=f.type7_count||
    !rd(r,x,a.context.sdk+0x30,flat)||!rd(r,x,flat+8,count)||count!=f.nodes||!rd(r,x,flat+16,array)||
    array<0x10000||array>UINT64_MAX-f.nodes*sizeof(uint64_t)||!r(x,array,c.graph.data(),f.nodes*sizeof(uint64_t)))return false;
 for(size_t i=0;i<f.nodes;i++)if(!c.graph[i])return false;c.graph_count=f.nodes;accessory=c.graph[f.accessory];
 if(!rd(r,x,a.context.sdk+0x60,list)||!rd(r,x,list+8,count)||count!=f.type7_count||!rd(r,x,list+16,items)||!items||
    !rd(r,x,items+f.type7_index*8,c.type7)||!rd(r,x,c.type7,kind)||kind!=7||!rd(r,x,c.type7+8,resource)||resource!=accessory||
    !rd(r,x,c.type7+0x40,c.embedded)||!rd(r,x,c.embedded+8,resource)||resource!=accessory||!rd(r,x,c.embedded+0xea,mark)||mark!=1||
    !rd(r,x,c.embedded+0xd8,embedded_child)||!embedded_child||!rd(r,x,embedded_child+8,resource)||resource!=c.embedded||
    !rd(r,x,embedded_child+0xe0,resource)||resource||!rd(r,x,embedded_child+0xd8,resource)||resource)return false;
 if(!rd(r,x,c.type7+0x50,particle_list)||!rd(r,x,particle_list+8,count)||count!=1||!rd(r,x,particle_list+16,particle_array)||
    !rd(r,x,particle_array,c.particle)||!rd(r,x,c.particle,kind)||kind!=8||!rd(r,x,c.particle+8,resource)||resource!=embedded_child||
    !rd(r,x,c.particle+0x58,c.wrapper)||!rd(r,x,c.wrapper,kind)||kind!=7||!rd(r,x,c.wrapper+8,c.state)||
    !rd(r,x,c.state+0x50,kind)||kind!=1||!rd(r,x,c.state+0x88,c.reference)||!rd(r,x,c.state+0x90,c.parent)||
    !rd(r,x,c.reference,resource)||!rd(r,x,resource,kind)||kind!=2||!rd(r,x,c.reference+8,c.sdk)||!c.sdk||c.sdk==a.context.sdk||
    !rd(r,x,c.sdk+0x28,c.root)||!c.root||!rd(r,x,c.root+8,resource)||resource!=c.parent)return false;
 c.token=life.token(c.state);if(!c.token.birth||!c.token.binding)return false;
 // Synthetic parent is independently placed by native subtype7 update. It need
 // not be a child of the outer graph: its native local matrix carries that world.
 for(auto p:{a.context.synthetic_root,c.embedded,embedded_child,c.parent,c.root,c.type7,c.particle,c.wrapper,c.state}){
  if(!p||std::find(c.graph.begin(),c.graph.begin()+c.graph_count,p)!=c.graph.begin()+c.graph_count)return false;c.graph[c.graph_count++]=p;
 }
 uint64_t child_flat{},child_nodes{},first{};
 if(!rd(r,x,c.sdk+0x30,child_flat)||!rd(r,x,child_flat+8,count)||count!=3||!rd(r,x,child_flat+16,child_nodes)||
    !rd(r,x,child_nodes,first)||first!=c.root)return false;
 for(size_t i=1;i<3;i++){uint64_t p{};if(!rd(r,x,child_nodes+i*8,p)||!p||
    std::find(c.graph.begin(),c.graph.begin()+c.graph_count,p)!=c.graph.begin()+c.graph_count)return false;c.graph[c.graph_count++]=p;}
 if(!life.associate(c.state,c.sdk))return false;c.active=true;out=c;return true;
}
inline constexpr std::array<uint16_t,0> no_palette{};
inline constexpr std::array<catherine::private_root_profiles::RigidMember,6> child_rigid={{{0,2,30},{1,2,30},{2,2,30},{3,2,30},{4,2,30},{5,2,30}}};
// Local value descriptor used only AFTER exact ancestry+life capture. This is not
// inserted into the public owner/profile lookup and cannot admit a character.
inline const RootProfile child_profile{20,0,1,"c20_00_bracelet.GFS","2ef20500302703037d2068812aaacc5a8b6c8720ad0bf300c991bbffb29fe3c7",3,6,0,6,18,0,"RootNode","",no_palette,child_rigid};
}
