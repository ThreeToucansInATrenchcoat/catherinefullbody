# Project working rules

User-requested layout: one canonical game source set under Source/ and one active build directory Build/. Implement future fixes in the sources selected by Source/build-manifest.json; do not resume compiling from Old/ or temporary directories. Use root build.py; it tracks content fingerprints and offers --rebuild and --check-dependencies.

Keep the main executable Build/catherine.exe. Temporary concurrent test executables may be named only catherine1.exe and catherine2.exe in Build/; delete them after their tests and close only processes owned by the test. Do not make dated build/source trees or leave temporary executable copies behind. Keep all intermediates/logs under Build/.

Old/ is the archive of prior audits, builds and source trees. Read it for evidence when needed; it is not the active source set. Preserve existing save.dat, catherine-settings.ini and assets.pak. Avoid copying or hashing the large assets file for routine validation.

The user requested that the independently developed DirectX 12 plugin be left alone. Preserve graphics/dx12/, its supporting graphics source files, and base/assets/ unless the user explicitly authorizes changes to that plugin. This is the explicit exception to the consolidated game source layout.

Game tests should default to hidden, muted owned processes. Never acquire or move the user's active mouse or focus. Do not resume graphics migration while it remains parked by the user. The historical audit ledger is Old/timing-audit/remaining-cadence-execution-ledger.json.
