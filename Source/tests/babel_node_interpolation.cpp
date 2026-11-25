#include "babel_node_interpolation.h"
#include <cassert>
#include <cstdio>
using namespace catherine::visual;using namespace catherine::visual::babel;
Matrix translation(float x){return {1,0,0,x,0,1,0,0,0,0,1,0,0,0,0,1};}
uint64_t clock(unsigned tick,float alpha,bool enabled=true){uint32_t bits;std::memcpy(&bits,&alpha,4);return(uint64_t(tick)<<32)|bits|(enabled?0x80000000u:0);}
int main(){
 for(unsigned id:{2,3,4,5,6,7,8,9,14,15,19,20}){unsigned count=id==2?91:id==3?167:id==4?131:id==5?249:id==6?114:id==7?80:id==8?174:id==9?107:id==19?186:id==20?221:126;assert(supported(id,30,count));assert(!supported(id,30,count+1));}
 assert(supported(2,31,74));assert(!supported(21,30,65));assert(!supported(2,10,91));
 NodeHistory h;h.sample(translation(0),1,clock(1,0));h.sample(translation(0),2,clock(1,.5f));
 assert(h.sample(translation(10),3,clock(2,0))[3]==0);
 assert(std::abs(h.sample(translation(10),4,clock(2,.5f))[3]-5)<1e-5);
 auto repeated=h.sample(translation(10),4,clock(2,.5f));assert(repeated[3]==5);
 NodeHistory native;native.sample(translation(0),1,clock(1,0));native.sample(translation(5),2,clock(1,.5f));assert(native.sample(translation(10),3,clock(2,0))[3]==10);
 NodeHistory late;late.sample(translation(0),1,clock(1,0));assert(late.sample(translation(10),2,clock(1,.5f))[3]==0);assert(late.sample(translation(10),3,clock(2,0))[3]==5);assert(late.sample(translation(20),4,clock(2,.5f))[3]==10);assert(late.sample(translation(20),5,clock(3,0))[3]==15);
 NodeHistory high;float last=0;for(unsigned f=0;f<32;++f){unsigned tick=1+f/8;float phase=(f%8)/8.f;float source=f<4?0:10.f*(1+(f-4)/8);float out=high.sample(translation(source),f+1,clock(tick,phase))[3];assert(out>=last-.0001f&&out-last<=1.251f);last=out;}
 assert(h.sample(translation(30),5,clock(3,0,false))[3]==30);
 assert(h.sample(translation(40),6,clock(4,0))[3]==40);
 NodeHistory other;assert(other.sample(translation(900),1,clock(1,0))[3]==900);
 Matrix inverse{},bound{},bind=translation(-30),world=translation(45);assert(inverse_affine(world.data(),inverse.data()));multiply_affine(world.data(),bind.data(),bound.data());assert(bound[3]==15);
 assert(transpose(transpose(world))==world);std::puts("Babel admission, 30/60 cadence, native 60 channel, resets and matrix convention passed");
 // Rigid and skinned versions of one point must remain coincident during a
 // turn. Blending an already inverse-bound skin matrix used to break this.
 Matrix turn0=translation(0),turn90={0,0,1,0,0,1,0,0,-1,0,0,0,0,0,0,1};
 NodeHistory turn;turn.sample(turn0,1,clock(1,0));turn.sample(turn0,2,clock(1,.5f));turn.sample(turn90,3,clock(2,0));Matrix halfway=turn.sample(turn90,4,clock(2,.5f));
 Matrix skin{};auto inv_bind=translation(-20);multiply_affine(halfway.data(),inv_bind.data(),skin.data());
 for(unsigned axis=0;axis<3;++axis){float rigid_point=halfway[4*axis]*10+halfway[4*axis+3];float skinned_point=skin[4*axis]*30+skin[4*axis+3];assert(std::abs(rigid_point-skinned_point)<.0001f);}
 assert(std::abs(halfway[0]-std::sqrt(.5f))<.0001f&&std::abs(halfway[8]+std::sqrt(.5f))<.0001f);
 std::puts("Shared-node rigid/skin inverse-bind coherence during a 90-degree turn passed");
}
