#pragma once
#include <vector>
#include <type_traits>
#include <cstdlib>
#include <cstdio>
#include "C:/catherine/native-gpu-experiment/Source/native_byte_equal.h"
namespace experimental_replay {
inline int layout_mode(){static const int mode=[] {const auto* e=std::getenv("CATHERINE_NATIVE_REPLAY_LAYOUT");return e?std::atoi(e):0;}();return mode;}
template<class Value>bool duplicate_layout(const std::vector<Value>& current,const void* bytes,uint32_t count,unsigned kind){
 static_assert(std::is_trivially_copyable_v<Value>);
 const int mode=layout_mode();if(!mode)return false;
 const bool same=count<=32&&current.size()==count&&experimental_bytes::native(current.data(),bytes,size_t(count)*sizeof(Value));
 if(mode==2){
  static uint64_t queries=0,hits=0;++queries;hits+=same;
  if(queries==1||queries%100000==0)std::fprintf(stderr,"Native replay layouts: kind %u queries %llu duplicates %llu\n",kind,(unsigned long long)queries,(unsigned long long)hits);
  return false; // Independent live-state census retains the original assignment.
 }
 return mode==1&&same;
}
}
