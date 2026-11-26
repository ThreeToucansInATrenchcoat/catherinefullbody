#pragma once
#include <array>
#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
namespace nx::nvn::temporal {
using Matrix=std::array<float,16>;
inline Matrix multiply(const Matrix& a,const Matrix& b){Matrix result{};for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)for(unsigned k=0;k<4;++k)result[row*4+col]+=a[row*4+k]*b[k*4+col];return result;}
inline bool inverse(const Matrix& source,Matrix& result){
 double work[4][8]{};for(unsigned row=0;row<4;++row){for(unsigned col=0;col<4;++col){if(!std::isfinite(source[row*4+col]))return false;work[row][col]=source[row*4+col];}work[row][row+4]=1;}
 for(unsigned col=0;col<4;++col){unsigned pivot=col;for(unsigned row=col+1;row<4;++row)if(std::abs(work[row][col])>std::abs(work[pivot][col]))pivot=row;if(std::abs(work[pivot][col])<1e-10)return false;for(unsigned k=0;k<8;++k)std::swap(work[col][k],work[pivot][k]);const double divisor=work[col][col];for(unsigned k=0;k<8;++k)work[col][k]/=divisor;for(unsigned row=0;row<4;++row)if(row!=col){const double scale=work[row][col];for(unsigned k=0;k<8;++k)work[row][k]-=scale*work[col][k];}}
 for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col)result[row*4+col]=float(work[row][col+4]);return true;
}
struct Camera {Matrix view_projection{},view{},projection{},inverse_view{};float near_plane=0,far_plane=0,fov=0;bool valid=false;};
// The game's 144-byte vertex CB1 starts with row-major view-projection and view
// matrices. Accept only a matched rigid view and perspective projection pair.
inline Camera extract(const void* bytes,size_t count){
 Camera camera;if(!bytes||count<128)return camera;
 std::memcpy(camera.view_projection.data(),bytes,64);std::memcpy(camera.view.data(),static_cast<const unsigned char*>(bytes)+64,64);
 for(float value:camera.view_projection)if(!std::isfinite(value))return camera;
 if(!inverse(camera.view,camera.inverse_view))return camera;
 const auto& v=camera.view;
 if(std::abs(v[12])+std::abs(v[13])+std::abs(v[14])+std::abs(v[15]-1)>.005f)return camera;
 for(unsigned row=0;row<3;++row)for(unsigned other=0;other<3;++other){float dot=0;for(unsigned col=0;col<3;++col)dot+=v[row*4+col]*v[other*4+col];if(std::abs(dot-(row==other?1.f:0.f))>.005f)return camera;}
 camera.projection=multiply(camera.view_projection,camera.inverse_view);const auto& p=camera.projection;
 if(p[0]<.1f||p[0]>10||p[5]<.1f||p[5]>10||std::abs(p[14]+1)>.005f||std::abs(p[15])>.02f||p[10]>=-1||p[11]>=0)return camera;
 for(unsigned i:{1u,3u,4u,7u,8u,9u,12u,13u})if(std::abs(p[i])>.03f)return camera;
 camera.near_plane=p[11]/(p[10]-1);camera.far_plane=p[11]/(p[10]+1);camera.fov=2*std::atan(1/p[5]);
 camera.valid=std::isfinite(camera.far_plane)&&camera.near_plane>0&&camera.far_plane>camera.near_plane&&camera.fov>.1f&&camera.fov<3;
 return camera;
}
}
