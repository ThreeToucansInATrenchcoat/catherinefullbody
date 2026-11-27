// Included in the sets TU's private namespace. The original helper below keeps
// its one models/instances owner; this only avoids work proven inactive.
namespace inactive_c3 {
int mode() {
    static const int value=[] {const auto* p=std::getenv("CATHERINE_INACTIVE_C3");return p&&!std::strcmp(p,"1")?1:p&&!std::strcmp(p,"2")?2:0;}();
    return value;
}
struct Census {uint64_t requests=0,skipped=0,compared=0,full=0,unknown=0,active=0,clock_on=0,diagnostic=0;} census;
void report() {
    if(census.requests==1||census.requests%100000==0)
        log("Inactive C3: requests %llu skipped %llu compared %llu full %llu unknown %llu active %llu clockOn %llu diagnostic %llu",
            (unsigned long long)census.requests,(unsigned long long)census.skipped,(unsigned long long)census.compared,
            (unsigned long long)census.full,(unsigned long long)census.unknown,(unsigned long long)census.active,
            (unsigned long long)census.clock_on,(unsigned long long)census.diagnostic);
}
bool canonical_affine_row(const uint8_t* bytes,uint64_t size) {
    if(!bytes||size!=64)return false;
    uint32_t row[4];std::memcpy(row,bytes+48,sizeof row);
    // Preserve original floating-point status effects for noncanonical rows.
    // Typical affine matrices use signed zeros and exactly positive one.
    return ((row[0]|row[1]|row[2])&0x7fffffffu)==0&&row[3]==0x3f800000u;
}
const uint8_t* transform(const uint8_t* bytes,uint64_t size,const uint8_t* vertices,uint64_t vertex_size,uint32_t stride,const ShaderInfo& shader) {
    auto original=[&] {
        uint64_t geometry;bool rigid;unsigned sectors;
        {perf22::Scope timing(perf22::GenericGeometry);geometry=geometry_identity98(vertices,vertex_size,stride);rigid=!written_overlaps(vertices,vertex_size);sectors=rigid?catherine::visual::chain_rotation_sectors(geometry,vertices,vertex_size,stride):1;}
        return puzzle_model_transform88(bytes,size,geometry,shader,rigid,sectors);
    };
    const int selected=mode();if(!selected)return original();
    ++census.requests;
#if defined(CATHERINE_HIGH_REFRESH_BUILD) && !defined(CATHERINE_POSE_DIAGNOSTICS)
    const uint64_t frame=puzzle_render_frame(),clock=puzzle_render_clock();
    const bool known=t_replay_serial&&t_replay_mark&&frame;
    if(known&&!generic_c3_active88()&&!(clock&0x80000000u)&&canonical_affine_row(bytes,size)) {
        if(selected==2) {
            auto* result=original();
            if(result!=bytes||generic_c3_active88()||puzzle_render_frame()!=frame||puzzle_render_clock()!=clock)
                fatal("Inactive C3 shortcut differs from original model transform");
            ++census.compared;
        }
        ++census.skipped;report();return bytes;
    }
    census.unknown+=!known;census.active+=generic_c3_active88();census.clock_on+=(clock&0x80000000u)!=0;
#else
    ++census.diagnostic;
#endif
    ++census.full;report();return original();
}
} // namespace inactive_c3
