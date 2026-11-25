#pragma once
#include "../../../../../platform/host_clock.h"
#include "dev_loader_native.h"

void f_700009da10(Cpu *);
void f_700009dad0(Cpu *);
void f_700009d360(Cpu *);
void catherine_dev_title_after_movie(void);

static uint32_t dev_standalone_target;
static uint32_t dev_standalone_request;
static int dev_standalone_started;
static int dev_standalone_retire_story;
static int dev_standalone_reported;
static unsigned long long dev_standalone_deadline;

static void dev_standalone_enqueue(Cpu *cpu, uint32_t target)
{
    Cpu call = *cpu;
    const uint64_t args = (call.sp - 0x40) & ~15ull;
    call.sp = args;
    memset((void *)(uintptr_t)args, 0, 0x18);
    call.x[0] = args;
    f_700009da10(&call);
    const int event = target >= 100000000;
    if (event) {
        const uint32_t id = target - 100000000;
        ST32(args + 0x10, id / 1000);
        ST32(args + 0x14, id % 1000);
    } else {
        ST32(args + 0x10, target - 30000);
        ST8(args + 0x14, 1);
    }
    call.x[0] = event ? 6 : 13;
    call.x[1] = args;
    call.x[2] = 0x18;
    call.x[3] = 2;
    f_700009d360(&call);
    call.x[0] = args;
    f_700009dad0(&call);
}

static void dev_standalone_start(uint32_t target)
{
    CatherineDevLoadStatus status={0};
    status.version=1;
    catherine_dev_loader_status(&status);
    dev_standalone_request=status.request_id;
    dev_standalone_target = target;
    dev_standalone_started = 0;
    dev_standalone_reported = 0;
    dev_standalone_deadline = catherine_host_milliseconds() + 120000;
    catherine_dev_loader_progress(CATHERINE_DEV_TRANSITION, "Opening the selected cutscene.");
}

static int dev_standalone_tick(Cpu *cpu, uint64_t owner)
{
    if (!dev_standalone_target || (LD8(owner) & 1)) return 0;
    const int root = (int32_t)LD32(owner + 4);
    if (!dev_standalone_reported && !catherine_dev_loader_pending() &&
        catherine_host_milliseconds() >= dev_standalone_deadline) {
        dev_standalone_reported=1;
        catherine_dev_loader_finish_request(dev_standalone_request,-53, "The selected cutscene did not begin playback in time.");
    }
    const int intro=dev_standalone_target==40001;
    const int event=dev_standalone_target>=100000000;
    int ready=0, finished=0;
    if (intro && root==3) {
        const uint64_t holder=LD64(0x7000ccb640ull);
        const uint64_t task=holder ? LD64(holder) : 0;
        const uint32_t phase=task ? LD32(task+8) : 0;
        ready=phase==9;
        if (ready) dev_standalone_started=1;
        finished=dev_standalone_started && phase>=10 && phase<=32;
    } else if (event && root==6) {
        const uint32_t id=dev_standalone_target-100000000;
        const uint64_t task=LD64(0x7000d000b8ull);
        if (task && LD32(task+0xc)==id/1000 && LD32(task+0x10)==id%1000) {
            dev_standalone_started=1;
            ready=LD32(task+0x14)==1 && (int32_t)LD32(task+0x1c)>0;
        }
    } else if (!intro && !event && root==13 && catherine_dev_movie_active()) {
        dev_standalone_started=1;
        ready=catherine_dev_movie_playing(dev_standalone_target-30000);
    }
    if (!dev_standalone_reported && !catherine_dev_loader_pending() && ready) {
        dev_standalone_reported=1;
        catherine_dev_loader_finish_request(dev_standalone_request,0,"Playing the selected cutscene.");
    }
    /* Movie and event roots switch to -1 after their native destructor.
       The tower intro instead leaves its own retained task before player input. */
    if (!finished && (root!=-1 || !dev_standalone_started)) return 0;
    if (intro && !catherine_dev_loader_pending())dev_standalone_retire_story=1;
    dev_standalone_target = 0;
    if (catherine_dev_loader_pending()) return 0;
    if (!dev_standalone_reported)
        catherine_dev_loader_finish_request(dev_standalone_request,-53,"The selected cutscene ended before playback began.");
    catherine_dev_loader_cutscene_ended(dev_standalone_request);
    catherine_dev_title_after_movie();
    Cpu call = *cpu;
    const uint64_t args = (call.sp - 0x20) & ~15ull;
    call.sp = args;
    ST32(args, 0x0101);
    call.x[0] = 2;
    call.x[1] = args;
    call.x[2] = 3;
    call.x[3] = 1;
    f_700009d360(&call);
    return 1;
}
