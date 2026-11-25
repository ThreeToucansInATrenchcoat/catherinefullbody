#ifdef _WIN32
#include "c02_guarded_read.h"
#include <cstring>
#include <immintrin.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace c02_bridge::direct {
struct Fault {unsigned code=0;void* address=nullptr;};
static int memory_fault(unsigned code,EXCEPTION_POINTERS* info,Fault* fault) {
  if(code!=EXCEPTION_ACCESS_VIOLATION&&code!=EXCEPTION_IN_PAGE_ERROR&&code!=EXCEPTION_GUARD_PAGE)
    return EXCEPTION_CONTINUE_SEARCH;
  fault->code=code;
  if(info&&info->ExceptionRecord&&info->ExceptionRecord->NumberParameters>=2)
    fault->address=reinterpret_cast<void*>(info->ExceptionRecord->ExceptionInformation[1]);
  return EXCEPTION_EXECUTE_HANDLER;
}
// Compile ONLY this small TU with -fasync-exceptions. Keep the SEH region in
// its own noinline leaf; there are no C++ objects requiring unwinding.
// Failed reads may change only caller scratch, which must be discarded on false.
__declspec(noinline) bool read(uint64_t source,void* destination,size_t bytes) {
  if(!source||(!destination&&bytes)||bytes>UINTPTR_MAX-source||
     bytes>UINTPTR_MAX-reinterpret_cast<uintptr_t>(destination))return false;
  if(!bytes)return true;
  const unsigned incoming=_mm_getcsr();bool result=false;Fault fault;
  __try {std::memcpy(destination,reinterpret_cast<const void*>(source),bytes);result=true;}
  __except(memory_fault(GetExceptionCode(),GetExceptionInformation(),&fault)) {
    // Reading a guard page consumes its guard. Self-RPM leaves it intact, so
    // restore only the exact fault page, on this cold failure path. Passing1
    // byte lets VirtualProtect round to this page even if VirtualQuery merged
    // it with adjacent regions after the fault. Never protect RegionSize.
    if(fault.code==EXCEPTION_GUARD_PAGE&&fault.address){
      MEMORY_BASIC_INFORMATION region{};
      if(VirtualQuery(fault.address,&region,sizeof region)==sizeof region&&region.State==MEM_COMMIT&&!(region.Protect&PAGE_GUARD)){
        DWORD previous=0;VirtualProtect(fault.address,1,region.Protect|PAGE_GUARD,&previous);
      }
    }
    result=false;
  }
  _mm_setcsr(incoming);return result;
}
} // namespace c02_bridge::direct

#include "c02_guarded_io_v5.h"
namespace c02_bridge::direct {
// The only production callers pass a computed stack array and a fresh render
// span that the original native function has just written (64 or5696 bytes).
// Failure never creates ownership proof. Like original WPM, failure need not
// leave the writable prefix unchanged; this is deliberately not transactional.
__declspec(noinline) bool write(uint64_t destination,const void* source,size_t bytes) {
  const uintptr_t input=reinterpret_cast<uintptr_t>(source);
  if(!destination||(!source&&bytes)||bytes>UINTPTR_MAX-destination||bytes>UINTPTR_MAX-input)return false;
  if(!bytes)return true;
  // The two native copy call sites are disjoint. Reject an unsupported overlap
  // rather than invoking memcpy with an overlapping source/destination.
  if(destination<input+bytes&&input<destination+bytes)return false;
  const unsigned incoming=_mm_getcsr();bool result=false;Fault fault;
  __try {std::memcpy(reinterpret_cast<void*>(destination),source,bytes);result=true;}
  __except(memory_fault(GetExceptionCode(),GetExceptionInformation(),&fault)) {
    // Measured self-WPM consumes a DESTINATION guard, but retains a SOURCE
    // guard. Restore only the latter, on exactly the fault page, cold path only.
    const uintptr_t at=reinterpret_cast<uintptr_t>(fault.address);
    if(fault.code==EXCEPTION_GUARD_PAGE&&fault.address&&at>=input&&at-input<bytes){
      MEMORY_BASIC_INFORMATION region{};
      if(VirtualQuery(fault.address,&region,sizeof region)==sizeof region&&region.State==MEM_COMMIT&&!(region.Protect&PAGE_GUARD)){
        DWORD previous=0;VirtualProtect(fault.address,1,region.Protect|PAGE_GUARD,&previous);
      }
    }
    result=false;
  }
  _mm_setcsr(incoming);return result;
}
} // namespace c02_bridge::direct

#else
#include "c02_guarded_io_v5.h"
#include "../../../platform/host_memory.h"
#include <cstdint>

bool c02_bridge::direct::read(uint64_t source, void* destination, size_t bytes) {
    if (!source || (!destination && bytes) || bytes > UINTPTR_MAX - source ||
        bytes > UINTPTR_MAX - reinterpret_cast<uintptr_t>(destination)) return false;
    return catherine_host_read(reinterpret_cast<const void*>(source), destination, bytes);
}

bool c02_bridge::direct::write(uint64_t destination, const void* source, size_t bytes) {
    const uintptr_t input = reinterpret_cast<uintptr_t>(source);
    if (!destination || (!source && bytes) || bytes > UINTPTR_MAX - destination ||
        bytes > UINTPTR_MAX - input) return false;
    if (!bytes) return true;
    if (destination < input + bytes && input < destination + bytes) return false;
    const iovec local = {const_cast<void*>(source), bytes};
    const iovec remote = {reinterpret_cast<void*>(destination), bytes};
    const long transferred = syscall(SYS_process_vm_writev, getpid(), &local, 1UL, &remote, 1UL, 0UL);
    return transferred >= 0 && static_cast<size_t>(transferred) == bytes;
}
#endif
