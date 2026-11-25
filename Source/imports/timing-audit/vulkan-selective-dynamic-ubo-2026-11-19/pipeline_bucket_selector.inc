// Private to this pipeline TU. Shared inline memo/key definitions are unchanged.
namespace pipeline_state_buckets {
int mode(){static const int value=[] {const char* p=std::getenv("CATHERINE_VULKAN_PIPELINE_STATE_BUCKETS");return p&&p[0]=='1'&&!p[1]?1:0;}();return value;}
uint32_t select(const ShaderInfo*vs,const ShaderInfo*fs,const Cmd&draw,const ProgramLayout&layout,const VkFormat*colors,VkFormat depth,uint32_t count,uint32_t ufloat,VkSampleCountFlagBits samples){
    const auto original=native_pipeline::memo_bucket(vs,fs,draw,layout,depth,count,samples);
    if(!mode())return original;
    uint64_t hash=original;
    auto add=[&](uint32_t low,uint32_t high){
        const uint64_t word=uint64_t(low)|(uint64_t(high)<<32);
#if defined(__x86_64__) || defined(_M_X64)
        hash=_mm_crc32_u64(hash,word);
#else
        hash=(hash^word)*0x9e3779b97f4a7c15ull;hash^=hash>>32;
#endif
    };
    const auto& fixed=producer_fixed::state.values;
    for(unsigned i=0;i<8;i+=2)add(fixed.blend[i].colorWriteMask,fixed.blend[i+1].colorWriteMask);
    const auto& blend=fixed.blend[0];
    add(blend.blendEnable,blend.srcColorBlendFactor);add(blend.dstColorBlendFactor,blend.colorBlendOp);
    add(blend.srcAlphaBlendFactor,blend.dstAlphaBlendFactor);add(blend.alphaBlendOp,fixed.alpha_to_coverage);
    add(fixed.depth.test,fixed.depth.write);add(fixed.depth.compare,fixed.depth.stencil);
    add(fixed.polygon.mode,fixed.polygon.cull);add(fixed.polygon.front,fixed.polygon.offset);
    add(fixed.depth_clamp,ufloat);
    const auto& vertex=producer_pipeline::state;
    add(vertex.attributes.count,vertex.bindings.count);
    for(unsigned i=0;i<std::min(vertex.attributes.count,2u);++i){const auto&a=vertex.attributes.values[i];add(a.format,a.binding);add(a.offset,vertex.attributes.int_kinds[i]);}
    for(unsigned i=0;i<std::min(vertex.bindings.count,2u);++i){const auto&b=vertex.bindings.values[i];add(b.stride,b.inputRate);}
    add(uint32_t(colors[0]),uint32_t(colors[1]));
    hash^=hash>>16;
    return uint32_t(hash)&(native_pipeline::memo_bucket_count-1);
}
}
