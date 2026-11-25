#pragma once
#include <stdint.h>
#include "timing_clock.h"

/* The first Rin support tutorial, not a boss announcement or an opening.
   The immutable five-localization BF identity and complete procedure are checked.
   Only the authored WAIT20 is paced; native message/input polling stays live. */
static inline int catherine_matches_rin_tutorial_wait(uint64_t vm) {
    const uint64_t name0=0x305f33305f6c7a70ull, name1=0x303030305f32ull;
    if (!vm || LD64(vm)!=name0 || LD64(vm+8)!=name1 ||
        LD32(vm+0x208)!=5 || LD32(vm+0x18)!=0x69) return 0;
    const uint64_t flow=LD64(vm+0x1d0);
    if (!flow || LD32(flow+8)!=0x30574c46u || LD32(flow+0x10)!=5 ||
        LD64(vm+0x1e0)!=flow+0x70 || LD64(vm+0x1f0)!=flow+0x550) return 0;
    const uint32_t declared=LD32(flow+4);
    static const uint32_t localized_sizes[]={0x2909,0x28fd,0x290d,0x28dd,0x294d};
    unsigned locale=0;
    while (locale<5 && localized_sizes[locale]!=declared) ++locale;
    if (locale==5) return 0;
    static const uint32_t common_sections[]={
        0,32,15,0x70, 1,32,24,0x250, 2,4,0x75b,0x550,
        3,1
    };
    for (unsigned i=0;i<sizeof common_sections/sizeof common_sections[0];++i)
        if (LD32(flow+0x20+4u*i)!=common_sections[i]) return 0;
    if (LD32(flow+0x58)!=declared-0x22bcu || LD32(flow+0x5c)!=0x22bc ||
        LD32(flow+0x60)!=4 || LD32(flow+0x64)!=1 ||
        LD32(flow+0x68)!=0xf0 || LD32(flow+0x6c)!=declared) return 0;
    const uint64_t entry=flow+0x70+5*32;
    if (LD64(entry)!=name0 || LD64(entry+8)!=name1 ||
        LD32(entry+24)!=0x61 || LD32(entry+56)!=0x8b) return 0;
    static const uint32_t procedure[]={
        0x00050007u,0x0001001du,0x20010008u,0x0bc5001du,0x20220008u,0x0001001du,
        0x20230008u,0x0014001du,0x00090008u,0x0005001du,0x00300008u,0x001f0008u,
        0x0006001du,0x00300008u,0x001f0008u,0x0007001du,0x00300008u,0x001f0008u,
        0x0008001du,0x00300008u,0x001f0008u,0x0009001du,0x00300008u,0x001f0008u,
        0x0001001du,0x20830008u,0x00000004u,0x00000016u,0x0000001cu,0x000a001du,
        0x00300008u,0x001f0008u,0x0013001du,0x20230008u,0x000b001du,0x00300008u,
        0x001f0008u,0x0000000du,0x20250008u,0x20020008u,0x00000009u,0x00000009u,
    };
    for (unsigned i=0;i<sizeof procedure/sizeof procedure[0];++i)
        if (LD32(flow+0x550+4u*(0x61+i))!=procedure[i]) return 0;
    return 1;
}

static inline int catherine_hold_rin_tutorial_wait(uint64_t vm) {
    return catherine_high_refresh_active() && !(LD8(0x7000d06360ull)&1u) &&
           !catherine_timing_tick() && catherine_matches_rin_tutorial_wait(vm);
}
