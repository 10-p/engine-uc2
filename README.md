# engine-uc2: Unreal Championship 2 portable engine

## What?

Unreal Championship 2: The Liandri Conflict (Unreal Engine 2 build 2226, package version 151), ported from
Epic's Xbox/Win32 source tree to Linux, Windows (MinGW-w64) and the Web (Emscripten). Private engine
repository: source only. Game content lives out of tree in `<WorkUC2>/assets/uc2`
(`tools/dev/mkassets-uc2.sh` builds it from the Epic tree; `tools/dev/mkrun-uc2.sh` assembles a run tree). The game's
UnrealScript is content too: the repo holds none, only our changes to it as patch sets ([`patches/`](./patches/README.md)),
which the build applies to scripts decompiled from your own game files.

- Agent guide: [`CLAUDE.md`](./CLAUDE.md)
- Building: [`BUILD.md`](./BUILD.md)
- The plan and where we are: [`docs/porting/PLAN.md`](./docs/porting/PLAN.md), [`docs/porting/LOG.md`](./docs/porting/LOG.md),
  open problems in [`docs/porting/BLOCKERS.md`](./docs/porting/BLOCKERS.md)
- Every change to Epic's engine, where and why: [`docs/porting/REPORT.md`](./docs/porting/REPORT.md)
- How the Epic tree was imported: [`docs/SOURCE-LAYOUT.md`](./docs/SOURCE-LAYOUT.md)

## Changes from the original source

* Core's platform layer on GCC/Clang (engine-ut2004's `UnUnix.cpp`/`UnGnuG.h` with UC2's platform contract),
  POSIX threads, every package a static library (`__STATIC_LINK`, as the Xbox build).
* An SDL2 client and launcher (`SDL2Drv`, `SDLLaunch`: `uc2.bin`, `uc2.exe`, `SDLLaunch.js`), game pads through
  SDL's GameController API, keyboard and mouse for the PC.
* An OpenGL render device (`UC2GLDrv`) with the Xbox pixel shaders translated to GLSL and the Xbox
  post-processing (distortion, glow, bloom, depth of field); on the web the same driver through GL4ES on WebGL 2
  (WebGL 1 as the fallback).
* Audio from the cooked XACT data through OpenAL Soft (`UC2ALAudio`); the XMV movies transcoded to Ogg Theora
  (`UC2Movie`).
* GCC/MinGW and Emscripten builds; 32-bit by default (the shipped scripts' layout), x86_64 Linux and Windows too.
* A WebAssembly build with a WebRTC net driver (`WebRTCDrv`) for the browser.

## Status

Plays natively on Linux (x86 32-bit and x86_64): every map loads, the menus, matches with bots, effects,
sound, movies, controls and network play (M0-M14, D-1..D-9, C-1/C-2, O-1..O-11, F-1..F-6 in `LOG.md`); Epic's
shipped script packages play as shipped, the rebuilt ones add the PC mouse (F-1). Windows:
built with MinGW and run under wine only (never on real Windows). Web: boots to the menu and plays in the
frontend's browser specs (boot, audio, saves, keys, net, jitter); the owner's preview is the frontend's `uc2`
target. arm64: the cross build configures and builds (F-4c, `docs/porting/analysis/platform-f4.md`); it has
not been run on hardware. What is open is in `BLOCKERS.md` and the owner's review tasks in `PLAN.md`.

## Building

Short version: `cmake --preset native && cmake --build build-native -j12`. Every preset, the web build and
the artifact: [`BUILD.md`](./BUILD.md).
