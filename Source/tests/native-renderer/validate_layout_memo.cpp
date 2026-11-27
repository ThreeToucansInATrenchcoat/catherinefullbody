#include "native_layout_memo.h"
#include "C:/catherine/recomp/Source/imports/graphics/src/nvn/nvn.h"
#include <random>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <immintrin.h>
using namespace nx::nvn;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);std::abort();}}while(0)
struct List{CmdList full,shorter;};
struct State{std::vector<VertexAttrib> attrs;std::vector<VertexStream> streams;uint64_t a5=0,a6=0,s5=0,s6=0;};
static void capture(const State&s,std::vector<uint8_t>& trace){
 auto append=[&](const void*p,size_t n){if(!n)return;const auto*b=static_cast<const uint8_t*>(p);trace.insert(trace.end(),b,b+n);};
 const uint64_t n=s.attrs.size(),m=s.streams.size();append(&n,8);append(s.attrs.data(),n*sizeof(VertexAttrib));append(&m,8);append(s.streams.data(),m*sizeof(VertexStream));append(&s.a5,8);append(&s.a6,8);append(&s.s5,8);append(&s.s6,8);
}
static void replay(const std::vector<List>& lists,unsigned index,bool compact,State& state,std::vector<uint8_t>& trace){
 const auto& l=compact?lists[index].shorter:lists[index].full;
 for(const auto& c:l.cmds){
  const auto* p=c.blob<l.blobs.size()?l.blobs[c.blob].data():nullptr;const size_t size=c.blob<l.blobs.size()?l.blobs[c.blob].size():0;
  if(c.op==Cmd::BindVertexAttribs&&p&&size>=c.n*sizeof(VertexAttrib)){state.attrs.assign(reinterpret_cast<const VertexAttrib*>(p),reinterpret_cast<const VertexAttrib*>(p)+c.n);state.a5=c.a[5];state.a6=c.a[6];}
  else if(c.op==Cmd::BindVertexStreams&&p&&size>=c.n*sizeof(VertexStream)){state.streams.assign(reinterpret_cast<const VertexStream*>(p),reinterpret_cast<const VertexStream*>(p)+c.n);state.s5=c.a[5];state.s6=c.a[6];}
  else if(c.op==Cmd::CallList){CHECK(c.a[0]<index);replay(lists,unsigned(c.a[0]),compact,state,trace);}
  else if(c.op==Cmd::DrawArrays||c.op==Cmd::DrawElements||c.op==Cmd::ClearColor||c.op==Cmd::ClearDepthStencil)capture(state,trace);
 }
}
int main(){
 const unsigned saved=_mm_getcsr();std::mt19937 rng(0x271127);std::vector<List> lists(80);uint64_t logical=0,removed=0,observations=0;
 for(unsigned list=0;list<lists.size();++list){
  auto& a=lists[list].full;auto& b=lists[list].shorter;
  for(unsigned k=0;k<24;++k){std::vector<uint8_t> bytes((1+k%64)*(k%2?sizeof(VertexAttrib):sizeof(VertexStream)));for(auto& x:bytes)x=uint8_t(rng());a.blobs.push_back(bytes);b.blobs.push_back(bytes);}
  experimental_record::LayoutMemo<Cmd> memo;experimental_record::RawLayoutMemo<Cmd> raw;memo.reset();
  for(unsigned n=0;n<1600;++n){
   Cmd c{};const unsigned choice=rng()%20;
   if(choice<10){c.op=choice%2?Cmd::BindVertexAttribs:Cmd::BindVertexStreams;c.blob=rng()%24;c.n=1+(c.blob%64);if(n%53==0)c.n=0;if(n%97==0)c.n=64;if(n%79==0)c.a[7]=7;
    c.a[5]=n%107==0?8:0;c.a[6]=n%101==0?13:0;
   }else if(choice==10&&list>=4){c.op=Cmd::CallList;c.a[0]=rng()%4;}
   else if(choice==11)c.op=Cmd::BindDeferred;
   else if(choice==12)c.op=Cmd::ClearColor;
   else if(choice==13)c.op=Cmd::ClearDepthStencil;
   else if(choice==14)c.op=Cmd::SetRenderTargets;
   else if(choice==15)c.op=Cmd::CopyTextureToTexture;
   else c.op=Cmd::DrawElements;
   // Consecutive and separated exact repeats are the common workload.
   if(n%3&&n&&choice<10)c=a.cmds.back();
   const bool attr=c.op==Cmd::BindVertexAttribs,stream=c.op==Cmd::BindVertexStreams;
   const bool complete=(attr||stream)&&c.blob<a.blobs.size()&&a.blobs[c.blob].data()&&a.blobs[c.blob].size()>=c.n*(attr?sizeof(VertexAttrib):sizeof(VertexStream));
   const auto* source=c.blob<a.blobs.size()?a.blobs[c.blob].data():nullptr;const size_t source_size=c.blob<a.blobs.size()?a.blobs[c.blob].size():0;
   const bool raw_duplicate=raw.observe(c,source,source_size,attr?sizeof(VertexAttrib):stream?sizeof(VertexStream):0,true);
   a.cmds.push_back(c);memo.count_record();const bool duplicate=memo.observe(c,complete)||raw_duplicate;if(!duplicate)b.cmds.push_back(c);else ++removed;
   ++logical;CHECK(memo.logical_commands==a.cmds.size());
   // Guest memory accounting uses logical records, including every omitted setter.
   for(uint64_t size:{0ull,64ull,65ull,1024ull,1000000ull}){
    auto used=[&](size_t count,uint64_t per){return std::min(uint64_t(37)+count*per,size>64?size-64:0);};
    CHECK(used(a.cmds.size(),16)==used(memo.logical_commands,16));CHECK(used(a.cmds.size()+1,4)==used(memo.logical_commands+1,4));
   }
  }
  // Re-enter each immutable list with different caller-inherited layouts.
  for(unsigned repeat=0;repeat<2;++repeat){State x,y;x.attrs.resize(3+repeat);x.streams.resize(2+repeat);for(auto& v:x.attrs)v={rng(),rng(),rng()};for(auto& v:x.streams)v={rng(),rng()};y=x;
   std::vector<uint8_t> before,after;replay(lists,list,false,x,before);replay(lists,list,true,y,after);CHECK(before==after);before.clear();after.clear();capture(x,before);capture(y,after);CHECK(before==after);++observations;
  }
 }
 CHECK(removed>1000);CHECK(_mm_getcsr()==saved);
 std::printf("PASS %llu logical commands, %llu duplicate omissions, %llu inherited/nested replay comparisons and command-memory accounting\n",(unsigned long long)logical,(unsigned long long)removed,(unsigned long long)observations);
}
