#include "cyclic_rigid_interpolation.h"
#include "rigid_late_sample_interpolation.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
using namespace catherine::visual;
using Matrix=std::array<float,16>;
constexpr double pi=3.14159265358979323846;
Matrix pose(double degrees){const float c=float(std::cos(degrees*pi/180)),s=float(std::sin(degrees*pi/180));return {-c,s,0,950,0,0,1,-2827,s,c,0,1150,0,0,0,1};}
double angle(const float* p){return std::atan2(double(p[8]),-double(p[0]))*180/pi;}
void check(bool b,const char* s){if(!b){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){
    auto a=pose(39.98),b=pose(.02);Matrix old{},correct{};
    blend_matrix(a.data(),b.data(),.5f,old.data());
    blend_cyclic_z(a.data(),b.data(),.5f,correct.data(),9);
    check(std::abs(std::remainder(angle(old.data())-40.,40.))>19.,"fixture exposes backwards half-sector sweep");
    check(std::abs(std::remainder(angle(correct.data())-40.,40.))<.0001,"sector wrap keeps forward angle");
    double worst=0;
    for(unsigned tick=0;tick<4000;++tick){
        const double previous=39.9+tick*.05,current=previous+.05;
        a=pose(std::fmod(previous,40.));b=pose(std::fmod(current,40.));
        for(unsigned sub=0;sub<8;++sub){const float t=sub/8.f;
            blend_cyclic_z(a.data(),b.data(),t,correct.data(),9);
            double error=std::abs(std::remainder(angle(correct.data())-(previous+.05*t),40.));worst=std::max(worst,error);
            check(error<.0002,"five complete animation cycles stay continuous at 240 Hz");
            check(correct[3]==950&&correct[7]==-2827&&correct[11]==1150&&correct[15]==1,"preserve center and affine row");
            Matrix unchanged{};blend_cyclic_z(a.data(),b.data(),t,unchanged.data(),1);blend_matrix(a.data(),b.data(),t,old.data());
            check(!std::memcmp(unchanged.data(),old.data(),64),"ordinary interpolation remains byte-identical");
        }
    }
    RigidLateSampleInterpolator cyclic,legacy;uint64_t frame=1;double worst_history=0;unsigned old_reverse_frames=0;
    for(unsigned tick=0;tick<4000;++tick){Matrix input=pose(std::fmod(39.9+tick*.05,40.));
        for(unsigned sub=0;sub<8;++sub,++frame){float phase=sub/8.f;uint32_t bits;std::memcpy(&bits,&phase,4);uint64_t clock=(uint64_t(tick)<<32)|bits|0x80000000ull;
            const auto* bytes=reinterpret_cast<const uint8_t*>(input.data());
            const auto* out=cyclic.render(1,bytes,64,frame,clock,true,9);
            const auto* old_out=legacy.render(1,bytes,64,frame,clock,true);
            if(tick){double expected=39.9+(tick-1+phase)*.05;
                Matrix m,om;std::memcpy(m.data(),out,64);std::memcpy(om.data(),old_out,64);
                double error=std::abs(std::remainder(angle(m.data())-expected,40.));worst_history=std::max(worst_history,error);
                check(error<.0002,"history retains smooth fractional rotation across sector wraps");
                old_reverse_frames+=std::abs(std::remainder(angle(om.data())-expected,40.))>1;
            }
        }
    }
    check(old_reverse_frames>20,"real history fixture exposes repeated legacy backwards sweeps");
    std::printf("PASS: 32000 fractional history samples, five sector wraps, unchanged ordinary path; max angular error %.9g deg; legacy bad frames %u\n",worst_history,old_reverse_frames);
}
