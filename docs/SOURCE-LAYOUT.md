# Source layout: how the Epic tree was imported

The Epic tree (`<WorkUC2>/epic-sources`, 16,451 files, sha256 manifest in `<WorkUC2>/reference/epic-sources.sha256`)
is kept pristine and is never edited. The first commit of this repo was produced from it by
`tools/dev/import-epic-sources.py`. Nothing was edited in the import except line endings.

## What was imported

| Here | From | Rule |
|---|---|---|
| `Source/<Package>/` | the 42 code and build folders of the same name | text files only |
| `Source/Avaris.sln`, `defmacros.dsm`, `fixit`, `sync_LS.bat`, `BuildFileList.txt`, `ScriptFileList.txt` | tree root | byte for byte |
| `Thirdparty/nvTriStrip`, `nvDXT`, `DirectX8` | same names | sources and headers only (no `.lib`, `.tlb`) |
| `Thirdparty/zlib` | engine-ut2004's `Thirdparty/zlib` (1.1.4, has a CMakeLists) | replaces Epic's 1.1.3 |
| `Engine/Config/*.ini` | `System/Default.ini`, `DefUser.ini`, `DefaultXbox.ini`, `DefUserXbox.ini`, `KeyBinds.ini` | byte for byte at the import; the baseline our config changes diff against. LF since F-4d (2026-10-09; `Engine/Config/README.md`): compare with `git diff --ignore-cr-at-eol` |
| `docs/epic/` | `Help/ReadMe.txt`, `MacroLanguage.txt`, `XMV_Compression.txt`, `AVI2XMV.bat`, `Bundler/readme.txt` | the build notes worth reading |
| `vendor/glue/`, `cmake/TargetArch.cmake`, `cmake/toolchain-mingw-w64.cmake` | engine-ut2004 | byte for byte (`webrtc-shim.js` must stay identical across engine repos) |

Line endings: `.h .cpp .c .inl .inc .uc .huc .psh .vsh .pl .txt` and the makefiles were converted CRLF to LF.
`.vcproj .sln .bat .rc .def .idl .ini .inx .inf` and similar Windows-native files are byte for byte (seven
`Source/UIX` files are UTF-16). The sources are Latin-1 and some `.uc` files contain `0x02` bytes: use
`grep -a`, and never let an editor re-encode a file. `.gitattributes` turns off git's own conversion.

Totals: 3,448 files LF-normalised, 82 byte for byte, 570 binaries skipped.

## What was left in epic-sources (and why)

| Left out | Reason |
|---|---|
| Binary package import sources inside code folders (`.pcx .tga .dds .bmp .ase .wav`) | content, not code; needed only if `Engine.u`/`Editor.u` are rebuilt from scratch (M14 decides) |
| Windows resources (`.ico .cur`, 99 files) and `XboxLaunch` title image (`.xbx`) | only the unbuilt Win32/Xbox launchers use them |
| Prebuilt libraries (`.lib`, `ENCVAG`, `ifc23`, `ImpersonatorLib`, DirectX `.lib`/`.tlb`) | MSVC binaries, cannot link here |
| `metoolkit/`, `metoolkit_dummy/`, `KarmaData/` | Karma is compiled out of UC2 (package v140 "Removed Karma") |
| `Vorbis/` (libvorbis 1.0rc3), `OpenAL/` (1.0 headers), `SDL12/`, `MySQL/`, `IFC/` | unreferenced or superseded; ogg/vorbis arrive with the audio milestone, SDL2 and OpenAL Soft are fetched |
| `XboxLive/` data (`.xlast .xcp .act`, bitmaps), `Downloader/`, `Startup/`, `Fonts/`, `GUIRes/`, `HUD/`, `Tools/`, `Benchmark/`, `Help/` design documents | Xbox Live configuration, Xbox media, art sources and Word/Excel documents |
| `System/` | binaries, shipped `.u`, inis and localisation: that is the asset tree (`<WorkUC2>/assets/uc2/System`) |
| All content folders (`Maps`, `Textures`, `StaticMeshes`, `Animations`, `Sounds`, `SoundData`, `Music`, `Movies`, `Xbox*`, `MapsOnHold`, `TestMaps`) | assets never enter this repo |

## What is built and what is reference

Everything under `Source/` is kept, but only the packages named in `UC2_PACKAGES` in the root `CMakeLists.txt`
are compiled, and only once their own `CMakeLists.txt` lands. Reference-only for the whole project:
`D3DDrv` (the spec for the GL driver), `XboxDrv`, `XboxAudio`, `XboxLive`, `XboxLaunch`, `WinDrv`, `Window`,
`Launch`, `UnrealEd`, `UIX`, `SDLDrv`/`SDLLaunch` (SDL 1.2, replaced by SDL2 drivers), `Setup`, `Bundler`,
`XboxAssetConversion`, `GUIDesigner`, `DebuggerLaunch`, `FNameDebug*`, `UccDepend`, `BuildProcess`, `!Build*`.
Script-only packages (`Gameplay`, `UCInventory`, `UCEffects`, `UCCharacters`, `UCMutators`, `UCAnnouncements`,
`UCMenu`) are compiled by `ucc make` from M14 on; until then the shipped `.u` files are loaded.
