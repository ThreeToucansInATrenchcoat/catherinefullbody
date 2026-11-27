#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include <cstring>
#include "C:/catherine/native-gpu-experiment/Source/native_byte_equal.h"
namespace experimental_record {
inline constexpr uint64_t layout_oracle_marker=0x4e41544c41594f32ull;
// Each interned blob index is immutable for this unpublished recording. The
// replay reads n/blob and, for native trailers, a[5]/a[6] on these two setters.
template<class Command> class LayoutMemo {
    struct Entry {uint32_t n=0,blob=0;uint64_t prefix=0,tag=0;bool valid=false;} entries[2];
public:
    size_t logical_commands=0;
    void reset(){logical_commands=0;invalidate();}
    void invalidate(){entries[0].valid=entries[1].valid=false;}
    void count_record(){++logical_commands;}
    bool observe(const Command& c,bool complete_prefix){
        switch(c.op){
          case Command::CallList:case Command::BindDeferred:case Command::ClearColor:case Command::ClearDepthStencil:
          case Command::ClearTexture:case Command::CopyTextureToTexture:case Command::CopyBufferToTexture:case Command::SetRenderTargets:
            invalidate();return false;
          case Command::BindVertexAttribs:case Command::BindVertexStreams:break;
          default:return false;
        }
        auto& previous=entries[c.op==Command::BindVertexStreams?1:0];
        if(!complete_prefix||!c.n||c.n>32||c.a[7]){previous.valid=false;return false;}
        const bool same=previous.valid&&previous.n==c.n&&previous.blob==c.blob&&previous.prefix==c.a[5]&&previous.tag==c.a[6];
        if(!same)previous={c.n,c.blob,c.a[5],c.a[6],true};
        return same;
    }
};
// Before lowering, these two setters are solely a copied integer array when
// producer_pipeline mode is off. Same n/size/bytes therefore means same effect.
template<class Command>class RawLayoutMemo {
    struct Entry {std::array<uint8_t,384> bytes{};uint32_t n=0;size_t size=0;bool valid=false;} entries[2];
public:
    void invalidate(){entries[0].valid=entries[1].valid=false;}
    bool observe(const Command& c,const void* blob,size_t bytes,size_t stride,bool enabled){
        switch(c.op){
          case Command::CallList:case Command::BindDeferred:case Command::ClearColor:case Command::ClearDepthStencil:
          case Command::ClearTexture:case Command::CopyTextureToTexture:case Command::CopyBufferToTexture:case Command::SetRenderTargets:invalidate();return false;
          case Command::BindVertexAttribs:case Command::BindVertexStreams:break;
          default:return false;
        }
        auto& old=entries[c.op==Command::BindVertexStreams?1:0];
        if(!enabled||!blob||!c.n||c.n>32||!stride||bytes!=size_t(c.n)*stride||bytes>old.bytes.size()||c.a[5]||c.a[6]||c.a[7]){old.valid=false;return false;}
        if(old.valid&&old.n==c.n&&old.size==bytes&&experimental_bytes::native(old.bytes.data(),blob,bytes))return true;
        std::memcpy(old.bytes.data(),blob,bytes);old.n=c.n;old.size=bytes;old.valid=true;return false;
    }
};
}
