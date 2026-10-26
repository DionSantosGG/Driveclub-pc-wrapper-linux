# Driveclub-PC 0.2.5

This release brings the launcher, graphics and audio fixes made since 0.2.1,
plus optional native Vulkan FSR 3 frame generation.

## Changes

- **Four frame-rate modes:** 30 FPS, 60 FPS, 60 FPS + FG, and experimental
  120 FPS. The FG mode runs the game at 60 FPS and generates intermediate frames
  for approximately 120 presentations per second. The FPS counter shows the
  real game frame rate.
- **Optional frame generation:** native AMD Optical Flow / Frame Interpolation,
  with a separate pre-HUD scene input and paced presentation. Generated frames
  now display with correct opacity, fixing the alternating scene darkening
  found during testing. FG is independent of the existing upscaling setting.
- **Custom save-data root:** select a save location, open it, or return to the
  default. Existing saves are never moved automatically.
- **Launcher checks:** clearer firmware requirements, validation of game/update
  regions, an accessible scrolling settings window, and the quit shortcut.
  Additional Driveclub regions are accepted as experimental.
- **Graphics fixes:** corrected fixed shader bounds at higher render resolutions,
  subgroup-width handling, and the multisampled depth conversion path.
- **Audio fixes:** improved packet delivery, prebuffering and queue handling for
  OpenAL and SDL, reducing unnecessary stream restarts.
- **Release + ThinLTO build with a fresh PGO profile** for this source revision.

## Install or update

1. Extract `Driveclub-PC-v0.2.5.zip` into a folder you can write to. When updating,
   replace the program and accompanying release files; keep your `user` folder.
2. Run `Driveclub-PC.exe` and select your own Driveclub game and PS4 firmware
   modules in the launcher. The release does not include game files or firmware.
3. Choose a frame-rate mode. Ordinary 60 FPS remains the default; select
   **60 FPS + FG** to try generated frames.

Game data, firmware, saves, configuration and shader caches are excluded from
the download. A corresponding source archive and SHA256 checksums accompany it.

## Known limitations

- FG can still distort fast motion, disocclusions, cockpit edges, collisions and
  camera transitions. It does not increase simulation or input-update rate.
- Water spray, smoke and flare effects can flicker; this was also observed
  before FG. Replay-camera shake remains unresolved.
- The 120 FPS target remains experimental, including timing and achievable FPS.
- Local gameplay validation uses **CUSA00093 v1.28 on NVIDIA**. Other regions
  remain experimental; AMD-specific visual fixes require confirmation on AMD.
- Linux support is deferred. This release contains the Windows build.

Based on GR2fork/shadPS4. Frame-generation integration adapted from the local
Killzone port; AMD FidelityFX and portability-layer notices are included.
See the included licenses and source archive for the corresponding source.
