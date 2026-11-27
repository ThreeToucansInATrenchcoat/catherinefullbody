# Launcher integration

RecompLauncher and Quiver integration is included in Alpha 2.4 for Windows and
Linux. Catalog submission and listing approval are separate steps.

## Shared runtime support

- `recomplauncher.json` lives at the repository root and is copied beside the executable
  by both native build scripts and the Windows/Linux release packager.
- `--prepare` runs the existing asset installer without starting the game or opening
  setup dialogs. It prints `progress <0..1> <message>` lines and preserves error codes.
- `--game=FOLDER` and optional `--update=FOLDER` supply extracted Switch files. Separate
  flag/value arguments work too. A normal launch can use these paths for first-run
  setup and then continue into the game; `--prepare` exits after setup.
- `RECOMPLAUNCHER_DATA` names an existing directory for game-generated data: assets,
  saves, settings and logs. Both launchers use `<install>/recompdata`. Disposable
  pipeline, shader and installer caches stay below `cache/`. Supplied artwork and
  video libraries continue to load beside the executable.
- Without that environment variable, the existing standalone locations and setup UI
  remain in use. Existing saves are not automatically moved or overwritten.
- The installer still validates content against `assets-required.json`; selecting a
  folder is not proof that it contains a supported game/update.

## Recomp Launcher

The profile declares the extracted game folder and an optional separate update,
headless preparation, eleven existing INI settings, and the exact save/settings
files for backup. It does not invent dump hashes or claim Verified Copies support.

After the combined release is built, enable Developer tools in Recomp Launcher,
choose Add a build, select the release folder, and test setup, settings and Play.
Try a fresh data folder and repeat with an existing prepared package. Test without
launcher environment variables as well.

Submission: https://recomplauncher.com/submit?platform=pc
Developer account: https://recomplauncher.com/developers?signup=1
Specification: https://github.com/elripalda/RecompLauncher/blob/main/docs/DEVELOPERS.md

## Quiver Launcher

Quiver currently reads `recomplauncher.json` for the program, launch arguments,
environment, exit-code messages and INI settings. It supplies `RECOMPLAUNCHER_DATA`
with the same `recompdata` layout. It does **not** run the profile's `prepare` step or
use its `inputs` and `saves` declarations directly: those are catalog-owned in Quiver.
Do not add `--prepare` to normal launch arguments; that would exit before playing.

`launcher/quiver-catalog-settings.json` is a prepared **catalog InstallSettings
fragment for submission**, not a second manifest loaded from the release. Give it
to the Quiver maintainers with the repository URL. It supplies:

- Preferred Windows/Linux executables.
- A required extracted game folder passed as `--game=...` and optional separate
  update folder passed as `--update=...`.
- Backups of `app/recompdata/save.dat` and `app/recompdata/catherine-settings.ini`.
  Cross-platform save compatibility is deliberately not asserted without testing.

Quiver starts Catherine normally with the selected paths. Catherine's existing
setup window creates assets on the first run, then starts the game. Subsequent
launches reuse them. Users adding the repository manually can still use Catherine's
own file picker; catalog-assisted selection and backup defaults need the listing.

Submission: https://quiverlauncher.com/submit
Repository: https://github.com/scottjco/catherinefullbody

Checked against Quiver source commit `01f0b8822b45c23e5a26d6311afc198d6b0525c6`:

- `apps/launcher/src-tauri/src/port_setup.rs` (manifest loading/environment).
- `apps/launcher/src-tauri/src/port_config.rs` (INI paths, choices and defaults).
- `packages/api/src/index.ts` (`InstallSettings`, `SetupFile`, `SaveSettings`).

## Checks completed without a game rebuild

- Profile validated against the current official Recomp Launcher JSON schema.
- All changed native compilation units passed Windows and Linux syntax checks.
- Windows/Linux asset-installer C# compiled as validation assemblies, without
  publishing embedded installers or game executables.
- Windows startup harness executed the actual startup source with a stubbed game
  and installer. Covered preparation, error propagation, spaces/Unicode, separate
  update paths, invalid arguments and unchanged ordinary launches.
- The Windows setup-process fixture checked stdout/stderr and exit-code forwarding,
  and proved cancelling the parent also stops its embedded setup process.
- Linux startup harness executed the actual startup source with a dummy embedded
  setup tool, checking argument and progress forwarding and ordinary startup.
- Existing settings tests passed, with an additional test proving launcher settings
  stay in the supplied Unicode/spaced data directory and leave the standalone INI alone.

The Windows game has also been built and launched locally, and the actual embedded
installer's `--prepare` command successfully reused prepared game assets. Windows
and Linux release builds and package checks run in CI. Testing through each external
launcher's actual UI and fresh asset preparation remain separate integration checks.

## Developer mode resources

Both packages include `dev-checkpoints.pak`, the cutscene catalog, developer launch
shortcuts and `DEV-MODE.txt`. The checkpoint archive is validated against every
referenced catalog entry when packaging. It remains beside the executable even
when the launcher supplies a separate data directory. Developer mode remains off
by default (`devmenu=false`); enable it in the active data folder's INI to use F8
or the startup shortcut.

## Optional follow-ups

- NSP/XCI/NUP import with user-supplied keys is not added; inputs accurately advertise
  extracted folders only. Adding container/decryption support is a larger change.
- Linux still uses the existing tarball and system library requirements. A portable
  AppImage/older-runtime build and Flatpak dependency testing are separate work.
- No achievements integration, automatic TV/Big Picture fullscreen override or
  claimed cross-platform save compatibility is added. Existing controller support
  and fullscreen/controller-prompt settings remain available.
- Catalog approval and developer verification are separate from source integration.
  Do not mark the port listed or add an availability badge until it is accepted.
