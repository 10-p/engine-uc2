# O-9: the NVIDIA fixed-function gap: notebook

Branch `o9-nvidia`, worktree `<WorkUC2>/engine-uc2-o9` (base: main `d6d7e33`). Run trees `<WorkUC2>/run/o9*`,
logs and pictures `<WorkUC2>/logs/o9`. Brief: PLAN.md, task O-9; the finding: `analysis/fx-fixes.md`, step 6
("Side finding on the NVIDIA GPU"). Every run goes through `<WorkUC2>/bin/capped` (game 2G, builds 6G); GPU runs
headless through SDL's offscreen video driver (EGL), one at a time, nothing on the owner's display.

## Step 0 (2026-10-07): reading; what the hardware and Epic's code say

### The two GL implementations on this machine (`logs/o9/eglinfo-surfaceless.txt`, `logs/o9/glxinfo-mesa.txt`)

| | NVIDIA Quadro P620, driver 580.178.04, compatibility profile (EGL surfaceless) | Mesa 26.0.8 llvmpipe, compatibility profile (Xvfb) |
|---|---|---|
| `GL_MAX_TEXTURE_UNITS` (fixed-function combiner units) | **4** | 8 |
| `GL_MAX_TEXTURE_COORDS` | 8 | 8 |
| `GL_MAX_TEXTURE_IMAGE_UNITS` (GLSL) | 32 | 32 |
| `GL_ATI_texture_env_combine3` (`MODULATE_ADD_ATI` etc.) | **no** | yes |
| `GL_NV_texture_env_combine4` | yes | yes |
| `GL_ARB_texture_env_crossbar`, `_dot3`, `_combine` | yes | yes |
| `GL_NV_register_combiners` | yes | no |

### What our driver does with that (main `d6d7e33`)

- `UUC2GLRenderDevice::SetRes` (`UC2GLRenderDevice.cpp`): `NumTextureUnits = clamp(min(GL_MAX_TEXTURE_UNITS,
  MaxTextureUnits=8), 2, 8)`; `DeviceCaps.MaxSimultaneousTextures = MaxTextureBlendStages = NumTextureUnits`;
  `DeviceCaps.TextureOpCaps = combine3 ? (MODULATEALPHA_ADDCOLOR | MULTIPLYADD) : 0`. So: Mesa 8 stages and the
  two caps; NVIDIA 4 stages and no caps; the web (gl4es emulates both combiner extensions) 8 and the caps.
- `TranslateStageOp` (`UC2GLRenderInterface.cpp`): the seven three-operand D3D ops become `GL_MODULATE_ADD_ATI`
  with combine3; without it, "keep the modulation", i.e. **the added term is dropped** (`a*b+c` -> `a*b`).

### What the material compiler (Epic's `D3DMaterialState.cpp`, carried over as `UC2GLMaterialState.cpp`) emits

Colour/alpha ops, every site (grep of `D3DTOP_` in the compiler, the PS state handlers, post-process, movies):
SELECTARG1, SELECTARG2, MODULATE, MODULATE2X (directly and through `DUMMY_MODULATE2X`), MODULATE4X, ADD,
SUBTRACT, BLENDDIFFUSEALPHA, BLENDTEXTUREALPHA, BLENDFACTORALPHA, BLENDCURRENTALPHA, MODULATEALPHA_ADDCOLOR,
MULTIPLYADD; BUMPENVMAP / BUMPENVMAPLUMINANCE only under `BUMPMAP_ENABLE` (not defined). Arguments: CURRENT,
TEXTURE, DIFFUSE, TFACTOR, with COMPLEMENT and ALPHAREPLICATE; TEMP (+ `ResultArg`) only in the terrain path
that needs `D3DPMISCCAPS_TSSARGTEMP`, which no GL path reports.

The three-operand sites, and what each becomes on NVIDIA today:

| Site (`UC2GLMaterialState.cpp`) | Op | NVIDIA today |
|---|---|---|
| 771 specular stage, `UseDiffuseModulate` | MULTIPLYADD `CURRENT + DIFFUSE*TEXTURE` (unconditional) | approximated: `DIFFUSE*TEXTURE`, the base colour is lost |
| 785 specular stage, `UseConstantSpecularity` | MULTIPLYADD `CURRENT + TFACTOR*TEXTURE` (unconditional) | approximated: `TFACTOR*TEXTURE`, base lost |
| 791 specular stage, per-pixel specularity | MODULATEALPHA_ADDCOLOR `CURRENT + CURRENT.a*TEXTURE`, **only if the cap is set**, else Epic's legacy fallback `DUMMY_MODULATE2X` = `2*CURRENT*TEXTURE` | the legacy fallback: env map multiplied into the diffuse (the "orange metal" pillar, fx-fixes step 6: stage 1 colour op 5) |
| 884 `AddToCurrent` bitmap (a scion addition) | MODULATEALPHA_ADDCOLOR (unconditional) | approximated: `CURRENT.a*TEXTURE`, base lost |
| 1094 combiner `CO_Add_With_Mask_Modulation` | MODULATEALPHA_ADDCOLOR if the cap, else ADD | Epic's fallback ADD |
| 3196 / 3262 terrain, several layers per pass | MODULATEALPHA_ADDCOLOR / MULTIPLYADD+TEMP | not reached: `GetRenderCaps` reports one terrain layer per pass |

### Epic's own answer for 4 stages (the brief's option c)

`D3DRenderDevice.cpp` 418-422 (`CheckDeviceCaps`, PC **and** Xbox):

    DeviceCaps8.MaxSimultaneousTextures = Min<DWORD>(4,DeviceCaps8.MaxSimultaneousTextures);
    DeviceCaps8.MaxTextureBlendStages   = Min<DWORD>(4,DeviceCaps8.MaxTextureBlendStages);
    if( (DeviceCaps8.MaxSimultaneousTextures != 4) || (DeviceCaps8.MaxTextureBlendStages != 4) )
        appErrorf( TEXT("Card not suitable for creating content for UC2004") );

So UC2's material compiler only ever ran with **exactly 4 texture stages and 4 blend stages**, on the Xbox and
on Epic's PC build alike (the PC build refuses any other card). `SetEmulationMode(HEM_XBox)` sets 4/4 too. There
is no multipass answer for a missing *op*: the only device-dependent choices are the two `TextureOpCaps` tests
above, whose else-branches are the GeForce1-era fallbacks, and `DUMMY_MODULATE2X` (MODULATE on Voodoo3). Every
card UC2's PC build accepted (4 simultaneous textures: GeForce3/4 Ti, Radeon 8500 and later) and the Xbox's NV2A
have MODULATEALPHA_ADDCOLOR and MULTIPLYADD. So "as Epic's D3D path specifies" = **4 stages, the true ops**.

Consequence: NVIDIA's 4 units are Epic's number; it is Mesa's (and the web's) **8** that differs from Epic.
Whether 8 stages change how any shipped material compiles is measured below before deciding (a census).

Next: build (`logs/o9/build-0.log`), a material census switch, then Mesa runs with 8 units + combine3 (today's
Mesa), 4 units + combine3 (Epic's caps), 4 units without combine3 (today's NVIDIA, emulated on Mesa).

## Step 1 (2026-10-07): the program path, combine4 for the texture environment, the numeric test

Built: `capped 6G 3600 -- cmake --preset native && cmake --build build-native -j6` (`logs/o9/build-0.log`, main's
code: `run/o9/System/uc2-before.bin`), then incremental builds (`build-1.log`, `build-2.log`). Run tree `run/o9`
(`tools/dev/mkrun-uc2.sh build-native o9`).

What was written (all in `Source/UC2GLDrv`):

- `Src/UC2GLFFProgram.cpp` (new): a fixed-function pass's texture stages as a GLSL fragment shader, linked
  **without a vertex shader** (the fixed-function vertex stage stays: transform, lighting, texgen, texture
  matrices, fog coordinate). Generated per combination (ops, used args, 2D/cube target, colour sum, fog), cached.
  D3D8's definitions, every stage saturated; 2D lookups `texture2DProj` (= GL's s/q,t/q), cube `textureCube`;
  colour sum and linear fog done in the shader (GL does not apply them to a fragment shader). Stage LOD bias:
  the unit's `TEXTURE_FILTER_CONTROL` bias where the unit has a texture environment, in the lookup otherwise.
- `UC2GLRenderDevice.cpp`: `UpdateTextureCaps()`: with programs, stages = min(MaxTextureUnits=8,
  GL_MAX_TEXTURE_COORDS, GL_MAX_TEXTURE_IMAGE_UNITS) and `TextureOpCaps` = MODULATEALPHA_ADDCOLOR|MULTIPLYADD;
  without, GL_MAX_TEXTURE_UNITS and the caps only with combine3 or combine4. Switches: ini
  `FixedFunctionPrograms` (default True), `-glffprograms`/`-glnoffprograms`, console `FFPROGRAMS [0|1]`,
  `-glfftoggle=<s>,...`; masks `-glnocombine3`, `-glnocombine4`, `-glunits=N`; `-glffsource` (log generated
  sources); `-glcombinertest` / console `COMBINERTEST`. Texture-environment state (enables, TexEnv) only on units
  below GL_MAX_TEXTURE_UNITS (NVIDIA 4); texgen/matrices/arrays on all 8 coordinate sets.
- `UC2GLRenderInterface.cpp`: `TranslateStageOp` + `ToCombine4`: without combine3 but with
  `GL_NV_texture_env_combine4`, a three-operand op is `Arg0*Arg1 + Arg2*Arg3` (GL_COMBINE4_NV, GL_ADD) with the
  other half of the stage brought into the same form (REPLACE/MODULATE/ADD/ADD_SIGNED/INTERPOLATE). So the
  texture-environment path (the comparison switch) is also right on NVIDIA, with its 4 stages. `DrawPrimitive`
  binds the program after `CommitStreams`/`CommitTexCoords`; `CommitStreams` records the colour sum.
- `Src/UC2GLCombinerTest.cpp` (new): the numeric test (below). `Src/UC2GLPixelTrace.cpp`: `-glmatcensus`.

### The numeric test (`-glcombinertest`)

34 cases (now 35 with `fog-front`): every op the compiler emits and the others the translation knows (25 op cases incl.
COMPLEMENT/ALPHAREPLICATE, MULTIPLYADD on colour and alpha, the four compiler sites of the three-operand ops),
a 3-stage chain, 8 stages in one pass, colour sum, fog, a projected lookup (q=2), cube face selection, LOD bias
on stage 0 and stage 5. 4x4 random RGBA textures (with 0/128/255 texels), three diffuse/specular/tfactor sets,
16 pixels each, into a 4x4 framebuffer of its own; CPU = D3D8 SDK definitions (written separately from the
GLSL). Tolerance 2/255.

Runs (menu, `-glnoaa -glerrors -glcombinertest -glexec=25:exit`, `capped 2G 120`; GPU: `SDL_VIDEODRIVER=offscreen`):

| Path | Mesa llvmpipe | NVIDIA P620 |
|---|---|---|
| fragment programs (new default) | 34/34, largest error 0.50 (`ct-mesa-prog.out`) | 33/34 first: **fog FAILED** (see below); after the fix 34/34, largest 0.55 (`ct-gpu-prog2.out`) |
| texture environment, combine3 | 34/34 (`ct-mesa-ff-c3.out`) | (no combine3) |
| texture environment, combine4 | 34/34, `-glnocombine3` (`ct-mesa-ff-c4.out`) | 32/32, 2 SKIP (8 stages, stage-5 bias: 4 units) (`ct-gpu-ff-c4.out`) |
| texture environment, neither = **main's behaviour** | 21 pass, **11 FAIL**, 2 SKIP with `-glnocombine3 -glnocombine4 -glunits=4` (`ct-mesa-ff-none4.out`) | 21 pass, **11 FAIL**, 2 SKIP with `-glnocombine4` (`ct-gpu-ff-none.out`) |

The 11 failures before the change are exactly the seven three-operand ops (MULTIPLYADD x3, the four
MODULATE*_ADD*, ADDSMOOTH, BLENDTEXTUREALPHAPM) and the chain that uses one: errors up to 255/255 (the added term
is dropped). GL errors 0 in every run.

Found by the test on NVIDIA: with a fragment shader and the fixed-function vertex stage, **NVIDIA's
`gl_FogFragCoord` is the signed eye-space z** (Mesa: |z|); GL's own fog uses |z|. The shader now takes
`abs(gl_FogFragCoord)`; a second fog case with the quad at +z (`fog-front`: the driver's modelview is D3D camera
space, z forward) was added so both signs are covered.

Next: commit; census (which shipped materials compile differently with 4/8 stages and with/without the caps);
pictures (DM-Azure pillar and cobra, NVIDIA before/after, Mesa); the twin; the script `tools/dev/o9-nvidia.sh`.
