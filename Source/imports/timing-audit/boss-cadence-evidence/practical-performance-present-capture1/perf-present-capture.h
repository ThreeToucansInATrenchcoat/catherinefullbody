#pragma once
#include <cstdint>
#include <cstddef>
namespace catherine_perf {
constexpr uint64_t magic=0x3146504143544143ull;
constexpr uint32_t capacity=131072;
struct alignas(64) Header {
 uint64_t magic_value; uint32_t version, entry_size;
 uint64_t frequency, start_qpc; uint32_t pid, entries_capacity;
 volatile int64_t committed; uint64_t reserved[2];
};
struct Entry { volatile int64_t sequence; int64_t qpc; uint64_t present; int32_t result; uint32_t thread; };
static_assert(sizeof(Header)==64 && sizeof(Entry)==32);
constexpr size_t mapping_bytes=sizeof(Header)+size_t(capacity)*sizeof(Entry);
}
#ifdef _WIN32
#include <windows.h>
#include <cstdlib>
#include <cstdio>
namespace catherine_perf {
class Capture {
 HANDLE mapping=nullptr; Header* h=nullptr; uint64_t count=0;
public:
 Capture() {
  const char* enabled=std::getenv("CATHERINE_PERF_CAPTURE");
  if(!enabled || enabled[0]!='1' || enabled[1])return;
  char name[96];std::snprintf(name,sizeof name,"Local\\CatherinePerfCapture-%lu",GetCurrentProcessId());
  mapping=CreateFileMappingA(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,DWORD(mapping_bytes),name);
  if(!mapping)return;
  if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(mapping);mapping=nullptr;return;}
  h=(Header*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,mapping_bytes);
  if(!h){CloseHandle(mapping);mapping=nullptr;return;}
  LARGE_INTEGER freq,start;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&start);
  h->version=1;h->entry_size=sizeof(Entry);h->frequency=freq.QuadPart;h->start_qpc=start.QuadPart;
  h->pid=GetCurrentProcessId();h->entries_capacity=capacity;
  MemoryBarrier();InterlockedExchange64((volatile LONG64*)&h->magic_value,magic);
 }
 // Process-lifetime mapping intentionally retained; no shutdown callback/IO.
 void record(uint64_t present,int32_t result) {
  if(!h || count==capacity)return;
  LARGE_INTEGER now;QueryPerformanceCounter(&now);
  auto* entries=(Entry*)((uint8_t*)h+sizeof(Header));Entry& e=entries[count];
  e.qpc=now.QuadPart;e.present=present;e.result=result;e.thread=GetCurrentThreadId();
  // Append-only: payload publication before sequence and count, no slot reuse.
  InterlockedExchange64((volatile LONG64*)&e.sequence,count+1);
  ++count;InterlockedExchange64((volatile LONG64*)&h->committed,count);
 }
};
inline void record_present(uint64_t present,int32_t result) { static Capture capture;capture.record(present,result); }
}
#endif
