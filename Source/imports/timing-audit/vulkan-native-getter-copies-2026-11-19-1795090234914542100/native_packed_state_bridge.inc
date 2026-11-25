#include "native_packed_state_reader.h"
extern "C" void f_7005426370(void*);
extern "C" void f_7005426390(void*);
extern "C" void f_70054263d0(void*);
extern "C" void f_70054263e0(void*);
extern "C" void f_7005426110(void*);
extern "C" void f_7005426120(void*);
extern "C" void f_7005426160(void*);
extern "C" void f_70054262f0(void*);
extern "C" void f_7005426300(void*);
extern "C" void f_7005426310(void*);
extern "C" void f_7005426220(void*);
extern "C" void f_7005426520(void*);
extern "C" void f_7005426530(void*);
extern "C" void f_7005426540(void*);
extern "C" void f_7005426550(void*);
extern "C" void f_7005426560(void*);
extern "C" void f_7005426580(void*);
extern "C" void f_7005426660(void*);
extern "C" void f_7005426670(void*);
extern "C" void f_7005426680(void*);
extern "C" void f_7005426690(void*);
extern "C" void f_7005426760(void*);

namespace {
struct PackedShadow{
 alignas(16) uint8_t input[24],scratch[16],joined[48];uint64_t source=0,output=0,actual=0;size_t bytes=0,size=0,out_bytes=0;
 PackedShadow(uint64_t s,size_t n,uint64_t o,size_t on):bytes(n),out_bytes(on){
  if(o&&catherine::native_view::overlaps(s,n,o,on)){actual=std::min(s,o);size=size_t(std::max(s+n,o+on)-actual);std::memcpy(joined,reinterpret_cast<const void*>(actual),size);source=uint64_t(joined)+(s-actual);output=uint64_t(joined)+(o-actual);}
  else{std::memcpy(input,reinterpret_cast<const void*>(s),n);source=uint64_t(input);if(o){std::memcpy(scratch,reinterpret_cast<const void*>(o),on);output=uint64_t(scratch);}}
 }
 bool equal(uint64_t s,uint64_t o)const{return size?!std::memcmp(joined,reinterpret_cast<const void*>(actual),size):!std::memcmp(input,reinterpret_cast<const void*>(s),bytes)&&(!o||!std::memcmp(scratch,reinterpret_cast<const void*>(o),out_bytes));}
};
}
extern "C" int catherine_native_packed_state(nx::CPU* cpu,unsigned kind,uint64_t address,const uint64_t* args,unsigned argc,int compare,uint64_t* result){
 using namespace catherine::native_packed;using catherine::native_view::overlaps;
 if(!cpu||!active_guest||!result||!args||kind>=count||address!=addresses[kind]||argc!=meta[kind].argc||argc>6)return 0;
 uint64_t parameters[6]{};std::memcpy(parameters,args,argc*8);const auto m=meta[kind];const auto source=parameters[0],output=m.outputs?parameters[m.start]:0;
 if(!source||source>UINT64_MAX-m.bytes||overlaps(source,m.bytes,uint64_t(cpu),sizeof *cpu)||overlaps(source,m.bytes,uint64_t(active_guest),sizeof *active_guest))return 0;
 const size_t output_bytes=m.outputs?(m.outputs-1)*m.stride+(kind==0?8:kind==10?1:4):0;
 if(m.outputs){if(!output||output>UINT64_MAX-output_bytes||overlaps(output,output_bytes,uint64_t(cpu),sizeof *cpu)||overlaps(output,output_bytes,uint64_t(active_guest),sizeof *active_guest))return 0;for(unsigned i=0;i<m.outputs;++i)if(parameters[m.start+i]!=output+m.stride*i)return 0;}
 to_guest(*cpu,*active_guest);std::memcpy(active_guest->x,parameters,argc*8);active_guest->pc=address;
 if(compare){PackedShadow shadow(source,m.bytes,output,output_bytes);GuestCpu oracle=*active_guest;oracle.x[0]=shadow.source;for(unsigned i=0;i<m.outputs;++i)oracle.x[m.start+i]=shadow.output+i*m.stride;
  static void(*const original[])(void*)={f_7005426370,f_7005426390,f_70054263d0,f_70054263e0,f_7005426110,f_7005426120,f_7005426160,f_70054262f0,f_7005426300,f_7005426310,f_7005426220,f_7005426520,f_7005426530,f_7005426540,f_7005426550,f_7005426560,f_7005426580,f_7005426660,f_7005426670,f_7005426680,f_7005426690,f_7005426760};original[kind](&oracle);execute(*active_guest,kind);
  if(m.outputs)oracle.x[0]=active_guest->x[0];for(unsigned i=0;i<m.outputs;++i)oracle.x[m.start+i]=active_guest->x[m.start+i];
  if(std::memcmp(&oracle,active_guest,sizeof oracle)||!shadow.equal(source,output))nx::fatal("Native packed state differs from SDK kind %u",kind);
 }else execute(*active_guest,kind);
 *result=active_guest->x[0];
 if(compare){static std::atomic<uint64_t> calls[22];const auto n=calls[kind].fetch_add(1,std::memory_order_relaxed)+1;if(n==1||n%10000==0)nx::log("Native packed state kind %u: %llu compared",kind,(unsigned long long)n);}return 1;
}
