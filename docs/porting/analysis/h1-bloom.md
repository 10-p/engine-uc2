# H-1: the bloom is blocky at high resolutions (survey, measurement, prototype): notebook

Branch `h1-bloom`, worktree `../engine-uc2-h1-bloom` (from main `7ceeee7`), run trees `<WorkUC2>/run/h1*`, pictures and
logs `<WorkUC2>/logs/h1`. Everything that runs goes through `<WorkUC2>/bin/capped` (game 2G, 3G at 2560x1440 and up;
build 6G `-j4`; the Python model 1G). Every step is committed and appended here.

The owner (2026-10-11): "Bloom is a bit low resolution, it is accurate to the xbox but at high resolution it is a bit
blocky. Can we increase the resolution or the way the blurring works so that it doesn't look blocky but still covers
the same area. Explore a bit before you begin the work and tell me if there are other buffers that could do a bump.
If the resolution increase is enough to reduce the blockiness then you could add an extra bit in GL4ES driver that
reads (for example ...) FramebufferResolutionMultiplier=2". This task is the survey and the measurement; nothing ships
before the owner decides.

## Step 1. What scales with what (read, 2026-10-11)

Sources read: `postprocess.md` (M7), `pp-scale.md` (the 480-line reference picture, merged; it is what makes the
blurs "as on the Xbox at any resolution"), `decay-flash.md` (B-2), `UCCameraEffects.cpp`, `UnRenderUtil.cpp`
(`FMultiSampleFilter`, `GetPostProcessReference`, `FilterCopyRenderTarget`), `UC2GLPostProcess.cpp`,
`UC2GLPSMaterialState.cpp` (`SetMatState_PS_Blur`), the GLSL of `PS_Blur`, `PS_BloomSelect`, `PS_EmissiveSelectBlur`,
the script defaults in `epic-sources/UCGameplay/Classes`.

Notation: back buffer W x H; reference height 480 (`PostProcessReferenceHeight`, default 480); **k = H / 480** screen
pixels per reference pixel; reference picture Rw x 480 with Rw = W / k (853.3 at 16:9, 640 at 4:3, 890.3 at
2248x1212). Every size below is Epic's fraction of the reference picture, truncated (`UCPP_TargetSize`).

### How each chain is built (script defaults: bloom and emissive `BlurTargetScale` 0.25, DOF 0.5; emissive
`NumBlurTargets` 3, `NumFeedbackPasses` 2, `FalloffRate` 0.4)

`FilterCopyRenderTarget` puts its four bilinear taps at offsets **in texels of the source** (1221 kernels: +-1 weight
2/6, +-3 weight 1/6; 1331: 3/8, 1/8; box: (+-1,+-1) 0.25 each), except when the source is the back buffer
(`SrcViewport` given), where pp-scale made them reference pixels (k back-buffer pixels) read from the 480-line
reference copy. A destination pixel reads at ((x+.5)/w, (y+.5)/h): for a 2:1 reduction that is a texel boundary of the
source, so every tap at an integer offset is the average of two source texels.

- **Bloom** (`UUCScreenBloom`, inside `UUCScreenEmissiveWithBloom`, always on): select = one bilinear read of the
  reference copy per texel through `PS_BloomSelect` (threshold = `GetAverageSceneLuminosity()` of the dynamic gamma),
  Rw/4 x 120; plus the emissive result added (`FB_Brighten`); H 1221 into Rw/8 x 120; V 1221 into Rw/8 x 60;
  **composite: that 1/8 target drawn over the whole screen, bilinear, additive, alpha 128**.
- **Emissive glow** (`UUCScreenEmissive`, `DisplayOutput` 0 inside the pair): select = `PS_EmissiveSelectBlur`, box
  taps +-2 reference px, into its own Rw/4 x 120 accumulation target, blended with alpha 0.4 over the last frame
  (streaks); copy to BlurTargets(0); twice: V 1221 into Rw/4 x 60, H 1221 into Rw/8 x 60, box (+-1 texel) back up
  into BlurTargets(0) (overwrite on the first pass, added on the second). The result goes into the bloom's select.
- **Depth of field** (`UUCScreenDOF`, always on beyond z 0.98): V 1331 from the back buffer (reference px) into
  Rw/2 x 120, H 1331 into Rw/4 x 120; composite through `PS_DepthOfField`, the Rw/4 x 120 target bilinear over the
  screen, alpha from the depth ramp.
- **Back-buffer copy** (driver, every frame): W/2 x H/2 (+ depth), box taps +-1 reference px from the reference
  copy. Also the distortion accumulation target (`FPostProcessRender`, with `CopyDepthBuffer`).

### Formulas (screen pixels)

| Quantity | Formula | 640x480 (Xbox) | 1280x720 | 2560x1440 | 2248x1212 |
|---|---|---|---|---|---|
| blur reach (every chain) | fixed in reference px, x k | 1 | x1.5 | x3 | x2.525 |
| bloom select grid / its footprint | 4 ref px / 2x2 of each 4x4 ref px | 4 px | 6 px | 12 px | 10.1 px |
| bloom final texel (composite) | 8 ref px = 8k | 8 px | 12 px | **24 px** | 20.2 px |
| DOF final texel (composite) | 4 ref px = 4k | 4 px | 6 px | 12 px | 10.1 px |
| emissive final texel (into the bloom select) | 4 ref px | 4 px | 6 px | 12 px | 10.1 px |
| back-buffer copy texel | 2 screen px | 2 | 2 | 2 | 2 |

So **the area is constant in screen space** (as a share of the picture height: pp-scale measured 8.54% of the height
at 640x480 and 1440x1080 for the DM-Deadbolt tube), and so is the number of target texels across a halo. What grows
with the resolution is the **magnification of the last target**: the composite is a bilinear magnification, i.e. the
halo is a surface of straight-line ramps between samples 8k screen pixels apart, with a corner
(a change of slope) at every sample. At 640x480 the corners are 8 px apart; at 2560x1440 24 px apart, which is what
reads as "blocky": diamond and cross shapes aligned to a 24-px grid around every small light. (A second, different
artefact of the same coarse grid: the select reads 2x2 of every 4x4 reference pixels, so a light smaller than ~4
reference px is seen or missed depending on where it falls: the halo pulses as the camera moves. That is the Xbox's
behaviour too, at 4 px instead of 12.)

### The buffer table

| Buffer | Size formula | Xbox 640x480 | 1280x720 | 2560x1440 | 2248x1212 | Format, filter | How it reaches the screen | Would more texels change the look? |
|---|---|---|---|---|---|---|---|---|
| snapshot (back buffer as texture) | W x H | (the back buffer itself) | 1280x720 | 2560x1440 | 2248x1212 | RGBA8, nearest (copy), no mips | read by full-screen passes 1:1 | already full resolution |
| depth as colour | W x H | (the depth buffer itself) | 1280x720 | 2560x1440 | 2248x1212 | RGBA8 (24-bit depth in B,G,R), linear (Epic's stage state) | read by the DOF composite 1:1 | already full resolution |
| reference copy | round(W x 480/H) x 480 | none (640x480 is the reference) | 853x480 | 853x480 | 890x480 | RGBA8, linear, area average of ceil(k)^2 bilinear samples | read by every downsampling pass (selects, DOF pass 0, back-buffer copy, luminance) | it *defines* what the thresholds see (bloom select averages 2x2 of it): a finer one lets thinner bright things bloom: changes the look |
| bloom select | Rw/4 x 120 | 160x120 | 213x120 | 213x120 | 222x120 | RGBA8, linear, pooled (also the size of the emissive targets) | through H and V | finer: no shimmer of small lights; threshold sees the same reference picture |
| bloom blur H, V | Rw/8 x 120, Rw/8 x 60 | 80x120, 80x60 | 106x120, 106x60 | 106x120, **106x60** | 111x120, 111x60 | RGBA8, linear | **last one bilinear over the screen (8x / 12x / 24x / 20.2x)**: the blocks | yes: smoother, see step 3 for what else changes |
| emissive accumulation | Rw/4 x 120 (`FAuxRenderTarget`) | 160x120 | 213x120 | 213x120 | 222x120 | RGBA8, linear | via BlurTargets(0) into the bloom select | yes, as the bloom (it is one of the bloom's inputs) |
| emissive blur 0, 1, 2 | Rw/4 x 120, Rw/4 x 60, Rw/8 x 60 | 160x120, 160x60, 80x60 | 213x120, 213x60, 106x60 | same | 222x120, 222x60, 111x60 | RGBA8, linear | box-upsampled into blur 0, added to the bloom select | as above |
| DOF blur 0, 1 | Rw/2 x 120, Rw/4 x 120 | 320x120, 160x120 | 426x120, 213x120 | same | 445x120, 222x120 | RGBA8, linear | blended over the screen where z > 0.98 (6x / 12x / 10.1x vertical magnification) | the far-field softening is a blur of the far scene: less visible; its edge against the near scene follows the full-resolution depth, not the target |
| back-buffer copy (+ depth) = distortion accumulation | W/2 x H/2 | 320x240 | 640x360 | 1280x720 | 1124x606 | RGBA8 + depth texture, linear | readers: distortion quad (2x), player-cam screens, colour remap (blurred copy), back-buffer effect | already half of the real back buffer (pp-scale kept it so); no |
| luminance (dynamic gamma) | 16x16 | 16x16 | 16x16 | 16x16 | 16x16 | RGBA8, read back | histogram only | no (a statistic) |
| motion blur accumulation (`UUCScreenMotionBlur`) | W x H | 640x480 | (full) | (full) | (full) | RGBA8 | full screen | full resolution already; no script creates one |
| radial blur (speed, teleport, sniper) | none: six zoomed copies of the snapshot drawn into the back buffer | | | | | | | nothing to raise; full resolution |
| heat shimmer (`UUCScreenHeatShimmer`) | none: a distortion material over the screen | | | | | | | creation commented out in script; nothing to raise |
| concussion blur (`UUCScreenBlur`, BlurMe) | none: four offset reads of the snapshot | | | | | | | full resolution |
| shadow textures (M7b) | the projector's USize x VSize, not pooled | | | | | | | not screen space; not part of this |
| Engine `UMotionBlur` (UT2004's) | viewport size | | | | | | | not used by UC2 |

Texture objects: every render target is one GL texture, single level (no mipmaps), `GL_LINEAR` both ways
(`UC2GL_CreateColorTarget`, the pool's `FOpenGLTexture`); the stage state that samples it is Epic's (`D3DTEXF_LINEAR`
default; `PS_MotionBlur` reads its stage 0 point-sampled).

## Step 2. A model of the chain (`tools/dev/h1-bloom-model.py`, 2026-10-11)

Pure-Python model of the bloom + emissive chain exactly as `FilterCopyRenderTarget` samples it (bilinear taps,
texel-boundary convention), in float (no 8-bit rounding), for a 2x2 reference-pixel light at 3x3 positions across one
coarse cell, at 2560x1440 (`H1_LIGHT=2 h1-bloom-model.py table 1440`). Halo of the row through the light, screen px:

| Variant | peak | sigma | r10 | r50 | kink (corners) | jitter (peak spread over positions) |
|---|---|---|---|---|---|---|
| today: scale 1, bilinear composite | 0.00570 | 40.4 | 84.5 | 43.0 | 12.65 | 1.19 |
| scale 1, bicubic B=0.5 C=0.25 composite | 0.00573 | 40.3 | 83.5 | 43.0 | **1.41** | 1.23 |
| scale 1, cubic B-spline composite | 0.00537 | 41.6 | 86.5 | 44.0 | 1.14 | 1.20 |
| scale 1, Catmull-Rom composite | 0.00610 | 39.0 | 79.0 | 41.0 | 1.64 | 1.26 |
| scale 1, cascade of 2x box upsamples (engine-only alternative) | 0.00487 | 43.5 | 90.5 | 46.5 | 1.44-3.84 | 1.15 |
| scale 2, taps left in texels (= a higher PostProcessReferenceHeight) | 0.02225 | 20.6 | 43.0 | 21.5 | 6.85 | 0.02 |
| scale 2, taps x2, no fill | 0.00510 | 41.4 | 86.0 | 42.5 | **69.0** (comb: holes) | 0.99 |
| scale 2, taps x2 + fill (the prototype) | 0.00566 | 40.2 | 84.0 | 44.5 | 19.7 | **0.007** |
| scale 4, taps x4 + fill | 0.00565 | 40.2 | 84.5 | 46.5 | 30.4 | 0.000 |

What it says:
- The bicubic composite with B=0.5, C=0.25 (same second moment as bilinear) keeps size and peak exactly and removes
  the corners (kink 12.7 -> 1.4). B-spline widens by 3% and dims the peak 6%; Catmull-Rom narrows; the engine-only
  cascade of box upsamples widens 8% and dims 15% (rejected).
- Raising the texel count with the taps scaled **and filled** reproduces the Xbox's kernel (same peak, sigma, r10)
  sampled more finely: the shimmer of small lights (peak varying +-60% as they move across the 4-px select grid)
  disappears, but the kernel itself is a staircase (Epic's 1221 taps on 2-texel boxes): finer sampling shows its
  terraces instead of hiding them under the 24-px bilinear ramps (profile at scale 4: 100 99 98 97 90 79 68 64 62
  61 60 57 50 38 28 22 21 19 ...). Scaling the taps without the fill leaves holes (a comb) and must not be done;
  leaving the taps in texels halves the blur (the pre-pp-scale behaviour).

## Step 3. Pictures and the radius measurement (GPU, 2026-10-11)

Runner: `tools/dev/fx-owner.sh h1 custom` with `UC2_GPU=1` (Quadro P620, SDL offscreen, `bin/capped 2G`), DM-Deadbolt,
first person (`ToggleThirdCam`) at PlayerStart7 (`NETPLACE FxCam 1229.6 -672.9 -108 40448 0`): a white wall lamp
panel (bloom) and a blue light box (emissive glow) in view; game time frozen (`TimeDilation 0.0001`); the depth of
field switched off for the bloom measurements. Resolution through `-ini:` (`WindowedViewportX/Y`), AutoFOV off, so
the view is the same picture at 1280x720 and 2560x1440 (2248x1212 shows the same horizontal field, cropped
vertically). Scenarios: `tools/dev/h1/{base,proto,fps}.scen` (`UC2_GPU=1 FX_MAP=DM-Deadbolt FX_NAME=<name> FX_SCENARIO=tools/dev/h1/proto.scen FX_ARGS="-ini:System:[SDL2Drv.SDL2Client]:WindowedViewportX=2560 ...Y=1440" tools/dev/fx-owner.sh h1 custom`); pictures in `<WorkUC2>/logs/h1`.
Measure: `tools/dev/h1-measure.py profile` (glow = with - without, mean RGB, along a row, positions in the 1280x720
frame).

Baseline (main's behaviour; `fx-base-<res>-gpu-*.png`, glow without DOF): **measured**

| Profile (720-frame px) | 1280x720 | 2560x1440 |
|---|---|---|
| lamp, leftwards: peak / r50 / r25 / r10 | 88.9 / 40 / 52 / 64 | 90.0 / 40.5 / 52 / 64 |
| lamp, rightwards | 121.0 / 36 / 48 / 61 | 121.0 / 36.5 / 48.5 / 60.5 |
| blue light box, leftwards | 17.0 / 33 / 44 / 57 | 17.6 / 33.5 / 45.5 / 58 |

**The area is constant in screen space today** (radii equal within 1 px of the 720 frame, peaks equal): confirmed.
Repeat noise in one frozen run (two shots of the same settings, `s1` and `s1again`): up to 3 px in the blue box's r10
and 1.9 levels in its peak (the emissive accumulation is not perfectly still), 1 px / 1 level for the lamp.

Pictures: `h1-base-overview.png`; `h1-base-blue-glow-zoom4.png` and `h1-base-lamp-glow-zoom4.png` (4x / 2x zoom of the
glow alone at both resolutions); `h1-base-<res>-glow-x6.png` (whole frame). What is seen: at 2560x1440 the outer,
dim part of every glow is a set of 8-bit bands whose outlines are polygons on a 24-px grid (the bilinear patches of
the 106x60 target); at 1280x720 the same polygons are 12 px. That is the blockiness: dim (1-5 levels), and it is the
shape of the quantisation bands that gives it away, not the bright core.

My numeric "roughness" measures (`rough`: second differences; `resid`: departure from a Gaussian-smoothed copy) do
**not** separate the variants: the glow difference is modulated by the scene texture under it (`FB_Brighten` is a
screen blend: src x (1 - dst)), and in the dim ring the glow is 1-2 levels, i.e. rounding. They are in
`h1-proto-measure.txt`, `h1-proto-resid.txt` and are not evidence either way. The blockiness verdicts below are
**seen only**, from the gain sheets.

## Step 4. The prototype (test keys and console; NOT settings)

Code (all marked `H-1 prototype`):
- `UnRenDev.h`: `FRenderCaps::PostProcessScale` (default 1), `RenDevPostProcessScale()` (`UnRenderResource.cpp`).
- `UnRenderUtil.h/.cpp`: `FilterCopyRenderTarget(..., INT TapScale=1)`. For a target-to-target pass with TapScale
  > 1 the source is first filled along each axis the filter reaches (TapScale point taps 2 texels apart, equal
  weights; a private scratch target per size and axis, `H1ScratchTarget`), then the taps move TapScale times as many
  texels. PS_Blur adds its taps in doubled, clamped pairs, so the fill puts its taps in slots 0 and 2 (scale 2) or
  0..3 (scale 4); scale 3 can clamp above a mean of 3/4 (measuring only). Back-buffer passes (with a viewport) are
  unchanged: their taps are reference pixels read from the 480-line reference copy, which still decides what the
  bloom threshold sees (so thin bright things do not start to bloom).
- `UCCameraEffects.cpp`: DOF, emissive and bloom targets x PostProcessScale; their seven target-to-target passes pass
  TapScale.
- `UC2GLDrv`: `PostProcessScale`, `PostProcessSmooth` test keys (`-ini:System:[UC2GLDrv.UC2GLRenderDevice]:...`),
  console `PPSCALE [1..4]`, `PPSMOOTH [0|1]`; `GetRenderCaps` passes the scale. `PostProcessSmooth`: the desktop
  fixed-function program reads a render target that is magnified more than 2x into the main framebuffer (only the
  bloom's composite qualifies) with a 16-tap bicubic (B=0.5, C=0.25). **Desktop only** (GL4ES draws fixed function
  with its own generated shaders); a shippable version would draw the composite through a driver program.
- Known limits: at scale 4 the DOF's first pass and the emissive select read the reference copy 1:1, so their taps
  land on texel centres and cover one texel instead of two (a narrower first pass); the self-test `pp-sizes` expects
  scale 1.

Runs `fx-proto-<res>-gpu-<shot>.png` (one run per resolution, frozen; shots `s1` today, `s1smooth`, `s2`, `s4`,
`s2smooth`, `s3`, `s1again`, `off` = no glow, `dof1`/`dof2` = DOF only at scale 1/2). Builds: native `-j4`, capped
6G, exit 0 (`logs/h1/build-1.log`). Every run: 0 Critical, 0 GL errors, 0 failed programs, nothing left behind.

Radii, 720-frame px (`h1-proto-measure.txt`): **measured**

| | lamp left peak / r50 / r10 | blue box peak / r50 / r10 |
|---|---|---|
| 1280x720 scale 1 (s1 / s1again) | 89.0 / 40 / 64 ; 88.1 / 40 / 63 | 18.2 / 35 / 67 ; 16.3 / 33 / 59 |
| 1280x720 bicubic | 88.9 / 40 / 63 | 18.4 / 35 / 67 |
| 1280x720 scale 2 / 4 | 89.1 / 40 / 62 ; 89.5 / 40 / 63 | 15.9 / 29 / 51 ; 17.5 / 29 / 52 |
| 2560x1440 scale 1 (s1 / s1again) | 89.2 / 40.5 / 63 ; 89.0 / 40.5 / 63 | 17.6 / 33.5 / 56 ; 16.7 / 33.5 / 59 |
| 2560x1440 bicubic | 89.1 / 40.5 / 63 | 17.7 / 33.5 / 57 |
| 2560x1440 scale 2 / 3 / 4 | 89.5 / 40.5 / 62 ; 91.3 / 40.5 / 64.5 ; 90.0 / 40.5 / 63.5 | 16.3 / 28 / 51.5 ; 18.1 / 30 / 57 ; 17.8 / 29.5 / 52 |
| 2248x1212 scale 1 / bicubic | 96.0 / 40.4 / 65.5 ; 95.8 / 41.0 / 64.9 | 20.4 / 30.7 / 56.9 ; 20.4 / 30.7 / 56.4 |
| 2248x1212 scale 2 / 4 | 90.1 / 39.9 / 60.4 ; 92.1 / 39.9 / 62.1 | 16.9 / 27.9 / 47.3 ; 18.6 / 29.0 / 52.4 |

- **Bicubic composite**: radius and peak unchanged (within 1 px / 1 level, below the repeat noise). Seen: the band
  outlines are a little rounder; the change is small (largest pixel difference to scale 1: 11 levels, mean 0.04).
- **Scale 2 / 4**: the white lamp's halo keeps its size (r50 equal, r10 within 1-2 px) and peak. Seen
  (`h1-proto-2560-blue-glow8.png`, `h1-proto-2248-blue-glow8.png`, `h1-proto-2560-lamp-glow4.png`): the band
  outlines become round, the 24-px polygons are gone. **But the dim outer tail of the emissive glow is lost**: the
  blue box's r50 drops from 33.5 to 28-29.5 and its r10 from 56-59 to 51.5-52 (beyond the repeat noise); in the gain
  sheets the wide faint outer halo of scale 1 is absent at scale 2 and 4. Cause **not established**; suspect: 8-bit
  rounding of a tail of 1-2 levels through the extra fill passes inside the emissive feedback loop (two feedback
  rounds, each V, H, box-up, now each with fill passes), which the float model cannot show. Scale 3 keeps more of it
  (r10 57) but clamps in its fill.

Frame cost, 2560x1440, Quadro P620, one run, time frozen, same view, two rounds of 8-s windows, load 12-15 (shared
machine: **measured, noisy**; `fx-fpsab-2560-gpu.out`): post-processing off 8.2 / 9.1 ms; on, scale 1: 12.8, 12.0,
11.7, 12.7 ms; scale 2: 14.1, 12.7; scale 4: 14.3, 12.9; bicubic: 14.2, 12.7. So about **+0.5 to +1.3 ms** for
scale 2, +0.6 to +1.5 for scale 4 and +0.5 to +1.4 for the bicubic composite, against ~3.5-4 ms for the whole
post-processing. (Separate fps-bench runs per setting were useless: 52 / 75 / 47 / 48 / 66 fps with no order.)

## Step 5. Other buffers

- **DOF blur targets** (Rw/4 x 120 final, 12 px texels at 1440p): the same chain shape; scale 2 changes the picture
  (mean 2.5 levels at 2560x1440, `dof1` vs `dof2`), seen at 3x (`h1-dof-2560-zoom.png`): no clear visible
  improvement; its blend is masked per pixel by the full-resolution depth, so its edges are already sharp. Would
  follow the bloom if a scale ships; not a priority.
- **Emissive targets**: part of the bloom (its result is added into the bloom's select); they scale together.
- **Back-buffer copy / distortion accumulation**: half of the real back buffer already: no.
- **Reference copy**: no: it is what the bloom threshold and every first pass see; finer would let thinner bright
  things bloom (a change of look, not of smoothness).
- **Motion blur accumulation, radial blur, heat shimmer, concussion blur**: full resolution or no target: nothing to
  raise. **Luminance 16x16**: a statistic. **Shadow textures**: not screen space.
- Seen in passing (not this task): the DOF on/off difference (`h1-dof-diff-x10.png`) is large along the edges of the
  first-person weapon, i.e. the always-on DOF blends over the weapon; to look at separately.

## Recommendation (for the owner's decision)

1. Do not ship either prototype as a default yet.
2. The resolution increase does what the owner hoped for the shape (round, no 24-px polygons, and no shimmer of
   small lights) at about 1 ms per frame on the P620 at 1440p, and keeps the bloom's size and brightness; but as
   prototyped it shrinks the faint outer part of emissive glows by 10-15% of their radius. Next step if wanted:
   diagnose that tail (8-bit rounding in the feedback loop is my suspicion; test by skipping the fill inside the
   emissive feedback, or by moving the feedback to scale 1), then offer it as an ini setting, default 1 (Xbox).
3. The bicubic composite keeps size and brightness exactly and costs about 1 ms, but changes little on screen
   because the visible blocks are mostly 8-bit bands in a 1-5 level glow; and it needs a driver program to work on
   GL4ES. Not worth shipping alone.
4. Name: the setting scales the post-process blur targets, not the framebuffer:
   `[UC2GLDrv.UC2GLRenderDevice] PostProcessTargetScale=2` (also read from the GL4ES section), 1 = the Xbox's texel
   count. "FramebufferResolutionMultiplier" would suggest supersampling the whole picture.

# Part 2 (2026-10-11): why the emissive tail shrank at scale 2, the fix, the final key

Brief: find why the emissive glow's outer part shrinks 10-15% at x2/x4 (blue box r50 33.5 -> 28, r10 56-59 -> 51.5),
fix it so that every radius of the lamp and the blue box at x2 matches scale 1 within the repeat noise, then make
the key final (`PostProcessTargetScale`), remove the test-only artefacts.

Measurement points (recovered by search, the prototype did not write them down): `h1-measure.py profile <with> <off>
943 280 -1` (lamp, leftwards) and `... 545 180 -1` (blue box, leftwards from the gap between its slabs); these
reproduce every number of the step-4 table.

## P2-1. Diagnosis (temporary diagnostics, not committed: `logs/h1/h1-part2-diag.patch`)

A console switch to run the emissive chain and the bloom chain at different scales in one frozen run, a dump of the
render targets of every stage (glReadPixels to PPM), and CPU re-computations of each pass from the dumped input
(scripts in `logs/h1/dump/`: `dumpan.py`, `roundtest.py`, `chainsim*.py`, `mass.py`). DM-Deadbolt, 2560x1440, GPU,
time frozen, DOF off.

1. **Not the feedback loop as such.** Emissive chain at x1 with the bloom at x2: the blue box still loses (r50 29.5,
   r10 53); emissive at x2 with the bloom at x1: radii back (33.5 / 55) but the peak drops (14.6 vs 17.7): the
   radius alone hides a loss of the whole glow. The bloom passes at x2 are right: GL's B0 and B1 equal the CPU
   model (fill + scaled taps) of the dumped select within 1.3 levels. The bloom threshold is the same at both
   scales (0.3176 / 0.3195). So the loss is in the values the emissive chain carries, not in the kernel.
2. **Mass per stage** (sum of the target over a box around the light, per texel of the picture; green): x1 (fresh
   accumulation) acc 0.044 -> V 0.043 -> H 0.042 -> box-up 0.040 -> V 0.039 -> H 0.037; x2: acc 0.040 -> 0.039 ->
   0.036 -> 0.029 -> 0.025 -> 0.021. Linear passes conserve mass; x2 loses 48% of it by the last stage, x1 16%.
   The accumulation itself is equal at both scales (whole picture 0.741 vs 0.735).
3. **The GPU rounds exact halves down.** Re-computing the dumped V pass from its dumped input with 8-bit rounding
   of every write: "nearest, ties down" reproduces GL bit for bit (0 texels of 51,120 differ at x2, 1 of 12,780 at
   x1); "nearest, ties up" misses 847, truncation 984. Quadro P620 (NVIDIA); the GL spec leaves the choice open.
4. **Cause.** The prototype's fill pass writes the average of two 8-bit texels: half of its texels are exact ties
   (k + 1/2), each rounded down, -0.25 level per texel on average, and a 1-level texel next to a 0 becomes 0. x2
   adds ten such writes per frame (four per emissive feedback round, two in the bloom), most of them on the dim
   emissive glow, which is re-blurred twice: the glow loses a quarter to a half of its mass and its tail goes first.
   Epic's own passes (1221, box) put their taps on texel boundaries with weights 1/6, 2/6, 1/4: their outputs are
   sums over 12 or 16, ties are rare. The lamp is barely touched (bright, wide, already above the tail).
   Not a cause: the 8-bit accumulation (`x = round(0.4 s + 0.6 x)`) is the same at both scales; it is
   history-dependent (the fixed point lies anywhere in s-1..s+1 depending on whether it was approached from above or
   below), which is why two scale-1 shots of the same frame differ (whole-picture mass 0.968 for the target created
   at the level start vs 0.735 for one created fresh; r10 56 vs 59): that is the repeat noise of step 3.
5. **Prediction** (CPU chain from GL's own x2 accumulation, rounding as the GPU, validated: the 8-bit-fill variant
   lands within 0.5% of GL's emissive result): emissive result mass G / B: x1 1.332 / 1.437 (GL 1.302 / 1.405);
   x2 as prototyped 1.168 / 1.250 (GL 1.163 / 1.245); x2 with the fill kept exactly (float, half float or 10-bit
   unorm) 1.338 / 1.437; x2 with 8-bit fills whose ties go up on x and down on y 1.333 / 1.428; every fill
   rounding ties up 1.559 / 1.660 (too much).

First fix tried (failed, kept here as evidence): keep the 8-bit fill but decide its ties explicitly and in balance: the fill weights are 1/N x (1 + 1/2048) for a fill along x (its ties round up) and
1/N x (1 - 1/2048) along y (ties round down, as the GPU does anyway). A fill's output is k or k + 1/2 (k + 1/4,
1/2, 3/4 at x4), so the factor (at most 0.125 level at 255) moves nothing but the exact ties, on any GPU that rounds
to nearest. The chains alternate the axes (emissive per round: y, x, x, y; bloom: x, y), so the ups and downs
cancel. (Higher-precision scratch targets were the alternative: GL4ES maps RGB10_A2 to RGBA8 with a warning and
RGBA16F only with half-float extensions, so the web build would have kept the loss.)

## P2-2. Fix attempt 1 failed; attempt 2: the fill in the shader

Attempt 1 (the weights above, `1/N (1 +- 1/2048)`) changed nothing: in-run on the GPU the blue box at x2 measured
r50 28.0, r10 48.0 against 33.5 / 59-64.5 at x1. The self-test (below, run on the GPU) said why: the TapScale-2 path's
mean error stayed at -0.108 (horizontal: its ties were meant to go up) and -0.199 (box). A push of 2.4e-4 of a step
on an exact half does not survive the GPU's float-to-8-bit conversion, which is not exact near the half (Direct3D
10+ allows 0.6 of a unit in the last place for that conversion; the NVIDIA part evidently uses it). Any push large
enough to survive would also move values that are not ties (it is multiplicative). New diagnosis, so a new approach.

Attempt 2 (the brief's "single-pass kernel with 2x the taps"): no fill pass at all. A bilinear read of the fill at
position p equals the mean of bilinear reads of the source at p - 1 and p + 1 texels (the fill is linear and a shift
commutes with the interpolation), so the pass itself can take, for every one of its four taps, the mean of 2 (x4: 4)
reads 2 texels apart along each axis the filter reaches, in floating point, and round once, as at scale 1.
- Engine (`FilterCopyRenderTarget`): for TapScale 2/4 the taps move TapScale texels per offset as before and
  `UPSBlurMaterial::BlurBlendMode` gets flags `UC2_BLUR_FILL_X/Y/4` (`UnRenderUtil.h`; Epic's modes use the low
  bits 0..3). No scratch targets, no extra passes.
- Driver: `SetMatState_PS_Blur` splits the flags off into `FOpenGLMaterialState::BlurFill`; the program draw adds
  fragment variants `PSV_FillX/Y/4`, whose source is the generated PS_Blur with each `texture2DProj(u_texN, tc)`
  replaced by `uc2_fill(u_texN, tc)` (`UC2GL_WithFill`, UC2GLShaders.cpp), and sets `u_fillStep` = one texel of the
  stage-0 texture. GLSL 1.20 / ES 1.00 alike: the same code on desktop GL and GL4ES.
- Self-test: blur-h, blur-v, blur-box each also run the pass with TapScale 2 against the CPU (taps x2, each the mean
  of 2 reads per axis) and report the largest and the mean signed error (|mean| > 0.1 fails). Still 15 tests.
  GPU (Quadro P620), AntiAliasing=0: **numerically checked**: blur-h x1 0.50, x2 0.54, mean -0.033; blur-v 0.50 /
  0.54 / -0.026; blur-box 0.50 / 0.50 / -0.024; 15 passed, 0 failed; programs: 97 x 5, 0 could not be built. (With
  attempt 1's 8-bit fill the same test failed blur-h and blur-box: mean -0.108 and -0.199.)
