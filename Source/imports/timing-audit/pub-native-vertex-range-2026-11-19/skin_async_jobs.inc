// Workers touch only shared owned immutable inputs and this job's private host upload destination.
#include "skin_async_source_cache.inc"
struct AsyncSkinJob {
    std::shared_ptr<const SkinSourceVersion> input;
    std::vector<uint8_t> palette;
    std::vector<std::array<uint32_t,2>> preserved_w;
    std::shared_ptr<std::vector<uint8_t>> preserved_before;
    std::vector<SkinMatrix> matrices;
    float* destination=nullptr;
    uint32_t count=0,matrix_count=0;
    bool tangent=false,allow_canonical=false,success=false,joined=false,cached=false;
    uint64_t canonical_count=0;
    std::atomic<bool> done{false};
    std::mutex mutex;std::condition_variable condition;
    uint64_t execution_ns=0;
};
 #include "skin_async_job_arena.inc"

uint64_t async_skin_now() {return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
struct AsyncSkinStats {
    uint64_t accepted=0,vertices=0,snapshot_bytes=0,world=0,existing=0,short_mesh=0,invalid=0,budget=0,queue_repairs=0,worker_repairs=0,joined=0,execution_ns=0,canonical_jobs=0,canonical_vertices=0;
    std::array<uint64_t,7> waits{},wait_ns{};
} g_async_skin_stats;
std::vector<std::shared_ptr<AsyncSkinJob>> g_async_skin_jobs;
uint64_t g_async_skin_owned=0;
bool async_skin_enabled() {
    static const bool enabled=[] {const char* e=std::getenv("CATHERINE_ASYNC_CPU_SKIN");return e&&e[0]=='1'&&!e[1];}();
    return enabled;
}
bool execute_async_skin(AsyncSkinJob& job) {
    SkinReject reason{};uint32_t element=0;SkinJoints bad{};uint64_t canonical=0;
    const uint64_t start=async_skin_now();
    const float* preserved=job.preserved_before?reinterpret_cast<const float*>(job.preserved_before->data()):reinterpret_cast<const float*>(job.preserved_w.data());
    const bool success=job.preserved_before
        ?(job.tangent?run_cpu_skin_fused<true>(job.input->bytes.data(),job.matrices,job.destination,preserved,job.count,job.allow_canonical,reason,element,bad,canonical):run_cpu_skin_fused<false>(job.input->bytes.data(),job.matrices,job.destination,preserved,job.count,job.allow_canonical,reason,element,bad,canonical))
        :(job.tangent?run_cpu_skin_compact_preserved<true>(job.input->bytes.data(),job.matrices,job.destination,preserved,job.count,job.allow_canonical,reason,element,bad,canonical):run_cpu_skin_compact_preserved<false>(job.input->bytes.data(),job.matrices,job.destination,preserved,job.count,job.allow_canonical,reason,element,bad,canonical));
    job.execution_ns=async_skin_now()-start;return success&&canonical==job.canonical_count;
}
void repair_async_skin(AsyncSkinJob& job) {
    // Replay owner only. This original validated reference owns the only use of its legacy static palette vector.
    for(uint32_t i=0;i<job.count;++i){
        const uint64_t stride=job.tangent?48:32;auto* out=reinterpret_cast<uint8_t*>(job.destination)+uint64_t(i)*stride;
        if(job.preserved_before){std::memcpy(out+28,job.preserved_before->data()+uint64_t(i)*stride+28,4);if(job.tangent)std::memcpy(out+44,job.preserved_before->data()+uint64_t(i)*stride+44,4);}
        else{std::memcpy(out+28,&job.preserved_w[i][0],4);if(job.tangent)std::memcpy(out+44,&job.preserved_w[i][1],4);}
    }
    run_cpu_skin(reinterpret_cast<const float*>(job.input->bytes.data()),reinterpret_cast<const float*>(job.palette.data()),job.destination,
                 job.count,job.matrix_count,job.tangent,job.input->joints.data());
    // The original fused kernel explicitly canonicalizes only these proven input records.
    const uint32_t xyz[3]={0x7fffffffu,0x7fffffffu,0x7fffffffu};
    for(uint32_t i=0;i<job.count;++i)if(job.input->canonical_mask[i])
        std::memcpy(job.destination+uint64_t(i)*(job.tangent?12:8)+(job.tangent?8:0),xyz,sizeof xyz);
    job.success=true;
}
class AsyncSkinPool {
    std::mutex mutex;std::condition_variable condition;
    std::deque<std::shared_ptr<AsyncSkinJob>> queue;std::vector<std::thread> threads;bool stopping=false;
    void work() {
        for(;;) {
            std::shared_ptr<AsyncSkinJob> job;
            {std::unique_lock lock(mutex);condition.wait(lock,[&]{return stopping||!queue.empty();});
             if(stopping&&queue.empty())return;job=std::move(queue.front());queue.pop_front();}
            bool success=false;try {success=execute_async_skin(*job);}catch(...) {success=false;}
            {std::lock_guard lock(job->mutex);job->success=success;job->done.store(true,std::memory_order_release);}
            job->condition.notify_all();
        }
    }
public:
    AsyncSkinPool() {
        unsigned count=2;
        if(const char* value=std::getenv("CATHERINE_ASYNC_CPU_SKIN_WORKERS"))
            if((value[0]=='0'||value[0]=='1'||value[0]=='2')&&!value[1])count=unsigned(value[0]-'0');
        try {for(unsigned i=0;i<count;++i)threads.emplace_back([this]{work();});}
        catch(...) { // A partial thread start must never destroy joinable std::threads.
            {std::lock_guard lock(mutex);stopping=true;}condition.notify_all();
            for(auto& thread:threads)thread.join();threads.clear();
        }
    }
    ~AsyncSkinPool() {
        {std::lock_guard lock(mutex);stopping=true;}condition.notify_all();for(auto& thread:threads)thread.join();
    }
    bool submit(const std::shared_ptr<AsyncSkinJob>& job) {
        if(threads.empty())return false;
        try {{std::lock_guard lock(mutex);queue.push_back(job);}condition.notify_one();return true;}catch(...) {return false;}
    }
    static AsyncSkinPool& get(){static AsyncSkinPool pool;return pool;}
};
void wait_async_skin_job(const std::shared_ptr<AsyncSkinJob>& job,SkinJoinReason reason) {
    if(!job || job->joined)return;
    if(!job->done.load(std::memory_order_acquire)) {
        const uint64_t start=async_skin_now();
        std::unique_lock lock(job->mutex);job->condition.wait(lock,[&]{return job->done.load(std::memory_order_acquire);});
        const auto index=size_t(reason);++g_async_skin_stats.waits[index];g_async_skin_stats.wait_ns[index]+=async_skin_now()-start;
    }
    if(!job->success) {repair_async_skin(*job);++g_async_skin_stats.worker_repairs;}
    job->joined=true;++g_async_skin_stats.joined;g_async_skin_stats.execution_ns+=job->execution_ns;
    job->input.reset();job->preserved_before.reset(); // workers have completed; buffer capacities stay reusable

}
void wait_async_skin_region(Region& region,SkinJoinReason reason) {wait_async_skin_job(region.async_skin,reason);}
void wait_async_skin_jobs(SkinJoinReason reason) {
    for(const auto& job:g_async_skin_jobs)wait_async_skin_job(job,reason);
    for(const auto& job:g_async_skin_jobs)recycle_async_skin_job(job);
    g_async_skin_jobs.clear();g_async_skin_owned=0;trim_async_skin_arena();
}
void async_skin_log() {
    if(async_skin_enabled()) log("Async CPU skin reuse: source-hits %llu source-misses %llu compared %llu copied %llu source-live %llu source-evictions %llu job-new %llu job-reused %llu job-evicted %llu",(unsigned long long)g_skin_source_hits,(unsigned long long)g_skin_source_misses,(unsigned long long)g_skin_source_compared,(unsigned long long)g_skin_source_copied,(unsigned long long)g_skin_source_live.load(),(unsigned long long)g_skin_source_evictions,(unsigned long long)g_async_skin_job_new,(unsigned long long)g_async_skin_job_reused,(unsigned long long)g_async_skin_job_evicted);
    if(!async_skin_enabled())return;const auto& s=g_async_skin_stats;
    log("Async CPU skin jobs: accepted %llu vertices %llu snapshots %llu world-fallback %llu existing-fallback %llu small-fallback %llu invalid-fallback %llu budget-fallback %llu queue-repairs %llu worker-repairs %llu joined %llu worker-ms %.3f canonical-jobs %llu canonical-vertices %llu",
        (unsigned long long)s.accepted,(unsigned long long)s.vertices,(unsigned long long)s.snapshot_bytes,(unsigned long long)s.world,(unsigned long long)s.existing,
        (unsigned long long)s.short_mesh,(unsigned long long)s.invalid,(unsigned long long)s.budget,(unsigned long long)s.queue_repairs,(unsigned long long)s.worker_repairs,
        (unsigned long long)s.joined,double(s.execution_ns)/1e6,(unsigned long long)s.canonical_jobs,(unsigned long long)s.canonical_vertices);
    log("Async CPU skin joins: read %llu/%.3fms write %llu/%.3fms provenance %llu/%.3fms seal %llu/%.3fms erase %llu/%.3fms submit %llu/%.3fms reset %llu/%.3fms",
        (unsigned long long)s.waits[0],double(s.wait_ns[0])/1e6,(unsigned long long)s.waits[1],double(s.wait_ns[1])/1e6,
        (unsigned long long)s.waits[2],double(s.wait_ns[2])/1e6,(unsigned long long)s.waits[3],double(s.wait_ns[3])/1e6,
        (unsigned long long)s.waits[4],double(s.wait_ns[4])/1e6,(unsigned long long)s.waits[5],double(s.wait_ns[5])/1e6,
        (unsigned long long)s.waits[6],double(s.wait_ns[6])/1e6);
}
bool try_async_skin(const uint8_t* destination,uint64_t size,const uint8_t* source,uint64_t source_bytes,const uint8_t* source_identity,
                    const uint8_t* palette,uint64_t palette_bytes,uint32_t count,uint32_t matrix_count,bool tangent,It existing) {
    if(!async_skin_enabled())return false;
    if(g_dispatch_palette_interpolated){++g_async_skin_stats.world;return false;}
    if(existing!=g_regions.end()){++g_async_skin_stats.existing;return false;}
    if(count<128){++g_async_skin_stats.short_mesh;return false;}
    const uint64_t owned=source_bytes+palette_bytes+uint64_t(count)*(sizeof(SkinJoints)+1)+uint64_t(matrix_count)*sizeof(SkinMatrix);
    constexpr uint64_t budget=64ull<<20;
    if(owned>budget || g_async_skin_owned>budget-owned){++g_async_skin_stats.budget;return false;}
    std::shared_ptr<AsyncSkinJob> job;
    try {
        job=acquire_async_skin_job(count,matrix_count,tangent);job->palette.assign(palette,palette+palette_bytes);
        job->count=count;job->matrix_count=matrix_count;job->tangent=tangent;
        job->allow_canonical=canonical_skin_nan_opt_in(tangent?"NX_CPU_SKIN_NAN_TANGENTS":"NX_CPU_SKIN_NAN_POSITIONS");
        uint32_t element=0;
        if(!transpose_skin_palette(job->palette.data(),matrix_count,job->matrices,element)) {++g_async_skin_stats.invalid;job->joined=true;job->done.store(true);recycle_async_skin_job(job);return false;}
        job->input=skin_source_snapshot(source_identity,source,source_bytes,count,matrix_count,tangent,job->allow_canonical,job->matrices);
        if(!job->input){++g_async_skin_stats.invalid;job->joined=true;job->done.store(true);recycle_async_skin_job(job);return false;}
        job->canonical_count=job->input->canonical_count;
        g_async_skin_jobs.push_back(job); // Own every admitted job through region cleanup and submission boundaries.
    }catch(...) {++g_async_skin_stats.budget;return false;}
    Region fresh=prepare_fresh_skin_destination(destination,size);
    const uint8_t* preserved=fresh.before?fresh.before->data():destination;
    const uint64_t stride=tangent?48:32,produced=uint64_t(count)*stride;
    job->preserved_before=fresh.before;
    if(!fresh.before){
        if(job->preserved_w.size()<count)job->preserved_w.resize(count);
        for(uint32_t i=0;i<count;++i){std::memcpy(&job->preserved_w[i][0],preserved+uint64_t(i)*stride+28,4);job->preserved_w[i][1]=0;if(tangent)std::memcpy(&job->preserved_w[i][1],preserved+uint64_t(i)*stride+44,4);}
    }
    if(produced<size)std::memcpy(fresh.up.ptr+produced,preserved+produced,size-produced);
    std::memset(fresh.up.ptr+size,0,fresh.cap-size);
    job->destination=reinterpret_cast<float*>(fresh.up.ptr);fresh.async_skin=job;mark_skin_vertex(fresh);
    g_regions.emplace(destination,std::move(fresh));mark_written_region(destination,size);dedup_invalidate(destination,size);
    g_async_skin_owned+=owned;++g_async_skin_stats.accepted;g_async_skin_stats.vertices+=count;g_async_skin_stats.snapshot_bytes+=palette_bytes+(job->preserved_before?0:uint64_t(count)*(tangent?8:4));
    if(job->canonical_count){++g_async_skin_stats.canonical_jobs;g_async_skin_stats.canonical_vertices+=job->canonical_count;}
    (tangent?g_skin_nan_vertices:g_skin_nan_positions)+=job->canonical_count;
    bool queued=false;try {queued=AsyncSkinPool::get().submit(job);}catch(...) {queued=false;}
    if(!queued) {
        repair_async_skin(*job);job->done.store(true,std::memory_order_release);++g_async_skin_stats.queue_repairs;
    }
    return true;
}
