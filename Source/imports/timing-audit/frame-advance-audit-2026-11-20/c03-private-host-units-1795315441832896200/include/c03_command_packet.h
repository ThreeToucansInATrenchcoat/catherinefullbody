#pragma once
#include "c03_native_api.h"
#include "root9_replay_scope.h"
#include <cstring>
#include <type_traits>
namespace nx::nvn {
inline constexpr uint32_t c03_uniform_magic=0x43303352u;
static_assert(sizeof(CatherineC03RigidPacket)==160 && std::is_trivially_copyable_v<CatherineC03RigidPacket>);
inline bool c03_packet_valid(const CatherineC03RigidPacket& p,uint64_t address,uint64_t size){
 return p.actor&&p.component&&p.model&&p.sdk&&p.mesh&&p.node&&p.association&&p.continuity&&p.serial&&
 p.slot<3&&p.version==1&&p.actor<=UINT64_MAX-16&&p.component==p.actor+16&&address&&address<=UINT64_MAX-64&&p.address==address&&p.size==size&&size==64;
}
inline bool c03_capture_uniform(uint64_t command,Cmd& cmd,CatherineC03RigidPacket& out){
 out={};if(cmd.op!=Cmd::BindUniformBuffer||cmd.i[0]!=0||cmd.i[1]!=0)return false;
 CatherineC03RigidPacket p{};
 // Every actual C3 bind consumes native pending permission, including foreign/C02 replacements.
 const bool owned=catherine_c03_c3_snapshot(command,cmd.mask?0:cmd.a[0],cmd.mask?0:cmd.a[1],&p)!=0;
 if(cmd.mask||cmd.a[1]!=64||!owned||!c03_packet_valid(p,cmd.a[0],cmd.a[1]))return false;
 out=p;cmd.mask=c03_uniform_magic;return true;
}
struct C03UniformState {CatherineC03RigidPacket packet{};uint64_t invocation{};bool valid{};};
inline C03UniformState& c03_uniform_state(){static thread_local C03UniformState value;return value;}
inline void c03_apply_uniform(int stage,int slot,uint64_t address,uint64_t size,const uint8_t* blob){
 if(stage||slot)return;auto& s=c03_uniform_state();s={};if(!blob)return;
 CatherineC03RigidPacket p;std::memcpy(&p,blob,sizeof p);const uint64_t cookie=catherine_root9_replay_cookie();
 if(!cookie||!c03_packet_valid(p,address,size))return;s.packet=p;s.invocation=cookie;s.valid=true;
}
inline bool c03_uniform_matches(int stage,int slot,uint64_t address,const uint8_t* bytes,uint64_t size){
 const auto& s=c03_uniform_state();return stage==0&&slot==0&&s.valid&&s.invocation&&s.invocation==catherine_root9_replay_cookie()&&bytes&&size==64&&s.packet.address==address&&!std::memcmp(s.packet.words,bytes,64);
}
inline void c03_invalidate_uniform(uint64_t address,uint64_t size){
 auto& s=c03_uniform_state();if(!s.valid||!size)return;if(size>UINT64_MAX-address){s={};return;}const uint64_t b=s.packet.address;
 if(b<=address?address-b<64:b-address<size)s={};
}
}
