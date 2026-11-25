#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/sha.h>
#endif
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#define MINIZ_NO_ZLIB_APIS
#define MINIZ_NO_DEFLATE_APIS
#define MINIZ_NO_STDIO
#include "third_party/miniz/miniz.h"
#include "dev_checkpoint_pack.h"
namespace {
#ifdef _WIN32
std::wstring temporary;
#else
std::string temporary;
#endif
std::string failure;
bool fail(const char*s){failure=s;return false;}
bool sha(const std::vector<unsigned char>&b,const char *expected){
 unsigned char digest[32]{};
#ifdef _WIN32
 BCRYPT_ALG_HANDLE alg{};BCRYPT_HASH_HANDLE hash{};DWORD len=0,got=0;
 bool ok=BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
 if(ok)ok=BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&len,sizeof(len),&got,0)>=0;
 std::vector<unsigned char> object(len);
 if(ok)ok=BCryptCreateHash(alg,&hash,object.data(),len,nullptr,0,0)>=0;
 if(ok)ok=BCryptHashData(hash,(PUCHAR)b.data(),(ULONG)b.size(),0)>=0&&BCryptFinishHash(hash,digest,32,0)>=0;
 if(hash)BCryptDestroyHash(hash);if(alg)BCryptCloseAlgorithmProvider(alg,0);
#else
 const bool ok=SHA256(b.data(),b.size(),digest)!=nullptr;
#endif
 char hex[65]{};for(unsigned i=0;i<32;++i)std::snprintf(hex+i*2,3,"%02X",digest[i]);
 return ok&&expected&&std::strlen(expected)==64&&!std::strcmp(hex,expected);
}
}
extern "C" const char*catherine_dev_checkpoint_error(void){return failure.c_str();}
extern "C" void catherine_dev_checkpoint_release(void){if(!temporary.empty()){
#ifdef _WIN32
 DeleteFileW(temporary.c_str());
#else
 unlink(temporary.c_str());
#endif
 temporary.clear();}}
extern "C" int catherine_dev_checkpoint_materialize(const char*entry,const char*expected,const char*fallback,char*out,uint32_t cap){
 (void)fallback;catherine_dev_checkpoint_release();failure.clear();
 if(!entry||!expected||!out||cap<2)return fail("Invalid checkpoint package request.");
#ifdef _WIN32
 wchar_t exe[32768];DWORD n=GetModuleFileNameW(nullptr,exe,32768);if(!n||n>=32768)return fail("Cannot locate the executable.");
 std::wstring path(exe,n);auto at=path.find_last_of(L"\\/");if(at==std::wstring::npos)return fail("Cannot locate the executable folder.");path.resize(at+1);path+=L"dev-checkpoints.pak";
 HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(f==INVALID_HANDLE_VALUE)return fail("Place dev-checkpoints.pak beside the executable.");
 BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
 bool valid=GetFileInformationByHandle(f,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&GetFileSizeEx(f,&size)&&size.QuadPart>0&&size.QuadPart<=16*1024*1024;
 std::vector<unsigned char> bytes(valid?(size_t)size.QuadPart:0);DWORD got=0;
 if(valid)valid=ReadFile(f,bytes.data(),(DWORD)bytes.size(),&got,nullptr)&&got==bytes.size();CloseHandle(f);
#else
 int fd=open("dev-checkpoints.pak",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
 if(fd<0)return fail("Place dev-checkpoints.pak beside the executable.");
 struct stat info{};
 bool valid=fstat(fd,&info)==0 && S_ISREG(info.st_mode) && info.st_size>0 && info.st_size<=16*1024*1024;
 std::vector<unsigned char> bytes(valid?size_t(info.st_size):0);
 size_t offset=0;
 while(valid && offset<bytes.size()){
  ssize_t count=read(fd,bytes.data()+offset,bytes.size()-offset);
  if(count<0 && errno==EINTR)continue;
  if(count<=0)valid=false;
  else offset+=count;
 }
 close(fd);
#endif
 if(!valid)return fail("Invalid or oversized checkpoint package.");
 mz_zip_archive zip{};if(!mz_zip_reader_init_mem(&zip,bytes.data(),bytes.size(),0))return fail("Cannot read checkpoint ZIP package.");
 int index=mz_zip_reader_locate_file(&zip,entry,nullptr,0);mz_zip_archive_file_stat stat{};
 valid=mz_zip_reader_get_num_files(&zip)<=64&&index>=0&&mz_zip_reader_file_stat(&zip,index,&stat)&&stat.m_method==8&&stat.m_uncomp_size==0xd2000&&!stat.m_is_directory&&!stat.m_is_encrypted;
 std::vector<unsigned char> save(valid?0xd2000:0);
 if(valid)valid=mz_zip_reader_extract_to_mem(&zip,index,save.data(),save.size(),0)!=0;mz_zip_reader_end(&zip);
 if(!valid||!sha(save,expected))return fail("Checkpoint ZIP entry, CRC, length or SHA256 validation failed.");
#ifdef _WIN32
 wchar_t tempdir[32768],name[32768];if(!GetTempPathW(32768,tempdir)||!GetTempFileNameW(tempdir,L"CDV",0,name))return fail("Cannot create temporary checkpoint.");
 temporary=name;f=CreateFileW(name,GENERIC_WRITE,0,nullptr,TRUNCATE_EXISTING,FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 got=0;valid=f!=INVALID_HANDLE_VALUE&&WriteFile(f,save.data(),(DWORD)save.size(),&got,nullptr)&&got==save.size();if(f!=INVALID_HANDLE_VALUE)CloseHandle(f);
 if(!valid){catherine_dev_checkpoint_release();return fail("Cannot extract temporary checkpoint.");}
 int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,name,-1,out,(int)cap,nullptr,nullptr);
 if(!count){catherine_dev_checkpoint_release();return fail("Temporary checkpoint path is too long.");}return 1;
#else
 char name[]="checkpoint-XXXXXX";
 if(sizeof(name)>cap)return fail("Temporary checkpoint path is too long.");
 fd=mkostemp(name,O_CLOEXEC);
 if(fd<0)return fail("Cannot create temporary checkpoint.");
 temporary=name;
 offset=0;
 while(offset<save.size()){
  ssize_t count=write(fd,save.data()+offset,save.size()-offset);
  if(count<0 && errno==EINTR)continue;
  if(count<=0)break;
  offset+=count;
 }
 if(close(fd)!=0 || offset!=save.size()){
  catherine_dev_checkpoint_release();
  return fail("Cannot extract temporary checkpoint.");
 }
 std::memcpy(out,name,sizeof(name));
 return 1;
#endif
}
