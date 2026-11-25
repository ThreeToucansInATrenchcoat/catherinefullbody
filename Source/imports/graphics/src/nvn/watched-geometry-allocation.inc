    bool create() {
        if (attempted) return shadow != nullptr;
        attempted = true;
        {
            VkDevice device = vk_device();
            VkBufferCreateInfo buffer_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            buffer_info.size = kArenaSize;
            buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
            if (p_vkCreateBuffer(device, &buffer_info, nullptr, &buffer) != VK_SUCCESS) return false;
            VkMemoryRequirements requirements{};
            p_vkGetBufferMemoryRequirements(device, buffer, &requirements);
            VkPhysicalDeviceMemoryProperties properties{};
            p_vkGetPhysicalDeviceMemoryProperties(vk_physical(), &properties);
            const bool host = std::getenv("NX_WATCHED_GEOMETRY_HOST") != nullptr;
            const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                (host ? VK_MEMORY_PROPERTY_HOST_CACHED_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            uint32_t type = UINT32_MAX;
            for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
                if ((requirements.memoryTypeBits & (1u << i)) &&
                    (properties.memoryTypes[i].propertyFlags & wanted) == wanted) {
                    type = i;
                    break;
                }
            }
            if (type != UINT32_MAX) {
                VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
                allocation.allocationSize = requirements.size;
                allocation.memoryTypeIndex = type;
                void* data = nullptr;
                if (p_vkAllocateMemory(device, &allocation, nullptr, &memory) == VK_SUCCESS &&
                    p_vkBindBufferMemory(device, buffer, memory, 0) == VK_SUCCESS &&
                    p_vkMapMemory(device, memory, 0, requirements.size, 0, &data) == VK_SUCCESS)
                    mapped = static_cast<uint8_t*>(data);
            }
            if (!mapped) {
                p_vkDestroyBuffer(device, buffer, nullptr);
                if (memory) p_vkFreeMemory(device, memory, nullptr);
                buffer = VK_NULL_HANDLE;
                memory = VK_NULL_HANDLE;
                log("nvn: geometry residency disabled: no suitable mapped Vulkan memory");
                return false;
            }
            log("nvn: watched geometry residency arena %llu MB in %s Vulkan memory type %u",
                (unsigned long long)(kArenaSize >> 20), host ? "host-cached" : "device-local", type);
        }
        shadow = static_cast<uint8_t*>(std::malloc(kArenaSize));
        if (!shadow) {
            p_vkUnmapMemory(vk_device(), memory);
            p_vkDestroyBuffer(vk_device(), buffer, nullptr);
            p_vkFreeMemory(vk_device(), memory, nullptr);
            buffer = VK_NULL_HANDLE;
            memory = VK_NULL_HANDLE;
            mapped = nullptr;
        }
        return shadow != nullptr;
    }
