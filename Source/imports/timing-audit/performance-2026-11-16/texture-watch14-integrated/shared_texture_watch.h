#pragma once
#include <cstdint>
namespace nx::nvn::replay {
// Same GPU-lock-owned epoch broker as geometry: never independently reset its OS watch.
bool texture_write_watch(const uint8_t*,uint64_t,uint64_t&,uint64_t&,bool&);
}
