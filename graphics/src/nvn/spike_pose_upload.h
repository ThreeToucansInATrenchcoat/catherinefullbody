#pragma once
#include "character_pose_interpolation.h"
#include "spike_pose_interpolation.h"
namespace nx::nvn::replay {
const uint8_t *spike_gpu_palette(const uint8_t *,uint64_t,const ShaderInfo &);
inline uint64_t spike_source_hash(const uint8_t *bytes,uint64_t size){
 uint64_t h=14695981039346656037ull;for(uint64_t i=0;i<size;++i){h^=bytes[i];h*=1099511628211ull;}return h;
}
inline bool spike_geometry_signature(uint64_t hash,uint32_t count,uint32_t stride,uint32_t matrices){
 // c001_011.GMD: independently decoded28-entry joint section and three
 // tangent meshes360/500/400; native probe pins their exact immutable streams.
 if(stride!=80||matrices!=28)return false;
 return (count==360&&hash==0xd9967b9fc9c5fa9bull)||
        (count==500&&hash==0x5fbaa1338d4bdbafull)||
        (count==400&&hash==0xf521701979c3bc83ull);
}
inline bool spike_source_accepted(const uint8_t *source,uint64_t bytes,uint32_t count,uint32_t stride){
 if(!source||stride!=80||bytes!=uint64_t(count)*stride)return false;
 unsigned index=count==360?0:count==500?1:count==400?2:3;if(index==3)return false;
 // Exact immutable content cache: vectorized equality on repeat draws, serial
 // FNV only when contents are first seen/changed. No address-lifetime guess.
 static std::array<std::vector<uint8_t>,3> accepted;
 auto &entry=accepted[index];if(entry.size()==bytes&&!std::memcmp(entry.data(),source,bytes))return true;
 if(!spike_geometry_signature(spike_source_hash(source,bytes),count,stride,28))return false;
 entry.assign(source,source+bytes);return true;
}
inline const uint8_t *spike_pose_palette(const uint8_t *palette,uint64_t palette_size,
 const uint8_t *source,uint64_t source_size,uint32_t count,uint32_t stride){
#if defined(CATHERINE_HIGH_REFRESH_BUILD)
 if(!palette||!source||palette_size!=28*64||stride!=80||source_size!=uint64_t(count)*stride||
    (count!=360&&count!=500&&count!=400))return palette;
 if(!spike_source_accepted(source,source_size,count,stride))return palette;
 static catherine::visual::SpikePoseInterpolator history;
 static catherine::visual::ModelInstanceTracker instances;
 const uint64_t frame=puzzle_render_frame(),clock=puzzle_render_clock();
 if(!(clock&0x80000000u)){history.clear();instances.clear();return palette;}
 float root[16];std::memcpy(root,palette,64);
 for(size_t k=0;k<12;++k)if(!std::isfinite(root[k]))return palette;
 // All three material meshes share one native palette and one interpolated
 // palette history. Geometry upload addresses/palette buffers rotate by pass.
 const uint64_t identity=instances.resolve(0xc001011ull,root,frame);
 const auto *result=history.render(true,identity,palette,palette_size,frame,clock);
 if(result!=palette)written_pose_palette_interpolated();
#ifdef CATHERINE_SPIKE_DIAGNOSTICS
 if(result!=palette && std::memcmp(result,palette,palette_size)){
  struct Probe{uint64_t frame=~0ull,changed=0,held=0,last=0;std::array<uint8_t,28*64> raw{},out{};};
  static std::unordered_map<uint64_t,Probe> probes;auto &p=probes[identity];
  if(p.frame!=frame){++p.changed;
   if(p.frame!=~0ull&&!std::memcmp(p.raw.data(),palette,palette_size)&&std::memcmp(p.out.data(),result,palette_size))++p.held;
   p.frame=frame;std::memcpy(p.raw.data(),palette,palette_size);std::memcpy(p.out.data(),result,palette_size);
   if(frame>p.last+30){p.last=frame;float delta=0;
    for(unsigned j=0;j<28*16;j+=16)for(unsigned k=0;k<12;++k){float a,b;std::memcpy(&a,palette+4*(j+k),4);std::memcpy(&b,result+4*(j+k),4);delta=std::max(delta,std::fabs(a-b));}
    log("SpikeBlend frame=%llu tick=%llu instance=%llu vertices=%u changed=%llu held_intermediate=%llu max_delta=%.6f",(unsigned long long)frame,(unsigned long long)(clock>>32),(unsigned long long)identity,count,(unsigned long long)p.changed,(unsigned long long)p.held,delta);}}
 }
#endif
 return result;
#else
 return palette;
#endif
}
}
