#pragma once
#include <cstddef>
#include <cstdint>
namespace c02_bridge::direct {
bool read(uint64_t source,void* destination,size_t bytes);
} // namespace c02_bridge::direct
