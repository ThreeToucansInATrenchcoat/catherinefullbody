#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* All writes are bounded temporary earned-save extraction, never game assets. */
int catherine_dev_checkpoint_materialize(const char *entry,const char *sha256,
 const char *source_fallback,char *output,uint32_t capacity);
void catherine_dev_checkpoint_release(void);
const char *catherine_dev_checkpoint_error(void);
#ifdef __cplusplus
}
#endif
