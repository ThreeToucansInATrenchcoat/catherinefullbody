#pragma once
#include <algorithm>
#include <atomic>
#include <climits>
#include <cstring>
#include <type_traits>
#include "native_texture_bindings.h"
namespace nx::nvn::native_transfers {
struct Legacy {
    ViewInfo src_view,dst_view;
    int32_t src[6]{},dst[6]{};
    uint32_t flags=0;
};
static_assert(sizeof(Legacy)==100);
constexpr uint64_t tag=0x31524654564b584eull;
struct Intent {
    uint32_t version=1;
    VkImageCopy copy{};
    VkImageBlit blit{};
    VkExtent3D destination_extent{};
    VkFilter filter=VK_FILTER_NEAREST;
    uint32_t force_blit=0;
};
static_assert(std::is_trivially_copyable_v<Intent>);
inline int mode() {
    static const int value=[] {const auto* s=std::getenv("CATHERINE_VULKAN_IMAGE_TRANSFERS");return s?std::atoi(s):1;}();return value;
}
inline std::atomic<uint64_t> recorded{0};
struct Census {uint64_t packets=0,compared=0,copies=0,blits=0,overlap=0,rejected=0,bound=0,fallback=0;};
inline Census census;
inline bool encode(const Legacy& old,Intent& p) {
    std::memset(&p,0,sizeof p);p.version=1;
    const auto width=std::max(old.src[3],1),height=std::max(old.src[4],1);
    const auto dw=std::max(old.dst[3],1),dh=std::max(old.dst[4],1);
    auto end=[](int32_t x,int32_t size,int32_t& out) {
        const int64_t value=int64_t(x)+size;if(value<INT32_MIN||value>INT32_MAX)return false;out=int32_t(value);return true;
    };
    int32_t sx,sy,dx,dy;
    if(!end(old.src[0],width,sx)||!end(old.src[1],height,sy)||!end(old.dst[0],dw,dx)||!end(old.dst[1],dh,dy))return false;
    p.copy.srcSubresource={0,old.src_view.base_level,uint32_t(std::max(old.src[2],0))+old.src_view.min_layer,uint32_t(std::max(old.src[5],1))};
    p.copy.dstSubresource={0,old.dst_view.base_level,uint32_t(std::max(old.dst[2],0))+old.dst_view.min_layer,uint32_t(std::max(old.dst[5],1))};
    p.copy.srcOffset={old.src[0],old.src[1],old.src[2]};p.copy.dstOffset={old.dst[0],old.dst[1],old.dst[2]};
    p.copy.extent={uint32_t(width),uint32_t(height),uint32_t(std::max(old.src[5],1))};
    p.destination_extent={uint32_t(dw),uint32_t(dh),uint32_t(std::max(old.dst[5],1))};
    p.blit.srcOffsets[0]={old.src[0],old.src[1],0};p.blit.srcOffsets[1]={sx,sy,1};
    p.blit.dstOffsets[0]={old.dst[0],old.dst[1],0};p.blit.dstOffsets[1]={dx,dy,1};
    if(old.flags&4)std::swap(p.blit.dstOffsets[0].x,p.blit.dstOffsets[1].x);
    if(old.flags&8)std::swap(p.blit.dstOffsets[0].y,p.blit.dstOffsets[1].y);
    // Legacy mirror-Z forces the blit path but never swaps Z; preserve that.
    p.force_blit=(old.flags&(4|8|16))!=0;p.filter=(old.flags&1)?VK_FILTER_LINEAR:VK_FILTER_NEAREST;return true;
}
inline bool envelope(const Cmd& command,const CmdList& list) {
    return command.op==Cmd::CopyTextureToTexture&&command.a[8]==tag&&command.a[7]==sizeof(Legacy)&&command.blob<list.blobs.size()&&
        list.blobs[command.blob].size()==sizeof(Legacy)+sizeof(Intent);
}
inline bool read(const Cmd& command,const CmdList& list,Intent& intent) {
    if(!mode()||!envelope(command,list))return false;
    std::memcpy(&intent,list.blobs[command.blob].data()+sizeof(Legacy),sizeof intent);
    return intent.version==1;
}
inline bool attach(Cmd& command,const Legacy& old,std::vector<uint8_t>& blob) {
    if(!mode())return false;Intent p;if(!encode(old,p))return false;
    blob.resize(sizeof old+sizeof p);std::memcpy(blob.data(),&old,sizeof old);std::memcpy(blob.data()+sizeof old,&p,sizeof p);
    command.a[7]=sizeof old;command.a[8]=tag;
    const auto count=recorded.fetch_add(1,std::memory_order_relaxed)+1;
    if(count==1||count%10000==0)log("Native image transfer packets recorded: %llu",(unsigned long long)count);
    return true;
}
struct Prepared {
    VkImageCopy copy{};VkImageBlit blit{};VkFilter filter=VK_FILTER_NEAREST;
    bool direct=false,blittable=false,overlap=false;
};
inline Prepared prepare(const Intent& p,const GpuImage& src,const GpuImage& dst,bool same) {
    Prepared r;r.copy=p.copy;r.blit=p.blit;r.filter=src.aspect==VK_IMAGE_ASPECT_COLOR_BIT?p.filter:VK_FILTER_NEAREST;
    const bool s3=src.view_type==VK_IMAGE_VIEW_TYPE_3D,d3=dst.view_type==VK_IMAGE_VIEW_TYPE_3D;
    auto sub=[](VkImageSubresourceLayers in,const GpuImage& image,bool volume) {
        in.aspectMask=image.aspect;in.mipLevel=std::min(in.mipLevel,image.levels-1);
        in.baseArrayLayer=volume?0:std::min(in.baseArrayLayer,image.layers-1);
        in.layerCount=volume?1:std::clamp(in.layerCount,1u,image.layers-in.baseArrayLayer);return in;
    };
    r.copy.srcSubresource=sub(p.copy.srcSubresource,src,s3);r.copy.dstSubresource=sub(p.copy.dstSubresource,dst,d3);
    auto meet=[](int64_t a,int64_t n,int64_t b,int64_t m) {return a<b+m&&b<a+n;};
    r.overlap=same&&r.copy.srcSubresource.mipLevel==r.copy.dstSubresource.mipLevel&&
        meet(r.copy.srcSubresource.baseArrayLayer,r.copy.srcSubresource.layerCount,r.copy.dstSubresource.baseArrayLayer,r.copy.dstSubresource.layerCount)&&
        meet(p.copy.srcOffset.x,p.copy.extent.width,p.copy.dstOffset.x,p.destination_extent.width)&&
        meet(p.copy.srcOffset.y,p.copy.extent.height,p.copy.dstOffset.y,p.destination_extent.height)&&
        (!s3||meet(p.copy.srcOffset.z,p.copy.extent.depth,p.copy.dstOffset.z,p.destination_extent.depth));
    r.copy.srcOffset.z=s3?p.copy.srcOffset.z:0;r.copy.dstOffset.z=d3?p.copy.dstOffset.z:0;r.copy.extent.depth=s3?p.copy.extent.depth:1;
    r.direct=src.format==dst.format&&p.copy.extent.width==p.destination_extent.width&&p.copy.extent.height==p.destination_extent.height&&
        !p.force_blit&&r.copy.srcSubresource.layerCount==r.copy.dstSubresource.layerCount;
    auto compressed=[](VkFormat f){return f>=VK_FORMAT_BC1_RGB_UNORM_BLOCK&&f<=VK_FORMAT_ASTC_12x12_SRGB_BLOCK;};
    r.blittable=!compressed(src.format)&&!compressed(dst.format)&&src.aspect==dst.aspect;
    r.blit.srcSubresource=r.copy.srcSubresource;r.blit.dstSubresource=r.copy.dstSubresource;
    r.blit.srcSubresource.layerCount=r.blit.dstSubresource.layerCount=std::min(r.copy.srcSubresource.layerCount,r.copy.dstSubresource.layerCount);
    return r;
}
}
namespace nx::nvn {GpuImage* ensure_contents_bound(VkCommandBuffer,const native_bindings::Binding&);}
