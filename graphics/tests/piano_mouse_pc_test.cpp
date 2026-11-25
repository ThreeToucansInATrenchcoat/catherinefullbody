#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "../../base/guest-timing/include/pc_dialogs.h"

struct Cpu { uint64_t x[32]{}, pc{}; };
static uint64_t load64(uint64_t a) { uint64_t v; std::memcpy(&v, (const void*)a, 8); return v; }
static void store64(uint64_t a, uint64_t v) { std::memcpy((void*)a, &v, 8); }
#define LD64(a) load64(a)
#define ST64(a,v) store64(a,v)
static int mouse_fields[5]{};
int catherine_pc_mouse(unsigned field) { return mouse_fields[field]; }
static uint64_t app[16]{}, manager[1]{}, methods[8]{}, text[4]{};
static unsigned creates, releases;
unsigned catherine_pc_question;
static unsigned dialog_opens, dialog_done, dialog_result, exits;
void f_7000072920(Cpu *c) { assert(c->x[0] == 0x25); ++dialog_opens; }
void f_7000072a70(Cpu *c) { c->x[0] = dialog_done; }
void f_7000072b10(Cpu *c) { c->x[0] = dialog_result; }
void catherine_pc_exit_game() { ++exits; }
void f_700009c1a0(Cpu *c) { c->x[0] = (uint64_t)app; }
void f_70002abad0(Cpu *c) {
    assert(c->x[1] == 1 && c->x[2] == 0);
    assert(std::char_traits<char16_t>::compare((const char16_t*)c->x[0], u"Leave the game.", 16) == 0);
    ++creates;
    c->x[0] = (uint64_t)text;
}
void rt_dispatch(Cpu *c) {
    assert(c->x[0] == (uint64_t)manager && c->pc == 1234);
    assert(*(uint64_t*)c->x[1] == 5678 || *(uint64_t*)c->x[1] == (uint64_t)text);
    *(uint64_t*)c->x[1] = 0;
    ++releases;
}
#include "../../base/guest-timing/include/piano_mouse_pc.h"

static void point(int x, int y, bool click = false) {
    mouse_fields[0] = x; mouse_fields[1] = y;
    ++mouse_fields[2]; if (click) ++mouse_fields[3]; mouse_fields[4] = 1;
}

int main() {
    // Label-centre samples from the authored title-camera composition, expressed
    // in the host API's canvas. Every row remains selectable after pixel scaling.
    const int centres[5][2] = {{330, 267}, {340, 381}, {390, 493}, {420, 622}, {470, 766}};
    for (int scale : {720, 1080}) for (int row = 0; row < 5; ++row) {
        const int px = centres[row][0] * scale / 1080;
        const int py = centres[row][1] * scale / 1080;
        assert(catherine_piano_key_at(px * 1080 / scale, py * 1080 / scale) == row);
    }
    assert(catherine_piano_key_at(1450, 560) == -1); // Vincent / black keys.
    assert(catherine_piano_key_at(40, 1000) == -1);  // Outside the piano.
    assert(catherine_piano_key_at(1200, 1040) == -1); // Help description.

    uint32_t pressed = 0;
    catherine_piano_mouse_begin(42);
    assert(catherine_piano_mouse_input(42, 6, 0, &pressed) == -1);
    point(470, 766);
    assert(catherine_piano_mouse_input(42, 6, 0, &pressed) == 4);
    // A stationary mouse cannot steal selection from subsequent controller use.
    pressed = 1u << 4;
    assert(catherine_piano_mouse_input(42, 6, 2, &pressed) == -1);
    assert(pressed == (1u << 4));

    point(420, 622, true);
    pressed = 1u << 13; // Regular left-click binding also produced confirm.
    assert(catherine_piano_mouse_input(42, 6, 0, &pressed) == 3);
    assert(!(pressed & (1u << 13))); // Select first, then confirm correct row.
    assert(catherine_piano_mouse_input(42, 6, 3, &pressed) == -1);
    assert(pressed & (1u << 13));
    pressed = 0;
    assert(catherine_piano_mouse_input(42, 6, 3, &pressed) == -1);
    assert(!pressed); // Same click is never confirmed twice.

    point(1450, 560, true);
    pressed = 1u << 13;
    assert(catherine_piano_mouse_input(42, 6, 3, &pressed) == -1 && !pressed);
    mouse_fields[4] = 0; ++mouse_fields[3]; pressed = 1u << 13;
    assert(catherine_piano_mouse_input(42, 6, 3, &pressed) == -1 && !pressed);
    // A click from before menu entry, or from the closing question, is ignored.
    point(420, 622, true);
    catherine_piano_mouse_begin(42); pressed = 0;
    assert(catherine_piano_mouse_input(42, 6, 3, &pressed) == -1 && !pressed);
    pressed = 1u << 13;
    assert(catherine_piano_mouse_input(42, 6, 3, &pressed) == -1 && pressed); // Keyboard/controller confirms work.

    // The existing native resource list owns the replacement; adjacent entries
    // remain untouched and re-entering the same menu does not recreate it.
    uint64_t menu[20]{}, nodes[4][3]{};
    for (unsigned i = 0; i < 3; ++i) nodes[i][0] = (uint64_t)nodes[i+1];
    nodes[2][2] = 999; nodes[3][2] = 5678;
    menu[0x88/8] = (uint64_t)nodes;
    app[0x40/8] = (uint64_t)manager; manager[0] = (uint64_t)methods; methods[0x30/8] = 1234;
    Cpu cpu{};
    assert(catherine_piano_exit_help(&cpu, (uint64_t)menu, 3));
    assert(nodes[3][2] == (uint64_t)text && nodes[2][2] == 999 && creates == 1 && releases == 1);
    assert(catherine_piano_exit_help(&cpu, (uint64_t)menu, 3));
    assert(creates == 1 && releases == 1);
    // A new menu session may reuse the same menu/list/font addresses. It still
    // receives its own description; a stale pointer match cannot skip install.
    catherine_piano_mouse_begin((uint64_t)menu);
    assert(catherine_piano_exit_help(&cpu, (uint64_t)menu, 3));
    assert(creates == 2 && releases == 2);
    assert(!catherine_piano_exit_help(&cpu, (uint64_t)menu, 64));

    catherine_piano_exit_request(&cpu, 42);
    assert(dialog_opens == 1 && catherine_pc_question == CATHERINE_PC_QUESTION_EXIT);
    catherine_piano_exit_request(&cpu, 42);
    assert(dialog_opens == 1); // Repeated input cannot create another question.
    assert(catherine_piano_exit_pending(&cpu, 42) && !exits);
    dialog_done = 1; dialog_result = 1;
    assert(catherine_piano_exit_pending(&cpu, 42) && !exits && !catherine_pc_question);
    assert(!catherine_piano_exit_pending(&cpu, 42));
    catherine_piano_exit_request(&cpu, 42); dialog_result = 0;
    assert(catherine_piano_exit_pending(&cpu, 42) && exits == 1 && !catherine_pc_question);
    assert(!catherine_piano_exit_pending(&cpu, 42) && exits == 1);
    puts("PASS: piano hover/click ordering, controller coexistence, native help ownership and Exit Yes/No lifecycle.");
}
