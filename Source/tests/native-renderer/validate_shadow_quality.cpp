#include <algorithm>
#include "nvn_vk.h"
#include "C:/catherine/native-gpu-experiment/Source/shadow_quality.h"
#include <cassert>
#include <cstdio>
int main() {
    using namespace nx::nvn;
    GpuImage large{}, native{}, lower{};
    large.width=large.height=8192; large.shadow_base_width=large.shadow_base_height=2048;
    lower.width=lower.height=4096; lower.shadow_base_width=lower.shadow_base_height=2048;
    native.width=native.height=2048;
    VkViewport vp{100,2048,1900,-2048,0,1};
    VkRect2D sc{{100,200},{300,400}};
    shadow_quality::raster(large,vp,sc,true,true);
    assert(vp.x==400&&vp.y==8192&&vp.width==7600&&vp.height==-8192);
    assert(sc.offset.x==400&&sc.offset.y==800&&sc.extent.width==1200&&sc.extent.height==1600);
    VkViewport automatic{0,8192,8192,-8192,0,1}; VkRect2D full{{0,0},{8192,8192}};
    shadow_quality::raster(large,automatic,full,false,false);
    assert(automatic.height==-8192&&full.extent.width==8192);
    VkImageCopy copy{}; copy.srcOffset={10,20,0};copy.dstOffset={30,40,0};copy.extent={100,200,1};
    VkImageBlit blit{};blit.srcOffsets[0]={10,20,0};blit.srcOffsets[1]={110,220,1};
    blit.dstOffsets[0]={30,40,0};blit.dstOffsets[1]={130,240,1};
    bool direct=true;shadow_quality::transfer(large,lower,copy,blit,direct);
    assert(!direct&&copy.extent.width==400&&copy.srcOffset.x==40&&copy.dstOffset.x==60);
    assert(blit.srcOffsets[1].x==440&&blit.dstOffsets[1].x==260);
    int32_t region[6]={10,20,3,100,200,1};shadow_quality::rectangle(large,region);
    assert(region[0]==40&&region[1]==80&&region[2]==3&&region[3]==400&&region[4]==800&&region[5]==1);
    copy={};copy.extent={2048,2048,1};blit={};direct=true;
    shadow_quality::transfer(lower,lower,copy,blit,direct);
    assert(direct&&copy.extent.width==4096);
    copy={};copy.extent={2048,2048,1};blit={};direct=true;
    shadow_quality::transfer(native,native,copy,blit,direct);
    assert(direct&&copy.extent.width==2048);
    assert(catherine_shadow_resolution_with_limit(8192,4096)==4096);
    std::puts("Shadow viewport, scissor, copies, downsampling and limits passed");
}
