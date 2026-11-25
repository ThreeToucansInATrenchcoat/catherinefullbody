#pragma once
#include <stdint.h>
/* Guest-main-thread only, after earned payload restore and before the existing
   f294a00 continuation. No VM stack manipulation, archive write or record merge.
   The caller must validate/snapshot this scalar before starting a transaction. */
static int catherine_dev_difficulty_request_valid(int32_t request){
 return request>=-1 && request<=3;
}
/* Returns1 for keep/applied; -40 invalid scalar, -41 wrong/missing config,
   -42 alternate difficulty mode. Rejections precede all configuration writes.
   Cpu is untouched. Getter readback follows GET_DIFFICULTY's signed16 selection.
   Native SET_DIFFICULTY f778e0's sole config mutation is ST32(config+9b58).
   Native f294a00 subsequently reconciles records via9b6a0/9b6e0; do not duplicate
   that operation here, and do not mislabel those functions as field setters. */
static int catherine_dev_difficulty_apply(uint64_t restored_config,int32_t request,int32_t*readback){
 if(!catherine_dev_difficulty_request_valid(request))return -40;
 if(!restored_config)return -41;
 const uint64_t holder=LD64(0x7000cc9f60ull);
 if(!holder || LD64(holder)!=restored_config)return -41;
 const unsigned alternate=LD8(restored_config+0xce5c);
 if(request>=0 && alternate)return -42;
 if(request>=0)ST32(restored_config+0x9b58,(uint32_t)request);
 const int32_t actual=(int16_t)LD16(restored_config+(alternate?0xce60:0x9b58));
 if(readback)*readback=actual;
 return 1;
}
