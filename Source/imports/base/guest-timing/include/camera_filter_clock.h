#pragma once
#include "app_elapsed.h"
int catherine_high_refresh_active(void);
static uint32_t catherine_camera_filter_weight(uint32_t b,int retention){
 if(!catherine_high_refresh_active()||(LD8(0x7000d06360ull)&1u))return b;
 float g,dt;memcpy(&g,&b,4);uint32_t t=LD32(0x70031cc1e8ull);memcpy(&dt,&t,4);
 if(!isfinite(g)||g<=0.f||g>=1.f||!isfinite(dt)||dt<0.f||dt>.1f)return b;
 g=retention?expf(logf(g)*(dt*30.f)):-expm1f(log1pf(-g)*(dt*30.f));
 memcpy(&b,&g,4);return b;
}

#include "config_pc_host.h"
/* Only manual look targets are retained; scripted camera transforms still run. */
static int catherine_mouse_camera_hold(void) {
 catherine_config_setting(PC_CURSOR_HIDDEN,1);
 return catherine_config_setting(PC_CAMERA_INPUT_LAYOUT,0)==0;
}
static uint32_t catherine_mouse_camera_target(uint32_t previous,uint32_t input,float gain,float limit) {
 if(!catherine_mouse_camera_hold())return input;
 float p,v,dt;memcpy(&p,&previous,4);memcpy(&v,&input,4);
 uint32_t t=LD32(0x70031cc1e8ull);memcpy(&dt,&t,4);
 if(!isfinite(p)||!isfinite(v)||!isfinite(dt)||dt<0.f||dt>.1f||!(gain>0.f))return previous;
 /* The native controller's strongest 30 Hz response bounds mouse velocity.
    gain has already been adjusted for this frame's duration: recover its
    original 30 Hz weight before converting that response to units/second. */
 float native_gain=gain;
 if(catherine_high_refresh_active()&&!(LD8(0x7000d06360ull)&1u)&&dt>0.f&&gain<1.f)
  native_gain=-expm1f(log1pf(-gain)/(dt*30.f));
 float maximum_speed=fminf(limit,fabsf(v))*fmaxf(0.f,fminf(1.f,native_gain))*30.f;
 float velocity=fmaxf(-maximum_speed,fminf(maximum_speed,v*6.f));
 float next=fmaxf(-limit,fminf(limit,p+velocity*dt));
 /* Compensate the native smoothing without altering the controller path. */
 float target=p+(next-p)/gain;memcpy(&input,&target,4);return input;
}

static float catherine_camera_gain(float gain) {
 uint32_t b;memcpy(&b,&gain,4);b=catherine_camera_filter_weight(b,0);memcpy(&gain,&b,4);return gain;
}

static uint32_t catherine_campaign_mouse_axis(uint64_t owner,uint32_t previous,int32_t input,uint32_t native_result,unsigned horizontal,uint32_t base_angle) {
 /* Boss/campaign cameras own a separate pair of manual offsets. Bit 0x20
    disables manual control during native camera overrides; leave all original
    controller and scripted results intact in that case. No host angle cache is
    used, so returning control starts from the current guest camera state. */
 if((LD32(owner+0x10ull)&0x20u)||!catherine_mouse_camera_hold())return native_result;
 float p,dt,lower,upper,gain;memcpy(&p,&previous,4);
 uint32_t b=LD32(0x70031cc1e8ull);memcpy(&dt,&b,4);
 b=LD32(0x7000719c50ull);memcpy(&gain,&b,4);
 if(horizontal){
  float angle,pi,margin;memcpy(&angle,&base_angle,4);
  b=LD32(0x7000719678ull);memcpy(&pi,&b,4);
  b=LD32(0x700071a2ccull);memcpy(&margin,&b,4);
  lower=-angle;upper=(pi-angle)+margin;
 }else{
  b=LD32(0x70007195f8ull);memcpy(&lower,&b,4);lower=-lower;
  b=LD32(0x700071961cull);memcpy(&upper,&b,4);
 }
 if(!isfinite(p)||!isfinite(dt)||dt<0.f||dt>.1f||!isfinite(lower)||!isfinite(upper)||lower>upper||!isfinite(gain)||!(gain>0.f))return previous;
 if(input==0&&p>=lower&&p<=upper)return previous;
 /* The host mouse axis represents velocity. Use the native strongest 30 Hz
    response as a per-second rate toward the selected directional bound. The
    exponential keeps the response independent of presentation cadence. */
 float axis=fmaxf(-1.f,fminf(1.f,(float)input/100.f));
 float extent=fmaxf(0.f,axis<0.f?p-lower:upper-p);
 float response=-expm1f(-fabsf(axis)*fminf(1.f,gain)*30.f*dt);
 float value=p+(axis<0.f?-extent:extent)*response;
 value=fmaxf(lower,fminf(upper,value));
 memcpy(&native_result,&value,4);return native_result;
}
