#pragma once
#include "block20_owner_bridge.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>

namespace catherine::root9 {
// New payload; never reinterpret the existing already-corrected C02 marker.
struct RigidPacket {
    CatherineBlock20Packet owner{};
    uint64_t mesh=0,serial=0,address=0,size=0;
    std::array<uint32_t,16> words{}; // Uploaded row-major bytes, still native.
};
// All callers hold their bridge mutex. Reader and live-owner predicates belong
// to the producer, never to replay. No replay dereference of guest pointers.
class RigidTransport {
    struct Copy {RigidPacket packet{};uint64_t cpu=0;};
    struct Upload {RigidPacket packet{};uint64_t cpu=0,backing=0,offset=0,base=0;bool owned=false;};
    std::map<uint64_t,Copy> copies;
    std::map<uint64_t,std::vector<Upload>> pending;
    // Only owned pending writes can carry provenance worth revoking. Avoid
    // scanning thousands of unrelated unowned C3 uploads on every new write.
    std::map<uint64_t,std::vector<size_t>> owned_pending;
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
    void clear(){copies.clear();pending.clear();owned_pending.clear();clear_tags();if(!++epoch)++epoch;}
    void invalidate(uint64_t address,uint64_t size){
        erase_overlaps(copies,address,size);erase_gpu_overlaps(address,size);
        erase_index_overlaps(cpu_tags,address,size);
        // Unresolved offsets have no authenticated GPU base. Do not let a
        // queued packet resurrect provenance after an update in either domain.
        pending.clear();owned_pending.clear();if(!++epoch)++epoch;
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
        if(!owner||!owner->valid||owner->type!=9||!owner->lifetime||!mesh||!destination)return false;
        Copy copy;copy.cpu=destination;copy.packet.owner=*owner;copy.packet.mesh=mesh;
        if(!read(destination,copy.packet.words.data(),64))return false;
        if(copies.size()>=limit)copies.clear();
        if(!++serial)++serial;copy.packet.serial=serial;copies[destination]=copy;return true;
    }
    template<class Read,class Live>void loaded(Thread& t,uint64_t source,Read read,Live live){
        t.cursor={};t.latest_upload={};t.upload_owned=false;
        auto i=copies.find(source);std::array<uint32_t,16> actual{};
        if(i==copies.end()||!live(i->second.packet.owner)||!read(source,actual.data(),64)||actual!=i->second.packet.words)return;
        t.cursor={i->second,epoch,true};
    }
    template<class Read,class Live>void uploaded(Thread& t,uint64_t cpu,uint64_t backing,uint64_t offset,Read read,Live live){
        t.upload_owned=false;t.latest_upload={};Upload row;row.cpu=cpu;row.backing=backing;row.offset=offset;
        if(!cpu||!backing||offset>UINT64_MAX-64){clear();t={};return;}
        auto current=copies.find(t.cursor.copy.cpu);
        if(t.cursor.valid&&t.cursor.epoch==epoch&&current!=copies.end()&&current->second.packet.serial==t.cursor.copy.packet.serial&&live(t.cursor.copy.packet.owner)){
            row.packet=t.cursor.copy.packet;std::array<uint32_t,16> expected{},actual{};
            for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)expected[4*r+c]=row.packet.words[4*c+r];
            if(read(cpu,actual.data(),64)&&actual==expected){row.packet.words=actual;row.owned=true;}
        }
        // Even identical unowned writes invalidate earlier provenance. Resolve
        // every upload in order when the backing GPU base becomes available.
        // Revoke before queuing, including identical unowned replacements and
        // overlapping offsets, so another recording thread cannot use old tags.
        erase_upload_overlaps(cpu,backing,offset);
        for(auto i=owned_pending.begin();i!=owned_pending.end();){
            auto p=pending.find(i->first);if(p==pending.end()){i=owned_pending.erase(i);continue;}
            auto& indices=i->second;auto& older=p->second;
            for(auto k=indices.begin();k!=indices.end();){
                if(*k>=older.size()||!older[*k].owned){k=indices.erase(k);continue;}
                auto& item=older[*k];
                if(overlap(item.cpu,64,cpu,64)||(i->first==backing&&overlap(item.offset,64,offset,64))){item.owned=false;k=indices.erase(k);}else ++k;
            }
            if(indices.empty())i=owned_pending.erase(i);else ++i;
        }
        if(pending.size()>=limit){pending.clear();owned_pending.clear();clear_tags();if(!++epoch)++epoch;}
        auto existing=pending.find(backing);
        if(existing!=pending.end()&&existing->second.size()>=limit){pending.clear();owned_pending.clear();clear_tags();if(!++epoch)++epoch;}
        auto& rows=pending[backing];
        rows.push_back(row);if(row.owned)owned_pending[backing].push_back(rows.size()-1);
        t.upload_owned=row.owned;t.latest_upload=row.packet;t.cursor={};
    }
    template<class Read,class Live>void buffer_address(Thread& t,uint64_t backing,uint64_t base,Read read,Live live){
        t.address={backing,base,epoch,backing&&base};auto i=pending.find(backing);if(i==pending.end())return;
        for(auto& row:i->second){
            if(!base||row.offset>UINT64_MAX-base||base+row.offset>UINT64_MAX-64){clear_tags();continue;}
            const uint64_t gpu=base+row.offset;erase_gpu_overlaps(gpu,64);
            std::array<uint32_t,16> actual{};
            if(!row.owned||!live(row.packet.owner)||!read(row.cpu,actual.data(),64)||actual!=row.packet.words)continue;
            if(tags.size()>=limit)clear_tags();row.base=base;row.packet.address=gpu;row.packet.size=64;insert_tag(gpu,row);
        }
        pending.erase(i);owned_pending.erase(backing);
    }
    template<class Read,class Live>bool snapshot(const Thread& t,uint64_t gpu,uint64_t size,RigidPacket& out,Read read,Live live){
        out={};if(size!=64||!t.address.valid||t.address.epoch!=epoch)return false;auto i=tags.find(gpu);std::array<uint32_t,16> actual{};
        if(i==tags.end()||i->second.backing!=t.address.backing||i->second.base!=t.address.base||
           !live(i->second.packet.owner)||!read(i->second.cpu,actual.data(),64)||actual!=i->second.packet.words)return false;
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
