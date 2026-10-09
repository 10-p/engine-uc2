# F-3: GL4ES on an OpenGL ES 3 / WebGL 2 context: notebook

Branch `f3-webgl2`, worktree `/home/ion/WorkUC2/engine-uc2-f3-webgl2` (from main `1372dec`), run trees `run/f3*`,
logs and pictures `/home/ion/WorkUC2/logs/f3`. Task: PLAN.md "Owner review of 2026-10-09", F-3.

The owner's decision (verbatim): "No raw WebGL2/ES3 path. I was suggesting to use WebGL2 on top of GL4ES, or
rather retargeting GL4ES to WebGL2, rather than WebGL1." and "Lets explore GL4ES WebGL2 support first. I don't
want to do the driver raw, it is complicated and won't bring us benefits." Also: FXAA goes once real MSAA exists
on the web; fragment highp "Set high"; LOD bias applied on GLES by fixing our GL4ES fork ("make sure it makes a
difference"); the depth-texture gap closes with WebGL 2 core depth textures; fewer "GLES behind GL4ES' back".

GL4ES development clone: `<worktree>/.gl4es-dev` (clone of `https://github.com/10-p/gl4es.git`, branch
`fix/cubemap-bind-active-unit` at `98441ed`, remote `upstream` = ptitSeb/gl4es; excluded from git through
`info/exclude`, never committed into engine-uc2). Commits to the fork are made there, never pushed (the
coordinator pushes after the owner's review).

Running: every build/run through `/home/ion/WorkUC2/bin/capped` (builds 6G `-j4`, game 2G, Emscripten link 6G,
frontend build 4G, browser only through the frontend harness, one harness run machine-wide).

## Status

- 2026-10-10 01:55: notebook created; step 1 (exploration, read-only) in progress.

## Step 1: what exists (read-only exploration, 2026-10-09)

Sources read: the fork at `98441ed` (`.gl4es-dev`; upstream ptitSeb/gl4es master is at `ec16bedd`, 9 commits ahead
of our branch's base `17f0894`, none about ES3), Emscripten 5.0.7 `src/lib/libwebgl.js`, the driver
(`Source/UC2GLDrv`), the binding (`Source/UC2GL4ESDrv`), `Source/SDLLaunch/cmake/Web.cmake`, ufrontend `uc2` (read
only). "Verified" below = I read the code, nothing ran yet.

### GL4ES and an OpenGL ES 3 context

| Area | What GL4ES does today (file:line in the fork) | Consequence for ES3 / WebGL 2 |
|---|---|---|
| Back-end version | `LIBGL_ES` accepts only 1 and 2 (`src/gl/init.c:176-185`); `hardext.esversion = globals4es.es` (`src/glx/hardext.c:102`). There is no ES3 back end. Only one line knows ES3 (`hardext.c:300`: `esversion>2` makes draw buffers core), unreachable today. | We must teach it `LIBGL_ES=3`. All `esversion>1` paths (ES2) are the right base: ES3 is a superset. |
| Feature detection | By extension strings only (`hardext.c:296-390`): `GL_OES_depth_texture`, `GL_OES_packed_depth_stencil`, `GL_OES_depth24`, `GL_OES_element_index_uint`, `GL_OES_rgb8_rgba8`, `GL_EXT_frag_depth`... Under Emscripten `glGetString(GL_EXTENSIONS)` is the WebGL list plus a `GL_`-prefixed copy (`libwebgl.js:1260-1266`); a WebGL 2 context lists none of these (they are core), so GL4ES on WebGL 2 would believe it has no depth textures, no packed depth-stencil, no 24-bit depth. Mesa's ES 3 context still lists the OES names, so the twin and the browser would differ. | ES3 must switch these on from the version, not from strings. |
| NPOT | `npot=1` (limited) on ES2 unless `GL_OES_texture_npot` (`hardext.c:291-295`). | ES3: full NPOT (mipmaps, REPEAT): `npot=3`. Render targets sampled with WRAP stop being forced to CLAMP. |
| Shader language | Every shader converted to `#version 100` (`shaderconv.c:286-291, 504-518`); the 300 es / 310 es headers exist but the selection is under `#if 0` ("support for higher glsl require much more work"). `gl_FragDepth` -> `#extension GL_EXT_frag_depth` or **silently a dummy variable** (`shaderconv.c:546-556`); `dFdx` -> extension or a fake constant 0.0001 (`:563-573`); `texture2DLod` -> `EXT_shader_texture_lod` or an emulation (`:598-620`). | On WebGL 2 an ESSL 1.00 shader gets **no** `EXT_frag_depth`, `OES_standard_derivatives`, `EXT_shader_texture_lod` (WebGL 2 does not expose them to 1.00 shaders; Emscripten's WebGL 2 list does not have them either). So with ESSL 1.00 on WebGL 2, the depth copy (`gl_FragDepth`) would lose what WebGL 1 had. ESSL 3.00 output gives all three as core. The converter has token-aware replace helpers (`string_utils.c:11`), so an ESSL 1.00 -> 3.00 rewrite at the end of the conversion is feasible. |
| Precision | `wanthighp = !fpeShader` (`shaderconv.c:494-496`): user programs get `precision highp` when the hardware has fragment highp; **GL4ES' own fixed-function shaders (FPE) get `mediump`** by default, with selected highp variables (texcoords, lights, fog: `fpe_shader.c:208, 254-311, 374-383`); the texture env colour is `lowp` (`fpe_shader.c:1076, 1187`). | The owner's "fragment highp: Set high" is NOT true today for the fixed-function half of the picture (most of the world geometry). On desktop/ANGLE mediump is computed in 32 bits anyway; on mobile it is 16-bit. ES 3 guarantees fragment highp. |
| MSAA | `glRenderbufferStorageMultisample` is a stub that allocates a single-sampled buffer (`framebuffers.c:1059-1061`). | Fork: real call on ES3. |
| Blit | `glBlitFramebuffer` is emulated by drawing a textured quad; depth/stencil masks are dropped ("cannot copy DEPTH or STENCIL data on GLES", `framebuffers.c:1395-1404`); read/draw framebuffers are emulated by binding `GL_FRAMEBUFFER` only (`:221-260`, `ReadDraw_Push`). | Fork: real `glBlitFramebuffer` with the real read/draw bindings on ES3 (resolve of the multisampled main framebuffer, colour and depth-stencil). |
| Depth textures | Upload format = internal format, unsized (`texture.c:160-170, 366-387, 720-728`: `GL_DEPTH_STENCIL`/`GL_UNSIGNED_INT_24_8`); a DEPTH_STENCIL attachment is split into DEPTH and STENCIL (`framebuffers.c:765-850`, `:891-896` for renderbuffers). | ES 3 / WebGL 2 reject unsized depth internal formats (only `DEPTH_COMPONENT16/24/32F`, `DEPTH24_STENCIL8`, `DEPTH32F_STENCIL8`). Fork: sized internal formats on ES3. The split attachment is legal on ES3/WebGL 2 for one image (DEPTH24_STENCIL8 on both points). |
| Texture LOD bias | `glTexEnvf(GL_TEXTURE_FILTER_CONTROL, GL_TEXTURE_LOD_BIAS, b)` is stored in `glstate->texenv[tmu].filter.lod_bias` (`texenv.c:42-50`) **declared `GLenum`** (`texenv.h:32`: the float -0.5 is stored in an unsigned int) and never read by the shader generator; the texture-object `GL_TEXTURE_LOD_BIAS` is stored only (`texture_params.c:318`). | Fork: the FPE fragment shader takes a per-unit bias uniform where the bias is non-zero (`texture2D(s, uv, bias)` is ESSL 1.00 too: the fix works on WebGL 1 as well). The driver's own desktop "fixed-function programs" do exactly this (`UC2GLFFProgram.cpp:194-215, 320-326`). |
| Max level | Remembered, used to generate missing levels (ES2 has none). | ES3 has `GL_TEXTURE_MAX_LEVEL`; the driver's engine textures go around GL4ES anyway (below). Not touched in step 2. |

### Emscripten

- `-sMAX_WEBGL_VERSION=2` allows a WebGL 2 context; `-sMIN_WEBGL_VERSION=2` drops every WebGL 1 code path and
  the WebGL 1 extension names from the extension list (`libwebgl.js:169-190`); `MIN=1, MAX=2` keeps both.
- The context is the engine's (`UC2GL4ESDrv.cpp` `emscripten_webgl_create_context("#canvas")` on the worker,
  `majorVersion = 1` today); the frontend creates none for the game (it probes `webgl`/`webgl2` on a scratch canvas
  in `capabilities.ts:256-257` and in the harness' GPU canary `lib/gpu.js:38`, which already prefers `webgl2`).
  So the switch is `majorVersion = 2` in the binding and the link flags; no frontend change for the context.
- `-sFULL_ES2` (client-side arrays) stays; `-sFULL_ES3` adds `glMapBufferRange`/`glGetBufferSubData` emulation:
  GL4ES has `gl4es_glMapBufferRange` (`buffers.c:618`) but works on its own CPU copy; to be seen whether any
  ES3-only entry point is reached (link errors / `LIBGL` stubs would say).
- `GL_VERSION` on WebGL 2 reads "OpenGL ES 3.0 (WebGL 2.0 ...)" (`libwebgl.js:1299-1305`).

### Frontend (read only)

`packages/e2e/uc2-wasm-boot.spec.js` asserts the literal log lines `/WebGL 1 context created/` (:293) and
`binding: WebGL 1 through GL4ES` (:335), and compares the canvas with twin pictures made with FXAA
(`UC2_E2E_TWIN_MENU/MAP`). A WebGL 2 engine fails those two assertions by design; I may not change the frontend
(standing rule), so the spec is run as it is and the two lines are reported, with the new reference pictures.

### "GLES behind GL4ES' back": the inventory (58 raw calls, all in `UC2GLRenderDevice.cpp`)

| Function (line) | Raw calls | Why it is raw today | ES3 through a fixed GL4ES? |
|---|---|---|---|
| `FindRawExt` (:380), `DetectCapabilities` (:1415) | `glGetString` x3, `glGetShaderPrecisionFormat` | the hardware's own extension list / precision (GL4ES reports its own) | stays (a query, no state) |
| `CheckGLError` (:520) | `glGetError` | GL4ES answers from its own record | stays (a query) |
| `CreateDepthStencilTexture` (:1027) | `GetIntegerv`, `BindTexture` x2, `TexParameteri` x4, `TexImage2D` | GL4ES would make it unsized / 16-bit, which WebGL 1 rejects | **goes**: sized formats in the fork |
| `CreateDepthStencilRenderbuffer`, `DeleteDepthStencilRenderbuffer` (:1066, :1088) | `Gen/Bind/RenderbufferStorage/Delete` | GL4ES split depth/stencil | **goes** (`DEPTH24_STENCIL8` is core) |
| `RawBindFramebuffer`, `AttachDepthStencilTexture`, `AttachDepthStencilRenderbuffer`, `AttachMainDepthStencil` (:1106-1188) | `BindFramebuffer`, `FramebufferTexture2D` x3, `FramebufferRenderbuffer` x3, `GetIntegerv`, `BindTexture` | WebGL 1 needs one `DEPTH_STENCIL_ATTACHMENT` | **goes** (ES3 / WebGL 2 accept the image on both points) |
| `UploadMainDepth` (:1190) | `BindTexture` x2, `PixelStorei`, `TexSubImage2D`, `GetError` | self-test only: writes depth values | likely goes (to check: GL4ES' sub-image path for depth) |
| `CompressedTexImage2D` (:1216) | `GetIntegerv`, `BindTexture` x2, `CompressedTexImage2D`, `GetError` | GL4ES always decodes DXT in software | stays (not an ES3 question; a fork change could pass S3TC through, separate item) |
| `TexImage2D` (:1258) | `GetIntegerv` x2, `BindTexture` x2, `PixelStorei` x2, `TexImage2D` | GL4ES regenerates mip chains from `GL_TEXTURE_MAX_LEVEL` with a box filter | maybe: ES3 has max level; GL4ES would have to pass it through instead of generating. Step 6 |
| `PrepareGL4ESTextureZero` (:1380) | `GetIntegerv`, `GenTextures`, `ActiveTexture`, `BindTexture`, `TexImage2D` | WebGL has no default texture object | stays (WebGL 2 has none either) |

## Plan (written before any driver change)

GL4ES fork (local clone, branch `fix/cubemap-bind-active-unit`, one commit per item, every ES3 change behind
`LIBGL_ES=3` so engine-ut2004 on ES2 is unaffected):
1. ES3 back end: `LIBGL_ES=3`; on it, core features from the version (depth textures, packed depth-stencil,
   depth24, rgba8, uint indices, full NPOT, fragment highp, draw buffers); sized depth internal formats;
   real `glRenderbufferStorageMultisample` and `glBlitFramebuffer` (real read/draw framebuffer bindings).
2. ESSL 3.00 output on ES3: the converted ESSL 1.00 text rewritten (`attribute`/`varying` -> `in`/`out`,
   `gl_FragColor` -> an `out vec4`, `texture2D*`/`textureCube*` -> `texture*`, `gl_FragDepthEXT` -> `gl_FragDepth`),
   so `gl_FragDepth`, derivatives and explicit LOD are core on WebGL 2 as on the twin.
3. Fragment highp for GL4ES' own fixed-function shaders when the hardware has it (switch `LIBGL_FPE_HIGHP`,
   set by our binding; ES3 always has it).
4. Texture LOD bias applied: `lod_bias` as a float; a per-unit bias uniform in the FPE fragment shader when
   the unit's bias is not 0 (one state bit per unit in the program key).

Driver / binding (`Source/UC2GL4ESDrv`, `Source/UC2GLDrv`):
- the binding asks for ES 3.0 (twin: SDL ES profile 3.0; web: `majorVersion 2`), sets `LIBGL_ES=3`; an ini key
  `[UC2GL4ESDrv.UC2GL4ESRenderDevice] GLESVersion=3` (2 = the old OpenGL ES 2 / WebGL 1 path, for the WebGL 1
  question and for testing it on the twin); no new command-line switch;
- `DetectCapabilities`: on ES3 depth textures, 32-bit indices, `gl_FragDepth`, highp are core;
- MSAA on ES3 through GL4ES (the desktop code with `!IsGLES` relaxed), FXAA removed (shader, uniform, flags,
  docs, ini text);
- the depth-stencil storage and attachments through GL4ES on ES3 (raw only on ES2, if ES2 stays);
- the combiner test `lod-bias-stageN` runs on GLES once GL4ES applies the bias.
Web (`Source/SDLLaunch/cmake/Web.cmake`, M11 block only): `-sMAX_WEBGL_VERSION=2` (MIN stays 1 while the WebGL 1
question is open).
Tests: `m11-twin.sh` with `MESA_GLES_VERSION_OVERRIDE=3.0` (WebGL 2 = ES 3.0) for the ES3 twin.

## Step 2: the ES3 spike on the native twin (2026-10-09, 02:00-03:30; machine load 12-30 throughout)

Build: every build of this branch is configured with
`-DFETCHCONTENT_SOURCE_DIR_GL4ES=/home/ion/WorkUC2/engine-uc2-f3-webgl2/.gl4es-dev` (FetchContent's own override:
the fork clone with the commits below is compiled instead of the fetched branch; the M11 block of the root
CMakeLists.txt is unchanged). Native: `capped 6G -- cmake --preset native -D... && cmake --build build-native -j4`
(`logs/f3/build-native-*.log`, all rc=0). The ES3 twin is the twin with `MESA_GLES_VERSION_OVERRIDE=3.0` (WebGL 2 =
OpenGL ES 3.0; without the override Mesa gives 3.2).

GL4ES fork commits so far (local clone `.gl4es-dev`, branch `fix/cubemap-bind-active-unit`, on top of `98441ed`):

| Commit | What | Why (evidence) |
|---|---|---|
| `b53ae966` | GLES 3.0 back end `LIBGL_ES=3`: core features from the version, sized depth formats, DEPTH_STENCIL attachment point, real `glRenderbufferStorageMultisample` and `glBlitFramebuffer` | step 1 reading |
| `8695cfdc` | GLSL ES 3.00 output on ES3 (token-aware rewrite of the converted 1.00 text) | WebGL 2 gives 1.00 shaders no `EXT_frag_depth` |
| `7e25d753` | `LIBGL_FORCEHIGHP`: highp for GL4ES' own fixed-function shaders and as the sampler default | the owner's "fragment highp: Set high"; FPE shaders were mediump |
| `9653a8a1` | the texture unit's LOD bias applied in the fixed-function shaders (bias uniform, one state bit per unit; `lod_bias` stored as a float) | step 4 (proof pending; the commit is dropped if it makes no difference) |
| `1bd90647` | GLSL ES 3.00: no int overloads of built-ins, no non-constant-initializer extension line | first ES3 run: `present shader does not compile ... A shader cannot redefine or overload built-in function 'clamp' in GLSL ES 3.00` |
| `6a29a856` | `glBindRenderbuffer(GL_RENDERBUFFER, 0)` unbinds instead of raising `GL_INVALID_OPERATION` | second run: `GL error 0x0502 (GL_INVALID_OPERATION) in frame 1` (gl4es refused to unbind; found by reading `framebuffers.c`) |
| `6b6bccd4` | a multisampled colour renderbuffer stays the attachment (`LIBGL_FBOFORCETEX`, default on, replaced it by a single-sampled texture) | `4x multisampled main framebuffer incomplete (0x8d56)` = `FRAMEBUFFER_INCOMPLETE_MULTISAMPLE` |

Engine (this branch): `UC2GL4ESDrv` asks for OpenGL ES 3.0 / WebGL 2 (ini `GLESVersion=3`, falls back to 2 when
the context cannot be had), sets `LIBGL_ES=3` and `LIBGL_FORCEHIGHP=1`; `UC2GLDrv` gets `GLESMajor`, takes depth
textures / 32-bit indices / `gl_FragDepth` from the version on ES3, and multisamples the main framebuffer on ES3
through GL4ES (renderbuffers and resolve blit are GL4ES' own); `Web.cmake` (M11 block) `-sMAX_WEBGL_VERSION=2`.

Results (all `logs/f3t/*.out`, 1280x720):
- `es3-menu-2`: `GLES hardware: llvmpipe ... / OpenGL ES 3.0 Mesa 26.0.8`, `97 fragment shaders compiled, 0 failed`,
  `405 vertex shaders compiled (0 failed)`: every program of the driver and every fixed-function shader of GL4ES
  compiles as GLSL ES 3.00.
- `m11-twin.sh boot selftest progtest` (`M11_SERIAL=1 MESA_GLES_VERSION_OVERRIDE=3.0 M11_DESKTOP_TREE=f3d
  M11_TWIN_TREE=f3t`, `logs/f3/twin-es3-run1.txt`): **boot PASS** (`binding: OpenGL ES 3 through GL4ES`, 97/405
  shaders, 0 GL errors, 0 Mesa errors); **progtest PASS: 485 programs, 7760 values, 7760 identical, largest
  difference 0 of 255** (the ES2 twin had 1 of 255); selftest desktop 15/15, twin **12/15** (depth-color,
  depth-copy, dof-ramp FAIL: every depth value wrong).
- The depth failure, located by experiments (each a full self-test run, `fx-selftest-*.out`):
  | Run | depth-stencil texture storage | attachment | result |
  |---|---|---|---|
  | first | through GL4ES (sized) | through GL4ES (`DEPTH_STENCIL_ATTACHMENT`) | 12/15 |
  | `-rawdepth` (A) | raw, as on ES2 | raw | **15/15** |
  | `-expB` | raw | through GL4ES | 10/15: GL4ES' record of the texture says 1x1 RGBA, so its attach code re-specifies the storage at 1x1 (`depth upload ... failed (GL error 0x0501)`) |
  | `-expE` | through GL4ES, unsized (env switch in a scratch build) | through GL4ES | 12/15 |
  | `-expF` | through GL4ES (sized) | raw | 12/15 |
  | `-dbg3` | through GL4ES | through GL4ES, with prints in `realize_1texture`/`realize_textures` | 12/15; GL4ES binds texture 4 on unit 0 with NEAREST/CLAMP for each read, as asked |
  So: whenever GL4ES created the storage, the self-test's depth values (uploaded raw with `glTexSubImage2D`,
  `GL_NO_ERROR`) are not what the depth passes read; with raw storage they are. Not found further (the upload is a
  self-test-only path; whether the game's own depth writes are affected the same way was not isolated). Decision,
  for now: **the depth-stencil texture stays raw on ES3** (sized `DEPTH24_STENCIL8`, one `DEPTH_STENCIL_ATTACHMENT`),
  the multisampled renderbuffers go through GL4ES. With that: `fx-selftest` **15 passed, 0 failed**, `0 GL errors
  in 883 frames`; `es3-menu-aa`: `main framebuffer 1280x720 (...; drawn 4x multisampled, resolved into them)`.
- Fallbacks of the binding: `MESA_GLES_VERSION_OVERRIDE=2.0` (no ES 3.0 context): `no OpenGL ES 3.0 context: trying
  OpenGL ES 2.0`, `gl4es initialised (GLES2 back end ...)`; `-ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:GLESVersion=2`:
  `OpenGL ES 2.0 context requested`, GLES2 back end. Both draw the menu.

## Step 2, continued: the whole twin script on ES 3.0 (run 2, `logs/f3/twin-es3-run2.txt`, binary of `ae1400c`)

`M11_SERIAL=1 MESA_GLES_VERSION_OVERRIDE=3.0 M11_DESKTOP_TREE=f3d M11_TWIN_TREE=f3t tools/dev/m11-twin.sh` (the
script as on main; its expectations unchanged for this run). So far: shaders PASS, gputest PASS, boot PASS
(`binding: OpenGL ES 3 through GL4ES`, 97 + 405 shaders, 0 GL errors, 0 Mesa errors), selftest PASS (15/15 both,
0 GL errors), progtest PASS (485 programs, **7760 of 7760 values identical**), pinned FAIL by its old thresholds,
which assumed the twin has NO LOD bias:
- desktop (with its -0.5 bias) vs twin: **mean 0.384**, above8 0.262 %, above32 0.001 % (4 pixels);
- desktop without LOD bias vs twin: **mean 0.871**, above8 0.509 %;
- post-processing on, desktop vs twin: mean 0.550.
On main (ES 2 twin, GL4ES ignoring the bias) the two were the other way round: 1.415 with the bias, 0.374
without (REPORT.md R-19). So the fork's LOD-bias commit makes the twin draw what the desktop draws: step 4's
numeric proof, before the pictures. The thresholds move to the comparison with the bias (step 5 edits the script).

## Step 3: FXAA out (commit `8adc4a1`)

The FXAA function of the present shader, its uniform (`uFXAA`), `PresentFXAA`/`PresentFXAALoc`, the bilinear
filtering of the main colour texture it needed (main colour and the reference copy's restore are NEAREST again,
as before O-8) and the self-test's FXAA refusal are removed. `AntiAliasing` stays: the sample count of the
multisampled main framebuffer (desktop GL and OpenGL ES 3 / WebGL 2). On OpenGL ES 2 / WebGL 1 the log says
`anti-aliasing: none (no multisampled framebuffers on this context)`. The switches `-glaa=N` / `-glnoaa` are the
MSAA test switches of O-8 (the picture tests run with `-glnoaa`), not FXAA's; nothing else of FXAA existed (no ini
key of its own). Binaries kept for the A/B pictures: `scratch/bin/uc2-ae1400c.bin` (sha256 `c8b1d211...`, FXAA
still on the ES 2 path), `uc2-nofxaa.bin` (`28d00441...`).

F-2 (the FPS log line) is on main (`2a58dbc`, merged after this branch was made); this branch is not rebased
(standing rule: no merges/rebases by agents), so the frame-time numbers below come from the driver's own
`-glstats` line (`UC2GLDrv: stats: frames a-b: x ms/frame (y frames/s)`, every 300 frames).

### Step 2 run 2, the rest (`logs/f3/twin-es3-run2.txt`)

effects 13/14 PASS (blur PASS this time at 3.07; **liandri FAIL 6.33**: the overlay's random noise, a known
timing FAIL in REPORT.md's open issues), m6 4/4 PASS, fallbacks: s3tc / maxlevel / fragdepth / uintindex PASS,
depthtexture "not implemented" FAIL (step 5), movies 8/9 (menu-movie-shots, the known timing FAIL), leftovers PASS.
So against main's known FAILs (depth texture x2, menu-movie timing) the only new FAIL is pinned, by its old
thresholds (above).

## Step 3: the A/B pictures and the frame time (2026-10-09 04:40-05:10)

Pictures (`tools/dev/f3-ab.sh aa scratch/bin/uc2-ae1400c.bin`, `logs/f3/ab-aa-*.png`, `ab-aa.txt`): DM-Deadbolt,
pinned view, post-processing off, after the present pass, 1280x720, same frame:

| Variant | vs no AA (static part x>=760) |
|---|---|
| twin, FXAA (binary before step 3, OpenGL ES 2 path) | mean 1.751, above8 6.26 % (FXAA also softens textures and the poster: every high-contrast texel) |
| twin, 4x MSAA (OpenGL ES 3) | mean 0.166, above8 0.74 % (only geometry edges change) |
| desktop, 4x MSAA | mean 0.513, above8 1.02 % |
| twin MSAA vs desktop MSAA | mean 0.356, above32 0.011 % |

Crops (3x, `ab-aa-crops.png`, none | FXAA | MSAA twin | MSAA desktop): the lamp, the poster frame: FXAA blurs the
poster picture itself; MSAA smooths the frame's edges and leaves the texture as it is, as on the desktop.

Frame time, GPU (Quadro P620, headless EGL, `tools/dev/f3-gpu-fps.sh`, `logs/f3/gpu-fps.txt`, DM-Deadbolt pre-match
view, 1280x720, mean of the -glstats lines after the first; machine load 12-16):

| Variant | ms/frame |
|---|---|
| desktop GL, no AA / 4x MSAA | 11.72 / 12.27 (+0.55) |
| GL4ES on OpenGL ES 3 (NVIDIA ES 3.2), no AA / 4x MSAA | 5.91 / 6.31 (+0.40, 6.8 %) |
| GL4ES on OpenGL ES 2 path (FXAA binary) | **does not start on NVIDIA's ES driver**: `present shader does not compile: ... unable to find compatible overloaded function "clamp(float, float, float)"`: GL4ES' int overloads of built-ins (inserted on its ES 2 path) break NVIDIA's GLSL ES 1.00 overload resolution. Old, not caused here (the ES 2 twin only ever ran on Mesa; ANGLE in the browser accepts them); the ES 3 path does not insert them (fork `1bd90647`). Recorded as an open item. |

(Why GL4ES on ES is twice as fast as the desktop binding on this GPU was not looked into.)

Frame time, browser (headless Chromium, ANGLE/Vulkan on the Quadro, spec test 2 = DM-Deadbolt with
post-processing, -glstats over 300 frames; machine load 13-21, other agents' builds and games running: **noisy,
not a benchmark**):

| Build | frames 2-301 / 302-601 ms/frame |
|---|---|
| main `2a58dbc` (WebGL 1, FXAA; also F-4's frame pacing) | 55.67 / 46.45 |
| this branch, WebGL 2, AntiAliasing=0 (the same wasm with the preloaded ini patched, `scratch/wasm-noaa`) | 53.23 / 54.09 |
| this branch, WebGL 2, 4x MSAA (run 2 and run msaa2) | 70.08 / 63.27; 71.74 / 58.05 |

MSAA costs about 4-9 ms a frame in the browser here (7-17 %), much more than on the native GPU path (0.4 ms):
ANGLE's multisampled renderbuffers and resolves on Vulkan (4 resolve blits a frame with post-processing: colour and
depth) are the suspects, not measured further. WebGL 2 without AA vs main's WebGL 1 + FXAA: within the noise of
this machine. A quiet-machine measurement (or F-2's FPS line once this branch carries it) is still owed.

## Browser (the frontend's harness, read-only checkout; my wasm proven by sha256)

`yarn build --target uc2 --engine-artifact <worktree>/build-wasm-perf/Source/SDLLaunch` (capped 4G):
`serve-web/SDLLaunch.wasm` sha256 `f29cd354dae3218ec825b404dab2b399004da7a5664523013569471644a4907b` = this build's,
before and after each harness run. Harness: `PULSE_SINK=uc2f3 capped 6G 900|1200 -- node
packages/e2e/tools/e2e-mem-guard.mjs --max-gb=14 -- --target uc2 uc2-wasm-boot.spec.js [-g ...]`, null sink loaded
and unloaded by module index, one run at a time (no other harness was running).
- test 1 (menu), `logs/f3/e2e-run-1.txt`: **FAILS on the spec's literal check** `binding: WebGL 1 through GL4ES`
  (the spec asserts WebGL 1; the frontend is not mine to change: two regexes, lines 293 and 335, and the test's
  title). Everything else the test prints is right: `binding: WebGL 2 through GL4ES`, `GLES hardware: WebKit WebGL /
  OpenGL ES 3.0 (WebGL 2.0 (OpenGL ES 3.0 Chromium))`, `capabilities: S3TC yes, depth texture yes, gl_FragDepth yes,
  32-bit indices yes, ... fragment highp yes`, `main framebuffer 1024x768 (...; drawn 4x multisampled, resolved into
  them)`, 97 fragment + 405 vertex shaders compiled (GLSL ES 3.00), `frame 600, 0 GL errors so far`, PSS peak 1412
  MiB, cgroup peak 2766 MiB. Menu vs twin: the spec's reference method was stale (run-uc2-shot's references
  are drawn with the pre-D-2 UI and AutoFOV off; the browser has the UI box): 18.6 against both the old FXAA
  reference and a new one; with `UC2_UIBOX=1 UC2_AUTOFOV=1` twin pictures (`logs/f3/twin-ref/ref-menu-box-*.png`)
  the nearest is **4.505** (tolerance 10; the menu camera flies on wall-clock time).
- test 2 (DM-Deadbolt by URL), `-g "DM-Deadbolt by URL"`, `e2e-run-2.txt`: the same WebGL 1 assertion fails; its
  printout: **browser vs twin (ES3, MSAA, UI box) 2.928** (tolerance 3), 0 GL errors at frame 600, post-processing
  on, 104 draws/frame, PSS 1817 MiB, cgroup peak 3375 MiB, wasm memory 256 MiB.
- tests 3 and 4 (keyboard and mouse play test, D-6 mouse look), `-g "keyboard and mouse|mouse look"`,
  `e2e-run-3.txt`: **2 passed (2.8m)** on WebGL 2.
- main's wasm (sha256 `7d9a411e...`) test 2 for the frame-time reference: 1 passed.

## Step 4: texture LOD bias on GLES (fork `9653a8a1`; combiner test `Source/UC2GLDrv/Src/UC2GLCombinerTest.cpp`)

1. Numeric, exact: the combiner test's `lod-bias-stage0/5` (a 4x4/2x2/1x1 texture whose levels are red/green/blue,
   drawn 1:1 with bias +1, must read level 1) ran only on desktop ("OpenGL ES has no texture LOD bias"). Now on the
   ES 3 twin: `lod-bias-stage0: PASS ... reads mip level 1 (pixel 0: 0,255,0)`, `stage5` the same, **35 passed, 0
   failed**; with `-glnolodbias` level 0 (255,0,0), 35/35; desktop 35/35. (Two fixes of the test were needed for
   GLES: its levels went through GL4ES, which regenerates a chain from level 0 (everything red); and a raw-only
   upload left GL4ES without a record of the texture, so the stage was disabled (the diffuse colour came out).
   The test now prepares the texture as every engine texture is prepared, `FOpenGLTexture::PrepareStorage`.)
2. The pinned picture, whole static part: twin vs desktop **0.384** (desktop without bias 0.871); on main 1.415 (and
   0.374 without), i.e. the twin now draws the desktop's picture where it drew the bias-less one.
3. The far floor and wall of the pinned view (`tools/dev/f3-ab.sh lodbias`, box 760,470-1200,620, `ab-lodbias.txt`,
   `ab-lod-crops.png` = desktop | desktop without bias | twin | twin without bias, `ab-lod-twin-diff.png` x8):
   - twin vs desktop **0.658** (above8 1.09 %); twin without bias vs desktop 1.465 (above8 1.56 %); the bias changes
     the twin's far floor by 1.137 (above2 14.3 %);
   - high-frequency energy (mean squared Laplacian): desktop 1361.6, desktop without bias 1412.7, twin 1366.4, twin
     without bias 1416.8. **The bias LOWERS the energy here**: on this floor the detail texture dominates the fine
     scale, and its bias is +0.8 (`DetailTexMipBias`, D3DDrv's value), against -0.5 for the base texture; both
     are applied now, on the desktop as on the twin, and the twin follows the desktop within 0.4 %.
   Verdict: a measurable difference that makes the web picture the desktop's (mean distance halved), small to the
   eye at 1x (the crops need the enlargement to show it). The code stays; the owner may judge from the crops.

## Step 5: the depth-texture gap (commit `e40d6ec`, `tools/dev/m11-twin.sh`)

WebGL 2 has depth textures in core (`DEPTH24_STENCIL8`, sampled), so on the WebGL 2 path the "no depth texture"
case does not exist: the browser reports `capabilities: ... depth texture yes, gl_FragDepth yes, 32-bit indices
yes ... fragment highp yes` without any extension (`logs/f3/e2e-run-1.txt`). The case remains only for a browser
WITHOUT WebGL 2 whose WebGL 1 lacks `WEBGL_depth_texture` (UC2GL4ESDrv falls back to WebGL 1 when no WebGL 2
context can be had). `m11-twin.sh`: the twin is OpenGL ES 3.0 by default; `-glnodepthtexture` runs on the ES 2
path and is an INFO line (no longer a FAIL); new check `es2` (boot + self-test of the ES 2 / WebGL 1 path);
pinned compares with the desktop WITH its LOD bias. Run 3 (`logs/f3/twin-es3-run3.txt`): pinned PASS, fallbacks
PASS (4 masks; depth texture INFO: `selftest 12 passed, 3 failed` (the three depth readers), static part vs
the ES 3 twin 0.170, `no depth textures on this context: the depth of field ... stays off`), es2 PASS (`binding:
OpenGL ES 2 through GL4ES`, `OpenGL ES 2.0 Mesa`, 15/15, 0 GL errors).

**What a WebGL 1-only device gets now** (decision for the owner: support WebGL 1 at all?): the ES 2 path exactly as
before F-3 minus FXAA: no anti-aliasing at all (no multisampled framebuffers in WebGL 1; FXAA is gone); with
`WEBGL_depth_texture` everything else as before (depth of field, distortion occlusion); without it no depth of
field and distortion drawn over nearer geometry (fallback not implemented); without `EXT_frag_depth` the in-shader
depth test; GLSL ES 1.00 with GL4ES' int-overload helpers (which NVIDIA's own ES driver rejects: step 3). Cost of
keeping it: `-sMIN_WEBGL_VERSION=1` (Emscripten keeps its WebGL 1 code and extension names), the raw depth-stencil
path in the driver (about 25 raw GLES calls, below), the `es2` check. Every desktop browser with WebGL 2 (all
current Chrome, Firefox, Safari 15+) never takes it. Dropping it would be `MIN_WEBGL_VERSION=2`, `GLESVersion` gone,
and the raw depth path and its `#ifdef __EMSCRIPTEN__` deleted.

## Step 6: "GLES behind GL4ES' back" (commit `b6f5ced`)

The depth-stencil texture through GL4ES on ES3 first failed the self-test (step 2's experiment table). Experiment
G (`logs/f3x/fx-selftest-expG.out`): the same GL4ES path with the texture UNBOUND through GL4ES after its creation
(`glBindTexture(GL_TEXTURE_2D, 0)`, as the raw path always did): **15 passed, 0 failed, 0 GL errors**. Left bound,
a later GL4ES texture call that relies on "the bound texture" reached the depth texture (which call exactly was
not traced; the unbind is what the raw path did all along). With that, on OpenGL ES 3 / WebGL 2 the depth-stencil
texture, the depth-stencil renderbuffers (multisampled; and the masked no-depth-texture case) and their attachments
(one `DEPTH_STENCIL_ATTACHMENT`) go through GL4ES, and so does the self-test's depth upload.
Run 4 (`logs/f3/twin-es3-run4.txt`): boot PASS, selftest PASS (15/15 both), progtest PASS (7760/7760 identical),
es2 PASS; `es3-nodepthtex` (ES3, -glnodepthtexture, renderbuffer through GL4ES) and `es3-aa-map` (ES3, MSAA):
0 GL errors.

The list (all in `Source/UC2GLDrv/Src/UC2GLRenderDevice.cpp`; 58 `Raw_` call sites in the source, unchanged, because
the ES 2 / WebGL 1 path keeps them):

| Function | Raw calls | Runs on ES 3 / WebGL 2? | Why it stays raw (where it does) |
|---|---|---|---|
| `CreateDepthStencilTexture` | 9 | **no** (through GL4ES) | ES 2: GL4ES' unsized/16-bit depth, rejected by WebGL 1 |
| `CreateDepthStencilRenderbuffer`, `DeleteDepthStencilRenderbuffer` | 4 + 1 | **no** | ES 2: GL4ES splits depth and stencil |
| `RawBindFramebuffer`, `AttachDepthStencilTexture`, `AttachDepthStencilRenderbuffer` | 3 + 5 + 5 | **no** | ES 2: WebGL 1 wants one `DEPTH_STENCIL_ATTACHMENT`, GL4ES' ES 2 path splits it |
| `UploadMainDepth` (self-test) | 6 | **no** | ES 2: as above |
| `CompressedTexImage2D` | 5 | yes | GL4ES always decodes DXT in software (any ES version); a fork change could pass S3TC through where the hardware has it (candidate) |
| `TexImage2D` (engine textures) | 7 | yes | GL4ES regenerates mip chains from level 0 (seen again in the combiner test, step 4); ES3 has `GL_TEXTURE_MAX_LEVEL`, so a fork change passing levels and max level through would let these and the driver's chain completion go (candidate, not done) |
| `PrepareGL4ESTextureZero` | 5 | yes | WebGL 2 has no default texture either |
| `FindRawExt`, `DetectCapabilities`, `CheckGLError` | 4 + 1 | yes | queries of the hardware itself (no state) |

So on WebGL 2 the calls that change GLES state behind GL4ES are now the texture uploads (S3TC and engine
textures) and texture zero: 17 sites instead of 50; the queries stay. On ES 2 / WebGL 1 nothing changed.

## Step 7: the browser with the final wasm (`b6f5ced`, sha256 `9208758a04e50083a3a293534abec89d999ff45b01aa34b1cf1d00ca1748a596`)

`yarn build` with this artifact; `serve-web/SDLLaunch.wasm` = `9208758a...` before and after each run. Twin
references: `logs/f3/twin-ref/ref-menu-box-8.png`, `ref-map-box-4.png` (ES 3 twin, 4x MSAA, UI box, AutoFOV).
- menu test (`e2e-run-final.txt`): binding `WebGL 2 through GL4ES`, `drawn 4x multisampled`, 97 + 405 shaders,
  `frame 600, 0 GL errors`, **menu vs twin 7.812** (tolerance 10), PSS 1404 MiB, cgroup peak 2780 MiB; FAILS only on
  the spec's literal "WebGL 1" check.
- map test (`e2e-run-final-DM-Dea.txt`): **map vs twin 2.922** (tolerance 3), 0 GL errors, 53.02 ms/frame (load 14-16),
  PSS 1791 MiB, cgroup peak 3389 MiB; fails only on the same literal check.
- play tests (`e2e-run-final-keyboa.txt`): **2 passed** (keyboard/mouse path, D-6 mouse look).
- DM-Praxis: no spec of the frontend takes a picture of it (the boot spec uses DM-Deadbolt); not done.
- The spec needs (frontend agent, after the merge): the two "WebGL 1" regexes (`uc2-wasm-boot.spec.js:293, 335`) and
  the test title; its twin references made with `UC2_UIBOX=1 UC2_AUTOFOV=1` (the D-2 UI box), as here.
