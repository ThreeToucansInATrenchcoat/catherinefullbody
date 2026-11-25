#pragma once
#include "cpu.h"
#include "timing_clock.h"
#include <math.h>
#include <string.h>

uint64_t catherine_camera_render_clock(void);
uint64_t catherine_announcement_frame_clock(void);
typedef struct CatherineChurchCameraTransaction {
    struct CatherineChurchCameraTransaction *previous;
    uint64_t owner;
    uint32_t before[6], authoritative[6], displayed[6], zone[2];
    uint32_t epoch, selection;
    unsigned enabled, eligible, restore, tick;
    float phase;
} CatherineChurchCameraTransaction;
static _Thread_local CatherineChurchCameraTransaction *catherine_church_camera_active;
typedef struct CatherineChurchCameraHistory {
    uint64_t owner;
    uint32_t previous[6],current[6],selection,zone[2],epoch;
    unsigned valid;
} CatherineChurchCameraHistory;
static _Thread_local CatherineChurchCameraHistory catherine_church_camera_history;
static const unsigned catherine_church_pose_offsets[6]={0x30,0x34,0x38,0x50,0x54,0x58};
static int catherine_church_camera_owner(uint64_t owner) {
    uint64_t cell=LD64(0x7000cca408ull),root=cell?LD64(cell):0;
    return root && owner==root+0x8010ull;
}
static void catherine_church_camera_enter(uint64_t parent) {
    uint64_t owner=parent+0x60ull;
    if(catherine_church_camera_history.valid && (catherine_church_camera_history.owner!=owner ||
       !catherine_church_camera_owner(owner) || LD32(parent+0x160)!=0xffffffffu || LD32(owner+0x64)!=7u))catherine_church_camera_history.valid=0;
}
static void catherine_church_camera_begin(CatherineChurchCameraTransaction *tx,uint64_t owner) {
    memset(tx,0,sizeof *tx);tx->previous=catherine_church_camera_active;
    catherine_church_camera_active=tx;tx->owner=owner;
    if (!catherine_high_refresh_active() || (LD8(0x7000d06360ull)&1u) || !catherine_church_camera_owner(owner)) return;
    if (LD32(owner+0x64)!=7u || LD32(owner+0x100)!=0xffffffffu || LD32(owner+0x6c)!=0x40800000u ||
        LD32(owner+0x84) || LD32(owner+0x88) || LD32(owner+0x70) || LD32(owner+0x90)) return;
    uint32_t delta_bits=LD32(0x70031cc1e8ull);float dt;memcpy(&dt,&delta_bits,4);
    if (!isfinite(dt) || dt<0.f || dt>.1f) return;
    for(unsigned i=0;i<6;i++){tx->before[i]=LD32(owner+catherine_church_pose_offsets[i]);float f;memcpy(&f,&tx->before[i],4);if(!isfinite(f))return;}
    tx->zone[0]=LD32(owner+0x8c);tx->zone[1]=LD32(owner+0x90);
    tx->tick=catherine_timing_tick();tx->epoch=(uint32_t)(catherine_announcement_frame_clock()>>32);uint64_t clock=catherine_camera_render_clock();
    if(!tx->epoch)return;
    uint32_t phase_bits=(uint32_t)clock&0x7fffffffu;float phase;memcpy(&phase,&phase_bits,4);
    /* This consumer uses the publisher's residual, including zero-delta and
       32 Hz samples; its interpolation opt-in flag belongs to older samplers. */
    tx->phase=isfinite(phase)&&phase>=0.f&&phase<=1.f?phase:0.f;
    tx->enabled=1;
}
static void catherine_church_camera_mark(uint64_t owner,uint32_t selection,uint32_t geometry,uint64_t caller) {
    CatherineChurchCameraTransaction *tx=catherine_church_camera_active;
    if(!tx || !tx->enabled || tx->owner!=owner || caller!=0x70000d857cull || geometry!=0u) return;
    if(!((selection>=1u&&selection<=14u)||(selection>=20u&&selection<=23u)))return;
    if(LD32(owner+0x64)!=7u || LD32(owner+0x6c)!=0x40800000u || LD32(owner+0x84) || LD32(owner+0x88) || LD32(owner+0x70))return;
    if(LD32(owner+0x8c)!=tx->zone[0] || LD32(owner+0x90)!=tx->zone[1])return;
    tx->eligible=1;tx->selection=selection;
}
static void catherine_church_camera_present(CatherineChurchCameraTransaction *tx) {
    if(catherine_church_camera_active==tx)catherine_church_camera_active=tx->previous;
    CatherineChurchCameraHistory *history=&catherine_church_camera_history;
    if(!tx->eligible || !catherine_church_camera_owner(tx->owner)) {
        if(history->owner==tx->owner)history->valid=0;
        return;
    }
    float a[6],n[6],v[6];uint32_t next_bits[6];unsigned next_finite=1;
    for(unsigned i=0;i<6;i++){
        next_bits[i]=LD32(tx->owner+catherine_church_pose_offsets[i]);memcpy(&n[i],&next_bits[i],4);
        if(!isfinite(n[i]))next_finite=0;
        tx->authoritative[i]=tx->tick?next_bits[i]:tx->before[i];
    }
    if(tx->tick && !next_finite){history->valid=0;return;}
    unsigned matching=history->valid && history->owner==tx->owner && history->selection==tx->selection &&
       history->zone[0]==tx->zone[0] && history->zone[1]==tx->zone[1] &&
       (uint32_t)(tx->epoch-history->epoch)<=1u && !memcmp(history->current,tx->before,sizeof tx->before);
    if(!matching) {
        memcpy(history->previous,tx->before,sizeof tx->before);memcpy(history->current,tx->before,sizeof tx->before);
        history->owner=tx->owner;history->selection=tx->selection;memcpy(history->zone,tx->zone,sizeof tx->zone);history->valid=1;
    }
    if(tx->tick){if(!matching || history->epoch!=tx->epoch)memcpy(history->previous,tx->before,sizeof tx->before);memcpy(history->current,next_bits,sizeof next_bits);}
    history->epoch=tx->epoch;
    for(unsigned i=0;i<6;i++){memcpy(&a[i],&history->previous[i],4);memcpy(&n[i],&history->current[i],4);v[i]=a[i];}
    /* Interpolate a look target and a relative direction/radius, not a chord
       that could collapse the native camera radius halfway through a turn. */
    if(tx->phase>=1.f)memcpy(v,n,sizeof v);
    else if(tx->phase>0.f) {
        double da[3],dn[3],la=0,ln=0;
        for(unsigned i=0;i<3;i++){da[i]=(double)a[i+3]-a[i];dn[i]=(double)n[i+3]-n[i];la+=da[i]*da[i];ln+=dn[i]*dn[i];}
        la=sqrt(la);ln=sqrt(ln);
        if(isfinite(la)&&isfinite(ln)&&la>.001&&ln>.001) {
            double d[3],length=0,dot=0,t=tx->phase;
            for(unsigned i=0;i<3;i++){double ua=da[i]/la,un=dn[i]/ln;dot+=ua*un;d[i]=ua+(un-ua)*t;length+=d[i]*d[i];}
            length=sqrt(length);
            if(isfinite(length)&&length>.001&&dot>-.95) {
                double radius=la+(ln-la)*t;float next[6];unsigned valid=1;
                for(unsigned i=0;i<3;i++){next[i]=(float)((double)a[i]+((double)n[i]-a[i])*t);next[i+3]=(float)((double)next[i]+d[i]/length*radius);if(!isfinite(next[i])||!isfinite(next[i+3]))valid=0;}
                if(valid)memcpy(v,next,sizeof v);
            }
        }
    }
    for(unsigned i=0;i<6;i++){memcpy(&tx->displayed[i],&v[i],4);ST32(tx->owner+catherine_church_pose_offsets[i],tx->displayed[i]);}
    tx->restore=1;
}
static void catherine_church_camera_end(CatherineChurchCameraTransaction *tx) {
    if(tx->restore && !catherine_church_camera_owner(tx->owner) && catherine_church_camera_history.owner==tx->owner)
        catherine_church_camera_history.valid=0;
    if(tx->restore && catherine_church_camera_owner(tx->owner)) {
        if(LD32(tx->owner+0x100)!=0xffffffffu || LD32(tx->owner+0x64)!=7u || LD32(tx->owner+0x84) ||
           LD32(tx->owner+0x88) || LD32(tx->owner+0x70) || LD32(tx->owner+0x8c)!=tx->zone[0] ||
           LD32(tx->owner+0x90)!=tx->zone[1])catherine_church_camera_history.valid=0;
        unsigned unchanged=1;
        for(unsigned i=0;i<6;i++)if(LD32(tx->owner+catherine_church_pose_offsets[i])!=tx->displayed[i])unchanged=0;
        /* A native reset or external mutation during publication wins. */
        if(unchanged)for(unsigned i=0;i<6;i++)ST32(tx->owner+catherine_church_pose_offsets[i],tx->authoritative[i]);
        else if(catherine_church_camera_history.owner==tx->owner)catherine_church_camera_history.valid=0;
    }
    if(catherine_church_camera_active==tx)catherine_church_camera_active=tx->previous;
}
