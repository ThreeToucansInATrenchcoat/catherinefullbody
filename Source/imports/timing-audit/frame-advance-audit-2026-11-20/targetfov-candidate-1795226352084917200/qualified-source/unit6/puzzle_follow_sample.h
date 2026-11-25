#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
/* Presentation history only: never written back to the native follow state. */
typedef struct {
 uint64_t owner,stage;
 uint32_t mode,epoch;
 unsigned valid;
 float previous[3],current[3];
} CatherinePuzzleFollowSample;
static int catherine_puzzle_follow_sample(CatherinePuzzleFollowSample*s,
 uint64_t owner,uint64_t stage,unsigned mode,unsigned tick,uint64_t clock,
 unsigned high,unsigned legacy,float jump_limit,uint32_t row[3]) {
 uint32_t bits=(uint32_t)clock&0x7fffffffu,epoch=(uint32_t)(clock>>32);
 float fraction;memcpy(&fraction,&bits,4);
 if(!high||legacy||!owner||!stage||mode||!((uint32_t)clock>>31)||
    !isfinite(fraction)||fraction<0.f||fraction>=1.f||!(jump_limit>0.f)) {
  memset(s,0,sizeof *s);return 0;
 }
 float current[3];memcpy(current,row,sizeof current);
 for(unsigned i=0;i<3;++i)if(!isfinite(current[i])){memset(s,0,sizeof *s);return 0;}
 unsigned reset=!s->valid||s->owner!=owner||s->stage!=stage||s->mode!=mode||
  (uint32_t)(epoch-s->epoch)>1;
 if(!reset)for(unsigned i=0;i<3;++i)
  if(fabsf(current[i]-s->current[i])>jump_limit||(!tick&&current[i]!=s->current[i]))reset=1;
 if(reset) {
  memset(s,0,sizeof *s);s->valid=1;s->owner=owner;s->stage=stage;s->mode=mode;s->epoch=epoch;
  memcpy(s->previous,current,sizeof current);memcpy(s->current,current,sizeof current);
 }else if(epoch!=s->epoch) {
  memcpy(s->previous,s->current,sizeof current);memcpy(s->current,current,sizeof current);s->epoch=epoch;
 }else if(tick) {
  /* Preserve ordered multiple native updates within an admitted dispatch. */
  memcpy(s->current,current,sizeof current);
 }
 for(unsigned i=0;i<3;++i) {
  float result=s->previous[i];
  if(fraction!=0.f&&s->previous[i]!=s->current[i])
   result=(float)((1.0-(double)fraction)*(double)s->previous[i]+(double)fraction*(double)s->current[i]);
  memcpy(&row[i],&result,4);
 }
 return 1;
}
