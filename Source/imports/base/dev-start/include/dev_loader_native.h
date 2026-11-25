#pragma once
#include "dev_loader_api.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Guest scene-manager thread. Host API never invokes guest code. */
int catherine_dev_loader_pending(void);
/* In-game picker input. Mouse fields use the same native canvas and
   cumulative event serials as the game's other pointer-enabled menus. */
int catherine_dev_overlay_visible(void);
unsigned catherine_dev_overlay_generation(void);
void catherine_dev_overlay_close(void);
unsigned catherine_dev_overlay_actions(void);
unsigned catherine_dev_overlay_character(void);
int catherine_dev_overlay_mouse(unsigned field);
/* Promote the newest waiting selection at a stable native root boundary. */
int catherine_dev_loader_adopt_next(void);
int catherine_dev_loader_begin_movie(uint32_t target);
void catherine_dev_loader_cutscene_ended(uint32_t request);
void catherine_dev_loader_finish_request(uint32_t request, int error, const char *message);
/* The last bootstrap step calls this before scheduling the title. It stays at
   that step while a startup picker is waiting for a selection. */
int catherine_dev_loader_bootstrap_ready(void);
int catherine_dev_loader_is_bootstrapped(void);
int catherine_dev_movie_active(void);
int catherine_dev_movie_playing(uint32_t movie_id);
int catherine_dev_loader_target_kind(uint32_t target);
int catherine_dev_loader_skip_puzzle_intro(void);
int catherine_dev_loader_skip_puzzle_loading_ui(void);
void catherine_dev_loader_set_startup_menu(int waiting);
int catherine_dev_loader_startup_menu(void);
/* Copies the validated 0x8000-byte slot and commits its isolated save shadow.
   1 = ready, 0 = failed/inactive, -1 = existing save handles must close. */
int catherine_dev_loader_read_record(void *record, uint32_t capacity);
int catherine_dev_loader_difficulty(int32_t *value);
int catherine_dev_loader_recipe(uint32_t *request_id,int32_t *recipe_id);
int catherine_dev_loader_karma(int32_t *enabled,int32_t *raw_value);
int catherine_dev_loader_begin(char *archive_path, uint32_t capacity, int32_t *slot);
void catherine_dev_loader_release_archive(void);
void catherine_dev_loader_progress(uint32_t phase, const char *message);
void catherine_dev_loader_finish(int error, const char *message);
#ifdef __cplusplus
}
#endif
