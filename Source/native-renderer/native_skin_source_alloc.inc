// Each slot is fully retired before its cache can change. A source already
// consumed in this slot epoch is immutable until retirement, including when
// the same guest address is submitted again with different bytes.
struct NativeSourceStats {uint64_t requests=0,created=0,watch_hits=0,compare_hits=0,changed=0,budget=0,repeated=0,uploaded_bytes=0;} g_native_source_stats;
void frame_native_source_log() {
    const auto& s=g_native_source_stats;
    log("Native source cache: requests %llu entries %llu watched-hits %llu compared-hits %llu changed %llu budget-fallback %llu same-epoch-fallback %llu device-upload-bytes %llu",(unsigned long long)s.requests,(unsigned long long)s.created,(unsigned long long)s.watch_hits,(unsigned long long)s.compare_hits,(unsigned long long)s.changed,(unsigned long long)s.budget,(unsigned long long)s.repeated,(unsigned long long)s.uploaded_bytes);
}
struct NativeSourceWatch {
    uint64_t since=0,allocation=0;bool available=false,clean=false;
};
NativeSourceWatch native_source_watch_begin(const uint8_t* key,uint64_t size,const uint8_t* bytes,const NativeSourceEntry& entry) {
    static const bool on=[] {const auto* e=std::getenv("CATHERINE_NATIVE_SOURCE_WATCH");return !e||std::strcmp(e,"0");}();
    NativeSourceWatch watch;watch.since=entry.watch_since;watch.allocation=entry.watch_allocation;
    bool changed=true;
    if(on&&key==bytes)watch.available=replay::texture_write_watch(key,size,watch.since,watch.allocation,changed);
    watch.clean=watch.available&&!changed;
    return watch;
}
void native_source_watch_finish(const uint8_t* key,uint64_t size,NativeSourceEntry& entry,NativeSourceWatch watch) {
    bool changed=true;
    if(watch.available&&replay::texture_write_watch(key,size,watch.since,watch.allocation,changed)&&!changed) {
        entry.watch_since=watch.since;entry.watch_allocation=watch.allocation;
    } else entry.watch_since=entry.watch_allocation=0;
}

NativeSourceUpload frame_native_source(VkCommandBuffer cb,const uint8_t* key,uint64_t size,const uint8_t* bytes,uint32_t matrices,bool tangent) {
    if(!g_current||!g_current->replay||!g_current->ok||!size)return {};
    static const bool enabled=[] {const auto* e=std::getenv("CATHERINE_NATIVE_SOURCE_CACHE");return !e||std::strcmp(e,"0");}();
    if(!enabled)return {};
    auto& slot=*g_current;++g_native_source_stats.requests;
    const auto identity=std::make_tuple(reinterpret_cast<uintptr_t>(key),size,matrices,tangent);
    auto it=slot.native_sources.find(identity);bool changed=false;
    if(it==slot.native_sources.end()) {
        constexpr uint64_t budget=32ull<<20;
        if(size>budget||slot.native_source_bytes>budget-size||slot.native_sources.size()>=512){++g_native_source_stats.budget;return {};}
        NativeSourceEntry entry;
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=size;
        bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        const auto device=vk_device();
        if(p_vkCreateBuffer(device,&bi,nullptr,&entry.buffer)!=VK_SUCCESS)return {};
        VkMemoryRequirements requirements{};p_vkGetBufferMemoryRequirements(device,entry.buffer,&requirements);
        VkPhysicalDeviceMemoryProperties properties{};p_vkGetPhysicalDeviceMemoryProperties(vk_physical(),&properties);
        constexpr auto flags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        uint32_t type=UINT32_MAX;
        for(uint32_t i=0;i<properties.memoryTypeCount;++i)if((requirements.memoryTypeBits&(1u<<i))&&(properties.memoryTypes[i].propertyFlags&flags)==flags){type=i;break;}
        VkMemoryAllocateFlagsInfo address_flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};address_flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.pNext=&address_flags;ai.allocationSize=requirements.size;ai.memoryTypeIndex=type;
        void* pointer=nullptr;
        if(type==UINT32_MAX||p_vkAllocateMemory(device,&ai,nullptr,&entry.memory)!=VK_SUCCESS||
           p_vkBindBufferMemory(device,entry.buffer,entry.memory,0)!=VK_SUCCESS||
           p_vkMapMemory(device,entry.memory,0,VK_WHOLE_SIZE,0,&pointer)!=VK_SUCCESS) {
            p_vkDestroyBuffer(device,entry.buffer,nullptr);if(entry.memory)p_vkFreeMemory(device,entry.memory,nullptr);return {};
        }
        entry.ptr=static_cast<uint8_t*>(pointer);
        VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};address.buffer=entry.buffer;
        entry.address=p_vkGetBufferDeviceAddress(device,&address);
        if(!entry.address){p_vkUnmapMemory(device,entry.memory);p_vkDestroyBuffer(device,entry.buffer,nullptr);p_vkFreeMemory(device,entry.memory,nullptr);return {};}
        // Keep the immutable source in VRAM when available. The cached host
        // copy proves equality and supplies changed versions to the transfer.
        static const bool device_source=[] {const auto* e=std::getenv("CATHERINE_NATIVE_DEVICE_SOURCE");return !e||std::strcmp(e,"0");}();
        if(device_source) {
            VkBufferCreateInfo gpu_info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};gpu_info.size=size;
            gpu_info.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            if(p_vkCreateBuffer(device,&gpu_info,nullptr,&entry.gpu_buffer)==VK_SUCCESS) {
                VkMemoryRequirements gpu_req{};p_vkGetBufferMemoryRequirements(device,entry.gpu_buffer,&gpu_req);
                uint32_t gpu_type=UINT32_MAX;
                for(uint32_t i=0;i<properties.memoryTypeCount;++i)if((gpu_req.memoryTypeBits&(1u<<i))&&(properties.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){gpu_type=i;break;}
                VkMemoryAllocateInfo gpu_ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};gpu_ai.pNext=&address_flags;gpu_ai.allocationSize=gpu_req.size;gpu_ai.memoryTypeIndex=gpu_type;
                bool ok=gpu_type!=UINT32_MAX&&p_vkAllocateMemory(device,&gpu_ai,nullptr,&entry.gpu_memory)==VK_SUCCESS&&p_vkBindBufferMemory(device,entry.gpu_buffer,entry.gpu_memory,0)==VK_SUCCESS;
                VkDeviceAddress gpu_address=0;
                if(ok){address.buffer=entry.gpu_buffer;gpu_address=p_vkGetBufferDeviceAddress(device,&address);}
                if(gpu_address)entry.address=gpu_address;
                else {p_vkDestroyBuffer(device,entry.gpu_buffer,nullptr);if(entry.gpu_memory)p_vkFreeMemory(device,entry.gpu_memory,nullptr);entry.gpu_buffer=VK_NULL_HANDLE;entry.gpu_memory=VK_NULL_HANDLE;}
            }
        }
        const auto watch=native_source_watch_begin(key,size,bytes,entry);
        std::memcpy(entry.ptr,bytes,size);changed=true;
        native_source_watch_finish(key,size,entry,watch);++g_native_source_stats.created;
        it=slot.native_sources.emplace(identity,std::move(entry)).first;
        slot.native_source_bytes+=size;
    } else {
        auto& entry=it->second;auto watch=native_source_watch_begin(key,size,bytes,entry);
        static const bool verify_watch=[] {const auto* e=std::getenv("CATHERINE_NATIVE_SOURCE_WATCH_VERIFY");return e&&!std::strcmp(e,"1");}();
        if(watch.clean&&verify_watch) {
            const bool identical=!std::memcmp(entry.ptr,bytes,size);
            bool dirty=true;const bool ok=replay::texture_write_watch(key,size,watch.since,watch.allocation,dirty);
            if(!identical&&ok&&!dirty) {log("Native source watch returned a false clean result");std::abort();}
            if(!ok||dirty)watch.clean=false;
        }
        if(watch.clean) {entry.watch_since=watch.since;entry.watch_allocation=watch.allocation;++g_native_source_stats.watch_hits;}
        else {
            if(std::memcmp(entry.ptr,bytes,size)) {
                if(entry.used_epoch==slot.native_source_epoch){++g_native_source_stats.repeated;return {};}
                std::memcpy(entry.ptr,bytes,size);entry.validated=false;changed=true;++g_native_source_stats.changed;
            } else ++g_native_source_stats.compare_hits;
            native_source_watch_finish(key,size,entry,watch);
        }
    }
    auto& entry=it->second;entry.used_epoch=slot.native_source_epoch;
    if((changed||!entry.gpu_uploaded)&&entry.gpu_buffer) {VkBufferCopy copy{0,0,size};p_vkCmdCopyBuffer(cb,entry.buffer,entry.gpu_buffer,1,&copy);entry.gpu_uploaded=true;g_native_source_stats.uploaded_bytes+=size;}
    return {{entry.buffer,0,entry.ptr},entry.address,&entry,entry.validated};
}
void frame_native_source_validated(void* cookie) {
    if(cookie)static_cast<NativeSourceEntry*>(cookie)->validated=true;
}
