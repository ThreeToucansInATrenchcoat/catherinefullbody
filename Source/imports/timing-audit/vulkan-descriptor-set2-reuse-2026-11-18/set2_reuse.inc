namespace set2_reuse {
constexpr size_t kBindings=16,kBytes=4096,kBuckets=256,kWays=4;
int mode(){static const int v=[] {const auto*p=std::getenv("CATHERINE_VULKAN_DESCRIPTOR_SET2_REUSE");return p&&!std::strcmp(p,"1")?1:p&&!std::strcmp(p,"2")?2:0;}();return v;}
struct Element {uint32_t binding,type,offset,size;uint64_t sampler,view,layout;bool operator==(const Element&)const=default;};
struct Request {const DescriptorByteLayout*bytes=nullptr;VkDescriptorSetLayout layout=VK_NULL_HANDLE;uint32_t size=0,count=0;std::array<Element,kBindings>elements;};
struct Context {uint64_t generation=0,epoch=0,serial=0;VkDevice device=VK_NULL_HANDLE;VkBuffer ring=VK_NULL_HANDLE;VkDeviceAddress address=0;bool operator==(const Context&)const=default;};
struct Entry {Context context;Request request;uint64_t hash=0,offset=0;bool valid=false;std::array<uint8_t,kBytes>payload{};};
std::array<std::array<Entry,kWays>,kBuckets>cache;
std::array<uint8_t,kBuckets>next_way{};
Entry*last=nullptr;
struct Census {uint64_t values=0,writes=0,no_set2=0,requests=0,hits=0,misses=0,compared=0,stores=0,collisions=0,evictions=0,unsupported=0,guarded=0,rebuilds=0,ring_bytes_saved=0;} census;
void report(){if(!mode())return;log("Descriptor set2 reuse: values %llu writes %llu no-set2 %llu requests %llu hits %llu misses %llu compared %llu stores %llu collisions %llu evictions %llu unsupported %llu guarded %llu rebuilds %llu ring-bytes-saved %llu",(unsigned long long)census.values,(unsigned long long)census.writes,(unsigned long long)census.no_set2,(unsigned long long)census.requests,(unsigned long long)census.hits,(unsigned long long)census.misses,(unsigned long long)census.compared,(unsigned long long)census.stores,(unsigned long long)census.collisions,(unsigned long long)census.evictions,(unsigned long long)census.unsupported,(unsigned long long)census.guarded,(unsigned long long)census.rebuilds,(unsigned long long)census.ring_bytes_saved);}
Context context(){return {generation,replay::memo87::g_epoch.load(std::memory_order_relaxed),t_replay_serial,vk_device(),descriptor_ring_buffer(),descriptor_ring_address()};}
bool equal(const Request&a,const Request&b){return a.bytes==b.bytes&&a.layout==b.layout&&a.size==b.size&&a.count==b.count&&std::equal(a.elements.begin(),a.elements.begin()+a.count,b.elements.begin());}
uint64_t hash(const Request&r){uint64_t h=uintptr_t(r.bytes)^uintptr_t(r.layout)^r.size;auto add=[&](uint64_t x){h=(h^x)*0x100000001b3ull;h^=h>>31;};add(r.count);for(unsigned n=0;n<r.count;++n){const auto&e=r.elements[n];add(uint64_t(e.binding)<<32|e.type);add(uint64_t(e.offset)<<32|e.size);add(e.sampler);add(e.view);add(e.layout);}return h;}
Entry*find(const Context&c,const Request&r,uint64_t h){if(last&&last->valid&&last->context==c&&equal(last->request,r))return last;for(auto&e:cache[h&(kBuckets-1)])if(e.valid&&e.context==c){if(e.hash==h&&equal(e.request,r))return last=&e;++census.collisions;}return nullptr;}
Entry&vacant(const Context&c,uint64_t h){const auto bucket=h&(kBuckets-1);for(auto&e:cache[bucket])if(!e.valid||!(e.context==c))return e;++census.evictions;auto&e=cache[bucket][next_way[bucket]];next_way[bucket]=(next_way[bucket]+1)%kWays;return e;}
bool valid_element(const ProgramLayout&layout,uint32_t set,uint32_t binding,VkDescriptorType type,const VkDescriptorBufferInfo*buffer,const VkDescriptorImageInfo*image,Request&r){
    if(set>=4||!layout.bytes[set]||bool(buffer)==bool(image))return false;
    const auto&b=*layout.bytes[set];const auto size=descriptor_byte_size(type);
    if(!size||size>128||binding>=b.offsets.size()||b.offsets[binding]>b.size||size>b.size-b.offsets[binding])return false;
    if(buffer){if(type!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER&&type!=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER)return false;}
    else if(type!=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER&&type!=VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)return false;
    if(set!=2)return true;
    if(buffer||type!=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER||r.count==kBindings||b.size>kBytes||b.offsets[binding]>UINT32_MAX)return false;
    r.bytes=&b;r.layout=layout.sets[2];r.size=uint32_t(b.size);r.elements[r.count++]={binding,uint32_t(type),uint32_t(b.offsets[binding]),uint32_t(size),uintptr_t(image->sampler),uintptr_t(image->imageView),uint64_t(image->imageLayout)};return true;
}
bool ring_safe(const ProgramLayout&layout){
    const auto*props=descriptor_buffer_properties();const uint64_t ring=frame_ring_size(),used=frame_upload_used();
    if(!props||!ring||used>=ring||!t_replay_serial||!descriptor_ring_buffer()||!descriptor_ring_address())return false;
    const uint64_t alignment=props->descriptorBufferOffsetAlignment;if(!alignment||(alignment&(alignment-1)))return false;
    uint64_t total=0;
    for(const auto*set:layout.bytes){if(!set||set->size>ring||total>ring-set->size)return false;total+=set->size;if(total>ring-(alignment-1))return false;total=(total+alignment-1)&~(alignment-1);}
    // One original combined allocation can advance at most2*total+alignment:
    // aligning and skipping the physical ring end each cost less than that.
    const uint64_t available=ring-used;
    return alignment<=available&&total<=(available-alignment)/2;
}
Upload allocate_without_set2(const ProgramLayout&layout,std::array<VkDeviceSize,4>&offsets){const auto alignment=descriptor_buffer_properties()->descriptorBufferOffsetAlignment;VkDeviceSize total=0;for(unsigned k=0;k<4;++k){offsets[k]=total;if(k!=2)total=align_up(total+layout.bytes[k]->size,alignment);}return total?upload_alloc(total,alignment):Upload{descriptor_ring_buffer(),0,nullptr};}
void copy_payload(uint8_t*target,const Entry&e){for(unsigned n=0;n<e.request.count;++n){const auto&r=e.request.elements[n];std::memcpy(target+r.offset,e.payload.data()+r.offset,r.size);}}
void verify(const Entry&e){if(mode()!=2)return;std::array<uint8_t,kBytes>oracle{};for(unsigned n=0;n<e.request.count;++n){const auto&v=e.request.elements[n];VkDescriptorImageInfo image{VkSampler(v.sampler),VkImageView(v.view),VkImageLayout(v.layout)};write_bytes(oracle.data()+v.offset,VkDescriptorType(v.type),nullptr,&image);}for(unsigned n=0;n<e.request.count;++n){const auto&v=e.request.elements[n];if(std::memcmp(oracle.data()+v.offset,e.payload.data()+v.offset,v.size))fatal("Descriptor set2 cached payload differs from original encoding");}++census.compared;}
void store(const Context&c,const Request&r,uint64_t h,uint64_t offset,const uint8_t*data){auto&e=vacant(c,h);e.valid=false;e.context=c;e.request.bytes=r.bytes;e.request.layout=r.layout;e.request.size=r.size;std::copy_n(r.elements.begin(),r.count,e.request.elements.begin());e.request.count=r.count;e.hash=h;e.offset=offset;for(unsigned n=0;n<r.count;++n){const auto&v=r.elements[n];std::memcpy(e.payload.data()+v.offset,data+v.offset,v.size);}e.valid=true;last=&e;++census.stores;}
bool valid_upload(const Upload&data,const ProgramLayout&layout,bool without_set2,VkBuffer expected_ring){
    if(!data.buffer||data.buffer!=expected_ring)return false;
    bool needed=false;for(unsigned k=0;k<4;++k)if((!without_set2||k!=2)&&layout.bytes[k]->size)needed=true;
    return !needed||data.ptr;
}
template<class Write>bool execute(VkCommandBuffer cb,VkPipelineBindPoint point,const ProgramLayout&layout,const Request&r,Write&&write){
    ++census.requests;const auto initial=context();
    if(!ring_safe(layout)){++census.guarded;last=nullptr;return false;}
    const auto h=hash(r);Entry*hit=find(initial,r,h);if(hit)verify(*hit);
    std::array<VkDeviceSize,4>offsets{};
    Upload data=hit?allocate_without_set2(layout,offsets):allocate_descriptor_bytes(layout,offsets);
    Context allocated=context();
    if(!valid_upload(data,layout,bool(hit),allocated.ring)){++census.guarded;last=nullptr;return false;}
    bool reuse=hit&&allocated==initial;
    auto rebuild=[&] {
        // Fixed array entries keep their owned bytes through allocator resets;
        // no store/eviction occurs until this function finishes encoding.
        ++census.rebuilds;data=allocate_descriptor_bytes(layout,offsets);allocated=context();
        if(!valid_upload(data,layout,false,allocated.ring))return false;
        if(hit&&allocated.device==initial.device&&allocated.serial==initial.serial&&allocated.generation==initial.generation){copy_payload(data.ptr+offsets[2],*hit);write(data,offsets,true);}else write(data,offsets,false);
        return context()==allocated;
    };
    if(hit&&!reuse){if(!rebuild()){++census.guarded;last=nullptr;return false;}}
    else {
        write(data,offsets,reuse);
        if(!(context()==allocated)){reuse=false;if(!rebuild()){++census.guarded;last=nullptr;return false;}}
    }
    if(reuse){
        const auto saved=hit->offset;
        for(unsigned k=0;k<4;++k)offsets[k]=layout.bytes[k]->size?data.offset+offsets[k]:0;
        offsets[2]=saved;bind_offsets(cb,point,layout.layout,offsets);++census.hits;census.ring_bytes_saved+=r.size;
    }else{
        ++census.misses;const auto final=context();
        if(final==allocated&&final.serial==initial.serial&&final.generation==initial.generation&&final.device==initial.device&&ring_safe(layout)&&data.buffer==final.ring){store(final,r,h,data.offset+offsets[2],data.ptr+offsets[2]);verify(*last);}
        if(!(context()==allocated)){if(last)last->valid=false;last=nullptr;++census.guarded;return false;}
        bind_uploaded_descriptor_bytes(cb,point,layout,offsets,data.offset);
    }
    if(census.requests==1||census.requests%120000==0)report();return true;
}
bool values(VkCommandBuffer cb,VkPipelineBindPoint point,const ProgramLayout&layout,const std::vector<DescriptorValue>&values){
    if(!mode())return false;++census.values;
    if(std::none_of(values.begin(),values.end(),[](const auto&v){return v.set==2;})){++census.no_set2;return false;}
    Request r;if(layout.dynamic){++census.unsupported;return false;}
    for(const auto&v:values)if(!valid_element(layout,v.set,v.binding,v.type,v.uses_buffer?&v.buffer:nullptr,v.uses_buffer?nullptr:&v.image,r)){++census.unsupported;return false;}
    if(!r.count){++census.unsupported;return false;}
    return execute(cb,point,layout,r,[&](const Upload&data,const std::array<VkDeviceSize,4>&offsets,bool skip2){for(const auto&v:values){if(skip2&&v.set==2)continue;write_bytes(data.ptr+offsets[v.set]+layout.bytes[v.set]->offsets[v.binding],v.type,v.uses_buffer?&v.buffer:nullptr,v.uses_buffer?nullptr:&v.image);}});
}
bool writes(VkCommandBuffer cb,VkPipelineBindPoint point,const ProgramLayout&layout,const std::vector<VkWriteDescriptorSet>&writes,const std::vector<uint8_t>&sets){
    if(!mode())return false;++census.writes;
    if(std::find(sets.begin(),sets.end(),uint8_t(2))==sets.end()){++census.no_set2;return false;}
    Request r;if(layout.dynamic||writes.size()!=sets.size()){++census.unsupported;return false;}
    for(size_t n=0;n<writes.size();++n){const auto&w=writes[n];if(w.pNext||w.dstArrayElement||w.pTexelBufferView||w.descriptorCount!=1||!valid_element(layout,sets[n],w.dstBinding,w.descriptorType,w.pBufferInfo,w.pImageInfo,r)){++census.unsupported;return false;}}
    if(!r.count){++census.unsupported;return false;}
    return execute(cb,point,layout,r,[&](const Upload&data,const std::array<VkDeviceSize,4>&offsets,bool skip2){for(size_t n=0;n<writes.size();++n){if(skip2&&sets[n]==2)continue;const auto&w=writes[n];write_bytes(data.ptr+offsets[sets[n]]+layout.bytes[sets[n]]->offsets[w.dstBinding],w.descriptorType,w.pBufferInfo,w.pImageInfo);}});
}
}
