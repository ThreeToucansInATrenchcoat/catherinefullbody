// Expand an existing single-sample color attachment into identical samples.
#include <map>

#include "nvn_vk.h"

namespace nx::nvn {
using namespace nx::vk;

namespace {
#include "nvn_msaa_seed_shaders.inc"

struct SeedResources {
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkShaderModule vertex = VK_NULL_HANDLE, fragment = VK_NULL_HANDLE;
};

VkShaderModule module(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule shader = VK_NULL_HANDLE;
    p_vkCreateShaderModule(vk_device(), &ci, nullptr, &shader);
    return shader;
}

const SeedResources& resources() {
    static const SeedResources result = [] {
        SeedResources out;
        VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        if (p_vkCreateSampler(vk_device(), &sci, nullptr, &out.sampler) != VK_SUCCESS) return out;
        const VkDescriptorSetLayoutBinding binding{
            0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &out.sampler};
        VkDescriptorSetLayoutCreateInfo dci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dci.bindingCount = 1;
        dci.pBindings = &binding;
        if (p_vkCreateDescriptorSetLayout(vk_device(), &dci, nullptr, &out.descriptors) != VK_SUCCESS) return out;
        VkPipelineLayoutCreateInfo lci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        lci.setLayoutCount = 1;
        lci.pSetLayouts = &out.descriptors;
        if (p_vkCreatePipelineLayout(vk_device(), &lci, nullptr, &out.layout) != VK_SUCCESS) return out;
        out.vertex = module(kSeedVertex, sizeof kSeedVertex);
        out.fragment = module(kSeedFragment, sizeof kSeedFragment);
        return out;
    }();
    return result;
}

VkPipeline seed_pipeline(VkFormat format, VkSampleCountFlagBits samples, const SeedResources& seed) {
    static std::map<std::pair<VkFormat, VkSampleCountFlagBits>, VkPipeline> pipelines;
    auto [it, fresh] = pipelines.try_emplace({format, samples}, VK_NULL_HANDLE);
    if (!fresh) return it->second;
    const VkPipelineShaderStageCreateInfo stages[] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, seed.vertex, "main"},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, seed.fragment, "main"},
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = samples;
    VkPipelineColorBlendAttachmentState color{};
    color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &color;
    constexpr VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = states;
    VkPipelineRenderingCreateInfo ri{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    ri.colorAttachmentCount = 1;
    ri.pColorAttachmentFormats = &format;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, &ri};
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dyn;
    ci.layout = seed.layout;
    p_vkCreateGraphicsPipelines(vk_device(), VK_NULL_HANDLE, 1, &ci, nullptr, &it->second);
    return it->second;
}
}  // namespace

bool seed_msaa_color(VkCommandBuffer cb, const VkRenderingAttachmentInfo& source, const GpuImage& sidecar) {
    const SeedResources& seed = resources();
    if (!seed.vertex || !seed.fragment) return false;
    const VkPipeline pipeline = seed_pipeline(sidecar.format, sidecar.samples, seed);
    const VkDescriptorSet set = pipeline ? descriptor_set(seed.descriptors) : VK_NULL_HANDLE;
    if (!set) return false;
    VkDescriptorImageInfo input{seed.sampler, source.imageView, source.imageLayout};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &input;
    p_vkUpdateDescriptorSets(vk_device(), 1, &write, 0, nullptr);

    VkRenderingAttachmentInfo output{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    output.imageView = sidecar.view;
    output.imageLayout = sidecar.layout;
    output.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    output.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    const VkRect2D area{{0, 0}, {sidecar.width, sidecar.height}};
    VkRenderingInfo ri{VK_STRUCTURE_TYPE_RENDERING_INFO};
    ri.renderArea = area;
    ri.layerCount = 1;
    ri.colorAttachmentCount = 1;
    ri.pColorAttachments = &output;
    p_vkCmdBeginRendering(cb, &ri);
    p_vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    p_vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, seed.layout, 0, 1, &set, 0, nullptr);
    const VkViewport viewport{0, 0, float(sidecar.width), float(sidecar.height), 0, 1};
    p_vkCmdSetViewport(cb, 0, 1, &viewport);
    p_vkCmdSetScissor(cb, 0, 1, &area);
    p_vkCmdDraw(cb, 3, 1, 0, 0);
    p_vkCmdEndRendering(cb);

    VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    mb.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    mb.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    p_vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                           VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    return true;
}
}  // namespace nx::nvn
