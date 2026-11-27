#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <random>
#include <cstring>
#include <cstdio>
#include <immintrin.h>
#include "native_uniform_shapes.h"
struct Range{uint64_t address,size;};
static unsigned cases;
#define CHECK(x) do{if(!(x)){std::printf("FAIL case %u line %d\n",cases,__LINE__);std::abort();}}while(0)
static void compare(const Range* a,const Range* b){const auto before=_mm_getcsr();bool wanted=true;for(unsigned k=0;k<36;++k)if(a[k].size!=b[k].size||bool(a[k].address)!=bool(b[k].address)){wanted=false;break;}CHECK(experimental_uniform_shapes::native(a,b,a+18,b+18)==wanted);CHECK(_mm_getcsr()==before);++cases;}
int main(){
 std::mt19937_64 rng(0x27112704);std::array<Range,36> a,b;const auto saved=_mm_getcsr();
 for(unsigned n=0;n<100000;++n){
  for(unsigned k=0;k<36;++k){a[k]={rng()&3?rng():0,rng()};b[k]={a[k].address?(rng()|1):0,a[k].size};}
  unsigned csr=0x1f80|unsigned(rng()&63)|unsigned((rng()%4)<<13)|((rng()&1)?0x8040:0);_mm_setcsr(csr);compare(a.data(),b.data());
  const unsigned at=n%36;b[at].size^=uint64_t(1)<<(n%64);compare(a.data(),b.data());b[at].size=a[at].size;b[at].address=a[at].address?0:1;compare(a.data(),b.data());
  b[at]=a[at];a[at].size=b[at].size=0;compare(a.data(),b.data());
 }
 SYSTEM_INFO si{};GetSystemInfo(&si);const size_t page=si.dwPageSize;
 for(unsigned offset=0;offset<32;offset+=8){
  auto*left=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));auto*right=static_cast<uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(left&&right);DWORD old;CHECK(VirtualProtect(left+page,page,PAGE_NOACCESS,&old));CHECK(VirtualProtect(right+page,page,PAGE_NOACCESS,&old));
  // The zero-offset case ends exactly at an inaccessible page. Other cases
  // exercise 8/16/24-byte alignment while keeping every loaded byte valid.
  auto*la=reinterpret_cast<Range*>(left+page-sizeof(a)-offset);auto*rb=reinterpret_cast<Range*>(right+page-sizeof(a)-offset);std::memcpy(la,a.data(),sizeof a);std::memcpy(rb,a.data(),sizeof a);compare(la,rb);
  CHECK(VirtualFree(left,0,MEM_RELEASE));CHECK(VirtualFree(right,0,MEM_RELEASE));
 }
 _mm_setcsr(saved);std::printf("PASS %u uniform shapes: all 36 positions, 64-bit sizes/nullness, MXCSR, unaligned/end-of-page loads\n",cases);
}
