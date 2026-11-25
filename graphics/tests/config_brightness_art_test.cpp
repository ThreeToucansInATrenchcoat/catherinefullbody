#include "nvn/nvn_controller_art.h"
#include "nvn/controller_art_raster.h"
#include "platform.h"
#include "catherine_keyboard.h"
#include <fstream>
#include <cstdio>
#include <cassert>
#include <cstdarg>
#include <numeric>
#include <windows.h>
#include <bcrypt.h>
#include <string>
using namespace nx::nvn;
static bool ps=false; static unsigned mapping[16];
namespace nx { void log(const char*,...){} }
static unsigned copies=0;
static void VKAPI_PTR copy_stub(VkCommandBuffer,VkBuffer,VkImage,VkImageLayout,uint32_t,const VkBufferImageCopy*){++copies;}
namespace nx::vk { PFN_vkCmdCopyBufferToImage p_vkCmdCopyBufferToImage=copy_stub; }
namespace nx::nvn {
const VkPhysicalDeviceFeatures& vk_features(){static VkPhysicalDeviceFeatures f{};return f;}
Upload upload(const void*,uint64_t,uint64_t){return {};}
void transition(VkCommandBuffer,GpuImage& i,VkImageLayout layout){i.layout=layout;}
std::string sha1_hex(const void* bytes,size_t size){BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;unsigned char digest[20];assert(!BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA1_ALGORITHM,nullptr,0));assert(!BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0));assert(!BCryptHashData(hash,(PUCHAR)bytes,(ULONG)size,0));assert(!BCryptFinishHash(hash,digest,20,0));BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);std::string out;for(auto v:digest){char t[3];sprintf_s(t,"%02x",v);out+=t;}return out;}
}
static nx::plat::InputLayout device=nx::plat::InputLayout::KeyboardMouse;
static auto keys=catherine::kDefaultKeyboard;static uint64_t keyversion=1;
namespace nx::plat { bool dualsense_connected() noexcept{return device==InputLayout::PlayStation;} InputLayout active_input_layout() noexcept{return device;} unsigned keyboard_binding(unsigned i) noexcept{return keys[i];}uint64_t keyboard_binding_version() noexcept{return keyversion;}uint64_t controller_binding_map(){uint64_t v=0;for(unsigned i=0;i<16;++i)v|=uint64_t(mapping[i])<<(4*i);return v;} }

static std::vector<uint8_t> read(const char* p){std::ifstream s(p,std::ios::binary);return {std::istreambuf_iterator<char>(s),{}};}
static void write(const char* p,const std::vector<uint8_t>& b){std::ofstream f(p,std::ios::binary);f.write((const char*)b.data(),b.size());}
int main(int argc,char**argv){assert(argc==3);auto file=read(argv[1]);assert(file.size()==128+1024*1024);std::vector<uint8_t> original(file.begin()+128,file.end()), reference;
for(unsigned family=0;family<3;++family)for(unsigned remap=0;remap<2;++remap){device=nx::plat::InputLayout(family);for(unsigned i=0;i<16;++i)mapping[i]=(i+remap*3)%16;keys[0]=remap?'F':32;++keyversion;auto data=original;GpuImage image;image.width=image.height=1024;image.format=VK_FORMAT_BC3_UNORM_BLOCK;controller_art_identify(image,data.data(),data.size());assert(image.controller_art_id&&data!=original);
for(unsigned y=0;y<1024;y+=4)for(unsigned x=0;x<1024;x+=4){size_t at=(size_t(y/4)*256+x/4)*16;bool changed=memcmp(data.data()+at,original.data()+at,16);bool allowed=(x>=40&&x<192&&((y>=180&&y<212)||(y>=224&&y<256)))||(x>=316&&x<364&&y>=632&&y<720);assert(!changed||allowed);if(x>=316&&x<364&&y>=632&&y<720){if(reference.empty()){}else assert(!memcmp(data.data()+at,reference.data()+at,16));}}
if(reference.empty())reference=data;if(!remap){std::copy(data.begin(),data.end(),file.begin()+128);write((std::string(argv[2])+"-"+std::to_string(family)+".dds").c_str(),file);}
}
puts("PASS native Config triangle slots unchanged across keyboard/Xbox/PlayStation and remaps; edited BC3 blocks bounded to existing face slots and brightness hints.");}
