#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <unordered_map>

// Private value-only prototype. No engine memory access, no SDK writes, and
// no claim that the caller has established native owner/pose provenance.
namespace c03_private {
using Matrix = std::array<float,16>; // canonical column-vector, row-major
struct Owner {
  uint64_t actor{}, model{}, sdk{}, resource{}, root{}, generation{};
  bool operator==(const Owner&) const = default;
  bool valid() const { return actor && model && sdk && resource && root && generation; }
};
struct OwnerHash {
  size_t operator()(const Owner& k) const {
    size_t h=0; for(auto v:{k.actor,k.model,k.sdk,k.resource,k.root,k.generation})
      h^=size_t(v)+size_t(0x9e3779b97f4a7c15ull)+(h<<6)+(h>>2);
    return h;
  }
};
// One copied atomic camera_render_clock word is the only time identity.
// Epoch/continuity are explicit monotonic adapter reset serials, not clock reads.
struct Clock { uint64_t token{},epoch{},continuity{}; bool native60{}; };
struct Root {
  Matrix world{}, parent_world{};
  std::array<float,3> position{};
  std::array<float,4> quaternion{0,0,0,1};
  std::array<float,3> scale{1,1,1};
  bool same_root_parent{}, stable_copy{}, identity_profile{};
};
struct Context { uint64_t epoch{},continuity{}; bool operator==(const Context&) const = default; };
struct Target {
  Owner owner{}; uint64_t frame{}; uint32_t tick{};
  double yaw{}; bool valid{}; Context context{};
};
inline double angle_delta(double a,double b) {
  constexpr double pi=3.14159265358979323846;
  double d=std::remainder(a-b,2*pi);
  return d;
}
inline bool root_yaw(const Root& r,double& yaw) {
  if(!r.same_root_parent || !r.stable_copy || !r.identity_profile) return false;
  if(r.position!=std::array<float,3>{0,0,0} ||
     r.quaternion!=std::array<float,4>{0,0,0,1} ||
     r.scale!=std::array<float,3>{1,1,1}) return false;
  for(unsigned i=0;i<16;i++)
    if(!std::isfinite(r.world[i]) || r.world[i]!=r.parent_world[i]) return false;
  const auto& m=r.world;
  if(m[1]!=0 || m[4]!=0 || m[6]!=0 || m[9]!=0 || m[5]!=1 ||
     m[12]!=0 || m[13]!=0 || m[14]!=0 || m[15]!=1 ||
     m[0]!=m[10] || m[2]!=-m[8]) return false;
  // Prototype tolerance only; the actual native-world fixture must qualify it
  // before an engine admission adapter can use this helper.
  if(std::fabs(double(m[0])*m[0]+double(m[2])*m[2]-1)>4e-6) return false;
  yaw=std::atan2(double(m[2]),double(m[0])); return true;
}
class History {
  struct State {
    uint64_t frame{},continuity{}; uint32_t tick{};
    double previous{},current{};
    bool seeded{},ready{}; Target frozen{};
  };
  std::unordered_map<Owner,State,OwnerHash> states;
  uint64_t epoch{}; size_t limit;
public:
  explicit History(size_t cap=32):limit(cap){}
  size_t size() const {return states.size();}
  bool retire(const Owner& o){return states.erase(o)!=0;}
  void invalidate_actor(uint64_t actor){
    for(auto it=states.begin();it!=states.end();)
      if(it->first.actor==actor)it=states.erase(it);else ++it;
  }
  // Epochs and continuity are explicit monotonic reset serials, not hashes of
  // addresses, wall time, frame spacing, or permission-cache guesses.
  bool reset(uint64_t next_epoch){
    if(!next_epoch || next_epoch<epoch)return false;
    states.clear();epoch=next_epoch;return true;
  }
  void cap(size_t n){limit=n;if(states.size()>limit)states.clear();}
  Target capture(const Owner& owner,const Clock& clock,const Root& root) {
    const uint64_t token=clock.token;
    const uint32_t tick=uint32_t(token>>32),fb=uint32_t(token)&0x7fffffffu;
    float fraction;std::memcpy(&fraction,&fb,4);
    Target out{owner,token,tick,0,false,{clock.epoch,clock.continuity}};
    double raw{};
    if(!owner.valid() || !clock.epoch || !clock.continuity || clock.epoch<epoch)return out;
    if(clock.epoch>epoch)reset(clock.epoch);
    // The source active bit already encodes effective high-refresh timing.
    // Do not mix a later raw-delta or announcement getter into this identity.
    if(!(token&0x80000000ull) || clock.native60 || !std::isfinite(fraction) ||
       fraction<0 || fraction>1 || !root_yaw(root,raw)){
      states.erase(owner);return out;
    }
    auto it=states.find(owner);
    if(it==states.end()){
      if(states.size()>=limit)return out;
      it=states.emplace(owner,State{}).first;
    }
    auto& s=it->second;
    if(s.seeded && clock.continuity<s.continuity)return out;
    if(s.seeded && clock.continuity>s.continuity)s=State{};
    // A packed token is ordered time, not an ordinal frame counter. No numeric
    // "80 frames" subtraction is meaningful. Wrap/rollback requires new epoch.
    if(s.seeded && token<s.frame)return out;
    if(s.seeded && token==s.frame)return s.frozen;
    const uint32_t ticks=tick-s.tick;
    if(!s.seeded || ticks>1){
      s.seeded=true;s.ready=false;s.previous=s.current=raw;
    }else if(ticks==1){
      s.previous=s.current;s.current=raw;s.ready=true;
    }else if(std::fabs(angle_delta(raw,s.current))>1e-7){
      s.previous=s.current=raw;s.ready=false;
    }
    s.frame=token;s.tick=tick;s.continuity=clock.continuity;
    out.yaw=s.ready?s.previous+angle_delta(s.current,s.previous)*fraction:raw;
    out.valid=s.ready;s.frozen=out;return out;
  }
};
// Packet-specific delta: old and new represented roots may share one Target.
// This routine reads no mutable history or engine pointer.
inline bool delta(const Target& target,const Owner& owner,const Context& packet_context,const Root& represented,Matrix& d) {
  double raw{};
  if(!target.valid || !std::isfinite(target.yaw) || target.owner!=owner || target.context!=packet_context || !root_yaw(represented,raw)) return false;
  const double a=angle_delta(target.yaw,raw);
  if(std::fabs(a)<1e-12) return false;
  const double c=std::cos(a),s=std::sin(a);
  const auto& m=represented.world; const double x=m[3],z=m[11];
  Matrix candidate={float(c),0,float(s),float(x-c*x-s*z),0,1,0,0,
     float(-s),0,float(c),float(z+s*x-c*z),0,0,0,1};
  for(float v:candidate) if(!std::isfinite(v)) return false;
  d=candidate;return true;
}
inline bool matrix(const Matrix& d,const float* input,float* output) {
  if(!input || !output)return false;
  Matrix p,result;
  for(unsigned i=0;i<16;i++) {
    p[i]=input[i];if(!std::isfinite(p[i]) || !std::isfinite(d[i]))return false;
  }
  for(unsigned r=0;r<3;r++)for(unsigned c=0;c<4;c++) {
    const double v=double(d[r*4])*p[c]+double(d[r*4+1])*p[4+c]+double(d[r*4+2])*p[8+c]+double(d[r*4+3])*p[12+c];
    result[r*4+c]=float(v);if(!std::isfinite(result[r*4+c]))return false;
  }
  for(unsigned c=0;c<4;c++)result[12+c]=p[12+c];
  std::memcpy(output,result.data(),sizeof(result));return true;
}
inline bool palette(const Matrix& d,const float* input,float* output,size_t matrices) {
  if(!input || !output || matrices!=121) return false;
  for(float v:d) if(!std::isfinite(v)) return false;
  for(size_t i=0;i<matrices*16;i++) if(!std::isfinite(input[i])) return false;
  std::array<float,121*16> scratch;
  for(size_t n=0;n<matrices;n++) {
    if(!matrix(d,input+n*16,scratch.data()+n*16))return false;
  }
  std::memcpy(output,scratch.data(),sizeof(scratch));
  return true;
}
}
