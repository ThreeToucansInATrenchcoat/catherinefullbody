#include <stdlib.h>
#include "cpu.h"
#include "engine_native_arrays.h"
void f_70002fc180(Cpu* c);
void f_7000314330(Cpu* c);
void f_700032b574(Cpu* c);

static int front_end_mode(void) {
    static _Thread_local int mode=-1;
    if(mode<0) {
        const char* value=getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");
        mode=value?atoi(value):1;
    }
    return mode;
}

void catherine_engine_array_packet(Cpu* c) {
    const int mode=front_end_mode();
    if(!mode) {f_70002fc180(c);return;}
    const uint64_t context=c->x[0],packet=LD64(context+8);
    const uint64_t frame=c->sp-32;
    ST64(frame,c->x[20]);ST64(frame+8,c->x[19]);
    ST64(frame+16,c->x[29]);ST64(frame+24,c->x[30]);
    c->x[19]=context;c->x[20]=packet;c->x[29]=frame+16;
    c->x[30]=0x70002fc198ull;c->sp=frame;
    f_7000314330(c);

    const uint64_t engine=LD64(0x7000cc9498ull);
    const uint64_t command=LD64(engine+0x5ca0);
    const uint32_t primitive=LD32(packet+8);
    const uint32_t count=LD32(packet+12),first=LD32(packet+16);
    c->x[0]=command;c->x[1]=primitive;c->x[2]=count;c->x[3]=first;
    c->x[8]=engine;c->x[30]=0x70002fc1b0ull;
    const uint64_t callback=LD64(LD64(0x7000ccf1e8ull));
    if(primitive>=12||primitive==11||!catherine_engine_array_callback(callback)) {
        f_700032b574(c);
    } else {
        const uint64_t sdk=frame-48;
        ST64(sdk,c->x[21]); /* sdk+8 is deliberately untouched in the original. */
        ST64(sdk+16,c->x[20]);ST64(sdk+24,c->x[19]);
        ST64(sdk+32,c->x[29]);ST64(sdk+40,c->x[30]);
        CatherineEngineArrayDraw draw;
        draw.primitive=LD32(0x70007f4ac0ull+4ull*primitive);
        draw.first_vertex=(int32_t)first;draw.vertex_count=(int32_t)count;
        const uint64_t native_command=LD64(command+0xb8);
        catherine_engine_array_emit(native_command,&draw,mode==2);
        c->x[0]=native_command;c->x[1]=draw.primitive;
        c->x[2]=(uint32_t)draw.first_vertex;c->x[3]=(uint32_t)draw.vertex_count;
        c->x[4]=callback;c->x[31]=frame;c->pc=callback;
        c->nzcv&=0xf0000000ull;
    }
    const uint64_t next=LD64(context+8)+24;
    ST64(context+8,next);c->x[8]=next;
    c->x[29]=LD64(frame+16);c->x[30]=LD64(frame+24);
    c->x[20]=LD64(frame);c->x[19]=LD64(frame+8);c->sp=frame+32;
}
