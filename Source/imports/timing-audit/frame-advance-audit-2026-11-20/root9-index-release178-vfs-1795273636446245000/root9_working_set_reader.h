#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace catherine::root9::private_reader {
enum class Path : unsigned { rejected, working_set, virtual_query };
inline bool old_read(uint64_t address,void* destination,size_t size){
    if(!address||size>UINT64_MAX-address)return false;
    const uint64_t end=address+size;
    for(uint64_t p=address;p<end;){
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQuery(reinterpret_cast<const void*>(p),&info,sizeof info)||info.State!=MEM_COMMIT||
           (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))||info.Protect==PAGE_EXECUTE)return false;
        const uint64_t base=reinterpret_cast<uint64_t>(info.BaseAddress);
        if(info.RegionSize>UINT64_MAX-base||base+info.RegionSize<=p)return false;
        p=base+info.RegionSize;
    }
    std::memcpy(destination,reinterpret_cast<const void*>(address),size);return true;
}
inline DWORD page_size(){static const DWORD value=[](){SYSTEM_INFO info{};GetSystemInfo(&info);return info.dwPageSize;}();return value;}
// No permission/address cache. Each fast read obtains fresh page attributes.
// As with the previous VirtualQuery + memcpy sequence, protection changes from
// another thread between query and copy remain a native TOCTOU possibility.
template<class Query>inline bool read_with_query(uint64_t address,void* destination,size_t size,Path* selected,Query query){
    if(selected)*selected=Path::rejected;
    if(!address||size>UINT64_MAX-address)return false;
    const uint64_t pages=page_size();
    if(size&&pages){
        const uint64_t first=address-address%pages;
        const uint64_t last=(address+size-1)/pages*pages;
        if(last==first||last-first==pages){
            PSAPI_WORKING_SET_EX_INFORMATION info[2]{};
            info[0].VirtualAddress=reinterpret_cast<void*>(first);
            info[1].VirtualAddress=reinterpret_cast<void*>(last);
            const unsigned count=last==first?1:2;
            if(query(GetCurrentProcess(),info,DWORD(count*sizeof(info[0])))){
                bool complete=true;
                for(unsigned i=0;i<count;++i){
                    if(!info[i].VirtualAttributes.Valid){
                        if(info[i].VirtualAttributes.Invalid.Bad)return false;
                        complete=false;continue;
                    }
                    const DWORD protection=DWORD(info[i].VirtualAttributes.Win32Protection);
                    if(info[i].VirtualAttributes.Bad||(protection&(PAGE_NOACCESS|PAGE_GUARD))||(protection&0xff)==PAGE_EXECUTE)return false;
                    const DWORD base=protection&0xff;
                    const bool readable=base==PAGE_READONLY||base==PAGE_READWRITE||base==PAGE_WRITECOPY||
                        base==PAGE_EXECUTE_READ||base==PAGE_EXECUTE_READWRITE||base==PAGE_EXECUTE_WRITECOPY;
                    const DWORD known=0xff|PAGE_GUARD|PAGE_NOCACHE|PAGE_WRITECOMBINE;
                    if(!readable||(protection&~known))complete=false;

                }
                if(complete){
                    std::memcpy(destination,reinterpret_cast<const void*>(address),size);
                    if(selected)*selected=Path::working_set;
                    return true;
                }
            }
        }
    }
    if(selected)*selected=Path::virtual_query;
    return old_read(address,destination,size);
}
inline bool read(uint64_t address,void* destination,size_t size,Path* selected=nullptr){
    return read_with_query(address,destination,size,selected,[](HANDLE process,void* data,DWORD size){return K32QueryWorkingSetEx(process,data,size);});
}
}
