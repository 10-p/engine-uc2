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
