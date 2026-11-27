#include "native_record_scope.h"
#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <immintrin.h>
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);std::fflush(stderr);std::abort();}}while(0)
struct Lock {
 std::mutex m;std::atomic<unsigned> held{0};uint64_t acquisitions=0;
 void lock(){m.lock();CHECK(held.exchange(1)==0);++acquisitions;}
 void unlock(){CHECK(held.exchange(0)==1);m.unlock();}
};
struct State {Lock lock;std::vector<uint64_t> records;~State(){CHECK(lock.held==0);}};
using Scope=experimental_record::Scope<State>;
int main(){
 const auto saved=_mm_getcsr();
 auto first=std::make_shared<State>();std::weak_ptr<State> retired=first;
 uint64_t generation=1;unsigned resolves=0;
 auto resolver=[&](uint64_t,uint64_t){++resolves;return first;};
 {
  Scope s;CHECK(!s.active());auto a=s.begin();CHECK(s.active());
  auto* p=s.acquire(11,generation,0,resolver);CHECK(p&&p->lock.held==1);p->records.push_back(1);
  CHECK(s.acquire(11,generation,0,resolver)==p);p->records.push_back(2);CHECK(resolves==1&&s.stats.locks==1);
  s.end(a);CHECK(!s.active()&&p->lock.held==0);
  auto b=s.begin();CHECK(s.acquire(11,generation,0,resolver)==p&&resolves==1);s.end(b);
  auto outer=s.begin(),inner=s.begin();CHECK(s.acquire(11,generation,0,resolver)==p);s.abandon();CHECK(!s.active()&&p->lock.held==0);
  auto fresh=s.begin();CHECK(s.acquire(11,generation,0,resolver)==p);s.end(inner);s.end(outer);CHECK(s.active()&&p->lock.held==1);s.end(fresh);CHECK(!s.active()&&p->lock.held==0);
  auto c=s.begin();CHECK(s.acquire(11,generation,0,resolver)==p);
  first=std::make_shared<State>();++generation;CHECK(!retired.expired());
  auto* q=s.acquire(11,generation,0,resolver);CHECK(q&&q!=p&&retired.expired());q->records.push_back(3);s.end(c);
  auto missing=s.begin();auto none=[](uint64_t,uint64_t){return std::shared_ptr<State>{};};CHECK(!s.acquire(0,++generation,0,none));s.end(missing);
  std::vector<uint64_t> tokens;for(unsigned i=0;i<33;++i)tokens.push_back(s.begin());CHECK(!s.active());for(auto t:tokens)s.end(t);CHECK(!s.active());
  auto d=s.begin();CHECK(d);CHECK(s.acquire(11,++generation,0,resolver));s.end(d);
 }
 // Contending producers and independent registry replacement. Every cached state
 // is strongly pinned while its lock can be retained across record boundaries.
 std::shared_mutex registry;std::shared_ptr<State> current=std::make_shared<State>();std::atomic<uint64_t> gen{1};
 std::atomic<uint64_t> delivered{0},completed{0};std::atomic<bool> start{false};std::vector<std::thread> threads;
 constexpr unsigned workers=4,groups=12000,width=6;
 for(unsigned w=0;w<workers;++w)threads.emplace_back([&,w]{
  Scope s;auto resolve=[&](uint64_t,uint64_t){std::shared_lock lk(registry);return current;};
  while(!start.load(std::memory_order_acquire))std::this_thread::yield();
  for(unsigned n=0;n<groups;++n){const auto token=s.begin();for(unsigned k=0;k<width;++k){
    auto* p=s.acquire(7,gen.load(std::memory_order_acquire),0,resolve);CHECK(p&&p->lock.held==1);
    p->records.push_back((uint64_t(w)<<48)|(uint64_t(n)<<8)|k);++delivered;
    if((n+k)%701==0){s.abandon();break;}
   }s.end(token);++completed;}
 });
 std::thread replace([&]{while(!start.load(std::memory_order_acquire))std::this_thread::yield();for(unsigned n=0;n<3000;++n){
   auto next=std::make_shared<State>();{std::unique_lock lk(registry);current=std::move(next);gen.fetch_add(1,std::memory_order_release);}if(n%16==0)std::this_thread::yield();
 }});
 start.store(true,std::memory_order_release);for(auto& t:threads)t.join();replace.join();CHECK(completed==workers*groups);CHECK(delivered>workers*groups);
 current.reset();first.reset();CHECK(_mm_getcsr()==saved);
 std::printf("PASS record scope reuse, cancellation, nesting, missing/replaced states, %llu concurrent groups and %llu immediate records\n",(unsigned long long)completed.load(),(unsigned long long)delivered.load());
}
