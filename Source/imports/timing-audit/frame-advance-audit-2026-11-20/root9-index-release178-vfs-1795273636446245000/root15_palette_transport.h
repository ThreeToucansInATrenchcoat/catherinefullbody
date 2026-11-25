#pragma once
#include "block20_owner_bridge.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <map>

// A distinct packet leaves CatherineBlock20Packet's public layout unchanged.
struct CatherineBlock15Packet {
    CatherineBlock20Packet owner{};
    uint64_t serial=0,cpu=0;
    std::array<uint8_t,960> bytes{};
};
extern "C" void catherine_block15_palette_copied(uint64_t,uint32_t,uint64_t,uint32_t);
extern "C" int catherine_block15_job_snapshot(CatherineBlock15Packet*);

namespace catherine::root15 {
class PaletteTransport {
    std::map<uint64_t,CatherineBlock15Packet> copies;
    uint64_t serial=0;
public:
    void clear(){copies.clear();}
    void retire(uint64_t model){for(auto i=copies.begin();i!=copies.end();)if(i->second.owner.model==model)i=copies.erase(i);else ++i;}
    void invalidate_entry(uint64_t entry){copies.erase(entry);}
    void invalidate_cpu(uint64_t address,uint64_t size){
        if(!size)return;
        for(auto i=copies.begin();i!=copies.end();){uint64_t a=i->second.cpu;
            if(a<=address?address-a<960:a-address<size)i=copies.erase(i);else ++i;
        }
    }
    // Both functions execute under the existing native owner-registry lock.
    // Read addresses refer to completed native CPU output, never to GPU values.
    template<class Read>bool copied(const CatherineBlock20Packet* owner,uint64_t entry,uint64_t cpu,Read read){
        copies.erase(entry);invalidate_cpu(cpu,960);
        if(!owner||!owner->valid||!owner->lifetime||(owner->type!=9&&owner->type!=12)||owner->entry!=entry||!cpu)return false;
        CatherineBlock15Packet p;p.owner=*owner;p.cpu=cpu;
        if(!read(cpu,p.bytes.data(),p.bytes.size()))return false;
        if(!++serial)++serial;p.serial=serial;
        if(copies.size()>=6144)copies.clear();copies[entry]=p;return true;
    }
    template<class Live>bool job(uint64_t entry,uint64_t entry_value,uint64_t palette_gpu,uint64_t current_cpu,uint64_t clock,CatherineBlock15Packet& out,Live live){
        out={};auto found=copies.find(entry);if(found==copies.end())return false;const auto& p=found->second;
        if(!palette_gpu||p.cpu!=current_cpu||p.owner.entry_value!=entry_value||!live(p.owner))return false;
        // Preserve native low-rate/60Hz compatibility and reject stale queued
        // lifetimes, including clock resets. No nearest-pose/address inference.
        uint32_t produced=uint32_t(p.owner.clock>>32),now=uint32_t(clock>>32);
        if(!(p.owner.clock&0x80000000u)||!(clock&0x80000000u)||now<produced||now-produced>2)return false;
        // Never reread a saved CPU pointer here. Its mapping may have retired.
        // The live native mapping and slot identify it; actual consumed bytes
        // are compared with the immutable snapshot at replay below.
        out=p;out.owner.palette_address=palette_gpu;return true;
    }
};
// Replay compares the actual palette against the saved production bytes before
// applying any root delta. A matching address by itself never establishes it.
inline bool matches(const CatherineBlock15Packet& p,uint64_t gpu,const uint8_t* bytes,uint64_t size){
    return p.owner.valid&&p.owner.lifetime&&p.serial&&p.owner.palette_address==gpu&&size==960&&bytes&&
           (p.owner.type==9||p.owner.type==12)&&!std::memcmp(p.bytes.data(),bytes,960);
}
}
