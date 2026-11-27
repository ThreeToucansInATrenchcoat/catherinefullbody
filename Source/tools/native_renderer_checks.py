"""Check the default renderer's caches, shadow transforms and optional GPU kernels."""
from pathlib import Path
import argparse
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--gpu', action='store_true')
args = parser.parse_args()
manifest = json.loads((ROOT/'Source/build-manifest.json').read_text())
recipe = next(u['command'] for u in manifest['units'] if u['id'] == 182)
includes = [a for a in recipe if a.startswith('-I')]
source = ROOT/'Source/tests/native-renderer'
out = ROOT/'Build/native-renderer-checks'
out.mkdir(parents=True, exist_ok=True)
names = ['validate_shadow_quality', 'validate_state_values', 'validate_layout_memo',
         'validate_uniform_shapes', 'validate_record_scope', 'validate_force_snapshot']
if args.gpu:
    names.append('validate_kernel')
for name in names:
    exe = out/(name+'.exe')
    command = [manifest['linker'], '-std=c++20', '-O2', '-march=x86-64-v3',
               '-ffp-contract=off', '-fms-runtime-lib=static', '-DNOMINMAX',
               '-I'+str(ROOT/'Source/native-renderer'), *includes,
               '-ivfsoverlay', str(ROOT/'Source/native-renderer/selected-vfs.json'),
               str(source/(name+'.cpp')), '-o', str(exe)]
    if name == 'validate_kernel':
        vulkan = next(Path(a[2:]) for a in includes if (Path(a[2:])/'vulkan/vulkan.h').is_file())
        command.append(str(vulkan.parent/'Lib/vulkan-1.lib'))
    subprocess.run(command, cwd=ROOT, check=True)
    subprocess.run([str(exe)], cwd=ROOT, check=True, timeout=120)
print('Default native renderer checks passed')
