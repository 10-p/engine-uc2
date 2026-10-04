# engine-uc2 progress log

One dated entry per green gate (PLAN.md), newest last. Evidence goes in the entry.

- 2026-10-04 — **M0 gate green: workspace and scaffold.**
  - Workspace reorganised by rename only, nothing deleted: ISOs → `reference/iso`, community packs →
    `reference/community`, Xbox SDK dump → `reference/xdk`, owner's brief copied to `reference/`.
  - Pristine tree: `reference/epic-sources.sha256` (16,451 files); `sha256sum -c` after all M0 work: 0 mismatches.
  - Repo: first commit `7633696` = the Epic sources as imported (3,448 LF-normalised, 82 byte for byte, 570
    binaries left behind; `docs/SOURCE-LAYOUT.md`). `git ls-files` holds no package, media or binary file.
  - Presets: `native` (pointer width 4, `-m32`), `native64` (8), `wasm` (emcc 5.0.7 from the workspace's own
    `emsdk/`), `windows` all configure. Only `Thirdparty/zlib` is in the build so far.
  - Frontend: `ufrontend/` cloned at `7fb637e`, local branch `uc2` (no such branch on the remote).
  - Assets v0: `assets/uc2` = 595 files, 1.1 GB (58 maps + 9 ComboMatinees, 62 `.utx`, 45 `.usx`, 16 `.ukx`,
    17 `.u`, 29 top-level inis, 16 ladder inis, 5 languages), built by `tools/dev/mkassets-uc2.sh`; every file's
    sha256 is present in the pristine manifest.
  - Found on the way: `epic-sources/System` already holds `default.xbe`, `defaultLL.xbe`, `defaultR.xbe`,
    `xbox.bin` and `Script.lin` (the leak's own Xbox builds), so decompilation references exist without the ISO.
  - Still running at the time of this entry: unpacking of the Xbox SDKs and ISO-only files
    (`reference/README.md` is that agent's running notes).
- 2026-10-04 — **M0 follow-up: reference material unpacked** (notes and inventory: `<WorkUC2>/reference/README.md`).
  - `bin/extract-xiso` (v2.7.1, built from source). ISO-only files in `reference/iso-extract/`: `default.xbe`,
    `xbox.bin`, the update/dash/downloader XBEs, `media/`, `Startup/` (built splash `.xpr`), `System/` (404 files).
  - Retail `default.xbe`: title ID 4D570021, certificate 2005-03-24, every library 1.00.5849 with QFE up to 15
    ⇒ **built with XDK 5849.15** (`reference/iso-extract/XBE-LIBS.md`).
  - XDKs unpacked to `reference/xdk/unpacked/`: `2005-02-5849.15` (the exact match), `2005-03-5849.17` (differs in
    6 files), `2003-02-5344`. Layout `XDK/xbox/{include,lib,bin}`, docs `XDK/doc/XboxSDK.chm`. All previously
    missing headers are there (xtl, d3d8, xgraphics, xact, xmv, xhv, uix, xonline, xbdm, dsound, dsstdfx) plus
    `xsasm.exe`, `xmvtool.exe`, `xactbld.exe`, `bundler.exe`. Reference only: the portable build never includes them.
  - Retail `System/` vs the leak: 385 of 404 files identical. `DefUser.ini` == the leak's `DefUserXbox.ini`.
    `Default.ini` is a slightly later `DefaultXbox.ini` (net rates 7000→10000/20000, four log suppressions on).
    17 more differ with no counterpart: `Characters.ini`, `Gametypes.ini`, `Maplists.ini` and 14 localisation
    files (`reference/iso-extract/DIFF-vs-epic-System.md`). Which side is newer per file is an M15 question.
- 2026-10-04 — **M1 gate green: Core compiles** (commits `82c9fd3`, `9faf167`, `e7fc07d`, `c36e3d2`; notebook
  `analysis/core-port.md`). Verified by the coordinator from deleted build directories:
  `cmake --preset native && cmake --build build-native --target Core` → `libCore.a`, 0 errors (GCC 15, `-m32`);
  `cmake --preset wasm && cmake --build build-wasm --target Core` → `libCore.a`, 0 errors (emcc 5.0.7).
  24 files under `Source/Core` changed (20 of Epic's, 4 new); root CMakeLists and Thirdparty untouched.
  - Platform layer: `UnGnuG.h`, `UnUnix.cpp`, `FFileManagerUnix.h` rebased on engine-ut2004's with UC2's PC
    contract re-added (ANSI TCHAR macros, `va_list` varargs, `appTrunc`/`appInvSqrt`/`appSqrt(FLOAT)`,
    `appCycles64`, `appSystemTime64`, contiguous allocator, file streaming, `appGetLanguageID`). New pthread
    threading (`UnThreadingPosix.*`). `wordwrap.cpp` made portable and built.
  - Carried forward: launchers must set `GSynchronizeFactory`/`GThreadFactory` before `appInit` and link with
    `-rdynamic` + whole-archive; `UCC.cpp` still creates the Windows factories.
  - Risks noted for M3: the Visual Studio projects compile with 4-byte struct packing (`/Zp4`), which i386 GCC
    matches for 8-byte members and wasm32 does not; null-`this` checks in `UObject::IsValid`/`GetFullName`.
  - Remaining warnings: `-Wwrite-strings` (1,128) and "invalid use of incomplete type" in `UnObjBas.h`/`UnClass.h`
    (accepted under `-fpermissive`; to be looked at if M3 misbehaves).
- 2026-10-04 — **M2 gate green: Engine and the game natives compile, `ucc-bin` links** (commits `f700305`,
  `482cfbb` and the one carrying this entry; notebooks `analysis/engine-baseline.md`, `analysis/engine-port.md`).
  - Libraries on both toolchains, 0 errors: Core, Engine, IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI.
  - `build-native/Source/UCC/ucc-bin` links with 0 undefined references and 0 multiple definitions; started
    with no run tree it initialises and stops at Epic's missing-ini error.
  - Size of the job: Engine needed changes in 20 of its files, the six smaller packages in 6 files, plus 13
    misplaced inline markers found by the linker. Nothing was stubbed and no source file of the PC project was
    left out (only `UnSVehicle.cpp` and `palette.cpp`, which are in no project).
  - Fixed on the way: the package macro (`GPackage=G<pkg>Package`) and 4-byte struct packing
    (`SUPPORTS_PRAGMA_PACK=1` on every target; it was silently off under GCC).
  - Carried to M3: struct layout on wasm32/x86_64 for hand-written structs with 8-byte members; the
    `FColor`→`FPlane` conversion assumption; a transient clang crash at `-j12` (re-run fixes it).
- 2026-10-04 — **M3 gate green: every map loads on a headless dedicated server** (commits `44f81c6`, `63bb34e`
  and the one carrying this entry; notebook `analysis/runtime-port.md`).
  - `ucc-bin verifyclasses`: 0 layout mismatches between the shipped script packages and our native classes
    (713 checks over six packages; it started at about 120 mismatching classes).
  - `tools/dev/map-census.sh build-native 15 4`: 58 of 58 maps brought up for play and still ticking after
    15 s, twice in a row. `CTF-Octane` for 60 s: `Game class is 'CTFGame'`, up for play, clean shutdown.
  - Gate as planned said `batchexport`; that commandlet lives in Editor, which is not built yet, so the
    engine's own generated layout check lists were used instead (a more direct test of the same thing).
  - Eight real defects fixed on the way, none by stubbing: object construction zero-filling, an overlapping
    `strcpy`, three GCC header-tagging rules, a header behind its script, the first command-line argument
    being eaten, a privileged default port, and a clock race that made time run backwards.
  - Open: registry/commandlet short names (empty), layout check still to be run on wasm32 and x86_64.
- 2026-10-04 — **M4 gate green: the client boots to the menu with the null render device and takes keyboard
  input** (commits `dcf245f`, `e030ac7`, `49189ba`, `3d7e719`, `ab0e95c`; notebook `analysis/client-boot.md`).
  Verified by the coordinator from a deleted build directory:
  - `cmake --build build-native` links `uc2.bin` (SDL2 client + launcher) and `ucc-bin`, 0 undefined references.
  - `SDL_AUDIODRIVER=dummy timeout 40 xvfb-run -a -s "-screen 0 1280x720x24" ./uc2.bin -log` → still running
    at the timeout; `Avaris.log`: `SDL2 video driver: x11`, `Client initialized`,
    `Bringing Level Entry.MyLevel up for play`, `Game class is 'UCMenuGame'`,
    `Bringing Level Menu.MyLevel up for play`, `Opened viewport (SDL window 1, 640x480)`,
    `Render device: Engine.NullRenderDevice`; 0 Critical, 1 Warning (the editor transaction buffer class).
  - Real X key events (XTEST) into the window, GUI log un-suppressed in the run tree: the default menu is
    `UCMenu.UC2MainMenu`; Down/Down/Up are handled as `XC_PadDown`/`XC_PadUp` by its selection panel, Return
    opens `UCMenu.UC2Arena_Main`, Escape closes the message box.
  - Regressions: `ucc-bin verifyclasses` passes; `ucc-bin server DM-Deadbolt` now works by short name; map
    census 58/58; the eight web libraries build.
  - Three defects fixed: the Unix file manager matched wildcards case-sensitively, so the registry and the
    localisation lookup found no files (`*.INT` vs `*.int`); `appItoa` had no non-MSVC implementation; the
    client's config properties had no compiled defaults.
  - Config: `ViewportManager` → `SDL2Drv.SDL2Client`, `RenderDevice` → `Engine.NullRenderDevice`, new
    `[SDL2Drv.SDL2Client]` section (`Engine/Config/README.md`).
  - Not done here: gamepads (hook only, M10); anything drawn (M5); the web client (M4w). Open: blank
    localised help strings for native commandlets (BLOCKERS).
  - Also recorded: `analysis/package-format-v151.md`, the on-disk differences of UC2's packages that the
    frontend's Node tools must learn (established on the shipped `Core.u`).
- 2026-10-04 — Decision for M5 (was open in BLOCKERS): **one GL driver source**, `Source/UC2GLDrv`, with the GL
  binding selectable at build time (desktop GL through the SDL proc table; GL4ES for GLES2/WebGL), instead
  of two mirrored directories as engine-ut2004 has for historical reasons. M4w and M5 run in parallel in
  worktrees `../engine-uc2-m4w` (branch `m4w`) and `../engine-uc2-m5` (branch `m5-gl`); the coordinator merges.
- 2026-10-04 — **Visual reference ready** (not a plan milestone; input to M5–M7). Epic's unmodified `avaris.exe`
  renders under wine 10 + Xvfb (Mesa software GL) with Epic's own unpatched `Core.dll`.
  `<WorkUC2>/bin/oracle-shot.sh <name> [map] [seconds]` writes `<WorkUC2>/logs/oracle/<name>.png` plus the game
  and wine logs; ten reference pictures are there (title, main menu at 1280x720 and 640x480, DM-Deadbolt,
  DM-Praxis ×3, DM-Undertow, DM-Azure, CTF-Octane), described in `logs/oracle/README.md`. Run tree
  `run/oracle`, prefix `.cache/wine-oracle`. Reported by the agent, pictures not yet reviewed by the
  coordinator. Limits: no gamma ramp under Xvfb, UI laid out for 640x480, no real-Windows render to compare
  against, about 776 Mesa shader-cache files were written to `~/.cache/mesa_shader_cache` before the cache was
  redirected into the prefix.
- 2026-10-04 — Work was interrupted with M4w and M5 in progress: both agents were cut off mid-edit with nothing
  committed and no notebook yet (their changes survived as uncommitted edits in `../engine-uc2-m4w` and
  `../engine-uc2-m5`). They were resumed with their context and told to checkpoint first. Lesson applied to
  every later brief: commit and write the notebook after the first step, not at the end.
- 2026-10-04 — Coordinator reviewed the wine reference pictures: the 640x480 main menu is complete (text,
  layout, the Xbox "A" prompt), `DM-Deadbolt.png` is a proper lit in-game frame with HUD, and
  `DM-Praxis-windows-A.png` shows the known PC bug on the original binary (the round room's window panes are
  flat periwinkle with nothing behind them): the before-picture for M6/M7.
- 2026-10-04 — **M5 gate green: maps render with our OpenGL device** (branch `m5-gl` merged as `18224d9`;
  commits `f107ffb`, `1700520`, `34dd131`, `b0aac24`; notebook `analysis/gl-renderer.md`).
  - `Source/UC2GLDrv` (about 11k lines): the three-way merge planned in `analysis/design.md`, desktop OpenGL
    through SDL's proc table, default render target is a framebuffer object presented through a small shader
    that applies brightness and gamma. UC2's material compiler is carried over function for function with its
    stages kept in Direct3D terms and translated to GL in one place, so it stays diffable against Epic's file.
  - Verified by the coordinator under Xvfb (Mesa software GL): menu 1,200 frames and DM-Deadbolt 2,400 frames
    with 0 GL errors and 0 Critical; `-nullrenderer` still boots; after the merge `main` builds from clean,
    renders DM-Deadbolt, `verifyclasses` passes. On the branch: census 58/58, web libraries build.
  - Pictures in `<WorkUC2>/logs/m5/`: menu, DM-Deadbolt (pre-match and in play), CTF-Octane, DM-Praxis,
    DM-Azure. `coord-DM-Praxis.png` is from the same spawn as the wine reference `logs/oracle/DM-Praxis.png`
    and agrees with it in geometry, textures, lighting, pickups, character and HUD.
  - Not yet: pixel-shader materials (off until M6, so player skins and other materials PC drew with its DX8
    shaders take the stage-0 fallback), post-processing (M7), movies (M9), GL4ES/WebGL build of the driver
    (M11), a run on the real GPU, 1280x720 comparisons with a pinned spawn.
