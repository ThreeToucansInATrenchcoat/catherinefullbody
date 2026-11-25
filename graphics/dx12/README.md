# Native Catherine Direct3D 12

The module records native Direct3D 12 commands and presents through DXGI. It builds with the Windows SDK. The replay test uses the port's translated shaders, and checks at runtime that the Vulkan loader stayed unloaded.

## Build and verify

Run from `graphics/dx12` in an x64 Visual Studio Developer PowerShell. Use CMake 3.24 or later. Enable the Windows Graphics Tools feature to run tests with the D3D12 debug layer. Adapter selection uses DXGI 1.6, available in desktop Windows 10 version 1803 and later. [DXGI requirements](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_6/nn-dxgi1_6-idxgifactory6)

```powershell
cmake -S . -B build-dx12 -A x64
cmake --build build-dx12 --config Release
ctest --test-dir build-dx12 -C Release --output-on-failure
```

To run the Catherine shader replay tests, use Python 3.11 or later, SPIRV-Cross and DXC to translate the port's local SPIR-V cache. The converter uses a new output directory. Its manifest records the source checksum and active D3D12 register ranges for each shader. [SPIRV-Cross HLSL backend](https://github.com/KhronosGroup/SPIRV-Cross#hlsl---sm-51)

```powershell
$spirv = 'C:/catherine/recomp/base/assets/spirv'
$dxil = Join-Path $PWD 'dx12-local-shaders'
python tools/translate_shaders.py --source $spirv --out $dxil --triangle-vertex-clip nvn
cmake -S . -B build-dx12 -A x64 "-DCATHERINE_DX12_REPLAY_SHADERS=$dxil" "-DCATHERINE_DX12_REPLAY_SPIRV=$spirv"
cmake --build build-dx12 --config Release
ctest --test-dir build-dx12 -C Release --output-on-failure
```

The two replay tests use the first recorded boot-pass shader pair and four morph shader variants. The graphics test creates NVN images, untile-uploads a guest texture, applies NVN pipeline and sampler state, executes the draw, and checks the final RGBA pixels. It also checks 4× and 8× color resolves where the device reports support. The compute test compares all output bytes and checks that untouched lanes survive. The optional SPIR-V path verifies how active DXIL descriptors map back to the original NVN resource slots.

## Renderer interfaces

- `Renderer` owns the D3D12 device, queue, frame fences, window swap chain, and offscreen targets. `NativeReplay` records direct and indexed draws, compute dispatches, clears, buffer transfers, texture uploads, color resolves, and presentation for a frame.
- Use a single `Resource` wrapper per native allocation on the replay queue. Keep the wrapper alive until submission or cancellation. The recorder retains GPU objects and descriptor and upload pages until their frame slot retires. It commits tracked resource states when a list executes and discards pending states when a recording is canceled.
- Pass CPU descriptors and each referenced resource's required state with a draw or dispatch. The replay copies descriptors into the shader-visible frame heaps. The application resolves guest addresses and handles to the storage and views used by these commands.
- The NVN conversion code maps supported texture and vertex formats, indexed and direct draw arguments, fixed-function state, and descriptor registers. It reuses the port's CPU-only Maxwell tiling, ASTC decoding, and SPIR-V reflection. Those files sit beside the module under `graphics/src/nvn`. The shader conversion uses triangle clip-space semantics.

## Game integration status

The native executor runs outside the game today. `Catherine.exe` still routes its NVN command lists and window presents through `gpu_submit` and `gpu_present` in the Vulkan backend. The direct game path needs a dispatcher that covers the recorded command stream, resolves guest memory and texture incarnations, and connects queue waits and GPU writeback to the native queue. Whole-frame gameplay validation follows that integration.

Unsupported paths currently include translated line loops, triangle fans and quads, tiled 3D texture uploads, and depth/stencil resolves. The 8× test verifies native color resolve. Gameplay MSAA also needs the port's depth and stencil resolve and pass sequencing.
