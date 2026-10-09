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
