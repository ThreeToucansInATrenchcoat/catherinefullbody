#pragma once

#include <stdint.h>

enum {
    PC_FPS, PC_MODE, PC_RESOLUTION, PC_AA, PC_MOVIES,
    PC_DISPLAY_ROWS,
    PC_BINDING = 16, PC_RESET_BINDINGS = 32,
    PC_PREVIEW, PC_KEEP, PC_REVERT, PC_RAW_PAD, PC_DUALSENSE, PC_APPLY, PC_DISCARD_DRAFT, PC_APPLY_BUTTON,
    PC_CONFIG_BEGIN, PC_CONFIG_SAVE, PC_CONFIG_END, PC_INPUT_LAYOUT, PC_RAW_KEY, PC_CURSOR_HIDDEN,
    PC_KEY_BINDING = 64, PC_RESET_KEYBOARD = 91,
    PC_BINDINGS_BEGIN = 100, PC_BINDINGS_DIRTY, PC_BINDINGS_SAVE, PC_BINDINGS_DISCARD,
    PC_MOVEMENT_PRESET, PC_SWAP_STICKS, PC_BINDINGS_ACTIVE,
    PC_BINDING_PLAYER = 107, PC_PLAYER1_DEVICE, PC_PLAYER2_DEVICE, PC_INPUT_DEVICE_COUNT,
    PC_INPUT_MENU_MODE, PC_MENU_KEYBOARD_ALLOWED, PC_CONNECTED_PLAYERS,
    PC_CAMERA_INPUT_LAYOUT, PC_BUTTON_PROMPTS
};

#ifdef __cplusplus
extern "C"
#endif
int catherine_config_setting(unsigned field, int change);

#ifdef __cplusplus
extern "C"
#endif
const char *catherine_keyboard_label(unsigned index);

/* Device dropdown indices: 0 Auto/None, 1 Keyboard & Mouse, 2+ controllers.
 * Absolute setters use index+1; PC_BINDING_PLAYER uses 1=P1, 2=P2.
 * PC_INPUT_MENU_MODE uses 1=gameplay, 2=menus; read returns whether menus are active. */
#ifdef __cplusplus
extern "C"
#endif
const char *catherine_input_device_label(unsigned player, unsigned index);

/* Absolute menu pointer, normalized to the game's 1920 x 1080 canvas.
 * 0/1: x/y, 2: movement serial, 3: left-click serial, 4: inside client. */
#ifdef __cplusplus
extern "C"
#endif
int catherine_pc_mouse(unsigned field);

#ifdef __cplusplus
extern "C" {
#endif
int catherine_dev_enabled(void);
/* The guest calls these only with its initialized game-system object. */
void catherine_load_native_config(unsigned char *game, int load_campaign_defaults);
int catherine_save_native_config(const unsigned char *game, int campaign_options_editable);
#ifdef __cplusplus
}
#endif
