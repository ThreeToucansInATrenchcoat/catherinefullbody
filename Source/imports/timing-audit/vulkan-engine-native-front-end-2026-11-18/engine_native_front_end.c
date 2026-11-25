#include <stdlib.h>
#include "cpu.h"
#include "engine_native_front_end.h"

void f_70002fc1d0(Cpu* c);
void f_7000314330(Cpu* c);
void f_700032b5c4(Cpu* c);

static int front_end_mode(void) {
    static _Thread_local int mode = -1;
    if (mode < 0) {
        const char* setting = getenv("CATHERINE_VULKAN_ENGINE_FRONT_END");
        mode = setting ? atoi(setting) : 1;
    }
    return mode;
}

/* Native version of the actual engine packet f_70002fc1d0. Keep the lazy state
 * flush until its independent material/pipeline paths are ported. Only the
 * indexed draw's SDK conversion + register bridge is replaced here. */
void catherine_engine_indexed_packet(Cpu* c) {
    const int mode = front_end_mode();
    if (!mode) { f_70002fc1d0(c); return; }

    const uint64_t context = c->x[0];
    const uint64_t packet = LD64(context + 8);
    const uint64_t frame = c->sp - 32;
    ST64(frame, c->x[20]); ST64(frame + 8, c->x[19]);
    ST64(frame + 16, c->x[29]); ST64(frame + 24, c->x[30]);
    c->x[19] = context; c->x[20] = packet;
    c->x[29] = frame + 16; c->x[30] = 0x70002fc1e8ull;
    c->sp = frame;
    f_7000314330(c);

    const uint64_t engine = LD64(0x7000cc9498ull);
    const uint64_t command = LD64(engine + 0x5ca0);
    const uint32_t index = LD32(packet + 8);
    const uint32_t primitive = LD32(packet + 12);
    c->x[0] = command; c->x[1] = primitive; c->x[2] = index;
    c->x[3] = packet + 16;
    c->x[4] = LD32(packet + 32); c->x[5] = LD32(packet + 36);
    c->x[8] = engine; c->x[30] = 0x70002fc204ull;

    const uint64_t callback_slot = LD64(0x7000ccf1f0ull);
    const uint64_t callback = LD64(callback_slot);
    /* Unsupported callbacks retain the original tail after the one lazy flush.
     * Primitive11 is the SDK's NVN quad mode: retain its special expansion. */
    if (primitive >= 12 || index >= 3 || primitive == 11 ||
        !catherine_engine_indexed_callback(callback)) {
        f_700032b5c4(c);
    } else {
        /* Preserve the exact guest SDK spill bytes even though the native
         * producer does not need or borrow the guest stack. */
        const uint64_t sdk = frame - 64;
        ST64(sdk, c->x[24]); ST64(sdk + 8, c->x[23]);
        ST64(sdk + 16, c->x[22]); ST64(sdk + 24, c->x[21]);
        ST64(sdk + 32, c->x[20]); ST64(sdk + 40, c->x[19]);
        ST64(sdk + 48, c->x[29]); ST64(sdk + 56, c->x[30]);

        CatherineEngineIndexedDraw draw;
        draw.index_address = LD64(packet + 16); /* f330370 is a no-op. */
        draw.primitive = LD32(0x70007f4ac0ull + 4ull * primitive);
        draw.index_type = LD32(0x70007f4b1cull + 4ull * index);
        draw.index_count = LD32(packet + 32);
        draw.base_vertex = (int32_t)LD32(packet + 36);
        const uint64_t native_command = LD64(command + 0xb8);
        catherine_engine_indexed_emit(native_command, &draw, mode == 2);

        /* Exact SDK + graphics bridge volatile/alias effects. No Cpu/register
         * pointer enters the typed producer. */
        c->x[0] = native_command; c->x[1] = draw.primitive;
        c->x[2] = draw.index_type; c->x[3] = draw.index_count;
        c->x[4] = draw.index_address;
        c->x[5] = (uint32_t)draw.base_vertex;
        c->x[6] = callback;
        c->x[31] = frame;
        c->pc = callback; c->nzcv &= 0xf0000000ull;
    }

    const uint64_t next = LD64(context + 8) + 40;
    ST64(context + 8, next); c->x[8] = next;
    c->x[29] = LD64(frame + 16); c->x[30] = LD64(frame + 24);
    c->x[20] = LD64(frame); c->x[19] = LD64(frame + 8);
    c->sp = frame + 32;
}
