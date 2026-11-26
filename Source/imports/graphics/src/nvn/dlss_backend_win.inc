// NVIDIA Streamline Vulkan proxy owns the instance/device/swapchain dispatch.
// Requested frame generation is kept separate from confirmed SDK activation.
#include "dlss_backend.h"
#include <filesystem>
#include <atomic>
#include <chrono>
#include "nvn_draw_state.h"
#include "dlss_camera.h"
#include "../../../base/guest-timing/include/catherine_settings_win.h"
#ifdef _WIN32
#include <windows.h>
#include "../../../../vendor/streamline/include/sl.h"
#include "../../../../vendor/streamline/include/sl_dlss_g.h"
#include "../../../../vendor/streamline/include/sl_reflex.h"
#include "../../../../vendor/streamline/include/sl_security.h"
namespace nx::nvn {
namespace {
HMODULE streamline_module=nullptr;
PFun_slInit* initialize_streamline=nullptr;
PFun_slGetFeatureFunction* get_feature_function=nullptr;
PFun_slIsFeatureSupported* is_feature_supported=nullptr;
PFun_slDLSSGGetState* get_dlssg_state=nullptr;
PFun_slDLSSGSetOptions* set_dlssg_options=nullptr;
PFun_slSetConstants* set_constants=nullptr;PFun_slSetTagForFrame* set_tags=nullptr;
PFun_slGetNewFrameToken* new_token=nullptr;PFun_slPCLSetMarker* marker=nullptr;
PFun_slReflexSetOptions* set_reflex=nullptr;PFun_slReflexSleep* reflex_sleep=nullptr;
sl::FrameToken* active_token=nullptr;uint32_t frame_index=0;bool sdk_initialized=false,adapter_supported=false,fg_enabled=false;
GpuImage scene_depth_copy,hudless_input;bool hudless_captured=false;GpuImage* candidate_depth=nullptr;temporal::Camera candidate_camera;
temporal::Camera scene_camera,previous_camera;bool fresh_camera=false,had_camera=false;
std::chrono::steady_clock::time_point last_valid_frame{};
uint32_t maximum_generated=1;
std::atomic<uint64_t> total_presents{0};std::atomic<bool> generated_recently{false};
#include "dlss_motion_win.inc"
template<class T> bool feature_function(sl::Feature feature,const char* name,T*& output){void* function=nullptr;if(get_feature_function(feature,name,function)!=sl::Result::eOk)return false;output=reinterpret_cast<T*>(function);return true;}
void api_error(const sl::APIError& error){std::fprintf(stderr,"[DLSS-G] Asynchronous Vulkan error=%d\n",int(error.vkRes));}
void report_result(const char* operation,sl::Result result){if(result!=sl::Result::eOk){static std::atomic<unsigned> errors{0};const auto count=errors.fetch_add(1,std::memory_order_relaxed);if(count<16||count%300==0)std::fprintf(stderr,"[DLSS-G] %s failed=%d\n",operation,int(result));}}
void sdk_log(sl::LogType,const char* message){std::fprintf(stderr,"[Streamline] %s\n",message?message:"");}
}
PFN_vkGetInstanceProcAddr dlss_vulkan_loader(PFN_vkGetInstanceProcAddr native){
    if(catherine::host_settings().frame_generation!=1)return native;
    std::filesystem::path directory;
    if(const char* configured=std::getenv("CATHERINE_STREAMLINE_PATH"))directory=std::filesystem::absolute(configured);
    else {wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);directory=std::filesystem::path(executable).parent_path()/"Streamline";}
    const bool development=std::getenv("CATHERINE_STREAMLINE_DEBUG")!=nullptr;
    if(!development&&!sl::security::verifyEmbeddedSignature((directory/L"sl.interposer.dll").c_str())){std::fprintf(stderr,"[DLSS-G] Runtime signature verification failed; using normal rendering.\n");return native;}
    streamline_module=LoadLibraryExW((directory/L"sl.interposer.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if(!streamline_module){std::fprintf(stderr,"[DLSS-G] Streamline runtime unavailable, frame generation disabled (error %lu).\n",GetLastError());return native;}
    initialize_streamline=reinterpret_cast<PFun_slInit*>(GetProcAddress(streamline_module,"slInit"));
    get_feature_function=reinterpret_cast<PFun_slGetFeatureFunction*>(GetProcAddress(streamline_module,"slGetFeatureFunction"));
    is_feature_supported=reinterpret_cast<PFun_slIsFeatureSupported*>(GetProcAddress(streamline_module,"slIsFeatureSupported"));
    auto loader=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(streamline_module,"vkGetInstanceProcAddr"));
    if(!initialize_streamline||!get_feature_function||!is_feature_supported||!loader){std::fprintf(stderr,"[DLSS-G] Incomplete Streamline exports; disabled.\n");return native;}
    const std::wstring plugins=directory.wstring(),logs=std::filesystem::current_path().wstring();const wchar_t* plugin_paths[]={plugins.c_str()};
    const sl::Feature features[]={sl::kFeatureDLSS_G,sl::kFeatureReflex,sl::kFeaturePCL};
    sl::Preferences preferences{};preferences.renderAPI=sl::RenderAPI::eVulkan;
    preferences.showConsole=false;preferences.logLevel=development?sl::LogLevel::eVerbose:sl::LogLevel::eDefault;
    preferences.logMessageCallback=sdk_log;preferences.pathsToPlugins=plugin_paths;preferences.numPathsToPlugins=1;
    preferences.pathToLogsAndData=logs.c_str();preferences.featuresToLoad=features;preferences.numFeaturesToLoad=3;
    preferences.engine=sl::EngineType::eCustom;preferences.engineVersion="1.0";
    // Unique custom-engine project identity (NGX ProjectID initialization path).
    preferences.projectId="3b76a2ff-7158-405d-be29-e264e920418a";
    preferences.flags=sl::PreferenceFlags::eDisableCLStateTracking|sl::PreferenceFlags::eDisableDebugText|sl::PreferenceFlags::eUseFrameBasedResourceTagging;
    const auto result=initialize_streamline(preferences,sl::kSDKVersion);
    std::fprintf(stderr,"[DLSS-G] Streamline init=%d, Vulkan proxy dispatch, runtime=%ls\n",int(result),plugins.c_str());
    if(result!=sl::Result::eOk)return native;
    sdk_initialized=true;
    set_constants=reinterpret_cast<PFun_slSetConstants*>(GetProcAddress(streamline_module,"slSetConstants"));
    set_tags=reinterpret_cast<PFun_slSetTagForFrame*>(GetProcAddress(streamline_module,"slSetTagForFrame"));
    new_token=reinterpret_cast<PFun_slGetNewFrameToken*>(GetProcAddress(streamline_module,"slGetNewFrameToken"));
    return loader;
}
bool dlss_proxy_active(){return sdk_initialized;}
VkImageLayout dlss_backbuffer_layout(){return get_dlssg_state?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;}
void dlss_adapter_selected(VkPhysicalDevice physical){
    if(!sdk_initialized)return;
    sl::AdapterInfo adapter{};adapter.vkPhysicalDevice=physical;
    const auto support=is_feature_supported(sl::kFeatureDLSS_G,adapter);
    adapter_supported=support==sl::Result::eOk&&!std::getenv("CATHERINE_DLSS_TEST_UNSUPPORTED");
    if(adapter_supported)return;
    // Remove NVIDIA device-extension requirements before Vulkan device creation.
    // The common proxy remains valid and forwards ordinary Vulkan presentation.
    auto unload=reinterpret_cast<PFun_slSetFeatureLoaded*>(GetProcAddress(streamline_module,"slSetFeatureLoaded"));
    if(unload)for(auto feature:{sl::kFeatureDLSS_G,sl::kFeatureReflex,sl::kFeaturePCL})unload(feature,false);
    std::fprintf(stderr,"[DLSS-G] Unavailable on the selected adapter (support=%d); using normal rendering.\n",int(support));
}
void dlss_device_ready(VkPhysicalDevice physical){
    if(!sdk_initialized||!adapter_supported||!get_feature_function)return;
    sl::AdapterInfo adapter{};adapter.vkPhysicalDevice=physical;
    const auto support=is_feature_supported(sl::kFeatureDLSS_G,adapter);
    std::fprintf(stderr,"[DLSS-G] Feature support=%d\n",int(support));
    if(support!=sl::Result::eOk)return;
    void* function=nullptr;
    if(get_feature_function(sl::kFeatureDLSS_G,"slDLSSGGetState",function)!=sl::Result::eOk)return;
    get_dlssg_state=reinterpret_cast<PFun_slDLSSGGetState*>(function);
    function=nullptr;if(get_feature_function(sl::kFeatureDLSS_G,"slDLSSGSetOptions",function)!=sl::Result::eOk)return;
    set_dlssg_options=reinterpret_cast<PFun_slDLSSGSetOptions*>(function);
    feature_function(sl::kFeaturePCL,"slPCLSetMarker",marker);
    feature_function(sl::kFeatureReflex,"slReflexSetOptions",set_reflex);
    feature_function(sl::kFeatureReflex,"slReflexSleep",reflex_sleep);
    if(set_reflex){sl::ReflexOptions options{};options.mode=sl::ReflexMode::eLowLatency;report_result("Reflex options",set_reflex(options));}
    sl::DLSSGState state{};const auto result=get_dlssg_state(sl::ViewportHandle(0),state,nullptr);
    std::fprintf(stderr,"[DLSS-G] State query=%d status=%u max_generated_frames=%u actually_presented=%u min_size=%u\n",int(result),unsigned(state.status),state.numFramesToGenerateMax,state.numFramesActuallyPresented,state.minWidthOrHeight);
    maximum_generated=std::max(1u,state.numFramesToGenerateMax);
}
void dlss_scene_targets(GpuImage* const* images,unsigned count){
    candidate_depth=nullptr;
    if(!get_dlssg_state||!count||!images[0]||!images[8])return;
    const auto& depth=*images[8];
    // Full-size HDR geometry passes, excluding square shadow maps and UI passes.
    if(images[0]->format!=VK_FORMAT_R16G16B16A16_SFLOAT||depth.width<320||depth.height<180||std::abs(float(depth.width)/depth.height-16.f/9.f)>.1f)return;
    const auto& range=replay::g_state.uniforms[0][1];
    if(!range.address||range.size<128)return;
    const auto camera=temporal::extract(replay::guest_bytes(range.address),size_t(range.size));
    if(!camera.valid)return;
    candidate_camera=camera;candidate_depth=images[8];
}
bool dlss_ui_needed(const char* vertex_hash){return get_dlssg_state&&!hudless_captured&&(fresh_camera||candidate_depth)&&std::strcmp(vertex_hash,"266d0aa411f67ef5")==0;}
void dlss_capture_hudless(VkCommandBuffer cb,GpuImage* color){
    if(!color||color->format!=VK_FORMAT_R8G8B8A8_UNORM||color->width!=scene_depth_copy.width||color->height!=scene_depth_copy.height)return;
    if(hudless_input.width!=color->width||hudless_input.height!=color->height){p_vkDeviceWaitIdle(vk_device());temporal_destroy_image(hudless_input);if(!temporal_image(hudless_input,color->width,color->height,color->format))return;}
    const auto previous=color->layout;transition(cb,*color,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);transition(cb,hudless_input,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.extent={color->width,color->height,1};
    p_vkCmdCopyImage(cb,color->image,color->layout,hudless_input.image,hudless_input.layout,1,&copy);
    transition(cb,*color,previous);transition(cb,hudless_input,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);hudless_captured=true;
}
GpuImage* dlss_debug_hudless(){return std::getenv("CATHERINE_DLSS_DUMP_HUDLESS")&&hudless_input.image?&hudless_input:nullptr;}
bool dlss_depth_snapshot_pending(){return candidate_depth!=nullptr;}
void dlss_scene_capture(VkCommandBuffer cb){
    if(!candidate_depth)return;
    GpuImage& depth=*candidate_depth;candidate_depth=nullptr;
    if(depth.samples!=VK_SAMPLE_COUNT_1_BIT)return;
    if(scene_depth_copy.width!=depth.width||scene_depth_copy.height!=depth.height||scene_depth_copy.format!=depth.format){
        p_vkDeviceWaitIdle(vk_device());temporal_destroy_image(scene_depth_copy);
        if(!temporal_image(scene_depth_copy,depth.width,depth.height,depth.format,depth.aspect))return;
    }
    const auto original=depth.layout;transition(cb,depth,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    transition(cb,scene_depth_copy,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageCopy copy{};copy.srcSubresource=copy.dstSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,0,0,1};copy.extent={depth.width,depth.height,1};
    p_vkCmdCopyImage(cb,depth.image,depth.layout,scene_depth_copy.image,scene_depth_copy.layout,1,&copy);
    transition(cb,depth,original);transition(cb,scene_depth_copy,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    scene_camera=candidate_camera;fresh_camera=true;
}
void dlss_render_begin(){
    if(!get_dlssg_state||active_token||!new_token)return;
    ++frame_index;report_result("Frame token",new_token(active_token,&frame_index));
    if(!active_token)return;
    if(reflex_sleep)report_result("Reflex sleep",reflex_sleep(*active_token));
    if(marker)report_result("Render begin",marker(sl::PCLMarker::eRenderSubmitStart,*active_token));
}
namespace {
temporal::Matrix zero_one_projection(temporal::Matrix projection){for(unsigned col=0;col<4;++col)projection[8+col]=.5f*(projection[8+col]+projection[12+col]);return projection;}
sl::float4x4 sdk_matrix(const temporal::Matrix& m){sl::float4x4 output;for(unsigned row=0;row<4;++row)output[row]={m[row],m[4+row],m[8+row],m[12+row]};return output;}
}
namespace {
void invalidate_temporal_frame(){
        if(fg_enabled){sl::DLSSGOptions options{};report_result("Disable for non-game frame",set_dlssg_options(sl::ViewportHandle(0),options));fg_enabled=false;}
        had_camera=false;temporal_inputs.history_valid=false;
        const sl::ResourceTag tags[]={{nullptr,sl::kBufferTypeDepth,sl::eValidUntilPresent},{nullptr,sl::kBufferTypeMotionVectors,sl::eValidUntilPresent},{nullptr,sl::kBufferTypeHUDLessColor,sl::eValidUntilPresent}};
        report_result("Clear tags",set_tags(*active_token,sl::ViewportHandle(0),tags,3,nullptr));
}
}
void dlss_prepare_frame(VkCommandBuffer cb,GpuImage* color,unsigned output_width,unsigned output_height,unsigned backbuffers){
    if(!get_dlssg_state||!set_tags||!set_constants)return;
    dlss_render_begin();if(!active_token)return;
    const bool valid=color&&fresh_camera&&scene_depth_copy.image&&scene_camera.valid&&hudless_captured;
    const bool has_hudless=hudless_captured;hudless_captured=false;
    fresh_camera=false;
    if(!valid){invalidate_temporal_frame();return;}
    temporal::Matrix inverse_current{},clip_to_previous{};
    if(!temporal::inverse(scene_camera.view_projection,inverse_current)){invalidate_temporal_frame();return;}
    clip_to_previous=had_camera?temporal::multiply(previous_camera.view_projection,inverse_current):temporal::multiply(scene_camera.view_projection,inverse_current);
    float camera_distance=0;for(unsigned i:{3u,7u,11u})camera_distance+=std::pow(scene_camera.inverse_view[i]-previous_camera.inverse_view[i],2.f);
    const auto now=std::chrono::steady_clock::now();
    const bool reset=!had_camera||std::chrono::duration<double>(now-last_valid_frame).count()>.25||std::abs(scene_camera.fov-previous_camera.fov)>.05f||camera_distance>scene_camera.near_plane*scene_camera.near_plane*100.f||temporal_inputs.motion.width!=color->width/2||temporal_inputs.motion.height!=color->height/2;
    if(!record_temporal(cb,hudless_input,scene_depth_copy,clip_to_previous,reset)){invalidate_temporal_frame();return;}
    sl::Constants constants{};
    const auto projection=zero_one_projection(scene_camera.projection);
    temporal::Matrix inverse_projection{},inverse_vp{},prev_to_clip{},clip_to_prev{};
    temporal::inverse(projection,inverse_projection);
    const auto vp=temporal::multiply(projection,scene_camera.view);temporal::inverse(vp,inverse_vp);
    const auto previous_vp=!reset?temporal::multiply(zero_one_projection(previous_camera.projection),previous_camera.view):vp;
    clip_to_prev=temporal::multiply(previous_vp,inverse_vp);temporal::inverse(clip_to_prev,prev_to_clip);
    constants.cameraViewToClip=sdk_matrix(projection);constants.clipToCameraView=sdk_matrix(inverse_projection);
    constants.clipToPrevClip=sdk_matrix(clip_to_prev);constants.prevClipToClip=sdk_matrix(prev_to_clip);
    constants.jitterOffset={0,0};constants.mvecScale={1,1};constants.cameraPinholeOffset={0,0};
    const auto& view=scene_camera.inverse_view;
    constants.cameraPos={view[3],view[7],view[11]};constants.cameraRight={view[0],view[4],view[8]};
    constants.cameraUp={view[1],view[5],view[9]};constants.cameraFwd={-view[2],-view[6],-view[10]};
    constants.cameraNear=scene_camera.near_plane;constants.cameraFar=scene_camera.far_plane;constants.cameraFOV=scene_camera.fov;
    constants.cameraAspectRatio=scene_camera.projection[5]/scene_camera.projection[0];
    constants.depthInverted=sl::eFalse;constants.cameraMotionIncluded=sl::eTrue;constants.motionVectors3D=sl::eFalse;constants.reset=reset?sl::eTrue:sl::eFalse;
    report_result("Constants",set_constants(constants,*active_token,sl::ViewportHandle(0)));
    auto depth=temporal_resource(temporal_inputs.depth),motion=temporal_resource(temporal_inputs.motion);
    GpuImage* filtered_hudless=has_hudless?present_scene_only_record(cb,hudless_input,output_width,output_height):nullptr;
    if(!filtered_hudless){invalidate_temporal_frame();return;}
    sl::Resource hudless=temporal_resource(*filtered_hudless);
    if(filtered_hudless)hudless.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    bool use_hudless=filtered_hudless!=nullptr;
    const sl::Extent extent{0,0,output_width,output_height};
    const sl::ResourceTag tags[]={{&depth,sl::kBufferTypeDepth,sl::eValidUntilPresent},{&motion,sl::kBufferTypeMotionVectors,sl::eValidUntilPresent},{use_hudless?&hudless:nullptr,sl::kBufferTypeHUDLessColor,sl::eValidUntilPresent},{nullptr,sl::kBufferTypeBackbuffer,sl::eValidUntilPresent,&extent}};
    report_result("Frame inputs",set_tags(*active_token,sl::ViewportHandle(0),tags,4,nullptr));
    static bool log_hudless=false;if(use_hudless&&!log_hudless){std::fprintf(stderr,"[DLSS-G] Captured pre-HUD scene color %ux%u\n",hudless_input.width,hudless_input.height);log_hudless=true;}
    if(!fg_enabled){
        sl::DLSSGOptions options{};options.mode=sl::DLSSGMode::eOn;
        options.numFramesToGenerate=std::min(maximum_generated,uint32_t(std::clamp(catherine::host_settings().frame_generation_multiplier-1,1,3)));
        options.numBackBuffers=backbuffers;options.mvecDepthWidth=temporal_inputs.motion.width;options.mvecDepthHeight=temporal_inputs.motion.height;
        options.colorWidth=output_width;options.colorHeight=output_height;options.onErrorCallback=api_error;
        if(std::getenv("CATHERINE_STREAMLINE_DEBUG")&&std::getenv("CATHERINE_DLSS_SHOW_GENERATED"))options.flags|=sl::DLSSGFlags::eShowOnlyInterpolatedFrame;
        const auto result=set_dlssg_options(sl::ViewportHandle(0),options);report_result("Enable",result);fg_enabled=result==sl::Result::eOk;
        std::fprintf(stderr,"[DLSS-G] Requested %ux; enable=%d, depth/motion=%ux%u, color=%ux%u\n",options.numFramesToGenerate+1,int(result),options.mvecDepthWidth,options.mvecDepthHeight,output_width,output_height);
    }
    previous_camera=scene_camera;had_camera=true;last_valid_frame=now;
}
void dlss_backbuffer_ready(VkImage image,VkImageView view,VkFormat format,unsigned width,unsigned height){
    if(!get_dlssg_state||!set_tags||!active_token||!view)return;
    sl::Resource resource(sl::ResourceType::eTex2d,reinterpret_cast<void*>(image),nullptr,reinterpret_cast<void*>(view),dlss_backbuffer_layout());
    resource.width=width;resource.height=height;resource.nativeFormat=format;resource.mipLevels=resource.arrayLayers=1;resource.flags=0;
    resource.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    resource.internalFlags=sl::Resource::eVulkanSwapChainImage;
    const sl::Extent extent{0,0,width,height};const sl::ResourceTag tag{&resource,sl::kBufferTypeBackbuffer,sl::eValidUntilPresent,&extent};
    report_result("Backbuffer state",set_tags(*active_token,sl::ViewportHandle(0),&tag,1,nullptr));
}
void* dlss_render_end(){if(marker&&active_token)report_result("Render end",marker(sl::PCLMarker::eRenderSubmitEnd,*active_token));auto token=active_token;active_token=nullptr;return token;}
void dlss_present_begin(void* frame){auto* token=static_cast<sl::FrameToken*>(frame);if(marker&&token)report_result("Present begin",marker(sl::PCLMarker::ePresentStart,*token));}
void dlss_present_end(void* frame){
    auto* token=static_cast<sl::FrameToken*>(frame);if(marker&&token)report_result("Present end",marker(sl::PCLMarker::ePresentEnd,*token));
    if(get_dlssg_state){sl::DLSSGState state{};auto result=get_dlssg_state(sl::ViewportHandle(0),state,nullptr);
        total_presents.fetch_add(state.numFramesActuallyPresented,std::memory_order_relaxed);generated_recently.store(state.numFramesActuallyPresented>1,std::memory_order_relaxed);
        const uint32_t presented_frame=token?uint32_t(*token):0;
        if(presented_frame%60==0||state.status!=sl::DLSSGStatus::eOk)std::fprintf(stderr,"[DLSS-G] Frame=%u query=%d status=%u actual_presents=%u\n",presented_frame,int(result),unsigned(state.status),state.numFramesActuallyPresented);}
}
namespace {Upload input_readback[4];unsigned readback_width=0,readback_height=0;}
void dlss_debug_record(VkCommandBuffer cb){
    if(!std::getenv("CATHERINE_DLSS_DUMP_INPUTS")||!temporal_inputs.motion.image)return;
    readback_width=temporal_inputs.motion.width;readback_height=temporal_inputs.motion.height;
    GpuImage* images[4]={&temporal_inputs.motion,&temporal_inputs.depth,&temporal_inputs.history[temporal_inputs.current^1],&temporal_inputs.history[temporal_inputs.current]};
    for(unsigned i=0;i<4;++i){auto& image=*images[i];auto& readback=input_readback[i];readback=upload_alloc(uint64_t(readback_width)*readback_height*4,16);
        transition(cb,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);VkBufferImageCopy copy{};copy.bufferOffset=readback.offset;copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={readback_width,readback_height,1};
        p_vkCmdCopyImageToBuffer(cb,image.image,image.layout,readback.buffer,1,&copy);transition(cb,image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);}
    VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    p_vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);
}
void dlss_debug_write(){
    if(!readback_width)return;
    const char* names[]={"dlss-motion.bin","dlss-depth.bin","dlss-current.bin","dlss-previous.bin"};
    for(unsigned i=0;i<4;++i)if(FILE* file=std::fopen(names[i],"wb")){std::fwrite(&readback_width,4,1,file);std::fwrite(&readback_height,4,1,file);std::fwrite(input_readback[i].ptr,4,size_t(readback_width)*readback_height,file);std::fclose(file);}
    readback_width=0;
}
uint64_t dlss_presented_count(){return total_presents.load(std::memory_order_relaxed);}
bool dlss_generating(){return generated_recently.load(std::memory_order_relaxed);}

}
#else
namespace nx::nvn {
PFN_vkGetInstanceProcAddr dlss_vulkan_loader(PFN_vkGetInstanceProcAddr native){return native;}
bool dlss_proxy_active(){return false;}
VkImageLayout dlss_backbuffer_layout(){return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;}
void dlss_adapter_selected(VkPhysicalDevice){}
void dlss_device_ready(VkPhysicalDevice){}
void dlss_scene_targets(GpuImage* const*,unsigned){}
void dlss_scene_capture(VkCommandBuffer){}
bool dlss_ui_needed(const char*){return false;}
void dlss_capture_hudless(VkCommandBuffer,GpuImage*){}
GpuImage* dlss_debug_hudless(){return nullptr;}
void dlss_render_begin(){}
void dlss_prepare_frame(VkCommandBuffer,GpuImage*,unsigned,unsigned,unsigned){}
void dlss_backbuffer_ready(VkImage,VkImageView,VkFormat,unsigned,unsigned){}
void* dlss_render_end(){return nullptr;}
void dlss_present_begin(void*){}
void dlss_present_end(void*){}
void dlss_debug_record(VkCommandBuffer){}
void dlss_debug_write(){}
uint64_t dlss_presented_count(){return 0;}
bool dlss_generating(){return false;}
bool dlss_depth_snapshot_pending(){return false;}
}
#endif
