#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>
/* Owned by one scripted camera instance; no global history or puzzle clock change. */
typedef struct {
 uint64_t owner,table;
 uint32_t mode,count,consumed,expected,epoch;
 float fraction;
 unsigned key,consumption;
} CatherineRocketCameraSample;
/* Returns one only when row contains a qualified presentation sample.
   Zero leaves the caller on its original authored-table path. */
static int catherine_rocket_camera_row(CatherineRocketCameraSample *s,uint64_t owner,
 uint32_t mode,uint64_t table,int32_t count,int32_t frame,unsigned tick,
 uint64_t clock,unsigned high,unsigned legacy,uint32_t row[11]) {
 if(!high||legacy){memset(s,0,sizeof *s);return 0;}
 if(!owner||(mode!=25&&mode!=26)||count<=0||count>1048576||frame<0||frame>=count||
    table!=LD64(0x70009f84e8ull+mode*8ull)||count!=(int32_t)LD32(0x70007b4814ull+mode*4ull)){
  memset(s,0,sizeof *s);return 0;
 }
 uint32_t epoch=(uint32_t)(clock>>32),bits=(uint32_t)clock&0x7fffffffu;
 float f;memcpy(&f,&bits,4);
 unsigned valid=((uint32_t)clock>>31)&&isfinite(f)&&f>=0.f&&f<1.f;
 if(!s->key||s->owner!=owner||s->mode!=mode||s->table!=table||s->count!=(uint32_t)count){
  memset(s,0,sizeof *s);s->key=1;s->owner=owner;s->mode=mode;s->table=table;s->count=count;
 }
 uint32_t k;
 if(tick){
  s->consumed=(uint32_t)frame;s->expected=(uint32_t)(frame+1<count?frame+1:frame);
  s->epoch=epoch;s->fraction=valid?f:0.f;s->consumption=1;k=s->consumed;
 }else if(s->consumption&&s->expected==(uint32_t)frame&&s->epoch==epoch){
  k=s->consumed;if(valid)s->fraction=f;
 }else{
  s->consumption=0;
  /* A new shot has not consumed row zero until its first admitted tick. */
  if(frame!=0)return 0;
  k=0;s->fraction=0.f;
 }
 uint32_t next=k+1<(uint32_t)count?k+1:k;
 uint64_t a=table+44ull*k,z=table+44ull*next;
 for(unsigned i=0;i<11;i++)row[i]=LD32(a+i*4ull);
 static const unsigned continuous[8]={0,1,2,3,4,5,9,10};
 for(unsigned i=0;i<8;i++){
  unsigned field=continuous[i];uint32_t ab=LD32(a+field*4ull),bb=LD32(z+field*4ull);
  float av,bv;memcpy(&av,&ab,4);memcpy(&bv,&bb,4);
  if(!isfinite(av)||!isfinite(bv))return 0;
  if(s->fraction==0.f||k==next){row[field]=ab;continue;}
  double v=(1.0-(double)s->fraction)*(double)av+(double)s->fraction*(double)bv;
  double lo=av<bv?av:bv,hi=av>bv?av:bv;if(v<lo)v=lo;if(v>hi)v=hi;
  float result=(float)v;memcpy(&row[field],&result,4);
 }
 return 1;
}

uint64_t catherine_camera_render_clock(void);
/* One active scripted-camera cache; owner key prevents cross-instance reuse.
   Interleaved owners safely invalidate rather than sharing presentation history. */
static _Thread_local CatherineRocketCameraSample catherine_rocket_camera_context;
