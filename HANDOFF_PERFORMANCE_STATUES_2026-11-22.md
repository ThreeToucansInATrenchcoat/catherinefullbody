# Handoff: boss performance, eye and statues — 22 November 2026

Work is paused at the user's request. The user is called **Emperor**. Read `C:/catherine/recomp/AGENTS.md` first. Do not resume tests or optimization until asked. Graphics migration and the wider timing audit remain parked.

## Most important finding: the apparent launch-speed difference was a mismatched difficulty

The historical harness explicitly chooses **Normal** for Stage 9 / Floor 6. The user was leaving Difficulty on **Keep earned state**. Once the automated test used that same selection, it also ran at **202.7 FPS**, matching the user's approximately **200–202 FPS** manual launch of the exact same executable.

The experiment changed the difficulty override while retaining the same executable and checkpoint destination. Do not describe the remaining ~200 FPS workload as an unexplained manual-launch penalty. The earlier ~250 FPS benchmark is a different difficulty state. The precise inherited numeric difficulty was not read back before pausing; do not guess whether it is Easy or Safety.

Short observations using candidate SHA `0e6d4d17…`:

| Selection / launch | Observation | Evidence under `Build/perf/runs/` |
| --- | --- | --- |
| Normal, automated | 249.0 FPS titlebar | `20261122-204847-boss-shared-statue-pool-final` |
| Normal, automated, inherited compatibility layer removed | 239.0 FPS titlebar | `20261122-205132-boss-launch-without-compat-layer` |
| Hard, automated | 319.0 FPS titlebar | `20261122-205828-boss-hard-difficulty-match` |
| Keep earned state, automated | **202.7 FPS** titlebar; final log ~205.6 FPS over 60 frames | `20261122-205931-boss-earned-difficulty-match` |
| Same candidate, manually double-clicked | User reported **200 FPS** | Manual PID 34320; launch details in `Build/logs/manual-catherine1-launch.json` |

These are **short diagnostic observations**, not new 30-second averages or 1% lows. Normal/Hard/Earned should be compared again with explicitly matched settings if precise differences are needed. The user toggled MSAA during investigation; current `AntiAliasing=0` is their choice. Do not reset it silently.

The earlier lower 135 FPS was also real: producer memory-validation work dominated its recording thread. A shipped guarded-reader fix improved the user's normal launch to ~196 FPS. That improvement and the difficulty mismatch are separate findings.

Other checked differences:

- Both test and manual rendering were verified at **3840×2160 internal resolution**. A 3072×1728 swapchain/window does not imply a lower internal render resolution. Manual PID 34320 had `g_render_height=2160`, `g_pass_extent=(3840,2160)`.
- Earlier both launches logged 8× MSAA. The user says disabling/changing MSAA barely affects FPS, consistent with the CPU limitation.
- Alt-Tab away without pausing did not raise the user's FPS.
- Manually launching the exact `catherine1.exe` used by the harness still gave ~200 FPS, ruling out a filename/build difference in that comparison.
- Normal controller polling was separately tested and was not enough to explain the gap. Temporary input instrumentation was reverted.
- User process priority was Normal (`32`), affinity `0xffffffff`, NtGlobalFlag=0, no enabled process power-throttling mask. Its renderer defaults matched Claude's defaults, including `DISABLE_VULKAN_OBS_CAPTURE=1`.
- FPS Monitor still loads; that was intentionally preserved. No system driver, registry, overlay or graphics-quality settings were changed.

## User's current priorities and final request

Actual inherited-state boss workload still falls short of **240 FPS minimum / 300 FPS target**. It gets faster higher in the tower and slower near the bottom. Do not mistake a different stage position, pause menu or difficulty for an optimization result.

The user asked **“use my save?”**. This has **not yet been completed through the ordinary Load menu**. The final 202.7 FPS experiment still restores the bundled DEV boss checkpoint, now with Keep earned state instead of Normal. Keep earned state in DEV preserves the restored checkpoint's state; it is not proof that the user's own save was used. Next work should inspect/load the user's actual save and match their real load route when relevant. Do not overwrite it or fabricate a boss state by editing stage numbers.

The user's last suggestion is **early visibility/frustum culling**: avoid preparing/drawing objects outside the view. This is a priority candidate for the next optimization investigation, especially before CPU skinning, C3 transform uploads and draw recording. Climbing-related speedup is consistent with excess work but is not proof of off-screen drawing: stage unloading, boss coverage, shadows and effects also change. Count actual object preparations/submissions versus conservative visible bounds before claiming a cause.

Culling must preserve the same image: off-camera shadow casters/reflections/effects can affect visible pixels. Cull render preparation, not game simulation or boss logic. Use conservative animated/interpolated bounds and reset/revalidate interpolation history on visibility gaps so reappearing copies do not borrow stale poses. No culling implementation was added in this session.

## Exact delivered and pending builds

**Current main/release:**

- `C:/catherine/recomp/Build/Catherine.exe`
- `C:/catherine/recomp/Build/Catherine-Release.zip`
- SHA256: `b13e430a30b329b0f79abd6e7385819ca2eb845182aca668eed05d41853554fd`
- PGO + ThinLTO, retaining Claude's renderer defaults and OBS exclusion.
- Includes the eye fix, restored affine-aware statue matching, and the actually compiled guarded Root9 reader.
- User reported ~196 FPS after the guarded-reader fix. Statues were **better but still occasionally moved** in this build.
- `Build/BUILD-INFO.txt` and the ZIP's copy now describe the corrected measurement situation. The main executable was not changed while preparing this handoff.

**Unreleased candidate matching current Source/:**

- SHA256: `0e6d4d172ac875f2e238d74b297627cb284e54d7b573618c59c70cfe158a5c69`
- Archived at `C:/catherine/recomp/Old/Releases/Catherine-candidate-0e6d4d17-statues-pool-20261122.zip`
- Contains `Catherine.exe`, map, PDB and candidate notes; verified archive and executable hash.
- Adds the stricter shared-unowned-rigid-copy guard and empty transform-upload-record pooling described below.
- User manually tested this executable at ~200 FPS. **No user confirmation that its remaining statue movement is eliminated.** Do not claim the statue bug fully fixed.
- Removed `Build/catherine1.exe`, `catherine2.exe` and their temporary maps/PDBs after archiving. Only the main executable remains active.

**Source/ is ahead of the delivered main executable.** Rebuilding from current Source/ includes the candidate changes. Do not accidentally call that a byte-for-byte rebuild of the main release.

Build with:

```powershell
Set-Location C:/catherine/recomp
python build.py --pgo --output catherine1.exe --jobs 4
```

Use `--pgo` for performance comparisons. A plain build is not equivalent. Stored profile: `Build/perf/pgo/merged.profdata`. Keep assets, save, settings and DLLs in Build/. Leave the separate DX12 plugin and its supporting files alone.

## Changes and evidence

### Eye diagonal cut: shipped

Indexed NVN QUADS were not expanded to triangle indices. Four original indices sent to TRIANGLE_LIST drew only one triangle, cutting the eye glow diagonally. Array quads were already converted.

Active file:
`Source/imports/timing-audit/vulkan-native-draw-depth-2026-11-19-1795089013572113700/nvn_draw_native_depth.cpp`

Now both indexed/array quads expand to six indices, preserving source index width, original corner indices, base vertex and instance arguments. Converted draws are excluded from incompatible fast-template capture. The user/automated captures showed the eye glow restored. No texture/shader-quality reduction.

### Root9 producer reads: shipped, real CPU improvement

Live profiles of the user's slow process showed about 59–66% of recording-thread samples in Windows kernel calls beneath `private_reader::read_with_query`, mainly through `f_7000314330`. GPU replay itself was only ~2.4–3.1 ms; CPU frame preparation was limiting the rate.

The selected `read_bytes` now uses the existing `c02_bridge::direct::read` SEH leaf instead of K32QueryWorkingSetEx on every tiny read. It preserves packet-byte and live-owner validation. The leaf catches inaccessible/decommitted memory, restores consumed guard pages, and preserves MXCSR. Setting `CATHERINE_ROOT9_GUARDED_READ=0` restores the old query reader for comparisons.

**Critical build gotcha:** the logical root9-integration CPP is overridden by a Clang VFS overlay. Editing that logical file initially had no effect. It was restored; the fix is in the actual selected file:

`Source/imports/timing-audit/frame-advance-audit-2026-11-20/safety-root-candidate-1795300767913069600/candidate/root9_native_bridge.cpp`

Unit **244**, object `244-candidate-native.obj`. Its physical VFS input dependencies were added to `Source/build-manifest.json`. The binary now contains `CATHERINE_ROOT9_GUARDED_READ`; the ineffective intermediate build did not. The user's corrected live profile confirms the working-set-query hotspot disappeared.

Guarded-copy tests passed: readable/readonly data, inaccessible addresses, overflow, cross-page permissions, decommitted memory, guard restoration and MXCSR. Microbenchmark: ~0.229 μs/query-reader read versus ~0.004 μs/guarded read, not game-frame timings.

Evidence:

- `Build/perf/runs/interactive-64180/active.csv` — pre-fix live profile.
- `Build/perf/runs/interactive-31144/` — ineffective intermediate source override, still old reader.
- `Build/perf/runs/interactive-74400/playing.csv` — actually fixed user process while playing.
- `Build/logs/reader-validation.cpp`, `reader-validation.txt`.
- `Build/logs/guarded-reader-statue-validation.json`.

### Statues: partial shipped repair plus pending stricter guard

The active optimized tracker had lost the old affine-aware repair. Its regression test reproduced 16 incorrect static matrices / 12 identity swaps. Restoring the known repair while retaining the flat exact lookup made that fixture pass. The user then reported occasional residual movement.

Tracker:
`Source/imports/timing-audit/performance-2026-11-16/cmd131/model_tracker81.h`

The unreleased candidate additionally recognizes differently oriented copies of the same geometry observed in one presentation as ambiguous **without a native instance owner**. The generic C3 fallback renders those copies with their native matrix instead of borrowing a proximity-based animation history. Owner-qualified actor/block paths and the attachment path run before this fallback.

Selected fallback file:
`Source/imports/timing-audit/frame-advance-audit-2026-11-20/safety-root-candidate-1795300767913069600/candidate/puzzle_transform88.inc`

Unit **198**; this physical overlay dependency was also added to the manifest. The logical campaign-c02 file is not the selected implementation. Check actual include resolution before editing.

New tests cover the old 15-degree shared-copy ambiguity, reset on clear, and preservation of a single rotating instance. Existing static/animated90/120-degree copies, culling permutations, scale and rotation continuity fixtures pass. **Tradeoff to verify:** shared unowned moving rigid geometry may lose generic interpolation; this is a conservative native-matrix fallback, not an authoritative instance-ID implementation. Verify statues while panning at the middle-stage checkpoint and check relevant unowned falling props. Do not assume passing the old tracker tests proves the entire visual bug fixed.

Evidence: `Build/logs/tracker-test.cpp`, `statue-before.txt`, `statue-after.txt`, `statue-shared-guard-tests.txt`.

### Empty C3 upload-node reuse: pending candidate

Actual selected file:
`Source/imports/timing-audit/frame-advance-audit-2026-11-20/safety-root-candidate-1795300767913069600/candidate/root9_rigid_transport.h`

Reuses up to32 empty pending map nodes and their cleared upload vectors; retains only capacities≤256. Recycled nodes contain no ownership records. This avoids repeated allocations/frees per upload while preserving ordering, overlap revocation, retirement, invalidation and snapshot admission.

10,000 differential producer sequences against the pre-change class pass, including owned/unowned writes, changed bytes, overlaps, retirement, invalidation, clears and snapshot content. Tests in `Build/logs/transport-pool-test.cpp` / `.txt`. No measured standalone FPS benefit should be claimed yet.

## How to reproduce the relevant boss measurement

Owned processes only; hidden/muted by default; never acquire the user's mouse or focus. Don't run simultaneous performance tests or leave another game process competing for CPU. User prefers short tests, about30 seconds after the intro cutscene; skip with Esc then Ctrl. If death occurs, mark subsequent menu time separately rather than calling it gameplay FPS.

The diagnostic wrapper now supports the difficulty selection:

```powershell
python Source/tools/eye_capture.py --scene boss --tag earned-match --exe Catherine.exe --difficulty Earned --seconds 5 --settle 0 --capture 1 --profile 3 --env CATHERINE_PERF_BACKGROUND=1
```

- `--difficulty Earned` leaves the dev control at Keep earned state, matching the user's route used here.
- `--difficulty Normal` or Hard/Easy/Safety explicitly overrides it.
- Destination: **Stage 9 / Floor 6 - direct puzzle**, ID **10906**.
- Area Puzzles; Karma/Route Keep earned state; select difficulty **after** selecting destination, which resets the option controls.
- `CATHERINE_PERF_BACKGROUND=1` uses nonactivating background windows and QA input. Add `NX_HEADLESS=1` only for screenshot diagnosis without presentation; that is not equivalent to normal presentation timing.
- The wrapper bypasses the present-capture mapping requirement, captures frames via `Build/dump_frames`, and restores settings / verifies save hash.
- `VISUAL_CAPTURE_ONLY` results are not30-second benchmark statistics. Profile time suspends sampled threads briefly and can perturb FPS.
- Existing `Build/perf/harness/bench.py` still defaults to **Normal**; do not use it unchanged and claim it represents the inherited-state problem. Present capture wasn't available in some headless runs; don't wait60 seconds for a map after every scene load.
- `Build/perf/harness/effsrc.py 244-` and `effsrc.py 198-nvn_sets` show physical VFS-selected files. Root build dependency fingerprints previously omitted some physical inputs; fixed the touched units only, not the whole source-selection scheme.
- `Build/perf/harness/stacksampler.exe PID seconds OUTPUT.csv 8 4` samples a user-authorized running build without mouse/focus changes. `analyze.py`, `leafsys.py`, `paths.py` resolve stacks using the matching map. Keep maps paired with the EXE. Helpers/logs for launch inspection are under `Build/logs/`.

## Stop state and next steps

No game/test processes were left running at handoff. No game input or camera controls are active. save.dat was not deliberately edited; harness runs report save unchanged. Snapshot hashes at handoff are in `Build/logs/handoff-build-state-20261122.json`; user changes to AntiAliasing were retained.

On resume:

1. Establish the user's actual save/load route and inherited difficulty; use the ~202 FPS case, not forced Normal, as the performance baseline.
2. Verify whether the pending candidate truly stops statue movement while panning, with no unacceptable interpolation regression. Do not silently promote it as already confirmed.
3. Investigate early conservative culling before CPU skinning/transform/draw setup, as explicitly suggested by the user. Count work at the bottom versus higher in the same difficulty and camera state. Preserve graphics quality and off-camera contributions to visible shadows/effects.
4. Continue measured producer-side optimization toward240+ FPS for that actual workload. The guarded reader already delivered a real gain; the minimum target is not yet met.
