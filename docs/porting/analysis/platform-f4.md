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
