#pragma once

#include <stdint.h>

enum CatherinePcQuestion {
    CATHERINE_PC_QUESTION_NONE,
    CATHERINE_PC_QUESTION_EXIT,
    CATHERINE_PC_QUESTION_DISCARD_BINDINGS,
    CATHERINE_PC_QUESTION_SAVE_BINDINGS,
    CATHERINE_PC_QUESTION_SAVE_CONFIG,
    CATHERINE_PC_QUESTION_SAVE_CONFIG_DISPLAY
};

#ifdef __cplusplus
extern "C" {
#endif
extern unsigned catherine_pc_question;
int catherine_config_pc_is_open(void);
void catherine_config_pc_question_pointer_consumed(void);
unsigned catherine_config_display_countdown_seconds(void);
const uint16_t *catherine_config_display_countdown_text(void);
#ifdef __cplusplus
}
#endif
