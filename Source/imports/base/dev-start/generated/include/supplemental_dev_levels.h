#pragma once
#include "dev_loader_native.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void f_700009d360(Cpu *);
void f_7000223aa0(Cpu *);
void f_7000224350(Cpu *);
void f_700023f150(Cpu *);
void f_700023f0e0(Cpu *);
void f_700023fd40(Cpu *);
void f_7000224390(Cpu *);
void f_70002f2ce0(Cpu *);
extern int catherine_multiplayer_available(void);
__declspec(dllimport) unsigned long long __stdcall GetTickCount64(void);

/* All state belongs to the guest scheduler thread. Native root 16 calls the
   selector callback on the same thread once its arcade resources are ready. */
static uint32_t catherine_optional_target;
static uint32_t catherine_optional_previous;
static int catherine_optional_saw_args;
static int catherine_optional_rapunzel_chosen;
static int catherine_optional_release_arcade;
static unsigned long long catherine_optional_deadline;
static uint64_t catherine_optional_verified_vm;

static uint32_t catherine_optional_arcade_layout(uint32_t id) {
    return id >= 22301 ? 65+(id-22301)%4 : 1+(id-22001)%64;
}
static uint32_t catherine_optional_arcade_index(uint32_t id) {
    if (id < 22301) return id-22001;
    const uint32_t variant = id-22301;
    return 64*(variant/4)+51+4*(variant%4);
}

static int catherine_dev_optional_root(uint32_t id) {
    if ((id >= 20001 && id <= 20004) || (id >= 20101 && id <= 20104)) return 18;
    if (id >= 21001 && id <= 21011) return 19;
    if ((id >= 22001 && id <= 22256) || (id >= 22301 && id <= 22316)) return 16;
    return 11;
}
static int catherine_dev_optional_pair(uint32_t id) {
    return (id >= 20101 && id <= 20104) || (id >= 21001 && id <= 21011);
}
static int catherine_dev_optional_available(uint32_t id) {
    return !catherine_dev_optional_pair(id) || catherine_multiplayer_available();
}
static void catherine_dev_optional_enqueue(Cpu *c,uint32_t id) {
    const int root = catherine_dev_optional_root(id);
    const uint64_t old_sp = c->sp;
    const uint64_t args = (old_sp - 0x40ull) & ~15ull;
    c->sp = args;
    c->x[0] = root;
    c->x[1] = 0;
    c->x[2] = 0;
    c->x[3] = 1;
    if (root == 18 || root == 19) {
        ST32(args,id % 100);
        ST32(args+4,catherine_dev_optional_pair(id) ? 2 : 1);
        c->x[1] = args;
        c->x[2] = root == 18 ? 8 : 4;
    }
    f_700009d360(c);
    c->sp = old_sp;
}
static void catherine_dev_optional_apply(Cpu *c,uint64_t config,uint32_t id) {
    const int root = catherine_dev_optional_root(id);
    const int previous = catherine_dev_optional_root(catherine_optional_previous);
    const uint64_t mode = LD64(0x7000cc9c40ull);
    const uint64_t arcade_holder = LD64(0x7000ccb288ull);
    catherine_optional_release_arcade = root != 16 && arcade_holder && LD64(arcade_holder);
    if (previous == 18 || previous == 19 || (mode && LD32(mode) >= 2 && LD32(mode) <= 10)) {
        f_7000224350(c);
        if (mode) ST32(mode,0);
        c->x[0] = 1;
        f_70002f2ce0(c);
    }
    catherine_optional_previous = id;
    catherine_optional_target = root == 11 ? 0 : id;
    catherine_optional_saw_args = 0;
    catherine_optional_verified_vm = 0;
    catherine_optional_rapunzel_chosen = 0;
    if (root == 11) return;
    catherine_optional_deadline = GetTickCount64() + 120000;
    if (root == 16) {
        const uint32_t stage = catherine_optional_arcade_index(id)+1;
        ST32(config+0x7b88,stage);
        ST32(config+0x7b8c,stage);
        ST32(config+0x9b5c,(stage-1)/128);
        return;
    }
    /* The local stage selector publishes this 40-byte record in f223aa0
       before requesting root 18 or 19. Player IDs 0 and 1 designate the
       default Blue and Red Cap pair. A solo game carries no second player. */
    const uint64_t old_sp = c->sp;
    const uint64_t record = (old_sp-0x40ull)&~15ull;
    const int pair = catherine_dev_optional_pair(id);
    const uint32_t floor = id % 100;
    c->sp = record;
    memset((void *)(uintptr_t)record,0,40);
    ST32(record,1); ST32(record+4,floor);
    ST32(record+8,0); ST32(record+12,pair ? 1 : (uint32_t)-1);
    ST32(record+16,root == 19 ? 3 : 1);
    ST32(record+20,1); ST32(record+36,floor);
    c->x[0] = record;
    f_7000223aa0(c);
    c->sp = old_sp;
    if (mode) ST32(mode,root == 19 ? 5 : pair ? 3 : 2);
    c->x[0] = pair ? 2 : 1;
    f_70002f2ce0(c);
}
/* Native title ready path has loaded the localized Rapunzel archive, voices
   and table and initialized the session. Its ordinary exit callback retains
   the arcade owner for root 12 and sets the requested native current stage. */
int catherine_dev_optional_select_rapunzel(Cpu *cpu,uint64_t title) {
    if (catherine_dev_optional_root(catherine_optional_target) != 16 ||
        catherine_optional_rapunzel_chosen) return 0;
    const uint32_t zero_based = catherine_optional_arcade_index(catherine_optional_target);
    Cpu scratch = *cpu;
    scratch.x[0] = (zero_based/64)&1;
    f_700023f150(&scratch);
    ST32(title+0x35c,(zero_based%64)+1);
    ST32(title+0x20,1);
    f_700023f0e0(&scratch);
    catherine_optional_rapunzel_chosen = 1;
    return 1;
}
/* The first floor opens an interactive story before its puzzle. Four late
   floors branch to different map layouts after the common setup procedure.
   Identify the native floor program and redirect only that specific branch.
   Scripts in all five shipped languages have identical executable sections. */
uint32_t catherine_dev_optional_vm_branch(uint64_t vm,uint32_t *destination) {
    const uint32_t id = catherine_optional_target;
    if (!catherine_optional_rapunzel_chosen || !id || !vm) return UINT32_MAX;
    const uint32_t index = catherine_optional_arcade_index(id);
    const uint32_t floor = 1+index%64;
    if (floor!=1 && (floor<52 || floor%4)) return UINT32_MAX;
    const uint32_t extra = (index/64)&1;
    const uint64_t handle = LD64(vm+0x238);
    if (!handle || handle!=LD64(0x7000d01060ull) || LD64(handle+16)!=vm) return UINT32_MAX;
    const uint64_t flow = LD64(vm+0x1d0);
    if (!flow || LD32(flow+8)!=0x30574c46u || LD32(flow+0x28)!=68 ||
        LD32(flow+0x24)!=32 || LD32(flow+0x48)!=(extra ? 2516u : 2568u)) return UINT32_MAX;
    const uint64_t procedure = flow+LD32(flow+0x2c)+32*floor;
    char name[12];
    snprintf(name,sizeof name,extra ? "nxra_%02u" : "rap_%02u",floor);
    const size_t length = strlen(name)+1;
    if (memcmp((const void *)(uintptr_t)vm,name,length) ||
        memcmp((const void *)(uintptr_t)procedure,name,length)) return UINT32_MAX;
    const uint32_t entry = LD32(procedure+24);
    const uint64_t code = LD64(vm+0x1f0);
    if (code!=flow+LD32(flow+0x4c) || entry+27>=LD32(flow+0x48)) return UINT32_MAX;
    if (catherine_optional_verified_vm!=vm) {
        if (LD32(vm+0x18)!=entry) return UINT32_MAX;
        uint64_t hash=0xcbf29ce484222325ull;
        for (uint32_t i=0;i<4*LD32(flow+0x48);++i) {
            hash^=LD8(code+i); hash*=0x100000001b3ull;
        }
        if (hash!=(extra ? 0x11cf7dd1a83f9a5eull : 0xd37413acbb120c85ull)) {
            catherine_optional_target=0;
            catherine_dev_loader_finish(-50,"The Rapunzel program differs from the verified game data.");
            return UINT32_MAX;
        }
        catherine_optional_verified_vm=vm;
    }
    const uint32_t from = entry+(floor==1 ? 1 : 2);
    *destination = floor==1 ? entry+3 : entry+(catherine_optional_arcade_layout(id)>64 ? 23 : 18);
    return from;
}
static void catherine_dev_optional_tick(Cpu *cpu,uint64_t root) {
    if (catherine_optional_release_arcade) {
        Cpu scratch = *cpu;
        f_7000224390(&scratch);
        if (!(scratch.x[0]&1)) {
            f_700023fd40(&scratch);
            catherine_optional_release_arcade = 0;
        }
    }
    const uint32_t id = catherine_optional_target;
    if (!id) return;
    const int arcade = catherine_dev_optional_root(id) == 16;
    const int puzzle = arcade ? 3 : 5;
    if (LD32(root+4) == (uint32_t)puzzle) {
        const uint64_t args = LD64(root+24);
        if ((LD8(root)&1) && args) {
            const uint32_t index = arcade ? catherine_optional_arcade_index(id) : id%100-1;
            const uint32_t bank = arcade ? 1+(index/64)%2 : 1;
            const uint32_t floor = arcade ? catherine_optional_arcade_layout(id) : 1+index;
            catherine_optional_saw_args = LD32(args)==bank && LD32(args+4)==floor &&
                (arcade ? LD8(args+30)==2 && LD8(args+31)==index/128 :
                          LD32(args+16)==(id>=21001 ? 3u : 1u));
        } else if (catherine_optional_saw_args) {
            const uint64_t holder = LD64(0x7000ccb640ull);
            const uint64_t puzzle_task = holder ? LD64(holder) : 0;
            const uint64_t registry = LD64(0x7000ccadf0ull);
            const uint64_t actor = registry ? LD64(registry) : 0;
            if (puzzle_task && LD32(puzzle_task+8)==32 && actor && LD64(actor+0x20) &&
                (!catherine_dev_optional_pair(id) || LD64(registry+8))) {
                catherine_optional_target = 0;
                catherine_dev_loader_finish(0,"The selected native puzzle and player are loaded.");
                return;
            }
        }
    }
    if (GetTickCount64() >= catherine_optional_deadline) {
        catherine_optional_target = 0;
        catherine_dev_loader_finish(-49,"The selected optional puzzle did not finish loading.");
    }
}
