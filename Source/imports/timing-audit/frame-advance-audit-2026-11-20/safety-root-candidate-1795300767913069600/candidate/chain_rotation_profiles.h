#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
namespace catherine::visual {
// These immutable chain assemblies have nine repeated sectors around local Z.
// Full vertex fingerprints prevent applying their 40-degree loop to other art.
inline unsigned chain_rotation_sectors(uint64_t geometry,const uint8_t* vertices,size_t bytes,uint32_t stride){
    struct Profile {uint64_t geometry;size_t bytes;uint64_t fingerprint;};
    static constexpr Profile profiles[]={
        {0x5d85a105c9d1820full,279936,0x3cdb7d4d416eaafdull},
        {0x74b7033ab5e79527ull,77760,0xf2eae36d4d0c8e84ull},
        {0xb423bd2d0d6de2e9ull,508032,0xa3e926e6753d7572ull},
        {0xdde09f70b0bd2816ull,279936,0x9b9d3bc7c7ee5b5cull}
    };
    if(!vertices||stride!=36)return 1;
    struct Cache {std::unordered_map<const uint8_t*,bool> sources;};
    static std::array<Cache,4> cache{};
    for(unsigned i=0;i<4;++i){const auto& p=profiles[i];
        if(geometry!=p.geometry||bytes!=p.bytes)continue;
        auto& c=cache[i];if(auto found=c.sources.find(vertices);found!=c.sources.end())return found->second?9:1;
        uint64_t hash=14695981039346656037ull;
        for(size_t n=0;n<bytes;++n)hash=(hash^vertices[n])*1099511628211ull;
        const bool valid=hash==p.fingerprint;
        // Different instances may bind distinct immutable copies of one mesh.
        // Retain their admissions together instead of rehashing on every draw.
        if(c.sources.size()>=64)c.sources.clear();c.sources.emplace(vertices,valid);return valid?9:1;
    }
    return 1;
}
}
