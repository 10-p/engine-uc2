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

## Step 2 (2026-10-07): census of every map's materials; the stage count; pictures; the script

### Census (`tools/dev/o9-census.sh`, console `GLMATCENSUS`)

Every `UMaterial` in memory after a map has loaded (pixel-shader, projector, terrain, particle and movie materials
skipped) is compiled lit and unlit with three sets of caps, nothing drawn: **A** 8 stages + the three-operand
ops (Mesa and the web before O-9), **B** 4 stages + the ops (Epic's D3DDrv and the Xbox), **C** 4 stages without
(NVIDIA before O-9). Mesa (Xvfb), three maps at a time, run trees `run/o9c0..2`; per-map summary in
`logs/o9/census-summary.txt`, the differing materials with their three compilations in `logs/o9c*/census-<map>.out`.

DM-Azure (`logs/o9/census-DM-Azure.out`): 583 materials; **B/C differ for 31** (lit+unlit counted separately:
the cobra statue `NKMesh_Statues_Cobra01_COMB` (CO_Add_With_Mask_Modulation: MODULATEALPHA_ADDCOLOR
`tex + tex.a*current` -> ADD) and `_Sd`, the Horus statue, the wooden pillars `T_NKMesh_Deco_Wood0*_SD` (specular
stage MODULATEALPHA_ADDCOLOR -> MODULATE2X), iced walls/floors/trim, the invisibility pickup, ...); **A/B differ
for 2**: `T_Liquid_Falls_Water01_SD01` (the cobra's waterfall) and `T_FX.Beams.FX_Beams_Raiden_Body_Shader`:
with 8 stages the compiler puts the specular/second layer into the same pass (5 stages), with Epic's 4 it uses a
second pass with framebuffer blending (`sa,1` resp. `dc,0`). The formulas differ (the second pass blends with
what the first left in the framebuffer), so **8 stages is not Epic's/Xbox's picture for those materials**.

### Decision on the stage count: 4, as Epic's D3DDrv

`MaterialStages` (new key, default 4, `-glstages=N`): the compiler is given min(path's units, MaxTextureUnits,
MaterialStages). Reason: UC2's D3DDrv clamps both stage counts to 4 and refuses to run otherwise
(`D3DRenderDevice.cpp` 418-422, PC and Xbox); `MaxTextureUnits=8` came from Epic's UT2003-era *OpenGLDrv*
section in `Default.ini`, not from UC2's D3D path. With 4 everywhere the compile is identical on NVIDIA (programs
or texture environment with combine4), Mesa and the web, and equal to Epic's. The fragment programs can still do 8
(the test's 8-stage case passes with them on NVIDIA); the compiler just is not asked to. Old inis without the key
get the compiled default. This changes Mesa's and the web's picture for the A/B materials only (to Epic's split).

### First pictures on NVIDIA (`tools/dev/o9/azure.scen` through `fx-owner.sh`, `UC2_GPU=1`)

`logs/o9/fx-azure-before-gpu-pp.png` (main's binary) vs `fx-azure-gpu-pp.png` (this branch), post-processing on
as the owner plays: the pillar is wood instead of orange metal; the cobra is golden like Mesa's instead of the
redder ADD look. `fx-azure-gpu-texenv.png` (FFPROGRAMS 0: texture environment with combine4) shows the same
wooden pillar. Seen on the way (not O-9, both binaries): after `POSTPROCESS 0` at run time the cobra waterfall is
drawn as an opaque violet stream (`fx-azure-*-nopp.png`), i.e. the distortion material's fallback when
post-processing is switched off while running. To be reported, not touched here.

The Mesa pictures taken while the census ran three llvmpipe games in parallel failed (0.2 fps: the match never
started before the shots); to be retaken when the machine is idle.

### Script `tools/dev/o9-nvidia.sh`

Checks combiner-mesa (programs; texture environment combine3; combine4 only; "before" = main's NVIDIA path
emulated: texture environment without combine3/4, 4 units, expected to fail exactly the 11 three-operand cases),
combiner-gpu (programs; texture environment = combine4; "before" = `-glnocombine4`), combiner-twin, caps, pictures.
First run of `combiner-gpu` (`logs/o9/o9-nvidia-summary.txt`): programs 35/35 (largest error 0.56/255); texture
environment combine4 33 passed, 2 skipped (8 stages, stage-5 bias: 4 units); before: exactly the 11 three-operand
cases fail. `tools/dev/m11-twin.sh` takes `M11_DESKTOP_TREE`/`M11_TWIN_TREE` (default m11/m11t) so this branch can
run it in `o9m11`/`o9m11t`.

## Step 3 (2026-10-07): the census of all 50 maps; the ViperPit water; the decision

`tools/dev/o9-census.sh` over every DM-, CTF- and OD- map (`logs/o9/census-summary.txt`; 526-650 materials
per map, lit and unlit each):

- **B/C (the three-operand caps; what NVIDIA lost)**: every map, 8-62 compilations per map; **327 distinct
  materials** over all maps (Combiner, Shader, FinalBlend, TexScaler). Every one of them rendered differently on
  NVIDIA before O-9 (the caps' fallback: MODULATE2X instead of the specular add, ADD instead of the masked add).
  On top of those, the unconditional MULTIPLYADD / AddToCurrent stages compile the same in B and C but were
  approximated by the texture environment at draw time (the base colour dropped).
- **A/B (8 against 4 stages)**: 15 distinct materials, 1-6 per map: `T_FX.Beams.FX_Beams_Raiden_Body_Shader`
  (all 50 maps), `NKTemple_Banners_01_Sd`, `NKTech_Floor_Panel_*`, `NKTeam_Banners_02_Sd`, iced floor tiles,
  `T_Liquid_Falls_Water01_SD01`, `NKTech_Wall_LS06_Sd`, `NKTech_Effects_WalkWay_Sd`, `T_LI_Dammed_Wall004_SD`,
  `T_CF_SnowBlend_SD01`, the CTF-ViperPit ocean, the two DM-Horizon waters. With 8 stages the specular/env layer
  is folded into the first pass; with Epic's 4 it is a second pass blended onto the framebuffer (`sa,1`) or the
  lighting is a separate modulating pass (`dc,0`). Not the same formula.
- **Compile errors**: none with 8 stages; with 4, one: `T_Liquid_Viper_OceanWater_SD` *lit* ("No stages left for
  opacity bitmap", Epic's own message). In the game it is the skin of `FluidSurfaceInfo4` (`obj refs`,
  `logs/o9/fx-viperpit.out`), drawn unlit, and with 4 stages it compiles in one pass without fallback
  (`-glmatcensus` per draw, `logs/o9/fx-viperpit-water.out`: 91 compilations drawn, 0 fallbacks). The lit error is
  a context of the compile-only census that the game does not use.

### Decision, with the evidence

1. **Fragment programs by default on desktop GL** (option b). Evidence: the numeric test passes 35/35 with them
   on both GPUs with errors at rounding level (0.50 Mesa, 0.56 NVIDIA); the code is one path for every vendor
   (no dependence on combine3, which NVIDIA lacks, or combine4, which AMD lacks, or GL_MAX_TEXTURE_UNITS); the
   cost is ~300 lines and one generated program per stage combination (DM-Azure: 28 programs, 0 failed, 124,492
   program draws in a run, `fx-azure-gpu.out`). It is also where the one NVIDIA-only behaviour was found and handled
   (the sign of `gl_FogFragCoord`).
2. **combine4 in the texture-environment path** (option a) as the comparison switch's own fix: with
   `FixedFunctionPrograms=False` NVIDIA now expresses the three-operand ops (33 passed, 2 skipped for 4 units). So
   the switch compares "GL's combiners" with "GLSL", both right, not "broken" with "fixed".
3. **4 stages, Epic's number** (`MaterialStages=4`), instead of the 8 of Mesa/web before: 15 materials over all
   maps are split into passes as on the Xbox; this is the only intended change to Mesa's and the web's pictures.
4. **Epic's multipass answer (option c)** is not an answer for the missing ops: for a missing op Epic's compiler
   has only the GeForce1-era substitutions (MODULATE2X, ADD) that made the pillar metal; for stages it has the
   split into passes, which is what `MaterialStages=4` reproduces.
5. The web keeps gl4es' own fixed-function shaders (combine3 emulated; the same test on the twin, below).

Coverage note (not measured here, from the extension specs and what this machine shows): combine3 is ATI/AMD's
and Mesa's, combine4 NVIDIA's and Mesa's; a GL 2.0 fragment shader is on every driver this port can run on (the
driver already needs GL 2.0). `opengl.gpuinfo.org` could not be read (the listing is filled by script), so no
coverage percentages are quoted.

## Step 4 (2026-10-07): the script's first full runs; the pictures and what the numbers mean

`tools/dev/o9-nvidia.sh` (run 1, `logs/o9/o9-nvidia-run1.txt`): every combiner line PASS on Mesa, NVIDIA and the
twin; `caps` PASS (4 stages, exact operations on both GPUs). The twin: 33 passed, 2 skipped (LOD bias: none on
OpenGL ES), largest error 0.80/255: gl4es' emulation of combine3 is right, so the web had the ops already.

Pictures, fixes to the script on the way: the "before" executable lost its exec bit when the tree was rebuilt
(restored with `chmod 755`); fx-owner.sh deletes `fx-<name>-*.png` before a run, which included the "-before"
pictures (after-binary runs go first now); post-processed shots differ by the dynamic gamma ramp, which adapts
with the frame rate (NVIDIA hundreds of fps, llvmpipe a few): the numbers come from the shots without
post-processing; `-glnoaa`; the first-person close view of the cobra settles its eye height differently at the
two frame rates (a few pixels: NVIDIA vs Mesa 13.6 in that crop although the third-person crop of the same statue
differs by 0.29), so the cobra number comes from the third-person view and the close view stays a picture.

Measured (`logs/o9/o9-nvidia-pictures5.txt` and the crops recomputed on the same files), mean absolute difference
of 255 against Mesa after O-9:

| Crop | NVIDIA before (main) | NVIDIA after | Mesa before |
|---|---|---|---|
| wooden pillar | 59.8 | 0.76 | 0.000 |
| cobra statue (right half, third person) | 5.34 | 0.29 | 0.002 |

NVIDIA texture environment (combine4) against fragment programs, same run: pillar 0.10, cobra close-up 0.000.

### Two more maps (`pictures-other`, `logs/o9/o9-nvidia-other1.txt`)

DM-AcidRain (PlayerStart7) and DM-Tempest (PlayerStart1), third person, time frozen, `-glnoaa -glmatcensus`.
Of the materials drawn in these views, 23 of 72 (AcidRain) and 26 of 74 (Tempest) are in the census's B/C list
(their compilation depends on the three-operand caps). Whole frame without the pawn, mean difference of 255:

| View | NVIDIA before vs Mesa after | NVIDIA after vs Mesa after | Mesa before vs after | NVIDIA combine4 vs programs |
|---|---|---|---|---|
| DM-AcidRain | 16.1 | 0.70 | 0.25 | 0.09 |
| DM-Tempest | 13.9 | 0.57 | 0.14 | 0.04 |

`logs/o9/o9-acidrain-nvidia-before-after.png`: before, on the owner's GPU, most walls and the floor gratings of
that view were nearly black (MULTIPLYADD / AddToCurrent stages with the base colour dropped); after, textured as
on Mesa.

## Gate (2026-10-07, branch `o9-nvidia`; every run through `<WorkUC2>/bin/capped`, no ceiling hit, no exit 137)

(a) Builds:
- native: `capped 6G 3600 -- cmake --preset native && cmake --build build-native -j6` (`logs/o9/build-0.log`),
  then incremental `capped 6G 1800 -- cmake --build build-native -j6` after each change (`build-1..6.log`), all
  exit 0, `[100%] Built target SDLLaunch`; the last code change is in `build-6`.
- windows: `capped 6G 3600 -- cmake --preset windows && cmake --build build-windows -j6` -> `[100%] Built
  target SDLLaunch`, `Built target UCC`, `windows build exit 0` (`logs/o9/gate-windows-build.log`).
- wasm-perf: `capped 6G 5400 -- source env.sh && cmake --preset wasm-perf && cmake --build build-wasm-perf -j6`
  -> `[100%] Built target SDLLaunch`, `wasm build exit 0` (`logs/o9/gate-wasm-build.log`).

(b) `tools/dev/o9-nvidia.sh` on a fresh tree (`logs/o9/o9-nvidia-final.txt`): **12 PASS, 0 FAIL**:
combiner-mesa-programs 35/35 (worst 0.50/255), -texenv-combine3 35/35, -texenv-combine4 35/35, -before 22
passed / 11 failed = exactly the three-operand cases; combiner-gpu-programs 35/35 (worst 0.56), -texenv-combine4
33 passed / 2 skipped (4 units), -before 11 three-operand failures; combiner-twin 33 passed / 2 skipped (no LOD
bias on ES; worst 0.80); caps; pictures (pillar 59.8 -> 0.76, cobra 5.35 -> 0.29, Mesa before/after 0.000,
combine4 vs programs 0.10 / 0.000); pictures-other (AcidRain 16.2 -> 0.75, Tempest 13.9 -> 0.58); leftovers.

(c) Regressions on fresh trees:
- `tools/dev/m7-effects.sh o9gate` (`logs/o9/gate-m7-effects.txt`, `gate-m7-selftest.txt`): 14 effect lines, every
  one `critical=0 glerrors=0 programs_failed=0+0 left_behind=0`; self-test `11 passed, 0 failed`. (The self-test
  of the first call did not start: the script writes `logs/<tree>/fx-selftest.report` before that folder exists
  on a brand-new tree; rerun with `m7-effects.sh o9gate selftest`.)
- Pinned DM-Deadbolt `-nopostprocess` (`deadbolt-pinned.scen` through `m10-controls.sh --file`, this branch in
  `run/o9pin` against main's executable `uc2-before.bin` in `run/o9ref`): same view `loc=(1531.9,3111.5,-34.1)
  rot=(0,-40880,0)` on both, `pinned-compare.py`: `OK 99.997 90.6 28.08` (static part 99.997% within 2 levels),
  0 GL errors, 0 Critical. So the 4-stage change and the programs leave this view unchanged on Mesa.
- `M11_DESKTOP_TREE=o9m11 M11_TWIN_TREE=o9m11t tools/dev/m11-twin.sh` (`logs/o9/gate-m11-twin.txt`,
  `logs/o9/gate-m11-twin-summary-run1.txt`): **29 PASS, 5 FAIL** (main's run: 31 PASS, 3 FAIL). Failing:
  `fallback-glnodepthtexture` + `fallbacks` (known: not implemented); `movies` with two of nine m9 checks:
  `menu-movie-shots` (known timing-sensitive, failed on main too) and `logos-clean` ("None GL errors": the check
  reads the "frame 600, N GL errors so far" line, and under the machine's load of 24-37 at the time the twin
  presented 267 (rerun: 306) pictures in the 20 s of the logos movie against 791 in main's run, so no such line
  was written; rerun alone: same); `effect-liandri` + `effects` (B picture desktop vs twin mean 9.26 > 6: the
  effect's scan-line noise is random: two desktop runs of the same branch binary differ by 9.37, `o9gate` vs
  `o9m11`; this branch's desktop `o9gate` vs this twin: 4.02; main's run had 2.80). Every PASS line of main's run
  passes here, including pinned (no LOD bias: mean 0.380; main 0.374 in the M11 notes), progtest 420/420, self-test
  11/11 on both bindings, m6 4/4, the four implemented fallbacks.
- `./ucc-bin verifyclasses` in `run/o9gate`: `All native class layouts match`, exit 0
  (`logs/o9/gate-verifyclasses.txt`).

Performance (NVIDIA, DM-Azure pillar view, 1280x720, AA 4x, post-processing on, `-glstats`, one run with
`FFPROGRAMS` toggled every 8 s, machine load 13-20 from other work): programs 12.35 / 11.35 / 12.06 / 12.15 ms per
frame, texture environment (combine4) 12.01 / 11.52 / 11.37 / 12.16 / 11.95 ms: no difference beyond the noise
(about 0.2 ms, 1.5%). Not measured on an idle machine.

Logs outside `logs/o9`: the shared scripts name their log folder after the run tree, so `logs/o9c0..2` (census),
`logs/o9gate`, `logs/o9m11`, `logs/o9m11t` exist too (all mine, all run trees `run/o9*`).

## What the owner will see on the NVIDIA GPU (and what changes elsewhere)

- DM-Azure: the wooden pillars are wood (were orange metal), the cobra statue golden as on Mesa (was redder);
  over all maps 327 materials that used the caps fallback now compile as on Mesa/Epic, and the stages with
  MULTIPLYADD / "add to current" no longer lose their base colour: whole walls, floors and columns that were nearly
  black or wrongly tinted (DM-AcidRain, DM-Tempest pictures) are textured. Pictures: `logs/o9/o9-azure-nvidia-before-after.png`,
  `o9-azure-cobra-nvidia-before-after.png`, `o9-acidrain-nvidia-before-after.png`, `o9-tempest-nvidia-before-after.png`
  (left before, right after; post-processing on, as played), `o9-*-nvidia-vs-mesa.png`.
- On every GPU and the web: 15 materials (the Raiden beam body, the cobra waterfall shader, some banners, floor
  panels, the Horizon and ViperPit waters...) are split into passes as Epic's 4-stage compiler split them.
- Switch for comparison: `FixedFunctionPrograms=False` / `-glnoffprograms` / console `FFPROGRAMS 0` (texture
  environment; on NVIDIA now with combine4, 4 stages); `-glstages=8` gives the old 8-stage split.

## Unsure / not done

- AMD and Intel GPUs were not available: the program path is plain GLSL 1.20 on the compatibility profile; the
  one vendor difference met (NVIDIA's signed `gl_FogFragCoord`) is handled and tested on both signs.
- "4 stages is the Xbox's picture" rests on Epic's code (both platforms clamp to 4), not on an Xbox picture.
- The census's compile-only "lit" context is wider than the game's; the only 4-stage compile error it found
  (ViperPit ocean, lit) does not occur in the game (drawn unlit). Other runtime contexts (projectors on these
  materials, LightingOnly) were not enumerated.
- The web build was compiled, not run in a browser here: the web path is unchanged except the 4-stage split
  (gl4es' fixed-function emulation, checked numerically on the twin).
