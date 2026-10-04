# Exploration findings (2026-10-04)

The log of the three read-only exploration passes that preceded the plan. Paths under `assorted/`, `assets/*.iso`
and `xbox-sdks/` are as they were then; M0 moved them to `reference/community`, `reference/iso` and `reference/xdk`.

### Workspace (verified first-hand)
- /home/ion/WorkUC2: epic-sources 6.6G, assorted 12G, assets 12G (3 ISOs), prompt.txt. Not a git repo.
- assets/: "(USA).iso" and "(USA, Europe)…​.iso" are byte-identical (md5 251b9210…, 2.27G);
  "(USA, Europe)… (1).iso" is 7.8G (md5 1c8f12de…) — probably full redump vs trimmed xiso (agent checking).
- assorted/UC2004 looks like the unpacked "Unreal Championship 2 PC.zip" (archive.org, doomboom93, 2021).
- essentials.zip = Core.dll + msvcp71.dll (runesofwar). uc2-fixedmaps.zip = 13 maps (2022).
- 3 RARs = Leo(T.C.K.) map fixes (2020): MapsOnHold gametype fixes; restored myleveled static meshes (.ASE/.T3D + .unr).
- Tools present: cmake, ninja, gcc/g++, mingw i686/x86_64, wine, ffmpeg, unrar, 7z, gh (logged in as 10-p, repo scope).
  emsdk 5.0.7 lives in /home/ion/WorkUnreal/emsdk.
- Tools MISSING: ghidra, extract-xiso / xdvdfs (to read the ISOs), vgmstream-cli, xemu, clang, aarch64 cross gcc.
- GitHub org 10-p has: engine-ut2004, engine-ut99, engine-unreal1, engine-hp2, unreal-frontend-arch-private,
  unreal-frontend-azure-infra, metoolkit-karma-decomp (Karma 1.2 as shipped with UT2004 v3369, 145 objects
  recovered to portable C, 64-bit + wasm proven), gl4es fork, openal-soft-web fork, utglr-copy, XOpenGLDrv.

### Web research
- runesofwar guide: Core.dll+msvcp71.dll into System; ini defaults (bUnlockCharacters, mouse/analog/KB axis
  bindings, LockSearchTimer, shadows, KeyBinds layout); DLC map renames (EvilHands→EvilGrasp, Sever→Resect,
  Ascension→Ascent, Monoxide→Oxide, TotalControl→TotalCtrl); known issues: no audio, no movies, shader
  distortion (transparency shows normal maps/solid colours), no bloom, HUD locked to 640x480; 13 "fixed" maps
  remove the broken effect (NOT acceptable for us — we fix the renderer instead).
- reddit thread (VBelov_UZ): BLOCKED for my tools — need a copy from the user if README_TESTER.md is not enough.
- moddb: only "The Older Fix Mappack" (Leo, 2022, 7 MB) is listed under addons; the 3 RARs are from the files tab.

### Provenance (agent B, all by md5/CRC — DONE)
- `assorted/UC2004/` == "Unreal Championship 2 PC.zip" (16,460 files, 0 diffs). Pure duplicate.
- `assorted/UC2004/UC2004` == `epic-sources` (16,451/16,451 identical) + `System/msvcp71.dll` + empty
  `System/Profiles/`. ⇒ **epic-sources is the ground truth**; ~11.5 GB of assorted is redundant.
- Core.dll variants: epic original (md5 72e903f1, PE 2005-07-06); "patched" (79007b53: loose, Core.7z,
  test-suite) = ONE byte at 0x940c0 A1→C3 (RET; function not yet identified); essentials.zip one is a 2020
  rebuild from the leaked source (debug-ish). All irrelevant once we build from source, but the RET patch
  tells us which Core function crashes on PC → identify it (objdump/ghidra) as a known-bug hint.
- uc2-test-suite: Maps/Textures/Animations/StaticMeshes/Help are byte-identical to epic-sources; avaris.exe
  and all game DLLs are Epic's originals. It only adds: d3d8.dll loader (binary only, no source), dgVoodoo
  2.87.1, ffmpeg DLLs, DSP snapshot, Sound.ini, avaris.ini (= Default.ini + ~31 lines: MainMenuClass=
  UCMenu.StartMenu, TextureDetail UltraHigh, MaxDistortion=0, UseCompressedLightmaps=False…), User.ini (=DefUser.ini).
  Audio/XMV/XPR splash are all done inside the closed loader, reading retail Sounds\*.xwb/xsb + Movies\*.xmv.
- Retail ISO: trimmed xiso (2×, identical) and full redump (7.8G) contain the same 1,607 files. Every non-System
  folder (Movies, Sounds, Startup, Textures, Xbox*) is byte-identical to epic-sources. System: 17 .u identical;
  19 files differ (Characters.ini, Default.ini, DefUser.ini, Gametypes.ini, Maplists.ini + 14 Localization
  files) → retail has the final inis/loc. ISO-only: default.xbe, xbox.bin, update/dash xbe, media/.
  Retail volume date 2005-03-23; leaked PC DLLs are dated 2005-07-06 (post-retail tree).
- uc2-fixedmaps.zip: 13 maps, all differ from epic Maps/ (effect removed — reference only, do not ship).
- Leo RARs: 18 .unr fixing MapsOnHold/TestMaps maps (gametype refs, restored myleveled static meshes + ASE/T3D).
  Originals exist only in MapsOnHold/TestMaps (cut content) → candidate "bonus maps", late milestone.
- epic-sources asset inventory: Maps 67 .unr (58 + 9 ComboMatinees); XboxMaps 58 .unr + 57 .lin; Textures 62
  .utx + 35 .xpr + 87 .xmv (Textures/Movies); XboxTextures 599 .xpr; StaticMeshes 45; Animations 16;
  Sounds = XACT 183 .xwb + 79 .xsb (+ .xap project); SoundData = 8,173 source .wav; Music 23 .wav + 23 .wma;
  Movies 23 .xmv; Localization int/deu/fra/ita/spa; 17 .u in System (+ System/OriginalScript).

### engine-ut2004 standard + ufrontend contract (agent C — DONE)
- Repo standard: `Source/<Package>/{Inc,Src}` (all engine C++; Windows/editor code kept but unbuilt),
  `Thirdparty/` (vendored libs, sibling of Source), `Engine/Config/{Default,DefUser}.ini` + `web/` inis,
  `vendor/glue/` (frontend ABI: ue_abi.h, webrtc-shim.js, wasmfs-shim.js, proxynet), `cmake/`, root
  CMakeLists.txt + CMakePresets.json (native, native64, windows, windows64, wasm, wasm-perf…), CLAUDE.md,
  AGENTS.md, BUILD.md, docs/. Assets never in repo. engine-hp2 adds `docs/porting/{PLAN,LOG}.md` + `Assets/`
  (gitignored) + `Config/` — newest template for a from-raw-drop port.
- Fetched (not vendored): SDL2 release-2.32.x, 10-p/gl4es (1.1.7 fork), 10-p/openal-soft-web, karma decomp.
- Flags: -m32 default (UT_ALLOW_64BIT for native64), gnu++14, -fpermissive, -fno-strict-aliasing,
  __STATIC_LINK=1 + --whole-archive, -fshort-wchar native / ANSI TCHAR on web+mingw.
- Web: retail .u from assets (no ucc compile), PROXY_TO_PTHREAD + OffscreenCanvas, Asyncify frame yield,
  WasmFS JSImpl/FetchFS streaming, artifact = SDLLaunch.{js,wasm,data} + meta.json (fixed names).
- Drivers there: SDL2Drv (2.0k LOC; gamepad only on Android!), OpenGLDrv (~9.4k real), GL4ESDrv (10.9k),
  PixoDrv, Engine.NullRenderDevice, ALAudio (4.9k, OpenAL Soft + Vorbis), WebRTCDrv (1.1k), ProxyNetDrv (0.5k),
  SDLLaunch (1.9k), UCC. No movie playback in UT2004.
- Interface drift vs UC2: UnRenDev.h 311→842 lines (push buffers, post-process, PS materials) and UnAudio.h
  heavily different (sound cues/categories) ⇒ OpenGLDrv/GL4ESDrv/ALAudio are *adapt*, not copy.
  UnNetDrv.h nearly same ⇒ WebRTCDrv ports ~as-is. Core/Engine port fixes (UnGnuG.h, UnUnix.cpp, Asyncify
  fail-soft loader patches, -ini: overrides, FLUSHCONFIG, 64-bit SERIAL_POINTER_INDEX) = diff-port.
- Package format: UC2 v151 (v145 compact-index change, v148 tagged props by ref, v150 ULinker) — frontend
  Node tools (generate-deps/manifest/travel, lib/unreal writer hard-wired v128/lic29) must learn v151.
- Karma: decomp = Karma 1.2.1 build 631 (same SDK version as UC2's metoolkit headers) BUT UC2 appears to
  have REMOVED Karma (UnObjVer "Version 140: jg -- Removed Karma", `#if 0` WITH_KARMA, no K*.cpp).
  ⇒ to confirm with agent A; if so ragdolls/physics in UC2 are non-Karma and NO_KARMA is the faithful build.
- Frontend new-target touch list: targets/uc2/{target.json, branding/, file-manifest, dependency-manifest,
  precache}, test/skeleton.test.mjs TARGETS, glue fixtures, .ufront/gates list, packages/e2e/uc2-wasm-{boot,
  net}.spec.js + lib/engines/uc2.profile.js, native-smoke BUILD_TARGETS, publish.local.json assets.uc2,
  infra config requiredFolders; WorkUnreal/bin scripts (build-all-engines, mkruntree, smoke-runtree, e2e-all),
  assets/mount-assets.sh TREES. Precedent docs: frontend docs/migration/steps/2.6 (unreal1), 2.7 (ut99),
  docs/10-porting-lessons.md, docs/05-engine-repos.md.
- Rules: one folder per project; conventional commits; docs change with code; no symlinked content; never
  commit build-*/assets; real log lines for telemetry regexes; engine differences are data in the frontend.

### Misc first-hand
- Core.dll community RET patch (0x940c0) is a non-exported lazy-init near the end of .text (calls an import
  with (0,0)); unidentified, low priority since we build from source.
- ShaderData: 100 `.psh` (xps.1.1 asm incl. `xfc` final combiner, texm3x2pad/tex) + ~66 `.vsh` (xvs.1.1) as
  readable SOURCE, compiled by `xsasm` → translator to GLSL is feasible.
- Core/Engine file overlap with UT2004 port: Core Src 23/30, Engine Src 96/114, Engine Inc 121/141.

### epic-sources source map (agent A — DONE)
- Build: VS .NET 2003 (`Avaris.sln`), PC configs Debug/Release build DLLs + avaris.exe; Xbox = one static
  XBE (`XboxLaunch.vcproj`, 599 files, `_XBOX;__STATIC_LINK`, LL/LS = linear load/save + XBOX_ASSET_FORMAT).
  Overnight script `BuildProcess/sync_avaris.pl`: devenv → `ucc make` → `ucc patchscript` (ScriptPatches.h) →
  xboxassetconversion → .lin generation on devkit.
- Linux remnants (UT2003/icculus era): makefiles in Core/Engine/Editor/GUI/IpDrv/UWeb/UCC/SDLDrv/SDLLaunch but
  `makefile-header`/`makefile-general` are MISSING and object lists are stale; `UnGnuG.h`, `UnUnix.cpp` (lacks
  UC2 additions: appContiguousMalloc, appSystemTime64, appGetLanguageID…), `FFileManagerLinux.h`, SDL 1.2 SDLDrv.
- TCHAR is ANSI ("jg -- Removed unicode support", Core.h:164-201). ENGINE_VERSION = 2226 non-demo.
- Modules with native code: Core, Engine, Editor, UnrealEd, Window, WinDrv, D3DDrv, XboxDrv (platform layer,
  NOT a renderer), XboxAudio, XboxLive, IpDrv, UWeb, GUI, UCGameplay, UCElements, UnrealGame, UnrealUtil,
  Launch, UCC. Script-only: Gameplay, UCInventory, UCEffects, UCCharacters, UCMutators, UCAnnouncements, UCMenu.
  EditPackages (17): Core, Engine, UCAnnouncements, Editor, UnrealEd, IpDrv, UWeb, GamePlay, UnrealGame,
  UCElements, UCGameplay, UCEffects, UCCharacters, UCInventory, UCMutators, GUI, UCMenu. 17 shipped .u = v151/lic0.
  311 .uc use backtick macros (`Core/Classes/Avaris.huc`, Editor `FTextFilter.cpp`) → only UC2's own ucc compiles them.
  `Engine/Inc/ScriptPatches.h` = 30 bytecode patches applied to the shipped .u by FScriptPatcher (UnGame.cpp:2450).
- Missing: all XDK headers (xtl, xact, xmv, uix, xonline…), KarmaSupport.h, UnCDKey.h, UnrealUtilClasses.h,
  XboxMovie.h, ColorRemapGrayScale.h, FNameDebugCOM_i.c. DirectX 8 SDK headers ARE present (DirectX8/).
  Binary-only: IFC23 (unused), ImpersonatorLib (LIPSinc, disabled), encvag, dinterface (script debugger).
- RENDERER: D3DDrv = single D3D8 driver for PC+Xbox (204 `_XBOX` ifdefs). `D3DMaterialState.cpp` (4.8k, UT2003-style
  fixed-function material compiler) + `D3DPSMaterialState.cpp` (1k, per-EPixelShader handlers).
  Xbox: 100 `.psh` xps.1.1 → headers; combined vertex shaders from 67 `.vsh` fragments keyed by 64-bit enum,
  `VertexShaderConfigs.ini` precache list, `XboxVertexShaders/VertexShaders.xvs` cache.
  PC: does NOT use the .psh; has 64 hand-written ps.1.1/1.4 text shaders (D3DRenderDevice.cpp:2306+);
  `GetPixelShader` returns NULL for Distortion/AdditiveDistortion/Modulated/Subtractive + ColorRemap;
  vertex-shader fragment path is `#if _XBOX||XBOX_ASSET_FORMAT` (PC = fixed-function T&L + 2 inline vs.1.1);
  ALL post-processing (`UCGameplay/Src/UCCameraEffects.cpp`: blur, DOF, motion blur, heat shimmer, radial blur,
  bloom, emissive) is `#ifndef _XBOX return;`; movie materials `#ifdef _XBOX`.
  ⇒ the PC visual bugs (normal maps showing, no bloom) are *unimplemented PC paths*, not Xbox shaders on PC.
  Null device exists: `Engine/Src/NullDrv.cpp` (`Engine.NullRenderDevice`). No OpenGL driver ever existed.
- AUDIO: `UAudioSubsystem` is XACT/cue-shaped (`PlaySoundCue(FName)`, categories, `PlayMusic(FString)`; ~255
  script call sites). Xbox impl `XboxAudio/Src/XACTAudioSubsystem.cpp` (2.7k; banks `Sounds/<Bank>[_lang].xwb+.xsb`,
  music = streamed `M_<Song>.xwb`, I3DL2 reverb, DSP image). PC: `Engine.NullAudioDevice` only; no ALAudio; no
  .uax/.umx anywhere. `Sounds/Avaris.xap` = full XACT project (text; 182 wavebanks, 78 soundbanks, ~2438 cues,
  7181 waves) and `SoundData/` has the 8,173 source WAVs it references (all but ~23). `Music/*.wav` = sources.
  Vorbis dir = libvorbis 1.0rc3, referenced by nothing.
- KARMA: compiled out (`Engine.h:151 #if 0//jg -- Removed`; pkg v140 "Removed Karma"; no K*.cpp; all calls under
  `#ifdef WITH_KARMA`; script has no KActor/KarmaParams). metoolkit (1.2.1) + metoolkit_dummy + KarmaData are dead.
- VIDEO: XMV only, Xbox-only player (`XboxDrv/Src/XboxMovieThread.cpp`, `XMVHelper.cpp`, `FMoviePlayer` base in
  `UnVcWin32.h:801`). Uses: startup `UC2_Logos.xmv`, loading movie `Textures/Movies/UI_LoadingMovie.xmv`,
  ladder cinematics via URL `?StartCinematic=`/`?EndCinematic=` (UnGame.cpp ~1752-1810), `MovieMaterial`
  textures (87 .xmv in Textures/Movies — menu previews). TestMaps/ has AVI/MOV/WAV *sources* of some cinematics.
- INPUT: Xbox `XboxViewport.cpp` (XInput → IK_Joy1..16, per-pad routing `MasterProcessJoyInput`, 4-player split
  screen `UnSplitScreenMgr.cpp`); PC WinDrv DirectInput / SDL1.2 joystick (off by default). `KeyBinds.ini` pad
  layouts; `DefUser.ini` has KB+M binds. Gamepad assumptions: lock-on (UCPlayerController.uc:2064/2251/4061),
  GUI `eXControllerCodes` + `XControllerEvent` (GUI.uc:120, UnGUI.cpp:85), UCMenu ~139 A/B/X/Y handlers,
  virtual keyboards, raw axis keys 240/241.
- NET: standard `IpDrv/TcpNetDriver.cpp` (PC default) + `TcpNetDriverXbox.cpp`; dedicated server exists
  (`UServerCommandlet`, `ucc server`, RunServer.bat). Xbox Live code is `_XBOX`-only (UIX, matchmaking, DLC, stats).
- HAZARDS: pkg v145 FCompactIndex = plain INT32; lic-1 (Xbox) branches in mesh/model/terrain serializers; Xbox
  bulk data in XPR; linear loading (.lin) + DLO lists skippable on PC; inline x86 asm (UnVcWn32SSE.h, UnMath.h,
  UnRenderLight/StaticMesh/Batch, UnSkeletalMesh); no PTRINT, ~32 ptr→int casts + ~34 pointer-ish script ints;
  Win32 CreateFile/ReadFile + `D:\ Z:\ T:\` paths in Engine; 451 `_XBOX` ifdefs in Engine, 201 in Core;
  `#pragma pack` ×44; some .uc contain 0x02 bytes.
- Asset folders: Maps/Textures/StaticMeshes/Animations = PC format (lic 0) "source"; Xbox* = cooked (lic 1 +
  XPR); MapsOnHold = 37 old maps (v128–145); TestMaps = 18 test maps + cinematic sources; GUIRes/*.u and
  System/OriginalScript/*.u are byte-identical duplicates of System/*.u.

### Owner decisions (2026-10-04, round 1)
- WORKSPACE: fully self-contained in /home/ion/WorkUC2 — own frontend clone (branch `uc2`) AND own emsdk
  install; /home/ion/WorkUnreal is read-only reference, never modified.
- SCRIPTS: boot/play with the shipped .u + ScriptPatches first; later build ucc/Editor, recompile from
  source, make KB+M/menu fixes in script, drop the patches.
- AUDIO: cook from sources (Avaris.xap + SoundData WAV + Music WAV → OGG + cue table). Runtime Xbox bank
  (.xsb/.xwb) loading is an END-OF-PROJECT improvement.
- VIDEO: transcode offline to an open format with a small portable decoder. END-OF-PROJECT: optional ffmpeg
  bundling to decode XMV directly. Owner: "we will want to work with the original Xbox assets at some point,
  but not now" ⇒ keep an "original Xbox assets" track as a final stage (xwb/xsb, xmv, later maybe lic-1 pkgs).

### Machine facts for media/oracle (first-hand)
- ffmpeg here demuxes/decodes XMV: `UC2_Logos.xmv` = WMV2 640x480 30 fps + PCM s16le 44.1 kHz stereo.
  Encoders available: libtheora, libvorbis, libopus, mpeg1video, libvpx. No `oggenc` (use ffmpeg -c:a libvorbis).
- wine 10.0 + Quadro P620 (GL 4.6, NVIDIA 580) + Xvfb ⇒ original avaris.exe under wine is a usable visual oracle.
- Not installed: java (needed by Ghidra), qemu-aarch64, aarch64 cross gcc, clang, extract-xiso, vgmstream-cli, xemu.

### xbox-sdks (added by owner, first-hand look)
- `/home/ion/WorkUC2/xbox-sdks` = archive.org dump of 25 XDK drops (2000 alpha … 5849.17, 2005-03), ~10 GB.
  Each `.7z` wraps an installer EXE or a RAR, so headers cannot be listed without unpacking the inner file.
  Best matches for UC2 (retail 2005-03): `2005-03 - 5849.6, 5849.17` and `2003-12 - 5849`; `2003-02 - 5344 -
  Extracted SDK` is "files only" (easiest to read). Exact XDK used by UC2 = library stamps in `default.xbe`.
