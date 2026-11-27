#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned char memory[0x1000000];
#define AT(a) (memory+((uint64_t)(a)&0xffffff))
#define LD8(a) (*(uint8_t*)AT(a))
#define LD16(a) (*(uint16_t*)AT(a))
#define LD32(a) (*(uint32_t*)AT(a))
#define LD64(a) (*(uint64_t*)AT(a))
#define ST8(a,v) (LD8(a)=(v))
#define ST32(a,v) (LD32(a)=(v))
#define ST64(a,v) (LD64(a)=(v))
typedef struct { uint64_t x[32],sp; } Cpu;
#include "puzzle_pause_mouse.h"
#include "landing_mouse_pc.h"
static int mouse[8],raw,owners,hidden,sounds,skip_enabled;
int catherine_pc_mouse(unsigned f){return f<8?mouse[f]:0;}
int catherine_config_setting(unsigned f,int v){if(f==PC_RAW_KEY)return raw;if(f==PC_CURSOR_HIDDEN)hidden=v&&!owners;return 0;}
void catherine_menu_cursor_set(unsigned o,int v){if(v)owners|=o;else owners&=~o;}
int catherine_menu_cursor_visible(void){return owners!=0;}
void f_7000277ff0(Cpu*c){++sounds;}
void f_700021a0e0(Cpu*c){c->x[0]=skip_enabled;}
void f_70001af290(Cpu*c){c->x[0]=1;}
void f_700021a310(Cpu*c){}
void f_7000088650(Cpu*c){c->x[0]=c->x[1];}
void f_700007ff60(Cpu*c){
    /* The projection scratch lies below the input handler's stack frame. */
    assert(c->sp==0x10000-0x60);
    assert(c->x[1]==c->sp && c->x[2]==c->sp+0x30 && c->x[3]==1);
    ST32(c->x[2]+8,LD32(c->x[1]+0x10));
    ST32(c->x[2]+12,LD32(c->x[1]+0x14));c->x[0]=1;
}
static void putf(uint64_t a,float f){uint32_t b;memcpy(&b,&f,4);ST32(a,b);}
static void node(uint64_t n,float x,float y,uint64_t parent){
    ST64(n+8,parent);putf(n+0xa0,x);putf(n+0xa4,y);putf(n+0xbc,1);
    for(unsigned i=0;i<3;++i)putf(n+0xc0+4*i,1);
}
int main(void){
    Cpu c={0};c.sp=0x10000;uint32_t pressed;
    CatherineMenuPointer p={0};mouse[7]=1;raw=1;mouse[3]=1;
    catherine_menu_pointer_show(&p,1,1);assert(owners==1&&!hidden);
    pressed=0x2000;assert(!(catherine_menu_pointer_poll(&p,&pressed)&2)&&!pressed);
    mouse[7]=0;raw=0;catherine_menu_pointer_poll(&p,&pressed);
    mouse[3]++;mouse[7]=1;raw=1;pressed=0x2000;
    assert((catherine_menu_pointer_poll(&p,&pressed)&2)&&!pressed);
    catherine_menu_cursor_set(2,1);catherine_menu_pointer_show(&p,1,0);assert(owners==2&&!hidden);
    catherine_menu_cursor_set(2,0);
    mouse[7]=0;raw=0;catherine_menu_pointer_show(&p,1,1);catherine_menu_pointer_show(&p,1,0);assert(hidden);

    const uint64_t menu=0x20000;
    catherine_menu_pointer_show(&catherine_landing_pointer,2,1);
    mouse[4]=1;mouse[0]=960;mouse[1]=342;mouse[2]++;
    assert(!catherine_landing_mouse(&c,menu,0,0));assert(LD32(menu+0x298)==1);
    mouse[3]++;assert(catherine_landing_mouse(&c,menu,0,0)&0x2000);
    mouse[0]=100;mouse[3]++;assert(!(catherine_landing_mouse(&c,menu,0x2000,0)&0x2000));
    mouse[6]++;assert(catherine_landing_mouse(&c,menu,0,0)&0x4000);

    /* Native label transforms, including a parent offset, drive pause hit rows. */
    catherine_puzzle_pause_view=1;ST64(menu+8,1);ST64(0x7000ccb5d0ull,0x30000);
    node(0x40000,100,100,0);
    for(unsigned row=0;row<5;++row){
        /* The description nodes overlap below the menu. Hover must follow the
         * selectable-label table used by the native pause input handler. */
        ST8(0x70007cd1e4ull+row,row+5);
        ST64(0x30000+(row+5)*8,0x45000+row*0x200);
        node(0x45000+row*0x200,860,600,0x40000);
        ST8(0x70007cd22full+row,row);ST64(0x30000+row*8,0x41000+row*0x200);
        unsigned visual_row=row==3?4:row==4?3:row;
        node(0x41000+row*0x200,860,200+visual_row*50,0x40000);
    }
    assert(catherine_pause_mouse_row(&c,menu,0,960,400)==2);
    assert(catherine_pause_mouse_row(&c,menu,0,960,435)==3);
    assert(catherine_pause_mouse_row(&c,menu,0,960,500)==4);
    assert(catherine_pause_mouse_row(&c,menu,0,100,400)==-1);
    catherine_menu_pointer_show(&catherine_puzzle_pause_pointer,1,1);
    mouse[0]=960;mouse[1]=450;mouse[2]++;mouse[3]++;
    ST8(menu+0x18,0);
    assert(!(catherine_puzzle_pause_mouse(&c,menu,0,0)&0x2000));assert(LD8(menu+0x18)==0);
    mouse[1]=500;mouse[2]++;
    assert(!(catherine_puzzle_pause_mouse(&c,menu,0,0)&0x2000));assert(LD8(menu+0x18)==4);
    mouse[1]=400;mouse[2]++;mouse[3]++;
    assert(catherine_puzzle_pause_mouse(&c,menu,0,0)&0x2000);assert(LD8(menu+0x18)==2);
    /* An idle P2 pause object must not consume P1's movement/click serials. */
    const uint64_t inactive=0x50000;
    ST8(menu+0x34c,3);ST8(menu+0x34d,1);
    catherine_puzzle_pause_update(menu);
    mouse[0]=960;mouse[1]=350;mouse[2]++;mouse[3]++;
    catherine_puzzle_pause_update(inactive);
    assert(catherine_puzzle_pause_owner==menu && catherine_puzzle_pause_pointer.active);
    assert(catherine_puzzle_pause_mouse(&c,menu,0,0)&0x2000);
    assert(LD8(menu+0x18)==1);
    catherine_puzzle_pause_hide(inactive);
    assert(catherine_puzzle_pause_pointer.active);
    catherine_puzzle_pause_hide(menu);
    assert(!catherine_puzzle_pause_pointer.active);
    /* Co-op rows remain interactive without any projectable model nodes. */
    catherine_puzzle_pause_view=0;
    assert(catherine_pause_mouse_row(&c,menu,1,1000,360)==0);
    assert(catherine_pause_mouse_row(&c,menu,1,1000,456)==2);
    assert(catherine_pause_mouse_row(&c,menu,1,1000,504)==3);
    assert(catherine_pause_mouse_row(&c,menu,1,1000,552)==4);
    assert(catherine_pause_mouse_row(&c,menu,1,500,456)==-1);
    catherine_puzzle_pause_update(menu);
    mouse[0]=1000;mouse[1]=456;mouse[2]++;
    assert(!(catherine_puzzle_pause_mouse(&c,menu,1,0)&0x2000));
    assert(LD8(menu+0x18)==2);
    mouse[3]++;
    assert(catherine_puzzle_pause_mouse(&c,menu,1,0)&0x2000);
    mouse[1]=408;mouse[2]++;mouse[3]++;
    assert(!(catherine_puzzle_pause_mouse(&c,menu,1,0)&0x2000));
    assert(LD8(menu+0x18)==2); /* Grey Retry cannot be selected. */
    /* The emulated pad edge must not immediately confirm the default No. */
    catherine_pause_mouse_consumed=mouse[3];
    raw=1;mouse[7]=1;
    assert(!(catherine_pause_filter_click(0x2000)&0x2000));
    raw=0;mouse[7]=0;
    assert(!(catherine_pause_filter_click(0x2000)&0x2000));
    raw=32;
    assert(catherine_pause_filter_click(0x2000)&0x2000);
    raw=0;
    /* Confirmations retain visibility and consume fresh pointer events. */
    ST8(menu+0x340,1);ST32(menu+0x344,1);
    catherine_puzzle_pause_update(menu);
    mouse[2]++;
    catherine_puzzle_pause_update(menu); /* Must not swallow hover each frame. */
    assert(catherine_puzzle_pause_pointer.move != (unsigned)mouse[2]);
    assert(owners & 1);
    assert(catherine_pause_confirmation_input(menu,0,0)==0x10000);
    ST32(menu+0x344,0); /* Native up handler selects Yes. */
    mouse[3]++;raw=1;mouse[7]=1;
    assert(catherine_pause_confirmation_input(menu,0,0)&0x2000);
    assert(!(catherine_pause_confirmation_input(menu,0,0x2000)&0x2000));
    mouse[7]=0;raw=0;catherine_pause_confirmation_input(menu,0,0);
    mouse[3]++;mouse[7]=1;raw=1;
    assert(catherine_pause_confirmation_input(menu,1,0)==0x20000);
    ST32(menu+0x344,1);
    assert(catherine_pause_confirmation_input(menu,1,0)&0x2000);
    mouse[6]++;
    assert(catherine_pause_confirmation_input(menu,0,0x2000)==0x4000);
    for(unsigned sub=2;sub<=3;++sub){
        ST8(menu+0x340,sub);ST32(menu+0x348,1);
        catherine_puzzle_pause_update(menu);
        mouse[7]=0;raw=0;catherine_pause_confirmation_input(menu,0,0);
        mouse[2]++;
        assert(catherine_pause_confirmation_input(menu,0,0)==0x10000);
        ST32(menu+0x348,0);
        mouse[3]++;
        assert(catherine_pause_confirmation_input(menu,0,0)&0x2000);
        mouse[6]++;
        assert(catherine_pause_confirmation_input(menu,0,0)==0x4000);
        assert(owners & 1);
    }
    catherine_puzzle_pause_hide(menu);assert(!(owners & 1));
    assert(c.sp==0x10000);
    puts("Menu mouse tests passed: visibility ownership, hover, fresh clicks, outside clicks, back, disabled Skip and projected rows.");
}
