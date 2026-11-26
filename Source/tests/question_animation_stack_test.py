"""Exercise the actual question ornament updates on skipped animation ticks."""
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "Source/imports/timing-audit/frame-advance-audit-2026-11-20/sheep-question-1795346029478118600/main5-candidate.c"
OUT = ROOT / "Build/question-stack-test"
OUT.mkdir(exist_ok=True)
source = SOURCE.read_text()
functions = []
for name in ("f_70000f2760", "f_70000f29a0"):
    start = source.index("void " + name + "(Cpu *c) {")
    functions.append(source[start:source.index("\nvoid ", start + 1)])
native = "\n".join(functions)
calls = sorted(set(re.findall(r"\b(f_7000\w+)\(c\)", native)))
code = r'''
#include "cpu.h"
#include "simd.h"
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#undef FN_ENTER
#define FN_ENTER(x) ((void)0)
static unsigned char stackmem[4096],object[256],strategy[128],app[128];
static uint64_t vtable[16], expected_sp;
static int calls, strategy4;
#undef LD8
#define LD8(a) ((uint64_t)(a)==0x7000d06360ull ? 0 : *(uint8_t*)(uintptr_t)(a))
int catherine_qa_art_tick(void){return 0;}
int catherine_high_refresh_active(void){return 1;}
unsigned catherine_timing_tick(void){return 0;}
void rt_udf(Cpu *c,uint64_t pc){abort();}
void rt_dispatch(Cpu *c){
    if(c->sp!=expected_sp){fprintf(stderr,"strategy%d: child entered at wrong stack position\n",strategy4?4:3);exit(2);}
    ++calls;
    /* A real child uses memory below its incoming SP. It must not overwrite
       the parent's saved x19/x20, as happened in the results-chart crash. */
    memset((void*)(uintptr_t)(c->sp-96),0xa5,96);
}
void f_700009c1a0(Cpu *c){c->x[0]=(uintptr_t)app;}
'''
code += "\n".join("void " + name + "(Cpu *c){abort();}" for name in calls if name != "f_700009c1a0")
code += native
code += r'''
int main(void){
 for(strategy4=0;strategy4<=1;++strategy4){
  Cpu c={0};memset(stackmem,0,sizeof stackmem);memset(object,0,sizeof object);
  ST64((uintptr_t)strategy,strategy4?0x70009fcc08ull:0x70009fcbd8ull);
  ST64((uintptr_t)object,(uintptr_t)vtable);vtable[8]=0x70000f1680ull;
  ST32((uintptr_t)object+0x90,4);ST32((uintptr_t)object+0x94,6);
  c.sp=(uintptr_t)stackmem+sizeof stackmem;c.x[0]=(uintptr_t)strategy;c.x[1]=(uintptr_t)object;
  for(int i=19;i<=30;++i)c.x[i]=0x12340000+i;
  for(int i=8;i<=15;++i)c.v[i]=0x56780000+i;
  Cpu original=c;expected_sp=c.sp-80;
  if(strategy4)f_70000f29a0(&c);else f_70000f2760(&c);
  assert(c.sp==original.sp);
  for(int i=19;i<=30;++i)assert(c.x[i]==original.x[i]);
  for(int i=8;i<=15;++i)assert(c.v[i]==original.v[i]);
 }
 assert(calls==2);puts("Both question animation skip paths preserve the stack and saved registers.");
}
'''
fixture = OUT / "question_stack.c"
fixture.write_text(code)
compiler = json.loads((ROOT / "Source/build-manifest.json").read_text())["compiler"]
exe = OUT / "question_stack.exe"
subprocess.run([compiler, "-O2", "-I" + str(ROOT / "Source/imports/reference/runtime"), str(fixture), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
