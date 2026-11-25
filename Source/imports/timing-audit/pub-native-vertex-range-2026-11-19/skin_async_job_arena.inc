std::unordered_map<uint64_t,std::vector<std::shared_ptr<AsyncSkinJob>>> g_async_skin_arena;
uint64_t g_async_skin_job_new=0,g_async_skin_job_reused=0,g_async_skin_job_evicted=0;
uint64_t async_skin_job_key(uint32_t count,uint32_t matrices,bool tangent){return(uint64_t(matrices)<<33)|(uint64_t(count)<<1)|uint64_t(tangent);}
uint64_t async_skin_job_bytes(const AsyncSkinJob& job){return sizeof(AsyncSkinJob)+job.palette.capacity()+job.matrices.capacity()*sizeof(SkinMatrix)+job.preserved_w.capacity()*sizeof(std::array<uint32_t,2>);}
void recycle_async_skin_job(const std::shared_ptr<AsyncSkinJob>& job){
    if(job->cached)return;
    job->input.reset();job->preserved_before.reset();job->destination=nullptr;job->cached=true;
    g_async_skin_arena[async_skin_job_key(job->count,job->matrix_count,job->tangent)].push_back(job);
}
std::shared_ptr<AsyncSkinJob> acquire_async_skin_job(uint32_t count,uint32_t matrices,bool tangent){
    auto& bucket=g_async_skin_arena[async_skin_job_key(count,matrices,tangent)];
    for(size_t i=bucket.size();i>0;--i){
        auto& candidate=bucket[i-1];
        if(candidate.use_count()!=1||!candidate->joined||!candidate->done.load(std::memory_order_acquire))continue;
        auto job=std::move(candidate);candidate=std::move(bucket.back());bucket.pop_back();
        job->done.store(false,std::memory_order_relaxed);job->joined=false;job->success=false;job->cached=false;job->canonical_count=0;job->execution_ns=0;
        ++g_async_skin_job_reused;return job;
    }
    ++g_async_skin_job_new;return std::make_shared<AsyncSkinJob>();
}
void trim_async_skin_arena(){
    constexpr uint64_t budget=64ull<<20;uint64_t retained=0;
    for(const auto& [key,bucket]:g_async_skin_arena)for(const auto& job:bucket)retained+=async_skin_job_bytes(*job);
    if(retained<=budget)return;
    for(auto& [key,bucket]:g_async_skin_arena){
        for(auto it=bucket.begin();it!=bucket.end()&&retained>budget;){
            if(it->use_count()==1&&(*it)->joined&&(*it)->done.load(std::memory_order_acquire)){
                retained-=async_skin_job_bytes(**it);it=bucket.erase(it);++g_async_skin_job_evicted;
            }else ++it;
        }
    }
}
