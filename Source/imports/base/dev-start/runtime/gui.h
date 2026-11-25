#pragma once
#include <windows.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Window-thread seams. Call message before the platform WndProc switch and
   pretranslate before TranslateMessage. init is lazy; no graphics dependencies. */
int catherine_dev_gui_window_message(HWND owner, UINT message, WPARAM w, LPARAM l, LRESULT *result);
int catherine_dev_gui_pretranslate(MSG *message);
/* Any-thread, cheap atomics. Pause gameplay only at the safe guest boundary;
   keep the loader service, window, rendering and IO workers running. */
/* Default is input-only overlay; backend must reject unsafe load states.
   Enable pause mode only when its cooperative guest hook is proven. */
void catherine_dev_gui_set_pause_supported(int supported);
int catherine_dev_gui_pause_requested(void);
int catherine_dev_gui_input_blocked(void);
void catherine_dev_overlay_pad(unsigned held);
void catherine_dev_gui_set_paused(int acknowledged);
/* Explicit native metadata, supplied only after native value validation.
   Window thread only; strings copied. field: 0 karma, 1 difficulty, 2 route.
   Keep-earned is always separate; no guessed native enums are supplied. */
void catherine_dev_gui_set_choices(unsigned field, const char *const *labels,
                                  const int *values, unsigned count);
#ifdef __cplusplus
}
#endif
