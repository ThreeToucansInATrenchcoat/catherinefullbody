extern "C" int catherine_native_window_builder(CPU*,unsigned,uint64_t,uint64_t,uint64_t,int,uint64_t*);
namespace {
int native_window_mode(){static const int m=[] {const char* p=std::getenv("CATHERINE_VULKAN_NATIVE_WINDOW_BUILDER");if(!p)p=std::getenv("CATHERINE_VULKAN_NATIVE_TEXTURE_BUILDER");return p?std::atoi(p):1;}();return m;}
uint64_t window_builder_get(CPU& cpu,unsigned kind,uint64_t builder,uint64_t index=0){
 static const char* names[]={"nvnWindowBuilderGetNumTextures","nvnWindowBuilderGetTexture","nvnWindowBuilderGetPresentInterval","nvnWindowBuilderGetNumActiveTextures"};const auto address=real_proc(cpu,names[kind]);uint64_t value=0;
 if(native_window_mode()&&catherine_native_window_builder(&cpu,kind,address,builder,index,native_window_mode()==2,&value))return value;
 return kind==1?guest_call(cpu,address,{builder,index}):guest_call(cpu,address,{builder});
}
}
