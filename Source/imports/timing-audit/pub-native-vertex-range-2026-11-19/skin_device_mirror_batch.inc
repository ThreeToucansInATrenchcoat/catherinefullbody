void prepare_skin_mirrors(VkCommandBuffer cb) {
    if(!skin_mirror_mode() || !g_skin_mirror_pending || pass_open()) return;
    // A failed/unsupported batch stays on the host path. Retry only when a new skin version is produced.
    g_skin_mirror_pending=false;
    constexpr uint64_t budget=64ull<<20;
    uint64_t total=0;
    std::vector<It> selected;
    for(auto it=g_regions.begin();it!=g_regions.end();++it) {
        auto& r=it->second;
        if(!r.skin_vertex || r.gpu_written || r.skin_mirror.buffer || !r.size || !r.up.buffer || !r.up.ptr) continue;
        const uint64_t bytes=(r.size+15)&~15ull;
        if(bytes>r.cap || bytes>budget || total>budget-bytes) continue;
        total=(total+255)&~255ull;
        if(total>budget-bytes) continue;
        total+=bytes;selected.push_back(it);
    }
    if(selected.empty()) return;
    const Upload destination=frame_skin_vertex_alloc(total);
    if(!destination.buffer) {++g_skin_mirror_stats.failures;return;}
    std::map<VkBuffer,std::vector<VkBufferCopy>> copies;
    uint64_t offset=0;
    for(auto it:selected) {
        auto& r=it->second;offset=(offset+255)&~255ull;
        const uint64_t bytes=(r.size+15)&~15ull;
        copies[r.up.buffer].push_back({r.up.offset,destination.offset+offset,bytes});
        // ptr deliberately remains the cached host bytes for CPU validation/capture users of Upload.
        r.skin_mirror={destination.buffer,destination.offset+offset,r.up.ptr};r.skin_mirror_cap=bytes;
        r.gpu_accessed=true; // the recorded copy reads it; later CPU mutations must not change it in place
        offset+=bytes;++g_skin_mirror_stats.regions;g_skin_mirror_stats.bytes+=bytes;
    }
    for(const auto& [source,ranges]:copies)
        for(size_t first=0;first<ranges.size();first+=16)
            p_vkCmdCopyBuffer(cb,source,destination.buffer,uint32_t(std::min<size_t>(16,ranges.size()-first)),ranges.data()+first);
    VkBufferMemoryBarrier barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer=destination.buffer;barrier.offset=destination.offset;barrier.size=total;
    p_vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,0,nullptr,1,&barrier,0,nullptr);
    g_read=true;++g_skin_mirror_stats.batches;
}
void substitute_skin_vertex(const uint8_t* p,uint64_t size,uint64_t padded,Upload& upload) {
    if(!skin_mirror_mode() || !p || !size || g_regions.empty()) return;
    auto it=g_regions.upper_bound(p);if(it==g_regions.begin()) return;--it;
    auto& r=it->second;
    if(!r.skin_vertex || r.gpu_written || !r.skin_mirror.buffer || p<it->first) return;
    const uint64_t offset=uint64_t(p-it->first);
    if(offset>r.size || size>r.size-offset || offset>r.skin_mirror_cap || padded>r.skin_mirror_cap-offset) return;
    if(upload.buffer!=r.up.buffer || upload.offset!=r.up.offset+offset || upload.ptr!=r.up.ptr+offset) return;
    if(skin_mirror_mode()==2) {++g_skin_mirror_stats.host_controls;return;}
    upload={r.skin_mirror.buffer,r.skin_mirror.offset+offset,r.up.ptr+offset};++g_skin_mirror_stats.bindings;
}
