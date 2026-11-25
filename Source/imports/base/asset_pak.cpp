// ZIP64 asset reader. Standard ZIP entries, raw Deflate, CRC on every cache fill.
// The archive and its bounded RAM cache live for the process. Never extracts files.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <filesystem>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "asset_pak.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include "asset_setup_impl.h"
#elif defined(__linux__) && !defined(__ANDROID__)
#include "../../platform/shader_translation_linux.inc"
#endif
#ifndef MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_ZLIB_APIS
#endif
#ifndef MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_DEFLATE_APIS
#endif
#ifndef MINIZ_NO_STDIO
#define MINIZ_NO_STDIO
#endif
#include "third_party/miniz/miniz.h"
namespace {
constexpr uint64_t block_size=256*1024, max_entry=512ull*1024*1024;
constexpr char manifest_name[]="catherine-pak.manifest";
struct Entry { mz_uint index; uint64_t size; uint32_t crc; uint16_t method; };
struct CachedBlock { uint64_t index=UINT64_MAX,last=0; std::vector<unsigned char> bytes; };
struct State {
    std::once_flag initialized;
    std::mutex lock;
#ifdef _WIN32
    HANDLE file=INVALID_HANDLE_VALUE;
#else
    int file=-1;
#endif
    uint64_t file_size=0,romfs_size=0,tick=0;
    mz_zip_archive zip{};
    int mode=0;
    std::string failure;
    std::map<std::string,Entry> entries;
    std::array<CachedBlock,128> cache; // At most32MiB uncompressed; no on-disk cache.
};
State& state(){static State* s=new State;return *s;}
thread_local std::string last_error;
[[noreturn]] void fail(const std::string& why){
    #ifdef _WIN32
    FILE* log=nullptr;fopen_s(&log,"assets-errors.log","a");
#else
    FILE* log=std::fopen("assets-errors.log","a");
#endif
    if(log){fprintf(log,"assets.pak: %s\n",why.c_str());fclose(log);}
    throw std::runtime_error(why);
}
size_t read_archive(void* opaque,mz_uint64 offset,void* destination,size_t count){
    auto& s=*static_cast<State*>(opaque);
    if(offset>s.file_size || count>s.file_size-offset)return 0;
#ifdef _WIN32
    LARGE_INTEGER where;where.QuadPart=static_cast<LONGLONG>(offset);
    if(!SetFilePointerEx(s.file,where,nullptr,FILE_BEGIN))return 0;
    size_t done=0;
    while(done<count){DWORD got=0,part=DWORD(std::min<size_t>(count-done,1u<<20));
        if(!ReadFile(s.file,static_cast<unsigned char*>(destination)+done,part,&got,nullptr)||!got)break;
        done+=got;
    }
#else
    size_t done=0;
    while(done<count){
        const size_t part=std::min<size_t>(count-done,1u<<20);
        const ssize_t got=pread(s.file,static_cast<unsigned char*>(destination)+done,
                               part,static_cast<off_t>(offset+done));
        if(got<0 && errno==EINTR)continue;
        if(got<=0)break;
        done+=static_cast<size_t>(got);
    }
#endif
    return done;
}
bool valid_name(const std::string& name){
    if(name.empty()||name.size()>=512||name.front()=='/'||name.back()=='/')return false;
    for(unsigned char ch:name)if(ch<32||ch>=127||ch=='\\'||ch==':')return false;
    size_t at=0;
    for(;;){size_t end=name.find('/',at);auto component=name.substr(at,end-at);
        if(component.empty()||component=="."||component=="..")return false;
        if(end==std::string::npos)return true;at=end+1;
    }
}
const Entry& find(State& s,const char* name){
    auto it=s.entries.find(name?name:"");
    if(it==s.entries.end())fail(std::string("Package entry missing: ")+(name?name:"(null)"));
    return it->second;
}
void extract(State& s,const Entry& e,void* destination,size_t count){
    if(count!=e.size || (!destination&&count))fail("Package output size mismatch");
    unsigned char dummy=0;
    if(!mz_zip_reader_extract_to_mem(&s.zip,e.index,count?destination:&dummy,count,0))
        fail(std::string("Package inflate/CRC failure: ")+mz_zip_get_error_string(mz_zip_get_last_error(&s.zip)));
}
uint32_t directory_crc(const State& s){
    mz_ulong crc=0;
    for(const auto& pair:s.entries){
        if(pair.first==manifest_name)continue;
        const auto& name=pair.first;const Entry& e=pair.second;
        unsigned char length[2]={static_cast<unsigned char>(name.size()),static_cast<unsigned char>(name.size()>>8)};
        unsigned char metadata[14];
        for(unsigned i=0;i<8;++i)metadata[i]=static_cast<unsigned char>(e.size>>(8*i));
        for(unsigned i=0;i<4;++i)metadata[8+i]=static_cast<unsigned char>(e.crc>>(8*i));
        metadata[12]=static_cast<unsigned char>(e.method);metadata[13]=static_cast<unsigned char>(e.method>>8);
        crc=mz_crc32(crc,length,2);crc=mz_crc32(crc,reinterpret_cast<const unsigned char*>(name.data()),name.size());crc=mz_crc32(crc,metadata,sizeof metadata);
    }
    return static_cast<uint32_t>(crc);
}
void validate_manifest(State& s){
    const auto& manifest=find(s,manifest_name);
    if(manifest.size>512)fail("Package manifest is oversized");
    std::string text(static_cast<size_t>(manifest.size),'\0');extract(s,manifest,text.data(),text.size());
    unsigned long long romfs=0,block=0,entries=0;unsigned crc=0;int end=0;
    if(std::sscanf(text.c_str(),"CATHERINE_PAK 1\nROMFS_SIZE %llu\nBLOCK_SIZE %llu\nENTRY_COUNT %llu\nDIRECTORY_CRC32 %x\n%n",&romfs,&block,&entries,&crc,&end)!=4 ||
        size_t(end)!=text.size() || !romfs || block!=block_size || entries+1!=s.entries.size() || crc!=directory_crc(s))
        fail("Package manifest/directory mismatch");
    s.romfs_size=romfs;
    const uint64_t blocks=romfs/block_size+(romfs%block_size!=0);
    if(blocks>s.entries.size())fail("Package ROMFS chunk count is invalid");
    for(uint64_t i=0;i<blocks;++i){char name[64];std::snprintf(name,sizeof name,"romfs/blocks/%06llu.bin",static_cast<unsigned long long>(i));
        const Entry& e=find(s,name);if(e.size!=std::min<uint64_t>(block_size,romfs-i*block_size))fail("Package ROMFS chunk length mismatch");
    }
    size_t actual_blocks=0;for(const auto& pair:s.entries)if(pair.first.compare(0,13,"romfs/blocks/")==0)++actual_blocks;
    if(actual_blocks!=blocks)fail("Unexpected package ROMFS chunks");
    for(const char* name:{"main.image","subsdk0.image","subsdk1.image","sdk.image","spirv/results.tsv"})
        if(!find(s,name).size)fail(std::string("Empty required package entry: ")+name);
}
void open_archive(State& s,const wchar_t* supplied,const char* native_path){
#ifdef _WIN32
    (void)native_path;
    std::wstring path;
    if(supplied)path=supplied;
    else {wchar_t name[32768];DWORD count=GetModuleFileNameW(nullptr,name,32768);
        if(!count||count>=32768)fail("Cannot locate executable for assets.pak");
        path.assign(name,count);auto slash=path.find_last_of(L"\\/");if(slash==path.npos)fail("Cannot locate executable folder");
        path.resize(slash+1);path+=L"assets.pak";
    }
    s.file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_RANDOM_ACCESS,nullptr);
    if(s.file==INVALID_HANDLE_VALUE){
        const DWORD error=GetLastError();
        if(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND){
            if(supplied)fail("The specified assets.pak does not exist");
            s.mode=0;return;
        }
        fail("Cannot open assets.pak (Windows error "+std::to_string(error)+")");
    }
    LARGE_INTEGER length;if(!GetFileSizeEx(s.file,&length)||length.QuadPart<22)fail("Package is truncated");s.file_size=uint64_t(length.QuadPart);
#else
    static_assert(sizeof(off_t)>=sizeof(int64_t));
    auto path=native_path?std::filesystem::path(native_path)
                        :supplied?std::filesystem::path(supplied)
                                 :std::filesystem::read_symlink("/proc/self/exe").parent_path()/"assets.pak";
    s.file=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NONBLOCK);
    if(s.file<0){
        const int error=errno;
        if(error==ENOENT||error==ENOTDIR){
            if(supplied||native_path)fail("The specified assets.pak does not exist");
            s.mode=0;return;
        }
        fail("Cannot open assets.pak: "+std::string(std::strerror(error)));
    }
    struct stat info{};
    if(fstat(s.file,&info)<0||!S_ISREG(info.st_mode))fail("assets.pak is not a readable regular file");
    if(info.st_size<22)fail("Package is truncated");
    s.file_size=static_cast<uint64_t>(info.st_size);
#endif
    s.zip.m_pRead=read_archive;s.zip.m_pIO_opaque=&s;
    if(!mz_zip_reader_init(&s.zip,s.file_size,0))fail("Invalid ZIP/ZIP64 package directory");
    mz_uint count=mz_zip_reader_get_num_files(&s.zip);if(!count||count>1000000)fail("Invalid package entry count");
    for(mz_uint i=0;i<count;++i){mz_zip_archive_file_stat stat{};
        if(!mz_zip_reader_file_stat(&s.zip,i,&stat)||!stat.m_is_supported||stat.m_is_encrypted||stat.m_is_directory||
            stat.m_uncomp_size>max_entry||(stat.m_method!=0&&stat.m_method!=8)||
            mz_zip_reader_get_filename(&s.zip,i,nullptr,0)>sizeof stat.m_filename)
            fail("Unsupported package entry");
        std::string name=stat.m_filename;if(!valid_name(name))fail("Invalid package entry path");
        if(!s.entries.emplace(name,Entry{i,stat.m_uncomp_size,stat.m_crc32,stat.m_method}).second)fail("Duplicate package entry");
    }
    validate_manifest(s);s.mode=1;
}
int initialize(const wchar_t* supplied,const char* native_path=nullptr){
    State& s=state();
    std::call_once(s.initialized,[&]{
        try{
            open_archive(s,supplied,native_path);
        }catch(const std::exception& e){
            s.failure=e.what();
            s.mode=-1;
            if(s.zip.m_pState)mz_zip_reader_end(&s.zip);
#ifdef _WIN32
            if(s.file!=INVALID_HANDLE_VALUE){
                CloseHandle(s.file);
                s.file=INVALID_HANDLE_VALUE;
            }
#else
            if(s.file>=0){
                close(s.file);
                s.file=-1;
            }
#endif
            s.entries.clear();
        }
    });
    if(s.mode<0)last_error=s.failure;return s.mode;
}
State& required(){if(initialize(nullptr)!=1)fail(state().mode<0?state().failure:"assets.pak is absent");return state();}
}
extern "C" int asset_pak_initialize(void){return initialize(nullptr);}
extern "C" int asset_pak_initialize_from(const wchar_t* path){return initialize(path);}
#ifndef _WIN32
extern "C" int asset_pak_initialize_from_native_path(const char* path){return initialize(nullptr,path);}
#endif
#ifdef CATHERINE_PAK_TESTING
extern "C" int asset_pak_initialize_for_test(const wchar_t* path){return initialize(path);}
#endif
extern "C" const char* asset_pak_error(void){return last_error.c_str();}
extern "C" void asset_pak_free(void* allocation){std::free(allocation);}
extern "C" int asset_pak_read(const char* name,void* output,size_t size){try{auto& s=required();std::lock_guard guard(s.lock);extract(s,find(s,name),output,size);return 1;}catch(const std::exception& e){last_error=e.what();return 0;}}
extern "C" int asset_pak_contains(const char* name){try{auto& s=required();std::lock_guard guard(s.lock);return name&&s.entries.find(name)!=s.entries.end();}catch(const std::exception& e){last_error=e.what();return 0;}}
extern "C" int asset_pak_read_alloc(const char* name,void** output,size_t* size){
    if(!output||!size)return 0;*output=nullptr;*size=0;
    try{auto& s=required();std::lock_guard guard(s.lock);const Entry& e=find(s,name);
        void* data=std::malloc(static_cast<size_t>(std::max<uint64_t>(1,e.size)));if(!data)fail("Out of memory reading package entry");
        try{extract(s,e,data,static_cast<size_t>(e.size));}catch(...){std::free(data);throw;}
        *output=data;*size=static_cast<size_t>(e.size);return 1;
    }catch(const std::exception& e){last_error=e.what();return 0;}
}
extern "C" uint64_t asset_pak_romfs_size(void){try{return required().romfs_size;}catch(const std::exception& e){last_error=e.what();return 0;}}
extern "C" int asset_pak_read_romfs(void* output,uint64_t offset,size_t count){try{
    auto& s=required();if(offset>s.romfs_size||count>s.romfs_size-offset||(!output&&count))fail("ROMFS read outside package image");
    std::lock_guard guard(s.lock);size_t done=0;
    while(done<count){uint64_t position=offset+done,index=position/block_size;CachedBlock* cached=nullptr;
        for(auto& candidate:s.cache)if(candidate.index==index){cached=&candidate;break;}
        if(!cached){cached=&*std::min_element(s.cache.begin(),s.cache.end(),[](const auto& a,const auto& b){return a.last<b.last;});
            cached->index=UINT64_MAX;cached->last=0;char name[64];std::snprintf(name,sizeof name,"romfs/blocks/%06llu.bin",static_cast<unsigned long long>(index));
            const Entry& e=find(s,name);cached->bytes.resize(static_cast<size_t>(e.size));extract(s,e,cached->bytes.data(),cached->bytes.size());cached->index=index;
        }
        cached->last=++s.tick;size_t within=size_t(position%block_size),take=std::min(count-done,cached->bytes.size()-within);
        std::memcpy(static_cast<unsigned char*>(output)+done,cached->bytes.data()+within,take);done+=take;
    }
    return 1;
}catch(const std::exception& e){last_error=e.what();return 0;}}
