Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# Heads-up for engine-ut2004 / engine-ut99 agents: GL4ES fork change in `realize_textures` (2026-10-10)

From the engine-uc2 port (B-2, "DM-Decay blown-out overlay"). Full explanation: the fork commit's message, and
`engine-uc2/docs/porting/analysis/decay-flash.md`, section "GL4ES push preparation".

## What changed

Fork `https://github.com/10-p/gl4es.git`, branch `fix/cubemap-bind-active-unit`: commit **`b0df7bec00999a5b557d82ef0147cea7e8b0b775` ("Bind each unit's 2D texture from its own enables, also when a cube map is on")** on top of
`6b6bccd44e65769336ae0e0c37e79f5950e63542` (the previous tip). One function: `realize_textures()` in
`src/gl/texture_params.c` (+10 / -4 lines). Written 2026-10-10 in the local clone
`/home/ion/WorkUC2/engine-uc2-f3-webgl2/.gl4es-dev`; pushed by the UC2 coordinator after the owner's go-ahead (check
`git ls-remote https://github.com/10-p/gl4es.git fix/cubemap-bind-active-unit`: `b0df7bec...` = pushed). Your next
configure fetches it without any change on your side.

GL4ES binds 1D/2D/3D/RECTANGLE textures lazily: `glBindTexture` only records the binding, and at each draw
`realize_textures()` puts, per texture unit, one texture into the unit's GLES `GL_TEXTURE_2D` slot. Before this
commit:

- it picked that texture from the enables of the **active** unit (`glstate->enable.texture[glstate->texture.active]`)
  for **every** unit, and
- when those enables said "cube map only" it **skipped the 2D bind for every unit** (and forgot the change), so
  the 2D slots kept whatever 2D texture GL4ES had last bound there.

Now unit `i` uses its own enables (`enable.texture[i]`), and a cube-only unit still gets its bound 2D texture
into its 2D slot (a GLSL `sampler2D` or an ARB `TEX ..., 2D` on that unit reads it). That is what desktop GL does:
enables are per unit, the highest-priority enabled target of a unit (CUBE > RECT > 3D > 2D > 1D) is what fixed
function samples, and programs ignore the enables entirely.

## Does it reach you?

- **engine-ut2004**: yes, at the next configure (`CMakeLists.txt:923-925` fetch the branch; a branch ref is
  re-fetched). Read from the code (not run): GL4ESDrv enables `GL_TEXTURE_2D` on **every** used stage and adds
  `GL_TEXTURE_CUBE_MAP` for a cube stage (`GL4ESRenderInterface.cpp:666-675`), disables both on unused stages
  (`:901-906`), and its ARB pixel shaders sample `texture[n], 2D` only (`GL4ESRenderDevice.cpp:1497-1524`);
  PixoDrv's present path is 2D on unit 0. So every unit is "2D", "2D+CUBE" or "nothing" at every draw, and
  `realize_textures` takes the **same path before and after** this commit. Expected change: none.
- **engine-ut99 (UTGLR)**: by default **no**: `Source/CMakeLists.txt:562-571` fetches upstream
  `ptitSeb/gl4es@master`, not the fork (the build caches agree). UTGLR uses only `GL_TEXTURE_2D` (no cube map,
  3D or rectangle), so even pointed at the fork nothing can change. NOpenGLESDrv/XOpenGLDrv do not use gl4es.

## If it bites anyway: the symptom

Only through GL4ES (native `-GL4ESRENDERER` / the web), never on the desktop OpenGL device:

- a surface shows **another material's picture** (a stale texture) **right after a cube-mapped draw**: reflective
  or environment-mapped materials (`TexEnvMap`, `Cubemap` stages: shiny vehicles and weapons, glass), water
  surfaces, skyboxes drawn through a cube map; the wrong one is the draw **after** the cube-mapped one, on the
  same texture unit or another one;
- or a full-screen pass (a copy, a post-process, a render-target present) showing a sprite/texture instead of the
  scene;
- it depends on **draw order** (where you stand, what was drawn last), not on the material, and can flicker as
  translucent sorting changes;
- possibly one `GL_INVALID_OPERATION` early on (the mipmap block of `realize_textures` runs on a cube-only unit's
  cube map once per cube texture and calls `glGenerateMipmap(GL_TEXTURE_2D)` on the current unit; pre-existing
  code, reached in more cases now: see the commit message, danger 5).

Since engine-ut2004 never leaves a unit with the cube map enabled and 2D disabled, the first place to look for a
**new** cause there is any change that does (a GLSL path that disables 2D on a cube stage, a material path that
calls `glDisable(GL_TEXTURE_2D)` before a cube draw).

## Where to look in GL4ES

- `src/gl/texture_params.c`: `realize_textures()` (the `tmp`/`tgt` choice and the `tex2d` line),
  `gl4es_glBindTexture()` (cube maps bound immediately, the rest deferred), `gl4es_glActiveTexture()` (lazy: only
  records the unit), `realize_1texture()` (sampler state per texture), `realize_bound()`.
- `src/gl/enable.c`: the `GL_TEXTURE_1D/2D/3D/RECTANGLE/CUBE_MAP` cases (per-unit enable bits, `bound_changed`).
- `src/gl/fpe.c`: `fpe_gettexture()` (which sampler type the generated shader uses per unit) and the
  `fpe_bound_changed` loop in `realize_glenv()`.
- Debug: build gl4es with `#define DEBUG` at the top of `texture_params.c` to print each `realize_textures` and
  `Binding ...` line.

## One-line A/B (old behaviour back)

Both engine-ut2004 and engine-uc2 fetch with `GIT_SHALLOW TRUE`, which **cannot** check out a commit that is no
longer a branch tip (tested 2026-10-10 with CMake 4.2.3: `-DGL4ES_GIT_TAG=<full sha of a non-tip commit>` fails
with "unable to read tree"; an abbreviated sha fails with "invalid reference"). So use one of:

```sh
# a) a local checkout of the previous tip (always works)
git clone https://github.com/10-p/gl4es.git /tmp/gl4es-ab && git -C /tmp/gl4es-ab checkout 6b6bccd44e65769336ae0e0c37e79f5950e63542
cmake --preset <preset> -DFETCHCONTENT_SOURCE_DIR_GL4ES=/tmp/gl4es-ab      # engine-ut2004 / engine-uc2
#   (engine-ut99: -DGL4ES_SOURCE_DIR=/tmp/gl4es-ab)
# b) once the tag is pushed (local tag before-per-unit-textures = 6b6bccd4): -DGL4ES_GIT_TAG=before-per-unit-textures
```

Undo with `-DFETCHCONTENT_SOURCE_DIR_GL4ES=` (empty) or `-DGL4ES_GIT_TAG=fix/cubemap-bind-active-unit`. If the
picture is right with `6b6bccd4` and wrong with the tip, it is this commit (it is the only commit between the two).

## The UC2 test that proves the new behaviour

engine-uc2, `tools/dev/decay-flash.sh <tree> <name> tools/dev/decay-flash/repro.scen -gl4es` (DM-Decay, the owner's
spot) with the engine's own workaround (`UUC2GLRenderDevice::BindProgramTexture2D`) switched off: the middle of
the screen measures luma ~210 with the old tip (a particle texture stretched over the screen) and ~36 with this
commit (the desktop picture). Same for `DF_MAP=DM-Fury ... fury.scen` (~155 vs ~35). Results:

| run (GL4ES twin, engine workaround off) | fork tip `6b6bccd4` | this commit |
|---|---|---|
| DM-Decay owner view, Mesa llvmpipe | 209.8 / 158.6 | 36.4 / 37.1 |
| DM-Decay owner view, NVIDIA GLES 3.2 | 206.7 / 153.2 | 37.4 / 37.7 |
| DM-Fury, 20 pictures, Mesa / NVIDIA (middle) | 155.1 / 150.9 | 35.0 / 34.6 |
| combiner case `cube-then-2d` (fixed function, cube on stage 0, 2D on stage 1) | FAIL, error 138.8/255 | PASS, 0.49/255 (= desktop GL) |

Also with this commit: UC2 `m11-twin.sh` 32 PASS / 2 FAIL (the time-dependent `effect-blur` at load ~20 and its
aggregate; quiet rerun within the threshold, the same as with `6b6bccd4`); GPU reflective scenes (DM-Azure pool,
DM-Praxis panes, waterfall) as close to the desktop as before; UC2's wasm on WebGL 2 through this GL4ES, the
frontend boot spec 5/5; frame rate unchanged within run-to-run spread; 0 GL errors everywhere. Details:
`engine-uc2/docs/porting/analysis/decay-flash.md`, "GL4ES push preparation", and the logs next to this note.

To reproduce in engine-uc2 (worktree with this commit's GL4ES): the combiner case runs with
`-ini:System:[UC2GLDrv.UC2GLRenderDevice]:CombinerTest=True -gl4es` (or `tools/dev/o9-nvidia.sh combiner-twin`),
line `UC2GLDrv: combinertest cube-then-2d: PASS|FAIL`.
