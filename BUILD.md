# Building engine-uc2 (native, Windows, WebAssembly)

This is the private engine repo for the Unreal Championship 2 port: engine source only. Game content (maps,
textures, sounds, movies, the compiled `.u` packages) lives out of tree in the game folder (`<workspace>/assets/uc2-assets`
here) and is never tracked. One build step reads it: the script patches (below, "Script patches"), which turn the
game's shipped `.u` into our patched packages; `-DUE_SCRIPT_PATCHES=OFF` builds without them and reads nothing.
Run trees are assembled from a build and the asset tree by `tools/dev/mkrun-uc2.sh`.

## Quick start

```
# Linux, with the game folder (System/ with the 17 shipped .u and Epic's inis, Maps/, Textures/, ...):
cmake --preset native -DUE_ASSETS_DIR=/path/to/UC2
cmake --build build-native -j8
#   -> build-native/Source/SDLLaunch/uc2.bin, build-native/Source/UCC/ucc-bin,
#      build-native/Scripts/System/{UCGameplay,GUI,UCMenu}.u (the patched script packages)

# the same without the script patches (the game runs the shipped .u; no game folder needed to build):
cmake --preset native -DUE_SCRIPT_PATCHES=OFF
cmake --build build-native -j8

# the web build after it (it takes build-native/Scripts; Emscripten 5.0.7 in the environment, here
# `source /home/ion/WorkUnreal/env.sh`):
cmake --preset wasm-perf && cmake --build build-wasm-perf -j4
```

> Status (2026-10-09): every preset below configures and builds. `native` (the reference), `native64`,
> `windows`, `windows64` and `wasm-perf` are built at every merge; `arm64` was configured and built for the
> first time in F-4c (`docs/porting/analysis/platform-f4.md`); `native64-uchar`, `native64-asan`, `wasm`,
> `wasm-debug` are opt-in.

## Prerequisites

The same system packages as engine-ut2004 (32-bit multilib GCC, MinGW-w64 posix, the X11/GL/audio `-dev`
packages in both architectures, cmake, ninja or make, Node, wine, xvfb, ffmpeg). On this machine they are
already installed. Emscripten 5.0.7 is private to the workspace: `source /home/ion/WorkUnreal/env.sh`.

The workspace (`<workspace>` = `<repo>/..` = `/home/ion/WorkUnreal`, the home since 2026-10-11) holds the game folder
`assets/uc2-assets` (the cooked tree) and the retail disc `assets/uc2-retail-assets`; the build finds both without
options (`cmake/UC2Scripts.cmake`), and `<workspace>/bin/build-all-engines.sh engine-uc2` builds every preset in order.

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
source /home/ion/WorkUnreal/env.sh              # Emscripten 5.0.7, private to the workspace ($EMSDK)
cmake --preset wasm-perf
cmake --build build-wasm-perf -j4               # inside bin/capped 6G (CLAUDE.md)
#   -> build-wasm-perf/Source/SDLLaunch/SDLLaunch.{js,wasm,data} + meta.json   (the browser artifact)
#   -> build-wasm-perf/Source/UCC/ucc-bin.{js,wasm}                           (the same engine under Node)
```

- `scripts/` beside them (T3): the patched script packages (a copy of the build's `Scripts/System`, empty with
  `UE_SCRIPT_PATCHES=OFF`), listed in `meta.json` as `"scripts": [{"path": "scripts/GUI.u", "assetPath":
  "System/GUI.u", "size": …, "sha256": "sha256-…"}]`: each replaces the asset tree's file at `assetPath` (the frontend
  overlays them on its manifest, precache and published tree). The artifact relinks when they change.
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

The three web modes (`UE_WEB_PERF`, `UE_WEB_DEBUG`, set by the presets):

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
bin/capped 6G 1800 -- cmake --build build-windows -j8             # -> Source/UCC/ucc.exe (with the script compiler
                                                                  #    since S2), Source/SDLLaunch/uc2.exe,
                                                                  #    _deps/openalsoft-build/OpenAL32.dll
tools/dev/mkrun-uc2.sh build-windows m12                          # run tree <workspace>/run/m12 (exe + dll in System/)
tools/dev/m12-wine-smoke.sh                                       # every check under wine, PASS/FAIL lines
tools/dev/package-windows.sh build-windows                        # <workspace>/run/m12-package: a folder to copy to Windows
```

(`bin/capped` is `<workspace>/bin/capped`; every run goes through it, CLAUDE.md.)

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
  prefix `<workspace>/.cache/wine-m12` and kills its wineserver afterwards.
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
tools/dev/mkrun-uc2.sh build-native64 m13                # run tree <workspace>/run/m13
(cd ../run/m13/System && ../../../bin/capped 3G 300 -- ./ucc-bin verifyclasses)   # 64-bit layout proof
tools/dev/check-64bit.sh                                   # every check, PASS/FAIL lines (needs run/m13-32 too)
```

The shipped `.u` files were compiled for 32-bit pointers; the 64-bit build keeps their 4-byte pointer slots in
bytecode (object indices), gives script `int`s that stand in for C++ pointers their C++ size when classes link
(the placeholder table in `Source/Core/Src/UnClass.cpp`), packs every script mirror to 4 and stops at start-up if
a native class's C++ size differs from its script size. `ucc-bin verifyclasses` proves it on the running ABI;
`tools/dev/layout-check.py` compares every class, script struct and event parameter struct with the
binary's DWARF. Integer-to-pointer casts are errors at 64-bit; `tools/dev/m13-cast-census.sh <build.log>`
counts pointer-to-int narrowing (0). Details, every site, and what no test reaches:
`docs/porting/analysis/64bit.md`. 32-bit is still the default and the reference (wasm32 has its layout).

## Scripts (M14): `ucc-bin make`

The game runs on the shipped compiled scripts (the 17 `.u` of the asset tree's `System/`, plus the 30 load-time
patches of `Source/Engine/Inc/ScriptPatches.h`). The native 32-bit Linux build also contains Epic's script
compiler (the `Editor` package linked into `ucc-bin`). **This repository holds no script sources** (since 2026-10-11:
UnrealScript is game content, `docs/porting/analysis/scripts-removed.md`): the build compiles the patched packages from
your shipped `.u` and `patches/` (next section), and a script is changed by changing its set (`patches/README.md`,
"Changing a script").

```
bin/capped 6G 3600 -- cmake --build build-native -j4      # ucc-bin with Editor (32-bit pointers only) + Scripts/System
bin/capped 6G 600 -- cmake --build build-native --target check-headers   # generated headers current? (T2)
tools/dev/regen-headers.sh                                # regenerate them into Source/*/Inc (T2)
tools/dev/m14-scripts-check.sh                            # the game checks on the build's packages
```

- The generated C++ headers `Source/<Pkg>/Inc/<Pkg>Classes.h` (Editor, Engine, GUI, IpDrv, UCElements, UCGameplay,
  UnrealEd, UnrealGame) are pure exporter output since T2, 64-bit-correct as generated: a script `int` that holds a
  C++ pointer is written `PTRINT` from Core's placeholder table (`GNativePlaceholders`, UnClass.cpp), on any host.
  Never edit them by hand (except a class's `cpptext` and `struct native` blocks, below): change the script (its
  patch set), then `tools/dev/regen-headers.sh` (export in a scratch tree, write into `Source/<Pkg>/Inc`, CRs dropped)
  and commit them with the change. `tools/dev/check-headers.sh` (CMake target `check-headers` of the native preset,
  not in ALL) exports and compares: exit 0 current, 1 stale (unified diff printed, also in `<workspace>/logs/hdr/`), 2
  could not run. Both read the build's patched tree `build-*/Scripts/tree` by default (`--classes-root <dir>` for
  another `<dir>/<Pkg>/Classes`; CMake: `-DUE_HEADERS_CLASSES_ROOT=<dir>`) and take `--build <dir>`.
  `docs/porting/analysis/headers.md`. A patched tree has only the packages the patches rebuild: the check compiles
  those against the shipped ones (`uc-build.sh --headers`, into `build-*/Scripts/headers`) and compares their
  generated headers, except what the shipped packages do not carry (each class's cpptext, and `struct native` blocks:
  `UStruct::StructFlags` is not saved), which is taken from the committed header (`tools/dev/check-headers-cpptext.py`;
  `regen-headers.sh` keeps them as committed); a changed or added member, function, event, enum or constant is caught.
  With `-DUE_SCRIPT_PATCHES=OFF` there is no patched tree: the target says so and fails unless a root is given.
- A run tree takes the build's packages (`tools/dev/mkrun-uc2.sh build-native <tree>`). The script patcher leaves
  rebuilt packages alone (it patches only the first-generation shipped files it was made for) and logs
  `ScriptPatcher: <Pkg> ... patch not applied`; the packages that were not rebuilt are the shipped ones and are patched.
- `tools/dev/m14-scripts-check.sh`: the game checks (verifyclasses, the patcher, menu, a match, controls, audio,
  effects, the 58-map census, movies, network) on the build's packages (`build-native/Scripts/System`; `M14_SCRIPTS=<dir>`
  for another folder), PASS/FAIL lines.
- `make` exits 1 when there was any warning (Epic's `ucc`); the verdict is the log's last line,
  `Success - 0 error(s), N warning(s)`. Memory: well under 3G.
- **Dev machine only: `tools/dev/script-build.sh`**, the M14 proof tool. It rebuilds all 17 packages from Epic's leaked
  script text (`/home/ion/WorkUC2/epic-sources/<Pkg>/Classes`, read-only, outside the workspace: `$UC2_EPIC_SOURCES`, or `--classes-root <dir>`) the
  way Epic's build machine did (`Source/BuildProcess/sync_avaris.pl`: `ucc make -nobind -noh` with the Xbox inis, then
  `ucc editor.stripsource` with the PC inis, conforming each package to the shipped one) and compares the result with
  the shipped packages object by object (`tools/dev/script-diff.py`; every difference is explained in
  `docs/porting/analysis/scripts.md`). Output `<workspace>/run/m14-make/System/*.u` (`--tree`), logs in
  `<workspace>/logs/m14/`; `--build build-windows` does the same with `ucc.exe` under wine (S2); `--headers` exports the
  headers into the tree's own `<Pkg>/Inc` and diffs them with `Source/*/Inc` (which carry our patches: expect those
  differences). It is never a build input, and it needs no file of this repository's but the tools.

## Script patches (T3): the patched script packages, built by default

The game's logic is UnrealScript compiled into the 17 `.u` of the game's `System/`. This repo holds no script, only
**patch sets** (`patches/`, what each does: `patches/README.md`). By default the build makes the patched packages from
**your** game files, as part of `cmake --build` (target `scripts`, in ALL):

1. checks that the game folder's 17 `.u` are the shipped ones (`patches/shipped.sha256`; a package rebuilt from
   source is refused, with the file named: it cannot be the base);
2. decompiles each package the build needs (`tools/scripts/uc-decompile.py`; UCMenu: its shipped text) into
   `build-*/Scripts/base/<Pkg>/Classes`;
3. applies the chosen sets (`tools/scripts/uc-patches.py apply`, exact context) into `build-*/Scripts/tree`;
4. compiles them with this build's `ucc-bin make` against the shipped packages and strips them as Epic did
   (`tools/scripts/uc-build.sh`, work tree and logs in `build-*/Scripts/work`) into
   **`build-*/Scripts/System/<Pkg>.u`**: only the packages the sets change (default: UCGameplay, GUI, UCMenu).

About 25 s the first time (decompile 5 s, apply 1 s, compile and strip 17 s); each stage has a stamp, so an unchanged
input rebuilds nothing, a changed patch file rebuilds from the apply on, and a new `ucc-bin` or shipped file from
where it enters. Needs Python 3 and bash (on Windows: MSYS2).

**Which preset compiles.** The script compiler is `ucc` with the Editor package, 32-bit pointers only, and it has to
run on the build machine: the **host preset** is `native` on Linux and `windows` (MinGW i686, `ucc.exe`) on a Windows
host. Every other preset (`wasm*`, `windows`/`windows64` cross-built from Linux, `native64*`, `arm64`) copies the
host build's `Scripts/System` into its own `build-*/Scripts/System` (checked: Unreal packages; the host build's sets
are printed at configure), so **build the host preset first**. The web artifact carries them
(`build-wasm*/Source/SDLLaunch/scripts/` + `meta.json` `scripts`, below). Run trees take them:
`tools/dev/mkrun-uc2.sh <build-dir> <tree>` installs `<build-dir>/Scripts/System/*.u` over the asset tree's and says
which (`UC2_SCRIPTS=<dir>` takes them from another folder, `UC2_SCRIPTS=none` keeps the asset tree's).

| Option | Default | Meaning |
|---|---|---|
| `UE_SCRIPT_PATCHES` | ON | OFF: no `Scripts/` at all; the game runs the shipped `.u` (with Epic's load-time patches). Nothing of the game folder is read. |
| `UE_SCRIPT_PATCH_SETS` | `default` | the sets to apply, `;` or `,` separated (`patches/sets.txt` names); `default` = those marked `on` there, in its order. Dependencies are enforced (`keys-menu` needs `mouse-look`; everything needs `epic-fixes`) |
| `UE_ASSETS_DIR` | empty | the game folder: `System/` (the shipped `.u`, Epic's `DefaultXbox.ini`, `DefUserXbox.ini`, `Default.ini`, `DefUser.ini`, `Localization/`) and the content folders the scripts load (`Textures/`, `StaticMeshes/`, ...); `tools/dev/mkassets-uc2.sh` makes one from Epic's tree. Empty: the `UC2_ASSETS_DIR` environment variable (the dev tools read it too), else `<repo>/../assets/uc2` when it exists (this workspace's layout), else the configure stops and says what to pass. Engine-ut2004 names its game folder the same way (`UT_ASSETS_DIR`) |
| `UE_SCRIPT_PACKAGES_DIR` | `UE_ASSETS_DIR/System` | where the 17 shipped `.u` are, if not in the game folder (e.g. the game folder has rebuilt ones: point this at the disc's `System`) |
| `UE_SCRIPT_RETAIL_INIS_DIR` | empty | optional: the retail disc's `System/`; its `Characters.ini`, `Gametypes.ini`, `Maplists.ini` are what the shipped packages' config defaults were stripped with (without it the game folder's are used, and a few config defaults differ from the shipped ones). Empty: `<repo>/../reference/iso-extract/System` when it exists |
| `UE_SCRIPTS_DIR` | `build-native/Scripts/System` | non-host presets: the host build's packages (`build-windows/...` on a Windows host). Missing with patches ON: the configure stops ("run the native preset first, or pass UE_SCRIPTS_DIR, or -DUE_SCRIPT_PATCHES=OFF") |

- Every error stops the **configure** with what to do: no game folder, a rebuilt package in it (named, with its
  sha256 and the shipped one), an unknown set or a missing dependency, no Python/bash, no host packages.
- `cmake --build build-native --target scripts-proof`: the bytecode proofs of the pipeline
  (`tools/scripts/uc-patches-proof.sh`: P1 base + `epic-fixes` = shipped + load-time patcher, P2 the packages of
  `patches/reference.sha256` (sha256 and size of what the default build makes; regenerate with `uc-patches-proof.sh p2
  --write-reference` when a set changes on purpose), P3 determinism; minutes; P1's Engine part needs Epic's texture
  sources, dev machine only, else it leaves Engine out). Not in ALL. `sha256sum -c ../../../patches/reference.sha256` in
  `build-native/Scripts/System` checks a default build by hand.
- Changing a script = changing its patch set: `patches/README.md`, "Changing a script" (`tools/dev/uc-patches-regen.sh
  edit|make <set>`).
- `check-headers` (below) defaults to the build's patched tree.
- Windows host (MSYS2 `mingw32` shell, not run yet): the same commands with `cmake --preset windows
  -DUE_ASSETS_DIR=C:/path/to/UC2`; `uc-build.sh` runs `ucc.exe` under MSYS2's bash, and links the content folders into
  its work tree with `ln -s` (set `MSYS=winsymlinks:nativestrict` with Developer Mode, or it copies them).
- Notebook: `docs/porting/analysis/script-build-rules.md`.

## Options

| Option | Default | Meaning |
|---|---|---|
| `UE_ALLOW_64BIT` | OFF | build x86_64 instead of demoting to `-m32` |
| `UE_UNSIGNED_CHAR_AUDIT` | OFF | `-funsigned-char` instead of `-fsigned-char` (audit build) |
| `UE_SANITIZE` | empty | `-fsanitize=` list, e.g. `address,undefined` |
| `UE_WEB_PERF`, `UE_WEB_DEBUG` | OFF | the three web build modes (see the presets) |
| `UE_SCRIPT_PATCHES`, `UE_SCRIPT_PATCH_SETS`, `UE_ASSETS_DIR`, `UE_SCRIPT_PACKAGES_DIR`, `UE_SCRIPT_RETAIL_INIS_DIR`, `UE_SCRIPTS_DIR` | see "Script patches" | the patched script packages |
| `UE_HEADERS_CLASSES_ROOT` | empty | `check-headers`' script sources (default: the build's `Scripts/tree`; with the patches OFF, `Source/`) |

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
