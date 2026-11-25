#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "config_pc_host.h"
static uint64_t slot[1],app[12],manager[8],nodes[12][4],tasks[12][3];
static int config_open,last_mode,publishes;static unsigned reads;
static uint64_t world_slot[1],world[20],players[2][1];
static uint64_t read64(uint64_t p){++reads;if(p==0x7000cc9f98ull)return (uintptr_t)slot;if(p==0x7000cca5d8ull)return (uintptr_t)world_slot;uint64_t v;memcpy(&v,(void*)(uintptr_t)p,8);return v;}
static uint32_t read32(uint64_t p){++reads;uint32_t v;memcpy(&v,(void*)(uintptr_t)p,4);return v;}
#define LD64(p) read64(p)
#define LD32(p) read32(p)
int catherine_config_pc_is_open(void){return config_open;}
int catherine_config_setting(unsigned field,int change){assert(field==PC_INPUT_MENU_MODE);assert(change==1||change==2);last_mode=change;++publishes;return change==2;}
#include "menu_input_scope.h"
static void setup(void){memset(world_slot,0,sizeof world_slot);memset(world,0,sizeof world);memset(players,0,sizeof players);memset(app,0,sizeof app);memset(manager,0,sizeof manager);memset(nodes,0,sizeof nodes);memset(tasks,0,sizeof tasks);slot[0]=(uintptr_t)app;app[9]=(uintptr_t)manager;manager[5]=(uintptr_t)nodes[0];for(unsigned i=0;i<12;++i){nodes[i][2]=(uintptr_t)tasks[i];if(i<11)nodes[i][0]=(uintptr_t)nodes[i+1];}config_open=0;reads=0;}
static void id(unsigned i,unsigned value){memcpy((char*)tasks[i]+12,&value,4);}
static void expect(int mode){int count=publishes;catherine_input_menu_scope_publish();assert(publishes==count+1&&last_mode==mode);}
int main(void){
 setup();id(0,1);expect(2); // Title/piano; a nested Config cannot clear this owner.
 config_open=1;expect(2);config_open=0;expect(2);
 id(0,3);expect(1);config_open=1;expect(2);config_open=0;expect(1); // Gameplay/config/gameplay.
 setup();id(0,7);id(5,1);expect(2);id(5,9);expect(1); // Unknown IDs do not create menu ownership.
 setup();slot[0]=0;expect(1);config_open=1;expect(2);
 setup();app[9]=0;expect(1);
 setup();manager[5]=0;expect(2);
 setup();manager[5]=(uintptr_t)manager+0x28;expect(2);
 world_slot[0]=(uintptr_t)world;world[0]=0x7000ad96e0ull;expect(2);
 world[17]=(uintptr_t)players[0];players[0][0]=0x7000abff90ull;expect(1);
 world[18]=(uintptr_t)players[1];players[1][0]=0x7000abff90ull;expect(1);
 world[17]=world[18]=0;expect(2); // Actual stage teardown back to optional selector.
 world[0]=0x7000123450ull;expect(1); // Unknown world type remains conservative gameplay.
 world[0]=0x7000ad96e0ull;world[17]=(uintptr_t)players[0];config_open=1;expect(2);config_open=0;expect(1);
 setup();nodes[0][2]=0;id(1,1);expect(2); // A task-less node is not a menu.
 setup();nodes[11][0]=(uintptr_t)nodes[0];expect(1);assert(reads<100); // Circular native list.
 setup();nodes[11][0]=(uintptr_t)nodes[3];expect(1);assert(reads<800); // Non-head cycle is bounded.
 setup();id(0,1);for(unsigned i=0;i<240;++i)expect(2); // Per-render publication is stable.
 puts("PASS title/piano/config owner handoff; gameplay/unknown/empty IDs; null tasks; sentinel and bounded cycles; 240 per-frame publishes.");
}
