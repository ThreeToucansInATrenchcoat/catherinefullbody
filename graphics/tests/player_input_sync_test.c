#include "cpu.h"
#include <assert.h>
#include <stdio.h>
#include "config_pc_host.h"
static uint64_t vector[3],fake_slot;
static unsigned connected=2,menu,calls;
static uint64_t read_sync(uint64_t address){
    if(address==0x7000ccef78ull)return fake_slot;
    return *(uint64_t *)(uintptr_t)address;
}
#undef LD64
#define LD64(address) read_sync(address)
#include "player_input_sync.h"
int catherine_config_setting(unsigned field,int change){
    (void)change;return field==PC_INPUT_MENU_MODE?menu:field==PC_CONNECTED_PLAYERS?connected:0;
}
void f_70002fdd80(Cpu *c){
    assert(c->x[0]==2&&c->x[1]==0&&c->x[2]==2);++calls;
    vector[1]=vector[0]+8;
    memset(c,0xa5,sizeof *c);
}
int main(void){
    Cpu cpu={0};cpu.x[0]=1;cpu.x[8]=0x12345678;cpu.sp=0x98765432;cpu.v[3]=0x7654;
    const Cpu original=cpu;
    fake_slot=(uintptr_t)vector;vector[0]=0x1000;vector[1]=0x1004;
    catherine_restore_second_player_input(&cpu,0);assert(!calls);
    connected=1;catherine_restore_second_player_input(&cpu,1);assert(!calls);connected=2;
    vector[1]=vector[0]+8;catherine_restore_second_player_input(&cpu,1);assert(!calls);
    vector[1]=vector[0]+4;catherine_restore_second_player_input(&cpu,1);
    assert(calls==1&&vector[1]==vector[0]+8&&!memcmp(&cpu,&original,sizeof cpu));
    catherine_restore_second_player_input(&cpu,1);assert(calls==1);
    vector[1]=vector[0]+4;menu=1;catherine_restore_second_player_input(&cpu,1);assert(calls==2&&!memcmp(&cpu,&original,sizeof cpu));
    fake_slot=0;catherine_restore_second_player_input(&cpu,1);assert(calls==2);
    puts("Native reconnect setup is scoped to requested P2, actual two-player availability and a collapsed cache; guest CPU preserved");
}
