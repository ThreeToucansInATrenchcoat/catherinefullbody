// Included inside nx::nvn::replay, after the legacy pure/data helpers.
// Immutable plans are compiled lazily on the single GPU execution thread.
// No Program or bound data is captured, and no Vulkan call occurs in compilation.
namespace materials {
Census census;
struct Execution {
    float vw=1280,vh=720;
    bool shared=true,support_built=false;
    SupportBuffer support; // fully assigned only if a support resolver needs it
};
bool static_extent_enabled() {
    static const bool enabled=[] {const auto* s=std::getenv("NX_UNIFORM_STATIC_EXTENT");return s&&s[0]!='0';}();
    return enabled&&!draw_slow(kSlowUploads);
}
Range uniform_range(const Recipe& r) {
    return r.slot>=0&&r.slot<18?g_state.uniforms[r.stage][r.slot]:Range{};
}
bool memo_always_match(const Recipe&,const memo87::Entry&,float,float) {return true;}
bool memo_always_capture(const Recipe&,memo87::Entry&,float,float) {return true;}
bool memo_uniform_source(const Recipe& r,const uint8_t*& p,uint64_t& size) {
    if(r.model_transform)return false;
    const auto range=uniform_range(r);
    p=range.address?guest_bytes(range.address):nullptr;
    size=p?std::min<uint64_t>(range.size,0x10000):0;
    return size<=sizeof(memo87::Entry::bytes)&&(!size||!written_overlaps(p,size));
}
bool memo_uniform_match(const Recipe& r,const memo87::Entry& e,float,float) {
    const uint8_t* p;uint64_t size;
    return memo_uniform_source(r,p,size)&&size==e.size&&(!size||std::memcmp(p,e.bytes,size)==0);
}
bool memo_uniform_capture(const Recipe& r,memo87::Entry& e,float,float) {
    const uint8_t* p;uint64_t size;
    if(!memo_uniform_source(r,p,size))return false;
    e.size=size;if(size)std::memcpy(e.bytes,p,size);return true;
}
bool memo_viewport_match(const Recipe&,const memo87::Entry& e,float vw,float vh) {
    return std::memcmp(&vw,&e.vw,4)==0&&std::memcmp(&vh,&e.vh,4)==0;
}
bool memo_viewport_capture(const Recipe&,memo87::Entry& e,float vw,float vh) {e.vw=vw;e.vh=vh;return true;}
bool memo_driver_match(const Recipe& r,const memo87::Entry& e,float,float) {
    return std::memcmp(g_state.storage[r.stage],e.storage,sizeof e.storage)==0;
}
bool memo_driver_capture(const Recipe& r,memo87::Entry& e,float,float) {
    std::memcpy(e.storage,g_state.storage[r.stage],sizeof e.storage);return true;
}
bool memo_lookup(const Recipe& r,float vw,float vh,VkDescriptorBufferInfo& info) {
    bool hit=false;
    if(memo87::on()&&r.memo_match) {
        auto& e=memo87::slot(r.legacy);
        hit=e.b==r.legacy&&e.epoch==memo87::g_epoch.load(std::memory_order_relaxed)&&r.memo_match(r,e,vw,vh);
        if(hit)info=e.info;
    }
    if(mode()==2) {
        VkDescriptorBufferInfo old{};
        const auto hits=memo87::g_hits;
        const bool legacy_hit=memo87::lookup(r.shader,*r.legacy,r.stage,vw,vh,old);
        memo87::g_hits=hits;
        ++census.memo_compared;
        if(hit!=legacy_hit||(hit&&(info.buffer!=old.buffer||info.offset!=old.offset||info.range!=old.range)))
            fatal("Native material memo hit differs from legacy bytes/decision");
    }
    if(hit) {++memo87::g_hits;++census.memo_hits;}
    return hit;
}
void memo_store(const Recipe& r,float vw,float vh,const VkDescriptorBufferInfo& info) {
    if(!memo87::on()||!r.memo_capture)return;
    ++memo87::g_misses;
    auto& e=memo87::slot(r.legacy);e.b=nullptr;
    if(!r.memo_capture(r,e,vw,vh))return;
    e.info=info;e.epoch=memo87::g_epoch.load(std::memory_order_relaxed);e.b=r.legacy;
}
bool storage_source(const Recipe& r,const uint8_t*& p,uint64_t& size) {
    const Range range=r.slot>=0&&r.slot<16?g_state.storage[r.stage][r.slot]:Range{};
    p=range.address?guest_bytes(range.address):nullptr;
    size=p?std::min<uint64_t>(range.size,16ull<<20):0;
    const bool writes=r.compute&&(all_write_backs()||r.shader_writes);
    if(mode()==2) {
        const auto& b=*r.legacy;const int st=api_stage(r.shader->stage);
        const Range old=b.nvn.index>=0&&b.nvn.index<16?g_state.storage[st][b.nvn.index]:Range{};
        const auto* bytes=old.address?guest_bytes(old.address):nullptr;
        const auto n=bytes?std::min<uint64_t>(old.size,16ull<<20):0;
        const bool old_writes=r.shader->stage==ShaderStage::Compute&&
            (all_write_backs()||(b.binding<64&&(written_storage_bindings(r.shader)>>b.binding&1)));
        // Equal pointer and extent prove the exact same current bytes; do not copy
        // or scan a 16 MiB storage range merely to compare it with itself.
        if(p!=bytes||size!=n||writes!=old_writes)fatal("Native storage source/write policy differs from legacy");
    }
    return writes;
}
VkDescriptorBufferInfo resolve_support(VkCommandBuffer cb,const Recipe&,Execution& e) {
    const SupportBuffer* block=e.shared?support_block(e.vw,e.vh):nullptr;
    if(block) {auto u=dedup_upload(reinterpret_cast<const uint8_t*>(block),sizeof(SupportBuffer),sizeof(SupportBuffer),256);return {u.buffer,u.offset,sizeof(SupportBuffer)};}
    if(!e.support_built) {e.support=support_buffer_defaults();support_buffer_set_viewport(e.support,e.vw,e.vh);e.support_built=true;}
    return buffer_of(cb,&e.support,sizeof(SupportBuffer),16,false,false);
}
const StagePlan& stage_plan(const ShaderInfo*);
VkDescriptorBufferInfo resolve_constants(VkCommandBuffer cb,const Recipe& r,Execution&) {
    const auto& bytes=stage_plan(r.shader).constants;
    return buffer_of(cb,bytes.data(),bytes.size());
}
VkDescriptorBufferInfo resolve_uniform(VkCommandBuffer cb,const Recipe& r,Execution&) {return uniform_info(cb,r);}
VkDescriptorBufferInfo resolve_storage(VkCommandBuffer cb,const Recipe& r,Execution&) {
    const uint8_t* p;uint64_t size;const bool written=storage_source(r,p,size);const uint8_t* native=p;
    if(!written&&!r.compute)written_graphics_storage(p,size);
    if(!written&&r.palette) {
        p=vincent_pose_palette(p,size,g_state.storage[5][1].address,g_state.storage[5][0].address);
        p=spike_gpu_palette(p,size,*r.shader);
        p=campaign_block_gpu_palette(p,size,*r.shader);
        p=block20_gpu_palette(p,size,*r.shader);
    }
    return buffer_of(cb,p,size,0x10000,written,p==native);
}
VkDescriptorBufferInfo resolve_driver(VkCommandBuffer cb,const Recipe& r,Execution& e) {
    if(const uint8_t* block=e.shared?constants_block(r.shader->stage,r.stage):nullptr) {
        const auto u=dedup_upload(block,0x1000,0x1000,256);return {u.buffer,u.offset,0x1000};
    }
    auto c0=driver_constants(r.shader->stage,r.stage);return buffer_of(cb,c0.data(),c0.size(),16,false,false);
}
VkDescriptorBufferInfo resolve_zero(VkCommandBuffer cb,const Recipe&,Execution&) {return buffer_of(cb,nullptr,0x1000,16,false,false);}

Recipe compile_recipe(const ShaderInfo* s,const DescriptorBinding& b,const UniformExtents& extents) {
    Recipe r;r.shader=s;r.legacy=&b;r.stage=api_stage(s->stage);
    r.destination={VkDescriptorType(b.type),b.set,b.binding,b.count,false};
    r.resolve=resolve_zero;r.memo_match=memo_always_match;r.memo_capture=memo_always_capture;
    switch(b.nvn.source) {
        case NvnSource::SupportBuffer:r.resolve=resolve_support;r.memo_match=memo_viewport_match;r.memo_capture=memo_viewport_capture;break;
        case NvnSource::ConstantData:r.resolve=resolve_constants;break;
        case NvnSource::UniformBlock:
        case NvnSource::UniformAsStorage:
            r.resolve=resolve_uniform;r.uniform=true;r.slot=b.nvn.index;
            r.model_transform=b.nvn.bank==3&&s->stage==ShaderStage::Vertex;
            r.computed_index=b.set==0&&b.binding<64&&(dynamic_uniform_bindings(s)>>b.binding&1);
            r.uniform_descriptor=b.set==0&&VkDescriptorType(b.type)==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            r.exact_extent=r.uniform_descriptor&&b.binding<extents.size()?extents[b.binding]:0;
            r.memo_match=memo_uniform_match;r.memo_capture=memo_uniform_capture;break;
        case NvnSource::StorageBuffer:
            r.resolve=resolve_storage;r.storage=true;r.slot=b.nvn.index;r.compute=s->stage==ShaderStage::Compute;
            r.shader_writes=b.binding<64&&(written_storage_bindings(s)>>b.binding&1);
            r.palette=r.compute&&r.slot==0&&vincent_skin_shader(*s);
            r.memo_match=nullptr;r.memo_capture=nullptr;break;
        case NvnSource::DriverConstants:r.resolve=resolve_driver;r.memo_match=memo_driver_match;r.memo_capture=memo_driver_capture;break;
        default:break;
    }
    return r;
}
const StagePlan& stage_plan(const ShaderInfo* shader) {
    static std::unordered_map<const ShaderInfo*,std::unique_ptr<StagePlan>> cache;
    if(auto it=cache.find(shader);it!=cache.end())return *it->second;
    auto made=std::make_unique<StagePlan>();auto& p=*made;p.shader=shader;p.constants=shader->constants;
    const auto extents=uniform_extents(*shader);p.extents=extents;
    for(const auto& b:shader->refl.bindings) {
        if(b.set<2) {p.buffers.push_back(compile_recipe(shader,b,extents));p.has_storage|=p.buffers.back().storage;}
        p.has_images|=b.set==3;
        const int st=api_stage(shader->stage);
        if(b.nvn.source==NvnSource::SamplerBinding) {
            if(st>1||b.nvn.index<0||b.nvn.index>=32)p.full_slots=true;
            else p.texture_mask[st]|=1u<<b.nvn.index;
        } else if(b.nvn.source==NvnSource::ImageBinding) {
            if(st>1||b.nvn.index<0||b.nvn.index>=16)p.full_slots=true;
            else p.image_mask[st]|=1u<<b.nvn.index;
        }
    }
    const auto* result=made.get();cache.emplace(shader,std::move(made));++census.compiled;return *result;
}
void verify_recipe(const Recipe& r) {
    const auto& b=*r.legacy;
    // Independent legacy decisions, not a second call through the compiler.
    Recipe expected;expected.stage=api_stage(r.shader->stage);
    expected.resolve=resolve_zero;expected.memo_match=memo_always_match;expected.memo_capture=memo_always_capture;
    switch(b.nvn.source) {
        case NvnSource::SupportBuffer:expected.resolve=resolve_support;expected.memo_match=memo_viewport_match;expected.memo_capture=memo_viewport_capture;break;
        case NvnSource::ConstantData:expected.resolve=resolve_constants;break;
        case NvnSource::UniformBlock:
        case NvnSource::UniformAsStorage:
            expected.resolve=resolve_uniform;expected.uniform=true;expected.slot=b.nvn.index;
            expected.model_transform=b.nvn.bank==3&&r.shader->stage==ShaderStage::Vertex;
            expected.computed_index=b.set==0&&b.binding<64&&(dynamic_uniform_bindings(r.shader)>>b.binding&1);
            expected.uniform_descriptor=b.set==0&&VkDescriptorType(b.type)==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            expected.exact_extent=expected.uniform_descriptor&&b.binding<64?stage_plan(r.shader).extents[b.binding]:0;
            expected.memo_match=memo_uniform_match;expected.memo_capture=memo_uniform_capture;break;
        case NvnSource::StorageBuffer:
            expected.resolve=resolve_storage;expected.storage=true;expected.slot=b.nvn.index;expected.compute=r.shader->stage==ShaderStage::Compute;
            expected.shader_writes=b.binding<64&&(written_storage_bindings(r.shader)>>b.binding&1);
            expected.palette=r.shader->stage==ShaderStage::Compute&&b.nvn.index==0&&vincent_skin_shader(*r.shader);
            expected.memo_match=nullptr;expected.memo_capture=nullptr;break;
        case NvnSource::DriverConstants:expected.resolve=resolve_driver;expected.memo_match=memo_driver_match;expected.memo_capture=memo_driver_capture;break;
        default:break;
    }
    if(r.resolve!=expected.resolve||r.memo_match!=expected.memo_match||r.memo_capture!=expected.memo_capture||
       r.stage!=expected.stage||r.slot!=expected.slot||r.uniform!=expected.uniform||r.model_transform!=expected.model_transform||
       r.computed_index!=expected.computed_index||r.uniform_descriptor!=expected.uniform_descriptor||r.exact_extent!=expected.exact_extent||
       r.storage!=expected.storage||r.compute!=expected.compute||r.shader_writes!=expected.shader_writes||r.palette!=expected.palette||
       r.destination.type!=VkDescriptorType(b.type)||r.destination.set!=b.set||r.destination.binding!=b.binding||r.destination.count!=b.count)
        fatal("Native material recipe differs from shader reflection");
    if(b.nvn.source==NvnSource::ConstantData) {
        const auto& own=stage_plan(r.shader).constants;const auto& old=r.shader->constants;
        if(own.size()!=old.size()||(!own.empty()&&std::memcmp(own.data(),old.data(),own.size())))
            fatal("Native material constant bytes differ from immutable shader constants");
    }
    ++census.compared;
}
Recipe legacy_recipe(const ShaderInfo* shader,const DescriptorBinding& binding) {
    // Mode-0 tower metadata is independently lowered from current reflection.
    // The resulting value is copied into the template, never borrowed from scratch.
    return compile_recipe(shader,binding,stage_plan(shader).extents);
}
TemplateMetadata template_metadata(const std::vector<const ShaderInfo*>& shaders,const ProgramLayout& layout) {
    TemplateMetadata out;
    if(!mode()) {
        for(const auto* s:shaders)for(const auto& b:s->refl.bindings) {
            out.has_storage|=b.set<2&&b.nvn.source==NvnSource::StorageBuffer;out.has_images|=b.set==3;
            const int st=api_stage(s->stage);
            if(b.nvn.source==NvnSource::SamplerBinding) {if(st>1||b.nvn.index<0||b.nvn.index>=32)out.full_slots=true;else out.texture_mask[st]|=1u<<b.nvn.index;}
            if(b.nvn.source==NvnSource::ImageBinding) {if(st>1||b.nvn.index<0||b.nvn.index>=16)out.full_slots=true;else out.image_mask[st]|=1u<<b.nvn.index;}
        }
    } else {
        const auto& p=plan(shaders,layout);verify_template(p,shaders);
        std::memcpy(out.texture_mask,p.texture_mask,sizeof out.texture_mask);std::memcpy(out.image_mask,p.image_mask,sizeof out.image_mask);
        out.full_slots=p.full_slots;out.has_storage=p.has_storage;out.has_images=p.has_images;
    }
    return out;
}
#include "material_plan_lookup.inc"

const LinkedPlan& plan(const std::vector<const ShaderInfo*>& shaders,const ProgramLayout& layout) {
    using Key=linked_lookup::Key;
    static std::map<Key,std::unique_ptr<LinkedPlan>,linked_lookup::Less> cache;
    static const std::pair<const Key,std::unique_ptr<LinkedPlan>>* last=nullptr;
    const int lookup_mode=linked_lookup::mode();if(lookup_mode)++linked_lookup::census.calls;
    if(last&&last->first.second==&layout&&last->first.first==shaders) {
        if(lookup_mode){++linked_lookup::census.last_hits;linked_lookup::report();}
        return *last->second;
    }
    std::optional<Key> key;
    auto it=cache.end();
    if(!lookup_mode) {key.emplace(shaders,&layout);it=cache.find(*key);}
    else {
        ++linked_lookup::census.lookups;
        it=cache.find(linked_lookup::View{shaders,&layout});
        if(lookup_mode==2) {
            key.emplace(shaders,&layout);++linked_lookup::census.oracle_copies;
            const auto original=cache.find(*key);++linked_lookup::census.compared;
            if(original!=it)fatal("Borrowed material-plan lookup differs from owning key lookup");
        }
    }
    if(it!=cache.end()) {
        last=&*it;
        if(lookup_mode){++linked_lookup::census.hits;linked_lookup::report();}
        return *it->second;
    }
    if(!key) {key.emplace(shaders,&layout);if(lookup_mode)++linked_lookup::census.insert_copies;}
    auto p=std::make_unique<LinkedPlan>();
    for(const auto* shader:shaders) {
        const auto& stage=stage_plan(shader);p->stages.push_back(&stage);
        p->has_storage|=stage.has_storage;p->has_images|=stage.has_images;p->full_slots|=stage.full_slots;
        for(int st=0;st<2;++st) {p->texture_mask[st]|=stage.texture_mask[st];p->image_mask[st]|=stage.image_mask[st];}
        for(const auto& r:stage.buffers) {
            if(r.destination.count!=1)p->supported=false;
            auto dst=r.destination;
            if(dst.set==0&&dst.binding<64&&(layout.dynamic>>dst.binding&1)) {dst.type=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;dst.dynamic=true;}
            p->buffers.push_back({&r,dst});if(r.storage)p->storage.push_back(&r);
            if(dst.set==0)p->set0_copies.push_back(r.destination);
        }
    }
    auto added=cache.emplace(std::move(*key),std::move(p)).first;last=&*added;++census.linked;
    if(lookup_mode){++linked_lookup::census.misses;linked_lookup::report();}
    return *last->second;
}
void verify_template(const LinkedPlan& p,const std::vector<const ShaderInfo*>& shaders) {
    if(mode()!=2)return;
    uint32_t tex[2]{},img[2]{};bool full=false,storage=false,images=false;size_t buffer=0;
    for(const auto* s:shaders)for(const auto& b:s->refl.bindings) {
        if(b.set<2) {if(buffer>=p.buffers.size()||p.buffers[buffer++].recipe->legacy!=&b)fatal("Native material order differs from reflection");storage|=b.nvn.source==NvnSource::StorageBuffer;}
        images|=b.set==3;const int st=api_stage(s->stage);
        if(b.nvn.source==NvnSource::SamplerBinding) {if(st>1||b.nvn.index<0||b.nvn.index>=32)full=true;else tex[st]|=1u<<b.nvn.index;}
        if(b.nvn.source==NvnSource::ImageBinding) {if(st>1||b.nvn.index<0||b.nvn.index>=16)full=true;else img[st]|=1u<<b.nvn.index;}
    }
    if(buffer!=p.buffers.size()||full!=p.full_slots||storage!=p.has_storage||images!=p.has_images||
       std::memcmp(tex,p.texture_mask,sizeof tex)||std::memcmp(img,p.image_mask,sizeof img))fatal("Native tower material metadata differs from reflection");
    for(const auto& op:p.buffers)verify_recipe(*op.recipe);
    ++census.template_compared;
}
} // namespace materials
