#pragma once

enum CatherineBindingRowKind {
    CATHERINE_BINDING_KEY,
    CATHERINE_BINDING_PAD,
    CATHERINE_BINDING_MOVEMENT,
    CATHERINE_BINDING_SWAP,
    CATHERINE_BINDING_RESET_KEYBOARD,
    CATHERINE_BINDING_RESET_CONTROLLER
};

typedef struct {
    unsigned kind, index;
    const char *label;
} CatherineBindingRow;

enum { CATHERINE_BINDING_VISIBLE = 8, CATHERINE_BINDING_KEYBOARD_ROWS = 17,
       CATHERINE_BINDING_CONTROLLER_ROWS = 14 };

static const CatherineBindingRow catherine_keyboard_rows[] = {
    {CATHERINE_BINDING_MOVEMENT, 0, "Movement"},
    {CATHERINE_BINDING_KEY, 0, "Grab / Talk / Select"},
    {CATHERINE_BINDING_KEY, 1, "Drop / Cancel"},
    {CATHERINE_BINDING_KEY, 2, "Open Phone / Apply"},
    {CATHERINE_BINDING_KEY, 3, "Use Item / Sip Drink"},
    {CATHERINE_BINDING_KEY, 4, "Left Stick Press"},
    {CATHERINE_BINDING_KEY, 5, "Right Stick Press"},
    {CATHERINE_BINDING_KEY, 6, "Undo / Previous Tab"},
    {CATHERINE_BINDING_KEY, 7, "Autoplay / Next Tab"},
    {CATHERINE_BINDING_KEY, 8, "Left Trigger Action"},
    {CATHERINE_BINDING_KEY, 9, "Right Trigger Action"},
    {CATHERINE_BINDING_KEY, 10, "Pause / Menu"},
    {CATHERINE_BINDING_KEY, 11, "View / Select"},
    {CATHERINE_BINDING_KEY, 20, "Grab / Select (Mouse)"},
    {CATHERINE_BINDING_KEY, 21, "Cancel (Mouse)"},
    {CATHERINE_BINDING_KEY, 26, "Confirm (Alternate)"},
    {CATHERINE_BINDING_RESET_KEYBOARD, 0, "Reset Keyboard & Mouse"}
};

static const CatherineBindingRow catherine_controller_rows[] = {
    {CATHERINE_BINDING_SWAP, 0, "Swap Sticks"},
    {CATHERINE_BINDING_PAD, 0, "Grab / Talk / Select"},
    {CATHERINE_BINDING_PAD, 1, "Drop / Cancel"},
    {CATHERINE_BINDING_PAD, 2, "Open Phone / Apply"},
    {CATHERINE_BINDING_PAD, 3, "Use Item / Sip Drink"},
    {CATHERINE_BINDING_PAD, 4, "Left Stick Press"},
    {CATHERINE_BINDING_PAD, 5, "Right Stick Press"},
    {CATHERINE_BINDING_PAD, 6, "Undo / Previous Tab"},
    {CATHERINE_BINDING_PAD, 7, "Autoplay / Next Tab"},
    {CATHERINE_BINDING_PAD, 8, "Left Trigger Action"},
    {CATHERINE_BINDING_PAD, 9, "Right Trigger Action"},
    {CATHERINE_BINDING_PAD, 10, "Pause / Menu"},
    {CATHERINE_BINDING_PAD, 11, "View / Select"},
    {CATHERINE_BINDING_RESET_CONTROLLER, 0, "Reset Controller Bindings"}
};

_Static_assert(sizeof(catherine_keyboard_rows) / sizeof(catherine_keyboard_rows[0]) == CATHERINE_BINDING_KEYBOARD_ROWS,
               "Keyboard rows must match navigation bounds");
_Static_assert(sizeof(catherine_controller_rows) / sizeof(catherine_controller_rows[0]) == CATHERINE_BINDING_CONTROLLER_ROWS,
               "Controller rows must match navigation bounds");

typedef struct {
    unsigned column, row[2], scroll[2], footer;
} CatherineBindingFocus;

static inline unsigned catherine_binding_rows(unsigned column) {
    return column ? CATHERINE_BINDING_CONTROLLER_ROWS : CATHERINE_BINDING_KEYBOARD_ROWS;
}

static inline const CatherineBindingRow *catherine_binding_row(unsigned column, unsigned row) {
    if (row >= catherine_binding_rows(column)) return 0;
    return column ? catherine_controller_rows + row : catherine_keyboard_rows + row;
}

static inline void catherine_binding_scroll(CatherineBindingFocus *focus) {
    const unsigned column = focus->column;
    if (focus->row[column] >= catherine_binding_rows(column))
        focus->row[column] = catherine_binding_rows(column) - 1;
    if (focus->row[column] < focus->scroll[column]) focus->scroll[column] = focus->row[column];
    if (focus->row[column] >= focus->scroll[column] + CATHERINE_BINDING_VISIBLE)
        focus->scroll[column] = focus->row[column] + 1 - CATHERINE_BINDING_VISIBLE;
}

static inline void catherine_binding_scroll_to(CatherineBindingFocus *focus, unsigned column, int scroll) {
    const unsigned count = catherine_binding_rows(column);
    const unsigned maximum = count > CATHERINE_BINDING_VISIBLE ? count - CATHERINE_BINDING_VISIBLE : 0;
    focus->column = column;
    focus->footer = 0;
    focus->scroll[column] = scroll < 0 ? 0 : (unsigned)scroll > maximum ? maximum : (unsigned)scroll;
    if (focus->row[column] < focus->scroll[column]) focus->row[column] = focus->scroll[column];
    if (focus->row[column] >= focus->scroll[column] + CATHERINE_BINDING_VISIBLE)
        focus->row[column] = focus->scroll[column] + CATHERINE_BINDING_VISIBLE - 1;
}

// Direction: 0 up, 1 down, 2 left, 3 right. Each column retains its own row/scroll.
static inline void catherine_binding_move(CatherineBindingFocus *focus, unsigned direction) {
    if (direction >= 2) {
        if (focus->footer) {
            if (direction == 2) focus->footer = focus->footer == 2 ? 1 : 3;
            else focus->footer = focus->footer == 3 ? 1 : 2;
        }
        else focus->column = direction == 2 ? 0 : 1;
    } else if (focus->footer) {
        if (!direction) focus->footer = 0;
    } else if (!direction) {
        if (focus->row[focus->column]) --focus->row[focus->column];
    } else if (focus->row[focus->column] + 1 < catherine_binding_rows(focus->column)) {
        ++focus->row[focus->column];
    } else focus->footer = 3;
    catherine_binding_scroll(focus);
}

static inline unsigned catherine_binding_capture_value(unsigned column, unsigned raw_key, unsigned raw_pad) {
    if (!column) return raw_key;
    raw_pad &= 0xfffu; // Physical face/shoulder/trigger/stick-click/menu buttons only.
    if (!raw_pad || (raw_pad & (raw_pad - 1))) return 0;
    unsigned index = 1;
    while ((raw_pad >>= 1) != 0) ++index;
    return index;
}
