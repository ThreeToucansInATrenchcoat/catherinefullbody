#pragma once
#include <stdint.h>
/* Native f686b0 initializes an embedded message by setting only header+9
   from0 to1. Never mutate game bytes or mask code/string/other header bytes. */
static inline int catherine_dev_recipe_flow_fingerprint(uint64_t flow,uint32_t size,
 uint32_t declared,uint32_t code,uint32_t words,uint64_t *out){
 if(!flow||!out||size<0x70||declared>size||code>declared||
    LD32(flow+4)!=declared||LD32(flow+0x48)!=words||LD32(flow+0x4c)!=code||
    LD32(flow+0x50)!=3||LD32(flow+0x54)!=1)return 0;
 const uint32_t bytes=LD32(flow+0x58),offset=LD32(flow+0x5c);
 if((uint64_t)code+4ull*words!=offset||offset>declared||bytes!=declared-offset)return 0;
 uint32_t initialized=UINT32_MAX;
 if(bytes){
  if(bytes<12||LD32(flow+offset)!=0x12345678u||LD32(flow+offset+4)!=0x00010000u||
     LD8(flow+offset+8)!=0||LD16(flow+offset+10)!=0||LD8(flow+offset+9)>1)return 0;
  initialized=offset+9;
 }
 uint64_t hash=0xcbf29ce484222325ull;
 for(uint32_t i=0;i<size;i++){hash^=i==initialized?0:LD8(flow+i);hash*=0x100000001b3ull;}
 *out=hash;return 1;
}
