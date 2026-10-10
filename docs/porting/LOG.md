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
- 2026-10-05 — **Network play merged (`net-join`), one defect open.** A client joins a dedicated or listen
  server and plays. Causes fixed, both Epic's: the script patcher appended imports outside
  BeginLoad/EndLoad (`check(GObjBeginLoadCount)` in a network client; the Xbox release builds compile checks
  out), and the package map counted the patcher's appended names/exports, which differ between client and
  server (both ends need this fix). Also three use-after-frees in connection/channel/commandlet teardown
  (ASan) and one buffer overrun of ours in `UnUnix.cpp`. Coordinator's run on merged main,
  `NET_SOAK_MIN=10 tools/dev/net-test.sh`: dedicated1 8/8, dedicated2 9/9, listen 8/8, travel 6/6,
  disconnect 6/6, glshot 3/3, soak 4/4 (10 min, RSS flat), leftovers: PASS = 46. **`lag` (PktLag=50,
  PktLoss=5): 0/8 in this run** - one client's join hung for the whole run: the server's CHALLENGE was
  dropped by the loss simulation and never retransmitted while the connection stayed open (logs kept in
  `<WorkUC2>/logs/coord/net-lag-hang/`). The agent had passed `lag` twice, so it is intermittent; a lost
  handshake packet must not hang a join (it will matter on a WebRTC data channel). Being diagnosed on branch
  `net-loss`. Never exercised: team games online, package download, more than 3 clients, server browser.
  Owner entry points added the same day: `bin/play-uc2` (native, own display) and the web preview service
  `uc2-preview` on port 8096 (`bin/publish-preview`).
- 2026-10-06 — **Network loss defects fixed (`net-loss`, merged e7e6d23).** Two causes of the hung join (a
  lost handshake message was never resent; the client now gives up a stalled handshake, measured from the last
  server message and after dispatch, with the engine's normal error) and a worse one found on the way: after
  the packet-id wrap, lost property updates were never resent (a client's scores went silently stale minutes
  into a lossy match; `-NETOLDACKS` keeps Epic's ack logic for comparison). Deterministic drop tests
  (`NETDROP=`, `NETPKTID=`, checks `handshake-loss`, `handshake-compat`). Coordinator's run on merged main,
  `NET_SOAK_MIN=10 tools/dev/net-test.sh`: **89 PASS, 0 FAIL** (`logs/coord/a1-net.log`).
- 2026-10-06 — **M13 green (64-bit).** Merged `m13-64bit` (064dd20): `native64` (LP64) plays; every
  pointer-width site fixed at its declaration (`analysis/64bit.md` site table), a 64-bit mode of
  `verifyclasses`, saved profiles byte-identical between 32- and 64-bit builds (the POSIX file manager now
  reports directories as existing, which saved profiles needed), `windows64` builds and passes the wine smoke
  (per the agent), an `arm64` preset (toolchain now installed by the owner; not yet built). Coordinator's run
  of `tools/dev/m13-64bit.sh` on merged main: verify, census 58/58 (same per-map result as 32-bit), controls
  5/5, audio, movies, glselftest 10/10, effects 14/14, pinned 64 vs 32 (static part 99.997% within 2 levels),
  psh2glsl, saves, soak 1200 s (RSS +1.5%), leftovers: **all PASS** (`logs/coord/a1-m13.log`).
- 2026-10-06 — **Web sound and movies, native output hardening (`m11-audio`, merged 14494a0).** UC2ALAudio on
  openal-soft's Web Audio backend in the browser, the movie player active on the web, explicit backend order
  natively (pipewire, pulse, alsa, oss; the chosen one logged), movie textures without a picture show their
  fallback or black instead of the engine's green default texture (the owner's "videos play green"), a zone
  survey (`UC2ALAudio.UC2ZoneSurvey`: DM-Azure's 18 zones are all Default; the Xbox runs one fixed Default
  reverb fed by per-sound send levels, which our device does too). Agent's evidence: audio suite 39/0,
  movie self-test 99/0, m9-movies 19/0, the wasm32 code under Node 39/0 and 99/0, browser spec
  `uc2-wasm-audio.spec.js` 3/3 twice (Web Audio device, menu music, output not silent by an analyser, logos with
  sound and skippable). Asset tree v1 (8cbea58) carries the cooked folders; frontend manifest regenerated
  (ufrontend 05862c4, 1,223 files). Coordinator re-verification on main after the renderer merge.
- 2026-10-07 — **C-1 and C-2 (web ini overrides, what persists) merged (`web-ini-saves`, 3a5a532).**
  `appApplyIniOverrides` ported from engine-ut2004 (`-ini:<File>:[<Section>]:<Key>=<Value>`, called once in
  `appInit`; a malformed token is logged and ignored; values are written back to the ini as in the template),
  `FLUSHCONFIG`, and a shared-code bug fixed: `UObject::RemoveNames` dropped the config cache without writing
  it on every garbage purge, losing `-ini:` values and `SaveURLConfig`'s `[DefaultPlayer]` (possibly part of
  the owner's "saving doesn't work"). Native saves work: profiles at `System/Profiles/<name>/AVARIS_PROFILE.PRO`
  survive a restart (since M13's directory fix); a player can still think saving is broken because the game
  starts on a transient "No Profile" and does not auto-select the saved one (D-3). The web build writes the
  same profile flat, `System/Profiles/<name>.PRO` (`__EMSCRIPTEN__` only, ULoadSaveManager.cpp), because the
  frontend's `userFiles` globs allow a wildcard only in the file name (coordinator's decision; alternative:
  teach the frontend directory wildcards and revert 22e9dbf). Frontend (ufrontend f90b70f): `network.playerName`
  (User.ini [DefaultPlayer] Name), `userFiles: System/Profiles/*.PRO`, player files excluded from the manifest.
  Coordinator's re-run on main: `tools/dev/c1-ini-overrides.sh` ALL PASS, `tools/dev/c1-saves.sh` ALL PASS
  (profile listed and loaded after a restart, bytes unchanged), `m10-controls.sh keyboard text` PASS; frontend
  `yarn test` 28/28. Not verified: the browser's own file layer (WasmFS mkdir under /System, the persistence
  daemon's scan, flushconfig round trip): C-3's browser e2e must create a profile, reload, and find it.
  D-4 facts (where the player name comes from and where the character name overwrites it without a profile)
  are in `analysis/web-ini-saves.md`.
- 2026-10-07 — **O-5 and O-7 merged (`fx-fixes`, d3e88e7); O-6 not reproduced.** O-5, the DM-Azure pool
  drawn as flat grey: its reflection cube map (`T_Liquid_Decay_Water_C_CM`) has only five faces; GL treats such
  a cube as incomplete and fixed-function GL switches the stage off, so the pool was its vertex colour at
  almost zero alpha. D3D allocates all six faces and fills the ones that exist: the driver now does the same
  (the missing -Z face is zero; assumption for the Xbox, never seen from above). Water now shows golden
  reflections, on Mesa and on the NVIDIA GPU (`logs/fx/o5-pool-gpu-before-after.png`); matches the owner's
  xemu shot in kind, not pixel-close. O-7 (tuning): the knob is Epic's `DISTORTION_CULL_DISTANCE = 5000`
  (`D3DMaterialState.cpp:1377`): `DistortionAccum.vsh` scales the distortion by `1 - saturate(z/5000)^2`;
  now `[UC2GLDrv.UC2GLRenderDevice] DistortionCullDistance` (5000 when absent = Xbox; Default.ini 15000) and the
  console `DISTORTIONCULLDISTANCE`. O-6, the DM-Azure waterfall in first person: the waterfall's distortion is
  drawn every frame on Mesa and NVIDIA, standing still or moving, as the only distortion item; the owner's
  exact spot is needed. Agent's regressions: m7-effects 10/10 + 14 lines clean, psh2glsl self/gpu tests,
  verifyclasses, pinned DM-Deadbolt unchanged. Found on the way: O-9 (NVIDIA: 4 fixed-function texture units, no
  combine3). New tools: `GLPIXTRACE`, `tools/dev/fx-owner.sh` (UC2_GPU=1 runs headless on the NVIDIA GPU).
- 2026-10-07 — **M11 renderer merged (`m11-webgl`, f17d095): the game draws in the browser.** One driver
  source with a GL4ES binding chosen at run time (WebGL 1), verified first on a native GLES 2 twin
  (`tools/dev/m11-twin.sh`): 420 complete programs desktop vs twin within 1/255, self-test 11/11 on both
  bindings, effect pairs against the desktop references. Defaults (owner, O-8): anisotropy 16x; AA 4x =
  multisampled main framebuffer on desktop, FXAA in the present pass on WebGL 1 (no multisampled FBOs there).
  Fallbacks implemented: no S3TC (driver DXT decoder; would have crashed on most phones before), texture max
  level, gl_FragDepth, 32-bit indices; NOT implemented: no depth textures (old mobile GPUs lose depth of field
  and distortion occlusion). Windows build fixed (the host's PipeWire headers leaked into MinGW: openal-soft's
  Linux backends are off for WIN32). Coordinator's verification of the combined main (all branches but M14,
  1538155, `logs/coord/a2/summary.txt`): verifyclasses; audio suite; movie self-test 99/0; m9-movies 19/0;
  controls 4/5 (pad: test walked off a ledge); m11-twin 31 PASS, 3 FAIL (the depth-texture fallback twice, one
  timing-sensitive movie picture); c1-saves ALL PASS; browser `uc2-wasm-boot.spec.js` and
  `uc2-wasm-audio.spec.js` PASS (browser PSS peak 1.8 GB, sound routed to a null sink). **Published to the
  owner's preview (port 8096) 2026-10-07 09:35: renderer, sound, movies, effects, defaults together.**
- 2026-10-07 — **M14 green (scripts).** Merged `m14-scripts` (24c80dc): `ucc-bin make` builds all 17 script
  packages from `Source/*/Classes` on Linux; `tools/dev/script-build.sh` + `script-diff.py` prove them against
  the shipped `.u`: 0 unexplained differences (9,621 of 10,456 functions byte-identical, 813 differ only in line
  positions in the 17 classes changed after retail); 29 of Epic's 30 load-time patches are already in the
  source; the patcher now skips rebuilt packages and logs it. Findings for the switch to rebuilt scripts
  (asset tree and web still use the shipped .u): Epic's `CTFFlag.home` patch is defective (a returning flag
  never re-checks touching actors; the source does it); `GUIController.bUnlockCharacters`/`bShowAllMuts` are
  `config` in the shipped GUI.u but not in the source (the community unlock tweak would stop working);
  `HTTPDownload`/`Prefab` are registered under the wrong package (predates M14). Coordinator's re-run on main
  after the merge (`logs/coord/a3/verify.txt`): verifyclasses, renderer self-test 11/11, and
  `tools/dev/m14-scripts-check.sh`: verify, menu, deadbolt, patcher, audio, effects, census 58/58 on rebuilt
  packages, net 16/0 PASS; controls and movies had one timing-sensitive failure each, fixed in the tests
  (half-stick check walks back over known ground; the fire click retries during spawn-in; the cinematic-skip
  check proves rendering after the skip with a timed picture): mouse+pad 3/3 runs PASS, cinematic-skip 3/3.
- 2026-10-07 — **D-2 green (UI scaling), natively, no script changed.** Merged `d2-ui-scale` (556a69f):
  `UCanvas::Update()` → `ApplyUIBox()` hands menus and HUD the 640x480 design space and draws it into the
  largest centred 4:3 box (`[SDL2Drv.SDL2Client] UIScaleMode=Fit43` default, `Native640` = before, console
  `UISCALE`); full-screen layers stretch to the window; WorldToScreen/ScreenToWorld and the GUI mouse work in
  design pixels; sharper text from taller sizes of the same fonts (`UISharpText`). Agent's
  `tools/dev/d2-ui-scale.sh`: 81 PASS (8 sizes incl. 5:4, 16:10 and portrait: UI box exact, 640x480
  pixel-identical to main, mouse 18/18 at 640x480 and 1920x1080, a browser run at 1920x1080). Known: the
  crosshair art sits off its anchor (-4 px at 4K, -1 px at 640x480 too); the world looks zoomed vertically at
  16:9 until D-8 (AutoFOV). Coordinator's run on main (`logs/coord/a4/summary.txt`): native, windows, wasm-perf
  build; verifyclasses; controls 5/5; renderer self-test 11/11 and effect pairs; m9 logos+cinematic 12/0;
  browser `uc2-wasm-boot.spec.js` 3/3 (play test now maps 640x480 positions into the box; ufrontend commit)
  and `uc2-wasm-audio.spec.js` 3/3. Republished to the owner's preview.
- 2026-10-07 — **D-7 and D-8 green (console, AutoFOV), natively, no script changed.** Merged
  `d78-console-fov` (00634fc). D-7: the console was never locked; the shipped `ConsoleKey=9` (Tab) shadowed
  the scoreboard and the menus' Tab=Start. Now the key left of 1 (`ConsoleKey=192`, a saved Tab is migrated
  at start-up); Enter runs, Escape closes, Up/Down history; replies go to the log (Epic's console has no output
  area); fixed on the way: Escape on an empty console line also closed the menu page underneath. D-8: Hor+
  AutoFOV ported from engine-ut2004 (`[ufront.AutoFOV] AutoFOV=2`, console `AUTOFOV`): the designed vertical
  FOV kept at any aspect (106.26 deg at 16:9, 121.28 at 21:9), weapon, third person, zoom/sniper, letterbox and
  WorldToScreen markers included; 4:3 unchanged. Agent: `tools/dev/d78.sh` 16/16, browser spec PASS.
  Coordinator's run on main (`logs/coord/a5/summary.txt`): native/windows/wasm-perf build; verifyclasses;
  controls 5/5 (keyboard failed once under load, then 2/2 PASS); renderer self-test 11/11; `d78.sh` 14/16: the
  two failures are measurement noise, not AutoFOV (the AutoFOV-off CONTROL's vertical scale at 21:9 1.706 for
  1.778 +-0.06; the third-person 4:3-vs-centre-crop match 96.6% for >=98.5%, 99.0% in the agent's run; every
  AutoFOV value measured exactly 1.000).
- 2026-10-07 — **O-9 merged (NVIDIA fixed-function gap), f2d7f25.** The compatibility profile on the owner's
  Quadro P620 has 4 texture units and no combine3: Epic's compiler then substituted GeForce1-era ops (the
  orange-metal pillar of DM-Azure) and our GL translation dropped the added term of three-operand ops (327
  materials across the maps). Fix: each pass's texture stages become a generated GLSL fragment program (D3D8
  semantics on every vendor; `FixedFunctionPrograms=True`), `GL_NV_texture_env_combine4` for the comparison
  path, and Epic's 4 stages (`MaterialStages=4`; UC2's D3DDrv accepts only 4). Coordinator's run on main
  (`logs/coord/a6/summary.txt`): builds; verifyclasses; `tools/dev/o9-nvidia.sh` combiner tests 35/0 on Mesa and
  NVIDIA, 33+2 skipped on the twin, the "before" defect reproduced, other-maps pictures NVIDIA vs Mesa 0.6-0.8;
  effect pairs clean; controls 5/5. Open: with post-processing OFF the DM-Praxis panes no longer match the
  original PC binary's flat look (also with `-glnoffprograms`), so the M7 praxis reference check fails; the
  O-9 agent investigates on `o9b` and makes that check deterministic. Reference-picture runs now use the pre-D-2
  UI and AutoFOV off (run-uc2-shot.sh, fx-owner.sh).
- 2026-10-07 — **D-3, D-4, D-5 green (profiles, names, text entry); first rebuilt script package in the asset
  tree.** Merged `d3-profiles` (2b2c972). The game starts in the last profile (`User.ini [Profiles]
  LastProfile`, `UC2Profiles.cpp`, a per-tick watch, no script change); deleting a profile works on Linux
  (`FFileManagerGeneric::DeleteFiles` only knew `\`); the profile owns the player name and without one the
  launcher's name survives the character page (`GUIController.SetProfilePlayerName`, script: the only changed
  function; `bUnlockCharacters`/`bShowAllMuts` config again); every keyboard page takes a real keyboard (Enter,
  Escape, Backspace clears a preselection, the opening letter no longer typed). Frontend (data-driven, every
  target): `network.playerName.ownedBy` disables the name field while a profile owns it. **Asset tree v2:**
  `tools/dev/mkassets-uc2.sh` overlays `assets/uc2-scripts/System/GUI.u` (rebuilt from main: 17 packages compare
  clean) and keeps the shipped file in `assets/uc2-shipped/System`; `System/SCRIPTS.txt` says so; frontend
  manifest regenerated (ufrontend 477eca5; dependencies unchanged; GUI is not a server package, Core.u
  unchanged). Coordinator's run on main (`logs/coord/a7/summary.txt`): windows/wasm-perf build; verifyclasses;
  d3-profiles, d4-names, d5-text, c1-ini-overrides ALL PASS; c1-saves ALL PASS (after rebuilding its tree);
  controls 5/5; browser boot 3/3, audio 3/3, saves 1/1 (profile restored after closing the tab), engine proven
  by the wasm hash. Published to the owner's preview.
- 2026-10-07 — **O-9b: the DM-Praxis signal was a test artefact, the coordinator's misreading.** The panes are
  drawn exactly as before O-9: the "swirled" probe was a close view, the oracle a distant one (the same
  fallback texture minified to periwinkle), and the 10 misses were the random spawn. The oracle's view is
  PlayerStart0 (-1220,288,680 yaw 5220); `tools/dev/m7-effects.sh` now places the camera there with NETPLACE
  (`LocalPlayer`) and runs once: A vs wine reference 2.58 (Mesa), 2.65-2.72 on NVIDIA (pre-O-9 NVIDIA 18.2).
  Agent's gate: m7-effects 11/0 + 14 lines clean, praxis first try; o9-nvidia.sh 12/0 with the AutoFOV-off/Native640
  defaults; m11-twin effects 13/14 (liandri overlay noise differs between any two runs: needs fixed random
  numbers or a mask, not done). Merged `o9b`.
- 2026-10-08 — **D-6 green (mouse and pad separated); UCGameplay.u rebuilt in the asset tree.** Merged `d6-mouse`
  (d24a82e). Measured cause of the owner's complaint: the web build had carried Epic's PC DefUser.ini (1.23
  deg/count, capped per tick, circles 16.6% out of round: the "hexagonal" feel); native was linear but ~5x too
  sensitive, with the third-person lazy camera sliding and the pad sliders acting on the mouse. Now
  (`UCPlayerInput.AddMouseLook`, script): mouse look linear at any speed and in both axes (circle within 0.29%),
  0.022 deg x `MouseLookSensitivity` 2.5 x FOV/90 per count, separate `bInvertMouseLook`, no drift after stopping,
  aim adhesion and auto-centre only under the pad, mouse-friendly lock-on (break by 25 deg); the pad back on
  Epic's Xbox curve (within 0.47% of the shipped scripts). `UC2InputMigrate.cpp` rewrites old User.ini values.
  Binding audit: nothing inverted; dead wheel commands fixed; Space while moving dodges. Asset tree:
  `UCGameplay.u` rebuilt (a server package: all peers run it), `GUI.u` as before; `tools/dev/script-build.sh`
  now conforms and compares against epic-sources' shipped packages (it had used the asset tree's, which hold
  rebuilt files: false problems in GUI/UCMenu). Coordinator's runs on main (`logs/coord/a8/`): windows/wasm-perf
  build; verifyclasses; d6-mouse 67/0; d3-profiles, d5-text PASS; net dedicated2/listen/travel 24/0; browser boot
  4/4 (incl. the mouse test), audio 3/3; at load ~61 several menu-timing checks failed, rerun at load ~15:
  d4-names, c1-saves, controls 5/5 PASS; browser saves spec: 2 passes by the D-6 agent at load ~16 (the failing
  run at load ~20 was the spec's Enter-retry overshooting when a Right arrived before the rules list was ready:
  spec robustness to fix). Republished the owner's preview.
- 2026-10-08 — **M7b, script SaveConfig and O-10 merged and verified together; preview republished.**
  M7b (`m7b-projectors`, 1b33254): the Xbox's level-static projector batching with the 12 `PS_Projector_*`
  shaders (`FRenderCaps::XboxProjectors`, `UseXboxProjectors`): looks the same as the PC path except the 19
  projectors with a `MaterialBlendingOp` (CTF-Recoil's grids clearer; 6 maps), fewer draw calls; a driver fix:
  draws inside deferred occlusion queries were all skipped; the Xbox shadow-texture filter
  (`FRenderCaps::XboxShadows`, `UseXboxShadows`): blob shadows lighter in the core (43% vs 21% of the light kept)
  and sharper; `PS_Terrain3Layer` behind `UseTerrain3Layer=False` (no visible difference). No Xbox reference
  for any picture. Script SaveConfig (`saveconfig`, 10c7605): Epic's `#if 0` natives back on behind
  `[Engine.Engine] ScriptSaveConfig=True`; a naive enable would have saved a base class's globalconfig defaults
  (FOV 90 -> 85 after a restart) and ~36 untouched defaults: the engine now writes only changed keys, never a
  live GameInfo; new `SAVECONFIGKEYS`; mouse sensitivity/invert (UCGameplay.u rebuilt again) and weapon hand
  persist; the profile wins over the ini where both hold a value. O-10 (`pp-scale`, e42eafb, seven conflicts
  with M7b resolved by hand): blur, bloom, DOF and emissive targets sized for a 480-line reference picture
  (`PostProcessReferenceHeight=480`), back-buffer taps in reference pixels through an area-averaged reference
  copy, radial-blur insets scaled; glow width 8.54% of the height at 640x480 and 1440x1080, 8.33% at 2880x2160
  (was 4.17% / 3.12%); 4K about 10% faster; fixed Epic's per-frame re-creation of blur targets at sizes like
  1366x768. Asset tree: `UCGameplay.u` rebuilt from main (17c1750c), ufrontend 6893b45. Coordinator's runs on
  main (`logs/coord/a9..a11`): native/windows/wasm-perf build; verifyclasses; scripts 0 problems except
  UCGameplay's intended 18; `m7b.sh` 11/0; `sc-saveconfig.sh` 91/0; `d6-mouse.sh` 69/0; `d3-profiles` PASS;
  controls 5/5; `pp-scale.sh` selftest/glow/dof/radial 6/0; `m7-effects.sh` self-test 15/15 and 14/14 effect lines
  clean; `m11-twin.sh` 31 PASS, 3 known FAIL (depth-texture fallback not implemented x2, menu-movie timing);
  browser boot 4/4, audio 3/3, saves 1/1. Published to the owner's preview.
- 2026-10-08 — **D-9 green (in-game KEYBOARD / MOUSE page); the owner's addendum is complete.** Merged
  `d9-rebind` (e319f56). OPTIONS gains KEYBOARD / MOUSE (PC only, `!Level.IsXBox()`), from the main menu and the
  pause menu, in UC2's own components: mouse sensitivity slider (0.022 deg x value per count), invert, and every PC
  action with two keys; capture any key or mouse button raw, conflicts swap (and say what moved), clear, defaults
  from our DefUser.ini; only changed keys saved (`SET Input` + `SAVECONFIGKEYS Engine.Input`), pad bindings never
  touched. Shared-code fixes: the GUI's raw key capture took the starting Enter's release / auto-repeat as the new
  key; an Epic bug in package saving reordered existing imports when a script build added new ones. Asset tree:
  `UCMenu.u` rebuilt (not a server package) next to GUI.u and UCGameplay.u; ufrontend manifest committed, new
  opt-in spec `uc2-wasm-keys.spec.js`. Coordinator's run on main (`logs/coord/a12/summary.txt`): native/windows/
  wasm-perf build; scripts: only UCGameplay (18), UCMenu (5) intended problems; verifyclasses; d9-rebind 119/0;
  sc-saveconfig 91/0; d6-mouse 69/0; d3-profiles, d5-text PASS; controls 5/5; browser boot 4/4, audio 3/3, saves
  1/1, keys 1/1. Republished to the owner's preview. Owner choices now on that page: C/V order, Space dodge (bind
  Space to JUMP ONLY), conflict swap vs clear.
- 2026-10-08 — **E-2 green (web multiplayer over WebRTC).** Merged `e2-webrtc` (9b63f3c): `Source/WebRTCDrv`
  (web build only) ported from engine-ut2004 and adapted (UC2's standby clock fed per packet, a closed data
  channel closes the connection, listen status for the glue, no per-tick sleep); web inis select
  `WebRTCDrv.WebRTCNetDriver` (ConnectionTimeout 60); `Browse` turns the launcher's `<key>?connect` into a network
  URL; frontend (ufrontend 6f2a302..4ac0d6d): uc2 `network` modes (p2p only), `serverName` from Avaris.ini (the
  profile only overwrites the class default), notices, host/client milestones, `uc2-wasm-net.spec.js`.
  Coordinator's run on main (`logs/coord/a13/summary.txt`): native/windows/wasm-perf build; verifyclasses; native
  net dedicated1/dedicated2/listen/travel 32/0; browser boot 4/4, net 2/2 (host + guest: join, spawn, the guest's
  walk seen on the host, a death replicated, a clean leave; the launcher's host/invite-link flow), audio 3/3; saves
  and keys were stopped by the memory guard (11-14 GB in use from other work) and passed on a rerun (saves 1/1 with a
  harness teardown complaint: a browser profile outlived the test -- spec hygiene, with the Enter-retry overshoot).
  Not done: in-game server discovery (UC2's System Link uses UDP broadcast), two guests, travel over WebRTC.
  Republished the owner's preview.
- 2026-10-08 — **O-11 green (third-person character jitter).** Merged `jitter` (c59c02d). Cause, measured with a new
  per-frame trace (`FRAMETRACE`, `Source/Engine/Src/UC2FrameTrace.cpp`; `-framepattern=` in SDLLaunch for repeatable
  uneven frames; `tools/dev/jit-trace.sh`, `jit-analyze.py`): a frame's view is computed in PlayerMove (for the aim)
  before the pawn's physics, and D-6's rigid mouse camera then drew the pawn its own move of that frame ahead of its
  place (fit: ahead = 1.000 x v*dt), a different amount every frame when frame times are uneven (the browser); Epic's
  lazy camera (the pad) hides it. Fix, UCGameplay.u only: `PlayerCalcView` computes the view again when this frame's
  camera was the mouse's and the pawn has moved (`UCPlayerController.uc` MouseLookCamera/MouseCameraLeftBehind,
  `UCPlayerInput.uc` MouseCameraTime). Coordinator's run on main (`logs/coord/a14`): native/windows/wasm-perf build;
  scripts 0 errors; UCGameplay.u switched (9ca783e7; SCRIPTS.txt; ufrontend 21b49eb); verifyclasses; jit-trace kbm
  uneven 0.53 px (before 10.5-20.7), 60 fps 0.26, uncapped 0.01 (limit 1.0); pad 1.87 px worst segment, identical
  before the fix (jit-main 17c1750c: 1.87; the lazy camera on the test spot's ramp); m10 5/5; d6-mouse 67/2 (the
  spawn-dependent lock check, and the pad walk check at a spawn by a wall: rerun twice 21/0, walk -353/+408); browser
  boot 4/4, jitter 1/1 (judder 0.50-0.70 px), keys 1/1, saves 1/1. What remains on screen is Epic's walk bob (about
  +-8 px at 1080p, smooth). Preview republished 13:29.
- 2026-10-09 — **Owner review of 2026-10-09: F-4, F-5, F-6, F-1 green and merged** (PLAN.md's new section has the
  decisions). Coordinator's checks on main: `logs/coord/a15` (F-4, 2a58dbc), `a17` (F-5, b6fcffb), `a18` + followup
  (F-1 + F-6, 9764bc8), `a16` (quiet WebGL 1 vs 2 browser frame times for F-3).
  - **F-2/F-4 platform like UT2004** (2a58dbc): `[Engine.GameEngine] FpsLogSeconds` / `FPSLOG` and `tools/dev/fps-bench.sh`;
    the frame rate is uncapped natively (`FrameRateCap=0`; DM-Praxis turn 63-70 fps on the Quadro at any resolution,
    132 at the spawn, 1336 on the null renderer; post-processing ~5 ms); web exceptions OFF with engine-ut2004's
    fail-soft loaders (wasm -8%; 58/58 maps under Node, 0 aborts; the "TransBuffer throws" story was wrong: it never
    threw); frame yield as engine-ut2004 (one yield per frame, hidden tab mutes); first arm64 build ever (verifyclasses
    under qemu-aarch64: 401 classes, 0 mismatches) and the 21-item hazard checklist; inis LF; docs; meta.json records
    the fetched libraries; `-maxfps`->`FrameRateCap`, `-framepattern`->`[SDLLaunch] FramePattern`, `-audiowave`/`-NOEFX`
    -> `WaveFile=`/`UseEFX=`. a15: builds, verifyclasses, jit 0.52 px, d6 41/4 (the 4 were a stale shipped tree:
    rerun 9/0, pad curves 0.04-0.27% of Epic's), m10 5/5, fps-bench 66.9 fps, audio regression, Node census 58/58,
    boot 4/4, audio 3/3; jitter spec failed under load only.
  - **F-5 renderer** (b6fcffb): `DOFShippedBlurAlpha` compiled default True (value 65 unchanged); `DistortionCullDistance`
    a per-device config key; dynamic gamma stays on (Epic's split-screen manager forces it too); occlusion queries:
    Xbox-only actor culling, nothing visible depends on it, UT2004 has none, not implemented; shaders: every one a
    material or effect can select is ported (16 vertex fragments left out with reasons); CTF-Aqueduct flicker NOT
    reproduced in nine conditions (`bProjectOnBackfaces` on 12 of 16 tree projectors is the map's design; waiting for
    the owner's conditions); shadow strength: xemu shot requested (`logs/f5/shadow`); **DM-Azure sky haze found and
    fixed**: Mesa's fixed-function fog value, not GL4ES; the generated FF programs compute the fog distance from depth
    (sky difference vs the twin 46.75 -> 2.49). a17: builds, verifyclasses, sync-web-ini, m7-effects 14/14 +
    self-test 15/15, m7b 11/0, fx-owner GPU scenes clean, boot 4/4.
  - **F-6 audio** (812361e + 54a509b): `tools/uc2xwb.py` reads the shipped Xbox wave banks (ADPCM layout settled against
    643 source WAVs); the 19 missing waves + `Machine_Generator03` decoded from the `.xwb` (cues 2465 fully / 0 partly
    / 4 none, was 2417/6/15); A_Vehicle cooked (31 cues, cut content); priority 255 for sounds without one (674 lines);
    cue tables inside the Ogg packs: recommended NO (banks do not line up, start-up would read 37x more; spike kept);
    `[UC2ALAudio.UC2ALAudioSubsystem] UseHRTF` as engine-ut2004 (interaural delay measured). Cook installed into
    assets/uc2-cooked (85 files) and the asset tree; ufrontend manifest 6e77b28. a18: audio-check 7191/7191,
    self-test 45/45, deadbolt capture clean, web audio spec 3/3.
  - **F-1 vanilla scripts** (9764bc8, 01139e3): Epic's shipped GUI.u/UCGameplay.u/UCMenu.u play as Epic shipped them
    with the curated inis (Epic's PC mouse path; pad curve within 0.17-0.43% of Epic's; the left stick walks on PC
    through `UnIn.cpp:324`, non-Xbox builds only, since Epic's script scales it only under `IsXBox()`); the rebuilt
    UCGameplay.u adds the linear PC mouse on top from the SAME ini; `UC2InputMigrate` removed; `bLazyMouseCamera`
    (Epic's lazy camera under the mouse, default off); arrows strafe (Epic's PC layout), `bKeysAsStick` and the 25
    degree lock break hard-wired; D-9 conflicts clear; SDL relative-mouse hints as engine-ut2004 (N counts in = N out
    at two speeds); UC2 has NO walk mode (owner: leave it); cheats `WHEREAMI` / `TELEPORT x y z [yaw [pitch]]` in
    `Source/Engine/Src/UC2Cheats.cpp`, allowed for a standalone game or a listen host, refused and logged elsewhere
    (`[Engine.GameEngine] AllowNetCheats` for the tests only, never shipped), NETPLACE/NETPLACENEAR/NETPROBE behind the
    same guard (a client's NETPROBE answers about itself only), NETAIM deleted (owner: test-specific commands do not
    ship); `docs/CHEAT-COMMANDS.md` for the frontend. Packages switched: UCGameplay.u 4ed72f0b, UCMenu.u b9202a59
    (ufrontend 227e5c3). a18 + followup: builds, scripts 0 errors, verifyclasses, jit 0.52 px, d6 89/2 then 16/0 and
    15/1 (the vanilla arrow-strafe check is spawn-dependent: a wall; to pin), m10 rebuilt 5/5 and vanilla 2/2,
    d9 119/0 on a fresh tree, d4 PASS, net listen 9/9, boot 4/4, keys 1/1, audio 3/3, saves 1/1; the browser net
    spec 2/2 on the f1b branch. Preview republished 13:55 with the 9764bc8 build (the cheat-guard build follows with
    the next publish).
  - F-3 (GL4ES on WebGL 2) is verified on its branch and the 7 fork commits are pushed (10-p/gl4es
    fix/cubemap-bind-active-unit 6b6bccd4); it merges after the F-4b switch conversion.
- 2026-10-09 — **F-4b and F-3 green and merged; preview republished 17:07 (abb4cc8).**
  - **F-4b command-line audit** (ab007a3): every test switch we added is now an ini key read ONCE through
    `appReadTestKey`/`appReadTestFlag` (`Core/Inc/UnFile.h:319`) and removed from the ini so it is never saved:
    `[SDL2Drv.SDL2Client] InputTestFile`, UC2GLDrv `ShotTimes/ShotRamp/ShotPresented/Exec/PixelShaderToggleTimes/
    PostProcessToggleTimes/SelfTest/CombinerTest/MatCensus/Stats/NoLODBias/MaskCaps/DualDiffuse`, `[UC2Movie] ShotTimes`
    + `StartupMovies`, `[UC2NetTest] CommandFile/Drop/PacketIdStart/OldAcks`; the on/off twins and unused probes are
    deleted (kept: `-log`, `-ini:`, `-windowed`, `-defaultres`, `-usehomedir`, the three renderer selectors, commandlet
    parameters); `tools/dev/glini` writes GL keys for both device sections; two bugs fixed on the way (the command
    line was cut at 1,024 characters, silently in SDLLaunch and unchecked in UCC: now 4,096 with a warning;
    o9-nvidia reused a tree's Avaris.ini between runs); Epic's three reference inis byte-identical again
    (sync-web-ini --check compares them); `FrameRateCap` default 0; meta.json branch only; `-sOFFSCREEN_FRAMEBUFFER`
    kept (UC2GL4ESDrv asks for `renderViaOffscreenBackBuffer`, the 2.43 present fix). Frontend 1fdc55d: the uc2 specs
    pass `-ini:` keys; the "GL4ES without post-processing" launcher option removed.
  - **F-3 GL4ES on WebGL 2** (c4aee1b, ff abb4cc8; fork 10-p/gl4es fix/cubemap-bind-active-unit 98441ed -> 6b6bccd4,
    7 commits: ES 3.0 back end `LIBGL_ES=3`, GLSL ES 3.00 output, `LIBGL_FORCEHIGHP`, LOD bias applied, real
    multisample + blit, two fixes): `[UC2GL4ESDrv.UC2GL4ESRenderDevice] GLESVersion=3` (ES 2 / WebGL 1 fallback kept:
    owner), core depth textures (the WEBGL_depth_texture gap closes on WebGL 2), 4x MSAA on ES3/WebGL 2, FXAA removed,
    highp everywhere, the twin's LOD bias matches the desktop (pinned 0.384 with / 0.871 without), 33 raw GLES
    call sites gone on ES3. Quiet browser A/B (`logs/coord/a16`): WebGL 1 + FXAA 27.5-35.4 ms vs WebGL 2 + MSAA
    29.8-36.3 ms per frame at 1024x768 (owner: negligible on a slow GPU).
  - Coordinator's full check `logs/coord/a19` (+followup): builds with the fork fetched from GitHub (6b6bccd4),
    verifyclasses, sync-web-ini, m11-twin 33/34 (menu-movie timing), m7-effects 14/14 + self-test 15/15, m7b 11/11 on
    rerun, fx-owner GPU 4 clean, jit 0.52 px, d6 87/4 then 52/1 and 53/0 (the arrow-strafe probe depends on the random
    spawn: to pin), m10 rebuilt 5/5 and vanilla 2/2 on rerun, d9 119/0, audio 7191/7191 + 45/45, net cheats 9/9 +
    listen, Node census 58/58, browser boot 4/4, net 2/2, keys, audio 3/3, saves; the jitter spec's strafe-right slope
    0.217-0.247 against a 0.2 limit on WebGL 2 (forward/back 0.000; native 0.000 everywhere): OPEN, needs a quiet look.
- 2026-10-06 (logged 2026-10-10, D-3) — **O-3 decided by the owner: the depth of field keeps the shipped blur alpha
  (65).** The always-on depth of field (`UUCScreenDOF`, every map beyond z = 0.98) blends its blur with alpha 65 in
  both camera modes, as the shipped Xbox binary did: Epic's stale `UCCameraEffects.h` made that binary read
  `FirstPersonAlpha`'s byte (65) where the script meant `AbsoluteBlurAlpha` (100 in third person); our header is
  fixed (`44f81c6`), so the shipped value is chosen explicitly by `FRenderCaps::ShippedDOFBlurAlpha` from the
  device key `DOFShippedBlurAlpha` (`UCCameraEffects.cpp` `UUCScreenDOF::PostRender`; console `DOFSHIPPEDALPHA 0|1`).
  The picture pair the owner asked for is `fx-dof-alpha` (DM-Azure, `tools/dev/m7-effects.sh`, postprocess.md P5b:
  the far region less softened with 65; mean difference 1.4/255 there, 4.2 between DOF off and on). The owner's
  review of 2026-10-06 (`answers.txt` asked why 100 vs 65; the decision is PLAN.md O-3 and BLOCKERS.md "Decided by the
  owner"): "default `DOFShippedBlurAlpha` to the shipped value (65) so the look matches the Xbox binary";
  commit `2208566` set `DOFShippedBlurAlpha=True` in `Default.ini` (README row 18); F-5 (`b6fcffb`, 2026-10-09)
  made the compiled default True as well, so an ini without the key gets 65 too; the owner's review of 2026-10-09
  confirmed it (PLAN.md: "DOF alpha stays 65 (it is an alpha, not a radius; no A/B)"). Off switch: `DOFShippedBlurAlpha=False` (the script's
  100 in third person). Never compared with an Xbox picture.
- 2026-10-10 — **The parked items done (owner: "work on them now"), the task audit, M16 audio.** Coordinator's checks
  on main `logs/coord/a20` (full, after G-4..G-8, D-1..D-4, M16 audio) and `a21` (+followup; G-9, G-10, G-10b).
  Repository on GitHub since today: `10-p/engine-uc2` (private; no CI).
  - **G-4 language** (7e7fc5b; ufrontend 7b540c8): a Language control in Advanced for every target from target data
    (uc2 int/deu/fra/ita/spa; ut2004 int/det/frt/itt/est; the three UT99/Unreal 1 targets ship English only: no
    control); per game (THE RULE: one home, the ini); browser spec: French seeded, `Joueur`, back to `int`, no seed
    when unchanged; UC2 natively in all five languages (menu caption, loading screen, cinematic track, announcer bank).
  - **G-5 gamepad on the web** (9ce41dd; ufrontend 1d9beff): Emscripten's SDL2 port delivers the browser's pads to the
    engine thread by itself; the launcher gets a generic `gamepad` target block, a "Game pad" switch, a "press any
    button" hint and connect/disconnect notices (UC2 only declares it; written for any target); `UseJoystick=True` in
    the web ini; the spec (fake gamepad) drives menus, spawns, fires, walks, turns, hot-plugs: 26/26; triggers are
    digital on the web (SDL's mapping); rumble: see G-10.
  - **G-6 split screen on PC** (b4debbd): Start on an unbound pad joins players 2-4 (UC2's menus allow two; 3-4 join
    in a match, `SplitScreenJoinInMatch`), Epic's layouts in one window, a pad per player, keyboard+mouse player 1,
    HUD/UI box, post-processing, dynamic gamma and AutoFOV per viewport, audio once; shipped scripts unchanged;
    tools/dev/g6-splitscreen.sh input 35-39 checks, render 59/59 (g6-measure), menu 9/9, audio cues once; no cost
    when not split (fps-bench); single-player audio unchanged (cue tables identical without bots).
  - **G-9** (18f2421): FOV A/B sheets for the owner (`logs/g9/fov-{2,3,4}players.png`; `[ufront.AutoFOV]
    SplitClampToFullScreen` proposal, off); full detail in split screen (`SplitScreenFullDetail=True`; two Epic bugs
    fixed on the way: per-view corona lists, a stale projector cache crashing with four players); keyboard+mouse a
    player of their own (`KeyboardIsPlayer=True` natively; pads take ports 1-3; the web keeps the pad as player one:
    `False` there, 495cbe6); kbplayer 34/34, old input phase 39/39.
  - **G-7 profiles** (d846e75; ufrontend e845bec): the frontend's userFiles accept one directory wildcard, the web
    build uses Epic's `Profiles/<name>/AVARIS_PROFILE.PRO` again (22e9dbf reverted; a preview player's flat file is
    moved once); the active profile's character on direct launches and network joins (`UC2ProfileInURL` from
    `MatchViewportsToActors` and `Browse`); d3-character ALL PASS with a negative control; profile-login 8/8; saves
    spec and net spec in the browser.
  - **G-8 double-tap dodge** (9357aec; ufrontend d0151bf): `[UCGameplay.UCPlayerInput] bDoubleTapDodge` (off) in the
    rebuilt package, keys and stick, wall dodge, Lock rules (Lock held: taps pick targets; locked on: a double tap
    dodges and keeps the target); the frontend's unit tests read the target's own defaults (5 targets green).
  - **G-1/2/3** (ufrontend 5433972): uc2 `audioHrtf` switch (HRTF ACTIVE in the engine log); the precache list from a
    capture (all of System + the 118 files the menu loads, 480 files / 408 MiB, 20% of the cache budget): the
    engine's own start Start->menu 14.6 -> 7.8 s, cue tables 1.0-1.3 -> 0.36 s; media stored uncompressed by default
    (ogg/oga/ogv/mp4/jpg/png/gif/avi: under 10% gain), `--compress-all`, the manifest marks them; publish 43% faster
    for 0.2% more download; a generator mtime bug fixed; `.ogp` packs still compress 23% (shared Vorbis headers: M15).
  - **D-1/D-2** (352dd5a): the "exit-time crash" was `timeout`'s second SIGTERM taking Epic's abort path inside the
    handler (deadlocks, self-waits): a repeated exit signal is ignored, the crash report bounded by `alarm(10)`,
    `FFileStream::Destroy` waits 5 s as engine-ut2004; censuses 58/58 at both widths and under ASan (no use-after-free);
    `HTTPDownload` in IpDrv and its `EINPROGRESS` fix (HTTP download works for the first time), `Prefab` in Editor;
    `map-census.sh` FAILs on a hang; a note for engine-ut2004 (`logs/d12/engine-ut2004-note.md`).
  - **D-3/D-4** (14719eb): REPORT.md section 60 "changes since" + regenerated appendices (`tools/dev/
    report-appendices.py`), stale comments fixed, O-3 logged above; d6 scenarios pinned to fixed spots; the browser
    strafe slope was the spec's fit without the walk bob (frontend 275273e); m7-effects self-test window;
    `MaterialTime` and `[UC2Movie] MaterialShotTimes` test keys make the twin 34/34.
  - **M16 audio** (d334834): the shipped `.xwb/.xsb` load and play at run time (`Source/UC2ALAudio/Src/UC2XboxBanks.cpp`),
    PCM byte-identical to the Python readers (371 waves), the `.xsb` tables equal to the cooked ones but documented
    rounding (2459 sounds, 2469 cues); a disc-only `Sounds/` tree plays everything (20 of 21 songs: `M_Adoratrice.xwb`
    is WMA: cooked Ogg first (G-10), WMA with M16's video decoder, else silence); F-6's "5 overflowed volumes" were
    LFE levels in the same field.
  - **G-10/G-10b** (d6caa7f, c93514c; ufrontend b7c3a64, d589ecd): WebRTC connections excluded from the Xbox standby
    check on both ends (`UNetDriver::StandbyCheckEnabled()`; a vanished guest or host no longer pauses the other side:
    net spec 4/4); web rumble through the browser's `vibrationActuator` (effects recorded: Nakhti 0.51/0.77 for
    0.25 s, death 0.8/0.8 for 0.75 s); AA off by default on the web; Adoratrice from the cooked Ogg on a disc tree.
  - BLOCKERS audited with the owner: 23 entries closed, 8 open. Preview republished 12:48.
  - Open for the owner: the FOV column, the pad-only rule natively, the four-player full-detail cost,
    `UseTerrain3Layer` on by default (A/B: never slower, up to 20% faster), the flicker conditions, the xemu shot.
