# Optical-flow frame generation (21.10.2026)

This is the separate native Vulkan FSR 3 implementation used by the owner's
Killzone project on 21.10.2026. It does not enable FSR temporal upscaling and
does not import the older `runtime-fg` experiment or its uncommitted changes.
The existing AA, render resolution and FSR 1 settings retain their meaning.

## Controls and data

`60 FPS + FG (2x, ~120 output)` in the launcher's Frame rate menu persists as
`frame_generation=0|1` in `user/driveclub_pc.ini`. Default off. The process
override is `DCPC_FRAME_GEN=0|1`. It adds one intermediate frame between
consecutive real frames; the game's target FPS remains the real-frame target.
The four launcher modes are 30, 60, 60 + FG, and 120 FPS. Only 60 + FG enables
generation; the other three store `frame_generation=0`. Existing INI settings
remain compatible, with FG normalized off for a saved non-60 target.
The existing FPS overlay still reports real game frames. The log separately
reports real frames, successful presentation pairs, GPU timestamp duration,
pair spacing and failures every five seconds.

Input is the completed SDR game image after the normal host image-quality
passes. Optical flow estimates motion from that image. This mode intentionally
uses neutral depth and zero game motion vectors, exactly as the Killzone
implementation; those inputs are not measurements or a reconstruction of
Driveclub geometry. By default, `DCPC_FG_HUDLESS=1` captures the actual video-out
scene before its second draw, before the HUD is added. It receives the same
AA/scaling/postprocessing as the full image. Optical flow uses this scene and
the SDK receives both HUD-less and full current images for HUD composition.
This capture point was checked at 1080p in the tested race, not every game mode.
`DCPC_FG_HUDLESS=0` restores full-image optical flow for comparison;
`DCPC_FG_HUD_DRAW` selects the capture draw (default 2, range 2 to 32).
Snapshots are owned per flip, pooled with a bound, and never reused as a stale
fallback when the current capture is missing. Availability changes reset history.
Both generated and real images pass through the same ImGui viewport and host
overlay path. HDR, splash screens and notification-only redraws do not generate
frames. A missing frame, gap above max(40 ms, 2.5 times the smoothed interval),
or size change resets interpolation history and skips the invalid midpoint.

The generated display view forces alpha to one. The SDK's interpolation output
stores an inpainting weight in alpha, and the final inpainting pass only writes
alpha one for pixels it modifies. Sampling that as ImGui source opacity darkens
the remaining pixels against the black viewport, causing alternating lighting.
The SDK image itself is unchanged; only its sampled display opacity is overridden.
Diagnostic generated-image dumps also save `-gen-alpha.png` and
`-gen-old-black-blend.png`. Ordinary PNG conversion forces alpha one and therefore
cannot, by itself, reveal this presentation error.

Image quality at HUD edges, disocclusions, fast cars, camera changes and rain
must be judged in Driveclub; good Killzone results are not proof for this game.
FG adds GPU work and presentation latency, and cannot improve simulation/input
rate. It can reduce real FPS when GPU or presentation work becomes the limit.

## Synchronization

`vk_frame_gen.*` records the genuine AMD Optical Flow and Frame Interpolation
passes. `vk_presenter.*` submits generated and real frames, then a bounded
pacing worker waits for their GPU timeline values outside the submit mutex.
It presents the generated image and the real image half an interval later,
using a Windows high-resolution waitable timer. The previous pair is drained
before the next acquire, recreation, reuse, or shutdown. A second queue in the
same graphics/present family isolates blocking WSI calls when available; a
single-queue device uses the submit lock after the GPU wait.

The swapchain requests two additional images, within device limits. Resize
and notification handling retain the ordinary presenter fallback. FG resource
and descriptor lifetime ends before the device and ImGui renderer lifetime.
Creation/dispatch failures log an error and fall back to real images; generated
counts are not used as evidence of image quality or physical monitor scan-out.

## Reproduction and provenance

Use `scripts/run_portable_fg_bench.ps1` in a separate PowerShell invocation per
variant. The 70–110 s race window excludes the optional later image dumps.
`DCPC_FRAME_GEN_DUMP_AT=<seconds since first generated frame>` saves eight
consecutive real/generated image pairs using internal GPU readbacks into
`user/screenshots/fg-NNN-real|gen|hudless.png`; it never captures the desktop.
`DCPC_FRAME_GEN_DUMP_AT2` adds a second eight-pair window.
`scripts/report_portable_fg.py` reads real guest frames separately from Vulkan
present calls. Dumping is diagnostic, off by default, and perturbs timing.

Killzone source location:
`killzone-profiler/gpu/shadps4/video_core/renderer_vulkan/vk_frame_gen.cpp`,
SHA256 `20583b8fafb13f5225bce87e82b48d62af46e6e32c55c9710d5be52924f6a9df`.
The GPL-2.0-or-later wrapper retains its attribution. `externals/fsr-vulkan`
is the MIT-licensed local portability layer and pinned AMD SDK source/shader
closure; its LICENSE, NOTICE and UPSTREAM inventory are retained. This path
links `ffx-vulkan::portable` (SDK 1.1.4 backend), not the experimental SDK 2.3
or FSR 4 targets. No signed FSR DLL or model payload is needed.

Local portability changes: conditional reuse of an existing Vulkan CMake
target and explicit unsigned error-code cases for clang-cl. On Windows the
existing Khronos loader export definition generates the system Vulkan loader
import library; no SDK installation or DLL copied from another project is
required. Runtime validation and release decision are recorded in the top-level
`docs/portable-fg-20261021.md` after gameplay testing.
