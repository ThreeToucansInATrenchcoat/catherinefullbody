// Independent offscreen GPU validation. No game assets, surface, or window.
#include "../src/nvn/nvn_present_fragment.inc"
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include <vulkan/vulkan.h>
static void check(VkResult r) {
    if (r != VK_SUCCESS)
        throw std::runtime_error("Vulkan result " + std::to_string(r));
}
static void require(bool b, const char *msg) {
    if (!b)
        throw std::runtime_error(msg);
}
static std::atomic<unsigned> errors{0};
static VKAPI_ATTR VkBool32 VKAPI_CALL debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        ++errors;
        std::fprintf(stderr, "validation: %s\n", data->pMessage);
    }
    return VK_FALSE;
}
int main(int argc, char **argv) try {
    require(argc == 2, "expected compiled fullscreen vertex shader path");
    constexpr uint32_t W = 64, H = 64, bytes = W * H * 4;
    VkInstance instance;
    VkDevice d;
    VkPhysicalDevice physical;
    VkQueue queue;
    uint32_t n = 0;
    check(vkEnumerateInstanceLayerProperties(&n, nullptr));
    std::vector<VkLayerProperties> layers(n);
    check(vkEnumerateInstanceLayerProperties(&n, layers.data()));
    const char *validation = "VK_LAYER_KHRONOS_validation";
    bool validated = std::any_of(layers.begin(), layers.end(),
                                 [&](auto &l) { return std::strcmp(l.layerName, validation) == 0; });
    const char *extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Catherine FXAA offscreen fixture";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app;
    if (validated) {
        ici.enabledLayerCount = 1;
        ici.ppEnabledLayerNames = &validation;
        ici.enabledExtensionCount = 1;
        ici.ppEnabledExtensionNames = &extension;
    }
    check(vkCreateInstance(&ici, nullptr, &instance));
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    auto destroy_debug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    if (validated) {
        VkDebugUtilsMessengerCreateInfoEXT mi{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        mi.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        mi.pfnUserCallback = debug;
        check(reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT"))(instance, &mi, nullptr, &messenger));
    }
    check(vkEnumeratePhysicalDevices(instance, &n, nullptr));
    require(n != 0, "no Vulkan device");
    std::vector<VkPhysicalDevice> devices(n);
    check(vkEnumeratePhysicalDevices(instance, &n, devices.data()));
    physical = devices[0];
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical, &properties);
    std::printf("GPU: %s; validation %s\n", properties.deviceName, validated ? "on" : "unavailable");
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, nullptr);
    std::vector<VkQueueFamilyProperties> families(n);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &n, families.data());
    uint32_t family = 0;
    while (family < n && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT))
        ++family;
    require(family < n, "no graphics queue");
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    check(vkCreateDevice(physical, &dci, nullptr, &d));
    vkGetDeviceQueue(d, family, 0, &queue);
    std::vector<std::function<void()>> cleanup;
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physical, &mp);
    auto memory = [&](VkMemoryRequirements req, VkMemoryPropertyFlags flags) {
        uint32_t type = 0;
        while (type < mp.memoryTypeCount && (!(req.memoryTypeBits & (1u << type)) ||
                                             (mp.memoryTypes[type].propertyFlags & flags) != flags))
            ++type;
        require(type < mp.memoryTypeCount, "no memory type");
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = type;
        VkDeviceMemory m;
        check(vkAllocateMemory(d, &ai, nullptr, &m));
        return m;
    };
    struct Buffer {
        VkBuffer buffer;
        VkDeviceMemory memory;
        void *data;
    };
    auto buffer = [&](VkDeviceSize size, VkBufferUsageFlags usage) {
        Buffer b{};
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size;
        bi.usage = usage;
        check(vkCreateBuffer(d, &bi, nullptr, &b.buffer));
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(d, b.buffer, &req);
        b.memory = memory(req, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkBindBufferMemory(d, b.buffer, b.memory, 0));
        check(vkMapMemory(d, b.memory, 0, size, 0, &b.data));
        cleanup.emplace_back([=] {
            vkUnmapMemory(d, b.memory);
            vkDestroyBuffer(d, b.buffer, nullptr);
            vkFreeMemory(d, b.memory, nullptr);
        });
        return b;
    };
    Buffer input = buffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
           output = buffer(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT),
           uniform = buffer(512, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    std::memset(uniform.data, 0, 512);
    struct Image {
        VkImage image;
        VkImageView view;
        VkDeviceMemory memory;
    };
    auto image = [&](VkImageUsageFlags usage) {
        Image im{};
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = VK_FORMAT_R8G8B8A8_UNORM;
        ii.extent = {W, H, 1};
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = usage;
        check(vkCreateImage(d, &ii, nullptr, &im.image));
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(d, im.image, &req);
        im.memory = memory(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkBindImageMemory(d, im.image, im.memory, 0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = im.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = ii.format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(d, &vi, nullptr, &im.view));
        cleanup.emplace_back([=] {
            vkDestroyImageView(d, im.view, nullptr);
            vkDestroyImage(d, im.image, nullptr);
            vkFreeMemory(d, im.memory, nullptr);
        });
        return im;
    };
    Image src = image(VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT),
          dst = image(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VkSampler sampler;
    check(vkCreateSampler(d, &si, nullptr, &sampler));
    cleanup.emplace_back([=] { vkDestroySampler(d, sampler, nullptr); });
    VkDescriptorSetLayoutBinding bindings[2] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, &sampler},
        {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}};
    VkDescriptorSetLayoutCreateInfo sli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    sli.bindingCount = 2;
    sli.pBindings = bindings;
    VkDescriptorSetLayout sl;
    check(vkCreateDescriptorSetLayout(d, &sli, nullptr, &sl));
    cleanup.emplace_back([=] { vkDestroyDescriptorSetLayout(d, sl, nullptr); });
    VkDescriptorPoolSize ps[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
                                  {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = ps;
    VkDescriptorPool pool;
    check(vkCreateDescriptorPool(d, &dpi, nullptr, &pool));
    cleanup.emplace_back([=] { vkDestroyDescriptorPool(d, pool, nullptr); });
    VkDescriptorSetAllocateInfo sai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    sai.descriptorPool = pool;
    sai.descriptorSetCount = 1;
    sai.pSetLayouts = &sl;
    VkDescriptorSet set;
    check(vkAllocateDescriptorSets(d, &sai, &set));
    VkDescriptorImageInfo ii{sampler, src.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo bi{uniform.buffer, 0, 512};
    VkWriteDescriptorSet writes[2] = {{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET},
                                      {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}};
    writes[0].dstSet = set;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &ii;
    writes[1].dstSet = set;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[1].pBufferInfo = &bi;
    vkUpdateDescriptorSets(d, 2, writes, 0, nullptr);
    struct Push {
        float rot[4] = {1, 0, 0, 1}, off[4] = {0, 0, 0, 1}, box[4] = {0, 0, 1, 1}, crop[4] = {0, 0, 1, 1},
              aa[4] = {1.f / W, 1.f / H, 0, 0};
    };
    static_assert(sizeof(Push) == 80);
    VkPushConstantRange pr{VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(Push)};
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &sl;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pr;
    VkPipelineLayout pl;
    check(vkCreatePipelineLayout(d, &pli, nullptr, &pl));
    cleanup.emplace_back([=] { vkDestroyPipelineLayout(d, pl, nullptr); });
    VkAttachmentDescription color{};
    color.format = VK_FORMAT_R8G8B8A8_UNORM;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    VkAttachmentReference ar{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ar;
    VkSubpassDependency deps[2]{};
    deps[0] = {VK_SUBPASS_EXTERNAL,
               0,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
               0,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               0};
    deps[1] = {0,
               VK_SUBPASS_EXTERNAL,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               VK_ACCESS_TRANSFER_READ_BIT,
               0};
    VkRenderPassCreateInfo rpi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1;
    rpi.pAttachments = &color;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sub;
    rpi.dependencyCount = 2;
    rpi.pDependencies = deps;
    VkRenderPass rp;
    check(vkCreateRenderPass(d, &rpi, nullptr, &rp));
    cleanup.emplace_back([=] { vkDestroyRenderPass(d, rp, nullptr); });
    VkFramebufferCreateInfo fbi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbi.renderPass = rp;
    fbi.attachmentCount = 1;
    fbi.pAttachments = &dst.view;
    fbi.width = W;
    fbi.height = H;
    fbi.layers = 1;
    VkFramebuffer fb;
    check(vkCreateFramebuffer(d, &fbi, nullptr, &fb));
    cleanup.emplace_back([=] { vkDestroyFramebuffer(d, fb, nullptr); });
    auto module = [&](const uint32_t *code, size_t size) {
        VkShaderModuleCreateInfo mi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        mi.codeSize = size;
        mi.pCode = code;
        VkShaderModule m;
        check(vkCreateShaderModule(d, &mi, nullptr, &m));
        cleanup.emplace_back([=] { vkDestroyShaderModule(d, m, nullptr); });
        return m;
    };
    std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
    require(bool(file), "vertex module missing");
    size_t length = size_t(file.tellg());
    std::vector<uint32_t> vertex(length / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char *>(vertex.data()), length);
    VkShaderModule vs = module(vertex.data(), length), fs = module(kFragment, sizeof kFragment);
    VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},
                                                 {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs;
    stages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0, 0, W, H, 0, 1};
    VkRect2D scissor{{0, 0}, {W, H}};
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState ba{};
    ba.colorWriteMask = 15;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &ba;
    VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vin;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &blend;
    gp.layout = pl;
    gp.renderPass = rp;
    VkPipeline pipeline;
    check(vkCreateGraphicsPipelines(d, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline));
    cleanup.emplace_back([=] { vkDestroyPipeline(d, pipeline, nullptr); });
    VkCommandPoolCreateInfo cpi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    cpi.queueFamilyIndex = family;
    cpi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool cp;
    check(vkCreateCommandPool(d, &cpi, nullptr, &cp));
    cleanup.emplace_back([=] { vkDestroyCommandPool(d, cp, nullptr); });
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = cp;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb;
    check(vkAllocateCommandBuffers(d, &cai, &cb));
    auto render = [&](Push p) {
        check(vkResetCommandPool(d, cp, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        check(vkBeginCommandBuffer(cb, &begin));
        VkImageMemoryBarrier bar{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        bar.image = src.image;
        bar.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bar.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        bar.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.srcQueueFamilyIndex = bar.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bar.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &bar);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {W, H, 1};
        vkCmdCopyBufferToImage(cb, input.buffer, src.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        bar.oldLayout = bar.newLayout;
        bar.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        bar.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        bar.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                             nullptr, 0, nullptr, 1, &bar);
        VkRenderPassBeginInfo rb{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rb.renderPass = rp;
        rb.framebuffer = fb;
        rb.renderArea = {{0, 0}, {W, H}};
        vkCmdBeginRenderPass(cb, &rb, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &set, 0, nullptr);
        vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof p, &p);
        vkCmdDraw(cb, 3, 1, 0, 0);
        vkCmdEndRenderPass(cb);
        vkCmdCopyImageToBuffer(cb, dst.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, output.buffer, 1,
                               &region);
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0,
                             nullptr, 0, nullptr);
        check(vkEndCommandBuffer(cb));
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cb;
        check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
        check(vkQueueWaitIdle(queue));
        auto *out = static_cast<uint8_t *>(output.data);
        return std::vector<uint8_t>(out, out + bytes);
    };
    auto *pixels = static_cast<uint8_t *>(input.data);
    Push p;
    for (unsigned i = 0; i < bytes; i += 4) {
        pixels[i] = pixels[i + 1] = pixels[i + 2] = 127;
        pixels[i + 3] = 77;
    }
    p.aa[2] = 1;
    auto flat = render(p);
    for (unsigned i = 0; i < bytes; i += 4)
        require(flat[i] == 127 && flat[i + 1] == 127 && flat[i + 2] == 127 && flat[i + 3] == 255,
                "flat color/alpha changed");
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x) {
            unsigned i = (y * W + x) * 4;
            pixels[i] = pixels[i + 1] = pixels[i + 2] = (x * 5 > y * 3 + 35) ? 255 : 0;
            pixels[i + 3] = 0;
        }
    p.aa[2] = 0;
    auto off = render(p);
    for (unsigned i = 0; i < bytes; i += 4)
        require(off[i] == pixels[i] && off[i + 1] == pixels[i + 1] && off[i + 2] == pixels[i + 2] &&
                    off[i + 3] == 255,
                "Off sample mismatch");
    p.aa[2] = 1;
    auto aa = render(p);
    unsigned changed = 0, partial = 0;
    for (unsigned i = 0; i < bytes; i += 4) {
        changed += aa[i] != off[i];
        partial += aa[i] != 0 && aa[i] != 255;
        require(aa[i] == aa[i + 1] && aa[i] == aa[i + 2] && aa[i + 3] == 255, "FXAA tint/alpha");
    }
    require(changed > 40 && changed < 400 && partial == changed, "edge smoothing absent or excessive");
    p.aa[2] = 0;
    require(render(p) == off, "live FXAA off did not restore exact output");
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x) {
            unsigned i = (y * W + x) * 4;
            bool inside = x >= 16 && x < 48 && y >= 16 && y < 48;
            pixels[i] = inside ? 63 : 255;
            pixels[i + 1] = pixels[i + 2] = inside ? 63 : 0;
        }
    p.aa[2] = 1;
    p.crop[0] = p.crop[1] = .25f;
    p.crop[2] = p.crop[3] = .5f;
    auto crop = render(p);
    for (unsigned i = 0; i < bytes; i += 4)
        require(crop[i] == 63 && crop[i + 1] == 63 && crop[i + 2] == 63, "outside crop leaked into FXAA");
    p.box[0] = p.box[1] = .25f;
    p.box[2] = p.box[3] = .5f;
    auto bars = render(p);
    for (unsigned y = 0; y < H; ++y)
        for (unsigned x = 0; x < W; ++x) {
            unsigned i = (y * W + x) * 4;
            unsigned expected = x >= 16 && x < 48 && y >= 16 && y < 48 ? 63 : 0;
            require(bars[i] == expected && bars[i + 1] == expected && bars[i + 2] == expected,
                    "letterbox filtering changed bars");
        }
    auto *shapes = static_cast<float *>(uniform.data);
    shapes[0] = shapes[1] = .5f;
    shapes[2] = .1f;
    shapes[3] = 1;
    p.off[2] = 1;
    auto overlay = render(p);
    require(overlay[(32 * W + 32) * 4] == 255 && overlay[0] == 0, "overlay composition changed");
    check(vkDeviceWaitIdle(d));
    for (auto it = cleanup.rbegin(); it != cleanup.rend(); ++it)
        (*it)();
    vkDestroyDevice(d, nullptr);
    if (messenger)
        destroy_debug(instance, messenger, nullptr);
    vkDestroyInstance(instance, nullptr);
    require(errors == 0, "Vulkan validation errors");
    std::printf("PASS GPU FXAA: %u smoothed edge pixels; flat color, alpha, Off equality, live toggle, crop "
                "isolation, bars, overlay; validation errors %u\n",
                changed, errors.load());
    return 0;
} catch (const std::exception &e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
