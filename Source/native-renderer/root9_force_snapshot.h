#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace experimental_root9 {
struct ForceTuple {
    uint64_t context=0;
    bool ever_owned=false,last_owned=false;
    bool operator==(const ForceTuple&)const=default;
};
struct ForceCommand {uint64_t command=0;bool known=false;};
struct ForceObservation {ForceTuple tuple;ForceCommand guest;uint64_t sequence=0;};
inline bool force_result(const ForceObservation& o,bool local_owned){
    // A false ever_owned tuple is deliberately handled by the locked original.
    return local_owned||!o.guest.known||o.guest.command!=o.tuple.context||o.tuple.last_owned;
}

// One writer, serialized by bridge_mutex. Readers never inspect the writer's
// ordinary fields. The guest reads are part of the validated interval, so an
// old command cannot be combined with a newer tuple. SC operations make both
// the publication proof and the first implementation intentionally explicit.
class ForceSnapshot {
    std::atomic<uint64_t> sequence{0},context{0};
    std::atomic<uint32_t> flags{0};
    ForceTuple last;
public:
    explicit ForceSnapshot(uint64_t initial_sequence=0):sequence(initial_sequence){}
    uint64_t begin_change(const ForceTuple& next){
        if(next==last)return 0;
        const auto before=sequence.load();
        if(before>=UINT64_MAX-1){sequence.store(UINT64_MAX);return 0;}
        const auto odd=before+1;sequence.store(odd);return odd;
    }
    void commit(uint64_t token,const ForceTuple& next){
        if(!token)return;
        context.store(next.context);
        flags.store(uint32_t(next.ever_owned)|(uint32_t(next.last_owned)<<1));
        last=next;sequence.store(token+1);
    }
    template<class ReadGuest>bool observe(ForceObservation& out,ReadGuest&& read_guest)const{
        for(unsigned attempt=0;attempt<2;++attempt){
            const auto before=sequence.load();if(before&1)continue;
            ForceObservation value;value.sequence=before;value.tuple.context=context.load();
            const auto f=flags.load();value.tuple.ever_owned=(f&1)!=0;value.tuple.last_owned=(f&2)!=0;
            if(!value.tuple.ever_owned)return false;
            value.guest=read_guest();
            if(sequence.load()==before){out=value;return true;}
        }
        return false;
    }
};
}
