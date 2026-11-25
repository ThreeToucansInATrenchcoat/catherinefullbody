#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <cassert>
#include <cstdio>
#include <cstdint>
static uint64_t frame=1,visual=0;
extern "C" uint64_t catherine_camera_render_clock(){return visual;}
extern "C" uint64_t catherine_announcement_frame_clock(){return frame<<32;}
namespace fixture {std::map<uint64_t,std::vector<unsigned char>> memory;
void region(uint64_t p,size_t n){memory[p].resize(n);}
bool copy(uint64_t p,void*out,size_t n,bool write=false){auto i=memory.upper_bound(p);if(i==memory.begin())return false;--i;size_t off=p-i->first;if(off+n>i->second.size())return false;if(write)std::memcpy(i->second.data()+off,out,n);else std::memcpy(out,i->second.data()+off,n);return true;}
template<class T>void put(uint64_t p,const T&v){assert(copy(p,(void*)&v,sizeof v,true));}}
namespace c02_bridge {bool read_bytes(uint64_t p,void*out,size_t n){return fixture::copy(p,out,n);}bool write_bytes(uint64_t p,const void*in,size_t n){return fixture::copy(p,(void*)in,n,true);}}
#include "babel_node_bridge.inc"
using namespace catherine::visual;using namespace catherine::visual::babel;
Matrix translation(float x){return {1,0,0,x,0,1,0,0,0,0,1,0,0,0,0,1};}
void clock_at(unsigned tick,float phase){uint32_t b;std::memcpy(&b,&phase,4);visual=(uint64_t(tick)<<32)|b|0x80000000u;}
void setup(unsigned player,uint64_t base,unsigned id){using namespace fixture;
 region(base,0x4000);put(0x10000+8*player,base);put(base,uint64_t(0x7000abff90));put(base+0x20,base+0x1000);
 put(base+0x1000,uint64_t(0x70009f5a10));uint32_t ids[]={id,30,1,0};assert(copy(base+0x15a0,ids,16,true));put(base+0x1468,base+0x2000);put(base+0x14a8,base+0x3000);put(base+0x2138,base+0x2800);put(base+0x2800,uint32_t(126));
 put(base+0x3020,transpose(translation(900)));put(base+0x3120,transpose(translation(910)));
}
int main(){using namespace fixture;using namespace babel_bridge;
 _putenv_s("CATHERINE_BABEL_NODE_INTERPOLATION","1");region(0x7000ccadf0,8);region(0x10000,16);put(0x7000ccadf0,uint64_t(0x10000));setup(0,0x20000,14);setup(1,0x30000,15);
 clock_at(1,0);world(0x22000,0x23100);world(0x32000,0x33100);
 frame=2;clock_at(1,.5f);world(0x22000,0x23100);world(0x32000,0x33100);
 frame=3;clock_at(2,0);put(0x23120,transpose(translation(920)));put(0x33120,transpose(translation(940)));world(0x22000,0x23100);world(0x32000,0x33100);
 frame=4;clock_at(2,.5f);uint64_t a=world(0x22000,0x23100);Matrix first;std::memcpy(first.data(),(void*)a,64);assert(std::abs(transpose(first)[3]-915)<.001f);
 uint64_t b=world(0x32000,0x33100);Matrix second;std::memcpy(second.data(),(void*)b,64);assert(std::abs(transpose(second)[3]-925)<.001f);
 Matrix authoritative;assert(read(0x23120,authoritative));assert(transpose(authoritative)[3]==920);assert(world(0x99999,0x23100)==0x23120);
 // Exact native rigid -> transpose -> GPU command proof, and unowned overwrite.
 uint64_t mesh=0x40000,dest=0x41000,upload=0x42000;region(mesh,0x200);region(dest,64);region(upload,64);put(mesh+8,uint64_t(0x23100));put(mesh+0x1c0,uint64_t(0x22000));put(mesh+0x1b8,dest);put(mesh+0x20,uint32_t(0));put(dest,authoritative);
 catherine_babel_rigid_copied(mesh,0,dest);c3_loaded(dest);Matrix native;assert(read(dest,native));put(upload,transpose(native));c3_uploaded(upload,0x50000,64);assert(force_bind());buffer_address(0x50000,0x60000);assert(record_owned(0x60040,64));assert(!force_bind());
 c3_loaded(0);c3_uploaded(upload,0x50000,64);assert(force_bind());buffer_address(0x50000,0x60000);assert(!record_owned(0x60040,64));
 // A lateral hanging hop rebases the grid anchor while root motion cancels
 // that step. Native world motion stays fast and continuous. Independently
 // smoothing the grid anchor used to move the whole character back 150 units.
 put(0x2018c,uint32_t(0x80));
 for(unsigned f=5;f<=12;++f){
  frame=f;clock_at(3+(f-5)/2,(f-5)%2*.5f);
  const float x=920.f+(f-5)*5.f;
  put(0x23020,transpose(translation(f<9?900:1050)));
  put(0x23120,transpose(translation(x)));
  uint64_t result=world(0x22000,0x23100);
  Matrix actual{};if(result==0x23120)assert(read(result,actual));else std::memcpy(actual.data(),(void*)result,64);
  if(f>=7)assert(std::abs(transpose(actual)[3]-x)<.001f);
 }
 // A faster changing ground pose must not bypass a held placement channel.
 for(unsigned f=13;f<=20;++f){
  frame=f;clock_at(7+(f-13)/2,(f-13)%2*.5f);
  const float anchor=900.f+10.f*((f-13)/2),angle=.1f*(f-13);
  Matrix local={std::cos(angle),0,std::sin(angle),10,0,1,0,0,-std::sin(angle),0,std::cos(angle),0,0,0,0,1},native_world{};
  auto placement=translation(anchor);multiply_affine(placement.data(),local.data(),native_world.data());
  put(0x33020,transpose(placement));put(0x33120,transpose(native_world));
  uint64_t result=world(0x32000,0x33100);Matrix actual{};
  if(result==0x33120)assert(read(result,actual));else std::memcpy(actual.data(),(void*)result,64);
  if(f>=16){float expected=anchor+10.f-((f-13)%2?5.f:10.f);assert(std::abs(transpose(actual)[3]-expected)<.001f);}
 }
 // Returning from a hanging world history to ground-local coordinates must
 // reset both histories, rather than blending matrices from different spaces.
 frame=21;clock_at(11,0);put(0x2018c,uint32_t(0));put(0x23020,transpose(translation(2000)));put(0x23120,transpose(translation(2010)));
 assert(world(0x22000,0x23100)==0x23120);
 // A different player model at the same registry slot cannot inherit a pose.
 put(0x20020,uint64_t(0));++frame;assert(world(0x22000,0x23100)==0x23120);
 std::puts("Babel native owner, two players, immutable simulation, coherent rigid upload and overwrite rejection passed");
}
