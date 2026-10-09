# F-2 and F-4: FPS in the log, platform like UT2004 (exceptions, frame yield, arm64, files, docs, switches)

Running notebook, appended after every step so a successor can continue. Worktree
`/home/ion/WorkUC2/engine-uc2-f4-platform`, branch `f4-platform` (base main `1372dec`). Run trees `run/f4*`,
logs `/home/ion/WorkUC2/logs/f4`. Brief: PLAN.md "Owner review of 2026-10-09" tasks F-2 and F-4.

Order: 1 F-2 FPS log line + `tools/dev/fps-bench.sh`; 2 F-4a exceptions off, fail-soft loaders; 3 F-4b frame
yield like UT2004; 4 F-4c arm64 layout hazards; 5 F-4d files and docs; 6 F-4e command-line audit.

Memory ceilings: game/ucc 2G, builds 6G with -j4, Emscripten link 6G, frontend build 4G (coordinator's rules).

## Step 0 - reading (2026-10-09)

Read: PLAN.md owner review, REPORT.md section 10 (features 2, 8, 9, 12, 13, tools inventory), notebooks
web-smoke.md, runtime-port.md. engine-ut2004 template findings are recorded under each step below.

## Step 1 - F-2: the frame rate in the log (commit 27569b4)

- `Source/Engine/Src/UC2FpsLog.cpp` (new): `[Engine.GameEngine] FpsLogSeconds=<s>` (0 = off, the default and the
  compiled default; `Engine/Config/Default.ini` and the generated `web/Default.ini` carry `FpsLogSeconds=0`, README row)
  and the console command `FPSLOG <s>` (0 stops; no argument says the setting). One line per interval, logged under the
  name `FPS` (Avaris.log: `FPS: ...`; the `-log` echo on stdout prints no names, as for every line):
  `8.01 s 498 frames 62.2 fps; frame ms avg 16.08 min 8.95 max 58.16 p99 30.40; 1% low 26.5 fps; 1280x720`.
  A frame = wall time between two `UGameEngine::Tick` calls (the whole main-loop iteration). 1% low = 1 / mean of the
  slowest 1% (at least one frame). Hooks: `UnGame.cpp` (declaration, `UGameEngine::Exec`, top of `UGameEngine::Tick`).
  Why GameEngine and not the stats class: `FStats` only exists in non-FINAL builds' draw path; Tick runs for every
  client and server, and the key is read with `GConfig` (no script property: the class layout is untouched).
- `Source/Engine/Src/UnStats.cpp` FStats::Render: STAT FPS divides only by a non-zero frame time, rounds with appRound
  (engine-ut2004 f7653f1; 1/0 cast to INT is undefined).
- `tools/dev/fps-bench.sh <tree> <map> <seconds> [WxH] [args]`: headless on the GPU (SDL offscreen/EGL, as fx-owner.sh
  UC2_GPU=1), no bots, God, NETPLACE at a fixed spot (DM-Praxis: the ledge at the first PlayerStart facing the round
  room, `-1220 288 690 5220 -600`; DM-Deadbolt: jit-trace's spot), `FPSLOG <seconds>`, then NETPLACE with the yaw +4096
  every 0.25 s (a full turn every 4 s, frame-rate independent); window size through `-ini:` (WindowedViewportX/Y,
  FullscreenViewportX/Y); one PASS/FAIL line with the FPS line, the GL renderer and the machine load. `FPS_SOFT=1`
  for llvmpipe. capped 2G.
- First run (functional only, machine load 16-20 from the other agents' builds: NOT a measurement):
  `PASS fps-bench f4-DM-Praxis-1280x720: 8.01 s 441 frames 55.1 fps; ... renderer 'Quadro P620/PCIe/SSE2'`.
  The numbers for the F-2 questions are taken at low load (below).

## Step 2 - F-4a: exceptions off, fail-soft loaders (in progress)

engine-ut2004 (read-only) facts:
- `CMakeLists.txt:1210-1240` and `Source/SDLLaunch/CMakeLists.txt:180-240`: the engine's objects are compiled with
  Emscripten's default (catching off, `-fignore-exceptions`, no `invoke_`); openal-soft alone gets `-fexceptions`;
  SDLLaunch links with `-sNO_DISABLE_EXCEPTION_CATCHING` in every mode (ufront 2.61, because openal-soft throws and
  catches internally). Native: GCC default (exceptions on, catches work).
- The fail-soft loader patches (all `#ifdef __EMSCRIPTEN__` or `__EMSCRIPTEN__ || __ANDROID__`): `UnObj.cpp`
  SafeLoadError (no rethrow on LOAD_Throw), GetPackageLinker (each of the 7 throws becomes a warning + `return NULL`;
  `FreshLinker` so only a linker made by this call is dropped, bfdd422; a constructed linker with `Success != 1` is
  dropped), StaticLoadObject (no ObjectNotFound throw), LoadPackage (EndLoad balance on a NULL linker); `UnLinker.cpp`
  ULinkerLoad ctor (open failed, already loaded, bad tag, too old: warning + return) and VerifyImport (private /
  missing import: PKG_BrokenLinks + warning + return); `Engine/Src/UnGame.cpp` the start-up map (fall back to the
  entry level), LoadMap (a NULL level and a NULL pending-package linker fail the load/join with the native reason,
  5f02771). Not fail-soft in UT2004 either: ResolveName's ConfigNotFound, StaticLoadClass's LoadClassMismatch,
  VerifyImport's oldver rethrow, CreateExport's FailedCreate, appErrorf in general (all abort on the web).
- `78c461d` (appUnwindf latched GIsCriticalError) is unreachable here (guard/unguard are braces on __UNIX__, web-smoke.md).

What throws in UC2 today (measured natively, gdb `break __cxa_throw` + bt, run tree f4, logs/f4/throw-*.txt):
- `ucc-bin server DM-Deadbolt`: 6 throws, all ucc's commandlet-name lookup (`StaticLoadClass("server")`, no package ->
  GetPackageLinker PackageResolveFailed, rethrown by SafeLoadError, caught by StaticLoadObject/StaticLoadClass). None
  during the map load.
- `uc2.bin DM-Deadbolt -nullrenderer`: 4 throws, all a script `DynamicLoadObject("--")` (same chain).
- `Missing Class Class Editor.TransBuffer (../System/Editor.u)` is logged, but by VerifyImport's SafeReplace branch
  (the class package exists, the object is not in Editor.u and not native-transient): **no throw**. web-smoke.md W0
  and REPORT.md section 10 ("the throw is caught") were wrong on this point. So nothing needs a placeholder class:
  UT2004's approach (fail-soft) covers it, and this import never needed either.

Done in UC2 (uncommitted at this entry): the patches above in `Core/Src/UnObj.cpp`, `Core/Src/UnLinker.cpp`,
`Engine/Src/UnGame.cpp`, with UC2's own shape where its code differs (StaticLoadObject keeps the package-remap retry and
the SafeLoadError warning on the web path, so the log says what failed). One addition UT2004 does not have:
`ServerListen` is a warning on the web (the level runs without network): ucc-bin.js under Node cannot listen, and with
catching off the old appErrorf would abort every map of the Node census after it loaded.
CMake: `cmake/UC2Web.cmake` no longer adds `-sNO_DISABLE_EXCEPTION_CATCHING` to every compile and no longer defines
`UC2_WEB_EXCEPTION_FLAGS`; SDLLaunch (`Web.cmake`) and ucc-bin.js (`UCC/CMakeLists.txt`) link with
`-sNO_DISABLE_EXCEPTION_CATCHING` for openal-soft only; openal-soft compiles with `-fexceptions` (UT2004's spelling).
`tools/dev/map-census.sh`: `CENSUS_NODE=1` runs `node ucc-bin.js` and fails a map on an abort.
Baseline for sizes: main's `build-wasm-perf` (built at c59c02d = the same Source as 1372dec): SDLLaunch.wasm
14,929,569 bytes, SDLLaunch.js 349,270 bytes.

## Step 4 - F-4c: arm64 layout hazards, the first arm64 build (2026-10-09)

Toolchain: `aarch64-linux-gnu-g++` GCC 15.2.0-16ubuntu1cross1, `qemu-aarch64` 10.2.1, `qemu-user-binfmt` (dpkg). No arm64
X11/GL `-dev` packages (multiarch not enabled), so SDL2 configures with X11 off (`SDL_X11 (Wanted: ON): OFF`).
- `capped 4G 900 -- cmake --preset arm64` -> rc 0, `Target arch: arm64; pointer width: 8 bytes` (logs/f4/cfg-arm64.log).
  (SDL2's configure also put the host's `-isystem/usr/include/pipewire-0.3` etc. into its EXTRA_CFLAGS; the UCC target
  does not build SDL2, the client would: to be looked at with the arm64 packages.)
- `capped 6G 7200 -- cmake --build build-arm64 -j4 --target UCC` -> exit 0 (logs/f4/build-arm64-ucc.log):
  `ucc-bin: ELF 64-bit LSB pie executable, ARM aarch64`, 55,280,712 bytes. `tools/dev/m13-cast-census.sh` on that log:
  `ptr-to-int sites: 0  int-to-ptr sites: 0`.
- `mkrun-uc2.sh build-arm64 f4-arm64`; `cd run/f4-arm64/System && capped 2G 900 -- qemu-aarch64 -L /usr/aarch64-linux-gnu
  ./ucc-bin verifyclasses` -> rc 0, `VERIFY: 64-bit native size check: 401 classes, 0 mismatches while linking`, every
  placeholder found at its C++ size, `bytecode scan: 6588 scripts read for placeholder references`, `VERIFY: all native
  class layouts match`. (x86_64 said 400 classes / 6,578 scripts at M13; the extra class and 10 scripts are what main
  gained since, e.g. the rebuilt UCGameplay.u in the asset tree.)
- Extra: `qemu-aarch64 ./ucc-bin server DM-Deadbolt` -> `Game class is 'UCDeathmatch'`, `Bringing Level DM-Deadbolt.MyLevel
  up for play`; SIGINT at 150 s -> `appRequestExit(0)`, the shutdown under emulation did not finish before capped's
  200 s limit (not judged). The client (`uc2.bin`) was not built (needs the packages above).

Checklist: engine-ut2004's arm64/armv7 (Android) fixes and the brief's hazard list, each against UC2.
engine-ut2004 sources read: `git log` (bc05013, f7653f1, 5af159b, 4a68972, f71d613, b4cd9eb), the `__ANDROID__`/`__arm__`/
`__aarch64__`/`__LP64__` sites in Core, Engine, IpDrv, BUILD.md "UT_ALLOW_64BIT", "Windows x64".

| # | Hazard (where UT2004 met it) | UC2 | Result |
|---|---|---|---|
| 1 | 64-bit detection must include aarch64 (`Core.h:111`) | `Core.h:101` `PLATFORM_64BITS` on x86_64, `_WIN64`, `__aarch64__`, any `__LP64__` | already done (M13); the arm64 build says pointer width 8 |
| 2 | No x86 intrinsics/asm off x86 (`UnGnuG.h:105`, `UT_NO_X86_INTRIN`) | `UnGnuG.h:110`; rdtsc/cpuid, `FloatToInt`, x87 transforms and SSE blocks gated `__i386__`/`__x86_64__`/`__HAS_SSE__` | compiled for aarch64 with no asm error: verified |
| 3 | `appCycles()` returned 0 on arm/wasm (f7653f1): STAT FPS showed 2^32 | microsecond counter from `appSecondsSlow` on non-x86 (`UnGnuG.h:381-405`), `GSecondsPerCycle` 1e-6 (`UnUnix.cpp:3015`) | already done; plus F-2's division guard in UnStats.cpp |
| 4 | armv7 unaligned bytecode reads (`UnScript.h:74`, `UnCorSc.cpp:2752` copy reads under `__ANDROID__ && __arm__`) | AArch64 allows unaligned normal loads (LDR/LDP fault only with SCTLR.A or on exclusive/atomic access); UC2 reads `*(INT*)Code` as x86_64 does | not applicable (armv7 is not a target); verifyclasses' 6,588-script bytecode walk ran on aarch64 under qemu (qemu does not enforce alignment either, so this is by reading) |
| 5 | armv7 unaligned WAV header reads (`UnAudio.cpp:1015`) | same reasoning; UC2's audio is the cooked Ogg packs, `UnAudio.cpp` RIFF reads only at import | not applicable |
| 6 | Native class larger than the 32-bit `.u` size tolerated on Android (`UnGame.cpp:2470`, warning instead of appErrorf) | UC2 is stricter: the placeholder table widens script layout to the C++ size and a mismatch is fatal | not adopted on purpose (UC2's guard proves 0 mismatches on aarch64) |
| 7 | 32-byte padding of every allocation at 64-bit (`FMallocAnsi.h` MALLOC_PADDING, Ryan Gordon's "four bytes extra at the end on the Hammer") | not adopted (REPORT.md section 10): the size guard replaces it | unchanged; same as x86_64 |
| 8 | UTF-16 text files read with `Size/sizeof(TCHAR)` on an ANSI build: 2x over-read (`UnMisc.cpp:1694`, fixed for Android only) | the same Epic code (`Core/Src/UnMisc.cpp:1472-1482`) on every UC2 target (TCHAR is ANSI everywhere) | not triggered: no UTF-16 file in `assets/uc2/System` or its Localization (checked: no `FF FE` BOM). Left as Epic wrote it; noted for the owner |
| 9 | `pthread_join` on a detached resolve thread aborts on bionic (`UnIpDrv.h:162`) | glibc on arm64 Linux: EINVAL, harmless (the same as x86_64) | not applicable to Linux arm64 |
| 10 | socket reset to 0 = stdin after a failed bind (`UdpLink.cpp:91`, `TcpLink.cpp:93`, Android only) | the same Epic bug in UC2; it ended `ucc-bin.js` under Node (JS TypeError) once F-4a let the server run without listening | **fixed on every target** (INVALID_SOCKET), F-4a commit |
| 11 | Thread stack sizes at 64-bit (bc05013: Android's 1 MB SDLThread, Karma's stack arrays doubled) | no Karma; main thread 8 MB (Linux default) as x86_64, which runs every map; web STACK_SIZE 2 MB (32-bit) | unchanged |
| 12 | `-m32` put back by side doors (f71d613) | `UC2_PTR_BYTES`; `file` says ELF 64-bit ARM aarch64 | verified |
| 13 | `#pragma pack` / `GCC_PACK` on mirrors | `GCC_PACK(n)` = `aligned(n)` (UT2004's), mirrors `#pragma pack(4)` (M13 list) | verified by verifyclasses on aarch64 |
| 14 | 64-bit pointers in script ints | 50-entry placeholder table, `SERIAL_POINTER_INDEX` for every 64-bit target (`UnObjBas.h:15`, aarch64 included) | verified (placeholder lines + bytecode scan) |
| 15 | FName/FString/TArray layouts | FName 4 bytes, FString/TArray = pointer + two INTs (16 bytes, 8-aligned at LP64), the same on x86_64 and aarch64 (both LP64 SysV-like ABIs) | verified indirectly: the 401 class sizes and offsets match on aarch64 |
| 16 | `appAlloca` | `((size+7)&~7)` through `alloca` (`UnGnuG.h:329`), identical to UT2004; aarch64 alloca is 16-aligned | nothing to do |
| 17 | Bitfields | `BITFIELD` stays `unsigned int` (4 bytes), never `long` (64bit.md) | verified by the layout check |
| 18 | `FPlane`/SIMD alignment | no `GCC_ALIGN(16)` on FPlane/FVector in UC2 (none in `Core/Inc`), no SSE path off MSVC, no NEON code | nothing to do |
| 19 | `char` signedness (arm's is unsigned) | `-fsigned-char` on every target incl. arm64; the x86_64 `-funsigned-char` audit build found one Epic difference (`ToAnsi`) | unchanged (64bit.md step 11) |
| 20 | Weak memory ordering (ARM, not x86 TSO): plain flags shared between threads | `FFileStream` streaming thread: data under `FScopeLock`, the `Destroyed` flag polled around `usleep`; `UnThreadingPosix` uses `__atomic` SEQ_CST for `TimeToDie`; no `volatile`-only handshakes found in Core/Engine/UC2Movie/UC2ALAudio (grep) | read only; no change |
| 21 | `va_list` passed MSVC-style | `GET_VARARGS` through `va_list` (`UnGnuG.h:202-224`), same as x86_64 where va_list is also not a pointer | nothing to do |

Not done: the arm64 client (packages missing), any arm64 run beyond qemu-user, a census at arm64 (58 maps under qemu would take
hours; the one map above loads).
