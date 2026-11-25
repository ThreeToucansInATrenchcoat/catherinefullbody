// Replay-owner-only state. The authoritative host upload is never replaced or mutated here.
bool g_skin_mirror_pending=false;
struct SkinMirrorStats { uint64_t batches=0,regions=0,bytes=0,bindings=0,host_controls=0,invalidations=0,failures=0,blocked_cpu_reads=0,blocked_cpu_writers=0; } g_skin_mirror_stats;
int skin_mirror_mode() {
    static const int mode=[] { const char* e=std::getenv("CATHERINE_SKIN_DEVICE_MIRROR");
        return e && (e[0]=='1'||e[0]=='2') && !e[1] ? e[0]-'0' : 0; }();
    return mode;
}
void invalidate_skin_mirror(Region& r) {
    invalidate_morph_identity(r);
    if(r.skin_mirror.buffer) ++g_skin_mirror_stats.invalidations;
    r.skin_vertex=false;r.skin_mirror={};r.skin_mirror_cap=0;
}
void mark_skin_vertex(Region& r) {
    invalidate_skin_mirror(r);
    if(!skin_mirror_mode() || r.gpu_written) return;
    r.skin_vertex=true;g_skin_mirror_pending=true;
}
void skin_mirror_log() {
    if(!skin_mirror_mode()) return;
    const auto& s=g_skin_mirror_stats;
    log("CPU skin device mirror: mode %d batches %llu regions %llu bytes %llu bindings %llu host-controls %llu invalidations %llu allocation-failures %llu blocked-CPU-reads %llu blocked-CPU-writers %llu",
        skin_mirror_mode(),(unsigned long long)s.batches,(unsigned long long)s.regions,(unsigned long long)s.bytes,
        (unsigned long long)s.bindings,(unsigned long long)s.host_controls,(unsigned long long)s.invalidations,
        (unsigned long long)s.failures,(unsigned long long)s.blocked_cpu_reads,(unsigned long long)s.blocked_cpu_writers);
}
