namespace selective_dynamic_ubo {
#include "tower_shader_projection.inc"
int mode(){static const int v=[] {const auto* p=std::getenv("CATHERINE_VULKAN_SELECTIVE_DYNAMIC_UBO");return p&&!std::strcmp(p,"1")?1:p&&!std::strcmp(p,"2")?2:0;}();return v;}
bool tower(const std::vector<const ShaderInfo*>& stages){for(const auto* s:stages)if(s->stage==ShaderStage::Vertex&&block_shader(s))return true;return false;}
struct Census{uint64_t layouts=0,tower=0,dynamic=0,static_other=0,compared=0;}census;
void verify(const std::vector<const ShaderInfo*>& stages,const std::map<uint32_t,VkDescriptorSetLayoutBinding>(&sets)[4],uint32_t maximum,uint64_t actual,bool excluded){
    if(mode()!=2)return;
    std::map<uint32_t,VkDescriptorSetLayoutBinding> expected[4];
    for(const auto* s:stages)for(const auto& b:s->refl.bindings)if(b.set<4){auto& v=expected[b.set][b.binding];v.binding=b.binding;v.descriptorType=VkDescriptorType(b.type);v.descriptorCount=std::max(1u,b.count);v.stageFlags|=stage_bit(s->stage);}
    uint64_t candidate=0;for(const auto&[n,b]:expected[0])if(n<64&&b.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER&&b.descriptorCount==1)candidate|=1ull<<n;
    const uint64_t wanted=!excluded&&uint32_t(__builtin_popcountll(candidate))<=maximum?candidate:0;
    if(wanted!=actual||excluded!=tower(stages))fatal("Selective dynamic UBO mask/classifier differs from immutable layout projection");
    for(unsigned k=0;k<4;++k){if(sets[k].size()!=expected[k].size())fatal("Selective dynamic UBO changed binding count");for(const auto&[n,b]:sets[k]){auto it=expected[k].find(n);if(it==expected[k].end())fatal("Selective dynamic UBO added a binding");auto e=it->second;if(k==0&&n<64&&(wanted>>n&1))e.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;if(b.binding!=e.binding||b.descriptorType!=e.descriptorType||b.descriptorCount!=e.descriptorCount||b.stageFlags!=e.stageFlags||b.pImmutableSamplers!=e.pImmutableSamplers)fatal("Selective dynamic UBO changed immutable binding meaning");}}
    ++census.compared;
}
void report(bool excluded,uint64_t dynamic){if(!mode())return;++census.layouts;if(excluded)++census.tower;else if(dynamic)++census.dynamic;else ++census.static_other;if(census.layouts==1||census.layouts%64==0)log("Selective dynamic UBO: layouts %llu tower-static %llu dynamic %llu other-static %llu compared %llu",(unsigned long long)census.layouts,(unsigned long long)census.tower,(unsigned long long)census.dynamic,(unsigned long long)census.static_other,(unsigned long long)census.compared);}
}
