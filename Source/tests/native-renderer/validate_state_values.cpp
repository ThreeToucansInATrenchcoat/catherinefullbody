#include "native_state_value_memo.h"
#include <random>
#include <cstdio>
#include <cstdlib>
#include <immintrin.h>
using Memo=experimental_record::FixedValueMemo;using Input=std::array<uint32_t,12>;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line%d at operation%u: %s\n",__LINE__,step,#x);std::abort();}}while(0)
struct Model {std::array<Input,8> blend{};std::array<Input,9> fixed{};std::array<bool,8> blend_valid{};std::array<bool,9> fixed_valid{};uint8_t refs[2]{};bool ref_valid[2]{};
 void apply(unsigned kind,const Input& in){if(kind==0){if(in[0]<8){blend[in[0]]=in;blend_valid[in[0]]=true;}}else if(kind==7){for(unsigned f=0;f<2;++f)if(in[0]>>f&1){refs[f]=uint8_t(in[1]);ref_valid[f]=true;}}else {fixed[kind]=in;fixed_valid[kind]=true;}}
 bool matches(unsigned kind,const Input& in)const{if(kind==0)return in[0]<8&&blend_valid[in[0]]&&blend[in[0]]==in;if(kind==7){for(unsigned f=0;f<2;++f)if((in[0]>>f&1)&&(!ref_valid[f]||refs[f]!=uint8_t(in[1])))return false;return true;}return fixed_valid[kind]&&fixed[kind]==in;}
};
int main(){const auto saved=_mm_getcsr();std::mt19937 rng(0x271128);Memo memo;Model actual;uint64_t hits=0,learned=0,stale=0;unsigned step=0;
 {
  Memo flags;Input input{};const uint32_t learned=0x1fa0u;CHECK(flags.learn(4,flags.stamp(4),input.data(),learned));
  CHECK(flags.matches(4,input.data(),learned));CHECK(!flags.matches(4,input.data(),0x1f80u));
  CHECK(!flags.matches(4,input.data(),learned|0x2000u));CHECK(!flags.matches(4,input.data(),learned&~0x80u));
  CHECK(flags.matches(4,input.data(),learned|4u));
 }
 std::array<std::array<Input,8>,9> catalog;for(unsigned k=0;k<9;++k)for(unsigned n=0;n<8;++n){for(auto& v:catalog[k][n])v=rng();catalog[k][n][0]=k==0?n:k==7?1+n%3:catalog[k][n][0];}
 auto invalidate=[&](unsigned kind,const Input& in){if(kind==0)memo.blend_changed(in[0]<8?int(in[0]):-1);else if(kind==7)memo.ref_changed(in[0]);else memo.invalidate(int(kind));};
 for(step=0;step<1000000;++step){
  const unsigned kind=rng()%9;const auto in=catalog[kind][rng()%8];const int key=Memo::key(kind,in[0]);
  if(step%101==0){memo.reset();actual=Model{};}
  if(step%7==0){invalidate(kind,in);actual.apply(kind,in);continue;}
  if(memo.matches(key,in.data())){CHECK(actual.matches(kind,in));++hits;continue;}
  invalidate(kind,in);actual.apply(kind,in);const uint64_t stamp=memo.stamp(key);
  if(step%13==0){const unsigned other=rng()%9;const auto changed=catalog[other][rng()%8];invalidate(other,changed);actual.apply(other,changed);}
  const bool accepted=memo.learn(key,stamp,in.data());learned+=accepted;stale+=!accepted;
  if(accepted){CHECK(memo.matches(key,in.data()));CHECK(actual.matches(kind,in));}
 }
 CHECK(hits>10000&&stale>1000);CHECK(_mm_getcsr()==saved);
 std::printf("PASS 1,000,000 native state-value operations, %llu proved omissions, %llu learned states, %llu rejected stale publications, blend targets and stencil-face overlap\n",(unsigned long long)hits,(unsigned long long)learned,(unsigned long long)stale);
}
