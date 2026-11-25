#pragma once
#include <cstdlib>
inline bool perf_background_window(){static const bool background=[]{const char* p=std::getenv("CATHERINE_PERF_BACKGROUND");return p && p[0]=='1';}();return background;}
