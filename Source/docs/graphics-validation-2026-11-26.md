# Graphics validation — 26 November 2026

Implemented: FSR 1 EASU/RCAS, optional FPS overlay, and NVIDIA DLSS Frame Generation through Streamline 2.14.16 on Windows. DLSS Super Resolution is not implemented.

## Integration

- Custom-engine project ID: `3b76a2ff-7158-405d-be29-e264e920418a`. Production DLLs and original licenses are pinned in `Source/vendor/streamline/`; the interposer signature is verified before loading.
- Vulkan uses Streamline proxy dispatch, including its swapchain. Proxy backbuffers finish in transfer-source layout; ordinary Vulkan swapchains finish in present layout. Reflex's present-ID dependency is enabled.
- The game's perspective vertex CB1 supplies real view/projection matrices. Square shadow targets and invalid/non-perspective matrices are rejected.
- Depth is copied while the native attachment borrow is valid. A dense current-to-previous motion field is estimated on the GPU from consecutive pre-HUD images, seeded by camera reprojection, with RGB block matching for moving objects. It is not a native engine velocity pass.
- The UI vertex shader `266d0aa411f67ef5` marks the pre-HUD capture boundary. That capture was visually checked. It receives the same display filtering as the final image before tagging, including FSR and letterboxing.
- Tagged inputs are GPU-owned and remain alive through presentation. Tokens travel with each present job. Loading, missing inputs, and discontinuities clear/reset history; ordinary presentation continues.
- INI requests remain separate from device support. GPU maximums clamp the requested multiplier. Missing runtime/plugin and the simulated unsupported-adapter path fall back to normal rendering. Unsupported plugins are unloaded before device creation.
- The counter uses `numFramesActuallyPresented`, not the requested multiplier. Frame generation defaults off; the counter defaults on. INI edits require a restart.

## Evidence

Host: RTX 5090 32 GB, NVIDIA driver 621.01, Windows 11 build 22631. Tests used private saves/configuration and the installed assets archive. The public save was preserved.

| Check | Result |
| --- | --- |
| Normal and PGO/ThinLTO builds | PASS |
| Settings round-trip, invalid values, all four FSR scale calculations | PASS |
| Camera extraction, inverse matrices, near/far/FOV, invalid data | PASS |
| Embedded shaders compiled and validated | PASS |
| FSR Quality: 1280×720 to 1920×1080 EASU/RCAS | PASS |
| FPS counter on/off captures | PASS |
| Repeated actual 2× / 3× / 4× presents in the final boss | PASS |
| 4× with a 120 FPS render cap | PASS |
| FSR plus 4× with counter hidden | PASS |
| Live final-boss → Stage 1 DEV load; generation resumes | PASS |
| Missing runtime, missing FG plugin, simulated unsupported adapter | PASS |
| Signed production runtime beside the optimized executable | PASS |
| Production capture: 240.3 presented FPS / 60.1 rendered FPS | PASS |
| Frame-pacing percentiles and other physical GPUs | NOT RUN |

GPU input readback contained finite, nonconstant depth and motion. A captured moving frame's RGB error fell from approximately 0.0370 to 0.0210 after motion compensation over moving pixels. Generated-only development output and normal production output were visually inspected with the HUD visible.

The Vulkan validation pass fixed application depth-image usage, the Reflex extension dependency, and proxy-backbuffer layouts. Remaining reports name SDK-owned generated-output clones, pacer semaphores, and concurrent SDK latency/presentation operations. Complete SDK validation is therefore PARTIAL; these reports are not asserted harmless. Production SDK status stayed zero and repeated presents/captures succeeded.

## Limits

Frame generation is experimental and currently Windows-only. Estimated motion can produce artifacts around occlusions, transparency, and fast motion; one GPU and a limited set of scenes were checked. SDK focus/pacing rules can reduce the effective multiplier. Video playback continues through the existing movie interpolation path.

Local evidence is under `Build/dlssg-test/` and `Build/upscale-fps-test/`; these contain no redistributed game data in source control.
