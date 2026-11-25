#pragma once
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>
#include "nvn_pipeline.h"
namespace nx::nvn::producer_pipeline {
constexpr uint64_t vertex_tag=0x31585456504b5655ull, topology_tag=0x31504f54504b5655ull;
enum class Kind:uint32_t { Attributes=1,Bindings=2 };
struct Header { Kind kind;uint32_t bytes; };
struct Attributes { uint32_t count;VkVertexInputAttributeDescription values[16];uint8_t int_kinds[16]; };
struct Bindings { uint32_t count;VkVertexInputBindingDescription values[16]; };
struct VertexInputs {
    Attributes attributes;
    Bindings bindings;
    VkPrimitiveTopology topology;
};
static_assert(std::is_trivially_copyable_v<VertexInputs>);
// perf 2026-11-22: default 0 (legacy reference path). The producer lowering cost ~30 FPS at the
// Stage 9/6 boss and ~12 FPS in the Day 5 bar; set the variable to 1 (or 2 to verify) to re-enable.
inline int mode(){static const int m=[] {const char* p=std::getenv("CATHERINE_VULKAN_PRODUCER_PIPELINE");return p?std::atoi(p):0;}();return m;}
inline Attributes empty_attributes(){Attributes a{};for(uint32_t i=0;i<16;++i)a.values[i].location=i;return a;}
inline Bindings empty_bindings(){Bindings b{};for(uint32_t i=0;i<16;++i)b.values[i].binding=i;return b;}
inline Attributes convert_attributes(const VertexAttrib* raw,uint32_t count){
    Attributes a=empty_attributes();a.count=std::min(count,16u);
    for(uint32_t i=0;i<a.count;++i){a.values[i]={i,raw[i].stream,vk_vertex_format(raw[i].format),raw[i].offset};a.int_kinds[i]=uint8_t(vertex_int_kind(raw[i].format));}return a;
}
inline Bindings convert_bindings(const VertexStream* raw,uint32_t count){
    Bindings b=empty_bindings();b.count=std::min(count,16u);
    for(uint32_t i=0;i<b.count;++i)b.values[i]={i,raw[i].stride,raw[i].divisor?VK_VERTEX_INPUT_RATE_INSTANCE:VK_VERTEX_INPUT_RATE_VERTEX};return b;
}
inline std::atomic<uint64_t> recorded_vertex_packets{0},recorded_topologies{0};
// Recording owns each converted value. Existing native raster/clear trailers remain at their original offsets.
inline void attach(Cmd& c,const void*& blob,size_t& bytes,std::vector<uint8_t>& owned){
    if(!mode())return;
    if(c.op==Cmd::DrawElements||c.op==Cmd::DrawArrays){c.a[2]=uint64_t(vk_topology(c.n));c.a[3]=topology_tag;recorded_topologies.fetch_add(1,std::memory_order_relaxed);return;}
    if(c.op!=Cmd::BindVertexAttribs&&c.op!=Cmd::BindVertexStreams)return;
    if(c.n==0||c.n>32)return;
    const bool attrib=c.op==Cmd::BindVertexAttribs;
    const size_t raw_bytes=size_t(c.n)*(attrib?sizeof(VertexAttrib):sizeof(VertexStream));
    if((raw_bytes&&!blob)||bytes<raw_bytes)return;
    // Texture-style formats whose Vulkan representation depends on device caps remain late.
    // Legal fixed vertex formats, including the integer/scaled family, are converted here.
    if(attrib){const auto* a=static_cast<const VertexAttrib*>(blob);for(uint32_t i=0;i<std::min(c.n,16u);++i)if(a[i].format==0x3f||(a[i].format>=0x79&&a[i].format<=0x94))return;}
    const uint32_t payload_size=attrib?sizeof(Attributes):sizeof(Bindings);
    Header h{attrib?Kind::Attributes:Kind::Bindings,payload_size};
    const size_t prefix=blob?bytes:0;owned.resize(prefix+sizeof h+payload_size);
    if(prefix)std::memcpy(owned.data(),blob,prefix);std::memcpy(owned.data()+prefix,&h,sizeof h);
    if(attrib){const auto a=convert_attributes(static_cast<const VertexAttrib*>(blob),c.n);std::memcpy(owned.data()+prefix+sizeof h,&a,sizeof a);}
    else{const auto b=convert_bindings(static_cast<const VertexStream*>(blob),c.n);std::memcpy(owned.data()+prefix+sizeof h,&b,sizeof b);}
    c.a[5]=prefix;c.a[6]=vertex_tag;blob=owned.data();bytes=owned.size();recorded_vertex_packets.fetch_add(1,std::memory_order_relaxed);
}
struct Shadow{Attributes attributes=empty_attributes();Bindings bindings=empty_bindings();bool attributes_valid=true,bindings_valid=true;};
inline Shadow state; // Replay owns this composable state, including inheritance across called lists/submits.
inline uint64_t applied_packets=0,unsupported_packets=0,prepared_inputs=0,validated_inputs=0,native_creations=0;
inline void apply(const Cmd& c,const CmdList& list){
    if(!mode()||(c.op!=Cmd::BindVertexAttribs&&c.op!=Cmd::BindVertexStreams))return;
    const bool attrib=c.op==Cmd::BindVertexAttribs;
    // Match the compatibility reader's empty-blob no-op, rather than changing it by adding a trailer.
    if(c.n==0 && (c.blob>=list.blobs.size() || list.blobs[c.blob].data()==nullptr))return;
    auto unavailable=[&]{if(attrib)state.attributes_valid=false;else state.bindings_valid=false;++unsupported_packets;};
    if(c.a[6]!=vertex_tag||c.blob>=list.blobs.size()){unavailable();return;}
    const auto& blob=list.blobs[c.blob];const size_t prefix=size_t(c.a[5]);
    if(prefix>blob.size()||sizeof(Header)>blob.size()-prefix){unavailable();return;}
    Header h;std::memcpy(&h,blob.data()+prefix,sizeof h);
    const size_t expected=attrib?sizeof(Attributes):sizeof(Bindings);
    if(h.kind!=(attrib?Kind::Attributes:Kind::Bindings)||h.bytes!=expected||expected>blob.size()-prefix-sizeof h){unavailable();return;}
    if(attrib){Attributes a;std::memcpy(&a,blob.data()+prefix+sizeof h,sizeof a);if(a.count>16){unavailable();return;}state.attributes=a;state.attributes_valid=true;}
    else{Bindings b;std::memcpy(&b,blob.data()+prefix+sizeof h,sizeof b);if(b.count>16){unavailable();return;}state.bindings=b;state.bindings_valid=true;}
    ++applied_packets;
}
inline VertexInputs legacy(const PipelineKey& key){return {convert_attributes(key.attribs,std::min(key.attrib_count,16u)),convert_bindings(key.streams,std::min(key.stream_count,16u)),vk_topology(key.topology)};}
inline VertexInputs prepare(const PipelineKey& key,const Cmd& draw){
    if(!mode())return legacy(key);
    VertexInputs p;
    p.attributes=state.attributes_valid?state.attributes:convert_attributes(key.attribs,std::min(key.attrib_count,16u));
    p.bindings=state.bindings_valid?state.bindings:convert_bindings(key.streams,std::min(key.stream_count,16u));
    p.topology=draw.a[3]==topology_tag?VkPrimitiveTopology(draw.a[2]):vk_topology(key.topology);
    ++prepared_inputs;return p;
}
// Remains GPU-side: only shader reflection determines which recorded attributes/streams are read.
inline void filter(const VertexInputs& input,const ShaderInfo* shader,
                   std::vector<VkVertexInputAttributeDescription>& attributes,
                   std::vector<VkVertexInputBindingDescription>& bindings){
    bool used[16]={},zero=false;
    if(shader)for(const auto& io:shader->refl.inputs){
        VkVertexInputAttributeDescription a{io.location,15,VK_FORMAT_R32G32B32A32_SFLOAT,0};
        if(io.location<input.attributes.count){const auto& recorded=input.attributes.values[io.location];if(recorded.format!=VK_FORMAT_UNDEFINED&&recorded.binding<15){a=recorded;a.location=io.location;used[a.binding]=true;}}
        if(a.binding==15)zero=true;attributes.push_back(a);
    }
    for(uint32_t i=0;i<15;++i)if(used[i])bindings.push_back(input.bindings.values[i]);
    if(zero)bindings.push_back({15,0,VK_VERTEX_INPUT_RATE_INSTANCE});
}
inline void validate(const PipelineKey& key,const VertexInputs& p){
    if(mode()!=2)return;const auto old=legacy(key);
    if(std::memcmp(&old,&p,sizeof p))fatal("Producer Vulkan vertex/topology inputs disagree with compatibility key");
    if(++validated_inputs==1||validated_inputs%10000==0)log("Producer Vulkan pipeline input verified: %llu requests, %llu packet applications, %llu unsupported",(unsigned long long)validated_inputs,(unsigned long long)applied_packets,(unsigned long long)unsupported_packets);
}
} // namespace nx::nvn::producer_pipeline
namespace nx::nvn {
VkPipeline graphics_pipeline_native(const PipelineKey&,const ProgramLayout&,const producer_pipeline::VertexInputs&);
}
