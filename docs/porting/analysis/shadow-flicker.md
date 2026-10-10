# B-1: CTF-Aqueduct projector shadow flicker and stipple (the owner's reproduction): notebook

Branch `b1-shadow-flicker` (from main 81fd44e), worktree `../engine-uc2-b1`, run trees `<WorkUC2>/run/b1*`, logs and
pictures `<WorkUC2>/logs/b1`. Earlier attempt: render-f5.md step 4 (not reproduced in nine conditions, none of them
close to the owner's camera).

**The owner's reproduction**: CTF-Aqueduct, first person, `RMODE 7` (lighting only), `TELEPORT 5284.9 858.1 -104.6
30811 418`; a rock ramp in black and white; moving the camera (not the pawn) slowly makes the projected tree shadow
flicker; in the screenshot (`<WorkUC2>/temp/SHADOWMAP BROKEN.png`, 1871x1202) the ramp is white near the pawn and grey
(a canopy shadow) further up, and the transition is a pixel-fine crosshatch, as z-fighting. The coordinator relayed
(2026-10-10): the screenshot was taken on the **web preview** (WebGL 2 through GL4ES on ES 3, AA off there since G-10,
Chromium on the Quadro). Primary target: the GLES twin; the desktop device checked too; the fix must hold on both.

## Step 0. Reading (2026-10-10)

- The stipple in the owner's picture (crops `logs/b1/owner-crop-3x.png`, `owner-crop-8x.png`): rows of 1-pixel dashes
  along a diagonal band between the grey (upper) and the white (lower) part of the ramp; on one side of the band the
  shadow is always drawn, on the other never. That is the signature of two surfaces whose depth difference changes
  smoothly across the screen and crosses the depth-test threshold along a line, not of noise.
- The static (map) projectors are drawn as the Xbox batch (M7b): `AProjector::RenderStaticPrimsBatch`
  (`Source/Engine/Src/UnProjector.cpp` ~1859) sets `RI->SetZBias(1)` and draws copies of the receiving geometry in
  **world space** (`LocalToWorld` = identity; vertices copied at load by `AddStaticPrimToBatch`), with
  `ProjectorAtten.vsh` (`u_clip * a_position`, `u_clip = ModelView * Projection` computed on the CPU in float). The
  receiving static mesh is drawn from its **local** vertices with its actor's `LocalToWorld`. So the two depths of the
  same point come from two different float paths.
- Our `SetZBias`: `glPolygonOffset(0, -5 * ZBias)` (`Source/UC2GLDrv/Src/UC2GLRenderInterface.cpp` 787-793 and the
  state restore at 230): a constant 5 depth units, no slope term; depth test `GL_LEQUAL` (same file, 1382); the depth
  buffer is `GL_DEPTH24_STENCIL8` on every binding.
- **What the Xbox does with ZBIAS** (Epic's D3D path: `D3DRS_ZBIAS`, `D3DDrv/Src/D3DRenderInterface.cpp` 1397). The
  XDK documentation (`XboxSDK.chm`, `D3DRENDERSTATETYPE`): "Setting a z-bias value also changes the values in the render
  states D3DRS_POLYGONOFFSETZSLOPESCALE, D3DRS_POLYGONOFFSETZOFFSET, ... D3DRS_SOLIDOFFSETENABLE". The XDK's own code,
  disassembled (`reference/xdk/unpacked/2005-02-5849.15/XDK/xbox/lib/d3d8.lib`, member `obj\i386\state.obj`,
  `_D3DDevice_SetRenderState_ZBias@4`, objdump):

  ```
  fildl  Value ; (unsigned fix-up) ; fchs        -> Bias = -(float)Value
  fsts   tmp ; fmuls 0.25 (__real@3e800000)       -> SetRenderState(77 POLYGONOFFSETZSLOPESCALE, Bias * 0.25)
                                                   -> SetRenderState(78 POLYGONOFFSETZOFFSET,     Bias)
                                                   -> SetRenderState(79/80/81 POINT/WIREFRAME/SOLIDOFFSETENABLE, Value != 0)
  ```

  So on the Xbox `ZBIAS 1` is a polygon offset of slope scale -0.25 and constant -1 (the NV2A's polygon offset, the
  same equation as GL's `glPolygonOffset(factor, units)`: offset = factor x max depth slope + units x r).
  Ours has 5x the constant and **no slope term**.

## Step 1. Reproduced, desktop device and GLES twin, on the Quadro (2026-10-10)

Tools: `tools/dev/f5-flicker.sh` gained `F5_TELEPORT` (the TELEPORT cheat with the owner's WHEREAMI line), `F5_RMODE`,
phases `mouse` (time running, one test-channel `mouse dx dy` before every frame: the camera turns, the pawn stands)
and `mousefz` (the same with time stopped), and `F5_PIXTRACE` (GLPIXTRACE x y all before every frame). The measure
`tools/dev/b1-stipple.py`: per frame the share of 1-pixel spikes (stipple, |difference| > 12 levels against both
neighbours of a row or a column, same sign), the [1,-2,1] energy (nyquist), and between frames f5-flicker.py's toggle
share (up-down by more than 24 levels) and the mean change.

Command (desktop; the twin adds `F5_GL4ES=1`: UC2GL4ESDrv, GL4ES on the NVIDIA OpenGL ES 3.2 context, SDL offscreen):

```
F5_OUT=<WorkUC2>/logs/b1/flicker F5_RES=1280x960 F5_TELEPORT="5284.9 858.1 -104.6 30811 418" F5_RMODE=7 \
F5_PHASES="frozen mousefz mouse" F5_MOUSE="3 0" F5_ARGS="$(tools/dev/glini AntiAliasing=0)" \
tools/dev/f5-flicker.sh d-r7-aa0 b1 12
```

The frame is the owner's (`logs/b1/flicker/d-r7-aa0-frozen-00.png` against `temp/SHADOWMAP BROKEN.png`): the ramp grey
with the canopy shadow above, white below, the same diagonal crosshatch between (crop `logs/b1/d-crop3x.png`). It
reproduces on **both** bindings. Ramp box (720,360)-(1180,620) (HUD and weapon outside), 12 frames:

| run | phase | stipple mean / max | toggle mean / max | change |
|---|---|---|---|---|
| owner's screenshot (box 1150,420-1450,720 of 1871x1202) | one frame | 2.49% | | |
| desktop, AA 0 | frozen | 1.79% / 1.79% | 0 / 0 | 0.00 |
| desktop, AA 0 | mousefz (3 counts a frame) | 0.32% / 1.79% | 9.85% / 46.0% | 11.33 |
| desktop, AA 0 | mouse (time running) | 0.17% / 0.99% | 3.62% / 16.3% | 8.31 |
| twin, AA 0 | frozen | 0.01% | 0 / 0 | 0.00 |
| twin, AA 0 | mousefz | 0.37% / 1.51% | 6.36% / 29.0% | 7.45 |
| twin, AA 0 | mouse | 0.64% / 1.93% | 1.55% / 6.60% | 7.21 |

With the camera still nothing changes (frozen: identical frames). Turning the view by 3 mouse counts a frame makes
whole triangles of the ramp switch between shadowed and unshadowed from one frame to the next, with crosshatched
edges (strips `logs/b1/strip-d-mousefz.png`, `logs/b1/strip-t-mouse.png`). That is the owner's flicker.

**What the pixel is** (`F5_PIXTRACE="700 500"`, run `d-trace1`, `logs/b1/flicker/d-trace1.out`): the ramp is
**terrain** (`Shader T_Terrain.Blends.Terrain_AqueductBlend_SD03`, fixed-function vertex stage, z test+write), then
seven `ProjectorMultiMaterial` passes (the Xbox static batches, `ps=82`, `ProjectorAtten.vsh`, z test only). In frames
where the pixel is grey one batch pass darkens it (241 -> 191); in frames where it is white the same seven passes run
and none changes it. The batches do not depend on the camera (static, world space, per-vertex attenuation), so between
two frames that differ by a 0.05 degree turn only the depth test can change their result.

## Step 2. The cause, with evidence (2026-10-10)

**The two passes.** At the owner's pixel the terrain is drawn by GL's fixed-function vertex stage (modelview and
projection loaded separately, `FOpenGLRenderInterface::CommitMatrices`), the projector batch over it by our vertex
shader (`gl_Position = u_clip * a_position`, `u_clip` = modelview x projection multiplied on the CPU,
`UC2GLShaders.cpp` ~981). The vertices are the same world positions (`ATerrainInfo::Vertices`, `UnTerrain.cpp` 402
and `UTerrainSector::GetVertexPos`), the triangulation the same (edge-turn bits honoured in `AttachProjector`); the
projector is meant to win the `GL_LEQUAL` test by its `SetZBias(1)` alone, which this driver turned into
`glPolygonOffset(0, -5)` (`UC2GLRenderInterface.cpp` 790-793 before the fix): 5 depth units, no slope term.

**A/B with throw-away binaries** (an environment knob for the two offset parameters and one that makes the vertex
shaders use `gl_ModelViewProjectionMatrix`, i.e. the matrix the fixed-function stage uses; neither committed). Desktop,
AA 0, RMODE 7, the owner's spot, phase `move` (time stopped, the view turned 8 yaw units a frame by NETPLACE: exactly
repeatable) and `mousefz`; ramp box; logs `logs/b1/ab-*.txt`, frames `logs/b1/ab/`:

| polygon offset (factor, units) | vertex shader position | move: toggle mean / stipple | mousefz: toggle mean |
|---|---|---|---|
| (0, -5) the port | u_clip | **20.4%** / 0.21% | **10.4%** |
| (-0.25, -1) the Xbox's ZBIAS 1 | u_clip | 0.002% / 0.010% | 0.020% |
| (-1, -1) engine-ut2004 OpenGLDrv | u_clip | 0.002% / 0.010% | 0.020% |
| (-0.25, -5) | u_clip | 0.002% / 0.010% | 0.020% |
| (0, -50) | u_clip | 0.002% / 0.010% | **11.3%** |
| (0, 0) | u_clip | **19.1%** / 0.58% | |
| (0, 0) | gl_ModelViewProjectionMatrix | 0.002% / 0.010% | |
| (0, -1) | gl_ModelViewProjectionMatrix | **14.0%** / 0.41% | |
| (0, -5) | gl_ModelViewProjectionMatrix | **23.8%** / 0.40% | |
| (0, +5) | gl_ModelViewProjectionMatrix | 0.93% (mostly unshadowed) | |
| (0, -20), (0, -100) | gl_ModelViewProjectionMatrix | 0.005%, 0.002% | |

(0.002% / 0.010% is the floor: the HUD-free ramp with time stopped and nothing toggling; the 0.010% "stipple" is the
terrain's own shading.) Reading:

1. With the port's transforms and no offset the shadow flickers as with the port's offset: the two paths do not
   produce the same depth (non-invariant), and the difference changes sign as the view turns.
2. With the same matrix in both paths and **no** offset the depths are identical and nothing flickers; but **any**
   non-zero constant offset of 1-5 units brings the flicker back on this GPU, and 20 units removes it: when an offset
   is applied, the rasterised depth of the second pass carries an error of several units here (the NVIDIA rasteriser's
   own; on a surface whose depth changes by ~2,600 units per pixel, a 1/256-pixel difference in where depth is
   evaluated is 10 units).
3. Every offset with a slope term removes it, whatever the constant; a constant of 50 units without one does not
   (`mousefz`).

Pixel trace with the depth buffer (GLPIXTRACE now logs the pass's ZBias, the GL polygon offset in force and the 24-bit
depth after it; run `dmt-u1`, invariant transform, offset (0,-1), pixel (940,472)): the GL state is what the engine
asked for in every frame (terrain 0,0; projectors 0,-1); the terrain's depth moves smoothly 16168305 -> 16203406 over the
12 frames, and the projector pass that shades the pixel passes in frames 1, 2, 5, 6, 7, 9 and fails in 3, 4, 8, 10-12.

**CPU model** (`tools/dev/b1-depth-model.py`: both transform orders emulated in float32 for the owner's camera, a 12
degree ramp under it, FOV 90, 1280x960): the two orders differ by 5-25 depth units on average, up to 56, growing
towards the camera (rows 500-900: 489 to 120 units away); the ramp's depth slope is 2,642 units per pixel. So the
port's 5 units are of the order of the difference (the stipple band is where it crosses 5; the white part near the
pawn is where it exceeds 5), while the Xbox's slope term gives 0.25 x 2,642 + 1 = 661 units there. The model's
numbers depend on the GPU's rounding (fused operations); it gives the size, not the exact values.

**Cause**: `FOpenGLRenderInterface::SetZBias` mapped D3D's ZBIAS to a constant polygon offset only. D3D's ZBIAS on the
Xbox is slope-scaled (`D3DDevice_SetRenderState_ZBias`: slope -0.25 x ZBias, constant -ZBias; step 0), and so is
Epic's own OpenGL driver for UT2004 (`OpenGLRenderInterface.cpp` 1098-1104: `glPolygonOffset(-ZBias, -ZBias)`). The
depth of a second pass over the same surface differs from the first by an amount that grows with the depth slope
(rasterisation, and this driver's two transform orders), which only a slope term covers. It is the same on GL4ES/WebGL:
`glPolygonOffset` is passed through to OpenGL ES (`gl4es` `wrap/gles.c` 1700), and the twin flickers alike (step 1).
Not the cause: the depth format (24-bit everywhere), the projector texture or attenuation (camera-independent), the
batching drawing twice or on back faces (the trace shows each batch once; the failing pass is the one that shades the
pixel in the next frame), MSAA (AA 0 here).

**Face-on check** (DM-DeepCore, wire-mesh light on the metal floor, first person looking straight down at about 100
units, `move` phase, runs `dc-cur`/`dc-mix`): the port's offset 0.005% toggle, with the slope 0.0005%: a face-on
surface had no visible problem (its depth slope is small), so the constant needs no change.

## Step 3. The fix and its proof (2026-10-10)

**Fix**: `FOpenGLRenderInterface::ApplyZBias` (`Source/UC2GLDrv/Src/UC2GLRenderInterface.cpp`, used by `SetZBias` and
by `PopState`): `glPolygonOffset(-0.25 x ZBias, -5 x ZBias)`: the Xbox's slope scale (exactly), the port's constant (5
units per ZBias since R1; the Xbox's is 1: kept because the driver's two transform orders are not invariant, and 5
units cost 0.7 world units at 5000 units from the camera). One driver function for both bindings (UC2GL4ESDrv shares
the render interface; GL4ES passes the call to OpenGL ES; WebGL has `polygonOffset(factor, units)`). `ZBias` in the
saved state is now the engine's value (0/1), not x5. Nothing removed: every projector and shadow draws as before.

**Proof** (`logs/b1/proof/`, `logs/b1/proof.txt`; binaries in run/b1/System: `uc2-before.bin` = main 81fd44e's driver,
`uc2-fix.bin` = commit 5107cf9, sha256 87fbc0a27e1787cf...; owner's spot, 1280x960, 12 frames a phase, ramp box
(720,360)-(1180,620); `mousefz` time stopped, `mouse` time running, 3 mouse counts a frame; toggle = mean share of
pixels toggling by more than 24 levels between frames, max in brackets; stipple = 1-pixel spikes):

| binding | AA | RMODE | before: stipple frozen / toggle mousefz / toggle mouse | after: stipple frozen / toggle mousefz / toggle mouse |
|---|---|---|---|---|
| desktop | 0 | 7 | 1.79% / 9.85% (46.0) / 3.62% (16.3) | 0.011% / 0.020% (0.026) / 0.020% (0.026) |
| desktop | 4 | 7 | 0.17% / 9.82% (45.8) / 3.54% (16.3) | 0.012% / 0.014% (0.017) / 0.014% (0.015) |
| twin (GL4ES, ES 3.2 NVIDIA) | 0 | 7 | 0.011% / 6.36% (29.0) / 1.55% (6.60) | 0.011% / 0.020% (0.026) / 0.020% (0.026) |
| twin | 4 | 7 | 0.017% / 6.34% (28.8) / 1.35% (6.46) | 0.017% / 0.016% (0.020) / 0.015% (0.020) |
| desktop | 0 | 6 | (texture) / 2.12% (6.30) / 1.37% (2.22) | (texture) / 0.89% (0.93) / 0.84% (0.87) |
| desktop | 4 | 6 | / 2.08% (6.25) / 1.34% (2.19) | / 0.87% (0.92) / 0.83% (0.85) |
| twin | 0 | 6 | / 1.79% (4.90) / 1.33% (2.01) | / 0.85% (0.90) / 0.82% (0.85) |
| twin | 4 | 6 | / 1.76% (4.93) / 1.29% (1.97) | / 0.83% (0.88) / 0.80% (0.83) |
| control: projectors off, desktop | 0 | 7 | | 0.011% / 0.009% (0.014) / 0.009% (0.013) |
| control: projectors off, desktop | 0 | 6 | | (texture) / 1.49% (1.54) / 1.46% (1.49) |

At RMODE 7 the stipple and the toggling fall to the floor of the projectors-off control (0.01-0.02%: the remaining
toggles are a few pixels of the ramp's edge). At RMODE 6 (normal) the 1-pixel measure is dominated by the terrain
texture (3-4% with or without projectors), but the toggling shows the flicker there too before the fix (mean 2.1%,
frames up to 6.3%, above the 1.5% of the moving texture alone) and none after (0.8-0.9% every frame, below the
projectors-off control because the shadow lowers the texture's contrast). AA 4 hides part of the stipple (multisample
resolve averages the alternate pixels) but not the flicker; the twin on the GPU reproduces it as strongly.

Pictures: `logs/b1/b1-frame-r7-before-after.png` (frozen frame, desktop above, twin below, before left: the white part
near the pawn and the crosshatched band are gone, the canopy shadow lies over the whole ramp as one grey pattern),
`logs/b1/b1-strip-r7-before-after.png` (12 frames each: before desktop, after desktop, before twin, after twin),
`logs/b1/b1-strip-r6-before-after.png`, `logs/b1/b1-diff-r7-before-after.png` (|frame 1 - frame 0| x 4: before
desktop/twin show whole triangles, after only the small motion).

## Step 4. Regressions (2026-10-10, binary of commit 5107cf9)

- `tools/dev/m7b.sh b1m7b` (desktop, Mesa llvmpipe; `logs/b1/m7b-desktop.txt`): **11 PASS, 0 FAIL**: census 24/24 and
  8/8 batched; shadow-filter PC 0.00/255, Xbox 0.56/255 (the shadow texture's own render passes, which also carry
  ZBias calls, unchanged); projector-agree DM-DeepCore mean 0.143 max 4 (M7b: 0.136-0.173, max 4); projector-differ
  CTF-Recoil mean 8.86 (M7b 8.7-9.0); draw calls 85/81 and 81/75 (as M7b); terrain PASS.
- `tools/dev/m7-effects.sh b1fx` (`logs/b1/m7-effects.txt`, pictures `logs/b1fx/`): 14 effect lines, every one
  critical=0 glerrors=0 programs_failed=0+0 left_behind=0 tries=1; praxis-panes A vs the wine reference 2.800 (below 8);
  self-test (`logs/b1/m7-selftest.txt`) 15 passed, 0 failed.
- `UC2_GPU=1 tools/dev/fx-owner.sh b1 <case>` for pool, waterfall-1p, waterfall-3p, praxis-near, praxis-far, with main's
  driver (`uc2.bin` of run/b1) and the fix (`UC2_BIN=uc2-fix.bin`), on the Quadro (`logs/b1/fx-owner.txt`, pictures
  `logs/b1/fx-<case>[-fix]-gpu-*.png`): all 10 runs critical=0 glerrors=0 programs_failed=0+0 left_behind=0. Before vs
  after, luminance: mean 0.30-1.66 on the still views, 2.6-6.3 on the waterfall shots; the difference pictures
  (`logs/b1/cmp-praxis-near-gpu-xbox.png`, `cmp-pool-gpu-on.png`, `cmp-waterfall-3p-gpu-still-a.png`: before, after,
  |difference| x 4) show only animated content (the aquarium's water and fish, the pool's surface, the pawn's idle
  pose, the waterfall); static geometry, decals and HUD are unchanged. Seen, not a numeric pass/fail (no two-run
  control of the same binary was taken).
