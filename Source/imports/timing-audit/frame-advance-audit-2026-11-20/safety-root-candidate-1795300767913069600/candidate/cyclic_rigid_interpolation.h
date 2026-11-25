#pragma once
#include "character_pose_interpolation.h"
namespace catherine::visual {
// A mesh with N identical sectors is invariant under local Z turns of 2*pi/N.
// Relabel the previous sector nearest the current pose before blending.
// This preserves forward motion when its authored angle wraps one sector.
inline void blend_cyclic_z(const float* a,const float* b,float t,float* out,unsigned sectors){
    if(sectors<2){blend_matrix(a,b,t,out);return;}
    float sa[3],sb[3];
    if(!decompose(a,sa)||!decompose(b,sb)){blend_matrix(a,b,t,out);return;}
    float cosine=0,sine=0;
    for(unsigned row=0;row<3;++row){
        const float ax=a[row*4]/sa[0],ay=a[row*4+1]/sa[1];
        const float bx=b[row*4]/sb[0],by=b[row*4+1]/sb[1];
        cosine+=ax*bx+ay*by;sine+=ay*bx-ax*by;
    }
    constexpr double tau=6.283185307179586476925286766559;
    const double step=tau/sectors;
    const int turns=int(std::round(std::atan2(double(sine),double(cosine))/step));
    if(!turns){blend_matrix(a,b,t,out);return;}
    const float c=float(std::cos(turns*step)),s=float(std::sin(turns*step));
    float equivalent[16];std::memcpy(equivalent,a,64);
    for(unsigned row=0;row<3;++row){
        equivalent[row*4]=a[row*4]*c+a[row*4+1]*s;
        equivalent[row*4+1]=-a[row*4]*s+a[row*4+1]*c;
    }
    // Keep the current native endpoint (and its vertex-color sector) intact.
    blend_matrix(equivalent,b,t,out);
}
}
