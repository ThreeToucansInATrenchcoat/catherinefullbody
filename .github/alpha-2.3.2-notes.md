This build is intended to improve performance on handhelds and make the game faster for everyone else.

- Native GPU skinning enabled by default, with an optional CPU fallback.
- Improved rendering performance and reduced CPU overhead.
- Linux now uses Vulkan timeline synchronization, improving performance in demanding puzzle floors.
- Higher-resolution shadows with automatic and manual quality settings.
- Added internal resolution controls for 720p, 1080p, 1440p and 4K.
- Added optional VSync and triple buffering.
- Added optional extracted-assets folder support and conversion to/from `assets.pak`.
- Fixed incorrect pause-menu mouse targeting after directly loading a puzzle.
- Fixed sheep pushing causing a softlock above 30 FPS.
- Fixed sheep interpolation causing a one-frame position jump after moving between blocks at high frame rates.
- Scott is still stupid, still not fixed
