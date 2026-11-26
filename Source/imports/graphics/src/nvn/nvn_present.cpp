// The NVN present pass (docs/nvn/ANDROID-PORT.md A2; DEBUG-2.md #79): the window texture drawn into the swapchain
// image by a render pass instead of vkCmdBlitImage.
// - Rotated by the swapchain's preTransform: phones scan out portrait, and a pre-rotated image goes to the display
//   controller as is (the compositor rotates otherwise, on the GPU whenever system UI is on top).
// - Fit with bars: a 16:9 picture on a 19.5:9 screen keeps its shape.
// - Alpha 1: an Android surface may take the image's alpha as transparency (compositeAlpha INHERIT), and the game's
//   final image has no meaningful alpha.
// On by default on Android, and on demand for Windows FXAA. NX_PRESENT_PASS=1 also enables the pass elsewhere.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "nvn_vk.h"
#include "nvn_descriptor_bytes.h"
#include "platform.h"
#include "dev_overlay_raster.h"
#include "dlss_backend_win.inc"

namespace nx::nvn {

using namespace nx::vk;

namespace {

// glslc -O --target-env=vulkan1.0 of present.vert:
//   #version 450
//   // A triangle covering the target; uv 0..1 across it (y down, as Vulkan framebuffer coordinates).
//   layout(location = 0) out vec2 uv;
//   void main() {
//       vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
//       uv = p;
//       gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
//   }
const uint32_t kVertex[] = {
    0x07230203, 0x00010000, 0x000d000b, 0x0000002f, 0x00000000, 0x00020011, 0x00000001, 0x0006000b, 0x00000001,
    0x4c534c47, 0x6474732e, 0x3035342e, 0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0008000f, 0x00000000,
    0x00000004, 0x6e69616d, 0x00000000, 0x0000000c, 0x00000018, 0x00000020, 0x00040047, 0x0000000c, 0x0000000b,
    0x0000002a, 0x00040047, 0x00000018, 0x0000001e, 0x00000000, 0x00030047, 0x0000001e, 0x00000002, 0x00050048,
    0x0000001e, 0x00000000, 0x0000000b, 0x00000000, 0x00050048, 0x0000001e, 0x00000001, 0x0000000b, 0x00000001,
    0x00050048, 0x0000001e, 0x00000002, 0x0000000b, 0x00000003, 0x00050048, 0x0000001e, 0x00000003, 0x0000000b,
    0x00000004, 0x00020013, 0x00000002, 0x00030021, 0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020,
    0x00040017, 0x00000007, 0x00000006, 0x00000002, 0x00040015, 0x0000000a, 0x00000020, 0x00000001, 0x00040020,
    0x0000000b, 0x00000001, 0x0000000a, 0x0004003b, 0x0000000b, 0x0000000c, 0x00000001, 0x0004002b, 0x0000000a,
    0x0000000e, 0x00000001, 0x0004002b, 0x0000000a, 0x00000010, 0x00000002, 0x00040020, 0x00000017, 0x00000003,
    0x00000007, 0x0004003b, 0x00000017, 0x00000018, 0x00000003, 0x00040017, 0x0000001a, 0x00000006, 0x00000004,
    0x00040015, 0x0000001b, 0x00000020, 0x00000000, 0x0004002b, 0x0000001b, 0x0000001c, 0x00000001, 0x0004001c,
    0x0000001d, 0x00000006, 0x0000001c, 0x0006001e, 0x0000001e, 0x0000001a, 0x00000006, 0x0000001d, 0x0000001d,
    0x00040020, 0x0000001f, 0x00000003, 0x0000001e, 0x0004003b, 0x0000001f, 0x00000020, 0x00000003, 0x0004002b,
    0x0000000a, 0x00000021, 0x00000000, 0x0004002b, 0x00000006, 0x00000023, 0x40000000, 0x0004002b, 0x00000006,
    0x00000025, 0x3f800000, 0x0004002b, 0x00000006, 0x00000028, 0x00000000, 0x00040020, 0x0000002c, 0x00000003,
    0x0000001a, 0x0005002c, 0x00000007, 0x0000002e, 0x00000025, 0x00000025, 0x00050036, 0x00000002, 0x00000004,
    0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0004003d, 0x0000000a, 0x0000000d, 0x0000000c, 0x000500c4,
    0x0000000a, 0x0000000f, 0x0000000d, 0x0000000e, 0x000500c7, 0x0000000a, 0x00000011, 0x0000000f, 0x00000010,
    0x0004006f, 0x00000006, 0x00000012, 0x00000011, 0x000500c7, 0x0000000a, 0x00000014, 0x0000000d, 0x00000010,
    0x0004006f, 0x00000006, 0x00000015, 0x00000014, 0x00050050, 0x00000007, 0x00000016, 0x00000012, 0x00000015,
    0x0003003e, 0x00000018, 0x00000016, 0x0005008e, 0x00000007, 0x00000024, 0x00000016, 0x00000023, 0x00050083,
    0x00000007, 0x00000027, 0x00000024, 0x0000002e, 0x00050051, 0x00000006, 0x00000029, 0x00000027, 0x00000000,
    0x00050051, 0x00000006, 0x0000002a, 0x00000027, 0x00000001, 0x00070050, 0x0000001a, 0x0000002b, 0x00000029,
    0x0000002a, 0x00000028, 0x00000025, 0x00050041, 0x0000002c, 0x0000002d, 0x00000020, 0x00000021, 0x0003003e,
    0x0000002d, 0x0000002b, 0x000100fd, 0x00010038,
};

// GPU presentation and optional FXAA; regenerated from shaders/present.frag.
#include "nvn_present_fragment.inc"

constexpr size_t kOverlayMax = 32;  // Overlay.shape[32] in present.frag

struct Push {
    float rot[4], off[4], box[4], crop[4], aa[4], fsr[4];
};

struct Pass {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkRenderPass render_pass = VK_NULL_HANDLE;
    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    std::vector<VkImage> images;
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> framebuffers;
    VkExtent2D extent{};
    VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    bool failed = false;
} g;

struct FsrTarget {GpuImage image;VkFramebuffer framebuffer=VK_NULL_HANDLE;};
std::vector<FsrTarget> fsr_targets,hud_targets,hud_upscale_targets;
VkRenderPass fsr_render_pass=VK_NULL_HANDLE,hud_render_pass=VK_NULL_HANDLE,hud_upscale_pass=VK_NULL_HANDLE;
void destroy_offscreen_targets(std::vector<FsrTarget>& targets,VkRenderPass& render_pass){
    const VkDevice device=vk_device();
    for(auto& target:targets){
        if(target.framebuffer)p_vkDestroyFramebuffer(device,target.framebuffer,nullptr);
        if(target.image.view)p_vkDestroyImageView(device,target.image.view,nullptr);
        if(target.image.image)p_vkDestroyImage(device,target.image.image,nullptr);
        if(target.image.memory)p_vkFreeMemory(device,target.image.memory,nullptr);
    }
    targets.clear();
    if(render_pass)p_vkDestroyRenderPass(device,render_pass,nullptr);
    render_pass=VK_NULL_HANDLE;
}
bool prepare_offscreen_targets(std::vector<FsrTarget>& targets,VkRenderPass& render_pass,unsigned count,unsigned width,unsigned height){
    if(targets.size()==count&&!targets.empty()&&targets[0].image.width==width&&targets[0].image.height==height&&targets[0].image.format==g.format)return true;
    const VkDevice device=vk_device();p_vkDeviceWaitIdle(device);destroy_offscreen_targets(targets,render_pass);
    VkAttachmentDescription attachment{};attachment.format=g.format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachment.finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference reference{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&reference;
    VkSubpassDependency dependency{};dependency.srcSubpass=VK_SUBPASS_EXTERNAL;dependency.dstSubpass=0;
    dependency.srcStageMask=dependency.dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependency.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo render{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};render.attachmentCount=1;render.pAttachments=&attachment;render.subpassCount=1;render.pSubpasses=&subpass;render.dependencyCount=1;render.pDependencies=&dependency;
    if(p_vkCreateRenderPass(device,&render,nullptr,&render_pass)!=VK_SUCCESS)return false;
    targets.resize(count);
    for(auto& target:targets){
        auto& image=target.image;
        VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};create.imageType=VK_IMAGE_TYPE_2D;create.format=g.format;create.extent={width,height,1};create.mipLevels=create.arrayLayers=1;create.samples=VK_SAMPLE_COUNT_1_BIT;create.tiling=VK_IMAGE_TILING_OPTIMAL;create.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if(p_vkCreateImage(device,&create,nullptr,&image.image)!=VK_SUCCESS){destroy_offscreen_targets(targets,render_pass);return false;}
        VkMemoryRequirements requirements{};p_vkGetImageMemoryRequirements(device,image.image,&requirements);
        VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if(p_vkAllocateMemory(device,&allocate,nullptr,&image.memory)!=VK_SUCCESS||p_vkBindImageMemory(device,image.image,image.memory,0)!=VK_SUCCESS){destroy_offscreen_targets(targets,render_pass);return false;}
        image.format=g.format;image.width=width;image.height=height;
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=g.format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        if(p_vkCreateImageView(device,&view,nullptr,&image.view)!=VK_SUCCESS){destroy_offscreen_targets(targets,render_pass);return false;}
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=render_pass;framebuffer.attachmentCount=1;framebuffer.pAttachments=&image.view;framebuffer.width=width;framebuffer.height=height;framebuffer.layers=1;
        if(p_vkCreateFramebuffer(device,&framebuffer,nullptr,&target.framebuffer)!=VK_SUCCESS){destroy_offscreen_targets(targets,render_pass);return false;}
    }
    return true;
}

void destroy_fsr_targets(){destroy_offscreen_targets(fsr_targets,fsr_render_pass);destroy_offscreen_targets(hud_targets,hud_render_pass);destroy_offscreen_targets(hud_upscale_targets,hud_upscale_pass);}
bool prepare_fsr_targets(unsigned width,unsigned height){
    const bool recreate=fsr_targets.empty()||fsr_targets[0].image.width!=width||fsr_targets[0].image.height!=height;
    const bool result=prepare_offscreen_targets(fsr_targets,fsr_render_pass,unsigned(g.images.size()),width,height);
    if(result&&recreate)log("nvn/vk: FSR 1 EASU + RCAS output %ux%u",width,height);return result;
}

GpuImage dev_ui_image;
uint64_t dev_ui_uploaded=UINT64_MAX;
GpuImage* overlay_texture(VkCommandBuffer cb){
    if(!dev_ui_image.image){
        auto& img=dev_ui_image;
        VkDevice d=vk_device();
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent={dev_overlay_raster::width,dev_overlay_raster::height,1};
        ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;
        ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if(p_vkCreateImage(d,&ci,nullptr,&img.image)!=VK_SUCCESS)return nullptr;
        VkMemoryRequirements req{};p_vkGetImageMemoryRequirements(d,img.image,&req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize=req.size;ai.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if(p_vkAllocateMemory(d,&ai,nullptr,&img.memory)!=VK_SUCCESS || p_vkBindImageMemory(d,img.image,img.memory,0)!=VK_SUCCESS){
            p_vkDestroyImage(d,img.image,nullptr);if(img.memory)p_vkFreeMemory(d,img.memory,nullptr);img={};return nullptr;
        }
        img.format=ci.format;img.width=ci.extent.width;img.height=ci.extent.height;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image=img.image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=img.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        if(p_vkCreateImageView(d,&vi,nullptr,&img.view)!=VK_SUCCESS){
            p_vkDestroyImage(d,img.image,nullptr);p_vkFreeMemory(d,img.memory,nullptr);img={};return nullptr;
        }
    }
    if(dev_ui_uploaded!=dev_overlay_raster::version){
        constexpr size_t bytes=size_t(dev_overlay_raster::width)*dev_overlay_raster::height*4;
        Upload upload=staging_alloc(bytes);if(!upload.ptr)return nullptr;
        if(dev_overlay_raster::pixels.empty())std::memset(upload.ptr,0,bytes);
        else std::memcpy(upload.ptr,dev_overlay_raster::pixels.data(),bytes);
        transition(cb,dev_ui_image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy copy{};copy.bufferOffset=upload.offset;
        copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={dev_ui_image.width,dev_ui_image.height,1};
        p_vkCmdCopyBufferToImage(cb,upload.buffer,dev_ui_image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        transition(cb,dev_ui_image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        dev_ui_uploaded=dev_overlay_raster::version;
    }
    return &dev_ui_image;
}

VkShaderModule module(const uint32_t* code, size_t bytes) {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = code;
    VkShaderModule m = VK_NULL_HANDLE;
    p_vkCreateShaderModule(vk_device(), &ci, nullptr, &m);
    return m;
}

void destroy_targets() {
    destroy_fsr_targets();
    VkDevice d = vk_device();
    for (VkFramebuffer f : g.framebuffers) p_vkDestroyFramebuffer(d, f, nullptr);
    for (VkImageView v : g.views) p_vkDestroyImageView(d, v, nullptr);
    g.framebuffers.clear(), g.views.clear(), g.images.clear();
}

void destroy_pipeline() {
    VkDevice d = vk_device();
    if (g.pipeline) p_vkDestroyPipeline(d, g.pipeline, nullptr);
    if (g.layout) p_vkDestroyPipelineLayout(d, g.layout, nullptr);
    if (g.set_layout) p_vkDestroyDescriptorSetLayout(d, g.set_layout, nullptr);
    if (g.sampler) p_vkDestroySampler(d, g.sampler, nullptr);
    if (g.render_pass) p_vkDestroyRenderPass(d, g.render_pass, nullptr);
    g.pipeline = VK_NULL_HANDLE; g.layout = VK_NULL_HANDLE; g.set_layout = VK_NULL_HANDLE;
    g.sampler = VK_NULL_HANDLE; g.render_pass = VK_NULL_HANDLE;
    g.format = VK_FORMAT_UNDEFINED;
}

bool make_pipeline(VkFormat format) {
    VkDevice d = vk_device();
    VkAttachmentDescription color{};
    color.format = format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;  // every pixel is written
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    // Streamline returns proxy images, which its pacer copies into the real swapchain.
    color.finalLayout = dlss_backbuffer_layout();
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1, rpi.pAttachments = &color;
    rpi.subpassCount = 1, rpi.pSubpasses = &sub;
    rpi.dependencyCount = 1, rpi.pDependencies = &dep;
    if (p_vkCreateRenderPass(d, &rpi, nullptr, &g.render_pass) != VK_SUCCESS) return false;

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (p_vkCreateSampler(d, &si, nullptr, &g.sampler) != VK_SUCCESS) return false;
    VkDescriptorSetLayoutBinding b[3] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &g.sampler},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &g.sampler}};
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 3, dli.pBindings = b;
    if (p_vkCreateDescriptorSetLayout(d, &dli, nullptr, &g.set_layout) != VK_SUCCESS) return false;
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1, pli.pSetLayouts = &g.set_layout;
    pli.pushConstantRangeCount = 1, pli.pPushConstantRanges = &push;
    if (p_vkCreatePipelineLayout(d, &pli, nullptr, &g.layout) != VK_SUCCESS) return false;

    VkShaderModule vs = module(kVertex, sizeof kVertex), fs = module(kFragment, sizeof kFragment);
    VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT, stages[0].module = vs, stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT, stages[1].module = fs, stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL, rs.cullMode = VK_CULL_MODE_NONE, rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = 0xF;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1, cb.pAttachments = &blend;
    VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    ds.dynamicStateCount = 2, ds.pDynamicStates = dyn;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2, gp.pStages = stages;
    gp.pVertexInputState = &vin, gp.pInputAssemblyState = &ia, gp.pViewportState = &vp;
    gp.pRasterizationState = &rs, gp.pMultisampleState = &ms, gp.pColorBlendState = &cb, gp.pDynamicState = &ds;
    gp.layout = g.layout, gp.renderPass = g.render_pass;
    VkResult r = p_vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gp, nullptr, &g.pipeline);
    p_vkDestroyShaderModule(d, vs, nullptr);
    p_vkDestroyShaderModule(d, fs, nullptr);
    g.format = format;
    return r == VK_SUCCESS;
}

}  // namespace

bool present_pass_wanted() {
    static const bool on = [] {
        const char* e = std::getenv("NX_PRESENT_PASS");
#if defined(__ANDROID__)
        return !e || e[0] != '0';
#else
        return e && e[0] == '1';
#endif
    }();
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    // Runtime selection: Off and MSAA keep the original blit; FXAA draws once.
    return dev_overlay_raster::refresh() || on || plat::anti_aliasing() == 1 || catherine::host_settings().upscaler==1 || catherine::host_settings().frame_generation==1;
#else
    return on;
#endif
}

void present_pass_release() { destroy_targets(); }  // before the swapchain whose images they view is destroyed
bool present_pass_failed() { return g.failed; }

bool present_pass_targets(VkFormat format, const std::vector<VkImage>& images, VkExtent2D extent,
                          VkSurfaceTransformFlagBitsKHR transform) {
    if (!present_pass_wanted() || g.failed) return false;
    // Also called lazily when FXAA is enabled after swapchain creation. Existing
    // framebuffers are immutable until swapchain retirement waits for the GPU.
    if (g.pipeline && g.format == format && g.images == images &&
        g.extent.width == extent.width && g.extent.height == extent.height &&
        g.transform == transform && g.framebuffers.size() == images.size()) return true;
    destroy_targets();
    if (g.format != format && g.pipeline) {  // a new swapchain format: a new render pass and pipeline
        destroy_pipeline();
    }
    if (!g.pipeline && !make_pipeline(format)) {
        log("nvn/vk: present pass unavailable: frames are blitted");
        destroy_pipeline();
        g.failed = true;
        return false;
    }
    VkDevice d = vk_device();
    for (VkImage img : images) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = img, vi.viewType = VK_IMAGE_VIEW_TYPE_2D, vi.format = format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VkImageView v = VK_NULL_HANDLE;
        if (p_vkCreateImageView(d, &vi, nullptr, &v) != VK_SUCCESS) {
            destroy_targets(); g.failed = true; return false;
        }
        g.views.push_back(v);
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = g.render_pass, fi.attachmentCount = 1, fi.pAttachments = &v;
        fi.width = extent.width, fi.height = extent.height, fi.layers = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        if (p_vkCreateFramebuffer(d, &fi, nullptr, &fb) != VK_SUCCESS) {
            destroy_targets(); g.failed = true; return false;
        }
        g.framebuffers.push_back(fb);
    }
    g.extent = extent, g.transform = transform;
    g.images = images;
    return true;
}

bool record_picture(VkCommandBuffer cb, GpuImage& src, const int32_t* crop, uint32_t index,VkFramebuffer target,VkExtent2D extent,int fsr_mode,bool scene_only=false,VkRenderPass scene_pass=VK_NULL_HANDLE) {
    if (!present_pass_wanted() || g.failed || !g.pipeline || !src.width || !src.height || index >= g.framebuffers.size() || !src.view || src.levels != 1 || src.layers != 1 ||
        src.view_type != VK_IMAGE_VIEW_TYPE_2D || !(src.aspect & VK_IMAGE_ASPECT_COLOR_BIT))
        return false;
    GpuImage* menu=fsr_mode==1||scene_only?nullptr:overlay_texture(cb);
    transition(cb, src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    // The set and the circles' uniform data come from this submit's slot (descriptor pool and upload ring, reset when
    // the slot is retired): nothing a frame still in flight reads is written. Per swapchain image they were not safe
    // (MAILBOX can hand an image back before the frame that last drew into it finished; review of DEBUG-2.md #88).
    VkDescriptorSet set = descriptor_set(g.set_layout);
    Upload ubo = upload_alloc(sizeof(float) * 4 * kOverlayMax, 256);
    std::memset(ubo.ptr, 0, sizeof(float) * 4 * kOverlayMax);
    VkDescriptorImageInfo ii{g.sampler, src.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo bi{ubo.buffer, ubo.offset, sizeof(float) * 4 * kOverlayMax};
    VkDescriptorImageInfo menu_ii{g.sampler,menu?menu->view:src.view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w[3] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}, {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    w[0].dstSet = set, w[0].descriptorCount = 1, w[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w[0].pImageInfo = &ii;
    w[1].dstSet = set, w[1].dstBinding = 1, w[1].descriptorCount = 1, w[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w[1].pBufferInfo = &bi;
    w[2].dstSet=set;w[2].dstBinding=2;w[2].descriptorCount=1;w[2].descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;w[2].pImageInfo=&menu_ii;
    p_vkUpdateDescriptorSets(vk_device(), 3, w, 0, nullptr);

    Push p{};
    p.aa[3]=menu&&!dev_overlay_raster::commands.empty()?1.0f:0.0f;
    p.fsr[0]=float(fsr_mode);p.fsr[1]=2.0f*(1.0f-float(catherine::host_settings().fsr_sharpness)/100.0f);
    if(catherine::host_settings().fsr_sharpness==0)p.fsr[1]=-1.0f;
    p.fsr[2]=float(extent.width);p.fsr[3]=float(extent.height);
    p.aa[0] = 1.0f / float(src.width), p.aa[1] = 1.0f / float(src.height);
#ifdef CATHERINE_HIGH_REFRESH_BUILD
    p.aa[2] = plat::anti_aliasing() == 1 ? 1.0f : 0.0f;
#endif
    // Host uv -> upright uv, undoing the clockwise turn the display applies (as vk_present.cpp).
    static const float kRot[4][6] = {{1, 0, 0, 1, 0, 0}, {0, -1, 1, 0, 0, 1}, {-1, 0, 0, -1, 1, 1}, {0, 1, -1, 0, 1, 0}};
    int r = g.transform == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR    ? 1
            : g.transform == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR ? 2
            : g.transform == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR ? 3
                                                                     : 0;
    std::memcpy(p.rot, kRot[r], 4 * sizeof(float));
    p.off[0] = kRot[r][4], p.off[1] = kRot[r][5];
    // The part of the texture shown (nvnWindowSetCrop), fit into the upright target keeping its aspect.
    float cx = 0, cy = 0, cw = float(src.width), ch = float(src.height);
    if (crop && crop[2] > 0 && crop[3] > 0 && crop[0] + crop[2] <= int32_t(src.width) &&
        crop[1] + crop[3] <= int32_t(src.height))
        cx = float(crop[0]), cy = float(crop[1]), cw = float(crop[2]), ch = float(crop[3]);
    p.crop[0] = cx / float(src.width), p.crop[1] = cy / float(src.height);
    p.crop[2] = cw / float(src.width), p.crop[3] = ch / float(src.height);
    bool sideways = r == 1 || r == 3;
    float tw = float(sideways ? extent.height : extent.width), th = float(sideways ? extent.width : extent.height);
    float scale = std::min(tw / cw, th / ch), bw = cw * scale / tw, bh = ch * scale / th;
    p.box[0] = (1 - bw) / 2, p.box[1] = (1 - bh) / 2, p.box[2] = bw, p.box[3] = bh;
    // The touch controls (platform_android_input.cpp), in upright uv; none elsewhere.
    plat::OverlayShape shapes[kOverlayMax];
    size_t count = plat::touch_overlay(shapes, kOverlayMax);
    auto* dst = reinterpret_cast<float*>(ubo.ptr);
    for (size_t k = 0; k < count; k++)
        dst[4 * k] = shapes[k].x, dst[4 * k + 1] = shapes[k].y, dst[4 * k + 2] = shapes[k].r, dst[4 * k + 3] = shapes[k].alpha;
    p.off[2] = float(count), p.off[3] = tw / th;

    VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rb.renderPass = scene_pass?scene_pass:(fsr_mode==1?fsr_render_pass:g.render_pass);rb.framebuffer=target;rb.renderArea={{0,0},extent};
    p_vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport v{0, 0, float(extent.width), float(extent.height), 0, 1};
    VkRect2D sc{{0, 0}, extent};
    p_vkCmdSetViewport(cb, 0, 1, &v);
    p_vkCmdSetScissor(cb, 0, 1, &sc);
    p_vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.pipeline);
    descriptor_bytes_legacy_bind(cb);
    p_vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, g.layout, 0, 1, &set, 0, nullptr);
    p_vkCmdPushConstants(cb, g.layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof p, &p);
    p_vkCmdDraw(cb, 3, 1, 0, 0);
    p_vkCmdEndRenderPass(cb);
    return true;
}

bool present_pass_record(VkCommandBuffer cb,GpuImage& source,const int32_t* crop,uint32_t index){
    if(index>=g.framebuffers.size()||!source.width||!source.height)return false;
    dev_overlay_raster::refresh();
    const auto settings=catherine::host_settings();
    const bool cropped=crop&&crop[2]>0&&crop[3]>0&&crop[0]>=0&&crop[1]>=0&&crop[0]+crop[2]<=int(source.width)&&crop[1]+crop[3]<=int(source.height);
    const unsigned width=cropped?unsigned(crop[2]):source.width,height=cropped?unsigned(crop[3]):source.height;
    const bool sideways=g.transform==VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR||g.transform==VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    const unsigned output_width=sideways?g.extent.height:g.extent.width,output_height=sideways?g.extent.width:g.extent.height;
    const float scale=std::min(float(output_width)/width,float(output_height)/height);
    static bool reported_scale=false;
    if(settings.upscaler==1&&!reported_scale){log("nvn/vk: FSR 1 input %ux%u output area %ux%u scale %.3f",width,height,output_width,output_height,scale);reported_scale=true;}
    const unsigned scaled_width=std::max(1u,unsigned(width*scale)),scaled_height=std::max(1u,unsigned(height*scale));
    if(settings.upscaler==1&&scale>1.01f&&prepare_fsr_targets(scaled_width,scaled_height)){
        auto& target=fsr_targets[index];
        if(record_picture(cb,source,crop,index,target.framebuffer,{scaled_width,scaled_height},1)){
            target.image.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            return record_picture(cb,target.image,nullptr,index,g.framebuffers[index],g.extent,2);
        }
    }
    return record_picture(cb,source,crop,index,g.framebuffers[index],g.extent,0);
}

// Apply exactly the display's filtering to the pre-HUD image, without host UI.
// Its format/size matches the swapchain even when FSR changes render resolution.
GpuImage* present_scene_only_record(VkCommandBuffer cb,GpuImage& source,unsigned width,unsigned height){
    if(!g.pipeline||g.failed||g.extent.width!=width||g.extent.height!=height)return nullptr;
    if(!prepare_offscreen_targets(hud_targets,hud_render_pass,1,width,height))return nullptr;
    auto& output=hud_targets[0];
    const float scale=std::min(float(width)/source.width,float(height)/source.height);
    const unsigned area_width=std::max(1u,unsigned(source.width*scale)),area_height=std::max(1u,unsigned(source.height*scale));
    const bool fsr=catherine::host_settings().upscaler==1&&scale>1.01f;
    GpuImage* input=&source;
    if(fsr){
        if(!prepare_offscreen_targets(hud_upscale_targets,hud_upscale_pass,1,area_width,area_height))return nullptr;
        auto& temporary=hud_upscale_targets[0];
        if(!record_picture(cb,source,nullptr,0,temporary.framebuffer,{area_width,area_height},1,true,hud_upscale_pass))return nullptr;
        temporary.image.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;input=&temporary.image;
    }
    if(!record_picture(cb,*input,nullptr,0,output.framebuffer,{width,height},fsr?2:0,true,hud_render_pass))return nullptr;
    output.image.layout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    transition(cb,output.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);return &output.image;
}

VkImageView present_pass_image_view(unsigned index){return index<g.views.size()?g.views[index]:VK_NULL_HANDLE;}
void present_pass_frame(){
    static bool reported=false;
    if(!reported){const auto settings=catherine::host_settings();log("nvn/vk: presentation options upscaler=%d quality=%d sharpness=%d fps_counter=%d",settings.upscaler,settings.upscale_quality,settings.fsr_sharpness,int(settings.show_fps));reported=true;}
    dev_overlay_raster::frame_tick();
}

// NX_PRESENT_TEST=<w>x<h>[:<90|180|270>] (headless, with NX_DUMP_FRAMES): the pass draws into an offscreen image
// of that size and transform, and that image is dumped in place of the window texture. It checks the rotation, the
// fit and the alpha without a phone (a Galaxy S23 held sideways: 1080x2340:90).
GpuImage* present_pass_test_target() {
    static GpuImage img;
    static bool tried = false;
    if (tried) return img.image ? &img : nullptr;
    const char* e = std::getenv("NX_PRESENT_TEST");
    uint32_t w = 0, h = 0, deg = 0;
    if(!e&&catherine_dev_overlay_visible()){w=1920;h=1080;}
    else if (!e || std::sscanf(e, "%ux%u:%u", &w, &h, &deg) < 2 || !w || !h) return nullptr;
    tried=true;
    VkDevice d = vk_device();
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D, ci.format = VK_FORMAT_R8G8B8A8_UNORM, ci.extent = {w, h, 1};
    ci.mipLevels = 1, ci.arrayLayers = 1, ci.samples = VK_SAMPLE_COUNT_1_BIT, ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (p_vkCreateImage(d, &ci, nullptr, &img.image) != VK_SUCCESS) return nullptr;
    VkMemoryRequirements req{};
    p_vkGetImageMemoryRequirements(d, img.image, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size, ai.memoryTypeIndex = memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    p_vkAllocateMemory(d, &ai, nullptr, &img.memory);
    p_vkBindImageMemory(d, img.image, img.memory, 0);
    img.format = VK_FORMAT_R8G8B8A8_UNORM, img.width = w, img.height = h;
    VkSurfaceTransformFlagBitsKHR t = deg == 90    ? VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR
                                      : deg == 180 ? VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR
                                      : deg == 270 ? VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR
                                                   : VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    if (!present_pass_targets(img.format, {img.image}, {w, h}, t)) {
        log("nvn/vk: present test: the pass is off (NX_PRESENT_PASS=1)");
        return nullptr;
    }
    log("nvn/vk: present test: %ux%u, turned %u degrees", w, h, deg);
    return &img;
}

}  // namespace nx::nvn
