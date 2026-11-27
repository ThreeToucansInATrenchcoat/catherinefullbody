#pragma once
#include <cstdint>
#include <cstdio>
#include "native_skin_spirv.h"

// Define EXP_VK(name) to the renderer's Vulkan dispatch table when embedded.
#ifndef EXP_VK
#define EXP_VK(name) name
#endif
namespace experimental_skin {
inline bool supported(VkPhysicalDevice physical) {
    VkPhysicalDeviceFloatControlsProperties controls{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FLOAT_CONTROLS_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&controls;
    EXP_VK(vkGetPhysicalDeviceProperties2)(physical,&properties);
    std::fprintf(stderr,"Native FP32 controls: FTZ %u signed-zero %u RTE %u\n",controls.shaderDenormFlushToZeroFloat32,controls.shaderSignedZeroInfNanPreserveFloat32,controls.shaderRoundingModeRTEFloat32);
    return controls.shaderSignedZeroInfNanPreserveFloat32&&controls.shaderRoundingModeRTEFloat32;
}
struct Parameters {
    uint64_t source, palette, destination, preserved;
    uint32_t count, matrices;
};
static_assert(sizeof(Parameters)==40);
struct Pipeline {
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline kernels[2]{};
    bool create(VkDevice device,VkPipelineCache cache=VK_NULL_HANDLE) {
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Parameters)};
        VkPipelineLayoutCreateInfo li{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        li.pushConstantRangeCount=1;li.pPushConstantRanges=&push;
        if(EXP_VK(vkCreatePipelineLayout)(device,&li,nullptr,&layout)!=VK_SUCCESS)return false;
        for(unsigned i=0;i<2;++i) {
            VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
            mi.pCode=i?native_skin_tangent:native_skin_regular;
            mi.codeSize=i?sizeof(native_skin_tangent):sizeof(native_skin_regular);
            VkShaderModule module{};
            if(EXP_VK(vkCreateShaderModule)(device,&mi,nullptr,&module)!=VK_SUCCESS){destroy(device);return false;}
            VkComputePipelineCreateInfo ci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
            ci.layout=layout;ci.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;ci.stage.module=module;ci.stage.pName="main";
            VkResult result=EXP_VK(vkCreateComputePipelines)(device,cache,1,&ci,nullptr,&kernels[i]);
            EXP_VK(vkDestroyShaderModule)(device,module,nullptr);
            if(result!=VK_SUCCESS){destroy(device);return false;}
        }
        return true;
    }
    void record(VkCommandBuffer cb,const Parameters& p,bool tangent) const {
        EXP_VK(vkCmdBindPipeline)(cb,VK_PIPELINE_BIND_POINT_COMPUTE,kernels[tangent?1:0]);
        EXP_VK(vkCmdPushConstants)(cb,layout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(p),&p);
        EXP_VK(vkCmdDispatch)(cb,(p.count+63)/64,1,1);
    }
    void destroy(VkDevice device) {
        for(auto pipeline:kernels)if(pipeline)EXP_VK(vkDestroyPipeline)(device,pipeline,nullptr);
        if(layout)EXP_VK(vkDestroyPipelineLayout)(device,layout,nullptr);
        *this={};
    }
};
}
