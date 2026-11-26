"""Fail early when CI's C++ library or Vulkan headers do not match the recipe."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
build = root / 'Build'
manifest = json.loads((root / 'Source/build-manifest.json').read_text())
compiler = manifest['linker'] if os.name == 'nt' else os.environ.get('CXX', 'clang++')
source = build / 'check-toolchain.cpp'
source.write_text('''#include <vector>
#if defined(_WIN32)
#include <windows.h>
#endif
#include <vulkan/vulkan.h>
static_assert(VK_HEADER_VERSION >= 363, "Vulkan headers 1.4.363 or newer required");
PFN_vkClearShaderInstrumentationMetricsARM check_extension = nullptr;
int main() { std::vector<int> values{1}; return values.front() - 1; }
''')
command = [compiler, '-std=c++23', '-fsyntax-only',
           '-I' + str(build / 'vulkan-headers/include'),
           '-I' + str(root / 'Source/imports/graphics/src'), str(source)]
if os.name == 'nt':
    command.insert(1, '-DVK_USE_PLATFORM_WIN32_KHR')
subprocess.run(command, cwd=root, check=True)
print('C++ library and Vulkan headers are compatible.')
