#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include "../../base/guest-timing/include/config_pc_host.h"
#include "../../base/guest-timing/include/pc_dialogs.h"

struct Cpu { uint64_t x[32]{}, pc{}; };
static uint32_t load32(uint64_t p) { uint32_t v; std::memcpy(&v, (void*)p, 4); return v; }
static void store32(uint64_t p, uint32_t v) { std::memcpy((void*)p, &v, 4); }
#define LD32(p) load32(p)
#define ST32(p,v) store32(p,v)
static int fields[5]{}, raw;
int catherine_pc_mouse(unsigned field) { return fields[field]; }
int catherine_config_setting(unsigned field, int) { assert(field == PC_RAW_KEY); return raw; }
unsigned catherine_pc_question, catherine_pc_display_dialog;
static bool config_open;
int catherine_config_pc_is_open(void) { return config_open; }
static unsigned pointer_consumed;
void catherine_config_pc_question_pointer_consumed(void) { ++pointer_consumed; }
static unsigned cursor_calls, sounds;
static uint64_t expected_dialog;
void f_7000072ef0(Cpu *c) {
    assert(c->x[0] == expected_dialog);
    assert(c->x[1] == LD32(expected_dialog + 0xec));
    ++cursor_calls;
}
void f_7000277ff0(Cpu *c) { assert(c->x[0] == 0xbba); ++sounds; }
void f_70002f38a0(Cpu *c) { c->x[0] = 123456; }
#include "../../base/guest-timing/include/pc_question_mouse.h"

static void pointer(int x, int y, bool clicked = false) {
    fields[0] = x; fields[1] = y; ++fields[2];
    if (clicked) ++fields[3]; fields[4] = 1;
}
int main() {
    uint32_t body[0x180/4]{};
    const uint64_t dialog = expected_dialog = (uint64_t)body;
    ST32(dialog + 0xc, 0x25); ST32(dialog + 0xe8, 20);
    const float base = 46;
    std::memcpy((void*)(dialog + 0x5c), &base, 4);
    Cpu cpu{};
    catherine_pc_question = CATHERINE_PC_QUESTION_EXIT;
    constexpr uint32_t confirm = 1u << 13;
    assert(catherine_pc_question_mouse_row(dialog, 960, 384) == 0);
    assert(catherine_pc_question_mouse_row(dialog, 960, 420) == 1);
    assert(catherine_pc_question_mouse_row(dialog, 700, 420) == -1);
    assert(catherine_pc_question_mouse_row(dialog, 960, 460) == -1);
    // The same actual 720p Yes/No text centres map to the 1080p canvas.
    assert(catherine_pc_question_mouse_row(dialog, 640*3/2, 256*3/2) == 0);
    assert(catherine_pc_question_mouse_row(dialog, 640*3/2, 280*3/2) == 1);

    pointer(470, 766, true); raw = 1; // Click opened Exit, outside dialog.
    catherine_pc_question_mouse_begin();
    assert(!(catherine_pc_question_mouse_input(&cpu, dialog, confirm) & confirm));
    pointer(960, 420);
    assert(!catherine_pc_question_mouse_input(&cpu, dialog, 0));
    assert(LD32(dialog + 0xec) == 1 && cursor_calls == 1 && sounds == 1);
    assert(LD32(dialog + 0x128) == 1 && LD32(dialog + 0x138) == 0);
    assert(LD32(dialog + 0xfc) == 1 && LD32(dialog + 0x12c) == 0);
    assert(LD32(dialog + 0x11c) == 56);

    // Controller can change the native selection despite stationary No hover.
    raw = 0; ST32(dialog + 0xec, 0);
    assert(catherine_pc_question_mouse_input(&cpu, dialog, confirm) == confirm);
    assert(LD32(dialog + 0xec) == 0 && cursor_calls == 1);
    pointer(960, 420, true); raw = 1;
    assert(catherine_pc_question_mouse_input(&cpu, dialog, confirm) == confirm);
    assert(LD32(dialog + 0xec) == 1 && LD32(dialog + 0xf0) == 0); // Native confirm still owns result.
    assert(!catherine_pc_question_mouse_input(&cpu, dialog, confirm)); // One click, one confirm.

    pointer(700, 420, true);
    assert(!catherine_pc_question_mouse_input(&cpu, dialog, confirm));
    assert(LD32(dialog + 0xec) == 1);
    fields[4] = 0; ++fields[3];
    assert(!catherine_pc_question_mouse_input(&cpu, dialog, confirm));
    // Reopening while the opening button is held does not accept default Yes.
    ST32(dialog + 0xec, 0); catherine_pc_question_mouse_begin();
    assert(!catherine_pc_question_mouse_input(&cpu, dialog, confirm));
    pointer(960, 384, true); ST32(dialog + 0xe8, 2);
    assert(!catherine_pc_question_mouse_input(&cpu, dialog, confirm));
    ST32(dialog + 0xe8, 20);
    assert(!catherine_pc_question_mouse_input(&cpu, dialog, 0)); // No deferred early click.

    // Only owned questions of the authored two-choice type are affected.
    catherine_pc_question = 0;
    assert(catherine_pc_question_mouse_input(&cpu, dialog, confirm) == confirm);
    catherine_pc_display_dialog = 1;
    ST32(dialog + 0xc, 0x47);
    assert(catherine_pc_question_mouse_input(&cpu, dialog, confirm) == confirm);
    ST32(dialog + 0xc, 0x25); catherine_pc_question_mouse_begin();
    pointer(960, 420, true);
    assert(catherine_pc_question_mouse_input(&cpu, dialog, 0) == confirm);
    assert(LD32(dialog + 0xec) == 1);
    // The actual Config question IDs and native line layout are distinct from
    // the custom two-line boxes. Native Save uses one message line.
    catherine_pc_question = catherine_pc_display_dialog = 0;
    for (unsigned type : {0x24u, 0x25u, 0x27u}) {
        ST32(dialog + 0xc, type); config_open = false;
        assert(!catherine_pc_question_mouse_eligible(type));
        assert(catherine_pc_question_mouse_input(&cpu, dialog, confirm) == confirm);
        config_open = true;
        assert(catherine_pc_question_mouse_eligible(type));
        ST32(dialog + 0xec, 0); ST32(dialog + 0xe8, 20);
        const float native_base = type == 0x24 ? 34.5f : 46.f;
        std::memcpy((void*)(dialog + 0x5c), &native_base, 4);
        catherine_pc_question_mouse_begin();
        assert(!catherine_pc_question_mouse_input(&cpu, dialog, confirm));
        const int no_y = (int)((191.f + native_base + 19.f + 23.f)*1.5f);
        pointer(960, no_y, true);
        assert(catherine_pc_question_mouse_input(&cpu, dialog, 0) == confirm);
        assert(LD32(dialog + 0xec) == 1);
        assert(!catherine_pc_question_mouse_input(&cpu, dialog, confirm));
        config_open = false;
        pointer(960, no_y, true);
        assert(catherine_pc_question_mouse_input(&cpu, dialog, confirm) == confirm);
    }
    config_open = true;
    for (unsigned type : {0x23u, 0x28u, 0x47u})
        assert(!catherine_pc_question_mouse_eligible(type));
    assert(pointer_consumed > 0);
    puts("PASS: native question hover/click chooses its own row; opening/outside/repeated clicks cannot confirm Yes; controller navigation and unrelated dialogs preserved.");
}
