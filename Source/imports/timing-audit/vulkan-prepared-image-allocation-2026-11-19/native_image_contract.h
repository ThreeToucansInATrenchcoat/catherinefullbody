#pragma once
#include <cstdlib>
#include "nvn_pipeline.h"
namespace nx::nvn::native_images {
inline int mode() {
    static const int value=[] {const auto* s=std::getenv("CATHERINE_VULKAN_IMAGE_MATERIALS");return s?std::atoi(s):1;}();
    return value;
}
struct Contract {
    VkImageViewType view_type=VK_IMAGE_VIEW_TYPE_2D;
    bool depth=false;
    VkImageCreateInfo sampled_dummy{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkImageViewCreateInfo sampled_view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    VkImageCreateInfo storage_dummy{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    VkImageViewCreateInfo storage_view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
};
inline Contract compile(const ImageShape& shape) {
    Contract c;
    c.view_type=shape.dim==0?(shape.arrayed?VK_IMAGE_VIEW_TYPE_1D_ARRAY:VK_IMAGE_VIEW_TYPE_1D):
        shape.dim==2?VK_IMAGE_VIEW_TYPE_3D:
        shape.dim==3?(shape.arrayed?VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:VK_IMAGE_VIEW_TYPE_CUBE):
        (shape.arrayed?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D);
    c.depth=shape.depth;
    const bool cube=c.view_type==VK_IMAGE_VIEW_TYPE_CUBE||c.view_type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    auto& a=c.sampled_dummy;a.flags=cube?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0;
    a.imageType=c.view_type==VK_IMAGE_VIEW_TYPE_3D?VK_IMAGE_TYPE_3D:
        (c.view_type==VK_IMAGE_VIEW_TYPE_1D||c.view_type==VK_IMAGE_VIEW_TYPE_1D_ARRAY)?VK_IMAGE_TYPE_1D:VK_IMAGE_TYPE_2D;
    a.format=c.depth?VK_FORMAT_D32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM;
    a.extent={1,1,1};a.mipLevels=1;a.arrayLayers=cube?6:1;a.samples=VK_SAMPLE_COUNT_1_BIT;
    a.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    auto& av=c.sampled_view;av.viewType=c.view_type;av.format=a.format;
    av.subresourceRange={VkImageAspectFlags(c.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT),0,1,0,a.arrayLayers};
    auto& b=c.storage_dummy;b.imageType=c.view_type==VK_IMAGE_VIEW_TYPE_3D?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D;
    b.format=VK_FORMAT_R32_UINT;b.extent={1,1,1};b.mipLevels=1;b.arrayLayers=1;
    b.samples=VK_SAMPLE_COUNT_1_BIT;b.usage=VK_IMAGE_USAGE_STORAGE_BIT;
    auto& bv=c.storage_view;
    bv.viewType=c.view_type==VK_IMAGE_VIEW_TYPE_3D?VK_IMAGE_VIEW_TYPE_3D:
        c.view_type==VK_IMAGE_VIEW_TYPE_2D_ARRAY?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D;
    bv.format=b.format;bv.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    return c;
}
inline void verify(const Contract& c,const ImageShape& shape) {
    if(mode()==2&&(c.view_type!=view_type(shape)||c.depth!=shape.depth))
        fatal("Native image contract differs from shader image declaration");
}
inline void verify_values(const Contract& c) {
    if(mode()!=2)return;
    const bool cube=c.view_type==VK_IMAGE_VIEW_TYPE_CUBE||c.view_type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    const auto& a=c.sampled_dummy;const auto& av=c.sampled_view;const auto& b=c.storage_dummy;const auto& bv=c.storage_view;
    const auto image_type=c.view_type==VK_IMAGE_VIEW_TYPE_3D?VK_IMAGE_TYPE_3D:
        (c.view_type==VK_IMAGE_VIEW_TYPE_1D||c.view_type==VK_IMAGE_VIEW_TYPE_1D_ARRAY)?VK_IMAGE_TYPE_1D:VK_IMAGE_TYPE_2D;
    const auto format=c.depth?VK_FORMAT_D32_SFLOAT:VK_FORMAT_R8G8B8A8_UNORM;
    const auto storage_view_type=c.view_type==VK_IMAGE_VIEW_TYPE_3D?VK_IMAGE_VIEW_TYPE_3D:
        c.view_type==VK_IMAGE_VIEW_TYPE_2D_ARRAY?VK_IMAGE_VIEW_TYPE_2D_ARRAY:VK_IMAGE_VIEW_TYPE_2D;
    if(a.sType!=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO||a.pNext||a.flags!=(cube?VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT:0)||
       a.imageType!=image_type||a.format!=format||a.extent.width!=1||a.extent.height!=1||a.extent.depth!=1||
       a.mipLevels!=1||a.arrayLayers!=(cube?6u:1u)||a.samples!=VK_SAMPLE_COUNT_1_BIT||
       a.usage!=(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT)||
       av.viewType!=c.view_type||av.format!=format||av.subresourceRange.aspectMask!=(c.depth?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT)||
       av.subresourceRange.baseMipLevel||av.subresourceRange.levelCount!=1||av.subresourceRange.baseArrayLayer||av.subresourceRange.layerCount!=a.arrayLayers||
       b.imageType!=(c.view_type==VK_IMAGE_VIEW_TYPE_3D?VK_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D)||b.format!=VK_FORMAT_R32_UINT||
       b.extent.width!=1||b.extent.height!=1||b.extent.depth!=1||b.mipLevels!=1||b.arrayLayers!=1||b.samples!=VK_SAMPLE_COUNT_1_BIT||b.usage!=VK_IMAGE_USAGE_STORAGE_BIT||
       bv.viewType!=storage_view_type||bv.format!=VK_FORMAT_R32_UINT||bv.subresourceRange.aspectMask!=VK_IMAGE_ASPECT_COLOR_BIT||
       bv.subresourceRange.baseMipLevel||bv.subresourceRange.levelCount!=1||bv.subresourceRange.baseArrayLayer||bv.subresourceRange.layerCount!=1)
        fatal("Native image allocation/view values differ from compatibility contract");
}
}
namespace nx::nvn {
bool dummy_ready_native(const native_images::Contract&);
VkImageView dummy_view_native(VkCommandBuffer,const native_images::Contract&);
VkImageView sampled_view_native(VkCommandBuffer,uint64_t,const native_images::Contract&,const ViewInfo*,VkImageLayout*);
VkImageView storage_view_native(VkCommandBuffer,uint64_t,const native_images::Contract&,const ViewInfo*);
}
