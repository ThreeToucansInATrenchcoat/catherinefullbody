# Catherine: Full Body PC Port

Based on the Switch version of the game, there was a shadps4 wrapper of this game available but that version is still locked to 30fps and is more of a no-frill type of recomp. I'm still going to thank and appreciate its existence though, because this was what got me interested in the game again.

I will say this has been a way more complicated port than anything else I've released so I'm less confident about this one.

## Features

- Native Windows and Linux support.
- 60 FPS by default, with an uncapped option. Code patches and interpolation enable higher frame rates, You'd be surprised just how many things broke when you go over 30 fps, most have been fixed but there may be minor things left that still run at the wrong speed.
- Full keyboard and mouse support.
- Button prompts switch automatically between keyboard and mouse icons and the active controller’s icons.
- Updated Menus, Xbox controllers and more now have updated tutorials.
- Real game configuration options that are inside of the game like a real PC game. Includes things like MSAA anti-aliasing and FMV smoothing.
- Local co-op. Online play had to be removed. Babel Pair and Colosseum automatically use local co-op and stay greyed out until two distinct input devices are connected and assigned.

## Screenshots

![The piano-key main menu](screenshots/01-piano-main-menu.webp)

<details>
<summary>Configuration and bindings (4K)</summary>

| | |
| --- | --- |
| ![Game](screenshots/config-game.webp)<br>Game | ![Display](screenshots/config-display.webp)<br>Display |
| ![Controls](screenshots/config-controls.webp)<br>Controls | ![System](screenshots/config-system.webp)<br>System |
| ![Audio](screenshots/config-audio.webp)<br>Audio | ![Player 1 bindings](screenshots/config-player-1-bindings.webp)<br>Player 1 bindings |
| ![Player 2 bindings](screenshots/config-player-2-bindings.webp)<br>Player 2 bindings |  |

</details>

<details>
<summary>Ten puzzle floors on Linux with live FPS</summary>

| | |
| --- | --- |
| ![Stage 1, Floor 1](screenshots/puzzle-stage-01-floor-01.webp)<br>Stage 1, Floor 1 | ![Stage 2, Floor 2](screenshots/puzzle-stage-02-floor-02.webp)<br>Stage 2, Floor 2 |
| ![Stage 2, Floor 3](screenshots/puzzle-stage-02-floor-03.webp)<br>Stage 2, Floor 3 | ![Stage 3, Floor 3](screenshots/puzzle-stage-03-floor-03.webp)<br>Stage 3, Floor 3 |
| ![Stage 5, Floor 1](screenshots/puzzle-stage-05-floor-01.webp)<br>Stage 5, Floor 1 | ![Stage 7, Floor 1](screenshots/puzzle-stage-07-floor-01.webp)<br>Stage 7, Floor 1 |
| ![Stage 7, Floor 2](screenshots/puzzle-stage-07-floor-02.webp)<br>Stage 7, Floor 2 | ![Stage 8, Floor 1](screenshots/puzzle-stage-08-floor-01.webp)<br>Stage 8, Floor 1 |
| ![Stage 9, Floor 3](screenshots/puzzle-stage-09-floor-03.webp)<br>Stage 9, Floor 3 | ![Stage 9, Floor 5](screenshots/puzzle-stage-09-floor-05.webp)<br>Stage 9, Floor 5 |

</details>

## Installation

1. Use your own fully decrypted, extracted copy of the North American or European Nintendo Switch release (title ID `0100BF00112C0000`) and its 1.0.1 update. You can use a tool such as NSPUnpacker to prepare the files. The port accepts extracted files only.
2. On Windows, launch `Catherine.exe` and point setup at the extracted files when prompted. Setup builds `assets.pak`, then generates the Vulkan shaders. See the [Windows setup guide](Source/resources/INSTALL-ASSETS.txt) for details.
3. On Linux, launch `./Build/catherine` and select the extracted game and update folders when prompted. The built-in installer creates `assets.pak` and then generates the Vulkan shaders. First-time setup uses SPIRV-Tools and KDialog or Zenity. Building the executable requires the .NET 10 SDK.

## Compile from source

Clone this repository and run the build commands from the checkout root. Original game files are only needed when setting up the built game, not when compiling it.

### Windows

Install:

- Python 3 and the .NET 10 SDK.
- Visual Studio with the **Desktop development with C++** workload, LLVM/Clang tools, and the Windows SDK.
- The Vulkan SDK, including its headers.

The Windows build recipe currently uses absolute paths. Place the checkout at `C:\catherine\recomp`, then configure [Source/build-manifest.json](Source/build-manifest.json) for your installed tools:

- Set `compiler`, `linker`, and `resource.compiler` to your `clang.exe`, `clang++.exe`, and `llvm-rc.exe` paths.
- Update the compiler path at the start of each compilation unit's `command` array to match those tools.
- Replace the Vulkan SDK include paths in those arrays with `-I` followed by your installed SDK's `Include` directory, which must contain `vulkan/vulkan.h`.

The recorded SDK paths are build-machine settings, not additional projects or folders you need to download. Keeping the checkout at the path above preserves the remaining project paths in the build and resource recipes.

From the checkout root in a Visual Studio Developer PowerShell:

```powershell
New-Item -ItemType Directory -Force Build\obj | Out-Null
python build.py --jobs 12
```

The executable is `Build\catherine.exe`. Build logs are written to `Build\logs`.

### Linux

Use an x86-64-v3-capable Linux system. Install Python 3, Clang, LLD, the .NET 10 SDK, pkg-config, and development files for Vulkan, SDL2, Cairo, OpenSSL, and FFmpeg (`libavcodec`, `libavutil`, and `libswscale`). The checkout can be in any directory; the Linux build script translates the recorded Windows project paths automatically.

```sh
python3 build.py --jobs 12
```

The executable is `Build/catherine`. Launch it with:

```sh
./Build/catherine
```

If your Vulkan headers are outside the compiler's default include path, set `CATHERINE_VULKAN_INCLUDE` to the directory containing the `vulkan` subdirectory. `CC` and `CXX` can select alternative Clang executables. Build logs are written to `Build/logs/linux`.

For first-time game setup on Linux, also install SPIRV-Tools (`spirv-as`, `spirv-dis`, and `spirv-val`) and either KDialog or Zenity. Linux builds currently do not support `--pgo`.

## Planned

- Further optimisation.
- A custom level editor.
- More configuration options like FSR and DLSS.
- Restore the voting system. Other online features are outside my plans, but forks and pull requests are welcome.

## AI

Yes. I used AI.
