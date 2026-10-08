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

## Step 3. Measuring (2026-10-07 21:15): tools/dev/pp-measure.py, tools/dev/pp-scale.sh

- `pp-measure.py glow` reproduces the coordinator's numbers on his pictures (`logs/coord/bloomres-*`): 640x480 peak
  11.30, width 8.54% of the height; 1440x1080 peak 15.53, width 4.17% (his: 11.3 / 8.3%, 15.2 / 4.2%).
  `pp-measure.py sigma` (Gaussian fit of test - base = a (G_s(base) - base) + b on the 480-line picture) finds a
  synthetic blend of the 640x480 picture with its own Gaussian blur: s = 3.05 for 3 (alpha 0.402 for 0.4),
  1.50 for 1.5.
- First triple with this branch (`logs/pp/try-bloom-*`: off, on with reference 480, on with reference 0 in the
  same run): 640x480 width 8.54%, peak 12.33; **1440x1080 reference 480: width 8.54%, peak 12.58; reference 0:
  width 4.17%** (as before). The log lists the targets: at 1440x1080 exactly the Xbox's 320x120, 160x120 (DOF),
  160x120, 160x60, 80x60 (emissive), 160x120, 80x120, 80x60 (bloom); `reference copy 640x480 of the 1440x1080
  back buffer (3x3 samples per pixel)`; after `PPREFERENCE 0` the old 720x270, 360x270, ... .
- Main's binary at 1440x1080 gave peak 18.75 at 56 s and 21.75 at 68 s in one run (`logs/pp2/try-bloom-base-*`),
  and this branch with reference 0 at 68 s also 21.75: **the peak depends on when the picture is taken**. Cause:
  the bloom selects pixels above `GetAverageSceneLuminosity()` = (CurMaxL - CurMinL) / 2 of the dynamic gamma,
  which moves 5% of the way to the frame's histogram per update and updates at most once per frame; at software-GL
  frame rates (6 frames/s at 1440x1080) it takes tens of seconds to settle from its start values (0, 1). So the
  coordinator's peaks (11.3 vs 15.2) compared different states of it. The widths do not depend on it.
- Protocol therefore (pp-scale.sh): freeze game time (`set LevelInfo TimeDilation 0.0001`) once the match runs,
  wait long enough per resolution (20 s at 640x480, 40 s at 1440x1080, 90 s at 2880x2160 after post-processing
  goes on; 12-55 s after a later switch), shots in one run differ only by what was switched. Pitfall found: frozen
  before post-processing goes on, the teleport-in overlay (colour remap + radial blur, timed in game time) stays
  on screen forever (`try-azure-640x480-1.png`); the freeze comes 3 s after post-processing goes on, or the run
  has post-processing on from the start.
- The launcher drops any argument that would take the command line past 1024 characters
  (`SDLLaunch.cpp`): a long `-glexec=` list silently does nothing.
- Runtime data (`OBJ DUMP` in `logs/pp/probe-objs.out`): the concussion blur `UCPixelShaderEffects.MotionBlurs.
  ConcussionBlur` offsets its four reads with `TexRotator RotBlur1/2` and `TexOscillator OscBlur1` (pan amplitude
  u 0.004, v 0.007 at 3/6 Hz), `OscBlur2` (0.003, 0.008 at 2/3 Hz): fractions of the picture, weights 0.3 each.
  The radial blur's `SpeedComboRadialBlur` has no back-buffer modifier (the code supplies it), masks
  `T_HUD.Overlay.HUD_Overlay_Speed_Mask`, falloff 0.45.

## Step 4. Calibration at 640x480 and 1440x1080 (2026-10-07 21:40)

`tools/dev/pp-scale.sh` first runs (`logs/pp/cal-640.txt`, `logs/pp2/cal-glow.txt`):

- glow 640x480: width 8.54%, peak 12.63 (reference 480 and 0 identical, as they must be at 480 lines);
  **1440x1080: reference 480 width 8.33% (ratio 0.98), peak 12.85 (1.02); reference 0 width 4.17% (0.49), peak
  21.75.** Frame rates were low (machine loaded: 5 frames/s at 640x480, 1.7 at 1440x1080), hence a check that at
  least 60 frames passed between post-processing on and the shot (the dynamic gamma's 0.95^n), and longer waits.
- depth of field: DM-Azure, DOF on against DOF off (`set UCScreenDOF Enabled 0`) in one frozen frame. The DOF
  only acts where the depth is beyond StartZ 0.98: through the gap in the middle (mountains, far temple) and the
  temple's top edge against the sky (difference picture looked at). Box (310,186)-(428,230) of the 480-line
  picture: sigma 3.10, alpha 0.228 (the shipped alpha 65/255 = 0.255 times the ramp), fit 0.964. (The first box,
  296..376 x 196..252, took in the near bridge and the crosshair: fit 0.54.)
- concussion blur: poster box (360,175)-(500,255): sigma 3.15, fit 0.87.
- radial blur held at distance 30: not a Gaussian (fit 0.65-0.76 whatever the box, sigma at the 12 limit near the
  edge). Its own model fits: `pp-measure.py zoom` (test = a * Z_d(base) + c * base + b, Z_d = the picture zoomed
  as Epic's quad inset by d pixels of 640x480 on every side): box (150,95)-(620,300): **inset 9.10, a 0.51, fit
  0.914** (the six copies at d = 30, 15, 10, 7.5, 6, 5, each blended at 0.45 over the last, act like one copy at
  about 9).
- `ucc-bin verifyclasses` with this branch's binary: `All native class layouts match` (exit 0).
- Windows build (`cmake --preset windows`, `cmake --build build-windows -j6`, capped 6G): `[100%] Built target
  UCC`, exit 0.

## The audit: every screen-space distance of the post-processing

"Pixels" below are pixels of the Xbox's 640x480 picture unless said; k = back-buffer lines / 480.

| Effect | Distance, where | Old behaviour (main) | Now | Why |
|---|---|---|---|---|
| depth of field | targets W/2 x H/4, W/4 x H/4 (`BlurTargetScale` 0.5, halved), `UUCScreenDOF::PreRender` | fractions of the back buffer: blur k times narrower | fractions of the 480-line reference picture | the blur's reach in target texels is then the Xbox's share of the picture |
| depth of field | pass 0 taps +-1, +-3 back-buffer pixels (`FilterCopyRenderTarget` with the viewport) | back-buffer pixels | reference pixels (x PixelScale), read from the reference copy | first pass of the chain; without the copy 4 bilinear taps would cover a fraction of each target texel's k-times-larger footprint (aliasing) |
| depth of field | later pass taps (target texels), composite (bilinear upsample), depth ramp (`StartZ`, 256-entry ramp) | follow the targets; depth only | unchanged | correct once the targets are | 
| emissive glow | select target W/4 x H/4 (accumulates over frames), blur targets W/4 x H/8, W/8 x H/8, feedback x2 | fractions of the back buffer | fractions of the reference picture | as above |
| emissive glow | select taps (box x2: +-2 back-buffer pixels) | back-buffer pixels | reference pixels, from the reference copy | as pass 0 above |
| emissive glow | streak falloff 0.4 per frame (`FalloffRate`) | per frame | unchanged | a time constant (frame-rate dependent), not a distance; not this task |
| bloom | select W/4 x H/4 (one read per texel: the Xbox read 2x2 of the 4x4 pixels under it), blur W/8 x H/4, W/8 x H/8 | fractions of the back buffer; at 4K the select would read 2x2 of 18x18 pixels | fractions of the reference picture; the select reads the reference copy | same share; the reference copy is what makes the select read the picture as the Xbox did |
| bloom / emissive / DOF composite | bilinear upsample of the last target to the screen | - | unchanged (bilinear, as on the Xbox) | the brief: the look of the Xbox's upsampling at the same share of the picture |
| back-buffer copy (`UpdateBackBufferCopyTarget`) | half the back buffer, taps +-1 back-buffer pixel | blur of 4x4 back-buffer pixels: k times smaller | taps +-1 reference pixel, from the reference copy; **target stays half the back buffer** | the target is also the distortion accumulation target with its depth copy: the distortion field and its occlusion stay sharp with more texels; its readers (`PSPlayerCamMaterial` screens, `PSBackBufferEffect`, a `PSColorRemapMaterial` with `bUseBlurredCopy`, `PSRefractionMaterial`) get the Xbox's blur |
| distortion | amounts in pixels (`MaxDistortion` 35, materials' `DistortionAmount`) | M7: x height/480 | x height/R (R = reference height; 480 when R = 0) | already scaled since M7; now one setting for both |
| distortion | accumulation target and depth copy size (half the back buffer) | - | unchanged | a field resolution, not a distance (see the back-buffer copy row) |
| radial blur (speed, teleport-in, sniper) | quads inset by `RadialBlurDistance`/i pixels (13 default, script drives it; 6 quads) | screen pixels: at 1440x1080 a 2.25 times weaker zoom | insets x W/(R*4/3) across, x H/R down | keeps the Xbox's zoom factor on each axis at any resolution and aspect (AutoFOV on or off: a point of the 4:3 middle moves as on the Xbox) |
| concussion / screen blur (`UUCScreenBlur`, `BlurMe`) | the material's `TexOscillator`/`TexRotator` offsets (pan amplitude 0.003-0.008) | fractions of the picture: already the same share at every resolution | unchanged | material data in texture-coordinate units; at 16:9 the horizontal wobble is the same share of the width (1.33 times as many 480-line pixels): left as Epic's data |
| motion blur with accumulation (`UUCScreenMotionBlur`) | full-size accumulation target, falloff per frame | no distance | unchanged | no script creates one |
| heat shimmer camera effect | distortion material, texture-coordinate units | - | unchanged | creation commented out in script |
| colour remaps (berserk, pause, combo flash, sun flare, teleport, Liandri incl. its noise stages) | per pixel; noise textures tiled in texture-coordinate units | - | unchanged | no distance in pixels |
| dynamic gamma | 16x16 luminance target (256 samples at fixed fractions of the screen) | each sample read 2x2 back-buffer pixels | reads the reference copy (a sample = 2x2 reference pixels, as the Xbox's 2x2 pixels) | sample positions were already fractions; the copy makes each sample average the same share |
| point-sample nudge (1/32 texel), half-texel conventions | sub-texel | - | unchanged | conventions of the GL port, not effect distances |
| shadow blur (`PS_ShadowBlur`) | shadow texture texels | not lifted (M7b's work) | not touched | not screen space; another agent's area |

## Step 5. First complete measurement (2026-10-07 22:30; `logs/pp/run1.txt`, `logs/pp2/run1.txt`)

Measured by `tools/dev/pp-scale.sh`, all 4:3 with the same pinned view, on the 480-line picture:

| Effect (measure) | 640x480 | 1440x1080 new | 1440x1080 old (reference 0) | 2880x2160 new | 2880x2160 old |
|---|---|---|---|---|---|
| glow around the tube: width above 10% of the peak, % of the height | 8.54 | 8.54 (1.00) | 4.17 (0.49) | 8.33 (0.98) | 3.12 (0.37) |
| glow: peak brightening, levels | 12.63 | 12.95 (1.03) | 21.75 | 13.08 (1.04) | 19.72 |
| depth of field, DM-Azure distance: Gaussian sigma, 480-line pixels (alpha) | 3.10 (0.228) | 3.15 (1.02) | 1.45 (0.47) | 3.10 (1.00) | 0.70 (0.23) |
| radial blur at 30: inset of the equivalent zoomed copy, 640x480 pixels | 9.10 | 9.20 (1.01) | 4.60 (0.51) | 9.20 (1.01) | 2.30 (0.25) |
| concussion blur (BlurMe): sigma | 2.60 | 3.30 | 3.50 | 2.65 | 2.60 |
| 1920x1080, AutoFOV on (the tube at x 518 of the 853x480 picture): glow width, peak | | new: 8.12%, 13.45 | main's binary: 4.17%, 21.75 | | |

(ratios to 640x480 in brackets). The old behaviour's numbers are exactly 480/height of the new ones for the
depth of field and the radial blur (0.47 and 0.23; 0.51 and 0.25), as the analysis says. The glow at the old
2880x2160 is narrower still but not a quarter: its width is limited by the tube's own width at that scale.

- The concussion blur's sigma moves between runs (2.60, 3.30, 2.65) but not between reference 480 and 0 in one
  run (2.60/2.60, 3.30/3.50, 2.65/2.60): its four reads are offset by the material's oscillators and the frozen
  frame catches them at a phase that differs per run. It is in texture-coordinate units and the setting does not
  touch it. The check now judges the two shots of one frame against each other and the resolutions within 30%.
- Self-test at 1440x1080: 15 of 15 (`ref-copy` 3x3 samples, largest error 1.00; `ref-taps` 1.25; `bb-copy-ref`
  1.37; `pp-sizes`: 320x120 160x120 160x60 80x60 160x120+depth 80x120 720x540+depth, none unexpected or missing).
  The 2880x2160 run was stopped by its time limit before the test (the script's limit was fixed at 150 s): rerun.
- Identity at 640x480 failed as a test: two runs of main's binary differ by up to 3 levels over a third of the
  static part (the dynamic gamma settles to a slightly different bloom threshold per run), main against this
  branch likewise (max 3, mean 0.34/0.40 against main/main 0.49). Rerun with `DYNAMICGAMMA 0` at the end of the
  first frame (the threshold then stays where one update put it).
- Wasm-perf build (`cmake --preset wasm-perf`, `cmake --build build-wasm-perf -j6`, capped 6G): `[100%] Built
  target SDLLaunch`, exit 0; `SDLLaunch.wasm` sha256 `43f2d9c4...`.

## Checkpoint, 2026-10-07 22:55 (coordinator's resume message)

The coordinator reported a usage-limit cut-off at about 21:10; from this side the work went on (the commits up to
`f544713` and the runs below are from 21:10-22:50). State at this checkpoint, nothing running:

- Done and committed: the code (`f8dfb79`), the ini setting + README row + web inis (`2aad748`), the tools
  (`pp-measure.py`, `pp-scale.sh`, `run-uc2-shot.sh` UC2_CAP, `o9-nvidia.sh` O9_TREE, self-test counts in
  `m11-twin.sh`/`m13-64bit.sh`), the audit table, notebook steps 1-5.
- Gate items observed: (a) native, windows, wasm-perf builds pass (step 4, 5); (b) pp-scale lines: glow 640/1440/
  2880 PASS, dof PASS, radial PASS (`logs/pp/run1.txt`, `logs/pp2/run1.txt`); after the script update `d034f97`
  (selftest time limit, GL error count from the exit line, concussion judged per frame, aspect with the UI box):
  selftest PASS (15/15 at 1440x1080 and 2880x2160), aspect PASS, blur-setting PASS, blur PASS
  (`logs/pp/run2.txt`, `logs/pp2/run2.txt`); identity FAIL as a test (see step 5: run-to-run noise of main's own
  binary, also with `DYNAMICGAMMA 0` from the first frame: main/main 8558 pixels of the static box differ, max 2);
  `ucc-bin verifyclasses` PASS.
- In flight at the cut-off as the coordinator saw it: nothing. Remaining: a deterministic identity check (a frame
  frozen from the first frame without spawning: main/main still differ in 1943 pixels, max 150: locate and mask
  them); perf on the GPU (the scenario must outlast the markers: `sleep 50` is too short); m7-effects on this tree;
  m11-twin (trees ppm11/ppm11t); o9-nvidia combiner lines (O9_TREE=ppo9); the owner's sheets; whether M7b's shadow
  blur reuses the primitives changed here; the final report.

## Step 6. Identity at 640x480, performance on the GPU, and a slow shader fixed (2026-10-07 23:45)

**Identity (gate item 3).** Two runs of main's binary differ even in a frozen frame (step 5). Found where: with
game time frozen from the first frame, the dynamic gamma stopped after its first update (`DYNAMICGAMMA 0` at the
end of frame 1) and no Fire (the view behind the "press fire" board; every camera effect runs: the log has the
effects' targets), two runs of main's binary differ only around the two spinning pickups (rows 236-306, glow up
to rows 228-303). `pp-scale.sh identity` now compares rows 0-219 and 320-479 (79% of the picture): main/main,
new/new and main/new (twice) **0 differing pixels** there; whole frame main/new 1039 and 1789 pixels against
main/main 1332 (the pickups). PASS (`logs/pp2/pp-identity`).

**Performance (gate item 5), GPU** (`UC2_GPU=1` of fx-owner.sh: the Quadro P620, offscreen, `-glnoaa`; pinned
DM-Deadbolt view, bots off, time frozen so every frame is the same picture; frames per second in three 15 s
windows: reference 480, 0, 480 in one run; main's binary the same way to show the drift). First attempts without
pin and freeze varied by 30% with the bots; with them:

| | 1920x1080 | 3840x2160 |
|---|---|---|
| first shader (loop with a uniform bound and `break`) 480 / 0 / 480 | 109.6 / 111.1 / 107.1 | **61.3 / 78.9 / 59.9** |
| main's binary, same windows | 112.7 / 101.5 / 107.9 | 67.5 / 75.9 / 78.7 |
| shader with N a compile-time constant (`7f58c55`) 480 / 0 / 480 | 111.1 / 111.1 / 110.9 | **90.3 / 80.3 / 87.3** |
| main's binary, same windows | 113.3 / 107.7 / 119.3 | 79.4 / 79.1 / 78.6 |

So the first version cost about 4 ms a frame at 4K: the reference copy's 5x5 reads in a loop whose bound was a
uniform (GLSL ES 1.00 needs constant limits; the break made it a real loop, reads one after another). The
program is now built for its N (`#define UC2_TAPS N` in front of the text; rebuilt when N changes, i.e. when the
resolution does; a 2x2 build at start-up tests that it compiles). Now the reference path is about 10% faster than
the old one at 3840x2160 (11.3 ms against 12.5 ms: three 853x480 copies instead of three 8-megapixel snapshots for
the downsampling passes, and smaller targets) and the same at 1920x1080. Self-test after the change: 15 of 15 at
1280x720.

All pp-scale measurements before `7f58c55` used the first shader (same arithmetic); they are repeated on the final
binary below.

## M7b's shadow blur and these changes (read in `../engine-uc2-m7b`, branch `m7b-projectors`, 2026-10-07 23:55)

M7b lifts the Xbox shadow blur (`UnShadowProjector.cpp`, `NEW_SHADOW_BLEND`, behind its own capability
`RenDevHasXboxShadows`). It does **not** use the primitives changed here: it draws one `UPSBlurMaterial` tile
(`PS_ShadowBlur`, `BLUR_SHADOW`) with its own `UTexMatrix` offsets of +-1 and +-1/3 shadow-texture texels
(`1.0f/USize`) from `TempRenderTarget` into `ShadowTexture`, through `FScreenPrimUtil`; no
`FilterCopyRenderTarget` (whose back-buffer taps are now scaled), no read of the default render target (whose
downsampling reads go to the reference copy), no camera-effect target. Its distances are shadow-texture texels,
not screen pixels: nothing to scale. Files both branches change (hand merge): `Engine/Inc/UnRenDev.h`
(`FRenderCaps` members and constructor list, a function declaration after `RenDevHasXboxPostProcess`),
`Engine/Src/UnRenderResource.cpp` (a function after `RenDevHasXboxPostProcess`), `UC2GLDrv/Inc/UC2GLRenderDevice.h`,
`UC2GLDrv/Src/UC2GLRenderDevice.cpp` (`GetRenderCaps`, constructor, `StaticConstructor`, `Exec`),
`UC2GLDrv/Src/UC2GLShaders.cpp`, `Engine/Config/README.md`, `Engine/Config/Default.ini` and `web/Default.ini`,
`tools/dev/m11-twin.sh` (M7b: shader/program counts; here: the self-test count), `tools/dev/o9-nvidia.sh` (the
same `O9_TREE` line on both, made identical here).

## Step 7. Builds and regressions on the final code (2026-10-07 23:40; code as of `7f58c55`)

- Native (`cmake --build build-native -j6`, capped 6G): exit 0, `uc2.bin` sha256 `47d2cd24...`.
- Windows (`cmake --build build-windows -j4`, capped 6G): `[100%] Built target SDLLaunch`, exit 0; `uc2.exe`
  `3bb53fa8...`. Wasm-perf (`cmake --build build-wasm-perf -j4`, capped 6G): `[100%] Built target SDLLaunch`, exit
  0; `SDLLaunch.wasm` `6fe60cb8...` (the GL driver is in the web build: `UC2GLPostProcess.cpp` recompiled). No
  warning in any changed file in any of the three logs.
- `O9_TREE=ppo9 tools/dev/o9-nvidia.sh combiner-mesa combiner-gpu combiner-twin` (`logs/ppo9`): 9 PASS, 0 FAIL
  (Mesa programs / combine3 / combine4 35 of 35; "before" defect reproduced 22/11; NVIDIA programs 35/35, combine4
  33+2 skipped, "before" 22/11; twin 33+2 skipped; nothing left running).
- Started: the final pp-scale run (`logs/pp/final.txt`, `logs/pp2/final.txt`), `m11-twin.sh` with trees
  ppm11/ppm11t (`logs/pp/m11-twin.txt`).

## Step 8. Final run under a load of 60; the twin stopped (2026-10-08 00:10)

The final pp-scale run (`logs/pp/final.txt`, `logs/pp2/final.txt`, binary `47d2cd24`) ran while the machine's load
average was 60-65 on 12 cores (two agents' twin suites and a compile). PASS: selftest (15/15 at 1440x1080 and
2880x2160), glow 640x480 and 2880x2160 (8.33% / 13.08 against 8.54% / 12.63; reference 0: 3.12%), aspect (16:9
AutoFOV on: new 8.33% / 12.70, main's binary 4.17% / 20.47), blur-setting and blur (3.20 / 3.10 / 3.15), identity
(0 differing pixels outside the pickups, main/new twice). Not valid, timing: glow 1440x1080 (only 59 frames to
settle: the guard refused it; its numbers are those of run 1: 8.33%, 12.85), dof 2880x2160 (fit 0: both shots
without the DOF: at 0.4 frames/s the switch one second after the shot time landed in the frame the shot was taken
from) and radial 2880x2160 (inset 13.1, fit 0.55, same cause; the shots were 4 frames apart). The script now
leaves G seconds (2, 4, 12 by resolution) between a shot and the next switch and refuses shots fewer than 3 frames
apart; 1440x1080 waits 70 s to settle.

`m11-twin.sh` (trees ppm11/ppm11t, `logs/pp/m11-twin.txt`) had passed shaders, gputest, boot, **selftest (15 of
15 on desktop and twin)**, progtest (420 programs, largest difference 1 of 255) and pinned when the coordinator's
rule came (at most one game run of an agent at a time; do not judge timing failures above a load of ~16). It runs
the two bindings at once, so it was stopped by PID during the effects check (my processes and their Xvfb only).
`M11_SERIAL=1` now makes it run them one after the other; the rest (effects, m6, fallbacks, movies) is rerun so.

## Step 9. The measurement on the final binary (2026-10-08 00:40; `uc2.bin` 47d2cd24, code `7f58c55`)

`tools/dev/pp-scale.sh`, one game run at a time. Lines from `logs/pp/final.txt`, `logs/pp2/final.txt` (00:00, load
60: only the lines that were valid) and the reruns of the timing-invalid ones at a load of 13-24 with the guarded
script (`logs/pp/rerun-glow.txt`, `rerun-dof.txt`, `rerun-radial.txt`):

| Check | 640x480 | 1440x1080 new (old) | 2880x2160 new (old) | Line |
|---|---|---|---|---|
| glow width, % of height | 8.54 | 8.54 (4.17) | 8.33 (3.12) | PASS (2880: final.txt; 640/1440: rerun-glow.txt) |
| glow peak, levels | 12.88 (final.txt 12.63) | 12.95 (21.75) | 13.08 (19.72) | PASS |
| depth of field sigma, 480-line px | 3.10 | 3.15 (1.45) | 3.10 (0.70) | PASS (1440: final.txt; 2880: rerun-dof.txt) |
| radial blur, equivalent inset, 640x480 px | 9.10 | 9.20 (4.70) | 9.20 (2.30) | PASS (1440: final.txt; 2880: rerun-radial.txt) |
| concussion sigma (new / reference 0, same frame) | 3.20 / 3.20 | 3.10 / 3.25 | 3.15 / 3.00 | PASS blur, blur-setting (final.txt) |
| 16:9 AutoFOV on (1920x1080): glow width, peak | | new 8.33%, 12.70; main 4.17%, 20.47 | | PASS aspect |
| self-test | | 15/15 | 15/15 (5x5 samples) | PASS selftest |
| identity 640x480 vs main | 0 differing pixels outside the spinning pickups (twice) | | | PASS identity |

Sheets for the owner (`tools/dev/pp-scale.sh pp sheets`): `logs/pp/sheets/` - per effect `<effect>-1440x1080.png`
and `-2880x2160.png` (640x480 | before | now, whole pictures at the 480-line scale) and `-detail.png` (a part
enlarged twice from each picture's own resolution), and `aspect-1920x1080-autofov*.png` (main's binary and this
one, 16:9 with AutoFOV on and the UI box).

## Step 10. The twin, one run at a time (2026-10-08 01:10)

`M11_SERIAL=1 M11_DESKTOP_TREE=ppm11 M11_TWIN_TREE=ppm11t tools/dev/m11-twin.sh effects m6 fallbacks movies`: the
first attempt stopped after the desktop half of `effects` on a bug of my serial mode (`$!` unbound under `set -u`
when nothing had been started in the background; fixed `2eb317b`). That desktop half is `tools/dev/m7-effects.sh
ppm11` on the final binary (`logs/pp/m7-effects-ppm11-lines.txt`): **14 lines, every one `critical=0 glerrors=0
programs_failed=0+0 left_behind=0 tries=1`; praxis-panes A against the wine reference mean 2.040 (first try)**.
Rerun of the four checks started (`logs/pp/m11-twin-3.txt`).
