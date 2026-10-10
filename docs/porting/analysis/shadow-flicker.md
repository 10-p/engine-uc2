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
