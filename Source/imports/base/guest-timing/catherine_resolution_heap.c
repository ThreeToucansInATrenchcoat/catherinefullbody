// Exact guest-native backing reservations. No Vulkan object construction here.
#include "cpu.h"
#include "catherine_resolution_live.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
void f_70002ec080(Cpu *c); void f_70002ec110(Cpu *c);
void f_70002f2680(Cpu *c);
void f_7000332608(Cpu *c); void f_70003325c0(Cpu *c);
void f_7000332dc8(Cpu *c); void f_7000332b60(Cpu *c);
void f_700033347c(Cpu *c); void f_70003334a8(Cpu *c);
enum { ROLE_COUNT=9 };
enum { STOCK_RT_HEAP = 512u << 20, PC_RT_HEAP = 768u << 20 };
static const unsigned slots[8]={0x64b0,0x64b8,0x64c0,0x64c8,0x64d0,0x6908,0x6910,0x6538};
typedef struct Reservation {
    uint64_t allocator,size,block,callsite,storage,alignment;
    unsigned char descriptor[36];
} Reservation;
static int descriptor_matches(const Reservation *r,uint64_t d) {
    if (!d) return 0;
    uint64_t expected=(uint64_t)(uintptr_t)r->descriptor;
    // Compare only initialized descriptor fields, never padding bytes 10/11.
    if (LD8(d)!=LD8(expected) || LD8(d+1)!=LD8(expected+1)) return 0;
    for (unsigned off=2;off<=8;off+=2) if (LD16(d+off)!=LD16(expected+off)) return 0;
    for (unsigned off=12;off<=32;off+=4) if (LD32(d+off)!=LD32(expected+off)) return 0;
    return 1;
}
static _Thread_local struct {
    Reservation roles[ROLE_COUNT];
    uint64_t root,heap,index_owner;
    unsigned used,active;
} held;
void catherine_resolution_allocate_heap(void *opaque) {
    Cpu *c = opaque;
    if (c->x[0] == STOCK_RT_HEAP && c->x[24] == STOCK_RT_HEAP) {
        // A live change can briefly need both sets of render targets.
        Cpu original = *c;
        c->x[0] = PC_RT_HEAP;
        f_70002f2680(c);
        if (c->x[0]) {
            c->x[24] = PC_RT_HEAP;
            return;
        }
        *c = original;
    }
    f_70002f2680(c);
}
static uint64_t call(Cpu *c,GuestFn fn,uint64_t a,uint64_t b) {
    Cpu saved=*c; c->x[0]=a; c->x[1]=b; fn(c);
    uint64_t result=c->x[0]; *c=saved; return result;
}
static uint64_t target(uint64_t root,unsigned role) {
    uint64_t p=LD64(root+slots[role]);
    if (!p || role==7) return p;
    p=LD64(p+0x50); return p ? LD64(p+8) : 0;
}
static int block_valid(uint64_t block,uint64_t heap,uint64_t needed) {
    if (!block || LD64(block+0x18)!=heap) return 0;
    uint64_t base=LD64(heap),capacity=LD64(heap+8);
    uint64_t ptr=LD64(block),size=LD32(block+0x10);
    return needed<=size && ptr>=base && ptr-base<=capacity && size<=capacity-(ptr-base);
}
static void cancel(Cpu *c) {
    held.active=0;
    for (unsigned i=0;i<ROLE_COUNT;++i)
        if (held.roles[i].block) call(c,f_70002ec110,held.roles[i].block,0);
    memset(&held,0,sizeof held);
}
static int request_size(Cpu *c,unsigned role,uint64_t d,GuestFn sizefn,GuestFn alignfn) {
    uint64_t size=call(c,sizefn,held.root+0x18,d),align=call(c,alignfn,held.root+0x18,d);
    if (!size || !align || (align&(align-1)) || size>0x1fffffff || align>0x100000) return 0;
    held.roles[role]=(Reservation){held.heap,size+align-1,0,
        role ? 0x70003139c8ull : 0x70003047fcull,size,align,{0}};
    if (role) memcpy(held.roles[role].descriptor,(const void*)(uintptr_t)d,36);
    return 1;
}
static int invalid(unsigned height,unsigned line) {
    if (catherine_resolution_trace_enabled())
        fprintf(stderr,"LiveResolution heap=invalid height=%u source_line=%u\n",height,line);
    return -1;
}
int catherine_resolution_heap_prepare(void *opaque,unsigned height) {
    Cpu *c=opaque;
    if (held.active) abort();
    if (height<360 || height>2160 || height%18u!=0) return invalid(height,__LINE__);
    memset(&held,0,sizeof held);
    uint64_t root=LD64(0x7000cc9498ull);
    if (!root || !(LD32(root+0x648c)&1)) return invalid(height,__LINE__);
    held.root=root; held.heap=LD64(root+0x6470); held.index_owner=LD64(root+0x6e18);
    if (!held.heap || !held.index_owner) return invalid(height,__LINE__);
    const uint64_t capacity = LD64(held.heap+8);
    if (capacity != STOCK_RT_HEAP && capacity != PC_RT_HEAP) return invalid(height,__LINE__);
    // Fixed quad indices are resolution-independent. Require the existing owner
    // before skipping the setter's constant tail, which otherwise leaks its owner.
    uint64_t index_block=LD64(held.index_owner+0x50),index_heap=LD64(root+0x6340);
    if (!index_heap || !block_valid(index_block,index_heap,0x1fffe)) return invalid(height,__LINE__);
    uint32_t old_width=LD32(root+0x6480),old_height=LD32(root+0x6484);
    if (!old_height || old_width!=old_height/9u*16u) return invalid(height,__LINE__);
    uint64_t old_blocks[ROLE_COUNT]={LD64(root+0x3658)};
    if (!block_valid(old_blocks[0],held.heap,1)) return invalid(height,__LINE__);
    uint32_t formats[8]={0xb01,0xb01,0xb01,0xb01,0x1307,0xf05,0xb01,0};
    uint64_t map=LD64(0x7000ccf130ull); int found=0;
    if (!map) return invalid(height,__LINE__);
    for (unsigned i=0;i<20;++i)
        if (LD32(map+i*8)==4) { formats[7]=LD32(map+i*8+4); found=1; break; }
    if (!found) return invalid(height,__LINE__);
    for (unsigned i=0;i<8;++i) {
        uint64_t t=target(root,i);
        if (!t || LD32(t+0xf4)!=old_width || LD32(t+0xf8)!=old_height || LD32(t+0xec)!=formats[i]) return invalid(height,__LINE__);
        old_blocks[i+1]=LD64(t+0x168);
        if (!block_valid(old_blocks[i+1],held.heap,1)) return invalid(height,__LINE__);
        for (unsigned j=0;j<=i;++j) if (old_blocks[j]==old_blocks[i+1]) return invalid(height,__LINE__);
    }
    // Same guest descriptor initializers and driver getters as native creation.
    _Alignas(16) unsigned char descriptor[64]={0}; uint64_t d=(uint64_t)(uintptr_t)descriptor;
    call(c,f_700033347c,d,0);
    ST8(d,3); ST32(d+4,0xb01); ST32(d+8,height/9u*16u); ST32(d+12,height);
    ST64(d+0x18,LD64(root+0x10));
    if (!request_size(c,0,d,f_7000332608,f_70003325c0)) return invalid(height,__LINE__);
    for (unsigned i=0;i<8;++i) {
        memset(descriptor,0,sizeof descriptor); call(c,f_70003334a8,d,0);
        // f_7000313c60 passes x3=1 for the direct final target; wrapped
        // color/depth targets pass x3=0. f_70003138c0 stores it at +2.
        ST16(d+2,i==7 ? 1 : 0);
        ST32(d+12,formats[i]); ST32(d+16,0x20);
        ST32(d+20,height/9u*16u); ST32(d+24,height);
        if (!request_size(c,i+1,d,f_7000332dc8,f_7000332b60)) return invalid(height,__LINE__);
    }
    for (unsigned i=0;i<ROLE_COUNT;++i) {
        Reservation *r=&held.roles[i];
        r->block=call(c,f_70002ec080,r->allocator,r->size);
        if (!r->block) {
            if (catherine_resolution_trace_enabled())
                fprintf(stderr,"LiveResolution heap=capacity height=%u failed_role=%u request_B=%llu reserved_roles=%u\n",height,i,(unsigned long long)r->size,i);
            cancel(c); return 0;
        }
        if (!block_valid(r->block,r->allocator,r->size)) abort();
        for (unsigned j=0;j<i;++j) if (r->block==held.roles[j].block) abort();
        for (unsigned j=0;j<ROLE_COUNT;++j) if (r->block==old_blocks[j]) abort();
    }
    held.active=1;
    if (catherine_resolution_trace_enabled()) {
        uint64_t bytes=0; for (unsigned i=0;i<ROLE_COUNT;++i) bytes+=LD32(held.roles[i].block+0x10);
        fprintf(stderr,"LiveResolution heap=reserved height=%u roles=9 bytes=%llu capacity=%llu\n",height,(unsigned long long)bytes,(unsigned long long)LD64(held.heap+8));
    }
    return 1;
}
int catherine_resolution_take_allocation(void *opaque) {
    Cpu *c=opaque;
    if (!held.active) return 0;
    if (held.used>=ROLE_COUNT) abort();
    Reservation *r=&held.roles[held.used];
    // Never silently fall back to unreserved allocation after destructive work.
    const uint64_t storage=c->x[held.used ? 27 : 20];
    const uint64_t alignment=c->x[held.used ? 28 : 21];
    const int metadata_match=!held.used || descriptor_matches(r,c->x[24]);
    if (c->x[0]!=r->allocator || c->x[1]!=r->size || c->x[30]!=r->callsite ||
        storage!=r->storage || alignment!=r->alignment || !metadata_match ||
        !block_valid(r->block,r->allocator,r->size)) {
        fprintf(stderr,"LiveResolution heap=contract_mismatch role=%u expected_size=%llu actual_size=%llu expected_pc=%llx actual_pc=%llx allocator_match=%u expected_alignment=%llu actual_alignment=%llu metadata_match=%u\n",held.used,(unsigned long long)r->size,(unsigned long long)c->x[1],(unsigned long long)r->callsite,(unsigned long long)c->x[30],c->x[0]==r->allocator,(unsigned long long)r->alignment,(unsigned long long)alignment,metadata_match);
        abort();
    }
    c->x[0]=r->block; ++held.used; return 1;
}
int catherine_resolution_heap_active(void) { return held.active!=0; }
unsigned catherine_resolution_native_height(void) {
    uint64_t r=LD64(0x7000cc9498ull);
    return r && (LD32(r+0x648c)&1) ? LD32(r+0x6484) : 0;
}
int catherine_resolution_heap_finish(void *opaque,unsigned height) {
    (void)opaque;
    if (!held.active || held.used!=ROLE_COUNT) return 0;
    uint64_t root=held.root; unsigned width=height/9u*16u;
    if (LD32(root+0x6480)!=width || LD32(root+0x6484)!=height ||
        LD32(root+0x64a4)!=1280 || LD32(root+0x64a8)!=720 ||
        LD16(root+0x6a94)!=width || LD16(root+0x6a96)!=height ||
        LD16(root+0x6aa4)!=width || LD16(root+0x6aa6)!=height ||
        LD64(root+0x3658)!=held.roles[0].block || LD64(root+0x6e18)!=held.index_owner) return 0;
    for (unsigned i=0;i<8;++i) {
        uint64_t t=target(root,i);
        if (!t || LD32(t+0xf4)!=width || LD32(t+0xf8)!=height || LD64(t+0x168)!=held.roles[i+1].block ||
            !descriptor_matches(&held.roles[i+1],t+0xe0) ||
            !block_valid(held.roles[i+1].block,held.heap,held.roles[i+1].size)) return 0;
    }
    if (catherine_resolution_trace_enabled())
        fprintf(stderr,"LiveResolution heap=verified height=%u main_targets=8 consumed=9 viewport=%ux%u auxiliary=1280x720 index_owner=preserved\n",height,width,height);
    // All allocations now belong to new native objects; do NOT free them.
    memset(&held,0,sizeof held); return 1;
}
