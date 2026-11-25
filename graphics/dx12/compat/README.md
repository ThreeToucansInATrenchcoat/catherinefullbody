# Mesa Dozen compatibility launcher

`compat/Start-Catherine-DX12.ps1` runs the Windows port on D3D12 using [Mesa Dozen](https://github.com/pal1000/mesa-dist-win/blob/master/readme.md). The port sends its existing Vulkan commands to Dozen, which translates and submits them to D3D12. This mode uses the port's current game and renderer code. The direct renderer and its replay tests are documented in the parent README.

```powershell
./compat/Start-Catherine-DX12.ps1 `
    -GameDirectory C:/catherine/recomp/native-vulkan-producer-test-build-2026-11-18 `
    -RunDirectory C:/catherine/recomp/dx12-test-profile
```

The first launch downloads the pinned 64-bit Mesa 26.3.0 build and verifies the archive and driver DLL checksums. Windows supplies the Vulkan loader, D3D12, curl and tar. The launcher sets the driver for its child process only. The private directory keeps its own executable, DLLs, settings, saves and shader caches and reads the existing asset pack by path.

The launcher selects the port's render-pass object path and applies its vertex clip-Z conversion for Dozen. The render-pass path uses the full pass memory barrier, which corrected visible corruption from attachment-scoped barriers. It supports single-sample rendering and FXAA. MSAA is disabled in this mode because the port's 4× resolve path faults on the tested Dozen driver.

Use `-GameExecutable` to select a separate qualified EXE, `-AssetPack` to point to a different local pack or `-PrepareOnly` to stage the profile without opening the game. Give a different build its own run directory. The profile's `catherine-dx12.log` should identify the GPU as `Microsoft Direct3D12 (<adapter name>)`.

On the RTX 5090, the launcher selected Direct3D12 and rejected a second launch into the same profile. Windowed tests reached playable Stage 1, Floor 1 and Stage 9, Floor 6. I inspected the 3840×2160 internal frames before and after resizing and verified the FXAA presentation pass on the boss scene. These checks used the isolated `pause-elapsed-candidate-1795188426360633700` executable, SHA-256 `50ddf9abcdc197bd05abfa380cad03ba763bdd262ed3ef5e551f22c16fe187f3`.
