#pragma once

#include <stdint.h>
#include <uchar.h>
#include "pc_dialogs.h"

/* Host coordinates use the game's 1920 x 1080 UI canvas, without the title bar. */
extern int catherine_pc_mouse(unsigned field);
extern int catherine_multiplayer_available(void);
static inline int catherine_piano_row_disabled(unsigned rows, int row) {
    return ((rows == 6 && row == 2) || (rows == 3 && row == 1)) &&
           !catherine_multiplayer_available();
}
extern void f_700009c1a0(Cpu *c);
extern void f_70002abad0(Cpu *c);
extern void f_7000072920(Cpu *c);
extern void f_7000072a70(Cpu *c);
extern void f_7000072b10(Cpu *c);
extern void catherine_pc_exit_game(void);

typedef struct { float x, y; } CatherinePianoPoint;

/* The title camera is authored and does not move between these five rows.
 * Limit selection to the labelled ends of the keys, excluding the black keys
 * and Vincent. The shared boundaries follow the piano's perspective. */
static const CatherinePianoPoint catherine_piano_keys[5][4] = {
    {{64, 278}, {648, 151}, {709, 248}, {92, 369}},
    {{92, 369}, {709, 248}, {769, 350}, {115, 487}},
    {{115, 487}, {769, 350}, {827, 455}, {140, 615}},
    {{140, 615}, {827, 455}, {895, 590}, {170, 760}},
    {{170, 760}, {895, 590}, {961, 737}, {205, 941}}
};

/* Story's New Game / Load Game keys use a closer authored camera. */
static const CatherinePianoPoint catherine_story_keys[2][4] = {
    {{155, 465}, {728, 315}, {842, 500}, {198, 668}},
    {{198, 668}, {842, 500}, {965, 705}, {240, 900}}
};

/* Babel Single / Pair / Record, on the closer three-key piano camera. */
static const CatherinePianoPoint catherine_babel_keys[3][4] = {
    {{90,330},{760,150},{850,325},{130,495}},
    {{130,495},{850,325},{950,470},{165,665}},
    {{165,665},{950,470},{1030,620},{200,830}}
};

/* Difficulty and Puzzle Type share the closest camera. Their first two
 * labelled keys occupy the same positions, unlike the Story submenu. */
static const CatherinePianoPoint catherine_new_game_keys[4][4] = {
    {{75,320},{850,120},{940,300},{110,510}},
    {{110,510},{940,300},{1040,470},{145,665}},
    {{145,665},{1040,470},{1150,655},{190,845}},
    {{190,845},{1150,655},{1270,865},{240,1050}}
};

static inline int catherine_piano_key_at(int x, int y, unsigned rows, unsigned page) {
    const int new_game = page == 0xba || page == 0xbc;
    const CatherinePianoPoint (*keys)[4] = new_game ? catherine_new_game_keys : rows == 3 ? catherine_babel_keys : rows == 2 ? catherine_story_keys : catherine_piano_keys;
    const unsigned count = new_game ? rows : rows == 3 ? 3 : rows == 2 ? 2 : 5;
    for (unsigned row = 0; row < count; ++row) {
        int inside = 1;
        for (unsigned edge = 0; edge < 4; ++edge) {
            const CatherinePianoPoint a = keys[row][edge];
            const CatherinePianoPoint b = keys[row][(edge + 1) & 3];
            if ((b.x - a.x) * (y - a.y) - (b.y - a.y) * (x - a.x) < 0) {
                inside = 0;
                break;
            }
        }
        if (inside) return (int)row;
    }
    return -1;
}

static struct {
    uint64_t menu;
    unsigned move_serial, click_serial, generation, rows, page;
    int pending_click;
    unsigned right_click_serial;
    int back_release_pending;
    uint32_t page_release_mask;
} catherine_piano_mouse;

static inline void catherine_piano_mouse_sync(uint64_t menu) {
    catherine_piano_mouse.menu = menu;
    catherine_piano_mouse.move_serial = (unsigned)catherine_pc_mouse(2);
    catherine_piano_mouse.click_serial = (unsigned)catherine_pc_mouse(3);
    catherine_piano_mouse.right_click_serial = (unsigned)catherine_pc_mouse(6);
    catherine_piano_mouse.pending_click = -1;
    const uint64_t pad = LD64(0x7000cc9070ull);
    catherine_piano_mouse.page_release_mask = pad ? LD32(pad) & ((1u<<13)|(1u<<14)) : 0;
    catherine_piano_mouse.rows = 0;
    catherine_piano_mouse.page = 0;
}

static inline void catherine_piano_mouse_begin(uint64_t menu) {
    catherine_piano_mouse_sync(menu);
    ++catherine_piano_mouse.generation;
}

/* Return a different row only for a new mouse event. The existing native
 * selection path must handle the row change so piano motion, help and male
 * narration retain their authored behaviour. A click on a different row is
 * confirmed on the next poll, after that native selection path has run. */
static inline int catherine_piano_mouse_input(uint64_t menu, unsigned rows, unsigned page,
                                             int selected, uint32_t *pressed) {
    const uint32_t confirm = 1u << 13;
    // The mapped mouse button also appears in native held/repeat input. Only
    // the click serial below may confirm; holding across a page must not.
    if (catherine_pc_mouse(7)) *pressed &= ~confirm;
    if (catherine_piano_mouse.menu != menu) catherine_piano_mouse_begin(menu);
    if (catherine_piano_mouse.rows != rows || catherine_piano_mouse.page != page) {
        catherine_piano_mouse_sync(menu);
        catherine_piano_mouse.rows = rows;
        catherine_piano_mouse.page = page;
    }
    // Confirm/Back held through a transition must be released before either
    // action can fire on the destination page, for keyboard and controllers.
    const uint64_t input_pad = LD64(0x7000cc9070ull);
    const uint32_t held = input_pad ? LD32(input_pad) : 0;
    const uint32_t release_mask = catherine_piano_mouse.page_release_mask;
    *pressed &= ~release_mask;
    catherine_piano_mouse.page_release_mask &= held;
    const unsigned moves = (unsigned)catherine_pc_mouse(2);
    const unsigned clicks = (unsigned)catherine_pc_mouse(3);
    const int moved = moves != catherine_piano_mouse.move_serial;
    const int clicked = clicks != catherine_piano_mouse.click_serial;
    catherine_piano_mouse.move_serial = moves;
    catherine_piano_mouse.click_serial = clicks;
    const unsigned right_clicks = (unsigned)catherine_pc_mouse(6);
    const int right_clicked = right_clicks != catherine_piano_mouse.right_click_serial;
    catherine_piano_mouse.right_click_serial = right_clicks;
    if (page == 0xb0 && catherine_piano_mouse.back_release_pending) {
        const uint32_t buttons = LD32(LD64(0x7000cc9070ull));
        const int raw_right = catherine_config_setting(PC_RAW_KEY, 0) == 2;
        const int back_active = ((buttons | *pressed) & (1u << 14)) != 0;
        *pressed &= ~(1u << 14);
        if (raw_right) *pressed &= ~confirm;
        catherine_piano_mouse.pending_click = -1;
        if (!back_active && !raw_right) catherine_piano_mouse.back_release_pending = 0;
        return -1;
    }
    if (page == 0xb0 && (right_clicked || (*pressed & (1u << 14)))) {
        /* Back wins even when RMB has been rebound to Confirm. */
        catherine_piano_mouse.pending_click = -1;
        *pressed = (*pressed & ~confirm) | (1u << 14);
        return -1;
    }

    if (moved || clicked) {
        int row = catherine_pc_mouse(4) ?
            catherine_piano_key_at(catherine_pc_mouse(0), catherine_pc_mouse(1), rows, page) : -1;
        if (catherine_piano_row_disabled(rows, row)) row = -1;
        if (clicked) {
            /* The regular left-click binding must not confirm a stale row. */
            *pressed &= ~confirm;
            catherine_piano_mouse.pending_click = row >= 0 && (unsigned)row < rows ? row : -1;
        } else if (row != catherine_piano_mouse.pending_click) {
            catherine_piano_mouse.pending_click = -1;
        }
        if (row >= 0 && (unsigned)row < rows && row != selected) return row;
    }

    if (catherine_piano_mouse.pending_click == selected) {
        catherine_piano_mouse.pending_click = -1;
        *pressed |= confirm;
    }
    return -1;
}

static inline void catherine_piano_exit_request(Cpu *c, uint64_t menu) {
    if (catherine_pc_question) return;
    catherine_piano_mouse_sync(menu);
    catherine_pc_question = CATHERINE_PC_QUESTION_EXIT;
    Cpu dialog = *c;
    dialog.x[0] = 0x25; // Authored two-choice dialog; pc_dialogs supplies its text.
    f_7000072920(&dialog);
}

/* Poll from the normal piano input function. Returning one consumes this input
 * frame both while the question is open and when it closes, so its final button
 * press cannot select the underlying menu entry a second time. */
static inline int catherine_piano_exit_pending(Cpu *c, uint64_t menu) {
    if (catherine_pc_question != CATHERINE_PC_QUESTION_EXIT) return 0;
    catherine_piano_mouse_sync(menu);
    Cpu dialog = *c;
    f_7000072a70(&dialog);
    if (dialog.x[0] & 1) {
        dialog = *c;
        f_7000072b10(&dialog);
        const int yes = (uint32_t)dialog.x[0] == 0;
        catherine_pc_question = CATHERINE_PC_QUESTION_NONE;
        if (yes) catherine_pc_exit_game();
    }
    return 1;
}

/* Replace the unused Arena description inside the native menu's resource list.
 * f_7000294260 then draws the new text with the original perspective, spacing
 * and font. The list retains ownership and its normal destructor releases it. */
static inline int catherine_piano_exit_help(Cpu *c, uint64_t menu, unsigned index) {
    static uint64_t installed_menu, installed_node, installed_handle;
    static unsigned installed_generation;
    if (index >= 64 || !menu) return 0;
    uint64_t node = LD64(menu + 0x88);
    for (unsigned i = 0; node && i < index; ++i) node = LD64(node);
    if (!node) return 0;
    if (installed_generation == catherine_piano_mouse.generation &&
        installed_menu == menu && installed_node == node &&
        installed_handle && LD64(node + 0x10) == installed_handle) return 1;

    static const char16_t text[] = u"Leave the game.";
    Cpu call = *c;
    call.x[0] = (uint64_t)(uintptr_t)text;
    call.x[1] = 1;
    call.x[2] = 0;
    f_70002abad0(&call);
    const uint64_t replacement = call.x[0];
    if (!replacement) return 0;

    uint64_t previous = LD64(node + 0x10);
    if (previous) {
        call = *c;
        f_700009c1a0(&call);
        const uint64_t manager = LD64(call.x[0] + 0x40);
        call.x[0] = manager;
        call.x[1] = (uint64_t)(uintptr_t)&previous;
        call.pc = LD64(LD64(manager) + 0x30);
        rt_dispatch(&call);
    }
    ST64(node + 0x10, replacement);
    installed_menu = menu;
    installed_generation = catherine_piano_mouse.generation;
    installed_node = node;
    installed_handle = replacement;
    return 1;
}
