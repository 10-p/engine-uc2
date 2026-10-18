Record — kept as history; the current state is in README.md, BUILD.md and the change inventory.

# F-2 and F-4: FPS in the log, platform like UT2004 (exceptions, frame yield, arm64, files, docs, switches)

> 2026-10-11 (T4): the script sources this notebook refers to (`Source/<Pkg>/Classes`, `.ucproject`) left the
> repository; Epic's text is `<WorkUC2>/epic-sources/<Pkg>/Classes` (dev machine only), the build's base is the
> decompiled `build-*/Scripts/base`, our changes are `patches/` (`scripts-removed.md`).

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

### F-2 answers (measured 2026-10-09 03:00-03:10, machine load 19-30 from other agents' builds, the Quadro P620 also
### drives the owner's desktop and a Sunshine stream: numbers are this machine at this time, not a clean benchmark)

Is the frame rate uncapped natively? **Yes.** `UGameEngine::GetMaxTickRate()` (`UnGame.cpp:3502`) is 0 for a standalone
game and a listen server (dedicated server: Net/LanServerMaxTickRate 60, clamped 10-120; a network client with
`bCapFramerate`: min(90, netspeed/MoveRepSize)); `UseVSync=False` in every render-device section of Default.ini (and the
compiled default 0, `UC2GLRenderDevice.cpp:206`), applied with `SDL_GL_SetSwapInterval(0)`; nothing else sleeps. The null
renderer runs DM-Praxis at **1,336 fps** (`f4-praxis-720-null`: 26,726 frames in 20 s, frame avg 0.75 ms): the game
tick itself is far from any limit. Since F-4e a cap exists only if asked for: `[Engine.Engine] FrameRateCap` (default 0).
`__GL_SYNC_TO_VBLANK=0` changes nothing (61.2 vs 63.0 fps): no hidden vsync on the offscreen EGL path.

What caps the web: see F-4b's section (the 0 ms timer clamped to >= 4 ms per frame, plus the Asyncify unwind and the
commit blit; no cap of the engine's own). Browser frame times measured there: 42-59 ms p50 at load 36 (not a benchmark).

Can it reach 120 fps on this machine on DM-Praxis? `tools/dev/fps-bench.sh f4 DM-Praxis 20 <WxH>` (GPU, headless):

| Run (20 s each) | fps | frame ms avg / min / max / p99 | 1% low |
|---|---|---|---|
| turn on the ledge, 1280x720 | 63.0 | 15.88 / 8.34 / 92.18 / 64.39 | 13.7 |
| turn on the ledge, 1920x1080 | 69.6 | 14.36 / 8.33 / 90.76 / 28.40 | 21.2 |
| turn on the ledge, 640x480 | 68.6 | 14.58 / 7.81 / 90.59 / 62.67 | 12.8 |
| turn, 1280x720, `UsePostProcess=False` | 95.2 | 10.51 / 5.83 / 73.25 / 19.47 | 40.0 |
| fixed: the ledge facing the round room (`FPS_TURN=0`), 1280x720 | 50.7 | 19.74 / 15.86 / 46.16 / 27.44 | 31.7 |
| the spawn view, no placement, 1280x720 | 132.5 | 7.55 / 5.16 / 34.23 / 15.87 | 44.3 |
| null renderer, 1280x720 | 1336.3 | 0.75 / 0.49 / 5.04 / 1.47 | 548.5 |

So: **not on the benchmark path, at 720p or 1080p** (63-70 fps average, 50 on the heaviest view); yes on light views (132
at the spawn). The rate does not depend on the resolution (640x480 68.6, 1280x720 63.0, 1920x1080 69.6): the frame is
not fill-bound on this GPU; it is per-draw/per-pass work (the post-processing alone is about 5 ms a frame: 95 fps
without it), which is where F-3/F-5 (WebGL 2, the renderer items) and a profile of UC2GLDrv would look. The isolated
90 ms frames on the turning path come with the teleports (NETPLACE every 0.25 s; the fixed view's worst is 46 ms).

Native before/after F-4a (the F-2 binary `logs/f4/bin-f2/uc2.bin` vs the F-4 binary, alternated, the same path, 720p):
69.9 / 68.0 / 69.6 / 70.7 fps: no difference, as expected (F-4a changes nothing natively but the socket sentinel).

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

## Step 3 - F-4b: frame yield and pacing, line by line against engine-ut2004 and engine-ut99 (commit d9226d1)

Read: engine-ut2004 `Source/SDLLaunch/Src/SDLLaunch.cpp` (yield 31-66, main loop 400-480, visibility 15-28 and 1435),
`Source/SDLLaunch/CMakeLists.txt` (link flags), `Source/GL4ESDrv/Src/GL4ESRenderDevice.cpp:1432`, git 682643d (the revert);
engine-ut99 `Source/UnrealTournament/Src/Launch.cpp` (18-66, 680-730), `Source/NSDLDrv/Src/NSDLViewport.cpp:515-530,888`,
`Source/UnrealTournament/CMakeLists.txt:99-190`; engine-unreal1 `Source/Unreal/Src/SDLLaunch.cpp:545-580`; the frontend's
`docs/migration/steps/2.43-frame-pacing.md`.

| Item | engine-ut2004 | engine-ut99 (and unreal1) | UC2 before | UC2 now |
|---|---|---|---|---|
| Resume primitive | `EM_ASYNC_JS` awaiting `setTimeout(0)` (2.43: MessagePort lost 44% of frames to coalescing; worker rAF rejected by the owner: 60/30 hopping) | the same | the same (`SDLLaunchWeb.cpp`) | unchanged |
| Place in the frame | after Tick and the tick-rate limiter, before `UpdateInput` (the message pump) | after Tick; `emscripten_sleep(ms)` when the cap leaves >1 ms, else the yield | the same as ut2004 | unchanged |
| When | every frame unless the limiter slept (`GUseFixedTimeStep \|\| !IncludeMessaging \|\| GetMaxTickRate()<=0`) | every frame (sleep or yield) | the engine-rate test: with the launcher's own cap (`-maxfps`, `-framepattern`) the limiter slept AND yielded (two event-loop turns per frame) | **yield iff the limiter did not sleep** (`LimiterSlept`), the same rule as ut2004's |
| Frame cap | none (uncapped standalone; net client capped by the engine) | `[Engine.Engine] FrameRateCap`, default 120, the launcher writes it with `-ini:` | `-maxfps=` (tests only) | `[Engine.Engine] FrameRateCap`, default 0 = uncapped (ut2004); F-4e |
| Present | `explicitSwapControl` + `renderViaOffscreenBackBuffer`, `emscripten_webgl_commit_frame()` per frame, but **no** `-sOFFSCREEN_FRAMEBUFFER` (reverted 682643d: its gl4es took raw entry points past Emscripten's bind-0 -> defaultFbo redirect, so the canvas was black with the flag; commit is then a stub and Chromium composites when the worker task ends) | the same attributes **with** `-sOFFSCREEN_FRAMEBUFFER` (2.43's root cause: without it the implicit path showed ~1 frame/s on Windows/D3D11) | with the flag, and the canvas renders (UC2GL4ESDrv hands gl4es `emscripten_webgl_get_proc_address`; web-renderer.md G3 checked `blitOffscreenFramebuffer` in the loader; why ut2004's gl4es got past the redirect and UC2's does not is NOT established) | **kept, a deliberate difference from ut2004**: ut99's lesson applies and ut2004's reason does not (the boot spec below draws the menu with it). The M11 link block belongs to f3-webgl2 now: not touched |
| `-sASYNCIFY_IMPORTS` | `['emscripten_sleep','emscripten_set_main_loop','__syscall_openat']` | the same | not set (Emscripten's defaults) | **ut2004's list** (Emscripten appends its defaults, so no functional change) |
| Hidden tab | `emscripten_set_visibilitychange_callback`: `alListenerf(AL_GAIN, 0/1)`, the engine keeps ticking (D-16) | nothing | nothing | **ut2004's callback** (`SDLLaunchWeb.cpp`, registered once the engine exists; UC2ALAudio never sets the listener gain) |
| Timers while hidden | the worker's setTimeout is not throttled like a background page's main thread; nothing special | the same | the same | unchanged |
| Exceptions in the frame | catching off for the engine (F-4a) | the same family | catching on (invoke_ trampolines, Asyncify around them) | off (F-4a) |
| Pool, stack | `PTHREAD_POOL_SIZE=4`, `STACK_SIZE=2MB`, `ASYNCIFY_STACK_SIZE=256K` | the same | the same | unchanged |

What caps the web's frame rate (answer for F-2, by reading; not measured in a browser here): nothing on purpose (no cap,
UseVSync does not apply to a worker's WebGL context). In practice each frame pays the yield: a `setTimeout(0)` that
Chromium clamps to >= 4 ms once timers nest (each resume runs the next frame inside the previous timer's task;
2.43 measured the timer's p50 wait at 4.0 ms), plus the Asyncify unwind/rewind of the engine stack, plus the blit of
`commit_frame`. So the web's ceiling is about 1000 / (frame work + ~4 ms), and the browser's compositor shows at most
the display rate.

## Step 5 - F-4d: files and docs (commits e178ae8, 81bed5d)

Line endings and encoding, checked in the siblings (`file -k`, read-only):
- engine-ut2004 `Engine/Config/{Default,DefUser}.ini` and `web/*.ini`: LF ("ASCII text", "with escape sequences", one
  UTF-8 web DefUser.ini). engine-ut99 `Config/System/*.ini`: LF. engine-unreal1 `Engine/Config/*.ini`: LF.
- Sources: engine-ut2004 kept Epic's CRLF in 1,736 of 1,788 `.cpp/.h/.uc` (its UC2-like files that it rewrote are LF)
  and Latin-1 (35 ISO-8859 files); engine-ut99 (326 `.cpp/.h`) and engine-unreal1 (883) are LF throughout, 0 CRLF.
  UC2: every source LF since the import (0 CRLF files among `git ls-files Source` `.cpp/.h/.uc`), 161 ISO-8859 files kept
  as Latin-1. So UC2 matches ut99/unreal1 on line endings and ut2004/ut99 on encoding; recorded in CLAUDE.md. Nothing
  re-encoded.
- Done: `Engine/Config/{DefUser,DefaultXbox,DefUserXbox,KeyBinds}.ini` and `web/DefUser.ini` CRLF -> LF (only `\r`
  bytes removed: 355 + 399 + 345 + 139 + 355 lines; no lone CR anywhere). `file -k` now says "ASCII text" for all seven.
  `tools/dev/sync-web-ini.py` refuses a CR in any `Engine/Config` or `web` ini (--check fails, a rewrite stops) and
  writes LF. `--check`: both web files up to date.
- Diff size against Epic's import (`git diff --numstat 7633696`): Default.ini 936 changed lines both before and after
  (it was already LF; the real change is 214 lines with `--ignore-cr-at-eol`); DefUser.ini 110 before (still CRLF, the
  real 110) and 664 after (every line's ending); the three reference copies 0 -> 798 / 690 / 278. The meaningful diff
  is `git diff --ignore-cr-at-eol 7633696 -- Engine/Config`: 214 / 110 / 0 / 0 / 0 (Engine/Config/README.md says so).

Docs: README.md (what / changes / status / building, as engine-ut2004's README), BUILD.md (status, a web section with the
three modes and the artifact, meta.json, arm64, the frame-rate log, the fetched libraries with GL4ES), CLAUDE.md (the
encoding facts), BLOCKERS.md (arm64 entry no longer contradicts itself), docs/SOURCE-LAYOUT.md and client-boot.md (the
ini CRLF statements). AGENTS.md is a pointer to CLAUDE.md, as engine-ut2004's: not stale, unchanged.
`UC2_ASSETS_DIR`: the CMake cache variable (`CMakeLists.txt:33`) was read by nothing: removed, and its BUILD.md row.
The tools' environment variable of the same name (mkrun-uc2.sh, map-census.sh, script-build.sh, package-windows.sh:
`${UC2_ASSETS_DIR:-$work/assets/uc2}`) is a working override and stays.

meta.json (engine-ut2004's fields): `cmake/ArtifactDeps.cmake` copied byte for byte from engine-ut2004 (ufront 2.70);
`Source/SDLLaunch/cmake/Web.cmake` passes `--dep-gl4es` and `--dep-openal-soft` from `GL4ES_GIT_*`/`OPENALSOFT_GIT_*` (the
cache variables FetchContent reads), the emitter (already engine-ut2004's) turns them into `toolchain.<lib> =
owner/repo@branch`. Not a commit: engine-ut2004 records the branch on purpose ("never a commit, because the branch tip is
what a rebuild takes"); the brief asked for a commit too: not added, because the owner's rule is "we follow what UT2004
does" (if wanted, a `toolchain.<lib>Commit` field from `_deps/<lib>-src` HEAD at link time is a few lines). SDL2 on the
web is Emscripten's port (named by `emscripten`), so it is not listed, as in engine-ut2004 (which lists karma too).
`buildFlags`: engine-ut2004's nine plus what UC2 links that it does not. Example (build-wasm-perf at e178ae8):
```
"toolchain": { "emscripten": "5.0.7", "gl4es": "10-p/gl4es@fix/cubemap-bind-active-unit",
               "openal-soft": "10-p/openal-soft-web@ufront/web" },
"buildFlags": [ "-sASYNCIFY", "-sFULL_ES2", "-sALLOW_MEMORY_GROWTH", "-sFORCE_FILESYSTEM", "-sWASMFS", "-pthread",
  "-sPROXY_TO_PTHREAD", "-sOFFSCREENCANVAS_SUPPORT", "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas", "-sPTHREAD_POOL_SIZE=4",
  "-sMAXIMUM_MEMORY=2147483648", "-sMIN_WEBGL_VERSION=1", "-sMAX_WEBGL_VERSION=1", "-sOFFSCREEN_FRAMEBUFFER",
  "-sNO_DISABLE_EXCEPTION_CATCHING" ]
```
(f3-webgl2 moves the WebGL limits to 2: the list must follow it.)

## Step 6 - F-4e: every command-line switch we added, and what happens to it (2026-10-09)

Census: every `Parse/ParseParam(appCmdLine()|CmdLine|Parms, TEXT("..."))` and `appStrfind(appCmdLine(), ...)` in the
built packages, compared with the import (`git show 7633696:...`): 131 tokens, of which the ones below are ours (the
rest are Epic's: `-LOG`, `LOG=`, `INI=`, `USERINI=`, `-SERVER`, `-LAZY`, `-BENCHMARK`, `SECONDS=`, `RECORDMOVIE`, `EXEC=`,
`FirstRun`, `consolecommand=`, `testrendev=`, `TestRenDev`, `-nosound`, `-lanplay`, `CPUSPEED=`, `-NOMMX`/`-NOSSE`,
`MULTIHOME=`, `PRIMARYNET`, `PORT=`, `PktLoss=`/`PktOrder=`/`PktDup=`/`PktLag=`, `-norunaway`, `-NOGC`, `-CONFLICTS`,
`-profilestats`, `-safe`/`readini`, `ONLYLOGLOWFPS`, `UPT`, the `-320x240`..`-1600x1200` resolution switches of
`Launch.cpp`, and the editor/commandlet parameters). `-ini:` and `-log` stay. "Tools" = tools/dev scripts that pass it
(grep), "Epic" = the token exists in Epic's import.

| Switch | Where | Tools | Epic | Proposal / done |
|---|---|---|---|---|
| `-maxfps=<n>` (D-6) | SDLLaunch.cpp | d6-mouse, d9-rebind, jit-trace, sc-saveconfig | `MAXFPS=` only as the PC benchmark's score cap (UnEngineWin.h:948), not a limiter | **converted**: `[Engine.Engine] FrameRateCap=<fps>` (the key engine-ut99 and engine-unreal1 read and the frontend's launcher writes with `-ini:`); default 0 = uncapped as engine-ut2004 |
| `-framepattern=<ms>:...` (jitter) | SDLLaunch.cpp | jit-trace, jit-strip | no | **converted**: `[SDLLaunch] FramePattern=` (test hook, not written to any ini) |
| `-frametrace=<n>` (jitter) | UC2FrameTrace.cpp | none (the tools use the console command) | no | **removed**; console `FRAMETRACE <n> [tag]` stays |
| `-audiowave=<file>` (M8) | UC2ALAudioSubsystem.cpp | run-uc2-audio, m9-movies.py, m12-wine-smoke | no | **converted** to the existing key `[UC2ALAudio.UC2ALAudioSubsystem] WaveFile=` |
| `-noefx` (M8) | UC2ALAudioSubsystem.cpp | audio-regression | no | **converted** to the existing key `UseEFX=False` |
| `-nostartupmovies`, `-nomovie` (M9) | UnMovie.cpp | -nostartupmovies: 15 scripts (fps-bench, fx-owner, jit-strip, m10-controls, m12-wine-smoke, m13-64bit, m14-scripts-check, m9-movies, net-test, o9-nvidia, run-uc2-audio, run-uc2-shot, sc-saveconfig, d2 web, ...) and every agent brief | no (UE3's names) | proposed: **remove**, the keys exist (`[UC2Movie] StartupMovies=False`, `Cinematics=`, `LoadingMovie=`); not done here: every running agent passes it today, so it is one sweep across all scripts at a merge point (owner/coordinator) |
| `-nullmovies` (M9) | UnMovie.cpp | none | no | proposed: ini key `[UC2Movie] NullRendererMovies=True` or remove (no user) |
| `-movieshot=<s>,...` (M9) | UnMovie.cpp:301 | m9-movies.py | no | proposed: `[UC2Movie] ShotTimes=` test key (not done: time) |
| `-nettest=<file>`, `-netdrop=` (net-join) | UC2NetTest.cpp | net-test.py (`-nettest`) | no | proposed: a `[UC2NetTest]` section (`CommandFile=`, `Drop=`); not done (time; net-test needs 3 processes) |
| `-netpktid=`, `-netoldacks` (net-loss) | UnConn.cpp | net-test.py (`-netoldacks`) | no (Epic has `PktLoss=` etc. in the same place) | proposed: `[UC2NetTest] PacketIdStart=`, `OldAcks=`; or keep next to Epic's `Pkt*=` network-emulation switches (owner) |
| `-noinimigrate` (D-6) | UC2InputMigrate.cpp | d6-mouse | no | goes with UC2InputMigrate (F-1 removes it) |
| `-inputtest=<file>` (M10) | SDL2Drv SDL2Client.cpp | m10-controls, m10-inject, fx-owner, fps-bench, jit-strip, c1-ini-overrides, m12-wine-smoke, net-test | no | owner decision: it is the test channel every game test drives; proposed `[SDL2Drv.SDL2Client] InputTestFile=` (f1-vanilla owns SDL2Drv now) |
| `-inputlog` (M10) | SDL2Pads.cpp | none | no | proposed: **remove**, `[SDL2Drv.SDL2Client] LogInput=True` exists |
| `-defaultres`, `-windowed` | SDL2Drv | none | yes (WinDrv; engine-ut2004's SDL2Drv has both) | keep (Epic's) |
| `-usehomedir` | FFileManagerUnix.h | none | no; engine-ut2004 has the same opt-in (cedc623) | keep: it decides where the ini lives, so it cannot be an ini key |
| `-nullrenderer`, `-opengl`, `-gl4es` | SDLLaunch.cpp render-switch table | 18 / 6 / 15 uses | no; engine-ut2004 has the same table (`-NULLRENDERER`, `-OPENGLRENDERER`, `-GL4ESRENDERER`) | keep (engine-ut2004's convention); `-ini:System:[Engine.Engine]:RenderDevice=...` does the same |
| `-glerrors`, `-nopixelshaders`/`-pixelshaders`, `-nopostprocess`/`-postprocess`, `-noxboxshadows`/`-xboxshadows`, `-noxboxprojectors`/`-xboxprojectors`, `-noterrain3layer`/`-terrain3layer`, `-glaa=`/`-glnoaa`, `-glunits=`, `-glstages=`, `-glnoffprograms`/`-glffprograms` | UC2GLDrv | glerrors 39, nopostprocess 34, glnoaa 14 ... | no | proposed: **remove**, each has its key in `[UC2GLDrv.UC2GLRenderDevice]` (CheckGLErrors, UsePixelShaders, UsePostProcess, UseXboxShadows, UseXboxProjectors, UseTerrain3Layer, AntiAliasing, MaxTextureUnits, MaterialStages, FixedFunctionPrograms): tools pass `-ini:` (f5-render owns UC2GLDrv now) |
| `-glshot=`, `-glshotramp`, `-glshotpresented`, `-glexec=`, `-glpptoggle=` | UC2GLDrv | glshot 39, glexec 32, glpptoggle 17 | no | proposed: test keys (e.g. `[UC2GLDrv.UC2GLRenderDevice] ShotTimes=`, `ExecList=`, `PostProcessToggleTimes=`); the most used test hooks after -inputtest (f5) |
| `-glselftest`, `-glselftest1d`, `-glcombinertest`, `-glmatcensus`, `-glsnapshotlog`, `-glstats`, `-glerrorsdraw`, `-glffsource`, `-gldualdiffuse=`, `-glprojnoclip`, `-glnofog`, `-glnolodbias`, `-glnocombine3/4`, `-glnodepthtexture`, `-glnofragdepth`, `-glnouintindex`, `-glnomaxlevel`, `-glnos3tc`, `-nogamma` | UC2GLDrv | a few each (m11-twin, o9-nvidia, m7-effects, pp-scale) or none | no | proposed: diagnostics with a user -> test keys; without a user (glselftest1d, glsnapshotlog, glstats, glerrorsdraw, glffsource, glprojnoclip, glnofog, nogamma) -> remove (f5) |
| `-gl4esdebug` | UC2GL4ESDrv | none | no | proposed: remove or a key (f3-webgl2 owns the binding) |
| `CONFORM`, `CONFORMNOCONFIG`, `NOEPICORDER` | Editor commandlets | script-build | no | keep: `ucc make`/`stripsource` commandlet parameters, not game switches |
| `DUMP=`, `LOAD=`, `MAP=`, `REF=` | UCC/UC2ALAudio/UC2Movie commandlets | several | no | keep: commandlet parameters |

Done now (separate commits, each with its tools): FrameRateCap + FramePattern; FRAMETRACE's switch; WaveFile/UseEFX. The rest
waits for the owner's decision per row (and, for SDL2Drv/UC2GLDrv/UC2GL4ESDrv, for the agents that own those files now).

## Results of the gate runs (2026-10-09, 02:20-02:51; machine load 20-38 from the other agents' builds throughout)

F-4a, measured:
- `CENSUS_NODE=1 CENSUS_NAME=f4-node-census CENSUS_PORT=7900 capped 7G 2400 -- tools/dev/map-census.sh build-wasm-perf 45 3`
  -> `maps: 58  PASS: 58  FAIL: 0` (`logs/f4-node-census/summary.tsv`): every map `up for play`, critical 0, aborts 0,
  still running at 45 s (56 maps 2 warnings = TransBuffer + the listen warning; 2 maps 1). The ucc-bin.js of 3c6a8f4's
  code (catching off). Before the UdpLink fix the first try ended with a JavaScript `TypeError` in SOCKFS (fd 0).
- Sizes (wasm-perf, -O3): `SDLLaunch.wasm` 14,929,569 (main c59c02d, catching on) -> 13,668,962 bytes (-8.4%);
  `ucc-bin.wasm` 11,809,272 -> 10,845,392 (-8.2%). Compile flags checked in `build-wasm-perf/Source/*/flags.make`: no
  `NO_DISABLE_EXCEPTION_CATCHING`/`-fexceptions` on any engine target, `-fexceptions` on openal-soft's two.
- Browser boot spec, artifact snapshot `logs/f4/artifact-f4ab` (F-4a+F-4b, wasm sha256 c61ec550...; the same before
  and after the run), assembled with `yarn build --target uc2 --engine-artifact <snapshot> --out run/f4-web-after`,
  served from there (`scratch/e2e-run.sh`: refuses if another e2e-mem-guard or :8080 exists; null sink by module index):
  **4 passed (4.8m)** (`logs/f4/e2e-boot-after.txt`), browser PSS peak 1517 MiB, machine in-use peak 17.9 GB (others ~12).
  The menu and DM-Deadbolt draw with `-sOFFSCREEN_FRAMEBUFFER` kept.
F-4b, browser frame times (`uc2-wasm-jitter.spec.js`, the frame trace's `dt` of every traced frame: 4 segments of
running), back to back at the same load (35-36), main's artifact (c59c02d) vs F-4a+F-4b:

| | frames | mean | p50 | p90 | p99 | min | max | > 2x median | successive swing > 1.5x |
|---|---|---|---|---|---|---|---|---|---|
| before (main) | 134 | 60.95 ms (16.4 fps) | 58.71 | 81.40 | 131.24 | 31.63 | 172.83 | 2.2% | 17.3% |
| after (F-4a+b) | 175 | 46.62 ms (21.4 fps) | 41.94 | 66.10 | 100.18 | 26.82 | 193.46 | 3.4% | 13.2% |

Both runs FAILED the spec's judder limit (1.5 px; e.g. strafe right 2.17 before / 1.56 after): at 40-60 ms frames on a
machine at load 36 the camera's walk bob alone exceeds it (the O-11 runs had 21-57 ms). The frame times are the
measurement asked for; one run each, under heavy unrelated load: **seen, not established**. The direction (shorter
frames after) agrees with removing the invoke_ trampolines; it is not a benchmark.

Native checks after F-4a..F-4e (run tree f4 from build-native at the F-4e source):
- `ucc-bin verifyclasses` -> `All native class layouts match`; `CENSUS_NAME=f4-census tools/dev/map-census.sh build-native 15 4`
  (capped 6G) -> `maps: 58  PASS: 58  FAIL: 0`.
- Windows (build-windows, MinGW i686) under wine, `M12_TREE=f4-win tools/dev/m12-wine-smoke.sh verify capture`: verify PASS;
  capture FAIL on the first try at load 40 (music correlation 0.445 at 8.60 s: the capture itself was written through
  the WaveFile key), PASS on the rerun (correlation 0.841 at 0.40 s, gain error -0.01 dB). Flaky under load: seen, not
  explained further.

F-4e, the tools that were changed, rerun (logs under logs/f4):
| Tool | What it exercised | Result |
|---|---|---|
| jit-trace.sh kbm uneven / kbm 60 | FramePattern / FrameRateCap through -ini: | PASS 0.63 px / PASS 0.30 px; dt mean 23.75 ms (pattern), 17.54 ms (cap 60) |
| jit-strip.sh | FramePattern | 8 pictures, Critical 0; dt values exactly 40/55/70 ms |
| d6-mouse.sh look | FrameRateCap=60 | 36 PASS, 0 FAIL |
| d9-rebind.sh actions | FrameRateCap=60 | 10 PASS, 0 FAIL |
| sc-saveconfig.sh g | FrameRateCap=60 | ALL PASS (33 checks) |
| audio-regression.sh f4 (run-uc2-audio.sh) | WaveFile=, UseEFX=False | all items PASS / Critical 0; praxis-dry logs "EFX switched off (UseEFX=False)" |
| m9-movies.py --only cinematic | WaveFile= | 7 PASS |
| m12-wine-smoke.sh capture | WaveFile= (wine) | PASS on the rerun (above) |
| map-census.sh CENSUS_NODE=1 / native | the new mode / unchanged mode | 58/58 / 58/58 |
| fps-bench.sh | new | PASS (table above) |
| d6-mouse.sh pad (D6_SHIP_TREE=f4ship: UC2_SCRIPTS=assets/uc2-shipped/System) | FrameRateCap=30, ours and shipped scripts | 9 PASS, 0 FAIL |
Not rerun: d9-rebind `match`/`restart` (they need the menu scenario's User.ini first; the same FrameRateCap argument as
`actions`).

## Final builds at 610887e (2026-10-09 03:2x, all inside bin/capped 6G, -j4; logs/f4/build-*-final.log, build-native64.log)

| Preset | Result |
|---|---|
| native | exit 0; `uc2.bin` ELF 32-bit i386; verifyclasses all match; census 58/58 |
| native64 | exit 0 (first build of this worktree); `ucc-bin` ELF 64-bit x86-64; verifyclasses: 401 classes, 0 mismatches, 6,588 scripts scanned |
| windows | exit 0; `uc2.exe`/`ucc.exe` PE32 i386; wine verify PASS, capture PASS (rerun) |
| wasm-perf | exit 0; SDLLaunch.wasm 13,668,590 bytes (main: 14,929,569), ucc-bin.wasm 10,845,392 (main: 11,809,272); meta.json gitSha 610887e with gl4es/openal-soft |
| arm64 | exit 0 (`--target UCC`); `ucc-bin` ELF 64-bit aarch64; verifyclasses under qemu-aarch64: 401 classes, 0 mismatches |

Memory ceilings used: builds 6G (none hit), game/ucc runs 2G, Node census 7G for 3 workers, browser runs 7G scope +
e2e-mem-guard 18 GB machine-wide (peaks 14.5-17.9 GB machine-wide, of which ~12 GB other agents). No ceiling was hit
(no exit 137). Leftovers checked after each run: none; the null sink was unloaded by index every time (0 left).

## Open questions for the owner

1. `-sOFFSCREEN_FRAMEBUFFER`: kept (engine-ut99's choice and frontend 2.43's root-cause fix; UC2's GL4ES path renders
   with it), which differs from engine-ut2004 (dropped there because its gl4es bypassed the redirect). Keep?
2. `FrameRateCap` default: 0 (uncapped, engine-ut2004) or 120 (engine-ut99/unreal1's default)?
3. meta.json: UT2004's `repo@branch` only (done), or also the fetched commit (the brief asked; UT2004 says never)?
4. The F-4e table's undecided rows: `-nostartupmovies`/`-nomovie` (keys exist; every agent uses the switch today),
   `-inputtest`, the UC2GLDrv switches with existing keys, the test hooks (`-glshot`, `-glexec`, `-glpptoggle`,
   `-movieshot`, `-nettest`...), the render switches (engine-ut2004 keeps the same table).
5. Engine/Config's three Epic reference copies (DefaultXbox.ini, DefUserXbox.ini, KeyBinds.ini) are LF now too, so a plain
   `git diff 7633696` shows every line; `--ignore-cr-at-eol` shows 0. Fine, or keep those three byte-identical (CRLF)?

# F-4 follow-up: the owner's answers (2026-10-09, worktree engine-uc2-f4b, branch f4b-switches from main b6fcffb)

The owner's decisions on the five questions: (1) switches: keep `-log`, `-ini:`, `-windowed`, `-defaultres`, `-usehomedir`,
`-nullrenderer`/`-opengl`/`-gl4es` and the commandlet parameters; convert the test switches to ini keys set with `-ini:`;
remove the UC2GLDrv on/off twins and the unused probes. (2) `FrameRateCap` default 0. (3) meta.json: branch only.
(4) `-sOFFSCREEN_FRAMEBUFFER`: keep. (5) the three Epic reference inis back to Epic's bytes.

## Step 7 - (5) Epic's three reference inis (commit 9d5683a)

`Engine/Config/{DefaultXbox,DefUserXbox,KeyBinds}.ini` copied from `epic-sources/System/`: `cmp` silent for all three
(`file -k`: "ASCII text, with CRLF line terminators"). `sync-web-ini.py`: `EPIC_COPIES` skipped by the CR rule and checked
with a byte comparison against epic-sources (`--check` fails if one differs); `--check` -> both web files up to date.
Who reads them (grep -a of Source, .uc included, and tools): **KeyBinds.ini IS read by the game**:
`Source/UCMenu/Classes/UC2Settings_Controller.uc:106` `GetConfigValue("KeyBinds", "Layout_" $ i, "Title", ...)` (the pad
layout page), from the run tree's System/KeyBinds.ini that mkrun copies from Engine/Config; it is Epic's file unchanged
(only its line endings had changed in F-4d; the config cache reads CRLF and LF alike). `DefaultXbox.ini` and
`DefUserXbox.ini`: read by nothing (the engine derives Avaris.ini/User.ini from Default.ini/DefUser.ini only).
README (Engine/Config) and CLAUDE.md say so.

## Step 8 - (2) FrameRateCap, (3) meta.json, (4) OFFSCREEN_FRAMEBUFFER

- (2) Confirmed: `Engine/Config/Default.ini:25` and `web/Default.ini:25` `FrameRateCap=0`; absent key = 0 too
  (`SDLLaunch.cpp:234`: `MaxFps = 0`, then `GetFloat`); nothing caps a standalone game.
- (3) No change: `toolchain.<lib> = owner/repo@branch` (engine-ut2004's rule).
- (4) Kept. The reason, read in Emscripten 5.0.7 (`src/settings.js:1766-1782`, `src/lib/libhtml5_webgl.js:150-210,270-300`,
  `src/lib/libwebgl.js:720-730,856-862,1135`): Emscripten documents `-sOFFSCREEN_FRAMEBUFFER` as the polyfill for explicit
  swap control (and for WebGL from a pthread) where OffscreenCanvas is missing, and with OffscreenCanvas support compiled in
  as well "allows one to utilize OffscreenCanvas where available, and to fall back to Offscreen Framebuffer otherwise".
  **But** `UC2GL4ESDrv.cpp` creates its context with `explicitSwapControl = 1` AND `renderViaOffscreenBackBuffer = 1`
  requested explicitly, and Emscripten honours the second whenever the flag is linked: `GL.createContext` then makes the
  emulated default framebuffer (`createOffscreenFramebuffer`, `preserveDrawingBuffer` forced on) even on the normal
  OffscreenCanvas path, and `emscripten_webgl_commit_frame()` blits it (`blitOffscreenFramebuffer`) every frame. So in
  UC2's configuration it is not only the fallback: it IS the present path (the frontend's 2.43 fix: without the flag
  `commit_frame` is a stub and presentation is the implicit end-of-task one, ~1 frame/s on Windows/D3D11). The
  coordinator's wording "the normal path does not use it" would be true only if the driver stopped requesting
  `renderViaOffscreenBackBuffer` (the binding belongs to F-3's agent; not changed here).

## Step 9 - (1) the switches (commits c00a538, then the command-line commit)

Mechanism. Core `appReadTestKey(Key, Value, Section[, Section2, Section3])` / `appReadTestFlag` (`Core/Inc/UnFile.h`,
`Core/Src/UnMisc.cpp` after `appApplyIniOverrides`): a TEST key of the system ini, set with `-ini:`, read ONCE and taken
out of the in-memory ini of every named section; the file is marked dirty when something was removed (the first try
did not mark it, and the window-settings save at start-up had already written the override into Avaris.ini: the smoke
run showed `ShotTimes=40`/`Exec=46:exit` left in the tree's Avaris.ini; fixed and re-checked: only `CheckGLErrors=True`
(a setting) and `StartupMovies=False` remain). A log line `Test key [<section>] <Key>=<Value> (read once, not kept in the
ini)` says what was taken. The GL device reads its own class section, then `[UC2GLDrv.UC2GLRenderDevice]`, and removes
the key from both device sections. The device's ordinary SETTINGS keep their normal config semantics (own class section;
written back like any `-ini:` value). `tools/dev/glini KEY=VALUE...` prints the `-ini:` tokens for both device sections.

Found on the way: `SDLLaunch.cpp` cut the command line at 1,024 characters (at an argument boundary, silently) and `UCC.cpp`
had a 1,024-character buffer with NO bound check (stack overflow). With settings as `-ini:` tokens a test run passes 1,024:
the wine pad run lost its `InputTestFile` token and its channel never opened (`FAIL pad ... no probe line`). Both now 4,096
(Core's `GCmdLine`), a token that does not fit is dropped whole and a warning says so.

The frontend (read-only grep of `/home/ion/WorkUC2/ufrontend`, branch uc2; nothing changed there):
- `packages/e2e/uc2-wasm-boot.spec.js:150` `ENGINE_OPTIONS = '-log -glerrors -glstats -nostartupmovies'`
- `uc2-wasm-keys.spec.js:41`, `uc2-wasm-saves.spec.js:48` `'-log -nostartupmovies'`; `uc2-wasm-audio.spec.js:205`
  `launch(page, '-log -nostartupmovies')`; `uc2-wasm-jitter.spec.js:33` `'-log -glerrors -nostartupmovies'`
- `targets/uc2/target.json:134` the launcher's video-driver choice "GL4ES without post-processing": `-GL4ES -nopostprocess`
  (a user-facing option, not a spec; it stops working with this change; the replacement below; and note that an `-ini:`
  value is written into the browser's saved Avaris.ini, so that choice would then stick for later launches until changed
  back, unlike the old switch).
`-nostartupmovies` is converted too (coordinator: if the key works from `-ini:` at that point of start-up): the keys
`[UC2Movie] StartupMovies/Cinematics/LoadingMovie` are read by `appCanPlayFullscreenMovie` when a movie is asked for, and
the logos are asked for after `InitEngine`, long after `appInit` applied the `-ini:` tokens. Measured: every native runner
(m10-controls, run-uc2-shot, fx-owner...) now passes `-ini:System:[UC2Movie]:StartupMovies=False` and no logo plays.

## The switch table, final (F-4 follow-up; the "done" column is what the code does now)

| Switch (who added it) | Owner's decision | Done | Replacement / where | Tools changed |
|---|---|---|---|---|
| `-log`, `-ini:` | keep | kept | | |
| `-windowed`, `-defaultres` (Epic, WinDrv; engine-ut2004's SDL2Drv too) | keep | kept | | |
| `-usehomedir` | keep | kept | decides where the ini lives | |
| `-nullrenderer`, `-opengl`, `-gl4es` (render switches, engine-ut2004's table) | keep | kept | | |
| commandlet parameters (`CONFORM`, `CONFORMNOCONFIG`, `NOEPICORDER`, `DUMP=`, `LOAD=`, `MAP=`, `REF=`) | keep | kept | | |
| `-maxfps=` (D-6) | (F-4e) | converted at F-4e | `[Engine.Engine] FrameRateCap` (default 0) | d6-mouse, d9-rebind, jit-trace, sc-saveconfig (F-4e) |
| `-framepattern=` (jitter) | (F-4e) | converted at F-4e, one-shot now | `[SDLLaunch] FramePattern` test key | jit-trace, jit-strip (F-4e) |
| `-frametrace=`, `-audiowave=`, `-noefx` | (F-4e) | removed / converted at F-4e | console `FRAMETRACE`; `[UC2ALAudio.UC2ALAudioSubsystem] WaveFile=`, `UseEFX=` | (F-4e) |
| `-inputtest=` (M10) | convert | converted | `[SDL2Drv.SDL2Client] InputTestFile` (test key) | m10-controls, m10-inject (doc), fx-owner, f5-flicker, jit-strip, fps-bench, net-test.py, m12-wine-smoke, c1-ini-overrides (doc) |
| `-inputlog` | remove | removed | `[SDL2Drv.SDL2Client] LogInput=True` exists | none used it |
| `-glshot=`, `-glshotramp`, `-glshotpresented`, `-glexec=`, `-glpptoggle=` | convert | converted | test keys `ShotTimes`, `ShotRamp`, `ShotPresented`, `Exec`, `PostProcessToggleTimes` | run-uc2-shot, m7-effects, m7b-census, o9-census, o9-nvidia, m11-twin, pp-scale, m9-movies, m12-wine-smoke, m13-64bit, m14-scripts-check |
| `-glpstoggle=` (M6) | (not named; a test hook with users) | converted | test key `PixelShaderToggleTimes` | run-uc2-shot, m11-twin |
| `-glfftoggle=` (O-9) | (not named; no user) | removed | console `FFPROGRAMS` | none |
| `-movieshot=` (M9) | convert | converted | `[UC2Movie] ShotTimes` (test key) | m9-movies |
| `-nettest=`, `NETDROP=`, `NETPKTID=`, `-NETOLDACKS` | convert | converted | `[UC2NetTest] CommandFile`, `Drop`, `PacketIdStart`, `OldAcks` (test keys) | net-test.py |
| `-nostartupmovies` (M9) | convert if the key works from `-ini:` at that point | converted (it does) | `[UC2Movie] StartupMovies=False` | 16 scripts + the two engine-repo web specs; the frontend's five uc2 specs: replacement strings for the coordinator |
| `-nomovie`, `-nullmovies` (M9) | (not named; no user) | removed | `[UC2Movie]` keys | none |
| `-glerrors`, `-[no]pixelshaders`, `-[no]postprocess`, `-[no]xboxshadows`, `-[no]xboxprojectors`, `-[no]terrain3layer`, `-glaa=`/`-glnoaa`, `-glunits=`, `-glstages=`, `-gl[no]ffprograms` | remove (each has its ini key) | removed | `CheckGLErrors`, `UsePixelShaders`, `UsePostProcess`, `UseXboxShadows`, `UseXboxProjectors`, `UseTerrain3Layer`, `AntiAliasing`, `MaxTextureUnits`, `MaterialStages`, `FixedFunctionPrograms`, each in the device's own section (`tools/dev/glini` writes both) | every script that passed them (glerrors 18, nopostprocess 9, glnoaa 7, o9-nvidia's units/ffprograms, m7b's noxboxprojectors); the frontend's `target.json` launcher option `-GL4ES -nopostprocess` (replacement given) |
| `-glselftest`, `-glcombinertest`, `-glmatcensus`, `-glstats`, `-glnolodbias`, `-glnocombine3/4`, `-glno{depthtexture,fragdepth,uintindex,maxlevel,s3tc}`, `-gldualdiffuse=` | probes used by tools -> test keys | converted | `SelfTest`, `CombinerTest`, `MatCensus`, `Stats`, `NoLODBias`, `MaskCaps=<list>`, `DualDiffuse` | m11-twin, m11-progcmp (doc), m13-64bit, pp-scale, o9-nvidia, fx-owner (doc) |
| `-glselftest1d`, `-glsnapshotlog`, `-glerrorsdraw`, `-glffsource`, `-glprojnoclip`, `-glnofog`, `-nogamma`, `-gl4esdebug` | unused probes -> delete | removed (code paths deleted; `LogFFPrograms`/`CheckGLErrorsPerDraw` members stay 0 for a debugger) | gl4es' own `LIBGL_DBGSHADERCONV`/`LIBGL_LOGSHADERERROR` environment for the last | none |
| `-noinimigrate` (D-6) | goes with F-1 | untouched here (F-1 removes UC2InputMigrate) | | d6-mouse (F-1's branch) |

## Step 10 - every tool touched, rerun (2026-10-09 10:00-14:40; machine load 3-36 from the other agents)

Commands: one tool at a time (`scratch/chain*.sh`, logs `logs/f4/f4b-tools/<tool>.txt`), trees `run/f4b*` (`run/m13-f4b`,
`run/m7b-f4b` where the tool insists on its prefix). Builds: native, windows, native64, wasm-perf at the final source.

| Tool | What it exercised | Result |
|---|---|---|
| run-uc2-shot.sh (smoke) | CheckGLErrors, ShotTimes, Exec, Stats; one-shot | 0 GL errors; `-glshot at 40.0 s`; `-glexec at 46.0 s: exit`; Avaris.ini afterwards: `CheckGLErrors=True`, `StartupMovies=False` (settings), no test key |
| m10-controls.sh (all 5) | InputTestFile, StartupMovies, AntiAliasing | keyboard 16, mouse 18, text 7, pad 33, both 14: PASS (10:05) |
| m12-wine-smoke.sh (13 checks) | the same under wine; ShotTimes; WaveFile | 13/13 PASS over two runs (keyboard/pad/pinned needed the 4,096-character command line; then PASS) ; final build: verify, gl PASS |
| fx-owner.sh praxis-near | InputTestFile, CheckGLErrors | GPU: `critical=0 glerrors=0 programs_failed=0+0 shots=[xbox raised]`; software (Mesa) at load 7: both shots, `0 GL errors in 76 frames` (its glerrors field reads the frame-600 line, which a 76-frame run never reaches); at load 22 the match did not start (8 frames: load) |
| f5-flicker.sh | StartupMovies, CheckGLErrors, InputTestFile | desktop GPU: 12 pictures, 0 GL errors in 1,987 frames; `F5_GL4ES=1` on the NVIDIA GPU: present shader does not compile there (known, render-f5.md "side finding"; the twin needs `F5_SOFT=1`) |
| jit-strip.sh / jit-trace.sh kbm uneven | FramePattern (one-shot) | 8 pictures, Critical 0, dt exactly the pattern / PASS 0.52 px; `Test key [SDLLaunch] FramePattern=... (read once)` |
| fps-bench.sh | InputTestFile, StartupMovies | PASS 67.3 fps (10 s, 720p, load 19) |
| c1-ini-overrides.sh | the -ini: mechanism with the runner's own -ini: tokens | first run: 1 FAIL (the check counted the runner's 4 overrides as well); check narrowed to its own section: ALL PASS |
| sc-saveconfig.sh u g m / w | settings written back; Node | ALL PASS / ALL PASS |
| audio-regression.sh (run-uc2-audio.sh) | StartupMovies, WaveFile | all items; menu music FAIL once at load 24 (correlation 0.461 at 8.30 s, the same signature as the wine capture flake of F-4), PASS on the rerun at load 10 (0.817 at 0.25 s, gain error -0.01 dB) |
| package-windows.sh | (text only) | DLL check PASS |
| m9-movies.py | [UC2Movie] ShotTimes, StartupMovies, CheckGLErrors | 19 checks, 0 failed |
| net-test.sh (all but soak, wine) | [UC2NetTest] CommandFile/Drop/PacketIdStart/OldAcks, InputTestFile | every check PASS except `lag.healthseen: NetB did not respawn` (seen twice before in the history; 23 earlier PASSes, 5 other FAILs); handshake-loss/compat/wrap all PASS; `Test key [UC2NetTest] ...` lines for every key |
| m7-effects.sh (14 + selftest) | UsePostProcess, ShotTimes, PostProcessToggleTimes, Exec, ShotRamp | selftest 15/0; 14 pairs critical=0 glerrors=0 programs_failed=0+0 |
| m7b.sh (tree m7b-f4b) | UseXboxProjectors, Exec, FX_ARGS | 11 PASS, 0 FAIL |
| m7b-census.sh / o9-census.sh (3 maps each, own trees) | Exec | census lines for every map (projectors, terrains / matcensus summaries) |
| o9-nvidia.sh | FixedFunctionPrograms, MaxTextureUnits, MaskCaps, CombinerTest, MatCensus | first run: `o9-caps` FAIL: the settings passed with -ini: persisted in the tree's Avaris.ini into the next run (fixed: fresh Avaris.ini per run, d8516d9); rerun: combiner mesa x4, gpu x3, twin, caps PASS. `pictures`/`pictures-other`: need main's executable as uc2-before.bin, see below |
| m11-twin.sh (all) | everything above on both devices; MaskCaps, NoLODBias, DualDiffuse, SelfTest | 27 PASS, 7 FAIL: the six F-5 knew (effect-blur, effect-liandri, effects, fallback-glnodepthtexture, fallbacks, movies) plus `m11-pinned`, whose two numbers swapped: desktop-with-LOD-bias vs twin 0.397, without 0.877 (F-5: 0.890 / 0.380). The NoLODBias key was applied to the right run (`Test key ... NoLODBias=True` only in pinned-deadbolt-nb.out); the twin's gl4es is now the fork's tip of 2026-10-09 02:14 (`6b6bccd`, fetched by today's configure; F-5 had `98441ed`), which honours the LOD bias (F-3's work) |
| pp-scale.sh selftest glow | Exec, ShotTimes, UsePostProcess, PostProcessToggleTimes | PASS x4 |
| m14-scripts-check.sh verify menu deadbolt effects | CheckGLErrors, ShotTimes, StartupMovies | PASS x4 |
| d78.sh (ref tree = this build) | UsePostProcess, CheckGLErrors via m10 | 15 PASS, 1 FAIL `world-third-person` 98.10% (>= 98.5): the same number as the run in logs/d78/run (known) |
| d2-ui-scale.sh run 640x480 1920x1080 | the same | menu and hud runs PASS at both sizes (measure not run: needs the reference trees with main's binaries) |
| m13-64bit.sh verify controls glselftest leftovers | 64-bit | verify PASS (401 classes, 50 placeholders), glselftest 15/0, leftovers PASS; controls: pad 29/33, both 13/14 (see below) |
| web: tools/dev/d78/web/uc2-d78.spec.js (frontend harness, my assembly run/f4b-web, wasm sha 24535e2b... before and after) | the -ini: strings through the launcher's Advanced field | 1 passed (37 s); `UC2GLDrv: frame 600, 0 GL errors so far` = CheckGLErrors=True reached the GL4ES device's section; mem-guard peak 13.5 GB machine-wide |
| map-census.sh CENSUS_NODE=1 | ucc-bin.js with the final Core | 58 / 58 PASS |
| verifyclasses (native, wine) | | all match |

The pad's left stick (m10 pad and both, m13 controls; 4 + 1 checks): FAILS since the asset tree's GUI.u/UCGameplay.u/UCMenu.u
were replaced at 11:28 by main's F-1 packages, and it is **not this branch**: (a) the branch BASE b6fcffb, built from
an export (`run/f4b-basesrc`) and run with ITS OWN tools (the old `-inputtest`), fails the same four checks today;
(b) main's binary (11:24, with F-1's engine side) with main's tools passes `pad (33 checks)`; (c) this branch passed
33/33 at 10:05, before the asset change. It goes away when this branch is merged with main (F-1's engine and config).

Not adopted: making `run-uc2-shot.sh` always start from a fresh Avaris.ini (it does so only with UC2_RES; every caller that
passes different SETTINGS per run sets UC2_RES today). A caller that does not, and passes a setting, inherits it in the next run.

Before/after comparisons with an executable OLDER than this change (`UC2_BIN=uc2-before.bin`, o9-nvidia's pictures,
d2/d78's reference trees with main's binary, pp-scale's uc2-base.bin) no longer get their test hooks: an old binary reads
`-inputtest=` and `-glshot=`, not the keys. Its settings still work (`-ini:` exists since C-1). A "before" for these tools has
to be built from a source with the keys.
