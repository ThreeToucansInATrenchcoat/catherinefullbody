#pragma once
#include <array>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include "producer_pipeline.h"
#include "producer_fixed_pipeline.h"

namespace nx::nvn::native_pipeline {
struct Definition {
    const ShaderInfo* vs=nullptr;
    const ShaderInfo* fs=nullptr;
    producer_pipeline::VertexInputs vertex{};
    producer_fixed::Inputs fixed{};
    VkFormat color_formats[8]{};
    VkFormat depth_format=VK_FORMAT_UNDEFINED;
    uint32_t color_count=0,ufloat_colors=0;
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
};
inline int mode(){static const int value=[] {const char* e=std::getenv("CATHERINE_VULKAN_NATIVE_PIPELINE");return e?std::atoi(e):1;}();return value;}
inline bool comparing(){return mode()==2||producer_pipeline::mode()==2||producer_fixed::mode()==2;}
inline bool ready(const Cmd& draw){
    return mode()&&producer_pipeline::mode()&&producer_fixed::mode()&&
        producer_pipeline::state.attributes_valid&&producer_pipeline::state.bindings_valid&&
        producer_fixed::state.valid==producer_fixed::AllBits&&draw.a[3]==producer_pipeline::topology_tag;
}
inline Definition current(const ShaderInfo* vs,const ShaderInfo* fs,const Cmd& draw){
    Definition result;
    result.vs=vs;result.fs=fs;
    result.vertex={producer_pipeline::state.attributes,producer_pipeline::state.bindings,VkPrimitiveTopology(draw.a[2])};
    result.fixed=producer_fixed::state.values;
    return result;
}
inline Definition original(const PipelineKey& key){
    Definition result;result.vs=key.vs;result.fs=key.fs;
    result.vertex=producer_pipeline::legacy(key);result.fixed=producer_fixed::legacy(key);
    std::copy_n(key.color_formats,8,result.color_formats);result.depth_format=key.depth_format;
    result.color_count=key.color_count;result.ufloat_colors=key.ufloat_colors;result.samples=key.samples;
    return result;
}
// Serialize named scalar fields, never C++ padding or borrowed-array addresses.
// Hash collisions are confirmed by the entire used word sequence.
struct Key {
    std::array<uint32_t,384> words{};
    uint32_t count=0;
    void put(uint32_t value){if(count==words.size())fatal("Native pipeline key overflow");words[count++]=value;}
    void wide(uint64_t value){put(uint32_t(value));put(uint32_t(value>>32));}
    bool operator==(const Key& other)const{return count==other.count&&!std::memcmp(words.data(),other.words.data(),count*sizeof(uint32_t));}
};
struct KeyHash {size_t operator()(const Key& value)const{return size_t(hash_bytes(reinterpret_cast<const unsigned char*>(value.words.data()),value.count*sizeof(uint32_t)));}};
inline Key key(const Definition& value,const ProgramLayout& layout){
    Key result;
    result.wide(reinterpret_cast<uintptr_t>(value.vs));result.wide(reinterpret_cast<uintptr_t>(value.fs));
    result.wide(uint64_t(layout.layout));result.put(layout.bytes[0]!=nullptr);
    const auto& vertex=value.vertex;
    result.put(vertex.attributes.count);
    for(unsigned i=0;i<16;++i){const auto& a=vertex.attributes.values[i];result.put(a.location);result.put(a.binding);result.put(uint32_t(a.format));result.put(a.offset);result.put(vertex.attributes.int_kinds[i]);}
    result.put(vertex.bindings.count);
    for(const auto& b:vertex.bindings.values){result.put(b.binding);result.put(b.stride);result.put(uint32_t(b.inputRate));}
    result.put(uint32_t(vertex.topology));
    const auto& fixed=value.fixed;
    for(const auto& b:fixed.blend){result.put(b.blendEnable);result.put(b.srcColorBlendFactor);result.put(b.dstColorBlendFactor);result.put(b.colorBlendOp);result.put(b.srcAlphaBlendFactor);result.put(b.dstAlphaBlendFactor);result.put(b.alphaBlendOp);result.put(b.colorWriteMask);}
    const auto& depth=fixed.depth;result.put(depth.test);result.put(depth.write);result.put(depth.stencil);result.put(depth.compare);result.put(depth.slack);
    for(const auto* face:{&depth.front,&depth.back}){result.put(face->failOp);result.put(face->passOp);result.put(face->depthFailOp);result.put(face->compareOp);result.put(face->compareMask);result.put(face->writeMask);result.put(face->reference);}
    result.put(fixed.polygon.mode);result.put(fixed.polygon.cull);result.put(fixed.polygon.front);result.put(fixed.polygon.offset);result.put(fixed.depth_clamp);result.put(fixed.alpha_to_coverage);
    for(auto format:value.color_formats)result.put(uint32_t(format));
    result.put(uint32_t(value.depth_format));result.put(value.color_count);result.put(value.ufloat_colors);result.put(uint32_t(value.samples));
    return result;
}
inline void validate(const Definition& value,const PipelineKey& old,const ProgramLayout& layout){
    if(!(key(value,layout)==key(original(old),layout)))fatal("Native pipeline definition differs from original compatibility inputs");
    producer_pipeline::validate(old,value.vertex);producer_fixed::validate(old,value.fixed);
}

inline int memo_mode(){static const int value=[] {const char* e=std::getenv("CATHERINE_VULKAN_NATIVE_PIPELINE_MEMO");return e?std::atoi(e):1;}();return value;}
struct Selection { VkPipeline pipeline;const Definition* definition; };
// A byte match over these owned scalar structures is sufficient for semantic
// equality. Padding/inactive bytes can only cause conservative cache misses.
// Do not compare Program*, mutable resource records or borrowed GPU owners.
inline bool unchanged(const Definition& saved,VkPipelineLayout saved_layout,bool saved_bytes,
                      const ShaderInfo* vs,const ShaderInfo* fs,const Cmd& draw,const ProgramLayout& layout,
                      const VkFormat* colors,VkFormat depth,uint32_t count,uint32_t ufloat,VkSampleCountFlagBits samples){
    return saved.vs==vs&&saved.fs==fs&&saved_layout==layout.layout&&saved_bytes==(layout.bytes[0]!=nullptr)&&
        saved.vertex.topology==VkPrimitiveTopology(draw.a[2])&&saved.depth_format==depth&&saved.color_count==count&&saved.ufloat_colors==ufloat&&saved.samples==samples&&
        !std::memcmp(saved.color_formats,colors,sizeof saved.color_formats)&&
        !std::memcmp(&saved.vertex.attributes,&producer_pipeline::state.attributes,sizeof saved.vertex.attributes)&&
        !std::memcmp(&saved.vertex.bindings,&producer_pipeline::state.bindings,sizeof saved.vertex.bindings)&&
        !std::memcmp(&saved.fixed,&producer_fixed::state.values,sizeof saved.fixed);
}
inline constexpr uint32_t memo_bucket_count=64,memo_ways=4;
inline uint32_t memo_bucket(const ShaderInfo* vs,const ShaderInfo* fs,const Cmd& draw,const ProgramLayout& layout,
                            VkFormat depth,uint32_t count,VkSampleCountFlagBits samples){
    // Only a cheap candidate selector. All shader/layout/attachment and owned
    // state fields are still checked by unchanged() before any hit is accepted.
    uint64_t hash=(uint64_t(vs)>>4)^((uint64_t(fs)>>4)*0x9e3779b97f4a7c15ull)^(uint64_t(layout.layout)>>4);
    hash^=uint64_t(draw.a[2])<<7;hash^=uint64_t(uint32_t(depth))<<13;hash^=uint64_t(count)<<25;hash^=uint64_t(samples)<<33;
    hash^=hash>>32;hash^=hash>>16;hash^=hash>>8;
    return uint32_t(hash)&(memo_bucket_count-1);
}
inline uint64_t memo_requests=0,memo_hits=0,memo_misses=0,memo_compared=0,memo_associative_hits=0,memo_collisions=0,memo_evictions=0;
inline void memo_report(){if(memo_requests==1||memo_requests%100000==0)log("Native pipeline memo: requests %llu hits %llu misses %llu compared %llu associative %llu collisions %llu evictions %llu",(unsigned long long)memo_requests,(unsigned long long)memo_hits,(unsigned long long)memo_misses,(unsigned long long)memo_compared,(unsigned long long)memo_associative_hits,(unsigned long long)memo_collisions,(unsigned long long)memo_evictions);}

inline uint64_t ordinary_requests=0,tower_requests=0,compatibility_requests=0,compared_requests=0,cache_hits=0,creations=0,compatibility_key_builds=0;
inline void report(){const uint64_t total=ordinary_requests+tower_requests+compatibility_requests;if(total==1||total%100000==0)
    log("Native pipeline definitions: ordinary %llu tower %llu compatibility %llu compared %llu hits %llu creations %llu legacy keys %llu",(unsigned long long)ordinary_requests,(unsigned long long)tower_requests,(unsigned long long)compatibility_requests,(unsigned long long)compared_requests,(unsigned long long)cache_hits,(unsigned long long)creations,(unsigned long long)compatibility_key_builds);}
}
namespace nx::nvn {VkPipeline graphics_pipeline_definition(const native_pipeline::Definition&,const ProgramLayout&);}

namespace nx::nvn {native_pipeline::Selection graphics_pipeline_current(const ShaderInfo*,const ShaderInfo*,const Cmd&,const ProgramLayout&,const VkFormat*,VkFormat,uint32_t,uint32_t,VkSampleCountFlagBits,const PipelineKey*);}
