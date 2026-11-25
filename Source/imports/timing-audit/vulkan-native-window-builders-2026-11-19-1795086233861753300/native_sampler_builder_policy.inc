extern "C" int catherine_native_sampler_builder(CPU*,unsigned,uint64_t,uint64_t,uint64_t,int,uint64_t*);
namespace {
int native_sampler_mode(){static const int m=[] {const char* p=std::getenv("CATHERINE_VULKAN_NATIVE_SAMPLER_BUILDER");if(!p)p=std::getenv("CATHERINE_VULKAN_NATIVE_TEXTURE_BUILDER");return p?std::atoi(p):1;}();return m;}
uint64_t native_sampler_get(CPU& cpu,unsigned kind,uint64_t b,uint64_t tmp){
 static const char* names[]={"nvnSamplerBuilderGetMinMagFilter","nvnSamplerBuilderGetWrapMode","nvnSamplerBuilderGetLodClamp","nvnSamplerBuilderGetLodBias","nvnSamplerBuilderGetCompare","nvnSamplerBuilderGetBorderColor","nvnSamplerBuilderGetMaxAnisotropy","nvnSamplerBuilderGetReductionFilter"};const auto address=real_proc(cpu,names[kind]);uint64_t value=0;
 if(native_sampler_mode()&&catherine_native_sampler_builder(&cpu,kind,address,b,tmp,native_sampler_mode()==2,&value))return value;
 if(kind==1)return guest_call(cpu,address,{b,tmp,tmp+4,tmp+8});
 if(kind==0||kind==2||kind==4)return guest_call(cpu,address,{b,tmp,tmp+4});
 if(kind==5)return guest_call(cpu,address,{b,tmp});return guest_call(cpu,address,{b});
}
float native_sampler_float(CPU& cpu,unsigned kind,uint64_t b){
 static const char* names[]={"nvnSamplerBuilderGetLodBias","nvnSamplerBuilderGetMaxAnisotropy"};const auto address=real_proc(cpu,names[kind==6]);uint64_t ignored=0;
 if(native_sampler_mode()&&catherine_native_sampler_builder(&cpu,kind,address,b,0,native_sampler_mode()==2,&ignored)){float value;std::memcpy(&value,&cpu.v[0],4);return value;}
 return guest_call_f(cpu,address,{b});
}
}
