"""Compile the actual dispatch-provenance API with small replay-state mocks."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = (ROOT / 'src/nvn/nvn_written.cpp').read_text()
    functions = []
    for signature in ('void written_pose_dispatch_begin(',
                      'void written_pose_palette_interpolated(',
                      'void written_pose_dispatch_complete(',
                      'bool written_skin_output('):
        start = source.index(signature)
        end = source.index('{', start) + 1
        depth = 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        functions.append(source[start:end])
    preamble = r'''
#include <array>
#include <atomic>
#include <cassert>
#include <cstring>
#include <string>
#include <vector>
#include <cstdio>
#include "HEADER"
using nx::nvn::replay::InterpolatedSkinOutputs;
using nx::nvn::replay::VerifiedSkinOutputs;
namespace catherine::visual {uint64_t pose_replay_frame(){return 1;}}
enum class NvnSource { StorageBuffer };
struct DescriptorBinding {
 uint32_t set=1,binding=2;
 struct {NvnSource source=NvnSource::StorageBuffer;int index=2;} nvn;
};
struct ShaderInfo {
 std::string hash;
 struct {std::vector<DescriptorBinding> bindings;} refl;
 uint64_t writes=4;
};
struct Range {uint64_t address=0,size=0;};
struct {std::array<std::array<Range,18>,6> uniforms,storage;const ShaderInfo* compute_shader=nullptr;} g_state;
std::atomic<uint64_t> g_frame{1};
InterpolatedSkinOutputs g_interpolated_skin_outputs;
VerifiedSkinOutputs g_verified_skin_outputs;
bool g_dispatch_palette_interpolated=false,g_dispatch_world_morph_base=false;
const uint8_t* blocked=nullptr;
const uint8_t* guest_bytes(uint64_t p){return reinterpret_cast<const uint8_t*>(p);}
const uint8_t* cpu_view(const uint8_t* p,uint64_t){return p==blocked?nullptr:p;}
bool written_overlaps(const uint8_t* p,uint64_t){return p==blocked;}
const uint8_t* skin_readable_view(const uint8_t* p,uint64_t size){return cpu_view(p,size);}
uint64_t written_storage_bindings(const ShaderInfo* s){return s->writes;}
bool vincent_skin_shader(const ShaderInfo& s){return s.hash=="eb49fe48c6f84ebf"||s.hash=="01c7cddd0aab2b5c";}
'''.replace('HEADER', (ROOT / 'src/nvn/interpolated-skin-output.h').as_posix())
    tests = r'''
int main(){
 std::array<uint8_t,2048> allocation{};
 auto* p=allocation.data(); uint32_t count=4;
 g_state.uniforms[5][0]={reinterpret_cast<uint64_t>(&count),4};
 ShaderInfo skin{"eb49fe48c6f84ebf",{{DescriptorBinding{}}},4};
 auto destination=[&](unsigned slot,uint8_t* begin,uint64_t size){g_state.storage[5][slot]={reinterpret_cast<uint64_t>(begin),size};};
 destination(2,p,1024);
 written_pose_dispatch_begin();written_pose_palette_interpolated();
 written_pose_dispatch_complete(skin,1,false);
 assert(written_skin_output(p,12)&&written_skin_output(p+96,12));
 assert(!written_skin_output(p+128,12)); // bound tail is not produced
 // A native/local palette must reset the flag and replace just its prefix.
 destination(2,p+512,1024);
 written_pose_dispatch_begin();written_pose_dispatch_complete(skin,1,false);
 assert(!written_skin_output(p+512,12)&&written_skin_output(p,12));
 // A substituted CPU attempt followed by GPU fallback still tags the output.
 written_pose_dispatch_begin();written_pose_palette_interpolated();
 written_pose_dispatch_complete(skin,1,false);
 assert(written_skin_output(p+512,12));
 // A local morph overwrites the middle, preserving adjacent skin objects.
 ShaderInfo morph{"9ae85fe23552c4e8",{{DescriptorBinding{}}},4};
 morph.refl.bindings[0].nvn.index=1;
 destination(1,p+48,1024);
 written_pose_dispatch_begin();written_pose_dispatch_complete(morph,1,false);
 assert(written_skin_output(p,12)&&!written_skin_output(p+48,12));
 assert(written_skin_output(p+512,12)); // its oversized binding tail is untouched
 // Tangent output has a 48-byte stride, with the same exact prefix rule.
 ShaderInfo tangent=skin;tangent.hash="01c7cddd0aab2b5c";
 destination(2,p+768,1024);
 written_pose_dispatch_begin();written_pose_palette_interpolated();
 written_pose_dispatch_complete(tangent,1,false);
 assert(written_skin_output(p+768+144,12)&&!written_skin_output(p+768+192,12));
 // Native overwrite clears only its actual positions, not another bound tail.
 destination(2,p+512,1024);
 written_pose_dispatch_begin();written_pose_dispatch_complete(skin,1,false);
 assert(!written_skin_output(p+512,12)&&written_skin_output(p+768,12));
 // If the count is GPU-only, do not guess the produced prefix.
 blocked=reinterpret_cast<const uint8_t*>(&count);
 written_pose_dispatch_begin();written_pose_palette_interpolated();
 written_pose_dispatch_complete(skin,1,false);
 assert(!written_skin_output(p+512,12)&&!written_skin_output(p+768,12));
 blocked=nullptr;
 written_pose_dispatch_begin();written_pose_palette_interpolated();
 written_pose_dispatch_complete(skin,1,true);
 assert(!written_skin_output(p+512,12));
 // Older, uncommitted submits cannot certify reused guest addresses.
 written_pose_dispatch_begin();written_pose_palette_interpolated();
 written_pose_dispatch_complete(skin,1,false);
 assert(written_skin_output(p+512,12));g_frame=2;
 g_verified_skin_outputs.begin_submit();
 assert(!written_skin_output(p+512,12));
 // Capture an in-place morph's world base BEFORE its position write. Its
 // additive output must retain world coordinates without any palette hook.
 destination(2,p,1024);g_state.compute_shader=&skin;
 written_pose_dispatch_begin();written_pose_palette_interpolated();
 written_pose_dispatch_complete(skin,1,false);
 destination(1,p,1024);g_state.compute_shader=&morph;
 written_pose_dispatch_begin();p[0]=7; // mock successful morph updated its base
 written_pose_dispatch_complete(morph,1,false);
 assert(written_skin_output(p,12)&&written_skin_output(p+96,12));
 // A native/local morph with unrelated input stays local.
 destination(1,p+1024,1024);
 written_pose_dispatch_begin();written_pose_dispatch_complete(morph,1,false);
 assert(!written_skin_output(p+1024,12));
 puts("PASS: actual dispatch API tags substituted CPU/GPU skin prefixes only; native/local and morph reuse remain eligible; bounds/tangent stride/count uncertainty, persistent lifetime and predispatch world-morph propagation verified.");
}
'''
    with tempfile.TemporaryDirectory(prefix='catherine-skin-dispatch-') as directory:
        directory = Path(directory)
        cpp, exe = directory / 'test.cpp', directory / 'test.exe'
        cpp.write_text(preamble + '\n'.join(functions) + tests)
        subprocess.run(['clang++', '-std=c++20', '-O2', str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
