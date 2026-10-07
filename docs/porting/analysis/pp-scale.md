# Post-process blur scale (480-line reference): notebook

Branch `pp-scale`, worktree `../engine-uc2-pp` (from main `e4ce0a5`), run trees `<WorkUC2>/run/pp*`, pictures and
logs `<WorkUC2>/logs/pp`. Everything that runs goes through `<WorkUC2>/bin/capped` (game 2G, 3G at 4K; builds 6G
`-j6`). Every step is committed and appended here.

The problem (coordinator, 2026-10-07): the bloom/emissive/depth-of-field blur targets are a fraction of the back
buffer (`BlurTargetScaleX/Y`, halved per level) and the blur taps sit a fixed number of texels apart, so every
blur reaches a fixed number of screen pixels: at 1440x1080 the glow around the DM-Deadbolt ceiling tube is half
as wide (as a share of the picture height) as at 640x480, the Xbox's picture. The goal (coordinator's decision,
the owner: "prefer the game looking beautiful than being correct"): every post-process blur covers the same
share of the picture as on the Xbox at 640x480, at any resolution and aspect ratio.

## Step 1. What the code does (read, 2026-10-07 19:50)

### The blur chains, in the Xbox's 640x480 numbers

Script defaults (`Source/UCGameplay/Classes`; the game runs the shipped `UCGameplay.u`, same defaults):
`UCScreenDOF` `BlurTargetScaleX/Y = 0.5`; `UCScreenEmissive` and `UCScreenBloom` 0.25; `NumBlurTargets = 3`,
`NumFeedbackPasses = 2`, `FalloffRate = 0.4` (emissive); radial blur `RadialBlurDistance = 13`, `NumRadialPolys
= 6`.

`FilterCopyRenderTarget` (`Engine/Src/UnRenderUtil.cpp` 2375) puts its four taps at `OneTexel = 1 / SOURCE
size` times the filter's offsets (+-1, +-3 for the 1221/1331 kernels, +-1 or +-2 for the box): **the taps are in
texels of the source**. Destination pixel (x,y) samples at ((x+0.5)/w, (y+0.5)/h) (convention 4 of
`UC2GLPostProcess.cpp`).

| Effect | Pass | Source -> destination at 640x480 | Taps |
|---|---|---|---|
| depth of field (`UUCScreenDOF`) | 0 | back buffer 640x480 -> 320x120 | vertical 1331, +-1/+-3 **back-buffer** pixels |
| | 1 | 320x120 -> 160x120 | horizontal 1331, +-1/+-3 texels of 320 |
| | composite | 160x120 -> screen, bilinear, alpha by depth ramp | - |
| emissive (`UUCScreenEmissive`) | select | back buffer -> 160x120 (own target, accumulates over frames, alpha 0.4) | box x2: +-2 **back-buffer** pixels |
| | feedback x2 | 160x120 -> 160x60 -> 80x60 -> back into 160x120 | 1221 vertical, 1221 horizontal, box +-1 |
| bloom (`UUCScreenBloom`) | select | back buffer -> 160x120 (`PS_BloomSelect`, one bilinear read per texel) + emissive result | - |
| | blur | 160x120 -> 80x120 -> 80x60 | 1221 horizontal, 1221 vertical |
| | composite | 80x60 -> screen, bilinear, `FB_Brighten` alpha 128 | - |
| back-buffer copy (driver, `UpdateBackBufferCopyTarget`) | | back buffer -> 320x240 (half) | box +-1 **back-buffer** pixel |

With the targets as a fraction of the back buffer, at 1440x1080 every target has 2.25 times the lines, the taps
in target texels reach 2.25 times fewer screen-height shares, and the first pass of each chain, whose taps are in
back-buffer pixels, also reaches 2.25 times fewer. Hence the halved glow.

### Why sizing the targets alone is not enough

If only the target sizes followed a 480-line reference, the first pass of each chain would downsample the real
back buffer by 2.25 (1080p) to 4.5 (4K) times more than on the Xbox while its taps stay in back-buffer pixels:
the taps would cover a smaller share of the source footprint and the blurs would start too narrow; and even with
the tap offsets scaled to the reference (+-1 reference pixel = +-k back-buffer pixels, k = height/480), four
bilinear taps would read only a fraction of the pixels under each target texel (the bloom select reads one
bilinear sample per target texel: on the Xbox 2x2 of the 4x4 pixels under it, at 4K 2x2 of 18x18). Small bright
things (lamps, the emissive strips) would then flicker in and out of the glow as the camera moves: not the
Xbox's look and not beautiful.

### The plan (refines the coordinator's preferred approach; same goal)

1. **Setting** `[UC2GLDrv.UC2GLRenderDevice] PostProcessReferenceHeight=480` (0 = old behaviour), console
   `PPREFERENCE [h]`, reaching the engine through `FRenderCaps::PostProcessReferenceHeight` (`UnRenDev.h`, set by
   the device, like `ShippedDOFBlurAlpha`); no member added to any script-mirrored class.
2. **The reference picture**: `RefH` lines, `W * RefH / H` columns (the back buffer's aspect, square pixels).
   One reference pixel = k = H / RefH back-buffer pixels.
3. **Engine** (`UCCameraEffects.cpp`): DOF, emissive and bloom targets sized from the reference picture instead
   of the back buffer (same scales, same halving); sizes compared as integers (see the defect below).
4. **Engine** (`FilterCopyRenderTarget`): when the source is the viewport's back buffer (`SrcViewport` given),
   the tap offsets are in reference pixels (k back-buffer pixels).
5. **Driver**: a pass that samples the back buffer while drawing into a target no taller than the reference
   picture (every downsampling pass of the post-processing: DOF pass 0, emissive select, bloom select, the
   luminance pass) reads a **reference copy** of the back buffer: RefH lines, made from the main framebuffer by
   an area-averaging downsample (N x N bilinear taps, N = ceil(k)). Every chain then runs texel for texel as on
   the Xbox at 640x480, on a well-filtered 480-line picture. At 640x480 the copy is not made (k = 1): the
   picture stays bit-identical to main.
6. **Radial blur**: `RadialBlurDistance` is the inset of a zoomed copy in 640x480 pixels; scaled so the zoom
   factor per axis equals the Xbox's (x by W/640, y by H/480 for RefH 480).
7. **Back-buffer copy** (driver): stays half the back buffer (it is also the distortion accumulation target and
   its depth copy: a finer field keeps distortion edges and occlusion sharp; distortion amounts are already
   scaled by height/480); its four taps move to +-1 reference pixel, read from the reference copy.
8. Distortion `PixelScale` = height / RefH (480 when the setting is 0, as before).
9. Audit of every other screen-space distance; verification script `tools/dev/pp-scale.sh`.

### Found on the way (pre-existing defect, to confirm by a run)

Epic compares the pooled targets' INT sizes with the FLOAT products (`GetWidth() != BlurSizeX`). Where a product
is not a whole number (1366x768: 1366 x 0.25 = 341.5) the test is true every frame: the pooled targets are
released and re-obtained each frame and the emissive accumulation target (an `FAuxRenderTarget`, deleted and
re-created) loses its streak history every frame. At 640x480, 1280x720, 1920x1080 every product is whole.

## Step 2. The implementation (2026-10-07 20:40), self-test 15/15 at 1280x720

Baseline binary for comparisons: main `e4ce0a5` built here before any change (`run/pp-bin/uc2-base.bin`, sha256
`0806446a...`; in the run tree `run/pp/System/uc2-base.bin`, used with `UC2_BIN=uc2-base.bin`).

What was changed (file: function, why):

- `Engine/Inc/UnRenDev.h`: `FRenderCaps::PostProcessReferenceHeight` (default 0) and `RenDevPostProcessReferenceHeight()`
  (defined in `UnRenderResource.cpp` next to `RenDevHasXboxPostProcess`).
- `Engine/Inc/UnRenderUtil.h`, `Src/UnRenderUtil.cpp`: `FPostProcessReference GetPostProcessReference(W, H)`: the
  reference picture (R lines, W*R/H columns, PixelScale = H/R) or the back buffer itself (R = 0 or R = H).
  `FilterCopyRenderTarget`: with a source viewport (= the back buffer) the taps are multiplied by PixelScale.
- `UCGameplay/Src/UCCameraEffects.cpp`: `UUCScreenDOF::PreRender`, `UUCScreenBloom::PreRender`,
  `UUCScreenEmissive::PreRender` size from the reference picture; sizes compared as the truncated whole numbers they
  are created with (`UCPP_TargetSize`; the per-frame re-creation defect of step 1); one log line per (re)created
  target (`UCCameraEffects: <effect> target <i>: WxH (reference picture ..., back buffer ...)`).
  `UUCScreenRadialBlur::PostRender`: insets times W/(R*4/3) across and H/R down.
- `UC2GLDrv`: ini `PostProcessReferenceHeight` (compiled default 480), console `PPREFERENCE [h]`, `GetRenderCaps` passes
  it on. `UC2GLPostProcess.cpp`: the reference copy (`GPostReferenceFragmentShader`, `UC2GL_UpdateReferenceCopy`;
  N x N bilinear samples per reference pixel, N = ceil(k) <= 8, from the main colour texture, flipped to top row
  first; made at most once per change of the main framebuffer, like the snapshot); `PrepareSampledTargets` decides
  per pass (`SampleReference`): back buffer sampled while drawing into a render target of at most 3/4 of the back
  buffer's height; `GetSampledTexture` binds it; `UpdateBackBufferCopyTarget` taps x PixelScale (target stays half
  the back buffer). `UC2GLShaders.cpp`: distortion `PixelScale` = H / R (480 when R = 0, as before).
- Self-test (`UC2GLSelfTest.cpp`): the eleven tests of M7/M11 now run with the reference height 0 (they check
  Epic's primitives as specified; unchanged); new: `pp-sizes` (first, with the configured reference: every live
  pooled target is one of the sizes Epic's formulas give for the reference picture and none is missing),
  `ref-copy`, `ref-taps`, `bb-copy-ref` (with R = 480, back buffer taller than 480 lines).

First run (`tools/dev/m7-effects.sh pp selftest`, 1280x720, `logs/pp/fx-selftest.out`): `15 passed, 0 failed`. The
targets logged at 1280x720: DOF 426x120, 213x120; emissive select 213x120, blur 213x120, 213x60, 106x60; bloom select
213x120, blur 106x120, 106x60 (at the reference picture 853.3x480: the Xbox's 640x480 numbers widened to 16:9);
`reference copy 853x480 of the 1280x720 back buffer (2x2 samples per pixel)`. `ref-copy` largest error 1.00,
`ref-taps` 1.38 (taps 1.5 back-buffer pixels apart, no extra copy made during the pass: the copy was current),
`bb-copy-ref` 1.41, `pp-sizes` 7 live targets, none unexpected, none missing.
