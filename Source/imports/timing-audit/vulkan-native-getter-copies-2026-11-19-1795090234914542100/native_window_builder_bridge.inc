#include "native_window_builder_reader.h"
extern "C" void f_7005429940(void*);extern "C" void f_7005429950(void*);extern "C" void f_7005429970(void*);extern "C" void f_7005429980(void*);
extern "C" int catherine_native_window_builder(nx::CPU* cpu,unsigned kind,uint64_t address,uint64_t builder,uint64_t index,int compare,uint64_t* result){
 using namespace catherine::native_window;using catherine::native_view::overlaps;
 if(!cpu||!active_guest||!result||kind>=4||address!=addresses[kind]||!builder||builder>UINT64_MAX-44||overlaps(builder,44,uint64_t(cpu),sizeof *cpu)||overlaps(builder,44,uint64_t(active_guest),sizeof *active_guest))return 0;
 if(kind==1){if(int32_t(index)<0)return 0;const auto count=load<uint32_t>(builder+0x10);if(int32_t(count)>int32_t(index)){const auto table=load<uint64_t>(builder+0x18);const uint64_t offset=uint64_t(uint32_t(index))*8;if(!table||table>UINT64_MAX-offset-8)return 0;const auto slot=table+offset;if(overlaps(slot,8,uint64_t(cpu),sizeof *cpu)||overlaps(slot,8,uint64_t(active_guest),sizeof *active_guest))return 0;}}
 to_guest(*cpu,*active_guest);active_guest->x[0]=builder;if(kind==1)active_guest->x[1]=index;active_guest->pc=address;
 GuestCpu oracle{};if(compare)oracle=*active_guest;execute(*active_guest,kind);
 if(compare){static void(*const original[])(void*)={f_7005429940,f_7005429950,f_7005429970,f_7005429980};original[kind](&oracle);if(std::memcmp(&oracle,active_guest,sizeof oracle))nx::fatal("Native window builder differs from SDK kind %u",kind);}
 *result=active_guest->x[0];
 if(compare){static std::atomic<uint64_t> calls[4];const auto n=calls[kind].fetch_add(1,std::memory_order_relaxed)+1;if(n==1||n%1000==0)nx::log("Native window builder kind %u: %llu compared",kind,(unsigned long long)n);}return 1;
}
