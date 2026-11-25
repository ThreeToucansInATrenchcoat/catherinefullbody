#define WIN32_LEAN_AND_MEAN
#include "thread_lifetime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static DWORD WINAPI finish(void *unused){(void)unused;return 0;}
static DWORD WINAPI blocked(void *event){assert(WaitForSingleObject(event,10000)==WAIT_OBJECT_0);return 0;}

static void publish(CatherineThreadLifetime *slot,LPTHREAD_START_ROUTINE fn,void *arg){
    slot->tls=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(slot->tls);
    HANDLE host=CreateThread(NULL,0,fn,arg,0,NULL);assert(host);
    catherine_thread_lifetime_publish(slot,host);
}

int main(void){
    CatherineThreadLifetime slots[64]={0};CatherineThreadLifetime *registry[64];
    for(unsigned i=0;i<64;++i)registry[i]=&slots[i];
    // The old registry failed its 65th lifetime even with every prior thread done.
    for(unsigned i=0;i<160;++i){
        int index=catherine_thread_lifetime_acquire(registry,64);assert(index==0);
        publish(&slots[index],finish,NULL);
        assert(WaitForSingleObject(slots[index].host,10000)==WAIT_OBJECT_0);
        assert(catherine_thread_lifetime_handle(&slots[index]));
        slots[index].closed=1;assert(!catherine_thread_lifetime_handle(&slots[index]));
    }
    // A failed allocation/creation rolls back its reservation and TLS together.
    int index=catherine_thread_lifetime_acquire(registry,64);assert(index==0);
    slots[index].tls=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);assert(slots[index].tls);
    void *failed_tls=slots[index].tls;
    catherine_thread_lifetime_discard(&slots[index]);
    MEMORY_BASIC_INFORMATION memory;assert(VirtualQuery(failed_tls,&memory,sizeof memory)&&memory.State==MEM_FREE);
    assert(!slots[index].host&&!slots[index].tls&&!slots[index].reserved);

    HANDLE event=CreateEventW(NULL,TRUE,FALSE,NULL);assert(event);
    HANDLE duplicate=NULL;
    for(unsigned i=0;i<64;++i){
        index=catherine_thread_lifetime_acquire(registry,64);assert(index==(int)i);
        publish(&slots[index],blocked,event);
    }
    assert(catherine_thread_lifetime_acquire(registry,64)==-1);
    assert(DuplicateHandle(GetCurrentProcess(),slots[0].host,GetCurrentProcess(),&duplicate,0,FALSE,DUPLICATE_SAME_ACCESS));
    for(unsigned i=0;i<64;++i)slots[i].closed=1;
    // Closing a live guest handle must never recycle its Cpu/TLS.
    void *live_tls=slots[0].tls;
    assert(catherine_thread_lifetime_acquire(registry,64)==-1&&slots[0].tls==live_tls);
    assert(WaitForSingleObject(duplicate,0)==WAIT_TIMEOUT);
    assert(SetEvent(event));
    for(unsigned i=0;i<64;++i)assert(WaitForSingleObject(slots[i].host,10000)==WAIT_OBJECT_0);
    assert(catherine_thread_lifetime_acquire(registry,64)==0);
    // A wait's duplicated reference survives concurrent close and registry reuse.
    assert(WaitForSingleObject(duplicate,0)==WAIT_OBJECT_0);
    assert(!slots[0].host&&!slots[0].tls&&slots[0].reserved);
    CloseHandle(duplicate);CloseHandle(event);
    for(unsigned i=0;i<64;++i)catherine_thread_lifetime_discard(&slots[i]);
    puts("PASS 160 real thread lifetimes, failed-create rollback, 64 live-thread exhaustion, closed-live nonreuse, duplicated wait survives reclamation");
}
