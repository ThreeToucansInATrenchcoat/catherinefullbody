#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
namespace nx::nvn {
struct GpuImage;
PFN_vkGetInstanceProcAddr dlss_vulkan_loader(PFN_vkGetInstanceProcAddr native);
bool dlss_proxy_active();
VkImageLayout dlss_backbuffer_layout();
void dlss_adapter_selected(VkPhysicalDevice device);
void dlss_device_ready(VkPhysicalDevice device);
void dlss_scene_targets(GpuImage* const* images,unsigned count);
void dlss_scene_capture(VkCommandBuffer cb);
bool dlss_ui_needed(const char* vertex_hash);
void dlss_capture_hudless(VkCommandBuffer cb,GpuImage* color);
GpuImage* dlss_debug_hudless();
void dlss_render_begin();
void dlss_prepare_frame(VkCommandBuffer cb,GpuImage* color,unsigned output_width,unsigned output_height,unsigned backbuffers);
void dlss_backbuffer_ready(VkImage image,VkImageView view,VkFormat format,unsigned width,unsigned height);
void* dlss_render_end();
void dlss_present_begin(void* frame);
void dlss_present_end(void* frame);
void dlss_debug_record(VkCommandBuffer cb);
void dlss_debug_write();
uint64_t dlss_presented_count();
bool dlss_generating();
bool dlss_depth_snapshot_pending();
}
