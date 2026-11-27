#pragma once
#include <cstdint>
#include <memory>
#include <limits>
#include <utility>
namespace experimental_record {
template<class State> class Scope {
    std::shared_ptr<State> cached;
    uint64_t guest=0,generation=0,mark=0,epoch=1;
    uint32_t depth=0;
    bool valid=false,held=false,disabled=false;
    void unlock(){if(held){cached->lock.unlock();held=false;}}
public:
    struct Stats {uint64_t begins=0,records=0,hits=0,locks=0,cancels=0,switches=0;} stats;
    Scope()=default;Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    ~Scope(){unlock();}
    bool active()const{return depth!=0;}
    uint64_t begin(){if(disabled)return 0;if(depth>=32){abandon();return 0;}++depth;++stats.begins;return epoch;}
    void end(uint64_t token){if(!token||token!=epoch||!depth)return;if(!--depth)unlock();}
    void abandon(){unlock();depth=0;++stats.cancels;if(epoch==UINT64_MAX)disabled=true;else ++epoch;}
    template<class Resolve>State* acquire(uint64_t g,uint64_t gen,uint64_t m,Resolve resolve){
        if(!active())return nullptr;
        ++stats.records;
        if(!valid||g!=guest||gen!=generation||m!=mark){
            // No registry lock is acquired while a prior command-state lock is held.
            if(held)++stats.switches;
            unlock();
            // Resolve MUST copy the strong reference under the registry read lock.
            auto next=resolve(g,m);
            cached=std::move(next);guest=g;generation=gen;mark=m;valid=true;
        }else ++stats.hits;
        if(cached&&!held){cached->lock.lock();held=true;++stats.locks;}
        return cached.get();
    }
};
}
