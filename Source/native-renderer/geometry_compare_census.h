#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "C:/catherine/native-gpu-experiment/Source/phase_timer.h"
namespace experimental_geometry {
inline uint64_t calls[2]{},bytes[2]{};
inline bool profile(){static const bool on=std::getenv("CATHERINE_GEOMETRY_COMPARE_PROFILE")!=nullptr;return on;}
inline int compare(unsigned kind,const void* a,const void* b,size_t n){
 if(!profile())return std::memcmp(a,b,n);
 ++calls[kind];bytes[kind]+=n;
 perf22::Scope timing(kind?perf22::Geo12Compare:perf22::Geo9Compare);
 return std::memcmp(a,b,n);
}
inline void report(){if(profile())std::fprintf(stderr,"Geometry compare census: root9 %llu calls %llu bytes; root12 %llu calls %llu bytes\n",(unsigned long long)calls[0],(unsigned long long)bytes[0],(unsigned long long)calls[1],(unsigned long long)bytes[1]);}
}
