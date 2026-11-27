#include <vulkan/vulkan.h>
#include "native_skin_pipeline.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <vector>
#include <immintrin.h>

void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS){std::fprintf(stderr,"%s failed: %d\n",operation,int(result));std::exit(2);}
}
float f(uint32_t word){return std::bit_cast<float>(word);}
uint32_t bits(float value){return std::bit_cast<uint32_t>(value);}
std::array<float,3> transform(const uint32_t* palette,uint32_t joint,const uint32_t* v,bool position) {
    std::array<float,3> out;
    for(unsigned row=0;row<3;++row){const auto* m=palette+joint*16+row*4;
        float n=f(v[0])*f(m[0]);n=std::fma(f(v[1]),f(m[1]),n);n=std::fma(f(v[2]),f(m[2]),n);
        if(position)n=std::fma(f(v[3]),f(m[3]),n);out[row]=n;
    }
    return out;
}
void reference(const uint32_t* source,const uint32_t* palette,uint32_t* output,unsigned count,bool tangent) {
    for(unsigned i=0;i<count;++i){const auto* src=source+i*(tangent?20:16);const auto* w=src+(tangent?12:8);const auto* j=w+4;auto* dst=output+i*(tangent?12:8);
        for(unsigned part=0;part<(tangent?3u:2u);++part){
            const auto a=transform(palette,j[1],src+part*4,part==0),b=transform(palette,j[0],src+part*4,part==0),c=transform(palette,j[2],src+part*4,part==0),d=transform(palette,j[3],src+part*4,part==0);
            for(unsigned row=0;row<3;++row){float v=a[row]*f(w[1]);v=std::fma(b[row],f(w[0]),v);v=std::fma(c[row],f(w[2]),v);v=std::fma(d[row],f(w[3]),v);dst[part*4+row]=bits(v);}
        }
        if((tangent && src[8]==0xffc00000u&&src[9]==0xffc00000u&&src[10]==0xffc00000u)||(!tangent&&src[0]==0xffc00000u&&src[1]==0xffc00000u&&src[2]==0xffc00000u&&src[3]==0x3f800000u))
            for(unsigned k=0;k<3;++k)dst[(tangent?8:0)+k]=0x7fffffffu;
        dst[3]=bits(1.f);
    }
}
int main(){
    _mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_NEAREST|_MM_MASK_MASK|_MM_FLUSH_ZERO_ON|_MM_DENORMALS_ZERO_ON);
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Native skin kernel validation";app.apiVersion=VK_API_VERSION_1_2;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ii.pApplicationInfo=&app;VkInstance instance{};check(vkCreateInstance(&ii,nullptr,&instance),"create instance");
    uint32_t count=0;check(vkEnumeratePhysicalDevices(instance,&count,nullptr),"enumerate GPUs");std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"enumerate GPUs");
    VkPhysicalDevice physical=VK_NULL_HANDLE;VkPhysicalDeviceProperties props{};
    for(auto d:devices){VkPhysicalDeviceProperties p{};vkGetPhysicalDeviceProperties(d,&p);if(p.deviceType==VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU){physical=d;props=p;break;}}
    if(!physical)return 3;if(!experimental_skin::supported(physical)){std::fprintf(stderr,"Required FP32 float controls unavailable\n");return 3;}std::printf("Validating on %s\n",props.deviceName);
    uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,nullptr);std::vector<VkQueueFamilyProperties> queues(families);vkGetPhysicalDeviceQueueFamilyProperties(physical,&families,queues.data());
    uint32_t family=0;while(family<families&&!(queues[family].queueFlags&VK_QUEUE_COMPUTE_BIT))++family;if(family==families)return 3;
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkPhysicalDeviceBufferDeviceAddressFeatures bda{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES};bda.bufferDeviceAddress=VK_TRUE;
    VkPhysicalDeviceFeatures features{};features.shaderInt64=VK_TRUE;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.pNext=&bda;di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;di.pEnabledFeatures=&features;
    VkDevice device{};check(vkCreateDevice(physical,&di,nullptr,&device),"create device");VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
    constexpr size_t bytes=4u<<20;VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=bytes;bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;VkBuffer buffer{};check(vkCreateBuffer(device,&bi,nullptr,&buffer),"create buffer");
    VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,buffer,&req);VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    uint32_t type=0;while(type<memory.memoryTypeCount&&(!(req.memoryTypeBits&(1u<<type))||(memory.memoryTypes[type].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))!=(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)))++type;if(type==memory.memoryTypeCount)return 3;
    VkMemoryAllocateFlagsInfo flags{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.pNext=&flags;ai.allocationSize=req.size;ai.memoryTypeIndex=type;VkDeviceMemory allocation{};check(vkAllocateMemory(device,&ai,nullptr,&allocation),"allocate memory");check(vkBindBufferMemory(device,buffer,allocation,0),"bind memory");void* mapped{};check(vkMapMemory(device,allocation,0,bytes,0,&mapped),"map memory");
    VkBufferDeviceAddressInfo address_info{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};address_info.buffer=buffer;VkDeviceAddress address=vkGetBufferDeviceAddress(device,&address_info);if(!address)return 3;
    auto* source=static_cast<uint32_t*>(mapped);auto* palette=source+(1u<<18);auto* destination=source+(1u<<19);auto* preserved=source+(3u<<18);
    experimental_skin::Pipeline pipeline;if(!pipeline.create(device))return 4;
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandPool pool{};check(vkCreateCommandPool(device,&pi,nullptr,&pool),"create pool");
    VkCommandBufferAllocateInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ci.commandPool=pool;ci.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ci.commandBufferCount=1;VkCommandBuffer cb{};check(vkAllocateCommandBuffers(device,&ci,&cb),"allocate CB");
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VkFence fence{};check(vkCreateFence(device,&fi,nullptr,&fence),"create fence");
    std::mt19937 rng(0x6A7E2026);auto random_float=[&](float scale){return bits((float(int(rng()%20001)-10000)/10000.f)*scale);};
    unsigned cases=0;uint64_t vertices=0,words=0;
    for(unsigned iteration=0;iteration<16;++iteration)for(bool tangent:{false,true})for(unsigned n:{1u,63u,64u,65u,513u,4096u}){
        constexpr unsigned matrices=97;for(unsigned k=0;k<matrices*16;++k)palette[k]=random_float(k%4==3?1000.f:2.f);
        unsigned input_words=tangent?20:16,output_words=tangent?12:8;
        for(unsigned k=0;k<n;++k){auto* v=source+k*input_words;for(unsigned j=0;j<input_words;++j)v[j]=random_float(2.f);v[0]=random_float(10000.f);v[1]=random_float(10000.f);v[2]=random_float(10000.f);v[3]=bits((k&1)?1.f:0.f);unsigned at=tangent?12:8;for(unsigned j=0;j<4;++j)v[at+4+j]=rng()%matrices;}
        if(iteration>=6) {
            // Include subnormal operands, normal operands whose product underflows,
            // cancellation, both zero signs, and boundary exponents.
            constexpr uint32_t edge[]={0u,0x80000000u,1u,0x80000001u,0x007fffffu,0x807fffffu,0x00800000u,0x80800000u,0x01000000u,0x3f000000u,0xbf000000u,0x3f800000u};
            for(unsigned k=0;k<matrices*16;++k)palette[k]=edge[(k+iteration)%12];
            for(unsigned k=0;k<n;++k) {
                auto* v=source+k*input_words;unsigned at=tangent?12:8;
                for(unsigned j=0;j<at+4;++j)v[j]=edge[(k+j*5+iteration)%12];
                v[3]=bits((k&1)?1.f:0.f);
            }
        }
        if(iteration>=12)for(unsigned k=0;k<n;++k)if(k%3==0) {
            auto* v=source+k*input_words;unsigned at=tangent?8:0;
            v[at]=v[at+1]=v[at+2]=0xffc00000u;if(!tangent)v[3]=0x3f800000u;
        }
        for(unsigned k=0;k<n*output_words+64;++k)destination[k]=rng();for(unsigned k=0;k<n;++k){preserved[k*(tangent?2:1)]=destination[k*output_words+7];if(tangent)preserved[k*2+1]=destination[k*output_words+11];}
        std::vector<uint32_t> expected(destination,destination+n*output_words+64);reference(source,palette,expected.data(),n,tangent);
        check(vkResetCommandBuffer(cb,0),"reset CB");VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;check(vkBeginCommandBuffer(cb,&begin),"begin CB");
        experimental_skin::Parameters parameters{address,address+(1u<<20),address+(2u<<20),address+(3u<<20),n,matrices};pipeline.record(cb,parameters,tangent);
        VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);check(vkEndCommandBuffer(cb),"end CB");
        check(vkResetFences(device,1,&fence),"reset fence");VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cb;check(vkQueueSubmit(queue,1,&submit,fence),"submit");check(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull),"wait fence");
        for(unsigned k=0;k<expected.size();++k)if(destination[k]!=expected[k]){std::fprintf(stderr,"Mismatch case %u tangent %d vertex %u word %u expected %08x actual %08x\n",cases,tangent,k/output_words,k%output_words,expected[k],destination[k]);return 5;}
        ++cases;vertices+=n;words+=expected.size();
    }
    std::printf("PASS %u native GPU cases, %llu vertices, %llu exact output/padding words\n",cases,(unsigned long long)vertices,(unsigned long long)words);
    check(vkDeviceWaitIdle(device),"idle");vkDestroyFence(device,fence,nullptr);vkDestroyCommandPool(device,pool,nullptr);pipeline.destroy(device);vkUnmapMemory(device,allocation);vkDestroyBuffer(device,buffer,nullptr);vkFreeMemory(device,allocation,nullptr);vkDestroyDevice(device,nullptr);vkDestroyInstance(instance,nullptr);
}
