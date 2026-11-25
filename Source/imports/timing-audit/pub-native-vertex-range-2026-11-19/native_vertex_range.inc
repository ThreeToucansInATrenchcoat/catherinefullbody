// Included after the original readers and skin-mirror helpers. A resolved
// iterator is private to this synchronous call and never survives an upload.
namespace native_vertex_range {
int mode(){static const int value=[] {const auto* p=std::getenv("CATHERINE_NATIVE_VERTEX_RANGE");return p&&!std::strcmp(p,"1")?1:p&&!std::strcmp(p,"2")?2:0;}();return value;}
enum class Kind {Unwritten,Contained,Compatibility};
struct NativeRead {Kind kind=Kind::Compatibility;It owner=g_regions.end();};
struct Census {uint64_t requests=0,unwritten=0,contained=0,compatibility=0,compared=0;} census;
void report(){if(census.requests==1||census.requests%100000==0)log("Native vertex ranges: requests %llu unwritten %llu contained %llu compatibility %llu compared %llu",(unsigned long long)census.requests,(unsigned long long)census.unwritten,(unsigned long long)census.contained,(unsigned long long)census.compatibility,(unsigned long long)census.compared);}
bool same(const Upload& a,const Upload& b){return a.buffer==b.buffer&&a.offset==b.offset&&a.ptr==b.ptr;}
NativeRead resolve(const uint8_t* p,uint64_t size,uint64_t padded) {
    if(written_separate())return {};
    if(!p||!size||g_regions.empty())return {Kind::Unwritten,g_regions.end()};
    const RegionSpan span=overlapping(p,size);
    if(span.empty())return {Kind::Unwritten,g_regions.end()};
    const auto it=containing(p,size,padded,16,span);
    return it==g_regions.end()?NativeRead{}:NativeRead{Kind::Contained,it};
}
// Original mirror selection, without its counter mutations. Mode2 obtains its
// owner by the old independent upper_bound lookup, never by the native packet.
Upload original_mirror(const uint8_t* p,uint64_t size,uint64_t padded,Upload upload) {
    if(!skin_mirror_mode()||!p||!size||g_regions.empty())return upload;
    auto it=g_regions.upper_bound(p);if(it==g_regions.begin())return upload;--it;
    const auto& r=it->second;
    if(!r.skin_vertex||r.gpu_written||!r.skin_mirror.buffer||p<it->first)return upload;
    const uint64_t offset=uint64_t(p-it->first);
    if(offset>r.size||size>r.size-offset||offset>r.skin_mirror_cap||padded>r.skin_mirror_cap-offset)return upload;
    if(upload.buffer!=r.up.buffer||upload.offset!=r.up.offset+offset||upload.ptr!=r.up.ptr+offset)return upload;
    if(skin_mirror_mode()==2)return upload;
    return {r.skin_mirror.buffer,r.skin_mirror.offset+offset,r.up.ptr+offset};
}
struct Oracle {Kind kind=Kind::Compatibility;It owner=g_regions.end();Upload expected{};};
Oracle original_choice(const uint8_t* p,uint64_t size,uint64_t padded,bool watched) {
    if(written_separate())return {};
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
    if(watched&&!written_overlaps(p,size))return {Kind::Unwritten,g_regions.end(),{}};
#endif
    // Replay the old written_read_impl predicates independently, without
    // performing waits, flags, allocation, copies or Vulkan recording twice.
    if(!p||!size||g_regions.empty()||written_separate())return {};
    const RegionSpan span=overlapping(p,size);
    if(span.empty())return {Kind::Unwritten,g_regions.end(),{}};
    const auto it=containing(p,size,padded,16,span);
    if(it==g_regions.end())return {};
    return {Kind::Contained,it,original_mirror(p,size,padded,at(it,uint64_t(p-it->first)))};
}
Upload original_read(VkCommandBuffer cb,const uint8_t* p,uint64_t size,uint64_t padded,const ShaderInfo& shader,bool watched) {
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
    if(watched&&!written_overlaps(p,size))return watched_geometry_upload(p,size,padded,false);
#endif
    auto result=shader.hash=="52421ce0a854d776"?read_upload(cb,p,size,padded,16):read_vertex_upload(cb,p,size,padded,16);
    substitute_skin_vertex(p,size,padded,result);return result;
}
Upload consume_contained(const NativeRead& input,const uint8_t* p,uint64_t size,uint64_t padded,bool allow_pending) {
    const auto it=input.owner;
    if(!allow_pending)wait_async_skin_region(it->second,SkinJoinReason::Read);
    ++g_shared;g_read=true;auto& r=it->second;r.gpu_accessed=true;
    auto result=at(it,uint64_t(p-it->first));
    // Same mirror flags, cap/range and authoritative host Upload identity as
    // substitute_skin_vertex, consuming the already resolved owner directly.
    if(!skin_mirror_mode()||!r.skin_vertex||r.gpu_written||!r.skin_mirror.buffer)return result;
    const uint64_t offset=uint64_t(p-it->first);
    if(offset>r.size||size>r.size-offset||offset>r.skin_mirror_cap||padded>r.skin_mirror_cap-offset)return result;
    if(result.buffer!=r.up.buffer||result.offset!=r.up.offset+offset||result.ptr!=r.up.ptr+offset)return result;
    if(skin_mirror_mode()==2){++g_skin_mirror_stats.host_controls;return result;}
    ++g_skin_mirror_stats.bindings;return {r.skin_mirror.buffer,r.skin_mirror.offset+offset,r.up.ptr+offset};
}
Upload read(VkCommandBuffer cb,const uint8_t* p,uint64_t size,uint64_t padded,const ShaderInfo& shader,bool watched) {
    const int selected=mode();if(!selected)return original_read(cb,p,size,padded,shader,watched);
    ++census.requests;
    const auto input=resolve(p,size,padded);
    Oracle oracle;
    if(selected==2) {
        oracle=original_choice(p,size,padded,watched);
        // The legacy early-exit has no-overlap semantics for null/empty input;
        // separate mode is deliberately kept fully compatible by resolve().
        if(!written_separate()&&(!p||!size||g_regions.empty()))oracle.kind=Kind::Unwritten;
        if(oracle.kind!=input.kind||(input.kind==Kind::Contained&&oracle.owner!=input.owner))fatal("Native vertex range selection differs from original lookup");
        ++census.compared;
    }
    if(input.kind==Kind::Compatibility){++census.compatibility;report();return original_read(cb,p,size,padded,shader,watched);}
    if(input.kind==Kind::Unwritten) {
        ++census.unwritten;report();
#ifdef NX_CATHERINE_WATCHED_GEOMETRY_BUILD
        if(watched)return watched_geometry_upload(p,size,padded,false);
#endif
        return dedup_upload(p,size,padded,16);
    }
    ++census.contained;
    auto result=consume_contained(input,p,size,padded,shader.hash!="52421ce0a854d776");
    if(selected==2&&!same(result,oracle.expected))fatal("Native vertex Upload differs from original host/mirror binding");
    report();return result;
}
} // namespace native_vertex_range
