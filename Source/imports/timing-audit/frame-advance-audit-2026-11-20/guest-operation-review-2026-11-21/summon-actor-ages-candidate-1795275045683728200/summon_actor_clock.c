#include "summon_actor_clock.h"
#include "timing_clock.h"
#include <string.h>
typedef struct {uint64_t wrapper,actor;unsigned admitted,observed;uint32_t last_age;} SummonAge;
static _Thread_local SummonAge records[16];
static _Thread_local uint64_t active_wrapper;
static uint64_t read64(uint64_t a){uint64_t v;memcpy(&v,(const void*)a,8);return v;}
static uint32_t read32(uint64_t a){uint32_t v;memcpy(&v,(const void*)a,4);return v;}
static uint16_t read16(uint64_t a){uint16_t v;memcpy(&v,(const void*)a,2);return v;}
static unsigned read8(uint64_t a){unsigned char v;memcpy(&v,(const void*)a,1);return v;}
static int live(uint64_t w,uint64_t a){
 if(!w||!a||read64(w)!=0x7000ac05c0ull||read16(w+0x10)!=2||read64(w+8)!=a||read64(a)!=0x7000abff90ull||read32(a+0xe0)!=9)return 0;
 const uint64_t holder=read64(0x7000cca5d8ull),manager=holder?read64(holder):0,stage=manager?read64(manager+0x668):0;
 if(!stage||read32(stage+0x18)!=10||read32(stage+0x1c)!=4||read8(stage+0x28)!=0)return 0;
 const uint64_t controller=read64(stage+0x5d0);if(!controller||read16(controller+8)!=1)return 0;
 const int index=(int16_t)read16(w+0x14),count=(int16_t)read16(controller+0xa);
 return index>=0&&count>0&&count<=5&&index<count&&read64(controller+0x10+8u*(unsigned)index)==w;
}
static void observe(SummonAge*r,uint32_t age){if((int32_t)age<5||(r->observed&&(int32_t)age<(int32_t)r->last_age))r->admitted=0;r->last_age=age;r->observed=1;}
static SummonAge* current(uint64_t a){
 if(!active_wrapper)return 0;
 for(unsigned i=0;i<16;++i)if(records[i].wrapper==active_wrapper&&records[i].actor==a)return live(active_wrapper,a)?&records[i]:0;return 0;
}
void catherine_summon_clock_forget(uint64_t w){for(unsigned i=0;i<16;++i)if(records[i].wrapper==w)memset(&records[i],0,sizeof records[i]);}
uint64_t catherine_summon_clock_enter(uint64_t w,uint64_t a){
 const uint64_t previous=active_wrapper;active_wrapper=0;if(!live(w,a))return previous;
 SummonAge*r=0;for(unsigned i=0;i<16;++i)if(records[i].wrapper==w){r=&records[i];break;}
 if(!r)for(unsigned i=0;i<16;++i)if(!records[i].wrapper){r=&records[i];r->wrapper=w;r->actor=a;break;}
 if(!r)return previous;if(r->actor!=a){r->actor=a;r->admitted=0;r->observed=0;}
 const uint32_t age=read32(a+0x23c);observe(r,age);
 active_wrapper=w;return previous;
}
void catherine_summon_clock_leave(uint64_t previous){active_wrapper=previous;}
int catherine_summon_clock_held(uint64_t a){SummonAge*r=current(a);if(!r)return 0;observe(r,read32(a+0x23c));return catherine_high_refresh_active()&&!(read8(0x7000d06360ull)&1u)&&!catherine_timing_tick();}
int catherine_summon_clock_grace_held(uint64_t a,uint32_t age){
 SummonAge*r=current(a);if(!r)return 0;
 observe(r,age);if((int32_t)age<5)return 0;
 if(!catherine_summon_clock_held(a)){r->admitted=1;return 0;}
 return !r->admitted;
}
