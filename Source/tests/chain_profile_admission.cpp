#include "chain_rotation_profiles.h"
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdio>
#include <cstdlib>
void check(bool b,const char* what){if(!b){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
int main(int argc,char** argv){
    const uint64_t ids[]={0x5d85a105c9d1820full,0x74b7033ab5e79527ull,0xb423bd2d0d6de2e9ull,0xdde09f70b0bd2816ull};
    for(auto id:ids){char path[512];std::snprintf(path,sizeof path,"%s/chain-geometry-%llx.bin",argc>1?argv[1]:"Build/logs",(unsigned long long)id);
        std::ifstream file(path,std::ios::binary);check(bool(file),"captured immutable mesh fixture exists");
        std::vector<uint8_t> first((std::istreambuf_iterator<char>(file)),{}),second=first,wrong=first;wrong.back()^=1;
        for(unsigned pass=0;pass<64;++pass){const auto& data=pass&1?first:second;
            check(catherine::visual::chain_rotation_sectors(id,data.data(),data.size(),36)==9,"distinct immutable instances retain profile admission");
        }
        check(catherine::visual::chain_rotation_sectors(id,wrong.data(),wrong.size(),36)==1,"reject changed mesh with matching first 64 bytes");
        check(catherine::visual::chain_rotation_sectors(id,first.data(),first.size()-1,36)==1,"reject changed bound size");
        check(catherine::visual::chain_rotation_sectors(id,first.data(),first.size(),32)==1,"reject changed stride");
        check(catherine::visual::chain_rotation_sectors(17,first.data(),first.size(),36)==1,"reject unrelated geometry");
    }
    puts("PASS all four chain assets, alternating source buffers, and fingerprint/size/stride exclusions");
}
