"""Configure the recorded Windows recipe on a disposable GitHub runner."""
import argparse
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('--clang', required=True)
args = p.parse_args()
clang = Path(args.clang)
assert clang.is_file()
manifest_path = root / 'Source/build-manifest.json'
manifest = json.loads(manifest_path.read_text())
manifest['compiler'] = clang.as_posix()
manifest['linker'] = clang.with_name('clang++.exe').as_posix()
manifest['resource']['compiler'] = clang.with_name('llvm-rc.exe').as_posix()
headers = (root / 'Build/vulkan-headers/include').as_posix()
assert Path(headers, 'vulkan/vulkan.h').is_file()
for unit in manifest['units']:
    old = unit['command'][0]
    unit['command'][0] = clang.with_name('clang++.exe' if '++' in old else 'clang.exe').as_posix()
    unit['command'] = [a.replace('-IC:/recomp/wetrix_dc/VulkanSDK/Include', '-I' + headers) for a in unit['command']]
(root / 'Build/obj').mkdir(parents=True, exist_ok=True)
manifest_path.write_text(json.dumps(manifest, indent=2))
