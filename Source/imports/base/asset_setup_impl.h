// Embedded setup and runtime shader translator. Contains tools, never game shader bytes.
#include "asset_setup.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mutex>
namespace {
std::mutex setup_mutex;
std::filesystem::path setup_tool;
struct ShaderProcessLock {
 HANDLE handle=nullptr; bool acquired=false;
 ShaderProcessLock(){handle=CreateMutexW(nullptr,FALSE,L"Local\\CatherineAssetShaderTranslation");if(handle){DWORD result=WaitForSingleObject(handle,120000);acquired=result==WAIT_OBJECT_0||result==WAIT_ABANDONED;}}
 ~ShaderProcessLock(){if(acquired)ReleaseMutex(handle);if(handle)CloseHandle(handle);}
};
void setup_error(const char* text){FILE* f=nullptr;fopen_s(&f,"assets-errors.log","a");if(f){fprintf(f,"Asset setup: %s (Windows error %lu)\n",text,GetLastError());fclose(f);}}
std::wstring setup_quote(const std::wstring& value){std::wstring out=L"\"";size_t slashes=0;for(wchar_t c:value){if(c==L'\\'){slashes++;continue;}if(c==L'\"')out.append(slashes*2+1,L'\\');else out.append(slashes,L'\\');slashes=0;out+=c;}out.append(slashes*2,L'\\');out+=L'\"';return out;}
bool setup_extract(){
 if(!setup_tool.empty())return true;HMODULE module=GetModuleHandleW(nullptr);HRSRC resource=FindResourceW(module,MAKEINTRESOURCEW(102),MAKEINTRESOURCEW(10));if(!resource){setup_error("Missing embedded installer");return false;}DWORD size=SizeofResource(module,resource);const char* bytes=static_cast<const char*>(LockResource(LoadResource(module,resource)));if(!bytes||!size)return false;
 uint64_t key=1469598103934665603ULL;for(DWORD i=0;i<size;i++)key=(key^static_cast<unsigned char>(bytes[i]))*1099511628211ULL;
 wchar_t local[32768];DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);if(!n||n>=32768)return false;std::wostringstream id;id<<std::hex<<key;
 std::error_code ec;auto folder=std::filesystem::path(local)/L"Catherine"/L"AssetTools"/id.str();std::filesystem::create_directories(folder,ec);if(ec)return false;auto path=folder/L"CatherineAssetSetup.exe";
 bool equal=false;{std::ifstream in(path,std::ios::binary|std::ios::ate);if(in&&in.tellg()==size){in.seekg(0);std::vector<char> old(size);in.read(old.data(),size);equal=in&&memcmp(old.data(),bytes,size)==0;}}
 if(!equal){auto temporary=folder/(L"setup-"+std::to_wstring(GetCurrentProcessId())+L".tmp");{std::ofstream out(temporary,std::ios::binary|std::ios::trunc);out.write(bytes,size);if(!out){setup_error("Could not extract installer");return false;}}if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){std::filesystem::remove(temporary,ec);return false;}}
 setup_tool=path;return true;
}
int setup_run(const std::wstring& args){
 // WinExe has no console; allow its explicit setup form to show. --headless and
 // --translate create no forms. SW_HIDE here suppresses the progress form itself.
 std::wstring command=setup_quote(setup_tool.wstring())+L" "+args;STARTUPINFOW startup={sizeof(startup)};startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_SHOWNORMAL;PROCESS_INFORMATION process={};if(!CreateProcessW(setup_tool.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){setup_error("Could not start embedded installer");return 1;}CloseHandle(process.hThread);WaitForSingleObject(process.hProcess,INFINITE);DWORD result=1;GetExitCodeProcess(process.hProcess,&result);CloseHandle(process.hProcess);return static_cast<int>(result);
}
bool setup_read(const std::filesystem::path& path,void** data,size_t* size){std::ifstream in(path,std::ios::binary|std::ios::ate);if(!in)return false;auto n=in.tellg();if(n<0||n>64*1024*1024)return false;void* p=malloc(static_cast<size_t>(n)+1);if(!p)return false;in.seekg(0);in.read(static_cast<char*>(p),n);if(!in){free(p);return false;}static_cast<char*>(p)[static_cast<size_t>(n)]=0;*data=p;*size=static_cast<size_t>(n);return true;}
}
extern "C" int catherine_asset_setup(const wchar_t* pack_path){
 std::lock_guard<std::mutex> lock(setup_mutex);if(!setup_extract())return 1;wchar_t root[32768];if(!GetCurrentDirectoryW(32768,root))return 1;auto pack=pack_path&&*pack_path?std::filesystem::absolute(pack_path):std::filesystem::path(root)/L"assets.pak";std::wstring args=L"--root "+setup_quote(root)+L" --pack "+setup_quote(pack.wstring());if(const char* hidden=getenv("NX_HEADLESS");hidden&&atoi(hidden))args+=L" --headless";return setup_run(args);
}
extern "C" int catherine_translate_shader(const char* hash,const uint8_t* code,size_t size,const uint8_t* control,void** binary,size_t* length,char** resources){
 if(strlen(hash)!=16||strspn(hash,"0123456789abcdef")!=16||size>16*1024*1024)return 0;std::lock_guard<std::mutex> lock(setup_mutex);if(!setup_extract())return 0;
 ShaderProcessLock process_lock;if(!process_lock.acquired)return 0;
 auto dir=setup_tool.parent_path()/L"Shaders";std::error_code ec;std::filesystem::create_directories(dir,ec);if(ec)return 0;auto base=dir/hash;
 auto read=[&](){size_t n=0;void* text=nullptr;if(!setup_read(base.wstring()+L".spv",binary,length))return false;if(!setup_read(base.wstring()+L".resources",&text,&n)){free(*binary);*binary=nullptr;return false;}*resources=static_cast<char*>(text);return true;};
 if(read())return 1;
 auto codepath=base.wstring()+L".code",ctrlpath=base.wstring()+L".control";{std::ofstream out(codepath,std::ios::binary);out.write(reinterpret_cast<const char*>(code),size);if(!out)return 0;}{std::ofstream out(ctrlpath,std::ios::binary);out.write(reinterpret_cast<const char*>(control),0x800);if(!out)return 0;}
 int result=setup_run(L"--translate "+setup_quote(codepath)+L" "+setup_quote(ctrlpath)+L" "+setup_quote(base.wstring()));std::filesystem::remove(codepath,ec);std::filesystem::remove(ctrlpath,ec);if(result||!read()){setup_error("Runtime shader translation failed");return 0;}return 1;
}
