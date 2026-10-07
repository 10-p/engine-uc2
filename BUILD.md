# Building engine-uc2

Status: M0. The presets configure; the engine packages join the build milestone by milestone
(`docs/porting/PLAN.md`). This file grows with them.

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
| `arm64` | `build-arm64/` | Linux aarch64, cross (build-only; needs `g++-aarch64-linux-gnu`) |
| `wasm` | `build-wasm/` | Web, `-O2 -g` dev build |
| `wasm-debug` | `build-wasm-debug/` | Web, `-O0` |
| `wasm-perf` | `build-wasm-perf/` | Web, `-O3`, what ships |
| `windows` | `build-windows/` | Windows x86, MinGW-w64 cross |
| `windows64` | `build-windows64/` | Windows x86_64, MinGW-w64 cross (LLP64; M13) |

```
cmake --preset native
cmake --build build-native -j12
```

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
| `UC2_ASSETS_DIR` | `../assets/uc2` | the asset tree; read only for the files the web build preloads |

## Things that are fixed on purpose

- Static link of every package (`__STATIC_LINK=1`), as the Xbox build did.
- ANSI `TCHAR` on every target (`NO_UNICODE_OS_SUPPORT=1`); no `-fshort-wchar`.
- `_XBOX`, `XBOX_ASSET_FORMAT`, `LIN_LOAD`, `LIN_SAVE`, `USE_LINEAR_LOADING` are never defined.

## Fetched libraries

SDL2 (`release-2.32.x`) and OpenAL Soft are fetched by CMake at configure time into `build-*/_deps/` (native and
Windows builds; the root `CMakeLists.txt` says why each option is set). The first `cmake --preset native` therefore needs network
access; later configures reuse the checkout.
