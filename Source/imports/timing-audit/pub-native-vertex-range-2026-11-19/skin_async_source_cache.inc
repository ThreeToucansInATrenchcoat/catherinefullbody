// Replay-owner cache. Every published version owns its bytes; workers retain shared immutable versions.
std::atomic<uint64_t> g_skin_source_live{0};
struct SkinSourceVersion {
    std::vector<uint8_t> bytes,canonical_mask;
    std::vector<SkinJoints> joints;
    uint64_t canonical_count=0,owned=0;
    ~SkinSourceVersion(){if(owned)g_skin_source_live.fetch_sub(owned,std::memory_order_relaxed);}
};
struct SkinSourceKey {
    const uint8_t* identity=nullptr;uint64_t bytes=0;uint32_t count=0,matrix_count=0;bool tangent=false,canonical=false;
    bool operator==(const SkinSourceKey& b)const{return identity==b.identity&&bytes==b.bytes&&count==b.count&&matrix_count==b.matrix_count&&tangent==b.tangent&&canonical==b.canonical;}
};
struct SkinSourceHash {
    size_t operator()(const SkinSourceKey& k)const{
        uint64_t h=(uint64_t(reinterpret_cast<uintptr_t>(k.identity))>>4)^(k.bytes*0x9e3779b97f4a7c15ull)^(uint64_t(k.count)<<32)^k.matrix_count;
        return size_t(h^(uint64_t(k.tangent)<<61)^(uint64_t(k.canonical)<<62));
    }
};
struct SkinSourceEntry {std::shared_ptr<const SkinSourceVersion> version;uint64_t last=0;};
std::unordered_map<SkinSourceKey,SkinSourceEntry,SkinSourceHash> g_skin_source_cache;
uint64_t g_skin_source_clock=0,g_skin_source_hits=0,g_skin_source_misses=0,g_skin_source_compared=0,g_skin_source_copied=0,g_skin_source_evictions=0;
bool skin_source_room(uint64_t bytes) {
    constexpr uint64_t budget=64ull<<20;
    while(bytes<=budget&&g_skin_source_live.load(std::memory_order_relaxed)>budget-bytes){
        auto oldest=g_skin_source_cache.end();
        for(auto it=g_skin_source_cache.begin();it!=g_skin_source_cache.end();++it)
            if(it->second.version.use_count()==1&&(oldest==g_skin_source_cache.end()||it->second.last<oldest->second.last))oldest=it;
        if(oldest==g_skin_source_cache.end())return false;
        g_skin_source_cache.erase(oldest);++g_skin_source_evictions;
    }
    return bytes<=budget;
}
std::shared_ptr<const SkinSourceVersion> skin_source_snapshot(const uint8_t* identity,const uint8_t* bytes,uint64_t size,
            uint32_t count,uint32_t matrix_count,bool tangent,bool allow,const std::vector<SkinMatrix>& matrices) {
    if(!identity||!bytes||!count||matrices.size()!=matrix_count||uint64_t(count)*(tangent?80:64)!=size)return {};
    const SkinSourceKey key{identity,size,count,matrix_count,tangent,allow};
    if(auto found=g_skin_source_cache.find(key);found!=g_skin_source_cache.end()){
        g_skin_source_compared+=size;
        if(found->second.version->bytes.size()==size&&std::memcmp(found->second.version->bytes.data(),bytes,size)==0){
            found->second.last=++g_skin_source_clock;++g_skin_source_hits;return found->second.version;
        }
    }
    ++g_skin_source_misses;
    const uint64_t estimate=sizeof(SkinSourceVersion)+size+uint64_t(count)*(sizeof(SkinJoints)+1);
    if(!skin_source_room(estimate))return {};
    auto result=std::make_shared<SkinSourceVersion>();result->bytes.assign(bytes,bytes+size);result->joints.resize(count);result->canonical_mask.resize(count);
    SkinReject reason{};uint32_t element=0;SkinJoints bad{};
    const bool valid=tangent
        ?validate_async_skin_source<true>(result->bytes.data(),matrices,nullptr,nullptr,count,allow,reason,element,bad,result->canonical_count,result->joints.data(),result->canonical_mask.data())
        :validate_async_skin_source<false>(result->bytes.data(),matrices,nullptr,nullptr,count,allow,reason,element,bad,result->canonical_count,result->joints.data(),result->canonical_mask.data());
    if(!valid)return {};
    const uint64_t actual=sizeof(SkinSourceVersion)+result->bytes.capacity()+result->joints.capacity()*sizeof(SkinJoints)+result->canonical_mask.capacity();
    if(!skin_source_room(actual))return {};
    result->owned=actual;g_skin_source_live.fetch_add(actual,std::memory_order_relaxed);g_skin_source_copied+=size;
    g_skin_source_cache[key]={result,++g_skin_source_clock};return result;
}
