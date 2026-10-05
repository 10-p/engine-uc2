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
- 2026-10-04 — **Incident: an unbounded browser test exhausted memory and the kernel killed the owner's desktop
  session** (details and cause in BLOCKERS.md). Both running agents (M4w, M6) died with it. Recovery:
  leftover harness servers and headless Chromium stopped; `bin/capped` (hard memory ceiling + time limit per
  process tree, verified: over-ceiling run killed in 0.25 s inside its own cgroup, nothing left behind);
  "Running anything" rules added to CLAUDE.md. State of the work: branch `m4w` has W0 and W1 committed
  (`8a232bd`, `5bd7f07`, `44932d1`, `45f7301`) and an uncommitted browser harness, none of it verified by the
  coordinator; branch `m6-shaders` has no commits (the agent had not written anything yet).
- 2026-10-04 — **M4w, steps W0 and W1 green; W2 (browser run) not done** (branch `m4w` merged; commits `8a232bd`,
  `5bd7f07`, `44932d1`, `45f7301`, `51a55f0`; notebook `analysis/web-smoke.md`). Verified by the coordinator
  on the merged tree, every run inside `bin/capped`:
  - W0: `node ucc-bin.js verifyclasses` (wasm32 under Node) → `all native class layouts match`. The known
    web risk (8-byte members aligned to 8 on wasm32) needed 4-byte packing on two hand-written script
    mirrors, `UnTex.h` and `UnParticleSystem.h`. `node ucc-bin.js server DM-Deadbolt` → `Game class is
    'UCDeathmatch'`, `Bringing Level DM-Deadbolt.MyLevel up for play`, then stops because Node's socket
    emulation has no `SO_BROADCAST` (the dedicated server cannot listen there; irrelevant to the client).
  - W1: `cmake --build build-wasm` → `SDLLaunch.js` (0.8 MB), `SDLLaunch.wasm` (47 MB dev build),
    `SDLLaunch.data`, `meta.json` (`targetId uc2`, UE2, `wasm-integrated`, ABI 1, three checksums). Explicit
    `-sMAXIMUM_MEMORY` of 2 GB on both wasm targets.
  - After the merge: native build, DM-Deadbolt with the GL device (1,800 frames, 0 GL errors, 2G ceiling),
    native and wasm `verifyclasses` all pass. `UC2GLDrv` stays out of the web build until M11.
  - W2 is the step that caused the memory incident. The agent's ad-hoc page (`tools/web-smoke/`, left
    untracked in `../engine-uc2-m4w`, not merged) kept every fetched file whole in JavaScript memory in each
    worker. It will not be used: the owner pointed to the frontend's own harness, which has the real
    file-streaming glue and a machine-wide memory guard (`packages/e2e/tools/e2e-mem-guard.mjs`). W2 becomes
    "minimal `uc2` target in the frontend clone, booted by its harness under that guard inside `bin/capped`".
- 2026-10-04 — **M6 gate green with two recorded gaps: Epic's Xbox shaders run as GLSL** (branch `m6-shaders`
  merged as `787c8c7`; commits `8289fbf`, `cf8330e`, `bd6a4e2`, `5cad7fd`; notebook `analysis/shaders.md`).
  Re-run by the coordinator, every run inside `bin/capped`:
  - `python3 tools/psh2glsl.py --selftest` → 100 shaders, 99 compared with Epic's compiled definition words
    (6,336 cases, maximum difference 0), 0 failed. `--gputest` under Xvfb → 107 shaders rendered, 6,848
    cases, 0 outside one 8-bit step (worst 0.5), all 107 also compile as GLSL ES 1.00.
  - DM-Deadbolt with shaders on: 59 fragment shaders compiled, 0 failed; 8 programs linked, 0 failed; about
    20,000 program draws; 0 GL errors over 1,800 frames. On/off pair from the pinned spawn
    (`<WorkUC2>/logs/m6/coord-deadbolt-0.png`, `-1.png`): with shaders on the player's helmet and shoulder
    pieces have the metal highlights the original PC binary shows in `logs/oracle/DM-Deadbolt.png`; off, they
    are flat as at M5; nothing else in the frame changes.
  - Merged tree: native build, null renderer, `verifyclasses`, census 58/58, wasm build all pass.
  - How it is verified, by class: numerically for all 100 shaders (source vs assembler output, GLSL vs
    interpreter); on screen against the original PC binary for the materials PC implemented (player skin,
    emissive/team shader on static meshes); on screen only as "runs clean and looks plausible" for the
    Xbox-only ones (team skin, specular-mask glass, emissive specular). There is no Xbox picture to compare.
  - Integrated 59 shaders; 25 wait for M7 (they need a back-buffer or depth copy). **Gaps against the gate:**
    the 12 projector shaders (the engine batches projectors only under `_XBOX`) and `PS_Terrain3Layer` (needs
    a render-caps change that alters how every terrain is drawn) are translated but not used. 37 of the 59
    integrated shaders are not reachable from shipped content at all (no DualDiffuse material exists in any
    package), so for those the numeric tests are the only evidence.
  - New ini switch `UsePixelShaders` (default True) restores the M5 behaviour for comparison.
- 2026-10-04 — **M4w complete: the game boots in a browser** (W2, through the frontend's own harness, as the
  owner directed). In the frontend clone `<WorkUC2>/ufrontend`, branch `uc2` (commits `2f4e39d`, `b244955`,
  `15ede5e`, `b5b0eb9`, local only): the package reader handles versions 145–151 (all 208 packages of the uc2
  tree parse to their exact length; other targets' output byte-identical), a minimal `targets/uc2` with
  neutral placeholder branding, and `packages/e2e/uc2-wasm-boot.spec.js`.
  - Coordinator's runs: `bin/capped 4G 420 -- node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=12 --
    --target uc2 uc2-wasm-boot.spec.js` → `1 passed (11.1s)`; `crossOriginIsolated: true`,
    `Bringing Level Entry.MyLevel up for play`, `Bringing Level Menu.MyLevel up for play`,
    `Render device: Engine.NullRenderDevice`; browser peak 1.5 GB, run peak 3.2 GB (ceiling 4 GB), machine peak
    9.4 GB (budget 12 GB), no profiles or processes left. Passed before and after the two engine fixes below.
  - Engine fixes it surfaced: `604a8a3` (a leading `Key=Value` token is a global option, not the map: the
    launcher passes the player name first and the engine tried to load a package of that name) and `429c17b`
    (the dev web build links at `-O2 -g2`: 9 MiB and about 0.5 GB to compile, instead of 45 MiB that did not
    compile within 3 GB).
  - Still to do on the web side (M11): the GL driver through GL4ES, audio, the network driver, removing the
    spec's `?map=Menu` workaround now that the engine fix is in, a real precache (the menu boot currently
    pulls 390 MiB through the dependency prefetch), and the tagged-property codec for version 148+.
- 2026-10-04 — Memory policy from the owner replaces the first, stricter ceilings: up to 16 GB for one piece
  of work when the memory is useful, up to 24 GB machine-wide for parallel work, kill above 24 GB.
  `CLAUDE.md` and `bin/mem-watchdog` (warn 16, kill 24) updated.
- 2026-10-04 — **M8 green (audio).** Merged `m8-audio` (9fdadf4). `tools/cook-audio.py` cooks `Avaris.xap` +
  `SoundData` + `Music` to Ogg Vorbis packs and cue tables (182 wave banks, 2,438 cues, 182 MB against the
  Xbox banks' 376 MB; 78/78 sound banks match the shipped `.xsb`, 181/182 wave banks match the `.xwb` entry by
  entry; 19 source WAVs missing, 15 cues unplayable). `UC2ALAudio` on OpenAL Soft (EFX zone reverb, music
  streaming, category volumes). Coordinator's re-run on a clean build of main, `tools/dev/audio-regression.sh
  coord`: `UC2AudioCheck` 7141/7141 waves at exact length, 0 failures; `UC2AudioSelfTest` 39 checks, 0 failed
  (gain, pan, pitch, loops, playlists, reverb decay); menu music identified as NuBattle at -19.02 dB (expected
  -19.00); four captures with 0 silent seconds, 0 failed cues, 0 Critical; `verifyclasses` passes; no audio
  device -> "sound is off" and the menu still comes up; a dedicated server opens no audio. Not established:
  that it sounds like the Xbox (captures sent to the owner to listen). Open in BLOCKERS.md: the localised
  taunt index defect in Epic's data, the 19 missing WAVs, absolute mix level against XACT. Headless runs must
  keep `ALSOFT_DRIVERS=null` (or the wave writer) so tests never play through the owner's desktop.
- 2026-10-05 — **M10 green (controls), native code and ini data only; no script edited.** Merged
  `m10-controls`. Game pads through SDL's GameController API after the Xbox driver (four ports, hot-plug,
  thresholds, rumble); the menus' key table is ini data (`[GUI.EmulatedJoypad]`), mouse hover/click/right
  click/wheel in the GUI, typing into on-screen keyboards; PC keyboard+mouse bindings in `DefUser.ini` next
  to the pad layout. Shared-code fixes: static native lookup ignores case (an ini key `Delete` broke
  UObject's `delete`), stick `SpeedBase` axes on PC no longer scaled by 20/dt. Coordinator's re-run on merged
  main (with audio), `tools/dev/m10-controls.sh`: keyboard 16, mouse 18, text 7, pad 33, both 14 checks, all
  PASS twice, 0 Critical; `verifyclasses` passes; audio self-test still 39/39. One check failed on the first
  re-run because a bot killed the test player: the scenarios now make the player invulnerable (`God`) after
  the spawn. The pad is SDL's virtual controller: no physical pad has been tried (owner). Script-side list
  for M14 in `analysis/controls.md` (mouse/stick separation in `UCPlayerInput`, aim adhesion, lock-on with a
  mouse, button glyphs, rebinding menu).
- 2026-10-05 — **M7 green (render targets and post-processing), with two leftovers still open.** Merged
  `m7-postprocess`. The Xbox post-process paths run behind one capability (`FRenderCaps::XboxPostProcess`,
  `UsePostProcess=True` in the GL device): distortion (DM-Praxis panes now show the sea, DM-Decay icicles,
  torch shimmer), emissive glow and bloom, always-on depth of field, screen and radial blur, colour remaps
  (berserk, pause, Liandri), dynamic gamma. Coordinator's re-run on merged main, `tools/dev/m7-effects.sh
  coord`: numeric self-test of the primitives 10 passed, 0 failed (snapshot orientation, depth copy, blur
  taps, back-buffer copy, DOF ramp, remap, luminance); 14 effect pairs with critical=0 glerrors=0
  programs_failed=0; the post-processing-off picture is pixel-identical to M6's in the static part of the
  frame. The Praxis picture depends on a random PlayerStart (about one in four): it missed in 10 tries once,
  hit on the first try the next time. No "on" picture has a reference (no Xbox here, Epic's PC build never
  had these effects). NOT done: the 12 projector shaders (about 560 lines of Xbox batch renderer to port),
  `PS_Terrain3Layer`, shadow blur. Owner decisions pending (BLOCKERS.md): depth-of-field alpha 100 vs the
  shipped 65, dynamic gamma on by default, distortion scaled by height/480, the pause overlay's teal negative.
  No GPU run yet: under software GL the effects cost 1.4x-3.8x frame time.
- 2026-10-05 — **M12 green (Windows, MinGW-w64 i686, under wine).** Merged `m12-windows`. `uc2.exe` and
  `ucc.exe` link statically (no MinGW runtime DLLs, SDL2 linked in; `OpenAL32.dll` built from the fetched
  source and loaded at run time); MinGW runs through the GCC Core layer with `_WIN32` branches; MS bitfield
  layout kept (verifyclasses passes with either). Coordinator's run of `tools/dev/m12-wine-smoke.sh` on merged
  main (with M7 post-processing, M8 audio, M10 controls): verify, server, netjoin (handshake only), null, gl
  (0 GL errors, wine gives GL 4.3 on llvmpipe), audiocheck, selftest 39/0, capture (menu music -19.02 dB),
  nosound, keyboard 16, pad 33, leftovers: PASS. The wine-vs-Linux picture comparison first failed on main:
  post-processing spreads moving things over the frame and the animated aiming reticle was not masked; the
  check now runs with `-nopostprocess` and masks the reticle: static part 100.000% within 2 levels, same
  position and rotation. wine is not Windows: the real-Windows checklist is in `analysis/windows.md`.
  64-bit Windows waits for M13. Found on the way: a client joining any server stops at
  `check(GObjBeginLoadCount)` after the handshake (all platforms) - being fixed on branch `net-join`.
- 2026-10-05 — **M9 green (video).** Merged `m9-video`. `tools/cook-movies.py`: 110 XMV (1052 MB) to Ogg
  Theora + per-language Vorbis (654 MB), frame and sample accounting exact for 110/110; vendored libtheora
  1.2.0 (decoder); `UC2Movie` player behind Epic's movie interface, sound through the audio device's stream
  with the video on the audio clock; startup logos (skippable, `-nostartupmovies`), MovieMaterial textures in
  the GL driver, intro cinematics on travel, a static loading screen. Coordinator's re-run on combined main
  (M7+M8+M9+M10+M12): all four targets build (native, windows, wasm, wasm-perf); `verifyclasses` passes;
  `movie-selftest.sh` 96 checks, 0 failed (pictures identical to ffmpeg's decode, sound within one step, loop
  wrap, A/V sync error 0); `m9-movies.py` 19 PASS (logos match their cooked frames at 82/66 dB PSNR, cinematic
  sound correlation 0.989 at 0.00 dB); audio suite 39/0, menu music -19.01 dB; renderer self-test 10/10;
  wine smoke 13/13; browser boot 1/1 (menu up 9.1 s, browser PSS peak 1.47 GB, machine peak 9.9 GB);
  controls 5/5 after two test fixes (the scenarios now pass `-nostartupmovies`: the logos own the input;
  the mouse click is held 1 s: a click shorter than a software-GL frame can be lost, `analysis/controls.md`).
  Not exercised: outro cinematics, MovieMaterial with sound, non-English tracks in game (tracks 1-4 are mapped
  from the Xbox code only). Off behind hooks: the animated loading movie (no thread; static screen instead),
  attract mode, legal/rating screens. Runtime XMV decoding is M16; web playback M11.
