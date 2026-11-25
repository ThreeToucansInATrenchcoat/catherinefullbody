#pragma once
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
namespace catherine::visual {
// Opt-in owned-process diagnosis. Fixed records; no synchronous text logging.
inline void trace_rigid_motion(uint64_t route,uint64_t frame,uint64_t clock,uint64_t geometry,
                               uint64_t identity,uint64_t native_mesh,const void* raw,const void* rendered){
    static const char* path=std::getenv("CATHERINE_TRACE_RIGID_MOTION");
    if(!path||!raw||!rendered)return;
    if(geometry==0x6e047bf4182d1f95ull||geometry==0xdd998f7e9c594a11ull)return;
    static FILE* file=std::fopen(path,"wb");if(!file)return;
    struct Key {uint64_t route,geometry,identity;bool operator==(const Key&)const=default;};
    struct Hash {size_t operator()(const Key& k)const{return k.route^(k.geometry*0x9e3779b97f4a7c15ull)^(k.identity<<7);}};
    static std::unordered_map<Key,uint64_t,Hash> seen;static unsigned count=0;
    if(count>=2000000)return;
    auto [i,fresh]=seen.try_emplace(Key{route,geometry,identity},~0ull);if(i->second==frame)return;i->second=frame;
    struct Record {uint64_t magic,route,frame,clock,geometry,identity,native_mesh;float raw[16],out[16];};
    static_assert(sizeof(Record)==184);
    Record row{0x314e4f49544f4d52ull,route,frame,clock,geometry,identity,native_mesh,{},{}};
    std::memcpy(row.raw,raw,64);std::memcpy(row.out,rendered,64);std::fwrite(&row,sizeof row,1,file);++count;
}
}
