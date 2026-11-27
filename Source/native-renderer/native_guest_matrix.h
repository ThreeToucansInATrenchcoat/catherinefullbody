#pragma once
#include "simd.h"
// Native integer transpose of the already-loaded rows. No floating arithmetic,
// guest memory reads or callbacks occur between this block's register writes.
// The earlier row loads and following ordered guest stores remain unchanged.
static inline void native_uniform_matrix(Cpu*c){
    const vu32 r0=VREG(vu32,0),r1=VREG(vu32,1),r2=VREG(vu32,2),r3=VREG(vu32,3);
    const vu32 lo01=SHUF(r0,r1,0,4,1,5),hi01=SHUF(r0,r1,2,6,3,7);
    const vu32 lo23=SHUF(r2,r3,0,4,1,5),hi23=SHUF(r2,r3,2,6,3,7);
    c->v[0]=(u128)SHUF(lo01,lo23,2,3,6,7);
    c->v[1]=(u128)SHUF(lo23,hi23,2,3,6,7);
    c->v[2]=(u128)SHUF(hi01,hi23,2,3,6,7);
    c->v[4]=(u128)SHUF(lo01,lo23,0,1,4,5);
    c->v[5]=(u128)SHUF(hi01,hi23,0,1,4,5);
}
