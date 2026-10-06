# engine-uc2 porting plan (read this first)

Approved by the owner on 2026-10-04. Progress is recorded in `LOG.md`, open problems in `BLOCKERS.md`,
evidence and design notes in `analysis/`.

## Context
Unreal Championship 2 (UE2 build 1226/2226, package v151, codename Avaris) was Xbox-only; a leaked Epic
tree contains a half-working Win32/D3D8 PC build (no audio, no movies, broken effects, gamepad-centric).
Goal: a clean, CMake-built, portable engine repo (`engine-uc2`, following the engine-ut2004 standard) that
runs on Linux, Windows (MinGW) and the Web (Emscripten) with the best achievable visual/audio quality, and
slots into the ufrontend system as target `uc2`. Everything lives in `/home/ion/WorkUC2` on `ion-kubuai`;
`/home/ion/WorkUnreal` is read-only reference. This first pass was: read everything, check the brief for
inconsistencies, and produce a staged plan. Exploration is complete (`analysis/exploration-findings.md`, `analysis/design.md`).

## What the exploration settled (corrections to the brief)
- **epic-sources is the ground truth.** archive.org tree + zip are checksum-identical to it; the retail ISO's
  content folders are identical to its `Xbox*`/`Sounds`/`Movies`; the leak is the later (post-retail) tree.
- **Karma is compiled out of UC2** (pkg v140 "Removed Karma") → no physics work, decomp not needed.
- **PC render bugs are unimplemented PC paths**, not Xbox shaders on PC: Xbox-only materials fall back to
  texture stage 0 (`D3DMaterialState.cpp:1468-1507`), PC distortion is opaque (`D3DPSMaterialState.cpp:342-457`),
  post-processing is `_XBOX`-gated in engine + driver. Shader sources (100 `.psh`, 67 `.vsh`) are readable text.
- **No audio driver and no OGG on PC**; audio interface is XACT cue-based. All source WAVs + the XACT project
  text (`Sounds/Avaris.xap`) exist. **No movie decoder on PC** (XMV = WMV2; ffmpeg here reads it).
- **A dedicated server does exist** (`ucc server`); proxy driver still skipped by owner choice.
- ANGLE/DXGL/WineD3D do not fit; UC2's D3DDrv is a strict superset of UT2004's D3DDrv and UT2004's OpenGLDrv
  mirrors that D3DDrv → the GL driver is a 3-way merge + UC2-only additions.
- TCHAR is ANSI in UC2 (never `-fshort-wchar`/UNICODE). Frontend Node tools must learn package v151.

## Owner decisions (2026-10-04)
| Topic | Decision |
|---|---|
| Workspace | Fully self-contained in WorkUC2: own frontend clone (branch `uc2`), own emsdk. WorkUnreal untouched. |
| Renderer | New GL driver by merge (UT2004 OpenGLDrv/GL4ESDrv + UC2 D3DDrv as spec); then implement all shaders/effects on top so every material renders correctly. Epic's D3DDrv stays in tree, unbuilt. |
| Scripts | Shipped `.u` + ScriptPatches first; recompile from source later (own ucc), then script-side fixes. |
| Audio | Cook from sources (xap + WAV → OGG + cue table). Runtime Xbox bank loading = end-of-project. |
| Video | Offline transcode (Theora video + Vorbis audio, libtheoradec). Runtime ffmpeg XMV decode = end-of-project. |
| arm64 | Later / build-only: keep code 64-bit clean, add preset, no device chase before web ships. |
| Cleanup | Reorganise at M0, delete nothing until the end and only from an owner-confirmed list. |
| Net | WebRTCDrv ported; ProxyNetDrv not. |

## Workspace layout (created at M0; moves are same-filesystem renames, no deletions)
```
/home/ion/WorkUC2/
  epic-sources/      pristine leak — never edited (sha256 manifest recorded at M0)
  engine-uc2/        NEW git repo (local only; no remote/push without owner go-ahead; private if pushed)
    Source/<Pkg>/{Inc,Src,Classes}   Thirdparty/   Engine/Config/{,web/}   vendor/glue/   cmake/   tools/
    docs/porting/{PLAN,LOG,BLOCKERS}.md   docs/porting/analysis/*.md   CLAUDE.md AGENTS.md BUILD.md README.md
    CMakeLists.txt CMakePresets.json (native, native64, windows, windows64, wasm, wasm-perf)
  ufrontend/         clone of 10-p/unreal-frontend-arch-private, branch uc2 (targets/uc2, e2e, v151 tools)
  emsdk/             own Emscripten 5.0.7
  assets/uc2/        canonical asset tree (PC-format packages + .u + inis/loc + cooked audio/video); gitignored
  reference/         iso/ (the ISOs), community/ (former assorted/*), xdk/ (xbox-sdks), prompt.txt
  run/ bin/ logs/ .cache/ env.sh
```

## Working rules
- ≤4 agents at once (normally ≤3). Every agent appends findings to `engine-uc2/docs/porting/analysis/<topic>.md`
  as it goes; coordinator writes one dated line per green gate in `docs/porting/LOG.md`, blockers in BLOCKERS.md.
- Model tiers: Sonnet = file shuffling/listings/CMake lists; Opus = compile-fix loops and ports with a spec;
  Fable sub-agent only for renderer/shader-translator design and layout/64-bit puzzles.
- `grep -a` always (ugrep here skips Latin-1 sources). Keep Latin-1 bytes; CRLF→LF once at import.
- Be gentle with Epic code: never define `_XBOX`, `XBOX_ASSET_FORMAT`, `LIN_LOAD`; lift Xbox-only gates through
  named capabilities (`UC2_POSTPROCESS`, `UC2_MOVIES`) rather than deleting code. Fix real bugs properly.
- One commit per coherent step, conventional-commit style; never commit assets or `build-*/`.

## Milestones (each ends at an observable gate; do not start N+1 before gate N is green unless marked ∥)
| # | Goal | Key work | Gate |
|---|---|---|---|
| **M0** | Workspace + scaffold | Layout above; import sources into `engine-uc2/Source` (LF); `Thirdparty/` (zlib, nvTriStrip, ogg/vorbis from UT2004 port, DirectX8 unbuilt); CMake skeleton + presets from engine-ut2004; own emsdk; frontend clone; build `extract-xiso` into `tools/`; unpack XDK 5849 + 5344 into `reference/xdk`; move Appendix A/B into `docs/porting/analysis/`; canonical `assets/uc2` v0 = epic-sources PC folders + System | `cmake --preset native` configures; `git ls-files` has no content files; `emcc --version` = 5.0.7; sha256 manifest of epic-sources unchanged |
| **M1** | Core compiles | UT2004 `UnGnuG.h`/`UnUnix.*`/`FFileManagerUnix.h` as merge source + UC2 additions (`appContiguousMalloc`, `appSystemTime64`, …), `UnThreadingPosix.cpp`, replay the 44-file ufront Core delta as a checklist; x86 asm → C | `g++ -m32` and `emcc`: 0 errors; `libCore.a` |
| **M2** | Engine + game natives + null drivers compile | Engine, GUI, UCGameplay, UCElements, UnrealGame, IpDrv, UWeb, UCC; static link (`__STATIC_LINK`, whole-archive); Xbox-only files left unbuilt (≤4 agents on disjoint file sets) | all libs + `ucc-bin` link |
| **M3** | Maps load headless | struct-layout guard against shipped v151 `.u`; `appInit`/ini derivation; path/case fixes | `ucc-bin batchexport` of 17 `.u` with 0 layout mismatches; `ucc-bin server DM-<map>` logs "Bringing … up for play" and ticks 60 s; census of all 58 maps in LOG |
| **M4** | Client boots, null render + null audio | SDL2Drv-UC2 (KB/mouse + pad table), SDLLaunch-UC2 | under Xvfb `-nullrenderer`: menu level up, StartMenu opened, key navigation logged |
| **M4w** ∥ | Early web smoke | wasm link with null drivers, FetchFS, Asyncify yield, `-ini:` overrides | headless Chromium reaches the same menu log line |
| **M5** | Renderer R1: fixed function | `UC2GLDrv` = 3-way merge; `_WORD` cache ids; FBO default target + present shader (gamma there); early spike: GL4ES accepts our GLSL (fragment-only, dependent reads) | menu + one DM map screenshots match original `avaris.exe` under wine (bugs included); no GL errors |
| **M6** | Renderer R2: programmable materials | `tools/psh2glsl.py` (+CPU reference interpreter) for the 100 `.psh`; GLSL vertex-fragment splicer for the 67 `.vsh` keyed by `VertexShaderEnum`; port `D3DPSMaterialState.cpp`; Xbox-only materials rendered for real | per-shader golden tests pass; team skins, emissive skins, dual-diffuse visibly correct |
| **M7** | Renderer R3: targets + post-process | render-target pool, back-buffer copy, distortion (see-through), colour remap, `CopyDepthBuffer`, DOF, bloom/emissive, motion/radial blur, heat shimmer; lift engine gates (`UnRender.cpp` FPostProcessRender, `UCCameraEffects.cpp`, `UnPixelShaderMaterial.cpp`) | each effect toggled and screenshotted; distortion no longer shows normal maps; GL4ES native twin passes the same |
| **M8** ∥(from M4) | Audio | `tools/cook-audio.py` (xap + WAV → per-bank OGG + `.uc2cues`; `.xsb` name cross-check); `UC2ALAudio` on OpenAL Soft (cue logic per `XACTAudioSubsystem.cpp`, categories, music stream, EFX reverb from zone presets); null audio kept; works with no Xbox DSP files | menu music, announcer, 3D weapon/footstep sounds; zone change logs reverb preset |
| **M9** | Video | `tools/cook-movies.sh` (ffmpeg → Theora + per-language Vorbis; use TestMaps AVI/MOV sources where they exist); `FTheoraMoviePlayer`; MovieMaterial as dynamic texture; logos, cinematics (`?StartCinematic=`), loading movie v1 | logos play and skip; a menu preview animates; one ladder cinematic plays with audio |
| **M10** | Controls | ini-driven key→pad-code table (no WASD collision), mouse click → A/B in GUI, SDL GameController on all platforms (+rumble), runesofwar defaults baked into our `Default.ini`/`DefUser.ini`/`KeyBinds.ini` | full menu flow + a match with keyboard/mouse only; same with a pad |
| **M11** | Web product | `UC2GL4ESDrv` on WebGL, web inis, WebRTCDrv; frontend `targets/uc2/*`, v151 support in generate-deps/manifest/travel + `lib/unreal`, e2e boot/net specs, gates | `yarn build --target uc2 && yarn serve` plays in Chromium; uc2 E2E green |
| **M12** | Windows | MinGW i686 + x86_64 presets | wine smoke reaches menu |
| **M13** | 64-bit + arm64 preset | `SERIAL_POINTER_INDEX` for LP64, pointer-in-script-int side tables, layout checks; aarch64 preset build-only | `native64` plays a match on this box; arm64 preset compiles when a toolchain is installed |
| **M14** | Scripts | Editor (no UnrealEd) + `ucc make` with UC2's backtick preprocessor; prove recompiled bytecode vs shipped + patches; retire ScriptPatches; script-side KB+M (UCMenu handlers, glyphs, virtual keyboards, lock-on toggle) | recompiled `.u` load and play; patches removed |
| **M15** | Content polish | canonical `assets/uc2` final; DLC map renames; HUD 640×480 lock; evaluate MapsOnHold + Leo(T.C.K.) fixes as bonus maps (no effect-removing "fixed maps") | owner review of the asset list |
| **M16** | Original-Xbox-assets track | runtime `.xsb/.xwb` loader; optional ffmpeg XMV decode; optional DSP-file reverb enhancement; (stretch) licensee-1 packages | plays from a plain retail-disc tree |
| **M17** | Close-out | owner-confirmed deletion list for `reference/`; publish only on go-ahead | — |

## Reuse (do not rewrite)
- From `/home/ion/WorkUnreal/engine-ut2004` (read-only source): `CMakeLists.txt`/`CMakePresets.json`/`cmake/*`,
  `vendor/glue/*` (byte-identical), `Source/SDLLaunch`, `Source/SDL2Drv`, `Source/OpenGLDrv` + `GL4ESDrv`,
  `Source/ALAudio` (device setup + `ALAudioStream.cpp`), `Source/WebRTCDrv`, `Core/Inc/UnGnuG.h`,
  `Core/Src/UnUnix.cpp`, Asyncify fail-soft loader patches, `appApplyIniOverrides`, `FLUSHCONFIG`,
  `Source/SDLLaunch/cmake/emit-artifact-meta.mjs`. Fetched: SDL2 2.32.x, 10-p/gl4es, 10-p/openal-soft-web.
- From epic-sources as *spec*: `D3DDrv/Src/*`, `XboxAudio/Src/XACTAudioSubsystem.cpp`, `XboxDrv/Src/XboxViewport.cpp`
  (pad mapping), `XboxDrv/Src/XboxMovieThread.cpp`, `ShaderData/*`, `Engine/Src/NullDrv.cpp` (already there).
- Playbooks: `10-p/engine-hp2:docs/porting/PLAN.md`, frontend `docs/10-porting-lessons.md`,
  `docs/05-engine-repos.md`, `docs/migration/steps/2.6-engine-unreal1.md`.

## Verification (end to end)
1. Native: `cmake --preset native && cmake --build …`; run tree via `tools/dev/mkrun-uc2.sh`; headless boot
   `xvfb-run ./uc2 -nullrenderer -log` (M4) then with GL (M5+); screenshots compared against the wine oracle
   (`wine avaris.exe` in a copy of epic-sources/System) and, if the owner supplies BIOS files, xemu.
2. Shader translator: headless golden tests (CPU interpreter vs GLSL output) for all pixel shaders.
3. Audio/video: cue-name cross-check vs shipped `.xsb`; log-asserted playback of music/cues/movies.
4. Web: `yarn build --target uc2 && yarn serve`, then the uc2 E2E specs under the memory guard.
5. Windows: wine smoke. Every gate's evidence line goes into `docs/porting/LOG.md`.

## Needed from the owner
All items of the first list were answered on 2026-10-06 (`<WorkUC2>/answers.txt`; summary below). Open:
- How the native build was launched when it had no sound and no movies (see O-1).
- Which keyboard bindings feel "backwards" (an example or two) (see O-14).
- Go-ahead, repository name and visibility for pushing engine-uc2 to GitHub and setting up CI (needed for
  the real-pad and real-Windows tests the owner wants to run from CI builds).

## Owner review of 2026-10-06 and the tasks it creates

Source: `<WorkUC2>/answers.txt` (the owner's answers to every open question, plus an addendum), with the
screenshots in `<WorkUC2>/temp/` (xemu captures of the Xbox game and ours). Decisions taken there:
- **Models:** no Fable for subagents from now on; Opus 5.5 for reasoning, Sonnet 5.5 for mechanical work.
- **Pause overlay** (teal negative): correct, matches the Xbox. **Dynamic gamma**: correct, keep as is.
- **Distortion size**: the DM-Praxis panes close up are "spot on"; other distortions need work (O-5..O-8).
- **Localised taunt indices**: keep the Xbox behaviour (no relink). **Zone reverb**: keep it on (the owner
  hears reverb/echo on the Xbox in DM-Azure).
- **Config and localisation set**: the leak, for simplicity (unless it changes behaviour).
- **Real gamepad and real Windows**: tested later, from CI-built binaries once the engine is on GitHub.
- **arm64 toolchain, qemu-user, xdotool**: installed.
- **Reddit thread**: saved in `<WorkUC2>/temp/` (only the key bindings matter; already known).
- **Corrections to my report**: the web build does render at the canvas size (4K fullscreen works, HUD
  aside); maps load and play in the browser, with the typed player name.
- **xemu** (`<WorkUC2>/xbox-emulation/run-uc2-iso.sh`) exists but is slow here: last resort only.
- **Jank last**: HUD/menu scaling, mouse aim, bindings and the other script-side issues come after the plan's
  remaining technical work, in the order of the addendum.

Tasks, in the order they will be done (each one ends at an observable check, logged in LOG.md):

**Phase A: finish and merge the work in flight**
- A-1 Merge M13 (64-bit; it also fixed "the POSIX file manager reports directories as existing (saved
  profiles load)", which may be the owner's save bug on native) and the `net-loss` branch (lost handshake
  packets no longer hang a join). Coordinator verifies each gate on main before logging it.
- A-2 Finish M11's renderer part (GL4ES twin + browser) with an Opus agent that continues from the Fable
  agent's notebook and commits (`analysis/web-renderer.md`); merge.

**Phase B: what the owner found wrong in what exists (engine and data side; no script changes)**
- O-1 **No sound, no movies ("they skip or play green, there are errors in the console")**. First find out
  where: the web build has neither yet (expected, O-2); natively, the only native logs on this machine are
  ours, and our run of `bin/play-uc2` through PulseAudio plays (2026-10-06: `ALSOFT 1.25.2 on "uc2test"`).
  Our OpenAL Soft build has no PipeWire backend (`libpipewire-0.3-dev` is not installed; PulseAudio and ALSA
  are built). Make device selection robust (try PipeWire, PulseAudio, ALSA in order and log which), make
  the run tree carry everything it needs, and ask the owner how they launched it.
- O-2 **Web audio and web movies** (M11 scope): openal-soft's Web Audio backend from the fork the sibling
  port uses, the cooked Ogg audio and Ogg Theora movies through the frontend's file system, precache rules.
- O-3 **Depth of field**: the owner cannot see it (what they see in DM-Praxis is bloom, correctly). Explain
  it with a picture pair, and default `DOFShippedBlurAlpha` to the shipped value (65) so the look matches the
  Xbox binary, consistent with the "keep the Xbox behaviour" decisions (see O-3 in LOG.md when done).
- O-4 **DM-Azure reverb**: verify that DM-Azure's zones carry a reverb preset and that it reaches OpenAL
  (EFX), with a capture pair; the owner hears it on the Xbox.
- O-5 **DM-Azure water pool**: renders as flat transparent black; on the Xbox it has a textured, reflective
  surface with golden caustics (`temp/AzureWaterDistortion.png` vs `temp/AzureWaterFlat.png`). Splash
  distortions show but do not fade properly.
- O-6 **DM-Azure waterfall, first person**: the distortion behind the waterfall disappears while the player
  stands still and returns when moving (`temp/AzureWaterfallStraight.png`); in third person it works
  (`AzureWaterfallDistorted.png`), but the waterfall is sometimes drawn on top of the character.
- O-7 **Distortion range (tuning, not a defect)**: the DM-Praxis panes lose their distortion at a distance
  (`temp/PraxisWindowPanesDistance.png`). Owner, 2026-10-07: "not meant to be a fix per se but figure out
  what tuneable is there AND THEN increase it": find the existing knob (and its Xbox value), then raise it
  through that knob, the Xbox value one setting away.
- O-8 **Defaults**: anisotropic filtering 16x; anti-aliasing 4x (multisampled main framebuffer; WebGL 1 has
  no multisampled framebuffer objects: WebGL 2 or a post-process AA on the web, decided with evidence).

**Phase C: finish the web product (M11)**
- C-1 Port engine-ut2004's `-ini:` command-line overrides (`appApplyIniOverrides`) to UC2: the frontend's
  player name, and the localisation option below, depend on it.
- C-2 Save games and profiles on the web: find every file the engine writes (profiles, `Save/`, inis) and
  declare them as user data in `targets/uc2` so they persist (the native part is A-1 / M13's fix).
- C-3 Precache list, target descriptor completed, e2e boot/play specs green.

**Phase D: M14 scripts, then the owner's addendum in this order (script changes allowed from here)**
- D-1 M14: build ucc/Editor, recompile the scripts, prove the bytecode against the shipped `.u`, retire
  ScriptPatches.
- D-2 **UI scaling** (most urgent of the addendum): menus and the in-game HUD, reticle included, drawn in a
  centred 4:3 area at any resolution (today they distort or stay in the top-left corner beyond 480p).
- D-3 **Saving**: profiles and saves persist (native and web), verified by a restart.
- D-4 **Player name and profile name**: the frontend's name wins when there is no profile; a profile's name
  wins when one exists; if changing a profile's name from outside is impractical, the frontend disables
  its name field for that case.
- D-5 **Text entry**: the on-screen keyboard pages accept a real keyboard, Enter accepts, Escape cancels.
- D-6 **Mouse and gamepad separated rigorously**: relative, uncapped, unsmoothed mouse look with no camera
  drift toward the movement and no "hexagonal" feel; the pad keeps its Xbox response curve, acceleration and
  aim assist; fix the "backwards" keyboard bindings.
- D-7 **Console**: find out whether it is locked or hidden and make it available.
- D-8 **AutoFOV**: port the widescreen field-of-view work of the other engines (frontend steps 2.16, 2.29).
- D-9 **Rebinding controls in game**: a menu to rebind keys (absolutely last of the addendum).

**Phase E: last of all**
- E-1 Split-screen on PC.
- E-2 Web multiplayer at launch (WebRTCDrv, as UT2004/UT99/the others have it).
- E-3 Gamepad support in the frontend, for every engine.
- E-4 A language option in the frontend's advanced section, for every engine: target data naming what to
  change (the `-ini:` setting) and a map of languages to values (e.g. `"English": "int"`); uses C-1.
- E-5 GitHub + CI (after the owner's go-ahead), then the real-pad and real-Windows sessions from CI builds.
- Then M15 (content polish), M16 (original Xbox assets), M17 (close-out), as planned.
