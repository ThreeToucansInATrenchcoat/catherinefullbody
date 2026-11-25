#include "cpu.h"
#include "simd.h"
#include "config_menu_pc.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>
static unsigned char menu[0x400],pad[0x100],settings[0xa000],stack[0x1000],listed[5];
static Cpu cpu;
static int raw_key,profile,devices[2],begin_profile,attached_controllers=2;
static int mouse[5],apply_button,pending,preview,opened,opens,cues,native_adjust,apply_calls;
static float gamma;
typedef struct { unsigned kind,color; float x,y; } Draw;
static Draw draws[16]; static unsigned draw_count;
static uint64_t font[8],vtable[8];
static float realf(uint64_t bits){uint32_t b=(uint32_t)bits;float f;memcpy(&f,&b,4);return f;}
void rt_dispatch(Cpu*c){
 if(c->pc==0x12345000){c->v[0]=catherine_config_pc_bits(96.0f);return;}
 assert(c->pc==0x12345018);assert(draw_count<16);
 draws[draw_count++]=(Draw){0,(unsigned)c->x[1],realf(c->v[0]),realf(c->v[1])};
}
void rt_udf(Cpu*c,uint64_t pc){(void)c;fprintf(stderr,"udf %llx\n",pc);abort();}
void f_700009bed0(Cpu*c){c->x[0]=(uintptr_t)settings;}
void f_700030ae10(Cpu*c){gamma=realf(c->v[0]);}
void f_7000118790(Cpu*c){(void)c;}
void f_70002fd990(Cpu*c){(void)c;}
void f_7000277ff0(Cpu*c){(void)c;++cues;}
void f_700009c1a0(Cpu*c){(void)c;}
void f_70002abad0(Cpu*c){(void)c;}
void f_700008fd00(Cpu*c){assert(draw_count<16);draws[draw_count++]=(Draw){(unsigned)c->x[1],(unsigned)c->x[2],realf(c->v[0]),realf(c->v[1])};}
void f_7000072920(Cpu*c){opened=(unsigned)c->x[0];++opens;}
void f_70000729d0(Cpu*c){(void)c;}
void f_7000072a70(Cpu*c){c->x[0]=0;}
void f_7000072b10(Cpu*c){c->x[0]=1;}
int catherine_pc_mouse(unsigned f){return mouse[f];}
const char*catherine_keyboard_label(unsigned i){(void)i;return "Key";}
int catherine_config_setting(unsigned f,int change){
 if(f==PC_RAW_KEY)return raw_key;
 if(f==PC_INPUT_LAYOUT||f==PC_RAW_PAD)return 0;
 if(f==PC_BINDING_PLAYER){if(change)profile=change-1;return profile;}
 if(f==PC_INPUT_DEVICE_COUNT)return attached_controllers;
 if(f==PC_PLAYER1_DEVICE||f==PC_PLAYER2_DEVICE){unsigned i=f-PC_PLAYER1_DEVICE;if(change)devices[i]=change-1;return devices[i];}
 if(f==PC_BINDINGS_BEGIN){begin_profile=profile;return 1;}
 if(f==PC_PREVIEW)return preview;
 if(f==PC_APPLY_BUTTON)return apply_button;
 if(f==PC_APPLY){if(change){++apply_calls;preview=1;}return pending;}
 if(f==PC_BINDINGS_ACTIVE||f==PC_BINDINGS_DIRTY)return 0;
 if(f==PC_MOVEMENT_PRESET)return 0;
 return 1;
}
#include CONFIG_MENU_NATIVE_FIXTURE
static void reset(unsigned mode,unsigned tab){
 memset(menu,0,sizeof menu);memset(pad,0,sizeof pad);memset(mouse,0,sizeof mouse);
 memset(&catherine_config_pc,0,sizeof catherine_config_pc);memset(&catherine_bindings_ui,0,sizeof catherine_bindings_ui);
 ST32((uintptr_t)menu+4,mode);ST32((uintptr_t)menu+8,tab);ST32((uintptr_t)menu+12,CATHERINE_CONFIG_ACTIVE);
 catherine_config_pc.menu=(uintptr_t)menu;catherine_config_pc.previous_tab=(unsigned)-1;
 raw_key=0;pc_config_arrow_key=pc_config_arrow_repeat=0;pc_config_arrow_wait=0;
 apply_button=pending=preview=0;pc_config_pending_tabs=pc_config_tab_key_held=pc_config_previous_tab_key=0;
 cpu=(Cpu){0};cpu.sp=(uintptr_t)stack+sizeof stack;cpu.x[0]=(uintptr_t)menu;
}
static int step(unsigned button,unsigned repeat){ST32((uintptr_t)pad+4,button);ST32((uintptr_t)pad+0x18,repeat);return catherine_config_pc_input(&cpu,(uintptr_t)menu);}
static int click(int x,int y){mouse[0]=x;mouse[1]=y;++mouse[2];++mouse[3];mouse[4]=1;return step(0,0);}
static void release(void){mouse[4]=0;step(0,0);}
static void rom(FILE*f,uint64_t at,size_t n){assert(fseek(f,(long)(at-0x7000000000ull),SEEK_SET)==0);assert(fread((void*)(uintptr_t)at,1,n,f)==n);}
int main(int argc,char**argv){
 assert(argc==2);FILE*f=fopen(argv[1],"rb");assert(f);
 assert(VirtualAlloc((void*)0x7000719000ull,0x2000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 assert(VirtualAlloc((void*)0x70007a0000ull,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 assert(VirtualAlloc((void*)0x7000cc9000ull,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 assert(VirtualAlloc((void*)0x70031cc000ull,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
 ST32(0x70031cc1e8ull,0x3b888889);
 rom(f,0x7000719000ull,0x2000);rom(f,0x70007a0000ull,0x1000);fclose(f);ST64(0x7000cc9070ull,(uintptr_t)pad);
 for(unsigned mode=0;mode<7;++mode){ST8(0x70007a0530ull+mode,2);ST8(0x70007a047cull+mode,2);}
 unsigned list_cases=0;
 for(unsigned mode=0;mode<7;++mode)for(unsigned tab=0;tab<5;++tab){
  unsigned char original[5];reset(mode,tab);baseline_list(&cpu);memcpy(original,listed,5);
  cpu.sp=(uintptr_t)stack+sizeof stack;cpu.x[0]=(uintptr_t)menu;relocated_list(&cpu);
  if(tab==4&&mode<6){assert(listed[0]==2);assert(!memcmp(listed+1,original,4));}else assert(!memcmp(listed,original,5));
  if(tab==0||tab==1)for(unsigned i=0;i<5;++i)assert(listed[i]!=2);
  ++list_cases;
 }
 for(unsigned mode=0;mode<6;++mode){
  reset(mode,0);ST32((uintptr_t)pad+4,1u<<10);f_70000320a0(&cpu);assert(LD32((uintptr_t)menu+8)==4);
  reset(mode,4);assert(step(0,CATHERINE_CONFIG_RIGHT)==CATHERINE_CONFIG_INPUT_HANDLED);
  unsigned before=LD8((uintptr_t)settings+0x300);assert(step(0,CATHERINE_CONFIG_LEFT)==CATHERINE_CONFIG_INPUT_HANDLED);assert(LD8((uintptr_t)settings+0x300)!=before);
  reset(mode,4);assert(click(600,240)==CATHERINE_CONFIG_INPUT_HANDLED);
  if(mode){release();assert(step(0,CATHERINE_CONFIG_UP)==CATHERINE_CONFIG_INPUT_HANDLED);assert(catherine_config_pc.selected_tab==5);assert(LD32((uintptr_t)menu+0x10)==0);}
 }
 reset(0,4);step(0,0);step(0,CATHERINE_CONFIG_DOWN);assert(!catherine_config_pc_selected((uintptr_t)menu));step(0,CATHERINE_CONFIG_UP);assert(catherine_config_pc_selected((uintptr_t)menu));
 // Every brightness segment is selected exactly, including large backward jumps.
 for(unsigned target=0;target<21;++target){
  reset(0,1);Cpu geometry=cpu;geometry.v[0]=catherine_config_pc_bits(540);geometry.v[1]=catherine_config_pc_bits(750);catherine_config_pc_brightness_geometry((uintptr_t)menu,&geometry,10);
  assert(click(333+24*target,783)==CATHERINE_CONFIG_INPUT_HANDLED);Cpu get=cpu;get.x[1]=3;f_7000034100(&get);assert(get.x[0]==target);
 }
 // Physical keyboard Left/Right drive the actual native brightness handler.
 reset(0,1);step(0,0);catherine_config_pc.selected_tab=0;ST32((uintptr_t)menu+0x10,0);
 unsigned char middle=LD8(0x70007a05aeull);ST8((uintptr_t)settings+0x2fb,middle);
 raw_key=0x25;assert(step(0,0)==CATHERINE_CONFIG_INPUT_NATIVE);cpu.x[0]=(uintptr_t)menu;f_7000031d70(&cpu);Cpu get_left=cpu;get_left.x[1]=3;f_7000034100(&get_left);assert(get_left.x[0]==9);
 raw_key=0;step(0,0);raw_key=0x27;step(0,0);cpu.x[0]=(uintptr_t)menu;f_7000031d70(&cpu);Cpu get_right=cpu;get_right.x[1]=3;f_7000034100(&get_right);assert(get_right.x[0]==10);
 raw_key=0;step(0,0);
 // Display Apply is gone; normal Confirm still starts the unified preview/save.
 reset(0,1);pending=1;int oldopens=opens;apply_button=1;assert(step(CATHERINE_CONFIG_CONFIRM,0)==CATHERINE_CONFIG_INPUT_HANDLED);assert(opens==oldopens&&!apply_calls);apply_button=0;step(0,0);assert(step(CATHERINE_CONFIG_CONFIRM,0)==CATHERINE_CONFIG_INPUT_HANDLED);assert(apply_calls==1&&catherine_bindings_ui.save_applying);
 for(unsigned tab=0;tab<5;++tab)for(unsigned action=1;action<=3;++action){
  reset(0,tab);unsigned x=action==1?1540:action==2?1740:1300;assert(click(x,1000)==CATHERINE_CONFIG_INPUT_HANDLED);
  assert(opened==(action==1?0x25:action==2?0x24:0x27));assert(LD32((uintptr_t)menu+0x14)==(action==1?1:action==2?0:2));assert(LD32((uintptr_t)menu+12)==CATHERINE_CONFIG_PROMPT);
 }
 // Each player dropdown changes only its assignment; each button enters its profile.
 reset(0,2);ST8((uintptr_t)menu+0x3c,255);
 for(unsigned player=0;player<2;++player)for(unsigned row=0;row<2;++row){
  catherine_config_pc.selected_tab=3;catherine_config_pc.control_player=player;catherine_config_pc.row=row;
  assert(catherine_config_page_alpha((uintptr_t)menu)==255);
  assert(catherine_config_row_highlight_alpha((uintptr_t)menu)==0);
 }
 catherine_config_pc.selected_tab=0;assert(catherine_config_row_highlight_alpha((uintptr_t)menu)==255);
 reset(0,2);assert(click(1150,690)==CATHERINE_CONFIG_INPUT_HANDLED);assert(catherine_config_pc.device_open==2);release();
 // The open list is anchored below the field, not near the page header.
 assert(click(1150,800)==CATHERINE_CONFIG_INPUT_HANDLED);assert(devices[1]==1&&devices[0]==0);release();
 assert(click(1150,690)==CATHERINE_CONFIG_INPUT_HANDLED);release();
 assert(click(1150,382)==CATHERINE_CONFIG_INPUT_HANDLED);assert(!catherine_config_pc.device_open&&devices[1]==1);release();
 // Keyboard-only leaves P2 at None, including unplugging while a list is open.
 attached_controllers=0;assert(pc_control_device_count(0)==2&&pc_control_device_count(1)==1);
 assert(click(1150,690)==CATHERINE_CONFIG_INPUT_HANDLED);release();
 assert(catherine_config_pc.device_row==0);assert(click(1150,750)==CATHERINE_CONFIG_INPUT_HANDLED);assert(devices[1]==0);release();
 attached_controllers=2;assert(click(1150,690)==CATHERINE_CONFIG_INPUT_HANDLED);release();
 catherine_config_pc.device_row=3;catherine_config_pc.device_scroll=1;attached_controllers=0;step(0,0);
 assert(catherine_config_pc.device_row==0&&catherine_config_pc.device_scroll==0);assert(click(1150,750)==CATHERINE_CONFIG_INPUT_HANDLED);release();attached_controllers=2;
 assert(click(1150,785)==CATHERINE_CONFIG_INPUT_HANDLED);assert(catherine_bindings_ui.open&&begin_profile==1);pc_bindings_close();assert(profile==0);release();
 assert(click(500,785)==CATHERINE_CONFIG_INPUT_HANDLED);assert(catherine_bindings_ui.open&&begin_profile==0);pc_bindings_close();release();
 // Hover changes foreground text only. Every native position, glyph and shadow matches.
 font[0]=(uintptr_t)vtable;vtable[3]=0x12345018;vtable[6]=0x12345000;
 for(unsigned tab=0;tab<5;++tab){
  reset(0,tab);ST8((uintptr_t)menu+0x3c,255);ST32((uintptr_t)menu+0x38,0x3f800000);ST64((uintptr_t)menu+0x2c0,(uintptr_t)font);ST64((uintptr_t)menu+0x2c8,(uintptr_t)font);ST64((uintptr_t)menu+0x2d0,(uintptr_t)font);
  draw_count=0;cpu.x[0]=(uintptr_t)menu;f_70000329d0(&cpu);assert(draw_count==9);Draw baseline[9];memcpy(baseline,draws,sizeof baseline);
  for(unsigned action=1;action<=3;++action){mouse[4]=1;mouse[0]=action==1?1540:action==2?1740:1300;mouse[1]=1000;draw_count=0;cpu.x[0]=(uintptr_t)menu;f_70000329d0(&cpu);assert(draw_count==9);
   for(unsigned i=0;i<9;++i){assert(draws[i].kind==baseline[i].kind&&draws[i].x==baseline[i].x&&draws[i].y==baseline[i].y);unsigned changed=action==2?1:action==1?4:7;assert(draws[i].color==(i==changed?0xff3698ff:baseline[i].color));}
  }
 }
 reset(0,2);catherine_bindings_ui.open=1;catherine_bindings_ui.focus.row[0]=1;mouse[0]=1300;mouse[1]=1000;mouse[2]=1;mouse[4]=1;assert(step(0,0)==CATHERINE_CONFIG_INPUT_HANDLED);assert(!catherine_bindings_ui.focus.footer&&!catherine_bindings_ui.capturing);
 catherine_bindings_ui.capturing=1;assert(catherine_config_pc_footer_text_color((uintptr_t)menu,3,0xffffffff)==0xffffffff);
 printf("PASS: %u native row lists/all modes; Audio navigation; 21 exact native brightness targets; Display Confirm without Apply; all parent footer clicks and text-only hover; child hover/capture.\n",list_cases);
}
