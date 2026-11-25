#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../../base/guest-timing/include/config_pc_host.h"
typedef struct { uint64_t x[31], pc; } Cpu;
static unsigned char mem[0x5000];
static uint32_t read32(uint64_t a){uint32_t v=0; if(a<sizeof mem-4)memcpy(&v,mem+a,4);return v;}
static uint64_t read64(uint64_t a){uint64_t v=0; if(a<sizeof mem-8)memcpy(&v,mem+a,8);return v;}
#define LD8(a) (mem[(a)])
#define LD32(a) read32(a)
#define LD64(a) read64(a)
#define ST8(a,v) (mem[(a)]=(uint8_t)(v))
static void st32(uint64_t a,uint32_t v){memcpy(mem+a,&v,4);}
#define ST32(a,v) st32(a,v)
static void st64(uint64_t a,uint64_t v){memcpy(mem+a,&v,8);}
static int pointer[5],raw,cursor;
int catherine_config_setting(unsigned field,int change){if(field==PC_RAW_KEY)return raw;if(field==PC_CURSOR_HIDDEN){cursor=change;return change;}return 0;}
int catherine_pc_mouse(unsigned f){return pointer[f];}
void rt_dispatch(Cpu *c){}
void f_700001fd20(Cpu *c){ST8(c->x[0]+0x14,c->x[1]);}
void f_7000277ff0(Cpu *c){}
void f_70002782c0(Cpu *c){}
void f_7000025690(Cpu *c){}
void f_70000258c0(Cpu *c){}
#ifdef PHONE
#include "../../base/guest-timing/include/phone_save_mouse_pc.h"
#define BEGIN catherine_phone_save_begin
#define END catherine_phone_save_end
#define INPUT catherine_phone_save_input
#define WIDGET_OFFSET 0x8b0
#define ROW_SELECT 0x824
#define STATE catherine_phone_save_mouse
#define X 600
#define ROWY 260
#define YESX 640
#define YESY 452
#else
#include "../../base/guest-timing/include/save_mouse_pc.h"
#define BEGIN catherine_save_mouse_begin
#define END catherine_save_mouse_end
#define INPUT catherine_save_mouse_input
#define WIDGET_OFFSET 0xad0
#define ROW_SELECT 0xa0c
#define STATE catherine_save_mouse
#define X 1420
#define ROWY 260
#define YESX 390
#define YESY 382
#endif
#include "../../base/guest-timing/include/save_dialog_mouse_pc.h"
static void fp(uint64_t a,float f){uint32_t b;memcpy(&b,&f,4);st32(a,b);}
int main(void){
 Cpu c={0};uint64_t panel=0x100,widget=panel+WIDGET_OFFSET;const uint32_t confirm=1u<<13;
 pointer[4]=1;BEGIN(panel);raw=1;assert(!(INPUT(&c,panel,confirm)&confirm));raw=32;assert(INPUT(&c,panel,confirm)&confirm);raw=0;
 ST8(panel+ROW_SELECT,0);pointer[0]=X;pointer[1]=ROWY;pointer[2]++;pointer[3]++;assert(INPUT(&c,panel,0)&confirm);
 ST32(widget+8,4);ST8(widget+0x51,20);ST8(widget+0x50,1);
#ifdef PHONE
 fp(widget+0x38,207);fp(widget+0x3c,250);
#else
 fp(widget+0x60,0);fp(widget+0x64,0);st64(widget+0x20,0x4000);
#endif
 raw=1;catherine_save_dialog_begin(widget);assert(!(catherine_save_dialog_input(widget,confirm,1)&confirm));raw=0;
 pointer[0]=YESX;pointer[1]=YESY;pointer[2]++;pointer[3]++;assert(catherine_save_dialog_input(widget,0,1)&confirm);assert(LD8(widget+0x50)==0);
 // Same kind and identical initial clock must consume the next opening click.
 ST8(widget+0x50,1);pointer[3]++;catherine_save_dialog_begin(widget);assert(!(catherine_save_dialog_input(widget,0,1)&confirm));assert(LD8(widget+0x50)==1);
 // A fresh keyboard/controller confirm must survive the first list call after No.
 STATE.suppress_click=1;raw=32;assert(INPUT(&c,panel,confirm)&confirm);raw=0;assert(STATE.suppress_click==0);
 pointer[0]=10;pointer[1]=10;pointer[2]++;pointer[3]++;assert(!(INPUT(&c,panel,confirm)&confirm));
 END(panel+1);assert(catherine_pc_save_widget==widget);END(panel);assert(!catherine_pc_save_widget);
 puts("Opening/closing click isolation, repeated dialog, raw LMB and keyboard/controller parity passed.");return 0;
}

