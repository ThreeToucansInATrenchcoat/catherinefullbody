#pragma once
#include "nvn_draw_state.h"
#include "producer_fixed_pipeline.h"
namespace nx::nvn::native_draw_depth {
struct Values {VkBool32 test,write,stencil,slack;bool operator==(const Values&)const=default;};
inline int mode(){static const int m=[] {const char* e=std::getenv("CATHERINE_VULKAN_DRAW_DEPTH");return e?std::atoi(e):1;}();return m;}
inline Values original(const DepthStencilInfo& d){return {VkBool32(bool(d.depth_test)),VkBool32(bool(d.depth_write)),VkBool32(bool(d.stencil_test)),VkBool32(equal_slack_pass(d))};}
inline Values read(const DepthStencilInfo& d){
 if(!mode()||!producer_fixed::mode()||&d!=&replay::g_state.depth||!(producer_fixed::state.valid&producer_fixed::DepthBit))return original(d);
 const auto& p=producer_fixed::state.values.depth;const Values native{p.test,p.write,p.stencil,p.slack};
 if(mode()==2){if(!(native==original(d)))fatal("Native draw depth decisions differ");static uint64_t checks=0;if(++checks==1||checks%100000==0)log("Native draw depth decisions: %llu compared",(unsigned long long)checks);}return native;
}
inline bool writes(const DepthStencilInfo& d){const auto v=read(d);return v.write||v.stencil;}
}
