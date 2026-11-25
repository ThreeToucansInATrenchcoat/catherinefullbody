#pragma once
#include "nvn_vk.h"
#include <cstdlib>
#include <cstring>
#include <type_traits>
namespace nx::nvn::producer_targets {
constexpr uint32_t tag=0x31544754;
struct Payload { TargetViews original; Targets prepared; };
static_assert(std::is_trivially_copyable_v<Payload>);
inline int mode(){static const int m=[] {const char* p=std::getenv("CATHERINE_VULKAN_PRODUCER_TARGETS");return p?std::atoi(p):1;}();return m;}
inline Targets legacy(const Cmd& c,const void* b,size_t size){
 Targets t{};t.color_count=c.n;for(unsigned i=0;i<8;++i)t.colors[i]=i<c.n?c.a[i]:0;t.depth=c.a[8];
 if(b&&size==sizeof(TargetViews))std::memcpy(&t.views,b,sizeof t.views);return t;
}
// Resource identities are captured now. Device views and lifetime retention remain on the GPU owner.
inline void attach(Cmd& c,const void*& blob,size_t& size,Payload& p){
 if(!mode()||c.op!=Cmd::SetRenderTargets||!blob||size!=sizeof(TargetViews)||c.n>8)return;
 std::memcpy(&p.original,blob,sizeof p.original);p.prepared=legacy(c,blob,size);blob=&p;size=sizeof p;c.mask=tag;
}
inline bool read(const Cmd& c,const CmdList& list,Targets& result){
 if(!mode()||c.op!=Cmd::SetRenderTargets||c.mask!=tag||c.blob>=list.blobs.size())return false;
 const auto& b=list.blobs[c.blob];if(b.size()!=sizeof(Payload))return false;
 Payload p;std::memcpy(&p,b.data(),sizeof p);if(p.prepared.color_count>8)return false;
 if(mode()==2){const auto old=legacy(c,&p.original,sizeof p.original);if(!targets_same(old,p.prepared))fatal("Producer native render targets differ");static uint64_t checks=0;if(++checks==1||checks%10000==0)log("Producer native render targets: %llu compared",(unsigned long long)checks);}
 result=p.prepared;return true;
}
}
