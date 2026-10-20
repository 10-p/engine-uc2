# Building engine-uc2 (native, Windows, WebAssembly, Android)

Engine source only. Game content (maps, textures, sounds, movies, the compiled `.u` packages) lives out of tree in the
game folder (`<repo>/../assets/uc2-assets` in the WorkUnreal folder) and is never tracked. One build step reads it: the
script patches ("Script patches" below), which turn the game's shipped `.u` into our patched packages;
`-DUE_SCRIPT_PATCHES=OFF` builds without them and reads nothing. `tools/dev/mkrun-uc2.sh` assembles a run tree from a
build and the game folder.

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

# the web build after it (it takes build-native/Scripts; Emscripten 5.0.7 in the environment,
# `source <repo>/../env.sh`):
cmake --preset wasm-perf && cmake --build build-wasm-perf -j4
```

In the WorkUnreal folder the game folder (`assets/uc2-assets`) sits beside the repo and the configure finds it
without options (`cmake/UC2Scripts.cmake`); the build reads nothing else — the retail disc
(`assets/uc2-retail-assets`) is read only by the pipeline proof's P1. The folder's `bin/build-all-engines.sh
engine-uc2` builds every preset in order.

## Prerequisites

32-bit multilib GCC, MinGW-w64 with the posix thread model, the X11/GL/audio `-dev` packages in both architectures,
cmake, make, Python 3, bash, Node, wine, xvfb and ffmpeg (the WorkUnreal folder's `setup/packages-<distro>.txt`
lists them). Emscripten 5.0.7 is private to the work folder: `source <repo>/../env.sh` sets `EMSDK`. Android: see
"Android" below.

## Presets

| Preset | Output dir | What |
|---|---|---|
| `native` | `build-native/` | Linux x86, 32-bit (`-m32`): the default, the host preset of the script build and the reference for wasm32 |
| `native64` | `build-native64/` | Linux x86_64 (LP64) |
| `native64-uchar` | `build-native64-uchar/` | x86_64 with `-funsigned-char` (arm's char; an audit build) |
| `native64-asan` | `build-native64-asan/` | x86_64 with AddressSanitizer + UBSan (run with `ASAN_OPTIONS=detect_leaks=0`) |
| `arm64` | `build-arm64/` | Linux aarch64, cross (`g++-aarch64-linux-gnu`; `ucc-bin` runs under `qemu-aarch64`) |
| `wasm` | `build-wasm/` | Web, `-O2`, development |
| `wasm-debug` | `build-wasm-debug/` | Web, `-O0` |
| `wasm-perf` | `build-wasm-perf/` | Web, `-O3`, what ships |
| `windows` | `build-windows/` | Windows x86, MinGW-w64 cross |
| `windows64` | `build-windows64/` | Windows x86_64, MinGW-w64 cross (LLP64) |

Build `native` first: every other preset copies its patched script packages. Several builds at once on one machine:
`<repo>/../bin/capped 6G 7200 -- cmake --build <dir> -j3`.

## Web (Emscripten)

```
source <repo>/../env.sh                         # Emscripten 5.0.7 ($EMSDK)
cmake --preset wasm-perf
cmake --build build-wasm-perf -j4               # inside bin/capped 6G (CLAUDE.md)
#   -> build-wasm-perf/Source/SDLLaunch/SDLLaunch.{js,wasm,data} + meta.json + scripts/   (the browser artifact)
#   -> build-wasm-perf/Source/UCC/ucc-bin.{js,wasm}                                     (the same engine under Node)
```

- `scripts/`: the patched script packages (a copy of the build's `Scripts/System`, empty with
  `UE_SCRIPT_PATCHES=OFF`), listed in `meta.json` as `"scripts": [{"path": "scripts/GUI.u", "assetPath":
  "System/GUI.u", "size": ..., "sha256": "sha256-..."}]`: each replaces the game folder's file at `assetPath` (the
  frontend overlays them on its manifest, precache and published tree). The artifact relinks when they change.
- The artifact names are fixed (`SDLLaunch.js`, `.wasm`, `.data`, `meta.json`): the frontend
  (`<repo>/../unreal-frontend-arch-private`, target `uc2`) consumes them (`yarn build --target uc2 --engine-artifact
  <dir>`). The page must be cross-origin isolated (SharedArrayBuffer) and support OffscreenCanvas: the engine runs on a
  worker (`-sPROXY_TO_PTHREAD`), yields to the browser once per frame through Asyncify, reads game files through the
  frontend's WasmFS backend (`vendor/glue/wasmfs-shim.js`), reaches the network through the glue's ABI v2 imports
  (`vendor/glue/ue_abi.h`, `net-shim.js`), renders on WebGL 2 with WebGL 1 as the fallback and has an explicit 2 GB
  memory ceiling. Every link flag and why: `Source/SDLLaunch/cmake/Web.cmake`.
- C++ exception catching is off for the engine's code: package loads, a failed map and a failed Listen fail soft with
  a logged reason instead of throwing; openal-soft alone is compiled with `-fexceptions` (`cmake/UC2Web.cmake`).
- Boot configuration: `Engine/Config/web/{Default,DefUser}.ini`, generated from the native files by
  `tools/dev/sync-web-ini.py` and preloaded as `/System/Default.ini`, `/System/DefUser.ini`.
- `ucc-bin.js` under Node is the wasm32 test vehicle (`cd <run>/System && node ucc-bin.js verifyclasses`;
  `CENSUS_NODE=1 tools/dev/map-census.sh build-wasm-perf` loads every map).

The three web modes (`UE_WEB_PERF`, `UE_WEB_DEBUG`, set by the presets):

| Preset | Compile and link | For |
|---|---|---|
| `wasm-perf` | `-O3 -g0`, no assertions, `--emit-symbol-map` (a side file: never publish it) | what ships |
| `wasm` | `-O2`, link `-g2` (names, no DWARF), assertions and stack checks | development |
| `wasm-debug` | `-O0 -g` | last resort, under Node only (a DWARF module does not fit a browser) |

### `meta.json`

`abiVersion` is `UE_ABI_VERSION` of the vendored `vendor/glue/ue_abi.h` (2). `toolchain` names Emscripten's version
and every repository FetchContent pulled for the artifact as `<repo>@<branch>`, never a commit (the branch tip is what
a rebuild takes): `gl4es` (`10-p/gl4es@fix/cubemap-bind-active-unit`) and `openal-soft`
(`10-p/openal-soft-web@ufront/web`), from the cache variables `FetchContent_Declare` reads (`cmake/ArtifactDeps.cmake`:
`GL4ES_GIT_*`, `OPENALSOFT_GIT_*`); a local checkout override shows as `local:<path>`. SDL2 on the web is Emscripten's
own port (named by the Emscripten version). `buildFlags` lists the link flags the host depends on (the WebGL versions
passed from `Web.cmake`, the rest kept in step with it in `Source/SDLLaunch/cmake/emit-artifact-meta.mjs`);
`checksums` are of the files next to it; `provenance.gitSha` is resolved when the artifact is linked. A branch is
re-fetched on every configure, never by a plain `cmake --build`.

## Windows (MinGW-w64, cross-compiled)

```
bin/capped 4G 600 -- cmake --preset windows                      # i686-w64-mingw32-g++-posix (GCC 13)
bin/capped 6G 1800 -- cmake --build build-windows -j8             # -> Source/UCC/ucc.exe (with the script compiler),
                                                                  #    Source/SDLLaunch/uc2.exe,
                                                                  #    _deps/openalsoft-build/OpenAL32.dll
tools/dev/mkrun-uc2.sh build-windows m12                          # run tree <repo>/../run/m12 (exe + dll in System/)
tools/dev/m12-wine-smoke.sh                                       # every check under wine, PASS/FAIL lines
tools/dev/package-windows.sh build-windows                        # <repo>/../run/m12-package: a folder to copy to Windows
```

(`bin/capped` is `<repo>/../bin/capped`; every run goes through it, CLAUDE.md.)

- Needs `g++-mingw-w64-i686-posix` (or any MinGW-w64 13+ with the posix thread model) and, to run it here, `wine`
  and `xvfb`. No Visual Studio, no MSYS2. The same CMake works from an MSYS2 `mingw32` shell on Windows (the toolchain
  file only adds what a cross build cannot infer).
- What is built: every package as for Linux, statically linked into `uc2.exe` (Windows subsystem) and `ucc.exe`
  (console), `-static`: no MinGW runtime DLL. OpenAL Soft is fetched and built as `OpenAL32.dll` and loaded at run
  time (absent: no sound). SDL2 is static. The imports of both exes are Windows system DLLs only (`objdump -p ... |
  grep 'DLL Name'`; `tools/dev/package-windows.sh` checks it).
- The platform layer is Core's GCC one (`UnGnuG.h`, `UnUnix.cpp` with its `_WIN32` branches, `FFileManagerUnix.h`,
  POSIX threads through winpthreads), not Epic's MSVC `UnVcWin32.*`.
- Under wine: `tools/dev/m12-wine.sh <dir> <prog.exe> [args]` (inside `capped`) runs one program in a private prefix
  (`$UC2_WINEPREFIX`, default `<repo>/../.cache/wine-m12`) and kills its wineserver afterwards.
- `windows64` (x86_64): see "64-bit" below.

## arm64 (aarch64 Linux, cross-compiled)

```
bin/capped 4G 900 -- cmake --preset arm64                         # aarch64-linux-gnu-g++, LP64
bin/capped 6G 7200 -- cmake --build build-arm64 -j3               # uc2.bin, ucc-bin
tools/dev/mkrun-uc2.sh build-arm64 arm64
(cd ../run/arm64/System && ../../../bin/capped 2G 900 -- qemu-aarch64 -L /usr/aarch64-linux-gnu ./ucc-bin verifyclasses)
```

The same LP64 code as `native64` (the layout contract below, `-fsigned-char`). The toolchain file shows pkg-config
the target's `.pc` files only, so no host library reaches the link. Without arm64 X11/GL and audio development
packages the fetched SDL2 is configured without X11 and without its audio backends (the client builds for the
offscreen/KMS paths only); a desktop arm64 client needs them through multiarch.

## Android

`Android/` is a Gradle project (Android Gradle plugin 9.4.0, Gradle 9.6.0 through the wrapper) whose
`externalNativeBuild` is this repository's root `CMakeLists.txt`, built with the NDK for `arm64-v8a` (phones such as
the Pixel 7 Pro) and `x86_64` (the emulator). The APK holds the engine (`libUC2.so`, `SDL_main` in
`Source/SDLLaunch`), `libSDL2.so`, `libopenal.so` (OpenSL ES), the process set-up bridge (`libuc2_android_bridge.so`)
and the Android inis; the game folder stays on the phone's storage.

Ubuntu's packages (Debian's `google-android-*-installer`s fetch Google's SDK parts into `/usr/lib/android-sdk`):

```
sudo apt install openjdk-17-jdk cmake ninja-build adb android-sdk-platform-tools-common \
  google-android-cmdline-tools-19.0-installer google-android-platform-36-installer \
  google-android-build-tools-36.0.0-installer google-android-ndk-r28c-installer
printf 'sdk.dir=/usr/lib/android-sdk\ncmake.dir=/usr\n' > Android/local.properties   # this machine's SDK and CMake
```

```
cd Android
./gradlew assembleDebug        # -> app/build/outputs/apk/debug/app-debug.apk (assembleRelease: the same, debug-signed)
adb install -r app/build/outputs/apk/debug/app-debug.apk
#   -Pabis=arm64-v8a      one ABI only (default arm64-v8a,x86_64)
#   -Psdl2Dir=<dir>       a local SDL2 checkout (or SDL2_SOURCE_DIR)
#   -PnativeDebug         the engine at the Gradle variant's own build type (-O0 for debug)
```

- The Gradle daemon runs on an installed JDK 17 (`gradle/gradle-daemon-jvm.properties`), whatever `java` is on the
  `PATH` or Android Studio's own JDK; Android Studio opens `Android/` as it is.
- The SDK parts are the packages' exactly: `compileSdk` 36 (platform 36), build-tools 36.0.0 (what AGP 9.4 asks for)
  and NDK 28.2.13676358. The SDK in `/usr/lib/android-sdk` is read-only, so a part AGP would download instead fails
  the build with its name. `targetSdk` stays 34 (Android 14's behaviour for the app); `minSdk` is 30 (Android 11:
  All files access, which reading the game folder in place needs).
- OpenAL Soft is built without C++20 modules on Android (`ALSOFT_ENABLE_MODULES=OFF`: the NDK's clang 19 rejects
  libc++'s range adaptors across a module boundary) and without RTKit.
- SDL2: one checkout for SDL's C library (the root CMakeLists gets it as `FETCHCONTENT_SOURCE_DIR_SDL2`) and SDL's
  Java classes (`SDLActivity`, compiled from its `android-project` sources), so the two always match; the build clones
  `release-2.32.x` once into `Android/build/SDL2`. GL4ES and OpenAL Soft are fetched by the CMake configure as for
  the other targets.
- The engine is built `RelWithDebInfo` in every Gradle variant, without the script patches
  (`-DUE_SCRIPT_PATCHES=OFF`): the game runs the game folder's packages. A native host build's
  `build-native/Scripts/System/*.u` copied into the game folder's `System/` gives it the patched ones.
- The Android inis (`Engine/Config/android/`, generated by `tools/dev/sync-android-ini.py` from the native ones)
  are installed into the game folder's `System/` before every start, each only where it differs.
- The game folder: `UC2` at the top of the shared storage (`/sdcard/UC2`, with `System/`, `Maps/`, `Textures/`,
  `Sounds/`, `Music/`, `Movies/`, ...: the same tree as a native run tree without its binaries), or at the top of an SD
  card. The launcher asks for All files access, runs the game from shared storage or from the app's internal copy
  (`/data/data/com.uc2.android/files/UC2`, much faster to read), copies the shared folder into internal storage (only
  what is new or changed: a stopped copy resumes), clears the internal copy, and copies its settings and saves
  (`System/Avaris.ini`, `System/User.ini`, `Save/`, `Saves/`) back to shared storage.
- The log goes to logcat (tag `UC2`; stdout and stderr as `UC2-stdio`) and to `System/Avaris.log`:
  `adb logcat -s UC2 UC2-stdio UC2Bridge SDL`. `adb shell setprop debug.uc2.nofault 1` leaves crashes to Android's
  crash reporter (a tombstone with the backtrace, `adb logcat -s DEBUG`).
- An engine command line from adb, to the launcher (the game's activity is not exported), whose countdown then starts
  the game:

  ```
  adb shell am start -n com.uc2.android/.MainActivity --es com.uc2.android.ARGS 'DM-Roman'
  ```

The client on Android: the window is the whole screen (immersive, landscape either way up), the frame is the screen's
size times `[SDL2Drv.SDL2Client] RenderScale` (0.5 in the Android ini) and is scaled up to it, the mouse is off
(`UseMouse=False`: no events, no capture, no cursor), the system's Back is Escape, and the on-screen pad
(`TouchPad=True`) is a virtual SDL controller fed from the app's overlay (`Source/SDL2Drv/Src/SDL2Android.cpp`,
`Android/app/src/main/java/com/uc2/android/PadOverlayView.java`): in the menus a D-pad, A B X Y, Start, Back and the
shoulders and triggers; in a match a move stick (left half) and a look stick (right half) where the finger lands, the
triggers, A B X Y, the shoulders, the stick clicks, a small D-pad, Start and Back. It is player one's pad unless a real
pad was connected first (it then stays off and its overlay hides); the "pad" key at the bottom hides it.

## 64-bit

```
bin/capped 4G 900 -- cmake --preset native64
bin/capped 6G 3600 -- cmake --build build-native64 -j6
file build-native64/Source/UCC/ucc-bin                  # ELF 64-bit ... x86-64
tools/dev/mkrun-uc2.sh build-native64 m13                # run tree <repo>/../run/m13
(cd ../run/m13/System && ../../../bin/capped 3G 300 -- ./ucc-bin verifyclasses)   # the layout check
tools/dev/check-64bit.sh                                 # every check, PASS/FAIL lines (needs run/m13-32 too)
```

The shipped `.u` files were compiled for 32-bit pointers; the 64-bit build keeps their 4-byte pointer slots in
bytecode (object indices). At every pointer width the script `int`s that stand in for C++ data get their C++ size when
classes link (the placeholder table in `Source/Core/Src/UnClass.cpp`), every script mirror is packed to 4, and the
load stops if a native class's C++ size differs from its script size. `ucc-bin verifyclasses` runs the whole check on
the running ABI (sizes, placeholders, and that no bytecode reads a placeholder); `tools/dev/layout-check.py` compares
every class, script struct and event parameter struct with the binary's DWARF. Integer-to-pointer casts are errors at
64 bits; `tools/dev/m13-cast-census.sh <build.log>` counts pointer-to-int narrowing (0). 32-bit stays the default and
the reference (wasm32 has its layout).

## CI

`.github/workflows/build.yml` builds `native` and `native64` (Linux), `windows` and `windows64` (MinGW-w64) and
`wasm-perf` on every push and pull request with `-DUE_SCRIPT_PATCHES=OFF` (no game folder in CI: the binaries run the
shipped script packages and the web artifact's `scripts/` is empty), uploads the binaries, and a push to `main`
replaces the rolling `latest` release.

## Scripts: `ucc-bin make`

The game runs on the shipped compiled scripts (the 17 `.u` of the game folder's `System/`, plus the 30 load-time
patches of `Source/Engine/Inc/ScriptPatches.h`). The native 32-bit Linux build also contains Epic's script compiler
(the `Editor` package linked into `ucc-bin`). This repository holds no script sources: the build compiles the patched
packages from your shipped `.u` and `patches/` (next section), and a script is changed by changing its set
(`patches/README.md`, "Changing a script").

```
bin/capped 6G 3600 -- cmake --build build-native -j4      # ucc-bin with Editor (32-bit pointers only) + Scripts/System
bin/capped 6G 600 -- cmake --build build-native --target check-headers   # are the generated headers current?
tools/dev/regen-headers.sh                                # regenerate them into Source/*/Inc
tools/dev/m14-scripts-check.sh                            # the game checks on the build's packages
```

- The generated C++ headers `Source/<Pkg>/Inc/<Pkg>Classes.h` (Editor, Engine, GUI, IpDrv, UCElements, UCGameplay,
  UnrealEd, UnrealGame) are pure exporter output, 64-bit-correct as generated: a script `int` that holds a C++ pointer
  is written `PTRINT` from Core's placeholder table (`GNativePlaceholders`, UnClass.cpp). Never edit them by hand
  (except a class's `cpptext` and `struct native` blocks, below): change the script (its patch set), then
  `tools/dev/regen-headers.sh` (export in a scratch tree, write into `Source/<Pkg>/Inc`, CRs dropped) and commit them
  with the change. `tools/dev/check-headers.sh` (CMake target `check-headers` of the native preset, not in ALL)
  exports and compares: exit 0 current, 1 stale (unified diff printed), 2 could not run. Both read the build's patched
  tree `build-*/Scripts/tree` by default (`--classes-root <dir>` for another `<dir>/<Pkg>/Classes`; CMake:
  `-DUE_HEADERS_CLASSES_ROOT=<dir>`) and take `--build <dir>`. A patched tree has only the packages the patches
  rebuild: the check compiles those against the shipped ones (`uc-build.sh --headers`, into `build-*/Scripts/headers`)
  and compares their generated headers, except what the shipped packages do not carry (each class's cpptext, and
  `struct native` blocks: `UStruct::StructFlags` is not saved), which is taken from the committed header
  (`tools/dev/check-headers-cpptext.py`; `regen-headers.sh` keeps them as committed). With `-DUE_SCRIPT_PATCHES=OFF`
  there is no patched tree: the target says so and fails unless a root is given.
- A run tree takes the build's packages (`tools/dev/mkrun-uc2.sh build-native <tree>`). The script patcher leaves
  rebuilt packages alone (it patches only the shipped files it was made for) and logs `ScriptPatcher: <Pkg> ... patch
  not applied`; the packages that were not rebuilt are the shipped ones and are patched.
- `tools/dev/m14-scripts-check.sh`: the game checks (verifyclasses, the patcher, menu, a match, controls, audio,
  effects, the map census, movies, network) on the build's packages (`build-native/Scripts/System`;
  `M14_SCRIPTS=<dir>` for another folder), PASS/FAIL lines.
- `make` exits 1 when there was any warning (Epic's `ucc`); the verdict is the log's last line,
  `Success - 0 error(s), N warning(s)`. Memory: well under 3G.
- `tools/dev/script-build.sh` rebuilds all 17 packages from Epic's script text (`$UC2_EPIC_SOURCES`, default
  `<repo>/../epic-sources/uc2-avaris-2226`, read-only; or `--classes-root <dir>`) the way Epic's build machine did
  (`Source/BuildProcess/sync_avaris.pl`: `ucc make -nobind -noh` with the Xbox inis, then `ucc editor.stripsource`
  with the PC inis, conforming each package to the shipped one) and compares the result with the shipped packages
  object by object (`tools/dev/script-diff.py`). Output `<repo>/../run/m14-make/System/*.u` (`--tree`), logs in
  `<repo>/../logs/m14/`; `--build build-windows` does the same with `ucc.exe` under wine; `--headers` exports the
  headers into the tree's own `<Pkg>/Inc` and diffs them with `Source/*/Inc` (which carry our patches: expect those
  differences). It is never a build input.

## Script patches: the patched script packages, built by default

The game's logic is UnrealScript compiled into the 17 `.u` of the game's `System/`. This repo holds no script, only
patch sets (`patches/`; what each does: `patches/README.md`). By default the build makes the patched packages from
your game files, as part of `cmake --build` (target `scripts`, in ALL):

1. checks that the game folder's 17 `.u` are the shipped ones (`patches/shipped.sha256`; a package rebuilt from
   source is refused, with the file named: it cannot be the base);
2. decompiles each package the build needs (`tools/scripts/uc-decompile.py`; UCMenu: its shipped text) into
   `build-*/Scripts/base/<Pkg>/Classes`;
3. applies the chosen sets (`tools/scripts/uc-patches.py apply`, exact context) into `build-*/Scripts/tree`;
4. compiles them with this build's `ucc-bin make` against the shipped packages and strips them as Epic did
   (`tools/scripts/uc-build.sh`, work tree and logs in `build-*/Scripts/work`) into `build-*/Scripts/System/<Pkg>.u`:
   only the packages the sets change (default: UCGameplay, GUI, UCMenu).

About 25 s the first time; each stage has a stamp, so an unchanged input rebuilds nothing, a changed patch file
rebuilds from the apply on, and a new `ucc-bin` or shipped file from where it enters. Needs Python 3 and bash (on
Windows: MSYS2).

Which preset compiles. The script compiler is `ucc` with the Editor package, 32-bit pointers only, and it runs on the
build machine: the host preset is `native` on Linux and `windows` (MinGW i686, `ucc.exe`) on a Windows host. Every
other preset (`wasm*`, `windows`/`windows64` cross-built from Linux, `native64*`, `arm64`) copies the host build's
`Scripts/System` into its own `build-*/Scripts/System` (checked: Unreal packages; the host build's sets are printed at
configure), so build the host preset first. The web artifact carries them (`build-wasm*/Source/SDLLaunch/scripts/` and
`meta.json` `scripts`, above). Run trees take them: `tools/dev/mkrun-uc2.sh <build-dir> <tree>` installs
`<build-dir>/Scripts/System/*.u` over the game folder's and says which (`UC2_SCRIPTS=<dir>` takes them from another
folder, `UC2_SCRIPTS=none` keeps the game folder's).

| Option | Default | Meaning |
|---|---|---|
| `UE_SCRIPT_PATCHES` | ON | OFF: no `Scripts/` at all; the game runs the shipped `.u` (with Epic's load-time patches). Nothing of the game folder is read. |
| `UE_SCRIPT_PATCH_SETS` | `default` | the sets to apply, `;` or `,` separated (`patches/sets.txt` names); `default` = those marked `on` there, in its order. Dependencies are enforced (`keys-menu` needs `mouse-look`; everything needs `epic-fixes`) |
| `UE_ASSETS_DIR` | empty | the game folder: `System/` (the shipped `.u`, Epic's `DefaultXbox.ini`, `DefUserXbox.ini`, `Default.ini`, `DefUser.ini`, `Localization/`) and the content folders the scripts load (`Textures/`, `StaticMeshes/`, ...); `tools/dev/mkassets-uc2.sh` makes one from Epic's tree. Empty: the `UC2_ASSETS_DIR` environment variable (the dev tools read it too), else `<repo>/../assets/uc2-assets` when it exists, else the configure stops and says what to pass |
| `UE_SCRIPT_PACKAGES_DIR` | `UE_ASSETS_DIR/System` | where the 17 shipped `.u` are, if not in the game folder (e.g. the game folder has rebuilt ones: point this at the disc's `System`) |
| `UE_SCRIPT_RETAIL_INIS_DIR` | empty | optional: a `System/` whose `Characters.ini`, `Gametypes.ini` and `Maplists.ini` the strip loads instead of the game folder's. Empty: the game folder's, the inis the game runs with, so the patched packages' config defaults and the inis agree. The pipeline proof's P1 passes the retail disc's, which the shipped packages were stripped with (`tools/scripts/uc-patches-proof.sh --retail-inis`) |
| `UE_SCRIPTS_DIR` | `build-native/Scripts/System` | non-host presets: the host build's packages (`build-windows/...` on a Windows host). Missing with patches ON: the configure stops ("run the native preset first, or pass UE_SCRIPTS_DIR, or -DUE_SCRIPT_PATCHES=OFF") |

- Every error stops the configure with what to do: no game folder, a rebuilt package in it (named, with its sha256
  and the shipped one), an unknown set or a missing dependency, no Python/bash, no host packages.
- `cmake --build build-native --target scripts-proof`: the bytecode proofs of the pipeline
  (`tools/scripts/uc-patches-proof.sh`: P1 base + `epic-fixes` = shipped + load-time patcher, P2 the packages of
  `patches/reference.sha256` (sha256 and size of what the default build makes; regenerate with
  `uc-patches-proof.sh p2 --write-reference` when a set changes on purpose), P3 determinism; minutes; P1's Engine part
  needs Epic's texture sources, else it leaves Engine out). Not in ALL. `sha256sum -c ../../../patches/reference.sha256`
  in `build-native/Scripts/System` checks a default build by hand.
- Changing a script = changing its patch set: `patches/README.md`, "Changing a script" (`tools/dev/uc-patches-regen.sh
  edit|make <set>`).
- Windows host (MSYS2 `mingw32` shell): the same commands with `cmake --preset windows -DUE_ASSETS_DIR=C:/path/to/UC2`;
  `uc-build.sh` runs `ucc.exe` under MSYS2's bash and links the content folders into its work tree with `ln -s` (set
  `MSYS=winsymlinks:nativestrict` with Developer Mode, or it copies them). Not run on Windows yet.

## Options

| Option | Default | Meaning |
|---|---|---|
| `UE_ALLOW_64BIT` | OFF | build x86_64 instead of demoting to `-m32` |
| `UE_UNSIGNED_CHAR_AUDIT` | OFF | `-funsigned-char` instead of `-fsigned-char` (audit build) |
| `UE_SANITIZE` | empty | `-fsanitize=` list, e.g. `address,undefined` |
| `UE_WEB_PERF`, `UE_WEB_DEBUG` | OFF | the three web build modes (see the presets) |
| `UE_SCRIPT_PATCHES`, `UE_SCRIPT_PATCH_SETS`, `UE_ASSETS_DIR`, `UE_SCRIPT_PACKAGES_DIR`, `UE_SCRIPT_RETAIL_INIS_DIR`, `UE_SCRIPTS_DIR` | see "Script patches" | the patched script packages |
| `UE_HEADERS_CLASSES_ROOT` | empty | `check-headers`' script sources (default: the build's `Scripts/tree`) |

## Things that are fixed on purpose

- Static link of every package (`__STATIC_LINK=1`), as the Xbox build did.
- ANSI `TCHAR` on every target (`NO_UNICODE_OS_SUPPORT=1`); no `-fshort-wchar`.
- `_XBOX`, `XBOX_ASSET_FORMAT`, `LIN_LOAD`, `LIN_SAVE`, `USE_LINEAR_LOADING` are never defined.

## Fetched libraries

SDL2 (`libsdl-org/SDL@release-2.32.x`; native and Windows), OpenAL Soft (`10-p/openal-soft-web@ufront/web`; native as
a shared library, Windows as `OpenAL32.dll`, web static) and GL4ES (`10-p/gl4es@fix/cubemap-bind-active-unit`; Linux
and web) are fetched by CMake at configure time into `build-*/_deps/`, on their branches (not pinned: the latest wins;
the root `CMakeLists.txt` says why each option is set). The first configure of each preset therefore needs network
access; later configures re-fetch the branch tips. Vendored in `Thirdparty/`: zlib, Ogg/Vorbis, Theora (decoder).

## Frame rate

`[Engine.GameEngine] FpsLogSeconds=<s>` (default 0 = off; console `FPSLOG <s>`) logs one `FPS:` line every `<s>`
seconds: frames, fps, frame time avg/min/max/p99, the 1% low. `tools/dev/fps-bench.sh <tree> <map> <seconds> [WxH]`
runs a fixed camera turn on this machine's GPU, headless, and prints one line. A standalone game is uncapped by default
(`UseVSync=False`, `[Engine.Engine] FrameRateCap=0`); a network client keeps the engine's own tick limit.
