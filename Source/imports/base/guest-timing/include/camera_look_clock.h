#pragma once
#include "camera_filter_clock.h"
static uint32_t catherine_camera_look_step(uint32_t b) {
 if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull)&1u)) return b;
 return (b&0x80000000u) | catherine_app_elapsed_step(b&0x7fffffffu,0x3d088889u,LD32(0x70031cc1e8ull));
}
static uint32_t catherine_camera_look_blend(uint32_t previous,uint32_t next,float gain) {
 if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull)&1u) || !(gain>0.f && gain<1.f)) return next;
 uint32_t g;memcpy(&g,&gain,4);uint32_t scaled=catherine_camera_filter_weight(g,0);float ng,p,n;memcpy(&ng,&scaled,4);memcpy(&p,&previous,4);memcpy(&n,&next,4);
 float value=p+(n-p)*(ng/gain);memcpy(&next,&value,4);return next;
}

static uint32_t catherine_mouse_camera_vertical_result(uint32_t previous,uint32_t input,uint32_t native_result,float gain) {
 /* The native vertical recurrence halves its entire result, including the
    previous offset. A compensated target alone therefore cannot retain mouse
    look. Integrate the original manual input directly for this one mouse path;
    the controller still receives the original native result and clock blend. */
 float native_gain=0.5f*(1.f+gain);
 if(!catherine_mouse_camera_hold())return catherine_camera_look_blend(previous,native_result,native_gain);
 float p,v,dt;memcpy(&p,&previous,4);memcpy(&v,&input,4);
 uint32_t t=LD32(0x70031cc1e8ull);memcpy(&dt,&t,4);
 if(!isfinite(p)||!isfinite(v)||!isfinite(dt)||dt<0.f||dt>.1f||!isfinite(native_gain)||!(native_gain>0.f))return previous;
 const float limit=40.f;
 if(v==0.f&&fabsf(p)<=limit)return previous;
 float maximum_speed=fminf(limit,fabsf(v))*fmaxf(0.f,fminf(1.f,native_gain))*30.f;
 float velocity=fmaxf(-maximum_speed,fminf(maximum_speed,v*6.f));
 float value=fmaxf(-limit,fminf(limit,p+velocity*dt));
 memcpy(&input,&value,4);return input;
}

static uint32_t catherine_mouse_camera_step(uint32_t step,unsigned axis) {
 if(!catherine_mouse_camera_hold())return step;
 uint64_t pad=LD64(0x7000cc9070ull);if(!pad)return step;
 float v;memcpy(&v,&step,4);v*=fminf(1.f,fabsf((float)(int32_t)LD32(pad+40+axis*4))/127.f);
 memcpy(&step,&v,4);return step;
}
