#pragma once
#include "character_pose_coherence.h"
#include <array>

namespace catherine::visual::babel {
using Matrix = std::array<float,16>;
inline Matrix transpose(const Matrix& m) {
 Matrix r;for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)r[4*i+j]=m[4*j+i];return r;
}
// Native node matrices precede inverse binds. Interpolate these, rather than
// independently rotating already bound skin matrices or guessing head parts.
struct NodeHistory {
 Matrix previous{},current{},raw{},output{};
 uint64_t frame=0;uint32_t tick=0;
 double time=0,changed_at=0;float phase=0;
 bool valid=false,ready=false,native_fast=false;
 static bool equivalent(const Matrix&a,const Matrix&b){
  for(unsigned i=0;i<12;++i)if(std::abs(a[i]-b[i])>(i%4==3?.002f:.00001f))return false;
  return true;
 }
 const Matrix& sample(const Matrix& value,uint64_t new_frame,uint64_t clock) {
  const uint32_t now=uint32_t(clock>>32),fraction=uint32_t(clock)&0x7fffffffu;
  float alpha;std::memcpy(&alpha,&fraction,4);
  auto reset=[&]{previous=current=raw=output=value;frame=new_frame;tick=now;phase=alpha;time=changed_at=0;valid=true;ready=false;native_fast=false;};
  bool finite=true;for(float f:value)finite&=std::isfinite(f);
  if(!(clock&0x80000000u)||!std::isfinite(alpha)||alpha<0||alpha>1||!finite){valid=false;output=value;return output;}
  if(!valid||new_frame<frame||new_frame-frame>8||uint32_t(now-tick)>1){reset();return output;}
  if(new_frame==frame)return std::memcmp(raw.data(),value.data(),64)?value:output;
  float distance=0;for(unsigned i:{3u,7u,11u}){float d=value[i]-current[i];distance+=d*d;}
  if(distance>200.f*200.f){reset();return output;}
  const double elapsed=double(uint32_t(now-tick))+double(alpha)-phase;
  if(elapsed<-.00001||elapsed>1.2){reset();return output;}
  time+=std::max(0.,elapsed);tick=now;phase=alpha;
  // Follow actual source publications, including 30 Hz samples arriving late
  // within a clock tick. An update inside a tick alone does not prove 60 Hz.
  // Two successive source changes closer than 3/4 of a tick do prove a faster
  // channel, whose existing samples must pass through unchanged thereafter.
  const bool changed=!equivalent(value,current);
  if(changed){native_fast=ready&&time-changed_at<.75&&time-changed_at>.001;
   previous=current;current=value;changed_at=time;ready=true;}
  frame=new_frame;raw=value;
  if(!ready||native_fast||previous==current){output=value;return output;}
  const float blend=float(std::clamp(time-changed_at,0.,1.));
  blend_matrix(previous.data(),current.data(),blend,output.data());return output;
 }
};
inline bool supported(uint32_t id,uint32_t variant,uint32_t count) {
 if(variant==31)return id==2&&count==74;
 if(variant!=30)return false;
 switch(id){
 case 2:return count==91;case 3:return count==167;case 4:return count==131;
 case 5:return count==249;case 6:return count==114;case 7:return count==80;
 case 8:return count==174;case 9:return count==107;
 case 14:case 15:return count==126;case 19:return count==186;case 20:return count==221;
 default:return false;
 }
}
}
