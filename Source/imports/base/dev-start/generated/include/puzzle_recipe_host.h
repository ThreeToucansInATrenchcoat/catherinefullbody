#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Core supplies a request snapshot only while the native earned restore is busy. */
int catherine_dev_loader_recipe(uint32_t *request_id,int32_t *recipe_id);
/* Cheap closed atomic query. No lock, clock, guest memory access or game calls. */
int catherine_dev_puzzle_pending(void);
uint32_t catherine_dev_puzzle_armed_recipe(void);
int catherine_dev_puzzle_arm(uint32_t request_id,uint64_t config);
int catherine_dev_puzzle_claim(uint64_t config);
void catherine_dev_puzzle_cancel(void);
void catherine_dev_puzzle_error(int error,const char *message);
/* Call before the loader core status mutex; may report an expired request. */
void catherine_dev_puzzle_poll(void);
#ifdef __cplusplus
}
#endif
