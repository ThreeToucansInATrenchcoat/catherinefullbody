#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>
namespace catherine::visual {
// Only callers with an audited trap geometry signature may enter this history.
// Palettes already include world placement. Blend them once; leave the native
// c3 model transform untouched, just as the existing skin-output path requires.
class SpikePoseInterpolator {
 struct State { uint64_t frame=~0ull; uint32_t tick=0; bool ready=false;
   std::vector<float> previous,current,output,frame_input; };
 std::unordered_map<uint64_t,State> states;
 uint64_t last_pruned=0;
public:
 void clear(){states.clear();}
 const uint8_t *render(bool audited,uint64_t identity,const uint8_t *input,
                       size_t bytes,uint64_t frame,uint64_t clock) {
  if(!audited || !identity || !input || bytes<64 || bytes>256*64 || bytes%64) return input;
  const uint32_t bits=uint32_t(clock)&0x7fffffffu, tick=uint32_t(clock>>32);
  float alpha;std::memcpy(&alpha,&bits,4);
  if(!(clock&0x80000000u)||!std::isfinite(alpha)||alpha<0||alpha>1){states.erase(identity);return input;}
  const float *src=reinterpret_cast<const float*>(input);const size_t words=bytes/4;
  for(size_t j=0;j<words;j+=16)for(size_t k=0;k<12;++k)
   if(!std::isfinite(src[j+k])){states.erase(identity);return input;}
  if(frame>last_pruned+64){last_pruned=frame;for(auto it=states.begin();it!=states.end();)
   if(frame>it->second.frame && frame-it->second.frame>80)it=states.erase(it);else ++it;}
  if(states.size()>4096)states.clear();
  auto &s=states[identity];
  auto reset=[&](){s.current.assign(src,src+words);s.previous=s.current;s.output=s.current;
   s.frame_input=s.current;s.frame=frame;s.tick=tick;s.ready=false;};
  if(s.current.size()!=words||frame<s.frame||frame>s.frame+80){reset();return input;}
  if(s.frame==frame)return std::memcmp(input,s.frame_input.data(),bytes)==0?
   reinterpret_cast<const uint8_t*>(s.output.data()):input;
  double distance=0;for(unsigned k:{3u,7u,11u}){double d=double(src[k])-s.current[k];distance+=d*d;}
  if(distance>200.*200.||uint32_t(tick-s.tick)>1){reset();return input;}
  s.frame=frame;s.frame_input.assign(src,src+words);
  if(tick!=s.tick){s.previous=s.current;s.current.assign(src,src+words);s.tick=tick;s.ready=true;}
  if(!s.ready){s.output.assign(src,src+words);return input;}
  s.output=s.current;
  for(size_t j=0;j<words;j+=16)for(size_t k=0;k<12;++k)
   s.output[j+k]=float(double(s.previous[j+k])*(1.-double(alpha))+double(s.current[j+k])*double(alpha));
  return reinterpret_cast<const uint8_t*>(s.output.data());
 }
};
}
