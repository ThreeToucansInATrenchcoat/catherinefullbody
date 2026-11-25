namespace native_transfers {
GpuImage* resolve(VkCommandBuffer cb,uint64_t key) {
    const auto binding=native_bindings::capture(key,get<Texture>(key),VK_NULL_HANDLE,ViewInfo{});
    if(auto* value=binding.get()) {++census.bound;return ensure_contents_bound(cb,*value);}
    return ensure_contents(cb,key);
}
// An independent pure lowering of the original replay decisions, used only by
// mode 2. It does not resolve resources, upload contents or record Vulkan work.
Prepared legacy_prepare(const Legacy& old,const GpuImage& src,const GpuImage& dst,bool same) {
    Prepared out;
    const bool s3=src.view_type==VK_IMAGE_VIEW_TYPE_3D,d3=dst.view_type==VK_IMAGE_VIEW_TYPE_3D;
    auto sub=[](const GpuImage& image,uint32_t level,int z,int depth,uint32_t base,bool volume) {
        const auto layer=volume?0:std::min<uint32_t>(uint32_t(std::max(z,0))+base,image.layers-1);
        const auto count=volume?1:std::clamp<uint32_t>(uint32_t(std::max(depth,1)),1,image.layers-layer);
        return VkImageSubresourceLayers{image.aspect,level,layer,count};
    };
    const auto ss=sub(src,std::min(old.src_view.base_level,src.levels-1),old.src[2],old.src[5],old.src_view.min_layer,s3);
    const auto ds=sub(dst,std::min(old.dst_view.base_level,dst.levels-1),old.dst[2],old.dst[5],old.dst_view.min_layer,d3);
    const int sw=std::max(old.src[3],1),sh=std::max(old.src[4],1),dw=std::max(old.dst[3],1),dh=std::max(old.dst[4],1);
    out.overlap=same&&ss.mipLevel==ds.mipLevel&&ranges_meet(ss.baseArrayLayer,ss.layerCount,ds.baseArrayLayer,ds.layerCount)&&
        ranges_meet(old.src[0],sw,old.dst[0],dw)&&ranges_meet(old.src[1],sh,old.dst[1],dh)&&
        (!s3||ranges_meet(old.src[2],std::max(old.src[5],1),old.dst[2],std::max(old.dst[5],1)));
    out.direct=src.format==dst.format&&sw==dw&&sh==dh&&!(old.flags&(4|8|16))&&ss.layerCount==ds.layerCount;
    out.blittable=!compressed(src.format)&&!compressed(dst.format)&&src.aspect==dst.aspect;
    out.copy.srcSubresource=ss;out.copy.dstSubresource=ds;
    out.copy.srcOffset={old.src[0],old.src[1],s3?old.src[2]:0};out.copy.dstOffset={old.dst[0],old.dst[1],d3?old.dst[2]:0};
    out.copy.extent={uint32_t(sw),uint32_t(sh),s3?uint32_t(std::max(old.src[5],1)):1u};
    out.blit.srcSubresource=ss;out.blit.dstSubresource=ds;
    out.blit.srcSubresource.layerCount=out.blit.dstSubresource.layerCount=std::min(ss.layerCount,ds.layerCount);
    out.blit.srcOffsets[0]={old.src[0],old.src[1],0};out.blit.srcOffsets[1]={old.src[0]+sw,old.src[1]+sh,1};
    out.blit.dstOffsets[0]={old.dst[0],old.dst[1],0};out.blit.dstOffsets[1]={old.dst[0]+dw,old.dst[1]+dh,1};
    if(old.flags&4)std::swap(out.blit.dstOffsets[0].x,out.blit.dstOffsets[1].x);
    if(old.flags&8)std::swap(out.blit.dstOffsets[0].y,out.blit.dstOffsets[1].y);
    out.filter=(old.flags&1)&&src.aspect==VK_IMAGE_ASPECT_COLOR_BIT?VK_FILTER_LINEAR:VK_FILTER_NEAREST;return out;
}
void execute(VkCommandBuffer cb,const Cmd& command,const Intent& intent,const Legacy* comparison,const Legacy* diagnostics) {
    native_bindings::begin_packet();
    auto* src=resolve(cb,command.a[0]);auto* dst=resolve(cb,command.a[1]);
    if(!src||!dst)return;
    const bool same=src==dst;
    static const bool no_same=std::getenv("NX_COPY_NO_SAME_IMAGE")!=nullptr;
    if(same&&no_same)return;
    const auto packet=prepare(intent,*src,*dst,same);
    if(comparison) {
        const auto expected=legacy_prepare(*comparison,*src,*dst,same);
        if(packet.direct!=expected.direct||packet.blittable!=expected.blittable||packet.overlap!=expected.overlap||packet.filter!=expected.filter||
           std::memcmp(&packet.copy,&expected.copy,sizeof packet.copy)||std::memcmp(&packet.blit,&expected.blit,sizeof packet.blit))
            fatal("Native image transfer decisions differ from compatibility lowering");
        ++census.compared;
    }
    if(same) {
        ++g_same_image;g_same_overlap+=packet.overlap;
        if(g_same_image==1||g_same_image%1000==0||(packet.overlap&&g_same_overlap<=5))
            log("nvn: %llu copies within one image (%llu overlapping, dropped); this one %llx level %u -> %u, layer %u -> %u%s",
                (unsigned long long)g_same_image,(unsigned long long)g_same_overlap,(unsigned long long)command.a[0],
                packet.copy.srcSubresource.mipLevel,packet.copy.dstSubresource.mipLevel,
                packet.copy.srcSubresource.baseArrayLayer,packet.copy.dstSubresource.baseArrayLayer,packet.overlap?" (overlapping)":"");
        if(packet.overlap) {++census.overlap;return;}
    }
    static const bool trace_cubes=std::getenv("NX_TRACE_CUBES")!=nullptr;static int cube_logs=0;
    if(trace_cubes&&diagnostics&&(dst->view_type==VK_IMAGE_VIEW_TYPE_CUBE||dst->view_type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)&&cube_logs++<400)
        log("cubes: copy %llx -> %llx level %u layer %u+%u (z %d depth %d view layer %u) %dx%d",
            (unsigned long long)command.a[0],(unsigned long long)command.a[1],packet.copy.dstSubresource.mipLevel,
            packet.copy.dstSubresource.baseArrayLayer,packet.copy.dstSubresource.layerCount,diagnostics->dst[2],diagnostics->dst[5],
            diagnostics->dst_view.min_layer,diagnostics->dst[3],diagnostics->dst[4]);
    const auto source_layout=same?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    const auto destination_layout=same?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    transition(cb,*src,source_layout);transition(cb,*dst,destination_layout);
    if(same)general_barrier(cb,*src);
    if(packet.direct) {
        p_vkCmdCopyImage(cb,src->image,source_layout,dst->image,destination_layout,1,&packet.copy);++census.copies;
    } else if(packet.blittable) {
        p_vkCmdBlitImage(cb,src->image,source_layout,dst->image,destination_layout,1,&packet.blit,packet.filter);++census.blits;
    } else {
        ++census.rejected;static int logged=0;
        if(logged++<5)log("nvn: copy %llx -> %llx not replayed (formats %d -> %d, %dx%d -> %dx%d)",
            (unsigned long long)command.a[0],(unsigned long long)command.a[1],int(src->format),int(dst->format),
            int(intent.copy.extent.width),int(intent.copy.extent.height),int(intent.destination_extent.width),int(intent.destination_extent.height));
        return;
    }
    if(same)general_barrier(cb,*dst);
    dst->msaa_valid=false;dst->rendered=true;
}
} // namespace native_transfers

void replay_copy(VkCommandBuffer cb,const Cmd& command,const CmdList& list) {
    if(command.op!=Cmd::CopyTextureToTexture) {replay_copy_compat(cb,command,list);return;}
    static const bool off=std::getenv("NX_NO_COPY")!=nullptr;if(off)return;
    native_transfers::Intent intent;
    if(native_transfers::read(command,list,intent)) {
        native_transfers::Legacy old;const native_transfers::Legacy* comparison=nullptr;const native_transfers::Legacy* diagnostics=nullptr;
        static const bool trace_cubes=std::getenv("NX_TRACE_CUBES")!=nullptr;
        if(native_transfers::mode()==2||trace_cubes) {
            std::memcpy(&old,list.blobs[command.blob].data(),sizeof old);
            diagnostics=&old;
        }
        if(native_transfers::mode()==2) {
            native_transfers::Intent expected;
            if(!native_transfers::encode(old,expected)||std::memcmp(&intent,&expected,sizeof intent))
                fatal("Recorded native image transfer does not match its legacy prefix");
            comparison=&old;
        }
        ++native_transfers::census.packets;native_transfers::execute(cb,command,intent,comparison,diagnostics);
        const auto& n=native_transfers::census;
        if(n.packets==1||n.packets%10000==0)
            log("Native image transfers: %llu packets, %llu comparisons, %llu copies, %llu blits, %llu overlap drops, %llu format rejects, %llu bound resolutions, %llu payload fallbacks",
                (unsigned long long)n.packets,(unsigned long long)n.compared,(unsigned long long)n.copies,(unsigned long long)n.blits,
                (unsigned long long)n.overlap,(unsigned long long)n.rejected,(unsigned long long)n.bound,(unsigned long long)n.fallback);
        return;
    }
    if(native_transfers::envelope(command,list)) {
        // The legacy reader requires exactly sizeof(CopyInfo). A recognized
        // native envelope is projected into an owned prefix-only list; arbitrary
        // oversized blobs still follow its strict rejection behavior.
        CmdList prefix;prefix.blobs.emplace_back(list.blobs[command.blob].begin(),list.blobs[command.blob].begin()+sizeof(native_transfers::Legacy));
        auto legacy=command;legacy.blob=0;legacy.a[7]=legacy.a[8]=0;++native_transfers::census.fallback;
        replay_copy_compat(cb,legacy,prefix);return;
    }
    replay_copy_compat(cb,command,list);
}
