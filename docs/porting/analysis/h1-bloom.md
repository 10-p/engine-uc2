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
