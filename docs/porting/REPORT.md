# UC2 port: every change to the engine, where and why

Review report for the owner, 2026-10-08. The engine (`engine-uc2`) is reviewed as of main `4813dd9`. The
third-person jitter fix merged after that (`c59c02d`) has its own section, 50. The frontend (`ufrontend`) is reviewed
as of branch `uc2` `4ac0d6d`.

Four writers produced sections 10-40 by reading the code and the notebooks. The coordinator wrote this overview,
section 50 and the generated appendices. Nothing was re-run for this report: measurements are quoted from LOG.md
and the notebooks, labelled *measured*, *seen only* or *not verified*.

## How to read this

- **Start with "Start here" below.** It lists what most needs your eyes and the decisions waiting on you.
- **Sections 10-50 cover one area each.** Every feature has the same blocks:
  - **what you see**
  - **where** (file:line and commits)
  - **how the parts fit** (native vs script vs ini vs frontend, and what you get if one part is missing)
  - **why**
  - **decisions and alternatives**
  - **disadvantages, risks, deviations**
  - **status**
  - **off switch**
- **Each section ends with two lists:** "Open issues in this area" and "Places where we changed Epic's shared code".
- **The appendices are generated from git, not written by hand.** Use them to check the sections.
  - Appendix A: every one of Epic's files we modified, with the commits that touched it.
  - Appendix B: our new modules.
  - Appendix C: every ini key that differs from Epic's.
- Line numbers refer to the snapshot `4813dd9`. `git -C engine-uc2 show 4813dd9:<path>` shows the exact file.

| Section | Area | Features |
|---|---|---|
| 10 | Platform, build, runtime: layout, CMake, Core, porting fixes, package loading, the web runtime, `-ini:`, Windows, 64-bit, inis, tools inventory | 14 + inventory; all 136 modified Epic files classified |
| 20 | Renderer: the GL driver, shader translation, post-processing, projectors/shadows, gamma, AA, GL4ES/WebGL | R-1 .. R-22 |
| 30 | Input, UI, scripts, profiles and saves: SDL input, pads, the mouse (D-6), UI box, profiles, names, text entry, console, AutoFOV, SaveConfig, rebinding, the script compiler, rebuilt packages | 30.1 .. 30.18 |
| 40 | Audio, movies, networking and the frontend | 40.1 .. 40.20 |
| 50 | Third-person character jitter (O-11), merged after the snapshot | 1 |
| A-C | Generated: modified Epic files with commits; new modules; ini keys vs Epic's | |

## Start here: what most needs your review

1. **Three script packages are no longer Epic's.**
   - The asset tree ships rebuilt `GUI.u`, `UCGameplay.u` and `UCMenu.u` (since D-4, D-6 and D-9; Phase D allowed
     script changes).
   - The mouse look is the one feature that stops working with the shipped `UCGameplay.u`; the other two packages
     carry the player name without a profile and the KEYBOARD / MOUSE page (table at the top of section 30).
   - They keep the shipped package GUIDs. Peers with the shipped and the rebuilt `UCGameplay.u` (a server package)
     pass the GUID check but run different code. Never mix them.
   - Rebuilding from source also changes CTF: Epic's load-time `CTFFlag.home` patch is defective. On the shipped
     package a returning flag never runs its `Begin:` code. Ours does, so an enemy standing on the base gets the
     flag after 0.1 s instead of on the next touch. Not tested in a game (30.18).
2. **We modified 136 of Epic's files, about +7,100/-2,900 lines.** Section 10 classifies all of them:
   - 95 are porting fixes only (compile, portability, 64-bit, bugs found while porting);
   - 18 mix a porting fix with a feature;
   - 23 are feature changes only (renderer gates, input, UI, saves, network fixes, movies).
   - Every Xbox-only path we needed is lifted through a named render capability. `_XBOX` is never defined.
3. **The mouse (section 30.5) is the model of how layers combine:**
   - SDL2Drv delivers raw relative counts (C++).
   - `UCPlayerInput` saves them before Epic's stick code overwrites the look axes, then adds them back linearly
     (script, `UCGameplay.u`).
   - `DefUser.ini` binds the mouse to `aTurn/aLookUp`, and `UC2InputMigrate.cpp` updates old User.ini files.
     *(Superseded (2026-10-09/10): see section 60, 00 and 30.5.)*
   - The section has a table of your complaints against the measured cause, before and after.
4. **The renderer aims at the Xbox look, mostly without an Xbox picture to compare (section 20).**
   - Glow/bloom strength, dynamic gamma, distortion size, the Xbox shadow filter and the projectors are "seen only"
     or checked against Epic's code. A few xemu shots would settle the biggest ones.
   - Owner-tuned values sit one setting away from the Xbox's: DOF alpha 65 (O-3), distortion fade 15000 instead of
     5000 (O-7), blurs sized for 480 lines (O-10), AA 4x / anisotropy 16x (O-8).
5. **The web build is WebGL 1 only, through GL4ES.** A device without `WEBGL_depth_texture` loses depth of field
   and distortion occlusion (no fallback yet).
   *(Superseded (2026-10-09/10): see section 60, 00 and R-20.)*
6. **Audio follows Xbox XACT behaviour on OpenAL Soft, with zone reverb. Nobody has compared it with a real Xbox**
   (mix level, HRTF colour, reverb loudness) (section 40). Movies are Theora/Vorbis, and their picture in a real
   browser has never been checked by a test.
7. **Networking:**
   - The join and packet-loss fixes are in Engine/Core (TcpNetDriver itself is unchanged), and both ends need them.
   - WebRTC works for one host and one guest.
   - A guest closing the browser tab pauses the host's game for up to 60 s. That is UC2's standby check, left as
     Epic wrote it (40.12-40.15).
8. **Some frontend changes reach every target** (section 40.17-40.18). Package versions 145-151 in the shared
   glue's readers, and the shared name-ownership and user-files code. The older targets' output was recorded as
   byte-identical after the change; that check was not re-run here.

## Decisions waiting for you

| # | Decision | Where |
|---|---|---|
| 1 | The walk bob under the mouse camera (about ±8 px of smooth sway at 1080p, now fully visible): keep, smooth or drop under the mouse only | 50 |
| 2 | O-14: an example of the "backwards" keyboard bindings (the audit found nothing inverted; candidates: C/V order, arrows turning instead of strafing) | 30.8 |
| 3 | D-9 page choices: C/V order, Space dodge while moving vs jump only, swap vs clear on a key conflict | 30.16 |
| 4 | The pad's slow turns at high frame rates (Epic's integer rotation per tick: below about 0.335 deflection nothing turns at 60 fps); carrying the fraction would change Epic's pad arithmetic | 30.8 |
| 5 | A guest closing the tab pauses the host for up to 60 s: keep Epic's standby check, or disable it for the WebRTC driver | 40.15 |
| 6 | 19 missing source WAVs (15 silent cues): decode them from the shipped `.xwb`? | 40.1 |
| 7 | O-6 DM-Azure waterfall: the exact spot, window size and view to reproduce it | 20 R-12 |
| 8 | Pin the fetched libraries (SDL2, OpenAL Soft fork, GL4ES fork) to commits instead of branches | 10, 20, 40 |
| 9 | Web profiles as flat files (chosen) or a directory wildcard in the frontend | 10 feature 10, 30.10 |
| 10 | E-5 GitHub + CI: go-ahead, repo name, visibility (also unlocks the real-pad and real-Windows sessions) | |
| 11 | When to resume E-1 split-screen, E-3 frontend gamepad, E-4 language option (stopped 2026-10-08 on your order; each has only its step-0 notebook on its branch) | |

> Superseded (2026-10-09/10): see section 60, 00 (decisions).

## Found while writing this report (not fixed: nothing was changed for the report)

- **Renderer**
  - `DOFShippedBlurAlpha` is `True` in the inis but its compiled default is 0 (`UC2GLRenderDevice.cpp:214`). An ini
    without the key gets the non-shipped alpha.
  - The device turns dynamic gamma back on at every session start, so a player can only switch it off per session.
  - The GL4ES device reads `DistortionCullDistance` only from the `[UC2GLDrv.UC2GLRenderDevice]` section; the copy
    in its own section is ignored.
  - Stale text: BLOCKERS.md still says the projector shaders and `PS_Terrain3Layer` are not drawn; a comment in
    `UnRenDev.h` names a file without a gate; a comment on `UseXboxProjectors` overstates. LOG.md has no O-3 entry.
- **Platform**
  - README.md and BUILD.md still say "Status: M0", and BUILD.md has no web section. `UC2_ASSETS_DIR` is documented
    but read by nothing.
  - The web `meta.json` does not record the fetched libraries.
  - BLOCKERS.md contradicts itself on arm64.
  - `Default.ini` has been LF since the M7 merge (a plain diff with Epic's CRLF file is noisy; use Appendix C).
  - engine-ut2004's "fail-soft loader" was not ported (real exception catching is used instead).
  - Whether the exit-time dedicated-server crash/hang (M13, BLOCKERS.md) is gone is not recorded.
- **Audio / network / frontend**
  - `audio.md` says category volumes below 0.1 are silent; the code plays them at -10 dB.
  - `WebRTCDrv.h` and `webrtc.md` say the data channel is unordered; the one the browser tests measured is
    reliable and ordered.
  - The test console commands (`NETPLACE`, ...) and `-NETOLDACKS` are compiled into the shipping engine.
  - Frontend `branding/help.md`, `portal-notes.json` ("must not be published as playable"), the target README and
    `STATE.md` still describe the first-light build.
  - The web manifest is 1,991 MB against a 2,048 MB cache budget; sound, music and movies are not precached.

> Superseded (2026-10-09/10): see section 60, 00 ("Found while writing").

## Never verified (by any test or person)

- **Real hardware and software:**
  - real Windows (wine only);
  - a physical game pad (SDL's virtual pad only; pads are off on the web);
  - AMD or Intel GPUs (Mesa llvmpipe and an NVIDIA Quadro P620 only);
  - Safari, Firefox, phones;
  - an arm64 configure or build.
- **Comparisons with the Xbox:** most of the renderer's look (item 4 above) and the audio mix and reverb.
- **Feel:** the mouse and pad were measured by script; no person has judged them.
- **Network:** team games and CTF online, package download, more than three clients, two web guests, travel over
  WebRTC, in-game server discovery on the web.
- **Never exercised in a game:** split-screen, voice stealing, outro cinematics, a movie material with sound.

## The size of it

| What | Files | Lines added / removed |
|---|---|---|
| Epic's source files we modified | 136 (Engine 68, Core 31, Editor 12, UCGameplay 6, IpDrv 6, GUI 6, other 7) | +7,085 / -2,906 |
| New source files of ours under `Source/` | 187 (UC2GLDrv 127, UC2ALAudio 9, SDL2Drv 8, UC2Movie 6, Core 6, Engine 6, SDLLaunch 5, WebRTCDrv 5, ...) | +51,600 |
| UnrealScript (.uc) | 9 files: 7 modified, 2 new (UCMenu); the jitter fix adds 2 more edits (section 50) | +1,875 / -21 |
| Tools (`tools/`: test scripts, shader translator, cookers) | 85 | +17,218 |
| Docs (`docs/`: plan, log, blockers, 34 notebooks) | 37 | +12,155 |
| Thirdparty (vendored zlib, Ogg/Vorbis, Theora) | 571 | +83,952 |
| Frontend (`ufrontend` uc2 vs its origin/main base) | 42 | +4,075 / -34 |

There have been 533 engine commits from the import (`7633696`, 2026-10-04) to the snapshot `4813dd9`.

## What is built, per platform

- **Built** (root `CMakeLists.txt` `UC2_PACKAGES`):
  - Epic's, ported: Core, Engine, IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI.
  - Ours:
    - UC2ALAudio (OpenAL Soft);
    - UC2Movie (Ogg Theora/Vorbis);
    - SDL2Drv (window, input, pads);
    - UC2GLDrv (OpenGL renderer);
    - UC2GL4ESDrv (the same renderer bound to GL4ES: WebGL and the native GLES twin);
    - WebRTCDrv (web only);
    - SDLLaunch (the game executable).
  - Editor + UCC (`ucc-bin`, the commandlet host and script compiler; the Editor only for 32-bit Linux).
- **Not built, kept in the tree as reference:** D3DDrv, WinDrv, Window, Launch, XboxDrv, XboxAudio, XboxLive,
  XboxLaunch, UIX, UnrealEd, UnrealUtil, Bundler, DebuggerLaunch, FNameDebug, GUIDesigner, XboxAssetConversion,
  SDLDrv (UT2003-era SDL 1.2).
- **Script-only packages:** their shipped `.u` are used, except GUI, UCGameplay and UCMenu, which are rebuilt.
- **Targets:**
  - native Linux 32-bit (the reference) and 64-bit;
  - Windows i686/x86_64 (MinGW, run under wine only);
  - web (Emscripten, wasm32);
  - arm64 (preset only).

## Milestones

- **Done and verified on main:**
  - M0-M14;
  - Phase A (merges);
  - Phase B: O-1..O-5, O-7..O-10;
  - Phase C (web product);
  - Phase D: D-1 scripts, D-2 UI box, D-3 saves, D-4 names, D-5 text entry, D-6 mouse, D-7 console, D-8 AutoFOV,
    D-9 rebinding, script SaveConfig;
  - E-2 (web multiplayer);
  - O-11 (jitter).
- **Open:** O-6 (needs your spot), O-14 (needs your example), E-1, E-3, E-4 (stopped), E-5 (needs your go-ahead),
  M15 content polish, M16 original Xbox assets, M17 close-out.
  *(Superseded (2026-10-09/10): see section 60, 00 (milestones).)*

## The rules every change followed

- Epic's code is changed as little as possible, and never by defining `_XBOX`, `XBOX_ASSET_FORMAT` or `LIN_LOAD`.
  Where the PC needed an Xbox-only path (post-processing, projectors, shadows), the gate is lifted through a named
  capability of the render device (`FRenderCaps`), and the PC branch is kept.
- Removing an effect is never a fix. The community's "fixed maps" are reference only.
- Behaviour that is tuning or mapping is ini data. The Xbox value is the default unless you chose otherwise.
- Every visible feature has a repeatable test in `tools/dev/`. The Xbox oracle, xemu, is a last resort.


---

# 10. Platform, build and runtime

Scope: everything that makes the engine compile, link, load and run on Linux, Windows (MinGW) and the Web, except the
renderer, input/UI/scripts, audio/movies and networking (other sections). Baseline `7633696` (import of Epic's
tree); code as of main `4813dd9`, read from the snapshot `/home/ion/WorkUC2/engine-uc2-report`. Line numbers are from
that snapshot. The frontend (`ufrontend`) was not read for this section: where it matters the text says what the
notebooks and LOG.md record about it.

## The things to know

1. **One tree, three platforms, the same code paths.** Epic's Win32/Xbox tree is built with GCC 15 (`-m32` by
   default), Emscripten 5.0.7 (wasm32) and MinGW-w64 13 (i686 by default). 32-bit Linux is the reference: its
   pointer width and struct layout equal wasm32 and i686 MinGW, and the shipped script packages (`.u`, version 151,
   licensee 0) were compiled for 32-bit pointers. Every package is a static library linked whole into the
   executable (`__STATIC_LINK`, as the Xbox XBE did). `_XBOX`, `XBOX_ASSET_FORMAT`, `LIN_LOAD`, `LIN_SAVE` and
   `USE_LINEAR_LOADING` are never defined; Xbox-only paths we need are lifted behind named capabilities.
2. **Core's platform layer is not Epic's MSVC one.** `Core/Inc/UnGnuG.h`, `Core/Src/UnUnix.cpp` and
   `Core/Inc/FFileManagerUnix.h` come from engine-ut2004 (the sibling UT2004 port) with UC2's platform contract added
   back; threading is new pthread code (`UnThreadingPosix.*`). Epic's `UnVcWin32.*` and `UnThreadingWindows.*` stay in
   the tree, unbuilt. MinGW uses the same GCC layer with `_WIN32` branches, not the MSVC layer.
3. **We edited 136 of Epic's files** (`git diff --name-status 7633696 4813dd9 -- Source`, status M), and added 187 new
   ones (127 of them the GL driver). 95 of the 136 are porting fixes only (compile, portability, 64-bit, bugs found
   while porting), 18 mix a porting fix with another section's change, 23 belong to other sections only. The full
   table, with a reason per file, is at the end of this section.
4. **Struct layout is the hazard, and one tool guards it.** Script and C++ must agree on every native class layout.
   Guards: `#pragma pack(4)` on hand-written mirror headers and `GCC_PACK(4)` tags in generated ones; at 64-bit a
   table of 50 "placeholders" for script `int`s that hide C++ pointers; `ucc-bin verifyclasses` (32-bit: 713 size and
   offset checks; 64-bit: 400 classes, 6,578 scripts scanned); and `UGameEngine::LoadMap` still runs Epic's own
   check, fatal on mismatch. `tools/dev/layout-diff.py` and `dwarf-layouts.py` compare whole-engine layouts across
   compilers.
5. **The web build is a threaded wasm module.** The engine runs on a worker (`PROXY_TO_PTHREAD`), yields to the browser
   once per frame through Asyncify, catches C++ exceptions through JavaScript (not wasm) exceptions, reads game
   files through a WasmFS JS-implemented backend the frontend's glue provides, and has an explicit 2 GB memory
   ceiling. The artifact names are fixed: `SDLLaunch.js`, `.wasm`, `.data`, `meta.json`.
6. **What has and has not been run.** Linux x86 32-bit and x86_64: fully (maps, client, GL, audio, movies, controls,
   network). Windows: only under wine 10 (never on real Windows). arm64: a preset and a toolchain file exist; no
   notebook records a configure or a build. Web: engine side by Node and by the frontend's own browser specs
   (boot, audio, saves, net, keys), run by the agents and re-run by the coordinator; I did not re-run anything.
7. **Every run goes through `bin/capped`** (memory ceiling and time limit in one systemd scope), after the incident of
   2026-10-04 when an unbounded headless-browser test took the owner's desktop down. `CLAUDE.md` makes this a rule.

## What is built and what stays unbuilt

`UC2_PACKAGES` in `CMakeLists.txt:408-412`; a package joins the build only if `Source/<Pkg>/CMakeLists.txt` exists
(`CMakeLists.txt:438-445`).

| Package | Role | Linux | Windows | Web |
|---|---|---|---|---|
| Core, Engine, IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI | Epic's native packages, all code kept | yes | yes | yes |
| UC2ALAudio, UC2Movie | our audio device (OpenAL Soft) and movie player | yes | yes | yes |
| SDL2Drv, SDLLaunch | our SDL2 client/viewport and the launcher (`uc2.bin`, `uc2.exe`, `SDLLaunch.js`) | yes | yes | yes (Emscripten's SDL2 port) |
| UCC | commandlet host (`ucc-bin`, `ucc.exe`, `ucc-bin.js` for Node) | yes | yes | Node only |
| Editor | Epic's script compiler (`ucc-bin make`), header exporter | 32-bit Linux only | no | no |
| UC2GLDrv | our OpenGL render device | yes | yes | yes |
| UC2GL4ESDrv | its GL4ES binding (GLES 2 / WebGL 1) | yes (the "twin") | no | yes |
| WebRTCDrv | browser network driver | no | no | yes |

Editor is removed for 64-bit and MinGW at `CMakeLists.txt:433-435` (its hand-written `UEditorEngine` mirrors script's
`int Pad[8]` with four pointers, correct only at 32 bits). UCC and Editor are in `UC2_NATIVE_ONLY_PACKAGES`
(`CMakeLists.txt:417`); the web build removes UCC, SDL2Drv and SDLLaunch from that list again (`:425-427`).

Kept in `Source/` but never compiled (reference or dead): `D3DDrv` (the specification of the GL driver),
`XboxDrv` (pad mapping), `XboxAudio` (XACT behaviour), `XboxLive`, `XboxLaunch`, `WinDrv`, `Window`, `Launch` (the Win32
launcher), `UnrealEd`, `UIX`, `SDLDrv` (SDL 1.2), `Setup`, `Bundler`, `XboxAssetConversion`, `GUIDesigner`,
`DebuggerLaunch`, `FNameDebug*`, `UccDepend`, `BuildProcess`, `ShaderData` (read by `tools/psh2glsl.py`, not
compiled), `UnrealUtil`. In Core: `UnVcWin32.cpp`, `UnThreadingWindows.cpp`, `FNameDebug.cpp`, `FContiguousMalloc*.cpp`,
`UnPSX2.cpp`, `UnGCN.cpp`. In Engine: `UnSVehicle.cpp` and `palette.cpp` are in no Visual Studio project and are
removed from the glob (`Source/Engine/CMakeLists.txt`). The script-only packages (Gameplay, UCInventory, UCEffects,
UCCharacters, UCMutators, UCAnnouncements, UCMenu) are compiled by `ucc-bin make` (M14), not by CMake.
Verified: `git diff --stat 7633696 4813dd9` shows no change in any of the unbuilt packages.

---

## Features

### 1. Repository layout and the import of Epic's tree (M0)

**What the player/owner sees** - nothing at run time. The owner sees a repo where `git diff 7633696 4813dd9` is the
complete, auditable list of changes to Epic's code.

**Where**
- Import: commit `7633696 import: Epic's Avaris source tree, text files only, LF line endings`, produced once by
  `tools/dev/import-epic-sources.py` (it refuses to overwrite; it is the record, not a sync tool). Rules and totals:
  `docs/SOURCE-LAYOUT.md`.
- Imported: 3,448 text files with CRLF changed to LF, 82 Windows-native project files byte for byte, 570 binaries
  skipped (of 16,451 files in the pristine tree; `reference/epic-sources.sha256`). Folders: `Source/<42 packages>`,
  `Thirdparty/{nvTriStrip,nvDXT,DirectX8}` (sources/headers only), `Engine/Config/{Default,DefUser,DefaultXbox,
  DefUserXbox,KeyBinds}.ini` (byte for byte, the baseline our config diffs against), `docs/epic/` (five build notes).
- Replaced at import: `Thirdparty/zlib` is engine-ut2004's 1.1.4 (Epic's was 1.1.3; `Thirdparty/zlib/zlib.h:40`), which
  has a CMakeLists. Copied from engine-ut2004 byte for byte: `vendor/glue/*`, `cmake/TargetArch.cmake`,
  `cmake/toolchain-mingw-w64.cmake`.
- Scaffold: `547bd73 build: CMake scaffold, presets, docs and the porting plan`.
- Policy files: `.gitattributes` (`* -text`: git must not touch line endings or encoding), `CLAUDE.md` (agent rules,
  workspace table, memory rules), `AGENTS.md` (points to CLAUDE.md), `BUILD.md`, `README.md`.
- Not in the repo, on purpose: assets (`<WorkUC2>/assets/uc2`, built by `tools/dev/mkassets-uc2.sh`), `build-*/`, anything
  from `reference/`, Epic's Visual Studio import binaries (`.lib`, icons, textures).

**How the parts fit** - the pristine tree lives outside the repo (`<WorkUC2>/epic-sources`, never edited); this repo
holds the text part; content never enters it. A change to Epic's code is therefore always visible against `7633696`.

**Why** - the layout is the repo standard of engine-ut2004 (`Source/<Package>/{Inc,Src}`, `Thirdparty/`,
`Engine/Config/`, `vendor/glue/`, `cmake/`), so the frontend and the sibling ports treat all engines alike.

**Decisions and alternatives**
- LF once at import, Latin-1 kept (sources and some `.uc` files contain 0x02 bytes). Alternative rejected: re-encode to
  UTF-8 (would change the bytes the compiler and package scripts see).
- Keep unbuilt Epic code under `Source/` instead of deleting it: D3DDrv, XboxAudio, XboxDrv are the specification of
  the new drivers; deleting anything needs the owner (`CLAUDE.md`).
- Epic's SDL 1.2 driver (`Source/SDLDrv`) and launcher were stale UT2003 leftovers; `SDLLaunch` was rewritten for SDL2
  (original in git history), `SDLDrv` stays as dead reference.

**Disadvantages, risks, deviations**
- Against `epic-sources` a diff needs `--ignore-cr-at-eol` (the pristine tree is CRLF).
- Latin-1 files: `grep` here is ugrep and silently skips them; the rule is `grep -a`, `find` is bfs. A tool that
  re-encodes a file corrupts it.
- `README.md` and the top of `BUILD.md` are stale: README says "Status: M0 (scaffold). Nothing compiles yet beyond zlib";
  BUILD.md says "Status: M0". `BUILD.md` has no section for the web build or for the artifact.
- `Engine/Config/Default.ini` is no longer a CRLF file (see feature 13): "byte copy of Epic's" is not true any more.
- The unbuilt packages are about 650 files (ShaderData 336, UnrealEd 113, Bundler 30, XboxDrv 28, and smaller ones) that nobody compiles; they are kept as
  reference and are not in the CMake build.

**Status** - M0 gate: pristine tree checked by `sha256sum -c` after all M0 work, 0 mismatches (LOG.md, 2026-10-04);
`git ls-files` holds no package, media or binary file (LOG.md, same). Measured now: 4,431 tracked files (Source
3,575, Thirdparty 703, tools 86).

**Off switch** - none.

---

### 2. Build system: CMake, presets, flags

**What the player/owner sees** - `cmake --preset native && cmake --build build-native -j12` makes `uc2.bin` and `ucc-bin`;
`source env.sh && cmake --preset wasm-perf && cmake --build build-wasm-perf` makes the web artifact; the `windows*`
presets cross-build `uc2.exe`/`ucc.exe`.

**Where**
- `CMakeLists.txt` (445 lines), commits `547bd73` (scaffold), `9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on
  every target`, `e030ac7 build: fetch SDL2 2.32`, `be2044b build: Ogg/Vorbis vendored, OpenAL Soft fetched`,
  `44932d1 build: ucc-bin as WebAssembly for Node`, `754ce47`/`1e7f700` (MinGW), `34d2e39` (Theora, UC2Movie),
  `b85f15f` (GL4ES), `2a0d111` (arm64, native64-uchar), `58cf932` (sanitizer preset, `-Werror=int-to-pointer-cast`),
  `19ca6e1` (OpenAL Soft on the web), `1538155` (PipeWire headers kept out of MinGW), `93686c0`/`24c80dc` (Editor).
  Sections:
  - options `UC2_ALLOW_64BIT`, `UC2_WEB_PERF`, `UC2_WEB_DEBUG`, `UC2_ASSETS_DIR` (`:30-34`), `UC2_SANITIZE` (`:132`),
    `UC2_UNSIGNED_CHAR_AUDIT` (`:137`);
  - architecture and pointer width: `cmake/TargetArch.cmake`, then `UC2_PTR_BYTES` (`:44-69`) because
    `CMAKE_SIZEOF_VOID_P` does not see the `-m32` demotion; the demotion itself (`:71-85`) adds `-m32` to compile, link
    and probe flags and `CMAKE_FIND_LIBRARY_CUSTOM_LIB_SUFFIX`;
  - global definitions (`:91-123`): `PLATFORM_32BIT`, `LINUX=1 __LINUX_X86__` (all non-Windows targets, wasm
    included), `__STATIC_LINK=1` (`:38`), `NO_UNICODE_OS_SUPPORT=1`, `__FILE_NOCASE=1`, `SUPPORTS_PRAGMA_PACK=1`, MinGW
    `-static -static-libgcc -static-libstdc++` (`:99-105`);
  - compiler flags (`:127-156`): `-fsigned-char -fno-strict-aliasing -pipe`; GCC adds `-fpermissive -fno-lifetime-dse
    -Wno-register`, and on 64-bit `-Werror=int-to-pointer-cast`; clang adds `-fdelayed-template-parsing`;
  - Emscripten flags (`:158-178`): one `-O` per configuration (`-O3 -g0` perf, `-O0 -g` debug, `-O2 -g` default),
    `-pthread` everywhere, and `include(UC2Web)`;
  - dependency blocks: zlib (`:184-185`), SDL2 (`:193-224`), Ogg/Vorbis (`:232-233`), OpenAL Soft native/Windows
    (`:237-288`) and web (`:300-349`), Theora (`:353`), GL4ES (`:361-400`);
  - package list and native-only list (`:408-445`).
- `CMakePresets.json` (109 lines): configure presets `native`, `native64`, `native64-uchar`, `native64-asan`, `arm64`,
  `wasm`, `wasm-debug`, `wasm-perf`, `windows`, `windows64`; matching build presets.
- `cmake/TargetArch.cmake` (engine-ut2004's), `cmake/toolchain-mingw-w64.cmake` (engine-ut2004's: picks
  `<arch>-w64-mingw32-gcc-posix` when present, finds libraries only in the cross sysroot),
  `cmake/toolchain-aarch64-linux-gnu.cmake` (new, 26 lines, M13), `cmake/UC2Web.cmake` (new, 44 lines, M4w; feature 8).
- One `CMakeLists.txt` per package in `Source/` (generated pattern: all `Src/*.cpp`, `GPackage=G<pkg>Package`,
  `ThisPackage=<pkg>`, link to Engine/Core). `Source/Core/CMakeLists.txt` lists files explicitly; `Source/UCC/CMakeLists.txt`
  and `Source/SDLLaunch/CMakeLists.txt` hold the per-platform link blocks.
- Tests: the gate of each milestone is a clean configure and build of the presets; `tools/dev/m13-cast-census.sh`
  counts pointer-width diagnostics in a build log.

Presets:

| Preset | Output | What |
|---|---|---|
| `native` | `build-native/` | Linux x86 32-bit (`-m32`); default and reference |
| `native64` | `build-native64/` | Linux x86_64 (LP64) |
| `native64-uchar` | `build-native64-uchar/` | x86_64 with `-funsigned-char` (audit build: arm's char) |
| `native64-asan` | `build-native64-asan/` | x86_64 with ASan + UBSan (no vptr/alignment) |
| `arm64` | `build-arm64/` | aarch64 cross, build-only; never configured |
| `wasm` | `build-wasm/` | web, `-O2 -g`, assertions on (dev) |
| `wasm-debug` | `build-wasm-debug/` | web, `-O0` |
| `wasm-perf` | `build-wasm-perf/` | web, `-O3 -g0`, no assertions: what ships |
| `windows` | `build-windows/` | MinGW-w64 i686, posix threads, cross |
| `windows64` | `build-windows64/` | MinGW-w64 x86_64 (LLP64) |

**How the parts fit** - the root file decides architecture, definitions and flags, then fetches third-party code with the
engine's definitions saved and restored around it (OpenAL Soft and GL4ES must not see `-fpermissive` or `-D`s; the
OpenAL block also forces `CMAKE_SIZEOF_VOID_P=4` for its own configure under the `-m32` demotion). Each package is a
static library; `UCC` and `SDLLaunch` link all of them with `--whole-archive` so class registrants and native tables
survive, plus `-Wl,-export-dynamic` on Linux because Core finds a linked-in package by `dlsym("GLoaded<Pkg>")`
(MinGW and Emscripten do not need it). A new package needs its own CMakeLists and a name in `UC2_PACKAGES`; without the
launcher link-list entry its classes are not registered.

**Why**
- Epic's Visual Studio solution and the stale UT2003 makefiles cannot build here; engine-ut2004 solved the same
  problem, so its flags and arrangement were taken (gnu++14, `-fpermissive`, `-fno-strict-aliasing`,
  `__STATIC_LINK`, `--whole-archive`).
- `-fno-lifetime-dse`: Epic calls destructors explicitly and reuses the object (`UArrayProperty::DestroyValue` runs
  `~FArray()` then `LoadConfig` adds to the same array); the compiler deletes the destructor's stores as dead and the
  next `AddZeroed` writes into freed memory. Found with ASan on the wasm build (`docs/porting/analysis/web-smoke.md`
  defect 3). GCC spelling at `CMakeLists.txt:145`, clang spelling in `cmake/UC2Web.cmake:34`.
- `SUPPORTS_PRAGMA_PACK=1` for every target: Epic's projects compile with `/Zp4` and generated headers wrap
  themselves in `#pragma pack(push,4)`; `Core.h` decided from `__LINUX__`, which is not defined yet at that point
  under GCC, so packing was silently off (M2 finding, `9b303b8`).
- `-Og` for `Core/Src/UnCorSc.cpp` and `UnProp.cpp` on GCC (`Source/Core/CMakeLists.txt`): "FFrame::Step reference
  juggling miscompiles at -O2" (found in the UE1/UT2004 ports; same VM code).
- `Z_PREFIX` (`CMakeLists.txt:184`) prefixes zlib symbols.

**Decisions and alternatives**
- 32-bit as default instead of x86_64: matches wasm32 and i686 MinGW, and the script layout. 64-bit is `UC2_ALLOW_64BIT`.
- Static libraries linked whole instead of DLLs per package (what the PC build did): the Xbox XBE did the same; no
  `GetProcAddress` tables, no `--export-all-symbols`. Cost: no per-package DLL modding.
- Emscripten flags set per configuration with `-O` blanked in `CMAKE_*_FLAGS_*` so nothing appends a second `-O`.
- Makefile generator ("Unix Makefiles") in every preset, as engine-ut2004.

**Disadvantages, risks, deviations**
- `-fpermissive` hides a class of errors: narrowing pointer-to-int casts compile as warnings (M13 had to build a census
  script for them, `tools/dev/m13-cast-census.sh`); two-phase-lookup and `Sort<INT>`/`Compare` forward-declaration
  problems pass the same way. New code of ours is compiled with it too.
- Fetched libraries use floating refs: SDL2 `release-2.32.x`, OpenAL Soft `ufront/web`, GL4ES
  `fix/cubemap-bind-active-unit` (branches/tags, `GIT_SHALLOW`), no commit pin; the first `cmake --preset` needs network;
  a moved branch changes the build. `emit-artifact-meta.mjs` records `repo@branch` for this reason, but Web.cmake does not
  pass `--dep-*` arguments, so the web `meta.json` lists only the Emscripten version (gl4es and OpenAL Soft, which the
  web build does fetch, are not recorded; the comment in the emitter, "no fetched libraries on the web yet", is stale).
- `UC2_ASSETS_DIR` (`CMakeLists.txt:33`, documented in BUILD.md) is defined and read by nothing.
- `-Og` on the two VM files costs script speed on GCC builds (native, MinGW); not measured. Clang (wasm) has no such
  rule.
- The `wasm` preset's description says "with DWARF"; the dev link uses `-g2` (names only) on purpose
  (`Source/SDLLaunch/cmake/Web.cmake:12-22`: DWARF made a 45 MiB module that did not link in 3 GB).
- emcc/clang crashed with a signal at `-j10`/`-j12` (UnModel.cpp, UnDemoPenLev.cpp, UnRenderBSP.cpp); re-running the same
  build continues. Not a source problem (notebooks), but not understood either.
- ASan, UBSan and arm64 builds are opt-in; none runs by default.

**Status** - M0 gate (presets configure), M1 (`libCore.a`, 0 errors on both compilers), M2 (8 libraries and `ucc-bin`
link, 0 undefined, 0 multiply defined; `ucc-bin` 45 MB), measured, LOG.md 2026-10-04. Each later merge logs
"native/windows/wasm-perf build" green (A4-A13 entries). `native64` first link: 6 min 10 s, 0 errors (`64bit.md` step 1).
`arm64`: not verified (never configured, `BLOCKERS.md`).

**Off switch** - the options above; `-DUC2_SANITIZE=`, `-DUC2_ALLOW_64BIT=OFF`.

---

### 3. Third-party libraries, fetched libraries, `vendor/glue`

**What the player/owner sees** - sound and movies need these; `uc2.exe` ships with `OpenAL32.dll` beside it; nothing else
to install on Linux except an OpenAL runtime that is built with the game.

**Where**
- Vendored in `Thirdparty/` (never under `Source/`):
  - `zlib` 1.1.4 (13 sources, `CMakeLists.txt`): used by `Core/Inc/FFileManagerGeneric.h` (compressed-file reads), so
    Core links it PUBLIC; Core's `.cpp` files do not use it.
  - `Vorbis/ogg`, `Vorbis/vorbis`: libogg 1.0 / libvorbis "20011231" (1.0 release) as vendored by engine-ut2004,
    copied byte for byte (CRLF kept), only the build-relevant parts; `README.uc2`; commit `be2044b`. UC2ALAudio decodes
    the cooked Ogg Vorbis from memory with vorbisfile.
  - `Theora`: libtheora 1.2.0 decoder only (official tarball, sha-256 recorded in `README.uc2`), no assembly, plain C on
    every target; libogg 1.0 lacks `ogg_uint16_t`/`ogg_uint64_t`, so the CMakeLists defines both as macros for it and for
    everything that links it; used by UC2Movie (M9).
  - `nvTriStrip`, `nvDXT`, `DirectX8`: Epic's headers/sources, imported for reference. No CMakeLists names any of them (only `Engine.vcproj`
    mentions nvTriStrip); the Engine code that needs nvDXT is guarded `_MSC_VER` (feature 5).
- Fetched by CMake into `build-*/_deps/` (never vendored): SDL2 `release-2.32.x` (static, X11 only with Wayland off and
  X11 linked, not `dlopen`ed; `SDL2main` only for MinGW; on the 32-bit build `/usr/lib/i386-linux-gnu` is added to the
  search paths or SDL's X11 probes link 32-bit objects against 64-bit libs and fail silently); OpenAL Soft from
  `10-p/openal-soft-web` branch `ufront/web` (native: SHARED `libopenal.so.1`, loaded with `dlopen` when the audio
  device starts, wave-file and null backends on, EAX off; Windows: built as `OpenAL32.dll`, WASAPI/DirectSound/WinMM,
  Linux backends off since the host's PipeWire headers broke MinGW (`1538155`); web: STATIC with the Web Audio backend,
  built with its own flags); GL4ES from `10-p/gl4es` branch `fix/cubemap-bind-active-unit` (static, GLES 2 back end, no
  X11/EGL of its own, no init constructor; Linux and web only; its `-O2 -w` overrides the engine's flags).
- On the web SDL2 is not fetched: `cmake/UC2Web.cmake:39-44` defines an INTERFACE target `SDL2::SDL2-static` carrying
  `-sUSE_SDL=2`, so SDL2Drv/SDLLaunch CMakeLists are identical on both toolchains.
- `vendor/glue/`: `ue_abi.h` (ABI version 1), `wasmfs-shim.js`, `webrtc-shim.js`, `proxynet-shim.js`,
  `ue_proxynet_abi.h`. All byte copies of engine-ut2004's; one commit (`547bd73`), unchanged since. Linked:
  `wasmfs-shim.js` (feature 8) and `webrtc-shim.js` (E-2). `proxynet-shim.js` and `ue_proxynet_abi.h` are not linked: the
  owner decided "WebRTCDrv ported; ProxyNetDrv not" (PLAN.md).

**How the parts fit** - `Web.cmake` reads `UE_ABI_VERSION` from `ue_abi.h` and passes it to the meta emitter; the frontend
checks the artifact's ABI against its own glue. `wasmfs-shim.js` forwards WasmFS hook calls to `Module.UE.fs.backend`,
which the host's glue injects before `main()`; on the worker thread it builds a Proxy table lazily and `importScripts`
the glue bundle `ufront-glue.js` (so the engine thread can read asset bytes synchronously) and also delivers
`ufrontExec` messages to `_ue_exec_command` on the engine thread. Without the injected `Module.UE` the shim throws
`[ue-abi] ... not injected or ABI mismatch`.

**Why** - Ogg/Vorbis/Theora and OpenAL Soft are what the cooked audio and movies need (M8, M9); SDL2 replaces SDL 1.2;
GL4ES is the engine-ut2004 route to GLES 2/WebGL 1; the glue is the frozen engine-to-frontend contract.

**Decisions and alternatives**
- Fetched, not vendored, for SDL2/GL4ES/OpenAL Soft (rule in `CLAUDE.md`), so the forks keep their history.
- OpenAL Soft built from source for Windows instead of engine-ut2004's prebuilt `soft_oal.dll`: the brief forbids
  downloading binaries.
- `dlopen` of OpenAL (never `DT_NEEDED`): a machine without it still boots, silent, and nothing of OpenAL runs before
  the engine allocator exists.
- Theora decoder only, bundled assembly left out: fewer files, same output on every target.

**Disadvantages, risks, deviations**
- Floating refs (see feature 2). The OpenAL block saves and restores CMake state around the fetch (directory
  definitions, C++ standard 20, `CMAKE_SIZEOF_VOID_P`); a CMake upgrade could break it quietly.
- The vendored `glue` files must stay identical across four engine repos; `webrtc-shim.js` must not be edited here.
- libvorbis 1.0 ignores the end granule: decoded waves are cut to their source length in UC2ALAudio (`3ab01aa`).
- PipeWire (BLOCKERS.md): the 32-bit game falls back to PulseAudio unless `libpipewire-0.3-modules:i386` is installed; a
  player needs the same package for native PipeWire.

**Status** - fetch-from-empty-directory builds were done at M4 (SDL2) and noted in the M4 gate; Windows OpenAL32.dll
imports checked (`objdump -p`, `package-windows.sh`); web OpenAL/GL4ES: browser specs (LOG.md 2026-10-06/07).
Not verified: a build with the fetch refs pointing at different commits.

**Off switch** - none for the libraries; audio can be selected off by `-nosound`, movies by `-nostartupmovies`/`-nomovie`.

---
### 4. Core's platform layer (M1)

**What the player/owner sees** - nothing directly. This is what gives the game a clock, files, threads, error handling and
a command line on Linux, Windows (MinGW) and the web. A wrong clock showed up as "Negative delta time!" in a quarter of
server runs before it was fixed (below).

**Where** (all under `Source/Core/`; commits `82c9fd3 Core: shared headers and sources compile with GCC 15 and em++`,
`9faf167 Core: portable platform layer (UnUnix.cpp rebased on engine-ut2004, pthread threading)`,
`e7fc07d Core: Unix file manager from engine-ut2004, portable word wrap, header fixes for launchers`,
`c36e3d2` notebook; later fixes named below)
- Epic's code changed:
  - `Inc/UnGnuG.h` (477 lines; the baseline was a stale UT2003 Unix header): rebased on engine-ut2004's UT2004 header,
    then given back UC2's contract, which is `UnVcWin32.h`'s PC branch. Gate or fix: platform selection for
    `__LINUX_X86__` (every non-Windows target, wasm included) and `__MINGW32__` (`:22-106`); `UT_NO_X86_INTRIN` on every
    non-x86 target (`:110`); `GCC_PACK(n)` is `aligned(n)` only (`:163`, UT2004's choice; the old UC2 header had
    `packed,aligned(n)`); `HANDLE`/`HINSTANCE` are `void*` (`:152`); `TCHAR_TO_ANSI`, `ANSI_TO_TCHAR`, `TCHAR_TO_OEM` are
    casts that drop const, as on PC, and defining `UNICODE` without `NO_UNICODE_OS_SUPPORT` is an `#error` (`:314-320`);
    `GET_VARARGS*` through `va_list` (`:202-224`; MSVC reads the stack); `appTrunc`, `appSqrt(FLOAT)`, `appInvSqrt`,
    `LARGEINT` (`:351-372`); `appCycles()`/`appCycles64()` use `rdtsc` on x86 and a microsecond counter otherwise
    (`:381-405`); `PTRINT` (`:270-276`); characters must be signed unless the audit build is on (`:284`); `_rotl`/`_rotr`
    C fallbacks (`:430-460`). Not taken from UT2004's header: its math-intrinsics block, `GBaseCycles`, `GIsMMX`,
    `GIsAltivec`, the wide-char layer, `BUGGYINLINE`, `UREG_*`.
  - `Src/UnUnix.cpp` (3,923 lines): engine-ut2004's file (UTF-8 punctuation in comments made ASCII), with: `USystem`
    replaced by UC2's (`:1115` on; Exec commands CONFIGHASH, EXIT/QUIT, DEBUG CRASH/GPF/RECURSE/EATMEM, DIR, NAMEHASH,
    VIEWNAMES, DUMPNAMES; RELAUNCH is UT2004's Unix version); `DoCPUID` on `<cpuid.h>`; `EdLoadErrorf` with UC2's body
    (`:3586`); and the UC2 platform contract at `:3603-3923`: `appGetSystemErrorMessage`, `appDebugMessagef` (log + stderr,
    no message box), `appGetLastError`, `appSystemTime64` (local-time Win32 FILETIME, `:3654`), `appSystemCmd`,
    `appContiguousMalloc/Free` (plain `GMalloc`, `check(dwAlign==4)` as on PC, `:3703`), `appMemCPUFastAccess` (no-op),
    `appSqrt(FLOAT)`, `appLogva`/`FTraceLog` under `DEBUG_LOGGING`, `appGetLanguageID` (always `LC_INT`, `:3772`),
    `ExternalContigMalloc/Free`, and `FFileStream::Read/Create/Destroy` on stdio plus the file-streaming pthread
    (`:3803-3920`, started from `appPlatformInit` at `:3221`; if the thread cannot start `FFileStream::Destroyed=2` so
    `appExit` does not wait forever). Kept from UT2004 although UC2 does not declare them: clipboard helpers,
    `appGetTime`, MinGW/Android/MacOSX branches (eases later 3-way merges).
  - `Inc/FFileManagerUnix.h` (932 lines, new): engine-ut2004's Unix file manager (`FFileManagerLinux.h` is now a 3-line
    shim: `typedef FFileManagerUnix FFileManagerLinux;` so the launchers keep their include and class name). UC2 changes:
    no load statistics; `GetUserDir()` a plain member (UT2004's virtual would change UC2's vtable); Windows `FindFirstFile`
    wildcard semantics in `WildcardMatch` (`:831`) and `SearchDirectory` (`:864`); `Exists()` also true for directories
    (`:604`); `UT_MKDIR` without a mode on `_WIN32` (`:24-28`); `O_BINARY` on every `open` (MinGW opens in text mode
    otherwise, `:32-35`); `USERPROFILE` for the user directory on Windows; `-usehomedir` opt-in (`:727-760`, default no
    home directory).
  - `Inc/FFileManagerGeneric.h`: vendored zlib by path; POSIX `stat` in `CompareFileTimes`; `DeleteFiles` cuts the pattern at
    the last `\` or `/` (commit `10b802f`: with only `\` nothing was deleted on POSIX and deleting a profile never worked).
  - `Inc/UnFile.h`: `check`/`checkSlow` were `__noop`, an MSVC intrinsic; now `((void)0)` off MSVC (`:155-176`). `NEW T[n]`
    placement form `operator new[](size_t,const ANSICHAR*,INT)`; sized `operator delete(void*,size_t)` and
    `delete[]` (C++14 sized deallocation would otherwise hand GMalloc memory to `free`); `UT_REPLACEMENT_ALLOC`
    (`always_inline`, `:735-743`) so the replacement global `new`/`delete` are never emitted out of line (otherwise the
    linker routes the whole program, including Emscripten's own start-up allocations, through GMalloc).
  - `Inc/UnMath.h`: x87 `ASMTransformPoint/Vector` only `ASMLINUX && !UT_NO_X86_INTRIN`, and `TransformPointBy/VectorBy`
    take the C path on GCC (`#elif 0 //ASMLINUX`, as UT2004's shipped Linux code: "apparently broken --ryan").
  - `Inc/UnTemplate.h`, `UnMem.h`, `UnName.h`, `UnType.h`, `UnObjBas.h`, `UnUnix.h`, `Core.h`, `UnAnsi.cpp`, `UnGUID.cpp`,
    `UnName.cpp`, `UnObj.cpp`, `wordwrap.*`: compile fixes (feature 5).
  - `Src/UnMisc.cpp:2033 appInit`: no longer drops the first command-line token off MSVC (`:2052`; the portable launchers
    pass arguments only, so it ate the map URL, commit `63bb34e`); `appItoa`/`appI64toa` implemented off MSVC with
    `snprintf` (`:359`, `:382`; they were `appErrorf("Not implemented")` and the first client run died in
    `SpawnPlayActor`).
  - `Src/UnUnix.cpp:1962 appSecondsSlow`: `clock_gettime(CLOCK_MONOTONIC)` minus a once-initialised origin (commit
    `1321d55`). engine-ut2004 accumulated `gettimeofday` differences in two statics; UC2's file-streaming thread and the
    game thread raced on them and the clock stepped backwards.
  - `Src/UnUnix.cpp:2075 appSleep`: on Emscripten `emscripten_sleep` only when `emscripten_has_asyncify()`, else
    `usleep` (commit `5bd7f07`); `:3015` no `CPUSPEED=` needed where there is no cycle counter (clock is microseconds,
    `GSecondsPerCycle` pinned to 1e-6). `:2835-2843` writes into the 32-character `GMachineCPU` are bounded (commit
    `a6c8edc`; UT2004's code overflowed it, found by ASan).
- New code of ours: `Inc/UnThreadingPosix.h` (199 lines), `Src/UnThreadingPosix.cpp` (786 lines): pthread versions of
  every class in `UnThreadingBase.h` (`FCriticalSectionPosix` recursive mutex, `FEventPosix` with manual/auto reset and
  Pulse, `FMutexPosix`, `FSemaphorePosix`, `FSynchronizeFactoryPosix`, `FQueuedThreadPosix`, `FQueuedThreadPoolPosix`,
  `FRunnableThreadPosix`, `FThreadFactoryPosix`, and the `GSynchronizeFactory`/`GThreadFactory` globals that
  `UnThreadingWindows.cpp` defines on Win32). `Inc/UnMovieBase.h` (movies, other section) holds `FMovieThread`/`FMoviePlayer`
  moved out of `UnVcWin32.h`.
- Launcher duty: `SDLLaunch.cpp:88-89,502-503` and `UCC.cpp:112-117,531-532` install `FSynchronizeFactoryPosix`/`FThreadFactoryPosix`
  before `appInit` (it creates critical sections); `-rdynamic` and whole-archive linking (feature 2).
- script / ini / frontend: none.
- Tests: `nm -C --defined-only build-native/Source/Core/libCore.a` shows exactly one strong definition of each listed UC2
  symbol (core-port.md Step 5); the `tools/dev/map-census.sh` census and every later run use this layer.

**How the parts fit** - the compiler picks `UnGnuG.h` (`Core.h` by `__GNUG__`); `Core.h` includes `UnUnix.h` for
`__UNIX__ || __MINGW32__`; the implementation is `UnUnix.cpp` for every non-MSVC target; the file manager the launcher
instantiates is `FFileManagerUnix`; threading is the Posix classes. Epic's `UnVcWin32.*` was an MSVC-only layer with inline
`__asm`, `#pragma`s and SEH; its UC2-specific functions (the Scion additions Epic's UT2003-era Unix layer lacks) were
re-created one by one in `UnUnix.cpp` from the PC branch, replacing each Win32 call with its POSIX equivalent.

**Why** - UC2's own `UnGnuG.h`/`UnUnix.cpp` were UT2003-era leftovers with no `appContiguousMalloc`, `appSystemTime64`,
`appGetLanguageID` and the rest, so Engine did not compile (1,193 errors in 107 files at the baseline, `engine-baseline.md`;
the biggest classes were `appTrunc`/`appTan` undeclared 225 each, `__noop` 161, packed-field binding 212). engine-ut2004's
layer was already GCC/Emscripten/MinGW-proven for the same UE2 code base.

**Decisions and alternatives**
- Rebase on engine-ut2004's `UnUnix.cpp` rather than port Epic's `UnVcWin32.cpp`: the latter is MSVC-only; the former
  existed and ran.
- Threading as a new pthread file implementing Epic's abstract `UnThreadingBase.h` interfaces, rather than rewriting the
  callers (`UnMisc.cpp`, `UnMaterial.cpp`, the movie thread): small, and Win32 semantics (recursive critical section,
  manual/auto-reset events, PulseEvent) are reproduced exactly on top of mutex and condition variable. Differences are by
  necessity: no `TerminateThread` (a thread that does not stop is detached and its handle leaked), no `SuspendThread` (logs a
  warning), priorities and affinity are recorded only, a dying queued thread does not call `ReturnToPool`.
- `GCC_PACK` stays `aligned(n)` and layout is fixed with `#pragma pack` instead (feature 6).
- Contiguous (GPU) allocator: plain GMalloc, as the PC build; `FContiguousMalloc*.cpp` stays unbuilt.
- `TCHAR` is ANSI on every target (Epic removed Unicode in UC2: "jg -- Removed unicode support", `Core.h`); never
  `-fshort-wchar` and never `UNICODE`.
- x86 asm to C (the replacements, all in Epic's files): `FloatToInt` becomes `lrintf` on every target that is not 32-bit
  x86 (`Engine/Src/UnRenderStaticMesh.cpp:18`, `UnRenderBatch.cpp:1197`); the two `F32vec4` SSE blocks in
  `Engine/Src/UnRenderUtil.cpp:45,186` are `#if __HAS_SSE__` (an MSVC-only switch) so GCC runs the scalar loop that was the
  `else`; the x87 transforms in `UnMath.h` (above); `rdtsc`/`cpuid` only on x86 (`UT_NO_X86_INTRIN` otherwise). The inline asm
  bodies of `ASMTransformPoint/Vector` remain in `UnMath.h` for x86 builds but nothing calls them.

**Disadvantages, risks, deviations**
- Bugs in Epic's code that were left (core-port.md "Suspected real bugs"): `USystem::Exec DIR` has a fixed 512-entry buffer;
  `appSystemCmd` tests `Code<=32` (ShellExecute's convention) on a `system()` result so success (0) reports "CmdFailed";
  `TMultiMap::operator=`/`Add` name `TMapBase<TK,TI>` instead of the 3-argument form; `TScopePointer` frees with `delete` what
  `NEW TCHAR[]` allocated; `UnThreadingBase.h` `FRunnable`/`FRunnableThread` lack virtual destructors; `FQueuedThreadWin::Run`
  calls `ReturnToPool` while the pool holds the thread-queue lock (the Posix version skips it); `FFileStream::Destroyed` is a
  plain `INT` polled across threads; `UObject::IsValid`/`GetFullName` test `this` against NULL (`UnObj.cpp:235`) and GCC/clang
  may delete such checks at -O2: `-fno-delete-null-pointer-checks` is not set. None has failed in a run.
- Not adopted from UT2004: its 32-byte allocation padding on 64-bit in `FMallocAnsi` (no padding exists in
  `Core/Inc/FMallocAnsi.h`); UC2 relies on the native-size guard instead (feature 12).
- The file-streaming thread polls every 1 ms forever (`usleep(1000)`) and nothing in UC2 creates a stream outside Core yet.
  Decision for the web: kept (pool slot).
- `appBaseDir` is the working directory plus `/` (UnUnix.cpp), not the executable's directory: the game must be started
  with `System/` as working directory on every platform.
- `UnUnix.cpp` carries Mac, PSX2 and GCN branches nobody builds.

**Status** - measured: M1 gate `cmake --build ... --target Core` 0 errors on GCC 15 `-m32` and emcc (LOG.md 2026-10-04);
symbols check; M3 census 58/58 uses it. The clock race: "Negative delta time!" gone after the fix (runtime-port.md item 8; the
rate before, "about a quarter of runs", is the agent's count). Not verified: the Posix threading classes beyond what the
engine uses (queued thread pool, semaphores): used by `UnMisc.cpp` and `UnMaterial.cpp` only.

**Off switch** - none. `-usehomedir` opts in to a home directory.

---

### 5. Making Engine and the game natives compile (M2): the porting-fix catalogue

**What the player/owner sees** - nothing; this is the groundwork. `ucc-bin` links with 0 undefined and 0 multiply defined
symbols.

**Where** (the per-file table is at the end of this section; commits `f700305 Engine: compiles with GCC 15 (-m32) and emcc
5.0.7`, `482cfbb IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI: build natively and for the web`, `9b303b8 UCC: ucc-bin
links (M2); 4-byte struct packing on every target`). The change classes, each with its sites:
1. `__noop` (MSVC) as a compiled-out macro: `Core/Inc/UnFile.h` (`check`, `checkSlow`), `Engine/Inc/Engine.h`
   (`CheckNotBool`), `Engine/Inc/UnRenDev.h` (`BEGIN_D3D_EVENT(x)` becomes `{ UC2_D3D_EVENT_NOOP` plus a variadic no-op,
   because it is used as `BEGIN_D3D_EVENT(n)(colour,fmt,...)`).
2. Implicit `int`, extra qualification on in-class declarations, two-phase lookup, `template<>`: `UnTemplate.h`
   (`FArray::Realloc`, `TScopePointer::operator=`/`&`, `this->` in `TTransArray`/`TLazyArray`/`TMultiMap`, `TPooledList`
   nested parameters renamed), `UnName.h`, `UnType.h`, `UnMem.h` (defaults moved from the friend `operator new`
   declarations to the definitions), `APawn.h`, `UnPlayerStats.h`, `UnChan.h`, `UnRenderResource.h`, `Engine/Src/UnRender.cpp`,
   `UnParticleEmitter.cpp`, `UnRenderVisibility.cpp` (explicit `PoolInstance` specialisations need `template<>`),
   `UnCamMgr.cpp` (`Compare(UViewport*&,UViewport*&)`), `FContentManager.cpp` (pointer-to-member syntax), `Editor/Src/UnVisi.cpp`,
   `IpDrv/Src/MasterServerUplink.cpp`, `Editor/Inc/Perlin.h` (`register INT`), `UnPrim.h` (namespace-scope `CompareHits`).
3. Invalid token pasting: `Engine/Src/UnStats.cpp` (10 macros: `##` before `.`, `(`, `[`, `=`; MSVC ignores it).
4. Inline functions defined in one `.cpp` and called from others: MSVC emits a callable copy, GCC/clang do not (13 unresolved at
   the first link). The `inline`/`FORCEINLINE` marker was removed (declaration and definition) from `AActor::GetGravity`,
   `IsJoinedTo`, `AddToRecentMap`; `UFont::GetCharSize`; `UReachSpec::supports`; `ULevel::IsPaused`;
   `UParticleEmitter::UpdateRelativeRot`; `FDynamicActor::UpdateSorting` (`Engine/Inc/UnRender.h`); `ALevelInfo::NotifyPauseChange`;
   `UGUIComponent::SaveCanvasState/RestoreCanvasState` (in the generated `GUIClasses.h` and its source
   `GUI/Classes/GUIComponent.uc`, so a regeneration keeps the change); free functions `FLinterp` (`UnUCRibbonEmitter.cpp`, `UnUCTrailEmitter.cpp`), `CheckCanvasClip` (`GUI/Src/UnGUICore.cpp`).
5. `FColor` converts to both `FPlane` and `FVector`: `FPlane(FColor)` is ambiguous in ISO C++. Three sites in Engine and one
   in UCElements call `.operator FPlane()` (the conversion that keeps alpha): `UnRenderStaticMesh.cpp:541`,
   `UnRenderBatch.cpp:1057,1060`, `UCElements/Src/UCElements.cpp:143`.
6. An `FString` object passed through `...`: `UnTex.cpp` (two `debugf`), `UnObj.cpp` SUPPRESS/UNSUPPRESS, `UnUnix.cpp` DIR.
   Harmless on MSVC by layout accident, undefined elsewhere.
7. A typeless constant: `UnParticleEmitter.cpp:267 static const INT MIN_ATTEN_DIFF = 0.2f;` - Epic's `static const
   MIN_ATTEN_DIFF = 0.2f;` was an `int`, so 0 and the "skip if almost equal" test never skips. Kept as shipped (typed `INT`,
   value still 0).
8. Windows-only code: `UnModelLight.cpp` nvDXT block `&& _MSC_VER`; `xbstrip.cpp` (standalone XDK sample; basic typedefs
   instead of `<D3D8.h>`, Win32 headers on MinGW, pre-standard for-scope variables, const `operator<`); `GUI/Src/GUI.cpp`
   (`<wtypes.h>` only on Windows); `GUI/Inc/UnUIXInteraction.h` (`ULONGLONG`, `HRESULT`, `BOOL`, `S_OK` off Windows);
   `UnInteraction.cpp` (literal function name where `guard()` omits `__FUNC_NAME__` on `__LINUX__`);
   `Core/Src/UnAnsi.cpp` (`copysign`, `atoll`, const results of `strstr/strchr/strrchr`); `IpDrv` sockets (feature 11).
9. Packing: `Core.h` honours `SUPPORTS_PRAGMA_PACK` from the build (it decided from `__LINUX__`, not yet defined at that
   point); hand-written mirror headers wrapped in `#pragma pack(push,4)` (feature 6, 12).
- UCC (`Source/UCC/Src/UCC.cpp`, 426 lines added): the `__STATIC_LINK` block did what `XboxLaunch/Src/Launch.cpp` does for the
  static XBE after `appInit` - clear `GNativeLookupFuncs`, add the two Core lookups, `AUTO_INITIALIZE_REGISTRANTS_*` for every
  package and `RegisterNames{GUI,UCElements,UCGameplay}`; UWeb's hand-written static header registers classes but not native
  lookups, so `FindUWebUWebResponseNative`/`FindUWebUWebRequestNative` are added (`:540-565`).

**How the parts fit** - Epic's files keep their MSVC branches; every change is either an `#if` or a form both compilers accept.
The `_XBOX`-only branches are untouched except where a named capability lifts a gate (renderer/movie sections).

**Why** - the 1,193 baseline errors (`engine-baseline.md`) were 7 classes of problem; the gate was zero errors without
stubbing and without leaving out any file of the PC project (only `UnSVehicle.cpp` and `palette.cpp`, which are in no project).

**Decisions and alternatives**
- `FPlane(FColor)` chosen as the alpha-keeping conversion on the assumption that MSVC 7.1 did the same. **Unverified
  assumption:** if lighting alpha looks wrong, `UnRenderStaticMesh.cpp:541` and `UnRenderBatch.cpp:1057,1060` are the places.
- Keep a shipped bug (MIN_ATTEN_DIFF = 0) rather than change behaviour.
- `-fpermissive` stays (feature 2) rather than a clean conformance pass: the Epic tree is about a million lines.
- No SSE path outside MSVC: a portable SSE/NEON convex-volume check is "a possible later optimisation, not a correctness
  issue" (engine-port.md).

**Disadvantages, risks, deviations**
- Removing `inline` from 13 functions changes performance characteristics: they are no longer inlined across translation
  units (not measured).
- `FloatToInt` via `lrintf` rounds to nearest in the current mode; x87 `frndint`+`fistp` did the same; no known difference.
- A transient emcc crash at `-j12` (re-run fixes it).
- "invalid use of incomplete type" in `UnObjBas.h`/`UnClass.h` and 1,128 `-Wwrite-strings` warnings remain (LOG.md M1).

**Status** - measured: M2 gate, 0 errors on both compilers for 8 libraries; `ucc-bin` links; run with no run tree it stops at
Epic's missing-ini error (as expected). Later regressions: every merge rebuilds native, windows and wasm-perf.

**Off switch** - none.

---

### 6. Loading v151 packages headless (M3): the layout guard, the bugs found by running, the census

**What the player/owner sees** - `ucc-bin server DM-Deadbolt` brings the level up and ticks; 58 of 58 maps do the same;
`ucc-bin verifyclasses` prints `all native class layouts match`.

**Where** (commits `44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u`,
`63bb34e`, `1321d55 Core: monotonic, race-free appSecondsSlow; map census tool; M3 notes`, `dcf245f`)
- Layout guard:
  - Epic's own check: `Engine/Src/UnGame.cpp:2046-2062` includes the generated `EngineClasses.h` (and the lists at
    `Engine/Inc/EngineClasses.h:10802-11012`, `#ifdef VERIFY_CLASS_SIZES`) at every `LoadMap`; `appErrorf("C++/ Script class size
    mismatch")` on a difference. This ran only once a map had loaded - too late to diagnose a failure that stops loading.
  - `ucc-bin verifyclasses` (`UCC/Src/UCC.cpp:287-331 VerifyNativeClasses`, dispatch `:594`): loads Engine, IpDrv, UnrealGame,
    UCElements, UCGameplay, GUI (64-bit also UWeb; with the Editor build, Editor) and runs the same generated lists.
    `LOG.md`: 713 checks over six packages, started at about 120 mismatching classes.
  - `tools/dev/layout-diff.py` (`8a232bd`): parses every TU of the wasm build for wasm32 and for `i386-pc-linux-gnu`
    with `-fdump-record-layouts-complete` and prints every record that differs; `tools/dev/dwarf-layouts.py` does the
    same from DWARF of two builds; `tools/dev/fix-bitfield-pack.py` (`44f81c6`) applies the GCC tagging rules to class headers.
- What was wrong (in order of discovery, each fixed in Epic's file):
  1. `Core/Inc/UnObjBas.h:457` `DECLARE_CLASS` built objects with `new(...) TClass()`: value-initialisation zero-fills a class
     without a user constructor, wiping `Class/Name/Outer/Index`. Now `new(...) TClass` (UT2004 has this).
  2. UWeb natives not found (`UCC.cpp:550-551`, above).
  3. `Core/Src/UnObj.cpp:3776 UObject::ResolveName` used `strcpy` on overlapping buffers; glibc's optimised copy corrupted
     class names (`UCDamageType` -> `UCDaocgeType`). Now `appMemmove`.
  4. C++ and script disagreed on class layout (crash in `AActor::PostLoad`: `Brush` held the float 1.0). Three causes: the
     header exporter tagged a bool `GCC_PACK(4)` when its `CPF_Transient` differed from the previous bool, which MSVC ignores and
     `UBoolProperty::Link` packs together (17 tags removed; `AActor` was 580 instead of 576); the exporter did not tag the
     first member of a class, so GCC put a small first member into the parent's tail padding (Itanium ABI), which MSVC and the
     script linker never do (183 first members tagged); and hand-written class headers needed the same rules (`UnTerrain.h`,
     `UnCameraEffects.h`, `UCCameraEffects.h`). The exporter `Editor/Src/UnEdExp.cpp` is fixed too so a regeneration agrees.
     Counted today in the generated headers: `EngineClasses.h` has 131 lines with a `GCC_PACK` tag added and 8 removed
     (`git diff`); `GUIClasses.h`, `IpDrvClasses.h`, `UCElementsClasses.h`, `UCGameplayClasses.h`, `EditorClasses.h` carry the
     rest.
  5. `UCGameplay/Inc/UCCameraEffects.h` had fallen behind `UCScreenDOF.uc`: `FirstPersonAlpha` and `ThirdPersonAlpha` were
     missing, so `AbsoluteBlurAlpha` sat at `FirstPersonAlpha`'s offset; MSVC's size check still passed. Members added.
     **A deliberate deviation from the shipped binary** - see "Disadvantages".
  6. `appInit` ate the first command-line token (feature 4); the map argument was lost.
  7. Port 1000 (the Xbox system-link port) is privileged on Linux: `Engine/Config/Default.ini` `[URL] Port=7777`.
  8. The clock race (feature 4).
  9. (M4) `FFileManagerUnix::SearchDirectory` used `strstr`, so `Localization\int\*.INT` (Xbox language codes are upper case)
     matched no `*.int` file: the registry (`UObject::CacheDrivers`) was empty and `ucc server` short names, `ucc help` and game-type
     lists found nothing. Fixed by the real wildcard matcher (feature 4); `Localize()` itself was never affected.
- ini derivation: `Core/Src/UnMisc.cpp:2118-2146` (Epic's code): `Avaris.ini` is created from `Default.ini`, and `User.ini` from
  `DefUser.ini`, when absent; the module name is `Avaris` (`GModule`, set in `SDLLaunch.cpp:493`; `ucc-bin` also), which also
  names the log (`Avaris.log`) and the product strings (`Localization/int/avaris.int`).
- Path and case: `appUnixPath` (`UnUnix.cpp:3474`) turns `\` into `/` and, with `__FILE_NOCASE` (always set), resolves case per
  path component through `find_alternate_filecase` (`:3432`) only when `access()` fails; `FFileManagerUnix` calls it for every
  open, stat, mkdir, rmdir, chdir.
- The census: `tools/dev/map-census.sh <build-dir> [secs] [workers]` (`1321d55`, `555078d`): each worker has its own run tree and
  port; a map passes when `Bringing Level ... up for play` is logged, no `Critical:` line, and it is still ticking when `timeout`
  stops it. `CENSUS_NAME`/`CENSUS_PORT` keep a second census apart.
- Config: `Engine/Config/README.md`.

**How the parts fit** - the generated `*Classes.h` headers carry both the C++ declarations and the size/offset lists; the script
linker (`UStruct::Link`) lays out the shipped `.u` properties; the check compares the two. At 32 bits every layout rule is "the
compiler's, plus the tags"; at 64 bits the link-time guard adds the placeholder table (feature 12).

**Why** - the shipped `.u` files cannot change before M14 (rebuilding them from source), so C++ must adapt. Each item above was
found by running, not by reading (runtime-port.md).

**Decisions and alternatives**
- Fix layout by tags and pragmas, not by editing script: script is the fixed side.
- The gate "ucc-bin batchexport of 17 .u" in PLAN.md was replaced by the engine's own layout lists because `batchexport` lives
  in Editor (not built then): "a more direct test of the same thing" (LOG.md).
- `Default.ini` Port 7777 (UE2 default) rather than keep 1000.

**Disadvantages, risks, deviations**
- **UCScreenDOF alpha** is a deliberate behaviour difference from the shipped Xbox binary: script sets `AbsoluteBlurAlpha` to
  `ThirdPersonAlpha` (100) or `FirstPersonAlpha` (65) per camera; with Epic's stale header native code always read 65. We
  read what the script intends. The owner later decided "depth-of-field alpha = the shipped value 65" (`DOFShippedBlurAlpha=True`
  in `Default.ini`, BLOCKERS.md O-3); that switch lives in the renderer section.
- `verifyclasses` does not check script structs on 32 bits ("does NOT check structs", web-smoke.md); struct and event-parameter
  layout is covered by `layout-diff.py`/`dwarf-layouts.py` and, at 64 bits, `m13-layout-check.py`. A C++-only record that is bulk
  `Serialize`d against a file format would not be caught; "none found by inspection of the list", "not exhaustively searched".
- `ComboMatinees/*.unr` (9 maps) are not on the package search path and are not in the census.
- Census PASS is weak on exit: `timeout` without `-k` reports 124 whatever happens after the TERM. An exit-time crash of the
  dedicated server (BLOCKERS.md "Open (M13)": `UObject::StaticExit` -> `PurgeGarbage` -> `ULinkerLoad::DetachExport`, about 2
  maps in 58, both widths) is listed as open there; the network branch fixed related exit-time use-after-frees (`006e725`), but
  no later note records whether the census crash is gone: **not established**.
- `Missing Class Editor.TransBuffer` is logged for 56 of 58 maps (Editor not built in the web and 64-bit configs; the throw is
  caught). With the Editor package linked (32-bit Linux) the class exists.

**Status** - measured: `verifyclasses` 713 checks, 0 mismatches (M3 gate); `ucc-bin Engine.ServerCommandlet CTF-Octane`, 60 s,
`Game class is 'CTFGame'`, clean shutdown; census `maps: 58  PASS: 58  FAIL: 0`, twice; repeated at 64-bit (58/58, same per-map
result as 32-bit).

**Off switch** - none. `ucc-bin verifyclasses` is a command, not a setting.

---

### 7. Client boot and the launch flow (M4)

**What the player/owner sees** - `uc2.bin` (Linux), `uc2.exe` (Windows) or the web page starts, creates a window, brings
`Entry.MyLevel` and then `Menu.MyLevel` up, opens `UCMenu.UC2MainMenu`, and ticks. (Input mapping, pads and UI are other
sections.)

**Where** (commits `e030ac7 build: fetch SDL2 2.32 (static, X11)`, `49189ba SDL2Drv: SDL2 client and viewport...`,
`3d7e719 SDLLaunch: uc2.bin, the SDL2 game launcher; portable client in Default.ini`, later `34dd131`, `8db663c`, `34d2e39`, `8ff02cd`,
`9df0ad4`, `1efa52e`, `941326a`)
- `Source/SDLLaunch/Src/SDLLaunch.cpp` (645 lines; Epic's SDL 1.2 launcher rewritten, original in `7633696`) and
  `SDLLaunchPrivate.h`. Flow in `main` (`:471`):
  1. `setbuf` unbuffered stdout/stderr, `appArgv0(argv[0])`, command line = argv[1..] joined with single spaces (no re-quoting,
     max 1,024 characters).
  2. `strcpy(GModule,"Avaris")`, `GPackage = appPackage()` (`:493`).
  3. `GIsClient=GIsGuarded=1`; Posix factories; `appInit(...)` with `FMallocAnsi`, `FOutputDeviceFile`, `FOutputDeviceAnsiError`,
     `FFeedbackContextAnsi`, `FFileManagerUnix`, `FConfigCacheIni::Factory` (`:504`).
  4. Static registration (`:513-565`): native lookups, `AUTO_INITIALIZE_REGISTRANTS_*` of every linked package (Engine, SDL2Drv,
     UC2GLDrv, UC2GL4ESDrv, UC2ALAudio, UC2Movie, WebRTCDrv, IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI), the UWeb
     lookups, `RegisterNames*`.
  5. Mode flags: `GIsServer=1`, `GIsClient = !-SERVER`, `GIsEditor=0`, `GIsScriptable=1`, `GLazyLoad = !GIsClient || -LAZY`.
     `-LOG` echoes the log to the terminal. `-BENCHMARK -SECONDS=n` runs 30 Hz steps and logs the average.
  6. `SelectRenderDevice()` (`:309`): `-NULLRENDERER`, `-OPENGL` (UC2GLRenderDevice), `-GL4ES` (UC2GL4ESRenderDevice) write
     `[Engine.Engine] RenderDevice` for the run if the device is in the build. `OverrideResolution()` (`:442`): `-640x480` ...
     `-1600x1200` written to the active client's `FullscreenViewportX/Y`.
  7. `InitEngine()` (`:371`): `ReplaceMissingDrivers()` (`:333`) replaces a `ViewportManager`, `RenderDevice` or `AudioDevice`
     the ini names but this build does not contain (with `SDL2Drv.SDL2Client`, `Engine.NullRenderDevice`,
     `Engine.NullAudioDevice`) and logs it - this is why an ini from the PC or Xbox release still boots; in the web build also
     switches `NetworkDevice=IpDrv.TcpNetDriver` to `WebRTCDrv.WebRTCNetDriver` (a browser has no sockets); `-testrendev=`;
     `Running.ini` semaphore file (deleted at clean exit); `FirstRun`; `UGameEngine` is constructed from
     `ini:Engine.Engine.GameEngine`, `UEngine::GEngine = Engine` before `Engine->Init()` (UC2-specific, the PC and Xbox
     launchers do it), `Startup time` is logged.
  8. After init: `EXEC=` file, `LOADWINDOWPOSITION`, the start-up logos (`appPlayMovieFullscreen("..\\Movies\\UC2_Logos.xmv",
     FSMOVIE_Startup, 1)`, skippable, `:604`), `MainLoop` (`CMainLoop::RunLoop`, `:170`), `GRenDev->Flush`, delete
     `Running.ini`, `appPreExit`, `appExit`.
  - `CMainLoop::RunLoop` (`:170`): tick (`Engine->Tick(DeltaTime)`; fixed 1/30 s while benchmarking or recording), tick-rate
    limiter by `appSleep` (`:219-241`; `-maxfps=<n>` caps a standalone game, added for D-6 tests), on the web the
    Asyncify yield (`:242-247`), then `Viewports(0)->UpdateInput(0,0)`, which pumps the SDL events.
  - Left out of the PC `WinMain` on purpose: config wizard, log window, installer mutex, the D3D hardware probe
    (`SetInitialConfiguration`), the exec hook with property windows. `consolecommand=` logs that it is not available.
- `Source/SDL2Drv` (input section) is the client and viewport the launcher selects. `Engine/Config/Default.ini`: `ViewportManager`,
  `RenderDevice`, `AudioDevice`, `[SDL2Drv.SDL2Client]` (copy of `[WinDrv.WindowsClient]`); `USDL2Client::StaticConstructor` gives
  `UClient` the same values as compiled defaults, because with Epic's own `Default.ini` (no such section) the render device got
  0x0.
- Tests: the M4 gate (below); `tools/dev/m10-controls.sh` runs `uc2.bin` under Xvfb for every later check.

**How the parts fit** - `SDLLaunch.cpp` is the only place that knows the list of linked packages; adding a package means a
registrant call there and in `UCC.cpp`, a CMake link-list entry and a `UC2_WITH_*` definition. The null devices are always
available, so any ini boots.

**Why** - the launcher structure is engine-ut2004's; what UC2 does at start-up comes from the PC `WinMain`
(`Source/Launch/Src/Launch.cpp`, `Engine/Inc/UnEngineWin.h`) and the static registration from `XboxLaunch/Src/Launch.cpp`.

**Decisions and alternatives**
- Module name `Avaris` so ini, log and product strings are the PC game's.
- One SDL window per viewport, as WinDrv created one Windows window per viewport. Split screen on PC is an owner question
  (BLOCKERS.md, unresolved).
- `-nullrenderer` selection is in the engine-ut2004 style (switch table), not an ini edit by the user.

**Disadvantages, risks, deviations**
- Command line is limited to 1,023 characters (silently truncated at an argument boundary).
- `ucc help` prints blank names/descriptions for native commandlets (`HelpCmd`/`HelpOneLiner` are localized properties and
  are empty on the class default object `UCC.cpp` reads). Whether the same blankness affects script reading
  `class'X'.default.<localized>` on a native class was "worth checking as soon as text is on screen"; the menus do show text, from which I
  infer it does not matter for the menu path, but no note closes the question (BLOCKERS.md).
- Default.ini suppresses the `DevUI`/`DevGUI` log names, so menu activity is not in the log by default; tests write their own
  `Avaris.ini` without those two lines.
- Ctrl+letter control characters, IME, the UnrealScript debugger, `consolecommand=` are not provided (client-boot.md).

**Status** - measured, M4 gate (LOG.md 2026-10-04): `uc2.bin -log` under Xvfb, still running at the 40 s timeout, `Opened viewport
(SDL window 1, 640x480)`, `Render device: Engine.NullRenderDevice`, 0 `Critical`, 1 `Warning` (Editor.TransBuffer); `ucc-bin server
DM-Deadbolt` by short name; `ucc-bin help`; real X key events reached `UCMenu.UC2Arena_Main`.

**Off switch** - `-nullrenderer`, `-nosound`, `-nostartupmovies`, `-nomovie`; `ReplaceMissingDrivers` cannot be switched off.

---
### 8. The web runtime (M4w, M11 runtime parts)

**What the player/owner sees** - the game runs in a browser tab: the frontend loads `SDLLaunch.js`/`.wasm`/`.data`, streams game
files on demand, and the engine boots to the menu and plays. The owner's preview runs on port 8096 (published with
`bin/publish-preview`).

**Where** (commits `44932d1 build: ucc-bin as WebAssembly for Node (exceptions caught, -fno-lifetime-dse, NODERAWFS,
pthreads)`, `5bd7f07 Core: appSleep blocks where there is no Asyncify; no CPUSPEED= needed...`, `8a232bd Engine: 4-byte packing for
the hand-written script mirrors UnTex.h and UnParticleSystem.h`, `45f7301 SDLLaunch: browser build (...), per-frame Asyncify
yield, ue_exec_command`, `51a55f0 build: explicit 2 GB wasm memory maximum`, `429c17b build: the dev web build links at -O2 with
names only`, `604a8a3 Core, Engine: a leading Key=Value token is a global option, not the map`, `81aea5f` merge,
`34b4ed2` GL4ES link flags, `19ca6e1` OpenAL Soft on the web / pool 4, `a860ac8 SDLLaunch: web ue_exec_command passes what the
engine does not take to the first viewport`, `941326a` WebRTC)
- new code of ours:
  - `cmake/UC2Web.cmake` (44 lines), included from `CMakeLists.txt:174-178`: `-sNO_DISABLE_EXCEPTION_CATCHING` at compile time for
    every target and `UC2_WEB_EXCEPTION_FLAGS` for every link (`:24-25`); `-fno-lifetime-dse` for clang (`:34`); the
    `SDL2::SDL2-static` INTERFACE target carrying `-sUSE_SDL=2` (`:39-44`).
    *(Superseded (2026-10-09/10): see section 60, 10, feature 8.)*
  - `Source/SDLLaunch/cmake/Web.cmake` (116 lines), included from `Source/SDLLaunch/CMakeLists.txt:61-64`; output `SDLLaunch.js`
    + `.wasm` + `.data` (`:10`), then `meta.json` as a POST_BUILD step (`:64-77`). Link flags:

    | Flag | Why |
    |---|---|
    | `-sENVIRONMENT=web,worker` | page plus the pthread workers |
    | `-pthread -sPROXY_TO_PTHREAD` | `main()` runs on a worker; Core's file-streaming thread and the audio threads are workers too |
    | `-sPTHREAD_POOL_SIZE=2`, later `4` (`:86`) | main + streaming thread; +2 for OpenAL Soft's mixer and event threads (a thread created with the pool empty starts only after the main thread has spun up a worker, which the audio ring would absorb as an underrun) |
    | `-sASYNCIFY -sASYNCIFY_STACK_SIZE=262144` | the blocking main loop yields once per frame; `appSleep`'s `emscripten_sleep` is the other suspension. `ASYNCIFY_IMPORTS` is not set: the default list carries `env.invoke_*`, which JS exceptions under Asyncify need |
    | `-sSTACK_SIZE=2097152` | engine-ut2004's value (Emscripten's default is 64 KB) |
    | `-sINITIAL_MEMORY=268435456 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=2147483648` | 256 MB start, explicit 2 GB ceiling (`CLAUDE.md`: never unbounded) |
    | `-sOFFSCREENCANVAS_SUPPORT "-sOFFSCREENCANVASES_TO_PTHREAD=#canvas"` | the canvas belongs to the engine worker so the GL device creates its context there |
    | `-sFORCE_FILESYSTEM -sWASMFS` + `--js-library vendor/glue/wasmfs-shim.js` + exported `_wasmfs_create_jsimpl_backend`, `_wasmfs_create_file`, `_ufront_wasmfs_register`, `_ufront_wasmfs_set_pending` | files are backend files the host creates and serves (JSImpl backend); the engine opens them with ordinary POSIX calls |
    | `-sDEFAULT_LIBRARY_FUNCS_TO_INCLUDE=['$ueWorkerBoot']` | the worker-side bootstrap hangs off a `__postset` that is only emitted if referenced |
    | exported `_main _malloc _free _close _ue_exec_command`, runtime methods `callMain stringToUTF8 UTF8ToString HEAPU8 stackSave stackAlloc stackRestore lengthBytesUTF8 PThread FS` | what the host calls |
    | `--preload-file Engine/Config/web/{Default,DefUser}.ini@/System/` | boot configuration from the tracked web set, never from the asset tree; `appInit` derives `Avaris.ini`/`User.ini` from them in the browser |
    | dev: `-O2 -g2 -sASSERTIONS=2 -sSTACK_OVERFLOW_CHECK=2`; perf: `-O3 -g0 --emit-symbol-map -sASSERTIONS=0 -sSTACK_OVERFLOW_CHECK=0` | the dev link at plain `-g` was 45 MiB and did not fit 3 GB; `-O2 -g2` is 9 MiB, about 0.5 GB |
    | with UC2GL4ESDrv: `-sGL_ENABLE_GET_PROC_ADDRESS=1 -sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=1 -sFULL_ES2=1 -sOFFSCREEN_FRAMEBUFFER` | gl4es targets GLES 2 = WebGL 1; `emscripten_webgl_commit_frame()` is a stub without `-sOFFSCREEN_FRAMEBUFFER` |
    | with WebRTCDrv: `--js-library vendor/glue/webrtc-shim.js` | the `webrtc_*` imports forward to `Module.UE.net` |

    *Superseded (2026-10-09/10): see section 60, 10, feature 8.*

  - `Source/SDLLaunch/Src/SDLLaunchWeb.cpp`/`.h` (new, compiled only under Emscripten): `UC2WebYieldFrame()` = an
    `EM_ASYNC_JS` that awaits `setTimeout(0)` (`:36-43`); `ue_exec_command` (`:50-64`), the frozen ABI export (`vendor/glue/ue_abi.h`):
    runs a console command and returns its captured output (a static `FString` valid until the next call; `""`, never NULL, before the
    engine exists), and since `a860ac8` passes what the engine does not take to the first viewport as a typed console line;
    `UC2WebSetEngine`.
  - `Source/SDLLaunch/cmake/emit-artifact-meta.mjs`: engine-ut2004's, `targetId uc2`, `engineFamily UE2`, `buildConfig wasm-integrated`,
    `abiVersion` from `ue_abi.h` (1), `loaderStyle global-module`, provenance (`repo engine-uc2`, git sha resolved at build time),
    `checksums` of `.wasm`, `.js` and `.data`, descriptive `buildFlags`, `exports.symbolContract 1`.
  - `Source/UCC/CMakeLists.txt:57-81`: the Node variant `ucc-bin.js`/`.wasm`: `-sENVIRONMENT=node -sNODERAWFS=1 -pthread
    -sPROXY_TO_PTHREAD -sPTHREAD_POOL_SIZE=2 -sEXIT_RUNTIME=1 -sSTACK_SIZE=2097152 -sINITIAL_MEMORY=256MB -sALLOW_MEMORY_GROWTH=1
    -sMAXIMUM_MEMORY=2GB -sASSERTIONS=0 -sSTACK_OVERFLOW_CHECK=2 -g` (assertions off because SOCKFS's "select() assumes fd < 64"
    aborts `server` when NODERAWFS hands out real descriptors). It is the wasm32 test vehicle: same objects as the browser build.
- Epic's code changed:
  - `Core/Src/UnUnix.cpp:2075-2095 appSleep` (above) and `:3015` (no `CPUSPEED=`).
  - `Engine/Inc/UnTex.h`, `UnParticleSystem.h`: hand-written script mirrors wrapped in `#pragma pack(push,4)`: wasm32 aligns
    `DOUBLE`/`QWORD` members to 8, i386 and Epic's `/Zp4` to 4. Before the fix `UTexture` was 208 instead of 204
    (`__LastUpdateTime`) and `FParticleTrailData` 32 instead of 28 (`Time`).
  - `Engine/Src/UnGame.cpp:338` and `Core/Src/UnMisc.cpp:1967 appIsGlobalOptionToken`: a leading `Key=Value` token is a global option,
    not a map (the launcher passes the player name first; the engine tried to load a package called `Name=Player`).
  - `SDLLaunch.cpp:242-247, 582, 613`: the yield and `UC2WebSetEngine`; `ReplaceMissingDrivers` (above).
- Frontend (per the notebooks, not read): the glue `ufront-glue.js` injects `Module.UE` and the FetchFS/WasmFS backend; `targets/uc2`
  (target.json, manifests, precache) and the package reader for versions 145-151; e2e specs `uc2-wasm-boot`, `-audio`, `-saves`,
  `-keys`, `-net`. The host posts `ufrontExec` messages that `wasmfs-shim.js` runs through `_ue_exec_command` on the engine thread
  (calling it on the main thread would execute engine code where there is no GL context).
- Tests: `tools/dev/layout-diff.py build-wasm` (159 TUs, 3,244 records: 25 differ, none script-visible, each checked; remaining risk
  "a raw bulk Serialize of a C++-only record against a file format"); `node ucc-bin.js verifyclasses` and `server`; the frontend specs.

**How the parts fit** - the native side runs the whole engine on one worker, calling `UC2WebYieldFrame()` when its own tick limiter did not
sleep; each yield unwinds the stack with Asyncify and resumes from a 0 ms timer task, which is when SDL's input events, the host's
`ue_exec_command` messages and the compositor run. Game bytes come from the host's JS backend through WasmFS; the engine itself does
nothing web-specific when it opens a file. Without the host glue the shim throws at the first file read; without `ue_exec_command`
the host cannot flush the config (feature 9) or run commands; without the `.ini` preload there is no `Avaris.ini` to derive.

**Why** - the web build must run the same engine code as native. A tab is closed, never shut down; browsers need a yielding main loop;
the module is threaded because the engine needs a streaming thread and OpenAL Soft needs its own.

**Decisions and alternatives**
- **JavaScript exceptions, not `-fwasm-exceptions`.** Emscripten 5.0.7 fails `wasm-opt` with `-fwasm-exceptions -sASYNCIFY`
  (`Module::getFunction: __asyncify_get_call_index does not exist`); JS exceptions with Asyncify work (measured with a throw, a
  catch and an `emscripten_sleep` inside the try). UC2's loaders throw on a missing package/object and catch a few frames up; 56 of 58
  maps import the unbuilt `Editor.TransBuffer`, so that throw is ordinary control flow and Emscripten's default (catch blocks
  compiled away, first throw aborts) cannot be used.
- **engine-ut2004's "Asyncify fail-soft loader patches" were not ported** (they compile catching off and patch every loader
  to return soft); real catching was used instead. Confirmed in the code: no `FreshLinker` or fail-soft branch exists in Core or Engine.
  *(Superseded (2026-10-09/10): see section 60, 10, feature 8.)*
- Keep the file-streaming thread on the web (idles at 1 ms; nothing creates streams yet).
- Timer task (`setTimeout(0)`) rather than a MessagePort task for the frame yield: engine-ut2004 measured (ufront 2.43) that a timer task
  also gives the compositor a rendering opportunity between frames.
- 2 GB maximum, not the toolchain default and not the 4 GB wasm32 ceiling: the owner's rule, with room to raise it "deliberately,
  with a measurement" (Web.cmake comment). The frontend keeps multi-GB asset residency in JS typed arrays, outside linear memory.
- Dev builds drop DWARF at link (`-g2`); debug the web engine as `wasm-debug` under Node, not in a browser.

**Disadvantages, risks, deviations**
- Cost of JS exceptions with Asyncify (an `invoke_*` trampoline around every call that can throw, Asyncify instrumentation around
  those) is called "an M11 performance question" in the notebook; no measurement of it is recorded.
- Threaded-only: the page needs cross-origin isolation (SharedArrayBuffer) and OffscreenCanvas; `meta.json` says so. A browser or
  context without them cannot run the build.
- `meta.json` is less informative than the template intends: `buildFlags` is hand-maintained and omits the pool size and the GL4ES flags;
  `toolchain` lists only the Emscripten version although gl4es and OpenAL Soft are fetched for the web build (Web.cmake passes no
  `--dep-*`); the file's header comment still says "no fetched libraries on the web yet".
  *(Superseded (2026-10-09/10): see section 60, 10, feature 8.)*
- Pool size is a hard-coded count (4) tied to what OpenAL Soft's web backend starts; a new thread user needs the number raised.
- Node cannot be used to test networking: its socket emulation has no `SO_BROADCAST`, so `ucc-bin.js server` loads the map and then
  stops with `Dedicated server can't listen` (accepted for W0).
- Memory: the browser's PSS peak was 1.5-1.8 GB in the specs (the whole browser, not only the module's linear memory, which starts at 256 MB and may
  grow to the 2 GB maximum). The notebooks give no figure for the linear memory the largest map needs, so the headroom under 2 GB is **not established**.
- `ue_exec_command` shares one static result string: not re-entrant.

**Status** - measured / recorded: W0 `node ucc-bin.js verifyclasses` -> `All native class layouts match`; `server DM-Deadbolt` -> level up
for play (then the expected listen failure). W1 artifact (before the `-O2 -g2` dev link of `429c17b`): `SDLLaunch.js` 782 KB, `.wasm` 47 MB (dev, DWARF), `.data` 18 KB; after it the dev module is about 9 MiB. W2, the first browser boot,
through the frontend's own harness under `bin/capped` and the memory guard: `1 passed (11.1s)`, `crossOriginIsolated: true`, menu level
up, browser peak 1.5 GB, run peak 3.2 GB, machine peak 9.4 GB (LOG.md 2026-10-04). Later coordinator runs: `uc2-wasm-boot` 3/3 then 4/4,
`uc2-wasm-audio` 3/3, saves 1/1, keys 1/1, net 2/2 (LOG.md 2026-10-06..08). Published to the preview 2026-10-07 09:35.
Not verified here: frame rate and load time in a real browser on a user's machine; browsers other than the headless Chromium of the frontend's
specs.

**Off switch** - none: the web build is a different artifact. `-sMAXIMUM_MEMORY` is a link flag in two files.

---

### 9. `-ini:` launch overrides, `FLUSHCONFIG` and the config-cache flush fix (C-1, runtime part of C-2)

**What the player/owner sees** - the web launcher (and any command line) can set an ini value for a launch:
`-ini:User:[DefaultPlayer]:Name=Anubis`, `-ini:System:[Engine.Engine]:Language=int`. The player name the launcher sends becomes the
engine's default name. Settings the game only sets in memory are no longer lost.

**Where** (commits `7bfdd62 Core: port engine-ut2004's -ini:<File>:[<Section>]:<Key>=<Value> command-line overrides (appApplyIniOverrides)`,
`f14ba00 Core: write the dirty config files before the cache is dropped (RemoveNames); tools: c1-ini-overrides.sh`,
`61c9f48 Engine: FLUSHCONFIG console command ... c1-saves.sh`, `604a8a3` for the global-option token)
- Epic's code changed:
  - `Core/Src/UnMisc.cpp:2002 appApplyIniOverrides` (about 60 lines with its comment) and its call in `appInit` right after `GConfig->Init` (`:2156`), before the
    language lookup, so even `Engine.Engine Language` can be overridden; declaration `Core/Inc/UnFile.h:314`. Each `-ini:` token is split
    into file, section, key, value; the file part uses the config cache's generic names (`System` -> `Avaris.ini` = `GIni`, `User` ->
    `User.ini`) or a literal ini name; the value is stored with `SetString` (the ini is marked dirty). A token that starts with `-ini:`
    and cannot be parsed is logged (`Warning: Ini override ignored (expected -ini:<File>:[<Section>]:<Key>=<Value>): <token>`) and
    skipped, as are empty file/section/key; engine-ut2004 skips silently. A value with spaces must reach the process as one token in double
    quotes (the launchers join argv with plain spaces).
  - `Core/Src/UnMisc.cpp:1967 appIsGlobalOptionToken` (`Key=Value` with a non-empty key and no `?` before the `=`), used by
    `UnGame.cpp:338` so a leading `-ini:...` or `Name=x` token still boots the default map.
  - `Core/Src/UnObj.cpp:5459-5495` (`UObject::RemoveNames`, called by every `PurgeGarbage`): `GConfig->Flush(0)` (`:5476`) before
    `GConfig->UnloadAll()` (`:5493`), `#ifndef _XBOX`. Epic's own `Flush` at the top of the function (`:5463`) was `#if 0`.
  - `Engine/Src/UnEngine.cpp:308-322`: `FLUSHCONFIG` console command first in `UEngine::Exec`: `GConfig->Flush(0)` and the reply
    `ufront: flushed config to disk (ok)`.
- script: none. Frontend: `ufrontend/packages/glue/src/config/ini-identity.js` `iniOverrideArg` emits the tokens;
  `ufrontend/packages/frontend/src/shell/boot.ts:752` calls `ueExecResult('flushconfig', 4000)` through `ue_exec_command` before it reads the
  inis (per web-ini-saves.md; not re-read). Frontend commit `f90b70f` (LOG.md).
- Tests: `tools/dev/c1-ini-overrides.sh` (30 checks, 0 failures at `logs/c1/c1-ini-run4.txt`): a system key and a user key reach the class
  defaults (proven by the console command `get <Class> <Property>`, not by the engine's own log line), a quoted token with spaces, `?` and `=`
  arrives whole, an `-ini:` token as the first token still boots to the menu, seven malformed tokens are logged and ignored without
  stopping the good token after them, ucc-bin applies them too, the values are in the ini after a SIGTERM exit, `FLUSHCONFIG` writes
  them to a SIGKILLed client. `tools/dev/c1-saves.sh` and `c1-web-saves.sh` (feature 10).

**How the parts fit** - the native side reads the switch once in `appInit`; both launchers (`SDLLaunch`, `UCC`) go through `appInit`, so one call
covers both. Without the frontend calling `flushconfig` the web persistence daemon would not see values that were only set; without the
`RemoveNames` fix the values would be dropped at the next garbage purge on every platform.

**Why**
- The launcher's "Player Name" box emits `-ini:User:[DefaultPlayer]:Name=<name>`; nothing read it until the switch was ported.
- The test found a shared-code bug: `UObject::RemoveNames` ends with `GConfig->UnloadAll()` (the cache is keyed by `FName`, whose names
  may be invalid afterwards), and the `Flush(0)` above it was compiled out. `SetString` only marks the ini dirty; only `SaveConfig` flushes.
  So every value set but not yet written was dropped at the next level change or at `StaticExit` (which runs BEFORE `appExit`'s flush):
  the `-ini:` values, `FURL::SaveURLConfig`'s `[DefaultPlayer]` values after every map load, the launcher's resolution switches. Found
  as 2 FAIL in the first ucc write-back check (`logs/c1/c1-ini-run2.txt`). The owner's report "saving doesn't seem to work" (web) is the same hole
  for anything held in the inis.
- A browser tab is closed, never shut down, so nothing may wait for an exit flush.

**Decisions and alternatives**
- Values are WRITTEN into the ini (`SetString` marks it dirty), not applied for this run only: engine-ut2004's deliberate trade (one mechanism;
  the web daemon keeps the ini). There is no launch-only override.
- Log malformed tokens (deviation from the template's silence) because an unrecognised switch is otherwise invisible.
- `FLUSHCONFIG` is the template's command, kept under the same name so the shared frontend daemon needs no UC2-specific code.
- `ServerName` cannot be the test key and cannot be seeded by `-ini:`: the profile code copies the profile's own `ServerName` over
  `GameReplicationInfo.default.ServerName` at start-up (`PlayerProfile.uc:764`).

**Disadvantages, risks, deviations**
- A value with a double quote inside it ends the token. The command line is limited to 1,023 characters.
  *(Superseded (2026-10-09/10): see section 60, 10, feature 9.)*
- The `RemoveNames` flush is a behaviour change on every platform: the ini files are written at every level change when dirty (not on the
  Xbox build). It writes nothing when nothing is dirty.
- An override persists: a launcher that sends `Name=` once overwrites the ini value for later runs without the switch.
- Not verified: the browser round trip of `flushconfig` and the daemon's 5-second scan by the engine side alone; the later browser `saves` spec
  (LOG.md D-3 entry) exercised it for profiles.

**Status** - measured: `c1-ini-overrides.sh` 30 PASS, 0 FAIL; coordinator's re-run on main "ALL PASS" (LOG.md 2026-10-07); `c1-saves.sh` 11 PASS;
under wasm/Node the three `-ini:` tokens and the write-back work (`c1-web-saves.sh` 16 PASS).

**Off switch** - none for the overrides (they only act when the switch is on the command line). `FLUSHCONFIG` acts only when called.

---

### 10. What the web build persists: flat profile files (C-2)

**What the player/owner sees** - on the web a profile and the inis survive closing the tab; natively the profile is a directory per profile.

**Where** (commit `22e9dbf Engine: the web build keeps a profile as one flat file Profiles/<name>.PRO ...`, `61c9f48` for the native inventory)
- Epic's code changed: `Engine/Src/ULoadSaveManager.cpp` (41 lines added): `UC2_FLAT_PROFILE_FILES` (defaults to 1 under `__EMSCRIPTEN__`, 0 elsewhere,
  or `-DUC2_FLAT_PROFILE_FILES=1`; `:18-25`) switches five functions - load, save, exists, delete, list (`:109,134,148,186` etc.) - from
  `Profiles/<name>/AVARIS_PROFILE.PRO` to `Profiles/<name>.PRO` in the engine's default directory (`/System/` in the browser). The header
  (and the Xbox subclass's virtual `GetProfilePath`) are untouched. The bytes inside are identical.
- `Engine/Inc/ULoadSaveManager.h` (M13, `4399e22`): a saved object reference is always 4 bytes (see feature 12).
- `Core/Inc/FFileManagerUnix.h:604` (M13, `0c9d5d2`): `Exists()` is true for directories; and `FFileManagerGeneric.h` `DeleteFiles` (D-3,
  `10b802f`). Without these no profile could be loaded or deleted outside Epic's Windows file manager.
- `UCC.cpp:335-410`: `ucc-bin profiletest save|load|list|exists|delete <profile> <file>` writes a profile with a deterministic value in
  every plain property through the game's own save path, or loads one, and exports every property as text (the cross-width and
  cross-layout save test).
- Frontend (per the notebooks, not read): `userFiles: ["System/Profiles/*.PRO"]`; `network.playerName` (`System/User.ini`, section
  `DefaultPlayer`, key `Name`); `manifestExclude` entries for `System/Profiles/**`, `*.PRO`, `System/Shot*.bmp`, `System/AutoPatch.txt`
  (ufrontend `f90b70f`).
- Tests: `tools/dev/c1-saves.sh` (native file inventory by diffing a 1,231-file tree before and after a session: it added only
  `Avaris.ini`, `User.ini`, `Avaris.log` and `Profiles/<name>/AVARIS_PROFILE.PRO`; plus the restart check: the profile is listed and loaded
  by a new process, `CharacterClassName` comes from the file), 11 PASS; `tools/dev/c1-web-saves.sh` (Node, 16 PASS: flat file, list, exists,
  delete, byte-identical to the native file, each build loads the other's file).

**How the parts fit** - the web host's persistence daemon lists ONE directory per `userFiles` entry and allows a wildcard only in the last
path segment, so `System/Profiles/*/AVARIS_PROFILE.PRO` is rejected by the frontend's compiler; the engine therefore writes a flat file the
glob `System/Profiles/*.PRO` matches. The host persists the inis through its engine-file derivation and the `flushconfig` call. Natively the
layout is unchanged. What is NOT persisted on purpose: `Avaris.log`, `Shot*.bmp`, `Detected.ini`/`Running.ini`, `AutoPatch.txt`, `uc2.uprof`,
the empty `Save/` and `Cache/` folders.

**Why** - the owner asked what must persist (PLAN.md C-2). The inventory found exactly one save game: the profile (`UPlayerProfile::Serialize`: settings,
button layout, ladder progression, unlocks, stats, ~57 KB). UT2004's web profiles are flat too (`Saves/*.uvx`).

**Decisions and alternatives**
- Coordinator's decision: flat file on the web. Rejected/noted alternative: keep the layout and teach the frontend's `compileUserFiles`/
  `scanUserFiles` one directory-wildcard segment, then revert `22e9dbf`. Also noted: a fallback needing no directory at all would keep
  profiles directly in `System/`.
- Native keeps Epic's layout (the Xbox save-game layout).

**Disadvantages, risks, deviations**
- Two layouts for the same data; code that names a profile path must go through `ULoadSaveManager`. A profile copied between native and web needs
  renaming by hand.
- A profile name that is not a legal file name on the web filesystem is not discussed in the notebooks.
- Left to the browser: WasmFS makes `/System` read-only (0555) for preloaded files and refuses `mkdir` below it; the frontend's backend `ensureDir`
  is expected to make it writable. The notebook marked this "not verified" under Node; the later browser `saves` spec passed (LOG.md D-3 entry:
  "saves 1/1 (profile restored after closing the tab)"), so it works in the headless Chromium used.
- Nothing is saved while the profile is the transient default ("No Profile"): every setting lives in the profile (profiles.md, other section).

**Status** - measured: as listed; coordinator's re-run `c1-saves.sh` ALL PASS; browser `saves` spec 1/1 on three later coordinator runs (one run
stopped by the memory guard, passed on rerun).

**Off switch** - `-DUC2_FLAT_PROFILE_FILES=0` at build time reverts the web layout.

---

### 11. Windows: MinGW-w64 i686, run under wine (M12; windows64 in feature 12)

**What the player/owner sees** - `uc2.exe` (Windows-subsystem program, no console) and `ucc.exe` (console) plus `OpenAL32.dll` in `System/`. The
package folder `run/m12-package` (made by `tools/dev/package-windows.sh`) is what to copy to a Windows PC. Nothing was ever run on real Windows.

**Where** (commits `754ce47 build, IpDrv, UC2ALAudio: every package library compiles for Windows (MinGW i686)`, `6aa92ca Core, Engine: compile with
MinGW-w64 i686`, `1e7f700 UCC, SDLLaunch, Engine: ucc.exe and uc2.exe link with MinGW-w64`, `2024551 Core, tools: Windows console Ctrl+C
handler (Epic's), wine wrapper, run tree with exe/dll`, `558956d` smoke script, `bc382c1` packaging, merge `5ee457f`)
- Build: preset `windows` (`CMakePresets.json`: `MINGW_TARGET_ARCH=i686`, `MINGW_THREAD_MODEL=posix`, `-fno-omit-frame-pointer`),
  `cmake/toolchain-mingw-w64.cmake`; root `CMakeLists.txt:99-105` (`-static -static-libgcc -static-libstdc++`; without `-static` the OpenAL DLL
  imported `libwinpthread-1.dll`), `:205-216` (SDL2 with `SDL2main` for MinGW, no X11), `:237-288` (OpenAL Soft as `OpenAL32.dll`, Linux
  backends off); `Source/UCC/CMakeLists.txt:39-52` (`ucc.exe`, console, `ws2_32 winmm`); `Source/SDLLaunch/CMakeLists.txt:45-52` (`uc2.exe`,
  `WIN32_EXECUTABLE`; SDL2main not used, MinGW's CRT calls `main()` for either subsystem).
- Epic's code changed (each guarded so only MinGW sees a difference; the notebook's table, re-checked against the code):
  - `Core/Inc/wordwrap.h`: on `!_XBOX && _WIN32` branch before the portable one - `<winnt.h>` types `WCHAR` as `wchar_t`, distinct from
    `UNICHAR`; file-local `WW_WCHAR/WW_LPCWSTR/WW_LPWSTR` are `#define`d over the Win32 names for the two TUs that include it (MSVC 7.1 had
    `WCHAR == unsigned short`).
  - `Core/Src/UnUnix.cpp`: `appSystemTime64` uses `GetLocalTime`+`SystemTimeToFileTime` on `_WIN32` (`:3654`; no `tm_gmtoff` in the MinGW CRT);
    `appWinCtrlHandler` (`:2752`, Epic's `CtrlCHandler`: first Ctrl+C/Break/close sets `GIsRequestingExit`; a second goes to Windows' default
    handler where Epic threw `appErrorf("Aborted!")` on the handler thread); crash reporter `SetUnhandledExceptionFilter`, `std::set_terminate`,
    SIGABRT (`:2551-2713, :3063-3070`) printing image base + rva for `addr2line`, naming the running exe; `appGetSystemDllHandle`/
    `appGetDllExport` through `LoadLibraryA`/`GetProcAddress` for `OpenAL32.dll` (`:1565-1665`); engine packages answer a "linked in"
    sentinel on `_WIN32`, as Epic's static builds did (`UnVcWin32.cpp` returns `(void*)1` under `__STATIC_LINK`), so no
    `--export-all-symbols`; POSIX signal handlers and `setrlimit` skipped.
  - `Engine/Src/UnModelLight.cpp:` `CompressLightmaps` (nvDXT) `&& _MSC_VER`: Epic's guards disagreed, only MinGW is neither MSVC nor Linux;
    `Engine/Src/xbstrip.cpp` Win32 headers; `GUI/Src/GUI.cpp` `<wtypes.h>` only on `_WIN32`.
  - `IpDrv/Inc/UnIpDrv.h:12-34,140`: WinSock chosen by platform `_WIN32`, not by compiler (`_MSC_VER`); `<ws2tcpip.h>` for `socklen_t`;
    `#undef MM_MAX`, `CreateEvent`, `CreateSemaphore`, `CreateMutex` (the Win32 headers rename a script enum value and Core's
    `FSynchronizeFactory` virtuals); resolver thread by `CreateThread`; `IpDrv/Src/UnSocket.cpp:366` `setsockopt` optval `const char*`.
  - `UCC/Src/UCC.cpp:9,66,84,110`: `WIN32` -> `_MSC_VER` at four places (the MSVC file manager, `UnThreadingWindows`,
    `GetCommandLineA`); `:94` `FFileManagerLinux` also for `__MINGW32__`; `:478` unbuffered stdout on MinGW.
  - Also (other sections' files): `UC2ALAudio` library names `OpenAL32.dll`/`soft_oal.dll` and `_putenv` for `setenv`; `SDL2InputTest.cpp`
    `clearerr` + `fseek(File,0,SEEK_CUR)` at EOF because wine's msvcrt keeps returning EOF on a growing file (all platforms).
  - `Core/Inc/FFileManagerUnix.h`: `mkdir` without a mode, `O_BINARY`, `USERPROFILE`.
- tools: `m12-wine.sh` (one program in its own prefix `<WorkUC2>/.cache/wine-m12`, `WINEDEBUG=-all`, winemenubuilder/mscoree/mshtml/winedbg disabled,
  the owner's display variables dropped, `wineserver -k` afterwards, Ctrl+C forwarded; `UC2_WINE_STOP_AFTER`), `m12-wine-smoke.sh` (checks verify,
  server, netjoin, null, gl, pinned, audiocheck, selftest, capture, nosound, keyboard, pad, leftovers), `package-windows.sh` (copies the
  tree, fails unless every imported DLL is a Windows system DLL or `OpenAL32.dll`), `dwarf-layouts.py`, `m12/deadbolt-pinned.scen`.

**How the parts fit** - MinGW is "Linux without the Unix identity": `UnGnuG.h` has a MinGW block that sets `__INTEL__`/ASM options but not
`__UNIX__`/`__LINUX__`, so POSIX paths in the file manager and sockets are chosen by `_WIN32` where needed; `UnUnix.cpp` is shared. SDL2 is static;
OpenAL is the only runtime DLL, loaded with `LoadLibrary` (absent: `UC2ALAudio: no OpenAL library ... sound is off`, the game still boots).

**Why** - PLAN.md M12: MinGW i686 + x86_64 presets, "wine smoke reaches menu". Epic's MSVC layer cannot be built here; engine-ut2004 already runs on MinGW
through the GCC layer. OpenAL Soft is built from the fetched source (the brief forbids downloading binaries).

**Decisions and alternatives** (windows.md decisions 1-5)
- The GCC/Unix Core layer with `_WIN32` branches rather than Epic's MSVC layer; `UnThreadingPosix.cpp` on winpthreads rather than
  `UnThreadingWindows.cpp`.
- **MinGW's default `-mms-bitfields` kept.** `verifyclasses` passes with it and with `-mno-ms-bitfields`; a whole-engine DWARF comparison
  (11,781 facts each) differs only in `UChannel`, `UControlChannel`, `UActorChannel`, `UFileChannel` (+4 bytes with MS rules) and
  `FDynamicActor`, all C++-only; MS rules are what MSVC 7.1 gave Epic's Windows binary and what the Windows headers are written for.
- `-mno-align-double` and `-fpack-struct=4` not used (would put every Windows/SDL header struct with 8-byte members out of the platform ABI). MinGW
  aligns `double`/`QWORD` members to 8 where i386 Linux aligns to 4: 39 engine types differ in size between the two builds, all C++-only
  (`UState`/`UClass` +8/+4, `ULevel`, `UViewport`, net channels, render caches, UC2ALAudio's state, platform structs); none is mirrored by script
  or written as a block.
- `appBaseDir` stays the working directory (as on Linux): the game must start with `System\` as the working directory (Explorer double-click
  does that; a shortcut needs "Start in"). Clipboard: a process-local string, as on Linux (the Windows clipboard is not used).
- Runtime DLLs: libgcc, libstdc++, winpthread static; SDL2 static; OpenAL Soft a DLL beside the exe. Imports: `ucc.exe` ADVAPI32, KERNEL32,
  msvcrt, WS2_32; `uc2.exe` also GDI32, IMM32, ole32, OLEAUT32, SETUPAPI, SHELL32, USER32, VERSION, WINMM; `OpenAL32.dll` AVRT, KERNEL32, msvcrt,
  ole32, SHELL32, USER32, WINMM.

**Disadvantages, risks, deviations**
- **Not tested on Windows.** wine 10 (a WoW64 build; `WINEARCH=win32` is refused) is not Windows. The GL path ran on Mesa llvmpipe through wine's WGL
  (reported as GL 4.3 compatibility); no NVIDIA/AMD/Intel Windows driver, no WASAPI (wine runs used the null and wave-file backends), no XInput
  hardware (SDL's virtual controller), no high-DPI, no non-ASCII path (TCHAR is ANSI: depends on the code page), no antivirus/SmartScreen check
  of an unsigned 81 MB exe. The notebook ends with a 10-item real-Windows checklist (windows.md) that nobody has run.
- A Linux client joining a Windows server: the handshake works (`netjoin` check); the full join was blocked by the network defect fixed afterwards
  on another branch; the combined run on the fixed code is not recorded (BLOCKERS.md: "Not done: real Windows / wine server against the fixed client").
- Linux build and wine build get different GL versions (4.5 vs 4.3 compatibility under wine): irrelevant to the driver.
- Windows uses an unsigned `uc2.exe` with debug info (81 MB; `UC2_PACKAGE_STRIP=1` strips copies).
- `Editor` (`ucc-bin make`) is not built for MinGW.

**Status** - measured under wine (LOG.md 2026-10-05): `ucc.exe verifyclasses` all match; server up and clean Ctrl+C; client with null renderer then
GL: 0 GL errors in 1,767 frames; keyboard 16 checks and pad 33 checks PASS; audio self-test 39 checks, 0 failed (identical to Linux to the hundredth);
audiocheck 78 cue tables, 2,438 cues, 0 failures; capture NuBattle at -19.01 dB; pinned DM-Deadbolt view vs Linux: static part 100.000% within 2
levels (with `-nopostprocess` and the reticle masked), same position and rotation; `nosound` PASS; smoke 13/13 on the combined main after M9.
Seen only: two engine screenshots of the main menu (the notebook says they were looked at).

**Off switch** - none; the Windows build is a preset.

---
### 12. 64-bit: LP64 x86_64, windows64, arm64 preset (M13)

**What the player/owner sees** - `native64` (Linux x86_64) plays like the 32-bit build; `windows64` builds `uc2.exe`/`ucc.exe` for x86_64; the `arm64`
preset exists but has never been configured. Saved profiles written by a 32-bit and a 64-bit build are byte-identical and load in each other.

**Where** (commits `56298d5 Core: 64-bit bytecode object slots hold object indices; registration stashes in pointer-sized fields`,
`f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)`,
`3afff92 Core: a None context clears its whole 64-bit destination; switch compares a pointer's width`, `555078d Core: the GC copies a whole object
pointer`, `2a0d111 Engine, build, tools: two punned pointer ints; arm64 and native64-uchar presets`, `4399e22 Engine: a save game's object
reference is 4 bytes on every width`, `0c9d5d2 Core: the POSIX file manager reports directories as existing`, `58cf932 build, UCC:
-Werror=int-to-pointer-cast at 64-bit, sanitizer preset, ucc profiletest`, `c67b332`, `a6c8edc`, merge `064dd20`; notebook `analysis/64bit.md`)
- Epic's code changed (the notebook's site table, re-checked against the snapshot):
  - **Bytecode object slots** (`SERIAL_POINTER_INDEX`). `Core/Inc/Core.h:101` `PLATFORM_64BITS` (x86_64, `_WIN64`, aarch64, any `__LP64__`);
    `Core/Inc/UnObjBas.h:15` turns `SERIAL_POINTER_INDEX` on for every 64-bit target (Epic had it for `__x86_64__` only, with Ryan Gordon's
    64K-entry side table `GSerializedPointers` searched linearly at every load and GC pass; removed). A bytecode slot stays 4 bytes (every jump and skip
    offset of the shipped 32-bit `.u` and every script patch counts those slots) and holds `Obj->Index`, `INDEX_NONE` for None:
    `UObject::SerialPointerIndex`/`SerialIndexPointer` (`:847-849`, O(1)). Readers: `FFrame::ReadObject` (`Core/Inc/UnScript.h`), `execBoolVariable`
    (`UnCorSc.cpp`), `XFERPTR` (`UnClass.cpp`).
  - **Layout.** Object properties align to 4, not `sizeof(UObject*)`, at link (`Core/Src/UnProp.cpp`, same value on every 32-bit target); the C++
    mirrors are `#pragma pack(4)`: whole-file in `UnMaterial.h`, `UnForceFeedbackWaveform.h`, `UnCanvas.h`, `UnPlayer.h`, `UnTerrain.h`,
    `UnFluidSurface.h`, `UnCameraEffects.h`, `UnGame.h`, `UnEngine.h`, `UnURL.h`, `UnCorObj.h`, `UCCameraEffects.h`, `UWeb.h`, and around `FLiveUser`
    (`UnObjBas.h`), `FOrientation` (`Engine.h`), `UUIXInteraction` (`UnUIXInteraction.h`). None has an 8-byte scalar member, so no 32-bit target's layout
    changes. `PTRINT` replaces `INT` in generated headers where native code casts to and from a pointer (`EngineClasses.h` 13 lines: `OctreeNodes`,
    `FStateInfo`, `LogAr`, five MovieMaterial fields, `Particles`, `ScriptedTexture.RenderTarget`, `ShadowBitmapMaterial.TextureInterfaces[3]`;
    `GUIClasses.h` `Designer`; `IpDrvClasses.h` `PrivateResolveInfo`, `LinkPtr`, `MSLinkPtr`).
  - **Native placeholders** (`Core/Src/UnClass.cpp:453` table, 50 entries: 46 members, 4 array strides; lookup `FindNativePlaceholder`): a script `int`
    that stands in for C++ data script never touches (the object header `ObjectInternal[7]`, vtable stand-ins `Subsystem.ExecVtbl`, `Player.vfOut/vfExec`,
    render-cache pointers, `MovieMaterial` fields, post-process render targets, Xbox Live UI pointers, IpDrv resolver/link pointers, UWeb maps) advances the
    layout by the C++ size, the property itself stays a 4-byte int. `UStruct::Link` applies it (`:568`); the C++ `sizeof` of every class
    registered by `DECLARE_CLASS` is recorded in both `UStruct` native constructors (`:373-411`) and compared with the linked script size; a mismatch is
    `appErrorf` (fatal), or a `VERIFY: [layout]` warning while `ucc verifyclasses` runs (`GLayoutWarnOnly`, set from argv before `appInit`,
    `UCC.cpp:526-528`).
  - **Registration stashes.** `UnObj.cpp` native constructors parked the `const TCHAR*` name in the 4-byte `FName Name` and the autoregister chain in
    `_LinkerIndex`; at 64-bit the name goes to `StateFrame` and the chain to `_Linker` (pointer-sized, NULL again after `Register()`); `ClassConfigName`
    (an `FName` inside pack(1) `UClass`) is stashed in a side table (`UnClass.cpp:1454`), because the 8-byte pointer overwrote half of `ClassConstructor`
    (SIGSEGV `0x0000555500005555` on the first object of Engine).
  - **Casts and puns.** `Align()`, `FMemStack::PushBytes`, `STRUCT_OFFSET` via `PTRINT`; the outer-hash input `(DWORD)(PTRINT)`; the GC copied 4 bytes of each
    object pointer in `FArchiveTagUsed` (`UnObj.cpp:5221`, now `sizeof(UObject*)`; SIGSEGV at the first garbage collection); `UInput`'s alias struct was
    described by arithmetic `sizeof(FName)+sizeof(FString)` and now by `sizeof(FAlias)`/`STRUCT_OFFSET` (`Engine/Src/UnIn.cpp:63`); `UnAudio.cpp` RIFF
    bound, `UnStats.cpp` memset length, `UnOctree.cpp`, `UnParticleEmitter.cpp`, `AFileLog.cpp`, `UnScript.cpp`, `UnMaterial.cpp`, IpDrv
    `MasterServerClient/Uplink.cpp`: `(INT)p` becomes `(PTRINT)p`. Cast census of a full 64-bit build: 29 pointer-to-int + 90 int-to-pointer = 119
    diagnostic lines before, **0 + 0** after (`tools/dev/m13-cast-census.sh`).
  - **VM.** `UnCorSc.cpp` (`#if PLATFORM_64BITS`): `X = None.Y` zero-fills the destination with the compiler's 32-bit byte count; where X is bigger in memory
    (a pointer, a struct holding one) the high half of the old pointer survived; `execLet` now zeroes the whole `ElementSize` when exactly that
    short fill landed on it, and `execSwitch` compares at least `sizeof(UObject*)` bytes (engine-ut99 `253aaf2`).
  - **Saved data.** `Engine/Inc/ULoadSaveManager.h`: an object reference in a save game was `sizeof(UObject*)` bytes (4 or 8); now always an `INT 0` of
    4 bytes on save and 4 bytes read on load, byte-for-byte the old 32-bit format.
  - **File manager.** `FFileManagerUnix::Exists` also answers for directories; before, `ULoadSaveManager::ExistsGameObject` never found a saved profile on
    Linux/MinGW/web and every load failed with "Unable to load profile" - a width-independent bug found by the cross-width save test.
- new code of ours: `UCC/Src/UCC.cpp:164-285` the 64-bit mode of `verifyclasses` (`VerifyNativeLayouts64`): (1) every class with a C++ size: C++ vs
  script (400 classes); (2) every placeholder table entry exists and is linked at its C++ size; (3) **no bytecode reads or writes a placeholder** - every
  `UStruct`'s script is walked with `SerializeExpr` on a collecting archive (6,578 scripts) and each `UProperty` it references is tested; `DUMP=<file>`
  (any width, opt-in) writes the linked layout of every class, script struct and function for `tools/dev/m13-layout-check.py`, which compares it with the
  DWARF of the same binary member by member (5,697 properties, 400 classes, 122 structs, 202 event parameter structs). `ucc-bin profiletest`.
- build: `CMakeLists.txt:146-151` (`-Werror=int-to-pointer-cast` at 64-bit), `:132-136` sanitizer, `:137-143` unsigned-char audit, presets `native64`,
  `native64-uchar`, `native64-asan`, `arm64`, `windows64`; `cmake/toolchain-aarch64-linux-gnu.cmake`.
- tools: `tools/dev/m13-64bit.sh` (checks `verify census controls audio movies glselftest effects pinned psh2glsl saves soak leftovers`, plus opt-in `asan`,
  `base32`; `M13_TREE`/`M13_BUILD` select the trees), `m13-layout-check.py`, `dwarf-layouts.py`, `m13-cast-census.sh`, `pinned-compare.py`,
  `map-census.sh` (`CENSUS_NAME`, `CENSUS_PORT`), `m12-wine-smoke.sh` (`M12_TREE`, `M12_REF`, `M12_OUT`: windows64 under wine).

**How the parts fit** - the shipped scripts see a 32-bit world: object references in bytecode are 4-byte indices, properties are laid out with 4-byte
slots; C++ sees 8-byte pointers. The two meet at three places: the bytecode readers (index <-> pointer), the property linker (placeholders widen the
script layout where C++ keeps a pointer in an `int`), and the guard that refuses to start when a native class's C++ size and script size differ.
Without the placeholder table the guard stops the game at the first mismatching class; without the guard a wrong layout is silent corruption.
Scripts that read a placeholder int as a value would see only its low 4 bytes - the bytecode scan proves none does.

**Why** - Owner decision (PLAN.md): arm64 "later / build-only: keep code 64-bit clean, add preset, no device chase before web ships"; 64-bit x86 is the
check that code is clean. The playbook is the frontend's UT99/Unreal 1 64-bit work (ufront 2.83/2.84) and engine-ut2004's (Ryan Gordon's) amd64 port; UC2
already had `SERIAL_POINTER_INDEX` for `__x86_64__` in the Epic tree.

**Decisions and alternatives**
- Keep the shipped `.u` unchanged and widen on the C++ side (placeholders, index slots), because rebuilding the scripts (UT2004 recompiled `Object.uc`
  with a `pointer` type) was "not open before M14".
- Object index instead of a side table: same scheme as ufront 2.83's UT99 decision; O(1), no 64K limit.
- Fatal at link time on any mismatch, as ufront 2.83's decision 5, except while `verifyclasses` lists them all.
- `BITFIELD` stays `unsigned int` (4 bytes), never `long`.
- arm64 keeps `-fsigned-char` like every target (arm's char is unsigned); the x86_64 `-funsigned-char` audit build shows the code does not depend on it
  (below). Windows 64-bit is LLP64: `PTRINT` is `unsigned long long` there (`UnGnuG.h:270-276`).
- `Editor` (script compiler) stays 32-bit-only (feature 14).

**Disadvantages, risks, deviations**
- The placeholder table is hand-maintained: a new native pointer kept in a script `int` needs an entry, and the failure mode is a startup
  `appErrorf` naming the class (good) - but only if the class is registered by `DECLARE_CLASS`.
- The 64-bit proof is "layout agrees and no bytecode reads a placeholder". What it does not prove is that the C++ code behind each pointer-in-int
  site works at 64 bits. The site table marks these as **exercised by no check**: `Orientation.MA` (matinee/cinematics - the SceneManager is in no check),
  `StateInfo` Node/StateNode/Code (PushState/PopState in AI scripts, not asserted), `FileLog.LogAr`, `MovieMaterial` in-memory decode thread,
  `ScriptedTexture.RenderTarget` and `ShadowBitmapMaterial.TextureInterfaces[3]` (punned pointers, effects runs "only if in view; not asserted"),
  `GUIController.Designer`, `UIXInteraction` (Xbox Live), IpDrv resolver/master-server links (no DNS or master server in the tests), UWeb maps,
  wave import's RIFF bound, `UnOctree.cpp CompareArray` (debug), None-context fill ("any run; not asserted").
- `Engine/Src/palette.cpp` (unbuilt, no caller) reads 32-bit pixels as `unsigned long*`: wrong at LP64, left as Epic wrote it.
- UT2004's 32-byte padding of every allocation on 64-bit is not adopted (no padding in `FMallocAnsi.h`); the guard replaces it.
- `-fpermissive` keeps pointer-to-int narrowing a warning; only the census script finds it, and only in a full clean build log.
- **arm64 is unverified.** The preset and toolchain file exist; no notebook records a configure or a build; BLOCKERS.md says the compiler and qemu-user
  were installed by the owner on 2026-10-06 and in the same entry lists `sudo apt install g++-aarch64-linux-gnu qemu-user ...` as still missing, so the entry
  contradicts itself. By reading (`64bit.md`, step 6 and 11): every x86 asm/intrinsic site is gated `__i386__`, `__x86_64__ || __i386__`, `_XBOX`,
  `__PSX2_EE__` or `__HAS_SSE__`, so arm64 takes the C paths that x86_64 and wasm take; bytecode operands are read as `*(INT*)Code` at unaligned addresses,
  fine for AArch64 normal loads (not for armv7, which is not a target); no atomics on pack(4) members. The client additionally needs the arm64 X11/GL
  development packages through multiarch because SDL2 is built from source.
- The unsigned-char audit found one real difference: `Core.h:214` `ToAnsi`/`FromUnicode` (`(_WORD)In<0x100 ? In : MAXSBYTE`) writes a Latin-1 character >= 0x80
  as 0x7F with signed char (every build here and Epic's MSVC) and keeps it with unsigned char. Recorded, not changed (Epic's shipped behaviour); it means
  accented text in saved inis can be mangled on every platform.
- A 64-bit `uc2.bin` is 66 MB and `ucc-bin` 54 MB at first link.

**Status** - measured (LOG.md, `analysis/64bit.md`): `ucc-bin verifyclasses` at 64 bits: 400 classes, 0 mismatches while linking, 50 placeholders found, 6,578
scripts scanned, exit 0; `m13-layout-check.py`: 2 mismatches (`Texture.__LastUpdateTime`/`MemUsage` declared in opposite order in C++ and script), identical at 32
bits, pre-existing in Epic's shipped PC/Xbox builds, script never reads either; 32-bit unaffected: DWARF of main's 32-bit `ucc-bin` vs the branch's differs only in
3-4 new template instantiations. Census 58/58 at 64-bit and 58/58 at 32-bit, same per-map result. 64-bit GL client, DM-Deadbolt with 4 bots, 70 s: max RSS
227 MB, 0 GL errors in 1,124 frames. Pinned DM-Deadbolt view 64-bit vs 32-bit: static part 99.997-100.000% within 2 levels (coordinator 99.997%). Soak 1,200 s:
RSS +1.5% (agent +3.2%), 0 Critical. Saves: profiles byte-identical (845 bytes), cross loads equal. Full run 2: every check PASS; coordinator's re-run on merged
main (`logs/coord/a1-m13.log`): verify, census 58/58, controls 5/5, audio, movies, glselftest 10/10, effects 14/14, pinned, psh2glsl, saves, soak, leftovers:
all PASS. `windows64`: 0 errors; under wine verify and server PASS and a 10-check smoke 10 PASS 0 FAIL (the agent's report; the coordinator logged it "per the
agent"). `native64-uchar` builds (435 commands carry `-funsigned-char`). Two failures in the first full run were load (pad 30/33 while a build ran; passed 33/33 three
times alone) and harness (soak time limit), both fixed in the script.

**Off switch** - build with the `native` preset (default); `-DUC2_ALLOW_64BIT=OFF`.

---

### 13. The ini structure: Default.ini, DefUser.ini, the web inis

**What the player/owner sees** - the first run creates `System/Avaris.ini` and `System/User.ini` from the shipped defaults; settings persist there. The
browser build has its own boot inis.

**Where**
- `Engine/Config/` (commit `7633696` baseline, then edited by many): `Default.ini` (568 lines, 33 sections), `DefUser.ini` (355 lines, 10 sections),
  `DefaultXbox.ini` and `DefUserXbox.ini` (Epic's, unchanged, reference), `KeyBinds.ini` (unchanged), `README.md` (every change from Epic's file with its reason:
  46 table rows), `web/Default.ini` (587 lines, 35 sections), `web/DefUser.ini` (355 lines).
- Derivation: `Core/Src/UnMisc.cpp:2118-2146 appInit` (Epic's code): `GIni = <module>.ini` (module `Avaris`; `INI=` on the command line overrides), `User.ini` (`USERINI=`);
  each is created by copying `Default.ini` / `DefUser.ini` when absent. `tools/dev/mkrun-uc2.sh` copies `Engine/Config/*.ini` over the asset tree's into
  every run tree; a native player's `System/` is therefore seeded from the repo's files, and the file in the asset tree (`assets/uc2/System`, the leak's
  copy) is overridden.
- Platform-relevant changes to Epic's `Default.ini` (rows for the other keys are in the README and in the other sections):

  | Section / key | Epic | Ours | Why |
  |---|---|---|---|
  | `[URL] Port` | 1000 | 7777 | 1000 is the Xbox system-link port, privileged on Linux |
  | `[Engine.Engine] ViewportManager` | `WinDrv.WindowsClient` | `SDL2Drv.SDL2Client` | WinDrv is Win32-only; Epic's line kept as a comment |
  | `[Engine.Engine] RenderDevice` | `D3DDrv.D3DRenderDevice` | `UC2GLDrv.UC2GLRenderDevice` (web: `UC2GL4ESDrv.UC2GL4ESRenderDevice`) | D3DDrv is not built; `-nullrenderer` selects the null device |
  | `[Engine.Engine] AudioDevice` | `Engine.NullAudioDevice` | `UC2ALAudio.UC2ALAudioSubsystem` | M8; `-nosound` selects the null device |
  | `[Engine.Engine] NetworkDevice` | `IpDrv.TcpNetDriver` | web: `WebRTCDrv.WebRTCNetDriver` | a browser has no sockets |
  | `[Engine.Engine] ScriptSaveConfig` | (none) | `True` | script `SaveConfig()` writes on PC (saveconfig section) |
  | `[SDL2Drv.SDL2Client]` | (none) | copy of `[WinDrv.WindowsClient]` plus later keys | the client reads its own class section; also the compiled defaults |
  | `[UC2GLDrv.UC2GLRenderDevice]`, `[UC2GL4ESDrv.UC2GL4ESRenderDevice]`, `[UC2ALAudio.UC2ALAudioSubsystem]`, `[SDL2Drv.PadMap]`, `[GUI.EmulatedJoypad]`, `[ufront.AutoFOV]`, `[Engine.Console] ConsoleKey` | (none / 9) | see the README | renderer, audio, input sections |
- Web inis: `Engine/Config/web/Default.ini` and `DefUser.ini` are GENERATED by `tools/dev/sync-web-ini.py` (`--check` fails when they are not what the
  script would write): the native file with the `DELTAS` (`RenderDevice`, `[SDL2Drv.SDL2Client] UseJoystick=False`, `NetworkDevice`), plus `WEB_KEYS`
  (`AudioDevice`) and `WEB_SECTIONS` (`[UC2ALAudio.UC2ALAudioSubsystem]` with the web-only `Backends=`, `[UC2Movie]`, `[WebRTCDrv.WebRTCNetDriver]`) kept as the
  web file has them. Measured against the native file today the only differences are those: `RenderDevice`, `NetworkDevice`, `UseJoystick`, `Backends=`, the
  `[UC2Movie]` and `[WebRTCDrv.WebRTCNetDriver]` sections (`WebRTCDrv` timeouts 60 s and 300 s against 30 s and 20 s because a browser streams a map's files on
  demand and a guest that loads sends nothing). They are preloaded into the module as `/System/Default.ini` and `/System/DefUser.ini` (`Web.cmake`).
  A host must not serve the asset tree's copies of those four names.
- Commits: `c66d275 config: web inis generated from the native ones (they had fallen behind M7 and M10)`, `ef34e6c`, `941326a`; many README commits.

**How the parts fit** - `Default.ini`/`DefUser.ini` are templates, read once when the working ini does not exist. Afterwards `Avaris.ini`/`User.ini` own the
values; a new key in `Default.ini` reaches an existing player's ini only through the compiled default of the class that reads it (the README says "also the
compiled defaults" for most new keys), through a migration in code (`UC2InputMigrate.cpp` for old input values; the `ConsoleKey` Tab -> tilde mapping at
`UnGame.cpp:284`; `ReplaceMissingDrivers`), or not at all. On the web the host keeps `Avaris.ini`/`User.ini` (persisted engine files) and `flushconfig`.

> Superseded (2026-10-09/10): see section 60, 10, feature 13.

**Why** - `Default.ini` is Epic's PC ini; the retail disc's `DefaultXbox.ini` is a slightly later variant (net rates 7000 -> 10000/20000, four log suppressions on).
The owner's rule (2026-10-06): "canonical config and localisation = the leak (unless a difference changes behaviour)".

**Decisions and alternatives**
- Generate the web inis instead of hand-maintaining a second copy: at M11 the web copies were still the M4w ones, without M7's `MaxDistortion` and without M10's
  key table, pad map and bindings (the menus would not have answered the keyboard).
- Keep Epic's other-platform sections (`[WinDrv.WindowsClient]`, `[D3DDrv...]`, `[XboxDrv...]`) in the file as dead text: the file stays diffable
  against Epic's.

**Disadvantages, risks, deviations**
- **Line endings drifted.** Epic's `Default.ini` was CRLF (367 CR lines at the baseline; `client-boot.md` says "The file keeps its CRLF line endings (it is a byte copy
  of Epic's)"). Since the M7 merge (`225b0d5`, hand-resolved conflict) `Default.ini` and `web/Default.ini` are LF, while `DefUser.ini` and `web/DefUser.ini` are
  CRLF. `git diff` against Epic's `Default.ini` is therefore 935 changed lines in the diff stat for about 213 real ones. The engine parses both; `sync-web-ini.py` keeps
  whichever the native file uses. The code wins over the notebook.
  *(Superseded (2026-10-09/10): see section 60, 10, feature 13.)*
- An existing `Avaris.ini` is never refreshed; stale values (an `Engine.NullRenderDevice` from an early run, `ConsoleKey=9`) stay unless a migration exists. A
  player who ran an older build keeps old defaults; the web persists the ini for ever (`Core/Src/UnObj.cpp` comments say a stale baked default shadows the next
  build's).
- `Default.ini` suppresses the `DevUI`/`DevGUI` log names (Epic's setting), so menu activity is not in the log by default.
- The README's table has grown to be the only index of config deviations; a change made without a row is invisible.

**Status** - I did not run `sync-web-ini.py --check` (no tests); a manual diff of the native and web files in the snapshot shows only the differences listed
above, so they are in step at `4813dd9`. Not verified: that a long-lived `Avaris.ini` from the first M4 build still boots (the driver replacement logic is tested with Epic's
original `Default.ini` as `Avaris.ini`: `WinDrv.WindowsClient is not in this build: using SDL2Drv.SDL2Client`, `D3DDrv.D3DRenderDevice ...: using Engine.NullRenderDevice`).

**Off switch** - `INI=<name>`/`USERINI=<name>` on the command line pick other files; `-ini:` overrides single keys (feature 9).

---

### 14. The Editor package as a porting target (M14, platform side)

**What the player/owner sees** - nothing in the game. `ucc-bin make` rebuilds all 17 script packages from `Source/<Pkg>/Classes` on 32-bit Linux (the
scripts section covers what that gives); this feature is what made Epic's `Editor` package compile, link statically and run.

**Where** (commits `93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)`, `586600b Core, Editor, tools: script comparison
tool, stripsource stage, backslash package names (M14 S2)`, `c3c7397 ...script reproduction matches the shipped packages`, `434ac53 Editor, UCC: Editor
layouts match (GCC bitfield tagging), verifyclasses checks them; Editor 32-bit Linux only`, `e2b3a8c Core: a conformed package save sorts only the new imports`)
- Build: `Source/Editor/CMakeLists.txt` (every `Src/*.cpp`; `UC2_STATIC_CLASS_ACCESSORS=1`); `CMakeLists.txt:433-435` removes it for 64-bit and MinGW;
  `Source/UCC/CMakeLists.txt:34-38` links it and defines `UC2_WITH_EDITOR`.
- new code of ours: `Editor/Src/UC2EditorRegistrants.cpp` (255 lines: static-link registration of the Editor package's classes through per-class C accessors,
  because most commandlets are declared inside their `.cpp` and `AUTO_INITIALIZE_REGISTRANTS_EDITOR` cannot name them), `Editor/Src/UC2EpicClassOrder.h`
  (495 lines, generated by `tools/dev/epic-class-order.py`: the order in which Epic's DLL build registered native classes, which is the order `ucc make`
  walked sibling classes in), `Core/Inc/UnObjBas.h:475-485` `UC2_STATIC_CLASS_ACCESSOR`.
- Epic's code changed (porting fixes): `Editor/Inc/Editor.h` (`GCC_PACK(4)` on `AutosaveTimeMinutes`: after a bitfield run MSVC and script start a new DWORD;
  a forward declaration of `FObjectExport`), `EditorClasses.h` (three `GCC_PACK(4)` tags), `Perlin.h` (`register INT`), `UnVisi.cpp` (pointer-to-member syntax),
  `UnParams.cpp` (five parsers duplicated in `Engine/Src/UnParams.cpp` collide when both are linked into one program: Editor's copies `#if !__STATIC_LINK`),
  `TTFontImport.cpp` (GDI rasterising only under `_MSC_VER`; elsewhere the factory exists but importing a font reports an error), `UAnalyzeMeshesCommandlet.cpp`
  (glibc's `MAXFLOAT` macro), `UnEdSrv.cpp` (`#exec` lines name files with `\`; `PATH_SEPARATOR` is `/`), `UnEdExp.cpp` (the header exporter: tag only the first
  bool of a run, and tag a class's first member, the same two rules as in feature 6, so a regeneration agrees with the hand-fixed headers).
- Also for reproducing Epic's compile (shared with the scripts section): `Editor/Src/UMakeCommandlet.cpp` (`.uc` files sorted as NTFS listed them; `-CONFORMNOCONFIG`),
  `UStripSourceCommandlet.cpp` (`-CONFORM`), `UnScrCom.cpp` (class walk in Epic's registration order, `-NOEPICORDER` keeps object-table order);
  in Core `UnObj.cpp:3629` (`\` in `#exec OBJ LOAD FILE=..\Textures\T_HUD.utx` - without it the package was named `""` and every texture of T_HUD.utx was saved as an
  import from a transient package), `UnClass.cpp` (`StructFlags` and `ScriptTextCRC` were uninitialised for native classes constructed on the heap, which a DLL build's
  zero-initialised static storage hid: `ucc make` wrote garbage into dependency records), `UnAnsi.cpp` (`appAtoi` wraps like MSVC's: `INPUT_All = 4294967295` compiled
  to -1 in the shipped scripts and 2147483647 with glibc's saturating `atoi`), `UnObj.cpp:4490` (D-9: the import sort started from 0 instead of the
  first new import and reordered existing imports; names are sorted from `SortStartPosition` at `:4411`).
- tools: `script-build.sh`, `script-diff.py`, `upkg151.py`, `epic-class-order.py`, `m14-scripts-check.sh` (scripts section).

**How the parts fit** - Editor is linked into `ucc-bin` only (not `uc2.bin`); the launcher registers it through `UC2InitializeEditorRegistrants`
(`UCC.cpp:52,563`), and `verifyclasses` checks its layouts too (`UC2VerifyEditorClasses`).

**Why** - the owner's decision (2026-10-04): boot with the shipped `.u`, later recompile from source to fix keyboard/mouse and drop the patches.

**Decisions and alternatives** - build Editor but not UnrealEd/Window (no GUI editor); keep it 32-bit Linux only: its hand-written `UEditorEngine` mirrors
script's `int Pad[8]` with four pointers; the MinGW build "has not been tried".

**Disadvantages, risks, deviations** - the web build and the asset tree keep the shipped `.u` except `GUI.u`, `UCGameplay.u` and `UCMenu.u`, which are rebuilt
(asset tree v2, `tools/dev/mkassets-uc2.sh`, `System/SCRIPTS.txt` says which); `UCGameplay.u` is a server package, so every peer must run it. Two shipped-vs-source
behaviour differences are listed in BLOCKERS.md (CTFFlag.home patch defective in the shipped scripts; `bUnlockCharacters`/`bShowAllMuts` `config` in the shipped
`GUI.u` only). A static-link consequence nobody has fixed: `UHTTPDownload` and `UPrefab` say `DECLARE_CLASS(...,Engine)` but live in IpDrv/Editor, which as DLLs
registered them under their own package; our static build registers them as `Engine.HTTPDownload`/`Engine.Prefab`, so `Default.ini`'s
`DownloadManagers=IpDrv.HTTPDownload` names a class that does not exist and package download has never been exercised.

**Status** - measured (LOG.md 2026-10-07): `ucc-bin make` builds all 17 packages (10 s per full build, well under 3 GB); `script-diff.py`: 0 unexplained
differences, 9,621 of 10,456 functions byte-identical, 813 differ only in line positions in 17 classes changed after retail; 29 of Epic's 30 patches already in the source.

**Off switch** - not building Editor (remove it from `UC2_PACKAGES`); `-NOEPICORDER`, `-CONFORMNOCONFIG`.

---
## Inventory: `tools/dev/*`, `tools/*`, `bin/*`

All repo scripts are bash or python3 (Pillow only, no numpy on this machine), print one `PASS`/`FAIL` line per check, and run the game, wine, node
or ffmpeg inside `bin/capped`. "Section" says which other section of the report explains what the script checks.

### tools/dev/ (run-tree and build helpers, platform checks: mine)

| Script | What it runs or measures | Milestone |
|---|---|---|
| `import-epic-sources.py` | one-shot import of the pristine tree into this repo's layout (refuses to overwrite) | M0 |
| `mkassets-uc2.sh` | builds the canonical asset tree `<WorkUC2>/assets/uc2` from the pristine tree: v0 = Epic's PC-format content (Maps 58 + 9 ComboMatinees, Textures, StaticMeshes, Animations, System `.u`/`.ini`/Localization, Ladders), v1 (2026-10-06) hard-links the cooked `Sounds/Music/Movies/Textures/Movies`, v2 (2026-10-07) overlays rebuilt `GUI.u`, `UCGameplay.u`, `UCMenu.u` (shipped copies kept in `assets/uc2-shipped/System`, `System/SCRIPTS.txt` lists them) and writes `MANIFEST.sha256`; real copies, never symlinks, never deletes | M0, M8, M9, M14 |
| `mkrun-uc2.sh <build-dir> [tree] [more-build-dirs]` | assembles `<WorkUC2>/run/<tree>`: hard links of the asset tree's content folders, a real copy of `System/` (the engine rewrites inis and logs there), `Engine/Config/*.ini` copied over, optional `UC2_SCRIPTS=<dir>` rebuilt packages, the build's `ucc-bin`, `uc2*`, `libopenal.so.1`, `ucc.exe`, `uc2.exe`, `OpenAL32.dll`, `ucc-bin.js/.wasm`; it only ever removes the tree it created | M3 on |
| `map-census.sh <build> [secs] [workers]` | loads every `Maps/*.unr` on a headless `Engine.ServerCommandlet` (own run tree and port per worker); PASS = "up for play", no `Critical:`, still ticking at the timeout; `logs/census/summary.tsv`; kills servers that hang on exit | M3 |
| `fix-bitfield-pack.py [--check]` | removes `GCC_PACK(4)` from bitfields that continue a run, and tags the first member of a generated class (the two GCC rules of feature 6) | M3 |
| `layout-diff.py <build-wasm>` | record layouts of every TU for wasm32 vs i386 (`-fdump-record-layouts-complete`) | M4w |
| `dwarf-layouts.py` | `Type size N` / `Type.member off N` facts from DWARF of object files (ELF, COFF, wasm); `diff` of two builds | M12 |
| `m12-wine.sh` | one Windows program under wine in its own prefix, nothing left running (feature 11) | M12 |
| `m12-wine-smoke.sh` | the Windows build under wine: verify, server, netjoin, null, gl, pinned, audiocheck, selftest, capture, nosound, keyboard, pad, leftovers (`M12_TREE/REF/OUT` for windows64) | M12, M13 |
| `m12/deadbolt-pinned.scen` | scenario: DM-Deadbolt, first PlayerStart, god, probe, `shot pinned` (same view on Linux and wine) | M12 |
| `pinned-compare.py A B diff` | static part of two pinned screenshots: % within 2 levels, % bit-identical, PSNR | M12, M13 |
| `package-windows.sh [build [dest]]` | a self-contained Windows folder (`run/m12-package`) and a check that every imported DLL is a Windows system DLL or `OpenAL32.dll` | M12 |
| `m13-64bit.sh [check...]` | the 64-bit gate: verify, census, controls, audio, movies, glselftest, effects, pinned, psh2glsl, saves, soak (1,200 s), leftovers; opt-in asan, base32 | M13 |
| `m13-layout-check.py dump dwarf` | linked script layout (`verifyclasses DUMP=`) vs DWARF member by member | M13 |
| `m13-cast-census.sh log` | distinct source lines with pointer-to-int or int-to-pointer diagnostics in a full build log | M13 |
| `c1-ini-overrides.sh` | 30 checks of `-ini:` and `FLUSHCONFIG` | C-1 |
| `c1-saves.sh` | which files the game writes (tree listing diffed before/after) and the profile restart check (11 checks) | C-2 |
| `c1-web-saves.sh` | wasm under Node: flat profile files, `-ini:` write-back (16 checks) | C-2 |
| `sync-web-ini.py [--check]` | regenerates `Engine/Config/web/{Default,DefUser}.ini` from the native files | M11 |
| `upkg151.py` | reader for package version 151 (summary, names, imports, exports; `--names`) | M11, M14 |
| `imgdiff.py A B` | mean/max difference and share of pixels over 2/8/32 levels, side-by-side and diff pictures | M11 |

### tools/dev/ (checks owned by other sections; one line each)

| Script | What it runs or measures | Milestone / section |
|---|---|---|
| `run-uc2-shot.sh <tree> <name> <secs> <fire-at> <map>` | headless client under Xvfb with software GL, presses Fire, collects the engine's own screenshots | M5 on / renderer |
| `m7-effects.sh`, `m7b.sh`, `m7b-census.sh`, `m7b-darker.py` | post-process effect pairs (14), projector/shadow/terrain paths against the PC path, scene census, "how much darker" measure | M7, M7b / renderer |
| `m11-twin.sh`, `m11-progcmp.py` | every renderer test against the GL4ES/GLES2 twin; compares program-test lines of two logs | M11 / renderer |
| `o9-nvidia.sh`, `o9-census.sh`, `o9/*.scen` (7) | fixed-function stages as GLSL: combiner tests, material census, DM-Azure/Praxis scenarios | O-9 / renderer |
| `fx-owner.sh` | pictures for review items O-5, O-6, O-7 | fx-fixes / renderer |
| `pp-scale.sh`, `pp-measure.py` | post-process blur scale at several resolutions | O-10 / renderer |
| `ps-census.py` | which packages define or use pixel-shader materials | M6 / renderer |
| `m10-controls.sh`, `m10-inject.py` | keyboard, mouse, text and pad scenarios by real X events and SDL's virtual controller; the runner that drives most game checks | M10 / input |
| `d2-ui-scale.sh`, `d2-measure.py`, `d2/*.scen`, `d2/web/*` | UI box at eight window sizes (81 checks) | D-2 / UI |
| `d3-profiles.sh`, `d4-names.sh`, `d5-text.sh` | last profile, player name, text entry | D-3..D-5 / profiles |
| `d6-mouse.sh`, `d6-checks.py` | linear mouse look, pad curves, lock-on | D-6 / input |
| `d78.sh`, `d78-measure.py`, `d78/*` | console and AutoFOV | D-7, D-8 / UI |
| `d9-rebind.sh`, `d9/explore.scen` | KEYBOARD / MOUSE settings page | D-9 / input |
| `audio-regression.sh`, `audio-capture-report.sh`, `audio-music-check.py`, `run-uc2-audio.sh` | cooked-audio check, loopback self-test, four wave-writer captures, music identity and level | M8 / audio |
| `m9-movies.py`, `movie-selftest.sh`, `movie-lang-evidence.py`, `xcorr.c` | movie scenarios, ffmpeg-referenced self-test, language-track evidence | M9 / movies |
| `net-test.sh`, `net-test.py`, `e2-web-net.sh` | dedicated/listen servers with clients on 127.0.0.1 in one capped scope; the browser net spec against the local wasm-perf artifact | net-join, E-2 / network |
| `script-build.sh`, `script-diff.py`, `epic-class-order.py`, `m14-scripts-check.sh`, `sc-saveconfig.sh` | `ucc-bin make` of the 17 packages and object-by-object comparison with the shipped ones; the game on rebuilt packages; script `SaveConfig` | M14, saveconfig / scripts |

### tools/ (outside `dev/`)

`cook-audio.py` (M8: XACT project + source WAVs to Ogg Vorbis packs and cue tables), `cook-movies.py` (M9: XMV to Ogg Theora/Vorbis), `psh2glsl.py` (M6:
Xbox pixel shaders to GLSL with a reference interpreter and `--selftest`/`--gputest`), `pshgpu/pshgpu.c` (M6 GPU test helper). Not mine; named for completeness.

### bin/ (in `/home/ion/WorkUC2/bin`, outside the repo and not under version control; read only here)

| Script | What it does |
|---|---|
| `capped <mem> <secs> -- cmd...` (44 lines) | runs the command and its whole process tree in one `systemd-run --user --scope` with `MemoryMax`, `MemorySwapMax=0`, `TasksMax=2000`, `timeout -s TERM -k 5`, core dumps off, `ALSOFT_DRIVERS=null` unless the caller set it; afterwards asks systemd why it ended (exit 137 = memory ceiling, 124 = time limit), kills and stops the scope so nothing survives |
| `mem-watchdog [warn=16] [kill=24] [hours=12]` (30 lines) | every 10 s logs machine memory in use to `logs/mem-watchdog.log`; above 16 GB a WARN line; above 24 GB kills every `uc2-capped-*` scope (and nothing else). The owner's memory policy of 2026-10-04 |
| `publish-preview <engine artifact dir>` (25 lines) | snapshots `SDLLaunch.{js,wasm,data}` + `meta.json`, refuses if the wasm hash differs from `meta.json` (build still linking), runs the frontend `yarn build --target uc2 --engine-artifact ... --out serve/uc2` inside `capped 4G 300`, appends to `serve/PUBLISHED.txt`; the server unit `uc2-preview` (port 8096) serves the directory |
| `play-uc2 [WxH] [uc2.bin args]` (25 lines) | for the owner: rebuilds the run tree `run/play` from `engine-uc2/build-native` every time (inis and `Save/` of the previous tree kept in `run/play-saved`), edits the window size into the tree's `Default.ini`, runs `uc2.bin ... -log` on the owner's display and speakers |
| `oracle-shot.sh <name> [map] [secs]` (282 lines) | runs Epic's original `avaris.exe` (Direct3D 8) under wine with software GL under Xvfb, optionally injects keys and console commands, grabs a screenshot to `logs/oracle/`: the PC-look reference for the renderer. Writes only under `run/oracle`, `.cache/wine-oracle`, `logs/oracle` |
| `extract-xiso` | ELF built from source (v2.7.1) to unpack the Xbox ISOs (`reference/iso-extract`) |
| `yarn`, `yarnpkg` | symlinks to corepack's yarn for the frontend build |

Not read (outside the allowed paths): `env.sh` (sets `EMSDK` for `source ../env.sh`).

---

## Open issues in this area

1. **Real Windows has never run it.** Everything in feature 11 is wine 10 on Mesa llvmpipe, null/wave-file audio and a virtual pad. The 10-item checklist in
   `docs/porting/analysis/windows.md` (double-click start, real GPU driver, WASAPI, XInput pad, mouse/fullscreen/DPI, paths with spaces and non-ASCII, SmartScreen,
   `cmd` server with the firewall prompt, a Linux client joining a Windows server, clean quit) is open. A Windows server against the fixed network client is not recorded.
2. **arm64 has never been configured or built.** Contradictory BLOCKERS entry (installed / still missing). The client needs arm64 X11/GL development packages through
   multiarch.
3. **Exit-time crash/hang of a dedicated server after SIGTERM** (BLOCKERS.md "Open (M13)": `StaticExit` -> `PurgeGarbage` -> `ULinkerLoad::DetachExport`, about 2 maps in 58 per
   census, then a hang in `FFileStream::Destroy` waiting for a thread that is not there). Related use-after-frees were fixed on the network branch; whether this one is gone is
   not recorded. `map-census.sh` still reports PASS for it (its `timeout` has no `-k`; the 64-bit script added a hang watcher).
4. **Fetch refs are not pinned** (SDL2 `release-2.32.x`, OpenAL Soft `ufront/web`, GL4ES `fix/cubemap-bind-active-unit`), the first configure needs network, and the web
   `meta.json` does not record the fetched libraries; its `buildFlags` and header comment are stale.
5. **Documentation drift.** `README.md` ("Status: M0") and the top of `BUILD.md` ("Status: M0") are stale; `BUILD.md` has no web section; `UC2_ASSETS_DIR` is documented and
   unused; `client-boot.md` says `Default.ini` keeps CRLF (it has been LF since `225b0d5`); the `wasm` preset description says DWARF.
6. **Null-`this` tests** in `UObject::IsValid`/`GetFullName` (`UnObj.cpp:235`) can be deleted by GCC/clang at -O2; `-fno-delete-null-pointer-checks` is not set. No failure seen.
7. **Suspected real bugs left as Epic wrote them** (feature 4 list), and `UnParticleEmitter.cpp` `MIN_ATTEN_DIFF` = 0.
8. **The FColor -> FPlane conversion** chosen at three Engine sites and one UCElements site is an assumption about MSVC 7.1; if lighting alpha looks wrong, look there first.
9. **Placeholder sites exercised by no check** (feature 12 list) and the hand-maintained 50-entry table.
10. **Asyncify + JavaScript exceptions** performance in a browser is unmeasured; the 2 GB wasm maximum has no recorded headroom measurement; pool size 4 is tied to OpenAL Soft's threads.
11. **`ucc help` blank descriptions** for native commandlets (cosmetic; possible effect on `class'X'.default.<localized>` in menus not followed up).
12. **Static-link registration:** `UHTTPDownload`/`UPrefab` are registered as `Engine.HTTPDownload`/`Engine.Prefab`; `DownloadManagers=IpDrv.HTTPDownload` names a class that does not exist; package download has never been exercised.
13. **`ToAnsi`/`FromUnicode` write 0x7F for Latin-1 characters >= 0x80** (signed char), so accented text in saved inis or log files may be mangled on every platform (Epic's behaviour; unchanged).
14. **Editor (`ucc-bin make`) exists on 32-bit Linux only.** Everything else uses the shipped `.u` plus three rebuilt packages.
15. **Owner questions still open here:** split-screen on PC (the SDL2 client creates one window per viewport as WinDrv did, nothing else was done), which arm64 hardware (M13), whether to pin the
    fetch refs, and the flat-file versus directory-wildcard choice for web profiles (coordinator decided flat; `22e9dbf` can be reverted if the frontend learns directory wildcards).
16. **The PipeWire package** (`libpipewire-0.3-modules:i386`) for the native 32-bit game's PipeWire backend (BLOCKERS.md).

---

## Places where we changed Epic's shared code

All 136 files that are modified relative to `7633696` (`git diff --name-status 7633696 4813dd9 -- Source`, status M), paths relative to `Source/`.
Class: **P** = a porting fix only (compile, portability, 64-bit, a bug found while porting) - explained in this section; **P+** = a porting fix and a change that
belongs to another section (named); **O** = another section's change only (named; not explained here). Counts: P 95, P+ 18, O 23.

### Core (31)

| File | Class | Change and reason |
|---|---|---|
| Core/Inc/Core.h | P | `SUPPORTS_PRAGMA_PACK` honoured from the build; `PLATFORM_64BITS`; `UnUnix.h` also for MinGW (feature 2, 12) |
| Core/Inc/FFileManagerGeneric.h | P | vendored zlib by path; POSIX `stat`; `DeleteFiles` cuts at `/` too (profile deletion never worked on POSIX) |
| Core/Inc/FFileManagerLinux.h | P | stale UT2003 stdio file manager replaced by a shim to `FFileManagerUnix.h` |
| Core/Inc/FMallocAnsi.h | P | guard message cast through `PTRINT` (compiled only where guards are try/catch: MinGW) |
| Core/Inc/FOutputDeviceFile.h | P | base-dir prefix only for MSVC (MinGW uses FFileManagerUnix) |
| Core/Inc/UnArc.h | P | `FArchive << PTRINT` for counting/GC archives over `TArray<PTRINT>` (64-bit) |
| Core/Inc/UnCorObj.h | P | `#pragma pack(4)` around `UCommandlet`'s tail |
| Core/Inc/UnCoreNative.h | P | native-function lookup ignores case (M10: an ini key `Delete` made `UObject`'s `delete` look for a differently cased symbol and the engine stopped at start-up) |
| Core/Inc/UnFile.h | P | `check`/`checkSlow` without `__noop`; `NEW T[n]` form; sized deletes; `UT_REPLACEMENT_ALLOC`; declarations of `appIsGlobalOptionToken`, `appApplyIniOverrides` |
| Core/Inc/UnGnuG.h | P | rebased on engine-ut2004 with UC2's platform contract (feature 4) |
| Core/Inc/UnMath.h | P | x87 asm guarded and the C transform path on GCC; `SafeNormal` extra qualification |
| Core/Inc/UnMem.h | P | default arguments moved from the friend `operator new` declarations to the definitions; `PTRINT` alignment in `FMemStack` |
| Core/Inc/UnName.h | P | implicit `int` return of `FNameEntry::GetName` |
| Core/Inc/UnObjBas.h | P | object-index bytecode slots on 64-bit; `new(...) TClass` without value-initialisation; `T::GetTransientPackage()` and `template` keyword; pack(4); Editor class accessors |
| Core/Inc/UnScript.h | P | `FFrame::ReadObject` through the object table |
| Core/Inc/UnTemplate.h | P | implicit ints, two-phase lookup (`this->`), `typename`/`template`, `TPooledList` parameter shadowing, `PTRINT` in `Align`/`STRUCT_OFFSET`, `FString::Mid` unsigned overflow |
| Core/Inc/UnType.h | P | forward declaration of `FindFieldNamed` |
| Core/Inc/UnUnix.h | P | `appGetSystemDllHandle` declaration; `appGetLocalIP` returns `DWORD` |
| Core/Inc/UnVcWin32.h | O | movies: `FMovieThread`/`FMoviePlayer` moved to the new `UnMovieBase.h` |
| Core/Inc/wordwrap.h | P | portable Win32 type subset; MinGW `WCHAR` clash |
| Core/Src/UnAnsi.cpp | P | const results of `strstr/strchr/strrchr`; `copysign`, `atoll`; `appAtoi` wraps like MSVC (M14) |
| Core/Src/UnClass.cpp | P+ | 64-bit native-size recorder, 50 placeholders, config-name stash; uninitialised `StructFlags`/`ScriptTextCRC`; (saveconfig: `UC2NoteDefaultOverrides` hooks) |
| Core/Src/UnCorSc.cpp | P+ | 64-bit None-context fill and switch width, bytecode property slot via the object table; (saveconfig: the two script natives) |
| Core/Src/UnCoreNet.cpp | O | network: package map counts only the file's names/exports; DevNet table line |
| Core/Src/UnGUID.cpp | P | include `Core.h`; `unsigned32` is 32 bits |
| Core/Src/UnMisc.cpp | P | `appInit` keeps the first token off MSVC; `appItoa`/`appI64toa`; `appIsGlobalOptionToken`; `appApplyIniOverrides`; reads `ScriptSaveConfig` |
| Core/Src/UnName.cpp | P | namespace-scope prototype of `AllocateNameEntry` |
| Core/Src/UnObj.cpp | P+ | overlapping `strcpy` in `ResolveName`; registration stashes; GC pointer copy; `RemoveNames` flush; `\` in `#exec` file names; import sort (D-9); (saveconfig: `SAVECONFIGKEYS`, changed-only save) |
| Core/Src/UnProp.cpp | P | object properties align to 4 |
| Core/Src/UnUnix.cpp | P | rebased platform layer (feature 4, 11) |
| Core/Src/wordwrap.cpp | P | portable branch; Win32 code-page parts compiled out |

### Editor (12)

| File | Class | Change and reason |
|---|---|---|
| Editor/Inc/Editor.h | P | `GCC_PACK(4)` on `AutosaveTimeMinutes`; `FObjectExport` forward declaration |
| Editor/Inc/EditorClasses.h | P | three `GCC_PACK(4)` first-member tags |
| Editor/Inc/Perlin.h | P | `register INT` |
| Editor/Src/TTFontImport.cpp | P | GDI rasterising only under `_MSC_VER` |
| Editor/Src/UAnalyzeMeshesCommandlet.cpp | P | undef glibc's `MAXFLOAT` |
| Editor/Src/UMakeCommandlet.cpp | P+ | NTFS file order, `-CONFORMNOCONFIG`, native defaults as a DLL load gave them (scripts: reproducing Epic's compile) |
| Editor/Src/UStripSourceCommandlet.cpp | P+ | NTFS order, `-CONFORM` (scripts) |
| Editor/Src/UnEdExp.cpp | P | header exporter: tag only a run's first bool, tag a class's first member |
| Editor/Src/UnEdSrv.cpp | P | `\` in `#exec` file names |
| Editor/Src/UnParams.cpp | P | Editor's copies of five parsers `#if !__STATIC_LINK` (Engine has them) |
| Editor/Src/UnScrCom.cpp | P+ | class walk in Epic's registration order, `-NOEPICORDER` (scripts) |
| Editor/Src/UnVisi.cpp | P | pointer-to-member syntax |

### Engine (68)

| File | Class | Change and reason |
|---|---|---|
| Engine/Classes/PSDistortionBaseMaterial.uc | O | renderer (script side of the PC distortion path) |
| Engine/Inc/AActor.h | P | `inline`/`FORCEINLINE` removed from `GetGravity`, `AddToRecentMap` (13 unresolved at first link) |
| Engine/Inc/ALevelInfo.h | P | same, `NotifyPauseChange` |
| Engine/Inc/APawn.h | P | extra qualification on in-class declarations |
| Engine/Inc/Engine.h | P | `CheckNotBool` without `__noop`; pack(4) around `FOrientation` |
| Engine/Inc/EngineClasses.h | P+ | generated header: `GCC_PACK(4)` tags (131 added, 8 removed), `PTRINT` for pointer-holding ints; (renderer: the M7 capability field) |
| Engine/Inc/ULoadSaveManager.h | P | a saved object reference is 4 bytes on every width |
| Engine/Inc/UParticleEmitter.h | P | inline marker, `UpdateRelativeRot` |
| Engine/Inc/UReachSpec.h | P | inline marker, `supports` |
| Engine/Inc/UnAudio.h | O | audio/movies: `OpenMovieAudio` |
| Engine/Inc/UnCamera.h | O | UI: D-2 `UIScaleMode`, `UIMenuBackground`, `UISharpText` |
| Engine/Inc/UnCameraEffects.h | P | pack(4), `BlurAlpha` first-member tag, one stray tag removed |
| Engine/Inc/UnCanvas.h | P+ | pack(4); (UI: D-2 `ApplyUIBox` and friends) |
| Engine/Inc/UnChan.h | P | extra qualification on `GetMapValueAddr` |
| Engine/Inc/UnEngine.h | P | pack(4) |
| Engine/Inc/UnFluidSurface.h | P | pack(4) |
| Engine/Inc/UnForceFeedbackWaveform.h | P | pack(4) (the baseline's "cannot bind packed field" errors, 212, came from this header) |
| Engine/Inc/UnGame.h | P | pack(4) |
| Engine/Inc/UnLevel.h | P | inline marker, `IsPaused` |
| Engine/Inc/UnMaterial.h | P | pack(4) |
| Engine/Inc/UnParticleSystem.h | P | pack(4): `FParticleTrailData` 28 vs 32 on wasm32 (M4w) |
| Engine/Inc/UnPlayer.h | P | pack(4) |
| Engine/Inc/UnPlayerStats.h | P | extra qualification on `DebugDump` |
| Engine/Inc/UnPrim.h | P | namespace-scope `CompareHits` declaration (its address is taken) |
| Engine/Inc/UnRenDev.h | P+ | `BEGIN_D3D_EVENT` as a variadic no-op off MSVC; (renderer: capabilities and render-target interface) |
| Engine/Inc/UnRender.h | P+ | inline marker, `FDynamicActor::UpdateSorting`; (renderer/UI: AutoFOV, scoped helpers) |
| Engine/Inc/UnRenderResource.h | P | `SetCoord` return type `void` |
| Engine/Inc/UnRenderUtil.h | O | renderer: post-process reference size, UI canvas state |
| Engine/Inc/UnTerrain.h | P | pack(4), first-member tag |
| Engine/Inc/UnTex.h | P | pack(4) (`UTexture` 208 vs 204 on wasm32, M4w); inline marker `GetCharSize` |
| Engine/Inc/UnURL.h | P | pack(4) around `FURL` |
| Engine/Src/AFileLog.cpp | P | `LogAr` stored as `PTRINT` |
| Engine/Src/FContentManager.cpp | P | pointer-to-member syntax in the handler table |
| Engine/Src/ULoadSaveManager.cpp | P | web: one flat file per profile (feature 10) |
| Engine/Src/UnActor.cpp | P | inline markers on `AddToRecentMap`, `IsJoinedTo` |
| Engine/Src/UnAudio.cpp | P | RIFF chunk bound through `PTRINT` |
| Engine/Src/UnCamMgr.cpp | P+ | `Compare(UViewport*&,UViewport*&)` const fix; (UI: D-2 enums, properties and `UISCALE` command) |
| Engine/Src/UnCanvas.cpp | O | UI: D-2 canvas box, sharper text |
| Engine/Src/UnChan.cpp | O | network: `RF_DebugDestroy` on a destroyed channel, test hooks |
| Engine/Src/UnConn.cpp | O | network: handshake resend and ack logic |
| Engine/Src/UnController.cpp | O | UI/input: D-8 AutoFOV in `WorldToScreen`/`ScreenToWorld`, D-2 mapping |
| Engine/Src/UnEngine.cpp | P+ | `FLUSHCONFIG` (feature 9); (movies: full-screen movie owns the input) |
| Engine/Src/UnFont.cpp | P | inline marker, `GetCharSize` |
| Engine/Src/UnGame.cpp | P+ | `appIsGlobalOptionToken` in the map choice (`:338`); (profiles D-3/D-4, console D-7, AutoFOV D-8, movies, M7b projectors, network `?connect`) |
| Engine/Src/UnIn.cpp | P+ | `UInput` alias struct by `sizeof(FAlias)`/`STRUCT_OFFSET`, `PTRINT` binding offsets; (input: stick scaling) |
| Engine/Src/UnInteraction.cpp | P | literal function name where `guard()` omits `__FUNC_NAME__` on `__LINUX__` |
| Engine/Src/UnInteractionMaster.cpp | O | input: console owns a key's release (D-7) |
| Engine/Src/UnMaterial.cpp | P | `MovieMaterial` pointer fields as `PTRINT` |
| Engine/Src/UnModelLight.cpp | P | nvDXT block `&& _MSC_VER` |
| Engine/Src/UnNetDrv.cpp | O | network: test hooks |
| Engine/Src/UnOctree.cpp | P | `OctreeNodes` as `PTRINT` |
| Engine/Src/UnParticleEmitter.cpp | P | `PoolInstance` specialisation; typed `MIN_ATTEN_DIFF` (value kept 0); `Particles` as `PTRINT` |
| Engine/Src/UnPenLev.cpp | O | network: handshake progress timeout |
| Engine/Src/UnPixelShaderMaterial.cpp | O | renderer: distortion/PS gates lifted behind capabilities |
| Engine/Src/UnRender.cpp | P+ | two `PoolInstance` specialisations; (renderer: AutoFOV, post-process hooks) |
| Engine/Src/UnRenderBatch.cpp | P | `FloatToInt` via `lrintf`; `.operator FPlane()` |
| Engine/Src/UnRenderResource.cpp | O | renderer: M7 additions |
| Engine/Src/UnRenderStaticMesh.cpp | P | `FloatToInt` via `lrintf`; `.operator FPlane()` |
| Engine/Src/UnRenderUtil.cpp | P+ | the two `F32vec4` blocks `#if __HAS_SSE__`; (renderer: tile draw order, post-process reference, UI canvas state) |
| Engine/Src/UnRenderVisibility.cpp | P | `PoolInstance` specialisation |
| Engine/Src/UnScript.cpp | P | `StateInfo` fields as `PTRINT`; inline marker `NotifyPauseChange` |
| Engine/Src/UnScriptPatcher.cpp | O | network/scripts: BeginLoad/EndLoad around `AppendImport`; rebuilt packages skip the patch |
| Engine/Src/UnShadowProjector.cpp | O | renderer: M7b Xbox shadow path |
| Engine/Src/UnStats.cpp | P | invalid `##` pastes removed; `PTRINT` in a `memset` length |
| Engine/Src/UnTex.cpp | P | `FString` passed through `...` fixed in two `debugf` |
| Engine/Src/UnUCRibbonEmitter.cpp | P | inline marker, `FLinterp` |
| Engine/Src/UnUCTrailEmitter.cpp | P | inline marker, `FLinterp` |
| Engine/Src/xbstrip.cpp | P | standalone XDK sample: scalar typedefs instead of `<D3D8.h>`, pre-standard for-scope, const `operator<` |

### GUI, IpDrv, launchers, UCElements, UCGameplay, UCMenu, UWeb (25)

| File | Class | Change and reason |
|---|---|---|
| GUI/Classes/GUIComponent.uc | P | `SaveCanvasState`/`RestoreCanvasState` lose `FORCEINLINE` in the exporter's source of `GUIClasses.h`, so a regeneration keeps the fix |
| GUI/Classes/GUIController.uc | O | UI/profiles: `SetProfilePlayerName` (D-4), config flags (D-3) |
| GUI/Inc/GUIClasses.h | P | generated header: `GCC_PACK(4)` tags, `Designer` as `PTRINT`, inline markers |
| GUI/Inc/UnUIXInteraction.h | P | Win32 scalar types off Windows; pack(4) |
| GUI/Src/GUI.cpp | P | `<wtypes.h>` only on Windows |
| GUI/Src/UnGUICore.cpp | P+ | inline marker `CheckCanvasClip`; `Designer` stored as `PTRINT`; (UI/input: menu key table as data, mouse, typing) |
| IpDrv/Inc/IpDrvClasses.h | P | generated header: `PrivateResolveInfo`, `LinkPtr`, `MSLinkPtr` as `PTRINT`; tags |
| IpDrv/Inc/UnIpDrv.h | P | WinSock by `_WIN32`; `ws2tcpip.h`; `#undef` of four Win32 macros |
| IpDrv/Inc/UnSocket.h | P | `WSAEHOSTUNREACH` alias |
| IpDrv/Src/MasterServerClient.cpp | P | `LinkPtr`/`MSLinkPtr` as `PTRINT` |
| IpDrv/Src/MasterServerUplink.cpp | P | `LinkPtr` as `PTRINT`; extra qualification |
| IpDrv/Src/UnSocket.cpp | P | resolver error codes not on Emscripten (same values as errno constants); `setsockopt` cast by `_WIN32` |
| SDLLaunch/Src/SDLLaunch.cpp | P | Epic's SDL 1.2 launcher rewritten for SDL2 and the static link (feature 7) |
| SDLLaunch/Src/SDLLaunchPrivate.h | P | rewritten with it |
| UCC/Src/UCC.cpp | P | static registration, Posix factories, `verifyclasses`, `profiletest`, MinGW (features 5, 6, 11, 12) |
| UCElements/Inc/UCElementsClasses.h | P | generated header tags |
| UCElements/Src/UCElements.cpp | P | `.operator FPlane()` |
| UCGameplay/Classes/UCPlayerController.uc | O | scripts/input (D-6) |
| UCGameplay/Classes/UCPlayerInput.uc | O | scripts/input (D-6 mouse look) |
| UCGameplay/Classes/UCTrainingController.uc | O | scripts |
| UCGameplay/Inc/UCCameraEffects.h | P+ | `UCScreenDOF` header brought in step with its script (`FirstPersonAlpha`, `ThirdPersonAlpha`), pack(4), tags; (renderer: M7 hook) |
| UCGameplay/Inc/UCGameplayClasses.h | P | generated header tags |
| UCGameplay/Src/UCCameraEffects.cpp | O | renderer: post-process gates lifted |
| UCMenu/Classes/UC2Settings_Main.uc | O | UI script: KEYBOARD / MOUSE entry (D-9) |
| UWeb/Inc/UWeb.h | P | pack(4) around the hand-written mirrors |

Counted from the tables (checked against `git diff --name-status`: 136 rows, no file missing or extra): Core 26 P + 3 P+ + 2 O; Editor 9 P + 3 P+; Engine 43 P + 10 P+ +
15 O; GUI, IpDrv, launchers, UCElements, UCGameplay, UCMenu, UWeb 17 P + 2 P+ + 6 O; total 95 P, 18 P+, 23 O.


---

# 20. The renderer (OpenGL, GLES, WebGL)

Scope: everything that makes UC2 draw on OpenGL / OpenGL ES / WebGL: the new driver `Source/UC2GLDrv`, its GL4ES
binding `Source/UC2GL4ESDrv`, the Xbox shader translation, render targets and post-processing, projectors,
shadows and three-layer terrain, dynamic gamma, the GL side of movie textures, anti-aliasing and anisotropy, the
web renderer, and the render-related ini keys. Baseline `7633696`, code read at main `4813dd9` (snapshot
`engine-uc2-report`). Line numbers are the snapshot's. Features are numbered R-1..R-22; owner items (O-n) and
milestones (Mn) are named where they apply.

## The things to know first

1. **One new driver, built from three sources.** `Source/UC2GLDrv` has 127 files: 25 C++ sources, headers and
   CMake files (about 19,700 lines) plus 102 generated shader files. It is a three-way merge: engine-ut2004's
   `OpenGLDrv` gave the GL structure, UC2's `D3DDrv` (Direct3D 8, in the tree, never built) is the
   specification. The material compiler is UC2's `D3DMaterialState.cpp` carried over function for function,
   stages kept in Direct3D vocabulary and translated to GL in one place: after renaming the types it still
   differs from Epic's file by only 56 removed / 168 added lines, so it can be diffed against Epic.
2. **The Xbox look, not the PC look, is the target.** Epic's PC build drew every Xbox-only material with its
   first texture, made distortion opaque (the flat periwinkle DM-Praxis panes) and had no post-processing at
   all. We translated all 100 Xbox pixel shaders (`.psh`) to GLSL with a tool that is checked against
   Microsoft's assembler output and on the GPU, ported the Xbox vertex-shader fragments the PC data can use,
   and lifted Epic's `_XBOX` gates through named render capabilities (`FRenderCaps::XboxPostProcess`,
   `XboxProjectors`, `XboxShadows`, `MaxSimultaneousTerrainLayers`, `PostProcessReferenceHeight`). `_XBOX` is
   never defined; both branches of every lifted gate stay in the source.
3. **There is no Xbox reference picture for most of it.** The evidence is numeric (shader translator against
   the assembler's definition words, GLSL against a reference interpreter, a driver self-test of every
   post-process primitive), plus pictures compared with Epic's PC binary under wine where PC drew the same
   thing, plus the owner's xemu screenshots for a few owner reports. "Looks like the Xbox" is unverified for
   bloom/glow strength, dynamic gamma, distortion size, the shadows and the projector look.
4. **One driver for desktop, GLES and WebGL.** The web build is the same driver behind GL4ES (desktop GL calls
   translated to OpenGL ES 2 = WebGL 1; no WebGL 2), chosen at run time by a subclass (`UC2GL4ESDrv`). Linux
   builds it too as a "native twin" so every renderer test runs on the web code path without a browser.
   Fallbacks for weak WebGL devices exist except one: without depth textures, depth of field and the distortion
   occlusion are lost.
5. **Owner-driven tuning on top of Epic's values, each one setting away from the Xbox value:** depth-of-field
   alpha = the shipped 65 (O-3), distortion fade distance 15000 instead of the Xbox 5000 (O-7), post-process
   blurs sized for a 480-line picture at any resolution (O-10), fixed-function stages as generated GLSL so
   NVIDIA draws what Mesa draws (O-9), AA 4x and anisotropy 16x by default (O-8). Default.ini and the web ini
   carry the same render keys.

Costs in one line: post-processing is expensive (3.3x-3.8x frame time on software GL; on the owner's Quadro P620
single views measured 9-12 ms a frame from 720p to 4K with everything on, never broken down per effect); twelve
of Epic's shared files are changed (listed at the end); the driver carries about 2,500 lines of test and debug
code; 12 of the 25 shaders M6 left for post-processing were never seen drawn (5 of them unreachable or unused
by design).

## Features

### R-1 The OpenGL render device `UC2GLDrv` (M5, fixed-function renderer)

**What the player/owner sees.** The game draws with OpenGL instead of Direct3D 8: menus, maps, lightmaps, meshes,
fog, projectors, the HUD. It is the default device on every platform (`RenderDevice=UC2GLDrv.UC2GLRenderDevice`).

**Where.**
- New code, `Source/UC2GLDrv` (127 files; 19,660 lines in `Src/` + `Inc/`, plus 102 generated files in
  `Shaders/`). Grouped by job:

  | Group | Files | What it does |
  |---|---|---|
  | Package, build | `CMakeLists.txt`, `Src/UC2GLDrv.cpp`, `Inc/UC2GLDrv.h` | static library `GPackage=GUC2GLDrvPackage`, links Engine + SDL2, **no libGL** (SDL loads it); `Shaders/` on the include path |
  | GL binding and entry points | `Inc/UC2GLBinding.h`, `Inc/UC2GLFuncs.h` (X-macro list of the GL entry points + extension flags), `Inc/UC2GLRawFuncs.h` (M11: GLES2 calls that must bypass GL4ES) | every GL call goes through the device's pointer table; prototypes are deliberately not declared, so a stray direct call fails to link |
  | Direct3D vocabulary | `Inc/UC2GLD3DTypes.h` (D3D8 fixed-function enums with d3d8types.h values, `D3DMATRIX`), `Src/UC2GLD3DXCompat.h` (D3DX matrix helpers on `FMatrix`; M7b added the projector matrices) | lets the material compiler keep Epic's text |
  | Device | `Inc/UC2GLRenderDevice.h`, `Src/UC2GLRenderDevice.cpp` (2,438) | `UUC2GLRenderDevice`: proc table, `Init` (:532), `SetRes` (:693), main framebuffer (`CreateMainFramebuffer` :828), present pass and gamma (`UpdateGamma` :1857, `Present` :2226), console commands (`Exec` :1878), `ReadPixels` (:2316, top row first, BGRA as D3D), `GetRenderCaps` (:2368), all test switches |
  | Render interface | `Inc/UC2GLRenderInterface.h`, `Src/UC2GLRenderInterface.cpp` (2,318) | UC2's `FRenderInterface`: saved-state / render-target / viewport / stencil stacks, lights (`SetLight` :590, UC2's directional-only selection), fog, transforms and the D3D-to-GL clip fix-up (`GetGLMatrices` :1763), **the one place where D3D stage state becomes GL** (`SetMaterialBlending` :1354, `TranslateStageOp`, `CommitTexCoords` :1958), streams, `DrawPrimitive` (:2066), occlusion queries (:2287-2310) |
  | Material compiler | `Src/UC2GLMaterialState.cpp` (4,898), `Src/UC2GLPSMaterialState.cpp` (1,002) | UC2's `D3DMaterialState.cpp` (4,786) and `D3DPSMaterialState.cpp` (986) carried over; after renaming `FD3D`/`UD3D` the differences are 56/168 and 20/36 lines (removed/added, measured with `diff -w` for this report); every deliberate change is marked `UC2GLDrv:` |
  | Resources | `Inc/UC2GLResource.h`, `Src/UC2GLResource.cpp` (1,253) | UC2's 16-bit cache ids per `ECacheHint`, textures (RGBA8, P8, G16, DXT1/3/5, L8, A8, R5G6B5, cube maps, composite lightmaps), render targets (texture + FBO + own or shared depth), VBO vertex/index buffers, D3DCOLOR-to-RGBA swizzle on upload |
  | Programmable path (R-3/R-4) | `Src/UC2GLShaders.cpp` (1,327), `Shaders/*` | see R-3, R-4 |
  | Post-processing (R-6..R-13) | `Src/UC2GLPostProcess.cpp` (1,429) | see R-6 |
  | Fixed function as GLSL (R-5) | `Src/UC2GLFFProgram.cpp` (360) | see R-5 |
  | Movies (R-17) | `Src/UC2GLMovie.cpp` (116) | see R-17 |
  | Tests and diagnostics (R-21) | `Src/UC2GLSelfTest.cpp` (1,195), `UC2GLCombinerTest.cpp` (652), `UC2GLPixelTrace.cpp` (289), `UC2GLSceneCensus.cpp` (220), `UC2GLShadowCheck.cpp` (147) | numeric self-tests and console probes compiled into the shipping driver |

- Outside the driver (our own port modules, not Epic's): `Source/SDL2Drv/Src/SDL2Viewport.cpp:681`
  `EnsureGLContext` (re-creates the plain window as `SDL_WINDOW_OPENGL` with the same title/position/size and
  creates the context when the device asks for `BLIT_OpenGL`; `DestroyGLContext` :722; member `GLContext` in
  `Inc/SDL2Drv.h:166`); `Source/SDLLaunch/Src/SDLLaunch.cpp:305` switch `-opengl` (and `-gl4es` :306),
  registration of the package (:39-42, :518); `Source/SDLLaunch/CMakeLists.txt:21-28`; root `CMakeLists.txt:412-426`
  (`UC2GLDrv` in `UC2_PACKAGES`, built on every target since M11).
- Epic's code: **none changed at M5** (later features change some; see the list at the end). Epic's `D3DDrv`
  stays in the tree, unbuilt.
- ini: `Engine/Config/Default.ini:19` `[Engine.Engine] RenderDevice=UC2GLDrv.UC2GLRenderDevice` (was
  `D3DDrv.D3DRenderDevice`; the null device is a commented line); new section `[UC2GLDrv.UC2GLRenderDevice]`
  (`Default.ini:377-406`, keys in R-22).
- Commits: `536073e` (decision: one GL driver source), `f107ffb` (driver R1), `1700520` (SDL2Drv GL context),
  `34dd131` (launcher, default device), `b0aac24` (gate evidence); merged as `18224d9`.

**How the parts fit.** The engine talks to the device through UC2's `FRenderInterface` exactly as it talked to
`D3DDrv`. The material compiler (Epic's text) fills a list of passes in Direct3D terms (stage colour/alpha ops
and arguments, blend, address, filter, texcoord index and texture-transform flags); `SetMaterialBlending` and
`CommitTexCoords` translate that to GL texture environment, blend and texture state at draw time. Since O-9
(R-5) the same D3D stage list is normally turned into a generated GLSL fragment program instead. `GIsOpenGL`
stays 0 (`UC2GLRenderDevice.cpp:540`): the engine keeps D3D colour and pixel conventions and the driver converts
(the sibling UT2004 driver set it to 1). SDL2Drv only creates the window and context; the device draws into an
offscreen main framebuffer and presents it (R-2).

**Why.** The owner's decision of 2026-10-04 (PLAN.md "Owner decisions"): "New GL driver by merge (UT2004
OpenGLDrv/GL4ESDrv + UC2 D3DDrv as spec)". The exploration found UC2's D3DDrv a strict superset of UT2004's
(design.md F2: RenderInterface 36/36, MaterialState 26/26 functions, plus about 44 UC2-only interface functions
and about 1,400 added material-compiler lines), and UT2004's OpenGLDrv a mirror of UT2004's D3DDrv. ANGLE,
DXGL and WineD3D were rejected at planning (PLAN.md "What the exploration settled").

**Decisions and alternatives.**
- One driver source for desktop, GLES and WebGL (LOG.md 2026-10-04) instead of engine-ut2004's two mirrored
  directories (`OpenGLDrv` + `GL4ESDrv`). The binding is chosen at run time by class (R-19).
- Keep the material compiler in D3D vocabulary and translate in one place, instead of hand-translating the
  ~1,400 UC2-only lines into inline GL calls as the sibling driver does. Reason: the file stays diffable
  against Epic's, and the same stage description is what the GLSL generator (R-5) and GL4ES consume.
- Dropped from the sibling: NV_VAR / ATI_VAO / APPLE_VAO / ATI element arrays, ARB_fp terrain shaders, DE's
  dynamic-quad helpers, WGL (gl-renderer.md "Merge table").
- Push buffers, `DrawPrimitiveUP`, fences, real occlusion queries are `_XBOX`-only at every call site: left at
  PC behaviour (design.md F3).
- Never define `XBOX_ASSET_FORMAT` (it changes serialized layouts): skeletal meshes stay CPU-skinned as on PC.

**Disadvantages, risks, deviations.**
- A large body of code to maintain (about 20k lines + generated shaders), only partly shared with the UT2004
  sibling: fixes there do not flow here automatically and vice versa.
- Occlusion queries always answer "visible" (`GetOcclusionQueryResults` writes `0x7fffffff`,
  `UC2GLRenderInterface.cpp:2305`): no occlusion culling benefit. Epic's PC stub never wrote the count at all
  (undefined behaviour); ours is defined but does no culling. Since M11 the engine's query boxes are not drawn
  while a query is open (`:2072-2078`).
- Deliberate differences from Epic's D3DDrv found while porting: UC2's `TResourceCache::Flush` could unlink the
  wrong chain entry (ours unlinks exactly the id); Epic's ambient-light colours are added as packed DWORDs with
  wrap-around (reproduced on purpose). The sibling's `glPolygonMode` constant condition, switch fall-throughs
  and texcoord-by-order binding were not carried over (gl-renderer.md "Defects found").
- The texture-matrix padding rules and the clip fix-up (z' = 2z-w, half-pixel shift, flip for render-target
  textures) were reasoned from D3D's rules, then confirmed indirectly by the M7 numeric self-test (R-6).

**Status.** Measured at the M5 gate (LOG.md 2026-10-04, Xvfb + Mesa llvmpipe): menu 1,200 frames and DM-Deadbolt
2,400 frames with 0 GL errors and 0 Critical; census 58/58 maps; `verifyclasses`, null renderer still boot. Seen:
pictures in `<WorkUC2>/logs/m5/` (DM-Praxis from the same spawn as Epic's PC binary under wine agrees in
geometry, textures, lighting, HUD). On the owner's NVIDIA Quadro P620: run since O-5/O-9 (headless EGL).
Real Windows: never (wine only, M12).

**Off switch.** `-nullrenderer` or `RenderDevice=Engine.NullRenderDevice`. Note: a device chosen on the command
line persists into the user's ini (gl-renderer.md "Untested / unsure"), so a later run without the switch keeps it.

### R-2 Window, main framebuffer, present pass and static gamma (M5)

**What the player/owner sees.** The picture appears in an SDL window; Brightness/Contrast/Gamma from the
client settings are applied to it, also in a window (PC applied the ramp only in full screen).

**Where.**
- `Src/UC2GLRenderDevice.cpp`: `CreateMainFramebuffer` (:828): an RGBA8 texture + depth-stencil (a texture
  since M7, so it can be sampled; multisampled renderbuffers since O-8, R-18); the present pass, shader
  `GPresentFragmentShader` (:1486; GLSL 1.20 written so it is also ESSL 1.00), D3D's gamma ramp formula, a 256x1
  ramp texture for dynamic gamma (R-9) and FXAA (R-18); `UpdateGamma` (:1857) reads `UClient::Gamma/
  Brightness/Contrast`; `RestoreGamma` does nothing (the display's own ramp is never touched); `BindingSwap` /
  `BindingSetSwapInterval` (:462-469; `UseVSync`).
- SDL2Drv: R-1.
- Commits: `f107ffb`, `1700520`; present pass extended by `47d3b6d` (M7), `6921897` (O-8).

**How the parts fit.** The engine draws into "the back buffer", which is our main framebuffer object (drawn the
GL way, bottom row first). `Present` (:2226) runs the present pass into the window's framebuffer and swaps.
Every reader that needs the back buffer as a texture gets a flipped copy (R-6); screenshots (`ReadPixels`) are
read from the main framebuffer before the present pass, so they do not contain the gamma ramp unless
`-glshotramp` is given.

**Why.** D3D's hardware gamma ramp has no portable GL equivalent and WebGL has none at all; applying it in a
shader works everywhere and leaves the desktop's ramp alone (design.md "GL4ES/WebGL limits": "no hardware
gamma (do it in the present shader)").

**Decisions and alternatives.** Offscreen main framebuffer + present shader instead of drawing into the
window's framebuffer: costs one full-screen pass per frame, gives gamma, FXAA and a sampleable depth buffer.

**Disadvantages, risks, deviations.** The ramp is applied in windowed mode too (PC: full screen only). One
extra full-screen pass per frame. `-NOGAMMA` turns the static ramp off.

**Status.** Measured: menu and maps with 0 GL errors (R-1). The gamma formula is Epic's; seen, not compared with
a calibrated PC.

**Off switch.** `-NOGAMMA` (static ramp off). The present pass itself cannot be turned off.

### R-3 The Xbox pixel-shader translator `tools/psh2glsl.py` and the generated GLSL (M6, step S1)

**What the player/owner sees.** Nothing directly: it produces the GLSL that R-4, R-6 and R-14..R-16 draw with.
All 100 of Epic's Xbox pixel shaders exist as GLSL.

**Where.**
- `tools/psh2glsl.py` (1,564 lines, Python standard library only; commit `8289fbf`, never changed since):
  - parser of Epic's `.psh` text (`parse_psh` :108; `xps.1.1` and three `ps.1.1` files; `+` pairing, `xmma`,
    `xdd`, `xfc`, modifiers, masks);
  - **path A**, a reference interpreter of the text from the XDK's instruction pages (`run_text` :507);
  - **path B**, an independent evaluator of the 60 definition words of the compiled `.h` Epic ships next to each
    source (`D3DPIXELSHADERDEF`, the register-combiner hardware model; `load_def` :561, `run_def` :633);
  - the GLSL generator (`to_glsl` :918): one fragment body per `.psh`, GLSL 1.20 that is also GLSL ES 1.00, no
    fixed-function built-ins (inputs `u_tex0..3`, `u_c0..7`, `u_fogColor`, `v_color0/1`, `v_fog`, `v_tex0..3`);
  - commands `--selftest` (A against B, 64 seeded cases per shader + 7 synthetic shaders worked out by hand),
    `--emit`, `--check-emitted` (fails if the checked-in files differ from the tool's output), `--gputest`,
    `--dump NAME` (`main` :1530).
- `tools/pshgpu/pshgpu.c` (406 lines; X11 + GLX + GL 2.0 + FBO): draws every test case of every shader as a quad
  and compares the framebuffer with path A; built by the tool into `build-pshgpu/`.
- Generated, checked in: `Source/UC2GLDrv/Shaders/*.glsl` (100 files, one per `.psh`, including the 32
  `DualDiffuse_*` variants), `Shaders/EPixelShader.tsv` (enum value, name, file; derived by the tool from the
  `_XBOX` switch of `UD3DRenderDevice::GetPixelShader`, `D3DRenderDevice.cpp` 4110-4320), `Shaders/
  UC2GLPixelShaders.inc` (7,930 lines: the same sources as C string literals plus the table
  `GUC2GLPixelShaders[PS_MAX]` with per-stage texture operation, constants read, fog use), so a run tree needs no
  shader files.
- Input, unchanged: `epic-sources/ShaderData/PixelShaders` (100 `.psh` + their compiled `.h`).
- Tools: `tools/dev/ps-census.py` (which pixel-shader material classes the packages contain, per map).

**How the parts fit.** The tool is offline. The driver only sees `UC2GLPixelShaders.inc`; it prepends
`#version 120` (desktop) or `#version 100` + precision (GLES) and per-variant defines (`UC2_TEXn_CUBE` for a
bound cube map, the Xbox's `TEXMODE_ADJUST`; `UC2_SPECULARENABLE`; `UC2_ALPHATEST`, used on GLES only).

**Why.** Epic's PC build drew Xbox-only pixel-shader materials with texture stage 0 (`D3DMaterialState.cpp`
1468-1507, design.md F1); the brief asks for the Xbox look. The shaders are readable text, so translating them
is possible; a hand translation of 100 shaders would not be checkable.

**Decisions and alternatives.**
- One generated file per `.psh`, including the 32 DualDiffuse variants (design.md had suggested collapsing them
  into one parametrised source): a second mechanism would be one more thing to keep right.
- Two independent readings (text interpreter vs Microsoft's assembler output) instead of one; floats instead of
  modelling the NV2A's 9-bit fixed-point combiners.
- GLSL 1.20 written ESSL-1.00-compatible from day one, so GL4ES/WebGL could take the same text (it did, R-19).

**Disadvantages, risks, deviations.**
- 8-bit-plus-sign combiner precision is not modelled: the Xbox had banding we do not, notably in the
  `xmma_x4` "all or nothing" masks.
- Two semantic rules are not exercised by any of Epic's sources (default blue source of the `xfc` alpha argument;
  paired halves reading each other's destination); covered only by the hand-worked synthetic shaders.
- `ColorRemapGrayScale.h` is missing from Epic's tree (Epic's own Xbox build would need `xsasm` first): that one
  shader has only path A and the GPU check as evidence.
- Several inferences with no document: fog factor 1 when fog is off, opaque black for a stage a material leaves
  empty, initial values of unwritten registers (shaders.md "Unsure / inferred").
- Evidence is "our reading of the text equals the assembler's output" and "the GLSL computes what the reading
  computes"; never a comparison with an Xbox.

**Status.** Measured (LOG.md M6, re-run by the coordinator): `--selftest` 100 shaders, 99 compared with their
definition words, 6,336 cases, maximum difference 0, 0 failed; `--gputest` (Mesa llvmpipe) 107 shaders, 6,848
cases, 0 outside one 8-bit step (worst 0.5), 420 program variants linked, all 107 compile as GLSL ES 1.00. The
tests have teeth: deliberately wrong semantics (`_x4` as `_x2`, alpha half ignoring `.b`) fail 12-29 shaders.
Re-run at every later renderer gate (M7, M7b, M13 `m13-64bit.sh`).

**Off switch.** Not applicable (offline tool). `UsePixelShaders=False` stops the driver using the output (R-4).

### R-4 Programmable materials: Xbox vertex fragments, GLSL programs, pixel-shader families (M6, steps S2-S4)

**What the player/owner sees.** Materials that PC drew flat or with the wrong colour now have their Xbox
look: player skins with specular highlights, team skins where only the masked markings glow in team colour,
emissive and team shaders on static meshes, the specular-mask glass. Visible difference to M5: the player's
helmet and shoulders gain metal highlights (as the original PC binary shows); the CTF team skins are no longer
tinted orange-red all over.

**Where.**
- `Source/UC2GLDrv/Src/UC2GLShaders.cpp` (1,327 lines; commit `cf8330e`, extended by M7/M7b/M11):
  - `UC2GL_IsM7PixelShader` (:110), `UC2GL_IsEngineGatedPixelShader` (:154): which shader families wait for
    post-processing or an engine capability;
  - `BuildVertexShaderSource` (:187): writes one GLSL 1.20 vertex shader per 64-bit `VertexShaderEnum`, from GLSL
    snippets hand-ported from the Xbox `.vsh` fragments, emitted in the order `FD3DCombinedVertexShader`
    (`D3DResource.cpp` 2230-2620) splices them, each under a comment naming its `.vsh`. Ported: 19 of the 67
    files (`DefaultPosition`, `NotBoned`, `InitRGB`, `LightAmbient`, `LightHardDirectional5`, `MulVertexSpec`,
    `AddVertexRGB`, `OutLightRGB`, the `Tex*` source and transform fragments, `Fog`); M7 added `Distortion`,
    `DistortionAccum`, `ColorRemap`; M7b the projector fragments (R-14);
  - `InitPixelShaders` (:619): compiles the base variant of every usable shader at start-up and precompiles
    the vertex shaders listed in Epic's `VertexShaderConfigs.ini` (:663-669);
  - `PixelShaderFamilyReady` (:732): a family = the enum values sharing one state handler; ready only if all of
    them compiled;
  - `GetStateVertexShaderEnum` (:769): builds Epic's enum, same bit layout, from the render state (lighting,
    vertex colour, stage sources/transforms, fog);
  - `BeginProgramPass` (:835) / `EndProgramPass` (:1114): per pass, streams as generic attributes, program,
    uniforms; `UC2_RunProgramTest` (:1147, M11's 420-program comparison).
- `Src/UC2GLMaterialState.cpp:1502` `SetPixelShaderMaterial`: PC's test `IsXboxOnly() == 0` replaced by
  `RenDev->PixelShaderFamilyReady(type)` (:1554); the Xbox branch (all four stages, then the state handler) is
  taken for Xbox-only materials too. Test switch `-gldualdiffuse=<n>` (:1512-1537) turns `PSEmissiveShader`
  materials into a `PSDualDiffuse` to exercise the 32 DualDiffuse shaders no package uses.
- `Src/UC2GLPSMaterialState.cpp`: Epic's 24 state handlers, unchanged in behaviour; constants go to
  `FOpenGLRenderInterface::SetPixelShaderConstant` (`u_c0..7`) instead of D3D's deferred state.
- `Src/UC2GLRenderInterface.cpp` `DrawPrimitive` -> `BeginProgramPass`; blending, depth, alpha test, culling,
  colour mask, texture binding stay in `SetMaterialBlending`, shared by both paths.
- ini: `[UC2GLDrv.UC2GLRenderDevice] UsePixelShaders=True`, `PrecompileVertexShaders=True` (`Default.ini:398-399`;
  commit `bd6a4e2`); README row `Engine/Config/README.md:15`.
- Tools: `tools/dev/run-uc2-shot.sh` (screenshots from a pinned spawn, A/B in one run with `-glpstoggle=`),
  `tools/dev/ps-census.py`.
- Epic's Engine/Core: **none** (shaders.md "Engine-side and configuration changes").
- Commits: `8289fbf`, `cf8330e`, `bd6a4e2`, `5cad7fd`; merged `787c8c7`.

**How the parts fit.** The engine and Epic's material classes are unchanged: a pixel-shader material's
`PreRender` and Epic's state handler choose the `EPixelShader` and constants as on Xbox. The driver turns the
pass's render state into Epic's vertex-shader enum and compiles a matching GLSL vertex shader, links it with the
translated pixel shader (R-3) and draws. Everything that is not a pixel-shader material stays on the fixed-
function path of R-1/R-5. Lighting for these passes is computed by the GLSL port of the Xbox fragments (up to 5
directional lights plus ambient, the selection `SetLight` already does on PC), not by GL's T&L.

**Why.** The M6 gate in PLAN.md: "Xbox-only materials rendered for real; team skins, emissive skins,
dual-diffuse visibly correct".

**Decisions and alternatives.**
- Vertex side generated per enum from Epic's fragments (the Xbox has no fixed-function vertex path at all), not
  GL fixed-function T&L: GL's fixed path cannot take diffuse/ambient material from the secondary colour, which
  static meshes need.
- Static-mesh vertex colour detected from the bound streams (`FVF_Specular` stream present), because the PC
  engine passes `VS_FixedFunction` where the Xbox engine passes `VS_StaticMeshVertexColor` (an `_XBOX` branch of
  `UnRenderStaticMesh.cpp`, left untouched).
- Where Xbox and PC differ (unlit, uncoloured geometry: ambient vs white), the Xbox formula wins (the brief);
  not seen on any content.
- Not ported: GPU skinning and packed positions/terrain fragments (need `XBOX_ASSET_FORMAT`), the
  non-hard-coded light path Epic compiled out.

**Disadvantages, risks, deviations.**
- 37 of the 59 shaders integrated at M6 are unreachable from shipped content (no package contains a
  `PSDualDiffuse`, `PSShenuSkin`, `PSEmissiveConstColor` or `PSTeamSkinShaderOpacity`): numeric tests are the only
  evidence for them.
- Epic's own defects are kept: a one-call lag in the skin handlers (the first draw of a `PSSkinShader` with
  `bUseLightingForSpecular` uses the non-SpecLight shader); stage compaction in `SetPixelShaderMaterial` (a
  material with a NULL middle stage gets later textures one stage early); the "crazy constant" 16.0 clamped to
  1.0 as on hardware (shaders.md "Defects found in Epic's shaders").
- `VertexShaderConfigs.ini` precompiles most but not all vertex shaders used (2-4 per map still compiled on first
  use); programs are linked on first use: possible first-use hitches.
- No state caching: program bound and uniforms uploaded per draw (shaders.md "Deferred: Performance").
- Pixel-shader constants are device-global as in D3D; with `UsePixelShaders=False` the team skin is untinted while
  PC's fallback tinted it (M5's fallback is not exactly PC's).
- No Xbox picture exists for any Xbox-only material (team skin, emissive skin, specular-mask glass).

**Status.** Measured (LOG.md M6): DM-Deadbolt 59 fragment shaders compiled, 0 failed; about 20,000-70,000 program
draws, 0 GL errors over 1,800-2,400 frames. Compared with the original PC binary under wine where PC had its own
DX8 shader: player skin mean difference over the character 6.3/255 with shaders, 16.7 without; CTF-Octane door
frame 0.39 with, 5.76 without (shaders.md S4). Seen only: team skin, specular-mask glass, emissive specular,
emissive skin ("runs clean, could not tell which object carries it").

**Off switch.** `UsePixelShaders=False`, `-nopixelshaders` (`-pixelshaders` to force on), console
`PIXELSHADERS 0|1`, `-glpstoggle=<s>`; `PrecompileVertexShaders=False` skips the start-up precompile.

### R-5 Fixed-function texture stages as generated GLSL, combine4, four stages (O-9, O-9b)

**What the player/owner sees.** On the owner's NVIDIA GPU (Quadro P620) the maps look as on Mesa: DM-Azure's
wooden pillars are wood (were orange metal), the cobra statue is golden, and in DM-AcidRain / DM-Tempest whole
walls and floor gratings that were nearly black are textured. On every GPU and the web, 15 materials (the Raiden
beam body, the cobra waterfall, some banners, floor panels, the Horizon and ViperPit waters) are now split into
passes as Epic's 4-stage compiler split them.

**Where.**
- New: `Source/UC2GLDrv/Src/UC2GLFFProgram.cpp` (360 lines): a pass's D3D texture stages become a GLSL fragment
  shader, generated per combination (ops, used arguments, 2D/cube target, colour sum, fog) and cached
  (`FFMakeKey` :100, `FFBuildSource` :189, `FFGetProgram` :237, `BeginFixedFunctionProgram` :303). It is linked
  **without a vertex shader**, so GL's fixed-function vertex stage (transform, lighting, texgen, texture
  matrices, fog coordinate) stays. D3D8 operation definitions, every stage saturated; colour sum and linear fog
  done in the shader; fog uses `abs(gl_FogFragCoord)` (:19, :232) because NVIDIA hands the signed eye-space z.
- `Src/UC2GLRenderDevice.cpp`: `UpdateTextureCaps` (:315): stages = min(path's units, `MaxTextureUnits`,
  `MaterialStages`); `TextureOpCaps` reports `MODULATEALPHA_ADDCOLOR | MULTIPLYADD` when the program path is on or
  combine3/combine4 exists; on GLES the program path is never used (GL4ES generates its own shaders). Switches
  :565-578; console `FFPROGRAMS` :2028, `GLMATCENSUS` :2042, `COMBINERTEST` :2103; property `MaterialStages`
  :197, default 4 (:233).
- `Src/UC2GLRenderInterface.cpp`: `TranslateStageOp` (:1196) + `ToCombine4` (:1324): with
  `GL_NV_texture_env_combine4` and no combine3, a three-operand op becomes `Arg0*Arg1 + Arg2*Arg3`; before O-9 the
  added term was dropped (`a*b+c` became `a*b`). `DrawPrimitive` binds the program after `CommitTexCoords`.
- New test: `Src/UC2GLCombinerTest.cpp` (652 lines): 35 cases, every op the compiler emits, a 3-stage chain, 8
  stages, colour sum, fog in front and behind, projected lookup, cube faces, LOD bias; GPU result against a CPU
  model written from the D3D8 SDK, tolerance 2/255. Census: `Src/UC2GLPixelTrace.cpp:166-221`
  (`-glmatcensus`, `GLMATCENSUS`: every loaded material compiled with three cap sets, nothing drawn).
- ini (`Default.ini:393-394` and the GL4ES section, web ini too): `MaterialStages=4`,
  `FixedFunctionPrograms=True`; README row `Engine/Config/README.md:21`.
- Tools: `tools/dev/o9-nvidia.sh` (220 lines: combiner tests on Mesa, the NVIDIA GPU headless through SDL's
  offscreen/EGL driver, and the twin; the "before" behaviour reproduced; caps; pictures), `tools/dev/o9-census.sh`,
  scenarios `tools/dev/o9/{azure,acidrain,tempest,praxis-panes,perf,viperpit-*}.scen`.
- O-9b: `Source/Engine/Src/UC2NetTest.cpp` (`NETPLACE LocalPlayer`, a test command of ours) and
  `tools/dev/m7-effects.sh` (the DM-Praxis reference check placed at the oracle's camera, one run).
- Commits: `51bb18e` (step 0), `c1df38f`, `dfe7030`, `e43d667`, `05ec567`, `28b7136`, `638017e`, `faf3f1e`;
  merged `f2d7f25`. O-9b: `fcceed4`, `5caec2b`, merged `81e9b05`.

**How the parts fit.** Epic's material compiler (R-1) is told how many stages it has and which three-operand ops
exist; it then emits D3D stage lists. Those are drawn either by the generated program (default on desktop GL)
or by GL's texture environment (comparison switch, and the only path on GLES/WebGL, where GL4ES emulates
combine3 itself). Pixel-shader materials (R-4) are not affected. Without the ini keys the compiled defaults
(4 stages, programs on) apply.

**Why.** Found by the O-5 work on the owner's GPU and raised by the owner's review (PLAN.md O-9): the NVIDIA
compatibility profile reports 4 fixed-function texture units and no `GL_ATI_texture_env_combine3`. Epic's
compiler then took its GeForce1-era fallbacks (MODULATE2X for the specular add: the "orange metal" pillar) and
our translation dropped the added term of every unconditional MULTIPLYADD / "add to current" stage. Census over
all 50 DM/CTF/OD maps: 327 distinct materials compiled differently on NVIDIA. Measured on NVIDIA against Mesa
(mean difference of 255): wooden pillar 59.8 before, 0.76 after; cobra 5.34 / 0.29; DM-AcidRain 16.1 / 0.70;
DM-Tempest 13.9 / 0.57.

**Decisions and alternatives.**
- (b) GLSL for the stages by default: one path for every vendor, no dependence on combine3 (AMD/Mesa only) or
  combine4 (NVIDIA/Mesa only) or `GL_MAX_TEXTURE_UNITS`; about 300 lines.
- (a) combine4 added to the texture-environment path too, so the comparison switch compares two correct paths.
- 4 stages, Epic's number (`MaterialStages=4`): UC2's `D3DRenderDevice.cpp` 418-422 clamps both stage counts to 4
  on PC and Xbox and refuses other cards; the old 8 came from Epic's UT2003-era OpenGLDrv ini section. With 8 the
  compiler folded 15 materials into one pass with a different formula.
- (c) Epic's multipass answer was rejected: for a missing op Epic has only the GeForce1 substitutions.

**Disadvantages, risks, deviations.**
- Changes the picture on Mesa and the web for the 15 split materials (towards Epic's split).
- AMD and Intel GPUs were never tried; the one vendor difference met (signed `gl_FogFragCoord`) is handled.
- "4 stages is the Xbox picture" rests on Epic's code, not on an Xbox picture.
- The web keeps GL4ES' own fixed-function emulation (checked numerically on the twin, not with this generator).
- The coverage of combine3/combine4 across drivers could not be read (`opengl.gpuinfo.org` is filled by script;
  BLOCKERS.md "O-9").
- Seen on the way, not fixed (BLOCKERS.md): `POSTPROCESS 0` at run time leaves DM-Azure's cobra waterfall as an
  opaque violet stream (the distortion material's fallback; see R-7).

**Status.** Measured (LOG.md 2026-10-07; o9-nvidia.md gate): `o9-nvidia.sh` 12 PASS, 0 FAIL; combiner test 35/35
with programs on Mesa (worst 0.50/255) and NVIDIA (0.56), texture environment combine4 33 + 2 skipped on NVIDIA,
the "before" path fails exactly the 11 three-operand cases, twin 33 + 2 skipped (no LOD bias on ES). Pinned
DM-Deadbolt `-nopostprocess` unchanged on Mesa (99.997% within 2 levels). Performance on NVIDIA (DM-Azure,
1280x720, AA 4x, post-processing on): programs 11.4-12.4 ms vs texture environment 11.4-12.2 ms, no difference
beyond noise (loaded machine). O-9b: the "swirled Praxis panes" signal was a test artefact (a close camera
against the oracle's distant one; panes drawn exactly as before O-9); the check now places the camera at the
oracle's PlayerStart0 and matches the wine reference at 2.58 (Mesa) / 2.65-2.72 (NVIDIA) mean difference.

**Off switch.** `FixedFunctionPrograms=False`, `-glnoffprograms` (`-glffprograms` to force), console
`FFPROGRAMS 0|1`, `-glfftoggle=<s>`; `-glstages=8` restores the old split; test masks `-glnocombine3`,
`-glnocombine4`, `-glunits=N`; `-glffsource` logs the generated sources.

### R-6 Render targets, the back buffer and the depth buffer as textures (M7, step P1; driver side)

**What the player/owner sees.** Nothing by itself: it is what every post-process effect (R-8) reads from. With
post-processing off the frame is pixel-identical to M6's in its static part.

**Where.**
- New: `Source/UC2GLDrv/Src/UC2GLPostProcess.cpp` (1,429 lines; commit `47d3b6d`, extended by `4ed20bc`,
  `f5fbcae`, `6921897`, `f8dfb79`, `7f58c55`). Its header comment states the five coordinate conventions
  (textures top row first as D3D; the main framebuffer the one exception, flipped only in the copy passes; GL
  window depth = D3D z; the half-texel convention of Epic's screen tiles; distortion's screen position).
  - small programs: copy (`GPostCopyFragmentShader` :74), depth encode (:90), depth copy (:103, writes
    `gl_FragDepth`), reference copy (:119, O-10);
  - `GetSampledTexture` (:367) / `UpdateSampledTexture` (:439) / `PrepareSampledTargets` (:565): a stage that
    samples the default render target binds a **snapshot** of the main framebuffer, refreshed before a draw only
    if something was drawn since (`MainColorVersion`);
  - depth as a texture: the main depth-stencil attachment is a texture; in "depth" usage a pass re-encodes it into
    RGBA8 with the byte layout of the Xbox's B8G8R8A8 view of D24S8 (blue = bits 23..16, green 15..8, red 7..0),
    which `DOF.psh` reads with `texreg2gb`;
  - Epic's Xbox bodies carried over with their comments: `GetBackBufferMaterial` (:614),
    `GetBackBufferCopyTarget` (:638), `UpdateBackBufferCopyTarget` (:689), `GetBackBufferCopyMaterial` (:806),
    `CopyDepthBuffer` (:841, a quad that writes the sampled depth as `gl_FragDepth`; source must be the default
    target), `UC2GL_UpdateDOFRampLookupTexture` (:940), `FOpenGLDynamicGamma` (:1075, R-9).
- Hooks: `UC2GLRenderInterface.cpp` (`DrawPrimitive`, `SetMaterialBlending`, `Clear`, `GetGLMatrices`: the
  1/32-texel `PointSampleNudge` for point-sampled render-target stages), `UC2GLShaders.cpp` (vertex fragments
  `Distortion.vsh`, `DistortionAccum.vsh`, `ColorRemap.vsh`), `UC2GLMaterialState.cpp` (`HandlePSVertexShader`
  :1390, Epic's `_XBOX` body, now run when post-processing is on; `UpdateDOFRampLookupTexture` :1235),
  `UC2GLPSMaterialState.cpp:367` (`SetMatState_PS_Distortion`: `#ifdef _XBOX` became
  `RenDev->PostProcessActive()`), `UC2GLResource.cpp` (render targets), `UC2GLRenderDevice.cpp`.
- Epic's shared code: this commit (`47d3b6d`) already added `FRenderCaps::XboxPostProcess` and
  `RenDevHasXboxPostProcess()` (`UnRenDev.h`, `UnRenderResource.cpp`); their users came in P2, see R-7.
- Tests: `Src/UC2GLSelfTest.cpp` (console `POSTSELFTEST`, `-glselftest`; R-21); `tools/dev/m7-effects.sh`.

**How the parts fit.** The engine (through the lifted gates of R-7) asks the interface for the back-buffer
material, the back-buffer copy target, a depth copy, a DOF ramp; Epic's Xbox driver code does the rest, drawing
through the engine's own materials (`UPSBlurMaterial`, `PS_LuminanceTexIndex`) so the same programs as the game's
effects are exercised. The driver substitutes copies where the Xbox sampled the framebuffer it was drawing into
(GL forbids that).

**Why.** Every post-process effect of the Xbox reads the back buffer or its depth while drawing (design.md F1:
`FPostProcessRender::Render` and the camera effects are `_XBOX`-only; PC's driver returned early).

**Decisions and alternatives.**
- Snapshot on demand (one full-screen copy per draw that reads the back buffer after it changed) instead of
  rendering the scene into a sampleable second colour buffer: simplest correct substitute; not optimised.
- Depth: re-encode the depth texture into the Xbox's byte layout, so Epic's DOF shader runs unchanged.
- Epic's `SetMatState_PS_Distortion` writes `InShader->PixelShaderType = NULL` in its "draw nothing" branches,
  which permanently changes the material object: **not carried over** (the only place in P1 where Epic's
  behaviour is not kept; `UC2GLPSMaterialState.cpp:455-459`).
- The render-target pool's destructor asserts no references are left; at exit the camera effects still hold
  some: `UUC2GLRenderDevice::Destroy` hands them to their holders (leaked at exit) (`dd3f362`).

**Disadvantages, risks, deviations.**
- Cost: three to four full-screen snapshots per frame with the default effects, plus the depth encode and about a
  dozen half/quarter-size passes. Measured under software GL at 1280x720 (M7 gate): DM-Deadbolt 30.1 fps off,
  7.9 on (3.8x); DM-Praxis 21.5 / 6.6 (3.3x). On the NVIDIA GPU the frame times are small (O-9: ~12 ms at 720p with
  AA and post-processing on; O-10: 4K about 11.3 ms after `7f58c55`), not measured per effect.
- DOF reads the encoded depth through a bilinear filter (Epic's stage state): where the middle byte wraps between
  neighbours the average is not a depth; estimated ~2% alpha error along thin lines, never seen in a picture.
- That NV2A resolves a point-sampled tie to the texel right/below is an assumption (used for `CopyDepthBuffer`
  and the nudge).
- Split-screen (more than one viewport) was never run.

**Status.** Measured: self-test (M7) `snapshot` exact over 921,600 pixels, `depth-color` max 2 units of 2^24,
`depth-copy` 1 unit, blur taps within 1.0/255, `bb-copy` 1.44, `dof-ramp` 0.72, `remap` 0.5, plus `luminance`;
10 tests at M7, 11 with M11's fog, 15 with O-10 (LOG.md 2026-10-08: 15/15). The tests caught a real bug (depth 1.0
encoded as 0.9961: the sky would have been blurred at 80% alpha) and the author's own wrong half-texel model.

**Off switch.** `UsePostProcess=False`, `-nopostprocess` / `-postprocess`, console `POSTPROCESS 0|1`,
`-glpptoggle=<s>`.

### R-7 The Xbox post-process paths lifted in Epic's engine and game code (`XboxPostProcess`, M7 step P2)

**What the player/owner sees.** Distortion is see-through (the DM-Praxis panes show the sea instead of flat
periwinkle; DM-Decay icicles are translucent ice instead of dark-blue shapes), emissive glow, bloom, an always-on
depth of field, screen and radial blur, colour remaps (berserk, pause, Liandri).

**Where.** Epic's shared code, all in commit `94654e2` unless said:
- `Engine/Inc/UnRenDev.h:770-819` `FRenderCaps::XboxPostProcess` (default 0; commit `47d3b6d`) and
  `ShippedDOFBlurAlpha` (R-10); declaration `RenDevHasXboxPostProcess()` (:894). `FRenderCaps` is a plain C++ struct, not mirrored in script.
- `Engine/Src/UnRenderResource.cpp:48` `RenDevHasXboxPostProcess()` (`47d3b6d`): false without a client or device (dedicated
  server, commandlets), for the null device and any device leaving the field 0.
- `Engine/Src/UnRender.cpp:3256-3262` `FPostProcessRender::Render`: the body after the draw-list test was
  `#ifdef _XBOX`; now `#ifndef _XBOX if( !RenDevHasXboxPostProcess() ) return; #endif` (gate lifted).
- `Engine/Classes/PSDistortionBaseMaterial.uc` (cpptext) and `Engine/Inc/EngineClasses.h:7678-7686`:
  `IsPostProcess()`/`RequiresSorting()` declared instead of PC's inline `POSTPROCESS_NONE`/`1`; defined in
  `Engine/Src/UnPixelShaderMaterial.cpp:408-421` (Xbox answer with the capability, PC's otherwise). Only the C++
  side changes; no package was rebuilt for this.
- `Engine/Src/UnPixelShaderMaterial.cpp`: `#ifdef _XBOX/#else` became `if( RenDevHasXboxPostProcess() ) {Xbox}
  else {PC}` in `UPSDistortionMaterial::PreRender` (:249), `UPSBlendedDistortionMaterial::PreRender` (:300),
  `UPSDistortionBaseMaterial::PreRender` (:349), `UPSBackBufferEffect::PreRender` (:841),
  `UPSPlayerCamMaterial::PreRender` (:885), `UPSColorRemapMaterial::PreRender` (:942).
- `UCGameplay/Src/UCCameraEffects.cpp`: Epic's `#ifndef _XBOX return; #endif` at the top of 16 functions became
  `#ifndef _XBOX if( !RenDevHasXboxPostProcess() ) return; #endif` (`UUCScreenBlur::PostRender` :89; Pre/PostRender
  of `UUCScreenDOF` :237/:312, `MotionBlur` :559/:587, `HeatShimmer` :709/:725, `RadialBlur` :775/:790, `Bloom`
  :934/:1014, `Emissive` :1210/:1315; PostRender of `Berserk`, `MenuPause`, `Liandri`); whole-body `#ifdef _XBOX`
  of `UUCScreenSniperOverlay` and `UUCScreenTeleportOverlay` Pre/PostRender became `if( RenDevHasXboxPostProcess() )`
  (:1959, :2004, :2197, :2249); `UUCScreenDOF` constructor/`Destroy` (:153-200): the Xbox constructor body moved
  into `InitSharedDOFResources()` (:170), called from the constructor if the capability is there, else on first
  `PreRender`; `Destroy` releases only what this object took. `UCGameplay/Inc/UCCameraEffects.h`: one non-virtual
  member function declared (no layout change).
- `Engine/Src/UnRenderUtil.cpp:1462-1476` `FScreenPrimUtil::DrawTile` (commit `6fa7758`): **bug fixed**: Epic's PC
  branch queued quad-ordered corners as a triangle strip, so a quarter of every tile (left edge to centre) was
  never drawn; now a triangle fan, one tile per draw. Only Xbox-only paths call it.
- Driver: `UUC2GLRenderDevice::GetRenderCaps` (`UC2GLRenderDevice.cpp:2368`) sets the capability when
  `PostProcessActive()` (ini switch on, pixel shaders on, copy programs built); `SetPostProcess`
  (`UC2GLPostProcess.cpp:275`) marks every actor's skins changed and clears the emitters' cached post-process flag
  when switched at run time; `Lock` (:2160-2183) applies the Xbox client defaults once (R-9, R-11).
- ini: `UsePostProcess=True` (`Default.ini:400`, `6fa7758`), `[SDL2Drv.SDL2Client] MaxDistortion=35.0` (:178).
- Tests: `tools/dev/m7-effects.sh` (one capped run per effect, A/B pictures, a line per effect with Critical, GL
  errors, failed programs, changed pixels; a DM-Praxis check against the wine reference); `POSTSELFTEST`.
- Commits: `47d3b6d`, `94654e2`, `4be1bb8`, `6fa7758`, `dd3f362`, `4ed20bc`, `d546816`, `0b1fa88`, `f4d1026`,
  `9a181ef`, `efd22ef`; merged `225b0d5`.

**How the parts fit.** The native engine side decides *what* to draw (Epic's Xbox code, unchanged except the
gate); the driver implements the interface it calls (R-6) and the 25 shaders (R-3). The `.uc` side is unchanged:
`UCPlayerController.CreateOverlays` already creates the emissive+bloom and always-on DOF overlays for every
player; the script defaults are the shipped `UCGameplay.u`'s. Without the capability (null device,
`UsePostProcess=False`, a dedicated server) every site takes Epic's PC branch exactly as before.

**Why.** The brief's M7 gate ("distortion no longer shows normal maps"); design.md F1 (PC distortion opaque, all
post-processing `_XBOX`-gated in engine and driver). CLAUDE.md rule: lift Xbox gates through a named capability,
keep both branches.

**Decisions and alternatives.** One capability and one test function for all sites instead of defining a macro
per site or deleting the PC branches. `UClient::Init`'s `_XBOX` lines (`DynamicGamma = 1; LuminosityBias = 1`)
were left alone (the device does not exist yet there); the device applies them at its first frame instead.

**Disadvantages, risks, deviations.**
- Touches Epic's shared code in nine files (listed at the end); every site keeps the PC branch.
- Switching post-processing off at run time does not restore the exact pre-M7 state of distortion materials (the
  PC branch writes only stage 1): DM-Azure's cobra waterfall becomes an opaque violet stream after `POSTPROCESS 0`
  (BLOCKERS.md, O-9; starting with `-nopostprocess` was not checked). Off-to-on is clean.
- Epic's `HasPostProcessMaterial` decides an actor's draw list from its first material only (kept).
- Screen-space distortion reads whatever pixel is displaced into place, foreground included (the Praxis pillar
  pulled into the panes; inherent to Epic's method, kept).

**Status.** Measured (LOG.md M7, coordinator's run): self-test 10/0; 14 effect pairs with 0 Critical, 0 GL errors,
0 failed programs; post-processing-off picture pixel-identical to M6 in the static part (0 differing pixels for
x >= 760). Latest: `m7-effects.sh` self-test 15/15 and 14/14 effect lines clean (LOG.md 2026-10-08). Seen only, no
reference: every "on" picture (Epic's PC build never had these effects; no Xbox picture except the owner's few
xemu shots).

**Off switch.** `UsePostProcess=False` / `-nopostprocess` / `POSTPROCESS 0` (then every Epic site takes its PC
branch).

### R-8 The effects themselves (distortion, glow, bloom, depth of field, blurs, colour remaps, player cam)

**What the player/owner sees.** See the table; "N+S" = numerically checked + seen, "S" = seen only (plausible, no
reference), "H" = handler ported, never observed in content.

| Effect | Shaders | Entry point (Epic's code, now running) | Status |
|---|---|---|---|
| Distortion, opaque (glass, energy fields, torch heat shimmer) | `PS_DistortionAccum`, `PS_Distortion`; `DistortionAccum.vsh`, `Distortion.vsh` | `FPostProcessRender::Render`, `UPSDistortion*::PreRender` | S (DM-Praxis panes, DM-Decay torch, DM-Undertow column) |
| Distortion, blended | `PS_ModulatedDistortion` (+ Additive, Subtractive) | same, `PP_STAGE_RENDER_COLOR` | modulated S (DM-Decay icicles); additive/subtractive H |
| Emissive glow | `PS_EmissiveSelectBlur`, `PS_Blur` | `UUCScreenEmissive` (always on) | S |
| Bloom | `PS_BloomSelect`, `PS_Blur` | `UUCScreenBloom` (always on) | S |
| Depth of field, always on | `PS_DepthOfField`, `PS_Blur` | `UUCScreenDOF`, `UpdateDOFRampLookupTexture` | N+S (only beyond z = 0.98: far mountains, sky edges) |
| Depth of field, focus on actor/distance (matinee, sniper) | same | `USubActionDepthOfField`, `UUCScreenSniperOverlay` | H |
| Screen blur (concussion, `BlurMe`) | `PS_Blur` | `UUCScreenBlur` | S |
| Radial/speed blur (sprint, teleport, sniper) | `PS_MotionBlur` | `UUCScreenRadialBlur` | S (held on by console) |
| Motion blur with accumulation | `PS_MotionBlur`, `PS_Mask` | `UUCScreenMotionBlur` | H (no script creates one) |
| Heat shimmer camera effect | `PS_Distortion` | `UUCScreenHeatShimmer` | H (creation commented out in `UCPlayerController`) |
| Colour remap: berserk, pause, combo flash, sun flare, teleport | `PS_ColorRemap`, `ColorRemap.vsh` | `UUCScreenBerserk`, `MenuPause` (+2 subclasses), `TeleportOverlay` | berserk, pause N+S; others H |
| Colour remap masked: sniper overlay | `PS_ColorRemapMasked` | `UUCScreenSniperOverlay` | H (sniper rifle not reachable in tests) |
| Liandri overlay | `PS_ColorRemapGrayScale` | `UUCScreenLiandri` | S (random noise: not comparable between runs) |
| Player cam screens | `PS_PlayerCam*` | `UPSPlayerCamMaterial::PreRender` | H (program links in CTF-Recoil, surface not identified) |
| Back-buffer effect, refraction, texture-index materials, emissive select without blur | `PS_MaskedLuminance`, `PS_Refraction`, `PS_UseTexIndex*`, `PS_EmissiveSelect` | various | H; refraction and texture-index unreachable (no object in any package); `PS_EmissiveSelect` compiled out by Epic (`#if 0`) |
| Depth swizzle | `PS_DepthSwizzleColor` | Xbox `CopyDepthBuffer` only | not used (replaced by the driver's depth copy) |

Of the 25 shaders M6 left for M7, 12 were drawn in a run at M7 and M7b added `PS_ShadowBlur` (R-15); 12 were
never drawn: `Mask`, `AdditiveDistortion`, `SubtractiveDistortion`, `MaskedLuminance`, `PlayerCamNoise`,
`PlayerCamTwoNoise`, `ColorRemapMasked`, and five that are unreachable or unused by design (`Refraction`,
`UseTexIndex`, `UseTexIndexMasked`, `EmissiveSelect`, `DepthSwizzleColor`) (postprocess.md "Per-effect status").

**Where.** As R-6/R-7; the effect-specific lines are in Epic's files listed there. Effect triggers for testing:
console `BlurMe`, `set UCScreenBerserk Enabled 1`, `set UCScreenLiandri Enabled 1`, `set UCScreenMenuPause Enabled
1` (also enables its two subclasses: a trap for testers), radial blur needs `set UCPlayerController
RadialBlurSpeed 0` etc. (postprocess.md P3 table).

**How the parts fit / Why.** As R-7.

**Decisions and alternatives.** All of Epic's effects are enabled as the Xbox script creates them; none was
removed or tuned except the owner items R-10, R-11, R-13.

**Disadvantages, risks, deviations.**
- No reference picture for any "on" state: strength of glow and bloom, the berserk overlay on pawn attachments
  (the shoulder orb is remapped, the pawn body is stencilled out), the pause screen's teal negative (owner: "matches
  the Xbox", PLAN.md owner review), the Praxis pillar artefact.
- The red pawn's halo in CTF-Recoil is strong (emissive team markings through emissive select and bloom);
  unknown whether the Xbox halo is that strong.
- Never run: split-screen, cinematics with focus DOF, the sniper overlay.

**Status.** As R-7. The owner reviewed the preview on 2026-10-06: pause overlay and dynamic gamma "correct";
DM-Praxis panes close up "spot on"; other distortions needed work (O-5..O-7).

**Off switch.** `POSTPROCESS 0` turns all of them off; individual camera effects through `set UCScreen<Name>
Enabled 0` (script properties), `DYNAMICGAMMA 0`.

### R-9 Dynamic gamma (M7)

**What the player/owner sees.** The picture's brightness adapts to the scene, as on the Xbox: dark maps are
brightened (DM-Deadbolt noticeably; bright highlights clip). Owner (2026-10-06): "it works as it should".

**Where.**
- `Src/UC2GLPostProcess.cpp:1075-1405`: `FOpenGLDynamicGamma` = Epic's `FD3DDynamicGamma`: every 0.05 s the back
  buffer is drawn into a 16x16 target through `PS_LuminanceTexIndex` (since O-10 from the 480-line reference copy),
  read back with `glReadPixels` (the Xbox read texture memory), turned into a histogram, and a 256-entry ramp is
  computed from its 10%/90% points, `LevelGamma` and `LevelContrast`; `SetGammaRamp` (:519) uploads it to the
  present pass's 256x1 ramp texture (R-2). `GetAverageSceneLuminosity` (:1391) also feeds the bloom threshold.
- `Src/UC2GLRenderDevice.cpp:2160-2183` (`Lock`, commit `4be1bb8`): with post-processing on, the first frame sets
  `UClient::DynamicGamma = 1` and `LuminosityBias = 1.0` (what `UClient::Init` does under `_XBOX`); a
  `LuminosityBias` of 0 is treated as 1.
- Console `DYNAMICGAMMA 0|1`, `LUMINOSITYBIAS`; `-glshotramp` puts the ramp into screenshots.

**How the parts fit.** The engine already calls `RI->UpdateDynamicGamma()` every frame on every platform
(`UnRender.cpp:2404-2408`, `FPlayerSceneNode::Render`); Epic's PC driver returned early. No engine change for this feature.

**Why.** Xbox default behaviour; part of M7's scope ("dynamic gamma" in the M7 notes).

**Decisions and alternatives.** On by default because the Xbox has it on; confirmed by the owner.

**Disadvantages, risks, deviations.**
- A user cannot turn it off persistently through the ini: the device sets `DynamicGamma = 1` again at the first
  frame of every session (only the console command works for that session).
- `UClient::DynamicGamma` already read True on PC builds with nothing setting it (not understood; suspected shared
  storage with the next bool property; postprocess.md defect 5).
- `glReadPixels` of the 16x16 target every 0.05 s is a GPU-CPU sync point (not measured as a cost).
- It adapts per frame update, so at low frame rates it takes tens of seconds to settle; this made picture
  comparisons time-dependent (pp-scale.md step 3) and is why tests freeze or disable it.
- The histogram debug overlay (`ShowDebug`, `bDebugShowGamma`) is not ported.

**Status.** Measured: self-test `luminance` (the luminance pass). Seen: `fx-dyngamma` pair. Whether the Xbox looked
this bright on dark maps: not verified (owner's opinion only).

**Off switch.** Console `DYNAMICGAMMA 0` (per session); `UsePostProcess=False` (all post-processing).

### R-10 Depth-of-field blur alpha: the shipped value (O-3)

**What the player/owner sees.** The always-on depth of field softens far geometry with alpha 65 in both camera
modes, as the shipped Xbox binary did. With the script's intended value it would be 100 in third person (stronger).

**Where.**
- Epic's shared code: `UCGameplay/Inc/UCCameraEffects.h` (`UUCScreenDOF`): the header had fallen behind
  `UCScreenDOF.uc` and lacked `FirstPersonAlpha`/`ThirdPersonAlpha` before `AbsoluteBlurAlpha`; added (layout fix,
  commit `44f81c6`, M3; runtime-port.md item 5). With Epic's stale header the shipped binary read
  `FirstPersonAlpha`'s byte (65) where it meant `AbsoluteBlurAlpha`.
- `UCGameplay/Src/UCCameraEffects.cpp:343-352` (`UUCScreenDOF::PostRender`, `#ifndef _XBOX`): `ModulateBlurColor.A =
  ShippedDOFBlurAlpha ? FirstPersonAlpha : AbsoluteBlurAlpha`, every frame (commit `94654e2`).
- `Engine/Inc/UnRenDev.h` `FRenderCaps::ShippedDOFBlurAlpha`; set by `GetRenderCaps` from the device's
  `DOFShippedBlurAlpha`.
- ini: `DOFShippedBlurAlpha=True` (`Default.ini:401`; was False until commit `2208566`, the owner's O-3 decision);
  README row 18. Console `DOFSHIPPEDALPHA 0|1`.

**How the parts fit.** Script chooses the alpha per camera mode; native code copies it; the device flag picks
which byte native reads. Without the ini key the compiled default is **0** (`UC2GLRenderDevice.cpp:214`), i.e. the
script's value 100 in third person.

> Superseded (2026-10-09/10): see section 60, R-10.

**Why.** Owner's review O-3 (PLAN.md): the owner could not see the DOF; decision "default `DOFShippedBlurAlpha` to
the shipped value (65) so the look matches the Xbox binary".

**Decisions and alternatives.** The shipped behaviour (a header bug of Epic's) wins over the script's intent,
consistent with "keep the Xbox behaviour". The alternative (100 in third person) is one setting away.

**Disadvantages, risks, deviations.**
- Ini and compiled default disagree (ini True, code 0): an ini without the key (an old user ini) gets the
  non-shipped value. Worth aligning.
  *(Superseded (2026-10-09/10): see section 60, R-10.)*
- O-3 also asked for an explanatory picture pair: `fx-dof-alpha` and `fx-azure` exist (postprocess.md P5b); the
  effect is subtle (mean difference 1.4/255 in the far region between 65 and 100).

**Status.** Seen: `fx-dof-alpha` pair (DM-Azure, far region less softened with 65). Measured as part of O-10: DOF
blur sigma 3.10 reference pixels, alpha 0.228 = 65/255 x the ramp. LOG.md has no O-3 line (PLAN.md says "see O-3
in LOG.md when done"): the change is recorded only in commit `2208566` and BLOCKERS.md "Decided by the owner".

> Superseded (2026-10-09/10): see section 60, R-10.

**Off switch.** `DOFShippedBlurAlpha=False` or `DOFSHIPPEDALPHA 0` (script value); `set UCScreenDOF Enabled 0`
removes the DOF.

### R-11 Distortion amount and range: `MaxDistortion`, resolution scaling, `DistortionCullDistance` (M7, O-7)

**What the player/owner sees.** Distortion displaces the picture by the Xbox's amount at any resolution, and
distant panes (DM-Praxis) keep their distortion farther away than on the Xbox (O-7: "figure out what tuneable is
there AND THEN increase it").

**Where.**
- `[SDL2Drv.SDL2Client] MaxDistortion=35.0` (`Default.ini:178`, web `:197`; commit `6fa7758`): PC's ini had no
  key, which reads 0 (no displacement and a division by zero in the accumulation pass); 35 is `DefaultXbox.ini`'s
  value; `Lock` falls back to 35 when it reads 0 (`UC2GLRenderDevice.cpp:2176-2180`).
- Resolution scaling: `Src/UC2GLShaders.cpp:1075-1076`: the distortion amounts (pixels of a 640x480 back buffer on
  Xbox) are multiplied by `height / PostProcessReferenceHeight` (480 when the setting is 0).
- Range: `Src/UC2GLMaterialState.cpp:1381-1419` (`HandlePSVertexShader`): Epic's `static FLOAT
  DISTORTION_CULL_DISTANCE = 5000.f` (`D3DMaterialState.cpp` 1377) became `GUC2GLDistortionCullDistance`, read at
  the first distortion draw from `[UC2GLDrv.UC2GLRenderDevice] DistortionCullDistance` (missing or not positive =
  5000, the Xbox value). `DistortionAccum.vsh` scales each vertex's distortion by `1 - saturate(z/D)^2`. Console
  `DISTORTIONCULLDISTANCE [<units>]` (`UC2GLRenderDevice.cpp:2013-2016`).
  *(Superseded (2026-10-09/10): see section 60, R-11.)*
- ini: `DistortionCullDistance=15000` (`Default.ini:402`, commit `7463d9c`; ported into the M11 driver by `93c2f27`);
  README row 22.
- Tools: `tools/dev/fx-owner.sh` (scenarios `praxis-far`, `praxis-near`; `UC2_GPU=1` runs on the NVIDIA GPU
  headless), console `GLPIXTRACE` (statistics of the distortion accumulation target).

**How the parts fit.** Driver-only (the knob is in Epic's D3D driver code, carried over); the client setting is
Epic's `UClient::MaxDistortion`.

**Why.** M7: without `MaxDistortion` nothing was displaced. Scaling: the Xbox amounts are pixels of a 480-line
picture. O-7: owner's picture `PraxisWindowPanesDistance.png` (distortion gone at a distance).

**Decisions and alternatives.** Scaling by height/480 (M7's own decision, later tied to `PostProcessReferenceHeight`
by O-10) instead of Epic's fixed pixels. O-7: the existing constant made a setting and raised; the effect itself
was not restructured; the mip LOD bias of -1.8 (a second, smaller factor) left alone.

**Disadvantages, risks, deviations.**
- 15000 is a deviation from the Xbox's 5000 by the owner's request: 97% of the distortion is left at 2650 units
  (Xbox 72%), 89% at 5000 (Xbox 0).
- The GL4ES subclass reads this key from the `[UC2GLDrv.UC2GLRenderDevice]` section (hard-coded section name),
  not its own; the web ini has both sections, so it works, but the GL4ES section's own copy of the key is ignored.
  *(Superseded (2026-10-09/10): see section 60, R-11.)*
- The size of the effect (height/480 scaling) is unverified against an Xbox; the owner called the close-up Praxis
  panes "spot on".
- The splash distortion fade (O-5's second sentence) was analysed as fading with particle alpha exactly as on the
  Xbox; inferred from the code, not measured.

**Status.** Measured (fx-fixes.md step 4): mean accumulated offset over the panes at 2650 units: 5000 -> 6.32/9.97,
15000 -> 8.51/13.33, no fade -> 8.81/13.78. Seen: `fx-praxis-far-*` pictures (difference modest at that camera).

**Off switch.** `DistortionCullDistance=5000` (or remove the key) for the Xbox range; `MaxDistortion` in the
client section for the amount.

### R-12 Cube maps with a missing face (DM-Azure water pool, O-5); the waterfall report (O-6)

**What the player/owner sees.** DM-Azure's centre pool has a water surface with golden reflections (was flat
transparent black/grey). The owner's waterfall report (O-6) is not reproduced and not changed.

**Where.**
- `Src/UC2GLResource.cpp` (`FOpenGLTexture::Cache`, cube branch; `UploadMip` accepts a NULL face): every cube face
  is uploaded, missing ones zeroed (DXT1 zero blocks = opaque black); on GLES the face's mip chain is completed
  too. Commit `2238100` (main), ported into the M11 driver by `93c2f27`.
- Debug tools from the same work: `Src/UC2GLPixelTrace.cpp` (console `GLPIXTRACE <x> <y> [all]`: what the next frame
  draws over a pixel, material, blend, stages, LOD bias, and the distortion accumulation target's statistics, as a
  PPM); `Engine/Src/UC2NetTest.cpp` `NETPLACE` takes a pitch (our test file).
- Tools: `tools/dev/fx-owner.sh` (scenarios `pool`, `pool-splash`, waterfall), `UC2_BIN=` before/after binaries.
- Commits: `bff872f`, `2238100`, `456dea3`, `7463d9c`, `8f0dafc`, `7f2a383`; merged `d3e88e7`; M11 port `93c2f27`.

**How the parts fit.** Driver-only. The pool is `FluidSurfaceInfo0`, its shader uses the cube map
`T_Liquid_Decay_Water_C_CM`, which has five faces (the -Z face is None in the package). GL treats an incomplete
cube as unusable and fixed-function GL switches the stage off, so only the vertex colour at almost zero alpha was
drawn. D3D (`D3DResource.cpp` 575-643, Xbox and PC) allocates all six faces and fills those that exist.

**Why.** Owner's review O-5 with xemu pictures (`temp/AzureWaterDistortion.png` vs `AzureWaterFlat.png`).

**Decisions and alternatives.** Reproduce D3D's allocation instead of skipping the material or fixing the package.

**Disadvantages, risks, deviations.** The missing face's content is an assumption (zero = black, never seen from
above). O-6 (the waterfall distortion disappearing in first person while standing still): drawn every frame on
Mesa and on the owner's NVIDIA GPU in our runs; needs the owner's exact spot (BLOCKERS.md "Owner review O-6"). "The
waterfall drawn over the character": explained as Epic's half-resolution depth mask and screen-space read (the
Xbox's design too); inferred, not changed.

**Status.** Seen: matches the owner's Xbox screenshot in kind (water layer, golden reflections), not pixel-close
(cameras differ), on Mesa and NVIDIA (`logs/fx/o5-pool-gpu-before-after.png`). Gate: m7-effects 10/10 + 14 lines
clean, psh2glsl tests, verifyclasses, pinned DM-Deadbolt unchanged (LOG.md 2026-10-07).

**Off switch.** None (a loader fix).

### R-13 Post-process blurs sized for a 480-line reference picture (O-10)

**What the player/owner sees.** Bloom, emissive glow, depth of field and radial blur cover the same share of the
picture at any resolution as on the Xbox at 640x480. Before: at 1440x1080 the glow was half as wide, at 4K a
third. 4K is about 10% faster than before.

**Where.**
- Epic's shared code (commit `f8dfb79`):
  - `Engine/Inc/UnRenDev.h:791-799` `FRenderCaps::PostProcessReferenceHeight` (default 0 = Epic's behaviour);
    `Engine/Src/UnRenderResource.cpp:93` `RenDevPostProcessReferenceHeight()`.
  - `Engine/Inc/UnRenderUtil.h:676-692` `FPostProcessReference` + `GetPostProcessReference()`;
    `Engine/Src/UnRenderUtil.cpp:2363` (the reference picture: R lines, W*R/H columns, `PixelScale = H/R`) and
    `FilterCopyRenderTarget` (:2469-2484): when the source is the back buffer, its taps are in reference pixels.
  - `UCGameplay/Src/UCCameraEffects.cpp`: `UUCScreenDOF::PreRender` (:259-300), `UUCScreenBloom::PreRender`
    (:949-1004), `UUCScreenEmissive::PreRender` (:1226-1283) size their targets from the reference picture;
    `UUCScreenRadialBlur::PostRender` (:839-846) scales the insets by W/(R*4/3) across and H/R down;
    `UCPP_TargetSize` (:60) compares sizes as the whole numbers they are created with. **Bug fixed** on the way:
    Epic compared the INT target size with the FLOAT product (`GetWidth() != BlurSizeX`), so at sizes like 1366x768
    (341.5) the pooled targets were released and re-created every frame and the emissive streak history was lost.
    One log line per (re)created target.
- Driver: `Src/UC2GLPostProcess.cpp`: the **reference copy** (`GPostReferenceFragmentShader` :119,
  `UC2GL_BuildReferenceProgram` :136, `UC2GL_UpdateReferenceCopy` :391): an area-averaging downsample of the main
  framebuffer to R lines (N x N bilinear taps, N = ceil(H/R) <= 8, a compile-time constant since `7f58c55`; the
  uniform-bounded loop cost ~4 ms a frame at 4K); `PrepareSampledTargets` routes every pass that samples the back
  buffer into a target no taller than 3/4 of it (DOF pass 0, emissive select, bloom select, luminance) to that
  copy; `UpdateBackBufferCopyTarget` (:747-750) taps x PixelScale, target stays half the back buffer.
  `Src/UC2GLShaders.cpp:1075` distortion scale = H/R. Property `PostProcessReferenceHeight` (compiled default 480),
  console `PPREFERENCE [h]`.
- Self-test: `UC2GLSelfTest.cpp` new tests `pp-sizes`, `ref-copy`, `ref-taps`, `bb-copy-ref` (:812-912); the older
  eleven run with reference 0.
- ini: `PostProcessReferenceHeight=480` in both device sections of `Default.ini` (:406) and `web/Default.ini`
  (commit `2aad748`); README row 24.
- Tools: `tools/dev/pp-scale.sh` (347 lines: selftest, glow, dof, blur, radial, identity at 640x480, aspect, GPU
  perf, sheets; freezes game time and guards frame counts), `tools/dev/pp-measure.py` (319 lines: glow width/peak,
  Gaussian sigma fit, radial zoom model), `run-uc2-shot.sh UC2_CAP`.
- Commits: `3f4a237` (plan), `eacf619`, `f8dfb79`, `2aad748`, `940796b`, `f08371b`, `082ad26`, `d034f97`,
  `9a2e8c7`, `5520a20`, `7f58c55`, `720a7a6`; merged `e42eafb` (seven conflicts with M7b resolved by hand).

**How the parts fit.** The engine side (Epic's camera effects) decides target sizes and tap distances from the
reference; the driver makes the reference copy so the first pass of each chain reads a well-filtered 480-line
picture texel for texel as the Xbox did. Without the driver's copy the engine change alone would alias (four taps
over a footprint k times larger: small lamps flickering in and out of the glow). Without the capability
(`PostProcessReferenceHeight=0`, other devices) Epic's sizes apply.

**Why.** Owner (2026-10-07): "I want to see if the bloom and blur radious is resolution dependant"; measured yes
(glow 8.3% of the height at 640x480, 4.2% at 1440x1080). Owner's guidance: "prefer the game looking beautiful than
being correct".

**Decisions and alternatives.** Size targets AND the first pass's taps for a reference picture, with a dedicated
area-averaged copy, instead of only resizing targets (rejected: aliasing) or only scaling taps. The back-buffer
copy target stays half size (it is also the distortion accumulation target; a finer field keeps distortion edges
sharp). Concussion blur, colour remaps, emissive falloff per frame, heat shimmer: unchanged (texture-coordinate
units or time constants; full audit table in pp-scale.md).

**Disadvantages, risks, deviations.**
- Deviates from Epic's PC code path deliberately (Epic's code at high resolution gives narrow blurs); equals the
  Xbox only by construction (same share of a 640x480 picture), not by an Xbox comparison.
- The emissive streak falloff (0.4 per frame) stays frame-rate dependent (not a distance; not this task).
- At 16:9 the concussion blur's horizontal wobble is Epic's material data in texture units, left as is.
- Several measurements needed re-runs because of machine load and the dynamic gamma's slow settling; the final
  run was on a quiet machine.

**Status.** Measured (pp-scale.md step 11, final binary, quiet machine, `pp-scale.sh` 10 PASS, 0 FAIL): glow width
8.54% (640x480), 8.54% (1440x1080; before 4.17), 8.33% (2880x2160; before 3.12); DOF sigma 3.10 / 3.15 / 3.10
reference pixels (before 1.45 / 0.70); radial blur equivalent inset 9.10 / 9.20 / 9.20 (before 4.60 / 2.30);
640x480 identical to main outside the spinning pickups; self-test 15/15. GPU (Quadro P620, frozen view): 3840x2160
about 11.3 ms against 12.5 ms before; 1920x1080 unchanged. LOG.md 2026-10-08: `pp-scale.sh` selftest/glow/dof/radial
6/0 on main.

**Off switch.** `PostProcessReferenceHeight=0` or console `PPREFERENCE 0` (Epic's sizes in back-buffer pixels).

### R-14 Xbox static-projector batching and the 12 projector shaders (M7b item 1, `XboxProjectors`)

**What the player/owner sees.** Map projectors (light patterns through grates and wire meshes, light pools) look
the same as before except the 19 projectors that blend with the surface's own material (`MaterialBlendingOp`): on
CTF-Recoil the corridor floors no longer turn dark and dirty under the wire-mesh grids; the grid is a clear pattern
of light. Six maps are affected (CTF-Recoil 8, DM-Meridian 4, OD-Cancer 2, DM-Legacy 2, ACT-SelketCheats 2,
OD-Prism 1); only CTF-Recoil was pictured. Fewer draw calls (DM-DeepCore 85 -> 81, CTF-Recoil 81 -> 75).

**Where.**
- Epic's shared code (commit `784596a`): `Engine/Src/UnGame.cpp:2681-2715` `UGameEngine::LoadMap`: the `_XBOX`
  block that re-packs every level-static projector into one `DT_StaticProjectorBatch` actor
  (`AProjector::RebuildStaticPrimsBatch`) runs `if( RenDevHasXboxProjectors() )`; the emptying of static-mesh
  index buffers that follows stays `_XBOX` (PC draws static meshes from them). `Engine/Inc/UnRenDev.h`
  `FRenderCaps::XboxProjectors`; `Engine/Src/UnRenderResource.cpp:61` `RenDevHasXboxProjectors()` (returns 1 under
  `_XBOX`). `RebuildStaticPrimsBatch`, `RenderStaticPrimsBatch` and the batch draw list were never gated.
- Driver: `Src/UC2GLMaterialState.cpp:3444` `SetProjectorMultiMaterial`: Epic's Xbox body (was `#ifdef _XBOX`, PC
  empty); `TS_WORLD/TS_WORLDINV` = the interface's `LocalToWorld` (:3594); the 13 `VSC_PROJ_*` registers to a
  `u_proj[13]` uniform (:3738); an Epic `debugf` with two `%s` and no arguments fixed (:3688); one log line per
  projector (:3843). `Src/UC2GLD3DXCompat.h`: D3DX vector helpers. `Src/UC2GLShaders.cpp:193-333`:
  `ProjectorAtten.vsh` and `TexSourceTexKill3.vsh` ported register for register; the 12 `PS_Projector_*` no longer
  engine-gated. `Src/UC2GLRenderDevice.cpp`: `ProjectorShadersReady`/`XboxProjectorsActive` (:2392-2407), switches
  (:599-602), console `XBOXPROJECTORS` (:2069, takes effect at the next map load); `ShaderStateKnown`: until
  `InitPixelShaders` has run the caps report what the switches ask for (the command-line map loads before the GL
  context exists, so the first map was batched the PC way).
- Driver defect fixed on the way: `Src/UC2GLRenderInterface.cpp:2072-2078`: M11 had made `DrawPrimitive` skip
  every draw between `BeginOcclusionQuery`/`EndOcclusionQuery`; deferred queries wrap an actor's real draw
  (`Projector.uc` `bUseDeferredHWOcclusionTests`), so most batches vanished. Now only passes with colour and depth
  writes off are skipped.
- Diagnostics: `Src/UC2GLSceneCensus.cpp` (console `GLSCENECENSUS`: every projector and terrain of the level,
  frustum-plane signs, leaf lists), `-glprojnoclip`, `GLSTATS [RESET]`.
- ini: `UseXboxProjectors=True` (`Default.ini:404`, both device sections, web too); README row 23.
- Tools: `tools/dev/m7b.sh` (171 lines: shadow, projector-*, terrain, twin-match), `tools/dev/m7b-census.sh` (47
  maps), `tools/dev/m11-twin.sh` (expects 97 fragment shaders and 485 programs since M7b).
- Commits: `458eebf`, `a891d63`, `d979b3b`, `bec8014`, `784596a`, `f7478bf`, `afb9914`; merged `1b33254`.

**How the parts fit.** The engine (Epic's code, gate lifted) builds the batch actor at map load; the driver draws
it with Epic's Xbox projector material: attenuation per vertex in `ProjectorAtten.vsh`, clipping per pixel by
`texkill`. Dynamic projectors (player shadows, decals, projectors following actors) stay on the PC path: on the
Xbox they go through push-buffer renderers whose per-pixel result is the PC path's (about 400 lines not ported for
no visible difference). No `.uc` change.

**Why.** M6/M7 had left the 12 projector shaders translated but unused (BLOCKERS.md "Open technical questions").
Rule for M7b: implement only where the Xbox path draws something visibly different, found on real content.

**Decisions and alternatives.** Only level-static projectors batched; dynamic ones not ported. Switching takes
effect at the next map load (switching a loaded level was tried and dropped: the static actor leaf lists do not
follow a re-attached projector).

**Disadvantages, risks, deviations.**
- Deliberate change of look on six maps (towards the Xbox's formula, which ignores `MaterialBlendingOp`, base
  opacity of masked surfaces, and does not darken whole unclipped BSP nodes); no Xbox reference picture.
- A comment in `UC2GLRenderDevice.cpp` near the compiled default says "projectors on skeletal meshes were drawn
  with nothing"; the notebook's pixel trace shows PC's software-skinned branch draws `FProjectorRenderInfo`'s
  material, so that comment overstates the reason (the code's behaviour is unaffected).
- `BLOCKERS.md` still lists the projector shaders and `PS_Terrain3Layer` as "not drawn yet" (stale since M7b).

**Status.** Measured: DM-DeepCore, where the formulas coincide, PC path vs Xbox batch mean 0.136/255, largest 4
levels (control 0.002); CTF-Recoil grid area mean 9.0 levels (deliberate). Twin: progtest 485 programs per side,
largest difference 1/255. LOG.md 2026-10-08: `m7b.sh` 11/0.

**Off switch.** `UseXboxProjectors=False`, `-noxboxprojectors`, console `XBOXPROJECTORS 0` (next map load).

### R-15 Xbox shadow textures (M7b item 3, `XboxShadows`)

**What the player/owner sees.** Player and bot blob shadows are lighter in the core and a little sharper: the
floor under the shadow keeps 43% of its light instead of 21%; PC's whole projection square was also 1.6% darker
than its surroundings, the Xbox's is not.

**Where.**
- Epic's shared code (commit `bec8014`): `Engine/Src/UnShadowProjector.cpp`: two `_XBOX && NEW_SHADOW_BLEND`
  sites now chosen at run time: the caster colour in `FShadowBitmapRenderInterface::SetMaterial` (:242-271, caster
  drawn in `ShadowDarkness` grey with alpha 255) and the filter in `UShadowBitmapMaterial::Get` (:611-761: one
  `PS_ShadowBlur` tile, four horizontal taps at -1, -1/3, +1/3, +1 texel, weights 1/6, 2/6, 2/6, 1/6, negated,
  instead of PC's nine canvas tiles). Both bodies unchanged; `RI->InsertFence()` stays `_XBOX`; the PC-only 256x256
  size override left alone. `UnRenDev.h` `FRenderCaps::XboxShadows`, `UnRenderResource.cpp:77`
  `RenDevHasXboxShadows()`.
- Driver: `XboxShadowsActive()` (`UC2GLRenderDevice.cpp:2430`): `UseXboxShadows` and the `PS_Blur` family drawable
  (i.e. post-processing on); switches `-noxboxshadows`/`-xboxshadows` (:589), console `XBOXSHADOWS` (:2048).
  Numeric check `Src/UC2GLShadowCheck.cpp` (console `GLSHADOWCHECK`: both render targets of every shadow material
  read back and the shadow recomputed on the CPU with the formula of the branch the engine took).
- ini: `UseXboxShadows=True` (`Default.ini:403`); README row 23. Tools: `tools/dev/m7b.sh shadow`,
  `tools/dev/m7b-darker.py` (pixel-by-pixel darkness comparison).

**How the parts fit.** Engine-side branch choice, drawn through the existing `PS_Blur` handler with
`BLUR_SHADOW`. Without post-processing the capability is off, so `-nopostprocess` also gives PC shadows.

**Why.** Reading Epic's code showed a visible difference by arithmetic (projectors.md step 0); M7 had left it
because it "would change every shadow and is not post-processing".

**Decisions and alternatives.** On by default (it differs visibly and is the Xbox behaviour). Tied to
post-processing rather than independent.

**Disadvantages, risks, deviations.** Changes every player shadow. No Xbox reference (the owner's xemu shots show
no shadow that can be made out on those dark floors). The shadow texture is stored RGBA8 where the Xbox used L8.

**Status.** Measured: `GLSHADOWCHECK` on DM-DeepCore, DM-Praxis, DM-Azure: PC branch recomputed exactly (0.00/255),
Xbox branch 0.56/255; core 55 vs 27, neutral 128 vs 126. Seen: DM-DeepCore pair, mean difference 0.305 (control
0.023).

**Off switch.** `UseXboxShadows=False`, `-noxboxshadows`, console `XBOXSHADOWS 0`, or `-nopostprocess`.

### R-16 Three-layer terrain pass, `PS_Terrain3Layer` (M7b item 2), off by default

**What the player/owner sees.** Nothing (by default off; when on, no visible difference: largest 2 levels of 255
on DM-Ascension, a few draw calls fewer).

**Where.** Driver only (the engine's PC branch already reads the caps): `GetRenderCaps`
(`UC2GLRenderDevice.cpp:2377`) reports `MaxSimultaneousTerrainLayers = 3` when `Terrain3LayerActive()` (:2409);
`SetTerrainMaterial` (`UC2GLMaterialState.cpp:2945`) sets the `_XBOX` mask constants c0..c2 and draws the combined
pass with the program; `RefreshTerrainPasses` (:2418) re-runs `ATerrainInfo::Update` on every loaded terrain;
switches `-terrain3layer`/`-noterrain3layer` (:594-598), console `TERRAIN3LAYER` (:2080). ini
`UseTerrain3Layer=False` (`Default.ini:405`). Commits `bec8014`, `3966bdb`.

**How the parts fit / Why.** The engine builds `RM_CombinedWeightMap` passes when the caps say 3 (the Xbox
hard-codes 3). The difference by arithmetic is one clamp/rounding instead of three; it only shows where a layer
saturates under bright vertex light.

**Decisions and alternatives.** Implemented, measured, left off (the rule: implement where the Xbox looks
different; here it does not). Kept as the exact Xbox structure and a draw-call saving.

**Disadvantages, risks, deviations.** Untested in game play with the switch on beyond three views; more code paths.

**Status.** Measured: DM-Ascension mean 0.039, largest 2; CTF-Judgment mean 0.147, largest 21 on 0.034% of pixels;
DM-Apex 0.050.

**Off switch.** It is off (`UseTerrain3Layer=False`).

### R-17 Movie textures on the GL side (`MovieMaterial`, M9; "never green", O-1)

**What the player/owner sees.** Animated movie textures in the menus (the arena page's game-type portraits) and
in levels play; a movie that cannot be opened shows its loading fallback or black, never Epic's bright-green
default texture. (The movie player, cooking and full-screen movies belong to the video section; only the GL side
is here.)

**Where.**
- New: `Source/UC2GLDrv/Src/UC2GLMovie.cpp` (116 lines; commit `dbbf486`): the PC branch of
  `FOpenGLRenderInterface::SetMovieMaterial` (`#if !defined(_XBOX)`). The movie logic (reset, open on first use,
  advance once per level time step, `OnDonePlaying`, `LoadingFallback`) is the engine's `appUpdateMovieMaterial`
  (`Engine/Src/UnMovie.cpp`, shared by every device); this file uploads the current BGRA frame into the cache
  entry's GL texture only when the material is drawn and the picture revision changed (kept in the material's
  transient `LastD3DTexture`), and sets the Xbox branch's state (SELECTARG1, SRCALPHA/INVSRCALPHA or
  ONE/INVSRCCOLOR when `bAdditive`, the mask's alpha on stage 1). No linear-texture matrix (the Xbox's `LIN_`
  textures were addressed in texels).
- `Src/UC2GLMaterialState.cpp:4647-4895`: Epic's Xbox body stays under `#ifdef _XBOX`; the PC stub that returned 0
  was removed (the only line range removed from that file).
- The "green" fix is in the engine, not the driver: `Engine/Src/UnMovie.cpp` `appUpdateMovieMaterial` returns a
  4x4 opaque black picture (`FBlackMoviePicture`) or the `LoadingFallback` while a material has no picture
  (web-audio.md step 1 (c); the video section owns it).
- Web: frames are uploaded as `GL_BGRA`, which WebGL 1 lacks; GL4ES converts to RGBA on the CPU.

**How the parts fit.** Engine: decoding and timing (`UC2Movie` player behind Epic's movie interface). Driver:
upload and blend state only. Without the engine fix a missing movie drew `Engine.DefaultTexture` (green); the web
build had no movies in its manifest at first, so every movie texture there was green.

**Why.** Epic's PC driver returned 0 from `SetMovieMaterial` (default material); M9 scope. Owner's O-1: "videos
play green".

**Decisions and alternatives.** Keep the driver part in its own file so `UC2GLMaterialState.cpp` stays diffable;
upload on draw, not on decode.

**Disadvantages, risks, deviations.** The CPU BGRA-to-RGBA conversion on the web costs time per frame upload (not
measured). The menu-movie screenshot check is timing-sensitive and fails on loaded machines (`m11-twin.sh` known
FAIL `m9-menu-movie-shots`).

**Status.** Measured (web-renderer.md G5 step 1): ES 2.0 twin with GL4ES forced to convert BGRA
(`LIBGL_NOBGRA=1`, the WebGL case): logos match their cooked frames at 82.4 / 65.8 dB PSNR (a red/blue swap would
not), menu movie advancing, 0 GL errors. Native: `m9-movies.py` 19/0, movie self-test 96-99/0 (LOG.md M9, M11 audio).

**Off switch.** None in the driver; movies themselves through the `[UC2Movie]` keys (video section).

### R-18 Anti-aliasing (4x MSAA on desktop, FXAA on GLES/WebGL) and 16x anisotropic filtering (O-8)

**What the player/owner sees.** Smoother edges and sharper floors at grazing angles by default: desktop GL draws
the scene 4x multisampled; the browser (and the GLES twin) applies FXAA in the present pass; textures are
filtered with 16x anisotropy wherever the hardware allows.

> Superseded (2026-10-09/10): see section 60, R-18.

**Where.**
- `Src/UC2GLRenderDevice.cpp`: `SUPPORTS_MSAA` (:359, GL 3.0 or `ARB_framebuffer_object`); the choice (:764-769:
  `PresentFXAA = AntiAliasing > 0 && (IsGLES || !SUPPORTS_MSAA)`); `CreateMainFramebuffer` (:880-893: multisampled
  RGBA8 and D24S8 renderbuffers); `ResolveMain` (:949; `glBlitFramebuffer` :963) resolves into the existing colour
  and depth textures only when something reads them and they changed: the back-buffer snapshot and depth-as-colour
  copy, `CopyDepthBuffer` and its fallback, the present pass, `ReadPixels`, render targets that borrow the main
  depth. `GPresentFragmentShader` (:1480-1520): "FXAA PC simple" (Timothy Lottes' public reduced form) inside the
  present pass, `uFXAA` uniform. Switches `-glaa=N`, `-glnoaa` (:607-609), `-glshotpresented` (screenshot after the
  present pass).
  *(Superseded (2026-10-09/10): see section 60, R-18.)*
- `Inc/UC2GLFuncs.h:144-147` the two optional entry points.
- Anisotropy: `Src/UC2GLRenderInterface.cpp:1449`: a linear minifying filter on a mipmapped texture gets
  `GL_TEXTURE_MAX_ANISOTROPY_EXT = LevelOfAnisotropy` (clamped to the hardware's maximum); point-filtered
  post-process reads and render targets stay at 1. Before, anisotropy was applied only to an explicit
  ANISOTROPIC filter, i.e. never (D3DDrv made every stage's default minifying filter anisotropic when
  `LevelOfAnisotropy > 1`, `D3DRenderState.cpp`).
- `Src/UC2GLSelfTest.cpp:1128-1132`: the self-test refuses to run with AA on (it compares exactly).
- ini: `LevelOfAnisotropy=16`, `AntiAliasing=4` (`Default.ini:390-391`, both device sections, web too; compiled
  defaults the same, `UC2GLRenderDevice.cpp:227-228`); README row 14.
- Tools: `tools/dev/run-uc2-shot.sh` adds `-glnoaa` to every run unless `UC2_AA=1`; `m10-controls.sh` runs with
  `-glnoaa` (its key timings predate AA; with 4x MSAA on software GL menu steps came late).
- Commits: `6921897`, `21cbe5f`, `6e542ac`, `518b5f5`, `bce3377`.

**How the parts fit.** Driver-only; the engine is unaware. On WebGL the context's own `antialias` attribute is off
because the scene is drawn into the driver's framebuffer, not the canvas.

**Why.** Owner decision O-8 (PLAN.md): "anisotropic filtering 16x; anti-aliasing 4x (multisampled main framebuffer;
WebGL 1 has no multisampled framebuffer objects: WebGL 2 or a post-process AA on the web, decided with evidence)".

**Decisions and alternatives.** Desktop: MSAA with resolve-on-read. WebGL 1: the context `antialias` attribute
rejected (it would multisample only the canvas, which receives one full-screen quad); WebGL 2 rejected for now
(GL4ES emits ESSL 1.00 only; WebGL 2 gives ESSL 1.00 shaders no `EXT_frag_depth` and rejects GL4ES' unsized depth
formats: it would move the web renderer to an untested context class); FXAA in the existing present pass chosen
(no extra pass or target).

> Superseded (2026-10-09/10): see section 60, R-18.

**Disadvantages, risks, deviations.**
- FXAA also smooths the HUD and menus (drawn into the same frame); 9 texture reads per pixel in the present pass.
- MSAA costs memory and resolve blits per read (3-4 per frame with post-processing); on software GL it slowed
  menus enough to break timing-based tests.
- A player's saved `Avaris.ini` (or the browser's saved settings in IndexedDB) from before O-8 keeps its
  `LevelOfAnisotropy=1` until the saved settings are cleared (the engine derives the user ini from `Default.ini`
  once); a missing `AntiAliasing` key gets the compiled default 4.
- Desktop and web anti-aliasing differ in method and look.

**Status.** Measured: desktop MSAA vs off mean 1.13 (0.99% of pixels above 32 levels: the edges); twin FXAA vs off
1.06 (1.19%); anisotropy before/after mean 2.33 (`logs/m11/aniso-crops.png`); 0 GL errors and 0 Mesa errors.
Browser: `anisotropy 1 of 16` was reported with the old ini; with the new default the extension is used (not
re-logged in the notebook).

**Off switch.** `AntiAliasing=0` or `-glnoaa`; `-glaa=N`; `LevelOfAnisotropy=1`.

> Superseded (2026-10-09/10): see section 60, R-18.

### R-19 The GL4ES binding (`UC2GL4ESDrv`) and the native GLES "twin" (M11)

**What the player/owner sees.** On Linux, `-gl4es` runs the game through the same code path the browser uses (an
OpenGL ES 2 context behind GL4ES). In the browser this is the renderer.

**Where.**
- New package `Source/UC2GL4ESDrv` (3 files, 329 lines): `UUC2GL4ESRenderDevice`, a subclass of the desktop
  device that overrides only the `Binding*` virtuals (`UC2GL4ESDrv.cpp`: context attributes :70, context creation
  :86, drawable size :164, swap interval :180, swap :187, GL4ES init :210 with the `LIBGL_*` environment of the
  engine-ut2004 template plus `LIBGL_NOPSA=1`, proc lookups :250-262). It is the only code that links GL4ES; GL4ES'
  headers are deliberately not on the include path. `CMakeLists.txt`: skipped where GL4ES is not fetched
  (Windows).
- Driver changes for the run-time binding (commits `aad99a0`, `5122c93`, `6c6c049`, `c111785`, `0d7cf34`,
  `209d8ad`, `74e34d0`, `a4975f2`, `c66d275`, `f5fbcae`, `89204c7`):
  - `Inc/UC2GLBinding.h` rewritten (the binding is a property of the device object); `Inc/UC2GLRawFuncs.h`: a
    second, "raw" table of GLES calls looked up under GL4ES;
  - `Src/UC2GLRenderDevice.cpp`: `Binding*` desktop implementations (:396-470), `ShaderPrologue` (:479; `#version
    120` or `#version 100` + precision, `UC2_HIGHP` for depth samplers), `FindRawExt` (:380), `DetectCapabilities`
    (:1415, the extension flags and the mask switches), depth-stencil storage and attachment **around GL4ES**
    (:1027-1215: GL4ES would give WebGL 1 a 16-bit depth + separate stencil, which WebGL rejects; one
    `#ifdef __EMSCRIPTEN__` for `DEPTH_STENCIL_ATTACHMENT`), engine texture storage around GL4ES (`TexImage2D`
    :1258, `CompressedTexImage2D` :1216: GL4ES would regenerate every mip chain with a box filter and always
    decode DXT in software), `NewBuffer`/`FreeBuffer` (:1306-1330: on GLES buffer names are reused, never deleted,
    because GL4ES caches attribute bindings by name), `PrepareGL4ESTextureZero` (:1380: a raw 1x1 black texture
    under GL4ES' "texture 0"; WebGL has no default texture, GL4ES set its parameters with null bound
    (`GL_INVALID_OPERATION`) and called `glGenerateMipmap` on whatever DXT texture was active);
  - `Src/UC2GLResource.cpp`: mip chains completed by the driver on GLES (`CompleteChain` :320,
    `CompleteCompressedChain` :473: no `GL_TEXTURE_MAX_LEVEL` in GLES 2), D3DCOLOR reordered to RGBA on GLES,
    targets smaller than the main framebuffer get no shared depth on GLES (WebGL: incomplete dimensions);
  - `Src/UC2GLRenderInterface.cpp`: wireframe drawn as line lists on GLES (`DrawWireframe` :2195; GL4ES' wireframe
    path crashed with real buffer objects; "written, never drawn by content"), query boxes not drawn;
  - shaders: no float literal with more than six significant digits (GL4ES reprints literals: `16777215.0`
    became `16777200`), `highp` depth samplers (lowp default computed 11 bits of depth).
- `CheckGLError` also drains the raw library's `glGetError` (GL4ES answered from its own record only, so the twin's
  first "0 GL errors" meant nothing); `-glerrorsdraw` checks after every draw.
- Build: root `CMakeLists.txt:356-399` fetches `https://github.com/10-p/gl4es.git` at branch
  `fix/cubemap-bind-active-unit` (static, GLES2, no X11/EGL, `-O2` forced); `Source/SDLLaunch/CMakeLists.txt:26-28`,
  `SDLLaunch.cpp:41-42, 306, 518-519` (`-gl4es`).
- ini: `[UC2GL4ESDrv.UC2GL4ESRenderDevice]` in `Default.ini:410-440` (same values as the desktop section).
- Tools: `tools/dev/m11-twin.sh` (255 lines: shaders, gputest, boot, selftest, progtest, pinned, effects, m6,
  fallbacks, movies, each desktop against twin; `M11_SERIAL=1` one binding at a time; the twin runs an ES 2.0
  context with `MESA_GLES_VERSION_OVERRIDE=2.0` and `MESA_DEBUG=1`), `tools/dev/m11-progcmp.py`,
  `tools/dev/imgdiff.py`.

**How the parts fit.** One object code for both bindings: everything else asks `IsGLES` and the `Cap*` flags at
run time, so a renderer fix reaches the web by itself. Through GL4ES go all fixed-function state (its emulation
is why GL4ES is there), all GLSL programs (already ES 1.00 subset, GL4ES swaps the header), buffers, colour
attachments; around it go depth-stencil storage/attachment and engine texture storage.

**Why.** PLAN.md M11 ("UC2GL4ESDrv on WebGL"); the owner's decision to reuse engine-ut2004's GL4ES approach; the
twin exists so the renderer tests run on the web code path without a browser (the memory incident of 2026-10-04
made browser runs expensive and rare).

**Decisions and alternatives.** Run-time binding by subclass instead of compiling the driver twice or mirroring
directories (the template's way); WebGL 1 instead of WebGL 2 (GL4ES emits ESSL 1.00 only); depth-stencil and
texture storage around GL4ES (each forced by a measured GL4ES behaviour).

**Disadvantages, risks, deviations.**
- Depends on a fork branch of GL4ES (`10-p/gl4es`, branch `fix/cubemap-bind-active-unit`, unpinned): an upstream
  change can break the build or behaviour.
- Calling GLES behind GL4ES' back is fragile: GL4ES caches state; the rules that keep it consistent are in comments
  only.
- No texture LOD bias on GLES (GL4ES stores and never applies it): the desktop uses `DefaultTexMipBias=-0.5`, so
  far textures are slightly softer on the web (pinned DM-Deadbolt desktop vs twin mean 1.415; 0.374 without the
  bias). Open.
- The DM-Azure fogged sky zone is hazier on desktop than on the twin (fog share 0.88-0.91 vs 0.54-0.57; the fog
  formula itself is equal on both, self-test `fog`); open, cause suspected in GL4ES' fixed-function emulation.
- A small unexplained residue (mean 0.38/255, nothing above 32 levels) on DM-Deadbolt desktop vs twin.
- Mesa answers an ES 2.0 request with ES 3.2 unless overridden: ES-2-only violations are caught only by the
  override or the browser.
- GL4ES draws one pixel for `GL_POINTS` with a user program (the engine's point lists would be wrong; none seen).
- GL4ES keeps a CPU copy of every buffer: vertex/index data exists two or three times in memory.

**Status.** Measured (LOG.md M11; web-renderer.md gate): `m11-twin.sh` 31 PASS, 3 FAIL on main (the depth-texture
fallback x2, menu-movie timing); self-test 11/11 then 15/15 on both bindings with the desktop's tolerances; program
test 420 (now 485) complete programs desktop vs twin, largest difference 1/255; effect pairs within the noise
floor (liandri random). Every GL4ES defect listed was found by these tests.

**Off switch.** Native: do not pass `-gl4es` (the desktop device is the default). Web: none (the only renderer).

### R-20 The web renderer: WebGL 1 context, fallbacks, frame presentation (M11)

**What the player/owner sees.** The game draws in the browser at the canvas's device-pixel size (4K full screen
works), with post-processing, effects, movies; in the harness it runs on the real GPU (ANGLE/Vulkan on the
Quadro P620).

> Superseded (2026-10-09/10): see section 60, R-20.

**Where.**
- `Source/UC2GL4ESDrv/Src/UC2GL4ESDrv.cpp` (`__EMSCRIPTEN__`): the WebGL 1 context is created by the device on
  the engine's worker (`emscripten_webgl_create_context("#canvas")`, :118-147: `majorVersion 1`, alpha/depth/
  stencil/antialias 0, explicit swap control, `renderViaOffscreenBackBuffer`); SDL keeps the window and input
  (Emscripten issue 24792: SDL's EGL path is proxied to the main thread, which no longer owns the canvas); the
  device draws at the canvas's size in device pixels whatever the ini says (:89-103, commit `c66d275`); present =
  `emscripten_webgl_commit_frame()` (:187-191); swap interval ignored on the web (:180-185).
- `Source/SDLLaunch/cmake/Web.cmake:88-104`: `-sGL_ENABLE_GET_PROC_ADDRESS=1`, `-sMIN_WEBGL_VERSION=1
  -sMAX_WEBGL_VERSION=1` (**no WebGL 2**), `-sFULL_ES2=1`, `-sOFFSCREEN_FRAMEBUFFER` (without it
  `commit_frame` is a stub and presentation depends on the compositor: about 1 frame/s on Windows in the sibling
  ports). Commit `34b4ed2`.
  *(Superseded (2026-10-09/10): see section 60, R-20.)*
- Frame pacing (launcher, not the driver; `Source/SDLLaunch/Src/SDLLaunchWeb.cpp:25-42`, M4w `45f7301`): one
  Asyncify yield per frame through `setTimeout(0)`; a standalone client's max tick rate is 0, so there is no
  frame cap and no `requestAnimationFrame` pacing: the browser composites whatever was committed.
- `Source/SDLLaunch/Src/SDLLaunchWeb.cpp` `ue_exec_command` passes what the engine does not take to the first
  viewport, so device console commands (`GLPROBE`, `POSTPROCESS` ...) work from the page (`a860ac8`).
- Fallbacks (`DetectCapabilities`, masks for testing on any binding):

  | Capability (WebGL 1 extension) | Without it | Status |
  |---|---|---|
  | S3TC (`WEBGL_compressed_texture_s3tc`; `-glnos3tc`) | the driver decodes DXT1/3/5 itself (`UC2GL_DecodeDXT`, `UC2GLResource.cpp:139`) | implemented; it first crashed on every level (BSP lightmaps have no `UTexture`), fixed `89204c7`; before that any phone without S3TC would have crashed at the first level; measured mean 0.019 vs S3TC |
  | `GL_TEXTURE_MAX_LEVEL` (none in GLES 2; `-glnomaxlevel` on desktop) | chains completed by the driver | implemented, always on GLES; DXT completion fixed for non-square levels (`a4975f2`, a WebGL `INVALID_VALUE`) |
  | `EXT_frag_depth` (`-glnofragdepth`) | the accumulation target's depth is cleared and every program pass into it tests the scene depth in the shader (`PSV_SceneDepthTest`: generated `main()` renamed, `discard`) | implemented (`f5fbcae`), self-test PASS (3 units of 2^24); a picture with distortion under the mask still pending |
  | `OES_element_index_uint` (`-glnouintindex`) | 32-bit indices narrowed to 16 bits on upload | implemented, self-test `index-narrow`; never reached by content (the engine makes no 32-bit index buffers) |
  | `WEBGL_depth_texture` (`-glnodepthtexture`) | depth of field off (picture stays sharp, log line) and distortion drawn over nearer geometry | **NOT implemented** (needs a second draw of every depth-writing primitive; about doubles draw calls on exactly the weak devices); every desktop ANGLE back end has the extension |
  | fragment `highp` | not handled (the depth encode needs it); the log says so | open |

  *Superseded (2026-10-09/10): see section 60, R-20.*

- ini: `Engine/Config/web/Default.ini:19` `RenderDevice=UC2GL4ESDrv.UC2GL4ESRenderDevice`; otherwise the render
  sections are identical to native (generated by `tools/dev/sync-web-ini.py`, which keeps only listed web
  deltas: render device, `UseJoystick=False`, network device, and the web-owned audio/movie sections).
- Frontend (ufrontend `uc2` branch; detail in the frontend section): `packages/e2e/uc2-wasm-boot.spec.js` (menu,
  DM-Deadbolt with post-processing, a play test with real input; compares the canvas with the twin's reference
  pictures, tolerances 10 and 3), commits `b1e7ef6`, `c26c4b7`, `68e3a5d`, `17758ef`.

**How the parts fit.** Same driver as R-19; the only web-specific code is context creation/presentation in the
GL4ES subclass, one `#ifdef` for the depth-stencil attachment point, and link flags. Owner-visible defaults equal
native except AA (FXAA instead of MSAA, R-18).

**Why.** PLAN.md M11 "Web product"; owner (2026-10-06): the web build renders at the canvas size and maps play.

**Decisions and alternatives.** WebGL 1 only (R-18, R-19). Context on the worker (template's approach).
Depth-texture fallback deferred with a written reason (web-renderer.md "Fallbacks").

> Superseded (2026-10-09/10): see section 60, R-20.

**Disadvantages, risks, deviations.**
- Tested in one browser/GPU combination only (headless Chromium, ANGLE on Vulkan, NVIDIA): every extension present,
  so no fallback was active in a real browser; the fallbacks are tested by masks on the twin and desktop.
- No frame cap or rAF pacing: frame rate is whatever the worker achieves; power use and tearing behaviour in real
  browsers not examined.
- Per frame in the browser (DM-Deadbolt, post-processing on, run 12 on a quiet machine): 26-30 ms, ~98 draws,
  34,399 primitives, 93 material state applications (the engine hands a new pool object per draw, so the driver
  re-applies state almost every draw; content-based skipping not done), 3 back-buffer snapshots + 1
  depth-as-colour per frame (Epic's blur reads, writes, reads the back buffer: not waste on GL).
- Pointer lock was not granted in the headless page (frontend's business).

**Status.** Measured: browser spec `3 passed` (runs 20, 23, 24, 25), 0 GL errors in every test; menu vs twin 6.36
(tolerance 10), DM-Deadbolt vs twin 1.12 (tolerance 3); browser PSS peak 1.4-1.7 GB, wasm memory 256 MiB. LOG.md
2026-10-08: browser boot 4/4 after M7b/O-10. Not verified: the DM-Azure pool cube-map fix (R-12) on the twin or in
the browser; any phone or Safari/Firefox.

**Off switch.** None for the web renderer itself; masks (`-glnos3tc` etc.) are test switches.

> Superseded (2026-10-09/10): see section 60, R-20.

### R-21 Test and diagnostic hooks compiled into the driver, and the renderer test scripts

**What the player/owner sees.** Nothing unless a switch or console command is used. They exist because there is
no Xbox to compare with: most renderer evidence in this report comes from them.

**Where.**
- In the shipping driver (all in `Source/UC2GLDrv`):

  | Hook | File | What it does |
  |---|---|---|
  | `-glerrors`, `CheckGLErrors=True`, `-glerrorsdraw` | `UC2GLRenderDevice.cpp` | `glGetError` per frame (and the raw GLES library's), "frame N, M GL errors so far" every 600 frames; per draw with the material named |
  | `-glshot=<s>,...`, `-glshotramp`, `-glshotpresented` | `UC2GLRenderDevice.cpp:34-58, 555` | the engine's own `SHOT` at those times; with the gamma ramp; after the present pass (FXAA, gamma) |
  | `-glexec=<s>:<cmd>@...` | `:46-48` | console commands at given times (`+` = space) |
  | `-glpstoggle=`, `-glpptoggle=`, `-glfftoggle=` | `:558-588` | flip pixel shaders / post-processing / FF programs at given times (A/B pictures from one camera) |
  | `POSTSELFTEST`, `-glselftest`, `-glselftest1d` | `UC2GLSelfTest.cpp` (1,195 lines) | 15 numeric checks of the post-process primitives, fog, the GLES fallbacks and O-10's reference copy, plus the 420/485-program test (`UC2GLShaders.cpp:1147`) |
  | `COMBINERTEST`, `-glcombinertest` | `UC2GLCombinerTest.cpp` | 35 cases of the fixed-function stage arithmetic against the D3D8 SDK model (R-5) |
  | `GLMATCENSUS`, `-glmatcensus` | `UC2GLPixelTrace.cpp` | every loaded material compiled with three cap sets (R-5) |
  | `GLPIXTRACE <x> <y> [all]` | `UC2GLPixelTrace.cpp` | what the next frame draws over a pixel; distortion accumulation statistics (R-12) |
  | `GLSCENECENSUS`, `GLSHADOWCHECK` | `UC2GLSceneCensus.cpp`, `UC2GLShadowCheck.cpp` | projectors/terrains of the level; shadow textures recomputed on the CPU (R-14, R-15) |
  | `GLINFO`, `RESOURCES`, `GLTEXELS <tex>`, `GLPROBE`, `GLSTATS [RESET]`, `-glstats`, `-glsnapshotlog` | `UC2GLRenderDevice.cpp`, `UC2GLPostProcess.cpp` | state dumps, texel dumps, per-frame counts every 300 frames, who asked for each back-buffer snapshot |
  | masks: `-glnos3tc`, `-glnomaxlevel`, `-glnofragdepth`, `-glnouintindex`, `-glnodepthtexture`, `-glnolodbias`, `-glnofog`, `-glnocombine3`, `-glnocombine4`, `-glunits=N`, `-glstages=N`, `-glprojnoclip`, `-gldualdiffuse=N`, `-glffsource` | `UC2GLRenderDevice.cpp:565-615` and others | run a fallback or a degraded path on any binding |
  | effect switches | `UC2GLRenderDevice.cpp` `Exec` | `PIXELSHADERS`, `POSTPROCESS`, `FFPROGRAMS`, `DOFSHIPPEDALPHA`, `DISTORTIONCULLDISTANCE`, `PPREFERENCE`, `XBOXSHADOWS`, `XBOXPROJECTORS`, `TERRAIN3LAYER` (Epic's own `DYNAMICGAMMA`/`LUMINOSITYBIAS` are in `UnCamMgr.cpp`) |

- Scripts (`tools/dev`, all run through `bin/capped`): `run-uc2-shot.sh` (121 lines: one capped run with Fire
  injected, the engine's screenshots, programs and error counts; `UC2_RES`, `UC2_PRECMD`, `UC2_BIN`, `UC2_AA`,
  `UC2_CAP`), `m7-effects.sh` (159: the 14 effect pairs, self-test, the DM-Praxis wine-reference check),
  `fx-owner.sh` (187: owner-report scenarios, `UC2_GPU=1` runs headless on the NVIDIA GPU), `m11-twin.sh` (255),
  `m11-progcmp.py`, `o9-nvidia.sh` (220), `o9-census.sh`, `o9/*.scen`, `m7b.sh` (171), `m7b-census.sh`,
  `m7b-darker.py`, `pp-scale.sh` (347), `pp-measure.py` (319), `ps-census.py`, `imgdiff.py`, `pinned-compare.py`,
  `sync-web-ini.py`; `tools/psh2glsl.py` and `tools/pshgpu/pshgpu.c` (R-3). Outside the repo:
  `<WorkUC2>/bin/oracle-shot.sh` (Epic's `avaris.exe` under wine: the PC reference pictures).

**Why / Decisions.** No Xbox oracle (xemu exists but is slow, "last resort"); numbers over pictures where
possible; every test runs inside a memory and time cap after the 2026-10-04 incident.

**Disadvantages, risks, deviations.**
- About 2,500 lines of test/diagnostic code ship inside the driver (self-test, combiner test, pixel trace,
  censuses, shadow check) and dozens of switches; harmless when unused, but maintenance weight and attack surface
  for console commands.
- Several picture checks are timing-sensitive and fail on a loaded machine (menu-movie shots, liandri noise,
  blur phase, the controls tests with AA); LOG.md records them as known FAILs rather than fixed tests.
- `run-uc2-shot.sh`/`fx-owner.sh` reference pictures now run with the pre-D-2 UI and AutoFOV off and without AA:
  the pictures do not show what the player sees by default.

**Status.** These are the instruments; their results are in each feature's Status.

**Off switch.** Not needed (off unless invoked).

### R-22 Render-related ini keys

All in `Engine/Config/Default.ini` (native) and `Engine/Config/web/Default.ini` (web, generated by
`tools/dev/sync-web-ini.py`); the web file differs from native only in `RenderDevice`. Each device section exists
twice with the same values: `[UC2GLDrv.UC2GLRenderDevice]` (`Default.ini:377-406`) and
`[UC2GL4ESDrv.UC2GL4ESRenderDevice]` (`:410-440`, read by the GL4ES subclass). "Compiled" = value when the key is
absent (`UC2GLRenderDevice.cpp:201-248`). Rows are in `Engine/Config/README.md` (lines 12-24, 72).

| Section | Key | Shipped/Epic | Ours (ini / compiled) | What it switches | Feature |
|---|---|---|---|---|---|
| `[Engine.Engine]` | `RenderDevice` | `D3DDrv.D3DRenderDevice` | `UC2GLDrv.UC2GLRenderDevice` (web: `UC2GL4ESDrv.UC2GL4ESRenderDevice`) | the device | R-1, R-20 |
| `[SDL2Drv.SDL2Client]` | `MaxDistortion` | none on PC (Xbox 35.0) | `35.0` / device falls back to 35 | distortion displacement, pixels of 640x480 | R-11 |
| `[SDL2Drv.SDL2Client]` | `Brightness`, `Contrast`, `Gamma` | 0.8 / 0.7 / 0.8 | unchanged | present-pass ramp | R-2 |
| device | `UsePixelShaders` | - | `True` / 1 | GLSL for pixel-shader materials | R-4 |
| device | `PrecompileVertexShaders` | - | `True` / 1 | precompile from `VertexShaderConfigs.ini` | R-4 |
| device | `FixedFunctionPrograms` | - | `True` / 1 | FF stages as generated GLSL (desktop only) | R-5 |
| device | `MaterialStages` | (D3DDrv clamps to 4) | `4` / 4 | stages given to the material compiler | R-5 |
| device | `MaxTextureUnits` | (Epic's OpenGLDrv section: 8) | `8` / 8 | upper bound on units | R-5 |
| device | `UsePostProcess` | - | `True` / 1 | `XboxPostProcess` capability | R-6..R-8 |
| device | `DOFShippedBlurAlpha` | - | `True` / **0** | DOF alpha 65 (shipped) vs script's 100 in third person | R-10 |
| device | `DistortionCullDistance` | constant 5000 | `15000` / 5000 | distortion fade distance (read from the `UC2GLDrv` section only) | R-11 |
| device | `PostProcessReferenceHeight` | - | `480` / 480 | blur sizes for a 480-line picture; 0 = Epic | R-13 |
| device | `UseXboxShadows` | - | `True` / 1 | Xbox shadow filter | R-15 |
| device | `UseXboxProjectors` | - | `True` / 1 | static projector batching | R-14 |
| device | `UseTerrain3Layer` | - | `False` / 0 | three terrain layers per pass | R-16 |
| device | `LevelOfAnisotropy` | 1 (D3DDrv section) | `16` / 16 | anisotropic filtering | R-18 |
| device | `AntiAliasing` | none | `4` / 4 | MSAA samples (desktop) or FXAA on (GLES) | R-18 |
| device | `CheckGLErrors` | - | `False` / 0 | per-frame GL error check | R-21 |
| device | `DetailTexMipBias`, `DefaultTexMipBias` | 0.8, -0.5 (D3DDrv) | same | LOD bias (not applied on GLES) | R-1, R-19 |
| device | `UseTrilinear`, `UseMipmapping`, `UseCubemaps`, `UseCompressedLightmaps`, `UseStencil`, `UsePrecaching`, `UseVSync`, `Use16bit`, `Use16bitTextures`, `DetailTextures`, `HighDetailActors`, `SuperHighDetailActors` | D3DDrv's values | same | Epic's generic device switches | R-1 |

Not ini keys but Epic's client properties the device sets at its first frame with post-processing on:
`UClient::DynamicGamma = 1`, `LuminosityBias = 1.0` (R-9).

## Open issues in this area

1. **No Xbox reference for most of the look.** Glow and bloom strength, dynamic gamma on dark maps, distortion
   size (height/480 scaling), the Xbox shadow filter, the projector change on six maps, the berserk overlay on pawn
   attachments: all "seen only" or numerically checked against Epic's code, never against an Xbox picture. xemu
   exists (`<WorkUC2>/xbox-emulation/run-uc2-iso.sh`) but is slow; a few targeted xemu shots would settle the biggest
   ones (glow strength, a shadow, CTF-Recoil's grid).
2. **WebGL 1 without `WEBGL_depth_texture`: fallback not implemented** (depth of field off, distortion drawn over
   nearer geometry); a device without fragment `highp` is not handled. `m11-twin.sh` reports both as known FAILs.
3. **Web vs desktop differences left open:** no texture LOD bias on GLES (far textures softer than desktop's -0.5
   bias); DM-Azure's fogged sky zone hazier on desktop than through GL4ES (cause not found); a 0.38/255 residue on
   DM-Deadbolt; the DM-Azure pool fix (R-12) not checked on the twin or in a browser.
4. **O-6 (DM-Azure waterfall in first person) not reproduced**; needs the owner's exact spot, window size and view
   history (BLOCKERS.md). The "waterfall over the character" is Epic's half-resolution depth mask; not changed.
5. **`POSTPROCESS 0` while running** leaves distortion materials on a wrong fallback (DM-Azure's cobra waterfall
   becomes an opaque violet stream); starting with `-nopostprocess` was not checked.
6. **`DOFShippedBlurAlpha`: ini `True`, compiled default `0`.** An ini without the key gets the non-shipped alpha.
7. **Dynamic gamma is forced on at every session start** (the device sets `UClient::DynamicGamma = 1`); a user can
   only turn it off per session (`DYNAMICGAMMA 0`).
8. **Performance not characterised on a GPU per effect.** Software GL: post-processing 3.3-3.8x frame time.
   Known waste: material state re-applied on ~95% of draws (the engine hands a new pool object per draw), program
   path binds program and uploads uniforms per draw, 3-4 full-screen snapshots per frame, MSAA resolves, GL4ES'
   CPU copies of buffers and BGRA conversion of movie frames on the web.
9. **Never run:** AMD or Intel GPUs, real Windows (wine only), phones, Safari/Firefox, split-screen, cinematics
   with focus depth of field, the sniper overlay; 12 of the 25 post-process shaders never drawn by content
   (additive and subtractive distortion, sniper colour remap, player-cam noise variants, the motion-blur mask, ...;
   five of them unreachable by design).
10. **Timing-sensitive checks** recorded as known FAILs rather than fixed: Liandri overlay noise (needs fixed random
    numbers or a mask), menu-movie screenshots, blur phase between bindings.
11. **External dependency:** GL4ES from the `10-p/gl4es` fork, branch `fix/cubemap-bind-active-unit`, not pinned to
    a commit.
12. **Stale text to clean up:** `BLOCKERS.md` "Open technical questions" still says the projector shaders and
    `PS_Terrain3Layer` are not drawn (done by M7b); the `UnRenDev.h` comment lists `USubActionDepthOfField.cpp`
    among the gate users (that file has no gate); the comment on `UseXboxProjectors`'s compiled default says
    projectors on skeletal meshes "were drawn with nothing" (the notebook's pixel trace shows PC draws them).
    *(Superseded (2026-10-09/10): see section 60, 20, open issue 12.)*
13. **Smaller deviations kept on purpose:** occlusion queries always "visible" (no occlusion culling); NV2A 8-bit
    combiner precision not modelled (no banding); `DistortionCullDistance` read only from the `UC2GLDrv` section;
    the emissive streak falloff is per frame (frame-rate dependent, as Epic's); no frame cap or
    `requestAnimationFrame` pacing on the web.
    *(Superseded (2026-10-09/10): see section 60, R-11 and R-20.)*

## Places where we changed Epic's shared code (renderer)

Epic's `D3DDrv` itself is not changed and not built. Everything below keeps Epic's PC branch; `_XBOX` is never
defined; the Xbox branch runs only for a device that reports the named capability.

| File | Change | Why | Feature |
|---|---|---|---|
| `Engine/Inc/UnRenDev.h` | `FRenderCaps::XboxPostProcess`, `ShippedDOFBlurAlpha`, `XboxShadows`, `XboxProjectors`, `PostProcessReferenceHeight` (+ constructor defaults); declarations of `RenDevHasXboxPostProcess/Shadows/Projectors`, `RenDevPostProcessReferenceHeight` | the capabilities behind which Xbox paths run | R-7, R-10, R-13..R-15 |
| `Engine/Src/UnRenderResource.cpp:48-100` | the four capability functions | one test per capability for engine and game code | same |
| `Engine/Src/UnRender.cpp:3256-3262` | `FPostProcessRender::Render`: `_XBOX` body runs with the capability | post-processing | R-7 |
| `Engine/Classes/PSDistortionBaseMaterial.uc` (cpptext), `Engine/Inc/EngineClasses.h:7678-7686` | `IsPostProcess`/`RequiresSorting` declared, defined in `UnPixelShaderMaterial.cpp` | distortion materials enter the post-process list | R-7 |
| `Engine/Src/UnPixelShaderMaterial.cpp` | six `PreRender` gates (`#ifdef _XBOX` to a run-time test) + the two definitions (:408-421) | distortion, back-buffer effect, player cam, colour remap | R-7 |
| `Engine/Src/UnRenderUtil.cpp:1462-1476` | `FScreenPrimUtil::DrawTile`: triangle fan, one tile per draw | **Epic bug**: a quarter of every tile was missing (only Xbox paths call it) | R-7 |
| `Engine/Inc/UnRenderUtil.h:676-692`, `Engine/Src/UnRenderUtil.cpp:2363, 2469-2484` | `FPostProcessReference`/`GetPostProcessReference`; `FilterCopyRenderTarget` back-buffer taps in reference pixels | O-10 | R-13 |
| `Engine/Src/UnShadowProjector.cpp:242-271, 611-761` | caster colour and shadow filter choose the Xbox branch at run time | Xbox shadow texture | R-15 |
| `Engine/Src/UnGame.cpp:2681-2715` | `UGameEngine::LoadMap` static-projector batching runs with the capability | Xbox projector batching | R-14 |
| `UCGameplay/Src/UCCameraEffects.cpp` | 16 early-return gates and 4 whole-body gates made run-time; `UUCScreenDOF` shared resources (`InitSharedDOFResources`); the DOF alpha switch (:343-352); O-10 target sizes (DOF, bloom, emissive), radial-blur insets, `UCPP_TargetSize` (**Epic bug**: INT vs FLOAT size compare re-created targets every frame at sizes like 1366x768) | post-processing, O-3, O-10 | R-7, R-10, R-13 |
| `UCGameplay/Inc/UCCameraEffects.h` | `InitSharedDOFResources` declared; `FirstPersonAlpha`/`ThirdPersonAlpha` added (M3 layout fix of Epic's stale header, `44f81c6`; M13 packing) | layout; makes O-3's choice possible | R-10 |

Related shared-code changes owned by other sections: `UnGame.cpp` movie hooks and the `ResetRenderStates` guard
(:1865-1870; video), `UnMovie.cpp` (video; the "never green" fallback of R-17), `UnRender.cpp`/`UnCanvas.cpp`/
`UnRenderUtil.h` UI box and AutoFOV changes (D-2, D-8), `UnRenDev.h`'s GCC fix for `BEGIN_D3D_EVENT` (build),
and compile/64-bit fixes in render files with no intended change of picture (`UnRenderBatch.cpp`,
`UnRenderStaticMesh.cpp`, `UnRenderVisibility.cpp`: `FloatToInt` in C off 32-bit x86, `FColor(FPlane)` spelled for
GCC, an occlusion-query pool instance; `UnMaterial.cpp`: `PTRINT` casts of the movie texture and thread handles;
`UnTex.cpp`: two format-string fixes; `UnModelLight.cpp`: the nvDXT path only under MSVC, for MinGW).
Our own engine-side test file `Engine/Src/UC2NetTest.cpp` (`NETPLACE`, used by the renderer checks) is not
Epic's code.


---

# 30. Input, UI, scripts, profiles and saves

Code as of main `4813dd9` (snapshot `/home/ion/WorkUC2/engine-uc2-report`), baseline `7633696` (Epic's tree as
imported). Frontend: `ufrontend` branch `uc2` at `4ac0d6d` against `7fb637e`. `path:line` are snapshot line numbers.
Notebooks behind this section: `docs/porting/analysis/` `client-boot.md` (M4), `controls.md` (M10), `scripts.md`
(M14), `ui-scale.md` (D-2), `profiles.md` (D-3/D-4/D-5), `mouse.md` (D-6), `console-fov.md` (D-7/D-8),
`saveconfig.md`, `rebind.md` (D-9), `web-ini-saves.md` (C-2).

## Summary: the five things to know

1. **Input is ours from SDL up, Epic's from `UEngine::InputEvent` on.** `Source/SDL2Drv` (new, C++ only, no `.u`)
   replaces WinDrv/XboxDrv: keyboard by scancode, relative mouse, text through `SDL_TEXTINPUT`, and game pads
   through SDL's GameController API reproducing the Xbox driver (four ports, hot-plug, 30/255 threshold, repeat,
   rumble). Everything after that (`UInput` bindings, `UCPlayerInput`, the GUI) is Epic's code with targeted
   fixes. Pads are **off on the web** (`UseJoystick=False`) and **no physical pad has ever been tried**: every pad
   result is from SDL's virtual controller.
2. **Three script packages are rebuilt from source and ship in the asset tree instead of Epic's:** `GUI.u` (D-4
   player name), `UCGameplay.u` (D-6 mouse look, saveconfig's persistent mouse commands) and `UCMenu.u` (D-9
   KEYBOARD / MOUSE page). Nine `.uc` files differ from Epic's; seven of them are this area's. Everything else in this
   section is native C++ or ini data and works with the shipped packages. **The mouse look is the one feature that
   breaks if a package is missing**: the D-6 `DefUser.ini` binds the mouse to `aTurn/aLookUp`, which the shipped
   `UCPlayerInput` overwrites, so with the shipped `UCGameplay.u` the mouse does not turn the view at all (the
   engine logs a warning saying so). The table below lists every feature against every layer.
   *(Superseded (2026-10-09/10): see section 60, 30.5 and 30.18.)*
3. **The mouse is a PC mouse now, the pad is the Xbox pad.** Mouse: linear, 0.022 deg x `MouseLookSensitivity`
   (2.5) per count, no clamp/curve/acceleration/friction/adhesion, own invert, rigid third-person camera while the
   mouse owns the look. Pad: Epic's Xbox curve, acceleration, dead zone 0.3, friction, adhesion, auto-centre
   (within 0.5% of the shipped scripts). Measured by script, never felt by a person; the rigid camera turned out to
   be the cause of the owner's "character jitter" report (O-11), fixed after this snapshot (section 50).
4. **The UI is drawn in a centred 4:3 box** (`UIScaleMode=Fit43`, default): one native change in
   `UCanvas::Update()` hands every script and the GUI a 640x480 design space; the world (and, with D-8 AutoFOV, a
   Hor+ field of view) fills the window. No script change.
5. **Settings now persist the PC way**: the game starts in the last profile (`User.ini [Profiles] LastProfile`,
   engine code), the profile owns the name, keyboard bindings and mouse settings live in `User.ini` and are written
   key by key (`SAVECONFIGKEYS`, behind `[Engine.Engine] ScriptSaveConfig`, which turns Epic's compiled-out script
   `SaveConfig()` back on), on the web the inis and a flat `System/Profiles/<name>.PRO` are the player's files.

## Which layer carries what (read this before reviewing the `.u` switch)

"Shipped .u" = what the player gets with our engine and inis but Epic's shipped script packages (the state before
asset tree v2). Every feature not listed with a script part works with the shipped packages.

| Feature | Native (C++) | Script (.uc, package) | Ini / data | Frontend | With the shipped .u |
|---|---|---|---|---|---|
| Keyboard, mouse buttons, text (M4) | SDL2Drv | none | `[SDL2Drv.SDL2Client]` | none | full |
| Pads (M10) | SDL2Drv `SDL2Pads.cpp`, `UnIn.cpp` stick fix | none | `[SDL2Drv.PadMap]`, `Pad*` keys, `Joy*` bindings | none (pads off on web) | full, but the left stick does not walk on PC (measured 0 units; M10 hid it with `SpeedBase=2400000`, D-6 removed that) |
| Menus by keyboard/mouse (M10) | `UnGUICore.cpp` | none | `[GUI.EmulatedJoypad]`, `bHideMouseCursor` | none | full |
| Mouse look separated (D-6) | `UC2InputMigrate.cpp` (migration, warning), SDL2Drv | `UCPlayerInput`, `UCPlayerController`, `UCTrainingController` in **UCGameplay.u** | `DefUser.ini` mouse on `aTurn/aLookUp`, new `[UCGameplay.UCPlayerInput]` keys | boot spec test | **no mouse look at all** (warning logged); pad as Epic's |
| Keys as a stick, lock-on by mouse, pad-only adhesion/auto-centre (D-6) | none | **UCGameplay.u** | `bKeysAsStick`, `MouseLockBreakAngle` | none | absent (Epic's behaviour) |
| UI box (D-2) | `UnCanvas.cpp`, GUI, controller, interaction master | none | `UIScaleMode`, `UIMenuBackground`, `UISharpText` | play test maps clicks into the box | full |
| Start in last profile, deletion, saves (D-3, C-2) | `UC2Profiles.cpp`, `FFileManagerGeneric.h`, `ULoadSaveManager.cpp` (web) | none | `User.ini [Profiles] LastProfile` | `userFiles`, manifest excludes | full |
| Profile names the player (D-4) | `UnGame.cpp` LoadMap / MatchViewportsToActors | `GUIController.SetProfilePlayerName` in **GUI.u** | `[DefaultPlayer] Name` | `playerName.ownedBy` | with a profile: correct; **without one the character's name ("Anubis") replaces the launcher's** |
| Real keyboard on text pages (D-5) | `UnGUICore.cpp` | none | `[GUI.EmulatedJoypad]` `/Type` codes | none | full |
| Console key (D-7) | `UnGame.cpp`, `UnInteractionMaster.cpp` | none | `ConsoleKey=192` | none | full |
| AutoFOV (D-8) | `UnRender.cpp`, `UnGame.cpp`, `UnCanvas.cpp`, `UnController.cpp` | none | `[ufront.AutoFOV]` | none | full |
| Script SaveConfig (saveconfig) | `UnCorSc.cpp`, `UnObj.cpp`, `UnClass.cpp`, `UnMisc.cpp` | `MouseLookSens`/`MouseLookInvert` call `SAVECONFIGKEYS` (**UCGameplay.u**) | `ScriptSaveConfig=True` | none | engine part works (e.g. `SetWeaponHand` persists); no mouse commands to persist |
| Rebinding page (D-9) | `UnGUICore.cpp` raw capture, `SAVECONFIGKEYS` | `UC2Settings_Keys`, `KeyBindingPanel`, `UC2Settings_Main` in **UCMenu.u** | DefUser.ini as "defaults" | keys spec | **no KEYBOARD / MOUSE button**; keys only by editing User.ini or console `SET Input` + `SAVECONFIGKEYS` |
| Script compiler (M14) | Editor package, Core fixes | none changed by M14 itself | none | none | n/a (tooling) |

> Superseded (2026-10-09/10): see section 60, 30.5, 30.7, 30.8.

Package couplings that are not obvious:
- `UCGameplay.u` and `DefUser.ini` must change together (D-6). A kept User.ini from an earlier build is rewritten
  at start-up by `UC2InputMigrate.cpp` only where a value is exactly an old default.
  *(Superseded (2026-10-09/10): see section 60, 30.7.)*
- `UCMenu.u` (D-9) refers to `UCPlayerInput.MouseLookSensitivity`/`bInvertMouseLook` (`UC2Settings_Keys.uc:546-580`),
  which exist only in the rebuilt `UCGameplay.u`. The rebuilt `UCMenu.u` with the shipped `UCGameplay.u` was never
  tried; expect unresolved imports at load. Treat the three rebuilt packages as one set.
- `UCGameplay.u` is a server package (`ServerPackages=UCGameplay`): every peer of a network game must run the same
  file; GUI and UCMenu are client-only.

## Features

### 30.1 SDL2Drv: keyboard, mouse and typed text (M4)

**What the player/owner sees.** Keys, mouse buttons, the wheel and typed characters reach the game on Linux,
Windows and in the browser through one SDL2 driver; Alt+Enter toggles fullscreen; the mouse is captured
(hidden, relative) in a match and free in a windowed menu.

**Where.**
- New code (no Epic file changed for this feature): `Source/SDL2Drv/` (`Inc/SDL2Drv.h`, `Src/SDL2Client.cpp`,
  `Src/SDL2Viewport.cpp`, `Src/SDL2Drv.cpp`), classes `SDL2Drv.SDL2Client`/`SDL2Viewport`, C++ only like WinDrv.
  Commits `49189ba` SDL2Drv: SDL2 client and viewport (keyboard, mouse, text; pad hook for M10), `1700520`
  (OpenGL window/context for GL devices).
  - `SDL2Viewport.cpp:41-` `InitKeyMap`: SDL scancode (physical key) -> the Windows virtual-key code the PC build
    got (`EInputKey`); both Shift/Ctrl/Alt -> `IK_Shift/IK_Ctrl/IK_Alt` (UC2's bindings and GUI test those);
    `SDL_SCANCODE_GRAVE` -> `IK_Tilde` (`:62`, the console key of D-7).
  - `SDL2Viewport.cpp:791-933` `HandleEvent` (the WndProc): key down/up -> `InputEvent(IST_Press/IST_Release)`
    with auto-repeat as further presses (`:829-861`); Backspace/Tab/Escape also as characters, never Enter, as
    `WM_CHAR` does (`:843-845`); `SDL_TEXTINPUT` UTF-8 decoded, only code points 32..255 sent as `Engine->Key`
    (`:862-872`); `SDL_MOUSEMOTION` -> `IK_MouseX = +xrel*MouseXMultiplier`, `IK_MouseY = -yrel*MouseYMultiplier`
    as DirectInput did, absolute position kept only while not captured (`:873-888`); buttons 1..5 (`:889-915`);
    wheel -> `IK_MouseW` 120 per notch plus `IK_MouseWheelUp/Down` press+release (`:916-930`).
  - `SDL2Viewport.cpp:938-969` focus change: re-capture, `ResetInput` of GUI and `UInput`, gamma.
    `:975-1000` `SetMouseCapture` = `SDL_SetRelativeMouseMode` + `SDL_SetWindowGrab`. `:1005-1018`
    `UpdateMousePosition` (the GUI's absolute cursor in a window, `SDL_GetMouseState`). `:1025-1066`
    `UpdateInput`: pumps events, then releases every key the engine holds that SDL says is up (Epic's
    `GetKeyState` resync).
  - `SDL2Client.cpp:103-140` `Init`: SDL video, pads, the test channel, `SDL_StartTextInput()`; `:70-93` compiled
    defaults (the WinDrv section's values, so Epic's own Default.ini still gives a usable window).
- Ini: `Default.ini:147-` `[SDL2Drv.SDL2Client]` (a copy of `[WinDrv.WindowsClient]`; `CaptureMouse=True` `:158`);
  `ViewportManager=SDL2Drv.SDL2Client` in `[Engine.Engine]`. `MouseXMultiplier/MouseYMultiplier` are not in the
  ini (compiled 1.0). Web inis identical for these keys.
- Web: the same driver through Emscripten's SDL2 port. Pointer lock is the frontend's existing shared
  `packages/frontend/src/shell/rawPointerLock.ts` (in the base `7fb637e`, not ours): raw deltas where the browser
  allows `unadjustedMovement` (Windows, macOS), the desktop's accelerated deltas on Linux browsers.
- Tools: every scenario of `tools/dev/m10-controls.sh` drives this path with real X events (XTEST, from
  `tools/dev/m10-inject.py`).

**How the parts fit.** Native only: SDL event -> `USDL2Viewport::HandleEvent` -> `UEngine::InputEvent/Key` ->
`UInteractionMaster` (console first, then the GUI controller, then the viewport's interactions) -> `UInput`
bindings -> script. No script or ini part is needed; without `[SDL2Drv.SDL2Client]` the compiled defaults apply.

**Why.** UC2's PC tree has only WinDrv (Windows) and an SDL 1.2 Linux driver that predates UC2's `UClient`
interface (`client-boot.md` step 4); engine-ut2004's SDL2 driver has UT2004's signatures, so it was a source of
SDL calls and the scancode table, not a drop-in.

**Decisions and alternatives.** Keep WinDrv's contract exactly (one code for both Shift keys, `WM_CHAR`
semantics, relative mouse at all times, absolute cursor read on demand) so that Epic's GUI and bindings see what
they saw on Windows; rejected: engine-ut2004's left/right modifier codes (UC2's bindings use `IK_Shift`).

**Disadvantages, risks, deviations.**
- Typing is Latin-1 only (TCHAR is ANSI by rule); characters above U+00FF are dropped silently.
- Ctrl+letter does not produce the control characters `WM_CHAR` gives on Windows (`client-boot.md`); no IME.
- No SDL mouse hint is set (`grep SDL_SetHint` finds none): whether the desktop's pointer acceleration reaches
  native mouse look is SDL's backend choice; not measured natively. In a Linux browser it does apply (frontend
  note above), so the browser's mouse feel depends on the desktop's mouse settings.
- Fullscreen tested only under Xvfb.

**Status.** Measured: M4 gate (XTEST keys reached the GUI, `LOG.md` 2026-10-04); `m10-controls.sh keyboard` 16
checks and `mouse` 18 checks PASS on every later gate (latest `logs/d9/final-m10.txt`, coordinator's
`logs/coord/a12`); browser `uc2-wasm-boot.spec.js` drives real keyboard and mouse (4/4 in the D-9 and E-2 gates).
A real Windows desktop (not wine) and a real browser on the owner's desktop: not verified by us.

**Off switch.** None (it is the client). `MouseXMultiplier/MouseYMultiplier` in `[SDL2Drv.SDL2Client]` scale raw
mouse counts.

### 30.2 Game pads through SDL's GameController API (M10)

**What the player/owner sees.** An XInput/PlayStation-style pad drives menus and matches as the Xbox pad did:
hot-plug (unplugging pauses with the "controller removed" box), analog triggers with the Xbox threshold, held
buttons repeating, rumble from weapons and hits. Natively only; the web build has pads switched off.

**Where.**
- New code: `Source/SDL2Drv/Src/SDL2Pads.cpp` (602 lines; commit `e254487` SDL2Drv: game pads through SDL's
  GameController API, after XboxDrv; test channel). The specification is Epic's Xbox driver (header `:1-20` cites
  `XboxViewport.cpp`/`XboxClient.cpp` line by line):
  - `:43` `DefaultPadMap` and `:90-126` `LoadPadMap`: SDL control -> Xbox control (`Joy1..16`, `JoyX/Y/U/V`,
    optional `Invert`); compiled default = the ini table, used for any control the section does not name.
  - `:138-165` `InitPads` (`UseJoystick`, `SDL_INIT_GAMECONTROLLER`, `-inputlog`), `:230-311`
    `PadAdded/PadRemoved` (first free of four ports; `IK_JoySlider1/2` press+release to the pad's viewport or the
    interaction master; on removal the held buttons are released first: a deliberate deviation from the Xbox,
    which left them down until a reset).
  - `:312-413` `ReadPad`, `SendButtonEdges`: 16 buttons scaled to 0..255, pressed above `PadButtonThreshold`
    (30 = `XINPUT_GAMEPAD_MAX_CROSSTALK`), repeat as `IST_Hold` after 0.2 s then every 0.1 s.
  - `:414-456` `UpdateBoundPad` (called from `SDL2Viewport.cpp:1037-1038` only from the player tick, where
    `DeltaSeconds>0`): `Engine->ActiveControllerId` = pad, buttons through `InputEvent`, sticks through
    `MasterProcessJoyInput` (GUI) then `Input->DirectAxis`. `:457-481` unbound pads -> interaction master only.
  - `:482-516` rumble: `WaveformManager->GetRumbleState` -> `SDL_GameControllerRumble(low=left, high=right,
    1000 ms)`, sent on change, refreshed while non-zero; `:588-600` zeroed on travel (`SetClientTravel`).
  - `:530-560` console `SETDEADZONE` (Epic's, stores `JoyDeadZone`, which nothing reads) and `PADS`.
  - `:570-586` `GetAnalogButtonAmount`: the bound pad's analog value, or 255 while a keyboard/mouse key bound to
    the same command is held. Epic's `UPlayer` returned 255 always on PC, so `UCPlayerInput`'s timed input
    sequences saw triggers/A/X held permanently (Epic's PC defect, fixed here in our driver).
- Epic's code changed: `Source/Engine/Src/UnIn.cpp:316-325` (`UInput::Exec`, the `AXIS ... SPEEDBASE=` branch;
  commit `de94c55` Engine: stick axes mean on PC what they mean on the Xbox). **Bug fix, non-Xbox only.**
  `UInput::ReadInput` scales every axis by `20/DeltaSeconds` on PC (1 on the Xbox); right for mouse/key deltas,
  wrong for a stick, which is a position: the stick was 20/dt times its Xbox strength (1,200x at 60 fps) and
  frame-rate dependent. Now an `IST_Hold` stick contribution is pre-divided by `20/dt`. No keyboard or mouse
  binding uses `SpeedBase`, so their numbers are unchanged (`controls.md` step 8 shows the arithmetic).
- Ini: `Default.ini:157` `UseJoystick=True` (WinDrv default False), `:188-197` `PadButtonThreshold=30`,
  `PadStickDeadZone=0`, `PadRepeatDelay=0.2`, `PadRepeatRate=0.1`, `PadRumble=True`, `PadRumbleScale=1`,
  `JoyDeadZone=0.35`, `LogInput=False`; `:202-221` new section `[SDL2Drv.PadMap]` (`a=Joy1` ... `rightshoulder=Joy5`
  (Black), `leftshoulder=Joy6` (White), `lefty=JoyY Invert`, `righty=JoyV Invert`). Black = right shoulder,
  White = left is a convention (the script names Black "Next"; xemu maps them the same way), not an Xbox fact.
  Bindings: `DefUser.ini:178-202` `Joy1..Joy16`, `JoyX/Y/U/V`, `JoySlider1/2` = `KeyBinds.ini` Layout_0 (30.6).
  **Web: `UseJoystick=False`** (`Engine/Config/web/Default.ini`, put there by `tools/dev/sync-web-ini.py:22`,
  "game pads through the browser's Gamepad API are untested").
- Tools: `tools/dev/m10-controls.sh pad` (33 checks) and `both` (14 checks) with SDL's virtual controller.

**How the parts fit.** The native side turns SDL controller state into the exact key events the Xbox driver
produced; the ini decides which SDL control is which Xbox button and how sticks are bound; the profile (script,
unchanged) then applies its controller layout by rewriting the `Joy*` bindings. No script change. Without
`[SDL2Drv.PadMap]` the compiled table applies. Without the `UnIn.cpp` fix the right stick saturates at any
deflection and turns at a frame-rate dependent speed.

**Why.** The owner wants the pad as on the Xbox (PLAN.md D-6 "the pad keeps its Xbox response curve"); WinDrv's
DirectInput joystick (off by default) has none of the Xbox behaviour.

**Decisions and alternatives.** Reproduce XboxDrv rather than port WinDrv's joystick; keep the dead zone in the
bindings (`DeadZone=0.3`) as the Xbox did (`PadStickDeadZone=0`); fix the stick scaling in `UInput` (one branch,
non-Xbox) instead of retuning every value as UT2004 did ("amb" fix). M10 first compensated the left stick with
`JoyX/JoyY SpeedBase=2400000`; D-6 replaced that by a script fix (30.5) because a profile's "swap sticks"
rewrites `SpeedBase=100`.

**Disadvantages, risks, deviations.**
- **No physical pad has ever been connected** (BLOCKERS.md "M10, pads"): SDL's own mapping of real devices, real
  rumble motors, `[SDL2Drv.PadMap]` overrides, `PadStickDeadZone>0`, `PadRumble=False`, rumble zeroed on travel,
  key repeat assertions and `GetAnalogButtonAmount` were never exercised (`controls.md` "Implemented, never
  exercised").
- No split screen: Start on a second, unbound pad does not join (`SDL2Pads.cpp:16-20`; E-1, owner decision).
- Pads off on the web.
- The pad's slow turns lose up to one rotation unit per frame (Epic's integer rotation): just past the dead zone
  nothing turns below ~0.335 deflection at 60 fps, ~0.352 at 144 fps, where the Xbox at 30 fps turned from ~0.32
  (`mouse.md` "Not done"). Fixing it changes Epic's pad arithmetic: owner decision, not done.

**Status.** Measured with the virtual pad: `m10-controls.sh pad` 33 PASS, `both` 14 PASS (every gate since M10;
latest D-9 `logs/d9/final-m10.txt`, coordinator `logs/coord/a12`); trigger 29/255 no, 33/255 yes; rumble request
reaches the virtual device; pad curves vs the shipped scripts within 0.47% (`d6-mouse.sh pad`). Real pad: not
verified (planned from CI builds, E-5).

**Off switch.** `[SDL2Drv.SDL2Client] UseJoystick=False`; `PadRumble=False`.

### 30.3 The input test channel (`-inputtest=<file>`) and the probes

**What the player/owner sees.** Nothing in a normal game. It is how every input, UI and settings claim in this
section was measured.

**Where.**
- New code: `Source/SDL2Drv/Src/SDL2InputTest.h` (command list `:1-30`), `SDL2InputTest.cpp` (558 lines); started
  in `SDL2Client.cpp:122-123` only with `-inputtest=` **and** pads initialised (so not on the web, where
  `UseJoystick=False`). Commits `e254487` (M10: `pad attach/detach/button/axis`, `probe`, `exec`, `log`; a virtual
  controller via `SDL_JoystickAttachVirtualEx` at `:249`), `43dc4fd` (works on the Windows CRT, under wine),
  `792407e` (D-6: `mouse <dx> <dy>` pushes a real `SDL_MOUSEMOTION` `:361`, `watch`, `place`, `aim`,
  `probe2/probe3` `:489-524`), `84c3082` (D-9: `keys`, `probe4` `:515`).
- Log name `SDL2Input`, quiet unless `LogInput=True`, `-inputlog` or the channel (`SDL2Pads.cpp:133-148`); made
  `RF_Native` (`SDL2Client.cpp:110-113`) because the name purge otherwise handed its index to another name.
- `Source/SDLLaunch` `-maxfps=<n>` (`1efa52e`, D-6): a frame cap for frame-rate-dependent measurements.
- Tools: `tools/dev/m10-controls.sh` (one capped Xvfb run per scenario; `M10_RES`, `M10_UIMAP`, `M10_XVFB`,
  `M10_INI`, `M10_KEEP`, `M10_SLOW`, `M10_KEYHOLD` were added by later tasks), `tools/dev/m10-inject.py` (XTEST
  keys/mouse, channel lines, log waits, probe parsing), `tools/dev/d6-mouse.sh` + `d6-checks.py`,
  `tools/dev/d9-rebind.sh`, `tools/dev/d2-ui-scale.sh`, `tools/dev/d78.sh`, `tools/dev/d3-profiles.sh`,
  `d4-names.sh`, `d5-text.sh`, `sc-saveconfig.sh`.

**How the parts fit.** Native test hook only; the scenarios combine real X input (XTEST) with channel commands
and read the log. Nothing in script.

**Why.** No physical pad, no `xdotool` at the time, and no person to play: the evidence had to be log lines.

**Decisions and alternatives.** Drive the real path from SDL on (a virtual SDL controller, a pushed SDL motion
event, real X key events) rather than calling engine functions directly; rejected: a kernel-level virtual pad
through `/dev/uinput` (it would appear on the owner's whole desktop session while a test runs, BLOCKERS.md).

**Disadvantages, risks.** The tests are timing-sensitive under software GL on a loaded machine: UC2's menu
animations count frames, so fixed waits failed at load 24-50 and had to be replaced by waits on log lines
(`7eaaa7d`, `628038a`, `mouse.md` steps 8-10; `console-fov.md` step 7 needed `M10_SLOW/M10_KEYHOLD`). A click
shorter than one frame can be lost (`controls.md` coordinator note: inferred from pass rates, not traced); a test
file name containing `-server` starts a dedicated server (`ParseParam` matches it, `profiles.md` step 6).

**Status.** In use by every gate since M10.

**Off switch.** It is off unless `-inputtest=` is given.

### 30.4 The menus by keyboard and mouse (M10; the cursor mapping from D-2)

**What the player/owner sees.** Arrows or WASD move through the menus, Enter/Space accept, Escape/right click go
back, Tab is Start, F1 is Back, PageUp/PageDown are the triggers, the wheel scrolls lists; hovering over an item
selects it, a left click acts on the item (or list row) under the cursor; in fullscreen the game draws its own
cursor. On a page with a text field the letter keys type instead (30.12).

**Where.**
- Epic's code changed, `Source/GUI/Src/UnGUICore.cpp` (commit `69b31bc` GUI: menu key table as ini data
  ([GUI.EmulatedJoypad]); mouse click/hover; typing into on-screen keyboards):
  - `:860-1036` new: `[GUI.EmulatedJoypad]` parser (`LoadEmulatedPad` `:931-980`, read once, on the first menu
    key). **Behaviour change**: Epic's hard-wired PC table (letters A B X Y K W, `[` `]`, `;` `'`, Enter = Start,
    Escape = Back) becomes data; Epic's table is kept verbatim as the fallback when the section is missing
    (`:944-958`), so an Epic or community ini behaves as before.
  - `:1046-` `GetXCode` and `:1218-1260` the emulation in `NativeKeyEvent` read the table; a second code applies
    while the active page has a writable text field (`TextEntryTarget` `:1002-1005`).
  - `:2543-2551` hover: the item under the cursor takes the focus when the mouse really moved (Epic's hover code
    is `#if 0`); `:2556-2604` `MouseClickPress/Release` (called from `:1467`, `:1482`): a press on a list selects
    the row under the cursor (its own `InternalOnClick`), a release over the same item that has no `OnClick` but
    an `XControllerEvent` handler sends `XC_A`. DevUI log lines `Mouse hover selects ...`, `Mouse click is A on ...`.
  - D-2 additions (`14d9738`, `66445f6`): cursor and hit-testing in the UI's design pixels (`:675`, `:706`,
    `:1288-1296` software-cursor deltas divided by the UI scale, `:1767-1776` the cursor may leave the 4:3 box,
    `:1814`, `:1844`).
- Epic's code changed, `Source/Core/Inc/UnCoreNative.h:27` (commit `75e4258` Core: static native lookup ignores
  case). **Bug fix in shared Core**, found by this feature: with `Delete=...` in the system ini, `uc2.bin` and
  `ucc-bin` stopped at start-up (`Can't find 'intUObjectexecDelete'`): the ini is read before `Core.u`, its keys
  become FNames, an FName keeps the first spelling, and the static-link native table compared case-sensitively.
  Now `appStricmp`; all 788 native names are distinct without case (checked).
- Ini: `Default.ini:531-566` `[GUI.EmulatedJoypad]` (table below; the comment block above it documents the
  syntax), `:519` `bHideMouseCursor=False` (Epic: True; fullscreen has no system cursor and the GUI draws its own
  only when this is False), `:517` `bEmulatedJoypad=True` and `:521` `MenuMouseSens=1.0` (Epic's). Web identical.

| Keys | Pad code (`/` on a text page) | Why |
|---|---|---|
| arrows; W A S D | D-pad; D-pad / Type | WASD like the game's movement keys, but they type on text pages |
| Enter; Space | A / Start; A / Type | Start = "done" on a keyboard page |
| Escape, right mouse; Backspace | B; B / Type | back (cancels text entry too) |
| X, Y; Q, E | X, Y / Type; White, Black / Type | the letters the glyphs show; Q/E as the shoulders |
| PageUp/PageDown; `[` `]` | triggers; triggers / Type | `[` `]` are Epic's |
| Tab; F1 | Start; Back | 15 menu files act on `XC_Start` apart from A; `XC_Back` = scoreboard in game |
| Home, End; Delete | stick clicks / Type; Raw / Type | `XC_RightThumb` is used in 7 menu files |
| NumPad 8 2 4 6; wheel | right stick; D-pad up/down | Epic's; wheel scrolls by moving the selection |

**How the parts fit.** UC2's pages (script, unchanged) act on pad codes (`XC_A`, `XC_Start` ...), not on keys or
`OnClick`. The native side turns keys into those codes from the ini table and turns clicks into `XC_A` where
Epic's `OnClick` path would do nothing. Without the ini section: Epic's letter table (W and A navigate even
while typing; no WASD). Without `bEmulatedJoypad`: raw keys reach pages that ignore them.

**Why.** Epic's PC table collided with WASD and with typing; many UC2 items have no `OnClick`, so a click did
nothing, and a click on a list accepted the previously selected row (`controls.md` step 3).

**Decisions and alternatives.** Data instead of code (an owner can remap menu keys); a click is A only where
Epic's click does nothing; hover selects only on real motion, so a resting cursor never steals the focus from
keys or pad. Not done (needed script, listed for M14 and still open): button glyphs that show the bound key
instead of the Xbox A/B (`GUIController.XBoxButtonChars`, `Footer.uc`), hover on list rows (`GUIVertList.uc`).

**Disadvantages, risks, deviations.** Hints still say "A SELECT" etc.; the player has to know Enter = A. The
`Tab=Start` row was dead until D-7 (the console sat on Tab). The `UnCoreNative.h` change touches every native
lookup of the static build (bind time only).

**Status.** Measured: `m10-controls.sh mouse` 18 checks (hover then Enter opens the hovered page; click opens the
item under the cursor, not the focused one; right click answers B; a click on a list row selects and accepts it;
wheel moves a list both ways), at 640x480 and, mapped into the D-2 box, at 1920x1080 (`d2-ui-scale.sh`); D-2
browser spec: hover and click hit at 1920x1080. Epic's fallback table (no section): not exercised.

**Off switch.** Delete `[GUI.EmulatedJoypad]` (Epic's table); `bHideMouseCursor=True` (Epic's cursor rule).

### 30.5 Mouse look separated from the pad (D-6): "the mouse support"

**What the player/owner sees.** With the mouse: one count turns 0.055 degrees at the default field of view
(0.022 x `MouseLookSensitivity` 2.5), whatever the speed (200 counts in one frame turn ten times what 20 do),
the same up/down as sideways (a circle stays a circle), the view stops the moment the mouse stops, the
third-person camera no longer slides after a turn, no aim assist pulls the mouse, walking no longer re-centres
the pitch, and the mouse has its own invert. With the pad: the Xbox's response (squared curve, acceleration to
2.5x after 0.5 s, dead zone 0.3, friction, adhesion, auto-centre). Both can be used in the same session; the
device that moved last "owns" the look.

**The owner's complaints, and what was measured as their cause** (`mouse.md` steps 0, 3, 6; evidence runs
`before` = native with M10's ini, `epic` = Epic's PC `DefUser.ini`, which the web build carried until `c66d275`,
2026-10-07, i.e. what the owner played in the browser on 2026-10-06):

| Owner's words | Cause | Before (measured) | After (measured) |
|---|---|---|---|
| "mapped to the gamepad joystick ... if you exceed the gamepad movement speed, you get capped" | Epic's PC binding `MouseX=... Axis aMouseX Speed=6.0` feeds the **stick's** variable; `UCPlayerInput` clamps it to `aMouseX_Limit=100` per tick, then a turn rate | web: 20 counts in one tick turn 441 units, 200 counts 440 | 10x the counts = 10x the turn |
| "extremely sensitive ... a tiny bit of movement will make the camera jerk" | same path + Speed 6; natively M10's linear tuning at 52.4 units/count; and the profile's pad sliders (0.8) applied to the mouse | 1.23 deg/count (web), 0.29 deg/count (native) | 0.055 deg/count |
| "the camera itself slides to the direction of the movement" | the third-person camera is lazy: `AUCPlayerController::execCalcThirdPersonView` (`UCGameplay.cpp:289`, `MoveVectorTo` `:565`) moves toward its place behind the pawn at `CameraInterpSpeed`; a mouse turn swings that place in one frame | 23.8 units of slide after a circle, 63.9 after a flick (native) | 0.0 units |
| "feels hexagonal if I try to do a circle" | per-axis clamp + squared fall-off of the stick path, applied separately to X and Y | circle 12,293 x 11,212 units, 16.6% out of round (web) | 0.29% |
| "keyboard bindings being backwards" | no example given; audit in 30.8 found nothing inverted | | O-14 still open |

**Where.**
- Native (C++):
  - `Source/SDL2Drv/Src/SDL2Viewport.cpp:873-888`: relative mouse counts as `IK_MouseX/IK_MouseY` (since M4;
    D-6 did not change it). `Source/Engine/Src/UnIn.cpp` (Epic's, not changed by D-6): a count adds `0.01 x Speed`
    to the axis the binding names; `ReadInput` multiplies by `20/dt`.
  - `Source/Engine/Src/UC2InputMigrate.cpp` (new) called at `UnGame.cpp:188`: kept User.ini values moved (30.7)
    and the start-up warning when the mouse is bound to `aTurn/aLookUp` but the loaded `UCGameplay.u` has no
    `MouseLookSensitivity` (`:111-117`, commit `6857174`).
  - Test channel `mouse`/`watch`/`place`/`aim` and `probe2/probe3` (30.3, `792407e`); SDLLaunch `-maxfps` (`1efa52e`).
    *(Superseded (2026-10-09/10): see section 60, 30.5.)*
- Script, all in the rebuilt **`UCGameplay.u`** (commits `49d4705` UCGameplay: D-6 mouse look separated from the
  pad, `0475725` camera at its place at once, exact FOV scale, `d75990b`, `e3e927d` console commands persist):
  - `UCGameplay/Classes/UCPlayerInput.uc:84-96` new properties (`MouseLookSensitivity`, `bInvertMouseLook`,
    `MouseLockBreakAngle`, `bKeysAsStick` globalconfig; `bMouseOwnsLook`, `MouseYaw/MousePitch`, carries, push).
  - `:333-345` in Epic's `PlayerInput`: what the mouse and the look keys put in `aTurn/aLookUp` this tick is saved
    BEFORE Epic's stick code overwrites those two variables; the look owner is updated (stick moved -> pad; mouse
    or look key moved -> mouse; the mouse wins a tick both moved in; starts as the mouse on PC, the pad on Xbox).
  - `:396-397` after Epic's stick code and the pad's invert: `AddMouseLook(...)`; `:483-516` `AddMouseLook`:
    rotation units = counts x `MouseLookSensitivity` x 4.0049778 (= 0.022 deg in units) x `DesiredFOV/90`, own
    invert, the fraction of a unit carried to the next tick, added to `aTurn/aLookUp` as `(units +- 0.5)/(32 dt)` so
    that `UpdateRotation`'s `int(32 dt aTurn)` gives exactly that many units. With the mouse still nothing is
    added: the stick keeps Epic's arithmetic to the bit.
  - `:728-730`, `:772-774` aim adhesion returns 0 while the mouse owns the look (its state keeps updating).
    Friction needs no change: it acts on `aMouseX/aMouseY`, which is now the stick alone.
  - `:553-569` `MouseBreaksLock`; `:577-590` console `MouseLookSens <x>`, `MouseLookInvert <bool>` (30.15);
    `:1335-1338` defaults.
  - `UCGameplay/Classes/UCPlayerController.uc:2809-2813`, `:2836-2839` the lock's pull is held off while the
    MOUSE moves against it (Epic did this for the stick only); `:2877-2882` the mouse breaks a lock by moving
    `MouseLockBreakAngle` (25) degrees past the clamp; `:2929-2933` reset when not locked; `:2950-2953`
    auto-centre pitch only while the pad owns the look; `:3036-3067` `MouseOwnsLook`, `MouseAgainstLock`,
    `MouseLookCamera` (while the mouse owns the look the camera is put at `DesiredCameraLocation`, where Epic's
    lazy camera is heading, walls included); `:3227` called after `CalcThirdPersonView` in `CalcBehindView`.
  - `UCGameplay/Classes/UCTrainingController.uc:262-322` (D-6 hunks at `:272`, `:303-322`): the tutorial's
    "look up" question answered with the mouse sets the mouse's invert; its toggle toggles the invert of the device
    that owns the look.
- Ini (`Engine/Config/DefUser.ini`, commits `b22c067`, `88c61c7`; the web `DefUser.ini` is identical,
  regenerated by `tools/dev/sync-web-ini.py`):

| Key | Epic's PC value | Now | Meaning |
|---|---|---|---|
| `[Engine.Input] MouseX` / `MouseY` (`:101-102`) | `Count bXAxis \| Axis aMouseX Speed=6.0` | `Count bXAxis \| Axis aTurn Speed=1.0` (Y: `aLookUp`) | the mouse feeds the mouse channel |
| `Left`/`Right`/`PageUp`/`PageDown` (`:64,66,107,62`) | `StrafeLeft`/`StrafeRight`/empty/`LookDown` | `Axis aTurn Speed=-16/+16`, `Axis aLookUp Speed=+16/-16` | look keys on the linear channel (~88 deg/s) |
| `JoyX/JoyY/JoyU/JoyV` (`:197-200`) | none on PC | `SpeedBase=100.0 DeadZone=0.3` (retail Xbox) | the right stick stays on `aMouseX/aMouseY` |
| `[UCGameplay.UCPlayerInput] MouseLookSensitivity`, `bInvertMouseLook`, `MouseLockBreakAngle`, `bKeysAsStick` (`:310-316`) | none | `2.5`, `False`, `25.0`, `True` | new mouse keys |
| `bNonlinearAccel`, `aMouseX_Limit`/`aTurn_Limit`, `bAccelMultipliers`, friction ranges (`:321-344`) | `true`, 100/1024, `true`, `0.75`/`0.65` | the retail disc's: same, friction `0.95`/`0.9` | the pad's curve; friction now acts on the stick only |
| `[UCGameplay.UCPlayerController] bAutoCenterPitch` (`:285`), `LockSearchTimer` (`:253`) | `true`, `0.1` | `true`, `0.3` (retail) | auto-centre is the pad's now |

> Superseded (2026-10-09/10): see section 60, 30.5.

- Frontend: `ufrontend` `51cab98` test "mouse look (D-6)" in `packages/e2e/uc2-wasm-boot.spec.js` and the
  `targets/uc2/README.md` "Mouse look" section; manifests for the rebuilt package `40216a2`, `6893b45`.
- Tools: `tools/dev/d6-mouse.sh` (`look ini migrate pad lock before epic`), `tools/dev/d6-checks.py` (circle
  least-squares fit, camera there-and-back metric), `m10-inject.py`.

**How the parts fit.**
- The native side only delivers counts: SDL -> `IK_MouseX/Y` -> the `UInput` binding adds 0.01 per count to the
  variable the ini names. It is the same code as before D-6.
- The ini decides the route: the mouse into `aTurn/aLookUp` (the mouse channel), the right stick into
  `aMouseX/aMouseY` (the stick channel, Epic's route, also what a profile's controller layout writes).
- The script does the separation: Epic's `UCPlayerInput.PlayerInput` computes `aTurn/aLookUp` FROM the stick
  channel through clamp, friction, acceleration and fall-off, overwriting whatever was in them. D-6's code saves
  the mouse's value first, lets Epic's code run unchanged for the stick, then adds the mouse linearly, and turns
  the pad-only helpers off while the mouse owns the look.
- **Without the `.uc` change** (shipped `UCGameplay.u`, our engine and ini): the mouse writes `aTurn`, Epic's code
  overwrites it every tick: **the mouse does not turn the view at all.** The engine logs `Input: the mouse is
  bound to aTurn/aLookUp (D-6) but this UCGameplay.u has no D-6 mouse look ...`. The pad still works (Epic's
  path), but the left stick does not walk on PC (Epic scales it by 24000 only on the Xbox; 30.8).
- **Without the ini change** (rebuilt package, a User.ini that still says `Axis aMouseX`): the mouse goes back
  through the stick's curve (capped, accelerated, hexagonal, with friction and adhesion): what the owner
  complained about. `UC2InputMigrate.cpp` rewrites exactly-old values at start-up (30.7).
  *(Superseded (2026-10-09/10): see section 60, 30.5.)*
- **Web**: the same package and inis; the browser's pointer lock delivers `movementX/Y` to SDL; on a Linux desktop
  the browser applies the desktop's pointer acceleration (frontend note, 30.1).

**Why.** Owner, PLAN.md review of 2026-10-06: the mouse is "mapped to the gamepad joystick ... no relative
movement ... extremely sensitive ... the camera itself slides", "feels hexagonal", "separating the inputs of both
really rigorously". Measured causes in the table above.

**Decisions and alternatives.**
- The mouse channel is `aTurn/aLookUp`, which Epic's code overwrites (so they were dead as inputs): no new input
  variable and no native layout change. M10 had made the stick path linear in the ini so the mouse would be
  proportional, which took the Xbox curve away from the pad; rejected in D-6.
- Scale: the Quake/Source convention 0.022 deg/count x 2.5 (about 21 cm per 360 degrees at 800 dpi), times
  `DesiredFOV/90` (zoom slows the mouse, as on the pad).
- "Who owns the look" by last motion, not a setting. The rigid camera under the mouse: a first attempt (shifting
  `OldCameraLoc`) left a slide wherever a wall shortens the camera arm; replaced by putting the camera at its
  target. Not done: keeping some movement smoothing for the mouse (a lag on the pivot only).
- Mouse invert outside the profile: the profile is a binary file with one invert flag, which its menu calls the
  thumbstick's.
- The pad's values back to the retail disc's (friction 0.95/0.9 instead of the PC leak's 0.75/0.65, which were
  tuned for a mouse that went through the stick path).

**Disadvantages, risks, deviations.**
- It is a gameplay-script change in a **server package**: every peer must run the same `UCGameplay.u` (shipped
  and rebuilt peers together: not supported, not tested).
- The camera under the mouse has no smoothing of the pawn's movement (stairs, dodges). **O-11 (owner, 2026-10-08):
  "the camera moves smoothly, it is the character that moves jittery"**: this rigid camera drew each frame from the
  view computed before the pawn moved; fixed after this snapshot by computing the view again (section 50). The walk
  bob still reaches the screen in full under the mouse (about +-8 px at 1080p).
- The feel was never judged by a person: 0.055 deg/count is a convention, measured exact, not tuned.
- The profile still applies its pad sliders to `MouseSensitivity_X/Y` (even the transient "no profile" profile:
  0.8/0.8), so the ini's 1.0/0.9 never act; the pad's speed is the profile's (Epic's behaviour).
- Lock-on checks are flaky (bot positions; 2 of 69 failed once in the D-9 regression, also on main without D-9).
- Epic's `UCPlayerInput.UpdateSensitivityX` assigns the instance twice and never the default (BLOCKERS.md):
  left as is, no effect on the mouse since D-6.

**Status.** Measured (natively, null renderer, 60 fps; pad at 30 fps): `d6-mouse.sh` 67 PASS at the D-6 gate,
69/0 after the saveconfig checks (coordinator `logs/coord/a9..a12`): 10.012 units per count at 100, 500 and 2000
counts and in one tick (exact within the carried fraction), pitch 10.012, circle radius deviation 0.29%, camera
0.0 units of motion after a circle or a flick, the two inverts independent, adhesion 881 units under the pad and 0
under the mouse, auto-centre only under the pad, pad curves within 0.47% of the shipped scripts. The evidence runs
reproduce every symptom in the table. Browser (`uc2-wasm-boot.spec.js`, 4/4 in the D-6, D-9 and E-2 gates):
10 / 10.0125 / 10.01 / 10.01 units per pixel and no drift; pointer lock was refused by headless Chromium, so the
browser test ran without it. **Felt by a person: not verified.**

**Off switch.** No single switch. `MouseLookSensitivity`, `bInvertMouseLook`, `MouseLockBreakAngle` in
`User.ini [UCGameplay.UCPlayerInput]`; binding `MouseX/MouseY` back to `aMouseX/aMouseY` sends the mouse through
the pad's curve again (Epic's behaviour, with our script). `-noinimigrate` keeps an old User.ini untouched.

> Superseded (2026-10-09/10): see section 60, 30.5.

### 30.6 PC control defaults: `DefUser.ini` and `KeyBinds.ini` (M10, revised by D-6)

**What the player/owner sees.** A PC layout around WASD that reaches every pad command: left mouse/Alt fire,
right mouse alt-fire and shield, middle mouse or L lock-on, E melee, Q back to ranged, C 1st/3rd person, V voice
and taunts, Space jump/dodge, Ctrl jump/dodge without combo, Backspace plain jump, Tab/F1 scores, T chat; and a
pad works before any profile has applied its layout.

**Where.** Ini only (no code). `Engine/Config/DefUser.ini` `[Engine.Input]` (commits `e3c06aa` config: PC
keyboard/mouse/pad bindings in DefUser.ini, `e6a0457`, `7bbd7ea` README rows; D-6's `b22c067` for the mouse,
look keys, sticks, wheel). Every row is in `Engine/Config/README.md:35-46`. Changes against Epic's PC file (the
leak's `DefUser.ini` = the community's `User.ini`):

| Key (line) | Epic's PC binding | Now | Reason |
|---|---|---|---|
| `RightMouse` (`:51`) | `AltFire` | `AltFire \| Shield 1 \| onrelease Shield 0` | the pad's left trigger: PC players had no block/reflect |
| `MiddleMouse` (`:52`) | `Zoom` | `Lock 1 \| onrelease Lock 0` | lock-on (pad right-stick click); `Zoom` is a UT2004 leftover |
| `Tab` (`:53`) | `Type` | `ShowScores` | pad Back; `T` stays chat |
| `Ctrl` (`:56`) | `Jump` | `SmartJump 1` | the pad's left-stick click |
| `E` (`:117`), `Q` (`:129`) | empty, `AllAmmo` (a cheat) | melee (pad B), `MeleeFinishAttack \| NextWeapon` (pad Y) | |
| `C` (`:174`), `V` (`:134`) | none, `CallForHelp` | `BlackButton \| ToggleThirdCam`, `WhiteButton \| VoiceMenuNext` | pad Black / White |
| `P O U I M`, `NumPad0-6`, `Home/End` | `PlayersOnly`, `ShowDebug`, editor and camera hacks, `ACTOR ALIGN` | emptied / `CenterView` | debug keys next to the movement keys |
| `Joy1..Joy16`, `JoySlider1/2` (`:178-202`) | none on PC | `KeyBinds.ini` Layout_0 | a pad works before a profile applies its layout; Epic's `DefUserXbox.ini` had Joy11/Joy12 swapped against the XInput bit order, fixed here |
| look keys, mouse, sticks, wheel | see 30.5 and 30.8 | | |

`Engine/Config/KeyBinds.ini` (the profile's pad layouts) and `DefUserXbox.ini`/`DefaultXbox.ini` are **not
changed** (`git diff --stat 7633696 4813dd9 -- Engine/Config/`): the pad layouts are Epic's, and the Xbox inis are
what the script build compiles with (30.17).

**How the parts fit.** Pure data. The profile (script, unchanged) applies its controller layout by writing only the
`Joy*` keys (`ControllerConfiguration.Apply`), so the `Joy*` lines here only matter before a profile applies and
the keyboard keys are never touched by a profile. A User.ini is derived from `DefUser.ini` once and then kept:
these defaults reach a player only with a fresh User.ini, through the migration (30.7) for exact old values, or
through D-9's DEFAULTS button (30.16).

**Why.** Epic's PC file was a development file: no melee key, no shield on PC, cheats and debug commands next to
WASD, no pad lines (`controls.md` step 4).

**Decisions and alternatives.** Each pad command on a key near WASD; arrows turn (UT2004's default) where Epic's
strafed. Owner choices still open on the D-9 page (LOG.md 2026-10-08): the C/V order (C = Black = "next" sits left
of V = White = "previous", the reverse of the shoulders), Space dodging while moving (30.8), swap vs clear on a
conflict.

**Disadvantages, risks, deviations.** A deviation from Epic's PC defaults throughout. Dead commands Epic bound are
still bound (Shift `Walking`, G `Grab`, J `Taunt Taunt1`, R `ForceReload`, F2, F3, `=`, `-`, `\`, `[`, `]`, and the
`Shield 1/0` part, which Epic's Xbox binding has too): harmless, listed in `mouse.md` step 4.

**Status.** Measured: `m10-controls.sh keyboard` 16 checks (W forward 600 units, Right arrow turns, E melee, Q
ranged, Alt fires, Escape pause and back). Exercised later: Space and J jump (`d9-rebind.sh match`). Not exercised
by any test: scoreboard keys, lock-on keys in a match by key (the D-6 lock test uses the console), taunt/voice,
the view key, the combo key X.

**Off switch.** Edit User.ini, or the D-9 page.

### 30.7 Old `User.ini` values moved at start-up (`UC2InputMigrate`, D-6)

> Superseded (2026-10-09/10): see section 60, 30.7 (removed).

**What the player/owner sees.** A player whose User.ini came from an earlier build (natively, or the copy the
browser keeps) gets D-6's mouse and pad without deleting anything; the log says what moved.

**Where.**
- New code: `Source/Engine/Src/UC2InputMigrate.cpp` (commits `02289c7`, `6857174`, `82f25cc`): `:30-53` the list
  (Epic's and M10's `MouseX/MouseY`, M10's look keys and sticks, the dead wheel commands, Epic's PC friction
  ranges), `:56-64` M10's linear pad section (recognised by its 1,000,000/10,240,000 limits, moved as a whole),
  `:76-90` apply one key only when its value is EXACTLY the old one (case-insensitive, trailing blanks ignored),
  log `Input: User.ini [...] ... is a value from before D-6; now ...`, and re-read the config of a class that is
  already loaded (the static build loads UCGameplay before the game engine starts; found by the test); `:96-100`
  `-noinimigrate`; `:111-117` the warning when the shipped `UCGameplay.u` is combined with D-6 bindings.
- Epic's code changed: `Source/Engine/Src/UnGame.cpp:183-188` one call at the start of the client part of
  `UGameEngine::Init`, before any viewport reads its bindings.
- Tools: `tools/dev/d6-mouse.sh migrate` (and `before`/`epic` use `-noinimigrate` to reproduce the old state).

**How the parts fit.** Native only, on the config cache; the file is written at exit (web: at `flushconfig`).
Works with any package set.

**Why.** User.ini is derived once and kept for years natively and for ever on the web (C-2); the owner's browser
may still hold Epic's or M10's bindings, which would send the mouse into the stick's curve with the rebuilt
script.

**Decisions and alternatives.** Exact-match only, so anything a player chose stays. Rejected: resetting User.ini or
overwriting whole sections.

**Disadvantages, risks.** A hand-maintained list; a value that differs in anything but case/trailing blanks is
not moved. D-9's rebinding page writes only today's commands, none of which is in the list, so the migration can
never undo a rebinding (`rebind.md` step 0).

**Status.** Measured: `d6-mouse.sh migrate` 8 PASS (18 lines moved from M10's User.ini, 6 from Epic's; afterwards
500 counts = 5,006 units and the pad's limits 100 with the curve on).

**Off switch.** `-noinimigrate`.

### 30.8 Keys as a stick, the stick on PC, and the "backwards bindings" audit (D-6)

**What the player/owner sees.** On a keyboard, Space while moving dodges and wall-dodges (as A does on the pad),
Lock + A/D switches target, the on-screen "push the stick" prompts can be answered; the wheel changes weapon; the
pad's left stick walks on PC with Epic's own binding.

**Where.**
- Script (**`UCGameplay.u`**, commit `49d4705`): `UCGameplay/Classes/UCPlayerInput.uc:403-405`, `:528-541`
  `KeysAsStick`: when `bKeysAsStick` and a movement key is held, a full stick (0.707 per axis on a diagonal) is
  added to `Ctrl_Move_Fwd_Pct/Ctrl_Move_Stf_Pct`, the stick direction that SmartJump's dodges, the lock-target
  search and the timed input sequences read. `:406-412`: on PC `aForward/aStrafe` (the stick's 0..1) are
  multiplied by 24000 in `PlayerInput`, because `UCPlayerController.PlayerMove` does it only when `Level.IsXBox()`
  (`UCPlayerController.uc:3901-3908`, `:4079-4086`, Epic's comment "aForward range on PC is +/- 24000.0").
- Ini: `DefUser.ini:316` `bKeysAsStick=True`; `:214-215` wheel `NextWeapon` both ways (Epic: `NextSkill`/`PrevSkill`,
  which exist in no script package; UC2's `PrevWeapon` is an empty function); `:197-198` `JoyX/JoyY SpeedBase=100.0
  DeadZone=0.3` (M10 had `SpeedBase=2400000` to work around the missing scale).
  *(Superseded (2026-10-09/10): see section 60, 30.8.)*
- Tools: `d6-mouse.sh lock` (Lock + D switches target), `d6-mouse.sh pad` (0.28 deflection walks/turns nothing) and
  `m10-controls.sh pad` (the left stick walks at 0.35 and at full deflection).

**How the parts fit.** Script does both; the ini switch and the wheel binding are data. Without the `.uc`: the
keyboard never dodges or switches target, and the left stick does not move the player on PC (measured -0.0 units
with the shipped scripts and `SpeedBase=100`). M10's ini work-around made the stick walk but was undone by any
profile that swaps the sticks (`ControllerConfiguration.ApplyJoysticks` writes `SpeedBase=100`) and saturated
`Ctrl_Move_*_Pct` at any deflection.

> Superseded (2026-10-09/10): see section 60, 30.8.

**Why.** The owner's "keyboard bindings being backwards" (no example given). The audit (`mouse.md` step 4) looked
up every `[Engine.Input]` command as an alias, an exec function or an engine command and checked every direction
against measurements: **nothing is inverted or swapped**. What was wrong: the dead wheel commands, the movement
keys not counting as a stick (the community's reddit fix bound WASD to analog axes for this), the look keys on the
stick's curve. The reddit guide's other advice ("turn OFF Centerview") is the auto-centre that D-6 made pad-only.

**Decisions and alternatives.** Keys count as a full stick (not proportional). Side effect accepted as UC2's
SmartJump design: Space while moving dodges; Backspace stays a plain jump (owner choice open: bind Space to JUMP
ONLY on the D-9 page).

> Superseded (2026-10-09/10): see section 60, 30.8.

**Disadvantages, risks.** Changes gameplay on PC compared with Epic's PC build (which could not dodge by keyboard
at all); the owner's example is still needed (PLAN.md "Needed from the owner", O-14) to know whether "backwards"
meant something else, e.g. the C/V order.

**Status.** Measured: lock target switch by Lock + D; the left stick walks at 0.35 and full deflection, not at
0.28; the keyboard dodge itself is not asserted by a test (seen through the binding audit and the code).

**Off switch.** `bKeysAsStick=False`.

> Superseded (2026-10-09/10): see section 60, 30.8.

### 30.9 The UI in a centred 4:3 box (D-2)

**What the player/owner sees.** At any window size the menus, the HUD and the reticle are drawn in the largest
centred 4:3 area (scaled to the height, or to the width on a window narrower than 4:3), at the same layout as the
Xbox's 640x480 screen; the 3D world (and the menu level behind the menus) fills the window; a menu page's dark
background layer covers the whole window; text drawn larger takes its glyphs from bigger sizes of the same font,
so it stays crisp. At 640x480 nothing changes.

**Where.**
- Epic's code changed (commits `14d9738` Engine: the UI's 640x480 design space in a centred 4:3 box, `66445f6`
  menus clipped to the UI box, `f9e055f`/`519b5de` sharper text, `dd78796` the console's line clipped, `37fa6d4`):
  - `Source/Engine/Src/UnCanvas.cpp:985-987` `UCanvas::Update()` calls `ApplyUIBox`; `:1007-1083` `ApplyUIBox`:
    with `Fit43`, `ClipX/ClipY/SizeX/SizeY` = 640x480 (a split viewport: its share) and `CanvasToScreen` = scale
    `s = min(W/640, H/480)` plus the box's corner; the identity case (640x480, or `Native640`) runs Epic's code
    path unchanged. **Behaviour change** of the viewport canvas only (camera effects, post-processing, shadows and
    scripted textures build their own `FCanvasUtil` and are untouched).
  - `UnCanvas.cpp:56-72` a tile that covers the whole design space without a transform is a full-screen layer:
    in game it goes to the whole viewport; in a menu it follows `UIMenuBackground` (logged once per material,
    `UIScale: full-screen tile ...`). `:96` what the GUI's pages draw is clipped to the box (as the Xbox's screen
    clipped it). `:190-410` (`FontHeight`, `ParseFontPt`, `GlyphInkCentre`, `SharpFontFor`) and
    `:426-560` (their use where strings are drawn): sharper text, the 640x480 font's layout with each whole
    glyph taken from a taller size of the same family in the font's package (sizes ranked by real glyph height, ink
    centred on the small glyph's ink); `:1330` `DrawActorTile`, `:1588` `DrawPortal`, `:2238` `SetCanvasMatrix`
    mapped through the box; `:1085-1120` `GetUIScale`, `TargetToDesign`, `SetUIContext`.
  - `Source/Engine/Inc/UnCanvas.h:85-95` (non-virtual methods, no new data: the script layout of `Canvas` is
    untouched), `Source/Engine/Inc/UnRenderUtil.h:256-290` (`FCanvasUtil` gains the box and a game/menu context),
    `Source/Engine/Src/UnRenderUtil.cpp:793`.
  - `Source/Engine/Inc/UnCamera.h:499-510`, `:546-548` `UClient` gains `UIScaleMode`, `UIMenuBackground`,
    `UISharpText` (C++ only); `Source/Engine/Src/UnCamMgr.cpp:98-107` their config properties, `:143-161` console
    `UISCALE [Native640|Fit43] [Box|Fill] [Sharp|Soft]` (session only).
  - `Source/Engine/Src/UnController.cpp:3307`, `:3340` `WorldToScreen` returns and `ScreenToWorld` takes design
    pixels (the lock-on reticle, the reflect icon and the Sonar blips land on their actors).
  - `Source/GUI/Src/UnGUICore.cpp:1594` the GUI's `ResX/ResY` = canvas size; `:1662-1723` pages drawn in menu
    context, cursor and designer frames back in game context; cursor conversions (30.4).
  - `Source/Engine/Src/UnInteractionMaster.cpp:288-292` the global interactions (console line, GUI) clipped to
    the box (at 4K the console line ran 72 px into the side band).
  - `Source/Engine/Src/UnMovie.cpp:325` (our M9 file) the full-screen movie frame stays in window pixels.
  - Measurement aid: `Source/SDL2Drv/Src/SDL2Client.cpp:207-213` `TOGGLERENDERLEVEL` (the Xbox client's
    command), `Source/Engine/Src/UnRender.cpp:2352` clears to black with the level off.
- Ini: `Default.ini:179-186` `[SDL2Drv.SDL2Client] UIScaleMode=Fit43`, `UIMenuBackground=Fill`, `UISharpText=True`
  (also the compiled defaults, `SDL2Client.cpp:91-93`); README row `Engine/Config/README.md:27`. Web identical.
- Tools: `tools/dev/d2-ui-scale.sh` + `tools/dev/d2-measure.py` (runs and PIL checks at 8 sizes),
  `tools/dev/d2/*.scen`, the browser spec outside the frontend `tools/dev/d2/web/uc2-d2-uiscale.spec.js` +
  `playwright.d2.config.js`; `m10-controls.sh` `M10_RES`, `M10_UIMAP` (clicks mapped into the box).
- Frontend: `ufrontend` `735f607` the boot spec's play test maps 640x480 positions into the box.

**How the parts fit.** Native only. Scripts and the GUI keep drawing in 640x480 design pixels (the HUD's
`pos_*` structs, the crosshair at 320,240, absolute menu layouts, relative ones times `ResX/ResY`); the canvas
maps that into the box. No script was changed, so it works with the shipped packages. With `Native640` the canvas
is the window's pixels again, i.e. exactly the old behaviour (menus 1:1 top-left, relative parts stretched, HUD in
the corner).

**Why.** Owner (addendum, 2026-10-06): "it is a disaster beyond 480p, where the menu is very distorted, but worse,
the in game ui stays in the corner, including the reticle! We need to make the UI to scale to the 4:3 of fullscreen
and be centered." Cause: UC2's HUD is 640x480 design pixels (`UCHUD.uc:2` "designed for 640x480 only") with
`ScreenWidth = Canvas.SizeX`; the GUI multiplies relative sizes by the window and draws absolute ones 1:1.

**Decisions and alternatives.** One native place (`UCanvas::Update`) instead of per-element script fixes (the HUD
is a tree of hundreds of `CanvasElement`s); identity at 640x480 by construction (the regression anchor). Menu
background `Fill` by default (with `Box` the box's edge shows as a brightness step over the moving menu level).
The HUD is not clipped (actor markers may lie outside the box). Fonts: UC2 has one font per `GUIFont` for all
resolutions, so picking by resolution was not an option; glyph substitution keeps every measured layout.

**Disadvantages, risks, deviations.**
- A large edit of Epic's shared canvas code (`UnCanvas.cpp` +507 lines) that every 2D draw goes through.
- The full-screen rule is a heuristic: any tile exactly covering 640x480 at 0,0 is treated as a full-screen layer.
- The crosshair art sits about 1.5 design pixels left of its anchor (Epic's art): -1 px at 640x480, -2.5 px at
  2560x1440, -4 px at 4K from the screen centre. Not ours to fix without changing the art or the script.
- Sharp text: small kerning differences against the soft text (the 'f' of "Profile"); the Xbox button glyphs
  have no bigger size and stay magnified.
- Split screen (E-1) has code for its share of the box but was never run.

**Status.** Measured: `d2-ui-scale.sh` final pass **81 PASS, 0 FAIL** (`ui-scale.md` step 6): UI box exact at 8
sizes (including 5:4, 16:10 and portrait), 640x480 pixel-identical to main outside the self-animating parts,
`Native640` 99.68% identical to main at 1920x1080, HUD scaled within 0.5 px, lock-on reticle at the same window
pixels boxed and unboxed, the M10 mouse scenario 18/18 at 640x480 and 1920x1080; browser spec 1 passed at
1920x1080 (box 240,0 1440x1080, hover and click hit). Seen only: the pictures (`logs/d2/run/contact-sheet.png`).

**Off switch.** `UIScaleMode=Native640` (Epic's), `UIMenuBackground=Box`, `UISharpText=False`; console `UISCALE`.

### 30.10 Profiles and saves: start in the last profile, deletion, the web's files (D-3, C-2)

**What the player/owner sees.** The game starts in the profile used last (its settings, character and name
applied before any menu), profiles survive a restart natively and a closed browser tab, and deleting a profile on
the Profiles page works on Linux.

**Where.**
- New code: `Source/Engine/Src/UC2Profiles.cpp` (commits `e3529cc` Engine: the first player starts in the profile
  they last played with, `c2e7b83` PROFILESTATE diagnostics): `:63-87` `UC2StartWithLastProfile` (reads `User.ini
  [Profiles] LastProfile`; if the profile exists it is loaded into the first viewport's transient profile with the
  same two calls as `APlayerController::execLoadProfile`; the PlayerController takes it over at its first
  `SetGamepadID`); `:89-116` `UC2RememberProfile` (once per tick: the first viewport's profile name, "" for
  "no profile", against the remembered one; on a change `SetString` + `Flush` of User.ini and a log line);
  `:164-` console `PROFILESTATE`.
- Epic's code changed: `Source/Engine/Src/UnGame.cpp:245-249` (call right after `Client->NewViewport` in
  `UGameEngine::Init`), `:3529` (per tick, first thing in `UGameEngine::Tick`), `:457` (exec chain).
- Epic's code changed, **bug fix in shared Core**: `Source/Core/Inc/FFileManagerGeneric.h:240-243`
  `DeleteFiles` cut the pattern at the last `\` only, so with `/` every file was deleted as `<dir>/*/<file>` and
  deleting a profile never worked on Linux (commit `10b802f`).
- Web layout, Epic's code changed under a macro: `Source/Engine/Src/ULoadSaveManager.cpp:18-23` and the five
  functions at `:109`, `:134`, `:148`, `:186`, `:217`: under `__EMSCRIPTEN__` (or `-DUC2_FLAT_PROFILE_FILES=1`) a
  profile is one flat file `Profiles/<name>.PRO` (same bytes) instead of Epic's `Profiles/<name>/AVARIS_PROFILE.PRO`
  (commit `22e9dbf`, C-2). Native keeps Epic's layout.
- Related engine pieces from C-1/C-2 that saving depends on (reported in detail with the web work): `FLUSHCONFIG`
  (`Source/Engine/Src/UnEngine.cpp:308-316`, `61c9f48`), which the frontend sends before it stores the inis; the
  `RemoveNames` fix (`f14ba00`: the config cache was dropped without being written at a garbage purge); M13's
  `FFileManagerUnix::Exists` fix (`0c9d5d2`: a saved profile was reported missing, so every load failed).
  `Source/UCC/Src/UCC.cpp:335-` `ucc-bin profiletest save|load|list|exists|delete`.
- Ini: `User.ini [Profiles] LastProfile` (written by the engine; not in `DefUser.ini`; a section no class reads).
  The dead `[Engine.PlayerProfile] defaultProfileDirectoryPath` (`Default.ini:510`) is Epic's and read nowhere.
- Frontend: `targets/uc2/target.json` `userFiles: ["System/Profiles/*.PRO"]` (`f90b70f`), `manifestExclude`
  `System/Profiles/**`, `*.PRO`, `System/Shot*.bmp`, `System/AutoPatch.txt`; `Avaris.ini`/`User.ini` persisted as
  engine files (the existing manifestExclude derivation + `flushconfig`); README section "Profiles, the player
  name and text entry" (`899b27e`); opt-in `packages/e2e/uc2-wasm-saves.spec.js` (`b9fb10e` .. `e3a508d`).
- Tools: `tools/dev/d3-profiles.sh`, `tools/dev/c1-saves.sh` (file inventory + restart), `tools/dev/c1-web-saves.sh`
  (the wasm `ucc-bin.js` under Node with the frontend's own `userFiles` compiler).

**How the parts fit.** Engine only, no script change: comparing state once per tick covers every script path
(select, create, copy, rename, delete) without hooks, so it works with the shipped packages and on the web. The
profile file itself is written by Epic's code (`ULoadSaveManager`). On the web the frontend's daemon stores the
flat profile file (a `userFiles` glob may have a wildcard only in the file name) and the inis after `flushconfig`.

**Why.** Owner: saving does not work. Found (`web-ini-saves.md` step 3, `profiles.md` step 0): profiles DID save
natively after M13's fix, but Epic's game starts every run on a transient "No Profile", every setting lives in the
profile, a transient profile is never saved, and nothing remembers the last profile (the Xbox neither): a player
who made a profile found "No Profile" and default settings at the next start. Deleting failed on Linux; the web
could not persist a per-profile directory.

**Decisions and alternatives.** Remember in User.ini (per player, persisted by the web host) rather than in a new
file; first viewport only (a split-screen guest keeps Epic's behaviour); poll instead of script hooks. For the web,
flat files in the engine; alternative left to the coordinator: teach the frontend a directory wildcard and revert
`22e9dbf`.

**Disadvantages, risks, deviations.** New behaviour compared with the Xbox and Epic's PC build (they always start
without a profile). UC2's UI has no "no profile" choice once a profile exists (only deleting the active one gets
back to none). Native and web store the same bytes under different paths. A known gap (BLOCKERS.md): a launch that
names no class (the web Play form, a console `open`) uses `[DefaultPlayer] Class`, which after switching profile
without launching is the previous profile's character.

**Status.** Measured: `d3-profiles.sh` ALL PASS (27 lines: starts in "Saver" with no menu action, invert and
character applied in a directly started map, rename/delete followed, starts without a profile after deletion);
`c1-saves.sh` 11 PASS; `c1-web-saves.sh` 16 PASS under Node; browser `uc2-wasm-saves.spec.js` 1 passed (D-3 runs 9,
10, 11 with the wasm's sha256 proved; coordinator gates a7, a12, a13). Browser runs 4-7 were void (another
branch's engine was served from the shared `serve-web/`, `profiles.md` step 10).

**Off switch.** None for the remembering (an empty `LastProfile` starts without a profile; deleting the active
profile empties it). The web layout: the `UC2_FLAT_PROFILE_FILES` compile-time macro.

### 30.11 Player name vs profile name (D-4)

**What the player/owner sees.** Without a profile the player is called by the launcher's name (the web name
field, `-ini:`, `?Name=`), also after choosing a character; with a profile, by the profile's name everywhere,
also in a map started directly by the launcher; the launcher's own name is never overwritten by the profile's;
in the browser the name field is disabled with "Your game profile "X" names you in the game ..." while a
profile is active.

**Where.**
- Script, the rebuilt **`GUI.u`** (commits `4069362` GUI: without a profile the player keeps the launcher's name;
  `fd81f64` the launcher's name is the first player's only): `GUI/Classes/GUIController.uc:2757-2781`
  `SetProfilePlayerName`, transient-profile branch: the name is `PC.Level.GetDefaultURL("Name")` (= `User.ini
  [DefaultPlayer] Name`) for the first player; only if that is empty, or for a split-screen guest, the character's
  name as Epic did. `GUIController.uc:186`, `:190`: `bUnlockCharacters`, `bShowAllMuts` are `config` again, as in
  the shipped `GUI.u` (the source had lost the flag, so the community `bUnlockCharacters=True` tweak would have
  stopped working with a rebuilt GUI.u; M14 finding).
- Epic's code changed (commit `6dda321` Engine: a saved profile's name is not the player's own name):
  `Source/Engine/Src/UnGame.cpp:2961-2964` `UGameEngine::LoadMap` writes the URL's `Name` back to `[DefaultPlayer]`
  only while no saved profile names the player (`UC2ProfileNamesThePlayer`, `UC2Profiles.cpp:130`);
  `UnGame.cpp:1162-1167` `MatchViewportsToActors` spawns the first viewport's player with a copy of the URL whose
  `Name` is the active profile's (`UC2ProfileNameInURL`, `UC2Profiles.cpp:149`).
- Frontend (generic, data-driven, every target): `network.playerName.ownedBy` (`52e993d`):
  `packages/glue/src/config/ini-identity.js` `readPersistedNameOwner`, `packages/frontend/src/stores/net.ts`
  `resolveNameOwner`/`nameOwnerHint`, `NetworkPanel.vue` (field `:disabled`, hint), `ProxyNetworkPanel.vue`, tests
  `config-name-owner.test.ts`, `ini-identity.test.cjs`; the uc2 target names `User.ini [Profiles] LastProfile`
  with its hint (`targets/uc2/target.json:70-74`, `9a1c80a`, `d23794f`).
- Tools: `tools/dev/d4-names.sh` (n1..n4); the saves spec with `UC2_E2E_REBUILT_GUI=1`.

**How the parts fit.** The `.uc` decides the name without a profile; the engine keeps `[DefaultPlayer] Name` as
the player's own and makes the profile's name reach a directly started map (the GUI binds its name delegate only
when a menu is first shown); the frontend reads the key D-3's engine code writes and disables its field.
- **Without the rebuilt `GUI.u`**: with a profile everything is right (engine part); without one, the character's
  name ("Anubis", "Sapphire") replaces the launcher's as soon as the character page is used, goes into the match
  URL and is then written to `[DefaultPlayer] Name` (measured: `d4-names.sh` on the shipped packages, 4 FAIL
  exactly there).
- Without the engine part: the profile's name leaks into `[DefaultPlayer] Name`, which the web launcher reads back
  as the player's own name next time.
- Without the frontend part: the field stays editable while its value has no effect.

**Why.** Owner's rule (PLAN.md D-4): "the frontend's name wins when there is no profile; a profile's name wins
when one exists; if changing a profile's name from outside is impractical, the frontend disables its name field".
The facts are in `web-ini-saves.md` step 4.

**Decisions and alternatives.** Disable the field rather than rename a profile from the launcher (a profile's
name is its file/directory name). A split-screen guest keeps Epic's character name.

**Disadvantages, risks, deviations.** A changed GUI script function (the only one in `GUI.u`; 684 -> 778 bytes;
one of Epic's load-time patches targets it, which the patcher now skips for the rebuilt file). A generic frontend
change that touches every target (inactive unless a target declares `ownedBy`).

**Status.** Measured: `d4-names.sh` ALL PASS on the rebuilt tree (n1 11, n2 7, n3 3, n4 4 scenario checks + file
checks; coordinator a7); browser with the rebuilt GUI.u: "without a profile the player has the launcher's name:
Player-dd0b49"; the disabled field and hint in the saves spec. Frontend unit tests: glue 4 new, frontend 5 new.

**Off switch.** None in the engine. Remove `ownedBy` from the target to keep the field editable.

### 30.12 Text entry with a real keyboard (M10, D-5)

**What the player/owner sees.** Every on-screen keyboard page (new/rename/copy profile, the host's session name,
the mid-game server name) takes the real keyboard: letters type exactly what is pressed (W, A, S, D, X, Y no
longer navigate), Backspace deletes and clears a preselected name, Enter finishes, Escape cancels; the X that opens
the session-name page is not typed into it. Chat (T) and the console are Epic's `Console` and work as before.

**Where.**
- Epic's code changed, `Source/GUI/Src/UnGUICore.cpp` (commits `69b31bc`, `86e0cf7` typed characters reach an
  on-screen keyboard's field through its OnKeyType, `18b839d` D-5 fixes):
  - `:1002-1005` `TextEntryTarget`: the active page's visible, writable `GUIEditBox` (on a keyboard page it never
    has the focus; its key buttons do).
  - `:1219-1258` with a text field up, a key whose table code is `Type` (or any key that makes a character) is
    not a pad code: editing keys go to the field's `NativeKeyEvent`, nothing else on the page sees them.
    `:1248-1253` D-5: a real Backspace on a fully selected text is given as Delete (clears it); the on-screen
    Backspace button (pad) keeps deleting one letter.
  - `:2384-2399` characters go to the field's `OnKeyType` delegate (the handler `UC2KeyboardPanel` calls for its own
    buttons; `UGUIComponent::NativeKeyType` refuses because the field has `bAcceptsInput` false).
  - `:1018-1023`, `:1226-1231`, `:2389-2394` D-5 `GPadKeyHeld`: a character key whose press acted as a pad button
    while no text field was up has its character dropped if it arrives at a keyboard page (SDL delivers the
    character after the press, which had already opened the page synchronously).
- Ini: the `/Type` and `/Start` codes of `[GUI.EmulatedJoypad]` (`Enter=A/Start`, `Escape=B`, letters `/Type`).
- Tools: `m10-controls.sh text` (7 checks), `tools/dev/d5-text.sh` (k1 new profile, k2 rename, k3 copy, k4 session
  name, k5 chat), browser `uc2-wasm-saves.spec.js`.

**How the parts fit.** Native and ini only; `UC2KeyboardPanel`/`UC2Keyboard` (script) unchanged; works with the
shipped packages. Without the routing: Epic's behaviour, where W/A/X/Y navigate or accept while "typing".

**Why.** Owner (PLAN.md D-5): "the on-screen keyboard pages accept a real keyboard, Enter accepts, Escape
cancels". D-5's runs found the two defects fixed above (`profiles.md` step 6).

**Decisions and alternatives.** On a text page Enter = Start (= "done" in UC2's keyboard pages), Escape = B
(cancel). Backspace on a selection behaves like any PC text field.

**Disadvantages, risks.** Latin-1 only (30.1); profile names are cut at Epic's 15 characters; `GPadKeyHeld` is a
file-static heuristic (cleared at the key's release or any other press).

**Status.** Measured: `d5-text.sh` ALL PASS (k1 13, k2 14, k3 4, k4 14, k5 10); `m10-controls.sh text` 7 checks
(`WaSdKxYb` typed exactly, no navigation while typing); browser saves spec types, deletes, renames and cancels.
The mid-game server-name page (`UC2MidGame_Config`) uses the same `UC2Keyboard` but was not run.

**Off switch.** None (removing the `/Type` codes from the table brings Epic's navigation back).

### 30.13 The console on the key left of 1 (D-7)

**What the player/owner sees.** The key left of 1 (`` ` ``/`~` on a US layout; whatever that physical key is on
another layout) opens Epic's one-line console in a match and over the menus; Enter runs the line, Escape closes it
without closing the menu page beneath; Up/Down recall lines. Tab is the scoreboard in a match and Start in the
menus again. Replies are written to the log only.

**Where.**
- Ini: `Default.ini:501-502` `[Engine.Console] ConsoleKey=192` (`IK_Tilde`; Epic's 9 = Tab) (commit `9e96e95`;
  README `:28`; web regenerated). `SDL2Viewport.cpp:62` maps `SDL_SCANCODE_GRAVE` to `IK_Tilde`.
- Epic's code changed (commit `6f9a9fa`): `Source/Engine/Src/UnGame.cpp:277-289` a `ConsoleKey` of 9 (a kept
  `Avaris.ini`, the web's stored one) is moved to `IK_Tilde` at start-up for the run, with a log line.
- Epic's code changed (commits `868f41d`, `0f77515`): `Source/Engine/Src/UnInteractionMaster.cpp:85-97`,
  `:114-116`, `:154-169` (`GConsoleOwnsKey`): the release (and the control character) of a key whose press the
  console took stops at the console. **Bug fix**: Epic's `Console.uc` consumes presses only, so the release of
  Escape went on to the menu page, which closes on Escape's release.
- Tools: `tools/dev/d78/console-game.scen` (12 checks), `console-menu.scen` (8), `tools/dev/d78.sh`; browser
  `tools/dev/d78/web/uc2-d78.spec.js` (outside the frontend).

**How the parts fit.** Ini + native; `Engine/Classes/Console.uc` unchanged. Works with any package set.

**Why.** Owner: "Is the console locked or hidden? Can we unlock it?" It was never locked: Epic's shipped ini put
it on Tab, which PC players do not try, and the console sees every key first, so it also hid M10's
`Tab=ShowScores` and `Tab=Start`.

**Decisions and alternatives.** A physical key (scancode), not a character. Not done: an on-screen output area
(Epic's `Console.Message` is empty, `Console.uc:55-58`); it would need a `Console.uc` change or a native drawing
path.

**Disadvantages, risks.** Replies only in the log (`System/Avaris.log`, the terminal with `-log`, the browser
console). `ConsoleKey=9` can no longer be chosen: the start-up code moves 9 to 192 on every run (the runtime value
is not written back). The console key pressed during a D-9 key capture opens the console (left so, asserted).

**Status.** Measured: console-game 12/12, console-menu 8/8 (`console-fov.md` step 2); browser 1 passed (Backquote
opens it over the main menu, `AUTOFOV` and `get` run, Escape leaves the menu as it was).

**Off switch.** `[Engine.Console] ConsoleKey=<another key code>` (not 9).

### 30.14 AutoFOV: Hor+ field of view on wide windows (D-8)

**What the player/owner sees.** On a 16:9 or 21:9 window the view shows more to the sides instead of a vertically
zoomed 4:3 view; the 4:3 picture is the exact centre of the wide one (world, first person, zoom, letterbox); the
first-person weapon keeps its size and place; the lock-on reticle stays on its target. 4:3 is unchanged.

**Where.**
- Epic's code changed (commit `6f9a9fa` Engine: AutoFOV (Hor+) for every view of the player; port of
  engine-ut2004's template, ufront steps 2.16/2.29):
  - `Source/Engine/Inc/UnRender.h:1140-1199`, `Source/Engine/Src/UnRender.cpp:1754-1916`: `[ufront.AutoFOV]` read
    once; `ufrontAutoFovSize` (`:1801`, split viewports use the whole window's aspect), `ufrontEffectiveFov`
    (`:1830`), `ufrontBaseFov` (`:1858`, the inverse), `ufrontWeaponFov` (`:1869`), console `AUTOFOV` (`:1898`,
    session only), log lines in the template's format; `:2297-2309` the letterbox's `ScaleFOVY` made exact in
    tan space when widened.
  - `Source/Engine/Src/UnGame.cpp:3096-3144` `FScopedAutoFov`, armed at `:3176` in `UGameEngine::Draw` after
    `PlayerCalcView`: `Viewport->Actor->FovAngle` is the effective FOV only while the frame is drawn, so script
    (and the mouse's `DesiredFOV/90`) sees the real FOV; a FOV written during the frame (the teleport effect) is
    mapped back to base units. `:455` exec chain.
  - `Source/Engine/Src/UnCanvas.cpp:1394-1409` the canvas-drawn actor (first-person weapon) pass; `:1445`
    `execDrawActor`'s omitted FOV is -1 so it can be resolved exactly.
  - `Source/Engine/Src/UnController.cpp:3250-3260` `AutoFovViewFov`, used by `WorldToScreen`/`ScreenToWorld`
    (`:3286`, `:3337`).
- Ini: `Default.ini:504-507` `[ufront.AutoFOV] AutoFOV=2` (Adaptive), `ReferenceAspect=1.333333`,
  `WeaponScale=1.0` (also compiled defaults); README `:29`; web identical.
- Tools: `tools/dev/d78.sh`, `tools/dev/d78-measure.py`, `tools/dev/d78/fov.scen`, `sniper.scen`.

**How the parts fit.** Native only, no script; the template's section name lets the frontend's
`-ini:System:[ufront.AutoFOV]:AutoFOV=0` reach it.

**Why.** Owner: "Lets implement the AutoFOV work that has worked so well in all of the other engines." UE2's
`FovAngle` is horizontal, so at 16:9 the vertical FOV shrank from 73.7 to 58.7 degrees (Vert-); the Xbox's
widescreen mode did the same.

**Decisions and alternatives.** The template's scoped override; UC2 additions listed in `console-fov.md` step 0
(split-viewport aspect, the in-frame FOV write-back, the tan-space letterbox, `WorldToScreen` outside the frame,
the exact omitted-FOV case, a session console command for A/B).

**Disadvantages, risks, deviations.** Deviates from the Xbox (Vert-) by design. The teleport write-back path was
never exercised (no teleport at spawn in DM). The `AUTOFOV` console command departs from the template's "one home
per setting" rule. The frontend's shared AutoFOV e2e (`autofov-tests.js`) is not registered for uc2.

**Status.** Measured: `d78-measure.py` 16 PASS (logged effective FOV = `2 atan(tan(base/2) x aspect/(4/3))`
within 0.001; the 4:3 picture vs the centre of 16:9/21:9: third person 99.55%/98.56%, first person and zoom 100%;
weapon area and centroid 0.0; lock-on reticle within 1 px; 640x480 and 1024x768 identical to main in the static
part); browser 1 passed (menu level `base=65 effective=80.69` at 1920x1080).

**Off switch.** `[ufront.AutoFOV] AutoFOV=0`; console `AUTOFOV 0` for the session.

### 30.15 Script `SaveConfig()` on PC, writing only what changed (saveconfig: `ScriptSaveConfig`, `SAVECONFIGKEYS`)

**What the player/owner sees.** What script saves now stays after a restart (the weapon hand from `SetWeaponHand`
/ `ShowGun`, the mouse sensitivity and invert from the console or the D-9 page, rebound keys); nothing else in the
inis changes, and a match's URL options never leak into `Gametypes.ini`.

**Where.**
- Epic's code changed, **a gate lifted behind a setting**: `Source/Core/Src/UnCorSc.cpp:4909-4991`. Epic compiled
  the bodies of `execSaveConfig`/`execStaticSaveConfig` out on PC and Xbox (`#if 0 // rjp - calling SaveConfig()
  has mucho side-effects`; the Xbox reason: certification forbids unprompted disk writes, settings persist through
  the profile there). Now `UC2ScriptSaveConfigEnabled()` (`:4925`, `[Engine.Engine] ScriptSaveConfig`) decides;
  `_XBOX` keeps the no-op; a live `GameInfo` instance is not saved (logged once). Commits `e3e927d` Core: script
  SaveConfig()/StaticSaveConfig() write the ini on PC; SAVECONFIGKEYS; a base-class save leaves out what a subclass
  sets, `7a5dac9` a script StaticSaveConfig writes only what changed, `f72face`, `6d0798a`.
- Epic's code changed, `Source/Core/Src/UnObj.cpp`: `:1446-1563` the filter state and helpers, `:1565-1667`
  `UObject::SaveConfig` with the filters, active only for the script path and `SAVECONFIGKEYS` (`GSaveConfigOnly`;
  `UC2NoteDefaultOverrides` `:1469`; `UC2SetByAnotherClass` `:1507`; `UC2ConfigValueChanged` `:1529`; used at
  `:1583-1640`); `:2735` `UC2SaveConfigFromScript`; `:2752-2796` (`UC2SaveConfigKeys` `:2762`) and `:2813` the
  console command `SAVECONFIGKEYS <Class> <Prop>...` (exactly the named config properties, the same
  file/section/`ExportText`/flush code; replies one line per key, or `off`). `Source/Core/Src/UnClass.cpp:1499`,
  `:1879` record a class's deliberate defaults right after its default object loads its config.
  `Source/Core/Src/UnMisc.cpp:2158-2160` reads the switch once in `appInit` (after the `-ini:` overrides). New
  private header `Source/Core/Src/UC2ScriptSaveConfig.h`.
- Test hook: `Source/UCC/Src/UCC.cpp:411-` `ucc-bin saveconfigtest static|instance|gameinfo|keys|get` (runs the
  native as the VM would; also in the wasm `ucc-bin.js`).
- Script (**`UCGameplay.u`**): `UCPlayerInput.uc:571-590` `MouseLookSens`/`MouseLookInvert` set the class default
  and send `SAVECONFIGKEYS UCGameplay.UCPlayerInput <key>` (not `StaticSaveConfig()`, which would also write the
  profile's pad invert and sensitivity that the profile applies to that class's defaults).
- Ini: `Default.ini:42` `[Engine.Engine] ScriptSaveConfig=True` (web the same); README `:47`.
- Tools: `tools/dev/sc-saveconfig.sh` (u: native ucc, w: wasm ucc under Node, g: game set/kill/restart/off,
  m: menu round trip and a full match with the natives on and off); `d6-mouse.sh look` checks the two console
  commands land in User.ini.

**How the parts fit.** The native side makes Epic's natives work and safe; the five script call sites stay Epic's
(`saveconfig.md` step 1: `PlayerController.SetHand` (wanted), `PlayerInput.UpdateSensitivity/Accel/Smoothing`
(harmless in UC2), `UdpGamespyQuery` (server, unreachable as shipped), `DeathMatch.AdjustSkill` (a live gametype:
now refused), `charconfig` (developer)). The only script change is the two mouse commands. **With the shipped
`UCGameplay.u`**: the engine part works (e.g. the weapon hand persists) and there are no mouse commands to save.

**Why.** Owner: "this is a PC game now". What "mucho side-effects" really meant, found by experiment: a whole-class
save of a base class writes its `globalconfig` defaults over every subclass's at the next start (`SetWeaponHand`
saved `[Engine.PlayerController] DesiredFOV=85`, and UCPlayerController's FOV became 85 instead of 90 after a
restart: FOV, mouse scale and AutoFOV base all change); and a class dump bakes ~36 untouched script defaults into
the ini, so a later rebuilt package's default would never reach that player.

**Decisions and alternatives.** A class save writes a property only when it differs from a baseline (the ini's
value, else the default recorded at load, else the superclass default), and never a globalconfig property another
class deliberately sets; an instance save (a server object's state) writes the whole instance as before;
`SAVECONFIGKEYS` writes exactly the named keys. The rule between profile and ini: **the profile owns everything it
applies** (pad invert and sensitivities, auto-centre, handicap, weapons, character, name; applied after the ini at
every player start, so an ini value never wins over a loaded profile); **the ini owns what the profile has no field
for** (the mouse, the weapon hand, the FOV).

**Disadvantages, risks, deviations.** Changes the heart of `UObject::SaveConfig` (native C++ callers take the old
path; only the script path and `SAVECONFIGKEYS` are filtered). Arrays and maps are not written by a class save.
Cost measured: 995 class records, 1.5 ms in total at start, about 50 KB. Known gap (BLOCKERS.md): the character of a
launch that names no class comes from `[DefaultPlayer] Class`, not the active profile.

**Status.** Measured: `sc-saveconfig.sh` **91 PASS, 0 FAIL** (notebook gate; coordinator a9..a12 91/0): exactly one
key written for `SetWeaponHand`, FOV still 90 after a restart, `ScriptSaveConfig=False` writes nothing and logs
`off`, the profile wins where both hold a value.

**Off switch.** `[Engine.Engine] ScriptSaveConfig=False` (Epic's no-op; `SAVECONFIGKEYS` answers `off`; the mouse
commands and the D-9 page then last for the session only).

### 30.16 The KEYBOARD / MOUSE page: rebinding in game, mouse slider (D-9)

**What the player/owner sees.** OPTIONS (from the main menu and from the pause menu) has KEYBOARD / MOUSE between
CONTROLLER and GAMEPLAY, on PC only: a mouse sensitivity slider (0.1-10 with the value and the degrees per count
shown), INVERT MOUSE LOOK, and a list of 33 actions with two key columns. Enter, pad A or a click on a cell then
any key or mouse button binds it; Escape cancels, Backspace/Delete clear; a key another action had is swapped (the
bottom line says what moved); White = DEFAULTS (our `DefUser.ini`), Black = CANCEL, B/Escape = save and back. Pad
buttons are never listed or changed (they belong to the profile's controller layout).

> Superseded (2026-10-09/10): see section 60, 30.16.

**Where.**
- Script, the rebuilt **`UCMenu.u`** (commits `07d0d06` UCMenu: KEYBOARD / MOUSE settings page, first version,
  `3219806`, `c401614`, `c9982ee`):
  - `UCMenu/Classes/UC2Settings_Keys.uc` (new, 1,246 lines): actions with their exact `DefUser.ini` commands
    `:1159-1191` (MENU on Escape and CONSOLE shown but fixed); `:372-` `LoadBindings` reads the live input object
    (`GetRawInputObject().GetPropertyText`) or, for DEFAULTS, `GetConfigValue("DefUser","Engine.Input",<Key>)`
    (`:394`); `:428-460` `SaveBindings`: `SET Input <Key> <cmd>` for each changed key, then `SAVECONFIGKEYS
    Engine.Input <keys>` in batches of 16; `:505-` `LoadMouse`, `:546-580` `SaveMouse` (`SAVECONFIGKEYS
    UCGameplay.UCPlayerInput ...`); `:586-` `SliderClick` (Epic's `GUISlider` has no mouse handling); `:661-691`
    `KeyCaptured` (Escape cancels, Backspace/Delete clear, pad buttons and the console key refused); `:693-`
    `AssignKey` (swap rules).
  - `UCMenu/Classes/KeyBindingPanel.uc` (new, 323 lines): the list (a `Settings_ListPanel` like
    `WeaponPriorityPanel`), rows drawn by the page through `OnDrawItem`.
  - `UCMenu/Classes/UC2Settings_Main.uc:18`, `:65-66`, `:106-108`, `:130`, `:183-193`: the button, added only when
    `!Level.IsXBox()`.
- Epic's code changed: `Source/GUI/Src/UnGUICore.cpp:1025-1029`, `:1158-1193` (commit `4567556` GUI: a raw key
  capture takes only keys pressed while it runs). **Bug fix** for UC2's menus, which act on presses: Epic's
  `OnNeedRawKeyPress` path delivered the RELEASE of the Enter that started the capture as the new key, and SDL's
  auto-repeat presses too.
- Epic's code changed, build tooling: `Source/Core/Src/UnObj.cpp:4485-4490` (commit `e2b3a8c`). **Epic's bug**: a
  conformed package save sorted as many imports as were added, but starting at 0, reordering existing imports;
  first met when D-9's package added imports. Runs only in `ucc make`/strip; the game never saves packages.
- Engine pieces used: `SET Input` (Epic's `StaticExec`/`GlobalSetProperty`), `SAVECONFIGKEYS` (30.15).
- Test channel `keys`, `probe4` (`84c3082`). Tools: `tools/dev/d9-rebind.sh` (menu, match, restart, nodefaults,
  pad, actions, look); `tools/dev/mkassets-uc2.sh` lists UCMenu (`69ae15d`).
- Frontend: opt-in `packages/e2e/uc2-wasm-keys.spec.js` (`48657a4`), manifest for the rebuilt package (`f6ae5b2`),
  README "Keys and mouse settings in game".

**How the parts fit.** The page is script (UCMenu.u); the capture fix and the key-by-key save are native; the
mouse fields are the rebuilt UCGameplay.u's properties. Without the rebuilt `UCMenu.u`: no button; keys can still
be changed in User.ini or with `SET Input` + `SAVECONFIGKEYS Engine.Input <Key>` on the console. Without the
capture fix: the starting Enter would bind itself. Without `SAVECONFIGKEYS` (or with `ScriptSaveConfig=False`):
changes last for the session.

**Why.** Owner (addendum): "We need to have an extra IN GAME option to rebind controls. This is the most absolute
last of them all." Plus the mouse slider D-6 left undone.

**Decisions and alternatives.** Edit a copy, write only the changed keys; two columns (more keys shown as "+n");
conflicts swap rather than clear; a key bound to a command no row lists (Epic's `SwitchWeapon N`, `Taunt Taunt1`)
is simply taken and the hint says what it was; defaults read from `DefUser.ini` at run time; a `DefUser.ini`
without `[Engine.Input]` keeps every key ("DEFAULT KEYS NOT FOUND").

> Superseded (2026-10-09/10): see section 60, 30.16.

**Disadvantages, risks, deviations.** A new UI of about 1,570 lines of UnrealScript in Epic's style that we
maintain. The cell order is recomputed after a restart (bindings unchanged). The engine writes a key as `j` (FName
spelling; cosmetic). The console key during a capture opens the console. Owner choices still open: C/V order,
Space dodge, swap vs clear (LOG.md).

> Superseded (2026-10-09/10): see section 60, 30.16.

**Status.** Measured: `d9-rebind.sh` **132 PASS, 0 FAIL** (`logs/d9/final/summary.txt`; coordinator 119/0 in a12):
slider by mouse and keyboard, capture by click and Enter, swaps, clearing, defaults, cancel, pad capture refused,
J jumps after the rebind and a restart, mouse sensitivity 5.0 doubles the turn, invert flips; browser keys spec 1
passed (19/19 steps: rebinding and sensitivity survive a closed tab).

**Off switch.** None (the button is absent on the Xbox only).

### 30.17 The script compiler on Linux: `ucc-bin make` and the proof against the shipped `.u` (M14)

**What the player/owner sees.** Nothing directly. The owner gets a way to change UnrealScript: `ucc-bin make`
on the native 32-bit Linux build rebuilds all 17 script packages from `Source/<Pkg>/Classes` the way Epic's build
machine did, and a tool proves the result equal to the shipped packages plus Epic's 30 load-time patches. Every
`.u` change in this section was built with it.

**Where.**
- New code: `Source/Editor/CMakeLists.txt` (Epic's Editor package built static, `GPackage=GEditorPackage`,
  `UC2_STATIC_CLASS_ACCESSORS=1`); `Source/Editor/Src/UC2EditorRegistrants.cpp` (registers Editor's 82 classes and
  2 native tables in `ucc-bin`; `verifyclasses` checks the Editor layouts too);
  `Source/Editor/Src/UC2EpicClassOrder.h` (generated by `tools/dev/epic-class-order.py`). Commits `93686c0` Editor:
  builds natively and ucc-bin make compiles all 17 script packages, `586600b`, `c3c7397`, `434ac53`, `d2d1648`.
- Epic's Editor code changed (portability and reproduction; each from a compile error or a byte difference,
  `scripts.md` steps 1-2b): `Editor/Inc/Perlin.h` (`register INT`), `Editor/Inc/Editor.h:282` (`GCC_PACK(4)` on
  `AutosaveTimeMinutes`: GCC packed it into a bitfield word, every later member was 4 bytes off and `make` crashed
  at exit), `:613` (forward declaration), `Editor/Inc/EditorClasses.h` (first members tagged),
  `Editor/Src/UnVisi.cpp` (pointer-to-member syntax), `UAnalyzeMeshesCommandlet.cpp` (`#undef MAXFLOAT`),
  `TTFontImport.cpp:8-` (GDI TrueType import only under MSVC), `UnParams.cpp:22-` (parsers Engine also defines,
  compiled once), `UnEdSrv.cpp:447`, `:527`, `:1705` (`#exec` file names split at `\` too),
  `UMakeCommandlet.cpp:34`, `:97-98`, `:116`, `:207-212` (`.uc` files in NTFS order; native defaults refreshed as a
  DLL load did; `-CONFORMNOCONFIG`), `UnScrCom.cpp:7298-7408` (classes compiled in Epic's DLL registration order:
  the scripts only compile in that order, e.g. `UCWeapon.uc` uses `UCPawn.EWeaponAnimType`; `-NOEPICORDER` turns it
  off; `UC2RefreshNativeDefaultsLikeDllLoad` `:7377`), `UStripSourceCommandlet.cpp` (NTFS order, `-CONFORM`),
  `UnEdExp.cpp` (the C++ header exporter's `GCC_PACK` rules).
- Epic's Core/Engine code changed for it:
  - `Source/Core/Inc/UnObjBas.h:475-478` static `IMPLEMENT_CLASS` accessors (Editor only).
  - `Source/Core/Src/UnClass.cpp:370`, `:399` **bug fix of our static link**: native `UStruct`s were constructed
    on the heap with garbage `ScriptTextCRC`/`StructFlags` (Epic's DLLs used zeroed static storage), so dependency
    records carried garbage CRCs.
  - `Source/Core/Src/UnObj.cpp:3629` **real Linux bug, also in the game**: a package name taken from a file name
    now splits at `\` too (`#exec obj load file=..\Textures\T_HUD.utx` named the package `""`).
  - `Source/Core/Src/UnAnsi.cpp:510-532` `appAtoi` = MSVC's algorithm (wraps modulo 2^32; glibc saturates), found
    by `const INPUT_All = 4294967295`. **Engine-wide**: an out-of-range ini integer now wraps as on Windows/Xbox.
  - `Source/Core/Src/UnObj.cpp:4485-4490` the conform import sort (D-9, 30.16).
  - `Source/Engine/Src/UnScriptPatcher.cpp:291-308` a rebuilt package (not the first-generation file a patch was
    made for) is left unpatched, logged `ScriptPatcher: <Pkg> is not the package the patch was made for ...`;
    applied patches log `ScriptPatcher: <Pkg> patched (...)` (`:346`).
- `Source/UCC` links Editor when the target exists; root `CMakeLists.txt:430-435`: **Editor is built for 32-bit
  Linux only** (its hand-written `UEditorEngine` mirrors script's `int Pad[8]` with four pointers; MinGW not tried).
- UC2's backtick preprocessor (`Editor/Src/FTextFilter.cpp`, `` `include ``, `` `log ``, `` `logd ``,
  `Core/Classes/Avaris.huc`) is **unchanged**: Epic's build defined neither `debug` (only `-debug`) nor
  `FINAL_RELEASE` (only the Xbox launch configs), and ours does the same, so `` `log `` compiles to a `log()` call
  and `` `logd `` to nothing, byte-identical to the shipped bytecode. The 0x02 glyph bytes in 37 `.uc` strings pass
  through unchanged.
- Tools: `tools/dev/script-build.sh` (191 lines; stages `assemble make strip compare`; trees `run/m14*` or
  `run/d<N>*`; `--headers` exports C++ headers into the tree only, never `Source/`), `tools/dev/script-diff.py`
  (1,163 lines; parses every export with the v151 serialisers and compares field by field and token by token;
  per-function file and patch table), `tools/dev/upkg151.py`, `tools/dev/m14-scripts-check.sh` (the game on
  rebuilt packages: verify, menu, a match, controls, audio, effects, map census, movies, net, leftovers),
  `tools/dev/mkrun-uc2.sh` `UC2_SCRIPTS=` (a run tree with rebuilt packages). `BUILD.md` "Scripts (M14)".

**How the build works** (`script-build.sh`, reproducing `Source/BuildProcess/sync_avaris.pl`): the tree hard-links
the asset tree's content, copies `System/` without the 17 EditPackages, puts the **shipped** `.u` in `GUIRes/`
(conform target, taken from `epic-sources/System` since `8161ead`, because the asset tree now holds rebuilt files),
symlinks `<Pkg>/Classes` to `Source/`, and compiles with Epic's Xbox inis (`DefaultXbox.ini`/`DefUserXbox.ini`) as
`ucc-bin make -nobind -CONFORMNOCONFIG -noh`; then `ucc-bin editor.stripsource -CONFORM` with the PC inis and the
retail disc's `Characters.ini`/`Gametypes.ini`/`Maplists.ini` (from `reference/iso-extract`), keeping `UCMenu.u`
unstripped as Epic shipped it (it still carries Epic's retail UCMenu source text); then `script-diff.py`.

**Why.** PLAN.md D-1 ("build ucc/Editor, recompile the scripts, prove the bytecode against the shipped .u, retire
ScriptPatches") so that the addendum's script changes (D-4, D-6, D-9, saveconfig) are possible without guessing.
engine-ut2004 had no working `ucc make` on Unix to copy.

**Decisions and alternatives.** Reproduce Epic's exact pipeline (conform, Xbox inis for make, PC inis for strip)
so that an export-by-export comparison is possible, rather than "it compiles". `-CONFORMNOCONFIG`: conforming
without loading the ini into defaults (otherwise `GameEngine.MainMenuClass` would default to `StartMenu`, not
`UC2MainMenu`).

**Disadvantages, risks, deviations.**
- Scripts can be built only on the native 32-bit Linux build (not Windows, not 64-bit, not the web).
- The strip stage wants `reference/iso-extract` (falls back with a note: a few config defaults then differ) and
  `#exec` resources come from `epic-sources`.
- Two builds differ in the `InternalTime` stamp of 78 `#exec`-imported textures (by design); everything else is
  deterministic.
- 85 compiler warnings (Epic's scripts; e.g. locals used before assignment in `GUIController.uc:3365`, `:3387`),
  recorded, not touched.
- The C++ headers are compared, never regenerated (our `PTRINT` 64-bit placeholders would be lost).
- A generated class-order table to keep in step with the sources if classes are added (D-9's two new classes
  compiled without touching it).

**Status.** Measured (M14 gate, `scripts.md`): `Success - 0 error(s)`, all 17 packages; compare: 0 unexplained
differences (Core, UCAnnouncements, UnrealEd, UWeb byte-identical; 9,621 of 10,456 functions byte-identical, 813
differ only in line positions in the 17 classes changed after retail); 29 of the 30 patches are already in the
source; `m14-scripts-check.sh` 11 PASS on all 17 rebuilt packages (census 58/58 as shipped, net 16/0). Since then
each D-task's build: `Success - 0 error(s), 85 warning(s)`, and the compare shows problems only in the changed
packages (coordinator a12: UCGameplay 18, UCMenu 5, all intended).

**Off switch.** Not applicable (tooling). The game runs on whatever `.u` the tree holds.

### 30.18 The `.uc` files that changed, the rebuilt packages, the asset-tree overlay, and ScriptPatches

**The nine `.uc` files that differ from Epic's** (`git diff --stat 7633696 4813dd9 -- 'Source/*/Classes/*'`):

| File | Lines | Change | Package | Feature |
|---|---|---|---|---|
| `Engine/Classes/PSDistortionBaseMaterial.uc` | 8 | `cpptext` comment lines only, no bytecode change | (Engine.u not rebuilt) | M7, another section |
| `GUI/Classes/GUIComponent.uc` | 4 | `cpptext`: `FORCEINLINE` dropped for GCC, no bytecode change | (GUI.u) | M2, another section |
| `GUI/Classes/GUIController.uc` | 29 | `SetProfilePlayerName` transient branch; `bUnlockCharacters`/`bShowAllMuts` `config` | **GUI.u** | D-4 (30.11) |
| `UCGameplay/Classes/UCPlayerInput.uc` | 176 | mouse channel, owner, adhesion, keys as stick, stick scale, lock break, console commands | **UCGameplay.u** | D-6, saveconfig (30.5, 30.8, 30.15) |
| `UCGameplay/Classes/UCPlayerController.uc` | 59 | lock-on with a mouse, pad-only auto-centre, rigid camera under the mouse | **UCGameplay.u** | D-6 (30.5) |
| `UCGameplay/Classes/UCTrainingController.uc` | 31 | tutorial invert question per device | **UCGameplay.u** | D-6 (30.5) |
| `UCMenu/Classes/UC2Settings_Main.uc` | 20 | KEYBOARD / MOUSE button (PC only) | **UCMenu.u** | D-9 (30.16) |
| `UCMenu/Classes/UC2Settings_Keys.uc` | 1,246 (new) | the page | **UCMenu.u** | D-9 |
| `UCMenu/Classes/KeyBindingPanel.uc` | 323 (new) | the key list | **UCMenu.u** | D-9 |

> Superseded (2026-10-09/10): see section 60, 30.18.

**What each rebuilt package differs in** (`script-diff.py` against the shipped packages): GUI.u: one function
(`GUIController.SetProfilePlayerName`) plus the two `config` flags; UCGameplay.u: 18 "problems", all in the changed
code (`PlayerInput`, `Get*AdhesionAdjust`, the new functions and properties, `UpdateRotation`, `CalcBehindView`,
the training states) and 6 findings (M14's 5 + the new defaults); UCMenu.u: 5 problems (the appended exports of the
two new classes, the `btn_Advanced -> btn_Keys` property link, `InitOptions`, `CloseComplete`, `Free`). Every other
package compares as the shipped one; at D-9 a fresh build gave GUI.u and UCGameplay.u byte-identical to the asset
tree's copies.

**How the asset tree carries them.** `tools/dev/mkassets-uc2.sh:63-84` (asset tree v2; commits `e087510`,
`8161ead`, `69ae15d`): `UC2_REBUILT_PACKAGES` (default `GUI UCGameplay UCMenu`) are copied from
`assets/uc2-scripts/System` (the coordinator copies them there from a script-build tree) over `System/`, the shipped
files are put aside in `assets/uc2-shipped/System` (never deleted), `System/SCRIPTS.txt` lists each rebuilt package
with the first 16 hex digits of its sha256, and `MANIFEST.sha256` is regenerated. A package missing from the
rebuilt folder keeps the shipped file. Run trees take an overlay with `mkrun-uc2.sh UC2_SCRIPTS=<dir>`. Frontend
manifests follow the tree (`477eca5` GUI.u, `40216a2` and `6893b45` UCGameplay.u, `f6ae5b2` UCMenu.u); `Core.u` is
unchanged, so the frontend's asset fingerprint (sha256 of Core.u) still holds.

**Network.** A rebuilt package keeps the shipped GUID and adds a generation (a conformed save). `UCGameplay` is a
server package: native dedicated servers and all clients (native and web) must switch together; a peer with the
shipped file and one with the rebuilt file were never tested together and are not supported. GUI and UCMenu are
client-only. Evidence: `net` 16/0 in M14 (all 17 packages rebuilt) and the coordinator's native net
dedicated2/listen/travel 24/0 on main after the UCGameplay.u switch (LOG.md D-6).

**ScriptPatches status.** `Source/Engine/Inc/ScriptPatches.h` (Epic's 30 load-time patches: 21 bytecode patches, 9
appended objects) is **unchanged** and still applied at load to the shipped packages it targets; the patcher skips
the three rebuilt packages (their source already contains the patches). "Retire ScriptPatches" (D-1's wording) is
therefore done only for GUI, UCGameplay and UCMenu; Engine, Editor, UnrealGame, UCInventory and UCMutators still run
patched shipped files. Two patch facts change behaviour with a rebuilt package: Epic's `CTFFlag.home` patch is
defective (wrong label, stale label-table offset), so on the shipped `UCGameplay.u` a flag returning home never runs
its `Begin:` code (`Sleep(0.1); CheckTouching()`), while the rebuilt `UCGameplay.u`, now in the asset tree, runs it:
an enemy standing on the base gets the returning flag after 0.1 s instead of on the next touch (a CTF gameplay
difference from the shipped game, by inference also from the Xbox title update; not tested in a game);
`ConnectionFailureMenu` can probe its `Timer` (UCMenu, client-side). The UCMutators patches were never exercised in
a game (no test loads UCMutators).

**Regarding the owner's earlier instruction to leave `.u` changes to the end.** PLAN.md's Phase D allowed script
changes from D-1 on, and the three packages were switched in during D-4, D-6 and D-9 (LOG.md 2026-10-07/08). The
table at the top of this section lists what reverting to the shipped packages would cost; the mouse look is the
only feature that then stops working outright, and it would need `DefUser.ini` and `UC2InputMigrate.cpp` reverted
with it.

> Superseded (2026-10-09/10): see section 60, 30.18.

## Open issues in this area

1. **O-11, third-person character jitter**: fixed after this snapshot (main `c59c02d`, section 50). The cause was
   indeed D-6's rigid mouse camera drawing the frame from the view computed before the pawn moved; the fix is in
   UCGameplay.u only.
2. **O-14, "keyboard bindings being backwards"**: no example from the owner yet; the audit found nothing inverted
   (30.8). Candidates if it was meant: the C/V order, arrows turning instead of strafing.
   *(Superseded (2026-10-09/10): see section 60, 30, open issue 2.)*
3. **No physical pad has been tried** (all pad evidence is SDL's virtual controller); pads are off on the web
   (`UseJoystick=False`, browser Gamepad API untested; frontend gamepad support is E-3); real-pad sessions are
   planned from CI builds (E-5).
4. **The mouse's feel was never judged by a person.** Native OS pointer acceleration under SDL's relative mode was
   not measured; Linux browsers apply the desktop's acceleration under pointer lock; headless tests ran without
   pointer lock.
5. **The pad's slow turns at high frame rates** (Epic's integer rotation per tick): just past the dead zone nothing
   turns below about 0.335 deflection at 60 fps. Carrying the fraction would fix it but changes Epic's pad
   arithmetic: owner decision.
   *(Superseded (2026-10-09/10): see section 60, 30, open issue 5.)*
6. Owner choices left on the D-9 page: C/V order, Space dodging while moving (vs JUMP ONLY), swap vs clear on a
   key conflict.
   *(Superseded (2026-10-09/10): see section 60, 30, open issue 6.)*
7. Not done, needs script: button glyphs that show the bound key (the hints still show Xbox A/B/X/Y), hover on list
   rows, an on-screen console output area, camera movement smoothing for the mouse.
8. Split screen on PC (E-1): Start on a second pad does not join; the UI box has code for split viewports, never run.
9. The character of a launch that names no class comes from `[DefaultPlayer] Class`, not the active profile
   (BLOCKERS.md).
10. Rebuilt-package consequences: CTF's returning flag behaves as the source says, not as the shipped+patched game
    (not tested in a game); shipped and rebuilt `UCGameplay.u` peers must not be mixed; the rebuilt `UCMenu.u` was
    never tried with the shipped `UCGameplay.u`; `ScriptPatches.h` still patches five shipped packages; the
    UCMutators patches were never exercised.
11. The script compiler exists only in the native 32-bit Linux build.
12. Tests: the menu scenarios are timing-sensitive under software GL on a loaded machine (frame-counted menu
    animations), the lock-on checks are flaky (bot positions), a click shorter than a frame can be lost (inferred).
13. Smaller: the teleport effect's FOV write-back (D-8) never exercised; the frontend's shared AutoFOV e2e not
    registered for uc2; the crosshair art sits up to 4 px off centre at 4K (Epic's art); `ConsoleKey=9` cannot be
    chosen any more; native and web keep profiles under different paths (alternative: a directory wildcard in the
    frontend, then revert `22e9dbf`); Windows input verified only under wine.

## Places where we changed Epic's shared code (this area)

C++ (Epic's files):
- `Source/Core/Inc/UnCoreNative.h`: static native lookup ignores case (an ini key `Delete` stopped the engine; M10).
- `Source/Core/Inc/FFileManagerGeneric.h`: `DeleteFiles` splits at the last separator of either kind (deleting a
  profile never worked on Linux; D-3).
- `Source/Core/Inc/UnObjBas.h`: static class accessors, Editor only (M14).
- `Source/Core/Src/UnAnsi.cpp`: `appAtoi` wraps as MSVC's does (script constants; engine-wide; M14).
- `Source/Core/Src/UnClass.cpp`: native `UStruct` fields zeroed (garbage CRCs in our static link; M14); the class
  default records for script SaveConfig (saveconfig).
- `Source/Core/Src/UnCorSc.cpp`: Epic's `#if 0` SaveConfig natives on behind `ScriptSaveConfig` (saveconfig).
- `Source/Core/Src/UnObj.cpp`: `SaveConfig` filters and `SAVECONFIGKEYS` (saveconfig); package names from file
  names split at `\` (M14); the conform import sort (Epic's bug; D-9).
- `Source/Core/Src/UnMisc.cpp`: reads `ScriptSaveConfig` once in `appInit` (saveconfig).
- `Source/Engine/Src/UnIn.cpp`: stick `SpeedBase` axes no longer scaled by 20/dt on PC (M10).
- `Source/Engine/Src/UnGame.cpp`: User.ini migration call (D-6); start in the last profile, remember it per tick,
  `PROFILESTATE` (D-3); `ConsoleKey` 9 moved to the tilde key (D-7); the profile's name in the player's login URL
  and `[DefaultPlayer] Name` kept (D-4); `FScopedAutoFov` and `AUTOFOV` (D-8).
- `Source/Engine/Src/UnInteractionMaster.cpp`: the console keeps the releases of the keys it took (D-7); global
  interactions clipped to the UI box (D-2).
- `Source/Engine/Src/UnCanvas.cpp`, `Inc/UnCanvas.h`, `Inc/UnRenderUtil.h`, `Src/UnRenderUtil.cpp`: the UI box,
  full-screen layers, menu clipping, sharper text (D-2); the canvas-drawn actor's FOV (D-8).
- `Source/Engine/Inc/UnCamera.h`, `Src/UnCamMgr.cpp`: `UClient` UI settings and `UISCALE` (D-2).
- `Source/Engine/Src/UnController.cpp`: `WorldToScreen`/`ScreenToWorld` in design pixels (D-2) and with the
  effective FOV (D-8).
- `Source/Engine/Src/UnRender.cpp`, `Inc/UnRender.h`: AutoFOV and the letterbox in tan space (D-8); black clear
  with the level off (D-2 measurements).
- `Source/Engine/Src/ULoadSaveManager.cpp`: flat profile files on the web only (C-2).
- `Source/Engine/Src/UnEngine.cpp`: `FLUSHCONFIG` (C-2; the web host's "write the inis now").
- `Source/Engine/Src/UnScriptPatcher.cpp`: rebuilt packages are not patched, and both cases are logged (M14).
- `Source/GUI/Src/UnGUICore.cpp`: the menu key table as ini data, mouse click and hover, typing into keyboard pages
  (M10); design-pixel cursor and menu clipping (D-2); Backspace on a selection, the opening key not typed (D-5); a
  raw key capture takes only fresh presses (D-9).
- `Source/Editor/*` (Inc `Editor.h`, `EditorClasses.h`, `Perlin.h`; Src `UMakeCommandlet.cpp`, `UnScrCom.cpp`,
  `UStripSourceCommandlet.cpp`, `UnEdSrv.cpp`, `UnEdExp.cpp`, `UnVisi.cpp`, `UnParams.cpp`, `TTFontImport.cpp`,
  `UAnalyzeMeshesCommandlet.cpp`): GCC portability, Epic's file and class order, conform options, the DLL-load
  default refresh (M14).

UnrealScript (Epic's files, built into rebuilt packages): `GUI/Classes/GUIController.uc` (D-4),
`UCGameplay/Classes/UCPlayerInput.uc` (D-6, saveconfig), `UCGameplay/Classes/UCPlayerController.uc` (D-6),
`UCGameplay/Classes/UCTrainingController.uc` (D-6), `UCMenu/Classes/UC2Settings_Main.uc` (D-9).

Ini data (Epic's files): `Engine/Config/Default.ini` (`[SDL2Drv.SDL2Client]` UI and pad keys, `[SDL2Drv.PadMap]`,
`[GUI.EmulatedJoypad]`, `bHideMouseCursor`, `ConsoleKey`, `[ufront.AutoFOV]`, `ScriptSaveConfig`) and
`Engine/Config/DefUser.ini` (`[Engine.Input]`, `[UCGameplay.UCPlayerInput]`, `[UCGameplay.UCPlayerController]`), with
their generated web copies; every row in `Engine/Config/README.md`. `KeyBinds.ini` and the Xbox inis are unchanged.

New files of ours (not Epic's): `Source/SDL2Drv/*`, `Source/Engine/Src/UC2InputMigrate.cpp`,
`Source/Engine/Src/UC2Profiles.cpp`, `Source/Core/Src/UC2ScriptSaveConfig.h`, `Source/Editor/CMakeLists.txt`,
`Source/Editor/Src/UC2EditorRegistrants.cpp`, `Source/Editor/Src/UC2EpicClassOrder.h`,
`Source/UCMenu/Classes/UC2Settings_Keys.uc`, `Source/UCMenu/Classes/KeyBindingPanel.uc`.


---

# 40. Audio, movies, networking and the frontend

Code as of main `4813dd9` (snapshot `/home/ion/WorkUC2/engine-uc2-report`), baseline `7633696` (Epic's tree as
imported). Frontend: `ufrontend` branch `uc2` at `4ac0d6d` against `7fb637e` (its base on origin/main; the branch has
since moved on with an unrelated jitter spec, `aeaacc4`, which is not covered). `path:line` are snapshot line numbers.
Notebooks behind this section (`docs/porting/analysis/`): `audio.md` (M8), `video.md` (M9), `web-audio.md` (O-1,
O-2, O-4), `network.md` (net-join, net-loss), `webrtc.md` (E-2), `design.md` (audio and video design notes), plus
`LOG.md` and `PLAN.md` ("Owner review of 2026-10-06"). Where a notebook and the code disagree the code is used and
the difference is said (there are four, listed at the end of the summary).

Names to know: **XACT** is the Xbox audio tool and runtime Epic's game used (sound banks `.xsb`, wave banks `.xwb`,
the project file `Sounds/Avaris.xap`); **cooked** means converted offline into files of ours; **O-1/O-2/O-4**,
**M8/M9/M11**, **E-2** are the owner-review tasks and milestones of `PLAN.md`.

## Summary: the five things to know

1. **Sound is Epic's Xbox sound data, re-encoded offline and played by a new device that copies XACT's rules.**
   `tools/cook-audio.py` reads the XACT project text (`Avaris.xap`, 248,057 lines) and the source WAVs and writes Ogg
   Vorbis packs plus cue tables (182 MB instead of the Xbox banks' 376 MB). `Source/UC2ALAudio` (new, C++ only,
   OpenAL Soft) implements `UAudioSubsystem` the way `XboxAudio/Src/XACTAudioSubsystem.cpp` did: cue names, 2D/3D
   choice, distance cull, categories, variation playlists, loops, ambient sounds, music, pause. No `.uc` file
   changed for any of it; Epic's engine changed by one virtual function (`OpenMovieAudio`). It is **measured, never
   compared with a real Xbox**: 39 numeric checks pass (gain, pan, pitch, loops, playlists, reverb decay), but the
   absolute mix level is only right to "a dB or two", HRTF is not reproduced, and 19 source WAVs do not exist in the
   tree (the Xbox banks hold them; here 15 cues play nothing).
2. **Zone reverb is on, which the Xbox did not do.** Epic's `NotifyZoneChange` is inside `#if 0`; the Xbox ran one
   fixed Default reverb in every map. We switch an OpenAL EFX reverb preset per zone (26 I3DL2 presets from the
   XDK). The owner decided (2026-10-06) to keep it on. In DM-Azure (the owner hears reverb there on the Xbox) all
   18 zones are Default, so our device does there what the Xbox did; in the 34 of 58 maps with other presets it
   differs from the Xbox by that decision. The reverb's decay times are measured; its level against the Xbox is
   **not known** and nobody has listened to it side by side.
3. **Movies are cooked from XMV (WMV2) to Ogg Theora + Vorbis and played by a small, thread-free player.**
   `tools/cook-movies.py` (not `.sh`) turns 110 XMV files into 186; `Source/UC2Movie` + `Engine/Src/UnMovie.cpp` (new) play
   the start-up logos, the ladder cinematics (`?StartCinematic=` / `?EndCinematic=`) and every `MovieMaterial`
   texture, with the picture following the sound clock. Verified frame-for-frame against ffmpeg. **Not done:** the
   animated loading movie (a static loading screen instead), attract mode, the Xbox legal/rating screens; the
   non-English sound tracks rest on Epic's code, with no reference recording.
4. **Network play is Epic's code with its latent bugs fixed, not a new transport.** `IpDrv/TcpNetDriver.cpp` has
   zero changed lines. A client could not join at all (an assertion Epic's Xbox build compiled out) and, once it
   could, a lost first handshake packet or any loss after ~16,000 packets was never resent (a silent hang; stale
   scores minutes into a lossy match). Seven fixes sit in shared engine files (`UnScriptPatcher`, `UnCoreNet`,
   `UnConn`, `UnPenLev`, `UnChan`, `UCC`, `UnUnix`). Natively: 89 PASS, 0 FAIL in the coordinator's full run at the
   net-loss merge, 32 PASS again after E-2 (dedicated and listen servers, 1-2 clients, travel, disconnect, 5 % loss + 100 ms
   lag, a 10-minute soak, a wine server). **Never tried on a real network**, with team games, package download or more than 3 clients.
5. **The browser has the same sound device and a new network driver.** Web audio is the same `UC2ALAudio` on the
   `10-p/openal-soft-web` fork's Web Audio backend (O-2; plays in Chromium, measured by an analyser, not by ears).
   Web multiplayer is `Source/WebRTCDrv` (E-2), ported from engine-ut2004: one host and one guest play a match over
   the frontend's WebRTC data channels, from the launcher's host form and invite link (2 browser tests pass). Not
   done: in-game server discovery (UC2's System Link browser is UDP broadcast), two guests, travel over WebRTC.
   The frontend `uc2` target (`targets/uc2`, 42 commits) is mostly data; **three of its changes touch code shared
   with UT2004/UT99/Unreal 1** (package-version 145-151 readers in the glue, the disabled name field
   `network.playerName.ownedBy`, and a CI gate that now forbids the literal `uc2` in generic source).

Where notebook and code disagree: (a) the lead task and `PLAN.md` say `tools/cook-movies.sh`; it is
`tools/cook-movies.py`. (b) `audio.md` says category volumes "below 0.1 = -100 dB silence"; the code (and Epic's
`CalcVolume`) clamps 0..0.1 to -10 dB and only a volume of ~0 is silent (40.2). (c) `WebRTCDrv.h` and `webrtc.md`
say the data channel is unordered / no retransmits; the glue's channel the browser tests measured is
`ordered=true, maxRetx=null` (reliable). (d) `targets/uc2/README.md` still describes the first-light state in places
(593 files / 1,094.7 MB, "audio and a net driver needed next"); the manifest has 1,224 files / 1,991 MB.

## Which layer carries what

| Feature | Native engine (C++) | Script (.uc) | Ini / data | Frontend | If a layer is missing |
|---|---|---|---|---|---|
| Cooked sound data (40.1) | `tools/cook-audio.py` (offline) | none | `assets/uc2-cooked/{Sounds,Music}` in the asset tree | manifest lists 444 files | no data: the game says so once and runs silent |
| Audio device (40.2-40.5) | `Source/UC2ALAudio`, one virtual in `UnAudio.h` | none (Epic's `UC2Settings_Audio.uc` calls the existing `SetCategoryVolume`) | `[UC2ALAudio.UC2ALAudioSubsystem]`, `AudioDevice=` | none | no libopenal / no device: "sound is off", game runs |
| Web audio (40.6) | same, openal-soft linked static, pool 4 | none | web `Default.ini` | `audioBuffer`, capability `webAudio` | no Web Audio: silent |
| Movies (40.9-40.11) | `Source/UC2Movie`, `Engine/Src/UnMovie.cpp`, `UC2GLMovie.cpp`, hooks in `UnGame.cpp`/`UnEngine.cpp` | none (Epic's ladder and menu scripts already ask for the movies) | optional `[UC2Movie]`, `-nostartupmovies` | manifest lists 186 files | no data: "Movies are off", textures show a fallback or black |
| Net fixes (40.12-40.13) | seven shared Engine/Core/UCC files | none | none | none | **both ends need them** (package map); one end alone can still hang |
| Net test tools (40.14) | `Engine/Src/UC2NetTest.cpp` | none | `-nettest=`, `NETDROP=`, `NETPKTID=` | uc2 net spec reuses `NETPROBE` | tests only |
| WebRTC driver (40.15) | `Source/WebRTCDrv` (web only), `UnGame.cpp` `Browse`, `SDLLaunch.cpp`, `Web.cmake` | none | web `Default.ini` `[WebRTCDrv.WebRTCNetDriver]` | `network` block, host/invite flow | no driver: a browser has no sockets, no online play |
| Frontend target (40.16-40.20) | none | none | n/a | `targets/uc2`, glue v151 readers, e2e specs | n/a |

## Switches and ini keys added in this area

| Key / switch | Where | Default | What it does |
|---|---|---|---|
| `[Engine.Engine] AudioDevice` | `Default.ini:23`, web `:23` | `UC2ALAudio.UC2ALAudioSubsystem` (Epic: `Engine.NullAudioDevice`) | the device; the old line stays as a comment; `-nosound` (Epic's) selects the null device for a run |
| `[UC2ALAudio.UC2ALAudioSubsystem] OutputDevice` | `Default.ini:139`, web `:139` | empty | `alcOpenDevice` name; empty = default device |
| `... Backends` | not in native `Default.ini`; web `:140` | Linux compiled default `pipewire,pulse,alsa,oss`; Windows/web: library's own | becomes `ALSOFT_DRIVERS` unless the environment already sets it |
| `... WaveFile` / `-audiowave=<f>` | `:140` / web `:141` | empty | render to a `.wav` through OpenAL Soft's wave writer (tests) |
| `... MaxVoices` | `:141` / web `:142` | 64 (clamped 8..255) | voice pool; the Xbox had 188 2D + 64 3D |
| `... ZoneReverb` | `:142` / web `:143` | True | zone -> I3DL2 preset; False = one fixed Default reverb (the Xbox) |
| `... UseEFX` / `-NOEFX` | `:143` / web `:144` | True | reverb through EFX; off = dry sound |
| `... MusicFades` | `:144` / web `:145` | True | honour PlayMusic/StopMusic fade times; False = the Xbox (ignored them) |
| `... CueLog` | `:145` / web `:146` | True | one `DevSound` line per cue request; `DevSound` is suppressed in `Default.ini:67`, so silent unless unsuppressed |
| `[UC2Movie] StartupMovies / Cinematics / LoadingMovie` | web `Default.ini:148-151` only | True when absent | switch the three kinds of full-screen movie |
| `-NOMOVIE`, `-NOSTARTUPMOVIES` | command line | off | no full-screen movies / no logos (UE3's switch names) |
| `-NULLMOVIES` | command line | off | play full-screen movies on the null renderer too (tests) |
| `-movieshot=<t>,<t>` | command line | none | test: engine `SHOT` at those movie times |
| `[Engine.Engine] NetworkDevice` | native `:25`, web `:25` | `IpDrv.TcpNetDriver` / `WebRTCDrv.WebRTCNetDriver` | transport |
| `[WebRTCDrv.WebRTCNetDriver] ...` | web `Default.ini:153-164` | `ConnectionTimeout=60`, `InitialConnectTimeout=300`, rest as TcpNetDriver | see 40.15 |
| `-nettest=<file>`, `NETDROP=`, `NETPKTID=`, `-NETOLDACKS` | command line | off | network test hooks (40.14) |
| console `NETPROBE`, `NETPLACE`, `NETPLACENEAR`, `NETAIM`, `UC2AUDIO STATS/ZONE/PLAY` | console | n/a | test commands |

---

## Features

### 40.1 The audio cook: `Avaris.xap` + WAVs -> Ogg packs and cue tables

**What the player/owner sees.** Nothing directly. It produces the files the game plays; without them the game is
silent (and says so once in the log and on the console).

**Where.**
- New code: `tools/cook-audio.py` (1,135 lines, commit `ec082f4`). Strict parser for the `.xap` text grammar, a
  schema of every block and key (anything unknown goes to the report: 0 on `Avaris.xap`), an `.xsb`/`.xwb`
  cross-check reader, and the encoder driver (ffmpeg `libvorbis`, `-q:a 4` effects and speech, `-q:a 5` music).
  Options: `--epic`, `--out` (default `<WorkUC2>/assets/uc2-cooked`), `--jobs`, `--banks`, `--no-encode`,
  `--quality`, `--music-quality`, `--relink-localised` (see 40.8).
- Outputs (formats owned by us, specified in `audio.md` "Cooked format"): `Sounds/<WaveBankFile>.uc2wb` (182, text
  index per wave bank, localised banks `_deu/_fra/_ita/_spa`), `.ogp` (161, one Ogg Vorbis stream per wave,
  concatenated per wave bank), `Sounds/<SoundBank>.uc2cues` (78, text: sounds, tracks, events, variations,
  cues), `Music/<Track>.ogg` (23), `Sounds/cook-report.txt`, `.cache/` keyed by source SHA-1.
- Asset tree: `tools/dev/mkassets-uc2.sh` hard-links `Sounds/ Music/ Movies/ Textures/Movies/` from
  `assets/uc2-cooked` into `assets/uc2` ("asset tree v1", `8cbea58`); `tools/dev/mkrun-uc2.sh` does the same for run
  trees and copies `libopenal.so.1` beside `uc2.bin`.
- Frontend: `targets/uc2/file-manifest.json` lists the cooked files (Sounds 421 files / 124 MB, Music 23 / 65 MB);
  `manifestExclude` drops the cook's by-products (`Movies/.cook/**`, `*/cook-report.txt`, ...).
- Script/ini: none.

**How the parts fit.** The cook is offline and run by us; the shipped asset tree already contains its output. The
runtime never reads the Xbox `.xsb/.xwb`; reading them at run time is milestone M16 ("end of project").

**Why.** The engine's audio interface (`Engine/Inc/UnAudio.h`) is shaped around named XACT cues and had only an Xbox
implementation; the PC build ran `Engine.NullAudioDevice`. Owner decision (PLAN, 2026-10-04): "Cook from sources
(xap + WAV -> OGG + cue table). Runtime Xbox bank loading = end-of-project."

**Decisions and alternatives.**
- Units decoded from the data, not guessed: volumes in hundredths of a dB, pitch in cents, a sound group's volume is
  baked into each sound (confirmed on all 2,428 sounds: stored volume = `-512 - (Volume + group Volume)/16`).
- One `.ogp` per wave bank (the engine's loading unit: the Xbox registered a bank at first use). Rejected: 6,777
  per-wave files; one 122 MB archive (all fetched up front on the web).
- Quality `q4` for effects (SDR 20.2 dB against the Xbox ADPCM's 16.8-27.8 dB on weapons; 12 % of PCM size against
  the Xbox's 28 %), `q5` for music. Measured on 24 samples; SDR flatters ADPCM, so this is "at least as good", not
  "identical".
- Loop points live in the index (`loop=start,length`), not in the Ogg (Vorbis has none).

**Disadvantages, risks, deviations.**
- 19 wave entries have no source WAV (the shipped `.xwb` has them): `A_Combo` 4, `A_Gameplay` 3, `A_Weapon` 1,
  `A_WeaponRace` 1, `A_Announcer_spa` 3 (`Congratulations`, `FinishHer`, `FinishIt`), 7 level ambients. 15 cues have no
  playable wave (e.g. `Combo_BladeRush_Travel`, `Gib_Explosion_LI`, seven level ambient loops) and play nothing.
  Decoding them from the `.xwb` was considered and **not done** (the brief said not to invent data; open in
  `BLOCKERS.md`: "decode those 19 from the .xwb ... or leave them out").
  *(Superseded (2026-10-09/10): see section 60, 40.1.)*
- `A_Vehicle.xsb/.xwb` (31 vehicle cues) ship but are not in the project, so they are not cooked.
  *(Superseded (2026-10-09/10): see section 60, 40.1.)*
- One source differs from the shipped bank: `A_ChemicalDawn` entry 7 (`Machine_Generator03`): the tree's WAV is 55,808
  frames, the Xbox's 193,472; the cook uses ours with its loop region clamped.
  *(Superseded (2026-10-09/10): see section 60, 40.1.)*
- Vorbis `q4` is lossy; the encode starts from the project's source WAVs, not from the Xbox's ADPCM, so there is one lossy
  generation, not two.
- The script finds `bin/capped` by a fixed relative path (`<WorkUC2>/bin/capped`) and ffmpeg on `PATH`: it is a
  workspace tool, not a portable one.

**Status.** Measured (`logs/m8/cook-2.out`): 182 wave banks, 7,181 entries, 78 sound banks, 2,428 sounds, 2,438
cues, 6,777 distinct sources, 0 encode failures, 0 unknown schema keys; 78/78 sound banks agree with the shipped
`.xsb` (names, counts), 181/182 wave banks agree with the `.xwb` entry by entry (the one difference is the Machine
generator above). Every cooked wave has the Xbox wave's length to within one 64-frame ADPCM block (a 2.9 ms
worst case) except that one. Inferences without documentation, listed so the owner can veto them: playlist type
numbering (consistent with the data: the 43 "no immediate repeat" events are footsteps, swings, pain), priority
direction (lower value = more important), "Recurring = fade".

> Superseded (2026-10-09/10): see section 60, 40.1.

**Off switch.** None (offline tool). A tree without `Sounds/` simply has no sound.

### 40.2 The audio device: XACT behaviour on OpenAL Soft

**What the player/owner sees.** Menu music, the announcer, weapons, footsteps, pickups, ambient loops and taunts in
the player's language, in stereo with panning and distance falloff, at the Music/FX/Voice volumes of the options
menu. With no usable sound device the game still starts, silent.

**Where.**
- New code, `Source/UC2ALAudio/` (CMake `Source/UC2ALAudio/CMakeLists.txt`; commits `0b216d7` the device,
  `3ab01aa` decoded waves cut to the index length, `6aa5fc2` timestamped cue log + `-NOEFX`, `c37b1c3` music headroom):
  - `Inc/UC2ALAudio.h` - class `UUC2ALAudioSubsystem : UAudioSubsystem` with the config properties of
    the switches table, the commandlets `UC2AudioCheck`, `UC2AudioSelfTest`, `UC2ZoneSurvey`.
  - `Src/UC2ALAudioSubsystem.cpp` (2,320 lines) - the XACT rules. Anchors: banks `:37-45` (23 persistent sound banks,
    XACT `:215-237`), cue-name resolution `ResolveCue :405` (`Bank.Group.Cue` -> `Bank\Cue`), `GetWaveBank :265`
    (`<Bank>_<lang>` then `<Bank>`), voice pool `AllocVoice :788`, variation picker `ChooseVariation :830`, per-voice
    gain/pitch/position `UpdateVoice :951`, `StartTrack :1002`, `PlaySoundCue :1322` (2D/3D choice `:1368-1391`, distance
    cull `:1392-1404`), `GetSoundCueDuration :1461`, `GetSoundCueRadius :1481`, categories `:1500-1509`, ambient
    cues `UpdateAmbientCues :1719`, `Update :1792`, `NotifyLevelChange :1865`, `NotifyPauseChange :1946`, console
    commands `Exec :1982` (`UC2AUDIO STATS|ZONE <n>|PLAY <cue>`, `XACT MUSIC SONG=`, `XACT SOUNDTEST`).
  - `Src/UC2AudioData.cpp` - readers for the cooked text files and Vorbis decoding from memory (vorbisfile callbacks).
  - `Src/UC2ALDevice.cpp` - OpenAL binding (dlopen of `libopenal.so.1`/`OpenAL32.dll` natively, linked symbols on the
    web), the I3DL2 preset table `:186-214`, EFX loader `UC2ALLoadReverb :227`.
- Epic's code changed: `Engine/Inc/UnAudio.h:115` one virtual `OpenMovieAudio` (default NULL; used by movies, 40.10);
  `Engine/Src/UnScript.cpp:5131` `ALevelInfo::NotifyPauseChange` lost a stray `FORCEINLINE` (a function defined in
  a `.cpp` and called from elsewhere does not link with GCC; a build fix). Epic's call sites (`UnEngine.cpp:73`
  `InitAudio`, `UnLevAct.cpp` zone notification, `UnGame.cpp` update/level change) are unchanged.
- Registration: `Source/SDLLaunch/Src/SDLLaunch.cpp:521-523` and `Source/UCC/Src/UCC.cpp:556-557`
  (`AUTO_INITIALIZE_REGISTRANTS_UC2ALAUDIO`), `Source/SDLLaunch/CMakeLists.txt:30`; root `CMakeLists.txt:226-350`
  builds the vendored Ogg/Vorbis (`Thirdparty/Vorbis`, libvorbis 1.0 from engine-ut2004) and fetches OpenAL Soft.
- ini/data: see the switches table; script (`.uc`): none.
- Tests: 40.7.

**How the parts fit.** Epic's script calls `PlaySoundCue(actor, 'A_Weapon.RL_Fire', ...)`, `PlayMusic`,
`SetCategoryVolume`; the engine routes them to `UAudioSubsystem`; our class looks the cue up in the cooked tables,
chooses a variation, takes a voice (an OpenAL source), and each frame (`Update`) refreshes gain, pitch and position
of every live voice. Without the cooked data the class loads and refuses every cue; without OpenAL it loads and
plays nothing; the engine falls back to the null device only if `Init()` returns 0 (it never does: "sound is off"
keeps the object).

**The rules in one place (what the numbers are).**
- *2D vs 3D:* no actor = fire-and-forget 2D. An actor that is a viewer's player controller, or is owned by its view
  target, plays 2D (no panning); anything else is 3D for the closest viewport (XACT `:2176-2203`).
- *Distance cull (non-ambient 3D cues without a "Volume" parameter):* skipped when `radius < dist * (1 + cues/15)`,
  `cues` = live cue instances; radius = `2*MinDist + MinDist*(2/Rolloff)`. A sound with no 3D block gets
  DirectSound's defaults (min 1, rolloff 1) so its radius is 4 and it is culled beyond 4 units on another actor
  (an inference; see risks).
- *Volume of a voice, in hundredths of a dB (mB):* `sound volume (+ its group's) + variation volume + Set Volume
  events - 600 (6 dB per-voice headroom, unless "Headroom Boost") + category + RPC curve + distance`, clamped to
  `[-10000, 0]` (DirectSound cannot amplify), then `AL_GAIN = 10^(mB/2000)`. OpenAL's own distance model is off
  (`AL_NONE`) and doppler is off, as on the Xbox (`XACT :500`); positions only steer panning.
- *Category (Music/FX/Voice; VoiceChat is stored only):* `1000 * log10(clamp(v, 0.1, 1))` mB, i.e. v = 1 is 0 dB, 0.4
  is -4.0 dB, 0.1 is -10.0 dB, and **everything between ~0 and 0.1 is also -10 dB**; only a value below
  `KINDA_SMALL_NUMBER` is silent (-100 dB). This is Epic's `CalcVolume` (`XACT :864-873`), kept; it makes the
  options sliders gentle (the notebook's "below 0.1 = silence" is wrong). Initial values come from the default
  profile object (Music 0.40, FX 0.80, Voice 0.80) until the profile or the options page sets them.
- *Distance:* DirectSound's rolloff `Min / (Min + Rolloff*(d - Min))`, `d` clamped to `[Min, Max]`, computed in
  software.
- *Pitch:* cents, `AL_PITCH = 2^(cents/1200)`.
- *Voices and stealing:* 64 sources (config). When all are busy the least important sound (highest priority value;
  at equal priority the oldest) is stopped for a more important one; a new sound that is less important than every
  voice is dropped ("no voice" in the cue log). Direction (lower value = more important) is inferred from the data
  (announcer 40-50, weapons 100, footsteps 150, taunts 225, ambience 240-254).
- *Variations:* six playlist types implemented; the data uses Random (2,515 events), Random-no-repeat (43) and
  Ordered (1). Loops: infinite on one wave = `AL_LOOPING` (partial loop region through `AL_SOFT_loop_points`);
  finite or "new variation per loop" = one buffer queued per pass.
- *Ambient:* static `AmbientSound` actors are managed once a second: the 5 nearest within their cue radius play
  (XACT `:2010-2120`); an ambient cue replaces the actor's previous one.
- *Pause:* all live cues pause and their schedules shift; music keeps playing (XACT `:338-351`). *Level change:*
  everything stops, music too, non-persistent banks unload.
- *3D voices* are mono: stereo waves are decoded a second time as a downmix, per wave, on first 3D use.
- *Not played (counted only):* low-pass events (4 in the project), LFO (1), pitch LFO (1), envelope generator (1), mix
  bins (5), loop start (1), parametric EQ (15 sounds), LFE send. Cones have no effect in the data (outside volume 0).

**Why.** The brief: PC had no audio driver; `design.md` "Audio design" lists the behaviours to reproduce. `PLAN.md` listed
engine-ut2004's `ALAudio` as reusable for device set-up; the cue logic had to be new because the interface is
XACT-shaped, and the web build settings follow engine-ut2004's (O-2).

**Decisions and alternatives.**
- XACT behaviour reproduced, not simplified to "play a WAV": the data (2,428 sounds, 2,559 tracks, RPC curves,
  variation weights) only makes sense through XACT's model.
- Software distance attenuation with OpenAL's own distance model switched off (`AL_NONE`): the DirectSound formula is
  summed with the other terms and clamped at 0 dB as DirectSound does (checked to 0.00 dB at six distances).
- Dynamic loading of OpenAL natively (`dlopen`), so a machine with no OpenAL runs the game silent and nothing of
  OpenAL runs before the engine's allocator exists.
- Music as a streamed Ogg through a dedicated source, 4 x 32,768-frame buffers refilled once per frame on the game
  thread (8,192-frame buffers left audible gaps during long first-load frames under software GL: `logs/m8/deadbolt-2`).
- One audio device object serves the movie player as well (the movie's sound must share the device, the volume and
  the capture path), hence `OpenMovieAudio` in Epic's interface.

**Disadvantages, risks, deviations.**
- **Never compared with a real Xbox.** OpenAL Soft's centred-source gain is -1.10 dB (3D) / -1.52 dB (2D) against
  XACT's unknown routing, so the whole mix may be a constant off by that order; the owner has been sent captures
  to listen to (LOG 2026-10-04); no reply is recorded.
- No HRTF (the Xbox used "full HRTF" on 3D voices): plain stereo/surround panning; OpenAL Soft's own configuration
  (`alsoft.conf`) decides what a real device does.
  *(Superseded (2026-10-09/10): see section 60, 40.2.)*
- Inferred semantics (documentation does not say): priority direction, "Recurring" = fade, default weight of an
  unset variation (counted as 10,000 when others are set), cue radius of sounds with no 3D block (4), "Parameter
  controlled" playlist (takes the first; nothing in UC2 sets it). The non-3D radius means other players' combo
  activations and similar non-3D cues on remote actors are silent here; if the Xbox reported another radius they
  were audible there.
- Voice stealing, split-screen listener placement (`MakeRelativeToViewport` equivalent), pause/resume and the
  `Shuffle`/`Ordered from random`/`Parameter-controlled` playlists are **implemented but have never run in a
  recorded test** (64 voices were never exhausted; no split-screen run; the data uses none of those playlists).
- Memory: a wave pack (96 KB - 4.3 MB) is read whole on first use and stays resident with its decoded buffers while
  its bank is loaded; the 23 persistent banks never unload. Worst case is the cooked effects total (122 MB packed).
- `MaxVoices` 64 against the Xbox's 252: a very busy fight can steal voices the Xbox would not have.
- Each wave is decoded on the game thread at first play (a possible hitch at first use of a large wave; not measured).

**Status.** *Measured*: `UC2ALAudio.UC2AudioCheck` (all 7,141 cooked waves decode to exactly their indexed length,
78 cue tables / 2,438 cues, 0 failures; 2,417 cues resolve fully, 6 partly, 15 to nothing); `UC2AudioSelfTest` 39
checks, 0 failed (details in 40.7). *Seen/heard by a person*: nothing recorded. *Not verified*: absolute level
against the Xbox, HRTF colour, stealing, split screen, pause.

> Superseded (2026-10-09/10): see section 60, 40.2.

**Off switch.** `AudioDevice=Engine.NullAudioDevice` in `Avaris.ini`, or `-nosound` for one run (Epic's switch). The
device object then stays but plays nothing; without `libopenal` it also stays and plays nothing.

### 40.3 Music

**What the player/owner sees.** The menu music (`NuBattle`) fades in over 3 seconds when the main menu opens; a
level's music starts with the level; one song at a time.

**Where.** `UC2ALAudioSubsystem.cpp`: `PlayMusic :1556`, `StopMusic :1625`, `StopAllMusic :1651`, `UpdateMusic :1656`
(refill and fade each frame); buffers `UC2_MUSIC_BUFFERS=4 x UC2_MUSIC_BUFFER_FRAMES=32768` (`:29`). Data:
`Music/<Track>.ogg` (23 files: the 21 tracks named by the `M_*` wave banks plus `AnubisFightsSelket` and `Theme`), cue in
`A_Music.uc2cues`. Epic's script calls (`ClientSetMusic`) are unchanged.

**How the parts fit.** The Xbox played the song as a streamed wave bank `M_<Song>.xwb` plus the cue `<Song>` of
`A_Music`; we read the cue for the volume and the loop flag and stream the matching `.ogg` with libvorbis.
Fade times are honoured (`MusicFades=True`) although the Xbox ignored them ("change the actual song cue").

**Why.** The M8 brief asked for fades (`audio.md`: "deliberate difference (brief): fades").

**Decisions and alternatives.** Music gets the same 6 dB per-voice headroom as every cue (found in a capture: it was
6 dB too loud before `c37b1c3`; the Xbox plays music as a cue of `A_Music`).

**Disadvantages, risks, deviations.** Fades are a deliberate deviation (`MusicFades=False` restores the Xbox).
Decoding 32,768 frames of Vorbis on the game thread costs 5.8-6.8 ms on average and up to 16 ms in the browser (logged
once per run: `UC2Audio: music refill cost`), one such hitch every ~0.74 s at 44.1 kHz; native cost was not recorded.

**Status.** *Measured*: `tools/dev/audio-music-check.py` finds the menu track in the capture by envelope
correlation (0.84-0.87) and measures its mix gain: -19.02 dB against the expected -19.00 dB (= -900 mB sound
- 600 mB headroom + category 0.40); a control track correlates 0.115. Web: music started 1.34 s (quiet machine) to
3.1-3.6 s (busy) after the device opened; 0-1 underruns of 1-3 ms in 12 s.

**Off switch.** `MusicFades=False` for the fades; no music: delete the `Music/` folder or lower the Music slider.

### 40.4 Zone reverb (EFX, the Xbox's I3DL2 presets) and the DM-Azure finding (O-4)

**What the player/owner sees.** Walking from a corridor into a hangar or underwater changes the room tone of 3D
sounds (footsteps, gunfire of others, ambient loops): echo in a hangar, muffled underwater. In DM-Azure there is one
fixed (Default) reverb, the one every map has as its base.

**Where.**
- New code: `UC2ALDevice.cpp:186-214` the 26 `DSI3DL2_ENVIRONMENT_PRESET_*` of the XDK's `DSound.h` in
  `EZoneAudioEffect` order (`Engine/Classes/ZoneInfo.uc:56-84`, unchanged), `:227` `UC2ALLoadReverb` (mB -> gain,
  percent -> 0..1, clamped to EFX ranges; EAX reverb when the device has it, else the standard reverb);
  `UC2ALAudioSubsystem.cpp:630-653` creates one auxiliary effect slot at start-up, `:1917-1943` `NotifyZoneChange`
  (loads the preset of the zone; "unchanged" and "no EFX" are logged), `:1090-1098` the per-voice send:
  `10^(I3DL2/2000)` through a low-pass filter used as a gain, **3D voices only**, none on 2D.
- Epic's code: none changed. Epic's `UnLevAct.cpp` already calls `NotifyZoneChange` for the local player's pawn.
- Tools: `UC2ALAudio.UC2ZoneSurvey` (`UC2ZoneSurvey.cpp`, `ucc-bin UC2ALAudio.UC2ZoneSurvey [MAP=<name>]`) lists every
  map's zones and presets; `UC2AUDIO ZONE <n>` forces a preset; `-NOEFX` / `UseEFX=False` is the dry comparison.
- ini: `ZoneReverb`, `UseEFX` (switches table). Script: none.

**How the parts fit.** The map data (each `ZoneInfo.ZoneAudioEffect`, an enum) and each sound's "I3DL2 Level" (in
the XACT project: 1,694 sounds at 0 mB = full send, 733 at -300 mB, 1 at -333 mB) are Epic's; the engine tells the
device about zone changes of the local pawn; the device swaps the one reverb effect's parameters.

**Why.** The brief asked for zone reverb. The shipped Xbox code has the zone switch inside `#if 0`
(`XACTAudioSubsystem.cpp NotifyZoneChange`), so the Xbox game **never changed reverb per zone**. O-4 (owner,
2026-10-06): "the owner hears reverb/echo on the Xbox in DM-Azure". The survey found: 58 maps, 34 with a non-Default
zone, **DM-Azure 18 zones, all Default**. The Xbox loads `xbox.bin` (byte-identical to the XDK's standard effects
image, I3DL2 reverb with its Default initial state) and never sets a listener, so every 3D sound went through one
Default reverb at its own send level in every map. Our device with `ZoneReverb=True` does the same in Azure.

**Decisions and alternatives.** Owner, 2026-10-06: "Zone reverb: keep it on." Alternative kept as the switch's other
setting: `ZoneReverb=False` = the Xbox's one fixed Default reverb everywhere.

**Disadvantages, risks, deviations.**
- In the 34 maps with non-Default zones the reverb **differs from the Xbox by decision** (hangars echo, underwater
  muffles; the Xbox sounded the same everywhere).
- Whether our reverb's *level* equals the Xbox DSP's (the Xbox ran an I3DL2 DSP program, `reverb24k`; we run EFX/EAX
  reverb with the parameters carried over 1:1) is **not known and cannot be measured here** without an Xbox capture.
  The decay times are matched (below), the loudness is not calibrated.
- 2D sounds (your own weapon, menus) are dry by rule; 3D sounds with no 3D block send at full level (XACT white
  paper).
- Needs EFX; on a device without it the sound is dry and the log says so (`zone ... no EFX, sound stays dry`).

**Status.** *Measured*: the self-test fits the decay tail (RT60) of one cue per preset: Hangar 10.07 s against 10.05 s
(+0 %), StoneRoom 2.48 s vs 2.31 s (+8 %), Default 1.68 s vs 1.49 s (+13 %), NoReverb no tail, 2D no tail (also
inside the wasm32 twin under Node). In-game, log-correlated only: in DM-Praxis the player walks Default -> Underwater
-> Default; the log shows `zone ZoneInfo2: reverb preset Underwater` and the capture is +2.0 dB louder with EFX in that
window (gameplay not identical between the two runs); the DM-Azure pair: +0.4 / +3.0 / -0.0 dB by window (the window before the
spawn, the only comparable one, differs by 0.4 dB). **Nobody has listened to it against the Xbox** (the owner hears
Azure's reverb on the Xbox; no side-by-side exists). *Not verified*: level against the Xbox.

**Off switch.** `ZoneReverb=False` (fixed Default reverb) or `UseEFX=False` / `-NOEFX` (dry).

### 40.5 Native output: backend order, device choice and "sound is off" messages (O-1)

**What the player/owner sees.** The game plays through PipeWire, else PulseAudio, else ALSA, else OSS, and the log
says which; if there is no sound data, no OpenAL or no output device, the log and the console say why, once, and the
game runs silent instead of failing quietly.

**Where.** `UC2ALAudioSubsystem.cpp:458-461` (compiled default `Backends=pipewire,pulse,alsa,oss`, Linux only),
`:533-576` (the order becomes `ALSOFT_DRIVERS` unless the environment already has one; OpenAL Soft's log is captured
while the device opens and reported), `:577-590` (on failure the devices of that backend are listed),
`:514-531` and `:1800-1814` (the notices: `Notice` string, logged at Init and sent once to the console with
`eventClientMessage` from `Update`, together with the movie notice). `UC2ALDevice.cpp:99-177` the log capture
(`alsoft_set_log_callback`, found through `alcGetProcAddress`: it is not a dynamic export). Commit `0c0c39e`. Native
OpenAL Soft is built **shared** and loaded with `dlopen` (`UC2ALDevice.cpp:18-61`; Windows: `OpenAL32.dll` /
`soft_oal.dll` beside the exe, then the system's). `tools/dev/mkrun-uc2.sh` copies `libopenal.so.1` beside `uc2.bin`
and prints a note when a cooked folder is missing; `bin/capped` sets `ALSOFT_DRIVERS=null` so tests never play
through the desktop (`CLAUDE.md`).

**How the parts fit.** Epic's engine asks for `AudioDevice=` and `-nosound` as before; the order of backends is
ours. PipeWire is only present when `libpipewire-0.3-dev` was installed at CMake configure time.

**Why.** The owner (2026-10-06) reported "no sound, no movies ('they skip or play green, there are errors in the
console')". The open question in `PLAN.md` ("how the native build was launched when it had no sound") was **never
answered in the notebooks**; `PLAN.md` itself notes that the web build had neither at that time ("expected, O-2"). Owner
request: "try PipeWire, PulseAudio, ALSA in order and log which".

**Decisions and alternatives.** OpenAL Soft's own order made explicit (so it is documented and logged), not a
second implementation. `Backends=` empty leaves the choice to the library and its `alsoft.conf`.

**Disadvantages, risks, deviations.**
- On this machine the 32-bit game's PipeWire backend fails at run time (`Failed to create PipeWire event context
  (errno: 2)`: the i386 PipeWire client modules are not installed) and the game falls back to PulseAudio, which the
  PipeWire server speaks, so sound works. `BLOCKERS.md`: `sudo apt install libpipewire-0.3-modules:i386`; a player's
  32-bit machine needs the same. The 64-bit preset has the amd64 modules.
- Backends initialise once per process, so the log capture sees only the first device open.
- OpenAL Soft is fetched from `https://github.com/10-p/openal-soft-web.git` branch **`ufront/web`** (a branch name,
  `GIT_SHALLOW`; root `CMakeLists.txt:238-247`), not a pinned commit: a rebuild next month can differ, and a build
  needs network access to GitHub at configure time. Same for the web build (`:302-310`).
- **Windows output is untested**: its backends (WASAPI, DirectSound, WinMM) were never exercised; every Windows run
  was under wine with the null or wave backend. The real-Windows session is planned (E-5).

**Status.** *Measured* by runs with the null backend, a PulseAudio null sink and a missing-data tree (literal log lines
in `web-audio.md` step 1): `order pipewire,pulse,alsa,oss: pipewire failed, pulse initialised; playing through pulse`;
no cooked data -> `Game sound is off (effects, speech, music): no cooked sound data in ../Sounds ...`; no output device
-> `OpenAL could not open an output device: sound is off` and the menu still comes up; `-nosound` -> no audio line.
*Not verified*: whether the owner's original "no sound" was fixed (cause not identified); the notice appearing on the
menu's console (log only was looked at).

**Off switch.** `OutputDevice=` / `Backends=` to choose; `AudioDevice=Engine.NullAudioDevice` or `-nosound` for none.

### 40.6 Web audio: openal-soft's Web Audio backend (O-2)

**What the player/owner sees.** In the browser the menu has music and page sounds after the Start click, matches have
the same sound as the native game, and the launcher's Advanced panel can set the audio period and buffer count.

**Where.**
- Engine, `UC2ALAudio` is no longer native-only: root `CMakeLists.txt:300-349` builds the same fork **static** under
  Emscripten (`ALSOFT_BACKEND_WEB`, SSE4.1 required through Emscripten's SIMD headers, EAX off, wave writer off,
  exception catching on because OpenAL Soft throws and catches internally) and `Source/UC2ALAudio/CMakeLists.txt`
  links `OpenAL` on the web; `UC2ALDevice.cpp:23-30` fills the entry-point table with the linked symbols.
  `Source/SDLLaunch/cmake/Web.cmake:79-87` `-sPTHREAD_POOL_SIZE=4` (the backend's mixer thread and the context's event
  thread on top of `main()` and Core's file-streaming thread); `Source/UCC/CMakeLists.txt:24-28` exports `_malloc/_free`
  from `ucc-bin.js` (the fork's config reader needs them). Commit `19ca6e1`.
- Web inis: `Engine/Config/web/Default.ini:23` (`AudioDevice=`), `:138-146` the section with an empty `Backends=` (one
  backend, `web`), generated by `tools/dev/sync-web-ini.py` (which treats these lines as web-owned: `WEB_KEYS`,
  `WEB_SECTIONS`, commit `ef34e6c`), README rows in `Engine/Config/README.md:69-74`.
- Log lines added for browser checks: `UC2ALAudio: N of 23 persistent cue tables read in X ms`,
  `UC2Audio: [t] read wave pack ...`, `[t] first sound: <cue>`, `[t] music <song>: playing ... file read in X ms`.
- Frontend: `targets/uc2/target.json` `audioBuffer {periodMs: 10, periods: 3}` (enables the launcher's two audio
  fields; openal-soft reads `Module.alsoftConfig`), capability `webAudio` recommended (not required),
  `packages/e2e/uc2-wasm-audio.spec.js` (40.19), `README` "Sound and movies".
- Script: none.

**How the parts fit.** The mixer runs on a pthread of its own and renders into a ring in the shared wasm memory; an
AudioWorklet reads it. The engine itself runs in a worker (`PROXY_TO_PTHREAD`), so no AudioContext call is made
from the engine thread (the backend proxies them to the browser main thread). The context starts on the user's first
gesture: the frontend's Start click counts as sticky activation (the context was `running` when the device opened in
every run) and the backend adds `pointerdown/keydown/touchend` listeners as a fallback. The cooked files come through
the same FetchFS manifest as the packages, **each fetched whole on first touch**.

**Why.** O-2: "openal-soft's Web Audio backend from the fork the sibling port uses, the cooked Ogg audio ... through the
frontend's file system, precache rules." Before this the web build used `Engine.NullAudioDevice`.

**Decisions and alternatives.** Same device, one code path (rejected: a separate web mixer in JS). Audio buffer
defaults are engine-ut2004's "measured-safe" 10 ms x 3, not tuned for UC2.

**Disadvantages, risks, deviations.**
- Needs cross-origin isolation, AudioWorklet and threads, all already required by the target; without Web Audio
  `open()` fails and the game is silent.
- **No end-to-end latency measurement** (input to ear). The ring is ~1,536 frames at 48 kHz (about 32 ms) plus the
  browser's output latency, which differs per machine. Underruns: 0 in the quiet run, 1 (1-3 ms) in busy runs, tied to
  a 45-100 ms mixer gap or a 67 ms long task on the page.
- Start-up cost on a busy machine: the 23 persistent cue tables take 0.5 s (quiet) to 1.3-1.6 s, first sound 3.1-3.6 s
  after the device opened; **cue tables, menu music and page sounds are not in `precache.json`** (README: "not added
  yet"), so they are fetched one by one on demand. Cinematics (e.g. `01_AscPre1.ogv` 29.7 MB) are fetched whole at first
  touch: a stall at the ladder's travel.
- The music refill hitch (40.3) and the fork pinned to a branch (40.5).
- Pool size 4 adds two idle workers to the whole artifact.
- Chrome-only evidence: only Chromium was used.

**Status.** *Measured* (`uc2-wasm-audio.spec.js`, 3 tests, passed in pass A and B of `web-audio.md` step 6 and in
every coordinator run since: "audio 3/3"): the device opens on `Web Audio` with `reverb EFX (EAX reverb)`; the
AudioContext is `running` with no page input after Start; the AnalyserNode on the worklet node shows the output is not
silent (audible 0.98-1.0); logos play 20.0 s of sound in 20.2-20.4 s wall time; a key press at ~3 s ends them and
the music follows. wasm32 code under Node: `UC2AudioCheck` 0 failures, `UC2AudioSelfTest` 39/0 (reverb included).
*Seen/heard by a person*: not recorded (the spec mutes Chromium and routes to a null sink, `PULSE_SINK`, by design).
*Not verified*: other browsers, mobile, latency, long-session stability.

**Off switch.** Web inis `AudioDevice=Engine.NullAudioDevice`, or the launcher's engine option `-nosound`.

### 40.7 Audio tests and the regression suite

**What the owner sees.** One command line of results per item; nothing to listen to.

**Where.**
- `ucc-bin UC2ALAudio.UC2AudioCheck` (`UC2ALAudio.cpp:36`): every cue of every cue table resolves, every wave decodes to
  the exact length in its index, all languages, music streams open. No device needed.
- `ucc-bin UC2ALAudio.UC2AudioSelfTest` (`UC2AudioSelfTest.cpp`, 552 lines; commit `ad44320`, reverb `1739a84`): the
  device runs in OpenAL Soft's **loopback** mode (`LoopbackTest`, set only by the commandlet) with simulated time; the
  cue goes through the device's own path (`UC2TestPlay`/`UC2TestRender`, `UC2ALAudioSubsystem.cpp:2059-2136`) and the
  output is compared with values computed independently from the documented rules.
- `tools/dev/audio-regression.sh <tree>`: Check + SelfTest + four captures (`audio-menu`, `audio-deadbolt`,
  `audio-praxis-efx`, `audio-praxis-dry`) through `tools/dev/run-uc2-audio.sh` (Xvfb, `-audiowave=` wave writer, seeded
  input loop, optional pre/post console commands), `audio-capture-report.sh` (RMS per band, silent seconds,
  spectrogram), `audio-music-check.py` (identifies the menu track and its level in the capture). One `SUMMARY` line each
  in `logs/<tree>/audio-summary.txt`.
- Debug: `UC2AUDIO STATS` prints per-cue requested/played/culled/failed/no-voice counts at shutdown.

**How the parts fit.** The checks use the same device code as the game; the capture runs use the real game, so a bug in
loading, voices or reverb shows up in either.

**Why.** No Xbox oracle: the owner asked for verification "without ears".

**Decisions and alternatives.** Expected values are computed in the test from the XACT rules, not copied from the
device. Rejected: comparing with ffmpeg's own mix.

**Disadvantages, risks, deviations.**
- The suite proves arithmetic and wiring, **not that it sounds like the Xbox**.
- The pitch measurement carries a +4.3 cent method bias, measured on a 0-cent control and subtracted (raw errors are
  printed too).
- Captures are not repeatable to the cue (bots and frame timing vary; the same Praxis scenario played 547, 346 and
  260 cues in three runs); the checks decide on silence, level, identification and log lines, not on counts.
- The float wave writer does not clip: peaks of +0.3 and +3.1 dBFS were recorded; a 16-bit device would clip or
  limit.
- The wave-writer path and the self-test only run natively (and under wine/Node); the hardware path is exercised
  only by a PulseAudio null sink.

**Status.** *Measured* (the standing numbers, repeated unchanged by the coordinator at every merge, last 2026-10-08):
`UC2AudioCheck` 7,141/7,141 waves, 21/21 music streams, 2,438 cues, 0 failures; `UC2AudioSelfTest` **39 checks, 0
failed** (gain: 0.00-0.16 dB from the formula over 18 cases at 6 distances, 4 category levels and the RPC curve; pan
+/-16 dB for a source 2,000 units to the side; pitch -0.4..+0.0 cents on three cues; finite and infinite loops;
playlists 4, 1, 0; radius/duration; reverb decay); menu music identified at -19.02 dB (expected -19.00), correlation
0.84-0.87; the four captures 0 silent seconds, 0 failed cues, 0 Critical. Same results under wine (`OpenAL32.dll`) and in
the wasm32 build under Node.

**Off switch.** Test-only.

### 40.8 Localised taunt order and the missing source WAVs: kept as the Xbox had them

**What the player/owner sees.** In German, French, Italian and Spanish the Sapphire taunts 40-53 (and one tutorial
line in Spanish) are the wrong lines for their names, exactly as on the Xbox; 15 cues are silent.

> Superseded (2026-10-09/10): see section 60, 40.8.

**Where.** Data: `A_TauntSapphire_{deu,fra,ita,spa}` entries 40-53 are shifted by one against the English bank (99 entries
in all, found by comparing lengths with the shipped `.xwb`); `tools/cook-audio.py --relink-localised` (line 753, 872-885)
re-orders by name; the default does not. Nothing in the engine.

**Why.** Epic's data defect. Owner decision, 2026-10-06 (PLAN, `BLOCKERS.md`): "Localised taunt indices: keep the Xbox
behaviour (no relink)."

**Decisions and alternatives.** Keep the Xbox order (faithful), rather than repair it. The repair exists as a switch of
the cook and costs nothing to enable later.

**Disadvantages, risks, deviations.** Players of those four languages hear mismatched taunts. The 19 missing source
WAVs (40.1) remain open in `BLOCKERS.md`; decoding them from the `.xwb` was not decided.

> Superseded (2026-10-09/10): see section 60, 40.8.

**Status.** Measured: lengths of localised entries match the shipped `.xwb`'s order (entry 40 of `A_TauntSapphire_deu`
is 21,564 ADPCM bytes = the 76,626-byte `Sorry` source, not the `Spammer` of the base bank). Not verified by ear.

**Off switch.** Re-cook with `--relink-localised`.

### 40.9 The movie cook: XMV (WMV2) -> Ogg Theora + Vorbis, with a sound file per language

**What the player/owner sees.** Nothing directly; it produces the movies the game plays. The ladder's 19 cinematics, the
credits, the start-up logos, the attract clips and the 87 menu/HUD texture clips all exist as `.ogv` files.

**Where.**
- New code: `tools/cook-movies.py` (493 lines, commits `c9cc2fd`, `c4e7fd3`). It runs ffmpeg and ffprobe inside
  `bin/capped 2G`, at most `--jobs 4` at a time. Options: `--epic`, `--out`, `--only`, `--video-quality 7`, `--keyint 150`,
  `--audio-quality 4`, `--pcm-audio-quality 5`, `--no-measure`, `--force`. A re-cook skips files whose source SHA-1 and
  settings are unchanged (`Movies/.cook/<Name>.json`).
- Outputs (format version 1, ours; `video.md` "Cooked format"): `Movies/<Name>.ogv` (23: Theora + the **international
  track**, XMV audio stream 0, as Vorbis), `Movies/<Name>_<lang>.oga` (76 = 19 cinematics x deu/fra/ita/spa, written
  only when the track differs from stream 0), `Textures/Movies/<Name>.ogv` (87: video only), `Movies/movie-manifest.json`,
  `Movies/cook-report.txt`.
- Helpers: `tools/dev/movie-lang-evidence.py`, `tools/dev/xcorr.c` (language-track evidence), `tools/dev/movie-selftest.sh`
  (40.10 tests).
- Asset tree and frontend: as for sound (`mkassets-uc2.sh` hard-links `Movies/` and `Textures/Movies/`; the web
  manifest lists 99 + 87 = 186 files, 654 MB).
- Script/ini: none.

**How the parts fit.** The cook is offline; the runtime player (40.10) reads the `.ogv` and, for a non-English game
language, the matching `.oga`. A web client therefore fetches one `.ogv` and at most one `.oga` per movie, never five
languages.

**Why.** The Xbox plays XMV (WMV2 video, Xbox ADPCM or PCM sound) with the XDK's decoder; nothing on PC reads it.
Owner decision (PLAN, 2026-10-04): "Video: Offline transcode (Theora video + Vorbis audio, libtheoradec). Runtime
ffmpeg XMV decode = end-of-project" (M16). The owner asked for a small portable decoder; libtheora's decoder is
10.5k lines of plain C that builds for native, MinGW and wasm without assembly.

**Decisions and alternatives.**
- Theora `q:v 7`, GOP 150 (5 s): 85 % of the WMV2 video size at PSNR-Y 36-45 dB. Quality grid in `video.md`:
  q8 costs +25 % size for +1.6 dB; q6 saves 25 % and loses 2 dB (the 256x256 clips fall to SSIM 0.969).
- The WMV2 encoder dropped frames (held pictures; gaps up to 1,035 ms); the cook makes a **constant 30 (or 15) fps**
  stream with `-vf fps`, which libtheora writes as zero-byte "dropped frame" packets. The player counts packets, not
  decoded pictures.
- Audio is decoded to WAV first and then encoded: ffmpeg's `xmv` demuxer stamps PCM packets with the ADPCM time
  base (granule positions 3.55x too large).
- TestMaps production sources (AVI/MOV) were **checked and not used**: not the same cut, video only, and 15-bit RGB
  stored (banding not obviously better than WMV2). `PLAN.md` M9 had said "use TestMaps AVI/MOV sources where they
  exist"; this deviates from it with evidence.
- Rejected (PLAN): runtime XMV decoding with ffmpeg, kept as the end-of-project option (M16). The notebooks do not
  record an evaluation of other codecs.

**Disadvantages, risks, deviations.**
- **Lossy-to-lossy**: WMV2 -> Theora. Worst clip `Crab_Display02` (128x64) PSNR-Y 35.5 dB; `Portrait_Char_Corrosion`
  SSIM 0.972; median PSNR-Y 40.1 dB, SSIM 0.983 against the decoded XMV.
- Theora is an old codec; fine here because only our cooked files are decoded.
- **Language tracks 1-4 = deu, fra, ita, spa rest on Epic's code only** (`XMVHelper.cpp:160-189`); no reference
  recording in those languages exists in the tree. Track 0 = English **is** established from content (waveform
  cross-correlation with the English voice stems: 10-60x the other tracks, 4 of 4 cinematics with stems).
- The cook needs ffmpeg built with the `xmv` demuxer, libtheora and libvorbis, and `bin/capped` at a fixed relative
  path; 25 minutes wall time for everything.
- 417 benign ffmpeg warnings (`xmv` demuxer EIO after the last packet) are kept in the report, classified, not hidden.
- Sizes: 1,052 MB of XMV -> 654 MB of Ogg (62 %); the web manifest is 42 % cooked audio/video by size.

**Status.** *Measured*, `logs/m9/cook-1.out`: 110 XMV -> 186 files, **frame/sample accounting exact for 110 of 110**
(packets out = granule frames = slots = round(last pts x fps) + 1; sound samples = decoded XMV samples), 0 decode
errors; A-V difference -0.034..-0.037 s on 21 movies (the cooked sound ends one frame early; the player pads it with
silence). Quality: PSNR-Y min 35.50 / median 40.14 / max 45.13 dB; SSIM min 0.9722 / median 0.9831 (single-threaded
ffmpeg references; ffmpeg's threaded Theora decoder is not deterministic).

**Off switch.** Offline tool.

### 40.10 The movie player and full-screen playback: logos, cinematics, loading screen, menu textures

**What the player/owner sees.** The 20-second start-up logos (skippable with any key after half a second) before the
menu; a ladder match's intro cinematic when it loads (`?StartCinematic=`), its outro when it ends (`?EndCinematic=`);
a still "LOADING..." screen between maps; animated portraits and TV screens on menu pages and
in maps (`MovieMaterial`).

**Where.**
- New code, `Source/UC2Movie/` (CMake `Source/UC2Movie/CMakeLists.txt`; commits `5b7ec74` player, `34d2e39` build +
  registration, `8001554` self-test): `Src/UC2MoviePlayer.cpp` (703 lines) `FUC2MoviePlayer : FPortableMoviePlayer` -
  one Ogg reader per stream on its own file handle (16 KB reads, only that stream's pages), libtheora decode, libvorbis
  decode, fixed-point BT.601 limited-range conversion to BGRA with chroma replicated per 2x2; `Src/UC2MovieSelfTest.cpp`
  (511 lines) the commandlet `UC2Movie.UC2MovieSelfTest`.
- New code, Engine (our files in Epic's package): `Engine/Inc/UnMovie.h` (144 lines) the portable interface
  (`FPortableMoviePlayer`, `FMovieAudioStream`, `GCreateMoviePlayer`, the named capability `WITH_PORTABLE_MOVIES`, which is 1 on
  every non-Xbox build); `Engine/Src/UnMovie.cpp` (664 lines) what `XboxMovieThread.cpp` and `XMVHelper.cpp` did, written
  once: name resolution from the Xbox paths the content uses (`appFindMovie :88`, case-insensitive: the content says
  `Portrait_Gametype_NS` for `Portrait_GameType_NS.xmv`), the language rule (`:79-123`), full-screen loop
  `appPlayMovieFullscreen :368`, draw `DrawMovieFrame :319` (aspect kept; the "hide subtitles" bands over rows 0-96 and
  384-480 of a 480-line picture when `GShowSubtitles` is off; "LOADING..." text), input capture `appMovieInputEvent :255`,
  MovieMaterial update `appUpdateMovieMaterial :523`, and the hooks `PlayIntroCinematic :621`, `PlayOutroCinematic :612`,
  `PlayLoadingMovie :634`, `SetEngineReady`, `IsPlayingMovie` with the Xbox names. `Core/Inc/UnMovieBase.h` (new) holds
  `FMovieThread`/`FMoviePlayer`, moved unchanged out of `UnVcWin32.h:792` so every platform has one definition (`UnGnuG.h`
  includes it; the GCC port had carried a copy).
- Audio side: `UC2ALAudioSubsystem.cpp:2138-2316` `FUC2ALMovieStream`: one relative 2D source with 12 x 4,096-frame buffers; the
  clock is buffers unqueued + `AL_SAMPLE_OFFSET`, made monotonic; an exclusive (full-screen) stream pauses all cues
  (`NotifyPauseChange`) and playing music and resumes them when deleted. Epic's interface gets `OpenMovieAudio`
  (`UnAudio.h:115`).
- Epic's code changed (small, delimited hunks): `Engine/Src/UnGame.cpp` - `#include "UnMovie.h"` `:24`; the four `#if`
  lines that were `_XBOX` only now say `(defined(_XBOX) || WITH_PORTABLE_MOVIES)` (extern block `:1821`, `bWasMenu :1856`, the
  start-of-`LoadMap` block `:1860-1885` that plays the outro and starts the loading movie, the end-of-`LoadMap` block
  `:2998-3009` that plays the intro); the condition of `GRenDev->ResetRenderStates()` at `:1865` gains `&& GLevel &&
  GLevel->Engine->GRenDev` (the Xbox always had a level and a device by then; a PC's first load and a dedicated
  server do not); `UGameEngine::Exec` handles the Xbox client's `SUBTITLES` command `:460-466` (a gate lifted: it was
  `XboxClient.cpp:169-180`). `Engine/Src/UnEngine.cpp:651-653` first statement of `UEngine::InputEvent`:
  `if( appMovieInputEvent( iKey, State ) ) return 1;`. `UnAudio.h`, `UnVcWin32.h`, `UnGnuG.h` as above;
  `UnMaterial.cpp:1134-1319` `INT` -> `PTRINT` for `MovieMaterial`'s pointer fields (64-bit, not movie logic).
  Not lifted: `PaintProgress` and `Draw`'s `IsPlayingMovie`/`SetEngineReady` blocks (nothing runs in the background here).
- Launcher: `Source/SDLLaunch/Src/SDLLaunch.cpp:599-604` plays `..\Movies\UC2_Logos.xmv` after `Engine->Init()` and
  `LOADWINDOWPOSITION`, before the main loop (commit `9df0ad4`); registration `:524-526`, `Source/UCC/Src/UCC.cpp:559-561`.
- Renderer (section 20 covers it): `Source/UC2GLDrv/Src/UC2GLMovie.cpp` (116 lines, commit `dbbf486`) is the PC body of
  `SetMovieMaterial` (BGRA upload with `glTexSubImage2D` only when the picture changed); Epic's 20-line PC stub in
  `UC2GLMaterialState.cpp` is replaced by a comment.
- Vendored: `Thirdparty/Theora` (libtheora 1.2.0, decoder only, byte-identical copies with SHA-256 recorded in
  `README.uc2`, no assembly, no legacy API; our `CMakeLists.txt` defines `ogg_uint16_t/ogg_uint64_t` because the vendored
  libogg is 1.0), root `CMakeLists.txt:352-354`. `Thirdparty/Vorbis` (libvorbis 1.0, 2001).
- ini: optional `[UC2Movie] StartupMovies / Cinematics / LoadingMovie` (default True when absent; only the web
  `Default.ini:148-151` writes them out); switches `-NOMOVIE`, `-NOSTARTUPMOVIES`, `-NULLMOVIES`, `-movieshot=`.
- Script (.uc): none. Epic's `TournamentLadderRung.uc:244,559` and `UC2RungDone.uc:339` already append
  `?StartCinematic=` / `?EndCinematic=`; `T_UI_2` etc. already hold the `MovieMaterial`s.
- Tests: 40.10 Status below. Frontend: manifest (186 files), `-nostartupmovies` in the boot spec, the audio spec's logo
  tests (40.19).

**How the parts fit.** The content (Epic's) asks for a movie by its Xbox name, `D:\Movies\04a_AscPre5.xmv` or a
`MovieMaterial.MovieFileName`; `UnMovie.cpp` maps it to the cooked file, picks the sound file by the game's language,
creates a `FUC2MoviePlayer` through `GCreateMoviePlayer` (set when the package registers) and opens the device's movie
stream. The picture follows the **sound clock** (frame n shows while played frames are in `[n/fps, (n+1)/fps)`; the
cooked sound is padded to the video's length so a loop is seamless); without sound or audio device it follows real time.
Full-screen playback is a blocking loop in the calling thread (no thread, so the single-threaded web build can do the
same); a `MovieMaterial` is advanced by the render device when it is drawn. Without `UC2Movie` registered `GCreateMoviePlayer`
stays NULL and nothing plays, as before; without the cooked files each missing movie is logged once and the first
failure says why on the console.

**Why.** UC2's movies were Xbox-only: `CXMVPlayer` and `XboxMovieThread` for the logos, cinematics and the
loading movie, `D3DDrv`'s `SetMovieMaterial` (`#ifdef _XBOX`) for textures; PC builds returned 0 and drew the default
material. The brief: logos, cinematics, a menu preview animate.

**Decisions and alternatives.**
- **No thread**: the Xbox thread existed to keep the loading movie going during a blocking load. Here blocking
  playback runs in the engine thread and the loading movie is not animated (below).
- **The loading movie is a static screen**: the first picture of `UI_LoadingMovie` with "LOADING..." is drawn once
  before the load (`PlayLoadingMovie :634`). Drawing from inside the loader would re-enter the renderer while the level is
  replaced. The animated version "stays off behind its hook" (`LoadingMovie` ini key only switches the static one).
- **Logos play after `Engine->Init()`**, not before as on the Xbox, because the window opens at the end of init.
  The Xbox's legal (5 s) and rating (4.9 s) screens (`Startup/UC2Startup.xpr` textures) are not shown.
- `bDecodeFromMemory` (the Xbox's background whole-file load with a fallback texture) is ignored: the player streams
  from the file with bounded buffers.
- Skip: any key, mouse button or pad button *press* from 0.5 s on (the Xbox armed its skip after the same half second,
  so the press that started a travel does not skip its cinematic); all other input is swallowed while a movie plays.
- Movie sound is 0 dB in no category (the Xbox played XMV sound through DirectSound outside XACT).
- Chroma replicated per 2x2 block rather than interpolated (what a 4:2:0 texture shows unfiltered, and cheaper); the
  Xbox decoder's choice is not documented.

**Disadvantages, risks, deviations.**
- **Not done / off behind hooks:** the animated loading movie; attract mode (`UC2_Attract.xmv`, `A51.xmv` cooked and
  sitting in the manifest, 104 MB, but nothing calls them: the Xbox's trigger is in `UCMenu` script); the legal and rating
  screens.
- **Never exercised in a run:** outro cinematics (`?EndCinematic=`, same code path as intro), a `MovieMaterial` with
  sound (the menu's "View movies" page: the `T_UI_2` cinematics with `bEnableAudio` and `bPlayLooping`; on the web each
  would fetch its file whole, up to 66 MB for the credits), non-English sound tracks in game.
- Cinematic and logo sound **ignores the options sliders** (no category, as on the Xbox).
- A slow machine does not skip pictures: every Theora packet up to the clock's frame is decoded in order (they predict
  from each other), at most 90 per `Advance` and only the last is converted, so a slow decoder lets the picture lag
  behind the sound rather than stall the game; the sound never waits. Decode cost 1.2-2.2 ms per update on average, 36-118 ms at worst (the first
  update includes the whole-file fetch on the web).
- Blocking loop: nothing else runs during a cinematic (as on the Xbox); `appSleep` of 2-10 ms per iteration.
- **On the web:** the player is built in and active (`UC2Movie` is not native-only; the web `Default.ini` writes the three
  `[UC2Movie]` keys), so the 20-second logos play by default in the browser, skippable, unless the launcher's engine options say
  `-nostartupmovies`. The movie file is fetched whole before it plays (3.2 MB for the logos, 30 MB for a cinematic, 66-75 MB for the
  credits and `A51`), so a ladder travel stalls for the download. gl4es converts every BGRA picture to RGBA on the CPU on WebGL 1
  (no BGRA format there); that cost per picture was not measured. The pictures were never seen or asserted in a browser (below).
- A 640x480 BGRA picture (1.2 MB) plus libtheora's frames per open movie, and one OpenAL stream per movie sound.
- Shared Epic code touched: see the closing list (`UnGame.cpp`, `UnEngine.cpp`, `UnAudio.h`).

**Status.** *Measured* (`tools/dev/movie-selftest.sh`, `ucc-bin UC2Movie.UC2MovieSelfTest`, commit `8001554`): **99 checks, 0
failed** (96 at M9 + 3 for 40.11): frame count = packet count; a new picture exactly where ffmpeg decodes one; **every
picture identical to ffmpeg's decode** (Adler-32 of the Y'CbCr planes, e.g. 1,310 of 1,310 on `03_AscPre3`); BGRA vs
ffmpeg swscale (BT.601 limited, replicated chroma) maximum difference 1; loop wrap with and without sound; **A/V sync
error 0 frames** over a whole movie under random 5-40 ms steps; sound within one 16-bit step of ffmpeg's decoder;
language rule (`int/deu/fra/ita/spa/jpn` for `03_AscPre3` -> `.ogv/_deu/_fra/_ita/_spa/.ogv`; `UC2_Logos` in German falls back to
the `.ogv`). In the game, `tools/dev/m9-movies.py` **19 PASS**: logos start, advance, match their cooked frames at
65.8-79.7 dB PSNR, end after 20.000 s (1,097 pictures in 20.002 s), skip at 2.987 s; the menu portrait animates in its
frame; an intro cinematic travels from the menu, matches its frames (66.6 / 67.3 dB; the subtitle bands black), its
sound correlates 0.989-0.997 with the cooked track at 0.00 dB gain, and it is skippable; null renderer and server play none.
Web: the logos run in real time with the Web Audio device (`uc2-wasm-audio.spec.js`, sound 20.000 s in 20.2-20.4 s);
GL4ES with `LIBGL_NOBGRA=1` (WebGL 1 has no BGRA) on the native ES 2 twin shows the same pictures (82.4 / 65.8 dB).
*Weak:* the `menu-movie-shots` check (a portrait changes between two screenshots 2 s apart) is timing-sensitive and
failed once under load (24.0 dB where < 20 was wanted); `d6d7e33` made three timing-sensitive checks (movies among them) robust. *Not verified:* the
movie **picture in a real browser** (no spec asserts it: the boot spec launches with `-nostartupmovies`; the audio spec
runs the logos but checks sound and time); outro cinematics; MovieMaterial sound.

**Off switch.** `-nostartupmovies`, `-NOMOVIE`, `[UC2Movie] StartupMovies=False / Cinematics=False / LoadingMovie=False`.
Removing `UC2Movie` from the registrants restores the old behaviour (no movies).

### 40.11 A movie texture that cannot play no longer shows the engine's green default texture

**What the player/owner sees.** Portraits and screens with no movie data show their `LoadingFallback` picture or plain
black instead of bright green (the owner's "videos play green").

**Where.** `Engine/Src/UnMovie.cpp:469-521` (`FBlackMoviePicture`: a 4x4 opaque black picture, revision -2; `NoPicture`)
and `:551-565`; commit `0c0c39e`. The GL device's code is unchanged (it uploads whatever picture it gets). Test: 3 more
checks in `UC2MovieSelfTest`.

**How the parts fit.** When a material has no picture yet (movie missing, nothing decoded, nothing converted) the
update returns its `LoadingFallback` if it has one, else the black picture; one log line per material and reason.
Before, `SetMovieMaterial` returned 0 and the device drew `Material.DefaultMaterial`, which in UC2 is
`Engine.DefaultTexture`, **bright green** (the Xbox did the same for a movie it could not open, but every movie the
content referenced on purpose existed there; the web build had none in its manifest, so every texture was green).

**Why.** O-1c, owner report. Seen: `logs/m11audio-nomovies/m9-menu-movie-0.png` showed the arena page's portrait as a solid
green square; after the fix it shows the fallback (`T_Portrait.Arena.UI_Arena_Null_Portrait`).

**Decisions and alternatives.** Black over green: a missing movie is not an error the player needs to see. The theory
that all-zero YUV converts to green was checked and does not apply (our BGRA buffer starts zeroed).

**Disadvantages, risks, deviations.** Hides missing-data problems visually (the log still names them). Names the content
references but the game never shipped (all `CH_*` crosshair clips, `RL_XB_*`, ...) failed to open on the Xbox too; they
are logged once and shown as fallback/black.

**Status.** *Measured*: the self-test cases; *seen*: the picture pair above.

**Off switch.** None (behaviour fix).

### 40.12 Network join: a client can connect to a server and play (branch `net-join`)

**What the player/owner sees.** A client joins a dedicated or a listen server, loads the level, spawns, moves,
shoots, scores, follows a server travel and leaves, on Linux against Linux and against the MinGW server under wine. Before
this a joining client died with `check(GObjBeginLoadCount)` right after `Welcomed by server`, on every platform.

**Where.** `Source/IpDrv/Src/TcpNetDriver.cpp` and `UnTcpNetDriver.h` have **zero changed lines**; every fix is in the shared
engine:
- `Source/Engine/Src/UnScriptPatcher.cpp:143-155` - `BeginLoad()/EndLoad()` around the `AppendImport` loop of
  `FLinkerPatchData::Apply` (commits `0926219` first attempt, `63de2e4` the fix). *Gate/bug fixed.*
- `Source/Core/Src/UnCoreNet.cpp:276-292` - `UPackageMap::Compute` counts only what the package **file** holds (its last
  generation) and not the names/exports the script patcher appended; `:304-305` Epic's commented per-package line revived
  under `DevNet` so two ends can compare tables (commit `8a5b0e2`). *Bug fixed.*
- `Source/Engine/Src/UnConn.cpp:164-179` - `UNetConnection::Destroy` destroys (does not delete) its channels while
  `GIsGarbageCollecting`; `Source/Engine/Src/UnChan.cpp:179-190` - `UChannel::RouteDestroy` marks the channel
  `RF_DebugDestroy` after the connection destroyed it; `Source/UCC/Src/UCC.cpp:721` - `Commandlet->AddToRoot()` so the
  collector inside the server's `LoadMap` does not free the commandlet whose `Main()` is running (commit `006e725`).
  *Bugs fixed (found with AddressSanitizer).*
- `Source/Core/Src/UnUnix.cpp` (bounded `appStrncpy/appStrncat` into `GMachineCPU[32]`, 5 sites, commit `a6c8edc`): the
  port's own bug (UT2004's code over UC2's smaller buffer), not Epic's.
- Diagnostics under `DevNet` (silent by default): `UnChan.cpp:1008-1009` what a bunch contained when "Received invalid
  actor class" fires, `:1440-1451` which persistent actor has no package-map index / which class is unmapped.
- IpDrv build/portability changes (no behaviour): `GCC_PACK(4)` and `PTRINT` on the pointer-sized members of the
  generated `IpDrvClasses.h` (64-bit), `_WIN32` instead of `_MSC_VER` for WinSock, `WSAEHOSTUNREACH`, a duplicate
  errno guard for Emscripten (`UnSocket.cpp:267-271`), `SendStatLine` qualification (GCC), a CMake file. Commits
  `482cfbb` (builds), `754ce47` (MinGW), `f60b44e` (64-bit).
- ini: none. Script: none. Frontend: none (native).

**How the parts fit.** Epic's join sequence (`HELLO` -> `CHALLENGE` -> `LOGIN` -> `USES` per package -> `WELCOME` -> level
load -> `JOIN`, `network.md` "The join sequence") is unchanged. On a client every package of the server's `USES` list
first exists as an empty `UPackage`; when `LoadMap` runs the script patcher it creates linkers for those placeholders
and appends imports whose package chain is unverified, which called `GetPackageLinker` outside `BeginLoad`. Epic's
shipped Xbox build defines `FINAL_RELEASE`, which compiles `check()` out, so it never saw this. The second fix matters
because the package map assigns object and name indices by running counts: a server patches what it has loaded at its
first `LoadMap` and the rest after `Listen`, a client patches every `USES` package before its `Compute`, so the counts
differed (UCMutators 765 names on the server, 766 on the client) and with them every later base and the bit width of a
name index. **Both ends must run the fix**; a fixed client against an unfixed server can desync.

**Why.** `BLOCKERS.md`: a client joining `ucc-bin server DM-Deadbolt` got `Welcomed by server: LEVEL=DM-Deadbolt` and then
the assertion (Linux -> Linux and Linux -> wine). All Epic's code paths, latent in the shipped build.

**Decisions and alternatives.**
- First attempt (hold `BeginLoad` across the whole `Apply`) was **wrong**: it queued the classes `PreloadExports` creates
  until after the reader swap, and they were serialised from the patch bytes (`Bad export index`, garbage). Lesson
  recorded: the position of each `EndLoad` in that function decides which reader an object is read from. The narrow
  scope (the `AppendImport` loop only; it creates linkers, never objects) is the fix.
- Package-map counts: clamp to the file's last generation. Patched-in objects (function locals) and names (all present in
  other packages' name tables) are then not addressable over the network, which nothing replicated needs.
- `Received invalid actor class` once per join was **left as is**: the server opens a channel for the run-time-spawned
  `DefaultPhysicsVolume` (`bNoDelete`, no linker, DM-Deadbolt has none in its file), the client marks the channel broken
  and keeps its own volume. UT2004 does the same. A candidate fix (skip actors with no package-map index in
  `ULevel::ServerTickClients`) is noted, not applied.
- ASan found and *did not fix*: 1-byte over-reads in Epic's interpreter (`ProcessInternal` peeking `EX_DebugInfo`
  past a function's last byte, `UnCorSc.cpp:5422`/`:5481`; `execLess_IntInt` / `P_FINISH` reading past the end of a class's
  script when a replication condition is evaluated, `UnScript.cpp:3582`). Benign heap slack, recorded for the script
  milestone.

**Disadvantages, risks, deviations.**
- The package-map clamp changes what client and server must agree on: an **old unfixed peer** can desync (a Windows
  server built before this branch). With M14's rebuilt script packages the patcher skips a package that is not the
  first-generation file the patch was made for (`UnScriptPatcher.cpp:291-308`, commit `d2d1648`), so the clamp matters for
  the shipped packages that are still patched.
- `UCGameplay.u` is a server package: every peer must run the same file (the rebuilt one is in the asset tree; see 30).
- UC2 returns a pawn to `LastServerMoveLocation` at every `ServerMove` and in the pawn tick (`UnController.cpp:413`,
  `UnLevTic.cpp:401-412`, Epic's "moelfke" code). A server-side teleport of a live player pawn is undone unless the code
  also resets that field (`PlayerController.Restart` does; respawns are fine). **Not tested with teleporters online.**
- **Never exercised:** team games/CTF online, package download (`ChannelDownload`), HTTP redirect download
  (`IpDrv.HTTPDownload` does not exist in UC2's `IpDrv.u`: a harmless server warning), split-screen child connections,
  voice, LAN beacon / server browser, admin login, match end by frag or time limit (travel was tested with
  `servertravel`), more than 3 clients, any real network.
- Observed, then handled elsewhere: after a travel the client logged in with its character's name ("Anubis"), not the
  launcher's (profile/name work, section 30).

**Status.** *Measured*, `tools/dev/net-test.sh`, final fresh-tree run at `b78b396` and the coordinator's on merged main:
dedicated1 8 items, dedicated2 9, listen 8, travel 6, disconnect 6 (including a frozen client dropped after the 30 s
`ConnectionTimeout` and a killed one in 0-3 s by ICMP port-unreachable), `lag` (PktLag=50 PktLoss=5, about 100 ms round
trip) 9, glshot 3, wine 5 (Linux client against the MinGW `ucc.exe`; the 67-line package-map tables of both ends
identical), soak 4 (10 minutes, 2 clients + 2 bots, RSS flat after the first 2 minutes: server 38 MB, clients 83-99 MB),
0 Critical in every log; coordinator at the merge: 46 PASS. *Seen only*: the GL client rendering the other player (name
label, lock-on reticle, scoreboard) in `logs/net/glshot-NetA-sees-NetB.png`. *Not verified*: everything listed under
"never exercised".

**Off switch.** None.

### 40.13 Lost handshake packets and the packet-id wrap (branch `net-loss`)

**What the player/owner sees.** A join no longer hangs for minutes when one handshake packet is lost; after about 4.5
minutes of play on a lossy link scores and other replicated values no longer go silently stale; a handshake that stops
making progress ends after 20 seconds with the normal "connection failed" menu.

**Where.**
- `Source/Engine/Src/UnConn.cpp:33-40` `PacketIdDelta` (signed distance modulo `MAX_PACKETID-1`), `:64-65`
  `OutAckPacketId`/`InPacketId` start at `MAX_PACKETID-2` ("the id before 0", UT2004: -1), `:513` and `:548-565`
  `ReceivedPacket` order check and virtual-NAK loop use it (commit `84aea3e`). Wire format unchanged.
- `Source/Engine/Src/UnPenLev.cpp:32` / `:74` / `:171` / `:642-653` - `UNetPendingLevel::Tick` fails a pending
  connection that got no control message for `InitialConnectTimeout` (20 s, `[IpDrv.TcpNetDriver]`), measured from the
  last message received and checked after the tick's dispatch (commits `84aea3e`, `e3c66ac`).
- Test switches in `UnConn.cpp`/`UnChan.cpp` and `UC2NetTest.cpp`: `NETDROP=`, `NETPKTID=`, `-NETOLDACKS`
  (`UnConn.cpp:18-31, 81-92, 379, 447`; `UnChan.cpp:689`), all under Epic's `DO_ENABLE_NET_TEST` (which is `1`).
- ini/script/frontend: none.
- Tests: `net-test.sh handshake-loss` (hello, challenge, netspeed, login, uses, welcome, join, afterjoin, wrap, wraponly,
  wrapclient), `handshake-compat` (6 cases), `lag`, `soak` with `.views`.

**How the parts fit.** UC2's reliability resends a lost bunch only on a NAK, and a NAK is deduced from an *ack of a later
packet* (`UnConn.cpp:548`). Two defects made some loss unrecoverable: (1) `OutAckPacketId` started at 0 and the first
packet a connection sends has id 0, so packet 0 is never inside a NAK range - if the server's `CHALLENGE` (its packet 0)
is lost nothing resends it, and because the client keeps sending keep-alives and the server keeps acking them neither
side times out: a silent hang (the coordinator's `lag` run: 3.5 minutes of nothing); (2) ids became `_WORD`s that wrap
(`UnConn.cpp:425`) but the comparisons stayed a plain `>`, so after ~16,000 packets an ack of a small post-wrap id is
never "greater" and **no NAK is ever deduced again** (`WELCOME` lost after the wrap = hang; in play, lost property
updates never resent). The fix is local to the side that *sends* the lost packet, so each end's fix covers its own
packets. The give-up is a defence for the case of an unfixed server.

**Why.** Coordinator's evidence `logs/coord/net-lag-hang/`; and "a lost handshake packet must not hang a join (it will
matter on a WebRTC data channel)". Epic's UC2 code (both causes), not the port; on a LAN or Xbox system link loss is rare,
over the internet 5 % loss on the first server packet hangs 1 join in 20.

**Decisions and alternatives.**
- Fix the ack logic the way UT2004 does (signed ids + a "before 0" start) instead of adding a timer resend of packet 0.
- Do not make a transport reliable to hide this (a WebRTC channel made reliable would add head-of-line blocking under the
  engine's own resends).
- The first version of the give-up (measured from the connection start, before dispatch) fired wrongly after a long
  software-GL frame; it is now measured from the last message and after dispatch.

**Disadvantages, risks, deviations.** Each end's fix covers only the packets that end sends. Compatibility matrix
(`handshake-compat`): a fixed server with an unfixed client joins when the server loses its `CHALLENGE` (2.2 s); an unfixed
server with a fixed client does **not** join (the client gives up after 20 s and shows the connection-failed menu); an
unfixed client whose `JOIN` is its packet 0 after a wrap does not join. Both ends of a game must therefore run the build
(they do: one engine, one asset tree). `PktLoss` simulates at `FlushNet`, which equals a wire loss for the sender, but real
links also reorder, duplicate and jitter. `-NETOLDACKS` leaves Epic's defective logic reachable in the shipped binary
(test-only).

**Status.** *Measured*: every handshake-loss case joins in 2.0-2.6 s after the client starts (10/10 items in the coordinator's
run); before the fix CHALLENGE-lost and WELCOME-after-wrap hung; compat 6/6 as the matrix above; `lag` 10/10 with the
final harness, after a first series of 7/10 whose three failures were in the *test* (a client holding the melee weapon
cannot hit at 450 units; a probe taken one second after a kill saw `deaths=0`), plus a PktLoss=20 run 9/9: all 21 runs
joined, none hung; an 8-minute soak at PktLoss=5 PktLag=50 passes the wrap: clients' scores equal the
server's with the fix, and **with `-NETOLDACKS` two clients still showed stale scores 10 s after play stopped** (the old
logic's defect, reproduced); coordinator's full run: 89 PASS, 0 FAIL (`logs/coord/a1-net.log`). *Observed, not chased*: under
simulated loss a player sometimes spawns holding the melee weapon or an empty energy weapon (server and client agree; not
a desync; untested on a real lossy link).

**Off switch.** None (test-only `-NETOLDACKS` restores Epic's logic for one process).

### 40.14 Network test tools and the native servers

**What the owner sees.** A scripted network test that can be re-run with one command; the dedicated and listen servers the
game already had.

**Where.**
- `Source/Engine/Src/UC2NetTest.cpp` (473 lines, ours, in Epic's Engine package; hooked by `UnGame.cpp:447-455`
  (`UGameEngine::Exec` first lines) and `:3528` (`UGameEngine::Tick`)): console commands `NETPROBE <tag>` (`:78`: a summary
  line, one per pawn and per `PlayerReplicationInfo`), `NETPLACE <name> x y z yaw [pitch]` (`:134`), `NETPLACENEAR` (`:198`),
  `NETAIM <name>` (`:248`); the command file `-nettest=<file>` (`:315`); `NETDROP=<spec>[,<spec>]` (`:399-462`);
  `KeepServerMoveSpot` (`:179`: records the placed spot in `LastServerMoveLocation`, otherwise UC2's `ServerMove` rewinds
  the teleported pawn). Commits `634d1b7`, `a4c33f6`, `7bb4b53`, `3047b6e`, `d27dfc6`.
- `tools/dev/net-test.sh` (52 lines) and `tools/dev/net-test.py` (893 lines): checks `dedicated1 dedicated2 handshake-loss
  handshake-compat listen travel disconnect lag glshot soak wine`; one `bin/capped 6G` scope, port 7850 (`NET_PORT` in
  7850-7899), each process on its own Xvfb with its own ini/log (`INI=`, `USERINI=`, `LOG=` are Epic's `appInit` options),
  a virtual pad per client through SDL2Drv's `-inputtest=` channel. Decisions come from log and probe lines only.
  `tools/dev/m12-wine-smoke.sh netjoin` was upgraded from "handshake seen" to "joined and spawned".
- Servers: `ucc-bin server <map>` (Epic's `UServerCommandlet`; `GIsClient` is 0, so **no audio device and no movies**) and
  `uc2.bin <map>?Listen`. Windows: `ucc.exe`/`uc2.exe`. The web has no dedicated server.
- Other tools use these commands: `NETPLACE LocalPlayer` positions the camera for the renderer tests (`m7-effects.sh`).

**How the parts fit.** The commands are plain console commands, so any process (a browser page through `ueExec`, the web
e2e spec) can use them.

**Why.** Network play has no oracle but itself; the owner's rule is that every visible feature has a repeatable test.

**Decisions and alternatives.** Everything compiled into the Engine package (not a test-only library), so web builds and
all tools share the commands.

**Disadvantages, risks, deviations.** The test commands are **reachable by anyone with a console** on a host or server:
`NETPLACE` moves any player's pawn, `NETPROBE` prints state. Harmless for a normal player (the console key was made
available by D-7) but it is cheat-capable surface in a listen server's host. `DO_ENABLE_NET_TEST 1` is Epic's constant, so
the packet-loss hooks are compiled in everywhere. The harness needed several fixes for its own weaknesses (placement
that slid back, melee/empty weapons, a probe taken too early, GL client giving up).

**Status.** The suites above are the status. `wine` is opt-in and needs the Windows run tree.

**Off switch.** Not compiled out. Unused unless a command or `-nettest=` is given.

### 40.15 WebRTC network driver for the browser (E-2)

**What the player/owner sees.** In the browser, "Host a game" in the launcher starts a listen server; "Copy invite link"
gives a link; a friend who opens it joins the match. Both play (spawn, move, shoot, score) and leave cleanly.

**Where.**
- New code, `Source/WebRTCDrv/` (web build only: its `CMakeLists.txt` returns early off Emscripten; commits `941326a`,
  `738b6e9`): `Inc/WebRTCDrv.h` (116 lines, the frozen C ABI and the two classes), `Src/WebRTCNetDriver.cpp` (340 lines,
  `UWebRTCNetDriver : UNetDriver`: `InitConnect :82`, `InitListen :120`, `TickDispatch :147`, `LogStats :244`,
  `PushListenStatus :276`, `LowLevelDestroy :315`, `NormalizeConfigDefaults :47`), `Src/WebRTCConnection.cpp` (93 lines,
  `UWebRTCConnection : UNetConnection`, 512-byte packets), `Src/WebRTCDrv.cpp`. Ported from engine-ut2004's driver
  (603 + 113 + 183 lines); the in-game server-browser file and the deferred-connect export were **not** ported.
- Epic's code changed: `Source/Engine/Src/UnGame.cpp:1426-1445` (`UGameEngine::Browse`, `#ifdef __EMSCRIPTEN__`): a LOCAL
  URL that carries the option `connect` and a map is the frontend's join key `<room>_<serverSlug>`; the URL's Host becomes
  that key, the map `FURL::DefaultMap`, and the `connect` option is removed (a partial travel copies every option, so
  leaving it would make a later real map name a host). Web-only; native builds do not compile it.
- Launcher/build: `Source/SDLLaunch/Src/SDLLaunch.cpp:50-51, 350-359, 527-528` (registers the classes; `ReplaceMissingDrivers`
  switches an `Avaris.ini` saved by an older web build from `IpDrv.TcpNetDriver` to the WebRTC driver);
  `Source/SDLLaunch/CMakeLists.txt:40-43`; `Source/SDLLaunch/cmake/Web.cmake:106-115` adds `--js-library
  vendor/glue/webrtc-shim.js`; `vendor/glue/webrtc-shim.js` is the frontend's ABI-1 shim (byte-identical to
  engine-ut2004's, commit `547bd73`); root `CMakeLists.txt:412` lists the package.
- ini: web `Engine/Config/web/Default.ini:25` `NetworkDevice=WebRTCDrv.WebRTCNetDriver`, `:153-164`
  `[WebRTCDrv.WebRTCNetDriver]` (`AllowDownloads=True`, `ConnectionTimeout=60.0` (native 30), `InitialConnectTimeout=300.0`
  (native 20: a guest streaming the map's files the first time sends nothing while it loads), `KeepAliveTime=0.2`,
  `MaxClientRate=20000`, `RelevantTimeout=5.0`, `SpawnPrioritySeconds=1.0`, `ServerTravelPause=4.0`, tick rates 60,
  `DownloadManagers=Engine.ChannelDownload`); generated by `tools/dev/sync-web-ini.py` (`DELTAS` + `WEB_SECTIONS`);
  README rows `Engine/Config/README.md:76-77`. The driver substitutes the web values for any config property that is not
  positive (an old `Avaris.ini` without the section would otherwise leave `ConnectionTimeout=0`, `RelevantTimeout=0`...).
- Script: none. Frontend: `targets/uc2` `network` block and notices, `uc2.profile.js` milestones,
  `uc2-wasm-net.spec.js` (40.16, 40.19). Test script: `tools/dev/e2-web-net.sh`.

**How the parts fit.** Division of labour: the engine's driver owns the *engine* connection (`UNetDriver`/`UNetConnection`: one
engine packet per data-channel message); the frontend's glue owns signalling (the PeerServer), WebRTC peers and data
channels; `webrtc-shim.js` forwards the nine `webrtc_*` calls the driver makes to the glue (send/receive through shared
ring buffers on the engine's worker, the rest proxied to the main thread where `RTCPeerConnection` lives).
- *Host:* `InitListen` builds `"<room>/<server name>"` (room from the `room=` option; server name from `?ServerName=`, else the
  **config** value `[Engine.GameReplicationInfo] ServerName`, because UC2's `PlayerProfile.SetServerName`
  (`Engine/Classes/PlayerProfile.uc:764`) overwrites the *class default* with the profile's name ("Liandri Corporation") at
  start-up and never writes the ini) and calls `webrtc_init_listen`; the glue registers the peer
  `uc2-<room>_<serverSlug>-srv`. Each tick it accepts pipes (an `ACCEPTC_Ignore` pipe is parked and offered again, not
  refused, so a guest does not livelock during a travel), pumps packets, turns a pipe the glue reports closed into
  `USOCK_Closed`, and about once a second pushes name, map, players and game type for the glue's status replies.
- *Guest:* the frontend starts the engine on `<room>_<serverSlug>?connect`; `Browse` turns it into a network URL;
  `InitConnect` dials `uc2-<room>_<serverSlug>-srv`. The client has exactly one transport slot, so a server travel
  (a second `InitConnect` while the old level's driver still ticks) detaches the old connection instead of closing it.
- *UC2 differences from the UT2004 driver (each for a UC2 reason):* `UpdateLastReceiveTime()` on every received packet
  (UC2's "standby cheat" check fires `NotifyStandbyHack` after 1 s of silence otherwise); no per-tick `emscripten_sleep`
  (the engine has run on a worker since M4w; the template's sleep would cap a lone listen server at 10 fps);
  `PreServerTravel(){}` as `UTcpNetDriver`; the listen status from `Notify->NotifyGetLevel()`; peer prefix `uc2`.
- *Without a layer:* without the `.ini` section the driver substitutes defaults; without the frontend (no `Module.UE`) the
  shim's calls throw `[ue-abi] Module.UE.net not injected`; natively none of this is compiled.

**Why.** PLAN Phase E-2 / owner decision (2026-10-04): "Net: WebRTCDrv ported; ProxyNetDrv not." and "P2P only (the owner's
choice for uc2)".

**Decisions and alternatives.** Closing the driver's connection when the pipe closes (as ICMP does natively; otherwise only the
timeout ends it). No in-game server browser: UC2's own System Link browser is `UdpBeacon`/`ClientBeaconReceiver` over UDP,
which a browser cannot send; mapping beacons to the glue's server rows is "a task of its own". No proxy mode.

**Disadvantages, risks, deviations.**
- **A guest that vanishes (tab closed, crash) pauses the host's game.** UC2's standby check (Epic's Xbox anti-cheat: no packet
  from an in-game client for 1 s) calls `SetPause`; the host is dropped only at `ConnectionTimeout` (60 s) or after the glue's
  90 s reconnect grace, whichever is first. A guest that leaves through the game is dropped at once (the engine's channel
  close). Natively the closed socket's ICMP drops it quickly. Left as Epic's behaviour; recorded as an owner note.
- The data channel the glue opens is **reliable and ordered** (`[PIPE] rtc=ordered=true/maxRetx=null`), not the unordered
  channel `webrtc.md` and `WebRTCDrv.h` describe: while the pipe is up a datagram is never lost, loss happens only while a pipe is
  dead and the session layer reconnects. The engine's resends run on top of that (the wrap and packet-0 fixes of 40.13 apply).
  Head-of-line blocking under bad networks is therefore possible; never measured on a real connection.
  *(Superseded (2026-10-09/10): see section 60, 40.15.)*
- **Not done:** in-game discovery; **two guests at once** (the third page costs ~1.6 GB of browser memory; the spec has no
  such test); servertravel / map change over WebRTC (the one-slot detach logic is the template's and passes natively, but
  was not run in a browser); hosting from UC2's own in-game network menus (no `room=` then; the glue uses the persisted
  room); a host that vanishes (guest side); package download over WebRTC (every peer has the full tree; `AllowDownloads=True`
  with `Engine.ChannelDownload`); `NETDROP`/`NETPKTID` in the browser (they pass natively).
- A browser has no UDP: the host logs `UdpBeacon ... BindPort: socket failed` (UC2 spawns its system-link beacon on a
  listen server); harmless.
- Two Chromium instances need ~3.1 GB PSS and a 5.7 GB cgroup; the net spec needs a 16 GB machine budget.
- STUN is the launcher's default (`stun.l.google.com`) and the broker the default PeerServer (`unrealpeer-we.azurewebsites.net`);
  two browsers behind strict NATs need a TURN server (launcher Advanced panel). **Not tried across a real NAT.**

**Status.** *Measured*, `uc2-wasm-net.spec.js` (2 tests; passes recorded in `webrtc.md` steps 3, 4, 6 and in the coordinator's
run of 2026-10-08): the host's `[WebRTC] InitListen: room ... server name ...` and the registered peer id; the guest's
join-key browse, pipe `FULLY OPEN`, `Welcomed by server: LEVEL=DM-Deadbolt`, level up, host `Join succeeded`; both spawn by a
real click held 1 s (the console `Fire` spawns nobody) and the match starts; the guest holds W and the *host* sees its pawn
move (651-901 units), rest positions agree to 0.0-0.4 units; the guest's `Suicide` counts on the host and replicates back;
`disconnect` removes the guest's PRI on the host in under one probe (2.6 s); under `UC2_E2E_NET_SIM='PktLoss=5&PktLag=50'`
on both ends: measured loss 3-8 % in, lag 166-183 ms, walk 668 units, agreement 0.0, but the leave took the host's 60 s
timeout (the simulation's delay queue held the close); the launcher-form flow on a fresh install (server name generated
`Another Unreal Server - c12abc`, room `default-public-room`, the engine listens under exactly those). On localhost the link
carries ~28-30 packets/s each way at 0.5-1 KB/s, 0 % loss, 38-44 ms lag. Native regressions after the branch: `net-test.sh`
dedicated1/dedicated2/listen/travel 32 PASS. Browser boot 4/4. *Not verified*: everything under "not done", real-world links.

**Off switch.** None for the driver itself: an ini that names `IpDrv.TcpNetDriver` is switched back to the WebRTC driver at launch
(`ReplaceMissingDrivers`). Nothing goes online unless the player hosts or opens an invite link.

---

## The frontend (`ufrontend`, branch `uc2`, 42 commits `7fb637e..4ac0d6d`)

The frontend is a separate repository (the shared launcher, build and test shell for UT2004, UT99, Unreal 1 and now UC2).
Everything below is on the local branch `uc2`; nothing was pushed. 42 files changed, +4,075 / -34 lines. Of those, 9 files
are the new `targets/uc2/` (data), 6 are new e2e files, and **the other 27 touch shared code, shared tests or shared docs, listed in
40.17, 40.18 and 40.20 and again in the closing list.** Input and profile features (D-3/D-4 name ownership, the saves and
keys specs) are covered by section 30; here each gets one line.

### 40.16 The `uc2` target: descriptor, manifests, branding, precache

**What the player/owner sees.** A fourth game card in the launcher ("UC2 Web (experimental)", chip `v2226`), the Play panel with
DeathMatch and Capture the Flag, 3 bots / 10 minutes toggles, a Multiplayer panel with P2P only, Advanced fields for the
audio buffer, and a sound/graphics capability check.

**Where.** `targets/uc2/` (all new; first commit `b244955`, later `f49e003`, `f90b70f`, `05862c4`, `9a1c80a`, `d23794f`, `6f2a302`,
`477eca5`, `40216a2`, `6893b45`, `f6ae5b2`, `4ac0d6d`):
- `target.json` (182 lines): `id uc2`, `engineFamily UE2`, `abiVersion 1`; `publish` (folder `uc2`, chip `v2226`, `chipWarn: true`,
  engine repo `engine-uc2` branch `main`, preset dir `.`); `assetMarkers` (SHA-256 of `System/Core.u`, 72,051 bytes, package version
  151; required files; forbidden `XGame.u`/`BotPack.u` so another game's tree is refused); `storage` (`cacheBudgetMb 2048`,
  `memfsBudgetMb 1024`); `network` (peer prefix `uc2`, public room, `serverName` read from `Avaris.ini [Engine.GameReplicationInfo]
  ServerName` with stock `Another Unreal Server`, `modes` P2P only, `networkDevice`, `playerName` with `ownedBy`);
  `manifestExclude`; `userFiles ["System/Profiles/*.PRO"]`; `defaults` (map `DM-Deadbolt`, game `UCGameplay.UCDeathmatch`, engine
  options `-log`); `videoDrivers` (GL4ES default, GL4ES without post-processing, null); `travel` (DeathMatch, CTF; `3 bots`,
  `10 min`; no presets, no mutators); `capabilities` (required: secure context, cross-origin isolation, SharedArrayBuffer, wasm
  threads and features, workers, `Atomics.wait`, OffscreenCanvas + WebGL on it, OPFS sync access; recommended: GPU
  acceleration, IndexedDB, `webAudio`, `webrtc`, `pointerLock`); `audioBuffer {10 ms x 3}`; `notices` (join-failed, join-refused,
  version-mismatch, host-failed, regexes on the engine's own lines); `telemetry` (`LoadMap:` and `Critical:` patterns).
- `file-manifest.json` (1,224 entries, 1,991 MB at `4ac0d6d`) and `dependency-manifest.json` (208 packages, 1,804 import
  references, all resolved), both **generated** from the owner's local asset tree with `generate-manifest.cjs` and
  `generate-deps.cjs` (commands in the README).
- `precache.json`: 17 `System/*.u` packages + `Maps/Entry.unr` + `Maps/Menu.unr` (10.5 MB).
- `branding/{theme.css,logo.svg,help.md,portal-notes.json}`: neutral placeholders that imitate no game's or company's
  branding.
- `README.md` (173 lines).

**How the parts fit.** The shared frontend reads the descriptor and builds the page, the file system manifest, the `-ini:` seeds
(player name, server name, network device) and the engine command line; the engine reads the manifest-backed files on demand.
The manifest has to match the asset tree the artifact is served with: after D-4, D-6, saveconfig and D-9 each rebuilt `GUI.u`,
`UCGameplay.u` and `UCMenu.u`, a frontend commit regenerated the manifests (`477eca5`, `40216a2`, `6893b45`, `f6ae5b2`).

**Why.** PLAN M11: "frontend `targets/uc2/*`, v151 support in generate-deps/manifest/travel, e2e boot/net specs, gates".

**Decisions and alternatives.** P2P only (owner's choice). Travel limited to the two game classes proven on this build (the CTF toggle
is labelled "not yet verified on the web build"). The cooked sound and movies are in the manifest (asset tree v1) so they are
served like packages; their by-products are excluded. Player files (`System/Profiles/**`, `*.PRO`, `Avaris.ini`, `User.ini`) are in
`manifestExclude` so the site never serves one player's saves; `userFiles` makes the browser keep them.

**Disadvantages, risks, deviations.**
- **The manifests are generated from files that are not in any repository** (`assets/uc2` is never committed). Anyone without that
  tree cannot regenerate them or build the target; a rebuilt `.u` needs manifest, dependency manifest and engine to move together.
- **User-facing text is out of date**: `branding/help.md` still says "null render device and null audio ... no sound ... no
  multiplayer yet" and `branding/portal-notes.json` says "Not playable yet. The web engine has no render, audio or network device,
  so the screen stays empty." and "must not be published as playable". The engine now renders, plays sound and plays online. The
  portal-notes file feeds the card on the published portal page (its own comment says so) and `help.md` is the target's help.
- `README.md` still has the first-light statements (593 files / 1,094.7 MB; "what the engine needs next: ... a GL device, audio, a
  net driver"); the root `README.md` row says "boots to the menu with no rendering".
- **Sound and movies are not in `precache.json`** ("not added yet" in the README): the cue tables, menu music and logos load on demand
  (3.1-3.6 s to first sound on a busy machine).
- Total manifest 1,991 MB (1,899 MiB) against a `cacheBudgetMb` of 2048: a player who touches everything stays just below the cache
  budget, with no headroom for a second version of the assets.
- The CTF travel entry and the `3 bots`/`10 min` toggles were not run through the launcher in a spec (the net spec drives
  `?map=DM-Deadbolt&Game=UCGameplay.UCDeathmatch`).
- No language option for sound and movies: the game language comes from `[Engine.Engine] Language` in `Avaris.ini` (`int` by default);
  PLAN E-4 (a language option for every engine) is not done, so a web player cannot pick German voices from the launcher.
- Chrome-based browsers only were exercised.

**Status.** *Measured*: `yarn test` 28/28 and `yarn gates` "all CI-tier gates green" at the E-2 commits (`webrtc.md` step 3); the boot,
audio and net specs below. *Not verified*: other browsers; a clean-machine build (the owner's preview is built on the coordinator's machine).

**Off switch.** Not applicable (a target is data); remove `targets/uc2` and its registrations to drop it.

### 40.17 Package versions 145-151 in the shared glue (affects every target)

**What the player/owner sees.** Nothing; it lets the build tools read UC2's `.u/.unr/.utx` headers so the dependency prefetch and the
manifest work.

**Where.** `packages/glue/lib/unreal/reader.cjs` (+99): new `tableLayout(version)`, `VER_INT_INDEX 145`, `VER_SHORT_RECORDS 150`; from 145 every
compact index is a plain i32, from 150 the import record's package index is an i16 and the export record starts with a 10-byte head of five
i16 (class, super, package are words 0, 2, 4 on load); `Cur.i16()`, `tableEnds`. `packages/glue/bin/generate-deps.cjs` (+39) uses the same
`tableLayout`. `packages/glue/lib/unreal/property-codec.cjs` (+26): `assertCodecVersion` makes the tagged-property decoder **throw** on a
version >= 145 ("not supported here yet"). `writer.cjs` (+7, comment: it only writes v128). Tests `packages/glue/test/unreal-package-versions.test.cjs`
(350 lines, 11 tests, synthesised v128/v147/v151 through both readers plus the real tree when `.ufront/publish.local.json` names `assets.uc2`),
`_target-fixtures.cjs`, `target-config.test.cjs`. Commit `2f4e39d` (2026-10-04). Docs: glue `CHANGELOG.md`, `README.md`.

**How the parts fit.** Engine-side counterpart: UC2's `UnLinker.cpp`/`UnObj.cpp` (`FCompactIndex<<`) define the layouts. The two glue
readers (build-time `generate-deps`, run-time `reader.cjs` for generators) share one function so they cannot disagree.

**Why.** Without it `yarn deps` cannot read a version-151 package (PLAN: "Frontend Node tools must learn package v151").

**Decisions and alternatives.** One place decides the layout; the tagged-property codec was *not* extended (it refuses instead of decoding
plausible garbage; the v148+ tag layout is not implemented), so the QuickScreenshots generator does not work for uc2.

**Disadvantages, risks, deviations.** **Shared with the other targets.** Below version 145 the code path is the old one; the CHANGELOG states that
on the four older targets' trees the reader's tables (SHA-256 over every package) and every dependency manifest are byte-identical before and
after, and `packages/e2e/tools/deps-validate.mjs` passes unchanged. Not re-run for this report (no tests are run here); it rests on the commit's record.
`generate-manifest`'s GUID reader needed no change.

**Status.** *Measured* (commit record): all 208 packages of the UC2 tree end exactly where their own tables and export extents say, every
import chain is rooted in a `Core.Package`, all 1,804 import references resolve; unit tests pass.

**Off switch.** None.

### 40.18 Shared frontend code added for UC2 (name ownership, user files): one line each, details in section 30

- **`network.playerName.ownedBy` (D-4)**, commit `52e993d` (+ target data `9a1c80a`, `d23794f`): the launcher's name field is disabled while the game's
  own identity names the player (UC2: `User.ini [Profiles] LastProfile`). **Shared code:** `packages/frontend/src/components/NetworkPanel.vue`,
  `ProxyNetworkPanel.vue`, `stores/net.ts` (`nameOwner`, `resolveNameOwner`, `nameOwnerHint`), `config/network.ts`, `env.d.ts`;
  `packages/glue/src/config/ini-identity.js` (`readPersistedNameOwner`) and `src/index.js`; docs `docs/06-targets-and-branding.md`,
  `packages/frontend/README.md`, `packages/glue/CHANGELOG.md`; tests `config-name-owner.test.ts` (107 lines), `ini-identity.test.cjs` (+93). A target
  without `ownedBy` never disables the field (UT2004, UT99, Unreal 1 are unaffected by design). Section 30 explains D-4.
- **Player name through `-ini:`, profiles as user files, player files never served (D-3/C-2)**, commit `f90b70f`: target data only.
- **Saves spec and keys spec** (`uc2-wasm-saves.spec.js`, `uc2-wasm-keys.spec.js`, opt-in, 542 and 255 lines): section 30.

### 40.19 The uc2 end-to-end specs

**What the owner sees.** Playwright specs that drive the real engine in Chromium and print evidence lines.

**Where.** `packages/e2e/` (all under the memory guard `tools/e2e-mem-guard.mjs`; run through `tools/dev/e2-web-net.sh` in the engine repo, which also
proves `serve-web/` holds the artifact under test and mutes the output with a PulseAudio null sink):
- `lib/engines/uc2.profile.js` (109 lines): the engine-specific data the shared harness needs: boot, render, host and client milestone regexes
  (the engine's log lines, which come WITHOUT the `Log: ` tag with `-log`), `bootSurface`, `netSurface`, `dmGameArgs`.
- `uc2-wasm-boot.spec.js` (785 lines, 4 tests, in the default set): menu with the GL device; `DM-Deadbolt` by URL with post-processing; keyboard + mouse
  play test (INSTANT ACTION, Fire, W, mouse look); mouse look linear (D-6). It launches with `-nostartupmovies` (`17758ef`), so it never sees the logos.
- `uc2-wasm-audio.spec.js` (392 lines, 3 tests, **opt-in**, commits `03858a5`, `721c6dd`): (1) the menu plays its music through openal-soft on Web Audio
  and the output is not silent (log lines for the device, the 23 cue tables, a wave pack, the music; the `AudioContext` `running`; an `AnalyserNode` on
  the worklet node: audible share > 0.5); (2) the logos play with their sound and on time (`-NULLMOVIES`); (3) a key press ends the logos and the music
  follows. It builds its own browser context with `--mute-audio` (the suite's fixture ignores `launchOptions`; the first run proved the flag had no effect:
  only `PULSE_SINK` kept it off the speakers).
- `uc2-wasm-net.spec.js` (587 lines, 2 tests, in the config's list): see 40.15.
- `uc2-wasm-saves.spec.js`, `uc2-wasm-keys.spec.js`: section 30.
- `playwright.config.js`: adds `<target>-wasm-audio`, `-saves`, `-keys` to the `testMatch` list (shared file; harmless for other targets, which have no such specs).
- `README.md` (+39): the uc2 profile, boot spec and net spec.

**How the parts fit.** The specs read the engine's log lines: the engine and the profile regexes must change together.

**Why.** PLAN M11 gate "uc2 E2E green".

**Disadvantages, risks, deviations.** Not runnable without the local engine artifact and asset tree; hard-coded `localhost:8080` and server ports (the
runner refuses to start when another guard or port is busy); **log text is the interface** (a reworded engine line fails a spec); the audio check
is "not silent", not "correct"; the audio spec needs the cooked files in the served tree (README); several checks are timing- or load-sensitive (an
earlier audio-spec attempt was stopped by the memory guard at 12.1 GB while other work ran; `LOG` records reruns); the net spec asserts nothing about
two guests, travel, or a real network; browser runs measure memory from outside the page and need a 16 GB budget.

**Status.** *Measured* (coordinator, 2026-10-08, `logs/coord/a13/summary.txt`): boot 4/4, net 2/2, audio 3/3; saves and keys passed on a rerun after the
memory guard stopped the first attempt (saves reported a browser profile outliving the test: a teardown complaint).

**Off switch.** The audio, saves and keys specs are opt-in (named explicitly).

### 40.20 Docs and CI-gate edits

**Where (shared files).** `.ufront/gates/no-engine-specifics.sh` (6 lines): the `uc2` literal is now forbidden in generic glue/frontend source, like `ut2004`,
`ut99`, `unreal1` (commit `b244955`); `README.md` (one table row); `docs/06-targets-and-branding.md` (the `ownedBy` field); `docs/migration/STATE.md`
(a 2026-10-04 "NEW TARGET uc2 (first light)" entry); `test/skeleton.test.mjs` (adds `uc2` to the manifest-vs-engine-`Paths=` check and two captured
console lines); `packages/frontend/test/capabilities.test.ts` (adds `uc2`).

**Disadvantages, risks, deviations.** `STATE.md` and the root `README.md` describe the first-light state only; later work (render, sound, online) is
in `targets/uc2/README.md` and the e2e README but not in the migration state log. The gate change is a new restriction on generic code, correct but
a shared CI behaviour.

---

## Open issues in this area

Ordered by what most affects a player or a decision of the owner.

**Not verified by a person, only measured**
1. Nobody has listened to the audio against a real Xbox. Mix level (+/- a dB or two: OpenAL Soft's centred gain is -1.10 / -1.52 dB, XACT's
   routing unknown), HRTF colour (not reproduced) and the **zone reverb level** (parameters carried 1:1, decay times measured, loudness not
   calibrated) are open. Captures were sent to the owner (LOG 2026-10-04); no verdict is recorded.
2. The movie **picture in a real browser** has no assertion (the boot spec disables the logos; the audio spec checks sound and time only). The native
   ES 2 twin with `LIBGL_NOBGRA=1` matched frame for frame, which is the nearest evidence.
3. The cause of the owner's original "no sound, no movies, errors in the console" on the native build was never identified (the question to the
   owner in `PLAN.md` is unanswered); the web build, which had neither before O-2, is the likely source.
4. Web audio latency (input to ear) and long-session stability were never measured; only Chromium was used.

**Content and data**
5. 19 source WAVs missing, 15 cues silent; whether to decode them from the shipped `.xwb` is an open owner decision (`BLOCKERS.md`).
6. Localised Sapphire taunts (and one Spanish tutorial line) play the wrong line, by decision (40.8). Movie language tracks 1-4 rest on Epic's
   code with no reference recording.
7. Inferred XACT semantics: priority direction, "Recurring = fade", unset-weight default, cue radius 4 for sounds with no 3D block (other players'
   non-3D cues are culled beyond 4 units).

**Features not done**
8. Animated loading movie, attract mode (`UC2_Attract`, `A51`), the Xbox legal and rating screens.
9. Never run: voice stealing, split-screen listener placement, pause/resume, three playlist types, outro cinematics, a `MovieMaterial` with sound
   (the menu's "View movies"), non-English movie tracks in game.
10. In-game server discovery on the web (UC2's System Link is UDP broadcast), two guests, travel over WebRTC, hosting from the in-game
    network menus, package download over WebRTC, any real network, a NAT without TURN.
11. Native network: team games/CTF online, package download, HTTP redirect download (class missing), split-screen connections, voice, LAN
    beacon, admin, match end by limit, more than 3 clients.

**Risks that can bite**
12. A guest that closes the tab **pauses the host's game for up to 60 s** (UC2's standby check; Epic's behaviour; a host-side mitigation such as
    disabling the check for the WebRTC driver was not made).
13. A teleporter moving a live player pawn online may be undone by UC2's `ServerMove` rewind (`LastServerMoveLocation`); only respawns and the
    test placement command were verified.
14. The packet-ack and package-map fixes need **both** ends on the build; an old peer can hang or desync.
15. OpenAL Soft is fetched by branch (`ufront/web`), not by commit, from GitHub at configure time; the 32-bit native game falls back from PipeWire
    to PulseAudio until `libpipewire-0.3-modules:i386` is installed; real Windows output (WASAPI) untested.
16. Music is decoded on the game thread (16 ms hitch in the browser every ~0.74 s); wave packs are read whole and stay resident.
17. Test commands (`NETPLACE`, ...) and `-NETOLDACKS` are compiled into the shipped Engine and reachable from the console.
18. The WebRTC data channel is reliable/ordered while the driver's header and notebook say unordered; head-of-line blocking on a bad network is
    unmeasured.

**Frontend**
19. `branding/help.md`, `branding/portal-notes.json`, `targets/uc2/README.md` header, the root `README.md` row and `STATE.md` still describe the
    first-light build (no rendering, no audio, no network). `portal-notes.json` says "must not be published as playable".
20. Manifests (`file-manifest.json`, `dependency-manifest.json`) come from a local asset tree that is in no repository; they must be regenerated
    whenever a rebuilt `.u` changes; `precache.json` has no sound or movie file; 1,991 MB of manifest against a 2,048 MB cache budget; no
    language option (E-4).
21. The e2e specs depend on the engine's log text and on a local artifact, and are not in a CI run.

## Places where we changed Epic's shared code

Engine, Core, IpDrv, UCC, launchers (file: why). Line numbers are the snapshot's.

| File | Why |
|---|---|
| `Source/Engine/Inc/UnAudio.h:115` | one virtual `OpenMovieAudio` (default NULL): a movie's sound goes through the game's audio device |
| `Source/Engine/Src/UnEngine.cpp:651-653` | a full-screen movie owns the input (`appMovieInputEvent`) |
| `Source/Engine/Src/UnGame.cpp:24, 1821-1884, 2998-3009` | Xbox-only movie hooks lifted through `WITH_PORTABLE_MOVIES` (gate lifted); `ResetRenderStates` guarded against no level / no device |
| `Source/Engine/Src/UnGame.cpp:460-466` | the Xbox client's `SUBTITLES` command (gate lifted) |
| `Source/Engine/Src/UnGame.cpp:447-455, 3528` | network test commands and `-nettest=` file (debug hook, quiet unless used) |
| `Source/Engine/Src/UnGame.cpp:1426-1445` | web only: `<key>?connect` is a WebRTC join key, browsed as a network URL |
| `Source/Engine/Src/UnScript.cpp:5131` | `ALevelInfo::NotifyPauseChange` lost `FORCEINLINE` (link fix; called from `UnActor.cpp:1362`) |
| `Source/Engine/Src/UnMaterial.cpp:1134-1319` | `INT` -> `PTRINT` for `MovieMaterial` pointer fields (64-bit) |
| `Source/Engine/Src/UnScriptPatcher.cpp:143-155` | bug fixed: `BeginLoad/EndLoad` around `AppendImport` (client join); `:291-308` rebuilt packages skip the patch (M14) |
| `Source/Core/Src/UnCoreNet.cpp:276-292, 304-305` | bug fixed: package map counts only the file's last generation; DevNet table line revived |
| `Source/Engine/Src/UnConn.cpp:14-40, 64-65, 164-179, 513, 548-565` | bug fixed: wrap-aware ack/NAK, packet 0 NAK-able, channels destroyed not deleted during GC |
| `Source/Engine/Src/UnConn.cpp:18-31, 81-92, 379, 447`, `UnChan.cpp:13, 689` | test hooks `NETOLDACKS`, `NETPKTID`, `NETDROP` |
| `Source/Engine/Src/UnChan.cpp:179-190` | bug fixed: `RouteDestroy` marks the channel destroyed (exit-time "failed to route Destroy") |
| `Source/Engine/Src/UnChan.cpp:1008-1009, 1440-1451` | DevNet diagnostics (what a bunch held; unmapped actors) |
| `Source/Engine/Src/UnPenLev.cpp:32, 74, 171, 642-653` | behaviour added: a stalled handshake fails after `InitialConnectTimeout` (measured from the last message) |
| `Source/UCC/Src/UCC.cpp:721` | bug fixed: commandlet rooted while it runs (use-after-free in a server's `LoadMap` GC) |
| `Source/Core/Src/UnUnix.cpp` (5 sites) | port bug: bounded writes into `GMachineCPU[32]` |
| `Source/Core/Inc/UnVcWin32.h:792`, `UnGnuG.h`, new `UnMovieBase.h` | `FMovieThread`/`FMoviePlayer` moved unchanged to one header |
| `Source/IpDrv/Inc/IpDrvClasses.h`, `UnIpDrv.h`, `UnSocket.h`, `Src/UnSocket.cpp`, `MasterServerClient.cpp`, `MasterServerUplink.cpp` | 64-bit (`PTRINT`, `GCC_PACK`), MinGW/WinSock (`_WIN32`), Emscripten errno guard, GCC qualification: no behaviour change; `TcpNetDriver.cpp` is untouched |
| `Engine/Config/Default.ini`, `web/Default.ini` | `AudioDevice`, `[UC2ALAudio...]`, `[UC2Movie]`, `NetworkDevice`, `[WebRTCDrv.WebRTCNetDriver]` |
| `Source/SDLLaunch/Src/SDLLaunch.cpp:50-54, 350-359, 521-528, 599-604` | registrants (audio, movie, WebRTC); network-device switch on the web; start-up logos |
| `Source/SDLLaunch/cmake/Web.cmake:79-115`, root `CMakeLists.txt:226-354`, `Source/UCC/CMakeLists.txt:19-33` | OpenAL Soft (fetched), Ogg/Vorbis/Theora, thread pool 4, WebRTC shim, malloc export |

Frontend files that other targets also use (UT2004, UT99, Unreal 1 build from the same packages):

| File | Change |
|---|---|
| `packages/glue/lib/unreal/reader.cjs`, `bin/generate-deps.cjs` | package versions 145-151 (`tableLayout`); unchanged output below 145 per the CHANGELOG |
| `packages/glue/lib/unreal/property-codec.cjs`, `writer.cjs` | codec refuses >= 145 (`assertCodecVersion`); comment |
| `packages/frontend/src/components/NetworkPanel.vue`, `ProxyNetworkPanel.vue`, `stores/net.ts`, `config/network.ts`, `env.d.ts`; `packages/glue/src/config/ini-identity.js`, `src/index.js` | optional `network.playerName.ownedBy`: the name field is disabled while the game names the player; inert for targets that do not declare it |
| `.ufront/gates/no-engine-specifics.sh` | the literal `uc2` is forbidden in generic source (CI gate) |
| `packages/e2e/playwright.config.js` | three opt-in spec names in `testMatch` |
| `test/skeleton.test.mjs`, `packages/frontend/test/capabilities.test.ts`, `packages/glue/test/_target-fixtures.cjs`, `target-config.test.cjs`, `ini-identity.test.cjs`, `unreal-package-versions.test.cjs`, `config-name-owner.test.ts` | tests cover uc2 or the new functions |
| `README.md`, `docs/06-targets-and-branding.md`, `docs/migration/STATE.md`, `packages/*/README.md`, `packages/glue/CHANGELOG.md`, `packages/e2e/README.md` | documentation |


---

# 50. Third-person character jitter (O-11), merged after the snapshot

Merged into main as `c59c02d` (branch `jitter`: `5f09fb4`, `c46bb34`, `51d1ffc`, `cd4f20a`, `f874f28`). Notebook:
`docs/porting/analysis/jitter.md`. Line numbers below are main at `c59c02d`.

Summary:
- The jitter you reported ("the camera moves forward, but the character stays still in place ... and then it
  updates") was real and came from our own change D-6 (the mouse's rigid camera, section 30), not from the renderer
  or the web build.
- The fix is three small script changes in UCGameplay.u: the frame is now drawn from where the pawn is drawn.
  Nothing is smoothed and the pad's camera is not touched.
- The C++ side only adds test tooling: a per-frame trace (`FRAMETRACE`) and repeatable uneven frame times
  (`-framepattern=`).
- What is left on screen is the camera's walk bob (Epic's `UCPawn.CheckBob`), a smooth sway of about ±8 px at 1080p.

### The mouse camera is drawn from where the pawn is drawn

**What you see.** With the mouse owning the look, in third person, your character stays fixed on screen while it
runs, at any frame rate and with uneven browser frame times. Before the fix it lurched ahead after a long frame and
dropped back after a short one.

**Where.**
- Script, `Source/UCGameplay/Classes/UCPlayerController.uc` (package UCGameplay.u, rebuilt):
  - `MouseLookCamera` (~3067): stamps `MouseCameraTime = Level.TimeSeconds` whenever it places the rigid camera.
  - new `MouseCameraLeftBehind()` (~3081): true when this frame's camera was the mouse's, the pawn is the view
    target, the mouse owns the look, and the pawn has moved since the view was computed
    (`Pawn.Location != OldPawnLocation`).
  - `PlayerCalcView` (~3162): one more condition in Epic's "compute the view again" test: `|| MouseCameraLeftBehind()`.
- Script, `Source/UCGameplay/Classes/UCPlayerInput.uc:97`: `var transient float MouseCameraTime`. UCPlayerInput
  has no native C++ class, so no struct layout changes.
- Engine (test tooling only, quiet by default):
  - `Source/Engine/Src/UC2FrameTrace.cpp` (new): console command `FRAMETRACE <frames> [tag]` and the switch
    `-frametrace=`. They log one line per drawn frame: the camera used, the pawn's location and velocity, the
    frame time, and the pawn's projected screen position.
  - `Source/Engine/Src/UnGame.cpp` (+8): the hooks for it in `UGameEngine` Exec and Draw.
  - `Source/SDLLaunch/Src/SDLLaunch.cpp` (~232-263): `-framepattern=<ms>:<ms>:...`, which plays back a fixed
    sequence of frame lengths, so a browser's uneven pacing can be repeated natively.
- Tools:
  - `tools/dev/jit-trace.sh`: a native run (null renderer, 1920x1080, DM-Deadbolt at a fixed spot) in four
    1.3 s segments (forward, back, strafe right, strafe left), for keyboard+mouse or pad, at 60 fps, uncapped or
    uneven.
  - `jit-analyze.py`: computes judder (each frame's distance from the line between its two neighbours) and the
    slope `b` (how far ahead the pawn sits, plotted against velocity × frame time).
  - `jit-strip.sh` / `jit-strip.py`: consecutive real GPU frames of the pawn, before and after.
  - `tools/dev/script-build.sh` now also accepts `jit*` trees.
- Frontend: `packages/e2e/uc2-wasm-jitter.spec.js` (ufrontend `aeaacc4`). It is opt-in, has a line in
  `playwright.config.js`, and makes the same measurement in the browser from the FRAMETRACE lines.

**How the parts fit.**
- The whole fix is in UCGameplay.u. Native code, data and the frontend are unchanged by it.
- Without the rebuilt UCGameplay.u you get Epic's lazy camera everywhere (the shipped package has no D-6), which
  never showed this. With D-6's UCGameplay.u but without this fix, you get the jitter you saw.
- UCGameplay is a server package, so a client and its server must carry the same build. Native trees, the web
  manifest and servers switch together (the web manifest's only changed entry is `system/ucgameplay.u`).

**Why.**
- UC2 computes a frame's view early in the tick: `PlayerWalking.PlayerMove` → `CalculateAimInfo` →
  `PlayerCalcView` (`UCGameplay.cpp:176-178`). That runs *before* the pawn's physics, because the pawn ticks after
  its controller (`UnLevTic.cpp:169-174`). `PlayerCalcView` then hands that cached view to the frame's drawing.
- Epic's lazy camera closes a share of the gap proportional to the frame time. Its lag is therefore `v/k` at any
  frame time, and drawing from the early view never shows.
- D-6's rigid mouse camera sits exactly behind the pawn. So every frame drew the pawn its own move of that frame
  ahead of its rest spot. The trace fits `ahead = 1.000 × v·dt` with a residual of 0.00-0.01 units, in all four
  directions.
- With even frames that is a constant offset and invisible. With uneven frames (any browser, or a native hitch) it
  changes every frame.

**Decisions and alternatives.**
- We fixed the order of operations, not added smoothing. Rejected options:
  - Smooth the mouse camera's position the way the lazy camera does: this hides the cause and puts back the lag
    D-6 removed on purpose.
  - Move the camera calculation after physics for everyone: this changes Epic's aim order and the pad's feel.
- The view is computed a second time only when it is the mouse's rigid camera *and* the pawn has moved. The aim,
  the pad's lazy camera and the end-of-round focused views keep Epic's single calculation.

**Disadvantages, risks, deviations.**
- The view drawn can differ from the one the aim trace used in that tick, by up to `v·dt` (about 7-25 units). It is
  a parallel shift with the same rotation, and the next tick aims from the drawn view. The agent judged it
  negligible; hit rates were not measured.
- The walk bob remains. The rigid camera carries all of it, about ±8 px of smooth sway at 1080p; the lazy camera
  passes about 60-70% of it. If you still call that sway jittery, the follow-up is to smooth or drop the bob under
  the mouse only (a decision for you).
- The pad's strafe segments keep 0.6-2.2 px of judder at every frame rate. This is identical with the shipped
  scripts, and comes from the lazy camera trailing the pawn vertically on the test spot's ramp.

**Status (measured).** Repeated by the coordinator on main (`logs/coord/a14`, LOG.md entry O-11):
- Native kb+mouse: uneven frames 0.53 px, 60 fps 0.26 px, uncapped 0.01 px (limit 1.0).
- Pad: 1.87 px worst segment, identical with the pre-fix UCGameplay.u (also 1.87).
- Browser jitter spec: judder 0.50-0.70 px.
- Browser boot, keys and saves specs pass; `m10-controls.sh` 5/5.
- `d6-mouse.sh` 67/2: the spawn-dependent lock check, and the pad walk check at a spawn by a wall. Rerun twice:
  21/0, walk -353/+408 units.

The agent's own numbers:

| Configuration | Before (judder RMS per segment) | After |
|---|---|---|
| Native kb+mouse, uneven 16.8-33.8 ms frames | 10.5 / 11.0 / 20.4 / 20.7 px, b = 1.000 | 0.53 / 0.34 / 0.51 / 0.52 px, b = 0.000 |
| Native kb+mouse, 60 fps | up to 3.0 px (one 37.5 ms hitch: 20 px) | 0.18-0.27 px |
| Native kb+mouse, uncapped | 0.20-0.80 px | 0.01 px |
| Native pad (lazy camera) | 0.15-0.44 px | unchanged |
| Browser kb+mouse, 1024x768, frames 20.8-57 ms | 4.1 / 7.3 / 11.4 / 10.9 px, b ≈ 1 | 0.78 / 0.50 / 0.56 / 0.53 px, b ≈ 0 |

- First person was never affected: b = 0.000 there before the fix too.
- Regressions: verifyclasses OK; `m10-controls.sh` 5/5 pass; browser boot spec 4/4; `d6-mouse.sh` 68 pass,
  1 fail. The failing check (`lock-pulls-back-after-the-mouse-stops`) depends on the random spawn. Pinned to that
  spawn it fails identically on main without the fix, and the new condition cannot act there because the pawn
  stands still.
- Pictures: `logs/jit/strip/strip-2.png` (12 consecutive frames per row, before and after) and
  `logs/jit/strip/strip-2.gif`.
- Not done: a physical pad; real Windows.

**Off switch.** None of its own. Using the shipped UCGameplay.u removes D-6 as a whole.


---

# 60. Changes since this report (2026-10-09/10)

Added 2026-10-10 (task D-3). Sections 00-50 are left as you reviewed them; where a paragraph no longer holds, a
one-line "Superseded (2026-10-09/10): see section 60" note follows it. This section says, per report section and
feature, what changed after the snapshot `4813dd9` and where the current truth is. The work came from your review of
2026-10-09 (PLAN.md "Owner review of 2026-10-09": F-1..F-6) and of 2026-10-10 (D-1..D-4, G-1..G-6). Main is `2ed330f`
at the time of writing: 620 engine commits since the import, 88 since the snapshot.

Where the current truth is:

| Task | LOG.md entry | Notebook |
|---|---|---|
| O-11 jitter (already section 50) | 2026-10-08 "O-11 green" | `jitter.md` |
| F-1 vanilla scripts (+ F-1b/c cheats) | 2026-10-09 "Owner review of 2026-10-09", F-1 | `vanilla.md` |
| F-2 / F-4 platform like UT2004 | 2026-10-09, F-2/F-4 | `platform-f4.md` |
| F-4b command-line audit | 2026-10-09 "F-4b and F-3 green" | `platform-f4.md` (steps 7-10) |
| F-3 GL4ES on WebGL 2 | 2026-10-09 "F-4b and F-3 green" | `webgl2.md` |
| F-5 renderer items | 2026-10-09, F-5 | `render-f5.md` |
| F-6 audio | 2026-10-09, F-6 | `audio-f6.md` |
| O-3 depth-of-field alpha (no LOG entry until now) | the entry D-3 wrote for the coordinator: `logs/d34/log-o3.md` | `postprocess.md` P5b |
| D-3 / D-4 stale text and test hygiene | (the coordinator's entry for D-3/D-4) | `d34-docs-tests.md` |

## 00 Overview

- "Start here" item 3: the mouse no longer needs `UC2InputMigrate.cpp` (deleted, F-1) nor the D-6 bindings; see 30.5.
  Item 5: the web build draws on WebGL 2 since F-3 (WebGL 1 is the fallback); see R-20.
- "Decisions waiting for you", decided since: 1 walk bob kept (no ini key); 2 O-14 closed (the bindings were "weird",
  not reversed; rebinding covers it); 3 D-9: Space + direction dodges as on the Xbox, a conflict CLEARS the other
  binding, the C/V order stays (rebinding covers taste); 4 the pad's turns at high frame rates stay as Epic wrote
  them; 6 the 19 waves decoded (40.1); 7 O-6 closed by you ("everything seems fixed"); 8 fetched libraries stay on
  branches, latest wins; 11 E-1/E-3/E-4 restarted as G-6/G-5/G-4. Still open: 5 (the standby pause), 9 (the web
  profile layout, with the profiles revisit), 10 (GitHub: a private `10-p/engine-uc2` exists since 2026-10-10; CI
  after the merge).
- "Found while writing this report", all handled: `DOFShippedBlurAlpha`'s compiled default (F-5), dynamic gamma (F-5:
  stays on, Epic's split-screen manager turns it on too), `DistortionCullDistance` (F-5), the stale text (D-3, this
  task: `UnRenDev.h`, `UseXboxProjectors`' comment, `WebRTCDrv.h`, `webrtc.md`, `audio.md`, the frontend files as
  a patch), README/BUILD status and the web section, `UC2_ASSETS_DIR` (F-4d), meta.json (F-4d), BLOCKERS arm64
  (F-4d, audited 2026-10-10), the ini line endings (F-4d), the fail-soft loaders (F-4a), the test console commands
  (F-1b: guarded cheats; N-1 asks which stay), `-NETOLDACKS` (F-4b: a test key). Still open: the exit-time
  dedicated-server crash/hang (D-1) and the web manifest vs cache budget (G-2 precache).
- "Milestones": F-1..F-6 done; open now: G-1..G-6 (running), D-1..D-4, N-1..N-5 (yours).

## 10. Platform, build and runtime

- **Feature 8, the web runtime (F-4a, F-4b step 3, F-4d, F-3).** C++ exception catching is OFF for the engine on
  the web, with engine-ut2004's fail-soft loaders (`Core/Src/UnObj.cpp`, `UnLinker.cpp`, `Engine/Src/UnGame.cpp`,
  `IpDrv/Src/TcpLink.cpp`, `UdpLink.cpp`); only openal-soft keeps exceptions. The report's reason for real catching
  was wrong: `Editor.TransBuffer` never threw (`VerifyImport`'s SafeReplace branch logs it). wasm -8%, Node census
  58/58 maps, 0 aborts. Frame yield exactly as engine-ut2004 (one yield per frame, only when the limiter did not
  sleep; a hidden tab mutes the listener; ut2004's `ASYNCIFY_IMPORTS`); `-sOFFSCREEN_FRAMEBUFFER` kept on purpose.
  `meta.json` records the fetched libraries (`cmake/ArtifactDeps.cmake`, `--dep-gl4es`, `--dep-openal-soft`). The
  WebGL link limits are 1..2 since F-3 (`Source/SDLLaunch/cmake/Web.cmake:121-123`). Truth: `platform-f4.md` steps
  2, 3, 5; LOG.md 2026-10-09.
- **Feature 9, `-ini:` (F-4b).** The command line holds 4,096 characters (was 1,023, cut silently in SDLLaunch):
  a token that does not fit is dropped whole with a warning (`SDLLaunch.cpp:524-526`). Every test switch we had added
  is now a test key read ONCE with `appReadTestKey`/`appReadTestFlag` (`Core/Inc/UnFile.h:319`) and never kept in the
  ini; kept switches: `-log`, `-ini:`, `-windowed`, `-defaultres`, `-usehomedir`, the three renderer selectors,
  commandlet parameters. `-ini:` itself is unchanged. Truth: `platform-f4.md` "The switch table, final".
- **Feature 13, the inis (F-4d, F-4 follow-up, F-1).** `Default.ini`, `DefUser.ini` and the web pair are LF; Epic's
  three reference copies (`DefaultXbox.ini`, `DefUserXbox.ini`, `KeyBinds.ini`) are Epic's bytes again (CRLF,
  `9d5683a`); `sync-web-ini.py` refuses a CR in our inis and `--check` compares the three with Epic's. No migration in
  code any more: `UC2InputMigrate.cpp` is gone (F-1, owner: "the curated ini is the way"). New keys since:
  `[Engine.Engine] FrameRateCap` (0 = uncapped) and `FpsLogSeconds`, `[UC2ALAudio.UC2ALAudioSubsystem] UseHRTF`,
  `WaveFile`, `UseEFX`, `[UC2GL4ESDrv.UC2GL4ESRenderDevice] GLESVersion=3`, `[UCGameplay.UCPlayerInput]
  bLazyMouseCamera`; `bKeysAsStick` and `MouseLockBreakAngle` removed (hard-wired). Appendix C has them all.
- Also in 10: the arm64 preset configured and built for the first time (F-4c: `ucc-bin verifyclasses` under qemu,
  401 classes, 0 mismatches); "Never verified: an arm64 configure or build" no longer holds (no arm64 hardware run).
  `UC2_ASSETS_DIR` removed from CMake (F-4d). New in Engine: `UC2Cheats.cpp` (WHEREAMI, TELEPORT; NETPLACE/NETPROBE
  behind the same guard), `UC2FpsLog.cpp` (F-2), `UC2FrameTrace.cpp` (O-11).

## 20. The renderer

- **R-10 DOF alpha (F-5, O-3).** The compiled default of `DOFShippedBlurAlpha` is True
  (`Source/UC2GLDrv/Src/UC2GLRenderDevice.cpp:217`), so an ini without the key also gets the shipped 65; ini and code
  agree. You confirmed 65 (it is an alpha, not a radius; no A/B). LOG.md's missing O-3 line: written for the
  coordinator in `logs/d34/log-o3.md`.
- **R-11 `DistortionCullDistance` (F-5).** One source of truth: a config property of each device class, read from
  the device's own section (`[UC2GLDrv.UC2GLRenderDevice]` on the desktop, `[UC2GL4ESDrv.UC2GL4ESRenderDevice]` for
  GL4ES); the global `GUC2GLDistortionCullDistance` and the hard-coded section are gone; compiled default 15000
  (`UC2GLRenderDevice.cpp:219`). Measured: the twin reads 7000 from its own section while the desktop keeps 15000
  (`render-f5.md` step 1). Open issue 13's "read only from the UC2GLDrv section" no longer holds.
- **R-18 anti-aliasing (F-3, F-4b).** FXAA is removed (`8adc4a1`). OpenGL ES 3 / WebGL 2 (the web and the twin, through
  GL4ES' ES 3 back end) gets real 4x multisampling with resolve, as the desktop; OpenGL ES 2 / WebGL 1 has no
  anti-aliasing. The switches `-glaa=N`, `-glnoaa`, `-glshotpresented` are gone: `AntiAliasing=<n>` in the device
  section (set with `-ini:`, `tools/dev/glini`), test key `ShotPresented`. Quiet A/B: WebGL 1 + FXAA 27.5-35.4 ms vs
  WebGL 2 + MSAA 29.8-36.3 ms per frame (`logs/coord/a16`).
- **R-20 the web renderer (F-3).** The device asks for WebGL 2 (`GLESVersion=3`, GLSL ES 3.00, `LIBGL_ES=3` in our
  GL4ES fork `10-p/gl4es` `fix/cubemap-bind-active-unit`) and falls back to WebGL 1. On WebGL 2 depth textures,
  `gl_FragDepth`, 32-bit indices and fragment `highp` are core, so the `WEBGL_depth_texture` gap of the fallbacks
  table closes there (still NOT implemented for a WebGL 1 context without the extension); 33 raw GLES calls behind
  GL4ES' back are gone on ES 3; the texture LOD bias is applied (the twin matches the desktop: 0.384 with, 0.871
  without). The masks `-glno*` are the test key `MaskCaps=<list>`. Truth: `webgl2.md`; LOG.md 2026-10-09.
- **Open issue 12 (stale text)**: done by D-3 (`UnRenDev.h:887-889`, `UC2GLRenderDevice.cpp:224-226`), BLOCKERS by
  the audit of 2026-10-10. Also F-5: occlusion queries explored, not implemented (nothing visible depends on them);
  the shader inventory (every shader a material or effect can select is ported); the DM-Azure sky haze found and
  fixed (Mesa's fixed-function fog coordinate; sky difference vs the twin 46.75 -> 2.49); CTF-Aqueduct flicker not
  reproduced (your conditions wanted); shadow strength waits for your xemu shot.

## 30. Input, UI, scripts, profiles and saves

- **30.5 the mouse (F-1, O-11).** Epic's shipped `UCGameplay.u` now plays with our inis: `DefUser.ini` binds the
  mouse as Epic's PC file and the retail disc do (`MouseX=Count bXAxis | Axis aMouseX Speed=6.0`, `:101-102`), so the
  shipped package turns the view through Epic's PC path (the stick's clamp and curve). The rebuilt `UCGameplay.u`
  takes the mouse's share out of `aMouseX/aMouseY` by the `Count` bytes and adds D-6's linear mouse on top, from
  the SAME ini (a D-6 User.ini with `aTurn/aLookUp` still works with it). `UC2InputMigrate.cpp`, its warning and
  `-noinimigrate` are gone; `-maxfps` became `[Engine.Engine] FrameRateCap`. New `bLazyMouseCamera` (Epic's lazy
  camera under the mouse, default off). Native: SDL's relative-mouse hints as engine-ut2004 (N counts in = N out at
  two speeds; no acceleration). The look keys: PageUp/PageDown are the look stick at 0.8 (`Axis aMouseY
  Speed=+-4.0`), the arrows strafe (Epic's PC layout). The rigid camera's jitter: section 50. Truth: `vanilla.md`
  steps 2-3, 6; LOG.md 2026-10-09 F-1.
- **30.7 `UC2InputMigrate`: removed** (F-1, owner: no auto-migration of user inis). The call in `UnGame.cpp` is gone;
  that part of `UGameEngine::Init` is Epic's again.
- **30.8 keys as a stick (F-1).** `bKeysAsStick` and `MouseLockBreakAngle` are hard-wired (the ini keys are gone;
  25 degrees). The stick's walk on PC moved from the rebuilt script into the engine, for BOTH packages
  (`Source/Engine/Src/UnIn.cpp:320-335`, non-Xbox builds: Epic's script scales the stick only under `IsXBox()`).
  Measured: W, the left stick at 1.0/0.5/0.35 and Shift+W all run at 430 units/s (UC2 has no walk mode; you: leave it).
  O-14 closed by you.
- **30.16 the KEYBOARD / MOUSE page (F-1).** A key another action had is now CLEARED from that action (your D-9
  decision), not swapped (`UC2Settings_Keys.uc` `AssignKey`); STRAFE LEFT/RIGHT list `A, Left` / `D, Right`; TURN
  LEFT/RIGHT have no key by default ("THE MOUSE TURNS"). The owner choices listed as open are decided (00 above).
- **30.18 the `.uc` files and the rebuilt packages (O-11, F-1).** `UCPlayerController.uc` (83 lines now) and
  `UCPlayerInput.uc` (221) changed again (the jitter fix, F-1's mouse share, `bLazyMouseCamera`, the hard-wired keys);
  the asset tree carries `UCGameplay.u` 4ed72f0b and `UCMenu.u` b9202a59 (`SCRIPTS.txt`). Reverting to the shipped
  packages now costs only the PC mouse, the player name without a profile and the KEYBOARD / MOUSE page: no ini to
  revert, no migration. Every suite runs both ways (`d6-mouse.sh vanilla`, `m10-controls.sh` on a shipped tree).
- **Open issues 2, 5, 6** decided (O-14 closed; the pad's turns stay; D-9 choices made). Item 12: the d6 lock checks
  were "flaky" because two PlayerStarts cannot show the pull-back; D-4 starts every movement scenario at a fixed spot
  (`d34-docs-tests.md` step 3).

## 40. Audio, movies, networking and the frontend

- **40.1 the cook (F-6).** `tools/uc2xwb.py` reads the shipped Xbox wave banks (the ADPCM layout settled against 643
  source WAVs); the 19 waves without a source and the long `Machine_Generator03` are decoded from the `.xwb`;
  `A_Vehicle` is cooked from the shipped `.xsb`/`.xwb` (31 cues; cut content); the 674 sounds without a priority
  get 255, as the shipped `.xsb` stores. Cues: 2,465 fully, 0 partly, 4 with no wave anywhere in the project (was
  2,417 / 6 / 15). Cue tables inside the Ogg packs: spiked and NOT recommended. Installed in `assets/uc2-cooked` (85
  files) and the asset tree; ufrontend manifest `6e77b28`. Truth: `audio-f6.md`; LOG.md 2026-10-09 F-6.
- **40.2 the device (F-6, F-4b).** `[UC2ALAudio.UC2ALAudioSubsystem] UseHRTF` as engine-ut2004 (default off; the
  interaural delay measured when on): "No HRTF" no longer holds. `-NOEFX` and `-audiowave=` are the ini keys `UseEFX`
  and `WaveFile`. Status now: audio-check 7,191/7,191 waves, self-test 45/45. `audio.md`'s "below 0.1 = silence"
  corrected (D-3).
- **40.8 the missing WAVs (F-6).** Decoded from the shipped banks (above); 4 cues stay silent (no wave exists in the
  project: `ST_Primary`, `JG_JumpChargeEthereal`, ...). The localised taunt order is unchanged (kept as the Xbox had it).
- **40.15 WebRTC.** `WebRTCDrv.h` and `webrtc.md` now say what was measured: the glue's channel is reliable and
  ordered (D-3).
- **Frontend (40.16-40.20).** Since `4ac0d6d` the frontend's `uc2` branch has the F-1/F-6 manifests (`227e5c3`,
  `6e77b28`) and the specs passing their test settings as `-ini:` keys (`1fdc55d`, the "without post-processing"
  launcher option removed). Its first-light texts (help.md, portal-notes.json, the target README, the root README row,
  STATE.md) have replacement text in `logs/d34/frontend-docs.patch` (not committed: frontend work is the
  coordinator's). The jitter spec's strafe slope (0.217-0.247 on WebGL 2) was the spec's fit leaving out the walk bob:
  `logs/d34/frontend-jitter-spec.patch`, `d34-docs-tests.md` step 6.

## 50. Third-person jitter

Unchanged. Decision 1 (the walk bob): kept, no ini key (your review of 2026-10-09).

## Appendices

Regenerated on 2026-10-10 for main `2ed330f` by `tools/dev/report-appendices.py` (the coordinator's method, now a
script; `report-appendices.py 4813dd9` reproduces the appendices this report had, byte for byte). Since the snapshot:
140 modified Epic files (was 136: `Engine/Src/UnPlayer.cpp` for the cheats, `Core/Src/UnLinker.cpp`,
`IpDrv/Src/TcpLink.cpp`, `UdpLink.cpp` for the fail-soft loaders); 189 new files (was 187: `UC2InputMigrate.cpp`
deleted; `UC2Cheats.cpp`, `UC2FpsLog.cpp`, `UC2FrameTrace.cpp` added); ini keys that differ from Epic's: Default.ini
118 (was 114), DefUser.ini 54 (was 59).


---

# Appendices (generated from git)

> Regenerated on 2026-10-10 for main `2ed330f` by `tools/dev/report-appendices.py` (section 60). The appendices as
> reviewed (snapshot `4813dd9`): `tools/dev/report-appendices.py 4813dd9`, byte for byte.

## Appendix A. Epic's files we modified (140), with the commits that touched them

Generated from `git diff --name-status 7633696 2ed330f -- Source` (files that existed in Epic's tree). +/- = lines added/removed. The commit list is every commit since the import that touched the file, oldest first.

### Engine (69 files)

- `Source/Engine/Classes/PSDistortionBaseMaterial.uc` (+5/-3)
  - 94654e2 Engine, UCGameplay: the Xbox post-process paths run for a device with the XboxPostProcess capability (M7 P2)
- `Source/Engine/Inc/AActor.h` (+2/-2)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
- `Source/Engine/Inc/ALevelInfo.h` (+1/-1)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
- `Source/Engine/Inc/APawn.h` (+3/-3)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Inc/Engine.h` (+10/-0)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Inc/EngineClasses.h` (+154/-152)
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
  - 94654e2 Engine, UCGameplay: the Xbox post-process paths run for a device with the XboxPostProcess capability (M7 P2)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
  - 2a0d111 Engine, build, tools: two punned pointer ints; arm64 and native64-uchar presets; wine smoke takes its trees (M13)
- `Source/Engine/Inc/ULoadSaveManager.h` (+8/-3)
  - 4399e22 Engine: a save game's object reference is 4 bytes on every width; tools: m13-64bit.sh (M13)
- `Source/Engine/Inc/UParticleEmitter.h` (+1/-1)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
- `Source/Engine/Inc/UReachSpec.h` (+1/-1)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
- `Source/Engine/Inc/UnAudio.h` (+5/-0)
  - e00bd2f Engine: UAudioSubsystem::OpenMovieAudio (default NULL) for a movie's streamed sound (WIP: not yet built)
- `Source/Engine/Inc/UnCamera.h` (+16/-0)
  - 14d9738 Engine: the UI's 640x480 design space in a centred 4:3 box (UIScaleMode=Fit43)
  - f9e055f Engine: sharper text in a scaled-up UI (UISharpText); d2 scenarios hold Fire, lock-on check
- `Source/Engine/Inc/UnCameraEffects.h` (+9/-3)
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Inc/UnCanvas.h` (+19/-0)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
  - 14d9738 Engine: the UI's 640x480 design space in a centred 4:3 box (UIScaleMode=Fit43)
- `Source/Engine/Inc/UnChan.h` (+1/-1)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Inc/UnEngine.h` (+6/-0)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Inc/UnFluidSurface.h` (+7/-1)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Inc/UnForceFeedbackWaveform.h` (+6/-0)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Inc/UnGame.h` (+6/-0)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Inc/UnLevel.h` (+1/-1)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
- `Source/Engine/Inc/UnMaterial.h` (+6/-0)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Inc/UnParticleSystem.h` (+12/-0)
  - 8a232bd Engine: 4-byte packing for the hand-written script mirrors UnTex.h and UnParticleSystem.h
- `Source/Engine/Inc/UnPlayer.h` (+6/-0)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Inc/UnPlayerStats.h` (+1/-1)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Inc/UnPrim.h` (+2/-0)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Inc/UnRenDev.h` (+76/-1)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
  - 47d3b6d UC2GLDrv: render targets as shader inputs and the post-process interface (M7 P1)
  - 94654e2 Engine, UCGameplay: the Xbox post-process paths run for a device with the XboxPostProcess capability (M7 P2)
  - bec8014 Engine, UC2GLDrv: M7b capabilities: Xbox shadow textures (XboxShadows), three terrain layers per pass, the Xbox projector material in the driv
  - f8dfb79 UCGameplay, Engine, UC2GLDrv: post-process blurs sized for a 480-line reference picture
  - 784596a Engine, UC2GLDrv: Xbox static projector batching behind FRenderCaps::XboxProjectors; caps before the GL context; deferred occlusion queries dr
  - e42eafb Merge branch 'pp-scale': post-process blurs sized for a 480-line reference picture (O-10)
- `Source/Engine/Inc/UnRender.h` (+62/-1)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
  - 6f9a9fa Engine: AutoFOV (Hor+) for every view of the player, console key off Tab (D-7, D-8)
- `Source/Engine/Inc/UnRenderResource.h` (+1/-1)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Inc/UnRenderUtil.h` (+34/-0)
  - 14d9738 Engine: the UI's 640x480 design space in a centred 4:3 box (UIScaleMode=Fit43)
  - f8dfb79 UCGameplay, Engine, UC2GLDrv: post-process blurs sized for a 480-line reference picture
- `Source/Engine/Inc/UnTerrain.h` (+7/-1)
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Inc/UnTex.h` (+13/-1)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
  - 8a232bd Engine: 4-byte packing for the hand-written script mirrors UnTex.h and UnParticleSystem.h
- `Source/Engine/Inc/UnURL.h` (+6/-0)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Src/AFileLog.cpp` (+1/-1)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Src/FContentManager.cpp` (+7/-7)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Src/ULoadSaveManager.cpp` (+41/-0)
  - 22e9dbf Engine: the web build keeps a profile as one flat file Profiles/<name>.PRO (the frontend's userFiles cannot name a per-profile directory); too
- `Source/Engine/Src/UnActor.cpp` (+2/-2)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
- `Source/Engine/Src/UnAudio.cpp` (+1/-1)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Src/UnCamMgr.cpp` (+34/-1)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
  - 14d9738 Engine: the UI's 640x480 design space in a centred 4:3 box (UIScaleMode=Fit43)
  - f9e055f Engine: sharper text in a scaled-up UI (UISharpText); d2 scenarios hold Fire, lock-on check
- `Source/Engine/Src/UnCanvas.cpp` (+490/-17)
  - 14d9738 Engine: the UI's 640x480 design space in a centred 4:3 box (UIScaleMode=Fit43)
  - 66445f6 Engine: menus clipped to the UI box; config keys, README row, the d2 verification script
  - f9e055f Engine: sharper text in a scaled-up UI (UISharpText); d2 scenarios hold Fire, lock-on check
  - 519b5de Engine: sharper text by real glyph height, its ink where the small glyph's ink is
  - 6f9a9fa Engine: AutoFOV (Hor+) for every view of the player, console key off Tab (D-7, D-8)
- `Source/Engine/Src/UnChan.cpp` (+24/-0)
  - 8a5b0e2 Core: package map counts only the package file's exports and names (patched-in ones differed between client and server); net diagnostics under
  - 006e725 Engine, UCC: connection destroys (not deletes) its channels during GC, routed channel destroy marked as routed; commandlet rooted while it run
  - d27dfc6 Engine, tools: deterministic packet drop (NETDROP=) and packet-id start (NETPKTID=) for the net tests; handshake-loss check; notebook: the lag
- `Source/Engine/Src/UnConn.cpp` (+89/-9)
  - 006e725 Engine, UCC: connection destroys (not deletes) its channels during GC, routed channel destroy marked as routed; commandlet rooted while it run
  - d27dfc6 Engine, tools: deterministic packet drop (NETDROP=) and packet-id start (NETPKTID=) for the net tests; handshake-loss check; notebook: the lag
  - 84aea3e Engine: wrap-aware packet ack/NAK and NAK-able packet 0 (UC2 never resent a lost first packet or anything after the id wrap); pending handshak
  - c00a538 Core, Engine, SDL2Drv, UC2GLDrv, UC2GL4ESDrv, tools: the remaining test switches become one-shot ini test keys set with -ini:; the on/off twin
- `Source/Engine/Src/UnController.cpp` (+24/-2)
  - 14d9738 Engine: the UI's 640x480 design space in a centred 4:3 box (UIScaleMode=Fit43)
  - 6f9a9fa Engine: AutoFOV (Hor+) for every view of the player, console key off Tab (D-7, D-8)
- `Source/Engine/Src/UnEngine.cpp` (+23/-0)
  - 9df0ad4 Engine, SDLLaunch: start-up logos (UC2_Logos) full screen after engine init; a full-screen movie owns the input (UEngine::InputEvent hook)
  - 61c9f48 Engine: FLUSHCONFIG console command (the web host's 'write the config now'); tools: c1-saves.sh (C-2 file inventory and profile restart check)
- `Source/Engine/Src/UnFont.cpp` (+1/-1)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
- `Source/Engine/Src/UnGame.cpp` (+233/-14)
  - 604a8a3 Core, Engine: a leading Key=Value token is a global option, not the map
  - 7dd3d3b Engine: lift the Xbox cinematic/loading-movie hooks in LoadMap through WITH_PORTABLE_MOVIES; guard ResetRenderStates (no level/device on PC's 
  - be21a3c Engine: test-only net commands NETPROBE/NETPLACE/NETAIM and the -nettest= command file (UC2NetTest.cpp)
  - 0f44cbf Merge branch 'net-join': a client joins a server and plays (network join defects fixed, net test suite)
  - e3529cc Engine: the first player starts in the profile they last played with (D-3)
  - 6f9a9fa Engine: AutoFOV (Hor+) for every view of the player, console key off Tab (D-7, D-8)
  - 6dda321 Engine: a saved profile's name is not the player's own name; a local player with a profile logs in under it (D-4)
  - c2e7b83 Engine: PROFILESTATE and a log line per change the profile poll sees (D-3 diagnostics); docs: step 8
  - 2b2c972 Merge branch 'd3-profiles': start in the last profile, the profile owns the name, real keyboard on every text page (D-3, D-4, D-5)
  - 02289c7 Engine: D-6 moves the mouse/pad values an earlier build left in User.ini (Epic's PC file, the web's until 2026-10-07; M10's) to D-6's at start
  - 784596a Engine, UC2GLDrv: Xbox static projector batching behind FRenderCaps::XboxProjectors; caps before the GL context; deferred occlusion queries dr
  - 1b33254 Merge branch 'm7b-projectors': Xbox projector batching, Xbox shadow blur, three-layer terrain switch (M7b)
  - 941326a WebRTCDrv: the browser's network driver, ported from engine-ut2004 and adapted to UC2 (E-2)
  - c46bb34 Engine: FRAMETRACE, a per-frame trace of the view and the pawn on the screen; tools: jit-trace
  - c59c02d Merge branch 'jitter': the mouse's rigid third-person camera is drawn from where the pawn is drawn (O-11)
  - 27569b4 Engine, tools: the frame rate in the log ([Engine.GameEngine] FpsLogSeconds, FPSLOG) and tools/dev/fps-bench.sh (F-2)
  - 3c6a8f4 Core, Engine, IpDrv, build: C++ exception catching off for the web engine, loaders fail soft as engine-ut2004 (F-4a)
  - b38973f UCGameplay, config, Engine: Epic's mouse binding plays with the shipped scripts; the rebuilt UCPlayerInput takes the PC mouse from it; lazy ca
  - 6c155f5 Engine: WHEREAMI and TELEPORT <x> <y> <z> [<yaw> [<pitch>]] as cheats; NETPROBE/NETPLACE/NETPLACENEAR/NETAIM behind the same guard
  - c00a538 Core, Engine, SDL2Drv, UC2GLDrv, UC2GL4ESDrv, tools: the remaining test switches become one-shot ini test keys set with -ini:; the on/off twin
  - 9764bc8 Merge branch 'f1-vanilla': Epic's shipped scripts play as shipped, the rebuilt packages add the PC mouse on top; UC2InputMigrate removed; lazy
  - 94971e1 Engine, tools: NETAIM removed from the engine; the network tests line players up around the shooter's own view
  - ab007a3 Merge branch 'f4b-switches': test switches become once-read ini keys, unused switches removed, Epic's reference inis byte-identical, command l
- `Source/Engine/Src/UnIn.cpp` (+29/-4)
  - de94c55 Engine: stick axes mean on PC what they mean on the Xbox (UnIn.cpp SpeedBase scaling)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
  - b38973f UCGameplay, config, Engine: Epic's mouse binding plays with the shipped scripts; the rebuilt UCPlayerInput takes the PC mouse from it; lazy ca
- `Source/Engine/Src/UnInteraction.cpp` (+5/-0)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Src/UnInteractionMaster.cpp` (+38/-1)
  - dd78796 Engine: the console's line and the other global interactions clipped to the UI box
  - 868f41d Engine: the console keeps the release of the keys it took (D-7)
  - 0f77515 Engine: a lost release does not leave a console mark behind (D-7); tools: lock-on figure searched inside the reticle
- `Source/Engine/Src/UnMaterial.cpp` (+4/-4)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Src/UnModelLight.cpp` (+1/-1)
  - 6aa92ca Core, Engine: compile with MinGW-w64 i686
- `Source/Engine/Src/UnNetDrv.cpp` (+16/-0)
  - 8a5b0e2 Core: package map counts only the package file's exports and names (patched-in ones differed between client and server); net diagnostics under
- `Source/Engine/Src/UnOctree.cpp` (+3/-3)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Src/UnParticleEmitter.cpp` (+7/-2)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Src/UnPenLev.cpp` (+21/-0)
  - 84aea3e Engine: wrap-aware packet ack/NAK and NAK-able packet 0 (UC2 never resent a lost first packet or anything after the id wrap); pending handshak
  - e3c66ac Engine: pending handshake give-up measured from the last server message, after dispatch (fired after a long GL frame); tools: GL client connec
- `Source/Engine/Src/UnPixelShaderMaterial.cpp` (+70/-18)
  - 94654e2 Engine, UCGameplay: the Xbox post-process paths run for a device with the XboxPostProcess capability (M7 P2)
- `Source/Engine/Src/UnPlayer.cpp` (+6/-0)
  - 6c155f5 Engine: WHEREAMI and TELEPORT <x> <y> <z> [<yaw> [<pitch>]] as cheats; NETPROBE/NETPLACE/NETPLACENEAR/NETAIM behind the same guard
- `Source/Engine/Src/UnRender.cpp` (+200/-5)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
  - 94654e2 Engine, UCGameplay: the Xbox post-process paths run for a device with the XboxPostProcess capability (M7 P2)
  - 14d9738 Engine: the UI's 640x480 design space in a centred 4:3 box (UIScaleMode=Fit43)
  - 6f9a9fa Engine: AutoFOV (Hor+) for every view of the player, console key off Tab (D-7, D-8)
- `Source/Engine/Src/UnRenderBatch.cpp` (+9/-3)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Src/UnRenderResource.cpp` (+58/-0)
  - 47d3b6d UC2GLDrv: render targets as shader inputs and the post-process interface (M7 P1)
  - bec8014 Engine, UC2GLDrv: M7b capabilities: Xbox shadow textures (XboxShadows), three terrain layers per pass, the Xbox projector material in the driv
  - f8dfb79 UCGameplay, Engine, UC2GLDrv: post-process blurs sized for a 480-line reference picture
  - 784596a Engine, UC2GLDrv: Xbox static projector batching behind FRenderCaps::XboxProjectors; caps before the GL context; deferred occlusion queries dr
  - e42eafb Merge branch 'pp-scale': post-process blurs sized for a 480-line reference picture (O-10)
- `Source/Engine/Src/UnRenderStaticMesh.cpp` (+8/-2)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Src/UnRenderUtil.cpp` (+56/-1)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
  - 6fa7758 Engine: FScreenPrimUtil tiles are complete on PC; config: UsePostProcess, DOFShippedBlurAlpha, MaxDistortion (M7 P4)
  - 14d9738 Engine: the UI's 640x480 design space in a centred 4:3 box (UIScaleMode=Fit43)
  - f8dfb79 UCGameplay, Engine, UC2GLDrv: post-process blurs sized for a 480-line reference picture
- `Source/Engine/Src/UnRenderVisibility.cpp` (+4/-0)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Src/UnScript.cpp` (+4/-4)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Engine/Src/UnScriptPatcher.cpp` (+27/-0)
  - 0926219 Engine: hold BeginLoad across FScriptPatcher's Apply (network client asserted in GetPackageLinker)
  - 63de2e4 Engine: BeginLoad/EndLoad around the patcher's AppendImport loop only (replaces the wider scope)
  - d2d1648 Engine, tools: the script patcher leaves rebuilt packages alone and logs; mkrun UC2_SCRIPTS, net-test NET_TREE (M14 S3)
- `Source/Engine/Src/UnShadowProjector.cpp` (+27/-4)
  - bec8014 Engine, UC2GLDrv: M7b capabilities: Xbox shadow textures (XboxShadows), three terrain layers per pass, the Xbox projector material in the driv
- `Source/Engine/Src/UnStats.cpp` (+14/-12)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
  - 27569b4 Engine, tools: the frame rate in the log ([Engine.GameEngine] FpsLogSeconds, FPSLOG) and tools/dev/fps-bench.sh (F-2)
- `Source/Engine/Src/UnTex.cpp` (+2/-2)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
- `Source/Engine/Src/UnUCRibbonEmitter.cpp` (+1/-1)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
- `Source/Engine/Src/UnUCTrailEmitter.cpp` (+1/-1)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
- `Source/Engine/Src/xbstrip.cpp` (+30/-6)
  - f700305 Engine: compiles with GCC 15 (-m32) and emcc 5.0.7
  - 1e7f700 UCC, SDLLaunch, Engine: ucc.exe and uc2.exe link with MinGW-w64

### Core (32 files)

- `Source/Core/Inc/Core.h` (+17/-2)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
  - 56298d5 Core: 64-bit bytecode object slots hold object indices; registration stashes in pointer-sized fields (M13, WIP)
- `Source/Core/Inc/FFileManagerGeneric.h` (+21/-1)
  - e7fc07d Core: Unix file manager from engine-ut2004, portable word wrap, header fixes for launchers
  - 10b802f Core: DeleteFiles strips the pattern at the last '/' too (deleting a profile never worked on Linux)
- `Source/Core/Inc/FFileManagerLinux.h` (+9/-617)
  - e7fc07d Core: Unix file manager from engine-ut2004, portable word wrap, header fixes for launchers
- `Source/Core/Inc/FMallocAnsi.h` (+1/-1)
  - c67b332 Core: the last pointer narrowing (MinGW guard message); docs: windows64 builds and verifies under wine (M13)
- `Source/Core/Inc/FOutputDeviceFile.h` (+3/-1)
  - e7fc07d Core: Unix file manager from engine-ut2004, portable word wrap, header fixes for launchers
- `Source/Core/Inc/UnArc.h` (+10/-0)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Core/Inc/UnCorObj.h` (+6/-0)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Core/Inc/UnCoreNative.h` (+7/-1)
  - 75e4258 Core: static native lookup ignores case (an ini key 'Delete' broke UObject's 'delete')
- `Source/Core/Inc/UnFile.h` (+68/-4)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
  - 604a8a3 Core, Engine: a leading Key=Value token is a global option, not the map
  - 7bfdd62 Core: port engine-ut2004's -ini:<File>:[<Section>]:<Key>=<Value> command-line overrides (appApplyIniOverrides); docs: web-ini-saves notebook s
  - c00a538 Core, Engine, SDL2Drv, UC2GLDrv, UC2GL4ESDrv, tools: the remaining test switches become one-shot ini test keys set with -ini:; the on/off twin
- `Source/Core/Inc/UnGnuG.h` (+183/-173)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
  - 1ccb0d2 Core: FMovieThread/FMoviePlayer move unchanged to UnMovieBase.h, included by both platform headers (WIP: not yet built)
  - 2a0d111 Engine, build, tools: two punned pointer ints; arm64 and native64-uchar presets; wine smoke takes its trees (M13)
- `Source/Core/Inc/UnMath.h` (+5/-5)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
- `Source/Core/Inc/UnMem.h` (+8/-8)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Core/Inc/UnName.h` (+1/-1)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
- `Source/Core/Inc/UnObjBas.h` (+32/-13)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
  - 9faf167 Core: portable platform layer (UnUnix.cpp rebased on engine-ut2004, pthread threading)
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
  - 56298d5 Core: 64-bit bytecode object slots hold object indices; registration stashes in pointer-sized fields (M13, WIP)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
- `Source/Core/Inc/UnScript.h` (+1/-3)
  - 56298d5 Core: 64-bit bytecode object slots hold object indices; registration stashes in pointer-sized fields (M13, WIP)
- `Source/Core/Inc/UnTemplate.h` (+41/-37)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
  - 9faf167 Core: portable platform layer (UnUnix.cpp rebased on engine-ut2004, pthread threading)
  - e7fc07d Core: Unix file manager from engine-ut2004, portable word wrap, header fixes for launchers
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Core/Inc/UnType.h` (+3/-0)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
- `Source/Core/Inc/UnUnix.h` (+5/-1)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
  - e7fc07d Core: Unix file manager from engine-ut2004, portable word wrap, header fixes for launchers
- `Source/Core/Inc/UnVcWin32.h` (+2/-15)
  - 1ccb0d2 Core: FMovieThread/FMoviePlayer move unchanged to UnMovieBase.h, included by both platform headers (WIP: not yet built)
- `Source/Core/Inc/wordwrap.h` (+25/-1)
  - e7fc07d Core: Unix file manager from engine-ut2004, portable word wrap, header fixes for launchers
  - 6aa92ca Core, Engine: compile with MinGW-w64 i686
- `Source/Core/Src/UnAnsi.cpp` (+25/-5)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
  - c3c7397 Core, Editor, tools: script reproduction matches the shipped packages (M14 S2)
- `Source/Core/Src/UnClass.cpp` (+219/-24)
  - 56298d5 Core: 64-bit bytecode object slots hold object indices; registration stashes in pointer-sized fields (M13, WIP)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
  - 2a0d111 Engine, build, tools: two punned pointer ints; arm64 and native64-uchar presets; wine smoke takes its trees (M13)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
  - e3e927d Core: script SaveConfig()/StaticSaveConfig() write the ini on PC; SAVECONFIGKEYS; a base-class save leaves out what a subclass sets
- `Source/Core/Src/UnCorSc.cpp` (+108/-9)
  - 56298d5 Core: 64-bit bytecode object slots hold object indices; registration stashes in pointer-sized fields (M13, WIP)
  - 3afff92 Core: a None context clears its whole 64-bit destination; switch compares a pointer's width (M13)
  - e3e927d Core: script SaveConfig()/StaticSaveConfig() write the ini on PC; SAVECONFIGKEYS; a base-class save leaves out what a subclass sets
  - 7a5dac9 Core: a script StaticSaveConfig writes only what changed, an unchanged default is never baked into the ini
- `Source/Core/Src/UnCoreNet.cpp` (+15/-0)
  - 8a5b0e2 Core: package map counts only the package file's exports and names (patched-in ones differed between client and server); net diagnostics under
- `Source/Core/Src/UnGUID.cpp` (+2/-2)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
- `Source/Core/Src/UnLinker.cpp` (+45/-1)
  - 3c6a8f4 Core, Engine, IpDrv, build: C++ exception catching off for the web engine, loaders fail soft as engine-ut2004 (F-4a)
- `Source/Core/Src/UnMisc.cpp` (+126/-6)
  - 63bb34e Core: do not eat the first command-line argument off Windows; default port 7777
  - dcf245f Core: Windows wildcard semantics in FindFiles (registry works); appItoa off MSVC
  - 604a8a3 Core, Engine: a leading Key=Value token is a global option, not the map
  - 7bfdd62 Core: port engine-ut2004's -ini:<File>:[<Section>]:<Key>=<Value> command-line overrides (appApplyIniOverrides); docs: web-ini-saves notebook s
  - e3e927d Core: script SaveConfig()/StaticSaveConfig() write the ini on PC; SAVECONFIGKEYS; a base-class save leaves out what a subclass sets
  - c00a538 Core, Engine, SDL2Drv, UC2GLDrv, UC2GL4ESDrv, tools: the remaining test switches become one-shot ini test keys set with -ini:; the on/off twin
- `Source/Core/Src/UnName.cpp` (+3/-0)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
- `Source/Core/Src/UnObj.cpp` (+361/-24)
  - 82c9fd3 Core: shared headers and sources compile with GCC 15 and em++
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
  - 56298d5 Core: 64-bit bytecode object slots hold object indices; registration stashes in pointer-sized fields (M13, WIP)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
  - 555078d Core: the GC copies a whole object pointer; tools: map-census.sh takes a tree name and port (M13)
  - f14ba00 Core: write the dirty config files before the cache is dropped (RemoveNames); tools: c1-ini-overrides.sh (C-1 check, 27 PASS); docs: web-ini-s
  - 586600b Core, Editor, tools: script comparison tool, stripsource stage, backslash package names (M14 S2)
  - e3e927d Core: script SaveConfig()/StaticSaveConfig() write the ini on PC; SAVECONFIGKEYS; a base-class save leaves out what a subclass sets
  - 7a5dac9 Core: a script StaticSaveConfig writes only what changed, an unchanged default is never baked into the ini
  - f72face Core: the class records leave out a config string too long for ExportText's buffer
  - e2b3a8c Core: a conformed package save sorts only the new imports (Epic's bug; D-9)
  - 3c6a8f4 Core, Engine, IpDrv, build: C++ exception catching off for the web engine, loaders fail soft as engine-ut2004 (F-4a)
- `Source/Core/Src/UnProp.cpp` (+3/-1)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/Core/Src/UnUnix.cpp` (+1822/-429)
  - 9faf167 Core: portable platform layer (UnUnix.cpp rebased on engine-ut2004, pthread threading)
  - 1321d55 Core: monotonic, race-free appSecondsSlow; map census tool; M3 notes
  - 5bd7f07 Core: appSleep blocks where there is no Asyncify; no CPUSPEED= needed without a cycle counter
  - 6aa92ca Core, Engine: compile with MinGW-w64 i686
  - 2024551 Core, tools: Windows console Ctrl+C handler (Epic's), wine wrapper, run tree with exe/dll
  - a6c8edc Core: bounded writes into GMachineCPU[32] (UT2004 base code overflowed it; found by ASan); notebook glshot + ASan
- `Source/Core/Src/wordwrap.cpp` (+33/-5)
  - e7fc07d Core: Unix file manager from engine-ut2004, portable word wrap, header fixes for launchers

### Editor (12 files)

- `Source/Editor/Inc/Editor.h` (+2/-1)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
  - 434ac53 Editor, UCC: Editor layouts match (GCC bitfield tagging), verifyclasses checks them; Editor 32-bit Linux only (M14 S1)
- `Source/Editor/Inc/EditorClasses.h` (+3/-3)
  - 434ac53 Editor, UCC: Editor layouts match (GCC bitfield tagging), verifyclasses checks them; Editor 32-bit Linux only (M14 S1)
- `Source/Editor/Inc/Perlin.h` (+2/-2)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
- `Source/Editor/Src/TTFontImport.cpp` (+21/-0)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
- `Source/Editor/Src/UAnalyzeMeshesCommandlet.cpp` (+5/-0)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
- `Source/Editor/Src/UMakeCommandlet.cpp` (+40/-1)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
  - 586600b Core, Editor, tools: script comparison tool, stripsource stage, backslash package names (M14 S2)
  - c3c7397 Core, Editor, tools: script reproduction matches the shipped packages (M14 S2)
- `Source/Editor/Src/UStripSourceCommandlet.cpp` (+15/-1)
  - 586600b Core, Editor, tools: script comparison tool, stripsource stage, backslash package names (M14 S2)
- `Source/Editor/Src/UnEdExp.cpp` (+16/-13)
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
- `Source/Editor/Src/UnEdSrv.cpp` (+4/-2)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
- `Source/Editor/Src/UnParams.cpp` (+5/-0)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
- `Source/Editor/Src/UnScrCom.cpp` (+120/-0)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
  - c3c7397 Core, Editor, tools: script reproduction matches the shipped packages (M14 S2)
- `Source/Editor/Src/UnVisi.cpp` (+3/-3)
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)

### IpDrv (8 files)

- `Source/IpDrv/Inc/IpDrvClasses.h` (+7/-7)
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/IpDrv/Inc/UnIpDrv.h` (+16/-3)
  - 754ce47 build, IpDrv, UC2ALAudio: every package library compiles for Windows (MinGW i686)
  - 1e7f700 UCC, SDLLaunch, Engine: ucc.exe and uc2.exe link with MinGW-w64
- `Source/IpDrv/Inc/UnSocket.h` (+1/-0)
  - 482cfbb IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI: build natively and for the web
- `Source/IpDrv/Src/MasterServerClient.cpp` (+2/-2)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/IpDrv/Src/MasterServerUplink.cpp` (+3/-3)
  - 482cfbb IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI: build natively and for the web
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/IpDrv/Src/TcpLink.cpp` (+5/-1)
  - 3c6a8f4 Core, Engine, IpDrv, build: C++ exception catching off for the web engine, loaders fail soft as engine-ut2004 (F-4a)
- `Source/IpDrv/Src/UdpLink.cpp` (+5/-1)
  - 3c6a8f4 Core, Engine, IpDrv, build: C++ exception catching off for the web engine, loaders fail soft as engine-ut2004 (F-4a)
- `Source/IpDrv/Src/UnSocket.cpp` (+4/-1)
  - 482cfbb IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI: build natively and for the web
  - 754ce47 build, IpDrv, UC2ALAudio: every package library compiles for Windows (MinGW i686)

### GUI (6 files)

- `Source/GUI/Classes/GUIComponent.uc` (+2/-2)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
- `Source/GUI/Classes/GUIController.uc` (+20/-9)
  - 4069362 GUI: without a profile the player keeps the launcher's name (D-4); bUnlockCharacters/bShowAllMuts config again
  - fd81f64 GUI: the launcher's name is the first player's only; a split-screen guest without a profile keeps the character's name
- `Source/GUI/Inc/GUIClasses.h` (+31/-31)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/GUI/Inc/UnUIXInteraction.h` (+15/-0)
  - 482cfbb IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI: build natively and for the web
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/GUI/Src/GUI.cpp` (+1/-1)
  - 482cfbb IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI: build natively and for the web
- `Source/GUI/Src/UnGUICore.cpp` (+376/-74)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
  - 69b31bc GUI: menu key table as ini data ([GUI.EmulatedJoypad]); mouse click/hover; typing into on-screen keyboards
  - 86e0cf7 GUI: typed characters reach an on-screen keyboard's field through its OnKeyType; tools: text scenario
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
  - 14d9738 Engine: the UI's 640x480 design space in a centred 4:3 box (UIScaleMode=Fit43)
  - 66445f6 Engine: menus clipped to the UI box; config keys, README row, the d2 verification script
  - 18b839d GUI: real-keyboard Backspace clears a selected keyboard-page field; the key that opened a keyboard page is not typed into it (D-5)
  - 4567556 GUI: a raw key capture takes only keys pressed while it runs (D-9)

### UCGameplay (6 files)

- `Source/UCGameplay/Classes/UCPlayerController.uc` (+78/-5)
  - 49d4705 UCGameplay: D-6 mouse look separated from the pad (linear mouse channel, pad-only adhesion/auto-centre, rigid camera orbit under the mouse, lo
  - 0475725 tools: D-6 mouse/pad measurement (tools/dev/d6-mouse.sh), probe2/probe3/watch parsing in m10-inject.py; UCGameplay: camera at its place at onc
  - 51d1ffc UCGameplay: draw the mouse's rigid third-person camera from where the pawn is drawn
  - b38973f UCGameplay, config, Engine: Epic's mouse binding plays with the shipped scripts; the rebuilt UCPlayerInput takes the PC mouse from it; lazy ca
- `Source/UCGameplay/Classes/UCPlayerInput.uc` (+221/-0)
  - 49d4705 UCGameplay: D-6 mouse look separated from the pad (linear mouse channel, pad-only adhesion/auto-centre, rigid camera orbit under the mouse, lo
  - 0475725 tools: D-6 mouse/pad measurement (tools/dev/d6-mouse.sh), probe2/probe3/watch parsing in m10-inject.py; UCGameplay: camera at its place at onc
  - d75990b UCGameplay: D-6 mouse console commands are for the session (script SaveConfig is a no-op in UC2); tools: d6-mouse look+ini scenarios, circle f
  - e3e927d Core: script SaveConfig()/StaticSaveConfig() write the ini on PC; SAVECONFIGKEYS; a base-class save leaves out what a subclass sets
  - 51d1ffc UCGameplay: draw the mouse's rigid third-person camera from where the pawn is drawn
  - b38973f UCGameplay, config, Engine: Epic's mouse binding plays with the shipped scripts; the rebuilt UCPlayerInput takes the PC mouse from it; lazy ca
  - 77897e7 UCGameplay, config, UCMenu: the keyboard moves and strafes (arrows as Epic's PC file); keys-as-stick and the 25-degree lock break fixed, their
- `Source/UCGameplay/Classes/UCTrainingController.uc` (+28/-3)
  - 49d4705 UCGameplay: D-6 mouse look separated from the pad (linear mouse channel, pad-only adhesion/auto-centre, rigid camera orbit under the mouse, lo
- `Source/UCGameplay/Inc/UCCameraEffects.h` (+17/-3)
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
  - 94654e2 Engine, UCGameplay: the Xbox post-process paths run for a device with the XboxPostProcess capability (M7 P2)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
- `Source/UCGameplay/Inc/UCGameplayClasses.h` (+22/-22)
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
- `Source/UCGameplay/Src/UCCameraEffects.cpp` (+229/-61)
  - 94654e2 Engine, UCGameplay: the Xbox post-process paths run for a device with the XboxPostProcess capability (M7 P2)
  - f8dfb79 UCGameplay, Engine, UC2GLDrv: post-process blurs sized for a 480-line reference picture

### SDLLaunch (2 files)

- `Source/SDLLaunch/Src/SDLLaunch.cpp` (+426/-893)
  - 3d7e719 SDLLaunch: uc2.bin, the SDL2 game launcher; portable client in Default.ini
  - 34dd131 SDLLaunch: register UC2GLDrv, -opengl switch; GL device is the default; docs: gl-renderer notebook
  - 45f7301 SDLLaunch: browser build (SDLLaunch.js/.wasm/.data + meta.json), per-frame Asyncify yield, ue_exec_command
  - 81aea5f Merge branch 'm4w': the engine as WebAssembly (ucc-bin under Node, browser artifact builds)
  - 8db663c SDLLaunch, UCC: register UC2ALAudio; Default.ini selects it; run trees get libopenal and the cooked audio
  - 34d2e39 build: Theora and the UC2Movie package join the build; ucc-bin and uc2.bin register the player
  - 9df0ad4 Engine, SDLLaunch: start-up logos (UC2_Logos) full screen after engine init; a full-screen movie owns the input (UEngine::InputEvent hook)
  - 8ff02cd UC2GL4ESDrv: the GL4ES binding as a subclass of the UC2GLDrv device; launcher registration and -gl4es (M11, work in progress)
  - 1efa52e SDLLaunch: -maxfps=<n> caps a standalone game's frame rate (tests of frame-rate-dependent behaviour on the null renderer); tools: d6 pad curve
  - 941326a WebRTCDrv: the browser's network driver, ported from engine-ut2004 and adapted to UC2 (E-2)
  - c46bb34 Engine: FRAMETRACE, a per-frame trace of the view and the pawn on the screen; tools: jit-trace
  - c59c02d Merge branch 'jitter': the mouse's rigid third-person camera is drawn from where the pawn is drawn (O-11)
  - d9226d1 SDLLaunch: frame yield and hidden tab as engine-ut2004 (F-4b)
  - 14d1b99 SDLLaunch, tools: -maxfps becomes [Engine.Engine] FrameRateCap, -framepattern becomes [SDLLaunch] FramePattern (F-4e)
  - c00a538 Core, Engine, SDL2Drv, UC2GLDrv, UC2GL4ESDrv, tools: the remaining test switches become one-shot ini test keys set with -ini:; the on/off twin
  - 9eb4427 SDLLaunch, UCC: command lines up to 4096 characters (Core's GCmdLine), a token that does not fit is dropped whole and said so
  - 82b66dc SDLLaunch: [SDLLaunch] FramePattern is a one-shot test key like the others (never kept in the ini)
- `Source/SDLLaunch/Src/SDLLaunchPrivate.h` (+2/-1)
  - 3d7e719 SDLLaunch: uc2.bin, the SDL2 game launcher; portable client in Default.ini

### UCElements (2 files)

- `Source/UCElements/Inc/UCElementsClasses.h` (+4/-4)
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
- `Source/UCElements/Src/UCElements.cpp` (+1/-1)
  - 482cfbb IpDrv, UWeb, UnrealGame, UCElements, UCGameplay, GUI: build natively and for the web

### UCC (1 files)

- `Source/UCC/Src/UCC.cpp` (+434/-64)
  - 9b303b8 UCC: ucc-bin links (M2); 4-byte struct packing on every target
  - 44f81c6 Runtime: objects construct correctly and native class layouts match the shipped .u
  - 8db663c SDLLaunch, UCC: register UC2ALAudio; Default.ini selects it; run trees get libopenal and the cooked audio
  - 34d2e39 build: Theora and the UC2Movie package join the build; ucc-bin and uc2.bin register the player
  - 1e7f700 UCC, SDLLaunch, Engine: ucc.exe and uc2.exe link with MinGW-w64
  - 776ed23 Merge branch 'm9-video': movie cook and the portable Theora/Vorbis player (M9)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)
  - 58cf932 build, UCC: -Werror=int-to-pointer-cast at 64-bit, sanitizer preset, ucc profiletest; docs: first census (M13)
  - 006e725 Engine, UCC: connection destroys (not deletes) its channels during GC, routed channel destroy marked as routed; commandlet rooted while it run
  - 0f44cbf Merge branch 'net-join': a client joins a server and plays (network join defects fixed, net test suite)
  - 064dd20 Merge branch 'm13-64bit'
  - 22e9dbf Engine: the web build keeps a profile as one flat file Profiles/<name>.PRO (the frontend's userFiles cannot name a per-profile directory); too
  - 93686c0 Editor: builds natively and ucc-bin make compiles all 17 script packages (M14 S1)
  - 434ac53 Editor, UCC: Editor layouts match (GCC bitfield tagging), verifyclasses checks them; Editor 32-bit Linux only (M14 S1)
  - e3e927d Core: script SaveConfig()/StaticSaveConfig() write the ini on PC; SAVECONFIGKEYS; a base-class save leaves out what a subclass sets
  - 6d0798a tools: ucc-bin saveconfigtest gameinfo (a live GameInfo instance is not saved, its class default is); sc-saveconfig.sh checks
  - 9eb4427 SDLLaunch, UCC: command lines up to 4096 characters (Core's GCmdLine), a token that does not fit is dropped whole and said so

### UCMenu (1 files)

- `Source/UCMenu/Classes/UC2Settings_Main.uc` (+20/-0)
  - 07d0d06 UCMenu: KEYBOARD / MOUSE settings page, first version (D-9)

### UWeb (1 files)

- `Source/UWeb/Inc/UWeb.h` (+6/-0)
  - f60b44e Core, Engine, GUI, UCGameplay, UWeb, IpDrv, UCC: 64-bit layout contract and the verifyclasses 64-bit mode (M13)

## Appendix B. New files of ours under Source/ (189), by module

| Module | Files | Lines | Examples |
|---|---|---|---|
| UC2GLDrv | 127 | 35333 | `CMakeLists.txt`, `Inc/UC2GLBinding.h`, `Inc/UC2GLD3DTypes.h`, `Inc/UC2GLDrv.h` |
| UC2ALAudio | 9 | 4593 | `CMakeLists.txt`, `Inc/UC2ALAudio.h`, `Src/UC2ALAudio.cpp`, `Src/UC2ALAudioPrivate.h` |
| Engine | 8 | 1936 | `CMakeLists.txt`, `Inc/UnMovie.h`, `Src/UC2Cheats.cpp`, `Src/UC2FpsLog.cpp` |
| SDL2Drv | 8 | 2968 | `CMakeLists.txt`, `Inc/SDL2Drv.h`, `Src/SDL2Client.cpp`, `Src/SDL2Drv.cpp` |
| Core | 6 | 2050 | `CMakeLists.txt`, `Inc/FFileManagerUnix.h`, `Inc/UnMovieBase.h`, `Inc/UnThreadingPosix.h` |
| UC2Movie | 6 | 1443 | `CMakeLists.txt`, `Inc/UC2Movie.h`, `Src/UC2Movie.cpp`, `Src/UC2MoviePlayer.cpp` |
| SDLLaunch | 5 | 389 | `CMakeLists.txt`, `Src/SDLLaunchWeb.cpp`, `Src/SDLLaunchWeb.h`, `cmake/Web.cmake` |
| WebRTCDrv | 5 | 587 | `CMakeLists.txt`, `Inc/WebRTCDrv.h`, `Src/WebRTCConnection.cpp`, `Src/WebRTCDrv.cpp` |
| Editor | 3 | 771 | `CMakeLists.txt`, `Src/UC2EditorRegistrants.cpp`, `Src/UC2EpicClassOrder.h` |
| UC2GL4ESDrv | 3 | 381 | `CMakeLists.txt`, `Inc/UC2GL4ESDrv.h`, `Src/UC2GL4ESDrv.cpp` |
| UCMenu | 2 | 1559 | `Classes/KeyBindingPanel.uc`, `Classes/UC2Settings_Keys.uc` |
| GUI | 1 | 14 | `CMakeLists.txt` |
| IpDrv | 1 | 14 | `CMakeLists.txt` |
| UCC | 1 | 83 | `CMakeLists.txt` |
| UCElements | 1 | 14 | `CMakeLists.txt` |
| UCGameplay | 1 | 14 | `CMakeLists.txt` |
| UWeb | 1 | 14 | `CMakeLists.txt` |
| UnrealGame | 1 | 14 | `CMakeLists.txt` |

## Appendix C. Every ini key that differs from Epic's

Generated by comparing our `Engine/Config/*.ini` (snapshot 2ed330f) with `epic-sources/System/*.ini`, key by key. Multi-valued keys (Paths, EditPackages, ServerPackages…) show only the values added (+) or removed (−). The comment column is the comment we wrote above the key, when there is one.

### Default.ini (native; the System ini, "Avaris.ini" at run time)

118 keys differ.

| Section | Key | Epic | Ours | Our comment in the ini |
|---|---|---|---|---|
| URL | Port | 1000 | 7777 |  |
| Engine.Engine | RenderDevice | D3DDrv.D3DRenderDevice | UC2GLDrv.UC2GLRenderDevice |  |
| Engine.Engine | AudioDevice | Engine.NullAudioDevice | UC2ALAudio.UC2ALAudioSubsystem |  |
| Engine.Engine | FrameRateCap | (absent) | 0 |  |
| Engine.Engine | ViewportManager | WinDrv.WindowsClient | SDL2Drv.SDL2Client |  |
| Engine.Engine | ScriptSaveConfig | (absent) | True |  |
| Engine.GameEngine | FpsLogSeconds | (absent) | 0 |  |
| UC2ALAudio.UC2ALAudioSubsystem (new section) | OutputDevice | (absent) |  |  |
| UC2ALAudio.UC2ALAudioSubsystem (new section) | WaveFile | (absent) |  |  |
| UC2ALAudio.UC2ALAudioSubsystem (new section) | MaxVoices | (absent) | 64 |  |
| UC2ALAudio.UC2ALAudioSubsystem (new section) | ZoneReverb | (absent) | True |  |
| UC2ALAudio.UC2ALAudioSubsystem (new section) | UseEFX | (absent) | True |  |
| UC2ALAudio.UC2ALAudioSubsystem (new section) | MusicFades | (absent) | True |  |
| UC2ALAudio.UC2ALAudioSubsystem (new section) | CueLog | (absent) | True |  |
| UC2ALAudio.UC2ALAudioSubsystem (new section) | UseHRTF | (absent) | False |  |
| SDL2Drv.SDL2Client (new section) | UseJoystick | (WindowsClient: False) | True |  |
| SDL2Drv.SDL2Client (new section) | MaxDistortion | (absent) | 35.0 |  |
| SDL2Drv.SDL2Client (new section) | UIScaleMode | (absent) | Fit43 | --- D-2 UI scaling (docs/porting/analysis/ui-scale.md): Fit43 draws the menus and the HUD (designed for 640x480) in a centred 4:3 box scaled to the window, the 3D world fills the window; Native640 = as before D-2 (the UI 1:1 from the top-left). UIMenuBackground: a menu page's full-screen background  |
| SDL2Drv.SDL2Client (new section) | UIMenuBackground | (absent) | Fill |  |
| SDL2Drv.SDL2Client (new section) | UISharpText | (absent) | True |  |
| SDL2Drv.SDL2Client (new section) | PadButtonThreshold | (absent) | 30 | --- M10 controls: game pads through SDL's GameController API (docs/porting/analysis/controls.md) --- PadButtonThreshold 30 of 255 is the Xbox's XINPUT_GAMEPAD_MAX_CROSSTALK; the Xbox driver has no stick dead zone of its own (the bindings' DeadZone does it), hence PadStickDeadZone 0. |
| SDL2Drv.SDL2Client (new section) | PadStickDeadZone | (absent) | 0.000000 |  |
| SDL2Drv.SDL2Client (new section) | PadRepeatDelay | (absent) | 0.200000 |  |
| SDL2Drv.SDL2Client (new section) | PadRepeatRate | (absent) | 0.100000 |  |
| SDL2Drv.SDL2Client (new section) | PadRumble | (absent) | True |  |
| SDL2Drv.SDL2Client (new section) | PadRumbleScale | (absent) | 1.000000 |  |
| SDL2Drv.SDL2Client (new section) | JoyDeadZone | (absent) | 0.350000 |  |
| SDL2Drv.SDL2Client (new section) | LogInput | (absent) | False |  |
| SDL2Drv.PadMap (new section) | a | (absent) | Joy1 |  |
| SDL2Drv.PadMap (new section) | b | (absent) | Joy2 |  |
| SDL2Drv.PadMap (new section) | x | (absent) | Joy3 |  |
| SDL2Drv.PadMap (new section) | y | (absent) | Joy4 |  |
| SDL2Drv.PadMap (new section) | rightshoulder | (absent) | Joy5 |  |
| SDL2Drv.PadMap (new section) | leftshoulder | (absent) | Joy6 |  |
| SDL2Drv.PadMap (new section) | lefttrigger | (absent) | Joy7 |  |
| SDL2Drv.PadMap (new section) | righttrigger | (absent) | Joy8 |  |
| SDL2Drv.PadMap (new section) | dpup | (absent) | Joy9 |  |
| SDL2Drv.PadMap (new section) | dpdown | (absent) | Joy10 |  |
| SDL2Drv.PadMap (new section) | dpleft | (absent) | Joy11 |  |
| SDL2Drv.PadMap (new section) | dpright | (absent) | Joy12 |  |
| SDL2Drv.PadMap (new section) | start | (absent) | Joy13 |  |
| SDL2Drv.PadMap (new section) | back | (absent) | Joy14 |  |
| SDL2Drv.PadMap (new section) | leftstick | (absent) | Joy15 |  |
| SDL2Drv.PadMap (new section) | rightstick | (absent) | Joy16 |  |
| SDL2Drv.PadMap (new section) | leftx | (absent) | JoyX |  |
| SDL2Drv.PadMap (new section) | lefty | (absent) | JoyY Invert |  |
| SDL2Drv.PadMap (new section) | rightx | (absent) | JoyU |  |
| SDL2Drv.PadMap (new section) | righty | (absent) | JoyV Invert |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | UseMipmapping | (absent) | True |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | LevelOfAnisotropy | (D3DRenderDevice: 1) | 16 |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | AntiAliasing | (absent) | 4 |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | MaxTextureUnits | (absent) | 8 |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | MaterialStages | (absent) | 4 |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | FixedFunctionPrograms | (absent) | True |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | CheckGLErrors | (absent) | False |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | UsePixelShaders | (absent) | True |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | PrecompileVertexShaders | (absent) | True |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | UsePostProcess | (absent) | True |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | DOFShippedBlurAlpha | (absent) | True |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | DistortionCullDistance | (absent) | 15000 |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | UseXboxShadows | (absent) | True |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | UseXboxProjectors | (absent) | True |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | UseTerrain3Layer | (absent) | False |  |
| UC2GLDrv.UC2GLRenderDevice (new section) | PostProcessReferenceHeight | (absent) | 480 |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | GLESVersion | (absent) | 3 |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | UseMipmapping | (absent) | True |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | LevelOfAnisotropy | (D3DRenderDevice: 1) | 16 |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | AntiAliasing | (absent) | 4 |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | MaxTextureUnits | (absent) | 8 |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | MaterialStages | (absent) | 4 |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | FixedFunctionPrograms | (absent) | True |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | CheckGLErrors | (absent) | False |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | UsePixelShaders | (absent) | True |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | PrecompileVertexShaders | (absent) | True |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | UsePostProcess | (absent) | True |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | DOFShippedBlurAlpha | (absent) | True |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | DistortionCullDistance | (absent) | 15000 |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | UseXboxShadows | (absent) | True |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | UseXboxProjectors | (absent) | True |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | UseTerrain3Layer | (absent) | False |  |
| UC2GL4ESDrv.UC2GL4ESRenderDevice (new section) | PostProcessReferenceHeight | (absent) | 480 |  |
| Engine.Console | ConsoleKey | 9 | 192 |  |
| ufront.AutoFOV (new section) | AutoFOV | (absent) | 2 |  |
| ufront.AutoFOV (new section) | ReferenceAspect | (absent) | 1.333333 |  |
| ufront.AutoFOV (new section) | WeaponScale | (absent) | 1.0 |  |
| GUI.GUIController | bHideMouseCursor | True | False |  |
| GUI.EmulatedJoypad (new section) | Up | (absent) | PadUp |  |
| GUI.EmulatedJoypad (new section) | Down | (absent) | PadDown |  |
| GUI.EmulatedJoypad (new section) | Left | (absent) | PadLeft |  |
| GUI.EmulatedJoypad (new section) | Right | (absent) | PadRight |  |
| GUI.EmulatedJoypad (new section) | W | (absent) | PadUp/Type |  |
| GUI.EmulatedJoypad (new section) | S | (absent) | PadDown/Type |  |
| GUI.EmulatedJoypad (new section) | A | (absent) | PadLeft/Type |  |
| GUI.EmulatedJoypad (new section) | D | (absent) | PadRight/Type |  |
| GUI.EmulatedJoypad (new section) | Enter | (absent) | A/Start |  |
| GUI.EmulatedJoypad (new section) | Space | (absent) | A/Type |  |
| GUI.EmulatedJoypad (new section) | Escape | (absent) | B |  |
| GUI.EmulatedJoypad (new section) | Backspace | (absent) | B/Type |  |
| GUI.EmulatedJoypad (new section) | RightMouse | (absent) | B |  |
| GUI.EmulatedJoypad (new section) | X | (absent) | X/Type |  |
| GUI.EmulatedJoypad (new section) | Y | (absent) | Y/Type |  |
| GUI.EmulatedJoypad (new section) | Q | (absent) | White/Type |  |
| GUI.EmulatedJoypad (new section) | E | (absent) | Black/Type |  |
| GUI.EmulatedJoypad (new section) | PageUp | (absent) | LeftTrigger |  |
| GUI.EmulatedJoypad (new section) | PageDown | (absent) | RightTrigger |  |
| GUI.EmulatedJoypad (new section) | LeftBracket | (absent) | LeftTrigger/Type |  |
| GUI.EmulatedJoypad (new section) | RightBracket | (absent) | RightTrigger/Type |  |
| GUI.EmulatedJoypad (new section) | Tab | (absent) | Start |  |
| GUI.EmulatedJoypad (new section) | F1 | (absent) | Back |  |
| GUI.EmulatedJoypad (new section) | Home | (absent) | LeftThumb/Type |  |
| GUI.EmulatedJoypad (new section) | End | (absent) | RightThumb/Type |  |
| GUI.EmulatedJoypad (new section) | Delete | (absent) | Raw/Type |  |
| GUI.EmulatedJoypad (new section) | NumPad8 | (absent) | AltUp |  |
| GUI.EmulatedJoypad (new section) | NumPad2 | (absent) | AltDown |  |
| GUI.EmulatedJoypad (new section) | NumPad4 | (absent) | AltLeft |  |
| GUI.EmulatedJoypad (new section) | NumPad6 | (absent) | AltRight |  |
| GUI.EmulatedJoypad (new section) | MouseWheelUp | (absent) | PadUp |  |
| GUI.EmulatedJoypad (new section) | MouseWheelDown | (absent) | PadDown |  |

### DefUser.ini (native; the user ini)

54 keys differ.

| Section | Key | Epic | Ours | Our comment in the ini |
|---|---|---|---|---|
| Engine.Input | RightMouse | AltFire | AltFire \| Shield 1 \| onrelease Shield 0 |  |
| Engine.Input | MiddleMouse | Zoom | Lock 1 \| onrelease Lock 0 |  |
| Engine.Input | Tab | Type | ShowScores |  |
| Engine.Input | Ctrl | Jump | SmartJump 1 |  |
| Engine.Input | PageDown | LookDown | Axis aMouseY Speed=-4.0 |  |
| Engine.Input | End | CenterView \| ACTOR ALIGN SNAPTOFLOOR ALIGN=1 | CenterView |  |
| Engine.Input | PageUp |  | Axis aMouseY Speed=+4.0 |  |
| Engine.Input | home | CenterView \| ACTOR ALIGN SNAPTOFLOOR ALIGN=0 | CenterView |  |
| Engine.Input | E |  | MeleeWeapon \| MeleeHeavyAttack \| MeleePounceAttack |  |
| Engine.Input | I | set d3drenderdevice reducemouselag false \| onrelease set d3drenderdevice reducem |  |  |
| Engine.Input | M | Look \| ACTOR SELECT MATCHINGSTATICMESH |  |  |
| Engine.Input | O | ShowDebug |  |  |
| Engine.Input | P | PlayersOnly |  |  |
| Engine.Input | Q | AllAmmo | MeleeFinishAttack \| NextWeapon |  |
| Engine.Input | U | actor update |  |  |
| Engine.Input | V | CallForHelp | WhiteButton \| VoiceMenuNext |  |
| Engine.Input | NumPad0 | set playercontroller bZeroRoll False \| set playercontroller rotation (pitch=0,ya |  |  |
| Engine.Input | NumPad1 | set playercontroller bZeroRoll False \| set playercontroller rotation (pitch=0,ya |  |  |
| Engine.Input | NumPad2 | set playercontroller bZeroRoll False \| set playercontroller rotation (pitch=0,ya |  |  |
| Engine.Input | NumPad3 | set playercontroller bZeroRoll False \| set playercontroller rotation (pitch=0,ya |  |  |
| Engine.Input | NumPad4 | set playercontroller bZeroRoll False \| set playercontroller rotation (pitch=1638 |  |  |
| Engine.Input | NumPad5 | set playercontroller bZeroRoll False \| set playercontroller rotation (pitch=-163 |  |  |
| Engine.Input | NumPad6 | set playercontroller bZeroRoll True \| set playercontroller rotation (pitch=0,yaw |  |  |
| Engine.Input | C | (absent) | BlackButton \| ToggleThirdCam | --- M10 controls: keyboard, mouse and pad defaults (docs/porting/analysis/controls.md, Engine/Config/README.md) --- |
| Engine.Input | Joy1 | (absent) | SmartJump 0 | The pad, as KeyBinds.ini Layout_0 (DEFAULT) applies it when a profile is loaded; here so a pad works before that. Joy1-16 are the Xbox buttons A B X Y Black White LeftTrigger RightTrigger DPadUp DPadDown DPadLeft DPadRight Start Back LeftThumb RightThumb (SDL2Drv [SDL2Drv.PadMap]). |
| Engine.Input | Joy2 | (absent) | MeleeWeapon \| MeleeHeavyAttack \| MeleePounceAttack |  |
| Engine.Input | Joy3 | (absent) | button bCombo \| ComboPressed |  |
| Engine.Input | Joy4 | (absent) | MeleeFinishAttack \| NextWeapon |  |
| Engine.Input | Joy5 | (absent) | BlackButton \| ToggleThirdCam |  |
| Engine.Input | Joy6 | (absent) | WhiteButton \| VoiceMenuNext |  |
| Engine.Input | Joy7 | (absent) | AltFire \| Shield 1 \| onrelease Shield 0 |  |
| Engine.Input | Joy8 | (absent) | Fire |  |
| Engine.Input | Joy9 | (absent) | Dpad_Up \| VoiceMenuUp |  |
| Engine.Input | Joy10 | (absent) | Dpad_Down \| VoiceMenuDown |  |
| Engine.Input | Joy11 | (absent) | Dpad_Left \| ServerSetTeamChat 1 |  |
| Engine.Input | Joy12 | (absent) | Dpad_Right \| ServerSetTeamChat 0 |  |
| Engine.Input | Joy13 | (absent) | ShowMenu |  |
| Engine.Input | Joy14 | (absent) | ShowScores |  |
| Engine.Input | Joy15 | (absent) | SmartJump 1 |  |
| Engine.Input | Joy16 | (absent) | Lock 1 \| onrelease Lock 0 |  |
| Engine.Input | JoyX | (absent) | Axis aStrafe SpeedBase=100.0 DeadZone=0.3 | The sticks as the Xbox binds them (retail DefUser.ini). The mouse (MouseX/MouseY) is bound as Epic bound it (Count bXAxis \| Axis aMouseX Speed=6.0), into the same variables as the right stick: with Epic's shipped UCGameplay.u it goes through the stick's curve as on Epic's PC build; the rebuilt UCGam |
| Engine.Input | JoyY | (absent) | Axis aForward SpeedBase=100.0 DeadZone=0.3 |  |
| Engine.Input | JoyU | (absent) | Axis aMouseX SpeedBase=100.0 DeadZone=0.3 \| Count bXAxis |  |
| Engine.Input | JoyV | (absent) | Axis aMouseY SpeedBase=100.0 DeadZone=0.3 \| Count bYAxis |  |
| Engine.Input | JoySlider1 | (absent) | ControllerAdded |  |
| Engine.Input | JoySlider2 | (absent) | ControllerRemoved |  |
| Engine.Input | MouseWheelDown | PrevSkill | NextWeapon |  |
| Engine.Input | MouseWheelUp | NextSkill | NextWeapon |  |
| UCGameplay.UCPlayerController | LockSearchTimer | 0.1 | 0.3 |  |
| UCGameplay.UCPlayerInput | MouseLookSensitivity | (absent) | 2.5 | D-6: the MOUSE, with the rebuilt UCGameplay.u only (Epic's shipped package ignores these keys and sends the mouse through the pad's curve below, as Epic's PC build did). Turn per mouse count at the default FOV is 0.022 degrees x MouseLookSensitivity (the Quake/Source scale: 2.5 gives 0.055 deg/count |
| UCGameplay.UCPlayerInput | bInvertMouseLook | (absent) | False |  |
| UCGameplay.UCPlayerInput | bLazyMouseCamera | (absent) | False | F-1: the third-person camera under the mouse: False = rigid (it sits behind the view at once, D-6), True = Epic's lazy camera (it lags behind and follows the turn, as under the pad and on the Xbox) |
| UCGameplay.UCPlayerInput | aTurn_FrictionRange | (Min=1.0,Max=0.75) | (Min=1.0,Max=0.95) |  |
| UCGameplay.UCPlayerInput | aLookUp_FrictionRange | (Min=1.0,Max=0.65) | (Min=1.0,Max=0.9) |  |

### KeyBinds.ini

0 keys differ.

| Section | Key | Epic | Ours | Our comment in the ini |
|---|---|---|---|---|

### web/Default.ini compared with OUR native Default.ini

18 keys differ.

| Section | Key | Epic | Ours | Our comment in the ini |
|---|---|---|---|---|
| Engine.Engine | RenderDevice | UC2GLDrv.UC2GLRenderDevice | UC2GL4ESDrv.UC2GL4ESRenderDevice |  |
| Engine.Engine | NetworkDevice | IpDrv.TcpNetDriver | WebRTCDrv.WebRTCNetDriver |  |
| UC2ALAudio.UC2ALAudioSubsystem | Backends | (absent) |  |  |
| UC2Movie (new section) | StartupMovies | (absent) | True |  |
| UC2Movie (new section) | Cinematics | (absent) | True |  |
| UC2Movie (new section) | LoadingMovie | (absent) | True |  |
| WebRTCDrv.WebRTCNetDriver (new section) | AllowDownloads | (absent) | True |  |
| WebRTCDrv.WebRTCNetDriver (new section) | ConnectionTimeout | (absent) | 60.0 |  |
| WebRTCDrv.WebRTCNetDriver (new section) | InitialConnectTimeout | (absent) | 300.0 |  |
| WebRTCDrv.WebRTCNetDriver (new section) | KeepAliveTime | (absent) | 0.2 |  |
| WebRTCDrv.WebRTCNetDriver (new section) | MaxClientRate | (absent) | 20000 |  |
| WebRTCDrv.WebRTCNetDriver (new section) | RelevantTimeout | (absent) | 5.0 |  |
| WebRTCDrv.WebRTCNetDriver (new section) | SpawnPrioritySeconds | (absent) | 1.0 |  |
| WebRTCDrv.WebRTCNetDriver (new section) | ServerTravelPause | (absent) | 4.0 |  |
| WebRTCDrv.WebRTCNetDriver (new section) | NetServerMaxTickRate | (absent) | 60 |  |
| WebRTCDrv.WebRTCNetDriver (new section) | LanServerMaxTickRate | (absent) | 60 |  |
| WebRTCDrv.WebRTCNetDriver (new section) | DownloadManagers | (absent) | Engine.ChannelDownload |  |
| SDL2Drv.SDL2Client | UseJoystick | True | False |  |

### web/DefUser.ini compared with OUR native DefUser.ini

0 keys differ.

| Section | Key | Epic | Ours | Our comment in the ini |
|---|---|---|---|---|

- `DefaultXbox.ini`: identical to Epic's (kept for reference, unused)
- `DefUserXbox.ini`: identical to Epic's (kept for reference, unused)

