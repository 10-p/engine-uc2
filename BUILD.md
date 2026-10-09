# Building engine-uc2 (native, Windows, WebAssembly)

This is the private engine repo for the Unreal Championship 2 port: engine source only. Game content (maps,
textures, sounds, movies, the compiled `.u` packages) lives out of tree in `<WorkUC2>/assets/uc2` and is never
tracked here; no build step reads it (the web build preloads only the two boot inis of `Engine/Config/web`).
Run trees are assembled from a build and the asset tree by `tools/dev/mkrun-uc2.sh`.

> Status (2026-10-09): every preset below configures and builds. `native` (the reference), `native64`,
> `windows`, `windows64` and `wasm-perf` are built at every merge; `arm64` was configured and built for the
> first time in F-4c (`docs/porting/analysis/platform-f4.md`); `native64-uchar`, `native64-asan`, `wasm`,
> `wasm-debug` are opt-in.

## Prerequisites

The same system packages as engine-ut2004 (32-bit multilib GCC, MinGW-w64 posix, the X11/GL/audio `-dev`
packages in both architectures, cmake, ninja or make, Node, wine, xvfb, ffmpeg). On this machine they are
already installed. Emscripten 5.0.7 is private to the workspace: `source /home/ion/WorkUC2/env.sh`.

## Presets

| Preset | Output dir | What |
|---|---|---|
| `native` | `build-native/` | Linux x86, 32-bit (`-m32`): the default and the reference for wasm32 |
| `native64` | `build-native64/` | Linux x86_64 (LP64; M13) |
| `native64-uchar` | `build-native64-uchar/` | x86_64 with `-funsigned-char` (arm's char; an audit build) |
| `native64-asan` | `build-native64-asan/` | x86_64 with AddressSanitizer + UBSan |
| `arm64` | `build-arm64/` | Linux aarch64, cross (`g++-aarch64-linux-gnu`; `ucc-bin` runs under `qemu-aarch64`) |
| `wasm` | `build-wasm/` | Web, `-O2 -g` dev build |
| `wasm-debug` | `build-wasm-debug/` | Web, `-O0` |
| `wasm-perf` | `build-wasm-perf/` | Web, `-O3`, what ships |
| `windows` | `build-windows/` | Windows x86, MinGW-w64 cross |
| `windows64` | `build-windows64/` | Windows x86_64, MinGW-w64 cross (LLP64; M13) |

```
cmake --preset native
cmake --build build-native -j12
#   -> build-native/Source/SDLLaunch/uc2.bin, build-native/Source/UCC/ucc-bin
```

## Web (Emscripten; M4w, M11)

```
source /home/ion/WorkUC2/env.sh                 # Emscripten 5.0.7, private to the workspace ($EMSDK)
cmake --preset wasm-perf
cmake --build build-wasm-perf -j4               # inside bin/capped 6G (CLAUDE.md)
#   -> build-wasm-perf/Source/SDLLaunch/SDLLaunch.{js,wasm,data} + meta.json   (the browser artifact)
#   -> build-wasm-perf/Source/UCC/ucc-bin.{js,wasm}                           (the same engine under Node)
```

- The artifact names are fixed (`SDLLaunch.js`, `.wasm`, `.data`, `meta.json`): the frontend (`ufrontend`,
  target `uc2`) consumes them (`yarn build --target uc2 --engine-artifact <dir>`). The page must be
  cross-origin isolated (SharedArrayBuffer) and support OffscreenCanvas: the engine runs on a worker
  (`-sPROXY_TO_PTHREAD`), yields to the browser once per frame through Asyncify (a 0 ms timer task, as
  engine-ut2004), reads game files through the frontend's WasmFS backend (`vendor/glue/wasmfs-shim.js`) and
  has an explicit 2 GB memory ceiling. Every link flag and why: `Source/SDLLaunch/cmake/Web.cmake`.
- C++ exception catching is off for the engine's code (as engine-ut2004): the package loaders fail soft on the
  web instead of throwing; openal-soft alone is compiled with `-fexceptions` (`cmake/UC2Web.cmake`).
- Boot configuration: `Engine/Config/web/{Default,DefUser}.ini`, generated from the native files by
  `tools/dev/sync-web-ini.py` and preloaded as `/System/Default.ini`, `/System/DefUser.ini`.
- `ucc-bin.js` under Node is the wasm32 test vehicle (`cd <run>/System && node ucc-bin.js verifyclasses`;
  `CENSUS_NODE=1 tools/dev/map-census.sh build-wasm-perf` loads every map).

The three web modes (`UC2_WEB_PERF`, `UC2_WEB_DEBUG`, set by the presets):

| Preset | Compile and link | For |
|---|---|---|
| `wasm-perf` | `-O3 -g0`, no assertions, `--emit-symbol-map` (a side file: never publish it) | what ships |
| `wasm` | `-O2`, link `-g2` (names, no DWARF), assertions and stack checks | development |
| `wasm-debug` | `-O0 -g` | last resort, under Node only (a DWARF module does not fit a browser here) |

### `meta.json` records what was fetched

As engine-ut2004's (ufront 2.70): `toolchain` names Emscripten's version and every repository FetchContent
pulled for the web artifact as `<repo>@<branch>`, never a commit (the branch tip is what a rebuild takes):
`gl4es` (`10-p/gl4es@fix/cubemap-bind-active-unit`) and `openal-soft` (`10-p/openal-soft-web@ufront/web`). The
values come from the cache variables `FetchContent_Declare` read (`cmake/ArtifactDeps.cmake`, engine-ut2004's
file: `GL4ES_GIT_*`, `OPENALSOFT_GIT_*`); a local checkout override shows up as `local:<path>`. SDL2 on the web
is Emscripten's own port (named by the Emscripten version). `buildFlags` is descriptive and hand-maintained
(`Source/SDLLaunch/cmake/emit-artifact-meta.mjs`); `checksums` are of the files next to it;
`provenance.gitSha` is resolved when the artifact is linked. A branch is re-fetched on every configure, never
by a plain `cmake --build`.

## Windows (MinGW-w64, cross-compiled here; M12)

```
bin/capped 4G 600 -- cmake --preset windows                      # i686-w64-mingw32-g++-posix (GCC 13)
bin/capped 6G 1800 -- cmake --build build-windows -j8             # -> Source/UCC/ucc.exe, Source/SDLLaunch/uc2.exe,
                                                                  #    _deps/openalsoft-build/OpenAL32.dll
tools/dev/mkrun-uc2.sh build-windows m12                          # run tree <WorkUC2>/run/m12 (exe + dll in System/)
tools/dev/m12-wine-smoke.sh                                       # every check under wine, PASS/FAIL lines
tools/dev/package-windows.sh build-windows                        # <WorkUC2>/run/m12-package: a folder to copy to Windows
```

(`bin/capped` is `<WorkUC2>/bin/capped`; every run goes through it, CLAUDE.md.)

- Needs the Debian/Ubuntu packages `g++-mingw-w64-i686-posix` (or any MinGW-w64 13+ with the posix thread model)
  and, to run it here, `wine` (10, WoW64) and `xvfb`. No Visual Studio, no MSYS2. The same CMake works from an
  MSYS2 `mingw32` shell on Windows (the toolchain file only adds what a cross build cannot infer).
- What is built: every package as for Linux, statically linked into `uc2.exe` (Windows subsystem) and `ucc.exe`
  (console), `-static`: no MinGW runtime DLL. OpenAL Soft is fetched and built from source as `OpenAL32.dll`
  and loaded at run time (absent = no sound). SDL2 is static. The imports of both exes are Windows system DLLs
  only (`objdump -p … | grep 'DLL Name'`; `tools/dev/package-windows.sh` checks it).
- The platform layer is Core's GCC one (`UnGnuG.h`, `UnUnix.cpp` with `_WIN32` branches, `FFileManagerUnix.h`,
  POSIX threads through winpthreads), not Epic's MSVC `UnVcWin32.*`. Why, and every Windows-specific change:
  `docs/porting/analysis/windows.md`.
- Under wine: `tools/dev/m12-wine.sh <dir> <prog.exe> [args]` (inside `capped`) runs one program in the private
  prefix `<WorkUC2>/.cache/wine-m12` and kills its wineserver afterwards.
- `windows64` (x86_64): see "64-bit" below.

## arm64 (aarch64 Linux, cross-compiled; F-4c)

```
bin/capped 4G 900 -- cmake --preset arm64                         # aarch64-linux-gnu-g++ (GCC 15), LP64
bin/capped 6G 7200 -- cmake --build build-arm64 -j4 --target UCC  # ucc-bin
tools/dev/mkrun-uc2.sh build-arm64 f4-arm64
(cd ../run/f4-arm64/System && ../../../bin/capped 2G 900 -- qemu-aarch64 -L /usr/aarch64-linux-gnu ./ucc-bin verifyclasses)
```

The same LP64 code as `native64` (64-bit layout contract, placeholders, `-fsigned-char`). This machine has no
arm64 X11/GL development packages, so the fetched SDL2 is configured without X11 (the client builds for the
offscreen/KMS paths only); a desktop arm64 client needs them through multiarch (`BLOCKERS.md`). The layout
hazards engine-ut2004 met on Android and how each one stands here: `docs/porting/analysis/platform-f4.md`.

## 64-bit (M13)

```
bin/capped 4G 900 -- cmake --preset native64
bin/capped 6G 3600 -- cmake --build build-native64 -j6
file build-native64/Source/UCC/ucc-bin                  # must say ELF 64-bit ... x86-64
tools/dev/mkrun-uc2.sh build-native64 m13                # run tree <WorkUC2>/run/m13
(cd ../run/m13/System && ../../../bin/capped 3G 300 -- ./ucc-bin verifyclasses)   # 64-bit layout proof
tools/dev/m13-64bit.sh                                   # every check, PASS/FAIL lines (needs run/m13-32 too)
```

The shipped `.u` files were compiled for 32-bit pointers; the 64-bit build keeps their 4-byte pointer slots in
bytecode (object indices), gives script `int`s that stand in for C++ pointers their C++ size when classes link
(the placeholder table in `Source/Core/Src/UnClass.cpp`), packs every script mirror to 4 and stops at start-up if
a native class's C++ size differs from its script size. `ucc-bin verifyclasses` proves it on the running ABI;
`tools/dev/m13-layout-check.py` compares every class, script struct and event parameter struct with the
binary's DWARF. Integer-to-pointer casts are errors at 64-bit; `tools/dev/m13-cast-census.sh <build.log>`
counts pointer-to-int narrowing (0). Details, every site, and what no test reaches:
`docs/porting/analysis/64bit.md`. 32-bit is still the default and the reference (wasm32 has its layout).

## Scripts (M14): `ucc-bin make`

The game runs on the shipped compiled scripts (the 17 `.u` of the asset tree's `System/`, plus the 30 load-time
patches of `Source/Engine/Inc/ScriptPatches.h`). The native 32-bit Linux build also contains Epic's script
compiler (the `Editor` package linked into `ucc-bin`), which rebuilds all 17 from `Source/<Pkg>/Classes`:

```
bin/capped 6G 3600 -- cmake --build build-native -j4      # ucc-bin with Editor (32-bit Linux only)
tools/dev/script-build.sh                                 # assemble, make, strip, compare
tools/dev/script-build.sh --tree m14-hdr --headers assemble make   # also export the C++ headers, diff them
```

- Output: `<WorkUC2>/run/m14-make/System/*.u` (what ships: 15 stripped like Epic's, UCMenu and Editor with their
  text), `<WorkUC2>/run/m14-make/Unstripped/*.u` (all with script text, for debugging). Logs and the comparison in
  `<WorkUC2>/logs/m14/` (`m14-make-compare.txt`, `m14-make-patches.txt`). Nothing outside the tree is written;
  the asset tree, `Source/` and the run trees are never written by it. Exported headers go to the tree's own
  `<Pkg>/Inc`, never over `Source/*/Inc` (our headers carry deliberate changes: compare, do not regenerate).
- It reproduces Epic's pipeline (`Source/BuildProcess/sync_avaris.pl`): `ucc make -nobind -noh` with the Xbox
  inis, then `ucc editor.stripsource` with the PC inis, conforming each package to the shipped one so they line
  up; `tools/dev/script-diff.py` then compares object by object. Every difference is explained
  (`docs/porting/analysis/scripts.md`); the rebuilt packages contain what the 30 patches carry.
- A run tree with the rebuilt packages: `UC2_SCRIPTS=<WorkUC2>/run/m14-make/System tools/dev/mkrun-uc2.sh
  build-native <tree>`. The script patcher leaves rebuilt packages alone (it patches only the first-generation
  shipped files it was made for) and logs `ScriptPatcher: <Pkg> ... patch not applied`.
- `tools/dev/m14-scripts-check.sh`: the game checks (verifyclasses, menu, a match, controls, audio, effects,
  the 58-map census, movies, network) on the rebuilt packages, PASS/FAIL lines.
- The web build and the asset tree keep the shipped `.u` until the owner decides otherwise.
- `make` exits 1 when there was any warning (Epic's `ucc`); the verdict is the log's last line,
  `Success - 0 error(s), N warning(s)`. Memory: well under 3G; 10 s per full build.

## Options

| Option | Default | Meaning |
|---|---|---|
| `UC2_ALLOW_64BIT` | OFF | build x86_64 instead of demoting to `-m32` |
| `UC2_UNSIGNED_CHAR_AUDIT` | OFF | `-funsigned-char` instead of `-fsigned-char` (audit build) |
| `UC2_SANITIZE` | empty | `-fsanitize=` list, e.g. `address,undefined` |
| `UC2_WEB_PERF`, `UC2_WEB_DEBUG` | OFF | the three web build modes (see the presets) |

## Things that are fixed on purpose

- Static link of every package (`__STATIC_LINK=1`), as the Xbox build did.
- ANSI `TCHAR` on every target (`NO_UNICODE_OS_SUPPORT=1`); no `-fshort-wchar`.
- `_XBOX`, `XBOX_ASSET_FORMAT`, `LIN_LOAD`, `LIN_SAVE`, `USE_LINEAR_LOADING` are never defined.

## Fetched libraries

SDL2 (`libsdl-org/SDL@release-2.32.x`; native and Windows), OpenAL Soft (`10-p/openal-soft-web@ufront/web`; native
as a shared library, Windows as `OpenAL32.dll`, web static) and GL4ES (`10-p/gl4es@fix/cubemap-bind-active-unit`;
Linux and web) are fetched by CMake at configure time into `build-*/_deps/`, on their branches (not pinned: the
latest wins; the root `CMakeLists.txt` says why each option is set). The first configure of each preset therefore
needs network access; later configures re-fetch the branch tips. Vendored in `Thirdparty/`: zlib, Ogg/Vorbis,
Theora (decoder).

## Frame rate

`[Engine.GameEngine] FpsLogSeconds=<s>` (default 0 = off; console `FPSLOG <s>`) logs one `FPS:` line every `<s>`
seconds: frames, fps, frame time avg/min/max/p99, the 1% low. `tools/dev/fps-bench.sh <tree> <map> <seconds>
[WxH]` runs a fixed camera turn on this machine's GPU, headless, and prints one line. Nothing caps a standalone
game's frame rate natively (`UseVSync=False`, the engine's tick limit is 0 outside network play).
