#pragma once
#include <cstdlib>
namespace nx::nvn::native_upload {
inline int mode(){static const int m=[] {const char* p=std::getenv("CATHERINE_VULKAN_NATIVE_UPLOAD");return p?std::atoi(p):1;}();return m;}
struct Intent { VkBufferImageCopy regions[3]{}; };
constexpr uint64_t tag=0x315055564b584eull;
template<class View> Intent prepare(const View& view,const int32_t* r) {
    Intent result{};
    for(int kind=0;kind<3;++kind) {
        const bool volume=kind==1,array1=kind==2;
        const int32_t y=array1?0:r[1],z=array1?r[1]:r[2];
        const int32_t height=array1?1:r[4],depth=array1?r[4]:r[5];
        auto& c=result.regions[kind];
        c.imageSubresource={0,view.base_level,volume?0u:uint32_t(uint64_t(uint32_t(z))+view.min_layer),volume?1u:uint32_t(depth)};
        c.imageOffset={r[0],y,volume?z:0};
        c.imageExtent={uint32_t(r[3]),uint32_t(height),volume?uint32_t(depth):1u};
    }
    return result;
}
inline VkImageSubresourceRange clear_range(const Intent& p,int kind,VkImageAspectFlags aspect){
    const auto& sub=p.regions[kind].imageSubresource;
    return {aspect,sub.mipLevel,1,sub.baseArrayLayer,sub.layerCount};
}
constexpr uint64_t clear_tag=0x314c43564b584eull;
struct ClearIntent {Intent geometry{};VkClearColorValue color{};};
}
