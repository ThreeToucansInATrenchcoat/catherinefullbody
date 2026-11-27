// Called only by the replay owner under the existing GPU lock, outside rendering.
// Slot storage is reused only after frame_begin has retired its old serial and waited for the janitor.
Upload frame_skin_vertex_alloc(uint64_t size) {
    constexpr uint64_t budget=64ull<<20;
    if(!g_current || !g_current->replay || !g_current->ok || !size || size>budget) return {};
    auto& blocks=g_current->skin_mirrors;
    for(auto& b:blocks) {
        const uint64_t at=(b.used+255)&~255ull;
        if(at<=b.capacity && size<=b.capacity-at) {b.used=at+size;return {b.buffer,at,nullptr};}
    }
    uint64_t retained=0;for(const auto& b:blocks) retained+=b.capacity;
    uint64_t capacity=1ull<<20;while(capacity<size) capacity*=2;
    if(retained>budget || capacity>budget-retained) return {};
    if(!descriptor_ring_address())return {};
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size=capacity;ci.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_VERTEX_BUFFER_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    SkinMirrorBlock block;
    if(p_vkCreateBuffer(vk_device(),&ci,nullptr,&block.buffer)!=VK_SUCCESS) return {};
    VkMemoryRequirements requirements{};p_vkGetBufferMemoryRequirements(vk_device(),block.buffer,&requirements);
    VkPhysicalDeviceMemoryProperties properties{};p_vkGetPhysicalDeviceMemoryProperties(vk_physical(),&properties);
    uint32_t type=UINT32_MAX;
    for(uint32_t i=0;i<properties.memoryTypeCount;++i)
        if((requirements.memoryTypeBits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {type=i;break;}
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
    flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;ai.pNext=&flags;
    ai.allocationSize=requirements.size;ai.memoryTypeIndex=type;
    if(type==UINT32_MAX || p_vkAllocateMemory(vk_device(),&ai,nullptr,&block.memory)!=VK_SUCCESS ||
       p_vkBindBufferMemory(vk_device(),block.buffer,block.memory,0)!=VK_SUCCESS) {
        p_vkDestroyBuffer(vk_device(),block.buffer,nullptr);
        if(block.memory)p_vkFreeMemory(vk_device(),block.memory,nullptr);return {};
    }
    block.capacity=capacity;block.used=size;blocks.push_back(block);
    return {block.buffer,0,nullptr};
}
