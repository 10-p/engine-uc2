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
| `native64` | `build-native64/` | Linux x86_64 |
| `wasm` | `build-wasm/` | Web, `-O2 -g` dev build |
| `wasm-debug` | `build-wasm-debug/` | Web, `-O0` |
| `wasm-perf` | `build-wasm-perf/` | Web, `-O3`, what ships |
| `windows` | `build-windows/` | Windows x86, MinGW-w64 cross |
| `windows64` | `build-windows64/` | Windows x86_64, MinGW-w64 cross |

```
cmake --preset native
cmake --build build-native -j12
```

## Options

| Option | Default | Meaning |
|---|---|---|
| `UC2_ALLOW_64BIT` | OFF | build x86_64 instead of demoting to `-m32` |
| `UC2_WEB_PERF`, `UC2_WEB_DEBUG` | OFF | the three web build modes (see the presets) |
| `UC2_ASSETS_DIR` | `../assets/uc2` | the asset tree; read only for the files the web build preloads |

## Things that are fixed on purpose

- Static link of every package (`__STATIC_LINK=1`), as the Xbox build did.
- ANSI `TCHAR` on every target (`NO_UNICODE_OS_SUPPORT=1`); no `-fshort-wchar`.
- `_XBOX`, `XBOX_ASSET_FORMAT`, `LIN_LOAD`, `LIN_SAVE`, `USE_LINEAR_LOADING` are never defined.

## Fetched libraries

SDL2 (`release-2.32.x`) is fetched by CMake at configure time into `build-*/_deps/` (native builds only; the
root `CMakeLists.txt` says why each option is set). The first `cmake --preset native` therefore needs network
access; later configures reuse the checkout.
