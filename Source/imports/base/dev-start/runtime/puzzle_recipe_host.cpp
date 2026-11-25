#include "puzzle_recipe_host.h"
#include "entry-descriptors.h"
#include <atomic>
#include <mutex>
#include <windows.h>
#include "dev_loader_native.h"
#ifndef CATHERINE_DEV_PUZZLE_NOW
#define CATHERINE_DEV_PUZZLE_NOW() GetTickCount64()
#endif
namespace {
std::atomic<uint32_t> pending{0};
std::mutex recipe_lock;
uint32_t generation=0,expected_recipe=0;
uint64_t expected_config=0,deadline=0;
void clear_locked(){pending.store(0,std::memory_order_release);generation=expected_recipe=0;expected_config=deadline=0;}
}
extern "C" int catherine_dev_puzzle_pending(void){return pending.load(std::memory_order_acquire)!=0;}
extern "C" uint32_t catherine_dev_puzzle_armed_recipe(void){return pending.load(std::memory_order_acquire);}
extern "C" void catherine_dev_puzzle_cancel(void){std::lock_guard<std::mutex>g(recipe_lock);clear_locked();}
extern "C" int catherine_dev_puzzle_arm(uint32_t request_id,uint64_t config){
 uint32_t live=0;int32_t recipe=0;
 if(!request_id||!config||!catherine_dev_loader_recipe(&live,&recipe)||live!=request_id||!catherine_dev_puzzle_first_profile((uint32_t)recipe))return 0;
 std::lock_guard<std::mutex>g(recipe_lock);
 if(pending.load(std::memory_order_relaxed))return 0;
 generation=request_id;expected_recipe=(uint32_t)recipe;expected_config=config;deadline=CATHERINE_DEV_PUZZLE_NOW()+12000;
 pending.store((uint32_t)recipe,std::memory_order_release);return 1;
}
extern "C" void catherine_dev_puzzle_error(int error,const char *message){
 catherine_dev_puzzle_cancel();catherine_dev_loader_finish(error,message);
}
extern "C" void catherine_dev_puzzle_poll(void){
 if(!catherine_dev_puzzle_pending())return;
 uint32_t expired=0;
 {std::lock_guard<std::mutex>g(recipe_lock);if(pending.load(std::memory_order_relaxed)&&CATHERINE_DEV_PUZZLE_NOW()>deadline){expired=generation;clear_locked();}}
 if(expired)catherine_dev_loader_finish_request(expired,-45,"Puzzle entry timed out before the expected native story task appeared.");
}
extern "C" int catherine_dev_puzzle_claim(uint64_t config){
 uint32_t live=0;int32_t recipe=0;
 if(!catherine_dev_loader_recipe(&live,&recipe)||!catherine_dev_puzzle_first_profile((uint32_t)recipe)){catherine_dev_puzzle_cancel();return 0;}
 int result=0;
 {std::lock_guard<std::mutex>g(recipe_lock);
  if(!pending.load(std::memory_order_relaxed))return 0;
  if(CATHERINE_DEV_PUZZLE_NOW()>deadline)result=-45;
  else if(live!=generation||(uint32_t)recipe!=expected_recipe||config!=expected_config)result=-46;
  else result=1;
  clear_locked();
 }
 if(result<0)catherine_dev_loader_finish(result,result==-45?"Puzzle entry expired.":"Puzzle request identity changed before native entry.");
 return result==1;
}
