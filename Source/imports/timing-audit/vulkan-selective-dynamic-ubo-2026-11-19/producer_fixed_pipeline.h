#pragma once
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>
#include "producer_pipeline.h"
namespace nx::nvn { VkStencilOp vk_stencil_op(uint32_t); }
namespace nx::nvn::producer_fixed {
constexpr uint64_t packet_tag=0x31584650504b5655ull;
enum class Kind:uint32_t { None,Blend,Color,Masks,Depth,Polygon,DepthClamp,Multisample };
enum Valid:uint32_t { BlendBits=255,ColorBit=1u<<8,MasksBit=1u<<9,DepthBit=1u<<10,PolygonBit=1u<<11,ClampBit=1u<<12,MultisampleBit=1u<<13,AllBits=(1u<<14)-1 };
struct Header { Kind kind;uint32_t bytes; };
struct Depth {
    VkBool32 test=0,write=0,stencil=0;
    VkCompareOp compare=VK_COMPARE_OP_ALWAYS;
    VkStencilOpState front{},back{};
    VkBool32 slack=0;
};
struct Polygon { VkPolygonMode mode=VK_POLYGON_MODE_POINT;VkCullModeFlags cull=VK_CULL_MODE_NONE;VkFrontFace front=VK_FRONT_FACE_CLOCKWISE;VkBool32 offset=0; };
struct Inputs {
    VkPipelineColorBlendAttachmentState blend[8]{};
    Depth depth;
    Polygon polygon;
    VkBool32 depth_clamp=0,alpha_to_coverage=0;
};
struct BlendPayload { uint32_t target;VkPipelineColorBlendAttachmentState value; };
struct EightValues { uint32_t value[8]; };
static_assert(std::is_trivially_copyable_v<Inputs>);
// perf 2026-11-22: default 0 (legacy reference path). The producer lowering cost ~30 FPS at the
// Stage 9/6 boss and ~12 FPS in the Day 5 bar; set the variable to 1 (or 2 to verify) to re-enable.
inline int mode(){static const int m=[] {const char* p=std::getenv("CATHERINE_VULKAN_PRODUCER_FIXED_PIPELINE");return p?std::atoi(p):0;}();return m;}
inline VkPipelineColorBlendAttachmentState factors(const BlendTarget& b){
    VkPipelineColorBlendAttachmentState a{};
    a.srcColorBlendFactor=vk_blend_factor(b.src_rgb);a.dstColorBlendFactor=vk_blend_factor(b.dst_rgb);a.colorBlendOp=vk_blend_op(b.eq_rgb);
    a.srcAlphaBlendFactor=vk_blend_factor(b.src_a);a.dstAlphaBlendFactor=vk_blend_factor(b.dst_a);a.alphaBlendOp=vk_blend_op(b.eq_a);return a;
}
inline Depth convert_depth(const DepthStencilInfo& d){
    Depth out;out.test=d.depth_test?VK_TRUE:VK_FALSE;out.write=d.depth_write?VK_TRUE:VK_FALSE;out.stencil=d.stencil_test?VK_TRUE:VK_FALSE;
    out.compare=d.depth_func==3&&!d.depth_write?VK_COMPARE_OP_LESS_OR_EQUAL:vk_compare(d.depth_func);
    VkStencilOpState* face[2]={&out.front,&out.back};
    for(unsigned i=0;i<2;++i){face[i]->failOp=vk_stencil_op(d.stencil_fail[i]);face[i]->depthFailOp=vk_stencil_op(d.depth_fail[i]);face[i]->passOp=vk_stencil_op(d.depth_pass[i]);face[i]->compareOp=vk_compare(d.stencil_func[i]);face[i]->compareMask=face[i]->writeMask=255;}
    out.slack=equal_slack_pass(d)?VK_TRUE:VK_FALSE;return out;
}
inline Polygon convert_polygon(const PolygonInfo& p){
    const uint32_t bit=p.mode<=2?1u<<p.mode:4u;
    return {p.mode==0?VK_POLYGON_MODE_POINT:p.mode==1?VK_POLYGON_MODE_LINE:VK_POLYGON_MODE_FILL,vk_cull(p.cull),p.front==0?VK_FRONT_FACE_CLOCKWISE:VK_FRONT_FACE_COUNTER_CLOCKWISE,(p.offset_enables&bit)?VK_TRUE:VK_FALSE};
}
inline Inputs legacy(const PipelineKey& k){
    Inputs p;for(unsigned i=0;i<8;++i){p.blend[i]=factors(k.blend[i]);p.blend[i].blendEnable=k.color.blend_enable[i]?VK_TRUE:VK_FALSE;p.blend[i].colorWriteMask=k.mask.mask[i]&15;}
    p.depth=convert_depth(k.depth);p.polygon=convert_polygon(k.polygon);p.depth_clamp=k.depth_clamp?VK_TRUE:VK_FALSE;p.alpha_to_coverage=k.alpha_to_coverage?VK_TRUE:VK_FALSE;return p;
}
inline Kind kind(Cmd::Op op){switch(op){case Cmd::BindBlendState:return Kind::Blend;case Cmd::BindColorState:return Kind::Color;case Cmd::BindChannelMaskState:return Kind::Masks;case Cmd::BindDepthStencilState:return Kind::Depth;case Cmd::BindPolygonState:return Kind::Polygon;case Cmd::SetDepthClamp:return Kind::DepthClamp;case Cmd::BindMultisampleState:return Kind::Multisample;default:return Kind::None;}}
inline uint32_t affected(Kind k){switch(k){case Kind::Blend:return BlendBits;case Kind::Color:return ColorBit;case Kind::Masks:return MasksBit;case Kind::Depth:return DepthBit;case Kind::Polygon:return PolygonBit;case Kind::DepthClamp:return ClampBit;case Kind::Multisample:return MultisampleBit;default:return 0;}}
inline std::atomic<uint64_t> recorded_packets{0};
template<class T> inline void store(Cmd& c,const void*& blob,size_t& bytes,std::vector<uint8_t>& owned,Kind k,const T& p){
    const size_t prefix=blob?bytes:0;Header h{k,sizeof(T)};owned.resize(prefix+sizeof h+sizeof p);if(prefix)std::memcpy(owned.data(),blob,prefix);std::memcpy(owned.data()+prefix,&h,sizeof h);std::memcpy(owned.data()+prefix+sizeof h,&p,sizeof p);
    c.a[5]=prefix;c.a[6]=packet_tag;blob=owned.data();bytes=owned.size();recorded_packets.fetch_add(1,std::memory_order_relaxed);
}
// Producer conversion is pure: no Vulkan device/handle queries. Original decoded-state prefix remains intact.
inline void attach(Cmd& c,const void*& blob,size_t& bytes,std::vector<uint8_t>& owned){
    if(!mode())return;const Kind k=kind(c.op);if(k==Kind::None)return;
    auto has=[&](size_t n){return blob&&bytes>=n;};
    switch(k){
    case Kind::Blend:{if(!has(sizeof(BlendTarget)))return;BlendTarget raw;std::memcpy(&raw,blob,sizeof raw);if(raw.target>=8)return;BlendPayload p{raw.target,factors(raw)};store(c,blob,bytes,owned,k,p);break;}
    case Kind::Color:{if(!has(sizeof(ColorStateInfo)))return;ColorStateInfo raw;std::memcpy(&raw,blob,sizeof raw);EightValues p{};for(unsigned i=0;i<8;++i)p.value[i]=raw.blend_enable[i]?VK_TRUE:VK_FALSE;store(c,blob,bytes,owned,k,p);break;}
    case Kind::Masks:{if(!has(sizeof(ChannelMaskInfo)))return;ChannelMaskInfo raw;std::memcpy(&raw,blob,sizeof raw);EightValues p{};for(unsigned i=0;i<8;++i)p.value[i]=raw.mask[i]&15;store(c,blob,bytes,owned,k,p);break;}
    case Kind::Depth:{if(!has(sizeof(DepthStencilInfo)))return;DepthStencilInfo raw;std::memcpy(&raw,blob,sizeof raw);const auto p=convert_depth(raw);store(c,blob,bytes,owned,k,p);break;}
    case Kind::Polygon:{if(!has(sizeof(PolygonInfo)))return;PolygonInfo raw;std::memcpy(&raw,blob,sizeof raw);const auto p=convert_polygon(raw);store(c,blob,bytes,owned,k,p);break;}
    case Kind::DepthClamp:case Kind::Multisample:{const VkBool32 p=c.n?VK_TRUE:VK_FALSE;store(c,blob,bytes,owned,k,p);break;}
    default:break;
    }
}
struct Shadow { Inputs values=legacy(PipelineKey{});uint32_t valid=AllBits; };
inline Shadow state; // Replay owns state, and called lists inherit without a reset.
inline uint64_t applied_packets=0,unsupported_packets=0,prepared_inputs=0,validated_inputs=0;
inline void apply(const Cmd& c,const CmdList& list){
    if(!mode())return;const Kind k=kind(c.op);if(k==Kind::None)return;const auto unavailable=[&]{state.valid&=~affected(k);++unsupported_packets;};
    if(c.a[6]!=packet_tag||c.blob>=list.blobs.size()){unavailable();return;}
    const auto& b=list.blobs[c.blob];const size_t off=size_t(c.a[5]);if(off>b.size()||sizeof(Header)>b.size()-off){unavailable();return;}
    Header h;std::memcpy(&h,b.data()+off,sizeof h);if(h.kind!=k||h.bytes>b.size()-off-sizeof h){unavailable();return;}const auto* p=b.data()+off+sizeof h;
    auto copy=[&](auto& value){if(h.bytes!=sizeof value)return false;std::memcpy(&value,p,sizeof value);return true;};
    switch(k){
    case Kind::Blend:{BlendPayload v;if(!copy(v)||v.target>=8){unavailable();return;}auto& a=state.values.blend[v.target];const auto enable=a.blendEnable;const auto mask=a.colorWriteMask;a=v.value;a.blendEnable=enable;a.colorWriteMask=mask;state.valid|=1u<<v.target;break;}
    case Kind::Color:{EightValues v;if(!copy(v)){unavailable();return;}for(unsigned i=0;i<8;++i)state.values.blend[i].blendEnable=v.value[i];state.valid|=ColorBit;break;}
    case Kind::Masks:{EightValues v;if(!copy(v)){unavailable();return;}for(unsigned i=0;i<8;++i)state.values.blend[i].colorWriteMask=v.value[i];state.valid|=MasksBit;break;}
    case Kind::Depth:if(!copy(state.values.depth)){unavailable();return;}state.valid|=DepthBit;break;
    case Kind::Polygon:if(!copy(state.values.polygon)){unavailable();return;}state.valid|=PolygonBit;break;
    case Kind::DepthClamp:if(!copy(state.values.depth_clamp)){unavailable();return;}state.valid|=ClampBit;break;
    case Kind::Multisample:if(!copy(state.values.alpha_to_coverage)){unavailable();return;}state.valid|=MultisampleBit;break;
    default:return;
    }++applied_packets;
}
inline Inputs prepare(const PipelineKey& key){
    if(!mode())return legacy(key);Inputs p=state.values;
    if(state.valid!=AllBits){const auto old=legacy(key);for(unsigned i=0;i<8;++i){const auto enable=p.blend[i].blendEnable;const auto mask=p.blend[i].colorWriteMask;if(!(state.valid>>i&1)){p.blend[i]=old.blend[i];p.blend[i].blendEnable=enable;p.blend[i].colorWriteMask=mask;}if(!(state.valid&ColorBit))p.blend[i].blendEnable=old.blend[i].blendEnable;if(!(state.valid&MasksBit))p.blend[i].colorWriteMask=old.blend[i].colorWriteMask;}if(!(state.valid&DepthBit))p.depth=old.depth;if(!(state.valid&PolygonBit))p.polygon=old.polygon;if(!(state.valid&ClampBit))p.depth_clamp=old.depth_clamp;if(!(state.valid&MultisampleBit))p.alpha_to_coverage=old.alpha_to_coverage;}
    ++prepared_inputs;return p;
}
inline void validate(const PipelineKey& key,const Inputs& p){if(mode()!=2)return;const auto old=legacy(key);if(std::memcmp(&old,&p,sizeof p))fatal("Producer Vulkan fixed pipeline input disagrees with compatibility key");if(++validated_inputs==1||validated_inputs%10000==0)log("Producer Vulkan fixed pipeline verified: %llu requests, %llu packet applications, %llu unsupported",(unsigned long long)validated_inputs,(unsigned long long)applied_packets,(unsigned long long)unsupported_packets);}
// Attachment format is known only on the GPU owner. Apply its correction to already-native factors.
inline VkPipelineColorBlendAttachmentState attachment(VkPipelineColorBlendAttachmentState p,bool ufloat){
    if(!ufloat)return p;auto factor=[](VkBlendFactor f){return f==VK_BLEND_FACTOR_DST_ALPHA?VK_BLEND_FACTOR_ONE:(f==VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA||f==VK_BLEND_FACTOR_SRC_ALPHA_SATURATE)?VK_BLEND_FACTOR_ZERO:f;};p.srcColorBlendFactor=factor(p.srcColorBlendFactor);p.dstColorBlendFactor=factor(p.dstColorBlendFactor);p.srcAlphaBlendFactor=factor(p.srcAlphaBlendFactor);p.dstAlphaBlendFactor=factor(p.dstAlphaBlendFactor);return p;
}
}
namespace nx::nvn { VkPipeline graphics_pipeline_native_fixed(const PipelineKey&,const ProgramLayout&,const producer_pipeline::VertexInputs&,const producer_fixed::Inputs&); }
