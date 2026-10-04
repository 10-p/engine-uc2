# Render targets and post-processing (M7, renderer R3): notebook

Branch `m7-postprocess`, worktree `../engine-uc2-m7`, run tree `run/m7`, pictures and logs `<WorkUC2>/logs/m7`.
Scope: PLAN.md row M7. Steps P1 (driver), P2 (engine gates behind a capability), P3 (the 25 waiting shaders
and the camera effects; projector shaders; `PS_Terrain3Layer`), P4 (ini switch), P5 (verification on content).
Every step is appended here when it is committed. Everything that runs goes through `<WorkUC2>/bin/capped`
(game and `ucc-bin`: 2G; builds: 6G; the wine reference script: 3G).

The specification is Epic's Xbox code, which is in the tree and never built: `Source/D3DDrv/Src`
(`D3DRenderInterface.cpp`, `D3DMaterialState.cpp`, `D3DPSMaterialState.cpp`, `D3DResource.cpp`), the `_XBOX`
branches of `Source/Engine/Src/UnRender.cpp`, `UnPixelShaderMaterial.cpp`, `Source/UCGameplay/Src/
UCCameraEffects.cpp`, and the shader sources in `Source/ShaderData`. There is no Xbox emulator here and Epic's PC
build never had these effects: **no reference picture exists for any "on" picture in this notebook.**

## P1. The driver: render targets as shader inputs

New file `Source/UC2GLDrv/Src/UC2GLPostProcess.cpp`; hooks in `UC2GLRenderInterface.cpp` (`DrawPrimitive`,
`SetMaterialBlending`, `Clear`, `GetGLMatrices`), `UC2GLShaders.cpp` (three vertex-shader fragments),
`UC2GLMaterialState.cpp` (`HandlePSVertexShader`, `UpdateDOFRampLookupTexture`), `UC2GLPSMaterialState.cpp`
(`SetMatState_PS_Distortion`), `UC2GLResource.cpp`, `UC2GLRenderDevice.cpp`.

### What the Xbox does that GL cannot, and what stands in for it

| Xbox (D3DDrv, `_XBOX`) | Here |
|---|---|
| The back buffer is a texture and is sampled **while it is the render target** (distortion quad, radial blur, colour remap, screen blur: `SetTextureInterface( RI->GetRenderTarget() )`, `GetBackBufferMaterial()`) | GL may not sample the framebuffer it draws into. A stage that samples the default render target binds a **snapshot** of it (`UUC2GLRenderDevice::GetSampledTexture`); `FOpenGLRenderInterface::PrepareSampledTargets` refreshes the snapshot before the draw if anything was drawn into the main framebuffer since it was taken (`MainColorVersion`, bumped by every draw and clear). One full-screen copy per draw call that reads the back buffer after it changed. |
| The depth buffer is a texture: `FDefaultRenderTarget::DepthBufferTexture`, a `D3DFMT_LIN_B8G8R8A8` header over the D24S8 memory, selected by `RT_MATERIAL_USE_DEPTHBUFFER`; `DOF.psh` reads green and blue (`texreg2gb`) | The main framebuffer's depth/stencil attachment is a **texture** now (was a renderbuffer at M5/M6). When a stage samples the default target in depth usage, a copy pass re-encodes it into an RGBA8 texture with the bytes that view had: blue = depth bits 23..16, green = 15..8, red = 7..0 (`GPostDepthEncodeFragmentShader`). Alpha (the stencil byte on Xbox) is 0; no shader of the game reads it. |
| `CopyDepthBuffer`: re-types the destination's depth buffer as a colour surface and the source's as a colour texture and draws a point-sampled quad (`PS_DepthSwizzleColor`) | The same quad with a shader that writes the sampled depth as `gl_FragDepth`, colour writes off. Source must be the default target (only its depth is a texture, as on Xbox); stencil is not copied. `PS_DepthSwizzleColor` itself is therefore never drawn here. |
| Linear (non-swizzled) render-target textures are addressed in texels; the engine passes 0..1 coordinates and the driver scales them (`IsTextureLinear`) | GL textures are addressed 0..1: nothing to scale. The one place that works in pixels, `Distortion.vsh`, is ported to 0..1 (below). |
| `UpdateBackBufferCopyTarget`, `GetBackBufferCopyTarget`, `GetBackBufferCopyMaterial`, `GetBackBufferMaterial`, `FD3DDynamicGamma`, `UpdateDOFRampLookupTexture` | Epic's bodies, carried over with their comments; changes marked `UC2GLDrv:`. They draw through the engine's own materials (`UPSBlurMaterial`, `PS_LuminanceTexIndex`), so they exercise the same path as the game's effects. |
| The render-target pool | `FRenderTargetPool` is engine code and was never gated; the interface owns one (`GetRenderTargetPool`), as at M5. |

GL objects added: three small programs (copy, depth encode, depth copy; GLSL 1.20, no built-ins except
`gl_FragDepth`), the snapshot and the depth-colour texture with their framebuffers (main framebuffer size,
created on first use), a 256x1 ramp texture for the dynamic gamma. No new GL entry point except `glUniform1f`.

### The coordinate conventions (also at the top of `UC2GLPostProcess.cpp`)

1. **Every texture a material can sample is stored top row first**, as Direct3D stores it (v = 0 is the top of
   the picture). Render-target textures are that way because M5's interface draws into them upside down in GL
   window space (`FOpenGLRenderTargetState::Flipped`: flipped projection, mirrored culling, D3D viewport
   rectangles used as they are). So the engine's texture coordinates are used unchanged everywhere.
2. **The main framebuffer is the one exception**: it is drawn the GL way (bottom row first) so the present pass
   shows it straight. It is never bound as a texture; the snapshot and the depth colour are made by a copy pass
   that flips (`FlipRect` in `UpdateSampledTexture`; `CopyDepthBuffer` flips the same way). This is the only
   place where a vertical flip is decided.
3. **Depth**: GL window depth equals D3D z (M5's clip fix-up maps z to 2z-w, depth range 0..1), so the 24-bit
   value in the depth texture is the value the Xbox ramp formula expects (`dwUV / D3DZ_MAX_D24S8`).
4. **Half-texel offset** (corrected after the self-test; my first reading was wrong). Direct3D 8 puts pixel
   centres on integer coordinates, so a full-target quad at 0..size with coordinates 0..1 would read a
   same-size texture on texel *boundaries*. Epic's screen tiles do not: `FScreenPrimUtil`'s canvas matrix moves
   every tile half a pixel up and left (`UnRenderUtil.h`, the `- 0.5f`), so destination pixel (x,y) samples at
   ((x+0.5)/w, (y+0.5)/h): texel centres for a same-size copy, the shared corner of 2x2 texels for a half-size
   one. M5's clip fix-up makes GL rasterise as D3D does, so the same holds here with no further correction.
   Checked numerically (self-test `blur-h`, `blur-v`, `blur-box`, `bb-copy`; with the half pixel left out of
   the expected values the same tests are off by 70 to 120 of 255). A **point-filtered** read exactly on a
   texel boundary is a tie; hardware takes the texel to the right/below, GL leaves it to rounding noise. For a
   pass with a point-filtered render-target stage the interface samples 1/32 pixel further right and down
   (`PointSampleNudge` in `GetGLMatrices`), and `CopyDepthBuffer` adds a quarter source texel for the same
   reason (a half-size target pixel (x,y) gets the depth of back-buffer pixel (2x+1, 2y+1): the tie of Epic's
   quad resolved the hardware way; **that NV2A resolves it this way is my assumption**). Passes without such
   a stage are bit-identical to M6.
5. **Distortion** computes "this pixel in the back buffer" in its vertex shader. The Xbox fragment takes the
   screen position after its own viewport transform, minus 0.5 (Xbox pixel centres sit at +0.53); the port
   takes GL's window coordinates divided by the framebuffer size (`u_distortionScreen`), which lands exactly on
   texel centres of the snapshot whatever the half-pixel conventions are. With zero distortion the read is the
   identity.

### Vertex-shader fragments added (`UC2GLShaders.cpp`, `BuildVertexShaderSource`)

| `.vsh` | What the GLSL does | Skips (as `FD3DCombinedVertexShader`) |
|---|---|---|
| `Distortion.vsh` (`VS_Distortion`) | `gl_Position /= abs(w)` (the Xbox sets `oPos.w` to +-1: everything interpolates linearly on screen; the point stays the same); `oT1 = (scale*alpha, 0, back-buffer u)`, `oT2 = (0, scale*alpha, back-buffer v)` | lighting, stages 1 and 2, the screen-space fragment |
| `DistortionAccum.vsh` (`VS_DistortionAccum`) | `oT2.xy = scale * alpha * (1 - sat(z/5000)^2)` | lighting, stage 2 |
| `ColorRemap.vsh` (`VS_ColorRemap`) | `oT1 = oT2 = (0.30, 0.59, 0.11, 1)` | lighting, colour output, stages 1 and 2 |

`HandlePSVertexShader` (Epic's `_XBOX` body) now runs when post-processing is on; its `VSC_*` constants go to
`FOpenGLRenderInterface::DistortionScale` / `ColorRemapConstant` and from there to uniforms.

**Decision to review: distortion amounts scale with the resolution.** On Xbox `DistortionAmount` /
`MaxDistortion` are pixels of a 640x480 back buffer. The port multiplies them by `height / 480` so the effect
covers the same fraction of the picture at any resolution (`PixelScale` in `BeginProgramPass`). At 640x480 it is
Epic's number.

`MaxDistortion` is read from the client section of the ini. PC's `Default.ini` has none, which reads as 0 and
would switch distortion off (and divide by zero in the accumulation pass): the driver then uses 20, Epic's
value for "no client". P4 adds the Xbox value (35) to our client section.

### Dynamic gamma

`FOpenGLDynamicGamma` = `FD3DDynamicGamma`: every 0.05 s the back buffer is drawn into a 16x16 target through
`PS_LuminanceTexIndex`, read back (`glReadPixels`; the Xbox reads the texture memory), turned into a
histogram, and a 256-entry ramp is computed from its 10% / 90% points, `LevelGamma` and `LevelContrast`. D3D's
`SetGammaRamp` is the present pass here: `GPresentFragmentShader` looks each channel up in a 256x1 texture
when a dynamic ramp is active. The static ramp (`UpdateGamma`) replaces it, as `SetGammaRamp` would.
`ShowDebug` (the histogram overlay, `bDebugShowGamma`) is not ported.

### Defects found in Epic's code (P1)

1. `SetMatState_PS_Distortion` writes `InShader->PixelShaderType = NULL` in its "don't render anything"
   branches. That changes the *material object* for good: on every later use its vertex shader and state
   handler are chosen by that value, so the material would be drawn as a fixed-function one from then on.
   **Not carried over** (the colour write mask alone draws nothing); the only case in P1 where Epic's
   behaviour is not kept.
2. `FRenderTargetPool::~FRenderTargetPool` asserts that every reference is gone; the camera effects release
   theirs when their objects are destroyed, which at exit happens in no particular order relative to the
   render device. On Xbox the game never exits. The device releases its own reference in `Exit`; see P5 for
   what a clean exit does.
3. `UClient::DynamicGamma` reads True on our build although nothing in the PC ini or code sets it
   (`Avaris.ini` of M6 runs has `DynamicGamma=True` too; `UClient::Init` sets it under `_XBOX` only). Not yet
   understood; see P3/P5.

### Evidence (P1 alone, engine gates not lifted yet)

```
bin/capped 6G 1500 -- cmake --build build-native -j12                 -> [100%] Built target UCC, exit 0
tools/dev/mkrun-uc2.sh build-native m7
UC2_RES=1280x720 UC2_PRECMD="set UCDeathmatch NumPlayers 0" tools/dev/run-uc2-shot.sh m7 p1-deadbolt 100 34 DM-Deadbolt -glerrors -glshot=42,52 -glpptoggle=47
  0 Critical; frame 2400, 0 GL errors so far; programs: 6 linked, 0 failed
  pixel shaders on: 84 fragment shaders compiled, 0 failed (59 after the toggle: "25 need post-processing, which is off")
```

`p1-deadbolt-0.png` (on) and `-1.png` (off, same run): the frame is the M6 frame; the only differences between
the two, and to M6's `coord-deadbolt-0.png`, are inside the box x 58-756, y 48-720 (the animated character,
the spinning pickup, the HUD), which also differs between two M6 runs. Only the per-frame back-buffer copy and
the luminance pass run at this point (programs `Blur`, `LuminanceTexIndex`).

## P2. The engine and game gates, behind one capability

**The capability**: `FRenderCaps::XboxPostProcess` (`Source/Engine/Inc/UnRenDev.h`; `FRenderCaps` is a plain C++
struct, not mirrored in script), default 0, set by `UUC2GLRenderDevice::GetRenderCaps` when
`PostProcessActive()` (ini switch on, pixel shaders on, copy programs built). The engine and the game ask
through one function, `ENGINE_API UBOOL RenDevHasXboxPostProcess()` (`UnRenderResource.cpp`): false without a
client or a render device (dedicated server, commandlets), for `Engine.NullRenderDevice` (its caps are the
struct's defaults) and for any device that leaves the field unset. `_XBOX` is not defined anywhere; both
branches of every lifted gate are kept; the Xbox branch is the body that runs with the capability.

Design F1 names three places; reading the code there are five kinds of site. All of them:

| # | File, function | Epic's gate | Now | Without the capability (null device, `UsePostProcess=False`, any other device) |
|---|---|---|---|---|
| 1 | `Engine/Inc/EngineClasses.h` + `Engine/Classes/PSDistortionBaseMaterial.uc` (cpptext), `UPSDistortionBaseMaterial::IsPostProcess`, `RequiresSorting` | `#ifdef _XBOX` inline: `RequiresSorting() ? BLEND : NOBLEND`; `#else` inline: `POSTPROCESS_NONE`, sorting 1 ("added so that something would be drawn on the PC") | the `#else` branch declares the two virtuals; `UnPixelShaderMaterial.cpp` defines them: with the capability the Xbox answers (`Super::RequiresSorting()`), otherwise PC's | `POSTPROCESS_NONE` and 1, as before: distortion materials are sorted as translucent and never enter the post-process list |
| 2 | `Engine/Src/UnRender.cpp`, `FPostProcessRender::Render` | body after the `PPDrawList` test in `#ifdef _XBOX` | `#ifndef _XBOX  if( !RenDevHasXboxPostProcess() ) return;  #endif`, then the body | returns; and the list is empty anyway (site 1), so it returned at the test above, as before |
| 3 | `Engine/Src/UnPixelShaderMaterial.cpp`: `UPSDistortionMaterial::PreRender`, `UPSBlendedDistortionMaterial::PreRender`, `UPSDistortionBaseMaterial::PreRender`, `UPSBackBufferEffect::PreRender`, `UPSPlayerCamMaterial::PreRender`, `UPSColorRemapMaterial::PreRender` | `#ifdef _XBOX` ... `#else` ... `#endif` | `if( RenDevHasXboxPostProcess() ) { Xbox branch } else { PC branch }` | the PC branch, text unchanged |
| 4 | `UCGameplay/Src/UCCameraEffects.cpp`, 16 functions (`UUCScreenBlur::PostRender`; `PreRender` and `PostRender` of `UUCScreenDOF`, `MotionBlur`, `HeatShimmer`, `RadialBlur`, `Bloom`, `Emissive`; `PostRender` of `UUCScreenBerserk`, `MenuPause`, `Liandri`) | `#ifndef _XBOX  return;  #endif` | `#ifndef _XBOX  if( !RenDevHasXboxPostProcess() ) return;  #endif` | returns at the same place |
| 5 | `UCCameraEffects.cpp`, `UUCScreenSniperOverlay::PreRender/PostRender`, `UUCScreenTeleportOverlay::PreRender/PostRender` | whole body in `#ifdef _XBOX` | `if( RenDevHasXboxPostProcess() ) { body }` (the two `PostRender`s also want their overlays to exist) | body skipped, as before |
| 6 | `UCCameraEffects.cpp`, `UUCScreenDOF::UUCScreenDOF`, `Destroy` (+ `Inc/UCCameraEffects.h`: one non-virtual member function declared) | constructor and destructor bodies in `#ifdef _XBOX` (the shared `UPSDepthOfFieldMaterial` and its reference count) | the constructor body is `InitSharedDOFResources()`, called from the constructor if the capability is there at that moment and otherwise from `PreRender` on first use; `Destroy` releases only if this object took a reference (`PixelShaderDOFMaterial != NULL`) | nothing allocated; the only difference to before: the constructor writes `PixelShaderDOFMaterial = NULL` (it was the property default, None) |
| 7 | `UCCameraEffects.cpp`, `UUCScreenDOF::PostRender` | (none; `RI->InsertFence()` stays `_XBOX`) | the review switch for the blur alpha, see P3 | not reached (site 4) |

Not gates of the engine, and unchanged: `FLevelSceneNode::Render` calls `PreRender`/`PostRender` of the camera
effects, `RI->UpdateBackBufferCopyTarget()` and `RI->UpdateDynamicGamma()` on every platform already
(`UnRender.cpp` 2140-2225); on PC it was the D3D driver that returned early. `UPSRefractionMaterial::PreRender`,
`FilterCopyRenderTarget`, `FRenderTargetPool`, `FAuxRenderTarget` were never gated. `UClient::Init`'s
`#ifdef _XBOX DynamicGamma = 1; LuminosityBias = 1.0f;` is left alone (the device does not exist yet at that
point); the driver treats a `LuminosityBias` of 0 as 1 (P3).

Class layouts: no member added to any script-mirrored class (`verifyclasses` in the gate). `EngineClasses.h`
and the `.uc` cpptext were changed together.

**Switching at run time** (`POSTPROCESS [0|1]`, `-glpptoggle=`): the engine caches per actor which draw list it
is on (`FDynamicActor::UpdateSorting`, `UParticleEmitter::HasPostProcessMaterial`), from `IsPostProcess()`.
`UUC2GLRenderDevice::SetPostProcess` therefore sets `bSkinsChanged` on every actor and clears the emitters'
cached value. The materials' `TextureStageN` are written by `PreRender` in both modes, but the PC branch of
the distortion classes writes only stage 1 (it relies on the serialized stage 0), so **switching from on to
off in one run does not restore the exact M6 state of a distortion material; off to on is clean**. A/B pictures
are therefore taken with the run starting off (`-nopostprocess -glshot=a,b -glpptoggle=t`, a < t < b).

### Evidence (P1 + P2)

```
UC2_RES=1280x720 UC2_PRECMD="set UCDeathmatch NumPlayers 0" tools/dev/run-uc2-shot.sh m7 p2-deadbolt 100 34 DM-Deadbolt -glerrors -nopostprocess -glshot=42,52 -glpptoggle=47
  0 Critical; frame 1800, 0 GL errors so far; programs: 14 linked, 0 failed; fragment shaders 84 compiled, 0 failed
  programs linked after the switch: Blur, DOF, MotionBlur, BloomSelect, EmissiveSelectBlur, LuminanceTexIndex, ColorRemap
```

- `p2-deadbolt-0.png` (off) against M6's `logs/m6/coord-deadbolt-0.png`: **0 differing pixels for x >= 760**
  (the static half of the frame); the differences (5.3% of the frame) are all inside x 58-743, y 48-720, the
  animated character, the spinning pickup and the HUD, which differ between any two runs.
- `p2-deadbolt-1.png` (on, same run and camera): the lamps glow. `p2-deadbolt-lamp-off-on.png` (2x crop, off
  left, on right): the four green lamps under the poster and the fluorescent tube have a soft halo that is
  not there with post-processing off; nothing is displaced, mirrored or lagging. `p2-deadbolt-diff-x6.png`
  (difference x6): glow around every emissive element (lamps, the bright parts of the poster, the light strip
  along the floor edge) and the animation of the character. The always-on depth of field is active too (program
  `DOF`); in this small room its effect is a slight softening of the far wall.

## Checkpoint, 2026-10-04 21:30 (written after an interruption; everything known so far, verified or not)

Commits so far: `47d3b6d` P1, `94654e2` P2, `4be1bb8` Xbox client defaults + verification switches, `6fa7758`
`FScreenPrimUtil` fix + P4 config, `dd3f362` pool at exit + run script. Web build (`cmake --preset wasm &&
cmake --build build-wasm -j6`, capped 6G) of the tree at `6fa7758`: `[100%] Built target SDLLaunch`, exit 0.
`python3 tools/psh2glsl.py --selftest`: `100 shaders, 99 compared ... 0 failed`; `--check-emitted`: `0 differ`.

### P3 as it stands: which of the 25 waiting shaders are drawn, by which handler

With `UsePostProcess` every one of the 25 is compiled and its material takes the Xbox branch of
`SetPixelShaderMaterial` (`UC2GL_IsM7PixelShader` only applies with post-processing off). The state handlers
are M5's line-for-line port of `D3DPSMaterialState.cpp`; the only one with an `_XBOX` branch,
`SetMatState_PS_Distortion`, now takes it at run time. What has actually been drawn (a program linked in a
run) and what has not:

| Shader(s) | Handler | Driven by | Seen linked in a run | Status at this checkpoint |
|---|---|---|---|---|
| `PS_Blur` | `SetMatState_PS_Blur` | `UpdateBackBufferCopyTarget` (every frame), `FilterCopyRenderTarget` (bloom, emissive, DOF blur chains), `UUCScreenBlur` (console `BlurMe`) | yes | drawn; picture pre-fix only |
| `PS_EmissiveSelectBlur` | `PS_Blur` handler with `BLUR_EMISSIVE` | `UUCScreenEmissive::PostRender` | yes | drawn (lamp glow on DM-Deadbolt) |
| `PS_BloomSelect` | `SetMatState_PS_BloomSelect` | `UUCScreenBloom::PostRender` | yes | drawn |
| `PS_DepthOfField` | `SetMatState_PS_DepthOfField` | `UUCScreenDOF::PostRender` (the always-on overlay) | yes | drawn; effect small in the pictures taken, not yet isolated |
| `PS_MotionBlur` | `SetMatState_PS_MotionBlur` | `UUCScreenRadialBlur::PostRender` (speed blur, teleport, sniper) | yes | linked at spawn (teleport-in); no picture shows it yet |
| `PS_ColorRemap` | `SetMatState_PS_ColorRemap` + `VS_ColorRemap` | `UUCScreenBerserk`, `MenuPause`, `Teleport` | yes | drawn (berserk: green ramp); pause ramp gives black, see doubts |
| `PS_ColorRemapMasked` | same | `UUCScreenSniperOverlay` (needs the sniper rifle zoomed), `PSColorRemapMaterial` on the sniper rifle skin | no | not yet observed |
| `PS_ColorRemapGrayScale` | `SetMatState_PS_ColorRemapGrayScale` | `UUCScreenLiandri` | yes | drawn (grey picture with scan-line noise) |
| `PS_LuminanceTexIndex` | `SetMatState_PS_LuminanceTexIndex` | dynamic gamma (every 0.05 s) | yes | drawn; its output is only read back |
| `PS_Distortion`, `PS_DistortionAccum` | `SetMatState_PS_Distortion` + `VS_Distortion`, `VS_DistortionAccum` | `FPostProcessRender::Render` | yes | drawn (DM-Praxis panes) |
| `PS_ModulatedDistortion`, `PS_AdditiveDistortion`, `PS_SubtractiveDistortion` | same handler, `PP_STAGE_RENDER_COLOR` | blended distortion materials (icicles: DM-Decay; combo and weapon effects in `T_FX`) | no | not yet observed |
| `PS_Mask` | `SetMatState_PS_Mask` | `UUCScreenMotionBlur` with a `FinalMask`; no script creates a `UCScreenMotionBlur` | no | handler ported, no trigger found |
| `PS_PlayerCam`, `PlayerCamNoise`, `PlayerCamTwoNoise` | `SetMatState_PS_PlayerCam` | `PSPlayerCamMaterial` (CTF-Recoil; two static-mesh packages) | no | not yet observed |
| `PS_MaskedLuminance` | `SetMatState_PS_MaskedLuminance` | `PSBackBufferEffect` (one object, in `T_FX`) | no | not yet observed |
| `PS_Refraction` | `SetMatState_PS_Refraction` | `PSRefractionMaterial`: no object in any package | no | unreachable from content |
| `PS_UseTexIndex`, `PS_UseTexIndexMasked` | `SetMatState_PS_UseTexIndex` | `PSUseTexIndexMaterial`: no object in any package, no code creates one | no | unreachable from content |
| `PS_EmissiveSelect` | `SetMatState_PS_EmissiveSelect` | only an `#if 0` block of `UUCScreenEmissive::PostRender` | no | unreachable (Epic compiled it out) |
| `PS_ShadowBlur` | `PS_Blur` handler with `BLUR_SHADOW` | the `_XBOX` branch of `UnShadowProjector.cpp` (shadow textures blurred by a pixel shader; PC blurs them with nine canvas tiles) | no | **gate not lifted**: it would change every shadow and is not post-processing; hook: `UnShadowProjector.cpp` ~553-680 |
| `PS_DepthSwizzleColor` | `SetMatState_PS_DepthSwizzleColor` | only `CopyDepthBuffer` on Xbox | no | not used: the depth copy has its own shader (P1) |

### Verification switches added to the device (all in `UC2GLRenderDevice.cpp`)

- `-nopostprocess` / `-postprocess`, console `POSTPROCESS [0|1]`, `-glpptoggle=<s>,...` (flip at those times
  after the first frame). A/B from one camera in one run: `-nopostprocess -glshot=a,b -glpptoggle=t`, a < t < b:
  picture 0 = off (clean M6 state), picture 1 = on.
- `-glexec=<s>:<command>[@<s>:<command>...]`: console commands through the viewport at those times; `+` stands
  for a space. Log line `UC2GLDrv: -glexec at <s> s (frame N): <command>`.
- `-glshotramp`: screenshots include the dynamic gamma ramp (they are read before the present pass otherwise).
- console `DOFSHIPPEDALPHA [0|1]`, ini `DOFShippedBlurAlpha`.

How each effect is triggered from the console (bindable or through `-glexec`):

| Effect | Command(s) | Notes |
|---|---|---|
| emissive glow + bloom, always-on depth of field | none: `UCPlayerController.CreateOverlays` enables both for every player | |
| screen blur | `BlurMe` (`UCPixelShaderEffects.MotionBlurs.ConcussionBlur`, 5 s); `BlurMe2` (0.2 s) | the timer only runs while the effect draws |
| berserk colour remap | `set UCScreenBerserk Enabled 1` | fades in over 1.25 s; pawns are excluded by stencil 0xBE |
| Liandri overlay (grey + noise) | `set UCScreenLiandri Enabled 1` | fades in over 0.3 s |
| pause-menu remap | `set UCScreenMenuPause Enabled 1` | |
| radial (speed) blur | `set UCScreenRadialBlur Enabled 1` alone shows nothing: `UCPlayerController.PlayerTick` drives `RadialBlurDistance` to 0 for a standing pawn. To hold it: `set UCPlayerController RadialBlurSpeed 0`, `set UCPlayerController RadialBlurMinChange -1`, `set UCScreenRadialBlur RadialBlurDistance 30`, then `Enabled 1` | not yet run |
| dynamic gamma | on with post-processing; `DYNAMICGAMMA 0` / `1` | visible in screenshots only with `-glshotramp` |
| depth-of-field alpha, shipped vs script | `DOFSHIPPEDALPHA 1` / `0` | third person: 65 vs 100 |
| distortion | content: DM-Praxis panes (unpinned spawn), CTF-Octane glass, DM-Azure water, torches (`UCEffects`), invisibility pickup | |

### Pictures so far (`<WorkUC2>/logs/m7`), all 1280x720, `-0` = off, `-1` = on unless said otherwise

- `p1-deadbolt-*`, `p2-deadbolt-*`: see P1/P2.
- `praxis-try1-0/1.png`, `praxis-try1-panes-off-on.png`: DM-Praxis, unpinned spawn, looking through a doorway
  at the round room. Off: the three window panes are flat periwinkle (128,128,255: their distortion texture
  drawn as a colour, the PC bug of `logs/oracle/DM-Praxis-windows-A.png`). On: the panes are transparent and
  the underwater exterior is seen through them (sea floor with rocks at the bottom, light from above:
  upright). In this run `MaxDistortion` was still 0, so nothing was displaced.
- `praxis-try2-0/1/2.png`, `praxis-try2-panes.png`: the spawn of the wine picture `DM-Praxis-windows-A.png`
  (same room, same camera). 0 = off, 1 = on with the dynamic gamma ramp applied to the screenshot
  (`-glshotramp`), 2 = on after `DYNAMICGAMMA 0`. Panes as in try1. Picture 1 is visibly brighter than 2.
- `praxis-try3-0/1.png`, `praxis-try3-panes.png` (try2 without displacement left, try3 with it right): with
  `MaxDistortion` 35 the view through the panes is shifted and wobbled; at the right edge of the third pane
  dark shapes of the pillar in front of the window are pulled into the pane. Screen-space distortion reads
  whatever is at the displaced position, foreground included, and `DistortionAccum.psh` only ever displaces
  towards the lower right; **I believe the Xbox shows the same, I cannot check it.**
- `fx-blur`, `fx-berserk`, `fx-radial`, `fx-liandri`, `fx-pause` (`-0` off, `-1` on): **taken before the
  `FScreenPrimUtil` fix (`6fa7758`) and therefore all wrong in the same way**: every full-screen effect covers
  the frame except a triangle between the left edge and the centre (corners top-left, bottom-left, centre).
  That is how the bug was found: Epic's PC branch of `FScreenPrimUtil::DrawTile` queues quad-ordered corners
  as a triangle strip. It also means the P2 and praxis pictures above had bloom, glow, depth of field and the
  distortion quad missing in that triangle (the panes there were see-through but not distorted: with no quad
  drawn over them nothing is drawn at all). All pictures have to be retaken.
  What they do show outside the triangle: blur = four displaced ghost copies of the scene (concussion);
  berserk = the scene remapped to greens and yellows, the pawn's body left in its own colours (stencil);
  liandri = grey picture with faint horizontal noise; pause = **black**; radial = no visible change (the
  script had driven the blur distance to 0).

### Defects found since P1

4. `FScreenPrimUtil::DrawTile` (PC branch): corners in quad order drawn as a strip; a quarter of each tile is
   missing. Fixed (`6fa7758`): a fan, one tile per draw. Only Xbox-only paths call it, so nothing changes for
   a device without the capability. One tile per draw also makes each quad of the radial blur read the back
   buffer as the previous quad left it, as on Xbox.
5. `UClient::DynamicGamma` reads True on PC although only the `_XBOX` code sets it (`get SDL2Drv.SDL2Client
   DynamicGamma` -> `True` with no such key in the ini). Suspected: the bool property registered right after
   it (`CaptureMouse`) shares its storage; not proven. With post-processing on the device now sets
   `DynamicGamma = 1` and `LuminosityBias = 1` itself once (what `UClient::Init` does on Xbox), so the default
   is deliberate; `LuminosityBias` 0 is treated as 1.
6. PC's ini has no `MaxDistortion`; 0 means no displacement at all. Our `Default.ini` now has the Xbox value
   (35) and the device uses 35 when it finds 0.

### Open doubts (nothing here is settled)

- The pause-menu remap came out black. Either `T_FX.ColorRamps.UI_PauseMenuRamp` is that dark for a dark
  scene, or the remap lookup is wrong for this ramp (berserk's ramp works). To do: read the ramp's texels.
- Dynamic gamma brightens DM-Deadbolt a lot (ramp[64,128,192] = 85,193,255 once settled: everything above
  63% luminance becomes white). That is what Epic's formula gives for a dark frame with `LevelContrast` 1;
  whether the Xbox looked like that I cannot check. On by default because it is the Xbox default.
- Depth of field reads the encoded depth through a bilinear filter (Epic's stage state). At a depth where the
  middle byte wraps, the average of neighbouring texels is not a depth. Estimated error for the always-on
  mode: about 2% alpha along thin contour lines; not yet looked for in a picture.
- Distortion amounts scale with height/480 (my decision, P1).
- Performance under software GL: DM-Praxis ran at about 20 frames/s off and 9 on in one run (to be measured
  properly for the gate).
- Projector shaders (12) and `PS_Terrain3Layer`: not started.

### Next, in this order

1. Retake every picture with the fixed binary; a committed script that regenerates them
   (`tools/dev/m7-effects.sh`). 2. Numeric self-tests in the driver for the primitive operations (snapshot
   orientation, depth encoding, depth copy, blur taps, DOF ramp, colour remap lookup). 3. The remaining
   effects on content (blended distortion, player cam, sniper overlay). 4. Gate a-e. 5. Projectors and
   terrain only if time remains and they can be verified.

## P5a. Numeric self-test of the primitives (`UC2GLSelfTest.cpp`, commit `4ed20bc`)

Console `POSTSELFTEST` (runs at the start of the next frame), command line `-glselftest`; through the script:
`tools/dev/m7-effects.sh m7 selftest`. Known patterns are written into the main framebuffer (colour or depth)
or into a render target, the operation runs through the same engine and driver path the game uses, the result
is read back and compared with a CPU computation of what Epic's code specifies. Result on Mesa llvmpipe 4.5,
1280x720, DM-Deadbolt (`logs/m7/selftest.out`):

| Test | Operation | What is compared | Result |
|---|---|---|---|
| `snapshot` | the copy bound when a stage samples the back buffer | all 921,600 pixels, exact, top row first | PASS, error 0 (255 against the same picture upside down) |
| `depth-color` | depth as the Xbox's B8G8R8A8 view of D24S8 | the 24-bit value rebuilt from blue/green/red, every pixel | PASS, largest error 2 units of 2^24 (half the values are 1-2 units off: the depth passes through a 32-bit float; the depth of field reads bits 23..8) |
| `depth-copy` | `CopyDepthBuffer` into a half-size target | target pixel (x,y) = depth of back-buffer pixel (2x+1, 2y+1), every pixel | PASS, largest error 1 unit of 2^24, 0 pixels from a different texel |
| `blur-h`, `blur-v` | `FilterCopyRenderTarget` with Epic's 1-2-2-1 kernels (`PS_Blur`), halving width / height | every pixel against sum of weight x bilinear sample at ((x+0.5)/w, (y+0.5)/h) + tap offset (+-1, +-3 texels) | PASS, largest error 1.0 of 255 |
| `blur-box` | the same with the box kernel, same size | as above, taps (+-1,+-1), 0.25 each | PASS, 0.5 |
| `bb-copy` | `UpdateBackBufferCopyTarget` 1280x720 -> 640x360 | every pixel, four taps at (+-1,+-1) back-buffer texels, 0.25 each | PASS, 1.44 |
| `dof-ramp` | `PS_DepthOfField` drawn as `UUCScreenDOF` draws it, absolute mode, StartZ 0.98 | blend alpha per 16x16 block of constant depth against a bilinear lookup of the ramp texture at (middle byte, high byte); and against alpha = (z - 0.98)/0.02 | PASS, 0.72; against the formula 1.8 |
| `remap` | `PS_ColorRemap` + `ColorRemap.vsh` with a lookup texture that answers with its coordinates | lookup coordinate = luminance (0.30, 0.59, 0.11) of the back-buffer pixel, per 8x8 block | PASS, 0.5 (also with a 256x1 lookup, `-glselftest1d`) |

`selftest summary: 9 passed, 0 failed`.

Do the tests have teeth? Yes, they caught three things:

1. **My own wrong model of the half-texel convention.** The first version of the expected values sampled at
   (x/w, y/h); all four filter tests failed by 73 to 120 of 255. The driver was right, my note was wrong:
   `FScreenPrimUtil` shifts its tiles by half a pixel (convention 4 above is the corrected text).
2. **A real bug in the depth encoding**: a depth of exactly 1 (cleared buffer, sky, far plane) rounded up to
   2^24 in the encode shader; the high byte saturated and the middle byte became 0, i.e. depth 0.9961 instead
   of 1. The always-on depth of field would have blurred the sky with 80% of the intended alpha. Found by
   `dof-ramp` (one block of 3,600 off by 49), fixed with a clamp.
3. **`CopyDepthBuffer`'s sample position**: checked to be the texel Epic's quad addresses.

What the self-test does not cover: the distortion vertex shader's coordinates (only seen on screen: with
`MaxDistortion` 0 the panes of DM-Praxis show the scene behind them undisplaced), `PS_EmissiveSelectBlur`,
`PS_BloomSelect`, `PS_MotionBlur`, `PS_LuminanceTexIndex` and the histogram (their GLSL is covered by M6's
`--gputest` against the interpreter; their use in the effects is only seen on screen), the point-sample
nudge, anything in a viewport that is not the whole screen (split-screen), and of course whether an Xbox
shows the same.

### The black pause picture was my test, not the renderer

`set UCScreenMenuPause Enabled 1` also enables `UCScreenComboFlash` and `UCScreenComboSunFlare`, which extend
`UCScreenMenuPause`: three colour remaps in a row. Probed with the new `GLTEXELS <texture>` command (size,
format, texels and the device's own copy of level 0): `T_FX.ColorRamps.UI_PauseMenuRamp` is 256x1 and runs
from teal (61,160,142) at luminance 0 to black at 1 (a negative), `BerserkRamp` from (0,29,23) to
(255,254,149), `FX_ColorRamps_TeleportIn` from (0,5,4) to (0,215,249). The script now switches the two
subclasses off again after enabling the pause overlay.
