#include "../imports/graphics/src/nvn/dlss_camera.h"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace nx::nvn::temporal;
int main(){
 Matrix view={1,0,0,-10,0,1,0,20,0,0,1,-30,0,0,0,1};
 const float near=25,far=144000,sy=1.920982f,sx=sy*9/16;
 Matrix projection={sx,0,0,0,0,sy,0,0,0,0,(far+near)/(near-far),2*far*near/(near-far),0,0,-1,0};
 std::array<float,36> constants{};const auto vp=multiply(projection,view);
 std::copy(vp.begin(),vp.end(),constants.begin());std::copy(view.begin(),view.end(),constants.begin()+16);
 const auto camera=extract(constants.data(),sizeof(constants));assert(camera.valid);
 assert(std::abs(camera.near_plane-near)<.01f&&std::abs(camera.far_plane-far)<200);
 assert(std::abs(camera.fov-2*std::atan(1/sy))<.001f);
 const auto identity=multiply(camera.view,camera.inverse_view);
 for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)assert(std::abs(identity[r*4+c]-(r==c?1.f:0.f))<.001f);
 assert(!extract(constants.data(),64).valid);
 constants[0]=std::numeric_limits<float>::quiet_NaN();assert(!extract(constants.data(),sizeof(constants)).valid);
 constants[0]=vp[0];constants[16]=2;assert(!extract(constants.data(),sizeof(constants)).valid);
 constants.fill(0);assert(!extract(constants.data(),sizeof(constants)).valid);
 puts("Real-camera extraction, near/far/FOV, inverse transforms and invalid-data rejection passed.");
}
