#pragma once
#include "character_pose_interpolation.h"
#include "block20_root_translation.h"

namespace catherine::visual {
// Private candidate: only c001_013's four authenticated weighted sources.
// The caller must supply an immutable native owner/lifetime and native world
// root. Never infer an owner or root from a bone, mesh proximity, or palette.
class Root12Translation {
    struct Profile {uint32_t count,stride;uint64_t hash;};
    static constexpr Profile profiles[]={{298,80,0x8ba57b98c1a550c3ull},
        {273,80,0xee72829ef2b96aaeull},{79,80,0xea98fb25079832a8ull},
        {64,64,0xdceaa966a7fcffd2ull}};
    struct Hash {size_t operator()(const Block20Owner& o)const {
        return size_t(o.owner^(o.generation*0x9e3779b97f4a7c15ull)^
                      (o.model<<7)^(o.parent>>3)^(o.lifetime*0xd6e8feb86659fd93ull));
    }};
    struct State {
        PoseInterpolator roots;
        uint64_t frame=~uint64_t(0);
        std::array<float,3> target{},raw{};
        std::array<float,240> output{};
        std::array<float,16> rigid_output{};
    };
    std::unordered_map<Block20Owner,State,Hash> states;
    std::array<std::vector<uint8_t>,4> source_cache;
    std::vector<uint8_t> cage_source_cache;
    uint64_t last_pruned=0;
    static unsigned profile(uint32_t count,uint32_t stride,uint64_t bytes) {
        for(unsigned i=0;i<4;++i)if(profiles[i].count==count&&profiles[i].stride==stride&&bytes==uint64_t(count)*stride)return i;
        return 4;
    }
    State& target(Block20Owner owner,const std::array<float,3>& root,uint64_t frame,uint64_t clock) {
        if(states.size()>1024)states.clear();
        if(frame>last_pruned+64){last_pruned=frame;for(auto it=states.begin();it!=states.end();) {
            if(it->second.frame!=~uint64_t(0)&&frame>it->second.frame&&frame-it->second.frame>80)it=states.erase(it);else ++it;
        }}
        State& state=states[owner];
        // Copy-validated samples share the frozen target even if their represented roots differ within this present.
        if(frame!=state.frame) {
            double distance=0;for(unsigned axis=0;axis<3;++axis){double d=double(root[axis])-state.raw[axis];distance+=d*d;}
            if(state.frame!=~uint64_t(0)&&(frame<state.frame||frame>state.frame+2||distance>=75.*75.))state.roots.clear();
            // Use the existing generic rigid translation clock law. Do not
            // blend the palette: its local SDK animation is already fractional.
            std::array<float,16> matrix{1,0,0,root[0],0,1,0,root[1],0,0,1,root[2],0,0,0,1};
            const auto* rendered=state.roots.render(1,reinterpret_cast<const uint8_t*>(matrix.data()),64,frame,clock,true);
            std::array<float,16> output;std::memcpy(output.data(),rendered,64);
            state.target={output[3],output[7],output[11]};state.frame=frame;state.raw=root;
        }
        return state;
    }
public:
    void clear(){states.clear();}
    static bool accepts(const uint8_t* source,uint64_t bytes,uint32_t count,uint32_t stride,uint64_t palette_bytes) {
        unsigned p=profile(count,stride,bytes);if(!source||palette_bytes!=960||p==4)return false;
        uint64_t hash=14695981039346656037ull;
        for(uint64_t i=0;i<bytes;++i)hash=(hash^source[i])*1099511628211ull;
        return hash==profiles[p].hash;
    }
    const uint8_t* render(bool validated,Block20Owner owner,const std::array<float,3>& root,
        const uint8_t* palette,uint64_t palette_bytes,const uint8_t* source,uint64_t source_bytes,
        uint32_t count,uint32_t stride,uint64_t frame,uint64_t clock) {
        auto reject=[&](){if(owner.valid())states.erase(owner);return palette;};
        uint32_t bits=uint32_t(clock)&0x7fffffffu;float fraction;std::memcpy(&fraction,&bits,4);
        if(!validated||!owner.valid()||!owner.lifetime||!palette||palette_bytes!=960||
           !(clock&0x80000000u)||!std::isfinite(fraction)||fraction<0||fraction>1)return reject();
        for(float value:root)if(!std::isfinite(value))return reject();
        for(unsigned i=0;i<240;++i){float v;std::memcpy(&v,palette+4*i,4);if(!std::isfinite(v))return reject();}
        unsigned p=profile(count,stride,source_bytes);if(!source||p==4)return reject();
        auto& cache=source_cache[p];
        if(cache.size()!=source_bytes||std::memcmp(cache.data(),source,source_bytes)) {
            if(!accepts(source,source_bytes,count,stride,palette_bytes))return reject();
            cache.assign(source,source+source_bytes);
        }
        State& state=target(owner,root,frame,clock);
        if(state.target==root)return palette;
        std::memcpy(state.output.data(),palette,960);
        for(unsigned axis=0;axis<3;++axis) {
            double offset=double(state.target[axis])-root[axis];
            for(unsigned joint=0;joint<15;++joint) {
                unsigned word=joint*16+axis*4+3;
                state.output[word]=float(double(state.output[word])+offset);
            }
        }
        return reinterpret_cast<const uint8_t*>(state.output.data());
    }
    // Only the authenticated c001_097 Safety cage, using the weighted owner target.
    const uint8_t* rigid(bool validated,Block20Owner owner,const std::array<float,3>& root,
        const uint8_t* bytes,uint64_t size,uint64_t geometry,const uint8_t* vertices,
        uint64_t vertex_bytes,uint32_t stride,uint64_t frame,uint64_t clock,bool* handled=nullptr) {
        if(handled)*handled=false;
        if(!(clock&0x80000000u)){if(owner.valid())states.erase(owner);return bytes;}
        uint32_t bits=uint32_t(clock)&0x7fffffffu;float fraction;std::memcpy(&fraction,&bits,4);
        if(!validated||!owner.valid()||!owner.lifetime||!bytes||size!=64||!vertices||
           geometry!=0x91d47031d45ed1c5ull||vertex_bytes!=34776||stride!=36||
           !std::isfinite(fraction)||fraction<0||fraction>1)return bytes;
        for(float value:root)if(!std::isfinite(value))return bytes;
        if(cage_source_cache.size()!=vertex_bytes||std::memcmp(cage_source_cache.data(),vertices,vertex_bytes)) {
            uint64_t hash=14695981039346656037ull;
            for(uint64_t i=0;i<vertex_bytes;++i)hash=(hash^vertices[i])*1099511628211ull;
            if(hash!=0x7471e26ff1938ce4ull)return bytes;
            cage_source_cache.assign(vertices,vertices+vertex_bytes);
        }
        std::array<float,16> raw;std::memcpy(raw.data(),bytes,64);
        for(float x:raw)if(!std::isfinite(x))return bytes;
        if(std::fabs(raw[12])>1e-5f||std::fabs(raw[13])>1e-5f||std::fabs(raw[14])>1e-5f||std::fabs(raw[15]-1)>1e-5f)return bytes;
        auto& state=target(owner,root,frame,clock);if(handled)*handled=true;
        if(state.target==root)return bytes;
        state.rigid_output=raw;
        for(unsigned axis=0;axis<3;++axis)
            state.rigid_output[axis*4+3]=float(double(raw[axis*4+3])+double(state.target[axis])-root[axis]);
        return reinterpret_cast<const uint8_t*>(state.rigid_output.data());
    }
};
// Defined once by the native bridge; weighted and cage paths call this same instance.
Root12Translation& root12_presentation();
}
