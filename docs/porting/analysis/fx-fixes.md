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

### Step 4: O-7, the DM-Praxis panes at a distance: the knob, measured, then raised (tuning, as the owner asked)

The coordinator relayed the owner's clarification: "not meant to be a fix per se but figure out what tuneable is
there AND THEN increase it"; the effect itself is not to be restructured.

Facts (`logs/fx/praxis-probe-0.out`, `fx-praxis-far.out`):
- The panes are `StaticMeshActor`s with `StaticMesh DM-Praxis.Arch.S_Praxis_DistWindow01` (two groups, at
  (0,-672,432) and (0,2208,432)) and `S_Praxis_Window_Distortion_01` (-2720,128,192); material
  `PSDistortionMaterial DM-Praxis.Effects.T_Praxis_Largewindow_Dists01`: DistortionAmount 30, Mask
  `T_Praxis_DiskMask01`, distortion texture `TexScaler T_Praxis_Largewindow_DistScaler01`. Every pane actor has
  `CullDistance` 0 (never culled). Nothing in `FPostProcessRender::Render` or the post-process list culls by
  distance or screen size.
- **The knob that sets the range**: `DISTORTION_CULL_DISTANCE = 5000.f` in Epic's
  `FD3DRenderInterface::HandlePSVertexShader` (`Source/D3DDrv/Src/D3DMaterialState.cpp` 1377), passed as
  `VSC_DISTORTION_SCALE.z = 1/5000` to `DistortionAccum.vsh`, which multiplies each vertex's distortion by
  `1 - saturate(z * 1/5000)^2` (z = camera-space depth). 75% left at 2500 units, 36% at 4000, 0 at 5000. Our port
  had the same constant (`Source/UC2GLDrv/Src/UC2GLMaterialState.cpp`, `HandlePSVertexShader`).
- A second, smaller factor with its own constant (not changed): the accumulation pass samples the distortion
  texture with `TextureMipLODBias = -1.8f` (`D3DPSMaterialState.cpp` 372; ours `UC2GLPSMaterialState.cpp` 383);
  far away the texture is minified and its mip levels average the offsets towards zero. Measured at the far
  view: a stronger bias (-4 or -8, identical: the mip chain bottoms out) adds about 15% to the mean offset.
  Also inherent and not a knob: the displacement is in screen pixels (at most `MaxDistortion` = 35 of 480),
  so a pane far away moves the same few pixels while it gets smaller; and the accumulation target is half
  the screen size.
- Measured at the view of the first PlayerStart (-1220,288,690, yaw 5220, the round room's panes about 2650
  units away; frozen time; mean accumulated offset r,g over the 4582 texels the panes cover):
  DistortionCullDistance 5000 (Xbox): 6.32, 9.97; 15000: 8.51, 13.33; 1000000 (no fade): 8.81, 13.78.
  Close to a pane (0,1850,450): 10.41, 18.04 over 51,713 texels.

Change: the constant is now a setting, `[UC2GLDrv.UC2GLRenderDevice] DistortionCullDistance` (read at the first
distortion draw; missing or not positive = 5000, the Xbox value), console `DISTORTIONCULLDISTANCE [<units>]`.
`Engine/Config/Default.ini` sets 15000: 97% of the distortion is left at 2650 units, 89% at 5000. Row in
`Engine/Config/README.md`. Nothing else about the effect changes.

Pictures (`tools/dev/fx-owner.sh fx praxis-far praxis-near`): `logs/fx/fx-praxis-far-xbox.png` (5000),
`-raised.png` (15000), `-nofade.png`; crop `logs/fx/o7-praxis-far-xbox-vs-raised-crop.png` (2x, top 5000,
bottom 15000). At this camera the difference is visible but modest (mean pixel difference in the pane area
about 1 of 255: pane edges and the white lamps a little more displaced), because 2650 units is where the Xbox
fade has only taken 28%. Farther away (the owner's picture may be from farther than my camera; I could not
match it exactly) the difference grows: at 4000 units 36% -> 93%. Label: **seen only**, plus the numbers
above; the Xbox reference shows the faded state, which `DistortionCullDistance=5000` reproduces by
construction (same formula and constant as Epic's driver).

### Step 5: O-5 pictures, the splash fade

- `tools/dev/fx-owner.sh fx pool` and `UC2_BIN=uc2-before.bin tools/dev/fx-owner.sh fx pool` (same camera:
  pawn at (8320,3480,-1218), yaw 16384, pitch -5000, third person, 1280x720, llvmpipe):
  `logs/fx/fx-pool-before-on.png` (without the fix: the pool is the bare floor under a nearly invisible film),
  `logs/fx/fx-pool-on.png` (with it: a darker water surface over the floor, the cube map's reflection with a
  golden ring under the cobra and golden glints), `fx-pool-nowater.png` (FluidSurfaceInfo hidden).
  Side by side with the owner's Xbox picture: `logs/fx/o5-pool-before-after-xbox.png` (before | after | xemu).
  Against the Xbox: the golden ring at the far end and the floor seen through the water match; the Xbox shows
  more golden streaks across the middle and its pool reads lighter (its camera is higher, its dynamic gamma
  state and the mega health's glow differ); the mega health pickup had not spawned in my run. Label:
  **matches the owner's Xbox screenshot in kind (water layer, golden reflections), not pixel-close; cameras
  differ.**
- Splash: `fx-pool-splash` drops the pawn into the pool (`logs/fx/o5-splash-strip.png`, 0.25 s steps; the
  camera moves with the fall, so frames are not comparable pixel by pixel). The splash's distortion sprite
  (`UCEM_Impact_Water_FootSplash.SpriteEmitter1`, `FX_Part_Shock_Combo_Distort_Shd`, FadeIn/FadeOut) fades by
  particle alpha (`UParticleEmitter`, `Color -= FadeFactor * MaxFade` for the default draw style,
  `UnParticleEmitter.cpp` 1279-1293); the sprite renderer calls `EnableLighting(0,1)` (RS_COLORVERTEX), so
  the vertex shader gets ambient alpha 0 and the vertex alpha (D3DRenderState.cpp 483: same rule in our
  `GetStateVertexShaderEnum`/uniforms), and `DistortionAccum.vsh` scales the distortion by that alpha: the
  distortion fades with the particle exactly as on the Xbox. **Inferred from the code, not measured**; with
  the water surface back the owner's "jarring" impression should be judged again. Not changed.

### Step 6: O-6 conclusions so far

- Not reproduced, on Mesa llvmpipe nor on this machine's NVIDIA Quadro P620 (the owner's GPU; driven headless
  through SDL's offscreen video driver and EGL: `UC2_GPU=1 tools/dev/fx-owner.sh ...`, nothing on the owner's
  display; GPU memory 617 -> 648 MiB during the run). First person, standing still at the cobra waterfall: the
  waterfall is in the post-process list every frame, its accumulation covers about 25,000 texels of the
  640x360 target with mean offsets (12.6,16.9) on NVIDIA and (12.8,15.6) on llvmpipe, the full-screen pass
  moves the pixels behind it, and the offsets change every frame (the panner). Moving (virtual pad) only
  changes the waterfall's size on screen (stats in `logs/fx/fx-wfmove*.out`). The NVIDIA post-process self-test
  passes 10/10 (`logs/fx/gpu-selftest-0.out`, includes the depth copy). It is the only distortion item in the
  frame in both first and third person, so "it only shows when a splash is also in the list" is not what
  happens in these runs.
- What the owner's two pictures differ in, that I can see: in third person the camera is farther, the
  distortion texture is smaller on screen and its wobble has a higher spatial frequency, crossing the bright
  block edge behind the water; in the close first-person view the same pixel offsets make a smooth, slow
  displacement (texture magnified), which reads as "straight" in a still picture. That explains a weaker
  look, not a complete absence; the owner's first-person picture shows a perfectly straight edge behind the
  water, which my runs do not. **Status: not fixed (not reproduced). Needed from the owner: the exact spot,
  window size, and whether it happens right after spawning or after switching views.**
- "Waterfall drawn on top of the character" (third person): not reproduced as such. Two mechanisms of Epic's
  method put the effect over the edge of a pawn standing in front of the water: the depth test of the
  accumulation pass uses a half-size copy of the depth buffer (one depth sample per 2x2 pixels: thin parts
  such as ears and horns are missed half the time), and the full-screen pass reads displaced pixels whatever
  they belong to (defect 9 in postprocess.md). Both are the Xbox's design too (its copy target is half the
  320x240 bloom size). Inferred, not changed.
- Side finding on the NVIDIA GPU (outside O-5..O-7, reported for a separate task): the fixed-function path
  has 4 texture units and no `combine3` there (`UC2GLDrv: 4 texture units (GL reports 4) ... combine3 no`), and
  materials that need `D3DTOP_MODULATEALPHA_ADDCOLOR` fall back to another op: DM-Azure's wooden pillars
  (`Shader T_NK_MeshSkins.Decorations.T_NKMesh_Deco_Wood02_Snow_SD`, stage 1 colour op 18 on llvmpipe, 5 =
  MODULATE2X on NVIDIA, `logs/fx/fx-pillar*.out`) look like orange metal on NVIDIA and like wood on llvmpipe.
  The cobra statue also looks different. NVIDIA has `GL_NV_texture_env_combine4`, which can express that op.

## Hunks in `Source/UC2GLDrv` (for the hand merge with `m11-webgl`), base `2208566`

The web branch (`m11-webgl`, merge base with this branch `776ed23`) rewrote large parts of `UC2GLResource.cpp`
(`UploadMip`: driver-side DXT decode, `PrepareStorage`) and `DrawPrimitive`; textual conflicts are expected in
hunks 1-2 and 4. What each hunk does, so it can be re-applied by meaning:

1. `UC2GLResource.cpp`, `FOpenGLTexture::UploadMip` (4 hunks, the fix of O-5): `Texture` may be NULL (a cube
   face the cubemap leaves empty). Every read of the source (`GetTextureData`, `ConvertDXT`, and on the web
   branch also the driver's own DXT decode and the last-mip readers) is skipped for NULL and the upload gets
   zeroed data of the same size and format (DXT: zero blocks = opaque black for DXT1, transparent black for
   DXT3/5; uncompressed: zero texels). Comment above the function explains why.
2. `UC2GLResource.cpp`, `FOpenGLTexture::Cache`, cube branch (1 hunk, the fix of O-5): the face loop calls
   `UploadMip` for every face, NULL included, and `UnloadRawTextureData` only for faces that exist. (If the web
   branch's `PrepareStorage` allocates immutable storage for all faces, a cube is complete there anyway on that
   path, but the desktop path still needs this.)
3. `UC2GLMaterialState.cpp`, `HandlePSVertexShader` + a global above it (2 hunks, O-7): new global
   `FLOAT GUC2GLDistortionCullDistance` (0 = not read yet); Epic's `static FLOAT DISTORTION_CULL_DISTANCE =
   5000.f;` becomes: on first use read `[UC2GLDrv.UC2GLRenderDevice] DistortionCullDistance` with
   `GConfig->GetFloat` (missing or <= 0: 5000), log one `Init` line, use it. The web branch did not touch
   this file.
4. `UC2GLRenderInterface.cpp`, `DrawPrimitive` (2 hunks, debug only): before the draw, if
   `UC2GL_PixelTraceActive(RenDev)`, read the traced pixel; after it, read it again and call
   `UC2GL_PixelTraceLog`. Three `extern` declarations inside the function. Quiet unless armed.
5. `UC2GLRenderDevice.cpp`, `Exec` (1 hunk): console commands `DISTORTIONCULLDISTANCE [<units>]` (O-7) and
   `GLPIXTRACE <x> <y> [all]` (debug), inserted before `POSTSELFTEST`.
6. New file `UC2GLPixelTrace.cpp` (debug only; picked up by the `Src/*.cpp` glob): arming, per-pass logging,
   accumulation-target statistics and PPM dump. Uses `glReadPixels`, `glPixelStorei`, `GFileManager`; on a
   multisampled main framebuffer (the web branch's anti-aliasing) the main-framebuffer read would need the
   resolved copy instead.

No header changed. Engine side: `Source/Engine/Src/UC2NetTest.cpp` (test command `NETPLACE` takes an optional
pitch; default 0 = old behaviour; not reachable without the console). No engine rendering code changed, so a
device without the post-process capability behaves exactly as before (the cube-map fix is a driver change
that applies with and without post-processing: see the pinned check in the gate).

## Gate (2026-10-07, final tree; every run through `<WorkUC2>/bin/capped`, no ceiling hit, no exit 137)

- (a) `capped 6G 3600 -- cmake --preset native && cmake --build build-native -j6`: `[100%] Built target
  SDLLaunch`, `native build exit 0` (`logs/fx/gate-native-build.log`). `capped 6G 5400 -- source env.sh && cmake
  --preset wasm-perf && cmake --build build-wasm-perf -j6`: `[100%] Built target SDLLaunch`, `wasm build exit 0`
  (`logs/fx/wasm-build.log`; UC2GLDrv is not in the web build on this base, the Engine change is).
- (c) Fresh run tree `tools/dev/mkrun-uc2.sh build-native fx`, then `tools/dev/m7-effects.sh fx`
  (`logs/fx/gate-m7-effects.txt`): self-test `10 passed, 0 failed`; bloom, blur, berserk, liandri, pause,
  radial, dyngamma, dof-alpha, azure, octane, decay, undertow, recoil, praxis-panes: every line
  `critical=0 glerrors=0 programs_failed=0+0 left_behind=0` (praxis on the first try).
  `capped 1G -- python3 tools/psh2glsl.py --selftest`: `100 shaders, 99 compared ... 0 failed, 0 lint warnings;
  7 synthetic semantics checks, 0 failed`; `--gputest` (xvfb, capped 2G): `107 shaders rendered, 6848 cases, 0
  outside the tolerance ... 0 shaders failed`; `--check-emitted`: `102 files, 0 differ, 0 stale` (translator
  untouched). `./ucc-bin verifyclasses` (capped 2G): `All native class layouts match`, exit 0.
  DM-Deadbolt pinned picture with `-nopostprocess` (`tools/dev/m12/deadbolt-pinned.scen` through
  `m10-controls.sh --file`, this branch in run tree `fx-pin` against main's build `engine-uc2/build-native` in
  `fx-ref`): same view `loc=(1531.9,3111.5,-34.1) rot=(0,-40880,0)`, `pinned-compare.py`: `OK 99.997 90.9 24.77`
  (static part 99.997% within 2 levels; the M13 threshold is 99.9), 0 GL errors in 207 frames, 0 Critical.
- Ceilings used: builds 6G, game runs 2G (`run-uc2-shot.sh`, `fx-owner.sh`, `m10-controls.sh`), psh2glsl 1G/2G,
  mkrun 2G. The NVIDIA runs (offscreen EGL) took the GPU's memory from 617 to at most ~650 MiB of 2048.
