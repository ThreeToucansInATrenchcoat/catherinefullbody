#pragma once

#include <cstdarg>

namespace nx {

struct PeriodicRenderLogQueue;
struct PeriodicRenderLogBatch;

// Periodic diagnostics only. Producers try-lock one bounded batch; contention
// or a full queue drops the batch rather than waiting for another producer/sink.
class PeriodicRenderLogScope {
public:
    PeriodicRenderLogScope();
    ~PeriodicRenderLogScope();
    PeriodicRenderLogScope(const PeriodicRenderLogScope&) = delete;
    PeriodicRenderLogScope& operator=(const PeriodicRenderLogScope&) = delete;

private:
    friend void log(const char* format, ...);
    friend void periodic_log(const char* format, ...);
    void append(const char* format, va_list args);
    PeriodicRenderLogQueue* queue_ = nullptr;
    PeriodicRenderLogBatch* batch_ = nullptr;
};

void start_periodic_render_log();
// Safe on guest recording/submission threads as well as inside a GPU scope.
// Unlike log(), this never falls back to synchronous output if the queue is absent.
void periodic_log(const char* format, ...);

}  // namespace nx
