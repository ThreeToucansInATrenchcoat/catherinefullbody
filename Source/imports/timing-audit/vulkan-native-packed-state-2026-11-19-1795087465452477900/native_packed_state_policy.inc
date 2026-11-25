extern "C" int catherine_native_packed_state(CPU*,unsigned,uint64_t,const uint64_t*,unsigned,int,uint64_t*);
namespace {
int native_state_getter_mode(){static const int m=[] {const char* p=std::getenv("CATHERINE_VULKAN_NATIVE_STATE_GETTERS");return p?std::atoi(p):1;}();return m;}
uint64_t native_state_get_at(CPU& cpu,unsigned kind,uint64_t address,std::initializer_list<uint64_t> args){
 uint64_t value=0;
 if(native_state_getter_mode()&&catherine_native_packed_state(&cpu,kind,address,args.begin(),unsigned(args.size()),native_state_getter_mode()==2,&value))return value;
 return guest_call(cpu,address,args);
}
uint64_t native_state_get(CPU& cpu,unsigned kind,std::initializer_list<uint64_t> args){
 static const char* names[]={"nvnVertexAttribStateGetFormat","nvnVertexAttribStateGetStreamIndex","nvnVertexStreamStateGetStride","nvnVertexStreamStateGetDivisor","nvnBlendStateGetBlendTarget","nvnBlendStateGetBlendFunc","nvnBlendStateGetBlendEquation","nvnColorStateGetBlendEnable","nvnColorStateGetLogicOp","nvnColorStateGetAlphaTest","nvnChannelMaskStateGetChannelMask","nvnDepthStencilStateGetDepthTestEnable","nvnDepthStencilStateGetDepthWriteEnable","nvnDepthStencilStateGetDepthFunc","nvnDepthStencilStateGetStencilTestEnable","nvnDepthStencilStateGetStencilFunc","nvnDepthStencilStateGetStencilOp","nvnPolygonStateGetCullFace","nvnPolygonStateGetFrontFace","nvnPolygonStateGetPolygonMode","nvnPolygonStateGetPolygonOffsetEnables","nvnMultisampleStateGetAlphaToCoverageEnable"};return native_state_get_at(cpu,kind,real_proc(cpu,names[kind]),args);
}
}
