# Claude: loading the two slow performance scenes

> **22 November late-session correction:** Read [HANDOFF_PERFORMANCE_STATUES_2026-11-22.md](HANDOFF_PERFORMANCE_STATUES_2026-11-22.md) first. The user uses **Keep earned state**, which reproduced ~202 FPS; this document's historical ~250 FPS boss baseline forces **Normal**. They are different difficulty workloads. Current main SHA is `b13e430a30b329b0f79abd6e7385819ca2eb845182aca668eed05d41853554fd`; current Source/ includes an unreleased candidate archived in Old/Releases. Do not treat the older hash below as current.

Updated 22 November 2026. These are the **Day 5 Stray Sheep bar** and **Stage 9 / Floor 6 boss**. The bar was historically about 160 FPS, later roughly 200; the boss had a roughly 240 FPS historical build and later regressed. These are historical observations, **not fresh measurements of today's EXE**. Establish a new baseline before comparing changes.

## Current project and launch

Project: `C:/catherine/recomp`. Read root `AGENTS.md` first.

- Current game: `Build/catherine.exe`.
- Current executable SHA-256: `d7e4bb9a08d6e71143dd1a66241d8e57010020fe622aee486883deecf48f92c9` (22 Nov 2026 performance build: bar ~251 FPS, boss ~252-258 FPS uncapped, visible window; previous `836f8065...` measured ~198 / ~195).
- Release configuration: `python build.py --pgo` (profile-guided + ThinLTO from `Build/perf/pgo/merged.profdata`; retrain with `Build/perf/pgo/pgo_build.py gen`, then `--pgo` runs of `Build/perf/harness/bench.py`).
- Benchmark harness: `python Build/perf/harness/bench.py --scene bar|boss --exe Catherine.exe` (visible, muted, owned process; restores settings; `--profile N` stack samples, `--capture N` frames). `ab.sh` runs interleaved A/B. Phase timers: set `CATHERINE_PERF_PHASES_BUILD 1` in `Source/imports/timing-audit/perf-2026-11-22/phase_timer.h`, rebuild with `--rebuild`, run with `CATHERINE_PERF_PHASES=1`.
- `Build/assets.pak`, `Build/dev-checkpoints.pak`, and the DLLs must remain beside the executable. The checkpoint pack supplies the earned game states; do not construct a boss state by changing only stage/day numbers.
- Active game sources: `Source/`, selected by `Source/build-manifest.json`; build with `python build.py`. Previous test scripts and performance reports are archived in `Old/` and contain obsolete absolute paths. Do not launch them unchanged or resume compiling archived source.
- Use only `Build/` for outputs. Concurrent temporary binaries may be `catherine1.exe` / `catherine2.exe`; remove them when finished. The independently developed DirectX 12 plugin and its supporting graphics sources must be left alone unless separately authorized.

Visible manual launch, with audio muted:

```powershell
Set-Location C:/catherine/recomp/Build
$env:NX_MUTE = '1'
.\catherine.exe --assets-pak .\assets.pak
```

For manual operation, launch without inherited `NX_HEADLESS` or `CATHERINE_PERF_BACKGROUND`. The DEV menu starts closed. Reach the normal title menu, then press **` / ~** to open **DEV - Direct Start**. Initial logos have their original timing. The wine-bottle intro video can be skipped with the normal Confirm action (Space with current bindings, or left click); stop sending Confirm once the main menu appears.

## Exact destinations

The DEV controls are labelled **Area**, **Stage / day**, **Puzzle / destination**, **Karma**, **Difficulty**, **Route**. Keeping Stage/day at **All stages / days** avoids selecting a similarly named checkpoint.

| Control | Slow bar | Slow boss |
| --- | --- | --- |
| Area | **Bar** | **Puzzles** |
| Stage / day | All stages / days, or Day 5 | All stages / days, or Day 9 / Stage 9 |
| Puzzle / destination | **Day 5 - Stray Sheep, before the nightmare** | **Stage 9 / Floor 6 - direct puzzle** |
| Internal target ID | **5100** | **10906** |
| Karma | **Keep earned state** | **Keep earned state** |
| Difficulty | Keep earned state if disabled | **Normal** for a consistent new baseline |
| Route | **Keep earned state** | **Keep earned state** |

Choose the destination first: changing it resets the option controls. Then set any supported difficulty override, press **Load**, wait for the loader to accept/queue it, and **close the DEV menu** so the game resumes. Loading returns through the native title/load sequence and can take time; do not count that as gameplay measurement time. DEV checkpoints are temporary session state; restart normally to return to the ordinary save. Preserve the user's save and settings anyway.

### Bar: ID 5100

This is the heavily populated Day 5 pub used for the seated-character/geometry investigations. The titlebar should say **Stray Sheep - Day 5**. Use the default seated-friends/table view and let the lead-in settle; keep the same dialogue/camera position between A and B. Do not substitute Day 8, a landing, the after-Quadrangle checkpoint (5200), or a different Day 5 bar snapshot and assume the workload is identical.

The historical bar runner explicitly used `Day 5 - Stray Sheep, before the nightmare` (`Old/timing-audit/pub-performance-2026-11-18/baseline.py`). Loose shapes around heads and missing necks were regressions from optimization experiments and were repaired. Maintain correct necks, hair, hats and attachments as well as performance. Seated gesture timing was also repaired: do not speed animations up to gain FPS.

### Boss: ID 10906

This is the original slow tower-boss benchmark, **Stage 9 / Floor 6**. It is **not Rin Night 10 / Floor 4**, which was a different boss pose-jump investigation.

Wait for the boss intro. To skip a skippable cutscene, press **Esc, then Ctrl** (the user's verified sequence); dismiss any remaining native introduction/confirmation prompt normally. Begin the sample only after the puzzle is playable. Keep the camera at its initial/default view, with the boss visible. Do not pan away: the user observed an FPS change when the boss left the camera view. Do not enable Autoplay for this still-scene benchmark.

If a question-results/loading screen appears, it is a lead-in, not the measured stage. Advance its actual Confirm prompt and wait for the puzzle. Repeated Confirm spam throughout loading can create misleading results. A retry also skips the first-entry announcement/intro, so do not mix retry and first-entry timing in the same comparison.

## Measurement and quality rules

1. Use **one game process at a time** for valid performance comparisons. Parallel processes compete for CPU/GPU; they are unsuitable for comparing the two frame rates.
2. Set the cap to **Unlimited** for measuring headroom (settings `Fps=0`; ordinary current settings use `Fps=240`). Record and restore the user's setting afterward. Do not write the INI while the game is running. Keep graphics quality, resolution, AA and interpolation identical in both runs. Historical boss figures used 4K/8x AA, but do not assume those historical settings are today's settings.
3. Measure **30 seconds after the scene is playable and the intro/cutscene has finished**, not five minutes including loading. Keep the camera still and matched. Exclude paused/DEV-menu/loading time. If a death occurs, mark that interval and compare the same gameplay window rather than averaging a game-over screen into the result.
4. Restart for the next scene or variant. Prefer interleaved A/B/B/A runs because machine/background load can shift FPS. Report average frame time/FPS and stutters separately. Titlebar FPS is a quick check, not a complete frame-time sample. CPU QueuePresent return intervals are not GPU-completion or display-scanout measurements.
5. Prefer hidden, muted owned tests: `CATHERINE_PERF_BACKGROUND=1`, `NX_HEADLESS=1`, `NX_MUTE=1`. This still requires a window/surface; use the existing per-process test input interface or an in-process harness, never the user's mouse/focus. A hidden HWND does not mean the stage is loaded. Check scene identity and active gameplay before sampling. Do not enable profiling in only one side of an A/B test.
6. Preserve repaired boss poses, interpolation, geometry and timing. `NX_DYNAMIC_UBO` is deliberately disabled by the current launcher after the neck/flashing-geometry regression; do not re-enable it as a shortcut.

## Automation details, if useful

The current compiled DEV loader API is in `Source/imports/timing-audit/direct-start/dev_loader_api.h`; the catalog is `Source/imports/base/dev-start/runtime/dev_loader_catalog.inc`. The GUI implementation is `Source/imports/timing-audit/performance-2026-11-16/background18/gui-tall.cpp`.

For an **in-process** harness, queue `catherine_dev_loader_request` with `version=1`, `target_id=5100` or `10906`, `karma_enabled=0`, `karma_value=0`, `route=-1`, and zero `reserved[]`. Use `difficulty=-1` for preserved earned difficulty, or `1` for Normal on the boss. Check supported capabilities from `catherine_dev_loader_describe`; the GUI uses these guards too. Poll `catherine_dev_loader_status`, handle errors, and then separately verify that the desired scene is playable. Request accepted / loader complete is not itself proof that a 30-second gameplay sample has begun.

For an owned native-window harness, the DEV window class is `CatherineDevDirectStart`; control IDs are Area **101**, Stage/day **102**, destination **103**, Karma **104**, Difficulty **105**, Route **106**, Load **107**, Close **108**. Find destination entries by exact label, not a remembered combo-box index. The existing test input mapping is `Local\CatherineBenchPad-<PID>`; its layout lives in the current `platform_icon.cpp`. Archived helpers assume old folders and sometimes a `perf*` temporary working directory, so adapt them to the consolidated Build layout rather than running them as-is.
