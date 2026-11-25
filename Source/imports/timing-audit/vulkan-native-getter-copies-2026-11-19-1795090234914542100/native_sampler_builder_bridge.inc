#include "native_sampler_builder_reader.h"
extern "C" void f_70054257f0(void*);
extern "C" void f_7005425810(void*);
extern "C" void f_7005425830(void*);
extern "C" void f_7005425850(void*);
extern "C" void f_7005425860(void*);
extern "C" void f_7005425880(void*);
extern "C" void f_7005425910(void*);
extern "C" void f_7005425920(void*);

namespace {
struct SamplerBuilderShadow{
 alignas(16) uint8_t input[72],scratch[16],joined[88];uint64_t builder=0,output=0,actual=0;size_t size=0;
 SamplerBuilderShadow(uint64_t b,uint64_t o){
  if(o&&catherine::native_view::overlaps(b,72,o,16)){actual=std::min(b,o);size=size_t(std::max(b+72,o+16)-actual);std::memcpy(joined,reinterpret_cast<const void*>(actual),size);builder=uint64_t(joined)+(b-actual);output=uint64_t(joined)+(o-actual);}
  else{std::memcpy(input,reinterpret_cast<const void*>(b),72);builder=uint64_t(input);if(o){std::memcpy(scratch,reinterpret_cast<const void*>(o),16);output=uint64_t(scratch);}}
 }
 bool equal(uint64_t b,uint64_t o)const{return size?!std::memcmp(joined,reinterpret_cast<const void*>(actual),size):!std::memcmp(input,reinterpret_cast<const void*>(b),72)&&(!o||!std::memcmp(scratch,reinterpret_cast<const void*>(o),16));}
};
}
extern "C" int catherine_native_sampler_builder(nx::CPU* cpu,unsigned kind,uint64_t address,uint64_t builder,uint64_t output,int compare,uint64_t* result){
 using namespace catherine::native_sampler;using catherine::native_view::overlaps;
 if(!cpu||!active_guest||!result||kind>=8||address!=addresses[kind]||!builder||builder>UINT64_MAX-72||overlaps(builder,72,uint64_t(cpu),sizeof *cpu)||overlaps(builder,72,uint64_t(active_guest),sizeof *active_guest))return 0;
 const auto nout=outputs[kind];if(nout&&(!output||output>UINT64_MAX-16||overlaps(output,16,uint64_t(cpu),sizeof *cpu)||overlaps(output,16,uint64_t(active_guest),sizeof *active_guest)))return 0;
 to_guest(*cpu,*active_guest);active_guest->x[0]=builder;for(unsigned i=0;i<nout;++i)active_guest->x[i+1]=output+4*i;
 active_guest->pc=address;
 if(compare){SamplerBuilderShadow shadow(builder,nout?output:0);GuestCpu oracle=*active_guest;oracle.x[0]=shadow.builder;for(unsigned i=0;i<nout;++i)oracle.x[1+i]=shadow.output+4*i;
  static void(*const original[])(void*)={f_70054257f0,f_7005425810,f_7005425830,f_7005425850,f_7005425860,f_7005425880,f_7005425910,f_7005425920};original[kind](&oracle);execute(*active_guest,kind);
  if(kind!=7)oracle.x[0]=active_guest->x[0];for(unsigned i=0;i<nout;++i)oracle.x[1+i]=active_guest->x[1+i];
  if(std::memcmp(&oracle,active_guest,sizeof oracle)||!shadow.equal(builder,nout?output:0))nx::fatal("Native sampler getter differs from SDK kind %u",kind);
 }else execute(*active_guest,kind);
 if(kind==3||kind==6)std::memcpy(&cpu->v[0],active_guest->v[0],16);*result=active_guest->x[0];
 static std::atomic<uint64_t> calls[8];const auto n=calls[kind].fetch_add(1,std::memory_order_relaxed)+1;if(n==1||n%1000==0)nx::log("Native sampler builder kind %u: %llu mode %d",kind,(unsigned long long)n,compare?2:1);return 1;
}
