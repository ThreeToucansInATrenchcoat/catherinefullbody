#pragma once
#include "root9_native_api.h"
#include <cstring>
namespace nx::nvn {
inline constexpr uint32_t root9_uniform_magic=0x52394333u;
inline bool root9_capture_uniform(uint64_t command,const Cmd& cmd,catherine::root9::RigidPacket& out){
    if(cmd.op!=Cmd::BindUniformBuffer||cmd.i[0]!=0||cmd.i[1]!=0)return false;
    // Previously established C02/C04 already-corrected provenance wins. Clear
    // this context's Root9 recording state even when no new payload is emitted.
    if(cmd.mask){catherine::root9::RigidPacket unowned;catherine_root9_c3_snapshot(command,0,0,&unowned);return false;}
    return catherine_root9_c3_snapshot(command,cmd.a[0],cmd.a[1],&out)!=0;
}
struct Root9UniformState {catherine::root9::RigidPacket packet{};uint64_t invocation=0;bool valid=false;};
inline Root9UniformState& root9_uniform_state(){static thread_local Root9UniformState value;return value;}
inline void root9_apply_uniform(int stage,int slot,uint64_t address,uint64_t size,const uint8_t* blob){
    if(stage!=0||slot!=0)return;auto& current=root9_uniform_state();current={};
    if(!blob||size!=64)return;catherine::root9::RigidPacket packet;std::memcpy(&packet,blob,sizeof packet);
    if(!packet.owner.valid||packet.owner.type!=9||!packet.owner.lifetime||!packet.serial||!packet.mesh||packet.address!=address||packet.size!=size)return;
    const uint64_t invocation=catherine_root9_replay_cookie();if(!invocation)return;
    current.packet=packet;current.invocation=invocation;current.valid=true;
}
inline const catherine::root9::RigidPacket* root9_uniform_owner(uint64_t address,const uint8_t* bytes,uint64_t size){
    const auto& current=root9_uniform_state();return current.valid&&current.invocation&&current.invocation==catherine_root9_replay_cookie()&&bytes&&size==64&&current.packet.address==address&&
        !std::memcmp(current.packet.words.data(),bytes,64)?&current.packet:nullptr;
}
inline void root9_invalidate_uniform(uint64_t address,uint64_t size){
    auto& current=root9_uniform_state();if(!current.valid||!size)return;const uint64_t bound=current.packet.address;
    if(bound<=address?address-bound<64:bound-address<size)current={};
}
}
