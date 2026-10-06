# Owner review O-5, O-6, O-7 (DM-Azure water pool, DM-Azure waterfall, DM-Praxis pane range): notebook

Branch `fx-fixes`, worktree `../engine-uc2-fx`, run trees `<WorkUC2>/run/fx*`, pictures and logs
`<WorkUC2>/logs/fx`. Brief: PLAN.md, "Owner review of 2026-10-06", tasks O-5, O-6, O-7; the owner's words are in
`<WorkUC2>/answers.txt` (answer 2 "Distortion size" and answer 6). Every run goes through `<WorkUC2>/bin/capped`
(game 2G, builds 6G). The driver (`Source/UC2GLDrv`) is being edited by the M11 web agent at the same time:
every hunk there is itemised at the end of this file for a hand merge.

## The owner's pictures (`<WorkUC2>/temp/`), what each one is

| File | Whose | What it shows |
|---|---|---|
| `AzureWaterDistortion.png` | Xbox (xemu window title) | DM-Azure centre pool, third person: a water surface over the pool floor, reflection of the cobra statue / mega health on it, golden shimmer in the centre, floor seen through it softened |
| `AzureWaterFlat.png` | ours (native, 2428x1786) | same place: the pool floor is sharp and plain, no water layer is visible at all |
| `AzureWaterfallStraight.png` | ours, first person | the waterfall from the cobra's mouth: the stone behind it is not displaced (owner: only while standing still) |
| `AzureWaterfallDistorted.png` | ours, third person | same waterfall, the stone behind it is displaced |
| `PraxisWindowPanesClose.png` | Xbox | DM-Praxis panes from close: the reference for "spot on" |
| `PraxisWindowPanesCloseWeb.png` | ours (web) | same, matches |
| `PraxisWindowPanesDistance.png` | **Xbox** | the panes across the room: no visible distortion. So the Xbox itself loses it at a distance; the owner asks for the limit to be found and raised ("increase the lod of this effect so that it stays farther") |

## Log

### Step 0 (2026-10-06 23:15): reading, first leads

- DM-Azure (`tools/dev/ps-census.py` + an export/import listing of the map): one `FluidSurfaceInfo`, one
  `FluidSurfaceOscillator`, one `PhysicsVolume`, two `FinalBlend`s, one `TexScaler`; imports from `T_Liquid`:
  `PSDistortionMaterial T_Liquid.Downfall.T_Liquid_Downfall_Water01_DIST04`, `Shader
  T_Liquid.Decay.T_Liquid_Decay_Water_C_SD`, `Shader T_Liquid.Water.Liquid_Water_AdvWater_Sd2`.
- The pool is (almost certainly) the `FluidSurfaceInfo`. `AFluidSurfaceInfo::Render` has an `_XBOX` body and a
  PC body (`Source/Engine/Src/UnFluidSurface.cpp` 454-773). The Xbox body writes every vertex colour as
  `FColor(255,255,255,VertAlpha)`; the PC body (`FillVertexBuffer`, 1306-1308) writes `FluidColor.RenderColor()`,
  and `FluidColor` defaults to (0,0,0,0) in `FluidSurfaceInfo.uc`. Lead for O-5: a material that uses the
  vertex colour gets black on PC, white on Xbox. To be confirmed with the map's actual properties and material.

### Step 1 (2026-10-07 00:40): tools for a deterministic camera and "what is under this pixel"

- Build: `bin/capped 6G 3600 -- cmake --preset native && cmake --build build-native -j6` (logs/fx/build-0.log),
  run tree `tools/dev/mkrun-uc2.sh build-native fx`; after each rebuild only `uc2.bin` is copied into
  `run/fx/System`.
- Camera: the test command `NETPLACE <name> <x> <y> <z> <yaw> [<pitch>]` (`Source/Engine/Src/UC2NetTest.cpp`,
  network tests) now takes an optional pitch (default 0: the existing callers are unchanged). With the map
  URL `DM-Azure?Name=FxCam`, `-glexec=44:SetAutoCenterPitch+0@45:NETPLACE+FxCam+x+y+z+yaw+pitch` puts the
  player anywhere. The pitch only holds once the pawn stands (place it at floor height, or place twice).
  `ToggleThirdCam` switches to first person. Properties of map objects: `getall <class> <property>`
  (`obj dump name=FluidSurfaceInfo0` crashes in property export: the class is `noexport`; other objects dump).
- New debug command (driver, quiet by default): `GLPIXTRACE <x> <y> [all]` logs, for the next frame, every pass
  that changes that pixel of the main framebuffer: material, blend state, stages (texture, cube, ops), before
  and after colour. `Source/UC2GLDrv/Src/UC2GLPixelTrace.cpp` (new file) + two calls in `DrawPrimitive` + the
  command in `UUC2GLRenderDevice::Exec`.

### Step 2: O-5, the DM-Azure pool. Cause found and fixed.

Facts (all read from the running game, `logs/fx/probe-azure-*.out`, `pool-*.out`):
- The pool is `FluidSurfaceInfo0` at (8320,4068,-1322), rotation yaw 16384, DrawScale3D (-1.2,0.8,1), 36x68
  grid, spacing 20, `CullDistance` 2000, `bUnlit`, `FluidColor` (115,115,115,0), skin
  `Shader'T_Liquid.Decay.T_Liquid_Decay_Water_C_SD'`: Diffuse = `TexEnvMap T_Liquid_Decay_Water_C_ENV`
  (EM_WorldSpace, world reflection vector, 3D coordinates) on `Cubemap T_Liquid_Decay_Water_C_CM` (DXT1,
  128x128), Opacity = `Texture T_Liquid.Water.Liquid_Water_Opacity` (16x16 DXT3), OB_Normal.
- `getall Cubemap Faces`: **`T_Liquid_Decay_Water_C_CM` has five faces; the sixth (-Z) is None.**
- `GLPIXTRACE 780 330` on the pool: the water pass blends (50,57,65) -> (51,58,66). With `set Shader Opacity
  None` the pool is a flat (115,115,115) = `FluidColor`, i.e. the vertex colour: stage 0 (the cube) contributes
  nothing.

Cause: `FOpenGLTexture::Cache` (`Source/UC2GLDrv/Src/UC2GLResource.cpp`, cube branch) uploaded only the faces
that exist. A GL cube map with a missing face is incomplete, and fixed-function GL treats a unit with an
incomplete texture as disabled: the stage passes the vertex colour and vertex alpha (the fluid's `VertAlpha`,
mostly near 0) through, so the pool was a nearly invisible grey/black film. D3D (`D3DResource.cpp` 575-643,
Xbox and PC) allocates all six faces of a cube texture and fills those that exist: the cube stays complete and
the five real faces are sampled.

Fix (driver, one function + its caller): `UploadMip` accepts a NULL texture and uploads zeroed blocks/texels;
the cube branch uploads every face, empty ones zeroed (DXT1 zero blocks = opaque black). **Assumption**: the
missing face reads as zero on the Xbox (freshly allocated contiguous memory); it is the -Z face, which a water
surface seen from above never reflects.

Not the cause (checked): the PC/Xbox difference in the fluid's vertex colour (`FillVertexBuffer` writes
`FluidColor`, the Xbox push-buffer path writes (255,255,255,VertAlpha); `UnFluidSurface.cpp` 584 vs 1306-1308).
With the cube complete, the shader takes colour from the cube and alpha from cube x opacity: the vertex colour is
not read. Left as is.

### Step 3: O-6, the waterfall: what it is and what the first measurements say

- The waterfall is two `StaticMeshActor`s with the same mesh `S_NK_Decorations.Misc.NK_Deco_Misc_Waterspout01`
  at (8318,4451,-1130) (the cobra's mouth), both `CullDistance` 3500, `bUseHWOcclusionTests`:
  `StaticMeshActor84` draws the mesh's own material `Shader T_Liquid_Falls_Water01_SD01` (diffuse combiner,
  env-map specular, OB_Brighten, two-sided: the visible water); `StaticMeshActor42` has
  `Skins=(PSDistortionMaterial'T_Liquid.Downfall.T_Liquid_Downfall_Water01_DIST04')`: DistortionAmount 15,
  Mask None, bTwoSided, distortion texture `TexScaler(4,5)` over `TexPanner` (rate 0.6) over
  `UCPixelShaderEffects.Distortions.UCP_T_Praxis_Distortion` (the texture of the Praxis panes).
- New in the pixel trace: during the accumulation stage it logs the whole accumulation target (texels with
  distortion, mean/max of the accumulated red/green offsets) and writes it as a PPM in `System/`.
- Measurements (llvmpipe, 1280x720, pawn at (8200,4200), yaw 14000, pitch 1500; `logs/fx/wf-*.out`):
  - first person, standing still: the waterfall is drawn in the accumulation pass every frame and the offsets
    change from frame to frame (the panner): pixel (250,420) (6,3) -> (1,23) -> (12,2) over three frames; the
    full-screen quad moves the main pixel accordingly.
  - time frozen (`set LevelInfo TimeDilation 0.0001`), same spot: first person 24,995 texels with distortion,
    mean offset (10.1,19.1), max (89,104); third person 9,138 texels, mean (10.3,14.9), max (46,64). The
    distortion is at least as strong in first person (the waterfall is larger on screen).
  - Frozen on/off pictures (`set PSDistortionMaterial DistortionAmount 0`): the stone behind the waterfall is
    displaced in both views.
  So on this machine (Mesa llvmpipe) the first-person, standing-still waterfall distortion is drawn. **Not
  reproduced yet.** The owner plays on this machine's NVIDIA Quadro P620 (`lspci`), which none of our tests
  use. Next: run the same scenario on the NVIDIA GPU headless (EGL), if that can be done without touching the
  owner's session.
- Tools: `tools/dev/fx-owner.sh` (scenario runner on top of `m10-inject.py`: fixed camera, the virtual pad for
  movement, shots); `tools/dev/run-uc2-shot.sh` takes `UC2_BIN=` (a second executable in the same tree for
  before/after pictures; `run/fx/System/uc2-before.bin` = this branch without the cube-map fix).
