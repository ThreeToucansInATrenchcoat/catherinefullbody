#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {unsigned minimum,maximum,single;} CatherineControllerSupportRequest;
static inline CatherineControllerSupportRequest catherine_controller_support_default(void){
    CatherineControllerSupportRequest request={0,4,0};return request;
}
static inline int catherine_controller_support_parse(CatherineControllerSupportRequest *request,const unsigned char *arg,size_t size){
    if(size<6||arg[0]>8||arg[1]>8||arg[0]>arg[1]||arg[5]>1)return 0;
    request->minimum=arg[0];request->maximum=arg[1];request->single=arg[5];return 1;
}
// The native SDK copies bytes 0..7 into ControllerSupportResultInfo, then reads
// the applet's success/cancel status at +8. Do not manufacture missing players.
static inline void catherine_controller_support_result(unsigned char output[12],CatherineControllerSupportRequest request,unsigned connected){
    memset(output,0,12);
    unsigned count=connected<request.maximum?connected:request.maximum;
    if(request.single&&count>1)count=1;
    output[0]=(unsigned char)count;
    if(count<request.minimum){const uint32_t cancelled=2;memcpy(output+8,&cancelled,4);}
}
