# F-5 renderer items (owner review of 2026-10-09): notebook

Branch `f5-render`, worktree `<WorkUC2>/engine-uc2-f5-render` (from main `1372dec`), run trees `<WorkUC2>/run/f5*`,
logs and pictures `<WorkUC2>/logs/f5`. Brief: PLAN.md "Owner review of 2026-10-09", task F-5. Every run through
`<WorkUC2>/bin/capped` (game 2G, builds 6G `-j4`).

Steps, in the agreed order, each a commit + an entry here:
1. small fixes: `DOFShippedBlurAlpha` compiled default 1; dynamic gamma stays on; `DistortionCullDistance` one source of truth;
2. occlusion queries: what the Xbox used them for, whether engine-ut2004 has them; implement only if visible and cheap;
3. shader inventory: every shader a material / post-process layer can select, ported or a reason;
4. CTF-Aqueduct shadow flicker under the canyon trees: reproduce, measure, cause, fix, before/after strip;
5. shadow strength vs the Xbox: what Epic's code implies, a spot for the owner's xemu capture, our picture;
6. DM-Azure sky haze: desktop vs GLES twin A/B with numbers and an explanation.

## Log

### 2026-10-09 01:55 start

Read: PLAN.md owner review, REPORT.md section 20 (R-1..R-22, open issues), the notebooks named in the brief.
Native build started in the worktree (`capped 6G 3600 -- cmake --build build-native -j4`, log `logs/f5/build-native.log`).

### Step 1 (02:10): small fixes. Done, measured.

**`DOFShippedBlurAlpha` compiled default 1** (`Source/UC2GLDrv/Src/UC2GLRenderDevice.cpp`, the defaults block after
`StaticConstructor`'s property list). The value the DOF uses stays the shipped 65 (it is an alpha byte, the owner's
"radius" reading notwithstanding); only an ini without the key changes (it now also gets 65, not script's 100 in
third person). `Engine/Config/README.md` row 18 says so.

**Dynamic gamma: on, stays on.** Who sets it, read in the code:
- the device, at the first frame drawn with post-processing on (`UC2GLRenderDevice.cpp` `Lock`, `XboxClientDefaultsApplied`):
  `UClient::DynamicGamma = 1`, `LuminosityBias = 1` (what `UClient::Init` does under `_XBOX`);
- Epic's own `USplitScreenMgr::UpdateDetailLevels` (`Engine/Src/UnSplitScreenMgr.cpp:1040-1055`) executes `DYNAMICGAMMA 1`
  whenever no split view is active and the menu game is not running, and `DYNAMICGAMMA 0` with split views (the ramp
  is full screen). So the Xbox logic itself turns it back on; nothing in our code turns it off.
- `DYNAMICGAMMA 0|1` (Epic's console command, `UnCamMgr.cpp:253`) stays as the per-session debugging aid; note that
  the split-screen manager may turn it on again at its next detail update. No change made.

**`DistortionCullDistance`: one source of truth.** Before: the GL4ES device (web, twin) read the key with a hard-coded
`GConfig->GetFloat("UC2GLDrv.UC2GLRenderDevice", ...)` in `UC2GLMaterialState.cpp` (`HandlePSVertexShader`), once, into a
global; its own section's copy (`[UC2GL4ESDrv.UC2GL4ESRenderDevice] DistortionCullDistance=15000`, in both
`Default.ini` and `web/Default.ini`) was dead text; the compiled default (no line) was 5000. Implication today: none
visible (both inis carry 15000 in both sections), but a user who edits the GL4ES section (or `-ini:` on it) got
nothing, and a native ini without the desktop section's key gave the web 5000.
After: `DistortionCullDistance` is a `CPF_Config` `UFloatProperty` of `UUC2GLRenderDevice` like the device's other keys
(`Inc/UC2GLRenderDevice.h`, the property list in `StaticConstructor`), so UE's per-class config reads it from the
section of the device's own class: `UC2GLDrv.UC2GLRenderDevice` for the desktop, `UC2GL4ESDrv.UC2GL4ESRenderDevice` for
GL4ES; the material code reads `RenDev->DistortionCullDistance` (not positive = the Xbox's 5000); the console command
`DISTORTIONCULLDISTANCE` sets the property; the global `GUC2GLDistortionCullDistance` is gone. Compiled default 15000,
as the ini (the owner's O-7 value; the owner's "sensible defaults in the cpp" for DOF applied here too). Why not one
shared section for all render keys: every other device key is already per class (UE's convention, and what the web ini
generator `sync-web-ini.py` and the README assume); one exception read from a foreign section was the inconsistency.

Evidence (tree `run/f5v` with both keys deleted from both device sections of its `Default.ini`; `logs/f5v/`):
- desktop, DM-Praxis: `get UC2GLRenderDevice DOFShippedBlurAlpha` -> `True`, `DistortionCullDistance` -> `15000.000000`,
  log `distortion fades out over 15000 units (DistortionCullDistance of UC2GLDrv.UC2GLRenderDevice ...)`; 0 GL errors.
- twin (`-gl4es`, `-ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:DistortionCullDistance=7000`): the GL4ES device reads
  7000 (`get UC2GL4ESRenderDevice DistortionCullDistance` -> 7000, the desktop class still 15000), log `distortion fades
  out over 7000 units (DistortionCullDistance of UC2GL4ESDrv.UC2GL4ESRenderDevice ...)`, console
  `DISTORTIONCULLDISTANCE` shows 7000; 0 GL errors.
- `tools/dev/sync-web-ini.py --check`: both web files up to date (no ini change was needed).

### Step 2 (02:20): occlusion queries. Explored; not implemented (no visible use on the Xbox). Read only.

**What UC2 uses them for** (all read in `epic-sources`, nothing run): one system, `FOcclusionQueryManager`
(`Engine/Src/UnRenderVisibility.cpp:319-909`, Scion's "jg" code; not in UT2004), culling of **dynamic actors**:
- Which actors: `AActor::bUseHWOcclusionTests` (`Actor.uc:47`), true in the defaults of `StaticMeshActor`, `Pawn`,
  `Emitter`, `Pickup`, `FluidSurfaceInfo`, `Projector`, `UCAmbient_Creature`; boxes larger than 2048 units across are
  never tested (`UnRender.cpp:917-928`, `MAX_BOX_SIZE`), nor anything within 64 units of the camera, nor skyboxes,
  shadow/child scene nodes or the editor (`CanUseHardwareOcclusion`, `UnRender.h:695`).
- How: an actor seen last frame is drawn normally inside a query ("deferred": `Projector.uc` sets
  `bUseDeferredHWOcclusionTests`, `Pawn.uc` explicitly not); otherwise its world bounding box is drawn as a **wireframe**
  unit cube scaled to the box, colour/alpha/depth writes off, depth test on (`SubmitQueries`, `RenderQuery` :427-461; the
  Xbox through a push buffer and the compressed-mesh vertex shader, PC through `DrawPrimitive`). Next frame
  `UpdateQueryResults` (:720-783) reads the pixel count: 0 = the actor is skipped by the visibility pass
  (`UnRenderVisibility.cpp:1673, 1709, 2670`: not added to the draw lists, and not offered to the corona list).
  Unfinished queries keep the actor visible and are retried. Epic's comment: "UE2X can render 1500 bounding boxes in
  ~3.4ms (CPU) on an Xbox": a vertex/fill-rate saving for the NV2A.
- Coronas do **not** depend on it: `FCoronaRender::IsOccluded` (`UnRender.cpp:110-150`) fades coronas with its own line
  traces (`SingleLineCheck`), as in UT2004. Nothing else reads the result (no gameplay, sound or AI use; only
  `AActor::LastRenderTime` is not refreshed for an occluded actor, which is "not rendered" in both cases).
- Epic's PC D3DDrv: `Begin/EndOcclusionQuery` return 1, `GetOcclusionQueryResults` returns 1 and **never writes the count**
  (`D3DDrv/Src/D3DRenderInterface.cpp:3713-3777`); the caller pre-sets `dwPixels = 1`, so PC = "always visible". Ours
  writes `0x7fffffff` (`UC2GLRenderInterface.cpp:2305`) and skips drawing the query boxes (`:2072-2079`), same result.

**engine-ut2004** (`/home/ion/WorkUnreal/engine-ut2004`, read only): `OpenGLDrv`/`GL4ESDrv` have no occlusion query at all
(`grep -ri occlusion Source/` finds only the `glext.h` headers); UT2004's `FRenderInterface` has no such methods. So this
is a UC2/UE2X (Xbox) addition, not UT2004 behaviour.

**Decision: stop here, as the brief says for a non-visible use.** The Xbox used queries only to skip drawing actors
that were already hidden: the picture is the same with "always visible", the cost is draw time only (on this GPU the
frame time is ~9-12 ms with everything on, R-1/R-13). A real implementation on desktop GL (core since 1.5:
`glGenQueries`, `glBeginQuery(GL_SAMPLES_PASSED)`, `GL_QUERY_RESULT_AVAILABLE`) is about half a day, but would bring
the Xbox's one-frame-late visibility (an actor coming round a corner is missing for a frame) and the wireframe-box
test (an actor whose box edges are all hidden but which shows through a gap is culled wrongly): both artefacts, no
gain the player sees. GL4ES/WebGL 1 have no occlusion query (WebGL 2 has `ANY_SAMPLES_PASSED` only, F-3's area).
Revisit only if a profile shows draw calls of hidden actors dominate a frame.

### Step 3 (02:40): shader inventory against "can a material or a post-process layer select it?". Read + counted.

Criterion (the owner): a shader that a material (including a post-process layer) can be assigned must be ported; one
that no material can reach may stay. Sources: `Shaders/EPixelShader.tsv` (the `_XBOX` switch of Epic's
`GetPixelShader`), every `.uc` class under `PixelShaderMaterial` (script below), the handler table of
`UC2GLPSMaterialState.cpp` (Epic's 24 handlers, all present, none stubbed), the engine sites that set `PixelShaderType`
themselves, and `FD3DCombinedVertexShader` (`D3DResource.cpp` 2230-2640) for the vertex side.

**Result: nothing reachable is missing; no new port was needed, so there is no newly ported shader to give a golden
test.** The existing golden tests already cover every reachable pixel shader: `psh2glsl.py --selftest` (100 shaders,
interpreter against Epic's compiled definition words), `--gputest` (the GLSL of all 100 + 7 synthetic against the
interpreter), `m11-twin.sh progtest` (all 97 enumerated shaders, 5 vertex shaders each = 485 complete programs, desktop
against GL4ES). "Never drawn by shipped content" (R-8's 12) is not "not ported": each of those is compiled and has its
handler; a map or mod that references the class gets it.

Material classes (script, `Source/*/Classes`), with the enum each selects by default (handlers switch within the family):
`PSAdditiveDistortion` (AdditiveDistortion), `PSBackBufferEffect` (MaskedLuminance), `PSBloomSelectMaterial`,
`PSBlurMaterial`, `PSColorRemapMaterial` (ColorRemap, Masked), `PSDepthOfFieldMaterial`, `PSDiffuseOpacity`,
`PSDiffuseOpacitySpecularMask` (+SpecLight, 2), `PSDistortionMaterial`, `PSDualDiffuse` (32 variants by its properties),
`PSEmissiveConstColor`, `PSEmissiveShader` (DiffuseOnly/Spec/SpecLight), `PSEmissiveSkinShader` (+SpecLight, Alpha),
`PSEmissiveSpecular`, `PSLuminanceTexIndexMaterial`, `PSModulatedDistortionMaterial`, `PSMotionBlurMaterial`,
`PSPlayerCamMaterial` (+Noise, TwoNoise), `PSRefractionMaterial`, `PSShenuSkin`, `PSSkinShader` (+SpecLight),
`PSSkinShaderOpacity` (+SpecLight), `PSSubtractiveDistortion`, `PSTeamShader` (+SpecLight, NoSpec, TeamBSPShader),
`PSTeamSkinShader` (+SpecLight, SpecLightAlpha), `PSTeamSkinShaderOpacity` (+SpecLight), `PSUseTexIndexMaterial`
(UseTexIndex, Masked). Engine-made objects: `UCScreenMotionBlur` (Mask), `UCScreenEmissive` (EmissiveSelectBlur),
`UCScreenLiandri` (ColorRemapGrayScale), `UShadowBitmapMaterial` (ShadowBlur), `UProjectorMultiMaterial` (the 12
projector shaders), terrain (Terrain3Layer). Post-process `SubAction`s: only `SubActionCameraEffect` (adds a camera
effect object: the same classes); `MotionBlur`/`ScreenFlash`/`ScreenFade` (Engine `CameraEffect`s) draw with ordinary
`Shader`/`FinalBlend` materials, no pixel shader.

**Pixel shaders (100 `.psh`, 98 enum values)**

| Enum values | Shaders | Selected by | Status |
|---|---|---|---|
| 1 | Terrain3Layer | terrain pass, `MaxSimultaneousTerrainLayers=3` | ported (R-16; `UseTerrain3Layer`, off by default: no visible difference) |
| 2 | Terrain4Layer | terrain with 4 layers per pass: a PC ps.1.4 cap no Xbox and no device of ours reports | **left out: no Xbox shader exists** (Epic's is inline PC ps.1.4 text in `D3DRenderDevice.cpp:2353`, no `.psh`), and no material selects it |
| 3-6, 8, 10 | Blur, DOF, Mask, MotionBlur, BloomSelect, EmissiveSelectBlur | `PSBlurMaterial`, `PSDepthOfFieldMaterial`, `UCScreenMotionBlur`, `PSMotionBlurMaterial`, `PSBloomSelectMaterial`, `UCScreenEmissive` | ported (M7); Mask never drawn by content (no script creates a `UCScreenMotionBlur`) |
| 7 | Refraction | `PSRefractionMaterial` | ported (M7); no object in any shipped package |
| 9 | EmissiveSelect | only a static object inside Epic's `#if 0` (`UCCameraEffects.cpp:1222`) | ported (compiled, handler present); no live selector |
| 11, 12, 90 | LuminanceTexIndex, UseTexIndex, UseTexIndexMasked | `PSLuminanceTexIndexMaterial` (and dynamic gamma), `PSUseTexIndexMaterial` | ported (M7); UseTexIndex* in no shipped package |
| 13-17 | Distortion, Modulated/Additive/SubtractiveDistortion, DistortionAccum | the distortion classes; accumulation by `FPostProcessRender` | ported (M7); additive/subtractive never drawn by content |
| 18-26 | the skin shaders | `PSSkinShader`, `PSTeamSkinShader`, `PSSkinShaderOpacity`, `PSTeamSkinShaderOpacity` | ported (M6) |
| 27-30 | TeamShader family | `PSTeamShader` | ported (M6) |
| 31-62 | 32 DualDiffuse variants | `PSDualDiffuse` | ported (M6); no shipped package (tested with `-gldualdiffuse=`) |
| 63-67 | DiffuseOpacitySpecularMask family, ShenuSkin | `PSDiffuseOpacitySpecularMask`, `PSShenuSkin` | ported (M6) |
| 68-75 | emissive skin / emissive shaders, EmissiveSpecular, EmissiveConstColor | the emissive classes | ported (M6) |
| 76-87 | 12 projector shaders | `UProjectorMultiMaterial` (static projector batching) | ported (M7b) |
| 88 | MaskedLuminance | `PSBackBufferEffect` | ported (M7); never drawn by content |
| 89 | ShadowBlur | `UShadowBitmapMaterial` (Xbox shadows) | ported (M7b) |
| 91-93 | PlayerCam, Noise, TwoNoise | `PSPlayerCamMaterial` | ported (M7); noise variants never drawn by content |
| 94 | DiffuseOpacity | `PSDiffuseOpacity` | ported (M6) |
| 95, 96, 98 | ColorRemap, ColorRemapMasked, ColorRemapGrayScale | `PSColorRemapMaterial`, sniper overlay, `UCScreenLiandri` | ported (M7); Masked never drawn by content |
| 97 | DepthSwizzleColor | only the Xbox driver's own `CopyDepthBuffer` (no material) | ported (compiled); our driver copies depth its own way (R-6) |
| none | `DistortionBlended.psh`, `DOF_Debug.psh`, `LuminanceTransform.psh` | no enum value: `GetPixelShader` never returns them | translated by `psh2glsl.py` (golden-tested), not compiled by the driver: **no material can select them** |

**Vertex-shader fragments (67 `.vsh`)**: 49 ported, 2 are include files, 16 left out, every one for a stated reason.
(R-4's "19 of 67" counted fragment kinds; the 49 counts files: each of the 4 stages' `TexSource*`, `TexPass*`,
`TexTransform*`, `TexSourcePass*`, `TexSourceTransform*_m4x2/m4x3z/m4x3w` is generated by the same loop.) The five unique
vertex shaders Epic defines (`EUniqueVertexShader`: Distortion, Projector_Atten, Projector_Atten_Clip, ColorRemap,
DistortionAccum) are all ported.

| `.vsh` | status |
|---|---|
| `AddVertexRGB` | ported |
| `AddVertexSpec` | left out: no code selects it (in Epic's fragment table, `D3DResource.cpp:2034`, never spliced) |
| `ColorRemap` | ported (M7) |
| `DefaultPosition` | ported |
| `Distortion` | ported (M7) |
| `DistortionAccum` | ported (M7) |
| `Fog` | ported |
| `InitLighting` | left out: no class selects it (Epic compiled the per-light path out: `HARDCODED_VS_LIGHTS`, `UnRenDev.h`; `D3DResource.cpp` 2357-2400 `#else` branch) |
| `InitRGB` | ported |
| `LightAmbient` | ported |
| `LightDirectional` | left out: as InitLighting |
| `LightHardDirectional5` | ported (the only light path Epic compiles: `HARDCODED_VS_LIGHTS`) |
| `LightPoint` | left out: as InitLighting |
| `LightSpot` | left out: as InitLighting |
| `MulVertexSpec` | ported |
| `NextLight` | left out: as InitLighting |
| `NotBoned` | ported |
| `OutLightRGB` | ported |
| `OutVertexRGB` | left out: no code selects it (`D3DResource.cpp:2031`, never spliced) |
| `ProjectorAtten` | ported (M7b) |
| `ProjectorAttenOut` | left out: not even in Epic's fragment table (dead source file) |
| `ProjectorDistanceAtten` | left out: not even in Epic's fragment table (dead source file) |
| `Screenspace` | ported (GL's own viewport transform does its job) |
| `ShaderConstants` | not a fragment: register-name include |
| `ShaderRegisters` | not a fragment: register-name include |
| `SkelFourBoned` | left out: as SkelOneBoned |
| `SkelOneBoned` | left out: GPU skinning, needs the Xbox GPU vertex stream (`VS_UnpackPosition|VS_SkelThreeBoned`, `GHardwareSkinning`, which only the Xbox sets; PC skins on the CPU) |
| `SkelThreeBoned` | left out: as SkelOneBoned |
| `SkelTwoBoned` | left out: as SkelOneBoned |
| `TerrainSpecial` | left out: needs the packed Xbox terrain vertex (`VS_TerrainSpecial`, set only under `_XBOX`, `UnTerrain.cpp:3544`) |
| `TexCamNormal` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexCamPosition` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexCamReflect` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexPass0` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexPass1` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexPass2` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexPass3` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSource0` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSource1` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSource2` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSource3` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceCamNorm` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceCamPos` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceCamRef` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourcePass0` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourcePass1` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourcePass2` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourcePass3` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTerrainSpec` | left out: same, terrain texcoords derived from the packed vertex |
| `TexSourceTexKill3` | ported (M7b) |
| `TexSourceTransform0_m4x2` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform0_m4x3w` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform0_m4x3z` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform1_m4x2` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform1_m4x3w` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform1_m4x3z` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform2_m4x2` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform2_m4x3w` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform2_m4x3z` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform3_m4x2` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform3_m4x3w` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexSourceTransform3_m4x3z` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexTransform0` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexTransform1` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexTransform2` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `TexTransform3` | ported (texture-coordinate loop of `BuildVertexShaderSource`) |
| `UnpackPosition` | left out: needs the packed Xbox vertex format (`FPackedPosition`, `XBOX_ASSET_FORMAT`); PC streams carry plain positions |

Counts: 49 ported, 16 left out, 2 includes; 67 files.

### Step 4 (02:20-03:00): CTF-Aqueduct shadow flicker. NOT reproduced; measured under 9 conditions; no fix made.

**What the shadows are** (`GLSCENECENSUS`, `OBJ DUMP`, logs `logs/f5/fx-aq-*.out`): 16 level-static projectors, all
with the texture `T_NK_Foliage.Shadows.NKFol_Shadow_Cherry01_Osc`, a `TexOscillator` (U stretch, V pan, rate 0.03 Hz,
amplitude 0.05: a slow sway, about one cycle in 33 s), modulate, `bGradient` on all, **`bProjectOnBackfaces` on 12 of the
16** (census letter `k`: Projector0, 1, 4, 5, 7, 8, 9, 10, 13, 15, 16, 17; not on 2, 3, 11, 12), projecting
on BSP/terrain/static meshes/actors; drawn as Xbox static-projector batches (R-14). No shadow projector or decal at load.

**Tools** (committed): `tools/dev/f5-flicker.sh` (one GPU run, headless, SDL offscreen as `fx-owner.sh UC2_GPU=1`;
consecutive frames, one `exec shot` per tick; phases `run` (time running, camera still), `frozen`, `move` (time
stopped; per frame the view turns `F5_STEP` yaw units and/or moves `F5_DXY` units: a path that repeats exactly, so a
run with projectors and one without (`-ini:System:[SDL2Drv.SDL2Client]:Projectors=False`) compare frame by frame),
`walk` (time running, pad forward); `F5_GL4ES=1 F5_SOFT=1` runs the GLES twin on Mesa). `tools/dev/f5-flicker.py`: the
measure: a pixel flickers at frame i when its luminance steps more than 24 levels one way and then back (an up-down-up
toggle; smooth motion and the slow sway do not count); per-frame share, mean change, a map, a strip.

**Measurements** (1280x720, AA 4x, `logs/f5/flicker/`; flicker = mean share of toggling pixels over the frames):

| Run | Condition | flicker | mean change/frame |
|---|---|---|---|
| `probe1` | 1p canyon floor (4300,300), time running, camera still | 0.28% (the first-person weapon's idle motion) | 1.12 |
| `probe1` | same, time stopped, view turned 8 yaw units a frame | 0.008% | 1.72 |
| `pawn-run` | 3p, pawn standing in the tree shadow at (4280,0), time running | 0.003% | 0.12 |
| `far-on` / `far-off` | 1p looking down the canyon floor (4300,600) at the far shadows, view turned 24 units a frame; projectors on / off | 0.032% / 0.039% | 3.62 / 3.89 |
| `fwd-on` / `fwd-off` | 1p walking 12 units a frame through the shadows of Projector10 (4300,400 -> 4300,112), time stopped; on / off | 3.38% / 5.07% (texture detail under translation: more without the shadows) | 10.1 / 12.3 |
| `fwd-twin` | same path, GLES twin (GL4ES, Mesa) | 3.46%; frame by frame against the desktop: mean 2.4-2.7 of 255 | 10.4 |
| `walk-desk` | 3p walking with time running (strip seen by eye) | 5.6% (motion) | 14.0 |
| `v1-on/off` | 1p floating over the canyon at (3813,-299,600) looking down at the projected rocks, frozen, turned 8 units a frame | consecutive frames 1.63 (on) vs 1.76 (off), every step equal | |

In none of them does the projected shadow change more than the scene around it from one frame to the next; frames
with time stopped and the camera still are identical (0.00-0.05 mean change), so nothing alternates between frames
(no "shadow texture every other frame", no projector drawn every other frame). The projector contribution (on minus
off, same frame) is a clean leaf pattern with soft edges, no speckle or stripes (z-fighting would show as noise).

**Why z-fighting is unlikely on this GPU, by arithmetic**: the batched projector geometry is drawn with `SetZBias(1)`
(`UnProjector.cpp` `RenderStaticPrimsBatch`), our `glPolygonOffset(0, -5)` (`UC2GLRenderInterface.cpp:796`, 5 depth
units); the near plane is 10 (`Core.cpp:236`), so at 3000 units one depth unit is 0.054 world units, the offset 0.27;
the copies of the geometry differ from the originals by float rounding (about 3e-4 units at coordinates near 5000).

**"Faces looking away from the light"**: by design of the map, as on the Xbox. 12 of the 16 tree projectors have
`bProjectOnBackfaces`; Epic's Xbox shader takes `max(N . -D, backface flag)` (`ProjectorAtten.vsh`, ported register
for register), i.e. full strength on surfaces facing away; the PC path skips its normal cube-map attenuation for them
too (`UC2GLMaterialState.cpp` 4515). UE2 projectors are not occluded either: the leaf pattern lands on everything in
the frustum within `MaxTraceDistance`, under overhangs and on rocks facing away included. Not a port defect.

**Side finding (for F-3, not changed)**: the GLES twin on the NVIDIA GPU (`F5_GL4ES=1` without `F5_SOFT`: an OpenGL ES
3.2 context from the NVIDIA driver) fails at start: "present shader does not compile: implicit cast from float to int"
and GL4ES-generated shaders with `clamp(float, float, float)`/`min(float, float)` overload errors, then `Assertion failed:
RenDev` (`logs/f5/flicker/fwd-twin.out` of the first attempt, overwritten; reproduced by the command above). NVIDIA's
GLSL ES compiler is stricter than Mesa's; the browser goes through ANGLE, so this is not the web path, but it is a
strictness bug in our present shader or GL4ES' output.

**What I need from the owner to go further**: native or web (and which browser/GPU), window size, AA, standing or
moving (mouse turn, walk), and a spot: `NETPLACE` coordinates are easiest from the console (`NETPROBE x` prints the
pawn's location in the log), or a screenshot of the place. Every condition above is re-runnable with one command.

### Step 6 (03:00-03:30): DM-Azure sky haze A/B. Cause found (Mesa's fixed-function fog coordinate), fixed in our FF programs. Measured.

**The A/B** (one frame: `NETPLACE 9002 2629 -706 -16268 1500` = the first PlayerStart of DM-Azure, third person, time
stopped; 1280x720; AA off by `-ini:...:AntiAliasing=0` in both device sections; post-processing on, and off with
`UsePostProcess=False`; tool `tools/dev/f5-flicker.sh` (`F5_MAP=DM-Azure F5_PHASES=frozen`), numbers and sheet
`tools/dev/f5-haze.py`; pictures `logs/f5/haze/f5-haze-ab.png` (before), `f5-haze-ab-after.png`, numbers in the `.txt`
next to them. Sky box (590,10)-(740,230), far-mountain box (612,345)-(732,412); differences are 0-255, mean of R,G,B.)

| post-processing | pair | sky mean / max before | sky mean / max after |
|---|---|---|---|
| on | desktop on the GPU (NVIDIA) vs twin (GL4ES on Mesa) | 1.48 / 13 | 1.48 / 13 |
| on | desktop on Mesa llvmpipe vs twin | **60.72 / 99** | 2.20 / 23 |
| off | desktop GPU vs twin | 1.07 / 15 | 0.37 / 13 |
| off | desktop Mesa vs twin | **46.75 / 80** | 2.49 / 26 |

Sky mean colour, post-processing off: desktop GPU (102,105,111), twin (102,105,111), desktop Mesa before (140,153,165),
after (102,105,111). Far mountains: within 0.2-1.4 mean everywhere (the max 229 after is the crosshair, which sits in
that box in one run).

**Finding: the haze was never "desktop vs GL4ES".** On the owner's GPU the desktop device already showed the clouds
exactly as the GLES/web path does; only the desktop device on **Mesa** (llvmpipe here; the M11 measurements and
`logs/m11t/crop-azure-sky.png` were all made on Mesa, as is the wine oracle's `logs/oracle/DM-Azure.png`) fogged the
sky zone almost flat. Steps that located it: Mesa's texture-environment path (`-glnoffprograms`) is as hazy as our
fixed-function programs (identical pixels), so the difference comes from Mesa's fixed-function **vertex** stage, the
only common part; `GL_FOG_DISTANCE_MODE_NV` reads `GL_EYE_PLANE_ABSOLUTE_NV` on Mesa, and setting it explicitly or
dropping `GL_FOG_HINT GL_NICEST` changes nothing (experiments with throw-away binaries, not committed). So the fog
coordinate Mesa's vertex stage hands to the fragment stage (`gl_FogFragCoord`) is larger than |eye z| for these
draws (the sky zone: fog 230-1600, very large far triangles); why exactly, without Mesa's source here, is not known.
Mesa's hardware drivers (AMD radeonsi, Intel iris) share that fixed-function code, so Linux players with those GPUs
would have seen the haze too (inferred, not run).

**Which is right**: D3D's table fog (what Epic's PC driver uses on every card with `D3DPRASTERCAPS_FOGTABLE`,
`D3DRenderDevice.cpp` 503-511, 1319-1327; the Xbox sets `D3DFOG_LINEAR` table fog) and Epic's `Fog.vsh`
(`-R_CAMERASPACE_POSITION.z`) use the eye-space depth per pixel: the clouds visible, as NVIDIA, GL4ES and now Mesa
show. The wine oracle is not a witness here (wine turns D3D fog into GL fog on Mesa).

**Fix** (`Source/UC2GLDrv/Src/UC2GLFFProgram.cpp`, `FFBuildSource`, and the file header): the generated
fixed-function fragment program computes the fog distance itself: eye z from the fragment's window depth through the
projection (`ndc = (2 gl_FragCoord.z - near - far)/diff`, `z = (P[3][2] - ndc P[3][3]) / (ndc P[2][3] - P[2][2])`,
perspective and orthographic alike; GLSL 1.20 built-ins `gl_ProjectionMatrix`, `gl_DepthRange`), instead of reading
`gl_FogFragCoord`. Desktop GL only (GL4ES generates its own fog code from `vertex.z`, unchanged). The texture
environment path (`FixedFunctionPrograms=False`, a comparison switch) still uses GL's own fog. Numeric check: the
self-test `fog` (nine cases, CPU model of D3D linear fog on camera-space depth) runs in the gate below.

### Step 5 (03:35-04:00): shadow strength against the Xbox. Analysis + our reference pictures; the owner's xemu shot is requested.

**What Epic's code says, per kind of shadow** (read; the numbers marked "measured" were run):

1. *Player shadow texture* (the local player's silhouette; bots and other players get Epic's blob texture: `UCPawn.SetupPlayerShadow`
   sets `bBlobShadow` unless the pawn is locally and humanly controlled, `Characters.ini bBlobShadow=False`). The Xbox branch of
   `UShadowBitmapMaterial::Get` (M7b, R-15) runs: caster in `ShadowDarkness` grey (200, `ShadowBitmapMaterial.uc`) on 127,
   four horizontal taps, `1 - x`: core 0.216, neutral 0.502; measured against a CPU recomputation to 0.56/255 (`GLSHADOWCHECK`,
   projectors.md step 2). After the 2x modulate the floor keeps 43% of its light in the core (PC branch: 21%). So **our player
   shadow is half as dark as it was before M7b, by Epic's Xbox code**; that is the most likely meaning of "recover the
   shadow strength as it were" if the owner compares with earlier builds.
2. *Projection of dynamic projectors* (player shadows): on the Xbox, `FProjectorRenderBatchDef::Render` (`UnRenderBatch.cpp`
   340-410, `_XBOX`) draws them with the same `UProjectorMultiMaterial` as the static batches: per-vertex distance fade
   `1 - d/MaxTraceDistance` (`ProjectorAtten.vsh`) times `N . -D`, then `Proj_Modulate_1`: `lerp(0.5, t, atten)`. Ours
   (PC path, kept since M7b) blends with `GRADIENT_Fade` per pixel and the normal cube map. `GRADIENT_Fade` is 8x64, alpha
   5 / 129 / 0 at rows 0 / 31 / 63 (`GLTEXELS`, measured): a linear ramp from 255 just in front of the projector to 0 at
   `MaxTraceDistance` (row 0 is the guard texel behind it), so the two fades agree to first order; the Xbox's is per vertex
   (interpolated over the floor's triangles), ours per pixel. For a player shadow (`MaxTraceDistance` 150, projector at the
   root bone about 45 units above the floor) both give atten ~0.7, i.e. the core keeps `2 x lerp(0.5, 0.216, 0.7)` = 60% of
   the floor's light. No difference found that would make the Xbox darker.
3. *Static (map) projectors*, e.g. CTF-Aqueduct's tree shadows: Xbox batches since M7b (R-14), Epic's formula register for
   register. On CTF-Aqueduct the batch draws the leaf shadows weaker than the PC path did before M7b: mean |with - without
   projectors| over 12 elevated views (`logs/f5/fx-aq-survey*-gpu-*.png`, copied to `logs/f5/shadow/`) 3.9 (Xbox batch,
   default) against 5.9 (PC path, `-noxboxprojectors`), the PC path stronger in 11 of 12 views (measured). From the canyon
   floor (the reference view below) the two paths differ little: 5.67 vs 6.11, Xbox vs PC 1.11. Likely reasons, inferred:
   the Xbox's per-vertex fade and `N . -D` on large terrain/BSP triangles (a vertex beyond `MaxTraceDistance` is clamped to 0
   and drags the whole triangle down), against PC's per-pixel gradient and cube map.

**The xemu shots we have** (`<WorkUC2>/temp/`, looked at again): `AzureWaterDistortion.png`, `PraxisWindowPanesClose.png`,
`PraxisWindowPanesDistance.png` are the Xbox ones; the player's feet are at the bottom edge or on dark floors, no
shadow can be made out. Nothing to compare with.

**Request for the owner (xemu)**: CTF-Aqueduct, third person, standing still on the canyon floor under the trees, looking
east along the canyon with the camera a little above the pawn, as in our picture
`<WorkUC2>/logs/f5/shadow/aq-ref-xbox-frozen-00.png` (1280x960; our `NETPLACE LocalPlayer 4300 300 900 0 -3000`: the pawn
lands at (4300,300,-177); view yaw 0 = east, pitch -3000). Window: xemu's 640x480 output at 2x (1280x960 client area), any
time of day (the map has none), no other player near. What it settles: the darkness of the leaf shadows on the grass and
on the rock wall ahead (Xbox batch vs PC path) and, if the pawn's own shadow is visible at the feet, its darkness.
Our three versions of that frame: `aq-ref-xbox-frozen-00.png` (default: Xbox projectors + Xbox shadows),
`aq-ref-pc-frozen-00.png` (`-noxboxprojectors`, `XBOXSHADOWS 0`: before M7b), `aq-ref-none-frozen-00.png` (projectors
off); side by side `aq-shadow-ref-sheet.png`. Tool: `F5_RES=1280x960 F5_MAP=CTF-Aqueduct F5_VIEW=3p F5_PHASES=frozen
F5_PLACE="4300 300 900 0 -3000" tools/dev/f5-flicker.sh <name> <tree> 1`.

Player shadow on a bright floor (DM-Azure flagstones near the first PlayerStart, `logs/f5/shadow/down-x1/x0*`): with the
3p camera behind and above, the shadow of a standing pawn lies under it (light direction (0.2,0,1), `UCPawn.uc`) and is
hidden by the body; `XBOXSHADOWS 1` vs `0` differ only on the pawn and in PC's 1.6% darker square. A player shadow is only
comparable while jumping or from a side camera, which the owner cannot place on the Xbox: not requested.

### Gate, part 1 (03:40-04:20; machine load 20-35 from the other agents, game runs one at a time)

- Builds (all `bin/capped 6G`, `-j4`): `build-native` exit 0 (`logs/f5/build-native.log`), `build-windows` exit 0
  (`uc2.exe` 86,144,641 bytes, `logs/f5/build-windows.log`), `build-wasm-perf` exit 0 (`SDLLaunch.wasm` 14,931,655 bytes,
  `logs/f5/build-wasm-perf.log`). No ceiling hit.
- `tools/dev/m7-effects.sh f5g` (`logs/f5/gate-m7-effects.txt`): 14 effect lines, every one critical=0 glerrors=0
  programs_failed=0+0 left_behind=0; DM-Praxis A vs the wine reference 2.423 (O-9b recorded 2.58 on Mesa). The first
  self-test attempt did not run (the script writes its report before the log folder of a new tree exists: a missing
  `mkdir` in `m7-effects.sh`, not changed); rerun: `selftest summary: 15 passed, 0 failed`, `fog` 0.50 of 255 (the new
  fog distance of step 6 against the CPU model of D3D linear fog).
- Combiner test (`-glcombinertest`, as `o9-nvidia.sh`): Mesa 35 passed / 0 failed (fog and fog-front 0.50 of 255); the
  NVIDIA GPU 35 passed / 0 failed (`logs/f5g/ct-mesa.out`, `ct-gpu.out`).
- `sync-web-ini.py --check`: up to date (step 1; no ini changed since).
