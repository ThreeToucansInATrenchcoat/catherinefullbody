"""Exercise the real NVN upload helper with pointer-cached guest reads.

Run with Python and clang++ available. The function is extracted afresh from
nvn_sets.cpp, so this fixture cannot silently keep testing an old copy.
"""
from pathlib import Path
import re
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parents[1] / 'src/nvn/nvn_sets.cpp'


def main():
    source = SOURCE.read_text()
    signature = 'VkDescriptorBufferInfo buffer_of('
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    function = source[start:end]
    # These are the two production call sites that pass interpolated scratch.
    for expression in (
        r'buffer_of\(cb,\s*p,\s*size,\s*padding,\s*false,\s*p\s*==\s*native_bytes\)',
        r'buffer_of\(cb,\s*p,\s*size,\s*0x10000,\s*written,\s*p\s*==\s*native_bytes\)',
    ):
        assert re.search(expression, source), 'Mutable interpolation upload lost its host/guest policy'

    preamble = r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <vector>
using VkCommandBuffer = uint64_t;
struct VkDescriptorBufferInfo {uint64_t buffer,offset,range;};
struct Upload {uint64_t buffer,offset;uint8_t* ptr;};
struct WriteBack {uint8_t* from;uint8_t* to;uint64_t size;};
static std::deque<std::vector<uint8_t>> allocations;
static std::unordered_map<const uint8_t*,Upload> guest_copies;
static std::vector<WriteBack> g_write_backs;
static unsigned guest_reads=0,written_reads=0;
static constexpr int kSlowUploads=1;
static bool draw_slow(int){return false;}
static bool written_separate(){return false;}
static Upload upload_alloc(uint64_t size,uint64_t alignment){
 assert(alignment==256);allocations.emplace_back(size,0xcc);
 return {1,uint64_t(allocations.size())*65536,allocations.back().data()};
}
static Upload read_upload(VkCommandBuffer,const uint8_t* p,uint64_t size,uint64_t padded,uint64_t align){
 ++guest_reads;
 if(auto it=guest_copies.find(p);it!=guest_copies.end())return it->second;
 auto u=upload_alloc(padded,align);std::memset(u.ptr,0,padded);
 if(p&&size)std::memcpy(u.ptr,p,size);guest_copies[p]=u;return u;
}
static Upload written_bind(VkCommandBuffer,const uint8_t* p,uint64_t size,uint64_t padded){
 ++written_reads;auto u=upload_alloc(padded,256);std::memset(u.ptr,0,padded);
 std::memcpy(u.ptr,p,size);return u;
}
static const std::vector<uint8_t>& stored(VkDescriptorBufferInfo b){
 assert(b.buffer==1&&b.offset%65536==0);return allocations.at(b.offset/65536-1);
}
static void contents(VkDescriptorBufferInfo b,size_t valid,uint8_t expected){
 const auto& a=stored(b);assert(a.size()==b.range);
 for(size_t i=0;i<valid;i++)assert(a[i]==expected);
 for(size_t i=valid;i<a.size();i++)assert(a[i]==0);
}
'''
    test = r'''
int main(){
 uint8_t native[64]={},scratch[64];
 std::memset(scratch,0x11,sizeof scratch);
 const uint8_t* p=scratch;const uint8_t* native_bytes=native;
 auto first=buffer_of(0,p,37,64,false,p==native_bytes);
 std::memset(scratch,0x77,sizeof scratch);
 auto second=buffer_of(0,p,37,64,false,p==native_bytes);
 assert(first.offset!=second.offset);contents(first,37,0x11);contents(second,37,0x77);
 assert(guest_reads==0&&written_reads==0&&g_write_backs.empty());
 // A full bone palette uses the storage-buffer padding and the same policy.
 std::vector<uint8_t> palette(103*64,0x44);
 p=palette.data();
 auto old_pose=buffer_of(0,p,palette.size(),0x10000,false,p==native_bytes);
 std::fill(palette.begin(),palette.end(),0x66);
 auto new_pose=buffer_of(0,p,palette.size(),0x10000,false,p==native_bytes);
 assert(old_pose.offset!=new_pose.offset);
 contents(old_pose,palette.size(),0x44);contents(new_pose,palette.size(),0x66);
 assert(guest_reads==0&&written_reads==0&&g_write_backs.empty());
 // Immutable native data still takes the existing shared-copy path.
 std::memset(native,0x22,sizeof native);p=native;
 auto guest1=buffer_of(0,p,32,64,false,p==native_bytes);
 auto guest2=buffer_of(0,p,32,64,false,p==native_bytes);
 assert(guest1.offset==guest2.offset&&guest_reads==2);contents(guest2,32,0x22);
 // Demonstrate the exact aliasing hazard if mutable scratch is marked guest:
 // pointer reuse returns its first snapshot, even after the source is changed.
 std::memset(native,0x99,sizeof native);
 auto stale=buffer_of(0,p,32,64,false,true);
 assert(stale.offset==guest1.offset);contents(stale,32,0x22);
 assert(written_reads==0&&g_write_backs.empty());
 puts("PASS: actual buffer_of copies changing host scratch and bone palettes at the same address; earlier uploads/padding remain intact; native guest reuse is retained; no host write-back; both production interpolation call policies guarded.");
}
'''
    with tempfile.TemporaryDirectory(prefix='catherine-upload-lifetime-') as directory:
        directory = Path(directory)
        cpp = directory / 'upload_lifetime.cpp'
        exe = directory / 'upload_lifetime.exe'
        cpp.write_text(preamble + function + test)
        subprocess.run(['clang++', '-std=c++20', '-O2', str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
