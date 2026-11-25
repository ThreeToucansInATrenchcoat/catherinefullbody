#pragma once
#include "movement_translation.h"
#include <algorithm>

namespace catherine::visual::facing {
using movement::Position;
constexpr double pi=3.14159265358979323846;
inline double difference(double a,double b){return std::remainder(a-b,2*pi);}
inline bool quaternion_yaw(const float* q,double& yaw){
  double n=0;for(int i=0;i<4;++i){if(!std::isfinite(q[i]))return false;n+=double(q[i])*q[i];}
  if(n<.5||n>1.5)return false;
  // Only the positively observed upright character facing is admitted.
  if(std::abs(q[0])>1e-5||std::abs(q[2])>1e-5)return false;
  yaw=std::atan2(-2*double(q[3])*q[1]/n,1-2*double(q[1])*q[1]/n);
  return std::isfinite(yaw);
}
struct Correction {
  double angle=0;
  Position pivot{};
  bool active=false;
};
class History {
  uint64_t owner_=0,observed_=0,changed_=0;
  double previous_=0,current_=0;
  bool segment_=false;
public:
  void reset(){*this=History{};}
  void publish(uint64_t owner,double old_yaw,double new_yaw,uint64_t clock){
    movement::Clock c;double gap;
    if(!owner||!std::isfinite(old_yaw)||!std::isfinite(new_yaw)||!movement::decode_clock(clock,c)||!c.enabled){reset();return;}
    // A stationary character may not publish another orientation for seconds.
    // The exact native old value and owner still have to match. Only the new
    // segment is rendered; an idle gap is never extrapolated across.
    if(owner_!=owner||!movement::elapsed_ticks(observed_,clock,65536,gap)||std::abs(difference(old_yaw,current_))>1e-5){
      owner_=owner;observed_=changed_=clock;previous_=current_=new_yaw;segment_=false;return;
    }
    observed_=clock;
    if(std::abs(difference(new_yaw,current_))<1e-7)return;
    if(!segment_||changed_!=clock)previous_=old_yaw;
    current_=new_yaw;changed_=clock;segment_=true;
  }
  Correction sample(uint64_t owner,double represented,uint64_t clock,const Position& pivot,bool enabled)const{
    Correction out;double age;
    if(!enabled||!owner||owner!=owner_||!segment_||!std::isfinite(represented)||
       std::abs(difference(represented,current_))>1e-5||!movement::elapsed_ticks(changed_,clock,2,age)||age>=1)return out;
    out.angle=difference(previous_,current_)*(1-age);out.pivot=pivot;out.active=std::abs(out.angle)>1e-7;return out;
  }
};
// Left-multiply the same upright world correction into current-frame skin and
// rigid copies. Native animation, actor orientation and gameplay stay intact.
inline bool apply(const Correction& r,const movement::Translation& t,uint64_t owner,
                  const void* source,void* destination,size_t bytes,size_t count,bool palette){
  if(!source||!destination||!owner||t.owner_lifetime!=owner||!count||bytes!=count*64)return false;
  if(!r.active)return movement::translate(t,owner,source,destination,bytes,count,palette?std::array<size_t,3>{3,7,11}:std::array<size_t,3>{12,13,14});
  if(!std::isfinite(r.angle))return false;
  const double c=std::cos(r.angle),s=std::sin(r.angle);
  auto transform=[&](const unsigned char* src,unsigned char* dst){
    float in[16],out[16];std::memcpy(in,src,64);std::memcpy(out,in,64);
    for(int col=0;col<4;++col){
      const int x=palette?col:col*4,z=palette?8+col:col*4+2;
      double a=in[x],b=in[z];
      if(col==3){a-=r.pivot[0];b-=r.pivot[2];}
      double xx=c*a-s*b,zz=s*a+c*b;
      if(col==3){xx+=r.pivot[0]+t.delta[0];zz+=r.pivot[2]+t.delta[2];}
      if(!std::isfinite(xx)||!std::isfinite(zz)||std::abs(xx)>std::numeric_limits<float>::max()||std::abs(zz)>std::numeric_limits<float>::max())return false;
      out[x]=float(xx);out[z]=float(zz);
    }
    const int y=palette?7:13;
    const double yy=double(in[y])+t.delta[1];
    if(!std::isfinite(yy)||std::abs(yy)>std::numeric_limits<float>::max())return false;
    out[y]=float(yy);if(dst)std::memcpy(dst,out,64);return true;
  };
  const auto* src=static_cast<const unsigned char*>(source);
  for(size_t i=0;i<count;++i)if(!transform(src+i*64,nullptr))return false;
  // Each transform reads its complete source matrix before writing, including
  // exact in-place use. Partial overlap is rejected rather than guessed.
  auto* dst=static_cast<unsigned char*>(destination);
  const auto a=reinterpret_cast<uintptr_t>(src),b=reinterpret_cast<uintptr_t>(dst);
  if(a!=b&&((a<b&&b-a<bytes)||(b<a&&a-b<bytes)))return false;
  for(size_t i=0;i<count;++i)if(!transform(src+i*64,dst+i*64))return false;
  return true;
}
}
