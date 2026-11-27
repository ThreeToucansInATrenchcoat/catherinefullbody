#include "C:/catherine/native-gpu-experiment/Source/native_byte_equal.h"
#pragma once
#include "character_pose_interpolation.h"
#include "spike_pose_interpolation.h"
namespace catherine::visual {
// c001_016.GMD only: native 3-matrix WORLD palette and its sole skin mesh.
// Exact content prevents unrelated characters or block variants entering history.
class CampaignBlockPaletteHistory {
 std::vector<uint8_t> accepted;
 ModelInstanceTracker instances;
 SpikePoseInterpolator history;
public:
 void clear(){instances.clear();history.clear();}
 bool accepts(const uint8_t*source,uint64_t bytes,uint32_t count,uint32_t stride,uint64_t palette_bytes){
  if(!source||count!=2989||stride!=80||bytes!=239120||palette_bytes!=192)return false;
  if(accepted.size()==bytes&&experimental_bytes::equal(accepted.data(),source,bytes))return true;
  uint64_t hash=14695981039346656037ull;for(uint64_t i=0;i<bytes;++i)hash=(hash^source[i])*1099511628211ull;
  if(hash!=0xfb95b0d7ae4c66a0ull)return false;
  accepted.assign(source,source+bytes);return true;
 }
 const uint8_t*render(const uint8_t*palette,uint64_t palette_bytes,const uint8_t*source,uint64_t source_bytes,uint32_t count,uint32_t stride,uint64_t frame,uint64_t clock){
  if(!palette||!accepts(source,source_bytes,count,stride,palette_bytes))return palette;
  if(!(clock&0x80000000u)){clear();return palette;}
  float root[16];std::memcpy(root,palette,64);
  for(unsigned j=0;j<3;++j)for(unsigned k=0;k<12;++k){float value;std::memcpy(&value,palette+j*64+k*4,4);if(!std::isfinite(value)){clear();return palette;}}
  const uint64_t identity=instances.resolve(0xc001016ull,root,frame);
  return history.render(true,identity,palette,palette_bytes,frame,clock);
 }
};
}
namespace nx::nvn::replay {
const uint8_t*campaign_block_gpu_palette(const uint8_t*,uint64_t,const ShaderInfo&);
inline const uint8_t*campaign_block_pose_palette(const uint8_t*palette,uint64_t palette_bytes,const uint8_t*source,uint64_t source_bytes,uint32_t count,uint32_t stride){
#if defined(CATHERINE_HIGH_REFRESH_BUILD)
 static catherine::visual::CampaignBlockPaletteHistory history;
 const auto*out=history.render(palette,palette_bytes,source,source_bytes,count,stride,puzzle_render_frame(),puzzle_render_clock());
 if(out!=palette)written_pose_palette_interpolated();
 return out;
#else
 return palette;
#endif
}
}
