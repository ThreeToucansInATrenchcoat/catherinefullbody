#pragma once
namespace nx::nvn {
struct NativeSourceUpload {
    Upload upload{};
    VkDeviceAddress address=0;
    void* cookie=nullptr;
    bool validated=false;
};
NativeSourceUpload frame_native_source(VkCommandBuffer,const uint8_t*,uint64_t,const uint8_t*,uint32_t,bool);
void frame_native_source_validated(void*);
void frame_native_source_log();
}
