#pragma once
#include <cstdint>
namespace catherine::refresh {
// Strict decimal policy: malformed, unavailable, or unsupported values preserve native pacing.
inline int parse_rate(const char* value) {
    if (!value || !*value) return 30;
    unsigned rate = 0;
    for (const char* p = value; *p; ++p) {
        if (*p < '0' || *p > '9') return 30;
        rate = rate * 10 + unsigned(*p - '0');
        if (rate > 360) return 30;
    }
    return rate >= 30 ? int(rate) : 30;
}
struct Policy {
    bool high;
    bool movie_capped;
    bool unpaced;
    uint64_t period;
};
inline Policy policy(int requested, bool movie) {
    const bool unpaced = requested == 0 && !movie;
    const int target = movie || requested < 0 ? 30 : requested;
    return {!movie && (unpaced || target > 30), movie, unpaced,
            unpaced ? 0 : 1'000'000'000ull / unsigned(target)};
}
// Same no-catch-up deadline progression as production's legacy interval pacer.
inline uint64_t next_deadline(uint64_t next, uint64_t now, uint64_t period) {
    return (next > now || now - next < period ? next : now) + period;
}
}
