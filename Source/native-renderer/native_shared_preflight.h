#pragma once
#include <stdlib.h>
/* Private experiment: retain every leaf check; omit only the duplicate entry
 * probes. Default off until real-original QA and matched timings justify it. */
static int native_omit_shared_preflight(void) {
    static _Thread_local int mode = -1;
    if (mode < 0) {
        const char *value = getenv("CATHERINE_NATIVE_SHARED_PREFLIGHT");
        mode = value && atoi(value) == 1;
    }
    return mode;
}
