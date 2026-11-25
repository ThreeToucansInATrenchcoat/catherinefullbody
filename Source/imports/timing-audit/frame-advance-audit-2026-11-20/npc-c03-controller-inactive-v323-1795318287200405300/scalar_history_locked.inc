struct Key{uint64_t actor,model,sdk,parent,generation;bool operator==(const Key&)const=default;};
struct Hash{size_t operator()(const Key&k)const{return size_t(k.actor^(k.model<<1)^(k.sdk<<2)^k.parent^k.generation);}};
struct State{uint64_t token;uint32_t tick;double before,now;bool ready;uint32_t last;};static std::unordered_map<Key,State,Hash> states;
static void forget_all_locked(){states.clear();}
static void forget_actor_locked(uint64_t a){for(auto i=states.begin();i!=states.end();)if(i->first.actor==a)i=states.erase(i);else ++i;}
static uint32_t sample_locked(uint64_t actor,uint64_t model,uint64_t sdk,uint64_t parent,uint64_t generation,uint64_t token,uint32_t rawbits,uint32_t nativebits,uint32_t factorbits,int native60,int config_high=1,uint32_t framework_bits=0x3c888889u){
Key key{actor,model,sdk,parent,generation}; for(auto i=states.begin();i!=states.end();)if(i->first.actor==actor && !(i->first==key))i=states.erase(i);else ++i;float raw,factor,fraction;memcpy(&raw,&rawbits,4);memcpy(&factor,&factorbits,4);uint32_t fb=uint32_t(token)&0x7fffffffu;memcpy(&fraction,&fb,4);uint32_t tick=uint32_t(token>>32);
// Sole packed token remains presentation time; the same-thread authored delta is only an exact-zero pause discriminator.
auto held=states.find(key);
if(std::isfinite(raw) && std::isfinite(factor) && factorbits==0x3c8efa35u && !(token&0x80000000ull) && config_high && !native60 && (framework_bits&0x7fffffffu)==0 && held!=states.end()){
 const auto& prior=held->second;float oldraw=float(prior.now);uint32_t oldbits;memcpy(&oldbits,&oldraw,4);
 if((prior.token&0x80000000ull) && ((token^prior.token)&~0x80000000ull)==0 && oldbits==rawbits)
  return prior.last;
}
if(!(token&0x80000000ull)||native60||!std::isfinite(raw)||!std::isfinite(factor)||!std::isfinite(fraction)||fraction<0||fraction>1){states.erase(key);return nativebits;}
auto it=states.find(key);if(it==states.end()){if(states.size()>=32)return nativebits;states[key]={token,tick,raw,raw,false,nativebits};return nativebits;}auto&s=it->second;
if(token<s.token){states.erase(it);return nativebits;}if(token==s.token){if(raw==s.now)return s.last;states.erase(it);return nativebits;}
uint32_t elapsed=tick-s.tick;if(elapsed>1){s={token,tick,raw,raw,false,nativebits};return nativebits;}
if(elapsed==1){s.before=s.now;s.now=raw;s.ready=true;}else if(std::remainder(double(raw)-s.now,360.)!=0){s={token,tick,raw,raw,false,nativebits};return nativebits;}
double out=s.ready?s.before+std::remainder(s.now-s.before,360.)*fraction:raw;float degrees=float(out);float radians=degrees*factor;uint32_t result;memcpy(&result,&radians,4);if(!s.ready)result=nativebits;s.token=token;s.tick=tick;s.last=result;return result;
}
