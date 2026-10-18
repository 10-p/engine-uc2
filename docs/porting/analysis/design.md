Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# Design notes (2026-10-04)

Renderer, audio, porting-order, input and video design, grounded in the code. File:line references are to
`epic-sources` (identical content to `Source/`, CRLF line endings there).

### Renderer facts
- F1 PC look root causes: (a) `D3DMaterialState.cpp:1468-1507` draws any `IsXboxOnly()` material with
  `SetSimpleMaterial(stage 0)` — base `PixelShaderMaterial` returns 1 (`PixelShaderMaterial.uc:171`), also
  PSTeamSkinShader/PSEmissiveSkinShader; (b) PC `SetMatState_PS_Distortion` sets ONE/ZERO blend
  (`D3DPSMaterialState.cpp:342-457`); (c) `FPostProcessRender::Render` (`UnRender.cpp:3058-3210`) is `#ifdef
  _XBOX`, `UCCameraEffects.cpp` has 23 gates, `UnPixelShaderMaterial.cpp:247-337` gates distortion PreRender.
- F2 UC2 D3DDrv ⊇ UT2004 D3DDrv by function name (RenderInterface 36/36, MaterialState 26/26, RenderState
  15/15, RenderDevice 23/25, Resource 18/20); `D3DMaterialState.cpp` differs from UT2004 by ~274 changed lines
  + ~1.4k added. No new `D3DTOP_*` ops. UT2004 OpenGLDrv mirrors UT2004 D3DDrv (FOpenGLMaterialState ≈ FD3DMaterialState).
- F3 Push buffers, DrawPrimitiveUP, fences, real occlusion queries are `_XBOX`-only at every call site →
  leave at defaults. PC occlusion stubs never write `dwPixels` (UB, `D3DRenderInterface.cpp:3713-3777`) → fix.
- F4 Never define `XBOX_ASSET_FORMAT` (changes serialized layouts: `UnObj.h:111,263`, `UnModel.h:26-415`,
  `UnSkeletalMesh.h:113`) → CPU skinning stays, as on PC.
- UC2-only driver work: RenderInterface +44 (post-process set, occlusion, dynamic gamma/histogram, viewport
  helpers, material vertex shaders); MaterialState +9 (`SetPixelShaderMaterial`, `HandlePSMaterialState`,
  `HandlePSVertexShader`, `SetMovieMaterial`, `SetMultiPassMaterial`, `SetProjectorMultiMaterial`,
  `HandleBumpMaterial`, `HandleDiffuseTFactor_Stage`, `UpdateDOFRampLookupTexture`) + `D3DPSMaterialState.cpp`
  (986 lines, portable state logic); Resource +17 (combined vertex shaders, default RT, `_WORD` cache ids);
  RenderState +7. Interface additions: `UnRenDev.h:579-729` (render targets, viewport, colour write mask,
  QWORD VertexShaderEnum, post-process stages `PP_STAGE_*`, back-buffer copy, CopyDepthBuffer, dynamic gamma).
- Shader census (.psh): 291 tex, 147 mul, 68 xmma, 60 xfc, 44 mad, 38 add, 19 lrp, 18 mov, few
  texm3x2*/texreg2gb/texkill/dp3/xdd; 32 DualDiffuse variants collapse to one parametrised source.
  .vsh ops: mov/mul/dp4/dp3/mad/m4x3/m3x3/m4x4/rsq/max/slt. Shader language: GLSL 1.20 written
  ESSL-1.00-compatible (explicit attributes/uniforms); ARB_fp is the fallback.
- GL4ES/WebGL limits to design around: dependent reads need the programmable path; depth texture only with
  `WEBGL_depth_texture` (fallback: linear-depth pass); occlusion queries are faked by gl4es; no hardware gamma
  (do it in the present shader); fragment texture LOD bias approximated. UNVERIFIED: fragment-only GLSL with
  fixed-function vertex stage in gl4es → the M5 spike.
- Oracles: wine + stock d3d8 with avaris.exe (PC look, expected to match R1); xemu (Xbox look; needs BIOS
  files); `.psh` CPU interpreter (numeric, headless).

### Audio design
- Interface `UnAudio.h:58-111`: PlaySoundCue/StopSoundCue(s)/GetSoundCueDuration/GetSoundCueRadius,
  Get/SetCategoryVolume, PlayMusic/StopMusic/StopAllMusic, Notify{Destroy,LevelChange,ZoneChange,PauseChange}.
- Behaviour to reproduce from `XACTAudioSubsystem.cpp`: 23 persistent banks (:213-235); bank lookup
  `<Bank>_<lang>` then `<Bank>` (:526-627); cue name `"Bank.Group.Cue"` → `Bank.Cue` (:800-856); 2D when source
  is viewer/view-target-owned, distance cull `radius < dist*(1+cues/15)`, per-viewport relative position
  (:2133-2370); radius `2*MinDist + MinDist*(2/Rolloff)` (:2609-2640); categories Music/FX/Voice (:2686-2718);
  music = `M_<Song>` (:1777-1886) → cooked from `Music/*.wav`; zone reverb `EZoneAudioEffect` → I3DL2 presets
  (:1169-1266) → OpenAL EFX presets; per-sound "I3DL2 Level" → aux send gain. DSP image never required.
- `.xap` grammar: `Wave Bank{Entry{Name,File,Channels,Sampling Rate,Loop Region}}`; `Sound Group`, `Sound
  Category`, `Sound Layer` (~:131596-131687); `Sound Bank{Sound{Priority,Volume,Category,Group,3D{Min/Max
  Distance,Rolloff,Doppler,cones},Track{Playlist Type,Pitch Low/High,Volume Low/High,Variable Delay,Loop
  Count,Registered Wave}},Cue{Name,Sound Index}}`. To verify: volume units (1/100 dB), pitch (cents), playlist
  type 4 semantics, priority direction, layers. ~23 WAVs missing → decode those entries from `.xwb` offline.
- Voice pool with priority stealing (~32 on web); pause all but music on `NotifyPauseChange`.

### Porting-order notes
- Replay list: `git -C engine-ut2004 diff -w cfc7e09 HEAD -- Source/Core Source/Engine` = 44 files, +2733/−115
  (≈470 lines Karma/CDKey not applicable). Mark each ported / n-a / already right.
- Static link scheme identical to UT2004 (`UnObjBas.h:404/470/546`); Editor/UnrealEd/UnrealUtil may lack native
  tables (unverified). Threading: `UnThreadingBase.h` (765) is Windows-only-implemented; users are `UnMisc.cpp`
  and `UnMaterial.cpp` → small `UnThreadingPosix.cpp`.
- PC paths already exist for: `Engine.LoadSaveManager` (`ULoadSaveManager.cpp`, 0 `_XBOX`), `UnSplitScreenMgr.cpp`,
  `UnPlayerProfile.cpp`; `FContentManager.cpp`/`UnPlayerStats.cpp` have PC branches. Hide Xbox Live UI, not code.
- ScriptPatcher: `UnGame.cpp:2448-2462`, `UnScriptPatcher.cpp:266` (30 patches, 21 functions) — keep until M14.

### Input notes
- Pad mapping spec `XboxViewport.cpp:12-38, 440-447`: A/B/X/Y→IK_Joy1..4, triggers thresholded→IK_Joy7/8,
  D-pad→9..12, Start/Back→13/14, stick clicks→15/16, left stick→IK_JoyX/Y (240/241), right→IK_JoyU/V,
  hotplug→IK_JoySlider1/2. Black/White→RB/LB (convention, verify vs KeyBinds.ini).
- GUI keyboard emulation exists (`bEmulatedJoypad`, `UnGUICore.cpp:858-889`, codes `GUIPrivate.h:27-50`) but maps
  letter keys A/B/X/Y/K/W → collides with WASD/text entry → make it ini data. Mouse cursor exists
  (`UnGUICore.cpp:669-706`, :1608); synthesise A/B on click in `NativeKeyEvent` (:893-985).

### Video notes
- All XMV: WMV2 640×480@30; cinematics carry 5 Xbox-ADPCM audio tracks (one per language); MovieMaterial
  clips 256×256 video-only. Plug-in points: `FMoviePlayer`/`FMovieThread` (move out of `UnVcWin32.h:793-806`),
  `FMovieTexture` (`UnMaterial.cpp:1096-1140`, streaming :1213-1240), `SetMovieMaterial`
  (`D3DMaterialState.cpp:4539+`), cinematic hooks `UnGame.cpp:1743-1805`, startup `XboxLaunch/Src/Launch.cpp:402`.

### Open items carried into execution (decide when reached, with the owner where noted)
- One GL driver source built twice vs two mirrored dirs (decide at M5). Per-bank packed OGG vs per-wave (M8).
- Config base: confirm the ISO's 19 differing System files are just the Xbox ini variants renamed (M0 diff).
- Owner: split-screen on PC in scope? web multiplayer visible at launch? which arm64 hardware (M13)?
