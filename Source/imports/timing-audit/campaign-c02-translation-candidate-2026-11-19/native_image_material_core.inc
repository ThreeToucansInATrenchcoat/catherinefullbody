// Inside nx::nvn::replay. Recipes own Vulkan contracts, never a bound resource.
namespace image_materials {
struct Recipe;
using HandleReader=uint64_t(*)(const Recipe&);
struct Recipe {
    const ShaderInfo* shader=nullptr;
    const DescriptorBinding* legacy=nullptr; // debug projection / comparator only
    materials::Destination destination;
    native_images::Contract contract;
    HandleReader read=nullptr;
    int stage=0,slot=-1;
    uint64_t byte_offset=0;
};
struct Plan {
    std::vector<Recipe> sampled,storage;
    bool supported=true;
};
struct Census {
    uint64_t plans=0,gathers=0,sampled=0,storage=0,compared=0,fallback=0;
};
Census census;
std::vector<native_bindings::BindingRef> sample_bindings;
uint64_t zero_handle(const Recipe&) {return 0;}
uint64_t texture_handle(const Recipe& r) {return g_state.textures[r.stage][r.slot];}
uint64_t image_handle(const Recipe& r) {return g_state.images[r.stage][r.slot];}
uint64_t uniform_handle(const Recipe& r) {
    const auto& range=g_state.uniforms[r.stage][r.slot];const auto* p=range.address?guest_bytes(range.address):nullptr;
    uint32_t handle=0;if(p&&r.byte_offset<=range.size&&range.size-r.byte_offset>=4)std::memcpy(&handle,p+r.byte_offset,4);
    return handle;
}
const Plan& plan(const std::vector<const ShaderInfo*>& shaders) {
    static std::map<std::vector<const ShaderInfo*>,std::unique_ptr<Plan>> cache;
    static const std::pair<const std::vector<const ShaderInfo*>,std::unique_ptr<Plan>>* last=nullptr;
    if(last&&last->first==shaders)return *last->second;
    auto found=cache.find(shaders);if(found!=cache.end()) {last=&*found;return *last->second;}
    auto p=std::make_unique<Plan>();
    for(const auto* s:shaders)for(const auto& b:s->refl.bindings) {
        if(b.set!=2&&b.set!=3)continue;
        Recipe r;r.shader=s;r.legacy=&b;r.stage=api_stage(s->stage);r.destination={VkDescriptorType(b.type),b.set,b.binding,b.count,false};
        r.contract=native_images::compile(b.image);r.read=zero_handle;
        if(b.count!=1)p->supported=false;
        if(b.set==2) {
            if(b.nvn.source==NvnSource::SamplerBinding&&b.nvn.index>=0&&b.nvn.index<32) {r.read=texture_handle;r.slot=b.nvn.index;}
            else if(b.nvn.source==NvnSource::HandleInUniform&&b.nvn.bank>=3) {
                if(b.nvn.bank-3>=18||b.nvn.word<0)p->supported=false;
                else {r.read=uniform_handle;r.slot=b.nvn.bank-3;r.byte_offset=uint64_t(b.nvn.word)*4;}
            }
            p->sampled.push_back(std::move(r));
        } else {
            if(b.nvn.source==NvnSource::ImageBinding&&b.nvn.index>=0&&b.nvn.index<16) {r.read=image_handle;r.slot=b.nvn.index;}
            p->storage.push_back(std::move(r));
        }
    }
    auto inserted=cache.emplace(shaders,std::move(p)).first;last=&*inserted;++census.plans;return *last->second;
}
void verify(const Recipe& r,uint64_t handle) {
    if(native_images::mode()!=2)return;
    const auto& b=*r.legacy;uint64_t old=0;const int st=api_stage(r.shader->stage);
    if(b.set==2) {
        if(b.nvn.source==NvnSource::SamplerBinding&&b.nvn.index>=0&&b.nvn.index<32)old=g_state.textures[st][b.nvn.index];
        else if(b.nvn.source==NvnSource::HandleInUniform&&b.nvn.bank>=3) {
            const auto& range=g_state.uniforms[st][b.nvn.bank-3];const auto* p=range.address?guest_bytes(range.address):nullptr;
            if(p&&uint64_t(b.nvn.word)*4+4<=range.size)old=*reinterpret_cast<const uint32_t*>(p+b.nvn.word*4);
        }
    } else if(b.nvn.source==NvnSource::ImageBinding&&b.nvn.index>=0&&b.nvn.index<16)old=g_state.images[st][b.nvn.index];
    native_images::verify(r.contract,b.image);
    native_images::verify_values(r.contract);
    if(old!=handle||r.destination.type!=VkDescriptorType(b.type)||r.destination.set!=b.set||r.destination.binding!=b.binding)
        fatal("Native image material source/destination differs from legacy");
    ++census.compared;
}
void verify_plan(const Plan& p,const std::vector<const ShaderInfo*>& shaders) {
    if(native_images::mode()!=2)return;
    size_t sampled=0,storage=0;
    for(const auto* s:shaders)for(const auto& b:s->refl.bindings) {
        if(b.set!=2&&b.set!=3)continue;
        const auto& recipes=b.set==2?p.sampled:p.storage;auto& index=b.set==2?sampled:storage;
        if(index>=recipes.size()||recipes[index].legacy!=&b||recipes[index].shader!=s)
            fatal("Native image recipe order/composition differs from reflection");
        const auto& r=recipes[index++];const int stage=api_stage(s->stage);
        HandleReader reader=zero_handle;int slot=-1;uint64_t offset=0;
        if(b.set==2&&b.nvn.source==NvnSource::SamplerBinding&&b.nvn.index>=0&&b.nvn.index<32) {reader=texture_handle;slot=b.nvn.index;}
        else if(b.set==2&&b.nvn.source==NvnSource::HandleInUniform&&b.nvn.bank>=3) {reader=uniform_handle;slot=b.nvn.bank-3;offset=uint64_t(b.nvn.word)*4;}
        else if(b.set==3&&b.nvn.source==NvnSource::ImageBinding&&b.nvn.index>=0&&b.nvn.index<16) {reader=image_handle;slot=b.nvn.index;}
        if(r.read!=reader||r.stage!=stage||r.slot!=slot||r.byte_offset!=offset||r.destination.count!=b.count)
            fatal("Native image concrete handle reader differs from compatibility meaning");
    }
    if(sampled!=p.sampled.size()||storage!=p.storage.size())fatal("Native image recipe count differs from reflection");
}
bool supported(const Plan& p) {
    if(p.supported)return true;
    if(++census.fallback==1)log("Native image material compatibility fallback: array count or unsupported uniform-handle source");
    return false;
}
uint32_t gather(const Plan& p) {
    native_bindings::begin_packet();
    sample_bindings.assign(p.sampled.size(),{});
    auto& t=g_textures;
    if(draw_slow(kSlowScratch))t=DrawTextures{};else t.bindings.clear();
    for(const auto& r:p.sampled)t.bindings.push_back({r.shader,r.legacy});
    t.handles.assign(p.sampled.size(),0);t.resolved.assign(p.sampled.size(),ResolvedTexture{});
    uint32_t out=p.storage.empty()?0:kNeedBreak;
    const auto submit_ns=submit_start_ns();
    for(size_t k=0;k<p.sampled.size();++k) {
        const auto& recipe=p.sampled[k];const auto handle=recipe.read(recipe);verify(recipe,handle);t.handles[k]=handle;
        auto& r=t.resolved[k];uint64_t texture=0;Texture* sampled=nullptr;
        // Existing marked pool resolver is retained as compatibility, including
        // its snapshot/#64 behavior. No new Texture* cache or owner is introduced.
        if(auto* slot=handle_slot(handle)) {
            sampled=slot->object;if(sampled)texture=slot->texture;
            r.sampler=slot->sampler;r.view=slot->view;
            auto& vk=slot->vk_sampler[recipe.contract.depth?1:0];
            if(!vk)vk=slot->published_sampler?sampler_for_published(*slot->published_sampler,recipe.contract.depth):sampler_for(slot->sampler,recipe.contract.depth);r.vk_sampler=vk;
        } else {
            lookup_handle(handle,texture,r.sampler,&r.view);sampled=texture?get<Texture>(texture):nullptr;
        }
        // format_aspect uses NVN bits: depth=1, stencil=2 (not Vulkan aspect flags).
        if(!sampled||(recipe.contract.depth&&!(format_aspect(sampled->format)&1u)))texture=0;
        if(texture)sampled->last_sampled=submit_ns;r.texture=texture;
        // Keep the old resolver's exact first-observation/cache-reset semantics.
        // Export values, never a newly guessed persistent pool incarnation.
        if(texture&&r.vk_sampler)sample_bindings[k]=native_bindings::capture(texture,sampled,r.vk_sampler,r.view);
        auto* binding=sample_bindings[k].get();
        if(texture?!(binding?sampled_ready_bound(*binding):sampled_ready(texture)):!dummy_ready_native(recipe.contract))out|=kNeedBreak;
        if(!texture)continue;
        auto* image=binding?image_of_bound(*binding):image_of(texture);
        if(binding)prepare_bound_view(*binding,recipe.contract,image,false);
        if(pass_wrote(image))out|=kNeedBreak;
        if(clears_pending_image(image))out|=kNeedBreak|kSampledPending;
    }
    if(++census.gathers==1||census.gathers%100000==0)
        log("Native image materials: %llu plans, %llu gathers, %llu sampled writes, %llu storage writes, %llu comparisons, %llu compatibility fallbacks",
            (unsigned long long)census.plans,(unsigned long long)census.gathers,(unsigned long long)census.sampled,
            (unsigned long long)census.storage,(unsigned long long)census.compared,(unsigned long long)census.fallback);
    return out;
}
} // namespace image_materials
