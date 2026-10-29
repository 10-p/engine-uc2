# engine-uc2: Unreal Championship 2 portable engine

Unreal Championship 2: The Liandri Conflict (Unreal Engine 2 build 2226 "Avaris", package version 151), from Epic's
Xbox/Win32 source tree, for Linux, Windows (MinGW-w64), the Web (Emscripten) and Android. Source only: the game content
(maps, textures, sounds, movies and the shipped script packages) comes from your own copy of the game and is never
tracked. The repository holds no UnrealScript either, only our changes to the scripts as patch sets
([`patches/`](./patches/README.md)), which the build applies to scripts decompiled from your shipped `.u` files.

- Building: [`BUILD.md`](./BUILD.md)
- Cooking the game folder from Epic's tree or the Xbox disc: [`tools/README.md`](./tools/README.md)
- Configuration defaults and why they differ from Epic's: [`Engine/Config/README.md`](./Engine/Config/README.md)
- The script patch sets: [`patches/README.md`](./patches/README.md)
- How the Epic tree was imported: [`docs/SOURCE-LAYOUT.md`](./docs/SOURCE-LAYOUT.md)
- Agent guide: [`CLAUDE.md`](./CLAUDE.md)
- `docs/porting/` holds the port's notebooks: records kept as history, not the current state.

## Changes from the original source

- **WebAssembly build**: the engine as `SDLLaunch.{js,wasm,data}` plus `meta.json`, on a worker with WasmFS fed by
  the host and WebGL 2 (WebGL 1 as the fallback); package loads and a failed map fail soft; `ucc-bin.js` runs the
  commandlets under Node.
- **SDL2 client** (`SDL2Drv`) in place of WinDrv: windows, keyboard, mouse, text input and game pads.
- **OpenGL renderer** (`UC2GLDrv`): Epic's D3DDrv carried onto OpenGL 2 with framebuffer objects, the Xbox pixel
  shaders translated to GLSL, gamma in a present pass, multisampling.
- **GL4ES renderer** (`UC2GL4ESDrv`): the same driver on OpenGL ES 3 / WebGL 2 (ES 2 / WebGL 1 as the fallback)
  through gl4es; the web renderer, and the `-GL4ES` device on Linux.
- **Null renderer**: Epic's `NullRenderDevice`, chosen by the ini, by `-NULLRENDERER`, or when the ini names a device
  the build lacks.
- **WebRTC net driver** (`WebRTCDrv`, web only): engine packets over the frontend's data channels; the System Link
  page lists the broker's servers.
- **OpenAL Soft audio** (`UC2ALAudio`): the Xbox XACT device (cues, music, zone reverb, voice pool) on OpenAL Soft,
  from the cooked sound data or the shipped Xbox banks.
- **64-bit and ARM**: LP64 builds (x86_64 Linux and Windows, arm64 cross, Android arm64 and x86_64) on the shipped
  32-bit script layout, with the C++/script layout checked at load on every pointer width.
- **Android build**: a Gradle/NDK project (`Android/`) for Android 7.0 and later: the engine as `libUC2.so` under SDL's activity, rendering through
  GL4ES on OpenGL ES 3, sound on OpenSL ES, the game folder copied into internal storage from a folder chosen with the
  system's folder picker (or read in place from shared storage), a resolution scale up to supersampling, an exit prompt
  on Back, an on-screen pad that is a virtual SDL controller (the mouse is off), hidden while a real pad is used and
  back on a touch, and a suspended game in the background (no tick, the OpenAL device paused).
- **Windows build**: MinGW-w64 cross builds (i686 and x86_64), statically linked, no runtime DLL but OpenAL's; the game
  and the editor declare one DPI awareness for the whole process (per-monitor), so Windows never scales their windows.
- **UCC on every desktop build**: `ucc-bin` (Linux) and `ucc.exe` (Windows), 32- and 64-bit, beside the game: the
  dedicated server and the commandlets.
- **The editor** (`UnrealEd.exe`, i686): Epic's UnrealEd, Window and WinDrv built with MinGW, its viewports drawn by the
  OpenGL renderer through WGL (`UC2WGLDrv`) in place of D3DDrv, with viewport hit testing (click selection); it ships
  with what it needs beyond a game folder cooked from the retail disc (its button bar's bitmaps and splash, from
  Epic's drop).
- **GCC/Clang portability**: Epic's MSVC code builds with GCC, Clang (Emscripten) and MinGW unchanged in behaviour.
- **Keyboard and mouse**: PC bindings, menus driven by keyboard and mouse, the console on the key left of 1, the
  PC mouse look of the mouse-look patch set.
- **Split screen and four pads**: up to four players in one window, joining from the menus or a match; players three
  and four on the character page (players34).
- **Wide windows**: the 640x480 menus and HUD in the largest centred 4:3 box of the window or of a split view, the
  view Hor+; the HUD's left and right groups at the view's sides and its centred items on its centre, the console's
  typing line across the window, the announcement band a gradient under shadowed texts (wide-hud).
- **Custom maps and mutators**: maps dropped into the map paths appear in the game's lists; a mutator's package joins
  the server's package map.
- **Script patch sets**: the build decompiles the shipped `.u`, applies `patches/` and compiles the packages they
  change, proved equal to the shipped bytecode plus Epic's load-time patches.
- **Game folder from the Xbox disc** (`tools/cook.py`): the game folder the engine runs from, made from the game's
  Xbox disc (an image or its files) or Epic's tree: the maps, textures, meshes and animations as PC packages that
  play with the PC game folder's, the sound and music as Ogg Vorbis, the movies as Ogg Theora; it needs Python 3.8
  and ffmpeg and ships as a zip for players ([`tools/README.md`](./tools/README.md)).
- **Native script compiler**: `ucc make` (Linux i386 and x86_64, MinGW i686) compiles packages equal to the shipped
  ones, the same bytes at both pointer widths.
- **Theora movies** (`UC2Movie`): the XMV movies cooked to Ogg Theora/Vorbis and played full screen, on materials and
  as the loading screen.
- **Save games**: player profiles in Epic's layout on every platform, portable between 32- and 64-bit builds.
- **Languages**: every cooked language from `[Engine.Engine] Language` (menus, voices, movie tracks).
- **Xbox post-processing on PC**: distortion, emissive glow, bloom, depth of field, the camera effects and dynamic
  gamma, plus the Xbox shadows, projectors and three-layer terrain.
- **Xbox Live content**: the alternate skins and the downloadable maps in the game folder are available.
- **Portable platform layer**: Core's Unix layer (Linux, MinGW, Emscripten) with UC2's platform contract.
- **Player log**: status lines, warnings, errors and console-command answers; the port's diagnostic and repeated lines
  (AutoFOV, dynamic gamma, resize and framebuffer, GL capabilities and programs, movies, network statistics, audio
  timing, SaveConfig traces, UI scale) only with `-UFRONTLOG` ([`BUILD.md`](./BUILD.md), "The log").

The features of every engine of the frontend, with this one's, are listed in the frontend's change inventory
(`<repo>/../unreal-frontend-arch-private/docs/14-change-inventory.md`).

## Status

Linux x86 (32-bit, the reference) and x86_64 play: every map loads, menus, matches with bots, effects, sound,
movies, controls, split screen and network play. Windows builds run under wine here and have not been run on Windows
itself; the editor, under wine, opens a map, selects surfaces, actors and wireframe lines by clicking, and browses
textures and static meshes. The web build boots and plays in the frontend's browser specs (target `uc2`). The arm64 cross build builds and
its `ucc-bin` runs under qemu; it has not been run on arm64 hardware. The Android build (arm64) boots to the menus
and plays a match with bots on a Pixel 7 Pro (Android 17), from shared storage.

## Building

`cmake --preset native && cmake --build build-native -j8` (the game folder beside the repo, or
`-DUE_ASSETS_DIR=<dir>`). Every preset, the web artifact and the script build: [`BUILD.md`](./BUILD.md).

## Credits

- Epic Games: the Unreal Championship 2 "Avaris" source tree (Xbox/Win32) this engine is built from.
- This engine shares its platform layer, SDL2 client and WebRTC driver with engine-ut2004 (Epic's UT2004 source and
  its Linux port by Loki Software and Ryan C. Gordon), which in turn adapted fgsfdsfgs/UE1's SDL key map.
- ptitSeb: gl4es (fork 10-p/gl4es), on Ryan Hileman's glshim.
- OpenAL Soft (Chris Robinson, kcat; LGPL), SDL2, Xiph.Org (Ogg, Vorbis, Theora), zlib (Jean-loup Gailly, Mark
  Adler), Doug Lea's dlmalloc (inside Epic's code), Emscripten.
- The runesofwar.net setup guide: the PC ini defaults the shipped inis follow.
- Reference tools, not shipped: xemu, extract-xiso (XboxDev), the Xbox SDK.

## Trademarks

Unreal Engine, Unreal Championship 2: The Liandri Conflict and related trademarks or copyrights are owned by Epic
Games. This repository is not affiliated with or endorsed by Epic Games. Do not use for commercial purposes.
