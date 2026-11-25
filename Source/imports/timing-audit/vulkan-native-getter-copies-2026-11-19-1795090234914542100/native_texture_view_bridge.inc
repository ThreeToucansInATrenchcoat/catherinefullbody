#include <algorithm>
#include <atomic>
#include <array>
#include <limits>
#include "native_texture_view_reader.h"

// The actual SDK functions remain linked and are used only on isolated copied
// input/output memory by diagnostic mode. Their scalar bodies make no calls.
extern "C" void f_7005427ca0(void*);
extern "C" void f_7005427cc0(void*);
extern "C" void f_7005427d00(void*);
extern "C" void f_7005427ce0(void*);
namespace {
struct ViewGetterShadow {
    alignas(16) uint8_t view_bytes[28],scratch_bytes[16],joined[44];
    uint64_t view=0,scratch=0,actual=0;
    size_t joined_size=0;
    ViewGetterShadow(uint64_t source,uint64_t output){
        if(catherine::native_view::overlaps(source,28,output,16)){
            actual=std::min(source,output);joined_size=size_t(std::max(source+28,output+16)-actual);
            std::memcpy(joined,reinterpret_cast<const void*>(actual),joined_size);
            view=uint64_t(joined)+(source-actual);scratch=uint64_t(joined)+(output-actual);
        }else{
            std::memcpy(view_bytes,reinterpret_cast<const void*>(source),28);
            std::memcpy(scratch_bytes,reinterpret_cast<const void*>(output),16);
            view=uint64_t(view_bytes);scratch=uint64_t(scratch_bytes);
        }
    }
    bool equal(uint64_t output)const{
        return joined_size?!std::memcmp(joined,reinterpret_cast<const void*>(actual),joined_size)
                          :!std::memcmp(scratch_bytes,reinterpret_cast<const void*>(output),16);
    }
};
void view_arguments(GuestCpu& guest,uint32_t kind,uint64_t view,uint64_t tmp){
    using namespace catherine::native_view;
    guest.x[0]=view;guest.x[1]=tmp;
    if(kind==Levels||kind==Layers)guest.x[2]=tmp+4;
    if(kind==Swizzle){guest.x[2]=tmp+4;guest.x[3]=tmp+8;guest.x[4]=tmp+12;}
}
std::atomic<uint64_t> native_view_requests{0},native_view_compared{0},native_view_fallbacks{0};
}
extern "C" int catherine_native_texture_view_getter(nx::CPU* cpu,uint32_t kind,uint64_t address,
                                                    uint64_t view,uint64_t tmp,int compare,uint64_t* result){
    using namespace catherine::native_view;
    // guest_call_original uses immutable const SDK tables. Resolve by name at
    // each call, admit its exact original address, and never cache a positive
    // decision that could hide a different late resolution.
    const bool safe=cpu&&active_guest&&result&&kind<Count&&address==addresses[kind]&&view&&tmp&&
        view<=std::numeric_limits<uint64_t>::max()-28&&tmp<=std::numeric_limits<uint64_t>::max()-16&&
        !overlaps(view,28,uint64_t(cpu),sizeof *cpu)&&!overlaps(tmp,16,uint64_t(cpu),sizeof *cpu)&&
        !overlaps(view,28,uint64_t(active_guest),sizeof *active_guest)&&!overlaps(tmp,16,uint64_t(active_guest),sizeof *active_guest);
    if(!safe){native_view_fallbacks.fetch_add(1,std::memory_order_relaxed);return 0;}
    to_guest(*cpu,*active_guest);
    active_guest->x[0]=view;active_guest->x[1]=tmp;
    if(kind==Levels||kind==Layers)active_guest->x[2]=tmp+4;
    if(kind==Swizzle){active_guest->x[2]=tmp+4;active_guest->x[3]=tmp+8;active_guest->x[4]=tmp+12;}
    active_guest->pc=address;
    if(compare){
        ViewGetterShadow shadow(view,tmp);
        GuestCpu oracle=*active_guest;view_arguments(oracle,kind,shadow.view,shadow.scratch);
        static void(*const original[Count])(void*)={f_7005427ca0,f_7005427cc0,f_7005427d00,f_7005427ce0};
        original[kind](&oracle);
        execute(*active_guest,kind);
        // Restore only remapped pointer arguments for the exact CPU comparison.
        oracle.x[1]=active_guest->x[1];
        if(kind==Levels||kind==Layers||kind==Swizzle)oracle.x[2]=active_guest->x[2];
        if(kind==Swizzle){oracle.x[3]=active_guest->x[3];oracle.x[4]=active_guest->x[4];}
        if(std::memcmp(&oracle,active_guest,sizeof oracle)||!shadow.equal(tmp))
            nx::fatal("Native texture-view getter disagrees with original SDK: kind %u",kind);
        native_view_compared.fetch_add(1,std::memory_order_relaxed);
    }else execute(*active_guest,kind);
    *result=active_guest->x[0];
    const uint64_t count=native_view_requests.fetch_add(1,std::memory_order_relaxed)+1;
    if(compare&&(count==1||count%100000==0))
        nx::log("Native texture-view getters: %llu compared %llu compatibility %llu",(unsigned long long)count,
                (unsigned long long)native_view_compared.load(std::memory_order_relaxed),(unsigned long long)native_view_fallbacks.load(std::memory_order_relaxed));
    return 1;
}
