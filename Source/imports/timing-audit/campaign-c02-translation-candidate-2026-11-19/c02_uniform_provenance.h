#pragma once
#include <atomic>
#include <cstdint>
#include <cstdlib>

// Private replay sidecar. Ownership comes only from the immutable bind command,
// never from a live actor, geometry hash, address registry or matrix value match.
namespace c02_uniform_provenance {
inline constexpr uint32_t command_magic=0xc0289001u;
struct OwnedRange {uint64_t address=0,size=0;bool owned=false;};
inline thread_local OwnedRange current;
inline bool qa() {
    static thread_local const bool enabled=[] {const char* p=std::getenv("CATHERINE_C02_TRANSLATION_QA");return p&&p[0]=='1'&&!p[1];}();
    return enabled;
}
inline std::atomic<uint64_t> records{},applied{},bypassed{},cleared{},deferred_cleared{},update_cleared{};
inline void observed(std::atomic<uint64_t>& counter,const char* label) {
    if(!qa())return;
    const uint64_t n=counter.fetch_add(1,std::memory_order_relaxed)+1;
    if(n==1||n%10000==0)nx::log("C02 C3 %s: %llu",label,(unsigned long long)n);
}
inline void record(bool owned) {observed(owned?records:cleared,owned?"owned-record":"unqualified-record");}
inline void apply(int stage,int slot,uint64_t address,uint64_t size,uint32_t mask) {
    if(stage!=0||slot!=0)return; // NVN slot n maps shader bank c[3+n].
    const bool owned=mask==command_magic&&address&&size==64;
    current={address,size,owned};
    observed(owned?applied:cleared,owned?"owned-replay":"unqualified-replay");
}
inline void deferred(int stage,int slot) {
    if(stage!=0||slot!=0)return;
    current={};observed(deferred_cleared,"deferred-clear");
}
inline void update(uint64_t address,uint64_t size) {
    if(!current.owned||!size)return;
    const auto end=[](uint64_t a,uint64_t n){return n>UINT64_MAX-a?UINT64_MAX:a+n;};
    if(address<end(current.address,current.size)&&current.address<end(address,size)) {
        current={};observed(update_cleared,"update-clear");
    }
}
inline bool matches(int stage,int slot,uint64_t address,uint64_t size) {
    const bool yes=stage==0&&slot==0&&current.owned&&size==64&&
        current.address==address&&current.size==size;
    return yes;
}
inline void bypass() {observed(bypassed,"generic-bypass");}
}
