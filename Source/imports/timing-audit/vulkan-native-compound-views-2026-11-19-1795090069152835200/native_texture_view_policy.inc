extern "C" int catherine_native_view_prefix_step(CPU*,unsigned,uint64_t,uint64_t,uint64_t,int,uint64_t*);
extern "C" int catherine_native_texture_view_getter(CPU*,uint32_t,uint64_t,uint64_t,uint64_t,int,uint64_t*);
namespace {
int native_view_mode(){static const int mode=[] {const char* value=std::getenv("CATHERINE_VULKAN_NATIVE_TEXTURE_VIEWS");return value?std::atoi(value):1;}();return mode;}
int compound_view_mode(){static const int mode=[] {const char* e=std::getenv("CATHERINE_VULKAN_COMPOUND_VIEWS");return e?std::atoi(e):1;}();return mode;}
uint64_t view_getter(CPU& c,uint32_t kind,uint64_t view,uint64_t tmp){
    static const char* names[]={"nvnTextureViewGetLevels","nvnTextureViewGetLayers","nvnTextureViewGetSwizzle","nvnTextureViewGetFormat"};
    const auto address=real_proc(c,names[kind]);uint64_t value=0;
    if(native_view_mode()&&compound_view_mode()&&kind<4&&catherine_native_view_prefix_step(&c,kind,address,view,tmp,compound_view_mode()==2||native_view_mode()==2,&value))return value;
    if(native_view_mode()&&catherine_native_texture_view_getter(&c,kind,address,view,tmp,native_view_mode()==2,&value))return value;
    if(kind==0||kind==1)return guest_call(c,address,{view,tmp,tmp+4});
    if(kind==2)return guest_call(c,address,{view,tmp,tmp+4,tmp+8,tmp+12});
    return guest_call(c,address,{view,tmp});
}
}
