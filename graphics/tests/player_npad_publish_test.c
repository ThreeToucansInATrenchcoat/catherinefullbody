#include "catherine_npad_publish.h"
#include <assert.h>
#include <stdio.h>
static unsigned char memory[0xa000];
int main(void){
    const int32_t left[4]={-32767,1234,0,99},right[4]={32767,-1234,81,0};
    for(uint64_t sample=2;sample<50;++sample){
        const uint64_t slot=(sample-1)%17;
        catherine_publish_npad(memory,sample,slot,1,left,1);
        catherine_publish_npad(memory+0x5000,sample,slot,2,right,1);
        const unsigned char *p1=memory+0x48+slot*0x30,*p2=p1+0x5000;
        assert(*(const uint64_t *)p1==sample&&*(const uint64_t *)p2==sample);
        assert(*(const uint64_t *)(p1+16)==1&&*(const uint64_t *)(p2+16)==2);
        assert(*(const int32_t *)(p1+24)==left[0]&&*(const int32_t *)(p2+24)==right[0]);
        assert(*(const uint64_t *)(memory+0x38)==slot);
    }
    catherine_publish_npad(memory+0x5000,50,15,~0ull,right,0);
    const unsigned char *p2=memory+0x5000+0x48+15*0x30;
    assert(*(const uint32_t *)(memory+0x5000)==0);
    assert(*(const uint64_t *)(p2+16)==0&&*(const uint64_t *)(p2+24)==0);
    assert(*(const uint32_t *)(p2+40)==0&&*(const uint32_t *)memory==1);
    puts("Independent P1/P2 native rings and disconnect clearing passed");
}
