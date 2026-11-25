#pragma once
#include "c02_guarded_read.h"
namespace c02_bridge::direct {
bool write(uint64_t destination,const void* source,size_t bytes);
}
