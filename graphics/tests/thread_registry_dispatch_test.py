"""Exercise the actual host Create/Start/Close/Wait branches, not copied logic."""
from pathlib import Path
import hashlib
import json
import subprocess

repo = Path(__file__).resolve().parents[2]
out = repo / 'timing-audit/thread-affinity-ending-evidence'
host = (repo / 'base/host.c').read_text()

def section(start, end):
    a = host.index(start)
    return host[a:host.index(end, a)]

parts = [
    section('typedef struct { Cpu cpu; CatherineThreadLifetime', 'static VOID CALLBACK wake_guest_wait'),
    section('static void wait_guest_objects', 'static DWORD WINAPI watchdog'),
    'void registry_svc(Cpu *c,uint32_t imm){\n',
    section(' if (imm==0x16 && c->x[0]>=200', ' if (imm==0x16 && c->x[0]>=400'),
    section(' if (imm==8) {', ' if(imm==0xa && c->thread)'),
    section(' if((imm==0xc||imm==0x25)', ' if(imm==0x10)'),
    ' if(imm==0x18){wait_guest_objects(c);return;}c->x[0]=0xe401;\n}\n',
]
prefix = r'''
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "cpu.h"
#include "thread_affinity.h"
#include "thread_lifetime.h"
static _Thread_local unsigned thread_slot;
static uint64_t thread_pc[65],thread_lr[65],thread_x0[65];
static int fail_create;
static HANDLE duplicate_observed;
static BOOL fixture_duplicate(HANDLE a,HANDLE b,HANDLE c,LPHANDLE d,DWORD e,BOOL f,DWORD g){
    BOOL result=DuplicateHandle(a,b,c,d,e,f,g);
    if(result&&duplicate_observed)SetEvent(duplicate_observed);
    return result;
}
#define DuplicateHandle fixture_duplicate
static HANDLE fixture_create(LPSECURITY_ATTRIBUTES a,SIZE_T s,LPTHREAD_START_ROUTINE f,LPVOID p,DWORD flags,LPDWORD id){
    if(fail_create){fail_create=0;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return NULL;}
    return CreateThread(a,s,f,p,flags,id);
}
#define CreateThread fixture_create
static HANDLE ipc_event(uint32_t unused){(void)unused;return NULL;}
void rt_dispatch(Cpu *cpu){
    if(cpu->x[0])assert(WaitForSingleObject((HANDLE)(uintptr_t)cpu->x[0],10000)==WAIT_OBJECT_0);
}
'''
suffix = r'''
#undef CreateThread
#undef DuplicateHandle
static DWORD WINAPI concurrent_wait(void *argument){
    Cpu cpu={0};uint32_t handle=(uint32_t)(uintptr_t)argument;
    cpu.x[1]=(uintptr_t)&handle;cpu.x[2]=1;cpu.x[3]=10000000000ull;
    registry_svc(&cpu,0x18);assert(cpu.x[0]==0);return 0;
}
static uint32_t create(Cpu *cpu,HANDLE event){
    cpu->x[1]=0x7000001000ull;cpu->x[2]=(uintptr_t)event;cpu->x[3]=0x123400;
    cpu->x[4]=44;cpu->x[5]=(uint32_t)-1;
    registry_svc(cpu,8);assert(!cpu->x[0]);return (uint32_t)cpu->x[1];
}
static void start(Cpu *cpu,uint32_t handle){cpu->x[0]=handle;registry_svc(cpu,9);assert(!cpu->x[0]);}
static void close_guest(Cpu *cpu,uint32_t handle){cpu->x[0]=handle;registry_svc(cpu,0x16);assert(!cpu->x[0]);}
static void wait_guest(Cpu *cpu,uint32_t handle,uint32_t expected){
    cpu->x[1]=(uintptr_t)&handle;cpu->x[2]=1;cpu->x[3]=10000000000ull;
    registry_svc(cpu,0x18);assert(cpu->x[0]==expected);
}
int main(void){
    Cpu cpu={0};uint64_t last_id=0;
    for(unsigned i=0;i<160;++i){
        uint32_t handle=create(&cpu,NULL);assert(handle==200);
        cpu.x[1]=handle;registry_svc(&cpu,0x25);assert(!cpu.x[0]&&cpu.x[1]>last_id);last_id=cpu.x[1];
        start(&cpu,handle);wait_guest(&cpu,handle,0);close_guest(&cpu,handle);
        wait_guest(&cpu,handle,0xe401);
    }
    assert(thread_count==1);
    fail_create=1;cpu.x[1]=0x123456;registry_svc(&cpu,8);
    assert(cpu.x[0]==0xce01&&cpu.x[1]==0x123456&&thread_count==1);
    assert(!threads[0].lifetime.host&&!threads[0].lifetime.tls&&!threads[0].lifetime.reserved);
    HANDLE event=CreateEventW(NULL,TRUE,FALSE,NULL);assert(event);
    uint32_t handles[64];
    for(unsigned i=0;i<64;++i){handles[i]=create(&cpu,event);assert(handles[i]==200+i);start(&cpu,handles[i]);}
    duplicate_observed=CreateEventW(NULL,TRUE,FALSE,NULL);assert(duplicate_observed);
    HANDLE waiter=CreateThread(NULL,0,concurrent_wait,(void *)(uintptr_t)200,0,NULL);assert(waiter);
    assert(WaitForSingleObject(duplicate_observed,10000)==WAIT_OBJECT_0);
    for(unsigned i=0;i<64;++i)close_guest(&cpu,handles[i]);
    cpu.x[1]=0x123456;registry_svc(&cpu,8);
    assert(cpu.x[0]==0xce01&&cpu.x[1]==0x123456&&thread_count==64);
    cpu.x[0]=200;registry_svc(&cpu,9);assert(cpu.x[0]==0xe401);
    cpu.x[0]=200;cpu.x[1]=0;cpu.x[2]=7;registry_svc(&cpu,0xf);assert(cpu.x[0]==0xe401);
    assert(SetEvent(event));
    for(unsigned i=0;i<64;++i)assert(WaitForSingleObject(threads[i].lifetime.host,10000)==WAIT_OBJECT_0);
    uint32_t handle=create(&cpu,NULL);assert(handle==200);start(&cpu,handle);wait_guest(&cpu,handle,0);
    assert(thread_count==64);
    assert(WaitForSingleObject(waiter,10000)==WAIT_OBJECT_0);
    CloseHandle(waiter);CloseHandle(duplicate_observed);duplicate_observed=NULL;
    close_guest(&cpu,handle);CloseHandle(event);
    for(unsigned i=0;i<64;++i)catherine_thread_lifetime_discard(&threads[i].lifetime);
    puts("PASS extracted production SVC dispatch:160 native lifetimes, failed-create rollback,64 closed-live nonreuse/exhaustion, invalid closed handles, concurrent close/wait, successful post-exhaustion reuse");
}
'''
source = out / 'thread-registry-dispatch-extracted.c'
source.write_text(prefix + ''.join(parts) + suffix)
(out / 'thread-registry-dispatch-provenance.json').write_text(json.dumps({
    'host_sha256': hashlib.sha256((repo / 'base/host.c').read_bytes()).hexdigest(),
    'extracted_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'scope': 'Exact production registry definitions, WaitSynchronization, thread entry and Create/Start/Close/priority/affinity SVC branches; injected CreateThread failure only.'
}, indent=2))
exe = out / 'thread-registry-dispatch-test.exe'
subprocess.run(['clang', '--target=x86_64-pc-windows-msvc', '-O2', '-fms-runtime-lib=static',
                '-I'+str(repo/'reference/runtime'), '-I'+str(repo/'base'), str(source), '-o', str(exe)], check=True)
result = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
(out / 'thread-registry-dispatch-results.txt').write_text(result.stdout + result.stderr)
print(result.stdout.strip().splitlines()[-1])
