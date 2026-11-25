// Immutable owning map keys are unchanged. A View exists only during find().
namespace linked_lookup {
using Shaders=std::vector<const ShaderInfo*>;
using Key=std::pair<Shaders,const ProgramLayout*>;
struct View {const Shaders& shaders;const ProgramLayout* layout;};
using ReferenceKey=std::pair<const Shaders&,const ProgramLayout*>;
struct Less {
    using is_transparent=void;
    bool operator()(const Key& a,const Key& b)const{return a<b;}
    bool operator()(const Key& a,const View& b)const{return ReferenceKey{a.first,a.second}<ReferenceKey{b.shaders,b.layout};}
    bool operator()(const View& a,const Key& b)const{return ReferenceKey{a.shaders,a.layout}<ReferenceKey{b.first,b.second};}
};
int mode() {
    static const int value=[] {const auto* p=std::getenv("CATHERINE_MATERIAL_PLAN_LOOKUP");return p&&!std::strcmp(p,"1")?1:p&&!std::strcmp(p,"2")?2:0;}();
    return value;
}
struct Census {uint64_t calls=0,last_hits=0,lookups=0,hits=0,misses=0,insert_copies=0,oracle_copies=0,compared=0;} census;
void report() {
    if(census.calls==1||census.calls%100000==0)
        log("Material plan lookup: calls %llu lastHits %llu lookups %llu hits %llu misses %llu insertCopies %llu oracleCopies %llu compared %llu",
            (unsigned long long)census.calls,(unsigned long long)census.last_hits,(unsigned long long)census.lookups,
            (unsigned long long)census.hits,(unsigned long long)census.misses,(unsigned long long)census.insert_copies,
            (unsigned long long)census.oracle_copies,(unsigned long long)census.compared);
}
} // namespace linked_lookup
