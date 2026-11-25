#pragma once
#include "root15_family.h"
// Include after the caller's selected block20_command_packet.h. Some legacy
// pose headers resolve a sibling copy of that unchanged 192-byte ABI.
#include "root15_palette_transport.h"

namespace nx::nvn {
inline constexpr uint32_t root15_command_magic=0x52313550u;
struct Root15CommandPacket {Block20CommandPacket command{};CatherineBlock15Packet produced{};};
inline bool root15_capture_command(const Cmd& cmd,Root15CommandPacket& out){
    if(cmd.op!=Cmd::BindStorageBuffer||cmd.i[0]!=5||cmd.i[1]!=0||cmd.a[1]!=960)return false;
    CatherineBlock15Packet p{};
    if(!catherine_block15_job_snapshot(&p)||!p.owner.valid||!catherine::root15::family(p.owner)||!p.owner.lifetime||!p.serial||p.owner.palette_address!=cmd.a[0])return false;
    out.command.owner=p.owner;out.command.address=cmd.a[0];out.command.size=960;out.produced=p;return true;
}
struct Root15ReplayPacket {Root15CommandPacket packet{};bool valid=false;};
inline Root15ReplayPacket& root15_replay_packet(){static thread_local Root15ReplayPacket value;return value;}
inline void root15_apply_command(int stage,int index,uint64_t address,uint64_t size,const uint8_t* blob){
    if(stage!=5||index!=0)return;
    auto& current=root15_replay_packet();current={};
    if(!blob||size!=960)return;
    Root15CommandPacket packet;std::memcpy(&packet,blob,sizeof packet);
    if(packet.command.address!=address||packet.command.size!=size||!packet.produced.serial||
       !catherine::root15::family(packet.produced.owner)||!packet.produced.owner.valid||!packet.produced.owner.lifetime||
       packet.produced.owner.palette_address!=address||std::memcmp(&packet.command.owner,&packet.produced.owner,sizeof(CatherineBlock20Packet)))return;
    current.packet=packet;current.valid=true;
}
inline bool root15_bound_matches(uint64_t address,const uint8_t* palette,uint64_t size){
    const auto& current=root15_replay_packet();return current.valid&&current.packet.command.address==address&&
        catherine::root15::matches(current.packet.produced,address,palette,size);
}
inline const CatherineBlock20Packet* root15_bound_owner(uint64_t address,uint64_t size){
    const auto& current=root15_replay_packet();return current.valid&&size==960&&current.packet.command.address==address?&current.packet.produced.owner:nullptr;
}
inline void root15_invalidate_command(uint64_t address,uint64_t size){
    auto& current=root15_replay_packet();if(!current.valid||!size)return;
    const uint64_t bound=current.packet.command.address;
    if(bound<=address?address-bound<960:bound-address<size)current={};
}
}
