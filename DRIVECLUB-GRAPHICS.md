# Driveclub-PC: graphics fixes, 20.10.2026

Baseline: release 0.2.1 (`b22594e`) plus launcher/preflight stage 1 (`f63d397`).
No FSR/FG experiments are included. This is a local Release+LTO test build,
not a PGO release. AMD reports are not considered verified fixed on NVIDIA.

## Resolution-dependent compute shader

Driveclub's deinterleave compute shader `0x2d9c66a5` contains fixed 960x540
bounds. Replace those two bounds with the actual storage image dimensions
using `OpImageQuerySize` when creating the Vulkan shader module. This covers
fresh compilation, async compilation and old serialized SPIR-V caches.
The original cache blobs and game files are not modified.

The transformation requires the exact guest hash and matching structural
signature: compute 8x8x1, four fetches, one storage write, two unsigned bound
comparisons, expected types and image binding. Unexpected layouts are logged
and left unchanged. Applying it twice is harmless. This targets one confirmed
shader; it is not a complete port of every Shader Autopatcher feature.

- `DCPC_SHADER_RES_FIX=0`: diagnostic opt-out (default on).
- `DCPC_SHADER_FIX_DUMP=1`: save the transformed module under
  `user/shader/driveclub_deinterleave_fixed.spv` for validation.

Research lead: [Kravickas, Driveclub Shader Autopatcher 2.0.1](https://www.moddb.com/mods/resolution-patch-game-update-128-shadps4/downloads/driveclub-shader-autopatcher-v2-0-1).
Its archive was inaccessible behind Cloudflare; no third-party patcher was run.
The implementation was checked against the actual shader dumped by this runtime
and an existing cached version. Runtime code was implemented independently.

## Vulkan subgroup size

Requiring 64 lanes now checks subgroup-size control, min/max bounds and the
specific stage's `requiredSubgroupSizeStages`. Compute shader lowering uses
the width actually selected for compute execution, rather than always using
the physical device's default subgroup width. This avoids wave32 lowering
when execution is explicitly pinned to wave64.

Devices whose selected compute width differs from their default use a separate
`CUSAxxxxx-dcpc-wave64` pipeline cache. Existing caches are preserved; stale
wave32-lowered code is not reused. Devices keeping their native width retain
the existing cache directory. Graphics stages request wave64 only when allowed.

Research lead: [OverkillLabs3 v0.2.4](https://github.com/OverkillLabs3/driveclub-recomp/releases/tag/v0.2.4).
Contract: [Vulkan shader stage requirements](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineShaderStageCreateInfo.html).
Release claims are not reproduced AMD results. Local NVIDIA reports subgroup
32, supported range 32-32, chosen compute 32, so AMD wave64 still needs testing.

## Verification and remaining issue

- Release+LTO build succeeds with clang-cl 19.1.5.
- Ten standalone checks against the real shader and old cached shader pass:
  transformation, unchanged input, unrelated hash, opt-out, idempotence,
  truncated header/instruction, zero word count, wrong group and wrong bounds.
- Original and transformed SPIR-V, including the runtime dump, validate with
  `spirv-val --target-env vulkan1.3`. Actual shader grows from 823 to 841 words.
- Automated races at 1920x1080, 2560x1440 and 3840x2160 with Precise, AF16,
  FXAA and target 60 reach gameplay. Internal screenshots were inspected.
  Async shader compilation and loading existing caches remain enabled.
  The 1440p run preloaded 634 pipelines and applied the fix during warm-up.
- No critical/assertion/device-loss markers found in these completed runs.
  Runs end at the requested timeout. This is not a performance comparison or
  proof that all AMD artifacts are resolved.
- The initial round identified a missing depth-overlap path: 1920x1080
  D32Sfloat, 4 samples -> R32G32B32A32Sfloat, 1 sample. The follow-up now
  preserves the four samples as separate RGBA channels. A standalone Vulkan
  roundtrip on NVIDIA preserved 48 distinct float values bit-exactly. Gameplay
  no longer logs the missing copy. Its relationship to specific AMD artifacts
  remains unverified. See `DRIVECLUB-STAGE2.md`.

Full local evidence and run IDs: `../docs/stage2-graphics-20261020.md`.
Audio fixes and camera investigation: `DRIVECLUB-STAGE2.md`. Loading-camera
shake remains unresolved; ultrawide FOV, DLSS/FG and photo mode are unchanged.
