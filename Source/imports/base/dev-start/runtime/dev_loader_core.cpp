#include "dev_loader_native.h"
#include "dev_checkpoint_pack.h"
#include "puzzle_recipe_host.h"
#include <atomic>
#include <mutex>
#include <cstdio>
#include <cstring>
#include <cstdint>

extern "C" int catherine_dev_save_isolate_movie(void);
extern "C" int catherine_dev_save_begin_record(const char *path,unsigned slot,unsigned char *record);

namespace {
struct Preset { CatherineDevTarget target; const char *archive,*entry,*sha256; };
#include "dev_loader_catalog.inc"
std::mutex lock;
std::atomic<bool> queued{false};
std::atomic<bool> startup_menu{false}, bootstrapped{false};
char prepared_path[1024]{};
CatherineDevLoadStatus state{1,CATHERINE_DEV_IDLE,0,0,0,0,"Choose a destination."};
const Preset *selected=nullptr;
std::atomic<bool> has_deferred{false};
const Preset *deferred_preset=nullptr;
CatherineDevLoadRequest deferred_request{};
int32_t selected_karma_enabled=0,selected_karma_value=0,selected_difficulty=-1;
void message(const char *s){std::snprintf(state.message,sizeof(state.message),"%s",s?s:"");}
const Preset *find(uint32_t id){for(const auto &p:presets)if(p.target.id==id)return &p;return nullptr;}
void release_prepared() {
 catherine_dev_checkpoint_release();
 prepared_path[0]='\0';
}
void start_locked(const Preset *preset,const CatherineDevLoadRequest &request) {
 catherine_dev_puzzle_cancel();
 release_prepared();
 selected=preset;
 selected_difficulty=request.difficulty;
 selected_karma_enabled=request.karma_enabled;
 selected_karma_value=request.karma_value;
 state.target_id=request.target_id;
 if (!++state.request_id) ++state.request_id;
 state.busy=1;
 state.error_code=0;
 state.phase=CATHERINE_DEV_WAIT_SAFE;
 message("Queued for the scene manager.");
 queued.store(true,std::memory_order_release);
}
int fail_locked(int code,const char *detail) {
 state.error_code=code;
 state.phase=CATHERINE_DEV_ERROR;
 state.busy=0;
 selected=nullptr;
 queued.store(false,std::memory_order_release);
 message(detail);
 release_prepared();
 return 0;
}
}
extern "C" uint32_t catherine_dev_loader_count(void){return uint32_t(sizeof(presets)/sizeof(*presets));}
extern "C" int catherine_dev_loader_describe(uint32_t i,CatherineDevTarget *out){
 if(!out||i>=catherine_dev_loader_count())return 0;*out=presets[i].target;return 1;
}
extern "C" void catherine_dev_loader_status(CatherineDevLoadStatus *out){if(out){catherine_dev_puzzle_poll();std::lock_guard<std::mutex> g(lock);*out=state;}}
extern "C" int catherine_dev_loader_request(const CatherineDevLoadRequest *r){
 if(!r||r->version!=1)return 0;
 for(auto x:r->reserved)if(x)return 0;
 if((r->karma_enabled!=0&&r->karma_enabled!=1)||(r->karma_enabled&&(r->karma_value<0||r->karma_value>255))||(r->difficulty < -1 || r->difficulty > 3)||r->route!=-1)return 0;
 const auto *p=find(r->target_id);if(!p||!(p->target.capabilities&CATHERINE_DEV_CAN_LOAD))return 0;
 if(r->karma_enabled && !(p->target.capabilities&CATHERINE_DEV_CAN_KARMA))return 0;
 if(r->difficulty>=0 && !(p->target.capabilities&CATHERINE_DEV_CAN_DIFFICULTY))return 0;
 std::lock_guard<std::mutex> guard(lock);
 if (state.busy || has_deferred.load(std::memory_order_relaxed)) {
  deferred_request=*r;
  deferred_preset=p;
  has_deferred.store(true,std::memory_order_release);
  return 1;
 }
 start_locked(p,*r);
 return 1;
}
extern "C" int catherine_dev_loader_adopt_next(void) {
 if (!has_deferred.load(std::memory_order_acquire)) return 0;
 std::lock_guard<std::mutex> guard(lock);
 if (!deferred_preset || state.phase==CATHERINE_DEV_READING || state.phase==CATHERINE_DEV_APPLYING) return 0;
 start_locked(deferred_preset,deferred_request);
 deferred_preset=nullptr;
 has_deferred.store(false,std::memory_order_release);
 return 1;
}
extern "C" int catherine_dev_loader_pending(void){return queued.load(std::memory_order_acquire);}
extern "C" void catherine_dev_loader_set_startup_menu(int waiting){startup_menu.store(waiting!=0,std::memory_order_release);}
extern "C" int catherine_dev_loader_startup_menu(void){return startup_menu.load(std::memory_order_acquire);}
extern "C" int catherine_dev_loader_bootstrap_ready(void) {
 if(catherine_dev_loader_startup_menu())return 0;
 bootstrapped.store(true,std::memory_order_release);
 return 1;
}
extern "C" int catherine_dev_loader_is_bootstrapped(void){return bootstrapped.load(std::memory_order_acquire);}
extern "C" int catherine_dev_loader_skip_puzzle_intro(void) {
 std::lock_guard<std::mutex> guard(lock);
 if(!state.busy || state.phase!=CATHERINE_DEV_TRANSITION || !selected)return 0;
 const uint32_t kind=selected->target.kind;
 return kind==CATHERINE_DEV_PUZZLE || kind==CATHERINE_DEV_BABEL ||
        kind==CATHERINE_DEV_COLOSSEUM || kind==CATHERINE_DEV_RAPUNZEL;
}
extern "C" int catherine_dev_loader_target_kind(uint32_t id) {
 const Preset *preset=find(id);
 return preset ? (int)preset->target.kind : 0;
}
extern "C" int catherine_dev_loader_skip_puzzle_loading_ui(void) {
 std::lock_guard<std::mutex> guard(lock);
 if(!state.busy || state.phase!=CATHERINE_DEV_TRANSITION || !selected)return 0;
 const uint32_t kind=selected->target.kind;
 return selected->target.id==40001 || kind==CATHERINE_DEV_PUZZLE ||
        kind==CATHERINE_DEV_BABEL || kind==CATHERINE_DEV_COLOSSEUM || kind==CATHERINE_DEV_RAPUNZEL;
}
extern "C" int catherine_dev_loader_skip_movie(void) {
 if(catherine_dev_loader_pending())return 1;
 std::lock_guard<std::mutex> guard(lock);
 return state.busy && state.phase==CATHERINE_DEV_TRANSITION && selected &&
        selected->target.kind!=CATHERINE_DEV_ROOM && selected->target.kind!=CATHERINE_DEV_MOVIE;
}
extern "C" int catherine_dev_loader_difficulty(int32_t *value){std::lock_guard<std::mutex>g(lock);if(!state.busy||!value)return 0;*value=selected_difficulty;return 1;}
extern "C" int catherine_dev_loader_recipe(uint32_t *request_id,int32_t *recipe_id){std::lock_guard<std::mutex>g(lock);if(!state.busy||!selected||!request_id||!recipe_id)return 0;*request_id=state.request_id;*recipe_id=(selected->target.capabilities&CATHERINE_DEV_CAN_STAGE_RECIPE)?(selected->target.id==40001 ? 10101 : (int32_t)selected->target.id):0;return 1;}
extern "C" int catherine_dev_loader_karma(int32_t *enabled,int32_t *value){std::lock_guard<std::mutex>g(lock);if(!state.busy||!enabled||!value)return 0;*enabled=selected_karma_enabled;*value=selected_karma_value;return 1;}
extern "C" int catherine_dev_loader_begin(char *path,uint32_t capacity,int32_t *slot){
 std::lock_guard<std::mutex> g(lock);
 if(!path||!slot||!selected||state.phase!=CATHERINE_DEV_WAIT_SAFE||!state.busy)return 0;
 if(capacity<1024)return 0;
 if(!catherine_dev_checkpoint_materialize(selected->entry,selected->sha256,selected->archive,path,capacity)){
  return fail_locked(-20,catherine_dev_checkpoint_error());
 }
 *slot=selected->target.slot_index;
 queued.store(false,std::memory_order_release);state.phase=CATHERINE_DEV_READING;message("Loading the selected save.");return 1;
}
extern "C" void catherine_dev_loader_release_archive(void){std::lock_guard<std::mutex> guard(lock);release_prepared();}

extern "C" int catherine_dev_loader_begin_movie(uint32_t target) {
 std::lock_guard<std::mutex> guard(lock);
 if (!selected || state.phase!=CATHERINE_DEV_WAIT_SAFE ||
     selected->target.id!=target || selected->target.kind!=CATHERINE_DEV_MOVIE) return 0;
 const int result=catherine_dev_save_isolate_movie();
 if (!result) {message("Waiting for an active save operation to finish.");return -1;}
 if (result<0) return fail_locked(result,"Cannot isolate the current save for movie playback.");
 queued.store(false,std::memory_order_release);
 state.phase=CATHERINE_DEV_APPLYING;
 message("Starting the selected movie.");
 return 1;
}
extern "C" int catherine_dev_loader_read_record(void *record,uint32_t capacity) {
 std::lock_guard<std::mutex> guard(lock);
 if(!selected || !state.busy || state.phase!=CATHERINE_DEV_WAIT_SAFE)return 0;
 if(selected->target.kind==CATHERINE_DEV_MOVIE && selected->target.id!=40001)return fail_locked(-21,"Standalone movie has no checkpoint record.");
 if(!record || capacity<0x8000)return fail_locked(-21,"Checkpoint destination is too small.");
 if(!prepared_path[0] &&
    !catherine_dev_checkpoint_materialize(selected->entry,selected->sha256,selected->archive,
                                          prepared_path,sizeof(prepared_path)))
  return fail_locked(-20,catherine_dev_checkpoint_error());
 int result=catherine_dev_save_begin_record(prepared_path,selected->target.slot_index,
                                           static_cast<unsigned char *>(record));
 if(result==0) {
  message("Waiting for an active save operation to finish.");
  return -1;
 }
 if(result<0) {
  char detail[128];
  std::snprintf(detail,sizeof(detail),"Cannot prepare the isolated checkpoint (Windows error %d).",-result);
  return fail_locked(result,detail);
 }
 release_prepared();
 state.phase=CATHERINE_DEV_APPLYING;
 queued.store(false,std::memory_order_release);
 message("Applying the selected checkpoint.");
 return 1;
}
extern "C" void catherine_dev_loader_progress(uint32_t phase,const char *s){
 std::lock_guard<std::mutex> g(lock);if(state.busy){state.phase=phase;message(s);}
}
extern "C" void catherine_dev_loader_finish_request(uint32_t request,int error,const char *s){
 std::lock_guard<std::mutex> guard(lock);
 if (request && (!state.busy || request!=state.request_id)) return;
 catherine_dev_puzzle_cancel();
 release_prepared();
 queued.store(false,std::memory_order_release);
 state.error_code=error;
 state.phase=error ? CATHERINE_DEV_ERROR : CATHERINE_DEV_COMPLETE;
 state.busy=0;
 selected=nullptr;
 message(s);
}
extern "C" void catherine_dev_loader_cutscene_ended(uint32_t request) {
 std::lock_guard<std::mutex> guard(lock);
 if (state.request_id==request && state.phase==CATHERINE_DEV_COMPLETE)
  message("Cutscene finished. Returning to the main menu.");
}
extern "C" void catherine_dev_loader_finish(int error,const char *s){
 catherine_dev_loader_finish_request(0,error,s);
}
