#include "controller_tutorial_audio.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

using namespace catherine::tutorial;
namespace fs=std::filesystem;

struct Fixture { const char* bank; uint64_t offset; unsigned message; };
constexpr Fixture fixtures[]={
    {"pzl",134215232,10056},{"pzl",134507264,10060},{"pzl",134874336,10065},
    {"lounge",266649216,18558},{"lounge",266418272,18554},{"lounge",266524096,18555},
    {"pzl",53067296,40560}
};

static std::vector<float> render(TutorialAudio& audio,const std::vector<float>& source,uintptr_t handle) {
    std::vector<float> output;
    float block[128];
    float* plane=block;
    for (size_t offset=0;offset<source.size();offset+=128) {
        unsigned n=unsigned(std::min<size_t>(128,source.size()-offset));
        std::copy_n(source.data()+offset,n,block);
        auto kept=audio.pcm(handle,&plane,n);
        assert(kept<=n);
        output.insert(output.end(),block,block+kept);
    }
    return output;
}

static void bind(Input& input,unsigned action,unsigned physical) {
    input.mapping=(input.mapping&~(uint64_t(15)<<(4*action)))|(uint64_t(physical)<<(4*action));
}

int main(int argc,char** argv) {
    assert(argc==2 || argc==3);
    Input keyboard,switchpad,xbox,ps;
    switchpad.layout=3; xbox.layout=1; ps.layout=2;
    // A stock Switch recording can also name a matching Xbox button.
    assert(mismatched_words(recordings[0],xbox)==0);
    assert(mismatched_words(recordings[0],switchpad)==1);
    assert(mismatched_words(recordings[6],ps)==0);
    assert(mismatched_words(recordings[6],xbox)==1);
    for (unsigned mask=0;mask<4;++mask) {
        auto input=switchpad;
        bind(input,3,(mask&1)?0:3);
        bind(input,1,(mask&2)?0:1);
        assert(mismatched_words(recordings[4],input)==mask);
    }
    for (size_t i=0;i<std::size(recordings);++i) {
        const auto& recording=recordings[i];
        const auto& fixture=fixtures[i];
        assert(voice_for(recording.topic,0)==fixture.message);
        std::array<unsigned char,96> header;
        std::ifstream bank(fs::path(argv[1])/(std::string(fixture.bank)+"_voice.awb"),std::ios::binary);
        assert(bank.seekg(fixture.offset).read(reinterpret_cast<char*>(header.data()),header.size()));
        TutorialAudio audio;
        std::vector<float> source(7*48000);
        for (size_t n=0;n<source.size();++n) source[n]=float((n%1009)+1)/1010;
        const uintptr_t handle=7;
        audio.snapshot(recording.topic,keyboard);
        // A nonmatching 96-byte header, even with valid magic, clears a reused decoder.
        audio.header(handle,header.data(),96,nullptr,0);
        auto other=header;
        other[90]^=1;
        audio.header(handle,other.data(),96,nullptr,0);
        assert(render(audio,source,handle)==source);

        auto all=mismatched_words(recording,keyboard);
        assert(all==(recording.topic==Topic::Drink?3u:1u));
        for (unsigned mask=0;mask<=all;++mask) {
            auto selection=switchpad;
            if(recording.topic==Topic::Rapunzel) selection=ps;
            for (unsigned w=0;w<2 && recording.words[w].name;++w) {
                const auto& word=recording.words[w];
                for (unsigned slot=0;slot<16;++slot) {
                    bind(selection,word.action,slot);
                    if (bool(std::strcmp(word.name,pad_control_name(selection,word.action)))==bool(mask&(1u<<w))) break;
                }
            }
            assert(mismatched_words(recording,selection)==mask);
            audio.snapshot(recording.topic,selection);
            audio.header(handle,header.data(),2,header.data()+2,94);
            audio.snapshot(recording.topic,keyboard);
            assert(render(audio,source,handle+1)==source);
            auto result=render(audio,source,handle);
            size_t written=0;
            for (unsigned n=0;n<source.size();++n) {
                float expected=source[n];
                bool drop=false;
                for (unsigned w=0;w<2;++w) if (mask&(1u<<w)) {
                    unsigned begin=sample_boundary(recording.words[w].begin_ms),end=sample_boundary(recording.words[w].end_ms);
                    if (n>=begin+2816 && n<end) drop=true;
                    else if (n>=begin && n<end) expected=0;
                    else if (n>=begin-128 && n<begin) expected*=float(begin-n-1)/127;
                    else if (n>=end && n<end+128) expected*=float(n-end)/127;
                }
                if (!drop) {assert(written<result.size() && result[written]==expected); ++written;}
            }
            assert(written==result.size());
        }
        if (argc==3) {
            auto stem=fs::path(argv[2])/std::to_string(fixture.message);
            std::ifstream raw(stem.string()+".f32",std::ios::binary|std::ios::ate);
            assert(raw && raw.tellg()>0 && size_t(raw.tellg())%4==0);
            source.resize(size_t(raw.tellg())/4);
            assert(raw.seekg(0).read(reinterpret_cast<char*>(source.data()),source.size()*4));
            audio.snapshot(recording.topic,keyboard);
            audio.header(handle,nullptr,0,header.data(),96);
            auto edited=render(audio,source,handle);
            std::ofstream out(stem.string()+"-edited.f32",std::ios::binary);
            assert(out.write(reinterpret_cast<const char*>(edited.data()),edited.size()*4));
            std::cout<<fixture.message<<" "<<source.size()<<" -> "<<edited.size()<<" samples\n";
        }
    }
    std::cout<<"Seven original HCA headers, all cut masks, untouched channels and control snapshots passed.\n";
}
