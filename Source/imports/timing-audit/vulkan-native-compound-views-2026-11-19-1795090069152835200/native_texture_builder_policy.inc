extern "C" int catherine_native_texture_builder_scalar(CPU*,unsigned,uint64_t,uint64_t,int,uint64_t*);
namespace {
int native_builder_mode(){static const int m=[] {const char* p=std::getenv("CATHERINE_VULKAN_NATIVE_TEXTURE_BUILDER");return p?std::atoi(p):1;}();return m;}
uint64_t builder_getter(CPU& cpu,unsigned kind,uint64_t builder){
    static const char* names[]={"nvnTextureBuilderGetFlags","nvnTextureBuilderGetTarget","nvnTextureBuilderGetWidth","nvnTextureBuilderGetHeight","nvnTextureBuilderGetDepth","nvnTextureBuilderGetLevels","nvnTextureBuilderGetFormat","nvnTextureBuilderGetSamples","nvnTextureBuilderGetDepthStencilMode","nvnTextureBuilderGetStride","nvnTextureBuilderGetMemoryPool","nvnTextureBuilderGetMemoryOffset","nvnMemoryPoolBuilderGetMemory","nvnMemoryPoolBuilderGetSize","nvnMemoryPoolBuilderGetFlags","nvnBufferBuilderGetMemoryPool","nvnBufferBuilderGetMemoryOffset","nvnBufferBuilderGetSize"};
    const auto address=real_proc(cpu,names[kind]);uint64_t value=0;
    if(native_builder_mode()&&catherine_native_texture_builder_scalar(&cpu,kind,address,builder,native_builder_mode()==2,&value))return value;
    return guest_call(cpu,address,{builder});
}
}

extern "C" int catherine_native_texture_builder_outputs(CPU*,unsigned,uint64_t,uint64_t,uint64_t,int,uint64_t*);
namespace {
uint64_t builder_outputs(CPU& cpu,unsigned kind,uint64_t builder,uint64_t output){
 const char* name=kind?"nvnTextureBuilderGetPackagedTextureLayout":"nvnTextureBuilderGetSwizzle";
 const auto address=real_proc(cpu,name);uint64_t value=0;
 if(native_builder_mode()&&catherine_native_texture_builder_outputs(&cpu,kind,address,builder,output,native_builder_mode()==2,&value))return value;
 return kind?guest_call(cpu,address,{builder}):guest_call(cpu,address,{builder,output,output+4,output+8,output+12});
}
}
