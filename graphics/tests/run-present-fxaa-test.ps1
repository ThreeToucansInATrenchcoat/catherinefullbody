param([string]$OutputDirectory=(Join-Path $env:TEMP 'catherine-fxaa-fixture'))
$ErrorActionPreference='Stop'
if (!$env:VULKAN_SDK) { throw 'Set VULKAN_SDK to your installed Vulkan SDK' }
$out=(New-Item -ItemType Directory -Force $OutputDirectory).FullName
& "$env:VULKAN_SDK/Bin/glslc.exe" -O --target-env=vulkan1.0 "$PSScriptRoot/present_fxaa_test.vert" -o "$out/fullscreen.spv"
if($LASTEXITCODE){throw 'Vertex shader compile failed'}
& clang++ -std=c++20 -O2 -fms-runtime-lib=static "-I$env:VULKAN_SDK/Include" "$PSScriptRoot/present_fxaa_test.cpp" "$env:VULKAN_SDK/Lib/vulkan-1.lib" -o "$out/present-fxaa-test.exe"
if($LASTEXITCODE){throw 'Fixture compile failed'}
& "$out/present-fxaa-test.exe" "$out/fullscreen.spv"
if($LASTEXITCODE){throw 'FXAA GPU fixture failed'}
