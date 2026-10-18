Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# Y-5: two web defects the launcher E2E found, fixed as engine-ut2004 does

Branch `y5-web` (worktree `<WorkUC2>/engine-uc2-y5-web`, from main `cfe0918`); frontend branch `uc2-y5-web`
(worktree `<WorkUC2>/ufrontend-y5`, from ufrontend main `e1c695d`). Logs `<WorkUC2>/logs/y5/`. Newest step at the
bottom.

The defects (Y-4, `ufrontend/docs/migration/notes/uc2-real-e2e.md`, "Findings the new tests made"; the two
`test.fail` tests of `packages/e2e/uc2-wasm-launcher.spec.js`):

1. **renderer-absent**: `-OPENGL` typed in the engine options kills the web game: the web build links
   `UC2GLDrv.UC2GLRenderDevice` (the GL4ES device's parent class), so `SelectRenderDevice` took it ("-OPENGL selects
   render device UC2GLDrv.UC2GLRenderDevice"); its desktop binding cannot make a context in a browser
   (`UC2GLBinding.h`: `UC2GL_HAS_DESKTOP_BINDING` 0 under Emscripten, `BindingCreateContext` returns 0) and
   `USDL2Viewport::OpenWindow` failed `check(RenDev)` (`SDL2Viewport.cpp:442`).
2. **resolution-dsf-exact**: at a 125 % display a 1023x767 CSS canvas is drawn 1278x959, not 1279x959.

## What engine-ut2004 does (read-only `/home/ion/WorkUnreal/engine-ut2004`)

- Renderer switches: `Source/SDLLaunch/Src/SDLLaunch.cpp:977-988` (`GRenderSwitches`: `-GL4ESRENDERER`,
  `-OPENGLRENDERER`, `-SOFTWARERENDERER`, `-NULLRENDERER`) and `SelectRenderDevice` `:1015-1038` (the warning at `:1034`): a switch whose
  device `IsRenderDeviceAvailable` (`:188`, StaticFindObject over the registered classes) selects it; otherwise
  `debugf(NAME_Warning, "ufront 2.9: render device %s not available (not compiled into this build) -> keeping the
  default; -%s ignored")` and the ini's device boots. Its web build does not link OpenGLDrv at all
  (`Source/SDLLaunch/CMakeLists.txt:75`: "no OpenGLDrv: no desktop GL in a browser"), so "in this binary" there
  also means "can run here". UC2's switch set is `-NULLRENDERER`, `-OPENGL`, `-GL4ES` (our `SDLLaunch.cpp`), and
  our message for an absent class was already the same idea ("-%s: render device %s is not in this build, ignored").
- Width alignment: `Source/SDL2Drv/Src/SDL2Viewport.cpp:627-632` (OpenWindow) and `:882-884` (ResizeViewport):
  `#if !defined(__EMSCRIPTEN__)` around `Align(NewX,2)`, comment "ufront 2.71: not on the web — Align rounds up, and a
  backing store 1 px wider than the canvas box is a fractional rescale in the browser (blur). PixoDrv, the only
  even-width consumer, is off there." Its device-pixel size helper (`:32-44`, `ufrontCanvasDevicePixelSize`) rounds
  with `lround`, no evenness.

## Changes here

- `Source/SDLLaunch/Src/SDLLaunch.cpp` (`GRenderSwitches`, `SelectRenderDevice`): each switch also says whether its
  device runs on this platform (`UC2_DESKTOP_GL_RUNS` = `UC2GL_HAS_DESKTOP_BINDING` where UC2GLDrv is linked). A
  class that is linked but cannot run here is logged `-OPENGL: render device UC2GLDrv.UC2GLRenderDevice cannot run on
  this platform, ignored: keeping RenderDevice=<the ini's>` (NAME_Warning, as engine-ut2004's) and nothing is set: the
  ini's device (web Default.ini: UC2GL4ESDrv.UC2GL4ESRenderDevice) boots. Natively every entry runs (unchanged). No
  new switch.
- `Source/SDL2Drv/Src/SDL2Viewport.cpp` OpenWindow and ResizeViewport: `Align(NewX,2)` under
  `#if !defined(__EMSCRIPTEN__)`, as engine-ut2004 2.71.
- `Source/UC2GL4ESDrv/Src/UC2GL4ESDrv.cpp` `BindingCreateContext` (web): the canvas's device-pixel width was also
  rounded down to even (`& ~1`, "the viewport keeps the width even"): removed, so an odd CSS width gives an odd
  backing (H-4's notebook noted this rounding).

## Steps

### 1. Builds (2026-10-12)

`bin/capped 6G 3600`: `cmake --preset native && cmake --build build-native -j4`, exit 0 (not hit); then
`cmake --preset wasm-perf && cmake --build build-wasm-perf -j4` (with `<WorkUC2>/env.sh`), exit 0 (not hit; a first
attempt started in parallel with the native build stopped at configure: the wasm build takes the host's compiled
scripts from `build-native/Scripts/System`, which did not exist yet). wasm sha256
`2b59806168dca21ce59105b0de3613d4bc2ae08b0c8cf637ef510e2f66eafcd5`.

### 2. Native: unchanged (numerically checked)

- `M11_SERIAL=1 M11_DESKTOP_TREE=y5 M11_TWIN_TREE=y5t tools/dev/m11-twin.sh boot selftest`
  (`logs/y5/m11-twin-boot-selftest.txt`): **PASS m11-boot** (`-gl4es` = `UC2GL4ESDrv.UC2GL4ESRenderDevice`, OpenGL ES 3
  through GL4ES, 97 fragment + 405 vertex shaders, 0 GL errors, Mesa errors 0), **PASS m11-selftest** (desktop 15/15, 0
  GL errors in 844 frames; twin 15/15, 0 in 1165), PASS m11-leftovers.
- The three switches natively (`tools/dev/run-uc2-shot.sh y5 sw-<SW> 35 0 '' -<SW>`, `logs/y5/sw-*.out`):
  `-OPENGL selects render device UC2GLDrv.UC2GLRenderDevice` + `binding: desktop GL through SDL`;
  `-NULLRENDERER selects render device Engine.NullRenderDevice`; `-GL4ES selects render device
  UC2GL4ESDrv.UC2GL4ESRenderDevice` + `binding: OpenGL ES 3 through GL4ES`. As before.

### 3. Post-processing at an odd back buffer (natively, a diagnostic binary)

Natively the width stays even, so a 1279-wide back buffer needs a diagnostic binary: the two `#if
!defined(__EMSCRIPTEN__)` of SDL2Viewport.cpp set to `#if 0` locally, `cmake --build build-native --target SDLLaunch`,
the binary copied to `run/y5/System/uc2-oddwidth.bin`, the source restored and rebuilt: the restored `uc2.bin` is
byte-identical to the one before (sha256 `87272bce...04eb` both times; nothing of the diagnostic is committed).
Self-test as `m7-effects.sh selftest` does it (DM-Deadbolt, pinned, `CheckGLErrors`, `POSTSELFTEST` at 44 s), at
`UC2_RES=1279x959`, Mesa llvmpipe:

| run | back buffer | pp-sizes | summary | GL errors |
|---|---|---|---|---|
| diagnostic, desktop | 1279x959 | PASS (639x479+depth, 320x240 ... none unexpected/missing) | 12 passed, 3 failed | 0 in 430 frames |
| diagnostic, GL4ES twin | 1279x959 | PASS (the same targets) | 12 passed, 3 failed | 0 in 369 frames |
| diagnostic, desktop | 1279x960 (odd width only) | PASS | 12 passed, 3 failed | 0 in 445 |
| **main's code** (`uc2.bin`), desktop | asked 1279x959 -> **1280x959** (odd height only) | PASS | **12 passed, 3 failed** | 0 in 483 |
| main's code, desktop | 1280x960 | PASS | 15 passed, 0 failed | 0 in 498 |

The three that fail at any odd dimension are `depth-copy`, `bb-copy`, `bb-copy-ref`: their CPU model expects
target pixel (x,y) = back-buffer pixel (2x+1,2y+1) exactly, i.e. a target of exactly half the back buffer. Epic's code
(`UC2GLPostProcess.cpp` `GetBackBufferCopyTarget`: `FullScreenSizeX * 0.5` truncated; `UpdateBackBufferCopyTarget` and
`CopyDepthBuffer` map the whole viewport onto the whole target) gives floor(W/2) texels for W pixels, a scale of
W/floor(W/2) (1279/639 = 2.0016, 959/479 = 2.0021), so the sampled source texel drifts by up to one back-buffer pixel
across the picture (worked out for 959 lines: the depth copy's sample passes 2y+2 from about row 180). The copies are
read back over the same full range, so the picture stays registered; only the self-test's exact-half model breaks.
**This is not new with Y-5**: main's own binary fails the same three at 1280x959, a size every native window with an
odd height already has, and the web had 959 lines at 125 % before this change. (Labels: the failing checks and main's
identical result are numerically checked; "the picture stays registered" is reasoned from the code plus the pictures
below, not measured.)

Pictures at 1279x959 with post-processing on (shot at 46 s; `logs/y5/oddpic-{desktop,gl4es}.png`): the ceiling tube's
glow, the poster, the pawn and the HUD are right, nothing skewed or shifted, 0 GL errors in 412 / 424 frames; desktop
and twin differ by mean 3.06 (the animated poster and the pawn). Seen only.

A side finding: Epic's `appCreateBitmap` (`Core/Src/UnMisc.cpp`, the `SHOT` command and `-glshot`) writes 24-bit rows
without the 4-byte row padding the BMP format asks for, so a screenshot at an odd width is a malformed BMP (PIL:
"image file is truncated"; `run-uc2-shot.sh` then converts nothing). engine-ut2004's copy (`Core/Src/UnMisc.cpp:1789`)
is the same. The pictures above were converted by reading the rows unpadded. Not changed here (Epic's code, as
engine-ut2004 keeps it); listed as an open question.

### 4. Web: the launcher E2E (numerically checked: DOM sizes and log lines; the menu by OCR)

Frontend worktree `ufrontend-y5` (branch `uc2-y5-web`, `173c14e`): the two `test.fail` marks removed;
renderer-absent expects the new line, resolution-dsf-exact also checks the GL device's `main framebuffer`.
`corepack yarn build --target uc2 --engine-artifact <this worktree>/build-wasm-perf/Source/SDLLaunch` (capped 6G);
runner `logs/y5/run-e2e.sh` (Y-4's, pointed here: refuses with another e2e-mem-guard, ports 8080/8081/9000 taken or
load 14+; sha256 of `serve-web/SDLLaunch.wasm` against this artifact before and after; null sink loaded and unloaded
by module index; `bin/capped 7G 3600 -- node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=23`). One harness
machine-wide (another agent's boot run was waited out).

- `launcher-1`, `uc2-wasm-launcher.spec.js`: **15 passed (10.2 min)**, ceiling not hit, guard peak 14.2 GB
  machine-wide (5.7 at start). `[ABSENT] ok ... -OPENGL: render device UC2GLDrv.UC2GLRenderDevice cannot run on this
  platform, ignored: keeping RenderDevice=UC2GL4ESDrv.UC2GL4ESRenderDevice`, the GL4ES device made, the main menu by
  OCR, no fault. `[DSF]`/`[DSFX] canvas {"w":1279,"h":959,"css":"1023x767","dpr":1.25}; UC2GLDrv: main framebuffer
  1279x959`.
- `boot-1`, `uc2-wasm-boot.spec.js`: **14 passed (5.1 min)**, guard peak 13.6 GB machine-wide.
- wasm sha256 `2b59806168dca21ce59105b0de3613d4bc2ae08b0c8cf637ef510e2f66eafcd5` before and after both; no `y5sink`
  left.

### Open questions

- The ini path of defect 1: an `Avaris.ini` on the web naming `RenderDevice=UC2GLDrv.UC2GLRenderDevice` (only by hand,
  e.g. the launcher's ini editor) would still assert: `ReplaceMissingDrivers` asks "is the class linked", and it is.
  engine-ut2004 there picks the best GL device present (its DevicePreference); ours replaces a missing render device
  with `Engine.NullRenderDevice` (natively too, e.g. a retail ini's D3DDrv). Not changed (outside the switch fix;
  natively unchanged was the brief).
- The post-process self-test (`depth-copy`, `bb-copy`, `bb-copy-ref`) expects an exact half-size copy and fails at any
  odd back-buffer dimension, on main as well (1280x959). A model that follows Epic's full-range mapping
  (W/floor(W/2)) would make it a check at every size; worth a follow-up, since browser canvases are often odd now.
- Epic's `appCreateBitmap` writes unpadded BMP rows (malformed at odd widths), as engine-ut2004's copy does.
