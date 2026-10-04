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
