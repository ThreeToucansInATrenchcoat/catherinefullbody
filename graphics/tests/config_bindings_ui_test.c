#include "cpu.h"
#include "simd.h"
#include "config_menu_pc.h"
#include <windows.h>
#include <assert.h>
#include <stdio.h>

static unsigned char menu[0x400], pad[0x100];
static Cpu cpu;
static int active, dirty, raw_key, raw_pad, layout, preset, swapped;
static int saves, discards, whole_config_saves, writes, last_field, last_value;
static int modal_done, modal_result, modal_opens;
static int mouse[5];

void rt_dispatch(Cpu *c) { (void)c; }
void f_7000031d70(Cpu *c) { (void)c; }
void f_7000034720(Cpu *c) { (void)c; }
void f_7000277ff0(Cpu *c) { (void)c; }
void f_700009c1a0(Cpu *c) { (void)c; }
void f_70002abad0(Cpu *c) { (void)c; }
void f_700008fd00(Cpu *c) { (void)c; }
void f_7000072920(Cpu *c) { assert(c->x[0] == 0x25); ++modal_opens; }
void f_70000729d0(Cpu *c) { (void)c; }
void f_7000072a70(Cpu *c) { c->x[0] = modal_done; }
void f_7000072b10(Cpu *c) { c->x[0] = modal_result; modal_done = 0; }
int catherine_pc_mouse(unsigned field) { return field < 5 ? mouse[field] : 0; }
const char *catherine_keyboard_label(unsigned index) { (void)index; return "Key"; }
int catherine_config_setting(unsigned field, int change) {
    if (field == PC_INPUT_LAYOUT) return layout;
    if (field == PC_RAW_KEY) return raw_key;
    if (field == PC_RAW_PAD) return raw_pad;
    if (field == PC_KEY_BINDING + 8 && !change) return preset == 1 ? 'W' : 'Q';
    if (field == PC_PREVIEW || field == PC_APPLY_BUTTON || field == PC_APPLY) return 0;
    if (field == PC_BINDINGS_BEGIN) { active = 1; dirty = 0; return 1; }
    if (field == PC_BINDINGS_DIRTY) return dirty;
    if (field == PC_BINDINGS_SAVE) { assert(active); ++saves; dirty = 0; return 1; }
    if (field == PC_BINDINGS_DISCARD) { ++discards; active = dirty = 0; return 1; }
    if (field == PC_CONFIG_SAVE) { ++whole_config_saves; return 1; }
    if (field == PC_MOVEMENT_PRESET) {
        if (change) { preset = (preset + (change < 0 ? 2 : 1)) % 3; dirty = 1; }
        return change ? 1 : preset;
    }
    if (field == PC_SWAP_STICKS) {
        if (change) { swapped = !swapped; dirty = 1; }
        return change ? 1 : swapped;
    }
    if (change && ((field >= PC_KEY_BINDING && field <= PC_RESET_KEYBOARD) ||
                   (field >= PC_BINDING && field <= PC_RESET_BINDINGS))) {
        assert(active); ++writes; last_field = field; last_value = change; dirty = 1; return 1;
    }
    return 1;
}

static int step(unsigned buttons, unsigned repeat) {
    ST32((uintptr_t)pad, repeat);
    ST32((uintptr_t)pad + 4, buttons);
    ST32((uintptr_t)pad + 8, repeat);
    ST32((uintptr_t)pad + 0x18, repeat);
    return catherine_config_pc_input(&cpu, (uintptr_t)menu);
}
static void released(void) {
    raw_key = raw_pad = 0;
    ST32((uintptr_t)pad, 0);
    assert(step(0, 0) == CATHERINE_CONFIG_INPUT_HANDLED);
}
static void open_child(void) {
    catherine_config_pc.row = 1;
    assert(step(CATHERINE_CONFIG_CONFIRM, 0) == CATHERINE_CONFIG_INPUT_HANDLED);
    assert(catherine_config_bindings_visible((uintptr_t)menu));
    released();
}
static void answer(int yes) {
    modal_done = 1; modal_result = !yes;
    assert(step(0, 0) == CATHERINE_CONFIG_INPUT_HANDLED);
}

int main(void) {
    assert(VirtualAlloc((void *)0x7000cc9000ull, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(VirtualAlloc((void *)0x70007a0000ull, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    assert(VirtualAlloc((void *)0x70031cc000ull, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    ST32(0x70031cc1e8ull, 0x3b888889u); // 1 / 240 seconds.
    ST64(0x7000cc9070ull, (uintptr_t)pad);
    ST32((uintptr_t)menu + 8, CATHERINE_CONFIG_CONTROL);
    ST32((uintptr_t)menu + 12, CATHERINE_CONFIG_ACTIVE);
    catherine_config_pc.menu = (uintptr_t)menu;
    catherine_config_pc.previous_tab = CATHERINE_CONFIG_CONTROL;
    catherine_config_pc.selected_tab = CATHERINE_CONFIG_CONTROL + 1;
    assert(pc_control_rows() == 2);
    open_child();

    // No individual movement/camera/stick-direction rows are exposed.
    for (unsigned col = 0; col != 2; ++col) {
        for (unsigned i = 0; i < catherine_binding_rows(col); ++i) {
            const CatherineBindingRow *row = catherine_binding_row(col, i);
            if (row->kind == CATHERINE_BINDING_KEY)
                assert(row->index < 12 || row->index == 20 || row->index == 21 || row->index == 26);
            if (row->kind == CATHERINE_BINDING_PAD) assert(row->index < 12);
        }
    }
    assert(step(0, CATHERINE_CONFIG_RIGHT) == CATHERINE_CONFIG_INPUT_HANDLED);
    assert(catherine_bindings_ui.focus.column == 1);
    for (int i = 0; i != 12; ++i) step(0, CATHERINE_CONFIG_DOWN);
    assert(catherine_bindings_ui.focus.scroll[1] > 0);
    step(0, CATHERINE_CONFIG_LEFT);
    assert(catherine_bindings_ui.focus.column == 0 && catherine_bindings_ui.focus.row[0] == 0);

    // Keyboard capture ignores all controller buttons and mouse hover until done.
    catherine_bindings_ui.focus.row[0] = 1;
    step(CATHERINE_CONFIG_CONFIRM, 0);
    raw_pad = 1 << 6;
    mouse[0] = 1100; mouse[1] = 300; mouse[2] = 1; mouse[4] = 1;
    step(0, 0);
    assert(catherine_bindings_ui.capturing == 1 && catherine_bindings_ui.focus.column == 0 && !writes);
    raw_key = 'P'; step(0, 0);
    assert(writes == 1 && last_field == PC_KEY_BINDING && last_value == 'P');
    raw_key = 0; step(0, 0);

    // Controller capture ignores keyboard keys; LB/RB are valid despite disabled tabs.
    raw_pad = 0; catherine_bindings_ui.focus.column = 1; catherine_bindings_ui.focus.row[1] = 1;
    step(CATHERINE_CONFIG_CONFIRM, 0);
    raw_key = 'V'; step(0, 0);
    assert(catherine_bindings_ui.capturing == 1 && writes == 1);
    raw_pad = 1 << 7; step(CATHERINE_CONFIG_TAB, 0);
    assert(writes == 2 && last_field == PC_BINDING && last_value == 8);
    raw_pad = raw_key = 0; step(0, 0);
    assert(catherine_binding_capture_value(1, 'X', 1 << 12) == 0);
    assert(catherine_binding_capture_value(1, 0, (1 << 6) | (1 << 7)) == 0);
    ST32((uintptr_t)pad + 4, CATHERINE_CONFIG_TAB);
    assert(!(catherine_config_pc_tab_buttons((uintptr_t)pad) & CATHERINE_CONFIG_TAB));
    assert(catherine_config_bindings_chrome_color((uintptr_t)menu, 0x10, 0xffffff80) == 0x68686880);
    assert(catherine_config_bindings_chrome_color((uintptr_t)menu, 0x16, 0xffffff80) == 0xffffff80);

    // No keeps the draft; Yes saves controls only and returns to Control.
    catherine_bindings_ui.focus.footer = 2;
    const int before_confirm = modal_opens;
    step(CATHERINE_CONFIG_CONFIRM, 0);
    assert(catherine_pc_question == CATHERINE_PC_QUESTION_SAVE_BINDINGS);
    answer(0); assert(dirty && !saves && catherine_bindings_ui.open);
    released(); step(CATHERINE_CONFIG_CONFIRM, 0); answer(1);
    assert(!dirty && saves == 1 && !whole_config_saves && !catherine_bindings_ui.open);
    assert(modal_opens == before_confirm + 2 && !active);
    released(); open_child();

    // Back clean closes, dirty Discard requires a native Yes and preserves No.
    step(1u << 14, 0); assert(!catherine_bindings_ui.open);
    released(); open_child(); dirty = 1;
    step(1u << 14, 0);
    assert(catherine_pc_question == CATHERINE_PC_QUESTION_DISCARD_BINDINGS);
    answer(0); assert(catherine_bindings_ui.open && dirty);
    released(); step(1u << 14, 0); answer(1);
    assert(!catherine_bindings_ui.open && !active && !dirty && !whole_config_saves);

    // Pointer input opens the parent button and selects the intended device.
    released();
    mouse[0] = 400; mouse[1] = 785; ++mouse[2]; ++mouse[3]; mouse[4] = 1;
    step(0, 0); assert(catherine_bindings_ui.open);
    released();
    catherine_bindings_ui.focus.column = 0;
    step(CATHERINE_CONFIG_CONFIRM, 0);
    assert(preset == 1 && dirty); // ESDF is a preset, not four separately editable directions.
    mouse[0] = 1200; mouse[1] = 337; ++mouse[2]; ++mouse[3]; raw_key = 1;
    step(0, 0);
    assert(catherine_bindings_ui.focus.column == 1 && catherine_bindings_ui.capturing == 1);
    step(0, 0); raw_pad = 1 << 6; step(0, 0);
    assert(last_field == PC_BINDING && last_value == 7);
    raw_pad = raw_key = 0; step(0, 0);
    mouse[0] = 1740; mouse[1] = 1000; ++mouse[2]; ++mouse[3]; raw_key = 1;
    const int before_mouse_confirm = saves;
    step(1u << 14, 0); // A Cancel-mapped mouse click still confirms its target.
    assert(catherine_pc_question == CATHERINE_PC_QUESTION_SAVE_BINDINGS);
    answer(1);
    assert(!catherine_bindings_ui.open && saves == before_mouse_confirm + 1 && !whole_config_saves);

    released(); // Child Confirm must release before parent tab shortcuts resume.
    // Keyboard Q/W aliases use physical rising edges, not a guessed native bit.
    // Reading row intent must not consume the native tab callback's edge.
    ST32((uintptr_t)pad + 4, 0); layout = 0; preset = 0; raw_key = 'Q';
    assert(catherine_config_pc_tab_intent((uintptr_t)pad) & (1u << 10));
    assert(catherine_config_pc_tab_buttons((uintptr_t)pad) & (1u << 10));
    assert(!(catherine_config_pc_tab_buttons((uintptr_t)pad) & (1u << 10)));
    raw_key = 0; catherine_config_pc_tab_intent((uintptr_t)pad); catherine_config_pc_tab_buttons((uintptr_t)pad);
    preset = 1; raw_key = 'W';
    catherine_config_pc_tab_intent((uintptr_t)pad);
    assert(catherine_config_pc_tab_buttons((uintptr_t)pad) & (1u << 10));
    assert(!(catherine_config_pc_tab_buttons((uintptr_t)pad) & (1u << 10)));
    raw_key = 0; catherine_config_pc_tab_intent((uintptr_t)pad); catherine_config_pc_tab_buttons((uintptr_t)pad);
    layout = 1; raw_key = 'W';
    catherine_config_pc_tab_intent((uintptr_t)pad);
    assert(!(catherine_config_pc_tab_buttons((uintptr_t)pad) & (1u << 10)));
    layout = 0;
    catherine_config_pc_tab_intent((uintptr_t)pad);
    assert(!(catherine_config_pc_tab_buttons((uintptr_t)pad) & (1u << 10))); // No new key press.
    raw_key = 0; catherine_config_pc_tab_intent((uintptr_t)pad); catherine_config_pc_tab_buttons((uintptr_t)pad);

    // Mouse wheels and scrollbar tracks operate each column without starting capture.
    released(); open_child();
    mouse[0] = 400; mouse[1] = 400; ++mouse[2]; raw_key = 257;
    step(0, 0); assert(catherine_bindings_ui.focus.scroll[0] == 3);
    step(0, 0); assert(catherine_bindings_ui.focus.scroll[0] == 3); // No per-frame runaway.
    for (unsigned i = 0; i != 31; ++i) step(0, 0);
    assert(catherine_bindings_ui.focus.scroll[0] == 6);
    raw_key = 0; step(0, 0);
    mouse[0] = 950; mouse[1] = 830; ++mouse[2]; ++mouse[3]; step(0, 0);
    assert(catherine_bindings_ui.focus.scroll[0] == 9 && !catherine_bindings_ui.capturing);
    mouse[0] = 1810; ++mouse[2]; ++mouse[3]; step(0, 0);
    assert(catherine_bindings_ui.focus.column == 1 && catherine_bindings_ui.focus.scroll[1] == 6);
    raw_key = 1; step(CATHERINE_CONFIG_CONFIRM, 0);
    assert(!catherine_bindings_ui.capturing); // Delayed LMB Confirm, pointer serial unchanged.
    mouse[1] = 255; ++mouse[2]; step(CATHERINE_CONFIG_CONFIRM, 0);
    assert(catherine_bindings_ui.focus.scroll[1] == 0); // Drag to top.
    assert(!catherine_bindings_ui.capturing);
    raw_key = 0; step(CATHERINE_CONFIG_CONFIRM, 0);
    assert(!catherine_bindings_ui.dragging && catherine_bindings_ui.wait_release);
    step(CATHERINE_CONFIG_CONFIRM, 0);
    assert(catherine_bindings_ui.wait_release && !catherine_bindings_ui.capturing);
    released();
    assert(!catherine_bindings_ui.wait_release && !catherine_bindings_ui.capturing);
    step(CATHERINE_CONFIG_CONFIRM, 0); // A new press after release still starts a normal capture.
    assert(catherine_bindings_ui.capturing == 1);
    catherine_bindings_ui.capturing = 0;
    step(1u << 14, 0); assert(!catherine_bindings_ui.open);
    puts("PASS: native bindings subpage, separate capture, scroll, shoulders, save/discard, release and Q alias.");
}
