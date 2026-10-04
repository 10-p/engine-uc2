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
4. **Half-texel offset**: Direct3D 8 puts pixel centres on integer coordinates, so a full-target quad with
   coordinates 0..1 samples a same-size texture on texel *boundaries*. M5's clip fix-up makes GL rasterise as
   D3D does, so this is reproduced, not corrected: bilinear reads average two by two texels exactly as on the
   Xbox (the blur chain's half-texel shifts included). A **point-filtered** read on a boundary is a tie; hardware
   takes the texel to the right/below (texel i for coordinate i), GL leaves it to rounding noise (which would
   show as torn columns). For a pass with a point-filtered render-target stage the interface therefore shifts
   the geometry 1/32 pixel less than the half pixel (`PointSampleNudge` in `GetGLMatrices`), i.e. samples
   1/32 pixel further right and down. Nothing else changes; passes without such a stage are bit-identical to M6.
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
