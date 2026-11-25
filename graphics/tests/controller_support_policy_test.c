#include "controller_support_policy.h"
#include <assert.h>
#include <stdio.h>
static uint32_t result_status(const unsigned char *result){uint32_t value;memcpy(&value,result+8,4);return value;}
int main(void){
    unsigned char output[12],arg[0x430]={0,4,1,1,1,0};
    CatherineControllerSupportRequest request=catherine_controller_support_default();
    assert(catherine_controller_support_parse(&request,arg,sizeof arg));
    catherine_controller_support_result(output,request,1);assert(output[0]==1&&result_status(output)==0);
    catherine_controller_support_result(output,request,2);assert(output[0]==2&&result_status(output)==0);
    arg[0]=2;arg[1]=2;assert(catherine_controller_support_parse(&request,arg,sizeof arg));
    catherine_controller_support_result(output,request,2);assert(output[0]==2&&result_status(output)==0);
    catherine_controller_support_result(output,request,1);assert(output[0]==1&&result_status(output)==2);
    catherine_controller_support_result(output,request,0);assert(output[0]==0&&result_status(output)==2);
    arg[0]=1;arg[1]=4;arg[5]=1;assert(catherine_controller_support_parse(&request,arg,sizeof arg));
    catherine_controller_support_result(output,request,2);assert(output[0]==1&&result_status(output)==0);
    arg[5]=0;arg[1]=1;assert(catherine_controller_support_parse(&request,arg,sizeof arg));
    catherine_controller_support_result(output,request,2);assert(output[0]==1&&result_status(output)==0);
    arg[0]=3;assert(!catherine_controller_support_parse(&request,arg,sizeof arg));
    assert(!catherine_controller_support_parse(&request,arg,5));
    puts("Native ControllerSupport min/max, single-player, two-player and unavailable-player cancel fixtures passed");
}
