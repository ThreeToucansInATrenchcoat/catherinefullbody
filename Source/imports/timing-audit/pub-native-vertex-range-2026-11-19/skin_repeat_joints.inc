namespace skin_repeat_joints {
int mode(){static const int value=[] {const char* p=std::getenv("CATHERINE_SKIN_REPEAT_JOINTS");return p&&(p[0]=='1'||p[0]=='2')&&!p[1]?p[0]-'0':0;}();return value;}
std::atomic<uint64_t> compared_chunks{0},compared_vertices{0},mismatches{0},allocation_fallbacks{0};
void report(){if(!mode())return;log("CPU skin repeat joints: mode %d compared-chunks %llu compared-vertices %llu mismatches %llu allocation-fallbacks %llu; tail-pair transform reuse, original weight FMA order",mode(),(unsigned long long)compared_chunks.load(std::memory_order_relaxed),(unsigned long long)compared_vertices.load(std::memory_order_relaxed),(unsigned long long)mismatches.load(std::memory_order_relaxed),(unsigned long long)allocation_fallbacks.load(std::memory_order_relaxed));}
}

template<bool Tangent>
__attribute__((target("avx,fma"))) bool compare_cpu_skin_repeat_joints(const uint8_t* source,const std::vector<SkinMatrix>&matrices,float*dest,const float*preserved,uint32_t count,bool allow_canonical,SkinReject&reason,uint32_t&element,SkinJoints&bad_joints,uint64_t&canonical_count){
    // Only diagnostic mode2 snapshots inputs. Both actual kernels read the
    // identical owned source, prior bytes and transposed palette, even while
    // the guest recording thread prepares a later frame.
    try{
        const size_t source_size=size_t(count)*(Tangent?80:64),output_size=size_t(count)*(Tangent?48:32);
        std::vector<uint8_t> owned_source(source_size),owned_prior(output_size),reference(output_size);
        std::vector<SkinMatrix> owned_matrices(matrices);
        if(source_size)std::memcpy(owned_source.data(),source,source_size);
        if(output_size){std::memcpy(owned_prior.data(),preserved,output_size);std::memcpy(reference.data(),dest,output_size);}
        auto original_reason=reason;auto original_element=element;auto original_canonical=canonical_count;SkinJoints original_bad{};
        const bool actual=run_cpu_skin_fused_repeated<Tangent>(owned_source.data(),owned_matrices,dest,reinterpret_cast<const float*>(owned_prior.data()),count,allow_canonical,reason,element,bad_joints,canonical_count);
        const bool expected=run_cpu_skin_fused_original<Tangent>(owned_source.data(),owned_matrices,reinterpret_cast<float*>(reference.data()),reinterpret_cast<const float*>(owned_prior.data()),count,allow_canonical,original_reason,original_element,original_bad,original_canonical);
        const bool bad=actual!=expected||reason!=original_reason||element!=original_element||canonical_count!=original_canonical||
            (!expected&&original_reason==SkinReject::JointIndex&&bad_joints!=original_bad)||
            (output_size&&std::memcmp(dest,reference.data(),output_size));
        skin_repeat_joints::compared_chunks.fetch_add(1,std::memory_order_relaxed);
        skin_repeat_joints::compared_vertices.fetch_add(count,std::memory_order_relaxed);
        if(bad){skin_repeat_joints::mismatches.fetch_add(1,std::memory_order_relaxed);fatal("CPU skin repeat-joint arithmetic/state differs from original");}
        return actual;
    }catch(const std::bad_alloc&){
        skin_repeat_joints::allocation_fallbacks.fetch_add(1,std::memory_order_relaxed);
        return run_cpu_skin_fused_original<Tangent>(source,matrices,dest,preserved,count,allow_canonical,reason,element,bad_joints,canonical_count);
    }
}

template<bool Tangent>
__attribute__((target("avx,fma"))) bool run_cpu_skin_fused(const uint8_t*source,const std::vector<SkinMatrix>&matrices,float*dest,const float*preserved,uint32_t count,bool allow_canonical,SkinReject&reason,uint32_t&element,SkinJoints&bad_joints,uint64_t&canonical_count){
    const int selected=skin_repeat_joints::mode();
    if(selected==2)return compare_cpu_skin_repeat_joints<Tangent>(source,matrices,dest,preserved,count,allow_canonical,reason,element,bad_joints,canonical_count);
    if(selected==1)return run_cpu_skin_fused_repeated<Tangent>(source,matrices,dest,preserved,count,allow_canonical,reason,element,bad_joints,canonical_count);
    return run_cpu_skin_fused_original<Tangent>(source,matrices,dest,preserved,count,allow_canonical,reason,element,bad_joints,canonical_count);
}
