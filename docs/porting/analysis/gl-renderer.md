# GL renderer R1 (M5): UC2GLDrv notebook

Running notebook for M5 (PLAN.md row M5, renderer R1: fixed function). Branch `m5-gl`, worktree
`../engine-uc2-m5`. Sibling commit the merge started from: engine-ut2004 `34d703e612163099aadcc219b02992cc9b452a39`.

## Status (2026-10-04, session stopped by the usage limit)

Ordered steps of the brief: (1) skeleton links, (2) window + GL context + FBO + present, (3) canvas/fonts:
menu 2D visible, (4) BSP/meshes with the material compiler, (5) lights, fog, projectors.

- **Reached: step 3, verified by running.** Steps 4 and 5 are *written* (the whole material compiler, lights,
  fog, projectors, lightmaps are in the code) but **not yet run on a map**.
- Verified by running (Xvfb, Mesa llvmpipe 4.5 compat):
  `cd /home/ion/WorkUC2/run/m5/System && SDL_AUDIODRIVER=dummy timeout 40 xvfb-run -a -s "-screen 0 1280x720x24" ./uc2.bin -log -glerrors -glshot=15,30`
  → exit 124 (still running at 40 s), log (`logs/m5/run1.out`): `GL_VENDOR : Mesa`,
  `GL_RENDERER : llvmpipe (LLVM 21.1.8, 256 bits)`, `GL_VERSION : 4.5 (Compatibility Profile) Mesa 26.0.8-1ubuntu0.3`,
  `UC2GLDrv: 8 texture units (GL reports 8), max texture 16384, S3TC yes, combine3 yes`,
  `Bringing Level Menu.MyLevel up for play (0)...`, `UC2GLDrv: frame 600, 0 GL errors so far`, 0 `Critical:`.
- Screenshot through the engine's own `SHOT` exec (`-glshot=15` → `ReadPixels` → `appCreateBitmap`):
  `logs/m5/menu-shot15s.png` (converted from `System/Shot00000.bmp`). It shows the main menu correctly:
  teal background art, "UNREAL CHAMPIONSHIP 2" title, STORY MODE (highlighted) / INSTANT ACTION / NETWORKED
  COMBAT / PROFILES / OPTIONS, "NOT SIGNED IN", "ENTER SINGLE PLAYER TOURNAMENT", the green (A) SELECT
  button glyph. Text, alpha-blended panels and colours look right (no red/blue swap). The second shot (30 s)
  was not taken: the run was killed at 40 s, the timer starts at the first frame (~15 s after launch).
- NOT yet done / not verified: DM-Deadbolt run and screenshot; GL error counts for 40 s menu and DM runs
  (only the in-run line above); `-nullrenderer` regression; `verifyclasses`, `ucc server`, map census;
  wasm build (UC2GLDrv is in `UC2_NATIVE_ONLY_PACKAGES`, not built there yet); ffmpeg x11grab cross-check.
- Next command for a successor (after `cmake --preset native && cmake --build build-native -j12` and
  `tools/dev/mkrun-uc2.sh build-native m5`):
  `cd /home/ion/WorkUC2/run/m5/System && SDL_AUDIODRIVER=dummy timeout 60 xvfb-run -a -s "-screen 0 1280x720x24" ./uc2.bin DM-Deadbolt -log -glerrors -glshot=20,35`
  then look at `Shot*.bmp` (BMP, 24-bit) and the `UC2GLDrv: GL error` lines.

## Approach and one deliberate technique choice

Three-way merge: base = engine-ut2004 D3DDrv, ours = engine-ut2004 OpenGLDrv (structure, GL code), theirs =
UC2 D3DDrv (spec). **Technique choice the coordinator should review:** the sibling OpenGLDrv writes GL
texture-env tokens inline in its material compiler. UC2's compiler adds ~1.4k lines; instead of hand-
translating every new line, `UC2GLMaterialState.cpp` is UC2's `D3DMaterialState.cpp` carried over function for
function (types renamed, D3DX matrix helpers on FMatrix) and stages stay in D3D8 vocabulary
(`Inc/UC2GLD3DTypes.h`, values of d3d8types.h). The D3D→GL translation (combiner ops/args, blend, address,
filters, texcoord index/texgen, D3D texture-matrix padding rules) is in one place:
`FOpenGLRenderInterface::SetMaterialBlending` / `TranslateStageOp` / `CommitTexCoords`. The file therefore
stays diffable against UC2's spec (every deliberate change marked `UC2GLDrv:`), and the same stage
description is what an M6 GLSL generator / GL4ES would consume.

## Files

| File | What |
|---|---|
| `Source/UC2GLDrv/CMakeLists.txt` | static lib, `GPackage=GUC2GLDrvPackage`, links Engine + SDL2 (no libGL) |
| `Inc/UC2GLBinding.h` | binding selection: `UC2GL_BINDING_DESKTOP` (built) / `UC2GL_BINDING_GL4ES` (#error, later); system `<GL/gl.h>`+`<GL/glext.h>` without prototypes |
| `Inc/UC2GLFuncs.h` | X-macro list of the ~110 GL entry points used (1.1–1.5, 2.0 shaders, FBO) + extension flags |
| `Inc/UC2GLD3DTypes.h` | D3D8 fixed-function vocabulary + `D3DMATRIX` (FMatrix layout) |
| `Inc/UC2GLResource.h`, `Src/UC2GLResource.cpp` | resources with UC2's 16-bit cache ids per `ECacheHint` (`TOpenGLResourceCache` = UC2 `TResourceCache`), back link to the engine resource; textures (RGBA8/P8/G16/RGBA7 via `GetTextureData`, DXT1/3/5 compressed, L8, A8, R5G6B5, cubemaps, composite lightmaps), render targets (texture + FBO + own or shared depth/stencil), default render target, VBO vertex/index buffers, dynamic buffers; D3DCOLOR→RGBA swizzle on upload |
| `Inc/UC2GLRenderInterface.h`, `Src/UC2GLRenderInterface.cpp` | UC2's FRenderInterface: state stacks (saved state, render target, viewport, stencil), SetMaterial, lights (UC2 PC selection: directional only), fog, transforms, D3D→GL clip fix-up, culling, stencil/depth, streams, draw, PC-equivalent post-process stubs, occlusion queries answering "visible" with the count written |
| `Src/UC2GLMaterialState.cpp` | material compiler = UC2 `D3DMaterialState.cpp` ported |
| `Src/UC2GLPSMaterialState.cpp` | UC2 `D3DPSMaterialState.cpp` ported (state of pixel-shader materials; PC runs it also for its Xbox-only fallback) |
| `Src/UC2GLD3DXCompat.h` | D3DXMatrixMultiply/Inverse/Transpose/Identity on FMatrix |
| `Inc/UC2GLRenderDevice.h`, `Src/UC2GLRenderDevice.cpp` | `UC2GLDrv.UC2GLRenderDevice`: proc table via `SDL_GL_GetProcAddress`, SetRes through the viewport, main FBO (RGBA8 texture + D24S8), present pass (GLSL 1.20/ESSL-1.00-compatible, D3D gamma ramp formula, `-NOGAMMA` off), `-glerrors`/`CheckGLErrors` per-frame glGetError with frame number, `-glshot=<s>,...` runs the engine's SHOT exec, ReadPixels top-down BGRA |
| `Src/UC2GLDrv.cpp` | `IMPLEMENT_PACKAGE(UC2GLDrv)` |

Changes outside the driver (small, delimited):
- `CMakeLists.txt`: `UC2GLDrv` appended to `UC2_NATIVE_ONLY_PACKAGES` (it was already in `UC2_PACKAGES`).
- `Source/SDL2Drv`: `GLContext` member; `ResizeViewport` with `BLIT_OpenGL` calls `EnsureGLContext()`, which
  re-creates the plain window as `SDL_WINDOW_OPENGL` (same title/position/size) and creates the context;
  context deleted in `CloseWindow`/`ShutdownAfterError`. Plain window path unchanged for the null device.
- `Source/SDLLaunch`: links/registers UC2GLDrv when the target exists (`UC2_WITH_UC2GLDRV`); `-opengl` switch.
- `Engine/Config/Default.ini`: `RenderDevice=UC2GLDrv.UC2GLRenderDevice` (null device kept as a comment line);
  new section `[UC2GLDrv.UC2GLRenderDevice]`. **Not yet recorded in `Engine/Config/README.md`** (to do).
- No Engine/Core change.

## Merge table (as it stands; function level)

Counts (sibling OpenGLDrv function → UC2 function): taken as is ~10 (stream/shader declarations, resource
list handling, GLError → CheckGLError, PushState, CommitMatrices structure, texture upload skeleton,
FindExt/FindProc pattern), adapted ~35 (every FRenderInterface virtual present in both: signature to UC2's,
behaviour to UC2 D3DDrv — SetRenderTarget/SetViewport now stacks, SetMaterial returns INT, SetLight UC2's
directional-only selection, cache ids `_WORD`+`ECacheHint`, ReadPixels without flip flag, PopState resets
lights/ambient as D3D), new from D3DDrv ~60 (ResetRenderTarget, GetRenderTarget, IsDefaultRenderTarget,
ResetViewport, GetViewport, SetScreenSpaceOffset, SetColorWriteMask, SetRenderStateDirect, ResetStencilOp,
EnableStencil, EnableDepth, GetLastMaterial, AddVertexShaderEnum, GetStageCountForPass, PrecacheMaterial,
GetPrecacheMode, occlusion ×3, GetPixelAspectRatio, ResetRenderStates, LoadVertexShaderEnum, all 26+9
material-state handlers incl. SetMultiPassMaterial, SetProjectorMultiMaterial, SetMovieMaterial,
HandleDiffuseTFactor_Stage, HandleBumpMaterial, the 24 PS state handlers), deferred to M6/M7/M9 (hooks):
see below. Dropped from the sibling: NV_VAR / ATI_VAO / APPLE_VAO / ATI element arrays, ARB_fp terrain
shaders, DE's dyn-quad helpers, WGL (not in UC2's interface or obsolete). A per-function table is still to
be written out in full.

## Deferred (hooks)

- M6 pixel shaders: `UC2GLMaterialState.cpp` `SetPixelShaderMaterial` (`UC2GL_PIXELSHADERS 0`: every PS
  material takes PC's Xbox-only fallback — texture stage 0 via SetSimpleMaterial + the PS state handler);
  `SetMaterialBlending` logs if a pass asks for a pixel shader; `GetRenderCaps` reports PixelShaderVersion 0.
  Note: on PC the non-Xbox-only PS materials (e.g. PSSkinShader) used real ps.1.x shaders; at R1 they get
  the fallback too — a known difference from PC until M6.
- M6 vertex shaders: `HandlePSVertexShader` body kept under `#ifdef _XBOX`; `LoadVertexShaderEnum`,
  `SetVertexShaderConstant`, `SetPixelShaderConstant` (records only).
- M7: `GetBackBufferCopyMaterial`, `UpdateBackBufferCopyTarget`, `GetBackBufferMaterial`, `CopyDepthBuffer`,
  `UpdateDynamicGamma`, `UpdateDOFRampLookupTexture` (all return/do what PC D3DDrv does).
- M9: `SetMovieMaterial` returns 0 as on PC (default material fallback).

## Defects found

- Sibling OpenGLDrv: `glPolygonMode(..., FM_Wireframe ? GL_LINE : GL_FILL)` (constant condition);
  `TCN_4DCoords` and `TCS_NoChange` switch fall-throughs; texcoord sets bound by order, not by
  `FVF_TexCoordN` (D3D binds by register). Not carried over.
- UC2 `TResourceCache::Flush` could unlink the wrong chain entry and only recycled ids of emptied slots:
  ours unlinks exactly the id.
- PC D3DDrv occlusion stubs never write `dwPixels`: ours writes a visible count.
- UC2 adds LE_Ambient light colours as packed DWORDs (no `FColor::operator+`): reproduced on purpose.

## Untested / unsure

- Everything 3D (BSP, static/skeletal meshes, terrain, lightmaps, projectors, fog, lights): code complete,
  never run. The texture-matrix padding rules (D3D (u,v,1,0) vs GL (s,t,0,1), q forced to 1 unless
  projected) and the clip fix-up (z' = 2z−w, half-pixel shift, flip for RT textures) are reasoned, not yet
  checked against a 3D image.
- `GIsOpenGL` is kept 0 (engine stays on D3D colour/pixel conventions; driver converts) — differs from the
  sibling, which set 1.
- `-nullrenderer` persists into `Avaris.ini` (M4 behaviour); a later run without switches then stays null —
  use `-opengl` or a fresh run tree.
- Gamma ramp is applied always (PC applied it only fullscreen); `-NOGAMMA` disables.
