#include "catherine_resolution_live.h"
#include <atomic>
#include <mutex>
#include <cstdlib>
#include <cstdio>
extern "C" unsigned catherine_internal_render_height(void);
namespace {
struct State {
    std::mutex lock;
    uint64_t issued=0, desired_generation=0, completed=0;
    unsigned desired_height=0, applied=0, status=CATHERINE_RESOLUTION_IDLE;
    bool claimed=false;
    std::atomic<bool> pending{false};
};
State& state() { static State s; return s; }
enum class Phase { Direct, AwaitDesired, Restore };
struct Transaction {
    uint64_t generation=0;
    unsigned desired=0, previous=0, step=0, attempts=0, cooldown=0;
    Phase phase=Phase::Direct;
    bool applying=false, event_ready=false;
};
thread_local Transaction tx;
void trace(const char* event,unsigned height=0) {
    if (catherine_resolution_trace_enabled())
        std::fprintf(stderr,"LiveResolution event=%s generation=%llu desired=%u previous=%u height=%u phase=%u attempt=%u\n",
            event,(unsigned long long)tx.generation,tx.desired,tx.previous,height,unsigned(tx.phase),tx.attempts);
}
bool valid(unsigned h) { return h==720 || h==1080 || h==1440 || h==2160; }
void complete(bool success) {
    trace(success ? "applied" : "rejected",catherine_resolution_applied_height());
    auto& s=state(); std::lock_guard guard(s.lock);
    s.completed=tx.generation; s.claimed=false;
    const bool pending=s.desired_generation>s.completed;
    s.status=pending ? CATHERINE_RESOLUTION_PENDING :
        (success ? CATHERINE_RESOLUTION_APPLIED : CATHERINE_RESOLUTION_REJECTED);
    s.pending.store(pending,std::memory_order_release); tx={};
}
void set_applied(unsigned h) { auto& s=state(); std::lock_guard guard(s.lock); s.applied=h; }
void later() { tx.event_ready=false; tx.cooldown=3; }
}
extern "C" uint64_t catherine_resolution_request(unsigned h) {
    if (!valid(h)) return 0;
    auto& s=state(); std::lock_guard guard(s.lock);
    s.desired_height=h; s.desired_generation=++s.issued;
    if (!s.claimed) s.status=CATHERINE_RESOLUTION_PENDING;
    s.pending.store(true,std::memory_order_release);
    if (catherine_resolution_trace_enabled())
        std::fprintf(stderr,"LiveResolution event=request generation=%llu height=%u\n",(unsigned long long)s.issued,h);
    return s.issued;
}
extern "C" unsigned catherine_resolution_applied_height(void) {
    auto& s=state(); std::lock_guard guard(s.lock);
    return s.applied ? s.applied : catherine_internal_render_height();
}
extern "C" unsigned catherine_resolution_status(void) {
    auto& s=state(); std::lock_guard guard(s.lock); return s.status;
}
extern "C" uint64_t catherine_resolution_completed_generation(void) {
    auto& s=state(); std::lock_guard guard(s.lock); return s.completed;
}
extern "C" int catherine_resolution_claim_event(void) {
    if (tx.generation) {
        if (tx.applying || tx.event_ready) return 0;
        if (tx.cooldown) { --tx.cooldown; return 0; }
        tx.event_ready=true; return 1;
    }
    auto& s=state();
    if (!s.pending.load(std::memory_order_acquire)) return 0;
    std::lock_guard guard(s.lock);
    if (s.claimed || s.desired_generation<=s.completed) return 0;
    tx.generation=s.desired_generation; tx.desired=s.desired_height;
    tx.previous=s.applied ? s.applied : catherine_internal_render_height();
    tx.event_ready=true; s.claimed=true; s.status=CATHERINE_RESOLUTION_APPLYING;
    s.pending.store(false,std::memory_order_release); trace("claim",tx.desired); return 1;
}
extern "C" int catherine_resolution_before_apply(void *cpu) {
    if (!tx.event_ready) return 1; // Original applet event, not our claimed event.
    if (!valid(tx.previous)) {
        tx.previous=catherine_resolution_native_height();
        if (valid(tx.previous)) set_applied(tx.previous);
    }
    if (tx.phase==Phase::Direct && tx.desired==tx.previous) { complete(true); return 0; }
    if (!valid(tx.previous) || !catherine_resolution_drain()) { complete(false); return 0; }
    if (tx.phase==Phase::Direct) {
        trace("reserve",tx.desired);
        int r=catherine_resolution_heap_prepare(cpu,tx.desired);
        if (r==1) { tx.step=tx.desired; tx.applying=true; return 1; }
        if (r<0) { complete(false); return 0; }
        // All smaller backing is reserved before touching old targets. Normal
        // native deferred destruction continues over subsequent guest frames.
        if (tx.previous!=720 && tx.desired!=720 && catherine_resolution_heap_prepare(cpu,720)==1) {
            trace("stage720",720);
            tx.step=720; tx.applying=true; return 1;
        }
        // A preceding change may have left deferred target releases in flight.
        tx.phase=Phase::AwaitDesired; later(); return 0;
    }
    unsigned target=tx.phase==Phase::Restore ? tx.previous : tx.desired;
    trace("retry-reserve",target);
    int r=catherine_resolution_heap_prepare(cpu,target);
    if (r==1) { tx.step=target; tx.applying=true; return 1; }
    if (r<0 || ++tx.attempts>=16) {
        if (tx.phase==Phase::Restore || tx.previous==catherine_resolution_applied_height()) {
            complete(false); return 0; // Retain the last successfully constructed set.
        }
        tx.phase=Phase::Restore; tx.attempts=0;
        trace("restore",tx.previous);
    }
    later(); return 0;
}
extern "C" void catherine_resolution_after_apply(void *cpu) {
    if (!tx.event_ready) return;
    // A post-destruction ABI mismatch cannot be repaired by restoring a setting.
    if (!tx.applying || !catherine_resolution_heap_finish(cpu,tx.step)) std::abort();
    set_applied(tx.step); tx.applying=false;
    trace("verified",tx.step);
    if (tx.step==tx.desired) { complete(true); return; }
    if (tx.phase==Phase::Restore) { complete(false); return; }
    tx.phase=Phase::AwaitDesired; tx.attempts=0; later();
}
extern "C" unsigned catherine_resolution_selected_height(void) {
    return tx.applying ? tx.step : catherine_resolution_applied_height();
}
extern "C" int catherine_resolution_trace_enabled(void) {
    static const bool enabled=[] { const char *e=std::getenv("NX_RESOLUTION_TRACE"); return e && e[0]=='1'; }();
    return enabled;
}
