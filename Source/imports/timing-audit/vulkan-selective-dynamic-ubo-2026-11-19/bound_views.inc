// Inside nx::nvn. Lower relative pool view values at the binding frontier;
// the GPU consumer below accepts only native VkImageViewCreateInfo values.
bool prepare_bound_view(native_bindings::Binding& binding,const native_images::Contract& contract,GpuImage* img,bool storage) {
    auto* texture=native_bindings::checked(binding);
    if(!texture||!img)return false;
    auto allocation=native_resources::find_owned(binding.texture.key);
    if(allocation.get()!=img)return false;
    ViewInfo view=binding.view;
    if(storage) {view.levels=1;view.swizzle=0;}
    else {
        if(!view.swizzle&&!img->alpha_from_r)for(int k=0;k<4;++k)view.swizzle|=uint32_t(std::min<uint8_t>(texture->swizzle[k],5))<<(8*k);
        if(!ufloat_raw()&&!img->alpha_from_r&&(img->nvn_format==0x3f||view.format==0x3f)) {
            uint32_t s=view.swizzle?view.swizzle:0x05040302u;
            for(int k=0;k<4;++k)if((s>>(8*k)&255)==5)s=(s&~(255u<<(8*k)))|(1u<<(8*k));
            view.swizzle=s;
        }
    }
    native_bindings::PreparedView p;p.allocation=allocation;
    const uint32_t packed=img->alpha_from_r?0:view.swizzle;
    VkComponentMapping mapping=img->swizzle;
    if(packed) {
        constexpr VkComponentSwizzle values[]={VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE,VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A};
        mapping={values[packed&255],values[(packed>>8)&255],values[(packed>>16)&255],values[packed>>24]};
    }
    const uint32_t base=std::min(view.base_level,img->levels-1);
    const uint32_t levels=view.levels?std::min(view.levels,img->levels-base):img->levels-base;
    const uint32_t first=std::min(view.min_layer,img->layers-1);
    const bool whole=base==0&&levels==img->levels&&first==0&&(!view.layers||view.layers==img->layers);
    const auto format=view_format(*img,view.format);
    p.own=contract.view_type==img->view_type&&identity(mapping)&&whole&&format==img->format;
    uint32_t layers=std::min(view.layers?view.layers:img->layers-first,img->layers-first);
    if(contract.view_type==VK_IMAGE_VIEW_TYPE_2D||contract.view_type==VK_IMAGE_VIEW_TYPE_1D)layers=1;
    if(contract.view_type==VK_IMAGE_VIEW_TYPE_CUBE)layers=6;
    if(contract.view_type==VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)layers-=layers%6;
    p.compatible=p.own||view_compatible(*img,contract.view_type,layers);
    p.values.viewType=contract.view_type;p.values.format=format;p.values.components=mapping;
    p.values.subresourceRange={img->aspect==(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT)?VkImageAspectFlags(VK_IMAGE_ASPECT_DEPTH_BIT):img->aspect,base,levels,first,layers};
    p.cache_range=uint64_t(base)|(uint64_t(levels)<<16)|(uint64_t(first)<<32)|(uint64_t(view.layers)<<48);
    p.mapping=uint32_t(mapping.r)|(uint32_t(mapping.g)<<8)|(uint32_t(mapping.b)<<16)|(uint32_t(mapping.a)<<24);
    p.prepared=true;binding.prepared=p;++native_bindings::census.prepared;return true;
}
VkImageView view_of_bound(GpuImage& img,const native_bindings::PreparedView& p) {
    if(!p.prepared||p.allocation.get()!=&img)return VK_NULL_HANDLE;
    if(p.own)return img.view;
    const auto type=p.values.viewType;const auto format=p.values.format;const auto range=p.cache_range;const auto mapping=p.mapping;
    SampledKey key{img.image,type,mapping,range,uint64_t(format)};
    const uint64_t hash=(uintptr_t(img.image)>>4)^(range*31)^mapping^(uint64_t(type)<<5)^uint64_t(format);
    auto& hit=g_view_hits[(hash^(hash>>8))&255];
    if(hit.view&&hit.key==key)return hit.view;
    auto& view=g_sampled_views[key];
    if(!view) {
        if(!p.compatible)return VK_NULL_HANDLE;
        auto values=p.values;values.image=img.image;
        if(p_vkCreateImageView(vk_device(),&values,nullptr,&view)!=VK_SUCCESS)view=img.view;
    }
    if(view)hit={key,view};return view;
}
VkImageView consume_bound_view(VkCommandBuffer,const native_bindings::Binding& binding,const native_images::Contract& contract,GpuImage& img,bool storage) {
    auto prepared=binding.prepared;
    if(!prepared.prepared||prepared.allocation.get()!=&img) {
        ++native_bindings::census.view_fallback;
        auto current=binding;
        if(!prepare_bound_view(current,contract,&img,storage))return VK_NULL_HANDLE;
        prepared=current.prepared;
    }
    if(native_bindings::mode()==2) {
        // Recompute only pure view decisions. No upload, transition, dirty
        // exchange or Vulkan view creation is performed by this comparison.
        auto expected=binding;
        if(!prepare_bound_view(expected,contract,&img,storage))fatal("Native bound view lost its allocation identity");
        const auto& a=prepared;const auto& b=expected.prepared;
        if(a.own!=b.own||a.compatible!=b.compatible||a.cache_range!=b.cache_range||a.mapping!=b.mapping||
           a.values.viewType!=b.values.viewType||a.values.format!=b.values.format||
           std::memcmp(&a.values.components,&b.values.components,sizeof a.values.components)||
           std::memcmp(&a.values.subresourceRange,&b.values.subresourceRange,sizeof a.values.subresourceRange))
            fatal("Native bound Vulkan view values differ at consumption");
        ++native_bindings::census.compared;
    }
    return view_of_bound(img,prepared);
}
