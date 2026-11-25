#pragma once

#if defined(_WIN32) && defined(__x86_64__) && defined(CATHERINE_HIGH_REFRESH_BUILD)
#define NX_CATHERINE_WATCHED_GEOMETRY_BUILD 1

namespace nx::nvn::replay {

inline constexpr uint64_t kWatchedGeometryMinimum = 16ull << 10;
bool watched_geometry_enabled(bool index);
Upload watched_geometry_upload(const uint8_t* p, uint64_t size, uint64_t padded, bool index);

}
#endif
