# B-2: DM-Decay white cloud that stays on screen (owner report 2026-10-10): notebook

Worktree `../engine-uc2-b2`, branch `b2-decay-flash` from main `81fd44e`; run trees `run/b2*`, pictures and logs
`<WorkUC2>/logs/b2*`. Every run through `<WorkUC2>/bin/capped` (game 2G, builds 6G).

The report (verbatim): "An additional bug in DM-Decay. If I go to a particular spot in the map (whereami being
-337.1 2.3 -418.4 32950 1193) in third person I get this extremely bright flash that doesn't go away. I can rotate
the camera quite a bit and it doesn't go away and it doesn't move, it is like an overlay gone crazy. I can move a bit
and eventually goes away." Screenshot `<WorkUC2>/temp/blownout.png` (2285x1290): stone hall with red banners, a soft
white cloud over the middle (brightest top-right of centre, a diagonal band), HUD unaffected, pawn at the bottom.
The player name on the HUD, `Player-cc0bc6`, is the web frontend's generated name pattern: the owner probably
played the web preview (to confirm).

Tools added: `tools/dev/decay-flash.sh` (one capped run of an m10-inject scenario on DM-Decay with no bots, any
window size, AA, GPU or Mesa, desktop or twin) and `tools/dev/decay-flash.py` (luminance of the middle 50% x 50%
against the rest, white fraction, saturation); scenarios in `tools/dev/decay-flash/`.

## Log

### Step 0 (13:40): reading, build, tools

Native build (`bin/capped 6G`, exit 0), run tree `run/b2` (`tools/dev/mkrun-uc2.sh build-native b2`).

### Step 1 (13:47-13:56): reproduced on the GL4ES twin only; cause found; fixed in the driver

The coordinator relayed (13:45) that the owner's screenshot was taken on the **web preview** (WebGL 2 through
GL4ES, AA off). The HUD name `Player-cc0bc6` agrees (the frontend launcher's generated name).

**Reproduction** (`tools/dev/decay-flash/repro.scen`; Xvfb + Mesa, 1280x720, AA 0, no bots; numbers = mean luma of
the middle 50% x 50% / of the rest, from `tools/dev/decay-flash.py`):

| shot | twin before | twin after fix |
|---|---|---|
| t0 (just teleported; camera still settling) | 34.2 / 35.7 | 36.5 / 37.8 |
| yaw +30 / +60 / -30 | 41.5 / 37.0 / 42.5 (middle) | 40.8 / 36.0 / 42.3 |
| pitch down (-3000) | 72.1 / 51.7 (veil) | 31.9 / 41.5 |
| **back at the owner's view** | **210.4 / 162.1**, 3.2% white, saturation 0.028 | **37.8 / 38.0**, 0%, 0.305 |
| moved 200 back | 56.6 / 41.7 (fading) | 34.8 / 35.6 |
| moved 200 forward | 43.5 / 39.4 | 42.1 / 38.9 |
| back again | 194.0 / 157.0 | 36.0 / 37.3 |

Pictures `logs/b2/df-twin-repro-<shot>.png` (before) and `df-twin-repro-fix-<shot>.png` (after). The "back" picture
before the fix is the owner's screenshot: the soft white cloud, brightest top-right of centre, the whole frame
washed out, the HUD untouched. The **desktop device does not reproduce it** at the same view
(`df-desk-ab-on.png`: 36.5 / 38.0).

**A/B** (`ab.scen`, twin, before the fix): `set UCScreenEmissive Enabled 0`, `set UCScreenBloom Enabled 0`,
`set UCScreenDOF Enabled 0`, `DYNAMICGAMMA 0`: no change (208 / 155 each; the game creates
`UCScreenEmissiveWithBloom`, so the first two `set`s touch nothing that runs: they say nothing); `POSTPROCESS 0`:
36.3 / 37.0. Post-processing, not the dynamic gamma.

**What draws it** (`GLPIXTRACE 640 300 all`, `df-twin-ab.out` and `df-desk-ab.out`): the main framebuffer pixel is
88,103,119 on both bindings until the bloom's final `FB_Brighten` quad (twin pass #91: 88 -> 226). Upstream of it
on the twin the emissive select target already holds 119 (desktop 0) and the bloom select 123 (desktop 0), although
the back buffer's alpha (the emissive mask) is 0 everywhere. So the emissive/bloom chain reads something that is
not the back buffer.

**The dump** (new: `GLPIXTRACE x y all dump`, `UC2GLPixelTrace.cpp`; files in `logs/b2/twin-dump/`): in the cloud
frame the first refresh of the pp-scale reference copy (`PixTrace-131-ref391.png`, the 853x480 picture every
downsampling pass reads: emissive select, bloom select, DOF, luminance) is **not the back buffer
(`-main391`, luma 37) but a texture: white RGB (luma 253) with a cloud-shaped alpha** (`-ref391-a.png`): a smoke
particle's texture stretched over the screen. The later refreshes of the same frame (`ref392`, `ref393`) are
correct. The emissive select multiplies RGB by alpha and blurs it, the bloom adds it to the picture: the cloud.
It is fixed on the screen because it is a texture drawn as a full-screen quad, and it depends on the place because
of what is drawn last before the post-processing.

**Cause.** The last scene draw before the post-processing at this view is DM-Decay's water,
`Shader T_Liquid.Decay.T_Liquid_Decay_Water_C_SD` (pass #90): fixed function, **stage 0 a cubemap**
(`TexEnvMap` on `T_Liquid_Decay_Water_C_CM`), so the interface leaves `GL_TEXTURE_CUBE_MAP` enabled on unit 0
(`UC2GLRenderInterface.cpp:1405-1409`). The next draw is the device's own copy quad (`DrawPostQuad`,
`UC2GLPostProcess.cpp`) with a GLSL program: `glActiveTexture(0); glBindTexture(GL_TEXTURE_2D, MainColorTexture)`.
GL4ES defers 2D binds to the draw (`gl4es_glBindTexture`, `texture_params.c:178-227`: cube maps are bound at once,
2D/1D/3D/rect later) and at the draw `realize_textures` (`texture_params.c:868-949`, fork branch
`fix/cubemap-bind-active-unit` at `6b6bccd`) chooses, for every unit, which of the unit's bound textures goes into
the GLES 2D slot from the enable flags of the **active** unit (`int tmp = glstate->enable.texture[glstate->
texture.active];`, line 870), and **skips the 2D bind when that says cube map**. The GLES 2D slot of unit 0 keeps the
last 2D texture GL4ES made real there: the smoke particle drawn just before (#89). Desktop GL ignores the enables
while a program is in use, so the desktop device never shows it. Two GL4ES defects in that line: the active unit's
flags are used for every unit (should be unit i's), and a GLSL draw's 2D slot is made to depend on fixed-function
enables at all.

**Fix (driver, both bindings): `UUC2GLRenderDevice::BindProgramTexture2D`** (`UC2GLPostProcess.cpp:310`): the
device's own GLSL draws bind their 2D textures through it, which also sets that unit's fixed-function enables to
2D (cube off) when the unit has a texture environment. Used by `DrawPostQuad` (snapshot, reference copy, depth
encode, depth copy: `:350`), the present pass (main colour and the ramp: `UC2GLRenderDevice.cpp:1633-1636`) and the
split-screen ramps (`UC2GLPostProcess.cpp:655`). The interface re-applies the material's own enables at the next
draw (`RestoreAfterDeviceDraw` -> `InvalidateHardwareState`), so nothing else changes. On desktop GL the enables
are ignored under a program: no change there by construction (to be confirmed by the regressions).

Open, next steps: the same GL4ES defect for fixed-function draws (a unit > 0 whose 2D texture is not realized
when unit 0 has a cube map: the water's own stage 1 mask) and for the interface's pixel-shader programs with a cube
on stage 0; the GL4ES fix itself (fork; needs the owner's go-ahead to push); native GPU, AA 4, 2285x1290; mouse
rotation; regressions.

### Step 2 (13:58-14:20): the cause proved from the GL4ES side too; native GPU, the owner's size, AA 0/4, mouse

**GL4ES-side proof.** `docs/porting/analysis/decay-flash-gl4es.patch` (against the fork's `fix/cubemap-bind-active-unit`
at `6b6bccd`, `src/gl/texture_params.c` `realize_textures`): the target of unit i from unit i's enables, and with a
cube map enabled the unit's GLES 2D slot is still made to hold its 2D texture (a GLSL sampler2D reads it). Built
into a test binary **with the driver fix switched off** (`uc2-gl4esonly.bin`, not kept): the twin's owner view
36.9 / 37.8 instead of 210 / 162 (`df-twin-repro-gl4esonly-*`). Either side removes the cloud. Not pushed: the fork
is shared with engine-ut2004 and its pushes need the owner's go-ahead; the build here fetches the branch as it is,
so the shipped fix is the driver's. Against the desktop the GL4ES patch changes nothing measurable in these views
(desktop vs twin mean |diff| 0.8-1.5 of 255 with either fix; animated water and pawn).

**Native, Quadro P620 (`UC2_GPU=1`, SDL offscreen; GL4ES on NVIDIA's ES 3.2), `rotate.scen`** (the owner's view, then
the camera turned with relative mouse motion through the input channel, 2002 yaw units = 11 degrees per step,
6 steps right, 12 left, back): before the fix (`uc2-before.bin` = main `81fd44e`'s driver) the twin shows the cloud
from yaw 30948 to 38957 (about 44 degrees: shots r0-r2, l4-l7: 207 / 154 each, identical frame to frame: "it
doesn't move"), and it is gone at 40959 and beyond and at 28946 and below (36-44 / 33-42): exactly the owner's "I can
rotate the camera quite a bit and it doesn't go away and it doesn't move" (the water stays the last scene draw over
that range). After the fix every step is 34-43 / 34-42 (`df-gpu-twin-rotate-{before,fix}.lum`). The desktop device
on the GPU never shows it, before or after (`df-gpu-desk-rotate-{before,fix}`: r0 36.8 / 36.6).

**The owner's size 2285x1290, GPU twin**: AA 0 before 207.0 / 154.1, after 36.3 / 38.5; AA 4 before 206.6 / 153.4,
after 36.7 / 38.7 (`df-gpu-twin-owner-aa{0,4}-{before,fix}-r0.png`; side by side
`logs/b2/owner-view-before-after-gpu-twin.png`: the "before" is the owner's `blownout.png`).
(The runner converts the engine's BMPs itself: `appCreateBitmap`, Core `UnMisc.cpp:1558`, writes 24-bit rows without
the 4-byte padding, which PIL rejects at widths like 2285. Epic's defect, harmless at 1280; not touched.)

**The effect is still there.** With the fix, the dump of the cloud frame (`logs/b2/twin-dump-fix/`): every reference
copy equals the back buffer (mean |diff| 0.39 of 255 against the main colour downsized, alpha 0 like the back
buffer). Post-processing on minus off at the owner's view: twin 0.83, desktop 0.88 mean |diff| (`df-*-ab-fix-on/nopp`),
and desktop vs twin with it on: 0.33: the glow and bloom contribute the same on both bindings.

### Step 3 (14:15-15:20): regressions, part 1 (machine load 15-20 from another agent's llvmpipe runs)

- Builds of `1f351a8`+: native (`logs/b2/build-native.log`, exit 0), windows (`build-windows.log`, exit 0; the only
  warnings in the touched headers are the pre-existing `UC2GLRenderDevice.h:70` -Wwrite-strings), wasm-perf
  (`build-wasm-perf.log`, exit 0; `SDLLaunch.wasm` sha256 `e1b2b839...afbc9`). All inside `bin/capped 6G`.
- `tools/dev/m7-effects.sh b2`, desktop binding: first with `UC2_BIN=uc2-before.bin` (main's driver;
  `logs/b2/m7-before.txt`), its B pictures installed as `fx-*-b.ref.png`, then the fixed `uc2.bin`
  (`m7-after.txt`). Self-test 15 / 15 both. Every effect: critical 0, GL errors 0, programs failed 0+0; B against
  main's B: bloom 0.29, blur 3.53, berserk 0.47, liandri 0.73, pause 0.25, radial 0.81, dyngamma 1.80, dof-alpha 0.54,
  azure 0.60, octane 0.73, decay 1.12, undertow 0.18, recoil 0.31, praxis-panes 0.53 (mean of 255; animated pawn,
  pickups and timing: blur and dyngamma are the timing-sensitive ones the report lists); praxis A against the wine
  reference 2.34 (before 2.56; limit 8). One `left_behind=1` (berserk) was the next run's process starting under load.
  Strict desktop identity is the pp-scale `identity` check below.
- `tools/dev/pp-scale.sh b2` (all default checks; `run/b2/System/uc2-base.bin` = main's driver; load 20-23 throughout;
  `logs/b2/pp-scale-new.txt`): PASS selftest (15/15 at 1440x1080 and 2880x2160), glow at 640x480 / 1440x1080 /
  2880x2160, dof, blur-setting, radial, **identity** (640x480, post-processing on, frozen frame: base vs new 0
  differing pixels outside the two spinning pickups' rows, as base vs base), aspect (new peak 13.45 width 8.12% =
  main's binary exactly). FAIL pp-blur: the concussion blur's sigma 3.25 / 2.10 / 3.85 at the three sizes (tolerance
  0.30); BlurMe's blur decays over time, so a shot's sigma depends on when it lands after the command, and the
  machine was at load 22. Main's binary in the same check: below.
- pp-blur with main's binary under the same load (`UC2_BIN=uc2-base.bin tools/dev/pp-scale.sh b2 blur`,
  `logs/b2/pp-blur-base.txt`): FAIL too (3.50 / 3.55 / 7.00, and blur-setting FAILs there): the check is timing
  dependent under load, not a change of this branch.
- `tools/dev/fps-bench.sh` DM-Praxis 20 s 1920x1080 on the Quadro (load 10-12; tree `run/b2base` = main's driver):
  main 74.0 / 71.7 fps, this branch 72.3 / 72.2 fps (alternating runs): unchanged within run-to-run noise.

### Step 4 (16:20-16:50): the owner's second spot, DM-Fury (803.2 79.3 1973.7 51049 -1153), and the owner's clue

The owner (via the coordinator): on the web preview the overlay at DM-Fury switches between shapes while the player
stands still (video `<WorkUC2>/temp/blown out switch.mp4`; frames at 4 per second in `logs/b2/fury-video/`, sheet
`logs/b2/fury-video-sheet.png`: normal frames, a puffy cloud, a horizontal white streak, in no fixed cadence; middle
luma 57-61 normal, 97-110 cloud, 195-197 streak), the lamps lose their bloom while it is there, and it looks like a
particle sprite (drip, splash, green smoke, DM-Decay's snow) blown up to the whole screen.

That is exactly the mechanism of step 1: the full-screen thing is a **particle's texture**, not a particle drawn
large. The main framebuffer is right (pixtrace: the traced pixel is unchanged until the bloom adds to it); the
reference copy that feeds the emissive select, the bloom select, the depth of field's first pass and the luminance
is, in those frames, the particle texture GL4ES last made real on unit 0, drawn as the full-screen copy quad.
The emissive select (texture RGB x alpha) and the bloom select then work on that texture instead of the picture, so
the real glow of the lamps is not computed that frame ("the subtle bloom disappears") and the bloom adds the
blurred particle texture over everything. It switches shape with whatever particle was drawn just before the
cube-mapped surface in that frame (sorting of translucent actors by distance changes with the particles' own
motion); "only in some areas" = where a cube-mapped water/puddle surface is the last thing drawn before the
post-processing.

- DM-Decay (`df-twin-dump.out`): pass #89 = a particle, stage 0 GL texture 99 (128x128), then #90 the water
  (`T_Liquid_Decay_Water_C_SD`, stage 0 cube map), then the post-processing; the dumped reference copy is a white
  texture with a cloud-shaped alpha (`twin-dump/PixTrace-131-ref391{,-a}.png`): the snow/smoke sprite.
- DM-Fury, GPU twin (`df-fury-gpu-twin-dump.out`, main's driver): pass #109 = a particle, GL texture 118 (256x256),
  then #110 `T_Liquid.Water.Liquid_Water_Puddle01_Sd` (stage 0 cube map, stage 1 2D), then the emissive select.
- Reproduction `tools/dev/decay-flash/fury.scen` (owner's position and view, standing still, a picture every 0.25 s
  for 5 s), GPU twin: main's driver 150.9 / 124.3 in all 20 pictures (`df-fury-gpu-twin-before-s*.png`: the scene
  veiled white, as in the video); this branch 28.8-28.9 / 39.1-39.6 in all 20 (`df-fury-gpu-twin-fix-s*.png`). (In
  this headless run, without bots, the same particle stays last, so the shape does not switch here.)
- The coordinator's alternatives (a particle with a NaN/huge size, a projection at the near plane, a large frame
  time, a stale vertex buffer) are excluded by the pass trace: no draw into the main framebuffer brightens the
  traced pixel before the bloom's final quad, and the reference copy differs from the main colour it is made from.
