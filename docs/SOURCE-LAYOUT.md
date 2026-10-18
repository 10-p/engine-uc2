# Source layout: how the Epic tree was imported

The baseline is Epic's drop in the work folder, `<repo>/../epic-sources/uc2-avaris-2226` (16,451 files; every file is
listed in `epic-sources/uc2-avaris-2226.sha256`, and `epic-sources/README.md` maps the drops to the engines). It is
kept pristine and never edited. The import commit `9fc7bcf` was produced from it by `tools/dev/import-epic-sources.py`;
nothing was edited in the import except line endings. A change is ours when the repo differs from that commit.

The script sources are not in the repo: the packages' `Classes/` folders (`.uc`, `.huc`, `.upkg`, `STY_*.txt`) and
Epic's `.ucproject` files are game content and stay in the drop (`<Pkg>/Classes`); the importer skips them, and our
script changes are the sets of `patches/`.

## What was imported

| Here | From | Rule |
|---|---|---|
| `Source/<Package>/` | the 42 code and build folders of the same name | text files only; no `Classes/`, no `.ucproject` |
| `Source/Avaris.sln`, `defmacros.dsm`, `fixit`, `sync_LS.bat`, `BuildFileList.txt`, `ScriptFileList.txt` | tree root | byte for byte |
| `Thirdparty/nvTriStrip`, `nvDXT`, `DirectX8` | same names | sources and headers only (no `.lib`, `.tlb`) |
| `Engine/Config/*.ini` | `System/Default.ini`, `DefUser.ini`, `DefaultXbox.ini`, `DefUserXbox.ini`, `KeyBinds.ini` | the baseline our config changes diff against. `Default.ini` and `DefUser.ini` are LF (compare them with `git diff --ignore-cr-at-eol`); `DefaultXbox.ini`, `DefUserXbox.ini`, `KeyBinds.ini` are Epic's bytes (CRLF) |
| `docs/epic/` | `Help/ReadMe.txt`, `MacroLanguage.txt`, `XMV_Compression.txt`, `AVI2XMV.bat`, `Bundler/readme.txt` | the build notes worth reading |

Line endings: `.h .cpp .c .inl .inc .uc .huc .psh .vsh .pl .txt` and the makefiles were converted CRLF to LF.
`.vcproj .sln .bat .rc .def .idl .ini .inx .inf` and similar Windows-native files are byte for byte (seven `Source/UIX`
files are UTF-16). The sources are Latin-1 and some script text contains `0x02` bytes: use `grep -a`, and never let an
editor re-encode a file. `.gitattributes` turns off git's own conversion.

Not from Epic: `Thirdparty/zlib` (1.1.4 with a CMakeLists, in place of Epic's 1.1.3), `Thirdparty/Vorbis` (Ogg and
Vorbis) and `Thirdparty/Theora` (the decoder); `cmake/TargetArch.cmake` and `cmake/toolchain-mingw-w64.cmake`, shared
with engine-ut2004; `vendor/glue/`, the frontend's ABI files (`ue_abi.h`, `net-shim.js`, `wasmfs-shim.js`), identical in
every engine repo.

## What was left in the drop (and why)

| Left out | Reason |
|---|---|
| Binary package import sources inside code folders (`.pcx .tga .dds .bmp .ase .wav`) | content, not code; needed only to rebuild `Engine.u`/`Editor.u` from scratch |
| Windows resources (`.ico .cur`, 99 files) and `XboxLaunch`'s title image (`.xbx`) | only the unbuilt Win32/Xbox launchers use them |
| Prebuilt libraries (`.lib`, `ENCVAG`, `ifc23`, `ImpersonatorLib`, DirectX `.lib`/`.tlb`) | MSVC binaries, cannot link here |
| `metoolkit/`, `metoolkit_dummy/`, `KarmaData/` | Karma is compiled out of UC2 (package v140 "Removed Karma") |
| `Vorbis/` (libvorbis 1.0rc3), `OpenAL/` (1.0 headers), `SDL12/`, `MySQL/`, `IFC/` | unreferenced or superseded: Ogg/Vorbis are vendored in current versions, SDL2 and OpenAL Soft are fetched |
| `XboxLive/` data (`.xlast .xcp .act`, bitmaps), `Downloader/`, `Startup/`, `Fonts/`, `GUIRes/`, `HUD/`, `Tools/`, `Benchmark/`, `Help/` design documents | Xbox Live configuration, Xbox media, art sources and Word/Excel documents |
| `System/` | binaries, shipped `.u`, inis and localisation: the game folder's |
| All content folders (`Maps`, `Textures`, `StaticMeshes`, `Animations`, `Sounds`, `SoundData`, `Music`, `Movies`, `Xbox*`, `MapsOnHold`, `TestMaps`) | assets never enter this repo |

## What is built and what is reference

Everything under `Source/` is kept; the packages named in `UC2_PACKAGES` in the root `CMakeLists.txt` are compiled
(Epic's Core, Engine, Editor, IpDrv, UWeb, GUI, UnrealGame, UCElements, UCGameplay and our UCC, SDL2Drv, SDLLaunch,
UC2GLDrv, UC2GL4ESDrv, UC2ALAudio, UC2Movie, WebRTCDrv). Reference only: `D3DDrv` (the spec for the GL driver),
`XboxDrv`, `XboxAudio`, `XboxLive`, `XboxLaunch`, `WinDrv`, `Window`, `Launch`, `UnrealEd`, `UIX`, `SDLDrv` (SDL 1.2),
`Setup`, `Bundler`, `XboxAssetConversion`, `GUIDesigner`, `DebuggerLaunch`, `FNameDebug*`, `UccDepend`,
`BuildProcess`, `!Build*`. Script-only packages (`Gameplay`, `UCInventory`, `UCEffects`, `UCCharacters`, `UCMutators`,
`UCAnnouncements`, `UCMenu`) keep only Epic's `.vcproj` here; the game loads the shipped `.u` files, and the build
compiles the packages the patch sets change from the shipped ones decompiled (`patches/README.md`).
