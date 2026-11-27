#define _CRT_SECURE_NO_WARNINGS
#include "root9_force_snapshot.h"
#include <array>
#include <atomic>
#include <thread>
#include <latch>
#include <mutex>
#include <random>
#include <unordered_set>
#include <cstdio>
#include <cstdlib>
#include <immintrin.h>
using namespace experimental_root9;
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d %s\n",__LINE__,#x);std::abort();}}while(0)
int main(){
 std::setvbuf(stdout,nullptr,_IONBF,0);
 const auto csr=_mm_getcsr();unsigned cases=0;
 ForceSnapshot m;ForceObservation o;unsigned reads=0;
 CHECK(!m.observe(o,[&]{++reads;return ForceCommand{1,true};}));CHECK(!reads);
 ForceTuple initial{0xa,true,true};m.commit(m.begin_change(initial),initial);
 bool changed=false;uint64_t guest=0xa;
 CHECK(m.observe(o,[&]{const auto old=guest;if(!changed){changed=true;guest=0xb;ForceTuple next{0xa,true,false};auto token=m.begin_change(next);CHECK(token);m.commit(token,next);}return ForceCommand{old,true};}));
 CHECK(o.guest.command==0xb&&o.tuple.context==0xa&&!o.tuple.last_owned&&force_result(o,false));++cases;
 ForceTuple next{0xc,true,true};auto token=m.begin_change(next);CHECK(token);reads=0;
 CHECK(!m.observe(o,[&]{++reads;return ForceCommand{0xc,true};}));CHECK(!reads);m.commit(token,next);
 CHECK(m.observe(o,[]{return ForceCommand{0xc,true};})&&force_result(o,false));++cases;
 const auto prior=o.sequence;CHECK(!m.begin_change(next));m.commit(0,next);CHECK(m.observe(o,[]{return ForceCommand{0xc,true};})&&o.sequence==prior);++cases;
 ForceSnapshot limit(UINT64_MAX-3);ForceTuple a{1,true,false};auto t=limit.begin_change(a);CHECK(t==UINT64_MAX-2);limit.commit(t,a);CHECK(limit.observe(o,[]{return ForceCommand{1,true};}));
 a.context=2;CHECK(!limit.begin_change(a));limit.commit(0,a);CHECK(!limit.observe(o,[]{return ForceCommand{2,true};}));a.context=3;CHECK(!limit.begin_change(a));CHECK(!limit.observe(o,[]{return ForceCommand{3,true};}));++cases;
 CHECK(_mm_getcsr()==csr);
 // Exercise the real set/update invariant and nextEver prediction independently.
 std::mt19937_64 rng(0x271127);ForceSnapshot serial;ForceTuple actual;std::unordered_set<uint64_t> owned;
 for(unsigned n=0;n<200000;++n){
  const bool active=(rng()%7)!=0,own=active&&(rng()%5==0);const uint64_t command=1+rng()%32;
  bool next_ever=actual.ever_owned||own;
  if(!active&&!own&&(owned.empty()||(owned.size()==1&&owned.contains(command))))next_ever=false;
  ForceTuple predicted{command,next_ever,own};_mm_setcsr(csr);auto publication=serial.begin_change(predicted);CHECK(_mm_getcsr()==csr);
  if(own)owned.insert(command);else owned.erase(command);
  if(own)actual.ever_owned=true;actual.context=command;actual.last_owned=own;
  if(!active&&owned.empty())actual.ever_owned=false;
  CHECK(predicted==actual);_mm_setcsr(csr);serial.commit(publication,predicted);
  ForceCommand input{1+rng()%32,(rng()%9)!=0};const bool local=rng()&1;
  const bool accepted=serial.observe(o,[&]{return input;});CHECK(accepted==actual.ever_owned);
  if(accepted){CHECK(o.tuple==actual);const bool original=!input.known||local||owned.contains(input.command)||(actual.ever_owned&&input.command!=actual.context);CHECK(force_result(o,local)==original);}
  CHECK(_mm_getcsr()==csr);++cases;
 }
 // Readers must never accept a tuple/guest pair spanning an in-flight update.
 ForceSnapshot concurrent;std::atomic<uint64_t> command{0};std::atomic<bool> known{false},done{false};std::atomic<uint64_t> accepted{0},fallbacks{0};
 std::mutex writer_lock;ForceTuple locked_tuple;
 std::array<std::thread,4> readers;std::latch ready(readers.size());
 for(auto&thread:readers)thread=std::thread([&]{
   // Prove fallback coverage before publishing: CI may otherwise schedule the
   // complete writer loop before any reader runs. The final stable publication
   // guarantees accepted coverage in each reader's post-writer iterations.
   ForceObservation empty;
   CHECK(!concurrent.observe(empty,[&]{return ForceCommand{command.load(),known.load()};}));
   {std::lock_guard lock(writer_lock);CHECK(!locked_tuple.context);fallbacks.fetch_add(1);}
   ready.count_down();ready.wait();
   unsigned after=0;while(!done.load()||after++<1000){
   ForceObservation value;
   if(concurrent.observe(value,[&]{return ForceCommand{command.load(),known.load()};})){
    const uint64_t serial_number=value.tuple.context>>2;
    CHECK(value.tuple.ever_owned&&value.tuple.last_owned==bool(serial_number&1));
    CHECK(value.guest.command==value.tuple.context+(serial_number%3==0));CHECK(value.guest.known==(serial_number%11!=0));accepted.fetch_add(1);
   }else{
    // The production fallback retains its original mutex and ordered guest reads.
    std::lock_guard lock(writer_lock);const auto input=ForceCommand{command.load(),known.load()};
    if(locked_tuple.context){const auto n=locked_tuple.context>>2;CHECK(input.command==locked_tuple.context+(n%3==0));CHECK(input.known==(n%11!=0));}
    fallbacks.fetch_add(1);
   }
 }});
 ready.wait();
 for(uint64_t n=1;n<=50000;++n){
  std::lock_guard lock(writer_lock);ForceTuple value{n<<2,n%17!=0,bool(n&1)};const auto publication=concurrent.begin_change(value);
  locked_tuple=value;command.store(value.context+(n%3==0));known.store(n%11!=0);concurrent.commit(publication,value);
 }
 done.store(true);for(auto&thread:readers)thread.join();CHECK(accepted.load()&&fallbacks.load());_mm_setcsr(csr);
 std::printf("PASS %u serial cases, stale-command counterexample, in-flight writer/unchanged/overflow, 50000 publications +4 readers (%llu accepted/%llu locked fallbacks), MXCSR\n",cases,(unsigned long long)accepted.load(),(unsigned long long)fallbacks.load());
}
