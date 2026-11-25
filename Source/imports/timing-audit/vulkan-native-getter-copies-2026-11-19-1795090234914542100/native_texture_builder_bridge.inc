#include "native_texture_builder_reader.h"
extern "C" void f_7005427760(void*);
extern "C" void f_7005427770(void*);
extern "C" void f_7005427780(void*);
extern "C" void f_7005427790(void*);
extern "C" void f_70054277a0(void*);
extern "C" void f_70054277b0(void*);
extern "C" void f_70054277c0(void*);
extern "C" void f_70054277d0(void*);
extern "C" void f_7005427810(void*);
extern "C" void f_7005427880(void*);
extern "C" void f_7005427890(void*);
extern "C" void f_70054278a0(void*);

extern "C" void f_700541ea10(void*);
extern "C" void f_700541ea20(void*);
extern "C" void f_700541ea30(void*);
extern "C" void f_70054126d0(void*);
extern "C" void f_70054126e0(void*);
extern "C" void f_70054126f0(void*);
extern "C" int catherine_native_texture_builder_scalar(nx::CPU* cpu,unsigned kind,uint64_t address,uint64_t builder,int compare,uint64_t* result){
    using namespace catherine::native_builder;
    using catherine::native_view::overlaps;
    if(!cpu||!active_guest||!result||kind>=count||address!=addresses[kind]||!builder||builder>UINT64_MAX-104||
       overlaps(builder,104,uint64_t(cpu),sizeof *cpu)||overlaps(builder,104,uint64_t(active_guest),sizeof *active_guest))return 0;
    to_guest(*cpu,*active_guest);active_guest->x[0]=builder;active_guest->pc=address;
    GuestCpu oracle{};if(compare)oracle=*active_guest;
    active_guest->x[0]=read(builder,kind);
    if(compare){
        static void(*const original[])(void*)={f_7005427760,f_7005427770,f_7005427780,f_7005427790,f_70054277a0,f_70054277b0,f_70054277c0,f_70054277d0,f_7005427810,f_7005427880,f_7005427890,f_70054278a0,f_700541ea10,f_700541ea20,f_700541ea30,f_70054126d0,f_70054126e0,f_70054126f0};original[kind](&oracle);
        if(std::memcmp(&oracle,active_guest,sizeof oracle))nx::fatal("Native texture builder getter differs from SDK kind %u",kind);
    }
    *result=active_guest->x[0];
    static std::atomic<uint64_t> calls{0};const auto n=calls.fetch_add(1,std::memory_order_relaxed)+1;
    if(n==1||n%1000==0)nx::log("Native texture builder scalars: %llu mode %d",(unsigned long long)n,compare?2:1);
    return 1;
}
