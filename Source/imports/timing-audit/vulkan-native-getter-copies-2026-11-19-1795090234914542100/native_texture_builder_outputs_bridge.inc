#include "native_texture_builder_outputs.h"
extern "C" void f_70054277e0(void*);extern "C" void f_7005427830(void*);
namespace {
struct BuilderOutputShadow{
 alignas(16) uint8_t builder_bytes[104],output_bytes[16],joined[120];
 uint64_t builder=0,output=0,actual=0;size_t size=0;
 BuilderOutputShadow(uint64_t b,uint64_t o){
   if(catherine::native_view::overlaps(b,104,o,16)){actual=std::min(b,o);size=size_t(std::max(b+104,o+16)-actual);std::memcpy(joined,reinterpret_cast<const void*>(actual),size);builder=uint64_t(joined)+(b-actual);output=uint64_t(joined)+(o-actual);}
   else{std::memcpy(builder_bytes,reinterpret_cast<const void*>(b),104);std::memcpy(output_bytes,reinterpret_cast<const void*>(o),16);builder=uint64_t(builder_bytes);output=uint64_t(output_bytes);}
 }
 bool equal(uint64_t b,uint64_t o)const{return size?!std::memcmp(joined,reinterpret_cast<const void*>(actual),size):!std::memcmp(builder_bytes,reinterpret_cast<const void*>(b),104)&&!std::memcmp(output_bytes,reinterpret_cast<const void*>(o),16);}
};
}
extern "C" int catherine_native_texture_builder_outputs(nx::CPU* cpu,unsigned kind,uint64_t address,uint64_t builder,uint64_t output,int compare,uint64_t* result){
 using namespace catherine::native_builder_outputs;using catherine::native_view::overlaps;
 if(!cpu||!active_guest||!result||kind>1||address!=addresses[kind]||!builder||!output||builder>UINT64_MAX-104||output>UINT64_MAX-16||
   overlaps(builder,104,uint64_t(cpu),sizeof *cpu)||overlaps(output,16,uint64_t(cpu),sizeof *cpu)||overlaps(builder,104,uint64_t(active_guest),sizeof *active_guest)||overlaps(output,16,uint64_t(active_guest),sizeof *active_guest))return 0;
 to_guest(*cpu,*active_guest);active_guest->x[0]=builder;active_guest->x[1]=output;
 if(!kind){active_guest->x[2]=output+4;active_guest->x[3]=output+8;active_guest->x[4]=output+12;}
 active_guest->pc=address;
 if(compare){BuilderOutputShadow shadow(builder,output);GuestCpu oracle=*active_guest;oracle.x[0]=shadow.builder;oracle.x[1]=shadow.output;
   if(!kind){oracle.x[2]=shadow.output+4;oracle.x[3]=shadow.output+8;oracle.x[4]=shadow.output+12;f_70054277e0(&oracle);}else f_7005427830(&oracle);
   execute(*active_guest,kind);
   if(!kind)oracle.x[0]=active_guest->x[0];oracle.x[1]=active_guest->x[1];
   if(!kind){oracle.x[2]=active_guest->x[2];oracle.x[3]=active_guest->x[3];oracle.x[4]=active_guest->x[4];}
   if(std::memcmp(&oracle,active_guest,sizeof oracle)||!shadow.equal(builder,output))nx::fatal("Native texture builder output differs from SDK kind %u",kind);
 }else execute(*active_guest,kind);
 *result=active_guest->x[0];
 static std::atomic<uint64_t> calls[2];const auto n=calls[kind].fetch_add(1,std::memory_order_relaxed)+1;
 if(n==1||n%1000==0)nx::log("Native texture builder output kind %u: %llu mode %d",kind,(unsigned long long)n,compare?2:1);return 1;
}
