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
