#pragma once
#include <stdint.h>
/* DEV-only guest-main-thread helper; invoke after native saved payload restore.
   No save-file edit, host-thread guest call, route toggle, or Rin-counter write. */
void f_700009bed0(Cpu*);void f_700009ac00(Cpu*);void f_700009ac10(Cpu*);
static int catherine_dev_karma_request_valid(int32_t enabled,int32_t value){
 return (enabled==0 || enabled==1) && (!enabled || (value>=0 && value<=255));
}
/* Returns1 on keep/applied; negative values reject before any setter mutation.
   Uses ordinary native counter0 even on a Rin-route save; counter13 is separate.
   Native call-clobbered Cpu registers may change, as in the surrounding loader. */
static int catherine_dev_karma_apply(Cpu*c,uint64_t restored_config,int32_t enabled,int32_t value,int32_t*readback){
 if(!catherine_dev_karma_request_valid(enabled,value))return -30;
 if(!restored_config)return -31;
 f_700009bed0(c);if(c->x[0]!=restored_config)return -31;
 if(enabled){
  /* Native setter updates the active ordinary meter. Native load continuation
     also requires this service. Reject an absent service instead of bypassing it. */
  if(!(LD8(restored_config+0x9818)&0x40u)){
   const uint64_t holder=LD64(0x7000cc96c8ull);
   if(!holder || !LD64(holder))return -32;
  }
  c->x[0]=restored_config;c->x[1]=0;c->x[2]=(uint32_t)value;f_700009ac10(c);
 }
 c->x[0]=restored_config;c->x[1]=0;f_700009ac00(c);
 if(readback)*readback=(int32_t)c->x[0];
 return !enabled || (int32_t)c->x[0]==value ? 1 : -33;
}
