#pragma once
#include "character_pose_interpolation.h"
#include "block20_root_translation.h"

namespace catherine::visual {
// Private prototype. Both rigid and weighted callers must use the SAME instance
// and immutable native owner. Independent per-mesh histories produce seams when
// a mesh is skipped and later starts drawing.
class Root9Presentation {
    struct Hash {size_t operator()(const Block20Owner& o)const {
        return size_t(o.owner^(o.generation*0x9e3779b97f4a7c15ull)^
                      (o.model<<7)^(o.parent>>3)^(o.lifetime*0xd6e8feb86659fd93ull));
    }};
    struct State {
        PoseInterpolator roots;
        uint64_t frame=~uint64_t(0);
        std::array<float,3> raw{},target{};
        std::array<float,240> palette{};
        std::array<float,16> rigid{};
    };
    std::unordered_map<Block20Owner,State,Hash> states;
    std::array<std::vector<uint8_t>,3> accepted;
    std::array<std::vector<uint8_t>,2> rigid_sources;
    static constexpr uint64_t source_hashes[]={0xe4e44b03bd81cd43ull,0x9bef72ac9e36f1f2ull,0x0c62a1828dc75405ull};
    uint64_t last_pruned=0;
    State* target(bool validated,Block20Owner owner,const std::array<float,3>& root,uint64_t frame,uint64_t clock) {
        uint32_t bits=uint32_t(clock)&0x7fffffffu;float phase;std::memcpy(&phase,&bits,4);
        if(!(clock&0x80000000u)||!std::isfinite(phase)||phase<0||phase>1){clear();return nullptr;}
        if(!validated||!owner.valid()||!owner.lifetime)return nullptr;
        for(float x:root)if(!std::isfinite(x)){states.erase(owner);return nullptr;}
        if(states.size()>1024)states.clear();
        if(frame>last_pruned+64){last_pruned=frame;for(auto i=states.begin();i!=states.end();){
            if(i->second.frame!=~uint64_t(0)&&frame>i->second.frame&&frame-i->second.frame>80)i=states.erase(i);else ++i;
        }}
        auto& s=states[owner];
        // Producer validation must establish each packet's represented root.
        // A worker may copy an earlier native root within this presentation.
        // Keep one target and subtract THAT packet's root, never draw it raw.
        if(frame==s.frame)return &s;
        double distance=0;for(unsigned k=0;k<3;++k){double d=double(root[k])-s.raw[k];distance+=d*d;}
        if(s.frame!=~uint64_t(0)&&(frame<s.frame||frame>s.frame+2||distance>=75.*75.))s.roots.clear();
        std::array<float,16> matrix{1,0,0,root[0],0,1,0,root[1],0,0,1,root[2],0,0,0,1};
        const auto* bytes=s.roots.render(1,reinterpret_cast<const uint8_t*>(matrix.data()),64,frame,clock,true);
        std::array<float,16> out;std::memcpy(out.data(),bytes,64);
        s.target={out[3],out[7],out[11]};s.raw=root;s.frame=frame;return &s;
    }
    bool source(const uint8_t* p,uint64_t size,uint32_t count,uint32_t stride) {
        if(!p||size!=1600||count!=25||stride!=64)return false;
        // All THREE shapes are identical: never choose by count/stride alone.
        for(const auto& copy:accepted)if(copy.size()==size&&!std::memcmp(copy.data(),p,size))return true;
        uint64_t hash=14695981039346656037ull;for(uint64_t i=0;i<size;++i)hash=(hash^p[i])*1099511628211ull;
        for(unsigned i=0;i<3;++i)if(hash==source_hashes[i]){accepted[i].assign(p,p+size);return true;}
        return false;
    }
public:
    void clear(){states.clear();}
    const uint8_t* palette(bool validated,Block20Owner owner,const std::array<float,3>& root,
        const uint8_t* bytes,uint64_t size,const uint8_t* vertices,uint64_t source_bytes,uint32_t count,uint32_t stride,
        uint64_t frame,uint64_t clock) {
        if(!(clock&0x80000000u)){clear();return bytes;}
        if(!bytes||size!=960||!validated||!source(vertices,source_bytes,count,stride))return bytes;
        for(unsigned i=0;i<240;++i){float x;std::memcpy(&x,bytes+4*i,4);if(!std::isfinite(x))return bytes;}
        auto* s=target(validated,owner,root,frame,clock);if(!s||s->target==root)return bytes;
        std::memcpy(s->palette.data(),bytes,960);
        for(unsigned k=0;k<3;++k){double delta=double(s->target[k])-root[k];for(unsigned j=0;j<15;++j){unsigned w=j*16+k*4+3;s->palette[w]=float(double(s->palette[w])+delta);}}
        return reinterpret_cast<const uint8_t*>(s->palette.data());
    }
    const uint8_t* rigid(bool validated,Block20Owner owner,const std::array<float,3>& root,
        const uint8_t* bytes,uint64_t size,uint64_t geometry,const uint8_t* vertices,uint64_t vertex_bytes,uint32_t stride,
        uint64_t frame,uint64_t clock,bool* handled=nullptr) {
        if(handled)*handled=false;
        if(!(clock&0x80000000u)){clear();return bytes;}
        if(!bytes||size!=64||!validated||!((geometry==0x31dd00dfe7224b86ull&&vertex_bytes==4032&&stride==36)||
             (geometry==0x9df9cb6f88ee3d2bull&&vertex_bytes==600&&stride==24)))return bytes;
        const unsigned profile=stride==36?0:1;
        if(!vertices)return bytes;
        auto& cache=rigid_sources[profile];
        if(cache.size()!=vertex_bytes||std::memcmp(cache.data(),vertices,vertex_bytes)){
            uint64_t hash=14695981039346656037ull;
            for(uint64_t i=0;i<vertex_bytes;++i)hash=(hash^vertices[i])*1099511628211ull;
            if(hash!=(profile==0?0xab88aa637f9cc810ull:0x2af1861a82cd3003ull))return bytes;
            cache.assign(vertices,vertices+vertex_bytes);
        }
        std::array<float,16> raw;std::memcpy(raw.data(),bytes,64);for(float x:raw)if(!std::isfinite(x))return bytes;
        if(std::fabs(raw[12])>1e-5f||std::fabs(raw[13])>1e-5f||std::fabs(raw[14])>1e-5f||std::fabs(raw[15]-1)>1e-5f)return bytes;
        auto* s=target(validated,owner,root,frame,clock);if(!s)return bytes;
        if(handled)*handled=true;
        if(s->target==root)return bytes;
        s->rigid=raw;for(unsigned k=0;k<3;++k)s->rigid[k*4+3]=float(double(raw[k*4+3])+double(s->target[k])-root[k]);
        return reinterpret_cast<const uint8_t*>(s->rigid.data());
    }
};
// Defined once in the new host object, shared by C3 and CPU/GPU palette routes.
Root9Presentation& root9_presentation();
}
