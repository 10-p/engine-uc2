Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

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
- `M11_SERIAL=1 M11_DESKTOP_TREE=b2m M11_TWIN_TREE=b2mt tools/dev/m11-twin.sh selftest pinned` (`logs/b2/m11-twin.txt`):
  PASS selftest (15/15 on both, 0 GL errors), PASS pinned (DM-Deadbolt static part, desktop vs twin: off 0.384 (the
  F-3 measure was 0.384), on 0.613; twin GL errors 0), PASS leftovers.

### Not done (for the coordinator)

- m11-twin `effects` (the 14 effects on the twin against the desktop), `fx-owner.sh` GPU scenes: not run (machine at
  load 18-23 most of the afternoon; the coordinator asked for the cause first).
- The browser through the frontend harness: not run. It writes into `<WorkUC2>/ufrontend` (`yarn build
  --engine-artifact`, `serve-web/`), outside this agent's write area. The wasm-perf artifact of this branch is
  `build-wasm-perf/Source/SDLLaunch` (sha256 of the .wasm `e1b2b839...afbc9`).
- The GL4ES patch (`decay-flash-gl4es.patch`): the defect stays for fixed-function and pixel-shader draws with a cube
  map on stage 0 and a 2D texture on a later stage (their later stage reads a stale texture on GL4ES: census of
  O-9's maps: `DM-AcidRain.Water.T_Acidrain_WaterSD02`, `T_Liquid.Water.Liquid_Water_Puddle01_Sd`; DM-Decay's water
  too). A frozen-frame A/B at DM-Decay's floor pits shows no visible change (desktop vs twin 0.57, vs twin with the
  patch 1.00 mean in the pit rows; the frozen time differs between runs, inconclusive). Pushing the fork (shared with
  engine-ut2004) needs the owner's go-ahead.

## GL4ES push preparation (2026-10-10, evening)

The owner, on pushing `decay-flash-gl4es.patch` to the fork: "Push it but let's make sure we have a thorough
explanation of the change and the dangers of the change. UT2004 and UT99 UTGLR use this, we don't want to break
them, but if we do we want to give another agent a heads up to be proactive of where to look so that I don't
spend ages chasing an invisible bug." This section is that explanation, the tests, and where the heads-up note is.
Nothing is pushed by this agent: the coordinator pushes. Logs and pictures: `<WorkUC2>/logs/b2/gl4es-push/`.

### Who fetches the fork (read from the sibling CMake files and their build caches, read-only)

| Engine | GL4ES source by default | Does this commit reach it? |
|---|---|---|
| engine-uc2 (this repo) | `10-p/gl4es` branch `fix/cubemap-bind-active-unit` (`CMakeLists.txt`, the M11 block) | yes, at the next configure (branch ref, re-fetched) |
| engine-ut2004 | the same fork and branch (`CMakeLists.txt:923-925`; all six build caches say so) | yes, at the next configure |
| engine-ut99 (UTGLR, `Source/OpenGLDrv`) | **upstream** `ptitSeb/gl4es` `master` (`Source/CMakeLists.txt:562-571`, `GL4ES_SOURCE_DIR` empty; build-wasm and build-wasm-perf caches say `ptitSeb/gl4es@master`) | **no** (only if someone sets `GL4ES_GIT_REPOSITORY`/`GL4ES_SOURCE_DIR` to the fork) |
| engine-ut99-436-compat | upstream `master`, as engine-ut99 (`Source/CMakeLists.txt:573-582`) | no |
| engine-unreal1 | no gl4es | no |

In engine-ut99 only UTGLR (`OpenGLDrv`) links gl4es (`USE_GL4ES`, the web build); NOpenGLESDrv and XOpenGLDrv talk to
GLES/WebGL directly. Upstream `master` (`ec16bedd`, 2026-10-04, fetched today) has the same line
(`texture_params.c:864`, `glstate->enable.texture[glstate->texture.active]`): UT99 lives with the old behaviour,
which (below) cannot affect what UTGLR draws.

### What GL4ES did

GL4ES keeps, per texture unit `i`, the client's bound texture for each target in `glstate->texture.bound[i][t]`
(`t` = 1D, 2D, 3D, RECTANGLE, CUBE_MAP, `texture.h:96-101`) and the client's enables in a bit mask
`glstate->enable.texture[i]` (`enable.c:200-260`). GLES has only two real targets per unit, `GL_TEXTURE_2D` and
`GL_TEXTURE_CUBE_MAP`: 1D, 3D and RECTANGLE textures are emulated as GLES 2D textures (`map_tex_target`,
`texture.h:164`). So the GLES 2D slot of a unit is shared by four desktop targets and GL4ES must choose which of
the unit's textures goes into it. It does that lazily:

- `gl4es_glBindTexture` (`texture_params.c:178`): a cube map is bound in GLES at once (after `realize_active()`,
  the fork's commit `627a77e4`); a 1D/2D/3D/RECTANGLE bind only records `bound[active][t]` and raises
  `glstate->bound_changed` to `active+1`.
- `gl4es_glActiveTexture` (`texture_params.c:607`) only records `glstate->texture.active`; the GLES unit is
  switched later (`realize_active`, or inside `realize_textures`).
- `glEnable/glDisable(GL_TEXTURE_xx)` (`enable.c`) set or clear the bit of the active unit and raise
  `bound_changed` (and `fpe_bound_changed` for the fixed-function shader generator).
- At every draw (`drawing.c:366`, `listdraw.c:708`, the blit `blit.c:352`) and before some texture/FBO calls
  (`realize_textures(0)`: `gl4es.c`, `framebuffers.c`, `stack.c`, `glx.c`), `realize_textures`
  (`texture_params.c:857`) walks the units `0 .. bound_changed-1` and for each picks a target `tgt`, binds
  `bound[i][tgt]` into the GLES 2D slot of unit `i` if that is not already there (`actual_tex2d[i]` caches what
  GLES has), and applies the texture's sampler state (`realize_1texture`).

The bug is the line that picks `tgt`:

```c
int tmp = glstate->enable.texture[glstate->texture.active];   // the ACTIVE unit's enables, for every unit i
... priority 3D > RECTANGLE > 2D > 1D > CUBE_MAP, default 2D ...
if(tgt!=ENABLED_CUBE_MAP) { ... gles_glBindTexture(GL_TEXTURE_2D, bound[i][tgt]->glname) ... }   // else: no 2D bind
```

Two defects:

1. **Every unit uses the enables of whichever unit happens to be active at the draw.** `glActiveTexture` is a
   selector for later calls; the active unit at draw time has no meaning for the other units. So unit 1's
   texture choice depended on unit 0's enables (or 3's, whichever the client last selected).
2. **When those enables say "cube map only", the GLES 2D slot of the unit is not refreshed at all**, and
   `bound_changed` is reset to 0 afterwards, so the 2D bind the client made is lost until that unit changes
   again: the GLES 2D slot keeps whatever 2D texture GL4ES last made real there.

### What desktop OpenGL specifies (what GL4ES should emulate)

- Texture enables are **per texture unit**: `glEnable(GL_TEXTURE_2D)` etc. apply to the unit selected by
  `glActiveTexture` at the time of the call, and each unit is enabled and bound independently of the others.
- When several targets are enabled on one unit, fixed-function texturing uses **the highest-priority enabled
  target of that unit**: CUBE_MAP > RECTANGLE (ARB_texture_rectangle) > 3D > 2D > 1D. The textures bound to the
  other targets of the unit stay bound and are simply not used by the fixed-function pipeline.
- **A GLSL program ignores the enables altogether**: a `sampler2D` on unit `i` reads the texture bound to
  `GL_TEXTURE_2D` on unit `i`, a `samplerCube` the one bound to `GL_TEXTURE_CUBE_MAP`, whatever is enabled.
  The same holds for ARB fragment programs (the instruction names the target).

GL4ES' fixed-function shader generator already does this right: `fpe_gettexture(i)` (`fpe.c:1036`) reads
`enable.texture[i]` (unit `i`) with the order CUBE_MAP > 3D > RECTANGLE > 2D > 1D and makes a `samplerCube` or
`sampler2D` for that unit. Only `realize_textures`, which feeds the texture objects to those samplers, read the
wrong unit and starved the 2D slot.

How UC2 hit it (steps 1-4 above): DM-Decay's water is a fixed-function pass with a cube map on stage 0 (UC2's
driver enables `GL_TEXTURE_CUBE_MAP` and disables `GL_TEXTURE_2D` on that unit, the D3D translation of a cube
stage) and ends with `glActiveTexture(GL_TEXTURE0)`. The next draw was the device's GLSL copy quad with
`glBindTexture(GL_TEXTURE_2D, MainColorTexture)` on unit 0: GL4ES saw "active unit 0 = cube only", skipped the
2D bind, and the copy sampled the particle texture drawn just before: the full-screen white cloud.

**Why the water's own stage 1 (a 2D mask on unit 1) did not show it, corrected** (step 4 listed it as open): the
skip should hit every unit of that pass too, but UC2's interface sets `GL_TEXTURE_MAX_ANISOTROPY_EXT` on every
stage's texture (`UC2GLRenderInterface.cpp:1460-1462`), and GL4ES does not defer that parameter
(`gl4es_glTexParameterfv`, `texture_params.c:298-353`: wrap and filters go into the sampler record, anything else
calls `realize_bound(active, target)`, which binds the active unit's 2D texture at once). So in UC2 every
fixed-function 2D stage is bound immediately and the deferred path is never reached; the GLSL copy quad, which
sets no parameter, is. Generally: `glTexImage*`, `glTexSubImage*`, `glCopyTex*`, compressed uploads, reading a
texture back and non-sampler `glTexParameter*` calls on a unit make GL4ES bind that unit's 2D texture at once and
hide the defect; a bind followed only by a draw exposes it. Measured with the new combiner case below (with the
anisotropy parameter left out, as on a WebGL context without `EXT_texture_filter_anisotropic`): stage 1 read the
stale texture, error 138.8 of 255.

### The change, line by line (`src/gl/texture_params.c`, `realize_textures`)

```diff
-        // get highest priority texture unit
-        int tmp = glstate->enable.texture[glstate->texture.active];
+        // get highest priority texture target of unit i (was the ACTIVE unit's flags for every unit)
+        int tmp = glstate->enable.texture[i];
```
Unit `i`'s own enables decide unit `i`'s target. The priority order inside `realize_textures` is unchanged
(3D > RECTANGLE > 2D > 1D, CUBE_MAP last): it only decides what goes into the GLES **2D** slot (a cube map has its
own GLES slot and is bound at `glBindTexture` time), so a unit with CUBE_MAP **and** 2D enabled still puts its
2D texture in the 2D slot (as before), and the fixed-function shader still samples the cube (`fpe_gettexture`).

```diff
         gltexture_t *tex = glstate->texture.bound[i][tgt];
-        GLuint t = tex->glname;
-        if(tgt!=ENABLED_CUBE_MAP) {// CUBE MAP are immediately bound
+        gltexture_t *tex2d = (tgt==ENABLED_CUBE_MAP)?glstate->texture.bound[i][ENABLED_TEX2D]:tex;
+        GLuint t = tex2d->glname;
+        {
```
When the unit's target is CUBE_MAP (cube enabled, no 1D/2D/3D/RECTANGLE), the 2D slot is no longer skipped: it is
filled with the unit's bound **2D** texture (`tex2d`), which is what a GLSL `sampler2D` (or an ARB program's
`2D` instruction) on that unit must read. For every other target `tex2d == tex` and the block is the old one.
The block's body (stream handling for Pandora's TEXSTREAM builds, the `actual_tex2d[i]` cache test,
`glActiveTexture(i)`, `glBindTexture(GL_TEXTURE_2D, t)`) is unchanged.

```diff
         realize_1texture(target, i, tex, glstate->samplers.sampler[i]);
+        if(tex2d!=tex)
+            realize_1texture(GL_TEXTURE_2D, i, tex2d, glstate->samplers.sampler[i]);
```
The cube map's sampler state is applied as before (on `GL_TEXTURE_CUBE_MAP`); the 2D texture now also put in the
slot gets its own filter/wrap applied (`realize_1texture` only calls `glTexParameteri` when the cached `actual`
state differs). Unchanged: the mipmap block between the two (`if(drawing)`), which runs on `tex` (the cube map
when `tgt` is CUBE_MAP); see danger 5.

### What can change for a client (the dangers)

1. **Fixed-function clients that enable a cube map on one unit and draw 2D on another** (DM-Decay's water: cube
   on unit 0, 2D mask on unit 1; in UC2 the anisotropy parameter hid it, the combiner case `cube-then-2d` below
   measures it): before, every unit's 2D bind was skipped whenever the *active* unit was
   cube-only, so the 2D units sampled stale textures; now each unit gets its own texture. A picture that was
   wrong can change. Expected direction: towards the desktop picture.
2. **Clients that enable cube + 2D on one unit** (engine-ut2004's GL4ESDrv does exactly this for every cube
   stage): no change: `tgt` was 2D before (the active unit, whose enables the old code read, is always 2D, 2D+CUBE or nothing in
   that driver) and is 2D now;
   the fixed-function shader samples the cube by `fpe_gettexture`, as before.
3. **Clients that relied on the old behaviour**: a client whose GLSL/ARB draw on unit `i` "worked" because the
   stale 2D slot happened to hold the right texture (it never re-bound it, and a cube-only active unit kept GL4ES
   from replacing it) now gets the texture it actually has bound on unit `i` in GL. If that is the NullTexture or
   another material's texture, the client was wrong on desktop GL too; on GL4ES it now shows. Likewise a client
   that selected a different active unit by accident and got the "right" target by luck. Also changed: the old
   code used the active unit's 3D/RECTANGLE/1D enable for every unit (a 3D texture on unit 2 made unit 0 bind
   its 3D slot); now each unit uses its own.
4. **A cube-only unit now always realises its 2D slot.** One extra `glBindTexture(GL_TEXTURE_2D)` (and a
   `glActiveTexture`) per draw only when that unit's 2D binding changed (the `actual_tex2d` cache), plus
   `glTexParameteri` calls only when the 2D texture's cached filter/wrap differ. If the unit's 2D binding is
   texture 0 (never bound), GL4ES applies sampler state to the default texture once (pre-existing behaviour for
   unbound 2D units; on WebGL `texParameteri` with no texture bound is a GL error, reported once).
5. **The mipmap block runs on the cube map for cube-only units** (`if(drawing)`: if the texture's min filter wants
   mipmaps and GL4ES has not marked them done, `glGenerateMipmap(GL_TEXTURE_2D)` is called once, on whatever GLES
   unit is current, and the texture is marked done). This code is not new and is not touched; before, it ran on
   the cube map of *every* unit whenever the active unit was cube-only, now only on the units that are cube-only
   themselves. For UC2 that is fewer calls than before; for a client that never had a cube-only active unit at
   a draw but has cube-only units, it is a new one-time call per cube texture. Symptom if it bites: one GL error
   (`GL_INVALID_OPERATION` on a compressed or absent 2D texture) or, at worst, the mip levels of the 2D texture
   then current on that GLES unit regenerated from its level 0 (a texture that looks slightly different at a
   distance).
6. **Performance**: `realize_textures` still only walks units `< bound_changed`; per unit the new work is the
   test above. No extra call for units whose target is not CUBE_MAP. No extra call at all for engine-ut2004 (no
   cube-only unit) or UTGLR (2D only). UC2: measured below.
7. **`LIBGL_ES=3` vs `LIBGL_ES=2`**: no difference; the code has no GLES-version branch except inside
   `TEXSTREAM` (Pandora builds, not compiled here: `CMakeLists.txt:54-58`). `LIBGL_ES=1` (GLES 1 fixed function,
   none of our engines): binding a 2D texture into a unit whose GLES 1 cube enable is on is harmless (GLES 1 gives
   the cube priority). Other switches that touch targets: `LIBGL_NOTEXRECT` (engine-ut2004 sets it: no RECTANGLE
   ever), `LIBGL_MIPMAP` (UC2 and UT2004 set 0; values 1/3 change the mipmap block of danger 5, not the target
   choice), `LIBGL_TEXSTREAM`/`LIBGL_STREAM` (Pandora only), `LIBGL_FBOFORCETEX` (attachments; not touched).
8. **Display lists / batching**: `realize_textures(1)` is also called by `listdraw.c:708`; the change applies
   there identically (UT2004 and UC2 set `LIBGL_BATCH=0` and compile no display lists).

### The sibling drivers, one by one (code read; not run, since the siblings are read-only)

**engine-ut2004, `Source/GL4ESDrv`** (the only UT2004 device that links gl4es; `OpenGLDrv` binds the system GL
through `SDL_GL_GetProcAddress` and is never built for the web or Android, `CMakeLists.txt:1283-1318`):

- `SetMaterialBlending` (`GL4ESRenderInterface.cpp:666-675`): for every used stage `s`: `glActiveTextureARB(s)`,
  `glEnable(GL_TEXTURE_2D)` **always**, then `glEnable(GL_TEXTURE_CUBE_MAP)` for a cube texture, else
  `glDisable(GL_TEXTURE_CUBE_MAP)`; `SetTexture` binds the cube to `GL_TEXTURE_CUBE_MAP` or the 2D texture /
  `NullTextureID` to `GL_TEXTURE_2D` (`:582-593`). Unused stages: both disabled (`:901-906`). Init
  (`GL4ESRenderDevice.cpp:908-915`): 2D disabled on every unit, enabled on 0; no cube.
- So every unit is in one of three states: 2D, 2D+CUBE, nothing. `realize_textures` picks 2D for all three,
  before and after (2D wins over CUBE in its order; nothing defaults to 2D), and the active unit (which the old
  code read) is also always in one of those states. **The patch takes the same path for every unit of every UT2004
  draw: the GLES call stream is the same.**
- Pixel shaders are ARB fragment programs converted by gl4es (`GL4ESRenderDevice.cpp:1497-1524`,
  `LIBGL_NOARBPROGRAM=0`): they sample `texture[0..4], 2D` only, i.e. the GLES 2D slots, which get the same
  textures as before.
- Cube maps (`GL4ESResource.cpp:478-607`: upload, `glGenerateMipmapEXT(GL_TEXTURE_CUBE_MAP)`, `MAX_LEVEL`) are
  untouched by the patch (immediate binds).
- `PixoDrv` (the software renderer) also links gl4es for its present path (`PixoRenderDevice.cpp:855-940`): one 2D
  texture on unit 0, `glEnable(GL_TEXTURE_2D)`: no change possible.
- Where a symptom would show **if** a future UT2004 change created a cube-only unit (for example a GLSL path that
  disables 2D on a cube stage, or a material with a cube stage drawn with `glDisable(GL_TEXTURE_2D)`): materials
  with a cube stage (`TexEnvMap`/`Cubemap` on reflective surfaces, water, shiny vehicles/weapons, skyboxes drawn
  through a cube) and **the draw right after one of them** on the same unit or on another unit.

**engine-ut99 UTGLR** (`OpenGLDrv`, `_deps/utglr-src/OpenGL.cpp`): no cube maps, no 3D, no RECTANGLE anywhere
(the only mentions are the `glext.h` defines); it enables and disables only `GL_TEXTURE_2D` per unit
(`OpenGL.cpp:2385, 7220-7589`). Every unit is 2D or nothing: `realize_textures` picks 2D before and after. **The
patch cannot change what UTGLR draws**, and engine-ut99 does not fetch the fork anyway (above).

**engine-uc2** (this repo, both bindings of `UC2GLDrv`): cube stages are cube-only (`UC2GLRenderInterface.cpp:
1423-1431`), and the interface ends with `glActiveTexture(GL_TEXTURE0)` (`:1532`): this is the engine that
exercises the change. The B-2 workaround (`BindProgramTexture2D`, `UC2GLPostProcess.cpp:310`) stays; with the
patch it becomes belt and braces.

### Tests (2026-10-10, 17:30-20:00)

Setup: the fork clone `engine-uc2-f3-webgl2/.gl4es-dev` at `6b6bccd4` with the patch applied (`git apply`, then
committed as below); this worktree fast-forwarded to main `321db38`; `build-native` configured with
`-DFETCHCONTENT_SOURCE_DIR_GL4ES=<the clone>` (`bin/capped 6G`, -j2..-j4; the cache and the compile commands show the
clone's `texture_params.c`), and back to the fetched branch for the "stock" binaries. Run tree `run/b2gp`:

| executable | GL4ES | engine workaround (`BindProgramTexture2D`) | sha256 |
|---|---|---|---|
| `uc2.bin` (= `uc2-gptwin.bin`, `uc2-desk.bin`) | patched | on (main's) | `923b8d25...` |
| `uc2-gp-noWA.bin` | patched | **off** (temporary local edit: the old plain bind; not committed) | `58daafb0...` |
| `uc2-stock.bin` | fork tip `6b6bccd4` | on (= main) | `1559c5b1...` |
| `uc2-stock-noWA.bin` | fork tip `6b6bccd4` | off | `178ec6a7...` |
| `uc2-ct-gp.bin` / `uc2-ct-stock.bin` | patched / tip | on, + the new combiner case | `150d1def...` / `6bd541d5...` |

Logs and pictures: `logs/b2/gl4es-push/` (`runs/` for the runs of tree b2gp, `m11/` and `m11t/` for the m11 trees;
during the runs `logs/b2gp`, `logs/b2gpm`, `logs/b2gpmt`, `logs/b2gps` were symlinks into it so the tools'
`logs/<tree>` landed there; removed at the end, so paths printed as `logs/b2gp*/...` in the logs are under
`logs/b2/gl4es-push/{runs,m11,m11t,fx-stock}/`).
`quiet-runs.sh` there ran the browser, the GPU and the m11 runs one after the other, each started at 1-minute
load < 16 (`quiet-runs.txt`: the load at each start).

**1. The overlay, workaround off** (`tools/dev/decay-flash.sh b2gp <name> tools/dev/decay-flash/repro.scen -gl4es`,
DM-Decay, the owner's spot; middle / outer mean luma; numerically checked):

| run | t0 | pitch down | **back (owner's view)** | back2 | max middle of all 11 shots | GL errors |
|---|---|---|---|---|---|---|
| Mesa twin, tip, workaround off (control) | 208.8 / 156.9 | 203.8 / 161.4 | **209.8 / 158.6** | 208.6 / 159.5 | 209.8 | 0 |
| Mesa twin, **patched, workaround off** | 37.0 / 35.1 | 31.5 / 41.5 | **36.4 / 37.1** | 36.6 / 38.0 | 42.6 (yaw-30) | 0 |
| Mesa twin, patched, workaround on | 36.3 / 37.7 | 31.4 / 41.7 | **36.4 / 37.1** | 36.4 / 37.3 | 42.4 | 0 |
| GPU twin (Quadro, ES 3.2), tip, workaround off | 37.0 / 35.2 | 208.0 / 161.1 | **206.7 / 153.2** | 208.4 / 153.7 | 208.4 | 0 |
| GPU twin, **patched, workaround off** | 36.3 / 37.8 | 31.8 / 41.6 | **37.4 / 37.7** | 36.0 / 37.4 | 42.2 | 0 |
| GPU twin, patched, workaround on | 36.4 / 37.7 | 31.8 / 41.6 | **36.5 / 37.2** | 36.0 / 37.8 | 41.7 | 0 |

DM-Fury (`DF_MAP=DM-Fury ... fury.scen`, 20 pictures 0.25 s apart, the owner's second spot): Mesa twin tip without the
workaround 155.1 middle (153.4-157.8), patched without it 35.0 (34.7-35.2); GPU twin tip without it 150.9, patched
without it 34.6, patched with it 34.6. The GL4ES patch alone removes both overlays; with both fixes the picture is
the same as with either.

**2. The fixed-function case, numerically: new combiner case `cube-then-2d`** (`UC2GLCombinerTest.cpp`,
`RunCube2D`: one pass, stage 0 a cube map (face +Z), stage 1 a 2D texture no earlier case put on unit 1,
MODULATE; the anisotropy parameter left out for that draw, see "Why the water's own stage 1 did not show it";
`CombinerTest=True`, Mesa llvmpipe, Xvfb; logs `runs/ct-*.out`):

| binding | `cube-then-2d` | summary | GL errors |
|---|---|---|---|
| desktop GL, fragment programs | PASS, largest error 0.49 of 255 | 36 passed, 0 failed | 0 |
| desktop GL, texture environment (`FixedFunctionPrograms=False`) | PASS, 0.49 | 36 / 0 | 0 |
| GL4ES twin, **patched** | PASS, 0.49 | 36 / 0 | 0 |
| GL4ES twin, fork tip `6b6bccd4` | **FAIL, 138.84** ("pixel 4, channel 3: 156, expected 17.2": the stale texture) | 35 / 1 | 0 |

(Before the anisotropy override the case passed on the tip too: that is how the masking was found.) Consequence
for `tools/dev/o9-nvidia.sh combiner-twin`: it FAILs on a build that fetches the fork tip `6b6bccd4` until the
fork commit is pushed (the branch is re-fetched at the next configure), and passes after.

**3. The reflective scenes on the GPU** (`UC2_GPU=1 UC2_BIN=<exe> FX_ARGS=<-gl4es or none> tools/dev/fx-owner.sh b2gp
<case>`; desktop GL vs twin with the tip vs twin patched; mean |difference| of 255 and share of pixels off by
more than 16; every run: critical 0, GL errors 0, programs failed 0+0, nothing left running):

| case / shot | desktop vs tip | desktop vs patched | tip vs patched |
|---|---|---|---|
| DM-Azure pool (cube-mapped water) `on` | 1.23, 1.65% | 1.08, 1.25% | 0.65, 0.72% |
| pool `nowater` | 0.65, 0.72% | 0.59, 0.49% | 0.23, 0.41% |
| DM-Praxis panes near, frozen, `xbox` / `raised` | 6.64 / 6.78 | 6.91 / 7.01 | 0.87 / 0.94 |
| Praxis panes far, frozen, `xbox` / `raised` / `nofade` / `off` | 2.73 / 2.80 / 2.80 / 2.68 | 2.78 / 2.84 / 2.86 / 2.78 | 0.38 / 0.43 / 0.49 / 0.55 |
| DM-Azure waterfall first person `still-a` / `still-c` | 1.31 / 0.47 | 5.77 / 3.62 | 5.52 / 3.59 |

Seen: the patched twin's pictures differ from the tip's by less than either differs from the desktop, except the
first-person waterfall, where the difference is the first-person hands' idle animation phase and the waterfall's
flow (sheet `cmp-waterfall-still-a.png`: desktop, tip, patched, difference x4: only the hands and the water
sheet differ); the desktop-vs-twin difference at Praxis (6.6-7.0) is the same before and after (pre-existing twin
difference of the distortion panes, not this change). Water at DM-Decay's floor pits, frozen
(`water.scen`, Mesa): desktop vs tip 0.87, desktop vs patched 0.96, tip vs patched 1.02 mean; the water pass's traced
pixel (`GLPIXTRACE`) 18,22,27 / 20,24,29 / 22,26,32 (desktop / tip / patched) at one pit, 37,41,53 / 37,41,53 /
38,42,54 at the other: inconclusive at that level (the frozen time differs per run), and expected to be equal
since the anisotropy parameter already bound those textures on the tip.

**4. The twin against the desktop, every check** (`M11_SERIAL=1 M11_DESKTOP_TREE=b2gpm M11_TWIN_TREE=b2gpmt
tools/dev/m11-twin.sh`, `m11-twin.txt`): **32 PASS, 2 FAIL lines** at machine load 18-29 (other agents' runs): selftest 15/15 on both bindings (0 GL
errors), progtest 485 programs identical (largest difference 0), pinned (DM-Deadbolt static part 0.384, as F-3 and
B-2 step 4), 13 of the 14 effects, m6 (4 views), fallbacks (4 masks), es2 (the OpenGL ES 2 path, `LIBGL_ES=2`),
movies (9 checks), leftovers. FAIL: `effect-blur` (B picture desktop vs twin 6.137 > 6) and the `effects` line that
counts it. That check is known to be time-dependent (BlurMe's blur decays over time; render-f5.md 6.416,
web-renderer.md 7.388, d34-docs-tests.md 5.485 / 4.619). Rerun at load ~15 (`quiet-runs2.sh`, twice, the same three
bindings one after the other): desktop vs twin **5.21 and 3.17 with the patch, 4.95 and 4.57 with the fork tip**,
patched vs tip 2.90 / 3.09; critical 0, GL errors 0, programs failed 0 in all six runs. Not a regression.

**5. The browser: WebGL 2 through the patched GL4ES.** `build-wasm-perf` configured with the clone (`bin/capped 6G`,
exit 0), `SDLLaunch.wasm` sha256 `dfebd593b7f9c44c17a63f6ad8319639a21f86685f1e0102231f86a158c34808`; `yarn build
--target uc2 --engine-artifact <this worktree>/build-wasm-perf/Source/SDLLaunch` in `<WorkUC2>/ufrontend` (nothing
committed there; `serve-web/SDLLaunch.wasm` = that sha256 before and after the run); one harness machine-wide
(checked), null sink `b2gp` loaded and removed by its module index, `bin/capped 7G 1800 -- node
packages/e2e/tools/e2e-mem-guard.mjs --max-gb=23 -- --target uc2 uc2-wasm-boot.spec.js`: **5 passed (4.2 min)**
(menu drawn with "WebGL 2 through GL4ES" on ANGLE/Vulkan on the Quadro; DM-Deadbolt with post-processing on;
keyboard and mouse; mouse look; the ini settings), 0 GL errors, guard peak 9.2 GB machine-wide (`e2e-boot.txt`).
**Note for the coordinator: `ufrontend/serve-web` now holds this worktree's wasm (dfebd593...).**

**Not run, by rule or by reach:** engine-ut2004 and engine-ut99 themselves (read-only; their conclusions above are
from the code, labelled so); `tools/dev/pp-scale.sh`; the Windows build; `LIBGL_ES=2` on a real ES 2 context except
through m11's `es2` check.

**6. Performance** (`tools/dev/fps-bench.sh <tree> <map> 20 1280x720 -gl4es` on the Quadro, the GPU twin; tree
`b2gpf` = patched, `b2gps` = tip, same engine code; alternating, each started at load < 16; `fps-runs.txt`,
`fps/`): DM-Decay at the owner's spot (the cube-mapped water in view, a turn on the spot): patched 119.8 / 117.9 fps, tip
120.3 / 119.3; DM-Azure at the pool: patched 144.1 / 150.3, tip 152.1 / 149.3 (1% lows 72-90 on both). Within the
run-to-run spread (the patched Azure pair alone spans 6 fps); no extra cost measurable, as expected from danger 6.

### The commits

- Fork clone (`engine-uc2-f3-webgl2/.gl4es-dev`, branch `fix/cubemap-bind-active-unit`): **`b0df7bec`** on top of
  `6b6bccd4`, "Bind each unit's 2D texture from its own enables, also when a cube map is on"; the message carries
  the bug, the desktop rule, the change, the dangers, who is affected, the UC2 verification and "if something
  breaks in another engine". Local tag **`before-per-unit-textures`** = `6b6bccd4`, for an A/B by tag name (see
  below). **Not pushed** (the coordinator pushes: `git -C <clone> push origin fix/cubemap-bind-active-unit` and,
  for the tag, `git -C <clone> push origin before-per-unit-textures`).
- This worktree: the combiner case `cube-then-2d` (`UC2GLCombinerTest.cpp`) and this section. The engine
  workaround stays (belt and braces; it costs one enable pair per device draw).
- The heads-up for the sibling agents: **`<WorkUC2>/logs/b2/gl4es-push/HEADS-UP-ut2004-ut99.md`** (what changed,
  the commit, does it reach you, the symptom, where to look in GL4ES, the A/B, the UC2 proof).

**The A/B line the owner asked for, corrected.** `-DGL4ES_GIT_TAG=6b6bccd4` does not work in engine-ut2004 or
engine-uc2: both declare `GIT_SHALLOW TRUE`, and a shallow fetch cannot check out a commit that is no longer a
branch tip. Tested with CMake 4.2.3 and a throwaway project (`FetchContent_Declare(... GIT_TAG <x> GIT_SHALLOW TRUE)`)
against the fork: the current tip's full sha works; the parent's full sha fails ("unable to read tree"), an
abbreviated sha fails ("invalid reference"). What works: a local checkout of `6b6bccd4` passed with
`-DFETCHCONTENT_SOURCE_DIR_GL4ES=<checkout>` (engine-ut99: `-DGL4ES_SOURCE_DIR=`), or, once the tag is pushed,
`-DGL4ES_GIT_TAG=before-per-unit-textures` (a tag name is shallow-fetchable).

### Labels

- Numerically checked: the overlay gone with the GL4ES patch alone (Mesa and NVIDIA twin, DM-Decay and DM-Fury);
  the fixed-function cube-then-2D case (tip FAIL 138.8, patched PASS 0.49 = desktop); m11-twin 32/34, the two FAIL lines being the known time-dependent blur at load ~20, within the threshold on the quiet rerun (patched 5.21 / 3.17, tip 4.95 / 4.57); 0 GL
  errors in every run, browser included; the browser boot spec 5/5.
- Seen only: the reflective GPU scenes (numbers above, judged by eye on the waterfall sheet).
- Reasoned from the code, never run: engine-ut2004 GL4ESDrv and PixoDrv, engine-ut99 UTGLR (same GLES call stream
  before and after); danger 5 (the mipmap block) in a client with cube-only units that never had a cube-only
  active unit.
- Not run: the Windows build against the clone; `pp-scale.sh`.
