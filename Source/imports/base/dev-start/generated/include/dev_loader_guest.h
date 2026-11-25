#pragma once
#include "dev_loader_native.h"
#include "karma-native.h"
#include "difficulty-native.h"
#include "puzzle_recipe_guest.h"
extern int catherine_dev_save_begin(const char *template_utf8_path);
void f_700003bae0(Cpu*); void f_700003bb30(Cpu*);
void f_700003ca40(Cpu*); void f_700003caf0(Cpu*);
void f_700009cd00(Cpu*); void f_700009bed0(Cpu*); void f_700009c1a0(Cpu*);
void f_7000026530(Cpu*); void f_700009a380(Cpu*); void f_70005c8060(Cpu*);
void f_7000294a00(Cpu*); void f_700008c7d0(Cpu*);

/* Called only in place of f295ad0's native SaveLoad UI transaction, from the
   piano coroutine's ordinary f294a00 load continuation. No scheduler nesting. */
static int catherine_dev_native_load(Cpu *c) {
 char path[1024];int32_t slot=-1;
 if(!catherine_dev_loader_begin(path,sizeof(path),&slot))return 0;
 const int shadow=catherine_dev_save_begin(path);
 catherine_dev_loader_release_archive();
 if(shadow!=1){catherine_dev_loader_finish(shadow?shadow:-10,"Cannot isolate saves while native file handles are open; return to the piano and retry.");return 0;}
 uint64_t preserved[12];__uint128_t vectors[8];
 memcpy(preserved,c->x+19,sizeof(preserved));memcpy(vectors,c->v+8,sizeof(vectors));
 const uint64_t original_sp=c->sp;const uint64_t buffer=(original_sp-0x8000ull)&~15ull;
 c->sp=buffer;
 const uint64_t holder=LD64(0x7000cc93b0ull);const uint64_t manager=holder?LD64(holder):0;
 int archive_owned=0;
 int result=0,error=-11;const char *error_text="Native save manager is not initialized.";
 if(manager&&slot>=0&&slot<20){
  const uint64_t archive_holder=LD64(0x7000cc94d0ull);
  if(!archive_holder||LD64(archive_holder)){
   error=-15;error_text="The native SaveLoad archive is already owned by another operation.";goto catherine_dev_native_restore_done;
  }
  /* SaveLoad native initialization: virtual+28 allocates the shared archive and
     initializes the IO request. Calling request_read alone passes a null buffer. */
  c->x[0]=manager;f_700003bae0(c);archive_owned=1;
  if(!LD64(archive_holder)){
   error=-16;error_text="The native SaveLoad archive allocation failed.";goto catherine_dev_native_restore_done;
  }
  memset((void*)(uintptr_t)buffer,0,0x8000);
  c->x[0]=manager;c->x[1]=(uint32_t)slot;c->x[2]=buffer;f_700003ca40(c);
  for(;;){
   c->x[0]=manager;f_700003caf0(c);
   if((uint32_t)c->x[0])break;
   /* Original SaveLoad also waits cooperatively. Never abandon a pending read
      whose destination points at this coroutine's guest stack. */
   f_700009cd00(c);
  }
  error=-12;error_text="The native save read failed.";
  if((uint32_t)c->x[0]==1){
   error=-13;error_text="Unsupported or empty earned native slot.";
   if(LD8(buffer)==slot&&LD8(buffer+1)==1&&LD32(buffer+0x3674)==0x10300){
    c->x[0]=buffer+0x48;f_7000026530(c);
    error=-14;error_text="The native content entitlement check rejected this checkpoint.";
    if(c->x[0]&1){
     f_700009bed0(c);const uint64_t config=c->x[0];
     ST8(config+0xce85,1);
     const uint64_t ui=LD64(0x7000cc9060ull);if(ui)ST8(ui+0x29,1);
     c->x[0]=config+0x9818;c->x[1]=buffer+0x48;c->x[2]=0x3634;f_70005c8060(c);
     int32_t karma_enabled=0,karma_value=0,karma_readback=0;
     if(!catherine_dev_loader_karma(&karma_enabled,&karma_value)){
      error=-30;error_text="The DEV load request lost its state snapshot.";goto catherine_dev_native_restore_done;
     }
     const int karma_result=catherine_dev_karma_apply(c,config,karma_enabled,karma_value,&karma_readback);
     if(karma_result<0){error=karma_result;error_text="The native karma override failed validation.";goto catherine_dev_native_restore_done;}
     int32_t difficulty=-1,difficulty_readback=0;
     if(!catherine_dev_loader_difficulty(&difficulty)){error=-40;error_text="The DEV difficulty snapshot is unavailable.";goto catherine_dev_native_restore_done;}
     const int difficulty_result=catherine_dev_difficulty_apply(config,difficulty,&difficulty_readback);
     if(difficulty_result<0){error=difficulty_result;error_text="The native difficulty override is not supported for this checkpoint.";goto catherine_dev_native_restore_done;}
     const int recipe_result=catherine_dev_puzzle_restore(c,config);
     if(recipe_result<0){error=recipe_result;error_text="The native puzzle entry recipe failed validation.";goto catherine_dev_native_restore_done;}
     const uint32_t elapsed=LD32(config+0xce40);
     f_700009c1a0(c);ST32(c->x[0]+0x7c,elapsed);
     c->x[0]=config;c->x[1]=0x201;c->x[2]=1;f_700009a380(c);
     /* Exact normal f295ad0 L295e28 success fade request. The unchanged
        f294a00 suffix waits for native f8c900 completion; never bypass its wait. */
     const uint64_t fade_holder=LD64(0x7000cc9488ull);
     const uint64_t fade=fade_holder?LD64(fade_holder):0;
     const uint64_t color=LD64(0x7000cc9948ull);
     if(!fade||!color){error=-17;error_text="The native fade service is unavailable.";goto catherine_dev_native_restore_done;}
     c->x[0]=fade;c->x[1]=0;c->x[2]=color;c->v[0]=0x3f400000u;f_700008c7d0(c);
     catherine_dev_loader_progress(CATHERINE_DEV_TRANSITION,"Native restore accepted; entering saved scene.");
     result=1;
    }
   }
  }
 }
catherine_dev_native_restore_done:;
 if(archive_owned){c->x[0]=manager;f_700003bb30(c);}
 c->sp=original_sp;memcpy(c->x+19,preserved,sizeof(preserved));memcpy(c->v+8,vectors,sizeof(vectors));
 if(!result)catherine_dev_loader_finish(error,error_text);
 return result;
}
