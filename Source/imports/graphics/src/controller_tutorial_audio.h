#pragma once
#include "controller_tutorial_text.h"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>

namespace catherine::tutorial {
struct SpokenControl {
    unsigned action;
    const char* name;
    unsigned begin_ms, end_ms;
};

struct Recording {
    Topic topic;
    uint64_t header_hash;
    SpokenControl words[2];
};

// English 48 kHz HCA headers, fingerprinted over all 96 bytes. Times bound
// the spoken name, including its consonants, in the decoded mono waveform.
inline constexpr Recording recordings[] = {
    {Topic::Grab,     0x070a50105abfdf18ull, {{0,"A",1710,1925},{}}},
    {Topic::Undo,     0x132bf3548a893220ull, {{6,"L",2775,3015},{}}},
    {Topic::Autoplay, 0x5517b36483e04fcfull, {{7,"R",2680,2950},{}}},
    {Topic::Phone,    0x6c480a433c7e7035ull, {{2,"X",2260,2507},{}}},
    {Topic::Drink,    0x669702de78b42ca3ull, {{3,"Y",3245,3559},{1,"B",5835,6114}}},
    {Topic::Menu,     0xa8cc84e4c4ca4942ull, {{10,"+",1535,1810},{}}},
    {Topic::Rapunzel, 0x75870fc43157dd55ull, {{0,"Cross",370,860},{}}}
};

inline unsigned mismatched_words(const Recording& recording,const Input& input) {
    unsigned mask=0;
    for (unsigned i=0;i<2;++i) {
        const auto& word=recording.words[i];
        if (word.name && (!input.layout || std::strcmp(pad_control_name(input,word.action),word.name)))
            mask |= 1u << i;
    }
    return mask;
}

inline constexpr unsigned sample_boundary(unsigned ms) {
    // The raw HCA inspection WAVs include the 128-sample encoder delay.
    // CRI discards it before writing into the playback planes.
    return ((ms*48+64)/128)*128-128;
}

class TutorialAudio {
    struct Decoder {
        uintptr_t handle=0;
        const Recording* recording=nullptr;
        unsigned mask=0, position=0;
        uint64_t generation=0;
    };
    std::array<unsigned,std::size(recordings)> snapshots{};
    std::array<Decoder,64> decoders{};
    std::mutex lock;
    std::atomic<unsigned> active{0};
    uint64_t generation=0;

    void release(Decoder& decoder) {
        decoder={};
        active.fetch_sub(1,std::memory_order_release);
    }

    static void prefix(unsigned char* dest,size_t count,const void* a,size_t na,const void* b) {
        size_t first=std::min(count,na);
        if (first) std::memcpy(dest,a,first);
        if (first<count) std::memcpy(dest+first,b,count-first);
    }

public:
    void snapshot(Topic topic,const Input& input) {
        std::lock_guard guard(lock);
        for (size_t i=0;i<std::size(recordings);++i)
            if (recordings[i].topic==topic) snapshots[i]=mismatched_words(recordings[i],input);
    }

    void header(uintptr_t handle,const void* a,unsigned na,const void* b,unsigned nb) {
        if (!handle || (na&&!a) || (nb&&!b) || na>INT32_MAX || nb>INT32_MAX) return;
        const size_t total=size_t(na)+nb;
        if (total<4) return;
        uint32_t magic;
        if (na>=4) std::memcpy(&magic,a,4);
        else prefix(reinterpret_cast<unsigned char*>(&magic),4,a,na,b);
        if ((magic & 0x7f7f7f7fu)!=0x00414348u) return;
        uint64_t hash=0;
        if (total>=96 && magic==0x00414348u) {
            unsigned char bytes[96];
            prefix(bytes,sizeof bytes,a,na,b);
            hash=0xcbf29ce484222325ull;
            for (unsigned char byte:bytes) hash=(hash^byte)*0x100000001b3ull;
        }
        std::lock_guard guard(lock);
        Decoder* slot=nullptr;
        for (auto& decoder:decoders) {
            if (decoder.handle==handle) { release(decoder); slot=&decoder; break; }
            if (!slot && !decoder.handle) slot=&decoder;
        }
        for (size_t i=0;i<std::size(recordings);++i) {
            if (recordings[i].header_hash!=hash || !snapshots[i]) continue;
            if (!slot) {
                slot=&*std::min_element(decoders.begin(),decoders.end(),[](const Decoder& x,const Decoder& y){
                    return x.generation<y.generation;
                });
                release(*slot);
            }
            *slot={handle,&recordings[i],snapshots[i],0,++generation};
            active.fetch_add(1,std::memory_order_release);
            break;
        }
    }

    unsigned pcm(uintptr_t handle,const void* planes,unsigned frames) {
        if (!frames || frames>128 || !planes || !active.load(std::memory_order_acquire)) return frames;
        std::lock_guard guard(lock);
        for (auto& decoder:decoders) {
            if (decoder.handle!=handle) continue;
            float* samples=nullptr;
            std::memcpy(&samples,planes,sizeof samples);
            if (!samples) return frames;
            constexpr unsigned fade=128, gap=2816; // 2.67 ms edge ramps, about 60 ms silence.
            const unsigned first=decoder.position;
            decoder.position+=frames;
            unsigned written=frames, last=0;
            for (unsigned i=0;i<2;++i) {
                if (!(decoder.mask&(1u<<i))) continue;
                const auto& word=decoder.recording->words[i];
                const unsigned begin=sample_boundary(word.begin_ms), end=sample_boundary(word.end_ms);
                last=end+fade;
                if (first+frames<=begin-fade || first>=last) continue;
                written=0;
                for (unsigned sample=0;sample<frames;++sample) {
                    unsigned at=first+sample;
                    if (at>=begin+gap && at<end) continue;
                    float value=samples[sample];
                    if (at>=begin && at<end) value=0;
                    else if (at<begin && at>=begin-fade) value*=float(begin-at-1)/float(fade-1);
                    else if (at>=end && at<end+fade) value*=float(at-end)/float(fade-1);
                    samples[written++]=value;
                }
            }
            if (decoder.position>=last) release(decoder);
            return written;
        }
        return frames;
    }
};
}
