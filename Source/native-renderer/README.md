# Default native renderer

The regular `python build.py` recipe includes the selected native renderer. No
separate experimental checkout, launcher environment variables, or cached game
objects are required. GPU skinning defaults to enabled; `NativeGpuSkinning=false`
selects the CPU fallback. The source also includes automatic/manual shadow quality,
internal resolution, VSync/triple buffering, extracted asset-folder support, the
direct-load pause mouse fix, and the high-refresh sheep-push fix.

`selected-vfs.json` preserves the compiler's original virtual file identities so
relative includes and profile matching remain unchanged. Names containing
`C:/catherine/native-gpu-experiment/Source` are virtual aliases: their backing files
are all here in the repository. Do not replace them with a dependency on a sibling
checkout. Each translation unit's physical dependency closure is recorded in
`Source/build-manifest.json` for incremental rebuilds.

Windows uses the selected O3 + ThinLTO profile (`release.profdata`) by default.
The profile SHA-256 is recorded in the build manifest. The embedded installer uses
.NET SDK 10.0.203, pinned by `global.json`. LLVM 22.1.3 produced the accepted Windows
build. LLVM can reorder equivalent PHI inputs/code during optimization, so matching
source and settings do not guarantee a byte-identical executable. The timestamp is
pinned to the reference release to remove timestamp-only differences.

Linux uses the same sources with O3 + ThinLTO, without the Windows compiler profile.
The build creates local aliases for the preserved virtual include identities.

## Linux profile-guided builds

Build a training executable with the same Clang version used for the final build:

```sh
CATHERINE_PGO_TRAIN=1 python3 build.py --output catherine1 --jobs 12
LLVM_PROFILE_FILE="$PWD/Build/pgo-linux/%p.profraw" ./Build/catherine1
```

Play representative scenes and close the game normally so the training build writes its profile. More runs can cover additional scenes. Merge the captures and build:

```sh
llvm-profdata merge -o Build/pgo-linux/merged.profdata Build/pgo-linux/*.profraw
python3 build.py --pgo --jobs 12
```

Checks:

```powershell
python build.py --check-dependencies
python build.py --rebuild
python Source/tools/release_input_checks.py
python Source/tools/native_renderer_checks.py --gpu
python Source/tools/native_skin_shaders.py
python Source/tools/test_asset_folder.py
```

The GPU check requires Vulkan hardware; the shader check requires `glslc` and the
SPIR-V tools. Shader regeneration is explicit: `native_skin_shaders.py --write`.
The normal build consumes the checked-in, validated SPIR-V header.
