// Native background provenance must survive rotating upload slots and culling.
#include "root9_rigid_transport.h"
#include "rigid_late_sample_interpolation.h"
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
using namespace catherine::root9;
void check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    RigidTransport transport;RigidTransport::Thread thread;
    std::unordered_map<uint64_t,std::array<uint32_t,16>> memory;
    auto read=[&](uint64_t a,void* d,size_t n){auto i=memory.find(a);if(i==memory.end()||n!=64)return false;std::memcpy(d,i->second.data(),64);return true;};
    uint64_t generation=1;
    auto live=[&](const RigidPacket& p){return p.background.identity==generation;};
    BackgroundOwner owner{1,100,200,300,400,500,600,0,1};
    auto& source=memory[400];for(unsigned k=0;k<16;++k)source[k]=k;
    check(transport.copied_background(owner,400,read),"admit native mesh copy");
    transport.loaded(thread,400,read,live);
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)memory[700][4*r+c]=source[4*c+r];
    transport.uploaded(thread,700,800,64,read,live);transport.buffer_address(thread,800,1000,read,live);
    RigidPacket packet;check(transport.snapshot(thread,1064,64,packet,read,live),"carry through transpose/upload");
    check(packet.background.identity==1&&packet.background.mesh==100,"preserve native instance identity");
    generation=2;check(!transport.snapshot(thread,1064,64,packet,read,live),"reject retired native identity");
    generation=1;memory[700][0]^=1;check(!transport.snapshot(thread,1064,64,packet,read,live),"reject overwritten bytes");
    transport.clear();check(!transport.snapshot(thread,1064,64,packet,read,live),"clear invalidates old address");
    // Keep many owned uploads pending together. Revoke across both CPU and
    // backing-offset domains, including an unowned write with unchanged bytes.
    memory.clear();transport.clear();thread={};generation=1;
    for(unsigned i=0;i<400;++i){uint64_t src=0x10000+i*64,dst=0x20000+i*64;
        auto& words=memory[src];for(unsigned k=0;k<16;++k)words[k]=i*16+k;
        check(transport.copied_background(owner,src,read),"dense native copy");transport.loaded(thread,src,read,live);
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)memory[dst][4*r+c]=words[4*c+r];
        transport.uploaded(thread,dst,0x30000+i%3,i*128,read,live);
    }
    transport.loaded(thread,0,read,live);
    transport.uploaded(thread,0x20000+5*64+32,0x90000,0,read,live);
    transport.loaded(thread,0,read,live);
    transport.uploaded(thread,0xa0000,0x30001,7*128+32,read,live);
    for(unsigned b=0;b<3;++b){uint64_t base=0x100000+b*0x100000;
        transport.buffer_address(thread,0x30000+b,base,read,live);
        for(unsigned i=b;i<400;i+=3){bool admitted=transport.snapshot(thread,base+i*128,64,packet,read,live);
            check(admitted==(i!=5&&i!=6&&i!=7),"dense pending overlaps authenticate exact domain");
        }
    }
    catherine::visual::RigidLateSampleInterpolator models;
    float matrices[5][16]{};
    for(unsigned i=0;i<5;++i){float a=i*.78539816339f;auto* m=matrices[i];m[0]=std::cos(a);m[2]=-std::sin(a);m[5]=1;m[8]=std::sin(a);m[10]=std::cos(a);m[3]=950;m[7]=-5306;m[11]=1150;m[15]=1;}
    for(unsigned frame=1;frame<=240;++frame){float phase=(frame%8)/8.f;uint32_t bits;std::memcpy(&bits,&phase,4);uint64_t clock=(uint64_t(frame/8)<<32)|bits|0x80000000ull;
        for(unsigned j=0;j<5;++j){unsigned i=(frame+j*3)%5;if((i+frame/11)%3==0)continue;
            const auto* out=models.render(i+1,reinterpret_cast<const uint8_t*>(matrices[i]),64,frame,clock,true);
            check(!std::memcmp(out,matrices[i],64),"coincident statue stays exact through camera culling");
        }
    }
    catherine::visual::RigidLateSampleInterpolator animated;
    auto* m=matrices[0];m[3]=0;
    float phase=.5f;uint32_t bits;std::memcpy(&bits,&phase,4);
    animated.render(10,reinterpret_cast<const uint8_t*>(m),64,1,(1ull<<32)|bits|0x80000000ull,true);
    m[3]=10;const auto* out=animated.render(10,reinterpret_cast<const uint8_t*>(m),64,2,(2ull<<32)|bits|0x80000000ull,true);
    float result[16];std::memcpy(result,out,64);check(std::fabs(result[3]-5)<1e-5f,"native identity retains fractional interpolation");
    puts("PASS background ownership, retirement, overwrite, culling, static copies and fractional motion");
}
