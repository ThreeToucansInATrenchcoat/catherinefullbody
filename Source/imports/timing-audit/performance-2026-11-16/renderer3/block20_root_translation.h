#pragma once
#include <array>
#include <vector>
#include <deque>
#include <algorithm>
#include <unordered_map>
#include <cstdint>
#include <cstring>
#include <cmath>
namespace catherine::visual {
// Standalone math only. Caller must validate native owner/lifetime and world root.
// Production requires nonzero host lifetime; standalone lifetime0 remains a key.
// clock is monotonic PRESENTATION/replay time, never a lagging native packet clock.
// This token is never inferred from palette, spatial proximity, or row pointers.
struct Block20Owner {uint64_t owner=0,generation=0,model=0,parent=0,lifetime=0;
 bool operator==(const Block20Owner&)const=default;
 bool valid()const{return owner&&model&&parent;}};
class Block20RootTranslation {
 struct Hash {size_t operator()(const Block20Owner&k)const{return size_t(k.owner^(k.generation*0x9e3779b97f4a7c15ull)^(k.model<<7)^(k.parent>>3)^(k.lifetime*0xd6e8feb86659fd93ull));}};
 struct Sample {double time;std::array<float,3> root;};
 struct State {uint64_t frame=0;uint32_t tick=0;float fraction=0;double time=0,last_change=0;bool moving=false;
  std::array<float,3> raw{},target{};std::deque<Sample> samples;std::array<float,320> output{};};
 std::unordered_map<Block20Owner,State,Hash> states;
 uint64_t last_pruned=0;
 std::array<std::vector<uint8_t>,2> accepted;
 static bool finite_root(const std::array<float,3>&r){for(float x:r)if(!std::isfinite(x))return false;return true;}
public:
 void clear(){states.clear();}
 static bool accepts(const uint8_t*src,uint64_t bytes,uint32_t count,uint32_t stride,uint64_t palette_bytes){
  uint64_t expected=count==1457&&stride==64&&bytes==93248?0x0aef33036766a217ull:count==393&&stride==80&&bytes==31440?0x4a71a10d4a67b03eull:0;
  if(!src||!expected||palette_bytes!=1280)return false;
  uint64_t hash=14695981039346656037ull;for(uint64_t i=0;i<bytes;++i)hash=(hash^src[i])*1099511628211ull;return hash==expected;
 }
 const uint8_t*render(bool validated,Block20Owner owner,const std::array<float,3>&root,
 const uint8_t*palette,uint64_t palette_bytes,const uint8_t*source,uint64_t source_bytes,uint32_t count,uint32_t stride,uint64_t frame,uint64_t clock){
  auto reject=[&](){if(owner.valid())states.erase(owner);return palette;};
  unsigned material=count==1457&&stride==64&&source_bytes==93248?0:count==393&&stride==80&&source_bytes==31440?1:2;
  if(!validated||!owner.valid()||!palette||!source||material>1||palette_bytes!=1280||!finite_root(root))return reject();
  auto&cache=accepted[material];if(cache.size()!=source_bytes||std::memcmp(cache.data(),source,source_bytes)){
   if(!accepts(source,source_bytes,count,stride,palette_bytes))return reject();cache.assign(source,source+source_bytes);}
  uint32_t bits=uint32_t(clock)&0x7fffffffu,tick=uint32_t(clock>>32);float fraction;std::memcpy(&fraction,&bits,4);
  for(unsigned j=0;j<20;++j)for(unsigned k=0;k<12;++k){float v;std::memcpy(&v,palette+j*64+k*4,4);if(!std::isfinite(v))return reject();}
  if(states.size()>1024)states.clear();
  if(frame>last_pruned+64){last_pruned=frame;for(auto i=states.begin();i!=states.end();)if(frame>i->second.frame&&frame-i->second.frame>80)i=states.erase(i);else ++i;}
  auto [it,fresh]=states.try_emplace(owner);State&s=it->second;
  auto reset=[&](){s.frame=frame;s.tick=tick;s.fraction=fraction;s.time=0;s.last_change=0;s.moving=false;s.raw=s.target=root;s.samples.clear();s.samples.push_back({0,root});return palette;};
  // First material owns one presentation target for this owner/present. Later
  // material packet clocks may lag/disagree; they cannot advance the history.
  if(!fresh&&frame==s.frame){/* cached target below; current child palette remains live */}
  else {
   if(!(clock&0x80000000u)||!std::isfinite(fraction)||fraction<0||fraction>1){states.erase(it);return palette;}
   if(fresh)return reset();
   uint32_t ticks=tick-s.tick;double dt=double(ticks)+double(fraction)-s.fraction;
   if(frame<s.frame||frame>s.frame+2||ticks>2||dt<0||dt>1.0)return reset();
   s.frame=frame;s.tick=tick;s.fraction=fraction;s.time+=dt;
   if(dt>0&&root!=s.raw){
    double distance=0;for(unsigned k=0;k<3;++k){double d=double(root[k])-s.raw[k];distance+=d*d;}
    double interval=s.time-s.last_change;
    if(distance>200.*200.||(s.moving&&interval<.5-1e-5))return reset();
    // For interval>delay the OLD trajectory is settled at THIS targettime;
    // a synthetic held sample starts the next motion without rewinding.
    if(!s.moving||interval>1.5+1e-5){s.samples.clear();s.samples.push_back({s.time-1.0,s.raw});s.moving=true;}
    s.samples.push_back({s.time,root});s.last_change=s.time;s.raw=root;
   }
   const double target_time=s.time-1.5; // fixed 50ms presentation delay
   while(s.samples.size()>2&&s.samples[1].time<=target_time)s.samples.pop_front();
   if(target_time<=s.samples.front().time)s.target=s.samples.front().root;
   else if(target_time>=s.samples.back().time)s.target=s.samples.back().root;
   else {for(size_t j=1;j<s.samples.size();++j)if(target_time<=s.samples[j].time){auto&a=s.samples[j-1];auto&b=s.samples[j];double t=(target_time-a.time)/(b.time-a.time);for(unsigned k=0;k<3;++k)s.target[k]=float(double(a.root[k])*(1.-t)+double(b.root[k])*t);break;}}
  }
  if(!s.moving)return palette;
  std::memcpy(s.output.data(),palette,1280);
  for(unsigned k=0;k<3;++k){double offset=double(s.target[k])-root[k];for(unsigned j=0;j<20;++j){unsigned p=j*16+k*4+3;s.output[p]=float(double(s.output[p])+offset);}}
  return reinterpret_cast<const uint8_t*>(s.output.data());
 }
};
}
