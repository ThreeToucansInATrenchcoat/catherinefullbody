"""Reproduce the c3-model/c4-projection shader allowlist from local SPIR-V."""
from pathlib import Path
import argparse,concurrent.futures,subprocess

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--spirv-dir',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    def classify(path):
        result=subprocess.run(['spirv-cross',str(path),'--version','450'],capture_output=True,text=True,check=True)
        if all(token in result.stdout for token in ('gl_Position.x','vp_c3_1._m0[3].w','vp_c4_1._m0[3].w')):
            return path.stem
    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        hashes=sorted(filter(None,pool.map(classify,args.spirv_dir.glob('*.spv'))))
    header='#pragma once\n#include <array>\n#include <string_view>\n// Audited from the shipped vertex programs: c3 is the 4x4 model-to-world\n// matrix; c4 projects that world position. UI-only transform programs are absent.\nnamespace catherine::visual {\ninline constexpr std::array<std::string_view, '+str(len(hashes))+'> world_model_shaders={\n'
    header+=''.join(' "'+value+'",\n' for value in hashes)+'};\n}\n'
    args.output.write_text(header)
    print(f'Classified {len(hashes)} model-to-world vertex programs')

if __name__=='__main__':main()
