#pragma once
#include <stdint.h>

typedef struct {
    float left, top, width;
    unsigned count, visible;
} CatherineDevicePopup;

typedef void (*CatherineDropdownRect)(void *,float,float,float,float,uint32_t);

static inline int catherine_dropdown_hit(const CatherineDevicePopup *p,float x,float y) {
    const float rows_top=p->top+10.0f;
    if(x<p->left||x>=p->left+p->width||y<rows_top||y>=rows_top+55.0f*p->visible)return -1;
    return (int)((y-rows_top)/55.0f);
}

static inline void catherine_dropdown_outline(void *ctx,CatherineDropdownRect rect,float x,float y,
                                               float width,float height,uint32_t rgba) {
    rect(ctx,x,y,width,2.0f,rgba);
    rect(ctx,x,y+height-2.0f,width,2.0f,rgba);
    rect(ctx,x,y,2.0f,height,rgba);
    rect(ctx,x+width-2.0f,y,2.0f,height,rgba);
}

static inline void catherine_dropdown_frame(void *ctx,CatherineDropdownRect rect,
                                             const CatherineDevicePopup *p,unsigned scroll,uint32_t alpha) {
    const float height=55.0f*p->visible,box_height=height+20.0f;
    rect(ctx,p->left,p->top,p->width,box_height,0x20202000u|alpha);
    catherine_dropdown_outline(ctx,rect,p->left,p->top,p->width,box_height,0xffffff00u|alpha);
    if(p->count>p->visible){
        const float thumb=height*p->visible/p->count;
        rect(ctx,p->left+p->width-8.0f,p->top+10.0f,2.0f,height,0x77777700u|alpha);
        rect(ctx,p->left+p->width-8.0f,p->top+10.0f+(height-thumb)*scroll/(p->count-p->visible),
             2.0f,thumb,0xff369800u|alpha);
    }
}
