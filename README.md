# Catherine Full Body

Run `Build/catherine.exe` from an existing local build. Current work includes the in-game F8 developer menu, direct level and cutscene loading, controller and mouse improvements, and package-first Vulkan shader setup.

This repository contains the active source, supplied interface artwork, installer tools and build recipes. Original game files, `assets.pak`, saves, local settings, compiled game executables, caches and historical experiments are excluded. Build recipes under `Build/` are tracked because the current build script reads them there.

- `Source/`: the canonical game source and selected headers. `Source/compile_commands.json` lists every compilation unit; `Source/build-manifest.json` defines the build. Historical directory names inside `Source/imports/` preserve relative includes, but these are the selected sources, not complete experiment trees.
- `Build/`: the only active game build/output directory, including assets, DLLs, save, settings, object files, logs and the current release ZIP.
- `Old/`: previous builds, release ZIPs, audit reports, original source trees and experiments. These are retained for reference and are not build inputs.
- `graphics/` and `base/assets/`: preserved DirectX 12 plugin/supporting graphics sources and shader asset data, left in place at your request. The obsolete Vulkan CMake build directory is archived as `Old/graphics-build/`.

## Build

From this directory, run `python build.py`. It checks source/header fingerprints and rebuilds changed units. Use `python build.py --rebuild` for a full compile, or `python build.py --check-dependencies` to verify the source closure. The installed Visual Studio LLVM toolchain, Windows SDK and Vulkan SDK remain external prerequisites.

The build configuration currently uses the original Windows workspace path, `C:/catherine/recomp`, and installed toolchain paths recorded in the manifest. Clone there to retain those paths. Python and the .NET 10 SDK are also required to build the embedded installer. Profile-guided release builds additionally require a locally trained `Build/perf/pgo/merged.profdata`; training profiles and compiled output are excluded from the repository.

The primary output is `Build/catherine.exe`. For simultaneous temporary tests, use `python build.py --output catherine1.exe` or `--output catherine2.exe`; remove those temporary EXEs when their tests finish. Do not create more build folders or dated source copies.

On a user's first launch, the installer verifies their extracted game files, builds `assets.pak`, then generates Vulkan shaders from that completed package. Missing shaders can be regenerated on a later launch without the extracted inputs. See `Source/resources/INSTALL-ASSETS.txt` for supported inputs and setup details.

Cleanup validation: all 263 compilation units compiled and linked from Source; their machine-code sections matched the previous release. Dependency resolution passed again after archival. The primary EXE, save and settings were preserved byte for byte.
