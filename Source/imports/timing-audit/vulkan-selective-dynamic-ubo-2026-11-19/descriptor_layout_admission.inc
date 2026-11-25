namespace descriptor_layouts {
// An application admission bound, not a Vulkan binding-number limit.
constexpr uint32_t kBindingLimit=256;
struct Census {uint64_t candidates=0,admitted=0,compared=0,support_queries=0,fallback[8]{};} census;
int mode(){static const int v=[] {const auto*p=std::getenv("CATHERINE_VULKAN_DESCRIPTOR_LAYOUTS");return p&&!std::strcmp(p,"1")?1:p&&!std::strcmp(p,"2")?2:0;}();return v;}
void report(){if(census.candidates==1||census.candidates%16==0)log("Extended descriptor layouts: candidates %llu admitted %llu compared %llu support-queries %llu fallbacks scalar %llu limits %llu no-query %llu unsupported %llu create %llu offsets %llu pipeline %llu precheck %llu",(unsigned long long)census.candidates,(unsigned long long)census.admitted,(unsigned long long)census.compared,(unsigned long long)census.support_queries,(unsigned long long)census.fallback[0],(unsigned long long)census.fallback[1],(unsigned long long)census.fallback[2],(unsigned long long)census.fallback[3],(unsigned long long)census.fallback[4],(unsigned long long)census.fallback[5],(unsigned long long)census.fallback[6],(unsigned long long)census.fallback[7]);}
struct Count {uint64_t sampler=0,uniform=0,storage=0,sampled=0,image=0;};
void add(Count&c,VkDescriptorType t,uint32_t n){switch(t){case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:c.uniform+=n;break;case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:c.storage+=n;break;case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:c.sampler+=n;c.sampled+=n;break;case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:c.image+=n;break;default:break;}}
bool limits(const std::vector<VkDescriptorSetLayoutBinding>(&sets)[4]){
    VkPhysicalDeviceProperties props{};p_vkGetPhysicalDeviceProperties(vk_physical(),&props);const auto&l=props.limits;
    if(l.maxBoundDescriptorSets<4)return false;
    Count total,stages[3];
    for(const auto&set:sets)for(const auto&b:set){
        add(total,b.descriptorType,b.descriptorCount);
        for(unsigned s=0;s<3;++s)if(b.stageFlags&(s==0?VK_SHADER_STAGE_VERTEX_BIT:s==1?VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_COMPUTE_BIT))add(stages[s],b.descriptorType,b.descriptorCount);
    }
    if(total.sampler>l.maxDescriptorSetSamplers||total.uniform>l.maxDescriptorSetUniformBuffers||total.storage>l.maxDescriptorSetStorageBuffers||total.sampled>l.maxDescriptorSetSampledImages||total.image>l.maxDescriptorSetStorageImages)return false;
    for(unsigned stage=0;stage<3;++stage){const auto&c=stages[stage];if(c.sampler>l.maxPerStageDescriptorSamplers||c.uniform>l.maxPerStageDescriptorUniformBuffers||c.storage>l.maxPerStageDescriptorStorageBuffers||c.sampled>l.maxPerStageDescriptorSampledImages||c.image>l.maxPerStageDescriptorStorageImages||c.uniform+c.storage+c.sampled+c.image+c.sampler+(stage==1?l.maxColorAttachments:0)>l.maxPerStageResources)return false;}
    // Combined samplers conservatively count twice in this aggregate bound.
    // Maximum fragment color attachments cover any later pipeline target set.
    // These conservative bounds can only reject an otherwise legal extension.
    return true;
}
bool admit(const std::vector<const ShaderInfo*>&stages,const std::vector<VkDescriptorSetLayoutBinding>(&sets)[4]){
    for(const auto*s:stages)for(const auto&b:s->refl.bindings)if(b.set<4&&b.count!=1){++census.fallback[0];return false;}
    if(!limits(sets)){++census.fallback[1];return false;}
    const auto core=p_vkGetDescriptorSetLayoutSupport;const auto khr=p_vkGetDescriptorSetLayoutSupportKHR;
    if(!core&&!khr){++census.fallback[2];return false;}
    for(const auto&set:sets){
        VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};ci.flags=VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;ci.bindingCount=uint32_t(set.size());ci.pBindings=set.data();
        VkDescriptorSetLayoutSupport support{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT};
        if(core)core(vk_device(),&ci,&support);else khr(vk_device(),&ci,&support);++census.support_queries;
        if(!support.supported){++census.fallback[3];return false;}
    }
    return true;
}
bool offsets_valid(const ProgramLayout&layout,const std::vector<VkDescriptorSetLayoutBinding>(&sets)[4]){
    const auto*props=descriptor_buffer_properties();if(!props)return false;
    uint64_t total=0;const uint64_t alignment=props->descriptorBufferOffsetAlignment;if(!alignment||(alignment&(alignment-1)))return false;
    for(unsigned k=0;k<4;++k){const auto*bytes=layout.bytes[k];if(!bytes||bytes->size>(1ull<<20)||bytes->size>props->maxResourceDescriptorBufferRange||bytes->size>props->maxSamplerDescriptorBufferRange)return false;
        for(const auto&b:sets[k]){const auto n=descriptor_byte_size(b.descriptorType);if(b.binding>=bytes->offsets.size()||bytes->offsets[b.binding]>bytes->size||n>bytes->size-bytes->offsets[b.binding])return false;}
        total=(total+bytes->size+alignment-1)&~(alignment-1);
    }
    // Existing upload buffers are128/256MiB. Keep malformed driver layouts far
    // below either path's capacity without changing its allocation policy.
    const auto ring=frame_ring_size();return total<=(1ull<<20)&&(!ring||total<=ring);
}
void verify_projection(const std::vector<const ShaderInfo*>&stages,const std::vector<VkDescriptorSetLayoutBinding>(&sets)[4]){
    if(mode()!=2)return;
    std::map<std::pair<uint32_t,uint32_t>,VkDescriptorSetLayoutBinding>oracle;
    for(const auto*s:stages)for(const auto&b:s->refl.bindings)if(b.set<4){auto&v=oracle[{b.set,b.binding}];v.binding=b.binding;v.descriptorType=VkDescriptorType(b.type);v.descriptorCount=std::max(1u,b.count);v.stageFlags|=stage_bit(s->stage);}
    size_t seen=0;for(unsigned k=0;k<4;++k)for(const auto&b:sets[k]){const auto i=oracle.find({k,b.binding});if(i==oracle.end())fatal("Extended descriptor layout added a binding");const auto&o=i->second;if(b.descriptorType!=o.descriptorType||b.descriptorCount!=o.descriptorCount||b.stageFlags!=o.stageFlags||b.pImmutableSamplers!=o.pImmutableSamplers)fatal("Extended descriptor layout changed shader binding meaning");++seen;}
    if(seen!=oracle.size())fatal("Extended descriptor layout dropped a binding");++census.compared;
}
}
