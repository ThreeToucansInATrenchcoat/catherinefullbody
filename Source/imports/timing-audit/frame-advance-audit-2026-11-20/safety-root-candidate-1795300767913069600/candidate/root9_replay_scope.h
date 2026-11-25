#pragma once
#include <cstdint>
extern "C" {
uint64_t catherine_root9_replay_enter(void);
void catherine_root9_replay_leave(uint64_t);
uint64_t catherine_root9_replay_cookie(void);
}
namespace catherine::root9 {
// Scope an invocation, not a reusable NVN command handle. Restoring the parent
// cookie never restores bindings changed by a nested list.
class ReplayScope {
    uint64_t previous;
public:
    ReplayScope()noexcept:previous(catherine_root9_replay_enter()){}
    ~ReplayScope(){catherine_root9_replay_leave(previous);}
    ReplayScope(const ReplayScope&)=delete;
    ReplayScope& operator=(const ReplayScope&)=delete;
};
}
