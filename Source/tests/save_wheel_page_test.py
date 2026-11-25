from pathlib import Path
import json,re,subprocess
R=Path(__file__).resolve().parents[2];B=R/'Build';S=R/'Source'
main0=S/'imports/timing-audit/frame-advance-audit-2026-11-20/phone-state5-unit-1795295619356223200/candidate/main0-phone-recurrence.c'
main16=S/'imports/timing-audit/frame-advance-audit-2026-11-20/audiolifetime-candidate-1795208096580893900/main16-audiolifetime.c'
def body(file,name):
 s=file.read_text();a=s.index('void '+name+'(Cpu *c) {');z=s.index('\nvoid ',a+1);return s[a:z]
shared='\n'.join(body(main0,'f_7000'+x)for x in ['01fc50','01fc60','01fd20'])
M=json.loads((S/'build-manifest.json').read_text());exe=B/'catherine2.exe';results=[]
try:
 for phone in [False,True]:
  names=['025690','0258c0']if phone else['26e8c0','26eb50'];native='\n'.join(body(main0 if phone else main16,'f_7000'+x)for x in names)
  code=r'''#include "cpu.h"
#include "simd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#undef FN_ENTER
#define FN_ENTER(x) ((void)0)
static unsigned char menu[8192],stackmem[8192];static uint64_t vt[32],bound[5];static int wheel,moves,px,py;
#ifdef PHONE
#define FIRST 0x910
#define ROW 0x824
#define CARD 0x5b8
#define STRIDE 0x78
#define DATA 0x18
#include "phone_save_mouse_pc.h"
#define page catherine_phone_save_page
#define begin catherine_phone_save_begin
#define input catherine_phone_save_input
#else
#define FIRST 0xb74
#define ROW 0xa0c
#define CARD 0x608
#define STRIDE 0xa0
#define DATA 0x20
#include "save_mouse_pc.h"
#define page catherine_save_mouse_page
#define begin catherine_save_mouse_begin
#define input catherine_save_mouse_input
#endif
int catherine_pc_mouse(unsigned f){return f==5?wheel:f==2?moves:f==4?1:f==0?px:f==1?py:0;}
int catherine_config_setting(unsigned f,int change){return 0;}
void f_7000277ff0(Cpu*c){}
void f_70002782c0(Cpu*c){}
void f_700001f1b0(Cpu*c){}
void rt_udf(Cpu*c,uint64_t pc){abort();}
void rt_dispatch(Cpu*c){if(c->pc==2){unsigned i=(c->x[0]-(uintptr_t)menu-CARD)/STRIDE;assert(i<5);bound[i]=c->x[1];}}
'''+shared+'\n'+native+r'''
static void reset(unsigned f,unsigned row){memset(menu,0,sizeof menu);vt[0xc8/8]=1;vt[0xe8/8]=2;for(unsigned i=0;i<5;i++){ST64((uintptr_t)menu+CARD+i*STRIDE,(uintptr_t)vt);bound[i]=(uintptr_t)menu+DATA+(f+i)*0x48;}ST8((uintptr_t)menu+FIRST,f);ST8((uintptr_t)menu+ROW,row);ST8((uintptr_t)menu+ROW+1,row);}
int main(void){unsigned cases=0;for(unsigned f=0;f<=15;f++)for(unsigned row=0;row<5;row++)for(unsigned up=0;up<2;up++){
 reset(f,row);Cpu c={0};c.sp=(uintptr_t)stackmem+sizeof stackmem-128;Cpu original=c;unsigned current=(f+row)/5;int blocked=up?current==0:current==3;unsigned target=blocked?f:(up?current-1:current+1)*5;
 page(&c,(uintptr_t)menu,up);assert(LD8((uintptr_t)menu+FIRST)==target);assert(LD8((uintptr_t)menu+ROW)==(blocked?row:0));for(unsigned i=0;i<5;i++)assert(bound[i]==(uintptr_t)menu+DATA+(target+i)*0x48);assert(!memcmp(&c,&original,sizeof c));cases++;}
 reset(5,1);Cpu c={0};c.sp=(uintptr_t)stackmem+sizeof stackmem-128;px=1200;py=600;wheel=0;begin((uintptr_t)menu);input(&c,(uintptr_t)menu,0);wheel=-60;input(&c,(uintptr_t)menu,0);assert(LD8((uintptr_t)menu+FIRST)==5);wheel=-120;moves++;input(&c,(uintptr_t)menu,0);assert(LD8((uintptr_t)menu+FIRST)==10&&LD8((uintptr_t)menu+ROW)==0);for(int i=0;i<100;i++){moves++;px+=(i&1)?-1:1;input(&c,(uintptr_t)menu,0);}assert(LD8((uintptr_t)menu+FIRST)==10&&LD8((uintptr_t)menu+ROW)==0);printf("PASS %u actual native page cases, full card rebinding, slot7 to11, partial wheel steps, and hover priority\n",cases);}
'''
  file=B/'logs/save-wheel-page-fixture.c';file.write_text(code);cmd=[M['compiler'],'-O2','-I'+str(S/'imports/reference/runtime'),'-I'+str(S/'imports/base/guest-timing/include'),*(['-DPHONE']if phone else[]),str(file),'-o',str(exe)];p=subprocess.run(cmd,capture_output=True,text=True);assert not p.returncode,p.stdout+p.stderr;p=subprocess.run([str(exe)],capture_output=True,text=True);assert not p.returncode,p.stdout+p.stderr;results.append(dict(phone=phone,status='PASS',result=p.stdout.strip()));print(p.stdout.strip(),flush=True)
finally:
 if exe.exists():exe.unlink()
(B/'logs/save-wheel-page-tests.json').write_text(json.dumps(results,indent=2))

