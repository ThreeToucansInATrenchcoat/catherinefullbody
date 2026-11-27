// Private experiment: all calls execute on the existing replay owner.
namespace {
experimental_skin::Pipeline g_native_pipeline;
bool g_native_pipeline_failed=false;
uint64_t g_native_verify_skipped=0,g_native_owned_sources=0,g_native_owned_bytes=0;
uint64_t g_native_done=0,g_native_vertices=0,g_native_rejected=0,g_native_bytes=0;
std::mutex g_native_verify_mutex;
std::unordered_map<const uint8_t*,std::shared_ptr<std::vector<uint8_t>>> g_native_verifications;
std::atomic<uint64_t> g_native_verified{0},g_native_verified_bytes{0};
bool native_skin_enabled() {static const bool on=[] {const auto* e=std::getenv("CATHERINE_NATIVE_SKIN");return catherine_native_gpu_skinning_enabled()&&(!e||std::strcmp(e,"0"));}();return on;}
bool native_skin_verify_enabled() {static const bool on=[] {const auto* e=std::getenv("CATHERINE_NATIVE_SKIN_VERIFY");return e&&!std::strcmp(e,"1");}();return on;}
__attribute__((target("avx2"))) void native_preserved_words_avx(const uint8_t* source,uint32_t* output,uint32_t count,bool tangent,bool prefetch) {
    const uint32_t stride=tangent?48:32;
    uint32_t i=0;
    const __m256i offsets=_mm256_setr_epi32(28,28+stride,28+2*stride,28+3*stride,28+4*stride,28+5*stride,28+6*stride,28+7*stride);
    for(;i+8<=count;i+=8) {
        if(prefetch&&count-i>136u){const auto* next=source+uint64_t(i+128)*stride;for(uint32_t at=0;at<stride*8;at+=64)_mm_prefetch(reinterpret_cast<const char*>(next+at),_MM_HINT_T0);}
        const auto* base=reinterpret_cast<const int*>(source+uint64_t(i)*stride);
        const __m256i normal=_mm256_i32gather_epi32(base,offsets,1);
        if(tangent) {
            const __m256i tangent_w=_mm256_i32gather_epi32(base,_mm256_add_epi32(offsets,_mm256_set1_epi32(16)),1);
            const __m256i lo=_mm256_unpacklo_epi32(normal,tangent_w),hi=_mm256_unpackhi_epi32(normal,tangent_w);
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(output+uint64_t(i)*2),_mm256_permute2x128_si256(lo,hi,0x20));
            _mm256_storeu_si256(reinterpret_cast<__m256i*>(output+uint64_t(i)*2+8),_mm256_permute2x128_si256(lo,hi,0x31));
        } else _mm256_storeu_si256(reinterpret_cast<__m256i*>(output+i),normal);
    }
    for(;i<count;++i) {
        std::memcpy(output+uint64_t(i)*(tangent?2:1),source+uint64_t(i)*stride+28,4);
        if(tangent)std::memcpy(output+uint64_t(i)*2+1,source+uint64_t(i)*stride+44,4);
    }
}
void native_preserved_words_scalar(const uint8_t* source,uint32_t* output,uint32_t count,bool tangent,bool prefetch) {
    const uint32_t stride=tangent?48:32;
    for(uint32_t i=0;i<count;++i){
        if(prefetch&&!(i&15u)&&count-i>144u){const auto* next=source+uint64_t(i+128)*stride;for(uint32_t at=0;at<stride*16;at+=64)_mm_prefetch(reinterpret_cast<const char*>(next+at),_MM_HINT_T0);}
        std::memcpy(output+uint64_t(i)*(tangent?2:1),source+uint64_t(i)*stride+28,4);
        if(tangent)std::memcpy(output+uint64_t(i)*2+1,source+uint64_t(i)*stride+44,4);
    }
}
void native_preserved_words(const uint8_t* source,uint32_t* output,uint32_t count,bool tangent) {
    static const int mode=[] {const auto* e=std::getenv("CATHERINE_NATIVE_GATHER");return e?std::atoi(e):1;}();
    if(mode==0||mode==3)native_preserved_words_scalar(source,output,count,tangent,mode==3);
    else native_preserved_words_avx(source,output,count,tangent,mode==2);
}
// Cached sources already have validated joints. Only the live palette bounds
// need rechecking; no CPU-transposed palette is consumed by the GPU kernel.
__attribute__((target("avx2"))) bool native_palette_valid(const uint8_t* bytes,uint32_t count) {
    const __m256i magnitude=_mm256_set1_epi32(0x7fffffff),limit=_mm256_set1_epi32(0x4e800000);
    __m256i bad=_mm256_setzero_si256();
    for(uint32_t n=0;n<count;++n) {
        const auto* matrix=bytes+uint64_t(n)*64;
        const auto first=_mm256_loadu_si256(reinterpret_cast<const __m256i*>(matrix));
        const auto third=_mm256_zextsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i*>(matrix+32)));
        bad=_mm256_or_si256(bad,_mm256_cmpgt_epi32(_mm256_and_si256(first,magnitude),limit));
        bad=_mm256_or_si256(bad,_mm256_cmpgt_epi32(_mm256_and_si256(third,magnitude),limit));
    }
    return _mm256_movemask_epi8(bad)==0;
}
enum NativePhase {Source,Palette,Validation,Prepare,Address,Record,Commit,Phases};
uint64_t g_native_phase_ns[Phases]{};
bool native_profile(){static const bool on=std::getenv("CATHERINE_NATIVE_PROFILE")!=nullptr;return on;}
uint64_t native_tick(){return native_profile()?uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()):0;}
void native_time(NativePhase p,uint64_t& t){if(!native_profile())return;auto now=native_tick();g_native_phase_ns[p]+=now-t;t=now;}
void native_skin_log() {
    frame_native_source_log();
    experimental_geometry::report();
    log("Provenance pages: queries %llu negatives %llu",(unsigned long long)g_verified_skin_outputs.page_queries,(unsigned long long)g_verified_skin_outputs.page_rejects);
    if(native_profile()&&g_native_done)log("Native CPU costs per accepted call us: source %.2f palette %.2f validation %.2f prepare %.2f address %.2f record %.2f commit %.2f",double(g_native_phase_ns[0])/g_native_done/1e3,double(g_native_phase_ns[1])/g_native_done/1e3,double(g_native_phase_ns[2])/g_native_done/1e3,double(g_native_phase_ns[3])/g_native_done/1e3,double(g_native_phase_ns[4])/g_native_done/1e3,double(g_native_phase_ns[5])/g_native_done/1e3,double(g_native_phase_ns[6])/g_native_done/1e3);
    log("Native owned sources: regions %llu CPU-copy bytes avoided %llu",(unsigned long long)g_native_owned_sources,(unsigned long long)g_native_owned_bytes);
    log("Native GPU skin: calls %llu vertices %llu admission-rejections %llu upload-bytes %llu verified-regions %llu verified-bytes %llu",
        (unsigned long long)g_native_done,(unsigned long long)g_native_vertices,(unsigned long long)g_native_rejected,
        (unsigned long long)g_native_bytes,(unsigned long long)g_native_verified.load(),(unsigned long long)g_native_verified_bytes.load());
}
}

void native_skin_verify(const WriteBack& w) {
    if(!native_skin_verify_enabled())return;
    std::shared_ptr<std::vector<uint8_t>> expected;
    {std::lock_guard lock(g_native_verify_mutex);auto it=g_native_verifications.find(w.from);
     if(it==g_native_verifications.end())return;expected=std::move(it->second);g_native_verifications.erase(it);}
    if(expected->size()!=w.size)fatal("Native skin verification size mismatch");
    if(std::memcmp(expected->data(),w.from,w.size)) {
        for(uint64_t i=0;i<w.size;++i)if((*expected)[i]!=w.from[i]) {
            log("Native skin mismatch byte %llu CPU %02x GPU %02x size %llu",(unsigned long long)i,unsigned((*expected)[i]),unsigned(w.from[i]),(unsigned long long)w.size);break;
        }
        fatal("Native skin GPU result differs from CPU reference");
    }
    ++g_native_verified;g_native_verified_bytes+=w.size;
}

bool native_skin_early_submit() {
    static const bool on=[] {const auto* e=std::getenv("CATHERINE_NATIVE_EARLY");return !e||std::strcmp(e,"0");}();
    return on&&native_skin_enabled()&&async_gpu()&&!pipelined();
}
void native_skin_flush_readbacks(VkCommandBuffer cb) {
    bool any=false;for(const auto& [p,r]:g_regions)any|=r.native_readback.buffer!=VK_NULL_HANDLE&&!r.native_flushed;
    if(!any)return;
    VkMemoryBarrier before{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    before.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    p_vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&before,0,nullptr,0,nullptr);
    for(auto& [p,r]:g_regions)if(r.native_readback.buffer&&!r.native_flushed) {
        VkBufferCopy copy{r.up.offset,r.native_readback.offset,r.size};
        p_vkCmdCopyBuffer(cb,r.up.buffer,r.native_readback.buffer,1,&copy);r.native_flushed=true;
    }
    VkMemoryBarrier after{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;after.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    p_vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,nullptr,0,nullptr);
}

bool try_native_skin(VkCommandBuffer cb,const ShaderInfo& cs,uint32_t x,uint32_t y,uint32_t z) {
    if(!native_skin_enabled()||!async_gpu()||pipelined()||written_separate()||!descriptor_ring_address()||!vk_features().shaderInt64)return false;
    static const bool float_controls=experimental_skin::supported(vk_physical());
    if(!float_controls)return false;
    static CpuShaderSignature regular{"eb49fe48c6f84ebf",0xda83f468f5fb7ec9ull};
    static CpuShaderSignature with_tangent{"01c7cddd0aab2b5c",0x1b818c23fea4dab9ull};
    const bool tangent=cpu_shader_matches(cs,with_tangent);
    if(!tangent&&!cpu_shader_matches(cs,regular))return false;
    auto timer=native_tick();
    auto reject=[] {++g_native_rejected;return false;};
    if(g_graphics_storage_seen||y!=1||z!=1||x>UINT32_MAX/64||!cpu_kernel_bindings(cs,1,0,2,2))return reject();
    const auto& args=g_state.uniforms[5][0];const auto& src=g_state.storage[5][1];
    const auto& pal=g_state.storage[5][0];const auto& dst=g_state.storage[5][2];
    const auto* arg=args.size>=4?guest_bytes(args.address):nullptr;
    const auto* source=guest_bytes(src.address);const auto* palette=guest_bytes(pal.address);const auto* destination=guest_bytes(dst.address);
    if(!arg||!source||!palette||!destination)return reject();
    const auto* av=cpu_view(arg,4);if(!av)return reject();
    uint32_t count;std::memcpy(&count,av,4);count=std::min(count,x*64);
    constexpr uint64_t cap=16ull<<20;
    const uint64_t source_size=uint64_t(count)*(tangent?80:64),used=uint64_t(count)*(tangent?48:32);
    const uint64_t size=std::min(dst.size,cap);const uint32_t matrices=uint32_t(std::min(pal.size,cap)/64);
    const uint64_t palette_size=uint64_t(matrices)*64;
    if(size%4||!count||source_size>std::min(src.size,cap)||used>size||!matrices||matrices>4096)return reject();
    if(!aligned_disjoint(source,source_size,destination,size)||!aligned_disjoint(palette,palette_size,destination,size))return reject();
    const auto* sv=skin_readable_view(source,source_size);const auto* pv=cpu_view(palette,palette_size);if(!sv||!pv)return reject();
    static const bool use_owned=[] {const auto* e=std::getenv("CATHERINE_NATIVE_OWNED_SOURCE");return !e||std::strcmp(e,"0");}();
    It source_owner=use_owned?containing(source,source_size,source_size,1,overlapping(source,source_size)):g_regions.end();
    if(source_owner!=g_regions.end()&&(source_owner->second.gpu_written||source_owner->second.up.buffer!=descriptor_ring_buffer()))source_owner=g_regions.end();
    It existing;if(!find_cpu_destination(destination,size,existing)||existing!=g_regions.end())return reject();
    if(g_native_pipeline_failed)return false;
    if(!g_native_pipeline.layout&&!g_native_pipeline.create(vk_device())) {g_native_pipeline_failed=true;return false;}
    // Reserve headroom for all three allocations, alignment and a possible ring lap.
    const uint64_t capacity=cpu_destination_capacity(size),needed=source_size+palette_size+capacity+uint64_t(count)*(tangent?8:4)+4096;
    if(needed>frame_ring_size()/2||frame_upload_used()>frame_ring_size()/2-needed)return reject();
    Upload device=frame_skin_vertex_alloc(capacity);if(!device.buffer)return reject();
    VkBufferDeviceAddressInfo address{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};address.buffer=device.buffer;
    static std::unordered_map<VkBuffer,VkDeviceAddress> addresses;
    auto [entry,new_buffer]=addresses.try_emplace(device.buffer,0);
    if(new_buffer)entry->second=p_vkGetBufferDeviceAddress(vk_device(),&address);
    const auto device_address=entry->second;if(!device_address)return reject();
    native_time(Address,timer);
    // Snapshot before validation: producer writes cannot invalidate the admission proof.
    NativeSourceUpload cached_source{};
    Upload source_upload{};VkDeviceAddress source_address=0;
    if(source_owner!=g_regions.end()) {
        source_upload=at(source_owner,uint64_t(source-source_owner->first));
        source_address=descriptor_ring_address()+source_upload.offset;
    } else {
        cached_source=frame_native_source(cb,source,source_size,sv,matrices,tangent);
        source_upload=cached_source.upload;source_address=cached_source.address;
        if(!source_upload.buffer) {source_upload=upload_alloc(source_size,16);std::memcpy(source_upload.ptr,sv,source_size);source_address=descriptor_ring_address()+source_upload.offset;}
    }
    sv=source_upload.ptr;native_time(Source,timer);
    pv=sheep_pose_palette(pv,palette_size,count,pal.address);
    pv=vincent_pose_palette(pv,palette_size,src.address,pal.address);
    pv=spike_pose_palette(pv,palette_size,sv,source_size,count,tangent?80:64);
    pv=campaign_block_pose_palette(pv,palette_size,sv,source_size,count,tangent?80:64);
    pv=block20_pose_palette(pv,palette_size,sv,source_size,count,tangent?80:64);
    const auto* before=pv;
    if(palette_size==960)pv=root12_pose_palette(pv,palette_size,sv,source_size,count,tangent?80:64);
    const bool root12=pv!=before;before=pv;
    if(palette_size==960)pv=root9_pose_palette(pv,palette_size,sv,source_size,count,tangent?80:64);
    const bool root9=pv!=before;
    Upload palette_upload=upload_alloc(palette_size,16);std::memcpy(palette_upload.ptr,pv,palette_size);pv=palette_upload.ptr;native_time(Palette,timer);
    static std::vector<SkinJoints> joints;SkinReject reason{};uint32_t element=0;
    static std::vector<SkinMatrix> transposed;static std::vector<uint8_t> canonical;
    uint64_t canonical_count=0;SkinJoints bad_joints;
    const bool source_verified=cached_source.validated&&!native_skin_verify_enabled();
    if(source_verified) {
        const bool admitted=native_palette_valid(pv,matrices);
        static const int check=[] {const auto* e=std::getenv("CATHERINE_NATIVE_PALETTE_CHECK");return e?std::atoi(e):1;}();
        if(check==2&&admitted!=transpose_skin_palette(pv,matrices,transposed,element))fatal("Native palette admission differs from CPU transpose admission");
        if(!admitted)return reject();
    } else if(!transpose_skin_palette(pv,matrices,transposed,element))return reject();
    static const bool canonical_positions=canonical_skin_nan_opt_in("NX_CPU_SKIN_NAN_POSITIONS");
    static const bool canonical_tangents=canonical_skin_nan_opt_in("NX_CPU_SKIN_NAN_TANGENTS");
    const bool canonical_allowed=tangent?canonical_tangents:canonical_positions;
    if(!source_verified){joints.resize(count);canonical.resize(count);}
    const bool valid=source_verified || (tangent
        ?validate_async_skin_source<true>(sv,transposed,nullptr,nullptr,count,canonical_allowed,reason,element,bad_joints,canonical_count,joints.data(),canonical.data())
        :validate_async_skin_source<false>(sv,transposed,nullptr,nullptr,count,canonical_allowed,reason,element,bad_joints,canonical_count,joints.data(),canonical.data()));
    if(!valid)return reject();
    frame_native_source_validated(cached_source.cookie);
    native_time(Validation,timer);
    Region r=prepare_fresh_skin_destination(destination,size);
    const uint8_t* preserved=r.before?r.before->data():destination;
    Upload preserved_upload=upload_alloc(uint64_t(count)*(tangent?8:4),16);
    auto* preserved_words=reinterpret_cast<uint32_t*>(preserved_upload.ptr);
    native_preserved_words(preserved,preserved_words,count,tangent);
    // The kernel fills every produced byte. Only unused records/padding need a seed.
    if(used<size)std::memcpy(r.up.ptr+used,preserved+used,size-used);
    std::memset(r.up.ptr+size,0,r.cap-size);
    if(palette_upload.buffer!=descriptor_ring_buffer()||r.up.buffer!=descriptor_ring_buffer()||preserved_upload.buffer!=descriptor_ring_buffer())return reject();
    if(native_skin_verify_enabled()) {
        r.native_expected=std::make_shared<std::vector<uint8_t>>(size,0);
        auto* expected=reinterpret_cast<uint32_t*>(r.native_expected->data());
        for(uint32_t i=0;i<count;++i){expected[uint64_t(i)*(tangent?12:8)+7]=preserved_words[uint64_t(i)*(tangent?2:1)];if(tangent)expected[uint64_t(i)*12+11]=preserved_words[uint64_t(i)*2+1];}
        if(used<size)std::memcpy(r.native_expected->data()+used,r.up.ptr+used,size-used);
        run_cpu_skin(reinterpret_cast<const float*>(sv),reinterpret_cast<const float*>(pv),reinterpret_cast<float*>(r.native_expected->data()),count,matrices,tangent,joints.data());
        for(uint32_t i=0;i<count;++i)if(canonical[i])for(unsigned c=0;c<3;++c)expected[uint64_t(i)*(tangent?12:8)+(tangent?8:0)+c]=0x7fffffff;
    }
    native_time(Prepare,timer);
    // An owned CPU morph result is already a stable upload. Copy that version
    // on the GPU; freeze it against subsequent CPU in-place writers.
    if(source_owner!=g_regions.end()) {
        Upload input=frame_skin_vertex_alloc(source_size);if(!input.buffer)return reject();
        VkBufferDeviceAddressInfo input_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};input_info.buffer=input.buffer;
        auto [input_entry,input_new]=addresses.try_emplace(input.buffer,0);
        if(input_new)input_entry->second=p_vkGetBufferDeviceAddress(vk_device(),&input_info);
        if(!input_entry->second)return reject();
        source_address=input_entry->second+input.offset;
        source_owner->second.gpu_accessed=true;g_read=true;
        VkBufferCopy input_copy{source_upload.offset,input.offset,source_size};
        p_vkCmdCopyBuffer(cb,source_upload.buffer,input.buffer,1,&input_copy);
        ++g_native_owned_sources;g_native_owned_bytes+=source_size;
    }
    // Seed preserved w lanes, untouched vertices, and descriptor padding.
    if(used<r.cap){VkBufferCopy copy{r.up.offset+used,device.offset+used,r.cap-used};p_vkCmdCopyBuffer(cb,r.up.buffer,device.buffer,1,&copy);}
    VkMemoryBarrier transfer{VK_STRUCTURE_TYPE_MEMORY_BARRIER};transfer.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;transfer.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    p_vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&transfer,0,nullptr,0,nullptr);
    const auto ring=descriptor_ring_address();
    g_native_pipeline.record(cb,{source_address,ring+palette_upload.offset,device_address+device.offset,ring+preserved_upload.offset,count,matrices},tangent);
    VkMemoryBarrier computed{VK_STRUCTURE_TYPE_MEMORY_BARRIER};computed.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;computed.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    p_vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&computed,0,nullptr,0,nullptr);
    native_time(Record,timer);
    r.native_readback=r.up;r.up={device.buffer,device.offset,r.native_readback.ptr};r.gpu_written=r.gpu_accessed=true;
    g_regions.emplace(destination,std::move(r));mark_written_region(destination,size);dedup_invalidate(destination,size);
    if(root12||root9)written_pose_palette_interpolated();
    native_time(Commit,timer);
    ++g_binds;
    ++g_native_done;g_native_vertices+=count;g_native_bytes+=source_size+palette_size+size;
    return true;
}
