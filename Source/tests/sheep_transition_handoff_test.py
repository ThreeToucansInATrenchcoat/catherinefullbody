"""Run the real queued-animation branch across puzzle sheep block handoffs."""
from pathlib import Path
import argparse,json,subprocess

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'Source/imports/timing-audit/frame-advance-audit-2026-11-20/mf14-guest-root-units-1795326299055484900/main3-walknative60/candidate/main3-walknative60.c'
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,default=SOURCE);a=p.parse_args()
s=a.source.read_text(encoding='utf-8')
start=s.index('    { uint64_t ea = x19 + 0x684ull; uint64_t v = LD8(ea); x8 = (uint64_t)(v); }',s.index('L_7000087b8c:;'))
end=s.index('L_7000087bf0:;',start)
branch=s[start:end]
code=r'''
#include "cpu.h"
#include "simd.h"
#include <assert.h>
#include <stdio.h>
static int high=1,tick=0,legacy=0,calls=0;
static uint64_t expected_sp;
static unsigned char model[0x900];
#undef LD8
#define LD8(a) ((uint64_t)(a)==0x7000d06360ull ? legacy : *(uint8_t*)(uintptr_t)(a))
int catherine_high_refresh_active(void){return high;}
unsigned catherine_timing_tick(void){return tick;}
void f_7000087f90(Cpu*c){assert(c->sp==expected_sp);assert(c->x[0]==(uintptr_t)model);assert(c->x[1]==(uintptr_t)model+0x660);++calls;}
static void evaluate(Cpu*c){
 Flags F={0};uint64_t x0=0,x8=0,x9=0,x19=(uintptr_t)model,sp_=expected_sp;
'''+branch+r'''
L_7000087bf0:;
}
static void check(unsigned kind,unsigned variant,unsigned resource,unsigned motion,float delay,int pending,int expected,float expected_delay){
 memset(model,0,sizeof model);calls=0;Cpu c={0};expected_sp=0x12345000;
 ST32((uintptr_t)model+0x5a0,kind);ST32((uintptr_t)model+0x5a4,variant);ST32((uintptr_t)model+0x5a8,resource);
 ST32((uintptr_t)model+0x660,motion);memcpy(model+0x670,&delay,4);ST8((uintptr_t)model+0x684,pending);
 evaluate(&c);
 if(calls!=expected){fprintf(stderr,"kind=%u variant=%u resource=%u motion=%u delay=%g high=%d tick=%d legacy=%d: calls=%d expected=%d\n",kind,variant,resource,motion,delay,high,tick,legacy,calls,expected);exit(2);}
 float actual;memcpy(&actual,model+0x670,4);assert(actual==expected_delay);assert(model[0x684]==(pending&&!expected));
}
int main(void){
 for(unsigned kind=21;kind<=22;++kind){
  high=1;tick=legacy=0;
  check(kind,0,1,0,1,1,1,0); // Next render consumes the block-to-idle handoff.
  check(kind,0,1,0,1,0,0,1); // No pending request.
  check(kind,0,1,0,2,1,0,2); // Longer authored waits keep their clock.
  check(kind,0,1,2,1,1,0,1); // Other clips keep their clock.
  check(kind,1,1,0,1,1,0,1);check(kind,0,2,0,1,1,0,1);
  tick=1;check(kind,0,1,0,1,1,1,0);check(kind,0,1,0,2,1,0,1);
  tick=0;high=0;check(kind,0,1,0,1,1,1,0);
  high=1;legacy=1;check(kind,0,1,0,1,1,1,0);
 }
 high=1;tick=legacy=0;
 for(unsigned kind=0;kind<30;++kind)if(kind!=21&&kind!=22)check(kind,0,1,0,1,1,0,1);
 puts("48 actual-source sheep handoff and animation-delay cases passed.");
}
'''
out=ROOT/'Build/sheep-handoff-test';out.mkdir(exist_ok=True)
fixture=out/'handoff.c';fixture.write_text(code,encoding='utf-8')
compiler=json.loads((ROOT/'Source/build-manifest.json').read_text(encoding='utf-8'))['compiler'];exe=out/'handoff.exe'
subprocess.run([compiler,'-O2','-I'+str(ROOT/'Source/imports/reference/runtime'),str(fixture),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
