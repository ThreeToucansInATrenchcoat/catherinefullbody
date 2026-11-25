#pragma once
#include "root15_family.h"
#include "block20_owner_bridge.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace catherine::root9 {
// New payload; never reinterpret the existing already-corrected C02 marker.
struct BackgroundOwner {
    uint64_t identity=0,mesh=0,parent=0,resource=0,storage=0,roster=0,rows=0,clock=0;
    uint32_t count=0;
};
struct RigidPacket {
    BackgroundOwner background{};
    CatherineBlock20Packet owner{};
    uint64_t mesh=0,serial=0,address=0,size=0;
    std::array<uint32_t,16> words{}; // Uploaded row-major bytes, still native.
};
inline bool valid_rigid_packet(const RigidPacket& p) {
    return (p.background.identity && p.background.mesh==p.mesh && p.background.parent &&
            p.background.resource && p.background.storage && p.background.roster && p.background.rows &&
            p.background.count && p.background.count<=64) ||
           (p.owner.valid && catherine::root15::rigid(p.owner) && p.owner.lifetime);
}
// All callers hold their bridge mutex. Reader and live-owner predicates belong
// to the producer, never to replay. No replay dereference of guest pointers.
class RigidTransport {
    struct Copy {RigidPacket packet{};uint64_t cpu=0;};
    struct Upload {RigidPacket packet{};uint64_t cpu=0,backing=0,offset=0,base=0;bool owned=false;};
    std::map<uint64_t,Copy> copies;
    using Pending = std::map<uint64_t,std::vector<Upload>>;
    Pending pending;
    // Empty nodes contain no ownership evidence. Reuse their allocation across
    // successive C3 uploads instead of allocating/freeing it for every draw.
    std::vector<Pending::node_type> spare_pending;
    // Only owned pending writes can carry provenance worth revoking. Avoid
    // scanning thousands of unrelated unowned C3 uploads on every new write.
    std::map<uint64_t,std::vector<size_t>> owned_pending;
    // Index pending authenticated writes by their actual overlap domains. A
    // new C3 upload must not scan every unrelated background instance.
    struct PendingRef {uint64_t backing=0;size_t index=0;};
    std::multimap<uint64_t,PendingRef> pending_cpu;
    std::map<uint64_t,std::multimap<uint64_t,size_t>> pending_offsets;
    void revoke_pending(uint64_t cpu,uint64_t backing,uint64_t offset){
        auto mark=[&](uint64_t b,size_t n){auto p=pending.find(b);
            if(p!=pending.end()&&n<p->second.size())p->second[n].owned=false;};
        auto a=pending_cpu.lower_bound(cpu>=63?cpu-63:0);
        for(;a!=pending_cpu.end();++a){if(a->first>cpu&&a->first-cpu>=64)break;mark(a->second.backing,a->second.index);}
        auto b=pending_offsets.find(backing);
        if(b!=pending_offsets.end()){
            auto i=b->second.lower_bound(offset>=63?offset-63:0);
            for(;i!=b->second.end();++i){if(i->first>offset&&i->first-offset>=64)break;mark(backing,i->second);}
        }
    }
    void remove_pending_index(uint64_t backing,const std::vector<Upload>& rows){
        auto found=owned_pending.find(backing);
        if(found!=owned_pending.end())for(auto n:found->second)if(n<rows.size()){
            auto [a,b]=pending_cpu.equal_range(rows[n].cpu);
            while(a!=b){if(a->second.backing==backing&&a->second.index==n)a=pending_cpu.erase(a);else ++a;}
        }
        pending_offsets.erase(backing);owned_pending.erase(backing);
    }
    using Tags=std::map<uint64_t,Upload>;
    Tags tags;
    std::multimap<uint64_t,uint64_t> cpu_tags;
    std::map<uint64_t,std::multimap<uint64_t,uint64_t>> backing_tags;
    void clear_tags(){tags.clear();cpu_tags.clear();backing_tags.clear();}
    Tags::iterator erase_tag(Tags::iterator i){
        const auto gpu=i->first;const auto& row=i->second;
        auto [a,b]=cpu_tags.equal_range(row.cpu);
        for(;a!=b;++a)if(a->second==gpu){cpu_tags.erase(a);break;}
        auto backing=backing_tags.find(row.backing);
        if(backing!=backing_tags.end()){
            auto [a,b]=backing->second.equal_range(row.offset);
            for(;a!=b;++a)if(a->second==gpu){backing->second.erase(a);break;}
            if(backing->second.empty())backing_tags.erase(backing);
        }
        return tags.erase(i);
    }
    void insert_tag(uint64_t gpu,const Upload& row){
        auto old=tags.find(gpu);if(old!=tags.end())erase_tag(old);
        tags.emplace(gpu,row);cpu_tags.emplace(row.cpu,gpu);
        backing_tags[row.backing].emplace(row.offset,gpu);
    }
    void erase_gpu_overlaps(uint64_t address,uint64_t size){
        if(!size)return;
        auto i=tags.lower_bound(address>=63?address-63:0);
        while(i!=tags.end()){
            if(i->first>address&&i->first-address>=size)break;
            if(overlap(i->first,64,address,size))i=erase_tag(i);else ++i;
        }
    }
    template<class Index>void erase_index_overlaps(const Index& index,uint64_t address,uint64_t size){
        if(!size)return;
        std::vector<uint64_t> remove;
        auto i=index.lower_bound(address>=63?address-63:0);
        for(;i!=index.end();++i){
            if(i->first>address&&i->first-address>=size)break;
            if(overlap(i->first,64,address,size))remove.push_back(i->second);
        }
        // Gather before deleting: erase_tag mutates both indexes and may
        // retire this backing's whole index. No borrowed iterator survives.
        for(auto gpu:remove){auto tag=tags.find(gpu);if(tag!=tags.end())erase_tag(tag);}
    }
    void erase_upload_overlaps(uint64_t cpu,uint64_t backing,uint64_t offset){
        erase_index_overlaps(cpu_tags,cpu,64);
        auto i=backing_tags.find(backing);
        if(i!=backing_tags.end())erase_index_overlaps(i->second,offset,64);
    }
    template<class Live>static bool is_live(Live live,const RigidPacket& p){
        if constexpr(requires {live(p);}) return live(p);
        else return !p.background.identity && live(p.owner);
    }
    uint64_t serial=0;
    uint64_t epoch=1;
    static constexpr size_t limit=4096;
    static bool overlap(uint64_t a,uint64_t an,uint64_t b,uint64_t bn){
        return an&&bn&&(a<=b?b-a<an:a-b<bn);
    }
    template<class Map>static void erase_overlaps(Map& map,uint64_t address,uint64_t size){
        if(!size)return;
        // Each stored copy/tag spans exactly64 bytes. Only the preceding63
        // addresses and keys inside the new write can overlap. Difference
        // comparisons preserve the original predicate near UINT64_MAX.
        auto i=map.lower_bound(address>=63?address-63:0);
        while(i!=map.end()){
            if(i->first>address&&i->first-address>=size)break;
            if(overlap(i->first,64,address,size))i=map.erase(i);else ++i;
        }
    }
public:
    struct Cursor {Copy copy{};uint64_t epoch=0;bool valid=false;};
    struct Address {uint64_t backing=0,base=0,epoch=0;bool valid=false;};
    // Per recording thread. It must be reset on EVERY load and upload.
    struct Thread {Cursor cursor{};Address address{};RigidPacket latest_upload{};bool upload_owned=false;};
    void clear(){copies.clear();pending.clear();owned_pending.clear();pending_cpu.clear();pending_offsets.clear();clear_tags();if(!++epoch)++epoch;}
    void invalidate(uint64_t address,uint64_t size){
        erase_overlaps(copies,address,size);erase_gpu_overlaps(address,size);
        erase_index_overlaps(cpu_tags,address,size);
        // Unresolved offsets have no authenticated GPU base. Do not let a
        // queued packet resurrect provenance after an update in either domain.
        pending.clear();owned_pending.clear();pending_cpu.clear();pending_offsets.clear();if(!++epoch)++epoch;
    }
    void retire(uint64_t model){
        for(auto i=copies.begin();i!=copies.end();)if(i->second.packet.owner.model==model)i=copies.erase(i);else ++i;
        for(auto i=tags.begin();i!=tags.end();)if(i->second.packet.owner.model==model)i=erase_tag(i);else ++i;
        for(auto& [backing,rows]:pending)for(auto& row:rows)if(row.packet.owner.model==model)row.owned=false;
    }
    // Caller has proved real unweighted mesh membership and native destination.
    // A null/foreign owner still invalidates a previous copy at the same address.
    template<class Read>bool copied(const CatherineBlock20Packet* owner,uint64_t mesh,uint64_t destination,Read read){
        erase_overlaps(copies,destination,64);
        if(!owner||!owner->valid||!catherine::root15::rigid(*owner)||!owner->lifetime||!mesh||!destination)return false;
        Copy copy;copy.cpu=destination;copy.packet.owner=*owner;copy.packet.mesh=mesh;
        if(!read(destination,copy.packet.words.data(),64))return false;
        if(copies.size()>=limit)copies.clear();
        if(!++serial)++serial;copy.packet.serial=serial;copies[destination]=copy;return true;
    }
    template<class Read>bool copied_background(const BackgroundOwner& owner,uint64_t destination,Read read){
        erase_overlaps(copies,destination,64);
        Copy copy;copy.cpu=destination;copy.packet.background=owner;copy.packet.mesh=owner.mesh;
        if(!valid_rigid_packet(copy.packet)||!read(destination,copy.packet.words.data(),64))return false;
        if(copies.size()>=limit)copies.clear();
        if(!++serial)++serial;copy.packet.serial=serial;copies[destination]=copy;return true;
    }
    template<class Read,class Live>void loaded(Thread& t,uint64_t source,Read read,Live live){
        t.cursor={};t.latest_upload={};t.upload_owned=false;
        auto i=copies.find(source);std::array<uint32_t,16> actual{};
        if(i==copies.end()||!is_live(live,i->second.packet)||!read(source,actual.data(),64)||actual!=i->second.packet.words)return;
        t.cursor={i->second,epoch,true};
    }
    template<class Read,class Live>void uploaded(Thread& t,uint64_t cpu,uint64_t backing,uint64_t offset,Read read,Live live){
        t.upload_owned=false;t.latest_upload={};Upload row;row.cpu=cpu;row.backing=backing;row.offset=offset;
        if(!cpu||!backing||offset>UINT64_MAX-64){clear();t={};return;}
        auto current=copies.find(t.cursor.copy.cpu);
        if(t.cursor.valid&&t.cursor.epoch==epoch&&current!=copies.end()&&current->second.packet.serial==t.cursor.copy.packet.serial&&is_live(live,t.cursor.copy.packet)){
            row.packet=t.cursor.copy.packet;std::array<uint32_t,16> expected{},actual{};
            for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)expected[4*r+c]=row.packet.words[4*c+r];
            if(read(cpu,actual.data(),64)&&actual==expected){row.packet.words=actual;row.owned=true;}
        }
        // Even identical unowned writes invalidate earlier provenance. Resolve
        // every upload in order when the backing GPU base becomes available.
        // Revoke before queuing, including identical unowned replacements and
        // overlapping offsets, so another recording thread cannot use old tags.
        erase_upload_overlaps(cpu,backing,offset);
        revoke_pending(cpu,backing,offset);
        if(pending.size()>=limit){pending.clear();owned_pending.clear();pending_cpu.clear();pending_offsets.clear();clear_tags();if(!++epoch)++epoch;}
        auto existing=pending.find(backing);
        if(existing!=pending.end()&&existing->second.size()>=limit){pending.clear();owned_pending.clear();pending_cpu.clear();pending_offsets.clear();clear_tags();if(!++epoch)++epoch;}
        auto entry=pending.find(backing);
        if(entry==pending.end()) {
            if(!spare_pending.empty()) {
                auto node=std::move(spare_pending.back());spare_pending.pop_back();
                node.key()=backing;entry=pending.insert(std::move(node)).position;
            } else entry=pending.try_emplace(backing).first;
        }
        auto& rows=entry->second;
        rows.push_back(row);if(row.owned){const size_t index=rows.size()-1;
            owned_pending[backing].push_back(index);pending_cpu.emplace(cpu,PendingRef{backing,index});
            pending_offsets[backing].emplace(offset,index);
        }
        t.upload_owned=row.owned;t.latest_upload=row.packet;t.cursor={};
    }
    template<class Read,class Live>void buffer_address(Thread& t,uint64_t backing,uint64_t base,Read read,Live live){
        t.address={backing,base,epoch,backing&&base};auto i=pending.find(backing);if(i==pending.end())return;
        for(auto& row:i->second){
            if(!base||row.offset>UINT64_MAX-base||base+row.offset>UINT64_MAX-64){clear_tags();continue;}
            const uint64_t gpu=base+row.offset;erase_gpu_overlaps(gpu,64);
            std::array<uint32_t,16> actual{};
            if(!row.owned||!is_live(live,row.packet)||!read(row.cpu,actual.data(),64)||actual!=row.packet.words)continue;
            if(tags.size()>=limit)clear_tags();row.base=base;row.packet.address=gpu;row.packet.size=64;insert_tag(gpu,row);
        }
        remove_pending_index(backing,i->second);
        if(spare_pending.size()<32 && i->second.capacity()<=256) {
            auto node=pending.extract(i);node.mapped().clear();
            spare_pending.push_back(std::move(node));
        } else pending.erase(i);
        owned_pending.erase(backing);
    }
    template<class Read,class Live>bool snapshot(const Thread& t,uint64_t gpu,uint64_t size,RigidPacket& out,Read read,Live live){
        out={};if(size!=64||!t.address.valid||t.address.epoch!=epoch)return false;auto i=tags.find(gpu);std::array<uint32_t,16> actual{};
        if(i==tags.end()||i->second.backing!=t.address.backing||i->second.base!=t.address.base||
           !is_live(live,i->second.packet)||!read(i->second.cpu,actual.data(),64)||actual!=i->second.packet.words)return false;
        out=i->second.packet;return true;
    }
    // Force a bind on a new copy even if address, owner and bytes are identical:
    // root/producer clock may have changed. An unowned replacement must clear it.
    static bool force_bind(const Thread& t,const RigidPacket* previous){
        // Conservatively refresh this narrow class on every upload; native
        // boolean/address dedup cannot authenticate retirement or root epochs.
        return t.upload_owned||previous!=nullptr;
    }
};
}
