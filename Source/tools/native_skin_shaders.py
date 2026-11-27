from pathlib import Path
import struct, subprocess, re, argparse
R=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser();parser.add_argument('--write',action='store_true');args=parser.parse_args()
(R/'Build/native-shaders').mkdir(parents=True,exist_ok=True)
arrays=['#pragma once\n#include <cstdint>\n']
for tangent,name in [(0,'regular'),(1,'tangent')]:
    out=R/'Build/native-shaders'/f'native_skin_{name}.spv'
    subprocess.run(['glslc','--target-env=vulkan1.2','-O',f'-DTANGENT={tangent}',str(R/'Source/native-renderer/native_skin.comp'),'-o',str(out)],check=True)
    # Select signed-zero preservation and nearest-even rounding explicitly.
    # The shader implements FTZ/DAZ bitwise, including GPUs without FTZ controls.
    # https://registry.khronos.org/SPIR-V/specs/unified1/SPIRV.html
    assembly=out.with_suffix('.spvasm')
    subprocess.run(['spirv-dis',str(out),'-o',str(assembly)],check=True)
    source=assembly.read_text();entry=re.search(r'OpEntryPoint GLCompute (\S+)',source).group(1)
    source=source.replace('OpCapability Shader','OpCapability Shader\n               OpCapability SignedZeroInfNanPreserve\n               OpCapability RoundingModeRTE',1)
    line=re.search(r'^.*OpExecutionMode '+re.escape(entry)+r' LocalSize.*$',source,re.M).group(0)
    source=source.replace(line,line+''.join('\n               OpExecutionMode '+entry+' '+mode+' 32' for mode in ['SignedZeroInfNanPreserve','RoundingModeRTE']),1)
    assembly.write_text(source)
    subprocess.run(['spirv-as','--target-env','vulkan1.2',str(assembly),'-o',str(out)],check=True)
    subprocess.run(['spirv-val','--target-env','vulkan1.2',str(out)],check=True)
    data=out.read_bytes();words=struct.unpack('<'+'I'*(len(data)//4),data)
    arrays.append('inline constexpr uint32_t native_skin_'+name+'[]={\n'+',\n'.join(','.join(f'0x{x:08x}'for x in words[i:i+8])for i in range(0,len(words),8))+'\n};\n')
header=R/'Source/native-renderer/native_skin_spirv.h'
result=''.join(arrays)
if args.write: header.write_text(result)
else: assert header.read_text()==result, 'Generated shader differs; inspect it before using --write'
print('Native regular/tangent SPIR-V kernels compiled, validated and matched' if not args.write else 'Updated native regular/tangent SPIR-V kernels')
