# Programmable materials (M6, renderer R2): notebook

Branch `m6-shaders`, worktree `../engine-uc2-m6`. Scope: PLAN.md row M6. Steps S1 (pixel-shader translator),
S2 (vertex side), S3 (driver integration), S4 (verification on content). Each step is appended here when it
is committed. Everything that runs goes through `<WorkUC2>/bin/capped` (CLAUDE.md on `main`, "Running
anything: memory ceiling, always").

## S1. `tools/psh2glsl.py`: the pixel shaders

### What it is

One Python file, no dependencies beyond the standard library:

- **Parser** of Epic's `.psh` text (`xps.1.1`, and the three `ps.1.1` files): texture declarations, combiner
  instructions with instruction modifiers, source modifiers and write masks, `+` pairing, `xmma`, `xdd`, `xfc`.
- **Path A, the reference interpreter**: evaluates the parsed text instruction by instruction, from the
  XDK's description of each instruction.
- **Path B, the independent evaluator**: decodes the 60 definition words of the compiled `.h` next to each
  source (`D3DPIXELSHADERDEF`, XDK `d3d8types.h` 2260-2684: texture modes, per-stage RGB/alpha inputs A-D with
  their input mappings, outputs AB/CD/SUM with their flags, constant mappings, final combiner inputs A-G) and
  evaluates the register-combiner hardware model directly. It shares only the number helpers and the test
  inputs with path A; how the text maps onto combiner stages is the assembler's work, not mine.
- **GLSL generator**: one fragment-shader body per `.psh`, GLSL 1.20 that is also valid GLSL ES 1.00. The
  files carry no `#version` line: the consumer prepends `#version 120` (or `#version 100` plus a `precision`
  statement) and optional defines. No fixed-function built-ins: inputs are `uniform sampler2D/samplerCube
  u_tex0..3`, `uniform vec4 u_c0..7`, `uniform vec4 u_fogColor`, `varying vec4 v_color0, v_color1`,
  `varying float v_fog`, `varying vec4 v_tex0..3`; output `gl_FragColor`.
- **GPU check** (`tools/pshgpu/pshgpu.c`, plain X11 + GLX + GL 2.0 with a framebuffer object, built by the
  tool on first use into `build-pshgpu/`): draws every test case of every shader as one quad with constant
  inputs, reads the framebuffer back and compares with path A.

Choice asked for by the brief: **one generated file per `.psh`**, including the 32 `DualDiffuse_*` variants.
They are produced mechanically, so a parametrised source would only add a second mechanism to keep right.

### Commands and evidence (2026-10-04, Mesa llvmpipe 4.5 compatibility under Xvfb)

```
bin/capped 1G 120 -- python3 tools/psh2glsl.py --selftest
  NODEF ColorRemapGrayScale   no compiled .h next to the source: path A only (64 cases evaluated)
  psh2glsl selftest: 100 shaders, 99 compared with their definition words (6336 cases, max difference
  0.00e+00), 1 without a .h, 0 failed, 0 lint warnings; 7 synthetic semantics checks, 0 failed

python3 tools/psh2glsl.py --emit            # writes Source/UC2GLDrv/Shaders (102 files), deterministic
python3 tools/psh2glsl.py --check-emitted   # fails if the checked-in files are not what the tool produces

bin/capped 2G 300 -- xvfb-run -a -s "-screen 0 640x480x24" python3 tools/psh2glsl.py --gputest
  pshgpu: GL_RENDERER llvmpipe (LLVM 21.1.8, 256 bits), GL_VERSION 4.5 (Compatibility Profile) Mesa 26.0.8
  pshgpu: 107 shaders rendered, 6848 cases, 0 outside the tolerance of 1.00 8-bit steps (worst 0.500),
  420 program variants linked, GLSL ES 1.00 compile 107/107, 0 shaders failed

python3 tools/psh2glsl.py --dump SkinShader # parsed text, decoded definition words, GLSL of one shader
```

- Test inputs: 64 cases per shader, seeded by the shader's name (deterministic). Textures are 16x16 RGBA8,
  nearest filtered, clamped; colours, constants and fog on the 8-bit grid with 0, 1 and 128/255 over-
  represented; every 8th case all-zero or all-one colours and constants; every 5th case a constant of 16.0
  (the engine really passes that: it must behave as 1.0); projective texture coordinates (q of 0.5, 1, 2);
  cases whose dependent texture read would land within 2% of a texel edge are regenerated, so the nearest
  filter is unambiguous on both sides.
- GPU tolerance: **1.0 step of the 8-bit framebuffer against the interpreter's unrounded value** (that is
  rounding plus float noise; the worst seen is exactly 0.5, pure rounding). 107 = the 100 sources + 7
  synthetic shaders. Each body is also linked with `UC2_SPECULARENABLE`, with `UC2_ALPHATEST`, with every
  applicable `UC2_TEXn_CUBE`, and compiled as `#version 100` (Mesa accepts ES 1.00 in a desktop context
  through `ARB_ES2_compatibility`): 107/107.
- Do the tests have teeth? Deliberately wrong semantics in path A (one-off runs, not kept): `_x4` treated as
  `_x2` → 12 shaders fail the selftest and 13 fail the GPU check; alpha half ignoring `.b` → 29 fail;
  unmasked dot product not writing alpha → 2 fail; `_sat` ignored → 1 fails. Two rules are **not exercised by
  any of Epic's 100 sources** (a wrong reading passes): the default blue source of the `xfc` alpha argument
  and the simultaneity of paired halves that read each other's destination. Those, the `sum`/`prod`
  registers, `-x`, `_bias`, `_bx2`, `_d2`, `sub` and the default final combiner with specular are covered by
  the 7 synthetic shaders whose expected values are worked out by hand from the XDK pages (in the tool,
  `SYNTHETIC`), and the synthetic shaders are rendered by the GPU check as well.

What this evidence is and is not: paths A and B agreeing means my reading of the *text* matches what
Microsoft's assembler *produced*; the GPU check means the GLSL computes what path A computes. Neither is a
comparison with an Xbox. The hardware model itself (clamping points, mapping formulas) is taken from the SDK
header and pages listed below and is the same in both paths.

### Instruction semantics implemented, with the source of each rule

XDK = `reference/xdk/unpacked/2005-02-5849.15/XDK`; "page" = a page of `doc/XboxSDK.chm`.

| Rule | Implemented as | Source |
|---|---|---|
| Registers | `r0 r1 t0-t3 v0 v1` signed [-1,1]; `c0-c7` [0,1]; `fog.rgb` fog colour, `fog.a` fog factor | page `_dx_registers_graphics` ("components of rn, tn, vn range from -1.0 to 1.0, cn from 0.0 to 1.0") |
| Constants out of range | clamped to [0,1] in the shader (`clamp(u_cN,0,1)`); Epic passes 16.0 on purpose | same page; `D3DPSMaterialState.cpp` skin handlers ("a crazy constant") |
| `r0.a` initial value | `t0.a`; `r0.rgb`, `r1` start at 0 | `d3d8types.h` 2551 "R0_ALPHA is initialized to T0_ALPHA in stage0" (0 for the rest is my choice; the lint reports any read of an unwritten register: none in the 100 sources) |
| Source, no modifier | value as is (signed) | `PS_INPUTMAPPING_SIGNED_IDENTITY` (what the assembler emits: verified in B) |
| `x_sat` | max(0,x) | `PS_INPUTMAPPING_UNSIGNED_IDENTITY`; pages xmma/xdd "Clamps negative Y to 0" |
| `1-x` | 1 - max(0,x) | `UNSIGNED_INVERT` |
| `-x` | -x | `SIGNED_NEGATE` |
| `x_bx2`, `-x_bx2` | 2*max(0,x)-1, 1-2*max(0,x) | `EXPAND_NORMAL`, `EXPAND_NEGATE` |
| `x_bias`, `-x_bias` | max(0,x)-0.5, 0.5-max(0,x) | `HALFBIAS_NORMAL`, `HALFBIAS_NEGATE` |
| `.a` on a source | rgb half: alpha replicated to rgb; alpha half: alpha | `PS_CHANNEL_ALPHA` |
| `.b` on a source | alpha half reads blue | `PS_CHANNEL_BLUE` ("used as ALPHA source") |
| no selector / `.rgb` | rgb half: rgb. Alpha half: alpha when no selector; **blue when `.rgb` is written** (e.g. `mul r0, r0.rgb, t0.a`) | the second half is from the assembler's output (path B), not from a page |
| `zero`, `1-zero`, `discard` as source | 0, 1, 0 | pages xmma, xdd (`xdd r0, discard, t0, c2, discard, discard` in terrain3layer) |
| Write mask | none: both halves run the instruction; `.rgb` / `.a`: one half; `+` puts a `.rgb` and an `.a` instruction in one stage | d3d8types.h combiner model; page `_dx_pixel_shader_assembler_reference` |
| Simultaneity | every source of a stage is read before any destination of that stage is written (both halves, all outputs) | combiner model: one stage = one evaluation |
| `mov add sub mul mad` | s0; s0+s1; s0-s1; s0*s1; s0*s1+s2 | D3D8 pages of each |
| `lrp d,s0,s1,s2` | s0*s1 + (1-max(0,s0))*s2 | page `_dx_lrp_graphics`; the complement is the `UNSIGNED_INVERT` mapping (path B) |
| `dp3` | dot of the rgb sources, replicated to rgb; **unmasked: also to alpha** | the assembler sets `PS_COMBINEROUTPUT_AB_BLUE_TO_ALPHA` (path B; LuminanceTexIndex) |
| `xmma d0,d1,d2,s0..s3` | d0=s0*s1, d1=s2*s3, d2=s0*s1+s2*s3, any of them `discard` | page `xbox_jbh_dxref_psh_xmma` |
| `xdd d0,d1,s0..s3` | d0=s0.s1, d1=s2.s3 (rgb; unmasked also alpha, as dp3) | page `xbox_jbh_dxref_psh_xdd`; flags 0xc3 in terrain3layer.h |
| `_x2 _x4 _d2 _bias _bx2` on an instruction | applied to every output of the stage: x*2, x*4, x/2, x-0.5, (x-0.5)*2 | `PS_COMBINEROUTPUT_SHIFTLEFT_1/2`, `SHIFTRIGHT_1`, `BIAS`, `SHIFTLEFT_1_BIAS` |
| Clamp after a stage | every written value clamped to [-1,1] | register range (page registers) |
| `xfc s0..s6` | rgb = s0*s1 + (1-s0)*s2 + s3, clamped to [0,1]; alpha = s6 | page `xbox_jbh_dxref_psh_xfc` |
| `xfc` inputs | clamped to [0,1] (`_sat` is the default), `1-x` allowed | same page |
| `xfc` alpha source s6 | **blue unless `.a` is written** | same page ("The blue channel is the default unless the .a modifier is applied") |
| `prod`, `sum` | prod = s4*s5; sum = sat(sat(r0)+sat(v1)) | same page; the assembler sets `PS_FINALCOMBINERSETTING_CLAMP_SUM` (0x80) in every definition |
| No `xfc` | rgb = lerp(fog colour, sat(r0) [+ v1 when `D3DRS_SPECULARENABLE`], fog factor), alpha = sat(r0.a) | same page, the four-row table; definition words: both final-combiner words are 0 |
| Fog disabled | not a shader matter: the driver supplies fog factor 1 | **inferred** (NV register combiners: fog factor is 1 when fog is off); the XDK page only says "no fog blending occurs" for shaders without `xfc` |
| `tex tN` | `texture2DProj(u_texN, v_texN)` (s/q, t/q), or `textureCube(u_texN, v_texN.xyz)` when the driver defines `UC2_TEXn_CUBE` | `PS_TEXTUREMODES_PROJECT2D`; `PS_GLOBALFLAGS_TEXMODE_ADJUST` (set in every definition: the mode follows the bound texture's type) |
| `texcoord tN` | clamp((s,t,r,q),0,1) as rgba | `PS_TEXTUREMODES_PASSTHRU` "argb = s,t,r,q" |
| `texkill tN` | discard if any of s,t,r,q < 0 | `PS_TEXTUREMODES_CLIPPLANE`, compare mode 0 in the three users |
| `texm3x2pad tN,tM` + `texm3x2tex tN+1,tM` | u = tcN.str . tM.rgb, v = tc(N+1).str . tM.rgb, sample stage N+1 at (u,v); colours taken 0..1 | `DOTPRODUCT` + `DOT_ST`, dot mapping `ZERO_TO_ONE`; page `_dx_texm3x2tex_graphics` |
| `texreg2gb tN,tM` | sample stage N at (tM.g, tM.b) | `PS_TEXTUREMODES_DPNDNT_GB` |

Not modelled: the hardware's 8-bit-plus-sign fixed-point precision (floats are used throughout, in the
interpreter and in GLSL); the visible consequence is banding the Xbox had and we do not, in particular in the
`xmma_x4` "all or nothing" selects. Mux (`PS_COMBINEROUTPUT_AB_CD_MUX`) is decoded by path B but no source
uses it.

Extras in the generated GLSL that are not Xbox semantics: `UC2_ALPHATEST` + `u_alphaRef` (discard unless
alpha > ref: D3D's alpha test, for targets without fixed-function alpha test; the desktop driver does not
define it) and the comment header naming the `.psh` and the enum values.

### Outputs

- `Source/UC2GLDrv/Shaders/<name>.glsl`: 100 files, one per `.psh`.
- `Source/UC2GLDrv/Shaders/EPixelShader.tsv`: value, enum name, file. Derived by the tool from Epic's own
  switch in the `_XBOX` branch of `UD3DRenderDevice::GetPixelShader` (`D3DRenderDevice.cpp` 4110-4320), not
  typed by hand.
- `Source/UC2GLDrv/Shaders/UC2GLPixelShaders.inc`: the same sources as C string literals plus the table
  `GUC2GLPixelShaders[PS_MAX]` (name, source, texture operation per stage, constants read, fog use) for the
  driver, so the run tree needs no shader files.

### Findings about Epic's shader data (nothing was changed)

- `ColorRemapGrayScale.h` does not exist in the tree, although `XboxPixelShaders.h` includes it and
  `GetPixelShader` uses it (`PS_ColorRemapGrayScale`). The leaked tree would not build for Xbox without
  running `xsasm` first. For us: this one shader has no second source of truth (path A and the GPU check only).
- `PS_Terrain4Layer` (2) has no Xbox shader at all (only a PC ps.1.4 one); `PS_None` is 0. So 97 enum values
  map to 97 distinct sources.
- Three sources belong to no enum value: `DistortionBlended.psh` (its case is commented out), `DOF_Debug.psh`
  (its include is commented out), `LuminanceTransform.psh`. They are translated and tested like the others.
- `UseTexIndex`, `LuminanceTexIndex`, `UseTexIndexMasked` are `ps.1.1`, not `xps.1.1`; the Xbox assembler
  accepts both and their `.h` files agree with path A.

## S2. The vertex side

### What the Xbox build does (the specification)

Nothing on Xbox is fixed function (`VERTEXSHADERS_ALWAYS_ON`). `FD3DDeferredState::Commit`
(`D3DRenderState.cpp` 470-660) turns the *fixed-function render state* into a 64-bit `VertexShaderEnum`
(`UnRenDev.h` 116-256) and into shader constants, and `FD3DCombinedVertexShader` (`D3DResource.cpp` 2230-2620)
splices the `.vsh` fragments that enum names:

- position: `DefaultPosition` or `UnpackPosition`; `NotBoned` / `SkelThreeBoned` / `TerrainSpecial`;
- colour: `InitRGB` (0), `LightAmbient` (always), `LightHardDirectional5` when `RS_LIGHTING`
  (`HARDCODED_VS_LIGHTS`: five directional lights, direction in local space, colour; nothing else), then, when
  `RS_COLORVERTEX`: static meshes `MulVertexSpec` (* SPECULAR) and, with an instance colour stream,
  `AddVertexRGB` (+ DIFFUSE); everything else `AddVertexRGB`; `OutLightRGB`;
- per texture stage with a texture: the source (`TEXCOORDn`, camera-space position / normal / reflection
  vector) and the transform (`none`, `m4x2`, `m4x3`, `m4x3 projected`), from `TSS_TEXCOORDINDEX` and
  `TSS_TEXTURETRANSFORMFLAGS` (`GetTexStageVertexShader`);
- `Fog` when `RS_FOGENABLE`: `oFog = (End - z_camera) / (End - Start)`;
- unique shaders a material asks for: distortion, distortion accumulation, colour remap, projector attenuation.

### What ours does (`Source/UC2GLDrv/Src/UC2GLShaders.cpp`)

The same two steps, for passes of pixel-shader materials only (everything else stays on the fixed-function path
of M5):

1. `FOpenGLRenderInterface::GetStateVertexShaderEnum` builds **Epic's enum, same bit layout**, from the same
   state: `UseDynamicLighting` -> `VS_Lit`, `UseStaticLighting` -> `VS_VertexColored`, the pass's stages ->
   `VS_Tex*_Source` / `VS_Tex*_Transform` by the rules of `GetTexStageVertexShader`, distance fog -> `VS_Fog`.
2. `UUC2GLRenderDevice::BuildVertexShaderSource` writes one GLSL 1.20 vertex shader for an enum: GLSL snippets
   hand-ported from the fragments, emitted in the order `FD3DCombinedVertexShader` splices them, each under a
   comment naming its `.vsh` file. Shader objects are cached per enum (`VertexShaderObjects`), programs per
   (pixel shader, variant, enum) (`Programs`), linked on first use.

Ported fragments (19 of the 67 files, counting the per-stage copies once): `DefaultPosition`, `NotBoned`,
`InitRGB`, `LightAmbient`, `LightHardDirectional5`, `MulVertexSpec`, `AddVertexRGB`, `OutLightRGB`,
`TexCamPosition`, `TexCamNormal`, `TexCamReflect`, `TexSourceN`, `TexSourceCamPos/CamNorm/CamRef`, `TexPassN`,
`TexSourcePassN`, `TexTransformN`, `TexSourceTransformN_m4x2/_m4x3z/_m4x3w`, `Fog`. `Screenspace` is GL's own
viewport transform. Not ported, with the reason: `UnpackPosition`, `SkelOne..FourBoned`, `TerrainSpecial`,
`TexSourceTerrainSpec` need `XBOX_ASSET_FORMAT` vertex data (packed positions, GPU skinning: never defined
here; skeletal meshes stay CPU-skinned as on PC); `Distortion`, `DistortionAccum`, `ColorRemap` belong to M7
shaders; `ProjectorAtten*`, `ProjectorDistanceAtten`, `TexSourceTexKill3` belong to the projector batching
the engine does only under `_XBOX`; `LightDirectional`, `LightPoint`, `LightSpot`, `InitLighting`, `NextLight`
are the non-hard-coded light path Epic compiled out (`HARDCODED_VS_LIGHTS`); `OutVertexRGB`, `AddVertexSpec`
are not referenced by the splicer.

Vertex attributes are generic (`a_position, a_normal, a_diffuse, a_specular, a_tex0..3` at locations 0-7, the
roles of `ShaderRegisters.vsh`); uniforms are the constants of `ShaderConstants.vsh` (`u_clip`,
`u_modelView`, `u_normalMatrix`, `u_ambient`, `u_lightDir[5]`, `u_lightColor[5]`, `u_fogParams`,
`u_texMatrix0..3`). `u_clip` includes M5's D3D-to-GL clip fix-up. No fixed-function built-in is used on
either side, so the pair is ES 1.00 material after the header swap (vertex side not yet compiled as ES).

### How lighting reaches these materials, and why

Decision: **the vertex colour of a pixel-shader pass is computed by the GLSL port of Epic's Xbox fragments**,
from the lights and ambient the engine hands the driver, not by fixed-function T&L.

Justification from the code:

- The light *selection* is already the Xbox one on PC: `SetLight` (ported at M5 from UC2's
  `FD3DRenderInterface::SetLight`) admits `VS_Lights_Directional_MAX` = 5 directional and
  `VS_Lights_Point_MAX` = 0 point lights and approximates every other light by a directional one scaled by
  `SampleLightAtten`. So both platforms light with at most five directional lights plus ambient.
- The *formula* of the fragments is, by Epic's own comment in `FD3DCombinedVertexShader`, the D3D reflectance
  model with the material sources PC sets (`EMISSIVEMATERIALSOURCE = COLOR1`, `DIFFUSE/AMBIENTMATERIALSOURCE
  = COLOR2`): `oD0 = (GlobalAmbient + Sum(light colour * max(N.-L, 0))) [* VertexSpecular] [+ VertexDiffuse]`.
  For lit geometry the two platforms therefore agree, and the port gives PC's result as well as Xbox's.
- Fixed-function GL cannot express the static-mesh case (diffuse and ambient material taken from the
  *secondary* colour); M5's fixed path uses the primary colour as emissive only. The GLSL path has no such
  limit, so for pixel-shader materials on static meshes ours is closer to both originals than M5 was.
- The Xbox engine passes `VS_StaticMeshVertexColor[Diffuse]` from `UnRenderStaticMesh.cpp` (an `_XBOX`
  branch); PC's passes `VS_FixedFunction`. The driver reads the same fact off the streams instead: with
  `RS_COLORVERTEX`, a stream declaring `FVF_Specular` (only static meshes do: their colour/alpha stream) gives
  `VS_StaticMeshVertexColor`, plus `...Diffuse` when an `FVF_Diffuse` stream (the instance's) is bound too.
  This is D3D's own rule (a material source falls back to the material when the vertex lacks the colour).
- Where the platforms differ, Xbox wins as the brief asks: with lighting off and no vertex colours the
  fragments output the ambient colour (alpha 1), where D3D's fixed function outputs white.
  **Not verified on content**: I found no pixel-shader material drawn that way in the runs below.
- Light directions: `HandleVertexShaderLights` transforms the world direction by the transpose of the world
  matrix and normalises (exact for rotations and uniform scale, Epic's approximation otherwise); reproduced.
- Skeletal meshes: PC skins on the CPU and hands world/local-space positions and normals to the driver;
  `VS_NotBoned` with `DefaultPosition` is then exactly right.

Inferred, not from the code: the value of registers nothing writes (`v_color1` = (0,0,0,1), `v_texN` =
(0,0,0,1), `v_fog` = 1, a missing DIFFUSE stream = (0,0,0,1), missing SPECULAR = 1): chosen so that they add
nothing; none of them is read in a combination I saw in use. Output colours and fog are clamped to [0,1] per
vertex (D3D vertex-shader outputs), and again per fragment by the generated shaders.

### `VertexShaderConfigs.ini` as a precompile list

`InitPixelShaders` constructs Epic's `UVertexShaderConfigs` and calls `PrecacheVertexShaderEnums`, which calls
our `LoadVertexShaderEnum` per entry (this is the Xbox device's start-up path, `D3DRenderDevice.cpp` 765-800).
`VS_UnpackPosition` is dropped from each entry (PC streams are not packed); entries needing GPU skinning,
packed terrain or a unique (M7 / projector) shader are counted and skipped. Measured (llvmpipe):
`VertexShaderConfigs.ini: 555 combinations, 405 vertex shaders compiled (0 failed), 137 need the Xbox asset
format (GPU skinning, packed terrain) or an M7 shader, 0.20 s` (the remaining 13 collapse onto others once
`VS_UnpackPosition` is dropped). Honest value of the list on PC data: in the runs below 2 to 4 of the vertex
shaders actually used were *not* in it (the periodic log line gives both numbers), so it removes most, not
all, first-use compiles. Programs are still linked on first use. Switch: `PrecompileVertexShaders`.

## S3. Driver integration

- **Programs** (`UC2GLShaders.cpp`): fragment shader objects per (EPixelShader, variant); variants are the
  cube-map mask of the bound textures (`UC2_TEXn_CUBE`, the Xbox `TEXMODE_ADJUST`) and `UC2_SPECULARENABLE`.
  The base variant of every integrated shader is compiled at start-up; a shader that fails is logged once
  (`pixel shader N (name.psh) is not usable; its materials are drawn with their first texture stage`) and its
  whole family takes the M5 fallback. A program that cannot be built at draw time is logged once and that
  draw uses the fixed-function path. Counters are logged every 600 frames with the GL error line and at exit.
- **`SetPixelShaderMaterial`** (`UC2GLMaterialState.cpp`): PC's condition `IsXboxOnly() == 0` is replaced by
  `RenDev->PixelShaderFamilyReady(type)`: the Xbox branch (all four texture stages, then the state handler) is
  taken when every shader the material's state handler can select is compiled and has its inputs. A family =
  the enum values sharing one handler in `D3DPSMaterialState.cpp`'s table. Materials flagged Xbox-only
  (`PSTeamSkinShader`, `PSEmissiveSkinShader`, `PSTeamShader`...) no longer fall back.
- **State handlers** (`UC2GLPSMaterialState.cpp`): unchanged from M5, which carried Epic's file over line for
  line. The only `_XBOX` branch in it is in `SetMatState_PS_Distortion` (M7). Constants go through
  `SetPixelShaderConstant` into `u_c0..7`.
- **Draw** (`DrawPrimitive` -> `BeginProgramPass` / `EndProgramPass`): per pass, vertex streams as generic
  attributes, the state's vertex shader enum, program, uniforms. Blending, depth, alpha test, culling, colour
  mask, texture binding and filtering are M5's `SetMaterialBlending`, shared by both paths. A stage the shader
  samples but the material leaves empty gets the opaque black texture (D3D's answer for an unset texture;
  **inferred** for Xbox). Fog: the program does it (per vertex, as on Xbox); GL's fixed fog does not apply
  to programs.
- **Switches**: `[UC2GLDrv.UC2GLRenderDevice] UsePixelShaders` (default True), `-nopixelshaders` /
  `-pixelshaders`, console `PIXELSHADERS [0|1]`, `-glpstoggle=<s>,...` (flip at those times after the first
  frame; with `-glshot=<s>,...` one run gives both pictures from one camera), `PrecompileVertexShaders`.
  `-gldualdiffuse=<n>` is a test switch (see S4). Found on the way: `-glshot=a,b` only ever took the first
  time (the engine's `Parse` stops at a comma); the list is now read by hand.

### Per-shader table

`integrated` = translated, numerically tested (S1) and drawn by programs when a material asks for it.
`M7` = translated and tested, stays on the M5 fallback behind `// M7:` hooks. `gated` = translated and tested,
nothing in the PC code path asks for it. "reads" lists constants, v0/v1 and fog; "(no xfc)" = final combiner
from render states.

| # | EPixelShader | .psh | t0-t3 | reads | status |
|---|---|---|---|---|---|
| 0 | None | - | | |  |
| 1 | Terrain3Layer | terrain3layer | tex tex tex tex | c0 c1 c2 fog v0 v1 (no xfc) | gated |
| 2 | Terrain4Layer | - | | | no Xbox shader |
| 3 | Blur | Blur | tex tex tex tex | c0 c1 c2 c3 | M7 |
| 4 | DepthOfField | DOF | tex tex reg2gb - | v0 | M7 |
| 5 | Mask | Mask | tex tex - - | fog v0 v1 (no xfc) | M7 |
| 6 | MotionBlur | MotionBlur | tex tex - - | v0 | M7 |
| 7 | Refraction | Refraction | tex - - - |  | M7 |
| 8 | BloomSelect | BloomSelect | tex - - - | c0 v0 | M7 |
| 9 | EmissiveSelect | EmissiveSelect | tex - - - | v0 | M7 |
| 10 | EmissiveSelectBlur | EmissiveSelectBlur | tex tex tex tex | c0 c1 c2 c3 v0 | M7 |
| 11 | LuminanceTexIndex | LuminanceTexIndex | tex - - - | c0 fog v1 (no xfc) | M7 |
| 12 | UseTexIndex | UseTexIndex | tex reg2gb - - | fog v1 (no xfc) | M7 |
| 13 | Distortion | Distortion | tex m3x2pad m3x2tex tex |  | M7 |
| 14 | ModulatedDistortion | ModulatedDistortion | tex - - - | c0 v0 | M7 |
| 15 | AdditiveDistortion | AdditiveDistortion | tex - - - | c0 v0 | M7 |
| 16 | SubtractiveDistortion | SubtractiveDistortion | tex - - - | c0 v0 | M7 |
| 17 | DistortionAccum | DistortionAccum | tex tex coord - |  | M7 |
| 18 | SkinShader | SkinShader | tex tex tex - | c0 fog v0 | integrated |
| 19 | SkinShaderSpecLight | SkinShaderSpecLight | tex tex tex - | c0 fog v0 | integrated |
| 20 | TeamSkinShader | TeamSkinShader | tex tex tex tex | c0 c1 fog v0 | integrated |
| 21 | TeamSkinShaderSpecLight | TeamSkinShaderSpecLight | tex tex tex tex | c0 c1 fog v0 | integrated |
| 22 | TeamSkinShaderSpecLightAlpha | TeamSkinSpecLightAlpha | tex tex tex tex | c0 c1 fog v0 | integrated |
| 23 | SkinShaderOpacity | SkinShaderOpacity | tex tex tex tex | c0 fog v0 | integrated |
| 24 | SkinShaderSpecLightOpacity | SkinShaderSpecLightOpacity | tex tex tex tex | c0 fog v0 | integrated |
| 25 | TeamSkinShaderOpacity | TeamSkinShaderOpacity | tex tex tex tex | c0 c1 fog v0 | integrated |
| 26 | TeamSkinShaderSpecLightOpacity | TeamSkinShaderSpecLightOpacity | tex tex tex tex | c0 c1 fog v0 | integrated |
| 27 | TeamShader | TeamShader | tex tex tex tex | c0 fog v0 | integrated |
| 28 | TeamShaderSpecLight | TeamShaderSpecLight | tex tex tex tex | c0 fog v0 | integrated |
| 29 | TeamShaderNoSpec | TeamShaderNoSpec | tex tex - - | c0 fog v0 | integrated |
| 30 | TeamBSPShader | TeamBSPShader | tex tex tex - | c0 fog | integrated |
| 31 | DualDiffuse_NM_Add_NSL | DualDiffuse_NM_Add_NSL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 32 | DualDiffuse_NM_Add_SL | DualDiffuse_NM_Add_SL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 33 | DualDiffuse_NM_Mod_NSL | DualDiffuse_NM_Mod_NSL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 34 | DualDiffuse_NM_Mod_SL | DualDiffuse_NM_Mod_SL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 35 | DualDiffuse_M1_Add_NSL | DualDiffuse_M1_Add_NSL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 36 | DualDiffuse_M1_Add_SL | DualDiffuse_M1_Add_SL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 37 | DualDiffuse_M1_Mod_NSL | DualDiffuse_M1_Mod_NSL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 38 | DualDiffuse_M1_Mod_SL | DualDiffuse_M1_Mod_SL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 39 | DualDiffuse_M1_Blend_NSL | DualDiffuse_M1_Blend_NSL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 40 | DualDiffuse_M1_Blend_SL | DualDiffuse_M1_Blend_SL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 41 | DualDiffuse_M2_Add_NSL | DualDiffuse_M2_Add_NSL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 42 | DualDiffuse_M2_Add_SL | DualDiffuse_M2_Add_SL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 43 | DualDiffuse_M2_Mod_NSL | DualDiffuse_M2_Mod_NSL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 44 | DualDiffuse_M2_Mod_SL | DualDiffuse_M2_Mod_SL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 45 | DualDiffuse_M2_Blend_NSL | DualDiffuse_M2_Blend_NSL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 46 | DualDiffuse_M2_Blend_SL | DualDiffuse_M2_Blend_SL | tex tex tex - | fog v0 v1 (no xfc) | integrated |
| 47 | DualDiffuse_NM_Add_NSL_SM | DualDiffuse_NM_Add_NSL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 48 | DualDiffuse_NM_Add_SL_SM | DualDiffuse_NM_Add_SL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 49 | DualDiffuse_NM_Mod_NSL_SM | DualDiffuse_NM_Mod_NSL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 50 | DualDiffuse_NM_Mod_SL_SM | DualDiffuse_NM_Mod_SL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 51 | DualDiffuse_M1_Add_NSL_SM | DualDiffuse_M1_Add_NSL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 52 | DualDiffuse_M1_Add_SL_SM | DualDiffuse_M1_Add_SL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 53 | DualDiffuse_M1_Mod_NSL_SM | DualDiffuse_M1_Mod_NSL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 54 | DualDiffuse_M1_Mod_SL_SM | DualDiffuse_M1_Mod_SL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 55 | DualDiffuse_M1_Blend_NSL_SM | DualDiffuse_M1_Blend_NSL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 56 | DualDiffuse_M1_Blend_SL_SM | DualDiffuse_M1_Blend_SL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 57 | DualDiffuse_M2_Add_NSL_SM | DualDiffuse_M2_Add_NSL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 58 | DualDiffuse_M2_Add_SL_SM | DualDiffuse_M2_Add_SL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 59 | DualDiffuse_M2_Mod_NSL_SM | DualDiffuse_M2_Mod_NSL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 60 | DualDiffuse_M2_Mod_SL_SM | DualDiffuse_M2_Mod_SL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 61 | DualDiffuse_M2_Blend_NSL_SM | DualDiffuse_M2_Blend_NSL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 62 | DualDiffuse_M2_Blend_SL_SM | DualDiffuse_M2_Blend_SL_SM | tex tex tex tex | fog v0 v1 (no xfc) | integrated |
| 63 | DiffuseOpacitySpecularMask | DiffuseOpacitySpecularMask | tex tex - - | fog v0 | integrated |
| 64 | DiffuseOpacitySpecularMaskSpecLight | DiffuseOpacitySpecularMaskSpecLight | tex tex - - | fog v0 | integrated |
| 65 | DiffuseOpacitySpecularMask2 | DiffuseOpacitySpecularMask2 | tex tex tex - | fog v0 | integrated |
| 66 | DiffuseOpacitySpecularMaskSpecLight2 | DiffuseOpacitySpecularMaskSpecLight2 | tex tex tex - | fog v0 | integrated |
| 67 | ShenuSkin | ShenuSkin | tex tex tex - | fog | integrated |
| 68 | EmissiveSkinShader | EmissiveSkinShader | tex tex tex tex | c0 c1 fog v0 | integrated |
| 69 | EmissiveSkinShaderSpecLight | EmissiveSkinShaderSpecLight | tex tex tex tex | c0 c1 fog v0 | integrated |
| 70 | EmissiveSkinShaderSpecLightAlpha | EmissiveSkinShaderSpecLightAlpha | tex tex tex tex | c0 c1 fog v0 | integrated |
| 71 | EmissiveShaderDiffuseOnly | EmissiveDiffuseOnly | tex tex - - | c0 c1 fog v0 | integrated |
| 72 | EmissiveShaderSpec | EmissiveShader | tex tex tex tex | c0 c1 fog v0 | integrated |
| 73 | EmissiveShaderSpecLight | EmissiveShaderSpecLight | tex tex tex tex | c0 c1 fog v0 | integrated |
| 74 | EmissiveSpecular | EmissiveSpecular | tex tex tex - | c0 c1 fog v0 | integrated |
| 75 | EmissiveConstColor | EmissiveConstColor | - - - - | c0 fog | integrated |
| 76 | Projector_Add_1 | Proj_Add_1 | tex tex - - | v0 | gated |
| 77 | Projector_Add_2 | Proj_Add_2 | tex tex tex - | v0 v1 | gated |
| 78 | Projector_Add_3 | Proj_Add_3 | tex tex tex tex | v0 v1 | gated |
| 79 | Projector_AlphaBlend_1 | Proj_AlphaBlend_1 | tex tex - - | v0 | gated |
| 80 | Projector_AlphaBlend_2 | Proj_AlphaBlend_2 | tex tex tex - | v0 v1 | gated |
| 81 | Projector_AlphaBlend_3 | Proj_AlphaBlend_3 | tex tex tex tex | v0 v1 | gated |
| 82 | Projector_Modulate_1 | Proj_Modulate_1 | tex tex - - | c0 v0 | gated |
| 83 | Projector_Modulate_2 | Proj_Modulate_2 | tex tex tex - | c0 v0 v1 | gated |
| 84 | Projector_Modulate_3 | Proj_Modulate_3 | tex tex tex tex | c0 v0 v1 | gated |
| 85 | Projector_Add_Clip | Proj_Add_Clip | tex tex - kill | v0 | gated |
| 86 | Projector_AlphaBlend_Clip | Proj_AlphaBlend_Clip | tex tex - kill | v0 | gated |
| 87 | Projector_Modulate_Clip | Proj_Modulate_Clip | tex tex - kill | c0 v0 | gated |
| 88 | MaskedLuminance | MaskedLuminance | tex tex - - | c0 fog | M7 |
| 89 | ShadowBlur | ShadowBlur | tex tex tex tex | c0 c1 c2 c3 | M7 |
| 90 | UseTexIndexMasked | UseTexIndexMasked | tex tex reg2gb - | fog v1 (no xfc) | M7 |
| 91 | PlayerCam | PlayerCam | tex - - - | fog | M7 |
| 92 | PlayerCamNoise | PlayerCamNoise | tex tex tex - | fog | M7 |
| 93 | PlayerCamTwoNoise | PlayerCamTwoNoise | tex tex tex tex | fog | M7 |
| 94 | DiffuseOpacity | DiffuseOpacity | tex tex - - | fog v0 | integrated |
| 95 | ColorRemap | ColorRemap | tex m3x2pad m3x2tex - | c0 fog v1 (no xfc) | M7 |
| 96 | ColorRemapMasked | ColorRemapMasked | tex m3x2pad m3x2tex tex | c0 fog v1 (no xfc) | M7 |
| 97 | DepthSwizzleColor | DepthSwizzleColor | tex - - - |  | M7 |
| 98 | ColorRemapGrayScale | ColorRemapGrayScale | tex tex tex tex | c0 c1 fog v1 (no xfc) | M7 |

Counts: 100 sources translated and tested (97 mapped to enum values + 3 unmapped); **59 integrated**; **25
wait for M7**; **13 gated** (12 projector shaders: the engine batches projectors into `UProjectorMultiMaterial`
only under `_XBOX`, and `SetProjectorMultiMaterial` is an `_XBOX` body, so PC keeps drawing projectors with
`SetProjectorMaterial` as at M5; `PS_Terrain3Layer`: see Deferred). Problematic: none found by the tests;
one without a second source of truth (`ColorRemapGrayScale`).

M7 list by enum: `PS_Blur, PS_DepthOfField, PS_Mask, PS_MotionBlur, PS_Refraction, PS_BloomSelect,
PS_EmissiveSelect, PS_EmissiveSelectBlur, PS_LuminanceTexIndex, PS_UseTexIndex, PS_Distortion,
PS_ModulatedDistortion, PS_AdditiveDistortion, PS_SubtractiveDistortion, PS_DistortionAccum,
PS_MaskedLuminance, PS_ShadowBlur, PS_UseTexIndexMasked, PS_PlayerCam, PS_PlayerCamNoise,
PS_PlayerCamTwoNoise, PS_ColorRemap, PS_ColorRemapMasked, PS_DepthSwizzleColor, PS_ColorRemapGrayScale`.
Hooks (`grep -a -n "M7:" Source/UC2GLDrv/Src/*`): `UC2GL_IsM7PixelShader` and the unique vertex shaders in
`BuildVertexShaderSource` (`UC2GLShaders.cpp`), the fallback branch of `SetPixelShaderMaterial`
(`UC2GLMaterialState.cpp`), plus M5's existing M7 stubs. The engine's `_XBOX` post-process gates are untouched.

### Engine-side and configuration changes

- `Source/Engine`, `Source/Core`: **none**.
- `Engine/Config/Default.ini`: two lines in our own `[UC2GLDrv.UC2GLRenderDevice]` section
  (`UsePixelShaders=True`, `PrecompileVertexShaders=True`; both are also the compiled defaults).
  `Engine/Config/README.md`: one table row. (That folder is shared with the web work: the edit is this small.)
- `Source/UC2GLDrv/CMakeLists.txt`: `Shaders/` added to the include path. Root `CMakeLists.txt`: untouched.

### Defects found in Epic's shaders and handlers (behaviour kept, not fixed)

1. **One-call lag in the skin handlers.** `SetPixelShaderMaterial` copies `InShader->PixelShaderType` into the
   pass *before* the handler runs; `SetMatState_PS_SkinShader` then writes `pMaterial->PixelShaderType = Base
   (+1 with bUseLightingForSpecular)` but not `InMaterialState->PixelShader`. The first time a `PSSkinShader`
   with `bUseLightingForSpecular` is set it is drawn with `SkinShader`, afterwards with `SkinShaderSpecLight`
   (seen in our log: both programs get linked for the same vertex shader). Same code on Xbox.
2. **Stage compaction.** `SetPixelShaderMaterial` packs the non-NULL `TextureStage0..3` into consecutive
   stages. A material whose stage 1 or 2 is NULL (a `PSSkinShader` without specular but with a rim light,
   say) gets its later textures one stage early, where the shader reads them as something else.
3. **`ColorRemapGrayScale.h` is missing** from the tree though included and used (S1).
4. `PSEmissiveSkinShader.uc` sets `BaseShaderType=PS_SkinShader` (unused by its own handler);
   `FSkinShaderOpacity` and three sibling PC shader classes register themselves as `PS_SkinShader`
   (`D3DRenderDevice.cpp` 3837-3914): PC-only, irrelevant to us, noted.
5. `SetMatState_PS_SkinShader` and the team/emissive handlers pass 16.0 as a pixel-shader constant ("a crazy
   constant"); on the hardware constants are 8-bit [0,1], so it is 1.0 and `xmma_x4 (t.a*c + t.a*c)` is an
   8x gain: the "all or nothing" mask is all only where the mask alpha is at least 1/8. Intentional or not,
   reproduced (the generated shaders clamp constants).
6. No shipped package contains a `PSDualDiffuse` (or `PSShenuSkin`, `PSEmissiveConstColor`,
   `PSTeamSkinShaderOpacity`) material: 37 of the 59 integrated shaders are unreachable from content.

## S4. Verification on content (2026-10-04, Xvfb + Mesa llvmpipe, run tree `run/m6`)

### What the content uses

`python3 tools/dev/ps-census.py <WorkUC2>/assets/uc2` (name, import and export tables only): objects per class
in the canonical tree: `PSEmissiveShader` 188, `PSSkinShader` 94, `PSEmissiveSkinShader` 81, `PSTeamSkinShader`
68, `PSDistortionMaterial` 42 (M7), `PSEmissiveSpecular` 27, `PSAdditiveDistortion` 15 (M7), `PSTeamShader` 13,
`PSModulatedDistortionMaterial` 12 (M7), `PSDiffuseOpacitySpecularMask` 10, `PSBlurMaterial` 5 and
`PSMotionBlurMaterial` 5 (M7), `PSSkinShaderOpacity` 5, `PSDiffuseOpacity` 4, `PSPlayerCamMaterial` 3 (M7),
`PSBackBufferEffect`, `PSColorRemapMaterial`, `PSSubtractiveDistortion` 1 each (M7). **No `PSDualDiffuse`,
`PSShenuSkin`, `PSEmissiveConstColor` or `PSTeamSkinShaderOpacity` object exists in any package**, in
`assets/uc2` or in the pristine tree. The tool also lists, per map, the pixel-shader materials it imports.

### How the pictures are made

`tools/dev/run-uc2-shot.sh <tree> <name> <seconds> <fire-at> <map> [args]`: the client under
`bin/capped 2G` + `xvfb-run`, Fire injected through the X test extension, the engine's own screenshots
converted to `logs/<tree>/<name>-N.png`, then the linked programs, the GL error line and the Critical count.
`UC2_RES=1280x720` sets the resolution, `UC2_PRECMD="set UCDeathmatch NumPlayers 0"` pins the spawn as the
wine reference script does (bound to F1, pressed before Fire). A/B from one camera in one run:
`-glshot=56,64 -glpstoggle=60` = picture 0 with pixel shaders on, picture 1 off.

```
UC2_RES=1280x720 UC2_PRECMD="set UCDeathmatch NumPlayers 0" tools/dev/run-uc2-shot.sh m6 deadbolt-720-ab 100 34 DM-Deadbolt -glerrors -glshot=42,52 -glpstoggle=47
UC2_RES=1280x720 UC2_PRECMD="set UCDeathmatch NumPlayers 0" tools/dev/run-uc2-shot.sh m6 octane-ab 125 38 CTF-Octane -glerrors -glshot=56,64 -glpstoggle=60
... same for DM-AxonCompressor, CTF-ViperPit, CTF-Transistor (names ab-<map>), and with -gldualdiffuse=12
```

### Pictures (all in `<WorkUC2>/logs/m6/`; `-0` = pixel shaders on, `-1` = off, same camera)

Every run below: 0 Critical, 0 GL errors, 0 programs failed.

| Files | Shaders linked in the run | What is on them |
|---|---|---|
| `deadbolt-720-ab-0/1.png`, `deadbolt-720-skin-oracle-on-off.png` (wine, on, off) | SkinShader, SkinShaderSpecLight, EmissiveShader, EmissiveDiffuseOnly | **Player skin (`PSSkinShader`)** at the same spawn as `logs/oracle/DM-Deadbolt.png`. On: the helmet horns are grey metal with white highlights, the shoulder pieces pale and reflective, as in the wine picture. Off (M5): flat brown, no reflection. Mean absolute difference to the wine picture over the character region (330,440)-(760,720): **6.3 of 255 with shaders on, 16.7 off**; whole frame 1.8 / 3.3 (the rest is animation phase). PC drew this material with its own DX8 shader, so this is the "at least as correct as PC" check, and it holds. |
| `octane-ab-0/1.png`, `octane-ab-on-off.png`, `octane-teamskin-oracle-on-off.png`, `octane-doorframe-oracle-on-off.png` | TeamSkinShader, TeamSkinShaderSpecLight, SkinShaderSpecLight, TeamShader, EmissiveShader, EmissiveDiffuseOnly | **Team skin (`PSTeamSkinShader`, Xbox-only)**, red team, same spawn as `logs/oracle/CTF-Octane.png`. On: the skin is lit normally with specular, and only the masked markings (back glyphs, belt) glow team red. Off: plain brown skin, no team colour. PC (wine): the whole body tinted orange-red, which is PC's stage-0 fallback, not the shader; so here ours intentionally differs from PC. It matches what `TeamSkinShader.psh` describes (team colour modulates the masked, emissive parts only); **there is no Xbox picture to check it against**. Door frame behind (static meshes with `PSEmissiveShader`/`PSTeamShader`, which PC drew with DX8 shaders): difference to the wine picture over (420,160)-(860,400) **0.39 of 255 on, 5.76 off**: the three crops show wine and ours-on indistinguishable, ours-off flatter with yellowish lamps and a missing lit panel. |
| `ab-DM-AxonCompressor-0/1.png`, `...-on-off.png`, `...-glass-crop.png` | DiffuseOpacitySpecularMask2, SkinShader(SpecLight), EmissiveShader, EmissiveDiffuseOnly | **Specular mask (`PSDiffuseOpacitySpecularMask`)**: the wire-glass pane on the right. On: translucent, the machinery and green lamps behind it visible through it, a faint sheen. Off: its diffuse texture drawn as a grey film, lamps dim. Also the pickup base ring glows cyan and the weapon pickup has highlights with shaders on. Looks right to me; no PC picture of this spot was made (PC had a DX8 shader for this family). |
| `ab-CTF-ViperPit-0/1.png`, `...-on-off.png` | DiffuseOpacitySpecularMask2, EmissiveSpecular, TeamSkinShader(SpecLight), EmissiveShader, EmissiveDiffuseOnly | **`PSEmissiveSpecular`** trims: the red strips on the pillar base and along the wall are brighter and saturated with shaders on; team skin as in Octane. The difference is small in this view (differences concentrate on the character). |
| `ab-CTF-Transistor-0/1.png`, `...-on-off.png` | EmissiveSkinShader, EmissiveShader, TeamSkinShader(SpecLight), SkinShaderSpecLight | **Emissive skin (`PSEmissiveSkinShader`, Xbox-only)** is linked and drawn here (the map's `Transistor_Emissive_Skin*` materials) and the door-frame lamps (emissive) are white and bright with shaders on. **I could not tell which object in the view carries the emissive-skin material**, so this picture proves the path runs clean, not that it looks right. Weapon and character-head emissive skins (the bulk of the 81 objects) did not come into view in any run (`NextWeapon` after the start changed nothing visible). |
| `dualdiffuse-DM-AxonCompressor-0/1.png`, `...-on-off.png` | DualDiffuse_M1_Blend_NSL (3 programs) + the above | **DualDiffuse**: not from content (none exists). `-gldualdiffuse=12` draws every `PSEmissiveShader` material that has diffuse and specular textures as a `PSDualDiffuse` (blend, mask 1) built from those two textures: the machine and the column on the left turn into the two textures blended by the first one's alpha. This shows `UPSDualDiffuse::PreRender`, the DualDiffuse handler and a DualDiffuse program working on real geometry with 0 errors; whether a real DualDiffuse material looks right cannot be shown. The numeric tests of S1 are the evidence for the 32 variants. |
| `deadbolt-ab-0/1.png`, `deadbolt-ab-crop.png` | as the first row | first pair, 640x480, unpinned spawn: the player from behind, on: green emissive details and specular; off: flat. |

Not shown by any picture: `PSSkinShaderOpacity` / multi-pass skins (5 objects), `PSDiffuseOpacity` (4),
`PSTeamShader` on its own (it is linked in Octane; I did not isolate its object), anything in fog (no run had
distance fog on a pixel-shader material that I could identify), cube-map variants (every program linked in
these runs was variant 0: the specular maps in view are 2D with a camera-space reflection source).

### Gate, literally

(a) `bin/capped 1G 120 -- python3 tools/psh2glsl.py --selftest`:
`psh2glsl selftest: 100 shaders, 99 compared with their definition words (6336 cases, max difference 0.00e+00),
1 without a .h, 0 failed, 0 lint warnings; 7 synthetic semantics checks, 0 failed`.
`bin/capped 2G 300 -- xvfb-run -a -s "-screen 0 640x480x24" python3 tools/psh2glsl.py --gputest`:
`pshgpu: 107 shaders rendered, 6848 cases, 0 outside the tolerance of 1.00 8-bit steps (worst 0.500), 420
program variants linked, GLSL ES 1.00 compile 107/107, 0 shaders failed`. Tolerance: 1.0 step of the 8-bit
framebuffer against the interpreter's unrounded value.

(b) `cmake --preset native && cmake --build build-native -j12`: `[100%] Built target UCC`, `[100%] Built
target SDLLaunch` (`uc2.bin`). `source ../env.sh && cmake --preset wasm && cmake --build build-wasm -j6`: exit
0, `libCore.a libEngine.a libGUI.a libIpDrv.a libUCElements.a libUCGameplay.a libUWeb.a libUnrealGame.a`
(the driver stays excluded there).

(c) 60 s runs, pixel shaders on, fresh run tree (`tools/dev/run-uc2-shot.sh m6 gate-menu 60 0 '' -glerrors`,
`... gate-deadbolt 60 30 DM-Deadbolt -glerrors`):
- menu: 0 Critical, `frame 2400, 0 GL errors so far`, `programs: 0 linked, 0 failed; fragment shaders 59
  compiled, 0 failed; vertex shaders 405 compiled (405 from VertexShaderConfigs.ini), 0 failed; 0 program
  draws` (the menu has no pixel-shader material).
- DM-Deadbolt (match started): 0 Critical, `frame 2400, 0 GL errors so far`, `programs: 8 linked, 0 failed;
  fragment shaders 59 compiled, 0 failed; vertex shaders 409 compiled (405 from VertexShaderConfigs.ini), 0
  failed; 69557 program draws, 0 fixed-function draws of pixel-shader passes`.

(e) from `run/m6/System`, each through `bin/capped 2G`: `./ucc-bin verifyclasses` -> exit 0, `All native class
layouts match`; `timeout 20 ./ucc-bin server DM-Deadbolt -log` -> `Game class is 'UCDeathmatch'`, `Bringing
Level DM-Deadbolt.MyLevel up for play (0)...`, still running at the time limit; `./uc2.bin -nullrenderer -log`
-> `Render device: Engine.NullRenderDevice (640x480 windowed)`, menu level up. Logs: `logs/m6/gate-*.out`.

## Deferred

- **M7 (25 shaders)**: listed in S3; hooks `// M7:`.
- **Projector shaders (12)**: need the engine's `_XBOX` projector batching and `SetProjectorMultiMaterial`
  (an `_XBOX` body in the material compiler) plus `ProjectorAtten.vsh`/`TexSourceTexKill3.vsh`; PC's
  multi-pass projectors keep working as at M5. Not an M7 item in PLAN.md: needs a decision.
- **`PS_Terrain3Layer`**: the driver's terrain compiler already asks for it when the engine chooses
  `RM_CombinedWeightMap`, which it does only if `GetRenderCaps()->MaxSimultaneousTerrainLayers` is 3 (ours
  says 1, as at M5). Enabling it = caps 3 + the three mask constants of the `_XBOX` lines in
  `SetTerrainMaterial` + un-gating it in `UC2GL_IsEngineGatedPixelShader`; the generic vertex shader covers
  its texture coordinates (camera-space position source, m4x3). Not done: it changes how every terrain map
  is drawn and I had no budget left to verify 20 terrain maps. Hook: `UUC2GLRenderDevice::GetRenderCaps`.
- GLSL ES / GL4ES (M11): fragment bodies compile as ES 1.00; the vertex shaders use nothing beyond ES 1.00
  but were not compiled as ES; alpha test stays fixed-function on desktop (`UC2_ALPHATEST` exists in the
  fragment shaders, the driver does not use it yet).
- Performance: uniforms are uploaded and the program is bound and unbound per draw; no state caching.

## Unsure / inferred (not verified by running)

- Fog factor 1 with fog off; opaque black for a stage the material leaves empty; the values of unwritten
  vertex outputs; ambient-only colour for unlit, uncoloured geometry (the Xbox formula; PC gives white).
- 8-bit combiner precision is not modelled (see S1).
- Texture-coordinate sets above 3 on a pixel-shader pass fall back to fixed function for that draw (logged
  once); not seen in the runs.
- Pixel-shader constants are device-global as in D3D: a multi-pass material's passes share the last values
  set. Same as Epic's code; not seen to matter.
- With `UsePixelShaders=False` the team skin is untinted while PC's fallback tints it (wine picture): M5's
  fallback is not exactly PC's for this material. Not investigated (it is the M5 path).
- No Xbox reference exists here (no emulator): for every Xbox-only material the evidence is the numeric
  tests plus the reading of the handlers, not a picture comparison.
