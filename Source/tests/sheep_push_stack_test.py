"""Execute the actual action15 branch with checked guest-call boundaries."""
from pathlib import Path
import subprocess, shutil, sys
E = Path(__file__).resolve().parents[2]
S = E/'Source/imports/timing-audit/frame-advance-audit-2026-11-20/type6wait-candidate-1795238710428422200/main8-type6wait.c'
s = S.read_text()
branch = s[s.index('L_70001442e0:;'):s.index('L_700014432c:;')]
B = E/'Build/sheep-push-stack-test'
B.mkdir(exist_ok=True)
cpp = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
struct Cpu { uint64_t x[32]{}, sp{}; };
struct Flags { bool eq; };
enum { CC_EQ };
static bool hold; static unsigned motion, calls;
static uint8_t mem[8192];
static uint32_t LD32(uint64_t p) { return *reinterpret_cast<uint32_t*>(mem+p); }
static void ST8(uint64_t p, uint32_t v) { mem[p]=v; }
static uint32_t fsub32(Flags* f,uint32_t a,uint32_t b) {f->eq=a==b;return a-b;}
static bool fc_sub32(Flags* f,int) {return f->eq;}
static bool catherine_hold_p1_action15_counter(uint64_t) {return hold;}
static void checked(Cpu* c) {
    ++calls;
    if(c->sp!=1024 || c->x[20]!=4096) {
        std::printf("FAIL hold=%d: nested call sp=%llu (expected 1024), x20=%llu (expected 4096)\n",hold,c->sp,c->x[20]);
        std::exit(1);
    }
}
static void f_7000138f80(Cpu* c) {checked(c);}
static void f_70001bfce0(Cpu* c) {checked(c);c->x[0]=0;}
static void f_70001bfbe0(Cpu* c) {checked(c);c->x[0]=motion;}
static int run() {
    Cpu cpu; Cpu* c=&cpu; Flags F{};
    uint64_t x0=4096,x8=0,x9=0,x20=4096,x30=0,sp_=1024;
    c->sp=3952; c->x[20]=1; c->x[19]=128; c->x[28]=256;
    BRANCH
L_7000141960: return 8;
L_7000141958: return 0;
}
int main() {
    for (bool h : {false,true}) for(unsigned m : {8u,9u}) {
        hold=h; motion=m; calls=0;
        int result=run();
        if(result!=(m==8?8:0) || calls!=(h?1:3) || mem[128]!=1) return 2;
    }
    std::puts("PASS: held and ordinary action15 calls preserve stack/register state, motion override and busy flag");
}
'''.replace('#include <cstdlib>', '#include <cstdlib>\n#include <initializer_list>').replace('BRANCH',branch)
(B/'test.cpp').write_text(cpp)
subprocess.run([shutil.which('clang++'),'-std=c++17','-O0',str(B/'test.cpp'),'-o',str(B/'test.exe')],check=True)
sys.exit(subprocess.run([str(B/'test.exe')]).returncode)
