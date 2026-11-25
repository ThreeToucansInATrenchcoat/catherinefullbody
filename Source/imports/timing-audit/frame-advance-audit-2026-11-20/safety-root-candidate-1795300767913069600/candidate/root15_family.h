#pragma once
#include "block20_owner_bridge.h"
namespace catherine::root15 {
// These are renderer families of immutable producer-validated packets.
// Native primary/subtype fields are preserved; live Safety admission is below.
inline unsigned family(uint32_t type,uint32_t subtype){
 return type==9||type==12?type:type==1&&(subtype==9||subtype==12)?subtype:0;
}
inline unsigned family(const CatherineBlock20Packet&p){return family(p.type,p.subtype);}
inline bool rigid(const CatherineBlock20Packet&p){const auto f=family(p);return f==9||(p.type==1&&f==12);}
}
