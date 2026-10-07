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
